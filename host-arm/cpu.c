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
#include <linux/mm.h>
#include <linux/gfp.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include "host/include/mm.h"
#include "host/include/cpu.h"
#include "vm/include/mm.h"
#include "guest/include/cpu.h"
#include "guest/include/mm.h"
#include "vm/include/logging.h"
#include "host/include/bt.h"
#include "vm/include/perf.h"
#include "host/include/perf.h"
#include <../arch/arm/include/asm/cacheflush.h>

extern volatile int step;
extern volatile int time_up;
extern int poke;
extern int ack_pic_once;
int last_poke = 0;
int usermode_tests_reset = 0;

struct h_cpu hostcpu;

#ifdef BT_CACHE
static int cache_offset;
void bt_cache_start1(void);
#endif

static void h_inject_int(struct v_world *world, unsigned int int_no);

void
debug_dump(struct v_world *world)
{
/*    int i;
    for (i = G_PA_BASE; i < world->pa_top; i+=H_PAGE_SIZE) {
        struct v_page * itr = &world->page_list[h_p2page(i - G_PA_BASE)];
        if (itr->has_virt) {
            void *virt = v_page_make_present(itr);
            int stepa;
            int L = 0, I = 0, N = 0, U = 0, X = 0, V = 0, E = 0, R = 0, S = 0;
            for (stepa = 0; stepa < H_PAGE_SIZE; stepa++) {
                if (*(unsigned char*)(virt+stepa) == 'L') L = 1;
                if (*(unsigned char*)(virt+stepa) == 'i') I = 1;
                if (*(unsigned char*)(virt+stepa) == 'n') N = 1;
                if (*(unsigned char*)(virt+stepa) == 'u') U = 1;
                if (*(unsigned char*)(virt+stepa) == 'x') X = 1;
                if (*(unsigned char*)(virt+stepa) == 'v') V = 1;
                if (*(unsigned char*)(virt+stepa) == 'e') E = 1;
                if (*(unsigned char*)(virt+stepa) == 'r') R = 1;
                if (*(unsigned char*)(virt+stepa) == 's') S = 1;
                if (L&&I&&N&&U&&X&&V&&E&&R&&S) {
                    V_ERR("found %x", i);
                    break;
                }
            }
        }
    }
*/
}

int
h_cpu_init(void)
{
    void *power;
    asm volatile ("mrc p15 ,0 ,%0, c0, c0, 0":"=r" (hostcpu.p15_id));
    V_LOG("main id: %x\n", hostcpu.p15_id);
    asm volatile ("mrc p15 ,0 ,%0, c1, c0, 0":"=r" (hostcpu.p15_ctrl));
    V_LOG("control: %x\n", hostcpu.p15_ctrl);
    asm volatile ("mrc p15 ,0 ,%0, c2, c0, 1":"=r" (hostcpu.p15_trbase));
    V_LOG("trbase: %x\n", hostcpu.p15_trbase);
    asm volatile ("mrc p15 ,0 ,%0, c2, c0, 0":"=r" (hostcpu.p15_trbase));
    V_LOG("trbase: %x\n", hostcpu.p15_trbase);
    asm volatile ("mrc p15 ,0 ,%0, c2, c0, 2":"=r" (hostcpu.p15_trctl));
    V_LOG("tr control: %x\n", hostcpu.p15_trctl);
    asm volatile ("mrc p15 ,0 ,%0, c2, c0, 0":"=r" (hostcpu.p15_trattr));
    hostcpu.p15_trattr &= 0x3fff;
    hostcpu.p15_trbase &= 0xffffc000;
    V_LOG("trattr: %x\n", hostcpu.p15_trattr);

    hostcpu.domain = 0;
    asm volatile ("mrc p15, 0, %0, c12, c0, 0":"=r" (hostcpu.p15_vector));
    V_LOG("Vector base: %x\n", hostcpu.p14_didr);

    power = (void *) (0xb2000000 + 0x4a306000 + 0x1a00);
    V_LOG("Power state is %x\n", *(unsigned int *) power);
    *(unsigned int *) power = 2;
    asm volatile ("mcr p14 ,0 ,%0, c1, c0, 4"::"r" (0));
    asm volatile ("isb");

    asm volatile ("mcr p14 ,0 ,%0, c0, c7, 0"::"r" (0));
    asm volatile ("isb");
    asm volatile ("mrc p14 ,0 ,%0, c0, c0, 0":"=r" (hostcpu.p14_didr));
    V_LOG("DIDR: %x\n", hostcpu.p14_didr);
    /* we take one breakpoint for single step purpose only */
    hostcpu.number_of_breakpoints = ((hostcpu.p14_didr & 0x0f000000) >> 24);
    V_LOG("Number of breakpoints %x", hostcpu.number_of_breakpoints);
    asm volatile ("mrc p14 ,0 ,%0, c0, c1, 0":"=r" (hostcpu.p14_dscr));
    V_LOG("DSCR: %x\n", hostcpu.p14_dscr);
    asm volatile ("mrc p14 ,0 ,%0, c1, c0, 0":"=r" (hostcpu.p14_drar));
    V_LOG("DRAR: %x\n", hostcpu.p14_drar);
    hostcpu.p14_drar &= 0xfffff000;
    asm volatile ("mrc p14 ,0 ,%0, c2, c0, 0":"=r" (hostcpu.p14_dsar));
    V_LOG("DSAR: %x\n", hostcpu.p14_dsar);
    asm volatile ("mrc p14 ,0 ,%0, c0, c0, 5":"=r" (hostcpu.p14_dsar));
    V_LOG("DBGBCR0: %x\n", hostcpu.p14_dsar);
    asm volatile ("mrc p14 ,0 ,%0, c0, c1, 5":"=r" (hostcpu.p14_dsar));
    V_LOG("DBGBCR1: %x\n", hostcpu.p14_dsar);
    asm volatile ("mrc p14 ,0 ,%0, c0, c2, 5":"=r" (hostcpu.p14_dsar));
    V_LOG("DBGBCR2: %x\n", hostcpu.p14_dsar);
    asm volatile ("mrc p14 ,0 ,%0, c0, c3, 5":"=r" (hostcpu.p14_dsar));
    V_LOG("DBGBCR3: %x\n", hostcpu.p14_dsar);
    asm volatile ("mrc p14 ,0 ,%0, c0, c4, 5":"=r" (hostcpu.p14_dsar));
    V_LOG("DBGBCR4: %x\n", hostcpu.p14_dsar);
    asm volatile ("mrc p14 ,0 ,%0, c0, c5, 5":"=r" (hostcpu.p14_dsar));
    V_LOG("DBGBCR5: %x\n", hostcpu.p14_dsar);

    asm volatile ("mrc p14 ,0 ,%0, c1, c5, 4":"=r" (hostcpu.p14_dsar));
    V_LOG("PowerState: %x\n", hostcpu.p14_dsar);
    asm volatile ("mrc p14 ,0 ,%0, c7, c14, 6":"=r" (hostcpu.p14_dsar));
    V_LOG("DbgAuth: %x\n", hostcpu.p14_dsar);

    asm volatile ("mcr p14 ,0 ,%0, c1, c0, 4"::"r" (0));
    asm volatile ("isb");

    asm volatile ("mcr p14 ,0 ,%0, c0, c7, 0"::"r" (0));
    asm volatile ("isb");

    hostcpu.p14_dscr |= 0x8000;
    asm volatile ("mcr p14 ,0 ,%0, c0, c2, 2"::"r" (hostcpu.p14_dscr));
    asm volatile ("mcr p14 ,0 ,%0, c0, c2, 2"::"r" (hostcpu.p14_dscr));
    V_LOG("Try to set DSCR to: %x\n", hostcpu.p14_dscr);
    asm volatile ("mrc p14 ,0 ,%0, c0, c1, 0":"=r" (hostcpu.p14_dscr));
    V_LOG("DSCR: %x\n", hostcpu.p14_dscr);
    cache_offset = ((void *) bt_cache_start1) - ((void *) h_switch_to1);
    return 0;
}

void
h_cpu_save(struct v_world *w)
{
    struct h_regs *h = &w->hregs;
    asm volatile ("mrc p15 ,0 ,%0, c2, c0, 0":"=r" (hostcpu.p15_trbase));
    asm volatile ("mrc p15 ,0 ,%0, c2, c0, 0":"=r" (h->hcpu.p15_trbase));
    V_LOG("saved trbase: %x\n", h->hcpu.p15_trbase);
    asm volatile ("mrc p15, 0, %0, c13, c0, 1":"=r" (h->hcpu.p15_asid));
}

static void inline
h_cpu_switch_mm(unsigned int tr, unsigned int asid)
{
    asm volatile ("mcr p15, 0, %0, c13, c0, 1"::"r" (0));
    asm volatile ("isb");
    asm volatile ("mcr p15, 0, %0, c2, c0, 0"::"r" (tr));
    asm volatile ("isb");
    asm volatile ("mcr p15, 0, %0, c13, c0, 1"::"r" (asid));
    asm volatile ("isb");
}

static void inline
h_flush_cache_all(void)
{
    flush_cache_all();
}

static void inline
h_flush_tlb_all(void)
{
    asm volatile ("isb");
    asm volatile ("dsb");
    asm volatile ("mcr      p15, 0, %0, c7, c1, 0"::"r" (0));
    asm volatile ("mcr      p15, 0, %0, c8, c6, 0"::"r" (0));
    asm volatile ("mcr      p15, 0, %0, c8, c5, 0"::"r" (0));
    asm volatile ("mcr      p15, 0, %0, c8, c7, 0"::"r" (0));
    asm volatile ("mcr      p15, 0, %0, c7, c5, 6"::"r" (0));
    asm volatile ("isb");
    asm volatile ("dsb");
}

#ifdef USERMODE_DEBUG

static int usermode_debug = 0;

#endif

#ifdef BT_CACHE

#define __TOTAL 0
#define __SET 4
#define __PB_TOTAL 8
#define __PB_SET 12
#define __PB_START 16
#define __PB_ENTRIES (BT_CACHE_CAPACITY)
#define __CB_START (__PB_START + __PB_ENTRIES * 8)

void
h_bt_squash_pb(struct v_world *world)
{
    void *cache = world->npage;
    cache += cache_offset;
    *((unsigned int *) (cache + __PB_TOTAL)) = 0;
}

void
h_bt_cache_restore(struct v_world *world)
{
    void *cache = world->npage;
    int total, set, pb_total, pb_set;
    struct h_bt_cache *hcache;
    struct h_bt_pb_cache *h_pb_cache;
    cache += cache_offset;
    total = *((unsigned int *) cache);
    set = *((unsigned int *) (cache + __SET));
    pb_total = *((unsigned int *) (cache + __PB_TOTAL));
    pb_set = *((unsigned int *) (cache + __PB_SET));
    V_VERBOSE("Total %x set %x, pb Total %x set %x", total, set, pb_total,
        pb_set);
    if (total != 0 && set != 0) {
        hcache = (struct h_bt_cache *) (cache + __CB_START);
        world->poi = hcache[total - set].poi;
        world->poi->expect = 0;
        V_LOG("BT restore poi to %lx", world->poi->addr);
        world->current_valid_bps = world->poi->plan.count;
         h_bt_reset(world);
        for (set = 0; set < world->poi->plan.count; set++) {
            world->bp_to_poi[set] = world->poi->plan.poi[set];
            h_set_bp(world, world->bp_to_poi[set]->addr, set);
        }
    } else if (pb_total != 0 && pb_set != 0) {
        h_pb_cache = (struct h_bt_pb_cache *) (cache + __PB_START);
        world->poi = h_pb_cache[pb_total - pb_set].poi;
        world->poi->expect = 1;
        V_VERBOSE("BT restore pb poi to %lx", world->poi->addr);
        h_bt_reset(world);
        world->hregs.gcpu.bcr[hostcpu.number_of_breakpoints] =
            BPC_BAS_ANY | BPC_ENABLED | BPC_MISMATCH;
        world->hregs.gcpu.bvr[hostcpu.number_of_breakpoints] = world->poi->addr;
    }
    *((unsigned int *) (cache + __SET)) = 0;
    *((unsigned int *) (cache + __PB_SET)) = 0;
}

void
h_bt_cache(struct v_world *world, struct v_poi_cached_tree_plan *plan,
    int count)
{
    void *cache = world->npage;
    struct h_bt_cache *hcache;
    struct h_bt_pb_cache *pb_cache;
    int i;
    int pb_total = 0;
    cache += cache_offset;
    *((unsigned int *) cache) = count;
    *((unsigned int *) (cache + __SET)) = 0;
    *((unsigned int *) (cache + __PB_TOTAL)) = 0;
    *((unsigned int *) (cache + __PB_SET)) = 0;
    pb_cache = (struct h_bt_pb_cache *) (cache + __PB_START);
    V_VERBOSE("Cache total %x", count);
    if (count != 0) {
        hcache = (struct h_bt_cache *) (cache + __CB_START);
        for (i = 0; i < count; i++) {
            int j;
            hcache[i].poi = plan[i].poi;
            hcache[i].addr = plan[i].addr;
            for (j = 0; j < plan[i].plan->count; j++) {
                hcache[i].bvr[j] = plan[i].plan->poi[j]->addr;
                hcache[i].bcr[j] = BPC_BAS_ANY | BPC_ENABLED;
                if ((plan[i].plan->poi[j]->type & V_INST_PB)
                    && pb_total <= 2 * BT_CACHE_CAPACITY) {
                    int k;
                    for (k = 0; k < pb_total; k++) {
                        if (pb_cache[k].addr == plan[i].plan->poi[j]->addr) {
                            goto found;
                        }
                    }
                    pb_cache[pb_total].addr = plan[i].plan->poi[j]->addr;
                    pb_cache[pb_total].poi = plan[i].plan->poi[j];
                    V_VERBOSE("cache %x is %lx", pb_total,
                        pb_cache[pb_total].addr);
                    pb_total++;
                }
            }
            found:
            for (; j < 6; j++) {
                hcache[i].bvr[j] = 0;
                hcache[i].bcr[j] = BPC_DISABLED;
            }
        }
    }
    for (i = 0; i < world->current_valid_bps; i++) {
        if ((world->bp_to_poi[i]->type & V_INST_PB)
            && pb_total <= 2 * BT_CACHE_CAPACITY) {
            int k;
            for (k = 0; k < pb_total; k++) {
                if (pb_cache[k].addr == world->bp_to_poi[i]->addr) {
                    goto found1;
                }
            }
            pb_cache[pb_total].addr = world->bp_to_poi[i]->addr;
            pb_cache[pb_total].poi = world->bp_to_poi[i];
            V_VERBOSE("cache %x is %lx", pb_total, pb_cache[pb_total].addr);
            pb_total++;
          found1:
            asm volatile ("nop");
        }
    }
    *((unsigned int *) (cache + __PB_TOTAL)) = pb_total;
}
#endif

#define STRINGIFY(tok) #tok

#ifdef BT_CACHE
#define CACHE_BT_QUICKPATH(function_no) \
    "cmp r12, #3\n\r" \
    "beq 10f\n\r" \
    "9:\n\r"
#else
#define CACHE_BT_QUICKPATH(function_no)
#endif

#ifdef BT_CACHE
#define CACHE_BT_REGION(function_no, cache_capacity) \
    asm volatile ("10:"); \
    asm volatile ("mov r11, pc"); \
    asm volatile ("b 11f"); \
    asm volatile (".global bt_cache_start"#function_no); \
    asm volatile ("bt_cache_start"#function_no":"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".rept "STRINGIFY(cache_capacity)); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".endr"); \
    asm volatile (".rept "STRINGIFY(cache_capacity)); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".endr"); \
    asm volatile ("11:"); \
    asm volatile ("mrc p15, 0, r0, c5, c0, 1"); /*ifsr*/\
    asm volatile ("cmp r0, #2"); \
    asm volatile ("bne 9b"); \
    asm volatile ("80:"); \
    asm volatile ("ldr r0, [r11]"); \
    asm volatile ("ldr r1, [r11, #8]"); \
    asm volatile ("adds r4, r0, r1"); \
    asm volatile ("beq 9b"); \
    asm volatile ("mov r4, #0"); \
    asm volatile ("21:"); \
    asm volatile ("cmp r1, #0"); \
    asm volatile ("beq 20f"); \
    asm volatile ("add r10, r11, #16"); \
    asm volatile ("ldr r5, [r10, r4, lsl #3]"); \
    asm volatile ("cmp r5, lr"); \
    asm volatile ("beq 50f"); \
    asm volatile ("sub r1, r1, #1"); \
    asm volatile ("add r4, r4, #1"); \
    asm volatile ("b 21b"); \
    asm volatile ("20:"); \
    asm volatile ("b 9b"); \
    asm volatile ("50:"); \
    asm volatile ("str r1, [r11, #12]"); \
    asm volatile ("mov r0, #0"); \
    asm volatile ("mcr p14, 0, r0, c0, c0, 5"); \
    asm volatile ("mcr p14, 0, r0, c0, c0, 4"); \
    asm volatile ("mcr p14, 0, r0, c0, c1, 5"); \
    asm volatile ("mcr p14, 0, r0, c0, c1, 4"); \
    asm volatile ("mcr p14, 0, r0, c0, c2, 5"); \
    asm volatile ("mcr p14, 0, r0, c0, c2, 4"); \
    asm volatile ("mcr p14, 0, r0, c0, c3, 5"); \
    asm volatile ("mcr p14, 0, r0, c0, c3, 4"); \
    asm volatile ("mcr p14, 0, r0, c0, c4, 5"); \
    asm volatile ("mcr p14, 0, r0, c0, c4, 4"); \
    asm volatile ("mov r0, #(1 << 22)"); \
    asm volatile ("add r0, r0, #(1 << 8)"); \
    asm volatile ("add r0, r0, #0xe5"); \
    asm volatile ("mcr p14, 0, r0, c0, c5, 5"); \
    asm volatile ("mcr p14, 0, lr, c0, c5, 4"); \
    asm volatile ("ldmia r13, {r0-r14}^"); \
    asm volatile ("movs pc, lr");
#else
#define CACHE_BT_REGION
#endif


#ifdef BT_CACHE
#define H_BT_CACHE_RESTORE(w) h_bt_cache_restore(w);
#else
#define H_BT_CACHE_RESTORE(w)
#endif

#define H_SWITCH_TO(function_no) \
void h_vector_entry##function_no(void); \
__attribute__ ((aligned (0x1000))) \
int \
h_switch_to##function_no(unsigned long trbase, struct v_world *w) \
{ \
    struct h_regs *h = &w->hregs; \
    unsigned int tr = ((unsigned int) trbase) | (hostcpu.p15_trattr); \
    unsigned long i; \
    unsigned int ifar, ifsr, dfar, dfsr; \
    h->hcpu.p15_trattr = hostcpu.p15_trattr; \
    local_irq_save(i); \
    asm volatile ("mcr p15, 0, %0, c1, c0, 0"::"r" (hostcpu.p15_ctrl & \
            (~P15_CTRL_V))); \
    asm volatile ("mcr p15, 0, %0, c12, c0, 0"::"r" (h_vector_entry##function_no)); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c0, 5"::"r" (w->hregs.gcpu.bcr[0])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c0, 4"::"r" (w->hregs.gcpu.bvr[0])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c1, 5"::"r" (w->hregs.gcpu.bcr[1])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c1, 4"::"r" (w->hregs.gcpu.bvr[1])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c2, 5"::"r" (w->hregs.gcpu.bcr[2])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c2, 4"::"r" (w->hregs.gcpu.bvr[2])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c3, 5"::"r" (w->hregs.gcpu.bcr[3])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c3, 4"::"r" (w->hregs.gcpu.bvr[3])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c4, 5"::"r" (w->hregs.gcpu.bcr[4])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c4, 4"::"r" (w->hregs.gcpu.bvr[4])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c5, 5"::"r" (w->hregs.gcpu.bcr[5])); \
    asm volatile ("mcr p14 ,0 ,%0, c0, c5, 4"::"r" (w->hregs.gcpu.bvr[5])); \
    h_cpu_switch_mm(tr, 0xb1); \
    h_flush_tlb_all(); \
 \
    asm volatile ( \
        /*world switch starts here */ \
        "mov r12, %0\n\t"               /*host cpu save base */ \
        "stmia r12, {r0-r14}\n\t"       /*save host cpu registers */ \
        "mov r10, %1\n\t"               /*guest cpu save base */ \
        "add r12, r12, #60\n\t" \
        "mov r0, #0xd1\n\t" /*FIQ*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r1, r13\n\t" \
        "mov r2, r14\n\t" \
        "mov r0, #0xd2\n\t" /*IRQ*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r3, r13\n\t" \
        "mov r4, r14\n\t" \
        "mov r0, #0xd7\n\t" /*ABT*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r5, r13\n\t" \
        "mov r6, r14\n\t" \
        "mov r0, #0xdb\n\t" /*UND*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r7, r13\n\t" \
        "mov r8, r14\n\t" \
        "mov r0, #0xd3\n\t" /*SVC*/ \
        "mov r9, r13\n\t" \
        "mov r11, r14\n\t" \
        "msr cpsr_c, r0\n\t" \
        "stmia r12, {r13-r14}^\n\t"   /* save banked reg */\
        "add r12, r12, #8\n\t" \
        "mrs r0, spsr\n\t" \
        "str r0, [r12]\n\t" \
        "add r12, r12, #8\n\t" \
        "stmia r12, {r1-r9, r11}\n\t"      /* save stack position */\
        "mov r0, #0xd1\n\t" /*FIQ*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r10\n\t" \
        "mov r14, r10\n\t" \
        "mov r0, #0xd2\n\t" /*IRQ*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r10\n\t" \
        "mov r14, r10\n\t" \
        "mov r0, #0xd7\n\t" /*ABT*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r10\n\t" \
        "mov r14, r10\n\t" \
        "mov r0, #0xdb\n\t" /*UND*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r10\n\t" \
        "mov r14, r10\n\t" \
        "mov r0, #0xd3\n\t" /*SVC*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r10\n\t" \
        "mov r14, r10\n\t" \
        "str r12, [r10, #124]\n\t"   /*assign saved_sp, gcpu.saved_sp = &hcpu.r13_fiq */\
/*debug        "str r10, [r12, #32]\n\t"*/\
        "mov lr, r10\n\t"       /* only lr won't be stomped by restore */\
        "ldr r0, [lr, #116]\n\t"        /* get gcpu.cpsr */\
        "msr spsr_csxf, r0\n\t" \
        "ldmia lr, {r0-r14}^\n\t" \
        "ldr lr, [lr, #120]\n\t"      /*get gcpu.pc */\
        "movs pc, lr\n\t" \
/*        ".global h_vector_test" */\
/*        "h_vector_test:\n\r" "cpsie i\n\r" "1:\n\r" "b 1b\n\r"*/\
        /* world switch out here */ \
        "h_vector_return"#function_no":\n\r" \
        CACHE_BT_QUICKPATH(function_no) \
        "mrs r0, spsr\n\t" \
/*debug        "mov r0, #0xff\n\t"*/\
        "str r0, [r13, #116]\n\t"       /* save gcpu.cpsr (=host environment spsr) */\
        "str lr, [r13, #120]\n\t"       /* save gcpu.pc (=host environment lr) */\
        "ldr r12, [r13, #124]\n\t"      /* r12 = gcpu.saved_sp = hcpu.r13_fiq */\
/*debug        "str r10, [r12, #40]\n\t"*/\
        "mov r0, #0xd3\n\t" \
        "msr cpsr_c, r0\n\t" \
        "ldmia r12, {r2-r11}\n\t" \
        "mov r0, #0xd1\n\t" /*FIQ*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r2\n\t" \
        "mov r14, r3\n\t" \
        "mov r0, #0xd2\n\t" /*IRQ*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r4\n\t" \
        "mov r14, r5\n\t" \
        "mov r0, #0xd7\n\t" /*ABT*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r6\n\t" \
        "mov r14, r7\n\t" \
        "mov r0, #0xdb\n\t" /*UND*/ \
        "msr cpsr_c, r0\n\t" \
        "mov r13, r8\n\t" \
        "mov r14, r9\n\t" \
        "mov r0, #0xd3\n\t" /*SVC*/ \
        "mov r13, r10\n\t" \
        "mov r14, r11\n\t" \
        "msr cpsr_c, r0\n\t" \
        "sub r12, r12, #8\n\t" \
        "ldr r0, [r12]\n\t" \
        "msr spsr_csxf, r0\n\t" \
        "sub r12, r12, #8\n\t" \
        "ldmia r12, {r13-r14}^\n\t" \
        "sub r12, r12, #60\n\t"::"r" (&(h->hcpu.r0)), "r"(&(h->gcpu.r0)):"r12"); \
\
    /* must maintain r12 until this instruction*/ \
    asm volatile ("ldmia r12, {r0-r14}"); \
 \
/*    asm volatile ("h_vector_return:");*/ \
    h_cpu_switch_mm(h->hcpu.p15_trbase, h->hcpu.p15_asid); \
    h_flush_tlb_all(); \
     /*IFAR*/ asm volatile ("mrc p15, 0, %0, c6, c0, 2":"=r" (ifar)); \
     /*IFSR*/ asm volatile ("mrc p15, 0, %0, c5, c0, 1":"=r" (ifsr)); \
     /*DFAR*/ asm volatile ("mrc p15, 0, %0, c6, c0, 0":"=r" (dfar)); \
     /*DFSR*/ asm volatile ("mrc p15, 0, %0, c5, c0, 0":"=r" (dfsr)); \
    asm volatile ("mcr p15, 0, %0, c1, c0, 0"::"r" (hostcpu.p15_ctrl)); \
    local_irq_restore(i); \
    H_BT_CACHE_RESTORE(w); \
    v_perf_inc(V_PERF_WS, 1); \
    V_LOG("breakpoints %x %x", w->hregs.gcpu.bcr[0], w->hregs.gcpu.bvr[0]); \
    V_VERBOSE("saved banked regs: %x %x %x %x %x %x %x %x", h->hcpu.r13_fiq, \
        h->hcpu.r14_fiq, h->hcpu.r13_irq, h->hcpu.r14_irq, \
        h->hcpu.r13_abt, h->hcpu.r14_abt, h->hcpu.r13_und, h->hcpu.r14_und); \
    V_VERBOSE("host regs: saved spsr %x, saved sp: %x", \
        h->hcpu.spsr_save, h->gcpu.save_sp); \
    V_LOG("Guest WS due to %x. Registers: pc=%x, cpsr=%x(%x), r0=%x, r1=%x, " \
        "r2=%x, r3=%x, r4=%x, r5=%x, r6=%x, r7=%x, r8=%x, r9=%x, r10=%x, " \
        "r11=%x, r12=%x, r13=%x, r14 = %x", h->gcpu.reason, h->gcpu.pc, \
        w->gregs.cpsr, h->gcpu.cpsr, h->gcpu.r0, h->gcpu.r1, h->gcpu.r2, \
        h->gcpu.r3, h->gcpu.r4, h->gcpu.r5, h->gcpu.r6, h->gcpu.r7, h->gcpu.r8, \
        h->gcpu.r9, h->gcpu.r10, h->gcpu.r11, h->gcpu.r12, h->gcpu.r13, \
        h->gcpu.r14); \
    if ((h->gcpu.reason == 3 || h->gcpu.reason == 4)) { \
        unsigned int fault = 0; \
        g_addr_t address = 0; \
        int bp_hit = 0; \
        V_LOG("IFAR %x, IFSR %x, DFAR %x, DFSR %x", ifar, ifsr, dfar, dfsr); \
        if (h->gcpu.reason == 3 && ((ifsr & 0x0d) == 0x0d)) { \
            V_ERR \
                ("Unhandled fault. Prefetch fault should never cause permission errors"); \
            w->status = VM_PAUSED; \
            time_up = 1; \
            return 0; \
        } \
        /*todo: assert other failures. debug events can't occur on dabt. cannot handle imprecise aborts.*/ \
        if (h->gcpu.reason == 3) { \
            int i; \
            if (g_in_priv(w)) { \
                if (h_get_bp(w, hostcpu.number_of_breakpoints) != g_get_ip(w) \
                    && (w->hregs.gcpu.bcr[hostcpu.number_of_breakpoints] & 1)) { \
                    bp_hit = 1; \
                    v_perf_inc(V_PERF_BT, 1); \
                    v_do_bp(w, g_get_ip(w), 1); \
                } else { \
                    for (i = 0; i < hostcpu.number_of_breakpoints; i++) { \
                        if (h_get_bp(w, i) == g_get_ip(w)) { \
                            bp_hit = 1; \
                            V_EVENT("Breakpoint %x hit at %x", i, g_get_ip(w)); \
                            v_perf_inc(V_PERF_BT, 1); \
                            v_do_bp(w, g_get_ip(w), 0); \
                            break; \
                        } \
                    } \
                } \
            } \
            fault = V_MM_FAULT_NP; \
            address = ifar; \
        } else if (h->gcpu.reason == 4) { \
            fault = ((dfsr & 0x0d) == 0x0d) ? V_MM_FAULT_W : V_MM_FAULT_NP; \
            fault |= ((dfsr & 0x800) ? V_MM_FAULT_W : 0); \
            address = dfar; \
        } \
 \
        if (!bp_hit) { \
            /*          v_perf_inc(V_PERF_PF, 1);*/ \
            v_perf_inc(V_PERF_PF, 1); \
            if ((fault = v_pagefault(w, address, fault)) != V_MM_FAULT_HANDLED) { \
                V_ERR("Guest injection of page fault %x, %x, %x, %x", dfar, \
                    ifar, dfsr, ifsr); \
                w->gregs.p15_dfar = dfar; \
                w->gregs.p15_ifar = ifar; \
                w->gregs.p15_dfsr = dfsr; \
                w->gregs.p15_ifsr = ifsr; \
                h_inject_int(w, h->gcpu.reason); \
            } \
            /*      h_perf_tsc_end(H_PERF_TSC_PF, 0);*/ \
        } \
/*              return 1;*/\
    } else if (h->gcpu.reason == 1) { \
        if (g_in_priv(w) || (w->gregs.cpsr & G_PRIV_MASK) == G_PRIV_RST) { \
            v_perf_inc(V_PERF_PI, 1); \
            h_do_fail_inst(w, g_get_ip(w)); \
        } else { \
            w->status = VM_PAUSED; \
            V_ERR("Unprivileged und fault"); \
            V_ERR("breakpoints %x %x", w->hregs.gcpu.bcr[0], \
                w->hregs.gcpu.bvr[0]); \
            V_ERR("breakpoints %x %x", w->hregs.gcpu.bcr[1], \
                w->hregs.gcpu.bvr[1]); \
            V_ERR("breakpoints %x %x", w->hregs.gcpu.bcr[2], \
                w->hregs.gcpu.bvr[2]); \
            V_ERR("breakpoints %x %x", w->hregs.gcpu.bcr[3], \
                w->hregs.gcpu.bvr[3]); \
            V_ERR("breakpoints %x %x", w->hregs.gcpu.bcr[4], \
                w->hregs.gcpu.bvr[4]); \
            V_ERR("breakpoints %x %x", w->hregs.gcpu.bcr[5], \
                w->hregs.gcpu.bvr[5]); \
            h_do_fail_inst(w, g_get_ip(w)); \
            h_inject_int(w, 1); \
        } \
    } else if (h->gcpu.reason == 2) { \
        if (!((w->gregs.cpsr & G_PRIV_MASK) == G_PRIV_RST)) { \
            V_ERR("Guest sys call @ %x, with %x %x %x", h->gcpu.pc, h->gcpu.r0, \
                h->gcpu.r1, h->gcpu.r2); \
            h_inject_int(w, 2); \
        } \
    } else if (h->gcpu.reason == 6) { \
        if (!(w->gregs.cpsr & H_CPSR_I)) { \
            if (last_poke + 100 < poke) { \
                last_poke = poke; \
                ack_pic_once = 1; \
                h_inject_int(w, 6); \
            } \
        } \
        time_up = 1; \
    } \
 \
 \
    asm volatile ("b skip_entrycode"#function_no); \
    asm volatile (".balign 32"); \
    asm volatile (".global h_vector_entry"#function_no); \
    asm volatile ("h_vector_entry"#function_no":"); \
    asm volatile ("b reset_entry"#function_no); \
    asm volatile ("b und_entry"#function_no); \
    asm volatile ("b swi_entry"#function_no); \
    asm volatile ("b pre_entry"#function_no); \
    asm volatile ("b data_entry"#function_no); \
    asm volatile ("b inv_entry"#function_no); \
    asm volatile ("b irq_entry"#function_no); \
    asm volatile ("b fiq_entry"#function_no); \
 \
    asm volatile ("reset_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("mov r12, #0"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
 \
    asm volatile ("und_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("sub lr, lr, #4"); \
    asm volatile ("mov r12, #1"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
 \
    asm volatile ("swi_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("mov r12, #2"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
 \
    asm volatile ("pre_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("sub lr, lr, #4"); \
    asm volatile ("mov r12, #3"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
 \
    asm volatile ("data_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("sub lr, lr, #8"); \
    asm volatile ("mov r12, #4"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
 \
    asm volatile ("inv_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("sub lr, lr, #4"); \
    asm volatile ("mov r12, #5"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
 \
    asm volatile ("irq_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("sub lr, lr, #4"); \
    asm volatile ("mov r12, #6"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
 \
    asm volatile ("fiq_entry"#function_no":"); \
    asm volatile ("stmia r13, {r0-r14}^"); \
    asm volatile ("sub lr, lr, #4"); \
    asm volatile ("mov r12, #7"); \
    asm volatile ("str r12, [r13, #72]"); \
    asm volatile ("b h_vector_return"#function_no); \
    CACHE_BT_REGION(function_no, BT_CACHE_CAPACITY) \
    asm volatile (".ltorg"); \
    asm volatile ("skip_entrycode"#function_no":"); \
    return 0; \
}

H_SWITCH_TO(0)
    H_SWITCH_TO(1)
    H_SWITCH_TO(2)
    H_SWITCH_TO(3)

int
h_read_guest(struct v_world *world, unsigned int addr, unsigned int *word)
{
    unsigned int pa = g_v2p(world, addr, 0);
    void *virt;
    struct v_page *mpage;
    mpage = h_p2mp(world, pa);
    if (mpage == NULL) {
        V_ERR("Guest GP Fault during access\n");
        return 1;
    }
    if ((addr & 0xfff) < 0xffd) {
        //no page crossing
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        (*word) = (*(unsigned int *) (virt));
    } else {
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        memcpy(word, virt, 0xfff - (addr & 0xfff) + 1);
        pa = g_v2p(world, addr + 0x4, 0);
        mpage = h_p2mp(world, pa);
        if (mpage == NULL) {
            V_ERR("Guest GP Fault during access\n");
            return 1;
        }
        virt = v_page_make_present(mpage);
        memcpy((void *) (word) + 0xfff - (addr & 0xfff) + 1, virt,
            3 + (addr & 0xfff) - 0xfff);

    }
    return 0;
}

int
h_write_guest(struct v_world *world, unsigned int addr, unsigned int word)
{
    unsigned int pa = g_v2p(world, addr, 0);
    void *virt;
    struct v_page *mpage;
    mpage = h_p2mp(world, pa);
    if (mpage == NULL) {
        V_ERR("Guest GP Fault during write\n");
        return 1;
    }
    if ((addr & 0xfff) < 0xffd) {
        //no page crossing
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        (*(unsigned int *) (virt)) = word;
    } else {
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        memcpy(virt, &word, 0xfff - (addr & 0xfff) + 1);
        pa = g_v2p(world, addr + 0x4, 0);
        mpage = h_p2mp(world, pa);
        if (mpage == NULL) {
            V_ERR("Guest GP Fault during write\n");
            return 1;
        }
        virt = v_page_make_present(mpage);
        memcpy(virt, ((void *) (&word)) + 0xfff - (addr & 0xfff) + 1,
            3 + (addr & 0xfff) - 0xfff);

    }
    return 0;
}


static inline void
h_switch_stack(struct v_world *world, unsigned int from, unsigned int to)
{
    if ((to & 0x1f) == H_CPSR_USR) {
        V_EVENT("Switching to user mode! %x, %x\n", world->hregs.gcpu.pc,
            world->hregs.gcpu.r14);
    }
    world->gregs.r13_modes[from & 0xf] = world->hregs.gcpu.r13;
    world->gregs.r14_modes[from & 0xf] = world->hregs.gcpu.r14;
    world->hregs.gcpu.r13 = world->gregs.r13_modes[to & 0xf];
    world->hregs.gcpu.r14 = world->gregs.r14_modes[to & 0xf];
}

static void
h_inject_int(struct v_world *world, unsigned int voff)
{
    unsigned int vector;
    unsigned int newmode = 0;
    unsigned int pc_save = 4;
    if (voff == 1)
        newmode = H_CPSR_UND;
    else if (voff == 2) {
        newmode = H_CPSR_SVC;
        pc_save -= 4;
    } else if (voff == 3)
        newmode = H_CPSR_ABT;
    else if (voff == 4) {
        newmode = H_CPSR_ABT;
        pc_save += 4;
    } else if (voff == 6)
        newmode = H_CPSR_IRQ;
    else {
        world->status = VM_PAUSED;
        V_ERR("Switching to unknown vector %x", voff);
        return;
    }
//    if ((world->gregs.cpsr & 0x1f) == H_CPSR_SVC) {
//        v_add_ipoi(world, g_get_ip(world), g_get_poi_key(world), world->poi);
//    }
    v_bt_reset(world);
    if (h_read_guest(world, 0xffff0000 + voff * 4, &vector)) {
        world->status = VM_PAUSED;
        V_ERR("Vector read fault. Guest crashed");
        return;
    }
    pc_save += world->hregs.gcpu.pc;
    V_EVENT("Switching to vector %x instruction %x, pc %x, r13 %x, r14 %x",
        voff, vector, pc_save, world->hregs.gcpu.r13, world->hregs.gcpu.r14);
    world->gregs.spsr[newmode & 0xf] =
        (world->hregs.gcpu.cpsr & 0xffffff00) | (world->gregs.cpsr & 0xff);
    world->hregs.gcpu.pc = 0xffff0000 + voff * 4;
    h_switch_stack(world, world->gregs.cpsr, newmode);
    world->hregs.gcpu.r14 = pc_save;
    world->gregs.cpsr &= (~0x1f);
    world->gregs.cpsr |= newmode;
    world->gregs.cpsr |= H_CPSR_I;
    V_VERBOSE("new cpsr %x", world->gregs.cpsr);
    flush_cache_all();
}


void
h_do_fail_inst(struct v_world *world, unsigned long ip)
{
    void *virt;
    struct v_page *mpage;
    g_addr_t phys;
    unsigned int inst, cond;

    if ((ip & 0x3) != 0) {
        V_ERR("Guest misaligned ip");
        world->status = VM_PAUSED;
        return;
    }
    phys = g_v2p(world, ip, 1);
    mpage = h_p2mp(world, phys);
    virt = v_page_make_present(mpage);
    inst = *(unsigned int *) ((unsigned int) virt + (ip & H_POFF_MASK));
    V_VERBOSE("inst is %x", inst);
    cond = (inst & 0xf0000000) >> 28;
    if (cond != 0x0e) {
        int cpsr = world->hregs.gcpu.cpsr;
        int satisfied = 0;
        int N = (cpsr & H_CPSR_N) ? 1 : 0;
        int V = (cpsr & H_CPSR_V) ? 1 : 0;
        int Z = (cpsr & H_CPSR_Z) ? 1 : 0;
        int C = (cpsr & H_CPSR_C) ? 1 : 0;
        switch (cond) {
        case 0:                //eq
            satisfied = Z;
            break;
        case 1:                //ne
            satisfied = !Z;
            break;
        case 2:                //cs
            satisfied = C;
            break;
        case 3:                //cc
            satisfied = !C;
            break;
        case 4:                //mi
            satisfied = N;
            break;
        case 5:                //pl
            satisfied = !N;
            break;
        case 6:                //vs
            satisfied = V;
            break;
        case 7:                //vc
            satisfied = !V;
            break;
        case 8:                //hi
            satisfied = C && !Z;
            break;
        case 9:                //ls
            satisfied = Z && !C;
            break;
        case 10:               //ge
            satisfied = N == V;
            break;
        case 11:               //lt
            satisfied = N != V;
            break;
        case 12:               //gt
            satisfied = !Z && (N == V);
            break;
        case 13:               //le
            satisfied = Z && (N != V);
            break;
        case 15:               //nv
            if ((inst & 0xfff1fe00) == 0xf1000000) {
                satisfied = 1;
                break;
            }
            V_ERR("Cond NV encountered");
            world->status = VM_PAUSED;
            break;
        default:
            break;
        }
        if (!satisfied) {
            goto processed;
        }
    }
    if ((inst & 0xfb00000) == 0x3200000) {
        int mask = (inst & 0xf0000) >> 16;
        int number = (inst & 0xff) << ((inst & 0xf00) >> 8);
        int R = (inst & 0x400000);
        V_LOG("MSR %x, %x", mask, number);
        if (R) {
            if (mask != 0xf) {
                world->status = VM_PAUSED;
            }
            world->gregs.spsr[world->gregs.cpsr & 0xf] = number;
        } else if (mask != 0) {
            if (mask != 1) {
                world->status = VM_PAUSED;
            }
            V_VERBOSE("Mode switch to %x", number);
            h_switch_stack(world, world->gregs.cpsr, number);
            world->gregs.cpsr = number;
        }
    } else if ((inst & 0xfb00ff0) == 0x1200000) {
        int mask = (inst & 0xf0000) >> 16;
        int *reg = &world->hregs.gcpu.r0;
        int R = (inst & 0x400000);
        reg = reg + (inst & 0xf);
        V_LOG("MSR %x, %x", mask, *reg);
        if (R) {
            if (mask != 0xf) {
                world->status = VM_PAUSED;
            }
            world->gregs.spsr[world->gregs.cpsr & 0xf] = *reg;
        } else if (mask != 0) {
            if (mask != 1) {
                world->status = VM_PAUSED;
            }
            V_VERBOSE("Mode switch to %x", *reg);
            h_switch_stack(world, world->gregs.cpsr, *reg);
            world->gregs.cpsr = *reg;
        }
    } else if ((inst & 0xc10f000) == 0x10f000) {
        int op = (inst & 0x1e00000) >> 21;
        int *reg = &world->hregs.gcpu.r0;
        unsigned int spsr;
        unsigned int newpc;
        reg = reg + (inst & 0xf);
        newpc = *reg;
#ifdef USERMODE_DEBUG
        if (usermode_debug) {
            V_ERR("opS %x, %x", op, *reg);
            world->status = VM_PAUSED;
        }
#endif
        V_LOG("opS %x, %x", op, *reg);
        if (op != 0x0d) {
            V_ERR("not implemented opS");
            world->status = VM_PAUSED;
            return;
        }
        spsr = world->gregs.spsr[world->gregs.cpsr & 0xf];
        V_VERBOSE("Mode switch to %x", spsr);
        h_switch_stack(world, world->gregs.cpsr, spsr);
        world->gregs.cpsr = spsr;
        world->hregs.gcpu.cpsr &= 0x1ff;
        world->hregs.gcpu.cpsr |= (world->gregs.cpsr & 0xfffffe00);
        world->hregs.gcpu.pc = newpc;
#ifdef USERMODE_DEBUG
        if (usermode_debug) {
            V_ERR("Mode switch to %x, with cpsr %x", spsr,
                world->hregs.gcpu.cpsr);
        }
#endif
        v_bt_reset(world);
        return;
    } else if ((inst & 0xe400000) == 0x8400000) {
        int P = inst & 0x1000000;
        int U = inst & 0x0800000;
        int W = inst & 0x0200000;
        int D = inst & 0x0100000;
        unsigned int *base = &world->hregs.gcpu.r0;
        int adjustment = 0;
        base = base + ((inst & 0xf0000) >> 16);
#ifdef USERMODE_DEBUG
        if (usermode_debug) {
            V_ERR("%sm %c%c%c, %x from %x", D ? "ld" : "st", P ? 'd' : 'i',
                U ? 'a' : 'b', W ? ' ' : '!', inst & 0xffff,
                (inst & 0xf0000) >> 16);
        }
#endif
        V_LOG("%sm %c%c%c, %x from %x", D ? "ld" : "st", P ? 'd' : 'i',
            U ? 'a' : 'b', W ? ' ' : '!', inst & 0xffff,
            (inst & 0xf0000) >> 16);
        if (!(!P && U)) {
            if (P && !U) {
                int r;
                int count = 0;
                for (r = 0; r <= 15; r++) {
                    if (inst & (1 << r))
                        count++;
                }
                adjustment = -count * 4;
            } else {
                V_ERR("ldm %c%c%c, %x from %x", P ? 'd' : 'i', U ? 'a' : 'b',
                    W ? ' ' : '!', inst & 0xffff, (inst & 0xf0000) >> 16);
                world->status = VM_PAUSED;
            }
        }
        {
            unsigned int *reg = &world->hregs.gcpu.r0;
            int r;
            unsigned int addr = *base;
            addr += adjustment;
            for (r = 0; r < 15; r++) {
                // skip to usr_r13 if pc is restored (ldm3)
                if (!(inst & 0x8000) && r == 13)
                    reg = &world->gregs.r13_modes[0];
                if (!(inst & 0x8000) && r == 14)
                    reg = &world->gregs.r14_modes[0];
                if (inst & (1 << r)) {
                    if (D) {
                        h_read_guest(world, addr, reg);
                    } else {
                        h_write_guest(world, addr, *reg);
                    }
#ifdef USERMODE_DEBUG
                    if (usermode_debug) {
                        V_ERR("ldm/stm reg %x from %x value %x", r, addr, *reg);
                    }
#endif
                    V_LOG("ldm/stm reg %x from %x value %x", r, addr, *reg);
                    addr += 4;
                }
                reg++;
            }
            if (inst & 0x8000) {
                unsigned int newmode;
                newmode = world->gregs.spsr[world->gregs.cpsr & 0xf];
                h_switch_stack(world, world->gregs.cpsr, newmode);
                world->gregs.cpsr = newmode;
                world->hregs.gcpu.cpsr &= 0x1ff;
                world->hregs.gcpu.cpsr |= (world->gregs.cpsr & 0xfffffe00);
                h_read_guest(world, addr, &world->hregs.gcpu.pc);
#ifdef USERMODE_DEBUG
                if (usermode_debug) {
                    V_ERR("new mode %x @ %x", newmode, world->hregs.gcpu.pc);
                }
#endif
                V_EVENT("new mode %x @ %x", newmode, world->hregs.gcpu.pc);
                v_bt_reset(world);
//                world->find_poi = 1;
                return;
            }
        }
    } else if ((inst & 0xfff1fe00) == 0xf1000000) {
        int imodM = (inst & 0xf0000) >> 16;
        V_LOG("cps %x, %x", imodM, inst & 0x1ff);
        if ((imodM & 8) == 8) {
            int bitResult = imodM & 4;
            int iA = inst & 0x80;
            int fA = inst & 0x40;
            if (imodM & 2) {
                int mode = inst & 0xf;
                V_VERBOSE("Mode switch to %x", mode);
                h_switch_stack(world, world->gregs.cpsr, mode);
                world->gregs.cpsr = mode;
            }
            if (bitResult) {
                if (iA)
                    world->gregs.cpsr |= H_CPSR_I;
                if (fA)
                    world->gregs.cpsr |= H_CPSR_F;
            } else {
                if (iA)
                    world->gregs.cpsr &= (~H_CPSR_I);
                if (fA)
                    world->gregs.cpsr &= (~H_CPSR_F);
            }
        }
    } else if ((inst & 0xfb00ff0) == 0x1000090) {
/*
        int *reg1 = &world->hregs.gcpu.r0;
        int *reg2 = &world->hregs.gcpu.r0;
        int *reg3 = &world->hregs.gcpu.r0;

        int R = (inst & 0x400000);
        reg1 = reg1 + ((inst & 0xf0000) >> 16);
        reg2 = reg2 + ((inst & 0xf000) >> 12);
        reg3 = reg3 + (inst & 0xf);
        V_LOG("swp %x, %x %x %x", R, *reg1, *reg2, *reg3);
*/
        world->status = VM_PAUSED;
    } else if ((inst & 0xfb00fff) == 0x1000000) {
        int rd = (inst & 0xf000) >> 12;
        unsigned int *reg = &world->hregs.gcpu.r0;
        int R = (inst & 0x400000);
        if (R) {
            *(reg + rd) = world->gregs.spsr[world->gregs.cpsr & 0xf];
#ifdef USERMODE_DEBUG
            if (usermode_debug) {
                V_ERR("MRS(s) %x = %x, %x", rd, *(reg + rd),
                    world->gregs.spsr[world->gregs.cpsr & 0xf]);
            }
#endif
            V_LOG("MRS(s) %x = %x, %x", rd, *(reg + rd),
                world->gregs.spsr[world->gregs.cpsr & 0xf]);
        } else {
            /* we need T bit, and highest 5 status bits */
            *(reg + rd) =
                (world->gregs.cpsr & 0x1ff) | (world->hregs.
                gcpu.cpsr & 0xf8000000);
            V_LOG("MRS %x = %x, %x", rd, *(reg + rd), world->gregs.cpsr);
        }
    } else if ((inst & 0xf000010) == 0xe000010) {
        int dir = inst & 0x100000;
        int opc1 = (inst & 0xe00000) >> 21;
        int opc2 = (inst & 0xe0) >> 5;
        int crn = (inst & 0xf0000) >> 16;
        int crm = inst & 0xf;
        int rd = (inst & 0xf000) >> 12;
        int reg = (inst & 0xf00) >> 8;
        unsigned int *rdreg = &world->hregs.gcpu.r0;
        if (crn == 7 || crn == 8) {     /* cache op, don't annoy us with the output */
            V_VERBOSE("%s %x, %x, R%x, c%x, c%x, %x", dir ? "MRC" : "MCR", reg,
                opc1, rd, crn, crm, opc2);
        } else {
            V_VERBOSE("%s %x, %x, R%x, c%x, c%x, %x", dir ? "MRC" : "MCR", reg,
                opc1, rd, crn, crm, opc2);
        }
        rdreg = rdreg + rd;
        switch (reg) {
        case 15:
            switch (crn) {
            case 0:
                // id register
                if (dir) {
                    V_EVENT("Id reg %x", *rdreg);
                    if (crm == opc2 && opc2 == opc1 && opc1 == 0) {
                        //main id
//                        int id;
//                        asm volatile ("mrc p15, 0, %0, c0, c0, 0":"=r"(id));
                        *rdreg = 0x410f9260;
                        goto processed;
                    }
                    if (crm == 0 && opc2 == 1 && opc1 == 0) {
                        //cache type reg
                        int id;
                        asm volatile ("mrc p15, 0, %0, c0, c0, 1":"=r" (id));
                        *rdreg = id;
                        goto processed;
                    }
                    if (crm == 1 && opc2 == 4 && opc1 == 0) {
                        int id;
                        asm volatile ("mrc p15, 0, %0, c0, c1, 4":"=r" (id));
                        *rdreg = id;
                        goto processed;
                    }
                    if (crm == 1 && opc2 == 5 && opc1 == 0) {
                        int id;
                        asm volatile ("mrc p15, 0, %0, c0, c1, 5":"=r" (id));
                        *rdreg = id;
                        goto processed;
                    }
                    if (crm == 0 && opc2 == 5 && opc1 == 0) {
                        //MPreg, return 0
                        *rdreg = 0;
                        goto processed;
                    }
                    if (crm == 0 && opc2 == 1 && opc1 == 1) {
                        //cache level id
                        int id;
                        asm volatile ("mrc p15, 1, %0, c0, c0, 1":"=r" (id));
                        *rdreg = id;
                        goto processed;
                    }
                    if (crm == 0 && opc2 == 0 && opc1 == 1) {
                        //cache size id
                        int id;
                        asm volatile ("mrc p15, 1, %0, c0, c0, 0":"=r" (id));
                        *rdreg = id;
                        goto processed;
                    }
                    if (crm == 0 && opc2 == 0 && opc1 == 2) {
                        //cache size selection
                        int id;
                        asm volatile ("mrc p15, 2, %0, c0, c0, 0":"=r" (id));
                        *rdreg = id;
                        goto processed;
                    }
                } else {
                    if (crm == 0 && opc2 == 0 && opc1 == 2) {
                        //cache size selection
                        goto processed;
                    }
                }
                break;
            case 1:
                //control register
                if (crm == opc2 && opc2 == opc1 && opc1 == 0) {
                    V_EVENT("Control reg %x", *rdreg);
                    if (dir) {
                        *rdreg = world->gregs.p15_ctrl;
                    } else {
                        int old = world->gregs.p15_ctrl;
                        world->gregs.p15_ctrl = *rdreg;
                        if ((world->gregs.p15_ctrl & P15_CTRL_M)
                            && !(old & P15_CTRL_M)) {
                            V_EVENT("Paging enabled");
                            world->gregs.mode = G_MODE_MMU;
                            h_new_trbase(world);
                            v_bt_reset(world);
                        }
                        if (!(world->gregs.p15_ctrl & P15_CTRL_M)
                            && (old & P15_CTRL_M)) {
                            V_EVENT("Paging disabled");
                            world->spt_list = NULL;
                            world->gregs.mode = G_MODE_NO_MMU;
                            h_new_trbase(world);
                            v_bt_reset(world);
                        }
                    }
                    goto processed;
                }
                goto processed;
                break;
            case 2:
                //trbase
                if (crm == opc2 && opc2 == opc1 && opc1 == 0) {
                    V_EVENT("trbase %x", *rdreg);
                    if (dir) {
                        *rdreg = world->gregs.p15_trbase;
                    } else {
                        int old = world->gregs.p15_trbase;
                        world->gregs.p15_trbase = *rdreg;
                        if (world->gregs.p15_trbase != old
                            && world->gregs.mode == G_MODE_MMU) {
                            h_new_trbase(world);
                            v_bt_reset(world);
                        }
                    }
                    goto processed;
                }
                goto processed;
                break;
            case 3:
                //domain control
                V_EVENT("Domain control %x", *rdreg);
                if (crm == opc2 && opc2 == opc1 && opc1 == 0) {
                    V_EVENT("domain control %x", *rdreg);
                    if (dir) {
                        *rdreg = world->gregs.p15_domain;
                    } else {
                        world->gregs.p15_domain = *rdreg;
                    }
                    goto processed;
                }
                break;
                break;
            case 5:
                V_EVENT("FSR");
                if (crm == 0 && opc2 == 0 && opc1 == 0) {
                    V_EVENT("DFSR %x", *rdreg);
                    if (dir) {
                        *rdreg = world->gregs.p15_dfsr;
                    } else {
                        world->gregs.p15_dfsr = *rdreg;
                    }
                    goto processed;
                }
                if (crm == 0 && opc2 == 1 && opc1 == 0) {
                    V_EVENT("IFSR %x", *rdreg);
                    if (dir) {
                        *rdreg = world->gregs.p15_ifsr;
                    } else {
                        world->gregs.p15_ifsr = *rdreg;
                    }
                    goto processed;
                }
                break;
            case 6:
                V_EVENT("FAR");
                if (crm == 0 && opc2 == 0 && opc1 == 0) {
                    V_EVENT("DFAR %x", *rdreg);
                    if (dir) {
                        *rdreg = world->gregs.p15_dfar;
                    } else {
                        world->gregs.p15_dfar = *rdreg;
                    }
                    goto processed;
                }
                if (crm == 0 && opc2 == 2 && opc1 == 0) {
                    V_EVENT("IFAR %x", *rdreg);
                    if (dir) {
                        *rdreg = world->gregs.p15_ifar;
                    } else {
                        world->gregs.p15_ifar = *rdreg;
                    }
                    goto processed;
                }
                break;
            case 7:
                h_perf_inc(H_PERF_CACHE, 1);
                flush_cache_all();
                //cache op
                if (crm == 0x0e && opc1 == 0 && opc2 == 2) {
                    goto processed;
                }
                if (crm == 0x0e && opc1 == 1 && opc2 == 0) {
                    goto processed;
                }
                if (crm == 5 && opc1 == 0 && opc2 == 0) {
                    goto processed;
                }
                if (crm == 0x0a && opc1 == 0 && opc2 == 1) {
                    goto processed;
                }
                goto processed;
                break;
            case 8:
                h_perf_inc(H_PERF_TLB, 1);
                //cache op
                if (crm == 0x7 && opc1 == 0 && opc2 == 0) {
                    goto processed;
                }
                goto processed;
                break;
            case 10:
                //TEX remap and TLB lockdown. just ignore
                goto processed;
                break;
            case 13:
                //FCSE. just ignore
                goto processed;
                break;
            default:
                V_ERR("Not handled cp15");
            }

        default:
            V_ERR("Not handled cp%d", reg);
            world->status = VM_PAUSED;
        }
    } else {
        world->status = VM_PAUSED;
        V_ERR("Unknown instructions %x", inst);
    }
  processed:
    world->hregs.gcpu.pc += 4;
}
