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
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "vm/include/world.h"

struct g_inst_match {
    unsigned int mask;
    unsigned int pattern;
    int type;
};

struct g_inst_match g_inst_table[] = {
    {0xfffffff, 0x320f000, V_INST_I},   /*override, looks like MSR but noop */
    {0xfe000000, 0xea000000, V_INST_UB}, /*BL*/ {0xfe000000, 0xfa000000, V_INST_UB},    /*BLX1 */
    {0xe000000, 0xa000000, V_INST_CB},  /*BLc */
    {0xfff000f0, 0xe1200030, V_INST_PB},        /*BLX2 */
    {0xfff000f0, 0x01200030, V_INST_I}, /*nv BLX2 */
    {0xff000f0, 0x1200030, V_INST_PB},  /*BLX2c */
    {0xfff000f0, 0xe1200010, V_INST_PB}, /*BX*/ {0xfff000f0, 0x01200010, V_INST_I}, /*BX*/ {0xff000f0, 0x1200010, V_INST_PB},   /*BXc */
    {0xfbf0fff, 0x10f0000, V_INST_F}, /*MRS*/ {0xfff1fe00, 0xf1000000, V_INST_F}, /*CPS*/ {0xfb00000, 0x3200000, V_INST_F},     /*MSR imm */
    {0xfb000f0, 0x1200000, V_INST_F},   /*MSR reg */
    {0xc50f000, 0x410f000, V_INST_PB},  /*LDR r15 */
    {0xc10f000, 0x10f000, V_INST_F},    /*data processing register shift 1,2, set cpsr, r15 */
    {0xc00f000, 0xf000, V_INST_PB},     /*data processing register shift 1,2, r15 */
    {0xe00f000, 0x400f000, V_INST_PB},  /*load store imm offset, r15 */
    {0xe00f010, 0x600f010, V_INST_PB},  /*load store reg offset, r15 */
    {0xe400000, 0x8400000, V_INST_F},   /*ldm/stm user reg */
    {0xe008000, 0x8008000, V_INST_PB},  /*ldm/stm r15 */
};

int g_tr_fault = 0;

void
g_tr_set_ip(struct v_world *world, unsigned long ip)
{
    unsigned int phys = g_v2p(world, ip, 0);
    struct v_page *mpage;
    void *virt;

    if (world->gregs.disasm_vip != 0) {
        h_deallocv_virt(((unsigned int) world->gregs.disasm_vip) & H_PFN_MASK);
    }

    mpage = h_p2mp(world, phys);
    g_tr_fault = 0;
    if (mpage == NULL) {
        V_ERR("Guest fault in g_tr_set_ip");
        g_tr_fault = 1;
        return;
    }
    world->gregs.disasm_ip = ip;
    virt = v_page_make_present(mpage);
    world->gregs.disasm_vip =
        (unsigned char *) ((unsigned int) (virt) + (ip & H_POFF_MASK));
}

unsigned long
g_tr_get_ip(struct v_world *world)
{
    return world->gregs.disasm_ip;
}

static unsigned int
g_tr_next4bytes(struct v_world *world)
{
    unsigned int ret;
    V_VERBOSE("disasm_vip: %p, disasm_ip %x", world->gregs.disasm_vip,
        world->gregs.disasm_ip);
    if ((((unsigned int) world->gregs.disasm_vip) & 0x3) != 0) {
        V_ERR("Guest pc misalignment! Thumb?");
        g_tr_fault = 1;
        return 0;
    }
    ret = *(unsigned int *) world->gregs.disasm_vip;
    world->gregs.disasm_ip += 4;
    world->gregs.disasm_vip += 4;
    if ((world->gregs.disasm_ip & H_POFF_MASK) == 0x0) {
        g_tr_set_ip(world, world->gregs.disasm_ip);
    }
    return ret;
}

static int
sign24_to_int(unsigned int i)
{
    int sign = i & 0x800000;
    if (sign) {
        return (i | 0xff000000);
    }
    return i;
}

int
g_tr_next(struct v_world *world, unsigned int *type, unsigned long *b_target)
{
    unsigned int inst, i;
    struct g_inst_match *table = g_inst_table;
    unsigned int saved_ip = g_tr_get_ip(world);
    inst = g_tr_next4bytes(world);
    if (g_tr_fault) {
        g_tr_set_ip(world, saved_ip);
        return 1;
    }
    V_VERBOSE("The instruction is %x", inst);
    for (i = 0; i < sizeof(g_inst_table) / sizeof(struct g_inst_match);
        i++, table++) {
        if ((table->mask & inst) == table->pattern) {
            V_VERBOSE("ip %x inst %x type %x", world->gregs.disasm_ip,
                inst, table->type);
            if (table->type == V_INST_UB || table->type == V_INST_CB) {
                /* can only be a B/BL inst */
                *b_target = saved_ip + 8 + (sign24_to_int(inst & 0xffffff) * 4);
                V_VERBOSE("Branch to %lx", *b_target);
            }
            *type = table->type;
            return 0;
        }
    }
    *type = V_INST_I;
    return 0;
}
