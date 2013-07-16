/*
 *  Virtual Machine using Breakpoint Tracing
 *  Copyright (C) 2012 Bi Wu
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "guest/include/mm.h"
#include "vm/include/bt.h"
#include "host/include/mm.h"
#include "vm/include/see.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "guest/include/seed.h"
#include "vm/include/world.h"

#define SEE_DEBUG(args...)
// V_ERR(args)

unsigned int g_seed_fault = 0;

struct g_seed_table {
    unsigned char byte;
    unsigned int valid;
    void (*action1) (struct v_world *, unsigned char);
    void (*action2) (struct v_world *, unsigned char);
};

#if 0
static char *reg_names[] =
    { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
static char *imm1_names[] =
    { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
static char *sft2_names[] =
    { "rol", "ror", "rcl", "rcr", "shl", "shr", "", "sar" };
static char *jcc_names[] =
    { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l",
    "ge", "le", "g"
};
#endif

static struct v_instruction *vi_current;
struct g_seed_ws *pws_current;
static unsigned int cur_mem;
static unsigned int cur_reg;
static unsigned int cur_mem_disp;

static unsigned int sb_target = 0;
static int si_type = 0;
static unsigned char g_seed_nextbyte(struct v_world *world);
static void g_seed_modrm(struct v_world *w, unsigned char byte);

#define G_SEED_ASSIGNMENT 1
#define G_SEED_CONTROL 2
#define G_SEED_RET 3
#define G_SEED_FC 4
#define G_SEED_CONTRL_COND_ALWAYS 0xff

#define G_SEED_ASSIGNMENT_MOV 1
#define G_SEED_ASSIGNMENT_ADD 2
#define G_SEED_ASSIGNMENT_OR 3
#define G_SEED_ASSIGNMENT_ADC 4
#define G_SEED_ASSIGNMENT_SBB 5
#define G_SEED_ASSIGNMENT_AND 6
#define G_SEED_ASSIGNMENT_SUB 7
#define G_SEED_ASSIGNMENT_XOR 8
#define G_SEED_ASSIGNMENT_CMP 9

#define G_SEED_ASSIGNMENT_ROL 19
#define G_SEED_ASSIGNMENT_ROR 20
#define G_SEED_ASSIGNMENT_RCL 21
#define G_SEED_ASSIGNMENT_RCR 22
#define G_SEED_ASSIGNMENT_SHL 23
#define G_SEED_ASSIGNMENT_SHR 24
#define G_SEED_ASSIGNMENT_SAR 26

#define G_SEED_ASSIGNMENT_MUL 17
#define G_SEED_ASSIGNMENT_LEA 18

#define G_SEED_WS_EXP_MAX 64
#define G_SEED_WS_COND_MAX 32
#define G_SEED_WS_MEM_MAX 32
#define G_SEED_WS_REG_MAX 10
#define G_SEED_WS_BRANCH_MAX 5
#define G_SEED_WS_SET_MAX (1 << (G_SEED_WS_BRANCH_MAX + 1))
#define G_SEED_WS_INST_MAX 128

#define G_SEED_ASSIGNMENT_MASK_REG 0xf
#define G_SEED_ASSIGNMENT_MASK_SIZE 0xf0
#define G_SEED_ASSIGNMENT_MASK_MEMINDEX 0xff00
#define G_SEED_ASSIGNMENT_MASK_MEMINDEX_SCALE 0xff0000
#define G_SEED_ASSIGNMENT_MASK_MEMINDEX_SCALE_INDEX 0xff000000

#define G_SEED_ASSIGNMENT_MEM 0xf
#define G_SEED_ASSIGNMENT_CONST 0xe
#define G_SEED_ASSIGNMENT_ECX 0x1
#define G_SEED_ASSIGNMENT_ESP 0x4
#define G_SEED_ASSIGNMENT_EBP 0x5
#define G_SEED_ASSIGNMENT_SIZE_SHIFT 4
#define G_SEED_ASSIGNMENT_MEMINDEX_SHIFT 8
#define G_SEED_ASSIGNMENT_MEMINDEX_SCALE_SHIFT 16
#define G_SEED_ASSIGNMENT_MEMINDEX_SCALE_INDEX_SHIFT 24

#define X86_MODRM_MASK_MOD 0xc0
#define X86_MODRM_MASK_REG 0x38
#define X86_MODRM_MASK_RM  0x7
#define X86_SIB_MASK_S 0xc0
#define X86_SIB_MASK_I 0x38
#define X86_SIB_MASK_B 0x7

struct g_set_info {
    int cond[G_SEED_WS_BRANCH_MAX];
    int ip[G_SEED_WS_BRANCH_MAX];
    int c_cond;
    int parent;
    int final;
};

struct g_seed_ws {
    struct v_expression exp[G_SEED_WS_EXP_MAX];
    struct v_cond cond[G_SEED_WS_COND_MAX];
    struct v_mem mem[G_SEED_WS_SET_MAX][G_SEED_WS_MEM_MAX];
    struct v_reg reg[G_SEED_WS_SET_MAX][G_SEED_WS_REG_MAX];
    struct v_instruction inst[G_SEED_WS_INST_MAX];
    struct g_set_info set[G_SEED_WS_SET_MAX];
    int c_inst;
    int c_reg;
    int c_mem[G_SEED_WS_SET_MAX];
    int c_exp;
    int c_cond;
    int c_sym;
    int c_set;
    int too_complex;
    int param_write;            //this function has parameterized writes
    unsigned int c_cbr;         //count of conditional branches
    unsigned int *l_cbr;        // list of conditional branches
    void *pbm;                  //pointer to branch map
};

static struct v_instruction *
g_seed_ws_get_inst(void)
{
    struct v_instruction *ret = &pws_current->inst[pws_current->c_inst];
    pws_current->c_inst++;
    return ret;
}

static void
g_seed_call(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_UB | V_INST_FC;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        unsigned int joff = 0;
        int jmps = 0;
        joff += g_seed_nextbyte(w);
        joff += (g_seed_nextbyte(w) << 8);
        sb_target = w->gregs.sdisasm_ip + jmps;

    } else {
        unsigned int joff = 0;
        joff += g_seed_nextbyte(w);
        joff += (g_seed_nextbyte(w) << 8);
        joff += (g_seed_nextbyte(w) << 16);
        joff += (g_seed_nextbyte(w) << 24);
        sb_target = w->gregs.sdisasm_ip + *(int *) (&joff);
    }
    vi_current->type = G_SEED_FC;
    vi_current->inst.c.target_ip = sb_target;
    vi_current->inst.c.cond = G_SEED_CONTRL_COND_ALWAYS;
}

static void
g_seed_jmps(struct v_world *w, unsigned char byte)
{
    char imm;
    unsigned char *p = &imm;
    si_type = V_INST_UB;
    *p = g_seed_nextbyte(w);
    SEE_DEBUG("offset %d", imm);
    sb_target = w->gregs.sdisasm_ip + imm;

    vi_current->type = G_SEED_CONTROL;
    vi_current->inst.c.target_ip = sb_target;
    vi_current->inst.c.cond = G_SEED_CONTRL_COND_ALWAYS;
}

static void
g_seed_push(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    si_type = V_INST_I;
    SEE_DEBUG("push %s", reg_names[byte & 0xf]);
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_MEM + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT) +
        (G_SEED_ASSIGNMENT_ESP << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
    vi_current->inst.a.targetmem = -size;
    vi_current->inst.a.op1 =
        (byte & 0xf) + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;
    vi_current = g_seed_ws_get_inst();
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_ESP + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 = G_SEED_ASSIGNMENT_CONST;
    vi_current->inst.a.op1mem = size;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_SUB;

}

static void
g_seed_pop(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    si_type = V_INST_I;
    SEE_DEBUG("pop %s", reg_names[(byte & 0xf) - 0x8]);
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        (byte & 0xf) - 0x8 + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_MEM + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT) +
        (G_SEED_ASSIGNMENT_ESP << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
    vi_current->inst.a.op1mem = 0;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;
    vi_current = g_seed_ws_get_inst();
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_ESP + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 = G_SEED_ASSIGNMENT_CONST;
    vi_current->inst.a.op1mem = size;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_ADD;
}

static void
g_seed_inc(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    si_type = V_INST_I;
    SEE_DEBUG("inc %s", reg_names[byte & 0xf]);
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        (byte & 0xf) + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 1;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_ADD;
}

static void
g_seed_dec(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    si_type = V_INST_I;
    SEE_DEBUG("dec %s", reg_names[(byte & 0xf) - 0x8]);
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        (byte & 0xf) - 0x8 + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 1;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_SUB;
}

static void
g_seed_mov(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("mov");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;
}

static void
g_seed_movd(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("movd");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;
}

static void
g_seed_movi(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    si_type = V_INST_I;
    SEE_DEBUG("movi %s", reg_names[((byte & 0xf) - 0x8)]);
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        ((byte & 0xf) - 0x8) + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
}

static void
g_seed_mov2(struct v_world *w, unsigned char byte)
{
    unsigned char modrm = g_seed_nextbyte(w);
    unsigned int op = (modrm & X86_MODRM_MASK_REG) >> 3;
    si_type = V_INST_I;
    SEE_DEBUG("mov");
    if (op != 0) {
        SEE_DEBUG("not recognized");
        si_type = V_INST_U;
    }
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;

    g_seed_modrm(w, modrm);
}

static void
g_seed_add(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("add");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_ADD;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_adc(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("adc");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_ADC;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_and(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("and");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_AND;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_xor(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("xor");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_XOR;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_or(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("or");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_OR;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_sbb(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("sbb");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_SBB;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_sub(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("sub");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_SUB;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_cmp(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("cmp");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_CMP;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_lea(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("lea");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_LEA;
}

static void
g_seed_movz(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_I;
    SEE_DEBUG("movz");
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;
}

static void
g_seed_leave(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    si_type = V_INST_I;
    SEE_DEBUG("leave");
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_ESP + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_EBP + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;

    vi_current = g_seed_ws_get_inst();
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_MEM + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT) +
        (G_SEED_ASSIGNMENT_ESP << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
    vi_current->inst.a.op1mem = 0;
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_EBP + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_MOV;

    vi_current = g_seed_ws_get_inst();
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_ESP + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 = G_SEED_ASSIGNMENT_CONST;
    vi_current->inst.a.op1mem = size;
    vi_current->inst.a.op = G_SEED_ASSIGNMENT_ADD;
}

static void
g_seed_ret(struct v_world *w, unsigned char byte)
{
    si_type = V_INST_PB | V_INST_FR;
    SEE_DEBUG("ret");
    vi_current->type = G_SEED_RET;
}

static void
g_seed_modrm(struct v_world *w, unsigned char byte)
{
    unsigned int mod = (byte & X86_MODRM_MASK_MOD) >> 6;
    unsigned int reg = (byte & X86_MODRM_MASK_REG) >> 3;
    unsigned int rm = byte & X86_MODRM_MASK_RM;
    int disp;
    short int temp;
    char temp2;
    unsigned char *pt = (unsigned char *) &temp;
    unsigned char *pt2 = (unsigned char *) &disp;
    unsigned char *pt3 = (unsigned char *) &temp2;
    SEE_DEBUG("mod %x, reg %x, rm %x", mod, reg, rm);
    cur_reg = reg;
    cur_mem_disp = 0;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        if (mod == 0x1) {
            *pt3 = g_seed_nextbyte(w);
            disp = temp2;
            SEE_DEBUG("disp %d", disp);
            cur_mem_disp = disp;
        } else if ((mod == 0x2) || (mod == 0 && rm == 6)) {
            *pt = g_seed_nextbyte(w);
            *(pt + 1) = g_seed_nextbyte(w);
            disp = temp;
            SEE_DEBUG("disp %d", disp);
            cur_mem_disp = disp;
        }
        /*bit 0-7, reg or mem; bit 8-15 memindex (0xff if nothing) */
        if (mod != 3) {
            cur_mem = G_SEED_ASSIGNMENT_MEM;
            if (mod == 0 && rm == 6)
                cur_mem +=
                    (G_SEED_ASSIGNMENT_CONST <<
                    G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
            else
                cur_mem += (rm << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
        } else {
            cur_mem = rm;
        }

    } else {
        unsigned char sib = 0;
        if (rm == 4 && mod != 3) {
            sib = g_seed_nextbyte(w);
            SEE_DEBUG("sib %x", sib);
        }
        if (mod != 3) {
            cur_mem = G_SEED_ASSIGNMENT_MEM;
            if (mod == 0 && rm == 5) {
                cur_mem +=
                    (G_SEED_ASSIGNMENT_CONST <<
                    G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
            } else if (rm == 4) {
                unsigned int ss = (sib & X86_SIB_MASK_S) >> 6;
                unsigned int index = (sib & X86_SIB_MASK_I) >> 3;
                unsigned int base = (sib & X86_SIB_MASK_B);
                cur_mem += (base << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
                if (index != 4) {
                    cur_mem +=
                        ((1 << ss) << G_SEED_ASSIGNMENT_MEMINDEX_SCALE_SHIFT);
                }
                cur_mem += (index << G_SEED_ASSIGNMENT_MEMINDEX_SCALE_INDEX_SHIFT);     /*note index 4 */
            } else
                cur_mem += (rm << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
        } else {
            cur_mem = rm;
        }
        if ((mod == 0 && rm == 5) || (((sib & X86_SIB_MASK_B) == 5)
                && (mod == 0))) {
            *pt2 = g_seed_nextbyte(w);
            *(pt2 + 1) = g_seed_nextbyte(w);
            *(pt2 + 2) = g_seed_nextbyte(w);
            *(pt2 + 3) = g_seed_nextbyte(w);
            SEE_DEBUG("disp32 %d", disp);
            cur_mem_disp = disp;
        } else if (mod == 1) {
            *pt3 = g_seed_nextbyte(w);
            disp = temp2;
            SEE_DEBUG("disp %d", disp);
            cur_mem_disp = disp;
        } else if (mod == 2) {
            *pt2 = g_seed_nextbyte(w);
            *(pt2 + 1) = g_seed_nextbyte(w);
            *(pt2 + 2) = g_seed_nextbyte(w);
            *(pt2 + 3) = g_seed_nextbyte(w);
            SEE_DEBUG("disp32 %d", disp);
            cur_mem_disp = disp;
        }
    }
}

static void
g_seed_EbGb(struct v_world *w, unsigned char byte)
{
    g_seed_modrm(w, g_seed_nextbyte(w));
    vi_current->inst.a.target = cur_mem + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 = cur_reg + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
}

static void
g_seed_EvGv(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    g_seed_modrm(w, g_seed_nextbyte(w));
    vi_current->inst.a.target =
        cur_mem + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 = cur_reg + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
}

static void
g_seed_GbEb(struct v_world *w, unsigned char byte)
{
    g_seed_modrm(w, g_seed_nextbyte(w));
    vi_current->inst.a.op1 = cur_mem + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = cur_mem_disp;
    vi_current->inst.a.target = cur_reg + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
}

static void
g_seed_GvEv(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    g_seed_modrm(w, g_seed_nextbyte(w));
    vi_current->inst.a.op1 = cur_mem + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = cur_mem_disp;
    vi_current->inst.a.target =
        cur_reg + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
}

static void
g_seed_imm1(struct v_world *w, unsigned char byte)
{
    unsigned char modrm = g_seed_nextbyte(w);
    unsigned int op = (modrm & X86_MODRM_MASK_REG) >> 3;
    si_type = V_INST_I;
    SEE_DEBUG("imm group 1 op %x, %s", op, imm1_names[op]);
    g_seed_modrm(w, modrm);
    vi_current->inst.a.op = op + G_SEED_ASSIGNMENT_ADD;
    vi_current->inst.a.update_flag = 1;
}

static void
g_seed_sft2(struct v_world *w, unsigned char byte)
{
    unsigned char modrm = g_seed_nextbyte(w);
    unsigned int op = (modrm & X86_MODRM_MASK_REG) >> 3;
    si_type = V_INST_I;
    SEE_DEBUG("shift group 2 op %x, %s", op, sft2_names[op]);
    g_seed_modrm(w, modrm);
    vi_current->inst.a.op = op + G_SEED_ASSIGNMENT_ROL;
}

static void
g_seed_EbIb(struct v_world *w, unsigned char byte)
{
    char imm;
    unsigned char *p = &imm;
    *p = g_seed_nextbyte(w);
    SEE_DEBUG("imm %d", imm);
    vi_current->inst.a.target = cur_mem + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = imm;
}

static void
g_seed_Eb1(struct v_world *w, unsigned char byte)
{
    SEE_DEBUG("const 1");
    vi_current->inst.a.target = cur_mem + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 1;
}

static void
g_seed_EbCL(struct v_world *w, unsigned char byte)
{
    SEE_DEBUG("CL");
    vi_current->inst.a.target = cur_mem + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_ECX + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
}

static void
g_seed_EvIz(struct v_world *w, unsigned char byte)
{
    short imm;
    int imm2;
    unsigned char *p = (unsigned char *) &imm;
    unsigned char *p2 = (unsigned char *) &imm2;
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        *p = g_seed_nextbyte(w);
        *(p + 1) = g_seed_nextbyte(w);
        size = 2;
        imm2 = imm;
    } else {
        *p2 = g_seed_nextbyte(w);
        *(p2 + 1) = g_seed_nextbyte(w);
        *(p2 + 2) = g_seed_nextbyte(w);
        *(p2 + 3) = g_seed_nextbyte(w);
    }
    SEE_DEBUG("imm %d", imm2);
    vi_current->inst.a.target =
        cur_mem + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = imm2;
}

static void
g_seed_Ev1(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    SEE_DEBUG("const 1");
    vi_current->inst.a.target =
        cur_mem + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 1;
}

static void
g_seed_EvCL(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    SEE_DEBUG("CL");
    vi_current->inst.a.target =
        cur_mem + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_ECX + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
}

static void
g_seed_EvIb(struct v_world *w, unsigned char byte)
{
    char imm;
    unsigned char *p = &imm;
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    *p = g_seed_nextbyte(w);
    SEE_DEBUG("imm %d", imm);
    vi_current->inst.a.target =
        cur_mem + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = cur_mem_disp;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = imm;
}

static void
g_seed_ALIb(struct v_world *w, unsigned char byte)
{
    char imm;
    unsigned char *p = &imm;
    *p = g_seed_nextbyte(w);
    SEE_DEBUG("imm %d", imm);
    vi_current->inst.a.target =
        0 /*eax */  + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = imm;
}

static void
g_seed_rAXIz(struct v_world *w, unsigned char byte)
{
    short imm;
    int imm2;
    unsigned char *p = (unsigned char *) &imm;
    unsigned char *p2 = (unsigned char *) &imm2;
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        *p = g_seed_nextbyte(w);
        *(p + 1) = g_seed_nextbyte(w);
        size = 2;
        imm2 = imm;
    } else {
        *p2 = g_seed_nextbyte(w);
        *(p2 + 1) = g_seed_nextbyte(w);
        *(p2 + 2) = g_seed_nextbyte(w);
        *(p2 + 3) = g_seed_nextbyte(w);
    }
    SEE_DEBUG("imm %d", imm2);
    vi_current->inst.a.target =
        0 /*eax */  + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = imm2;
}

static void
g_seed_ALOb(struct v_world *w, unsigned char byte)
{
    char imm;
    unsigned char *p = &imm;
    *p = g_seed_nextbyte(w);
    SEE_DEBUG("abs %d", imm);
    vi_current->inst.a.target =
        0 /*eax */  + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_MEM + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT) +
        (G_SEED_ASSIGNMENT_CONST << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
    vi_current->inst.a.op1mem = imm;
}

static void
g_seed_rAXOv(struct v_world *w, unsigned char byte)
{
    short imm;
    int imm2;
    unsigned char *p = (unsigned char *) &imm;
    unsigned char *p2 = (unsigned char *) &imm2;
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        *p = g_seed_nextbyte(w);
        *(p + 1) = g_seed_nextbyte(w);
        size = 2;
        imm2 = imm;
    } else {
        *p2 = g_seed_nextbyte(w);
        *(p2 + 1) = g_seed_nextbyte(w);
        *(p2 + 2) = g_seed_nextbyte(w);
        *(p2 + 3) = g_seed_nextbyte(w);
    }
    SEE_DEBUG("abs %d", imm2);
    vi_current->inst.a.target =
        0 /*eax */  + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_MEM + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT) +
        (G_SEED_ASSIGNMENT_CONST << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
    vi_current->inst.a.op1mem = imm2;
}

static void
g_seed_GvM(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    g_seed_modrm(w, g_seed_nextbyte(w));
    vi_current->inst.a.op1 = cur_mem + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = cur_mem_disp;
    vi_current->inst.a.target =
        cur_reg + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
}

static void
g_seed_ObAL(struct v_world *w, unsigned char byte)
{
    char imm;
    unsigned char *p = &imm;
    *p = g_seed_nextbyte(w);
    SEE_DEBUG("abs %d", imm);
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_MEM + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT) +
        (G_SEED_ASSIGNMENT_CONST << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
    vi_current->inst.a.targetmem = imm;
    vi_current->inst.a.op1 = 0 /*eax */  + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
}

static void
g_seed_OvrAX(struct v_world *w, unsigned char byte)
{
    short imm;
    int imm2;
    unsigned char *p = (unsigned char *) &imm;
    unsigned char *p2 = (unsigned char *) &imm2;
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        *p = g_seed_nextbyte(w);
        *(p + 1) = g_seed_nextbyte(w);
        size = 2;
        imm2 = imm;
    } else {
        *p2 = g_seed_nextbyte(w);
        *(p2 + 1) = g_seed_nextbyte(w);
        *(p2 + 2) = g_seed_nextbyte(w);
        *(p2 + 3) = g_seed_nextbyte(w);
    }
    SEE_DEBUG("abs %d", imm2);
    vi_current->inst.a.target =
        G_SEED_ASSIGNMENT_MEM + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT) +
        (G_SEED_ASSIGNMENT_CONST << G_SEED_ASSIGNMENT_MEMINDEX_SHIFT);
    vi_current->inst.a.targetmem = imm2;
    vi_current->inst.a.op1 =
        0 /*eax */  + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = 0;
}

static void
g_seed_GvEb(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    g_seed_modrm(w, g_seed_nextbyte(w));
    vi_current->inst.a.op1 = cur_mem + (1 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = cur_mem_disp;
    vi_current->inst.a.target =
        cur_reg + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
}

static void
g_seed_GvEw(struct v_world *w, unsigned char byte)
{
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        size = 2;
    }
    g_seed_modrm(w, g_seed_nextbyte(w));
    vi_current->inst.a.op1 = cur_mem + (2 << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = cur_mem_disp;
    vi_current->inst.a.target =
        cur_reg + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.targetmem = 0;
}

static void
g_seed_Iv(struct v_world *w, unsigned char byte)
{
    short imm;
    int imm2;
    unsigned char *p = (unsigned char *) &imm;
    unsigned char *p2 = (unsigned char *) &imm2;
    unsigned int size = 4;
    if (g_get_current_ex_mode(w) == G_EX_MODE_16) {
        *p = g_seed_nextbyte(w);
        *(p + 1) = g_seed_nextbyte(w);
        size = 2;
        imm2 = imm;
    } else {
        *p2 = g_seed_nextbyte(w);
        *(p2 + 1) = g_seed_nextbyte(w);
        *(p2 + 2) = g_seed_nextbyte(w);
        *(p2 + 3) = g_seed_nextbyte(w);
    }
    SEE_DEBUG("imm %d", imm2);
    vi_current->inst.a.op1 =
        G_SEED_ASSIGNMENT_CONST + (size << G_SEED_ASSIGNMENT_SIZE_SHIFT);
    vi_current->inst.a.op1mem = imm2;
}

static void
g_seed_jcc(struct v_world *w, unsigned char byte)
{
    SEE_DEBUG("j%s", jcc_names[byte & 0xf]);
    si_type = V_INST_CB;
    {
        unsigned int joff = 0;
        int jmps = 0;
        joff += g_seed_nextbyte(w);
        sb_target = w->gregs.sdisasm_ip + jmps;
    }
    vi_current->type = G_SEED_CONTROL;
    vi_current->inst.c.target_ip = sb_target;
    vi_current->inst.c.cond = byte & 0xf;
}

static void g_seed_escape(struct v_world *w, unsigned char byte);

struct g_seed_table g_seed_table_slow1[] = {
    {0xe8, 1, g_seed_call},

    {0x50, 1, g_seed_push},
    {0x51, 1, g_seed_push},
    {0x52, 1, g_seed_push},
    {0x53, 1, g_seed_push},
    {0x54, 1, g_seed_push},
    {0x55, 1, g_seed_push},
    {0x56, 1, g_seed_push},
    {0x57, 1, g_seed_push},

    {0x58, 1, g_seed_pop},
    {0x59, 1, g_seed_pop},
    {0x5a, 1, g_seed_pop},
    {0x5b, 1, g_seed_pop},
    {0x5c, 1, g_seed_pop},
    {0x5d, 1, g_seed_pop},
    {0x5e, 1, g_seed_pop},
    {0x5f, 1, g_seed_pop},

    {0x88, 1, g_seed_mov, g_seed_EbGb},
    {0x89, 1, g_seed_mov, g_seed_EvGv},
    {0x8a, 1, g_seed_mov, g_seed_GbEb},
    {0x8b, 1, g_seed_mov, g_seed_GvEv},

    {0x80, 1, g_seed_imm1, g_seed_EbIb},
    {0x81, 1, g_seed_imm1, g_seed_EvIz},
    {0x82, 1, g_seed_imm1, g_seed_EbIb},
    {0x83, 1, g_seed_imm1, g_seed_EvIb},

    {0xc0, 1, g_seed_sft2, g_seed_EbIb},
    {0xc1, 1, g_seed_sft2, g_seed_EvIb},

    {0xd0, 1, g_seed_sft2, g_seed_Eb1},
    {0xd1, 1, g_seed_sft2, g_seed_Ev1},
    {0xd2, 1, g_seed_sft2, g_seed_EbCL},
    {0xd3, 1, g_seed_sft2, g_seed_EvCL},

    {0x00, 1, g_seed_add, g_seed_EbGb},
    {0x01, 1, g_seed_add, g_seed_EvGv},
    {0x02, 1, g_seed_add, g_seed_GbEb},
    {0x03, 1, g_seed_add, g_seed_GvEv},
    {0x04, 1, g_seed_add, g_seed_ALIb},
    {0x05, 1, g_seed_add, g_seed_rAXIz},

    {0x10, 1, g_seed_adc, g_seed_EbGb},
    {0x11, 1, g_seed_adc, g_seed_EvGv},
    {0x12, 1, g_seed_adc, g_seed_GbEb},
    {0x13, 1, g_seed_adc, g_seed_GvEv},
    {0x14, 1, g_seed_adc, g_seed_ALIb},
    {0x15, 1, g_seed_adc, g_seed_rAXIz},

    {0x20, 1, g_seed_and, g_seed_EbGb},
    {0x21, 1, g_seed_and, g_seed_EvGv},
    {0x22, 1, g_seed_and, g_seed_GbEb},
    {0x23, 1, g_seed_and, g_seed_GvEv},
    {0x24, 1, g_seed_and, g_seed_ALIb},
    {0x25, 1, g_seed_and, g_seed_rAXIz},

    {0x30, 1, g_seed_xor, g_seed_EbGb},
    {0x31, 1, g_seed_xor, g_seed_EvGv},
    {0x32, 1, g_seed_xor, g_seed_GbEb},
    {0x33, 1, g_seed_xor, g_seed_GvEv},
    {0x34, 1, g_seed_xor, g_seed_ALIb},
    {0x35, 1, g_seed_xor, g_seed_rAXIz},

    {0x08, 1, g_seed_or, g_seed_EbGb},
    {0x09, 1, g_seed_or, g_seed_EvGv},
    {0x0a, 1, g_seed_or, g_seed_GbEb},
    {0x0b, 1, g_seed_or, g_seed_GvEv},
    {0x0c, 1, g_seed_or, g_seed_ALIb},
    {0x0d, 1, g_seed_or, g_seed_rAXIz},

    {0x18, 1, g_seed_sbb, g_seed_EbGb},
    {0x19, 1, g_seed_sbb, g_seed_EvGv},
    {0x1a, 1, g_seed_sbb, g_seed_GbEb},
    {0x1b, 1, g_seed_sbb, g_seed_GvEv},
    {0x1c, 1, g_seed_sbb, g_seed_ALIb},
    {0x1d, 1, g_seed_sbb, g_seed_rAXIz},

    {0x28, 1, g_seed_sub, g_seed_EbGb},
    {0x29, 1, g_seed_sub, g_seed_EvGv},
    {0x2a, 1, g_seed_sub, g_seed_GbEb},
    {0x2b, 1, g_seed_sub, g_seed_GvEv},
    {0x2c, 1, g_seed_sub, g_seed_ALIb},
    {0x2d, 1, g_seed_sub, g_seed_rAXIz},

    {0x38, 1, g_seed_cmp, g_seed_EbGb},
    {0x39, 1, g_seed_cmp, g_seed_EvGv},
    {0x3a, 1, g_seed_cmp, g_seed_GbEb},
    {0x3b, 1, g_seed_cmp, g_seed_GvEv},
    {0x3c, 1, g_seed_cmp, g_seed_ALIb},
    {0x3d, 1, g_seed_cmp, g_seed_rAXIz},

    {0x8d, 1, g_seed_lea, g_seed_GvM},

    {0x0f, 1, g_seed_escape},

    {0x70, 1, g_seed_jcc},
    {0x71, 1, g_seed_jcc},
    {0x72, 1, g_seed_jcc},
    {0x73, 1, g_seed_jcc},
    {0x74, 1, g_seed_jcc},
    {0x75, 1, g_seed_jcc},
    {0x76, 1, g_seed_jcc},
    {0x77, 1, g_seed_jcc},
    {0x78, 1, g_seed_jcc},
    {0x79, 1, g_seed_jcc},
    {0x7a, 1, g_seed_jcc},
    {0x7b, 1, g_seed_jcc},
    {0x7c, 1, g_seed_jcc},
    {0x7d, 1, g_seed_jcc},
    {0x7e, 1, g_seed_jcc},
    {0x7f, 1, g_seed_jcc},

    {0xc9, 1, g_seed_leave},
    {0xc3, 1, g_seed_ret},
    {0xc6, 1, g_seed_mov2, g_seed_EbIb},
    {0xc7, 1, g_seed_mov2, g_seed_EvIz},

    {0xb8, 1, g_seed_movi, g_seed_Iv},
    {0xb9, 1, g_seed_movi, g_seed_Iv},
    {0xba, 1, g_seed_movi, g_seed_Iv},
    {0xbb, 1, g_seed_movi, g_seed_Iv},
    {0xbc, 1, g_seed_movi, g_seed_Iv},
    {0xbd, 1, g_seed_movi, g_seed_Iv},
    {0xbe, 1, g_seed_movi, g_seed_Iv},
    {0xbf, 1, g_seed_movi, g_seed_Iv},

    {0xeb, 1, g_seed_jmps},

    {0xa0, 1, g_seed_movd, g_seed_ALOb},
    {0xa1, 1, g_seed_movd, g_seed_rAXOv},
    {0xa2, 1, g_seed_movd, g_seed_ObAL},
    {0xa3, 1, g_seed_movd, g_seed_OvrAX},

    {0x40, 1, g_seed_inc},
    {0x41, 1, g_seed_inc},
    {0x42, 1, g_seed_inc},
    {0x43, 1, g_seed_inc},
    {0x44, 1, g_seed_inc},
    {0x45, 1, g_seed_inc},
    {0x46, 1, g_seed_inc},
    {0x47, 1, g_seed_inc},

    {0x48, 1, g_seed_dec},
    {0x49, 1, g_seed_dec},
    {0x4a, 1, g_seed_dec},
    {0x4b, 1, g_seed_dec},
    {0x4c, 1, g_seed_dec},
    {0x4d, 1, g_seed_dec},
    {0x4e, 1, g_seed_dec},
    {0x4f, 1, g_seed_dec},

};

struct g_seed_table g_seed_table_fast1[0xff];

struct g_seed_table g_seed_table_slow2[] = {
    {0xb6, 1, g_seed_movz, g_seed_GvEb},
    {0xb7, 1, g_seed_movz, g_seed_GvEw},
};

struct g_seed_table g_seed_table_fast2[0xff];

static void
g_seed_escape(struct v_world *w, unsigned char b_dummy)
{
    unsigned char byte;
    byte = g_seed_nextbyte(w);
    if (g_seed_table_fast2[byte].valid == 0) {
        SEE_DEBUG("invalid escape opcode 0f %x", byte);
        return;
    }
    if (g_seed_table_fast2[byte].action1) {
        g_seed_table_fast2[byte].action1(w, byte);
    }
    if (g_seed_table_fast2[byte].action2) {
        g_seed_table_fast2[byte].action2(w, byte);
    }
}

void
g_seed_set_ip(struct v_world *world, unsigned long ip)
{
    struct v_page *mpage;
    void *virt;
    mpage = h_p2mp(world, ip);
    g_seed_fault = 0;
    if (mpage == NULL) {
        g_seed_fault = 1;
        return;
    }
    world->gregs.sdisasm_ip = ip;
    virt = v_page_make_present(mpage);
    world->gregs.sdisasm_vip =
        (unsigned char *) ((unsigned int) (virt) + (ip & H_POFF_MASK));
}

unsigned long
g_seed_get_ip(struct v_world *world)
{
    return world->gregs.sdisasm_ip;
}

static unsigned char
g_seed_nextbyte(struct v_world *world)
{
    unsigned char ret = *world->gregs.sdisasm_vip;
    world->gregs.sdisasm_ip++;
    world->gregs.sdisasm_vip++;
    if ((world->gregs.sdisasm_ip & H_POFF_MASK) == 0x0) {
        g_seed_set_ip(world, world->gregs.sdisasm_ip);
    }
    return ret;
}

void
g_seed_init(void)
{
    int i;
    for (i = 0; i < 0xff; i++) {
        g_seed_table_fast1[i].valid = 0;
        g_seed_table_fast2[i].valid = 0;
    }
    for (i = 0;
        i < sizeof(g_seed_table_slow1) / sizeof(struct g_seed_table); i++) {
        h_memcpy(&g_seed_table_fast1[g_seed_table_slow1[i].byte],
            &g_seed_table_slow1[i], sizeof(struct g_seed_table));
    }
    for (i = 0;
        i < sizeof(g_seed_table_slow2) / sizeof(struct g_seed_table); i++) {
        h_memcpy(&g_seed_table_fast2[g_seed_table_slow2[i].byte],
            &g_seed_table_slow2[i], sizeof(struct g_seed_table));
    }
    V_ALERT("SEE table size: %x", sizeof(struct g_seed_ws));
}

void *
g_seed_initws(struct v_world *world)
{
    void *r = h_valloc(sizeof(struct g_seed_ws));
    struct g_seed_ws *p = r;
    p->c_inst = 0;
    p->c_reg = 0;
    p->c_exp = 0;
    p->c_cond = 0;
    p->c_set = 0;
    return r;
}

static int
g_seed_find_inst(struct g_seed_ws *ws, unsigned long ip)
{
    int i;
    for (i = 0; i < ws->c_inst; i++) {
        if (ws->inst[i].ip == ip) {
            return i;
        }
    }
    return -1;
}

int
g_seed_next(struct v_world *world, unsigned int *type, unsigned long *b_target,
    void *ws)
{
    unsigned char byte;
    unsigned long ip = g_seed_get_ip(world);
    int fip;
    struct g_seed_ws *pws = ws;
    byte = g_seed_nextbyte(world);
    g_seed_fault = 0;

    if (ws == NULL) {
        return 1;
    }

    fip = g_seed_find_inst(ws, ip);
    if (fip >= 0) {
        if (pws->c_inst == 0) {
            SEE_DEBUG
                ("impossible state: encountered known inst when there's no inst");
            return -1;
        }
        if (pws->inst[pws->c_inst - 1].type != G_SEED_CONTROL
            || pws->inst[pws->c_inst - 1].inst.c.cond !=
            G_SEED_CONTRL_COND_ALWAYS) {
            /*last inst didn't jump and we have encountered a familiar instruction
             * create artificial jumps */
            vi_current = g_seed_ws_get_inst();
            vi_current->type = G_SEED_CONTROL;
            vi_current->inst.c.cond = G_SEED_CONTRL_COND_ALWAYS;
            vi_current->inst.c.target_ip = pws->inst[fip].ip;
            return -1;
        }
    }

    pws_current = pws;

    vi_current = g_seed_ws_get_inst();
    vi_current->ip = ip;
    vi_current->type = G_SEED_ASSIGNMENT;
    vi_current->inst.a.update_flag = 0;

    if (g_seed_table_fast1[byte].valid == 0) {
        SEE_DEBUG("invalid opcode %x", byte);
        return 1;
    }
    if (g_seed_table_fast1[byte].action1) {
        g_seed_table_fast1[byte].action1(world, byte);
    }
    if (g_seed_table_fast1[byte].action2) {
        g_seed_table_fast1[byte].action2(world, byte);
    }

    *type = si_type;
    *b_target = sb_target;
    SEE_DEBUG("t %x tmem %ld, op1 %x op1mem %ld", vi_current->inst.a.target,
        vi_current->inst.a.targetmem, vi_current->inst.a.op1,
        vi_current->inst.a.op1mem);
    return g_seed_fault;
}

struct pbm_graph {
    unsigned char encountered;
    unsigned char T;
    unsigned char NT;
};

static void
copy_stats(struct g_seed_ws *ws, unsigned int src, unsigned int dst)
{
    int i;
    unsigned char *p = ws->pbm;
    for (i = 0; i < ws->c_cbr; i++) {
        p[dst * ws->c_cbr + i] |= p[src * ws->c_cbr + i];
    }
}

#define PBM_TAKEN 2
#define PBM_NOTAKEN 1

static void
mark_taken(struct g_seed_ws *ws, unsigned int ip, unsigned int i)
{
    ((unsigned char *) ws->pbm)[ip * ws->c_cbr + i] |= PBM_TAKEN;
}

static void
mark_notaken(struct g_seed_ws *ws, unsigned int ip, unsigned int i)
{
    ((unsigned char *) ws->pbm)[ip * ws->c_cbr + i] |= PBM_NOTAKEN;
}

static void
backup_stats(struct g_seed_ws *ws, unsigned char *target, unsigned int ip)
{
    int i;
    unsigned char *p = ws->pbm;
    for (i = 0; i < ws->c_cbr; i++) {
        target[ip * ws->c_cbr + i] = p[ip * ws->c_cbr + i];
    }
}

static int
cmp_stats(struct g_seed_ws *ws, unsigned char *target, unsigned int ip)
{
    int i;
    unsigned char *p = ws->pbm;
    for (i = 0; i < ws->c_cbr; i++) {
        if (target[ip * ws->c_cbr + i] != p[ip * ws->c_cbr + i]) {
            unsigned char a = target[ip * ws->c_cbr + i], b =
                p[ip * ws->c_cbr + i];
            if (a == (PBM_TAKEN | PBM_NOTAKEN)) {
                a = 0;
            }
            if (b == (PBM_TAKEN | PBM_NOTAKEN)) {
                b = 0;
            }
            if (a != b) {
                return 1;
            }
        }
    }
    return 0;
}

void
g_seed_do_br(struct v_world *world, void *pws)
{
    struct g_seed_ws *ws = pws;
    int bytes, i;
    unsigned char *oldpbm;
    unsigned int ip;
    struct pbm_graph *graph;
    ws->c_cbr = 0;
    for (i = 0; i < ws->c_inst; i++) {
        if (ws->inst[i].type == G_SEED_CONTROL) {
            int t = g_seed_find_inst(ws, ws->inst[i].inst.c.target_ip);
            if (t < 0) {
                SEE_DEBUG
                    ("impossible state: control instruction jump to nowhere");
                return;
            }
            ws->inst[i].inst.c.target = t;
            if (ws->inst[i].inst.c.cond != G_SEED_CONTRL_COND_ALWAYS) {
                ws->c_cbr++;
                ws->inst[i].inst.c.id = ws->c_cbr;
            }
        }
    }
    SEE_DEBUG("total %d conditional control instructions", ws->c_cbr);
    bytes = ws->c_cbr * ws->c_inst;     // 2 bits per branch, bit 1 T and bit 0 NT
    ws->pbm = h_valloc(bytes);
    oldpbm = h_valloc(bytes);
    ws->l_cbr = h_valloc(ws->c_cbr * sizeof(unsigned int));
    graph = h_valloc(ws->c_cbr * sizeof(struct pbm_graph));
    for (i = 0; i < bytes; i++) {
        ((unsigned char *) ws->pbm)[i] = 0;
        oldpbm[i] = 0;
    }
    for (i = 0; i < ws->c_inst; i++) {
        if (ws->inst[i].type == G_SEED_CONTROL) {
            if (ws->inst[i].inst.c.cond != G_SEED_CONTRL_COND_ALWAYS) {
                ws->l_cbr[ws->inst[i].inst.c.id - 1] = i;
                graph[ws->inst[i].inst.c.id - 1].encountered = 0;
                graph[ws->inst[i].inst.c.id - 1].T = 0;
                graph[ws->inst[i].inst.c.id - 1].NT = 0;
            }
        }
    }

    if (ws->c_cbr == 0) {
        return;
    }
    /*get to the first branch */
    i = 0;
    while (1) {
        if (ws->inst[i].type == G_SEED_CONTROL) {
            if (ws->inst[i].inst.c.cond != G_SEED_CONTRL_COND_ALWAYS) {
                graph[0].encountered = 1;
                break;
            }
        }
        i++;
    }
    ip = 0;
    while (1) {
        /*check: explore encountered branches */
        int flag = 0;
        for (i = 0; i < ws->c_cbr; i++) {
            if (graph[i].encountered && (!(graph[i].T && graph[i].NT))) {
                flag = 1;
                if (!graph[i].T) {
                    graph[i].T = 1;
                    ip = ws->inst[ws->l_cbr[i]].inst.c.target;
                    /*mark taken */
                    copy_stats(ws, ws->l_cbr[i], ip);
                    mark_taken(ws, ip, i);
                    SEE_DEBUG("taken %d", ip);
                } else {
                    graph[i].NT = 1;
                    ip = ws->l_cbr[i] + 1;
                    /*mark not taken */
                    copy_stats(ws, ws->l_cbr[i], ip);
                    mark_notaken(ws, ip, i);
                    SEE_DEBUG("notaken %d", ip);
                }
                break;
            }
        }
        if (flag) {
            while (1) {
                /*propagate stats from ip until branch */
                SEE_DEBUG("propagating starting %d", ip);
                if (ws->inst[ip].type == G_SEED_CONTROL) {
                    unsigned int oldip = ip;
                    if (ws->inst[ip].inst.c.cond != G_SEED_CONTRL_COND_ALWAYS) {
                        unsigned int id = ws->inst[ip].inst.c.id;
                        if (!graph[id - 1].encountered) {
                            /* this is the stats when the branch is encountered */
                            graph[id - 1].encountered = 1;
                            backup_stats(ws, oldpbm, ip);
                        }
                        /*either way, abort current propagation */
                        break;
                    }
                    ip = ws->inst[ip].inst.c.target;
                    copy_stats(ws, oldip, ip);
                } else if (ws->inst[ip].type == G_SEED_RET) {
                    /* hit return instruction */
                    break;
                } else {
                    copy_stats(ws, ip, ip + 1);
                    ip++;
                }
            }
        } else {
            /*did we really exhaust all branches and all paths? */
            /*since flag is 0, all T and NT is done */
            flag = 0;
            for (i = 0; i < ws->c_cbr; i++) {
                if (!graph[i].encountered) {
                    SEE_DEBUG
                        ("impossible state: unexplored branch after all paths explored");
                    return;
                }
                if (cmp_stats(ws, oldpbm, ws->l_cbr[i])) {
                    /* stats different, reset taken and not taken, do again */
                    backup_stats(ws, oldpbm, ws->l_cbr[i]);
                    graph[i].T = 0;
                    graph[i].NT = 0;
                    flag = 1;
                    break;
                }
            }
            if (flag == 0) {
                /* really exhausted everything */
                break;
            }
        }
    }
    for (i = 0; i < bytes; i++) {
        if (i % ws->c_cbr == 0)
            printk("\n--");
        printk("%d:", ((unsigned char *) ws->pbm)[i]);
    }
    h_vfree(graph);
    h_vfree(oldpbm);
}

static int
g_seed_getexp_expconst(struct g_seed_ws *ws, int exp, long c, int op)
{
    int i;
    for (i = 0; i < ws->c_exp; i++) {
        if (ws->exp[i].sym < 0 && ws->exp[i].op == op) {
            if (ws->exp[i].p_exp == exp && ws->exp[i].p_exp2 < 0
                && ws->exp[i].c2 == c) {
                return i;
            }
            if (ws->exp[i].p_exp2 == exp && ws->exp[i].p_exp < 0
                && ws->exp[i].c == c) {
                return i;
            }
        }
    }
    /*nothing found. create */
    ws->exp[i].sym = -1;
    ws->exp[i].p_exp = exp;
    ws->exp[i].p_exp2 = -1;
    ws->exp[i].c2 = c;
    ws->exp[i].op = op;
    ws->c_exp++;
    return i;
}

static int
g_seed_getexp_expexp(struct g_seed_ws *ws, int exp, int exp2, int op)
{
    int i;
    for (i = 0; i < ws->c_exp; i++) {
        if (ws->exp[i].sym < 0 && ws->exp[i].op == op) {
            if (ws->exp[i].p_exp == exp && ws->exp[i].p_exp2 == exp2) {
                return i;
            }
            if (ws->exp[i].p_exp2 == exp && ws->exp[i].p_exp == exp2) {
                return i;
            }
        }
    }
    /*nothing found. create */
    ws->exp[i].sym = -1;
    ws->exp[i].p_exp = exp;
    ws->exp[i].p_exp2 = exp2;
    ws->exp[i].op = op;
    ws->c_exp++;
    return i;
}

static int
g_seed_getexp_const(struct g_seed_ws *ws, long c)
{
    int i;
    for (i = 0; i < ws->c_exp; i++) {
        if (ws->exp[i].sym < 0 && ws->exp[i].op == 0) {
            if (ws->exp[i].p_exp < 0 && ws->exp[i].p_exp2 < 0
                && ws->exp[i].c2 == c) {
                return i;
            }
            if (ws->exp[i].p_exp2 < 0 && ws->exp[i].p_exp < 0
                && ws->exp[i].c == c) {
                return i;
            }
        }
    }
    /*nothing found. create */
    ws->exp[i].sym = -1;
    ws->exp[i].p_exp = -1;
    ws->exp[i].p_exp2 = -1;
    ws->exp[i].c2 = c;
    ws->exp[i].op = 0;
    ws->c_exp++;
    return i;
}

static int
g_seed_getmem(struct g_seed_ws *ws, int set, unsigned int op, long opmem)
{
    int i, j, k, a = -1, b;
    if ((op & G_SEED_ASSIGNMENT_MASK_REG) != G_SEED_ASSIGNMENT_MEM) {
        SEE_DEBUG("not mem");
        return -1;
    }
    i = (op & G_SEED_ASSIGNMENT_MASK_MEMINDEX) >>
        G_SEED_ASSIGNMENT_MEMINDEX_SHIFT;
    j = (op & G_SEED_ASSIGNMENT_MASK_MEMINDEX_SCALE_INDEX) >>
        G_SEED_ASSIGNMENT_MEMINDEX_SCALE_INDEX_SHIFT;
    k = (op & G_SEED_ASSIGNMENT_MASK_MEMINDEX_SCALE) >>
        G_SEED_ASSIGNMENT_MEMINDEX_SCALE_SHIFT;

    if (k != 0) {
        a = g_seed_getexp_expconst(ws, ws->reg[set][j].p_exp, k,
            G_SEED_ASSIGNMENT_MUL);
    }
    if (i == G_SEED_ASSIGNMENT_CONST) {
        b = g_seed_getexp_const(ws, opmem);
    } else {
        if (opmem != 0) {
            b = g_seed_getexp_expconst(ws, ws->reg[set][i].p_exp,
                opmem, G_SEED_ASSIGNMENT_ADD);
        } else {
            b = ws->reg[set][i].p_exp;
        }
    }
    if (a >= 0) {
        b = g_seed_getexp_expexp(ws, a, b, G_SEED_ASSIGNMENT_ADD);
    }
    for (i = 0; i < ws->c_mem[set]; i++) {
        if (ws->mem[set][i].idx_p_exp == b) {
            return i;
        }
    }
    ws->mem[set][i].idx_p_exp = b;
    ws->mem[set][i].val_p_exp = ws->c_exp;
    ws->exp[ws->c_exp].sym = ws->c_sym;
    ws->c_exp++;
    ws->mem[set][i].sym = ws->c_sym++;
    ws->mem[set][i].access = 0;
    ws->c_mem[set]++;
    return i;
}

static int
g_seed_make_exp(struct g_seed_ws *ws, int op1_exp, long op1_c, int op,
    int op2_exp, long op2_c)
{
    int i = ws->c_exp;
    if (op == 0 && op1_exp < 0) {
        int j = g_seed_getexp_const(ws, op1_c);
        if (j >= 0)
            return j;
    }
    if (op != 0 && ((op1_exp < 0 && op2_exp >= 0) || (op1_exp >= 0
                && op2_exp < 0))) {
        int exp = (op1_exp >= 0) ? op1_exp : op2_exp;
        long c = (op1_exp >= 0) ? op2_c : op1_c;
        int j;
        if (op == G_SEED_ASSIGNMENT_SUB) {
            op = G_SEED_ASSIGNMENT_ADD;
            c = -c;
            if (op1_exp >= 0) {
                op2_c = -op2_c;
            } else {
                op1_c = -op1_c;
            }
        }
        j = g_seed_getexp_expconst(ws, exp, c, op);
        if (j >= 0)
            return j;
    }
    if (op != 0 && (op1_exp >= 0 && op2_exp >= 0)) {
        int j = g_seed_getexp_expexp(ws, op1_exp, op2_exp, op);
        if (j >= 0)
            return j;
    }
    ws->exp[i].sym = -1;
    ws->exp[i].p_exp = op1_exp;
    ws->exp[i].p_exp2 = op2_exp;
    ws->exp[i].c = op1_c;
    ws->exp[i].c2 = op2_c;
    ws->exp[i].op = op;
    return ws->c_exp++;
}

static void
g_seed_debug_dump(struct g_seed_ws *ws)
{
    int i, j;
    SEE_DEBUG("param_write = %d", ws->param_write);
    for (i = 0; i < ws->c_exp; i++) {
        SEE_DEBUG("exp %d, sym %d, op1 %s %ld, OP %d, op2 %s %ld", i,
            ws->exp[i].sym,
            (ws->exp[i].p_exp >= 0) ? "exp" : "const",
            (ws->exp[i].p_exp >=
                0) ? ws->exp[i].p_exp : ws->exp[i].c, ws->exp[i].op,
            (ws->exp[i].p_exp2 >= 0) ? "exp" : "const",
            (ws->exp[i].p_exp2 >= 0) ? ws->exp[i].p_exp2 : ws->exp[i].c2);
    }
    for (j = 0; j < ws->c_set; j++) {
        for (i = 0; i < ws->c_reg; i++) {
            SEE_DEBUG("set %d reg %d, exp %d", j, i, ws->reg[j][i].p_exp);
        }
        for (i = 0; i < ws->c_mem[j]; i++) {
            SEE_DEBUG("set %d mem %d,  [%s] idx %d, exp %d, sym %d",
                j, i, ws->mem[j][i].access ? "w" : " ",
                ws->mem[j][i].idx_p_exp,
                ws->mem[j][i].val_p_exp, ws->mem[j][i].sym);
        }
    }
}

int
g_seed_param_write_check(struct g_seed_ws *ws, int exp)
{
    int i = 0;
    if (ws->exp[exp].sym >= 0) {
        if (ws->exp[exp].sym != G_SEED_ASSIGNMENT_ESP) {
            return 1;
        } else {
            return 0;
        }
    }
    if (ws->exp[exp].p_exp >= 0)
        i |= g_seed_param_write_check(ws, ws->exp[exp].p_exp);
    if (ws->exp[exp].op != 0 && ws->exp[exp].p_exp2 >= 0)
        i |= g_seed_param_write_check(ws, ws->exp[exp].p_exp2);
    return i;
}

#define G_SEED_REG_FLAG 8

int
g_seed_onepass(struct v_world *world, struct g_seed_ws *ws, int *ip,
    int *c_ret, int set)
{
    while (1) {
        if (ws->inst[*ip].type == G_SEED_CONTROL) {
            if (ws->inst[*ip].inst.c.cond == G_SEED_CONTRL_COND_ALWAYS) {
                *ip = ws->inst[*ip].inst.c.target;
                continue;
            } else {
                return 0;
            }
        } else if (ws->inst[*ip].type == G_SEED_RET) {
            (*c_ret)--;
            return 0;
        } else if (ws->inst[*ip].type == G_SEED_ASSIGNMENT) {
            unsigned int op1 =
                ws->inst[*ip].inst.a.op1 & G_SEED_ASSIGNMENT_MASK_REG;
            unsigned int opt =
                ws->inst[*ip].inst.a.target & G_SEED_ASSIGNMENT_MASK_REG;
            int op1_exp = -1, op1_c = 0;
            int opt_exp = -1, opt_c = 0;
            int opr_exp = -1;
            int op1_mem = -1, opt_mem = -1;
            SEE_DEBUG("executing assignment %d", *ip);
            if (op1 == G_SEED_ASSIGNMENT_MEM) {
                op1_mem =
                    g_seed_getmem(ws, set,
                    ws->inst[*ip].inst.a.op1, ws->inst[*ip].inst.a.op1mem);
                op1_exp = ws->mem[set][op1_mem].val_p_exp;
            } else if (op1 == G_SEED_ASSIGNMENT_CONST) {
                op1_exp = -1;
                op1_c = ws->inst[*ip].inst.a.op1mem;
            } else {
                op1_exp = ws->reg[set][op1].p_exp;
            }

            if (opt == G_SEED_ASSIGNMENT_MEM) {
                opt_mem =
                    g_seed_getmem(ws, set,
                    ws->inst[*ip].inst.a.target,
                    ws->inst[*ip].inst.a.targetmem);
                opt_exp = ws->mem[set][opt_mem].val_p_exp;
            } else if (opt == G_SEED_ASSIGNMENT_CONST) {
                opt_exp = -1;
                opt_c = ws->inst[*ip].inst.a.targetmem;
            } else {
                opt_exp = ws->reg[set][opt].p_exp;
            }

            if (ws->inst[*ip].inst.a.op == 0) {
                SEE_DEBUG("unknown zero assignment op");
                return 1;
            } else if (ws->inst[*ip].inst.a.op == G_SEED_ASSIGNMENT_LEA) {
                if (opt == G_SEED_ASSIGNMENT_MEM
                    || op1 != G_SEED_ASSIGNMENT_MEM) {
                    SEE_DEBUG
                        ("lea target cannot be mem, and lea source must be mem");
                }
                opr_exp = ws->mem[set][op1_mem].idx_p_exp;
            } else if (ws->inst[*ip].inst.a.op == G_SEED_ASSIGNMENT_MOV) {
                if (op1_exp < 0) {
                    opr_exp = g_seed_make_exp(ws, op1_exp, op1_c, 0, 0, 0);
                } else {
                    opr_exp = op1_exp;
                }
            } else {
                opr_exp =
                    g_seed_make_exp(ws, opt_exp, opt_c,
                    ws->inst[*ip].inst.a.op, op1_exp, op1_c);
            }

            if (ws->inst[*ip].inst.a.op == G_SEED_ASSIGNMENT_CMP) {
                ws->reg[set][G_SEED_REG_FLAG].p_exp = opr_exp;
            } else {
                if (opt == G_SEED_ASSIGNMENT_MEM) {
                    if (ws->param_write == 0
                        && g_seed_param_write_check(ws, ws->mem[set]
                            [opt_mem].idx_p_exp)) {
                        ws->param_write = 1;
                    }
                    ws->mem[set][opt_mem].val_p_exp = opr_exp;
                    ws->mem[set][opt_mem].access |= 1;
                } else if (opt == G_SEED_ASSIGNMENT_CONST) {
                    SEE_DEBUG("op target is a const");
                    return 1;
                } else {
                    ws->reg[set][opt].p_exp = opr_exp;
                }
                if (ws->inst[*ip].inst.a.update_flag) {
                    ws->reg[set][G_SEED_REG_FLAG].p_exp = opr_exp;
                }
            }
        }
        (*ip)++;
        g_seed_debug_dump(ws);
    }
    return 0;
}

int
g_seed_init_set(struct g_seed_ws *ws, int from)
{
    int to = ws->c_set;
    h_memcpy(&ws->reg[to][0], &ws->reg[from][0],
        ws->c_reg * sizeof(struct v_reg));
    h_memcpy(&ws->mem[to][0], &ws->mem[from][0],
        ws->c_mem[from] * sizeof(struct v_mem));
    h_memcpy(&ws->set[to], &ws->set[from], sizeof(struct g_set_info));
    ws->set[to].parent = from;
    ws->c_mem[to] = ws->c_mem[from];
    ws->c_set++;
    return to;
}

void
g_seed_set_addcond(struct g_seed_ws *ws, int set, int ip, int exp, int taken)
{
    int c;
    if (ws->inst[ip].type != G_SEED_CONTROL
        || ws->inst[ip].inst.c.cond == G_SEED_CONTRL_COND_ALWAYS) {
        SEE_DEBUG("cannot create cond on nonconditionals or jmps");
        return;
    }
    c = ws->inst[ip].inst.c.cond;
    ws->cond[ws->c_cond].p_exp = exp;
    if (taken) {
        c = c ^ 1;
    }
    ws->cond[ws->c_cond].cond = c;
    ws->set[set].cond[ws->set[set].c_cond] = ws->c_cond;
    ws->set[set].ip[ws->set[set].c_cond] = ip;
    ws->set[set].c_cond++;
    ws->c_cond++;
}

void
g_seed_do_set(struct v_world *world, struct g_seed_ws *ws, int ip, int c_ret,
    int set)
{
    SEE_DEBUG("total %d ret instructions", c_ret);
    while (c_ret > 0) {
        if (g_seed_onepass(world, ws, &ip, &c_ret, set)) {
            return;
        }
        if (ws->inst[ip].type == G_SEED_CONTROL
            && ws->inst[ip].inst.c.cond != G_SEED_CONTRL_COND_ALWAYS) {
            int id = ws->inst[ip].inst.c.id - 1;
            int setid;
            if (set + 1 >= G_SEED_WS_SET_MAX) {
                ws->too_complex = 1;
                return;
            }
            if (((unsigned char *) ws->pbm)[ip * ws->c_cbr + id] == 0) {
                SEE_DEBUG("handling ifs at %d", ip);
                setid = g_seed_init_set(ws, set);
                g_seed_set_addcond(ws, setid, ip,
                    ws->reg[set][G_SEED_REG_FLAG].p_exp, 0);
                g_seed_do_set(world, ws, ip + 1, c_ret, setid);
                setid = g_seed_init_set(ws, set);
                g_seed_set_addcond(ws, setid, ip,
                    ws->reg[set][G_SEED_REG_FLAG].p_exp, 1);
                g_seed_do_set(world, ws,
                    ws->inst[ip].inst.c.target, c_ret, setid);
                return;
            } else {
                SEE_DEBUG("handling loops at %d", ip);
                return;
            }
        }
    }
    ws->set[set].final = 1;
}

#define X86_INIT_REGS 9
int
g_seed_execute(struct v_world *world, void *pws)
{
    struct g_seed_ws *ws = pws;
    int i, c_ret = 0, ip = 0, set = 0;
    for (i = 0; i < ws->c_inst; i++) {
        if (ws->inst[i].type == G_SEED_RET) {
            c_ret++;
        }
    }
    for (i = 0; i < X86_INIT_REGS; i++) {
        ws->exp[i].p_exp = -1;
        ws->exp[i].sym = i;
        ws->exp[i].op = 0;
        ws->reg[0][i].p_exp = i;
    }
    ws->c_exp = X86_INIT_REGS;
    ws->c_reg = X86_INIT_REGS;
    ws->c_sym = X86_INIT_REGS;
    ws->c_mem[0] = 0;
    ws->c_set = 1;
    ws->set[0].c_cond = 0;
    ws->set[0].final = 0;
    ws->param_write = 0;
    ws->too_complex = 0;
    g_seed_do_set(world, ws, ip, c_ret, set);
    if (!ws->param_write) {
        //verify here
        return SEE_SAFE;
    }
    if (ws->too_complex) {
        return SEE_TOO_COMPLEX;
    }
    return SEE_SAFE;
}

int
g_seed_verify_context(struct v_world *world, void *pws)
{
    return 0;
}
