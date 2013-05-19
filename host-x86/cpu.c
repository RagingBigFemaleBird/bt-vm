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
#include "host/include/mm.h"
#include "host/include/bt.h"
#include "vm/include/world.h"
#include "host/include/cpu.h"
#include "vm/include/mm.h"
#include "vm/include/bt.h"
#include "guest/include/cpu.h"
#include "guest/include/mm.h"
#include "vm/include/logging.h"
#include "guest/include/dev/fb.h"
#include "vm/include/perf.h"
#include "host/include/perf.h"
#include "guest/include/bt.h"

//#define DEBUG_CODECONTROL

extern volatile int step;
extern volatile int time_up;
struct h_cpu hostcpu;
#define QUICKPATH_GP_ENABLED

static void h_do_int(unsigned int int_no);

unsigned int bpaddr = /*0x8100; */ 0;   //0xc022e294;     //0x226255;
unsigned int bp_reached = 0;
#ifdef BT_CACHE
static int cache_offset;
void bt_cache_start(void);
#endif
#ifdef QUICKPATH_GP_ENABLED
void sensitive_instruction_cache(void);
static int sensitive_instruction_cache_offset;
#endif
void h_switcher(unsigned long trbase, struct v_world *w);

int
h_cpu_init(void)
{
    int wd_size = sizeof(struct v_world);
    V_ERR("World data size = %x", wd_size);
    if (wd_size > H_PAGE_SIZE) {
        V_ERR("Stop: world data size too large.");
        return 1;
    }
    asm volatile ("movl %%cr0, %0":"=r" (hostcpu.cr0):);
    V_LOG("CR0: %x\n", hostcpu.cr0);
    asm volatile ("movl %%cr3, %0":"=r" (hostcpu.cr3):);
    V_LOG("CR3: %x\n", hostcpu.cr3);
    asm volatile ("movl %%cr4, %0":"=r" (hostcpu.cr4):);
    V_LOG("CR4: %x\n", hostcpu.cr4);
    if (hostcpu.cr4 & H_CR4_PAE) {
        V_ERR("Unable to run on PAE enabled machine!");
        return 1;
    }
    if (hostcpu.cr4 & H_CR4_PGE) {
        V_ERR("Disabling global pages...");
        hostcpu.cr4 &= ~(H_CR4_PGE);
        asm volatile ("movl %0, %%cr4"::"r" (hostcpu.cr4));
    }
    V_LOG("TSC check, try 1: %llx", h_perf_tsc_read());
    V_LOG("TSC check, try 2: %llx", h_perf_tsc_read());
    V_LOG("TSC check, try 3: %llx", h_perf_tsc_read());
    asm volatile ("sgdt (%0)"::"r" (&hostcpu.gdt):"memory");
    V_LOG("GDT Base %x, limit %x\n", hostcpu.gdt.base, hostcpu.gdt.limit);
    asm volatile ("sidt (%0)"::"r" (&hostcpu.idt):"memory");
    V_LOG("IDT Base %x, limit %x\n", hostcpu.idt.base, hostcpu.idt.limit);
    asm volatile ("sldt %0":"=r" (hostcpu.ldt)::"memory");
    V_LOG("LDT selector = %x\n", hostcpu.ldt);
    asm volatile ("str %0":"=r" (hostcpu.tr)::"memory");
    V_LOG("TR selector = %x\n", hostcpu.tr);
    asm volatile ("push %cs");
    asm volatile ("pop %0":"=r" (hostcpu.cs));
    asm volatile ("push %ss");
    asm volatile ("pop %0":"=r" (hostcpu.ss));
    asm volatile ("pushf");
    asm volatile ("pop %0":"=r" (hostcpu.eflags));
    asm volatile ("movl $restoreCS, %0":"=r" (hostcpu.eip));
#ifdef BT_CACHE
    cache_offset = ((void *) bt_cache_start) - ((void *) h_switcher);
#endif
#ifdef QUICKPATH_GP_ENABLED
    sensitive_instruction_cache_offset =
        ((void *) sensitive_instruction_cache) - ((void *) h_switcher);
#endif
    return 0;
}

void
h_cpu_save(struct v_world *w)
{
    struct h_regs *h = &w->hregs;
    asm volatile ("movl %%cr0, %0":"=r" (h->hcpu.cr0):);
    asm volatile ("movl %%cr3, %0":"=r" (h->hcpu.cr3):);
    asm volatile ("sldt %0":"=r" (h->hcpu.ldt)::"memory");
    asm volatile ("str %0":"=r" (h->hcpu.tr)::"memory");
    asm volatile ("push %cs");
    asm volatile ("pop %0":"=r" (h->hcpu.cs));
    asm volatile ("push %ss");
    asm volatile ("pop %0":"=r" (h->hcpu.ss));
    asm volatile ("pushf");
    asm volatile ("pop %0":"=r" (h->hcpu.eflags));
/*    if (!bp_reached)
        h_set_bp(w, bpaddr, 3);*/
}

#ifdef BT_CACHE

#define __TOTAL 0
#define __SET 4
#define __PB_TOTAL 8
#define __PB_SET 12
#define __PB_START 16
#define __PB_ENTRIES (2 * BT_CACHE_CAPACITY)
#define __CB_START (__PB_START + __PB_ENTRIES * 8)

void
h_bt_squash_pb(struct v_world *world)
{
    void *cache = world->hregs.hcpu.switcher;
    cache += cache_offset;
    *((unsigned int *) (cache + __PB_TOTAL)) = 0;
}

void
h_bt_cache_restore(struct v_world *world)
{
    void *cache = world->hregs.hcpu.switcher;
    int total, set, dr7, pb_total, pb_set;
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
        if (world->poi->type & V_INST_CB) {
            world->current_valid_bps = world->poi->plan.count;
            dr7 = world->hregs.gcpu.dr7 & 0xffffff00;
            for (set = 0; set < world->poi->plan.count; set++) {
                unsigned int *bpreg = &world->hregs.gcpu.dr0;
                dr7 |= (0x700 | (3 << (set * 2)));
                world->bp_to_poi[set] = world->poi->plan.poi[set];
                bpreg[set] = world->bp_to_poi[set]->addr;
            }
            world->hregs.gcpu.dr7 = dr7;
        } else {
            world->current_valid_bps = 1;
            world->hregs.gcpu.dr7 = world->hregs.gcpu.dr7 & 0xffffff00;
            world->hregs.gcpu.dr7 = world->hregs.gcpu.dr7 | 0x703;
            world->bp_to_poi[0] = world->poi;
            world->hregs.gcpu.dr0 = world->poi->addr;
        }
    } else if (pb_total != 0 && pb_set != 0) {
        h_pb_cache = (struct h_bt_pb_cache *) (cache + __PB_START);
        world->poi = h_pb_cache[pb_total - pb_set].poi;
        world->poi->expect = 1;
        V_VERBOSE("BT restore pb poi to %lx", world->poi->addr);
        world->hregs.gcpu.dr7 &= 0xffffff00;
    }
    *((unsigned int *) (cache + __SET)) = 0;
    *((unsigned int *) (cache + __PB_SET)) = 0;
}

#define TOTAL_BT_CACHED_AREA_SIZE (16 + 8 * __PB_ENTRIES + 28 * BT_CACHE_CAPACITY)
void
h_bt_cache_direct(struct v_world *world,
    struct v_poi_cached_tree_plan_container *cont)
{
    void *cache = world->hregs.hcpu.switcher;
    cache += cache_offset;
    h_memcpy(cache, cont->exec_cache, TOTAL_BT_CACHED_AREA_SIZE);
}

void
h_bt_exec_cache(struct v_world *world,
    struct v_poi_cached_tree_plan_container *cont)
{
    void *cache = world->hregs.hcpu.switcher;
    cache += cache_offset;
    if (cont->exec_cache == NULL)
        cont->exec_cache = h_raw_malloc(TOTAL_BT_CACHED_AREA_SIZE);
    h_memcpy(cont->exec_cache, cache, TOTAL_BT_CACHED_AREA_SIZE);
}

void
h_bt_cache(struct v_world *world, struct v_poi_cached_tree_plan *plan,
    int count)
{
    void *cache = world->hregs.hcpu.switcher;
    struct h_bt_cache *hcache;
    struct h_bt_pb_cache *pb_cache;
    int i;
    int pb_total = 0;
    cache += cache_offset;
    *((unsigned int *) cache) = count;
    *((unsigned int *) (cache + __SET)) = 0;
    *((unsigned int *) (cache + __PB_TOTAL)) = 0;
    *((unsigned int *) (cache + __PB_SET)) = 0;
    if (plan == NULL)
        return;
    pb_cache = (struct h_bt_pb_cache *) (cache + __PB_START);
    V_VERBOSE("Cache total %x", count);
    if (count != 0) {
        hcache = (struct h_bt_cache *) (cache + __CB_START);
        for (i = 0; i < count; i++) {
            int j = 0;
            hcache[i].poi = plan[i].poi;
            hcache[i].addr = plan[i].addr;
            hcache[i].dr7 = world->hregs.gcpu.dr7 & (0xffffff00);
            if (!(plan[i].poi->type & V_INST_CB)) {
                unsigned int *bp = &hcache[i].dr0;
                hcache[i].dr7 |= (0x700 | (3 << (j * 2)));
                *(bp + j) = plan[i].poi->addr;
                if ((plan[i].poi->type & V_INST_PB)
                    && pb_total <= 2 * BT_CACHE_CAPACITY) {
                    int k;
                    for (k = 0; k < pb_total; k++) {
                        if (pb_cache[k].addr == plan[i].poi->addr) {
                            goto found2;
                        }
                    }
                    pb_cache[pb_total].addr = plan[i].poi->addr;
                    pb_cache[pb_total].poi = plan[i].poi;
                    V_VERBOSE("cache %x is %lx", pb_total,
                        pb_cache[pb_total].addr);
                    pb_total++;
                  found2:
                    asm volatile ("nop");
                }
                if (plan[i].poi->addr == plan[i].addr) {
                    hcache[i].addr = 0x0;
                }
            } else {
                for (j = 0; j < plan[i].plan->count; j++) {
                    unsigned int *bp = &hcache[i].dr0;
                    hcache[i].dr7 |= (0x700 | (3 << (j * 2)));
                    *(bp + j) = plan[i].plan->poi[j]->addr;
                    v_validate_guest_virt(world, plan[i].plan->poi[j]->addr);
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
                      found:
                        asm volatile ("nop");
                    }
                    V_VERBOSE("bp %x is %x", j, *(bp + j));
                }
                V_VERBOSE("dr7 is %x", hcache[i].dr7);
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
#define CACHE_BT_CACHE(cache_capacity) \
    asm volatile ("9:"); \
    asm volatile ("push $0xbeef"); \
    asm volatile ("sub $4, %esp"); \
    asm volatile ("pusha"); \
    asm volatile ("call 10f"); \
    asm volatile (".global bt_cache_start"); \
    asm volatile ("bt_cache_start:"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".long 0"); \
    asm volatile (".rept 2 *"STRINGIFY(cache_capacity)); \
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
    asm volatile (".endr"); \
    asm volatile ("10:"); \
    asm volatile ("pop %eax"); \
    asm volatile ("mov %cs:(%eax), %edx"); \
    asm volatile ("mov %cs:8(%eax), %ecx"); \
    asm volatile ("mov %edx, %ebx"); \
    asm volatile ("or %ecx, %ebx"); \
    asm volatile ("je 98f"); \
    asm volatile ("mov %dr6, %ecx"); \
    asm volatile ("mov %ecx, %edi"); /* KEEP EDI ALL THE WAY */\
    asm volatile ("and $0x4000, %edi"); \
    asm volatile ("test $0x4000, %ecx"); \
    asm volatile ("jz 60f"); \
    asm volatile ("mov %ss:52(%esp), %ecx"); \
    asm volatile ("and $0xfffefeff, %ecx"); \
    asm volatile ("mov %ecx, %ss:52(%esp)"); \
    asm volatile ("mov %ss:44(%esp), %ecx"); \
    asm volatile ("jmp 24f"); \
    asm volatile ("60:"); \
    asm volatile ("test $1, %ecx"); \
    asm volatile ("je 21f"); \
    asm volatile ("mov %dr0, %ecx"); \
    asm volatile ("jmp 24f"); \
    asm volatile ("21:"); \
    asm volatile ("test $2, %ecx"); \
    asm volatile ("je 22f"); \
    asm volatile ("mov %dr1, %ecx"); \
    asm volatile ("jmp 24f"); \
    asm volatile ("22:"); \
    asm volatile ("test $4, %ecx"); \
    asm volatile ("je 23f"); \
    asm volatile ("mov %dr2, %ecx"); \
    asm volatile ("jmp 24f"); \
    asm volatile ("23:"); \
    asm volatile ("test $8, %ecx"); \
    asm volatile ("jne 99f"); /*this should never happen*/\
    asm volatile ("mov %dr3, %ecx"); \
    asm volatile ("24:"); \
    asm volatile ("mov %eax, %ebx"); /*edx must be preserved all the way here*/\
    asm volatile ("add $8, %ebx"); /* __PB_TOTAL*/\
    asm volatile ("mov %cs:(%ebx), %esi"); \
    asm volatile ("cmp $0, %esi"); \
    asm volatile ("je 40f"); \
    asm volatile ("41:"); \
    asm volatile ("add $8, %ebx"); /* __PB_START*/\
    asm volatile ("cmp %cs:(%ebx), %ecx"); \
    asm volatile ("je 50f"); \
    asm volatile ("dec %esi"); \
    asm volatile ("jnz 41b"); \
    asm volatile ("40:"); \
    asm volatile ("test %edx, %edx"); \
    asm volatile ("jz 99f"); \
    asm volatile ("mov %eax, %ebx"); /*edx must be preserved all the way here*/\
    asm volatile ("add $("STRINGIFY(cache_capacity)"*16 + 16), %ebx"); /* __CB_START*/\
    asm volatile ("20:"); \
    asm volatile ("mov %cs:(%ebx), %esi"); \
    asm volatile ("cmp %esi, %ecx"); \
    asm volatile ("je 30f"); \
    asm volatile ("add $28, %ebx"); \
    asm volatile ("dec %edx"); \
    asm volatile ("jnz 20b"); \
    asm volatile ("jmp 99f"); \
    asm volatile ("30:"); \
    asm volatile ("mov %edx, %ss:4(%eax)"); \
    asm volatile ("xor %edx, %edx"); \
    asm volatile ("mov %edx, %ss:12(%eax)"); \
    asm volatile ("mov %cs:4(%ebx), %eax"); \
    asm volatile ("mov %eax, %dr7"); \
    asm volatile ("mov %cs:8(%ebx), %eax"); \
    asm volatile ("mov %eax, %dr0"); \
    asm volatile ("mov %cs:12(%ebx), %eax"); \
    asm volatile ("mov %eax, %dr1"); \
    asm volatile ("mov %cs:16(%ebx), %eax"); \
    asm volatile ("mov %eax, %dr2"); \
    asm volatile ("mov %cs:20(%ebx), %eax"); \
    asm volatile ("mov %eax, %dr3"); \
    asm volatile ("mov %dr6, %eax"); \
    asm volatile ("and $0xffff0ff0, %eax"); \
    asm volatile ("mov %eax, %dr6"); \
    asm volatile ("popa"); \
    asm volatile ("add $12, %esp"); \
    asm volatile ("iret"); \
    asm volatile ("50:"); \
    asm volatile ("mov %esi, %ss:12(%eax)"); \
    asm volatile ("xor %esi, %esi"); \
    asm volatile ("mov %esi, %ss:4(%eax)"); \
    asm volatile ("mov %dr7, %eax"); \
    asm volatile ("and $0xffffff00, %eax"); \
    asm volatile ("mov %eax, %dr7"); \
    asm volatile ("mov %dr6, %eax"); \
    asm volatile ("and $0xffff0ff0, %eax"); \
    asm volatile ("mov %eax, %dr6"); \
    asm volatile ("mov %ss:52(%esp), %eax"); /* 13 * 4 = eflags position */ \
    asm volatile ("or $0x10100, %eax"); /* TF | RF */ \
    asm volatile ("mov %eax, %ss:52(%esp)"); \
    asm volatile ("popa"); \
    asm volatile ("add $12, %esp"); \
    asm volatile ("iret"); \
    asm volatile ("98:"); \
    asm volatile ("mov %ss:44(%esp), %ecx"); \
    asm volatile ("cmp $0xc0108715, %ecx"); \
    asm volatile ("je 200f"); \
    asm volatile ("jmp 8b"); \
    asm volatile ("99:"); \
    asm volatile ("mov %ss:44(%esp), %ecx"); \
    asm volatile ("cmp $0xc0108715, %ecx"); \
    asm volatile ("je 200f"); \
    asm volatile ("test %edi, %edi"); \
    asm volatile ("jnz 8b"); \
    asm volatile ("jmp 200f");
#else
#define CACHE_BT_CACHE(cache_capacity)
#endif

#ifdef BT_CACHE
#define CACHE_BT_QUICKPATH \
    asm volatile ("cmp $(~0x1 + 0x80), (%esp)");\
    asm volatile ("je 9f");
#else
#define CACHE_BT_QUICKPATH
#endif

#ifdef QUICKPATH_GP_ENABLED
#define GP_FAULT_QUICKPATH \
    asm volatile ("cmp $(~0xd + 0x80), (%esp)");\
    asm volatile ("je 100f");
#else
#define GP_FAULT_QUICKPATH
#endif

#ifdef QUICKPATH_GP_ENABLED
#define GP_FAULT_QUICKPATH_HANDLER \
    asm volatile ("100:"); \
    asm volatile ("sub $4, %esp"); \
    asm volatile ("pusha"); \
    asm volatile ("200:"); \
    asm volatile ("call 110f"); \
    asm volatile (".global sensitive_instruction_cache"); \
    asm volatile ("sensitive_instruction_cache:"); \
    asm volatile (".long 0"); /*control flag */\
    asm volatile (".long 0"); /* rf */\
    asm volatile (".long 0"); /*ring*/\
    asm volatile (".long 0"); /*nt*/\
    asm volatile (".long 0"); /*iopl*/\
    asm volatile (".long 0"); /*int*/\
    asm volatile (".long 0"); /*fast iret allowed*/ \
    asm volatile ("110:"); \
    asm volatile ("pop %eax"); \
    asm volatile ("mov %cs:(%eax), %ecx"); /* eax points to control flag */\
    asm volatile ("cmp $0, %ecx"); \
    asm volatile ("je 111f"); \
    asm volatile ("mov %ss:44(%esp), %ebx"); /*ebx is eip */\
    asm volatile ("mov %ss:(%ebx), %cl"); \
    asm volatile ("cmp $0xfa, %cl"); \
    asm volatile ("jne 120f"); \
    asm volatile ("movl $0x2, %ss:(%eax)"); \
    asm volatile ("movl $0x0, %ss:20(%eax)"); \
    asm volatile ("inc %ebx"); \
    asm volatile ("mov %ebx, %ss:44(%esp)"); \
    asm volatile ("mov %ss:52(%esp), %ecx"); /* rf flag */\
    asm volatile ("and $0xfffeffff, %ecx"); \
    asm volatile ("mov %ecx, %ss:52(%esp)"); \
    asm volatile ("popa"); \
    asm volatile ("add $12, %esp"); \
    asm volatile ("iret"); \
    asm volatile ("jmp 130f"); \
    asm volatile ("120:"); \
    asm volatile ("cmp $0xfb, %cl"); \
    asm volatile ("jne 121f"); \
    asm volatile ("movl $0x2, %ss:(%eax)"); \
    asm volatile ("movl $0x1, %ss:20(%eax)"); \
    asm volatile ("inc %ebx"); \
    asm volatile ("mov %ebx, %ss:44(%esp)"); \
    asm volatile ("mov %ss:52(%esp), %ecx"); /* rf flag */\
    asm volatile ("and $0xfffeffff, %ecx"); \
    asm volatile ("mov %ecx, %ss:52(%esp)"); \
    asm volatile ("popa"); \
    asm volatile ("add $12, %esp"); \
    asm volatile ("iret"); \
    asm volatile ("jmp 130f"); \
    asm volatile ("121:"); \
    asm volatile ("mov %cs:24(%eax), %edx"); /* eax points to fast iret enable flag */\
    asm volatile ("cmp $0, %edx"); \
    asm volatile ("je 122f"); \
    asm volatile ("cmp $0xcf, %cl"); \
    asm volatile ("jne 122f"); \
    asm volatile ("mov %ss:56(%esp), %ebx"); /*get guest esp*/\
    asm volatile ("mov %ss:4(%ebx), %edi"); /*compare cs: must be (rpl3)*/\
    asm volatile ("and $3, %edi"); \
    asm volatile ("cmp $3, %edi"); \
    asm volatile ("jne 122f"); \
    /*note: we are not comparing ss, potential bug*/\
    asm volatile ("mov %ss:(%ebx), %edx"); \
    asm volatile ("mov %edx, %ss:44(%esp)"); \
    asm volatile ("mov %ss:4(%ebx), %edx"); \
    asm volatile ("mov %edx, %ss:48(%esp)"); \
    asm volatile ("mov %ss:12(%ebx), %edx"); \
    asm volatile ("mov %edx, %ss:56(%esp)"); \
    asm volatile ("mov %ss:16(%ebx), %edx"); \
    asm volatile ("or $3, %edx"); \
    asm volatile ("mov %edx, %ss:60(%esp)"); \
    asm volatile ("mov %ss:8(%ebx), %esi"); \
    asm volatile ("mov %esi, %edi"); \
    asm volatile ("shr $14, %esi"); \
    asm volatile ("and $1, %esi"); \
    asm volatile ("shr $12, %edi"); \
    asm volatile ("and $3, %edi"); \
    asm volatile ("130:"); \
    asm volatile ("movl $0x3, %ss:(%eax)"); \
    asm volatile ("movl $0x1, %ss:20(%eax)"); \
    asm volatile ("movl $0x3, %ss:8(%eax)"); \
    asm volatile ("movl %esi, %ss:12(%eax)"); \
    asm volatile ("movl %edi, %ss:16(%eax)"); \
    asm volatile ("popa"); \
    asm volatile ("add $12, %esp"); \
    asm volatile ("iret"); \
    asm volatile ("jmp 130f"); \
    asm volatile ("122:"); \
    asm volatile ("130:"); \
    asm volatile ("111:"); \
    asm volatile ("jmp 8b");
#else
#define GP_FAULT_QUICKPATH_HANDLER
#endif

#ifdef QUICKPATH_GP_ENABLED
void
h_gp_fault_quickpath_preamble(struct v_world *world)
{
    void *cache = world->hregs.hcpu.switcher;
    cache += sensitive_instruction_cache_offset;
    *((unsigned int *) (cache + 24)) = world->gregs.fast_iret_possible;
    if (world->gregs.ring == 0 && world->gregs.mode != G_MODE_REAL) {
        *((unsigned int *) (cache)) = 1;
        *((unsigned int *) (cache + 4)) = world->gregs.rf;
        *((unsigned int *) (cache + 20)) = v_int_enabled(world);
    } else {
        *((unsigned int *) (cache)) = 0;
        *((unsigned int *) (cache + 4)) = world->gregs.rf;
    }
}

void
  h_gp_fault_quickpath_postprocessing(struct v_world *world);

void
h_gp_fault_quickpath_postprocessing2(struct v_world *world)
{
    void *cache = world->hregs.hcpu.switcher;
    int result;
    cache += sensitive_instruction_cache_offset;
    result = *((unsigned int *) (cache));
    if (result == 2) {
        int interrupt = *((unsigned int *) (cache + 20));
        if (interrupt) {
            if (!(world->hregs.gcpu.eflags & H_EFLAGS_TF))
                g_pic_serve(world);
        }
    }
}
#endif

#ifdef QUICKPATH_GP_ENABLED
#define GP_FAULT_QUICKPATH_PREAMBLE \
    h_gp_fault_quickpath_preamble(w);
#else
#define GP_FAULT_QUICKPATH_PREAMBLE
#endif

#ifdef QUICKPATH_GP_ENABLED
#define GP_FAULT_QUICKPATH_POSTPROCESSING \
    h_gp_fault_quickpath_postprocessing(w);
#else
#define GP_FAULT_QUICKPATH_POSTPROCESSING
#endif

#ifdef QUICKPATH_GP_ENABLED
#define GP_FAULT_QUICKPATH_POSTPROCESSING2 \
    h_gp_fault_quickpath_postprocessing2(w);
#else
#define GP_FAULT_QUICKPATH_POSTPROCESSING2
#endif

#ifdef BT_CACHE
#define CACHE_BT_RESTORE \
    h_bt_cache_restore(w);
#else
#define CACHE_BT_RESTORE
#endif

/*todo: save fpu properly */

__attribute__ ((aligned (0x1000)))
void
h_switcher(unsigned long trbase, struct v_world *w)
{
    void monitor_log(struct v_world *mon_world, char c) {
        mon_world->monitor_buffer[mon_world->monitor_buffer_end] = c;
        mon_world->monitor_buffer_end++;
        if (mon_world->monitor_buffer_end >= MONITOR_BUFFER_MAX) {
            mon_world->monitor_buffer_end = 0;
        }
    }
    void *monitor_access(struct v_world *world, void *ptr) {
        int i;
        for (i = 0; i < world->pool_count; i++) {
            if (world->host_pools[i].virt <= (unsigned int) (ptr)
                && world->host_pools[i].virt + world->host_pools[i].total_size >
                (unsigned int) (ptr)) {
                return (void *) ((unsigned int) (ptr) -
                    world->host_pools[i].virt + world->host_pools[i].mon_virt);
            }
        }
        return NULL;
    }
    inline void monitor(void) {
        /* important:
         * do not initialize any var declared here: no stack yet
         * do not use 'break' anywhere, it breaks
         */
        struct v_world *mon_world;
        int dr;
        struct v_poi *poi, *poi_new, *temp, *temp2;
        unsigned int i, process;
        unsigned int addr;
        asm volatile ("mov %%esp, %0":"=r" (mon_world));
        asm volatile ("mov %esp, %ebp");
        mon_world =
            (struct v_world *) ((unsigned int) (mon_world) & H_PFN_MASK);
        poi = (struct v_poi *) monitor_access(mon_world, mon_world->poi);
//        monitor_log(mon_world, 't');
//        monitor_log(mon_world, '0' + poi->type);

        asm volatile ("mov %%dr6, %0":"=r" (dr));
        process = 0;
        if (!(dr & 0x4000)) {
            /* we are not single stepping */
            if (dr & 1) {
                poi_new = mon_world->bp_to_poi[0];
            } else if (dr & 2) {
                poi_new = mon_world->bp_to_poi[1];
            } else if (dr & 4) {
                poi_new = mon_world->bp_to_poi[2];
            } else if (dr & 8) {
                poi_new = mon_world->bp_to_poi[3];
            }
            process = 1;
//            monitor_log(mon_world, 'b');
        }
#ifdef V_POI_PB_CACHED_POI
        else {
            /* out of ss */
//            monitor_log(mon_world, 's');

            /* todo: assuming flat mem */
            for (i = 0; i < poi->pb_cache_poi.total; i++) {
                temp =
                    (struct v_poi *) monitor_access(mon_world,
                    poi->pb_cache_poi.targets[i]);
                if (temp->addr == mon_world->hregs.gcpu.eip) {
                    temp2 = NULL;
                    if (temp != NULL && (temp->type & V_INST_I)) {
                        while (temp != NULL && (temp->type & V_INST_I)) {
                            if (temp->next_inst == NULL) {
                                temp = NULL;
                            } else {
                                temp2 = temp->next_inst;
                                temp =
                                    (struct v_poi *) monitor_access(mon_world,
                                    temp->next_inst);
                            }
                        }
                    }
                    if (temp != NULL) {
                        if (temp->type & V_INST_CB) {
                            poi_new = temp2;
                            process = 2;
//                            monitor_log(mon_world, 'C');
                        }
                        if (temp->type & V_INST_PB) {
//                            monitor_log(mon_world, 'P');
                            if (mon_world->hregs.gcpu.eip != temp->addr) {
                                poi_new = poi->pb_cache_poi.targets[i];
                                process = 2;
//                                monitor_log(mon_world, '2');
                            } else {
                                temp->expect = 1;
                                mon_world->poi = poi->pb_cache_poi.targets[i];
                                dr &= 0xffff0ff0;
                                asm volatile ("mov %0, %%dr6"::"r" (dr));
                                mon_world->hregs.gcpu.eflags &=
                                    (~(H_EFLAGS_RF | H_EFLAGS_TF));
                                mon_world->hregs.gcpu.dr7 &= (0xff00);
                                mon_world->hregs.gcpu.dr7 |= 0x703;
                                mon_world->bp_to_poi[0] =
                                    poi->pb_cache_poi.targets[i];
                                mon_world->current_valid_bps = 1;
                                mon_world->gregs.rf = 0;
                                addr = temp->addr;
                                mon_world->hregs.gcpu.dr0 = addr;
                                asm volatile ("mov %0, %%dr0"::"r" (addr));
                                asm volatile ("mov %0, %%dr7"::
                                    "r" (mon_world->hregs.gcpu.dr7));
                                asm volatile ("pop %es");
                                asm volatile ("pop %ds");
                                asm volatile ("popa");
                                asm volatile ("add $12, %esp");
                                asm volatile ("iret");
                            }
                        }
                    }
                }
            }
        }
#endif
        if (process) {
            poi = (struct v_poi *) monitor_access(mon_world, poi_new);
            if ((poi->type & V_INST_U) || (poi->type & V_INST_PB)) {
                poi->expect = 1;
                mon_world->poi = poi_new;
                mon_world->hregs.gcpu.eflags |= (H_EFLAGS_TF | H_EFLAGS_RF);
                mon_world->hregs.gcpu.dr7 &= (0xff00);
                mon_world->gregs.rf = 1;
                dr &= 0xffff0ff0;
                asm volatile ("mov %0, %%dr6"::"r" (dr));
                asm volatile ("pop %es");
                asm volatile ("pop %ds");
                asm volatile ("popa");
                asm volatile ("add $12, %esp");
                asm volatile ("iret");
            }
            if ((poi->type & V_INST_CB) && poi->plan.valid) {
                poi->expect = 0;
                mon_world->poi = poi_new;
                dr &= 0xffff0ff0;
                asm volatile ("mov %0, %%dr6"::"r" (dr));
                mon_world->hregs.gcpu.eflags &= (~(H_EFLAGS_RF | H_EFLAGS_TF));
                mon_world->hregs.gcpu.dr7 &= (0xff00);
                i = 0x703;
                mon_world->bp_to_poi[0] = poi->plan.poi[0];
                mon_world->current_valid_bps = 1;
                mon_world->gregs.rf = 0;
                poi_new =
                    (struct v_poi *) monitor_access(mon_world,
                    poi->plan.poi[0]);
                addr = poi_new->addr;
                mon_world->hregs.gcpu.dr0 = addr;
                asm volatile ("mov %0, %%dr0"::"r" (addr));
                if (poi->plan.count > 1) {
                    i |= (3 << 2);
                    mon_world->bp_to_poi[1] = poi->plan.poi[1];
                    mon_world->current_valid_bps = 2;
                    poi_new =
                        (struct v_poi *) monitor_access(mon_world,
                        poi->plan.poi[1]);
                    addr = poi_new->addr;
                    mon_world->hregs.gcpu.dr1 = addr;
                    asm volatile ("mov %0, %%dr1"::"r" (addr));
                    if (poi->plan.count > 2) {
                        i |= (3 << 4);
                        mon_world->bp_to_poi[2] = poi->plan.poi[2];
                        mon_world->current_valid_bps = 3;
                        poi_new =
                            (struct v_poi *) monitor_access(mon_world,
                            poi->plan.poi[2]);
                        addr = poi_new->addr;
                        mon_world->hregs.gcpu.dr2 = addr;
                        asm volatile ("mov %0, %%dr2"::"r" (addr));
                        if (poi->plan.count > 3) {
                            i |= (3 << 6);
                            mon_world->bp_to_poi[3] = poi->plan.poi[3];
                            mon_world->current_valid_bps = 4;
                            poi_new =
                                (struct v_poi *) monitor_access(mon_world,
                                poi->plan.poi[3]);
                            addr = poi_new->addr;
                            mon_world->hregs.gcpu.dr3 = addr;
                            asm volatile ("mov %0, %%dr3"::"r" (addr));
                        }
                    }
                }
                mon_world->hregs.gcpu.dr7 |= i;
                asm volatile ("mov %0, %%dr7"::"r" (mon_world->hregs.gcpu.dr7));
                asm volatile ("pop %es");
                asm volatile ("pop %ds");
                asm volatile ("popa");
                asm volatile ("add $12, %esp");
                asm volatile ("iret");
            }
        }
    }
    h_addr_t tr = (h_addr_t) trbase;
    struct h_regs *h = &w->hregs;
    asm volatile ("cli");
    if (h->fpusaved) {
        asm volatile ("clts");
    } else {
        asm volatile ("movl %0, %%cr0"::"r" (h->hcpu.cr0 | H_CR0_TS));
    }
    asm volatile ("movl %0, %%dr7"::"r" (w->hregs.gcpu.dr7));
    asm volatile ("movl %0, %%dr0"::"r" (w->hregs.gcpu.dr0));
    asm volatile ("movl %0, %%dr1"::"r" (w->hregs.gcpu.dr1));
    asm volatile ("movl %0, %%dr2"::"r" (w->hregs.gcpu.dr2));
    asm volatile ("movl %0, %%dr3"::"r" (w->hregs.gcpu.dr3));
    asm volatile ("movl %0, %%dr6"::"r" (0xffff0ff0));
    {
        void *gdt = (void *) (h->gcpu.gdt.base);
        unsigned int *c;
        gdt = gdt + (h->gcpu.tr & 0xfff8);
        c = gdt + 4;
        (*c) = (*c) & (0xfffffdff);
    }
    asm volatile ("lgdt (%0)"::"r" (&(h->gcpu.gdt)));
    asm volatile ("lidt (%0)"::"r" (&(h->gcpu.idt)));
    asm volatile ("lldt %%bx"::"ebx" (h->gcpu.ldt));
    asm volatile ("ltr %%bx"::"ebx" (h->gcpu.tr));

    asm volatile ("mov %%esp, %0":"=r" (h->hcpu.save_esp));
    asm volatile ("movl %0, %%esp"::"r" (&h->hcpu.save_esp));
    asm volatile ("pusha");
    asm volatile ("push %ds");
    asm volatile ("push %es");
    asm volatile ("push %fs");
    asm volatile ("push %gs");

    asm volatile ("movl %0, %%cr3"::"r" (tr));

    asm volatile ("mov %%esp, %0":"=r" (h->gcpu.save_esp));

    asm volatile ("movl %0, %%esp"::"r" (&h->gcpu.gs));

    asm volatile ("pop %gs");
    asm volatile ("pop %fs");
    asm volatile ("pop %es");
    asm volatile ("pop %ds");

    asm volatile ("popa");
    asm volatile ("add $12, %esp");

    asm volatile ("iret");

    asm volatile (".balign 32");
    asm volatile ("5:");
    asm volatile ("cmp $(~0x08 + 0x80), (%esp)");
    asm volatile ("je 3f");
    asm volatile ("cmp $(~0x0a + 0x80), (%esp)");
    asm volatile ("je 3f");
    asm volatile ("cmp $(~0x0b + 0x80), (%esp)");
    asm volatile ("je 3f");
    asm volatile ("cmp $(~0x0c + 0x80), (%esp)");
    asm volatile ("je 3f");
    asm volatile ("cmp $(~0x0e + 0x80), (%esp)");
    asm volatile ("je 3f");
    asm volatile ("cmp $(~0x0d + 0x80), (%esp)");
    asm volatile ("je 3f");
    asm volatile ("cmp $(~0x11 + 0x80), (%esp)");
    asm volatile ("je 3f");
    CACHE_BT_QUICKPATH;
#ifdef BT_CACHE
#error Cannot have both monitor and BT cache!
#endif
    asm volatile ("cmp $(~0x1 + 0x80), (%esp)");
    asm volatile ("jne 9f");
    /*monitor start */
    asm volatile ("push $0xbeef");
    asm volatile ("sub $4, %esp");
    asm volatile ("pusha");
    asm volatile ("push %ds");
    asm volatile ("push %es");
    asm volatile ("mov %ss, %ax");
    asm volatile ("mov %ax, %ds");
    asm volatile ("mov %ax, %es");
    monitor();
    asm volatile ("jmp 10f");
    /*monitor end */
    asm volatile ("9:");
    asm volatile ("push $0xbeef");      /* some impossible value */

    asm volatile ("3:");
    asm volatile (".global monitor_fault_entry_check");
    asm volatile ("monitor_fault_entry_check:");
    /* following code must be in sync with world.c init and relocation code */
    asm volatile ("cmp $0x12345678, %esp");
    asm volatile ("je 180f");
    asm volatile ("mov $0x12345678, %esp");
    asm volatile ("jmp 10f");
    asm volatile ("180:");
    /* */
    GP_FAULT_QUICKPATH;

    asm volatile ("sub $4, %esp");

    asm volatile ("pusha");
    asm volatile ("8:");
    asm volatile ("push %ds");
    asm volatile ("push %es");
    asm volatile ("10:");
    asm volatile ("push %fs");
    asm volatile ("push %gs");

    asm volatile ("add $48, %esp");
    asm volatile ("pop %esp");

    asm volatile ("add $16, %esp");
    asm volatile ("mov %ss, %ax");
    asm volatile ("mov %ax, %ds");
    asm volatile ("mov %ax, %es");
    asm volatile ("popa");
    asm volatile ("pop %esp");

    asm volatile ("movl %0, %%cr3"::"r" (h->hcpu.cr3));

    asm volatile ("lgdt (%0)"::"r" (&(h->hcpu.gdt)));
    asm volatile ("lidt (%0)"::"r" (&(h->hcpu.idt)));
    asm volatile ("lldt %%bx"::"ebx" (h->hcpu.ldt));
    {
        void *gdt = (void *) (h->hcpu.gdt.base);
        unsigned int *c;
        gdt = gdt + (h->hcpu.tr & 0xfff8);
        c = gdt + 4;
        (*c) = (*c) & (0xfffffdff);
    }

    asm volatile ("ltr %%bx"::"ebx" (h->hcpu.tr));

    asm volatile ("mov %0,%%eax"::"r" (h->hcpu.ds):"eax");
    asm volatile ("mov %ax,%ds");
    asm volatile ("mov %0,%%eax"::"r" (h->hcpu.es):"eax");
    asm volatile ("mov %ax,%es");
    asm volatile ("mov %0,%%eax"::"r" (h->hcpu.fs):"eax");
    asm volatile ("mov %ax,%fs");
    asm volatile ("mov %0,%%eax"::"r" (h->hcpu.gs):"eax");
    asm volatile ("mov %ax,%gs");
    asm volatile ("mov %0,%%eax"::"r" (h->hcpu.ss):"eax");
    asm volatile ("mov %ax,%ss");

    if (h->gcpu.intr == 0xbeef) {
        h->gcpu.intr = h->gcpu.errorc;
    }
    h->gcpu.intr = ~((int) (h->gcpu.intr) - 0x80);

    asm volatile ("mov %%cr2, %0":"=r" (h->gcpu.page_fault_addr));
    asm volatile ("push %0"::"r" (h->hcpu.eflags));
    asm volatile ("push %0"::"r" (h->hcpu.cs));
    asm volatile ("push %0"::"r" (h->hcpu.eip));
    asm volatile ("iret");


    asm volatile (".global restoreCS");
    asm volatile ("restoreCS:":::"memory", "cc");

    asm volatile ("jmp 6f");
    asm volatile ("7:");
    CACHE_BT_CACHE(BT_CACHE_CAPACITY);
    GP_FAULT_QUICKPATH_HANDLER;
    asm volatile (".balign 32");
    asm volatile (".global trap_start");
    asm volatile ("trap_start:");
    asm volatile ("vector=0");
    asm volatile (".rept (256+6)/7");
    asm volatile (".balign 32");
    asm volatile (".rept 7");
    asm volatile ("    .if vector < 256");
    asm volatile ("        push $(~vector+0x80)");
    asm volatile ("        .if ((vector)%7) <> 6");
    asm volatile ("            jmp 2f");
    asm volatile ("        .endif");
    asm volatile ("        vector=vector+1");
    asm volatile ("    .endif");
    asm volatile (".endr");
    asm volatile ("2: jmp 4f");
    asm volatile (".endr");

    asm volatile ("4:");
    asm volatile ("jmp 5b");
    asm volatile ("6:");
    return;
}

int
h_switch_to(unsigned long trbase, struct v_world *w)
{
    struct h_regs *h = &w->hregs;
    if (w->status == VM_IDLE) {
        if (!(h->gcpu.eflags & H_EFLAGS_TF))
            g_pic_serve(w);
    }
    if (w->status == VM_IDLE) {
        return 0;
    }
    GP_FAULT_QUICKPATH_PREAMBLE;
    h_perf_tsc_begin(0);

    V_LOG("Using switcher %p", h->hcpu.switcher);

    h->hcpu.switcher(trbase, w);

    h->gcpu.ds = h->gcpu.ds & 0xffff;
    if ((h->gcpu.fs & 0xffff0000) != 0 || (h->gcpu.es & 0xffff0000) != 0
        || (h->gcpu.gs & 0xffff0000) != 0) {
        V_EVENT("Monitor fault");
    }
    if (w->monitor_buffer_start != w->monitor_buffer_end) {
        char buffer[MONITOR_BUFFER_MAX];
        int cp = 0;
        while (w->monitor_buffer_start != w->monitor_buffer_end) {
            buffer[cp++] = w->monitor_buffer[w->monitor_buffer_start++];
            if (w->monitor_buffer_start >= MONITOR_BUFFER_MAX) {
                w->monitor_buffer_start = 0;
            }
        }
        buffer[cp] = 0;
        V_ERR("Monitor log: %s", buffer);
    }
    h->gcpu.es = h->gcpu.es & 0xffff;
    h->gcpu.fs = h->gcpu.fs & 0xffff;
    h->gcpu.gs = h->gcpu.gs & 0xffff;

    h_perf_tsc_end(H_PERF_TSC_GUEST, 0);
    h_perf_tsc_begin(0);
    v_perf_inc(V_PERF_WS, 1);
    V_EVENT("cs:eip = %x:%x \t", h->gcpu.cs, h->gcpu.eip);
    V_LOG
        ("%s %s %s IOPL%x RING%x ds:%x, es:%x, ss:%x, esp:%x, cs:%x, eip:%x, eflags:%x, eax:%x, ebx:%x, ecx:%x, edx:%x, esi:%x, edi:%x, ebp:%x, errorcode:%x, save_esp:%x, tr:%x, intr:%x, v86ds:%x, v86es:%x, cstrue:%x, dstrue:%x, estrue:%x, sstrue:%x, trbase: %x, spt: %lx\n",
        w->gregs.mode == G_MODE_REAL ? "RM" : (g_get_current_ex_mode(w) ==
            G_EX_MODE_32 ? "32" : "16"), v_int_enabled(w) ? "IE" : "--",
        w->gregs.nt ? "NT" : "--", w->gregs.iopl, w->gregs.ring, h->gcpu.ds,
        h->gcpu.es, h->gcpu.ss, h->gcpu.esp, h->gcpu.cs, h->gcpu.eip,
        h->gcpu.eflags, h->gcpu.eax, h->gcpu.ebx, h->gcpu.ecx, h->gcpu.edx,
        h->gcpu.esi, h->gcpu.edi, h->gcpu.ebp, h->gcpu.errorc, h->gcpu.save_esp,
        h->gcpu.tr, h->gcpu.intr, h->gcpu.v86ds, h->gcpu.v86es, w->gregs.cstrue,
        w->gregs.dstrue, w->gregs.estrue, w->gregs.sstrue, w->gregs.cr3,
        w->htrbase);
    CACHE_BT_RESTORE;
    GP_FAULT_QUICKPATH_POSTPROCESSING;
    /* fix up the stupid RF flag */
    if (w->gregs.rf)
        h->gcpu.eflags |= H_EFLAGS_RF;
    else
        h->gcpu.eflags &= (~H_EFLAGS_RF);
#ifdef DEBUG_CODECONTROL
    if (w->poi && w->poi->expect && (h->gcpu.intr & 0xff) < 0x20
        && (h->gcpu.intr & 0xff) != 0x0e && (h->gcpu.intr & 0xff) != 0x01
        && w->poi->addr == g_get_ip(w)) {
        V_ALERT("POI ip == ip but not taking BP?");
    }
#endif
    if ((h->gcpu.intr & 0xff) == 0x01) {
        unsigned int dr;
        if ((!bp_reached) && g_get_ip(w) == bpaddr) {
            unsigned char *fb;
            w->status = VM_PAUSED;
            bp_reached = 1;
            h_clear_bp(w, 3);
            fb = g_fb_dump_text(w);
            V_ERR("BP reached");
            h_raw_dealloc(fb);
        }
        v_perf_inc(V_PERF_BT, 1);
        asm volatile ("mov %%dr6, %0":"=r" (dr));
        v_do_bp(w, g_get_ip(w), (dr & 0x4000) ? 1 : 0);
        dr &= 0xffff0ff0;
        asm volatile ("mov %0, %%dr6"::"r" (dr));
        h_perf_tsc_end(H_PERF_TSC_BT, 0);
        if (!(h->gcpu.eflags & H_EFLAGS_TF))
            g_pic_serve(w);
        return 1;
    } else if ((h->gcpu.intr & 0xff) == 0x0e) {
        unsigned int fault;
        v_perf_inc(V_PERF_PF, 1);
        if ((fault =
                v_pagefault(w, h->gcpu.page_fault_addr,
                    ((h->gcpu.errorc & 0x1) ==
                        0 ? V_MM_FAULT_NP : 0) | ((h->
                            gcpu.errorc & 0x2) ? V_MM_FAULT_W : 0))) !=
            V_MM_FAULT_HANDLED) {
            w->gregs.has_errorc = 1;
            w->gregs.errorc =
                ((fault & V_MM_FAULT_NP) ? 0 : 1) | ((fault & V_MM_FAULT_W) ? 2
                : 0) | ((w->gregs.ring == 0) ? 0 : 4);
            w->gregs.cr2 = h->gcpu.page_fault_addr;
            h_inject_int(w, 0x0e);
        }
        h_perf_tsc_end(H_PERF_TSC_PF, 0);
        return 1;
    } else if ((h->gcpu.intr & 0xff) == 0x07) {
        V_ALERT("FPU");
        h->fpusaved = 1;
        v_bt_reset(w);
        return 1;
    } else if ((h->gcpu.intr & 0xff) == 0x0d) {
        v_perf_inc(V_PERF_PI, 1);
        h_gpfault(w);
        h_perf_tsc_end(H_PERF_TSC_PI, 0);
        return 1;
    } else if ((h->gcpu.intr & 0xff) == 0x0a) {
        V_ERR(",Unidentified tss fault");
        w->status = VM_PAUSED;
    } else if ((h->gcpu.intr & 0xff) == 0x06) {
        unsigned char bound[20];
        unsigned int g_ip = g_get_ip(w);
        unsigned char *inst;
        h_read_guest(w, g_ip, (unsigned int *) &bound[0]);
        h_read_guest(w, g_ip + 4, (unsigned int *) &bound[4]);
        h_read_guest(w, g_ip + 8, (unsigned int *) &bound[8]);
        h_read_guest(w, g_ip + 12, (unsigned int *) &bound[12]);
        inst = (unsigned char *) &bound;
        if ((unsigned int) (*(inst + 0)) == 0x0f
            && (unsigned int) (*(inst + 1)) == 0x01
            && (unsigned int) (*(inst + 2)) == 0xc8) {
            /* monitor */
            w->hregs.gcpu.eip += 3;
        } else if ((unsigned int) (*(inst + 0)) == 0x0f
            && (unsigned int) (*(inst + 1)) == 0x01
            && (unsigned int) (*(inst + 2)) == 0xc9) {
            /* mwait */
            w->hregs.gcpu.eip += 3;
        } else {
            V_ERR("fault 06 by: %x %x %x %x", (unsigned int) (*(inst + 0)),
                (unsigned int) (*(inst + 1)), (unsigned int) (*(inst + 2)),
                (unsigned int) (*(inst + 3)));
            w->status = VM_PAUSED;
        }
    } else if ((h->gcpu.intr & 0xff) == 0xef) {
        time_up = 1;
        if (!(h->gcpu.eflags & H_EFLAGS_TF))
            g_pic_serve(w);
    } else {
        V_LOG("Warning int %x not received by host.\n", h->gcpu.intr & 0xff);
        if (!(h->gcpu.eflags & H_EFLAGS_TF))
            g_pic_serve(w);
    }
    GP_FAULT_QUICKPATH_POSTPROCESSING2;
    if ((h->gcpu.intr & 0xff) >= 0x20)
        h_do_int(h->gcpu.intr & 0xff);

    if (h->fpusaved) {
        h->fpusaved = 0;
    }
    return 0;
}

static int
h_gdt_load(struct v_world *world, unsigned int *seg, unsigned int selector,
    int ex, unsigned int *seg_true, int replace_rpl)
{
    h_addr_t addr = world->gregs.gdt.base;
    unsigned int word1, word2;
    unsigned int sel = (selector & 0xffff);
    void *host = (void *) (world->hregs.gcpu.gdt.base + (sel & 0xfff8));
    if (sel == 0) {
        *seg = 0;
        *seg_true = 0xffffffff;
        *(seg_true + 1) = 0xffffffff;
        *(seg_true + 2) = 0;
        return 0;
    }
    //take care of LDT
    if (sel & 0x4) {
        V_ERR("load LDT %x, not implemented", sel);
        world->status = VM_PAUSED;
        return 0;
    }
    addr += (sel & 0xfff8);
    if (sel > world->gregs.gdt.limit) {
        //inject gp fault
        V_ERR
            ("Guest GP Fault during selector load, %x off limits of %x\n",
            sel, world->gregs.gdt.limit);
        world->status = VM_PAUSED;
        return 0;
    }
    if (h_read_guest(world, addr, &word1) |
        h_read_guest(world, addr + 4, &word2)) {
        V_ERR("Guest access fault during selector load\n");
        world->status = VM_PAUSED;
        return 0;
    }
    //todo: check gp fault here

    (*seg_true) = word1;
    *(unsigned int *) (host) = word1;
    host += 4;
    (*(seg_true + 1)) = word2;
    *(unsigned int *) (host) = word2 & 0xffff90ff;      //no type, no DPL
    (*(seg_true + 2)) = selector;
    if ((!ex) && (((word2 & 0x6000) >> 13) < world->gregs.ring)) {
        //fault, comforming not checked;
        V_ERR("Guest GP Fault during selector load\n");
        world->status = VM_PAUSED;
        return 0;
    }
    if (!(word2 & 0x1000)) {
        //system descriptor
        return 1;
    }
    if (ex)
        *(unsigned int *) (host) |= 0x6800;
    else
        switch (word2 & 0xf00) {
        case 0xa00:
        case 0xb00:
        case 0xe00:
        case 0xf00:
        case 0x000:
        case 0x100:
            *(unsigned int *) (host) |= 0x6000;
            break;
        case 0x400:
        case 0x500:
            *(unsigned int *) (host) |= 0x6400;
            break;
        case 0x200:
        case 0x300:
            *(unsigned int *) (host) |= 0x6200;
            break;
        case 0x600:
        case 0x700:
            *(unsigned int *) (host) |= 0x6600;
            break;
        default:
            V_ERR("unknown descriptor type");
            break;
        }
    V_VERBOSE("Selector is: %x, host gdt %x %x, word1 %x, word2 %x", sel,
        *(unsigned int *) (host - 4), *(unsigned int *) (host), word1, word2);

    (*seg) = (selector & 0xffff) | ((replace_rpl) ? 0x3 : 0);
    return 0;

}

void
h_delete_trbase(struct v_world *world)
{
    void *htrv;
    unsigned int i, j;
    struct v_chunk v;
    v.order = H_TRBASE_ORDER;
    v.phys = world->htrbase;
    for (i = 0; i < (1 << H_TRBASE_ORDER); i++) {

        htrv = h_allocv(world->htrbase + i * H_PAGE_SIZE);
        for (j = 0; j < H_PAGE_SIZE; j += 4) {
            if ((*(unsigned int *) (htrv + j)) & 0x1) {
                struct v_chunk v;
                v.order = 0;
                v.phys = *(unsigned int *) (htrv + j) & 0xfffff000;
                h_raw_depalloc(&v);
            }
        }
        h_deallocv(world->htrbase + i * H_PAGE_SIZE);
    }
    h_raw_depalloc(&v);
}

static void
h_inv_pagetable(struct v_world *world, struct v_spt_info *spt,
    g_addr_t virt, unsigned int level)
{
//careful, when inv virt, check against known bridge pages
    void *htrv;
    unsigned int i, j;
    for (i = 0; i < (1 << H_TRBASE_ORDER); i++) {

        htrv = h_allocv(spt->spt_paddr + i * H_PAGE_SIZE);
        for (j = 0; j < H_PAGE_SIZE; j += 4) {
            if ((*(unsigned int *) (htrv + j)) & 0x1) {
                struct v_chunk v;
                v.order = 0;
                v.phys = *(unsigned int *) (htrv + j) & 0xfffff000;
                h_raw_depalloc(&v);
            }
        }
        h_clear_page((unsigned long) htrv);
        h_deallocv(spt->spt_paddr + i * H_PAGE_SIZE);
    }
    h_set_map(spt->spt_paddr, (h_addr_t) world,
        h_v2p((h_addr_t) world), 1, V_PAGE_W | V_PAGE_VM);
    V_LOG("world data at %x, ", (unsigned int) h_v2p((h_addr_t) world));
    h_set_map(spt->spt_paddr, (h_addr_t) world->hregs.hcpu.switcher,
        h_v2p((h_addr_t) world->hregs.hcpu.switcher), 1, V_PAGE_W | V_PAGE_VM);
    V_LOG("neutral page at %x\n",
        (unsigned int) h_v2p((h_addr_t) world->hregs.hcpu.switcher));
    h_set_map(spt->spt_paddr, (h_addr_t) world->hregs.gcpu.idt.base,
        h_v2p(world->hregs.gcpu.idt.base), 1, V_PAGE_W | V_PAGE_VM);
    V_LOG("idt at %x\n", (unsigned int) h_v2p(world->hregs.gcpu.idt.base));
    h_set_map(spt->spt_paddr, (h_addr_t) world->hregs.gcpu.gdt.base,
        h_v2p(world->hregs.gcpu.gdt.base), 1, V_PAGE_W | V_PAGE_VM);
    V_LOG("gdt at %x\n", (unsigned int) h_v2p(world->hregs.gcpu.gdt.base));
    for (i = 0; i < V_MM_MAX_POOL; i++) {
        spt->mem_pool_mapped[i] = 0;
    }
    h_monitor_setup_data_pages(world, spt->spt_paddr);

}

void
h_inv_spt(struct v_world *world, struct v_spt_info *spt)
{
//careful, when inv virt, check against known bridge pages
    struct v_inv_entry **p = &(spt->inv_list);
    while ((*p) != NULL) {
        struct v_inv_entry *psave = (*p);
        struct v_ptp_info **pl = &((*p)->page->ptp_list);
        V_LOG("invalidating mfn %x", (unsigned int) (*p)->page->mfn);
        while (*pl != NULL) {
            if ((*pl)->spt == spt) {
                struct v_ptp_info *next = (*pl)->next;
                V_LOG(", va %lx %s", (unsigned long) (*pl)->vaddr,
                    (*pl)->gpt_level == 0 ? "" : "pagetable");
                if (!h_check_bridge_pages(world, (*pl)->vaddr)) {
                    if ((*pl)->gpt_level == 0)
                        h_set_map(spt->spt_paddr,
                            (*pl)->vaddr, 0, 1, V_PAGE_NOMAP);
                    else
                        h_inv_pagetable(world, spt,
                            (*pl)->vaddr, (*pl)->gpt_level);
                }
                h_raw_dealloc((*pl));
                (*pl) = next;
            } else
                pl = &((*pl)->next);
        }
        *p = (*p)->next;
        h_raw_dealloc(psave);
    }
}

void
h_new_trbase(struct v_world *world)
{
    struct v_chunk *trbase;
    struct v_spt_info *spt;
    void *htrv;
    int i;
    unsigned int cr3 = (world->gregs.mode == G_MODE_PG) ? world->gregs.cr3 : 1;
    if ((spt = v_spt_get_by_gpt(world, cr3)) == NULL) {
        trbase = h_raw_palloc(H_TRBASE_ORDER);
        world->htrbase = (unsigned long) (trbase->phys);
        V_EVENT("new base: %lx from %x", world->htrbase, cr3);
        for (i = 0; i < (1 << H_TRBASE_ORDER); i++) {
            htrv = h_allocv(world->htrbase + i * H_PAGE_SIZE);
            h_clear_page((unsigned long) htrv);
            h_deallocv(world->htrbase + i * H_PAGE_SIZE);
        }
        h_set_map(world->htrbase, (long unsigned int) world,
            h_v2p((unsigned int) world), 1, V_PAGE_W | V_PAGE_VM);
        V_LOG("world data at %x, ", (unsigned int) h_v2p((unsigned int) world));
        h_set_map(world->htrbase,
            (long unsigned int) world->hregs.hcpu.switcher,
            h_v2p((unsigned int) world->hregs.hcpu.switcher), 1,
            V_PAGE_W | V_PAGE_VM);
        V_LOG("neutral page at %x\n",
            (unsigned int) h_v2p((unsigned int) world->hregs.hcpu.switcher));
        h_set_map(world->htrbase, (unsigned long) world->hregs.gcpu.idt.base,
            h_v2p(world->hregs.gcpu.idt.base), 1, V_PAGE_W | V_PAGE_VM);
        V_LOG("idt at %x\n", (unsigned int) h_v2p(world->hregs.gcpu.idt.base));
        h_set_map(world->htrbase,
            (unsigned long) world->hregs.gcpu.gdt.base,
            h_v2p(world->hregs.gcpu.gdt.base), 1, V_PAGE_W | V_PAGE_VM);
        V_LOG("gdt at %x\n", (unsigned int) h_v2p(world->hregs.gcpu.gdt.base));
        v_spt_add(world, world->htrbase, cr3);
        h_monitor_setup_data_pages(world, world->htrbase);
    } else {
        V_EVENT("spt found at %lx for %x", spt->spt_paddr, cr3);
        world->htrbase = spt->spt_paddr;
        h_inv_spt(world, spt);
        h_monitor_setup_data_pages(world, world->htrbase);
    }

}

static int
h_save_to_current_tr(struct v_world *world)
{
    unsigned int base;
    unsigned int limit;
    unsigned int eflags = world->hregs.gcpu.eflags;
    struct g_tr_table *table;
    V_LOG("tr word1 %x, word2 %x / ", world->gregs.tr_hid,
        world->gregs.tr_hidhigh);
    base = g_sel2base(world->gregs.tr_hid, world->gregs.tr_hidhigh);
    limit = g_sel2limit(world->gregs.tr_hid, world->gregs.tr_hidhigh);
    table = (struct g_tr_table *) (base);
    if (limit < sizeof(struct g_tr_table))
        return 1;
    if (v_int_enabled(world))
        eflags |= H_EFLAGS_IF;
    else
        eflags &= (~H_EFLAGS_IF);
    eflags &= 0xffffcfff;
    eflags |= (world->gregs.iopl << 12);
    if (world->gregs.nt)
        eflags |= H_EFLAGS_NT;
    if (h_write_guest
        (world, (unsigned int) &(table->eax),
            world->hregs.gcpu.eax) | h_write_guest(world,
            (unsigned int) &(table->ebx),
            world->hregs.gcpu.ebx) | h_write_guest(world, (unsigned int)
            &
            (table->ecx),
            world->hregs.gcpu.ecx) | h_write_guest(world, (unsigned int)
            &(table->edx),
            world->hregs.gcpu.edx) | h_write_guest(world, (unsigned int)
            &(table->esi),
            world->hregs.gcpu.esi) | h_write_guest(world, (unsigned int)
            &(table->edi),
            world->hregs.gcpu.edi) | h_write_guest(world, (unsigned int)
            &(table->esp),
            world->hregs.gcpu.esp) | h_write_guest(world, (unsigned int)
            &(table->ebp),
            world->hregs.gcpu.ebp) | h_write_guest(world, (unsigned int)
            &(table->eip),
            world->hregs.gcpu.eip) | h_write_guest(world, (unsigned int)
            &(table->ds),
            world->gregs.dstrue) | h_write_guest(world, (unsigned int)
            &(table->es),
            world->gregs.estrue) | h_write_guest(world, (unsigned int)
            &(table->fs),
            world->gregs.fstrue) | h_write_guest(world, (unsigned int)
            &(table->gs),
            world->gregs.gstrue) | h_write_guest(world, (unsigned int)
            &(table->ss),
            world->gregs.sstrue) | h_write_guest(world, (unsigned int)
            &(table->cs),
            world->gregs.cstrue) | h_write_guest(world, (unsigned int)
            &(table->cr3),
            world->gregs.cr3) | h_write_guest(world, (unsigned int)
            &(table->ldt),
            world->gregs.ldt_true) | h_write_guest(world, (unsigned int)
            &(table->eflags), eflags))
        return 1;
    return 0;
}

static int
h_load_from_tr(struct v_world *world, unsigned int trword1,
    unsigned int trword2, unsigned int link)
{
    unsigned int base;
    unsigned int limit;
    struct g_tr_table *table;
    base = g_sel2base(trword1, trword2);
    limit = g_sel2limit(trword1, trword2);
    table = (struct g_tr_table *) (base);
    if (limit < sizeof(struct g_tr_table))
        return 1;
    if (h_read_guest(world, (h_addr_t) & (table->cr3), &world->gregs.cr3))
        return 1;
    V_LOG("new cr3 = %x", world->gregs.cr3);
    h_new_trbase(world);
    if (h_read_guest
        (world, (h_addr_t) & (table->eax),
            &world->hregs.gcpu.eax) | h_read_guest(world,
            (h_addr_t) & (table->ebx),
            &world->hregs.gcpu.ebx) | h_read_guest(world, (h_addr_t)
            & (table->ecx),
            &world->hregs.gcpu.ecx) | h_read_guest(world, (h_addr_t)
            & (table->edx), &world->hregs.gcpu.edx)
        | h_read_guest(world, (h_addr_t)
            & (table->esi),
            &world->hregs.gcpu.esi) | h_read_guest(world, (h_addr_t)
            & (table->edi), &world->hregs.gcpu.edi)
        | h_read_guest(world, (h_addr_t)
            & (table->esp),
            &world->hregs.gcpu.esp) | h_read_guest(world, (h_addr_t)
            & (table->eip), &world->hregs.gcpu.eip)
        | h_read_guest(world, (unsigned int)
            &(table->ebp),
            &world->hregs.gcpu.ebp) | h_read_guest(world, (unsigned int)
            &(table->ds), &world->gregs.dstrue)
        | h_read_guest(world, (h_addr_t)
            & (table->es),
            &world->gregs.estrue) | h_read_guest(world, (h_addr_t)
            & (table->fs), &world->gregs.fstrue)
        | h_read_guest(world, (h_addr_t)
            & (table->gs),
            &world->gregs.gstrue) | h_read_guest(world, (h_addr_t)
            & (table->ss), &world->gregs.sstrue)
        | h_read_guest(world, (h_addr_t)
            & (table->cs),
            &world->gregs.cstrue) | h_read_guest(world, (h_addr_t)
            & (table->ldt), &world->gregs.ldt) | h_read_guest(world, (h_addr_t)
            & (table->eflags), &world->gregs.eflags))
        return 1;
    if (link && h_write_guest(world, (h_addr_t) & (table->prev), link))
        return 1;
    V_LOG("new ldt = %x", world->gregs.ldt);
    V_LOG("new eax = %x", world->hregs.gcpu.eax);
    V_LOG("new ebx = %x", world->hregs.gcpu.ebx);
    V_LOG("new ecx = %x", world->hregs.gcpu.ecx);
    V_LOG("new edx = %x", world->hregs.gcpu.edx);
    V_LOG("new esi = %x", world->hregs.gcpu.esi);
    V_LOG("new edi = %x", world->hregs.gcpu.edi);
    V_LOG("new esp = %x", world->hregs.gcpu.esp);
    V_LOG("new ebp = %x", world->hregs.gcpu.ebp);
    V_LOG("new eip = %x", world->hregs.gcpu.eip);
    V_LOG("new cs = %x", world->gregs.cstrue);
    V_LOG("new ds = %x", world->gregs.dstrue);
    V_LOG("new ss = %x", world->gregs.sstrue);
    V_LOG("new es = %x", world->gregs.estrue);
    V_LOG("new fs = %x", world->gregs.fstrue);
    V_LOG("new gs = %x", world->gregs.gstrue);
    h_gdt_load(world, &world->hregs.gcpu.ldt, world->gregs.ldt, 0,
        &world->gregs.ldt, 0);
    if (h_gdt_load
        (world, &world->hregs.gcpu.cs, world->gregs.cstrue, 1,
            &world->gregs.cs, 1))
        return 1;
    world->gregs.ring = ((world->gregs.cshigh & 0x6000) >> 13);
    V_LOG("New ring = %x /", world->gregs.ring);
    if (h_gdt_load
        (world, &world->hregs.gcpu.ds, world->gregs.dstrue, 0,
            &world->gregs.ds, 0))
        return 1;
    if (h_gdt_load
        (world, &world->hregs.gcpu.es, world->gregs.estrue, 0,
            &world->gregs.es, 0))
        return 1;
    if (h_gdt_load
        (world, &world->hregs.gcpu.fs, world->gregs.fstrue, 0,
            &world->gregs.fs, 0))
        return 1;
    if (h_gdt_load
        (world, &world->hregs.gcpu.gs, world->gregs.gstrue, 0,
            &world->gregs.gs, 0))
        return 1;
    if (h_gdt_load
        (world, &world->hregs.gcpu.ss, world->gregs.sstrue, 0,
            &world->gregs.ss, 1))
        return 1;
    world->hregs.gcpu.eflags = (world->hregs.gcpu.eflags & H_EFLAGS_RF) |
        (world->gregs.eflags & 0x3e0cd7) | (H_EFLAGS_IF | H_EFLAGS_RESERVED);
    V_LOG("new eflags = %x", world->gregs.eflags);
    if (world->hregs.gcpu.eflags & H_EFLAGS_VM) {
        V_LOG("V86 mode, nyi");
        world->status = VM_PAUSED;
    }
    if (world->gregs.eflags & H_EFLAGS_IF)
        v_enable_int(world);
    else
        v_disable_int(world);
    world->gregs.iopl = (world->gregs.eflags & H_EFLAGS_IOPL_MASK) >> 12;
    world->gregs.nt = (world->gregs.eflags & H_EFLAGS_NT) >> 14;
    if (world->gregs.eflags & H_EFLAGS_VM) {
        V_LOG("v86m guest, not implemented\n");
        world->status = VM_PAUSED;
    }
    return 0;
}

#define h_update(selector, ex, rpl) if ((world->gregs.selector##true & 0xfffc) != (world->hregs.gcpu.selector & 0xfffc)) \
             h_gdt_load(world, &world->hregs.gcpu.selector, world->hregs.gcpu.selector, ex, &world->gregs.selector, rpl)

static void
h_task_switch(struct v_world *world, unsigned int newtr, unsigned int link,
    unsigned int is_return)
{
    unsigned int ac;
    unsigned int oldtr = world->gregs.tr;
    unsigned int word2_old;
    unsigned int dummy;
    unsigned int j;
    h_update(ds, 0, 0);
    h_update(es, 0, 0);
    h_update(fs, 0, 0);
    h_update(gs, 0, 0);
    h_update(ss, 0, 1);
    if (h_gdt_load(world, &dummy, newtr, 1, &world->gregs.trtemp, 0)) {
        unsigned word1 = world->gregs.trtemp, word2 = world->gregs.trtemphigh;
        if (h_read_guest(world,
                world->gregs.gdt.base + (oldtr & 0xfff8) + 4, &word2_old)) {
            V_LOG("Guest access old tr failed");
            world->status = VM_PAUSED;
        }
        V_LOG("Sys desc, %x, %x /", world->gregs.trtemp,
            world->gregs.trtemphigh);
        switch (world->gregs.trtemphigh & 0xf00) {
        case 0x300:
            V_LOG("16 bit tss unimplemented");
            world->status = VM_PAUSED;
            break;
        case 0xb00:
            if (is_return)
                goto process_32bit_t;
            V_LOG("Busy tss, guest fault:");
            world->status = VM_PAUSED;
            break;
        case 0x500:
            V_LOG("Task gate");
            newtr = word1 >> 16;
            h_gdt_load(world, &dummy, newtr, 1, &world->gregs.trtemp, 0);
            word1 = world->gregs.trtemp;
            word2 = world->gregs.trtemphigh;
            j = world->gregs.trtemphigh & 0xf00;
            if (j != 0x100 && j != 0x900) {
                V_LOG("guest gp fault, task gate is not a task");
                world->status = VM_PAUSED;
                break;
            }
            if (j == 0x900)
                goto process_32bit_t;
        case 0x100:
            V_LOG("16 bit tss, unimplemented:");
            world->status = VM_PAUSED;
            break;
        case 0x900:
            if (is_return) {
                V_LOG("returning to non-busy tss");
                world->status = VM_PAUSED;
                break;
            }
          process_32bit_t:
            ac = world->gregs.ring;
            if ((newtr & 0x3) > world->gregs.ring)
                ac = newtr & 0x3;
            if (ac > ((world->gregs.trtemphigh & 0x6000) >> 13)) {
                V_LOG
                    ("Guest gp fault, tss call from %x to %x, unimplemented",
                    ac, ((world->gregs.trtemphigh & 0x6000) >> 13));
                world->status = VM_PAUSED;
                break;
            }
            if (h_save_to_current_tr(world)) {
                V_LOG("Guest gp fault saving tr");
                break;
            }
            if (h_load_from_tr
                (world, world->gregs.trtemp,
                    world->gregs.trtemphigh, link ? oldtr : 0)) {
                V_LOG("Guest gp fault loading tr");
                break;
            }
            if (((!link)
                    && h_write_guest(world,
                        world->gregs.gdt.base +
                        (oldtr & 0xfff8) + 4, word2_old & 0xfffffdff))
                || h_write_guest(world,
                    world->gregs.gdt.base + (newtr & 0xfff8) + 4,
                    word2 | 0x200)) {
                V_LOG("Guest set busy tr failed");
                world->status = VM_PAUSED;
            }
            world->gregs.tr = world->gregs.tr_true = newtr;
            world->gregs.tr_hid = word1;
            world->gregs.tr_hidhigh = word2;
            break;

        default:
            V_LOG("Guest gp fault, unimplemented");
            world->status = VM_PAUSED;
            break;

        }
    }
}

static void
h_do_return(struct v_world *world, int para_count, int is_iret)
{
    unsigned int sp, word, word2, word3, word4, eflags;
    v_bt_reset(world);
    world->find_poi = 1;
    if (is_iret && world->gregs.nt) {
        unsigned int base, prev;
        world->hregs.gcpu.eip++;
        base = g_sel2base(world->gregs.tr_hid, world->gregs.tr_hidhigh);
        if (h_read_guest(world, base, &prev)) {
            V_LOG("double fault accessing tss");
            world->status = VM_PAUSED;
            return;
        }
        V_LOG("linking back to %x /", prev);
        h_task_switch(world, prev, 0, 1);
        return;
    }
    sp = g_get_sp(world);
    V_LOG("esp=%x", sp);
    if (h_read_guest(world, sp, &word)) {
        V_LOG("Guest SS Fault\n");
        return;
    }
    if (!(world->gregs.cshigh & 0x00400000)) {
        unsigned int sel = (word & 0xffff0000) >> 16;
        if (g_get_sel_ring(world, sel) < world->gregs.ring) {
            V_LOG("Guest GP fault, attempting to switch inside");
            return;
        }
        if (g_get_sel_ring(world, sel) > world->gregs.ring) {
            V_LOG("umimplemented switch out");
            world->status = VM_PAUSED;
            return;
        }
        V_LOG("16 bit return to %x:%x", sel, word & 0xffff);
        world->hregs.gcpu.eip = word & 0xffff;
        if (h_gdt_load
            (world, &world->hregs.gcpu.cs, sel, 1, &world->gregs.cs, 1)) {
            V_LOG("16 bit switch to system descriptor, unimplemented");
            world->status = VM_PAUSED;
        }
        world->hregs.gcpu.esp += (4 + para_count);
    } else {
        unsigned int newcs, newss, ring;
#ifdef DEBUG_CODECONTROL
        unsigned int savedef = 0;
#endif
        if (h_read_guest(world, sp + 4, &word2)) {
            V_LOG("Guest SS Fault\n");
            world->status = VM_PAUSED;
            return;
        }
        newcs = word2 & 0xffff;
        ring = g_get_sel_ring(world, newcs);
        V_VERBOSE("new ring %x", ring);
        if (ring < world->gregs.ring) {
            V_LOG("Guest GP fault, attempting to switch inside");
            world->status = VM_PAUSED;
            return;
        }
        if (ring == 3)
            world->gregs.fast_iret_possible = 1;
        sp += 8;
        world->hregs.gcpu.esp += 8;
        if (is_iret) {
            if (h_read_guest(world, sp, &eflags)) {
                V_LOG("Guest SS Fault\n");
                world->status = VM_PAUSED;
                return;
            }
            V_LOG("new eflags = %x", eflags);
#ifdef DEBUG_CODECONTROL
            savedef = eflags;
#endif
            world->gregs.eflags = eflags;
            world->hregs.gcpu.eflags =
                (world->hregs.
                gcpu.eflags & H_EFLAGS_RF) | (eflags & 0x3e0cd7) | (H_EFLAGS_IF
                | H_EFLAGS_RESERVED);
            if (world->hregs.gcpu.eflags & H_EFLAGS_VM) {
                V_LOG("V86mode, nyi");
                world->status = VM_PAUSED;
            }
            if (eflags & H_EFLAGS_IF) {
                v_enable_int(world);
            } else {
                v_disable_int(world);
            }
            world->gregs.iopl = (eflags & H_EFLAGS_IOPL_MASK) >> 12;
            world->gregs.nt = (eflags & H_EFLAGS_NT) >> 14;

            sp += 4;
            world->hregs.gcpu.esp += 4;
        }
        if (ring > world->gregs.ring) {
#ifdef DEBUG_CODECONTROL
            if (!(savedef & H_EFLAGS_IF)) {
                V_ALERT("Sanity check failed, ring 1-3 has interrupt disabled");
                world->status = VM_PAUSED;
            }
#endif
            if (h_read_guest(world, sp, &word3) |
                h_read_guest(world, sp + 4, &word4)) {
                V_LOG("Guest SS Fault\n");
                world->status = VM_PAUSED;
                return;
            }
            newss = word4 & 0xffff;
            if (h_gdt_load
                (world, &world->hregs.gcpu.ss, newss, 0, &world->gregs.ss, 1)) {
                V_LOG("Guest GP Fault\n");
            }
            world->hregs.gcpu.esp = word3;
            V_VERBOSE("new ss:esp = %x:%x", newss, word3);
            h_update(ds, 0, 0);
            h_update(es, 0, 0);
            h_update(fs, 0, 0);
            h_update(gs, 0, 0);
            world->gregs.ring = ring;
            if ((world->gregs.dstrue & 0x3) < ring) {
                V_VERBOSE("clearing out DS");
                h_gdt_load(world, &world->hregs.gcpu.ds, 0, 0,
                    &world->gregs.ds, 0);
            }
            if ((world->gregs.estrue & 0x3) < ring) {
                V_VERBOSE("clearing out ES");
                h_gdt_load(world, &world->hregs.gcpu.es, 0, 0,
                    &world->gregs.es, 0);
            }
            if ((world->gregs.fstrue & 0x3) < ring) {
                V_VERBOSE("clearing out FS");
                h_gdt_load(world, &world->hregs.gcpu.fs, 0, 0,
                    &world->gregs.fs, 0);
            }
            if ((world->gregs.gstrue & 0x3) < ring) {
                V_VERBOSE("clearing out GS");
                h_gdt_load(world, &world->hregs.gcpu.gs, 0, 0,
                    &world->gregs.gs, 0);
            }
        } else {
            world->hregs.gcpu.esp += para_count;
        }
        V_LOG("32 bit return to %x:%x", newcs, word);
        if (h_gdt_load
            (world, &world->hregs.gcpu.cs, newcs, 1, &world->gregs.cs, 1)) {
            V_LOG("16 bit switch to system descriptor, unimplemented");
            world->status = VM_PAUSED;
        }
        world->hregs.gcpu.eip = word;
    }

}

#define USERMODE_TESTS

#ifdef USERMODE_TESTS

int usermode_tests_reset = 1;
int usermode_test_done = 9999;

#endif

void
h_inject_int(struct v_world *world, unsigned int int_no)
{
    unsigned int word1, word2;
    unsigned int sp, r0ss, r0esp;
    unsigned int base, eflags, newip, newcs;
    struct g_tr_table *table;
    unsigned long ip = g_get_ip(world);
    int igate = 0;
    V_ALERT("injecting interrupt %x", int_no);
#ifdef USERMODE_TESTS
    if (int_no == 0x80 && world->hregs.gcpu.eax == 24 && usermode_tests_reset) {
        v_perf_init();
        h_perf_init();
        usermode_tests_reset = 0;
        V_ERR("Usermode test reset counters!");
    }
    if (int_no == 0x80 && world->hregs.gcpu.eax == 24 && !usermode_tests_reset) {
        if (usermode_test_done > 0) {
            int i;
            usermode_test_done--;
            if (usermode_test_done <= 0) {
                usermode_test_done = 9999;
                usermode_tests_reset = 1;
                V_ERR("Usermode test done counters!");
                for (i = 0; i < V_PERF_COUNT; i++) {
                    V_ERR("VM Perf Counter %x, %lx", i, v_perf_get(i));
                }
                for (i = 0; i < H_PERF_COUNT; i++) {
                    V_ERR("Host Perf Counter %x, %lx", i, h_perf_get(i));
                }
                for (i = 0; i < H_TSC_COUNT; i++) {
                    V_ERR("Host TSC Counter %x, %llx", i, h_tsc_get(i));
                }
            }
        }
    }
#endif
    if (int_no == 0x0d) {
        V_ALERT("injecting GP");
        world->status = VM_PAUSED;
    }
    if (world->gregs.ring == 0) {
        v_add_ipoi(world, ip, g_get_poi_key(world), world->poi);
    }
    if (h_read_guest(world, world->gregs.idt.base + int_no * 8, &word1) |
        h_read_guest(world, world->gregs.idt.base + int_no * 8 + 4, &word2)) {
        V_LOG("guest double fault");
        world->status = VM_PAUSED;
        return;
    }
    v_bt_reset(world);
    V_VERBOSE("word1,2= %x, %x", word1, word2);
    switch (word2 & 0x1f00) {
    case 0x500:
        V_LOG("Task gate:");
        h_task_switch(world, (word1 & 0xff0000) >> 16, 1, 0);
        world->gregs.nt = 1;
        break;
    case 0xe00:
        V_LOG("Interrupt gate");
        igate = 1;
    case 0xf00:
        V_LOG("Trap gate");
        sp = g_get_sp(world);
        newip = (word1 & 0xffff) + (word2 & 0xffff0000);
        newcs = (word1 & 0xffff0000) >> 16;
        if (world->gregs.ring != g_get_sel_ring(world, newcs)) {
            unsigned int oldss, oldesp;
            world->gregs.ring = g_get_sel_ring(world, newcs);
            base = g_sel2base(world->gregs.tr_hid, world->gregs.tr_hidhigh);
            V_LOG("New ring = %x, trbase = %x ", world->gregs.ring, base);
            table = (struct g_tr_table *) (base);
            if (h_read_guest
                (world,
                    (unsigned int) (((void *) (&(table->ss0))) +
                        8 * world->gregs.ring),
                    &r0ss) | h_read_guest(world, (unsigned int) (((void *)
                            &(table->esp0)) + 8 * world->gregs.ring), &r0esp)) {
                V_LOG("double fault from int inject");
                world->status = VM_PAUSED;
                break;
            }
            oldss = world->gregs.sstrue;
            oldesp = world->hregs.gcpu.esp;
            V_LOG("switch stack to %x:%x / ", r0ss, r0esp);
            h_gdt_load(world, &world->hregs.gcpu.ss, r0ss, 0,
                &world->gregs.ss, 1);
            world->hregs.gcpu.esp = r0esp;
            sp = g_get_sp(world);
            V_LOG("new sp = %x", sp);
            if (h_write_guest(world, sp - 4, oldss) |
                h_write_guest(world, sp - 8, oldesp)) {
                V_LOG("double fault from int inject");
                world->status = VM_PAUSED;
                break;
            }
            world->hregs.gcpu.esp -= 8;
            sp -= 8;
        }
        eflags = world->hregs.gcpu.eflags;
#ifdef DEBUG_CODECONTROL
        if (eflags & H_EFLAGS_TF) {
            V_ERR("Wrong time to inject interrupts!!!");
            world->status = VM_PAUSED;
        }
#endif
        eflags &= 0x40cd7;
        if (v_int_enabled(world))
            eflags |= H_EFLAGS_IF;
        else
            eflags &= (~H_EFLAGS_IF);
        eflags &= (~H_EFLAGS_TF);
        eflags |= (world->gregs.iopl << 12);
        if (world->gregs.nt)
            eflags |= H_EFLAGS_NT;
        world->gregs.nt = 0;
        V_LOG("eflags pushed: %x ", eflags);
        if (igate) {
            v_disable_int(world);
            world->gregs.eflags &= (~H_EFLAGS_IF);
        }
        h_write_guest(world, sp - 4, eflags);
        h_write_guest(world, sp - 8, world->gregs.cstrue);
        h_write_guest(world, sp - 12, world->hregs.gcpu.eip);
        world->hregs.gcpu.esp -= 12;
        sp -= 12;
        V_LOG("switch to %x:%x", newcs, newip);
        h_gdt_load(world, &world->hregs.gcpu.cs, newcs, 1, &world->gregs.cs, 1);
        world->hregs.gcpu.eip = newip;
        if (world->gregs.has_errorc) {
            h_write_guest(world, sp - 4, world->gregs.errorc);
            world->hregs.gcpu.esp -= 4;
        }
        break;
    default:
        V_ERR("word1,2= %x, %x", word1, word2);
        V_ERR("unimplemented gates or faults");
        world->status = VM_PAUSED;
    }

}

static void
h_debug_sanity_check(struct v_world *world)
{
    if (world->gregs.mode == G_MODE_REAL)
        return;
#ifdef DEBUG_CODECONTROL
/* hack: linux sanity check*/
    if (world->gregs.cstrue == 0x10 && ((world->hregs.gcpu.eip > 0x200000
                && world->hregs.gcpu.eip < 0xc0000000)
            || (world->hregs.gcpu.esp >
                0xb0000000 && world->hregs.gcpu.esp < 0xc0000000))) {
        V_ERR("linux sanity check failed");
        world->status = VM_PAUSED;
    }
#endif
    if ((world->hregs.gcpu.cs > 0x100 && world->hregs.gcpu.cs != 0x7e3) ||
        world->hregs.gcpu.ds > 0x100 ||
        world->hregs.gcpu.es > 0x100 ||
        world->hregs.gcpu.fs > 0x100 ||
        world->hregs.gcpu.gs > 0x100 ||
        (world->hregs.gcpu.ss > 0x100 && world->hregs.gcpu.ss != 0x7eb) ||
        (world->gregs.cr3 & 0xfff) != 0 || world->hregs.gcpu.ldt > 0x100) {
        V_ERR("Sanity check failed \n");
        world->status = VM_PAUSED;
    }
}

static void
h_gdt_checkall(struct v_world *world)
{
    unsigned int boundary = world->gregs.gdt.limit + 1;
    if (world->hregs.gcpu.ds > world->gregs.gdt.limit)
        world->hregs.gcpu.ds = 0;
    if (world->hregs.gcpu.es > world->gregs.gdt.limit)
        world->hregs.gcpu.es = 0;
    if (world->hregs.gcpu.fs > world->gregs.gdt.limit)
        world->hregs.gcpu.fs = 0;
    if (world->hregs.gcpu.gs > world->gregs.gdt.limit)
        world->hregs.gcpu.gs = 0;
    if (boundary >= 0x7c0) {
        V_ERR("Warning: Very large GDT");
    } else {
        memset((void *) (world->hregs.gcpu.gdt.base + boundary), 0,
            0x7c0 - boundary);
    }
}

static inline void
h_fake_pe(struct v_world *world)
{
    void *gdt_entry = (void *) (world->hregs.gcpu.gdt.base);
    gdt_entry += 0x7e0;
    *(unsigned int *) gdt_entry = 0xffff | (world->hregs.gcpu.cs << 20);
    gdt_entry += 4;
    *(unsigned int *) gdt_entry =
        ((*(unsigned int *) gdt_entry) & (0xfffffff0)) |
        ((world->hregs.gcpu.cs >> 12) & 0xf);
    gdt_entry += 4;
    *(unsigned int *) gdt_entry = 0xffff | (world->hregs.gcpu.ss << 20);
    gdt_entry += 4;
    *(unsigned int *) gdt_entry =
        ((*(unsigned int *) gdt_entry) & (0xfffffff0)) |
        ((world->hregs.gcpu.ss >> 12) & 0xf);
}

#ifdef DEBUG_CODECONTROL
int last_is_cli = 0;
#endif

void
h_gpfault(struct v_world *world)
{
    void *virt;
    unsigned int g_ip = g_get_ip(world);
    h_addr_t map = h_get_map(world->htrbase, g_ip);
    unsigned char *inst;
    unsigned int parac;
    unsigned char bound[16];
    unsigned int mod66 = 0, csoverride = 0;
#ifdef DEBUG_CODECONTROL
    unsigned int this_is_cli = 0;
#endif
    V_LOG("GP Fault at %x: ", g_ip);
    if (map == 0) {
        V_LOG("PNP: Trying to resolve..");
        v_pagefault(world, g_ip, V_MM_FAULT_NP);
        map = h_get_map(world->htrbase, g_ip);
        if (map == 0) {
            V_LOG("Cannot proceed...");
            return;
        }
    }
    if (0xfff - (g_ip & 0xfff) < 0x10) {
        h_read_guest(world, g_ip, (unsigned int *) &bound[0]);
        h_read_guest(world, g_ip + 4, (unsigned int *) &bound[4]);
        h_read_guest(world, g_ip + 8, (unsigned int *) &bound[8]);
        h_read_guest(world, g_ip + 12, (unsigned int *) &bound[12]);
        inst = (unsigned char *) &bound;
    } else {
        virt = h_allocv(map & H_PFN_MASK);
        virt = virt + (g_ip & H_POFF_MASK);
        inst = virt;
    }
    if ((*inst) == 0x2e) {
        csoverride = 1;
        inst++;
        V_VERBOSE("CS:");
    }
    if ((*inst) == 0x66) {
        mod66 = 1;
        inst++;
        V_VERBOSE("MOD66");
    }
    switch (*inst) {
    case 0x26:
        if ((unsigned int) (*(unsigned int *) (inst)) == 0x3d010f26) {
            V_EVENT("invlpg");
            if (0 != world->gregs.ring) {
                world->gregs.has_errorc = 1;
                world->gregs.errorc = 0;
                h_inject_int(world, 0x0d);
                break;
            }
            h_inv_pagetable(world,
                v_spt_get_by_spt(world, world->htrbase), 0, 1);
            world->hregs.gcpu.eip += 8;
            break;
        } else if ((unsigned int) (*(unsigned int *) (inst)) == 0x38010f26) {
            V_EVENT("invlpg es:[eax]");
            if (0 != world->gregs.ring) {
                world->gregs.has_errorc = 1;
                world->gregs.errorc = 0;
                h_inject_int(world, 0x0d);
                break;
            }
            h_inv_pagetable(world,
                v_spt_get_by_spt(world, world->htrbase), 0, 1);
            world->hregs.gcpu.eip += 4;
            break;
        }
        goto undef_inst;
        break;
    case 0xfa:
        h_perf_inc(H_PERF_PI_INTSTATE, 1);
        V_EVENT("CLI:\n");
#ifdef DEBUG_CODECONTROL
        if (last_is_cli) {
            V_ALERT("two CLIs in a row. did we lose control?");
        }
        this_is_cli = 1;
#endif
        if (world->gregs.iopl < world->gregs.ring) {
            world->gregs.has_errorc = 1;
            world->gregs.errorc = 0;
            h_inject_int(world, 0x0d);
            break;
        }
        v_disable_int(world);
        world->gregs.eflags &= (~H_EFLAGS_IF);
        world->hregs.gcpu.eip++;
        break;
    case 0xfb:
        h_perf_inc(H_PERF_PI_INTSTATE, 1);
        V_EVENT("STI:\n");
        if (world->gregs.iopl < world->gregs.ring) {
            world->gregs.has_errorc = 1;
            world->gregs.errorc = 0;
            h_inject_int(world, 0x0d);
            break;
        }
        v_enable_int(world);
        world->gregs.eflags |= (H_EFLAGS_IF);
        world->hregs.gcpu.eip++;
        if (!(world->hregs.gcpu.eflags & H_EFLAGS_TF))
            g_pic_serve(world);
        break;
    case 0xcd:
        V_ALERT("INT:%x, %d(%x), %d(%x), %d(%x), %d(%x)",
            (unsigned int) (*(inst + 1)), world->hregs.gcpu.eax,
            world->hregs.gcpu.eax, world->hregs.gcpu.ebx,
            world->hregs.gcpu.ebx, world->hregs.gcpu.ecx,
            world->hregs.gcpu.ecx, world->hregs.gcpu.edx,
            world->hregs.gcpu.edx);
        if (v_do_int(world, (unsigned int) (*(inst + 1)))) {
            unsigned int int_no = (unsigned int) (*(inst + 1));
            V_LOG("Injecting int %x", int_no);
            world->hregs.gcpu.eip += 2;
            world->gregs.has_errorc = 0;
            h_inject_int(world, int_no);
            break;
        }
        world->hregs.gcpu.eip += 2;
        break;
    case 0xe4:
        /*no mod66 */
        h_perf_inc(H_PERF_PI_IO, 1);
        h_perf_inc(H_PERF_IO, 1);
        V_LOG("IO(in):%x", (unsigned int) (*(inst + 1)));
        g_do_io(world, G_IO_IN, (unsigned int) (*(inst + 1)),
            &world->hregs.gcpu.eax);
        world->hregs.gcpu.eip += 2;
        break;
    case 0xef:
        h_perf_inc(H_PERF_PI_IO, 1);
        if ((mod66 && g_get_current_ex_mode(world) == G_EX_MODE_16)
            || ((!mod66)
                && g_get_current_ex_mode(world) == G_EX_MODE_32)) {
            void *idx = &world->hregs.gcpu.eax;
            h_perf_inc(H_PERF_IO, 1);
            V_LOG("IO(out) dx(%x), eax(%x)",
                world->hregs.gcpu.edx & 0xffff, world->hregs.gcpu.eax);
            g_do_io(world, G_IO_OUT, world->hregs.gcpu.edx & 0xffff, idx);
            g_do_io(world, G_IO_OUT,
                (world->hregs.gcpu.edx & 0xffff) + 1, idx + 1);
            g_do_io(world, G_IO_OUT,
                (world->hregs.gcpu.edx & 0xffff) + 2, idx + 2);
            g_do_io(world, G_IO_OUT,
                (world->hregs.gcpu.edx & 0xffff) + 3, idx + 3);
        } else {
            void *idx = &world->hregs.gcpu.eax;
            h_perf_inc(H_PERF_IO, 1);
            V_LOG("IO(out) dx(%x), ax(%x)",
                world->hregs.gcpu.edx & 0xffff, world->hregs.gcpu.eax & 0xffff);
            g_do_io(world, G_IO_OUT, world->hregs.gcpu.edx & 0xffff, idx);
            g_do_io(world, G_IO_OUT,
                (world->hregs.gcpu.edx & 0xffff) + 1, idx + 1);
        }
        if (mod66)
            world->hregs.gcpu.eip += 1;
        world->hregs.gcpu.eip += 1;
        break;
    case 0xe6:
        /*no mod66 */
        h_perf_inc(H_PERF_PI_IO, 1);
        h_perf_inc(H_PERF_IO, 1);
        V_LOG("IO(out):%x, al(%x)", (unsigned int) (*(inst + 1)),
            world->hregs.gcpu.eax & 0xff);
        g_do_io(world, G_IO_OUT, (unsigned int) (*(inst + 1)),
            &world->hregs.gcpu.eax);
        world->hregs.gcpu.eip += 2;
        break;
    case 0xee:
        /*no mod66 */
        h_perf_inc(H_PERF_PI_IO, 1);
        h_perf_inc(H_PERF_IO, 1);
        V_LOG("IO(out) dx(%x), al(%x)", world->hregs.gcpu.edx & 0xffff,
            world->hregs.gcpu.eax & 0xff);
        g_do_io(world, G_IO_OUT, world->hregs.gcpu.edx & 0xffff,
            &world->hregs.gcpu.eax);
        world->hregs.gcpu.eip += 1;
        break;
    case 0xec:
        /*no mod66 */
        h_perf_inc(H_PERF_PI_IO, 1);
        h_perf_inc(H_PERF_IO, 1);
        V_LOG("IO(in) al(%x), dx(%x)", world->hregs.gcpu.eax & 0xff,
            world->hregs.gcpu.edx & 0xffff);
        g_do_io(world, G_IO_IN, world->hregs.gcpu.edx & 0xffff,
            &world->hregs.gcpu.eax);
        world->hregs.gcpu.eip += 1;
        break;
    case 0xed:
        h_perf_inc(H_PERF_PI_IO, 1);
        h_perf_inc(H_PERF_IO, 1);
        if ((mod66 && g_get_current_ex_mode(world) == G_EX_MODE_16)
            || ((!mod66)
                && g_get_current_ex_mode(world) == G_EX_MODE_32)) {
            void *idx = &world->hregs.gcpu.eax;
            h_perf_inc(H_PERF_IO, 1);
            V_LOG("IO(in) eax(%x), dx(%x)", world->hregs.gcpu.eax,
                world->hregs.gcpu.edx & 0xffff);
            g_do_io(world, G_IO_IN, world->hregs.gcpu.edx & 0xffff, idx);
            g_do_io(world, G_IO_IN,
                (world->hregs.gcpu.edx & 0xffff) + 1, idx + 1);
            g_do_io(world, G_IO_IN,
                (world->hregs.gcpu.edx & 0xffff) + 2, idx + 2);
            g_do_io(world, G_IO_IN,
                (world->hregs.gcpu.edx & 0xffff) + 3, idx + 3);
        } else {
            void *idx = &world->hregs.gcpu.eax;
            h_perf_inc(H_PERF_IO, 1);
            V_LOG("IO(out) ax(%x), dx(%x)",
                world->hregs.gcpu.eax & 0xffff, world->hregs.gcpu.edx & 0xffff);
            g_do_io(world, G_IO_IN, world->hregs.gcpu.edx & 0xffff, idx);
            g_do_io(world, G_IO_IN,
                (world->hregs.gcpu.edx & 0xffff) + 1, idx + 1);
        }
        if (mod66)
            world->hregs.gcpu.eip += 1;
        world->hregs.gcpu.eip += 1;
        break;
    case 0x8c:
        h_perf_inc(H_PERF_PI_LOADSEG, 1);
        V_LOG("MOV from SEG into REG %x", (unsigned int) (*(inst + 1)));
        if (world->gregs.mode == G_MODE_REAL) {
            world->status = VM_PAUSED;
        } else {
            unsigned int seg = ((unsigned int) (*(inst + 1)) & 0xf8);
            unsigned int reg = (unsigned int) (*(inst + 1)) & 0x7;
            unsigned int *target = &world->hregs.gcpu.eax;
            target = target - reg;
            if (*(inst + 1) == 0xa3) {
                unsigned int off = *(unsigned int *) (inst + 2);
                h_update(fs, 0, 0);
                h_update(ds, 0, 0);
                V_LOG("mov [ebx]%x, fs", off);
                h_write_guest(world,
                    world->hregs.gcpu.ebx + off +
                    g_sel2base(world->gregs.ds,
                        world->gregs.dshigh), world->gregs.fstrue);
                world->hregs.gcpu.eip += 4;
            } else if (*(inst + 1) == 0xab) {
                unsigned int off = *(unsigned int *) (inst + 2);
                h_update(gs, 0, 0);
                h_update(ds, 0, 0);
                V_LOG("mov [ebx]%x, gs", off);
                h_write_guest(world,
                    world->hregs.gcpu.ebx + off +
                    g_sel2base(world->gregs.ds,
                        world->gregs.dshigh), world->gregs.gstrue);
                world->hregs.gcpu.eip += 4;
            } else if (*(inst + 1) == 0xa6) {
                unsigned int off = *(unsigned int *) (inst + 2);
                h_update(fs, 0, 0);
                h_update(ds, 0, 0);
                V_LOG("mov [esi]%x, fs", off);
                h_write_guest(world,
                    world->hregs.gcpu.esi + off +
                    g_sel2base(world->gregs.ds,
                        world->gregs.dshigh), world->gregs.fstrue);
                world->hregs.gcpu.eip += 4;
            } else if (*(inst + 1) == 0xae) {
                unsigned int off = *(unsigned int *) (inst + 2);
                h_update(gs, 0, 0);
                h_update(ds, 0, 0);
                V_LOG("mov [esi]%x, gs", off);
                h_write_guest(world,
                    world->hregs.gcpu.esi + off +
                    g_sel2base(world->gregs.ds,
                        world->gregs.dshigh), world->gregs.gstrue);
                world->hregs.gcpu.eip += 4;
            } else {
                switch (seg) {
                case 0xc0:
                    h_update(es, 0, 0);
                    *target = world->gregs.estrue;
                    break;
                case 0xc8:
                    if (!world->gregs.zombie_jumped)
                        *target = world->gregs.zombie_cs;
                    else
                        *target = world->gregs.cstrue;
                    break;
                case 0xd0:
                    h_update(ss, 0, 1);
                    *target = world->gregs.sstrue;
                    break;
                case 0xd8:
                    h_update(ds, 0, 0);
                    *target = world->gregs.dstrue;
                    break;
                case 0xe0:
                    h_update(fs, 0, 0);
                    *target = world->gregs.fstrue;
                    break;
                case 0xe8:
                    h_update(gs, 0, 0);
                    *target = world->gregs.gstrue;
                    break;
                default:
                    V_ERR("unhandled seg number");
                    world->status = VM_PAUSED;
                }
            }
        }
        if (mod66) {
            world->hregs.gcpu.eip++;
        }
        world->hregs.gcpu.eip += 2;
        break;
    case 0x0f:
        h_perf_inc(H_PERF_PI_LOADSEG, 1);
        if ((unsigned int) (*(inst + 1)) == 0x01) {
            V_EVENT("LTables:");
            V_LOG("inst is %x %x %x %x %x %x %x %x\n",
                (unsigned int) (*(inst + 0)), (unsigned int) (*(inst + 1)),
                (unsigned int) (*(inst + 2)), (unsigned int) (*(inst + 3)),
                (unsigned int) (*(inst + 4)), (unsigned int) (*(inst + 5)),
                (unsigned int) (*(inst + 6)), (unsigned int) (*(inst + 7)));
            if ((unsigned int) (*(inst + 2)) >= 0x38
                && (unsigned int) (*(inst + 2)) <= 0x3f) {
                V_LOG("INVLPG [reg]");
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                h_inv_pagetable(world, v_spt_get_by_spt(world,
                        world->htrbase), 0, 1);
                world->hregs.gcpu.eip += 3;
                break;
            }
            if ((unsigned int) (*(inst + 2)) == 0x16) {
                struct v_page *mpage;
                void *virt;
                g_addr_t addr;
                if (world->gregs.mode == G_MODE_REAL)
                    addr =
                        (unsigned
                        int) (*(unsigned short *) (inst +
                            3)) +
                        (csoverride ? (world->hregs.gcpu.
                            cs << 4) : (world->hregs.gcpu.v86ds << 4));
                else {
                    addr =
                        g_sel2base(world->gregs.ds,
                        world->gregs.dshigh) +
                        (unsigned int) (*(unsigned short *) (inst + 3));
                }
                if (world->gregs.mode != G_MODE_REAL
                    && g_get_current_ex_mode(world) == G_EX_MODE_32) {
                    V_ERR("Unimplemented 32 bit lgdt");
                    world->status = VM_PAUSED;
                    break;
                }
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                addr = g_v2p(world, addr, 0);
                mpage = h_p2mp(world, addr);
                if (mpage == NULL) {
                    V_LOG("LGDT x86/16 Guest GP Fault\n");
                    break;
                }
                virt = h_allocv(mpage->mfn << H_PAGE_SHIFT);
                V_LOG("ds mem: %x /", addr);
                h_memcpy(&world->gregs.gdt,
                    (void *) (((unsigned int) (virt) & H_PFN_MASK)
                        + (addr & H_POFF_MASK)), sizeof(struct g_desc_table));
                V_LOG("gdt %x limit %x\n",
                    world->gregs.gdt.base, world->gregs.gdt.limit);
                if (mod66)
                    world->hregs.gcpu.eip += 1;
                if (csoverride)
                    world->hregs.gcpu.eip += 1;
                world->hregs.gcpu.eip += 5;
                v_bt_reset(world);
                break;
            }
            if ((unsigned int) (*(inst + 2)) == 0xd1) {
                V_LOG("xsetv %x", world->hregs.gcpu.ecx);
                world->hregs.gcpu.eip += 3;
                break;
            }
            if ((unsigned int) (*(inst + 2)) == 0x55) {
                /* lgdt 32 bit [ebp][1byteoff]/16 bit [di][1byteoff] */
                struct v_page *mpage;
                void *virt;
                g_addr_t addr;
                if (world->gregs.mode == G_MODE_REAL)
                    addr =
                        (int) (*(char *) (inst +
                            3)) + (world->hregs.gcpu.edi & 0xffff) +
                        (csoverride ? (world->hregs.gcpu.
                            cs << 4) : (world->hregs.gcpu.v86es << 4));
                else {
                    addr =
                        g_sel2base(world->gregs.ss,
                        world->gregs.sshigh) + world->hregs.gcpu.ebp +
                        (int) (*(char *) (inst + 3));
                }
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                addr = g_v2p(world, addr, 0);
                mpage = h_p2mp(world, addr);
                if (mpage == NULL) {
                    V_LOG("LGDT x86/16 Guest GP Fault\n");
                    break;
                }
                virt = h_allocv(mpage->mfn << H_PAGE_SHIFT);
                V_LOG("mem: %x /", addr);
                h_memcpy(&world->gregs.gdt,
                    (void *) (((unsigned int) (virt) & H_PFN_MASK)
                        + (addr & H_POFF_MASK)), sizeof(struct g_desc_table));
                V_LOG("gdt %x limit %x\n",
                    world->gregs.gdt.base, world->gregs.gdt.limit);
                if (mod66)
                    world->hregs.gcpu.eip += 1;
                if (csoverride)
                    world->hregs.gcpu.eip += 1;
                world->hregs.gcpu.eip += 4;
                v_bt_reset(world);
                break;
            }
            if ((unsigned int) (*(inst + 2)) == 0x15) {
                struct v_page *mpage;
                void *virt;
                g_addr_t addr;
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                if (world->gregs.mode != G_MODE_REAL
                    && g_get_current_ex_mode(world) == G_EX_MODE_16) {
                    V_ERR("Unimplemented 16 bit lgdt");
                    world->status = VM_PAUSED;
                    break;
                }
                h_update(ds, 0, 1);
                addr =
                    g_sel2base(world->gregs.ds,
                    world->gregs.dshigh) +
                    (unsigned int) (*(unsigned int *) (inst + 3));
                V_LOG("addr = %x, instaddr = %x/", addr,
                    (unsigned int) (*(unsigned int *) (inst + 3)));
                addr = g_v2p(world, addr, 0);
                mpage = h_p2mp(world, addr);
                if (mpage == NULL) {
                    V_LOG("LGDT Guest GP Fault\n");
                    break;
                }
                virt = h_allocv(mpage->mfn << H_PAGE_SHIFT);
                V_LOG("ds mem: %x /", addr);
                h_memcpy(&world->gregs.gdt,
                    (void *) (((unsigned int) (virt) & H_PFN_MASK)
                        + (addr & H_POFF_MASK)), sizeof(struct g_desc_table));
                V_LOG("gdt %x limit %x\n",
                    world->gregs.gdt.base, world->gregs.gdt.limit);
                h_gdt_checkall(world);
                world->hregs.gcpu.eip += 7;
                v_bt_reset(world);
                break;
            }
            if ((unsigned int) (*(inst + 2)) == 0x1d) {
                struct v_page *mpage;
                void *virt;
                g_addr_t addr;
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                if (g_get_current_ex_mode(world) == G_EX_MODE_16) {
                    V_ERR("Unimplemented 16 bit lidt");
                    world->status = VM_PAUSED;
                    break;
                }
                addr =
                    g_sel2base(world->gregs.ds,
                    world->gregs.dshigh) +
                    (unsigned int) (*(unsigned int *) (inst + 3));
                addr = g_v2p(world, addr, 0);
                mpage = h_p2mp(world, addr);
                if (mpage == NULL) {
                    V_LOG("LIDT Guest GP Fault\n");
                    break;
                }
                virt = h_allocv(mpage->mfn << H_PAGE_SHIFT);
                V_LOG("ds mem: %x /", addr);
                h_memcpy(&world->gregs.idt,
                    (void *) (((unsigned int) (virt) & H_PFN_MASK)
                        + (addr & H_POFF_MASK)), sizeof(struct g_desc_table));
                V_LOG("idt %x limit %x\n",
                    world->gregs.idt.base, world->gregs.idt.limit);
                world->hregs.gcpu.eip += 7;
                v_bt_reset(world);
                break;
            }
            if ((unsigned int) (*(inst + 2)) == 0x1e) {
                struct v_page *mpage;
                void *virt;
                g_addr_t addr;
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                if (world->gregs.mode == G_MODE_REAL) {
                    addr =
                        (world->hregs.gcpu.v86ds << 4) +
                        (unsigned short) (*(unsigned short *) (inst + 3));
                } else {
                    if (g_get_current_ex_mode(world) == G_EX_MODE_32) {
                        V_ERR("Unimplemented 32 bit lidt");
                        world->status = VM_PAUSED;
                        break;
                    }
                    addr =
                        g_sel2base(world->gregs.ds,
                        world->gregs.dshigh) +
                        (unsigned short) (*(unsigned short *) (inst + 3));
                }
                addr = g_v2p(world, addr, 0);
                mpage = h_p2mp(world, addr);
                if (mpage == NULL) {
                    V_LOG("LIDT Guest GP Fault\n");
                    break;
                }
                virt = h_allocv(mpage->mfn << H_PAGE_SHIFT);
                V_LOG("ds mem: %x /", addr);
                h_memcpy(&world->gregs.idt,
                    (void *) (((unsigned int) (virt) & H_PFN_MASK)
                        + (addr & H_POFF_MASK)), sizeof(struct g_desc_table));
                V_LOG("idt %x limit %x\n",
                    world->gregs.idt.base, world->gregs.idt.limit);
                if (mod66)
                    world->hregs.gcpu.eip += 1;
                world->hregs.gcpu.eip += 5;
                v_bt_reset(world);
                break;
            }
            if ((unsigned int) (*(inst + 2)) >= 0xf0
                && (unsigned int) (*(inst + 2)) <= 0xf7) {
                int reg = (unsigned int) (*(inst + 2)) & 0xf;
                unsigned int *target = &world->hregs.gcpu.eax;
                V_ALERT("LMSW %x at EIP: %lx", reg, g_get_ip(world));
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                target = target - reg;
                if (((*target) & 1)
                    && world->gregs.mode == G_MODE_REAL) {
                    //we just entered PE from REAL, fake selectors
                    V_EVENT("Entering protected mode");
                    world->gregs.zombie_cs = world->hregs.gcpu.cs;
                    world->gregs.zombie_jumped = 0;
                    h_fake_pe(world);
                    world->hregs.gcpu.cs = 0x7e3;
                    world->hregs.gcpu.ss = 0x7eb;
                    world->hregs.gcpu.eflags &= (~H_EFLAGS_VM);
                    world->hregs.gcpu.trsave.esp0 =
                        (unsigned int) (&world->hregs.gcpu.v86es);
                    world->gregs.mode = G_MODE_PE;
                    world->gregs.cr0 |= 1;
                    h_new_trbase(world);
                } else {
                    world->status = VM_PAUSED;
                    V_ERR("unhandled lmsw");
                }
                world->hregs.gcpu.eip += 3;
                v_bt_reset(world);
                break;
            }
            goto undef_inst;
            break;
        } else if ((unsigned int) (*(inst + 1)) == 0x20) {
            int reg = (unsigned int) (*(inst + 2)) & 0xf;
            int cr = (unsigned int) (*(inst + 2)) & 0xf0;
            unsigned int *target = &world->hregs.gcpu.eax;
            V_LOG("MOV from CR into reg %x, cr0 = %x, cr3 = %x", reg,
                world->gregs.cr0, world->gregs.cr3);
            if (0 != world->gregs.ring) {
                world->gregs.has_errorc = 1;
                world->gregs.errorc = 0;
                h_inject_int(world, 0x0d);
                break;
            }
            if (cr == 0xc0) {
                target = target - reg;
                *target = world->gregs.cr0;
            } else if (cr == 0xd0) {
                if (reg >= 8) {
                    target = target - (reg - 8);
                    V_LOG("Read CR3");
                    *target = world->gregs.cr3;
                } else {
                    target = target - reg;
                    V_LOG("Read CR2");
                    *target = world->gregs.cr2;
                }

            }
            world->hregs.gcpu.eip += 3;
            break;
        } else if ((unsigned int) (*(inst + 1)) == 0x22) {
            int reg = (unsigned int) (*(inst + 2)) & 0xf;
            int cr = (unsigned int) (*(inst + 2)) & 0xf0;
            unsigned int *target = &world->hregs.gcpu.eax;
            V_EVENT("MOV into CR %x from reg %x", cr, reg);
            if (0 != world->gregs.ring) {
                world->gregs.has_errorc = 1;
                world->gregs.errorc = 0;
                h_inject_int(world, 0x0d);
                break;
            }
            if (cr == 0xc0) {
                int newmode;
                target = target - reg;
                newmode = *target;
                if ((newmode & 0x80000001) != (world->gregs.cr0 & 0x80000001)) {
                    V_ALERT("mode change occured, %x to %x", world->gregs.mode,
                        newmode);
                    if ((newmode & 0x80000000)
                        && (newmode & 0x1)) {
                        v_bt_reset(world);
                        world->gregs.mode = G_MODE_PG;
                    }
                    if ((!(newmode & 0x80000000))
                        && (newmode & 0x1)) {
                        if (world->gregs.mode == G_MODE_REAL) {
                            //we just entered PE from REAL, fake selectors
                            world->gregs.zombie_cs = world->hregs.gcpu.cs;
                            world->gregs.zombie_ss = world->hregs.gcpu.ss;
                            world->gregs.zombie_jumped = 0;
                            h_fake_pe(world);
                            world->hregs.gcpu.cs = 0x7e3;
                            world->hregs.gcpu.ss = 0x7eb;
                            world->hregs.gcpu.eflags &= (~H_EFLAGS_VM);
                            world->hregs.gcpu.trsave.esp0 =
                                (unsigned int) (&world->hregs.gcpu.v86es);
                        }
                        world->gregs.mode = G_MODE_PE;
                    }
                    if ((!(newmode & 0x80000000))
                        && (!(newmode & 0x1))) {
                        V_LOG("return to real mode.");
                        world->gregs.mode = G_MODE_REAL;
                        world->hregs.gcpu.cs = world->gregs.zombie_cs;
                        world->hregs.gcpu.ss = world->gregs.zombie_ss;
                        world->hregs.gcpu.eflags |= H_EFLAGS_VM;
                        world->hregs.gcpu.trsave.esp0 =
                            (unsigned int) (&world->hregs.gcpu.cpuid0);
                        v_bt_reset(world);
                    }
                    if ((newmode & 0x80000000)
                        && (!(newmode & 0x1))) {
                        world->status = VM_PAUSED;
                        V_LOG("Unimplmented: inject GP fault");
                    }
                    h_new_trbase(world);
                }
                world->gregs.cr0 = newmode;
            } else if (cr == 0xd0) {
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                target = target - (reg - 8);
                V_EVENT("Guest CR3 set to: %x", *target);
                v_bt_reset(world);
                world->gregs.cr3 = *target;
                h_new_trbase(world);
            }
            world->hregs.gcpu.eip += 3;
            break;
        } else if ((unsigned int) (*(inst + 1)) == 0x23) {
            V_ALERT("mov into dr");
            world->hregs.gcpu.eip += 3;
            break;
        } else if ((unsigned int) (*(inst + 1)) == 0x00) {
            if (((unsigned int) (*(inst + 2)) & 0xf8) == 0xd8) {
                int reg = (unsigned int) (*(inst + 2)) & 0x7;
                unsigned int *target = &world->hregs.gcpu.eax;
                target = target - reg;
                V_LOG("LTR reg %x:%x / ", reg, *target);
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                if (!h_gdt_load
                    (world, &world->gregs.tr, *target, 0,
                        &world->gregs.tr_hid, 0)) {
                    V_LOG("Guest GP Fault:");
                    world->status = VM_PAUSED;
                }
                world->hregs.gcpu.eip += 3;
                break;
            } else if (((unsigned int) (*(inst + 2)) & 0xf8) == 0xd0) {
                int reg = (unsigned int) (*(inst + 2)) & 0x7;
                unsigned int *target = &world->hregs.gcpu.eax;
                target = target - reg;
                V_ALERT("lldt reg %x:%x / ", reg, *target);
                if (0 != world->gregs.ring) {
                    world->gregs.has_errorc = 1;
                    world->gregs.errorc = 0;
                    h_inject_int(world, 0x0d);
                    break;
                }
                if (*target == 0) {
                    world->gregs.ldt = world->gregs.ldt_true = 0;
                } else if (!h_gdt_load(world, &world->gregs.ldt,
                        *target, 0, &world->gregs.ldt_hid, 0)) {
                    V_LOG("Guest GP Fault:");
                    world->status = VM_PAUSED;
                }
                world->hregs.gcpu.eip += 3;
                break;
            }
            goto undef_inst;
        } else if ((unsigned int) (*(inst + 1)) == 0x06) {
            V_LOG("CLTS");
            if (world->gregs.ring != 0) {
                V_LOG("Guest GP Fault, clts");
                world->status = VM_PAUSED;
            } else {
                world->gregs.nt = 0;
                world->hregs.gcpu.eip += 2;
            }
            break;
        } else if ((unsigned int) (*(inst + 1)) == 0xb2) {
            if ((unsigned int) (*(inst + 2)) == 0x25) {
                if ((!mod66)
                    && (g_get_current_ex_mode(world) == G_EX_MODE_32)) {
                    int addr = *(unsigned int *) (inst + 3);
                    int lo, hi;
                    h_read_guest(world, addr, &lo);
                    h_read_guest(world, addr + 2, &hi);
                    hi = (hi >> 16);
                    V_ALERT("LSS esp, [%x] : %x, %x", addr, lo, hi);
                    if (h_gdt_load(world,
                            &world->hregs.gcpu.ss, hi, 0,
                            &world->gregs.ss, 1)) {
                        V_LOG("guest gp fault");
                        world->status = VM_PAUSED;
                    }
                    world->hregs.gcpu.esp = lo;
                    world->hregs.gcpu.eip += 7;
                    break;
                }
            }
        } else if ((unsigned int) (*(inst + 1)) == 0x32) {
            union {
                struct {
                    unsigned long low;
                    unsigned long high;
                } part;
                unsigned long long full;
            } ret;
            asm volatile ("rdmsr":"=a" (ret.part.low),
                "=d"(ret.part.high):"c"(world->hregs.gcpu.ecx));
            V_LOG("rdmsr %x is %lx %lx", world->hregs.gcpu.ecx, ret.part.low,
                ret.part.high);
            world->hregs.gcpu.eax = ret.part.low;
            world->hregs.gcpu.edx = ret.part.high;
            world->hregs.gcpu.eip += 2;
            break;
        } else if ((unsigned int) (*(inst + 1)) == 0x30) {
            V_LOG("wrmsr %x is %x %x", world->hregs.gcpu.ecx,
                world->hregs.gcpu.eax, world->hregs.gcpu.edx);
            world->hregs.gcpu.eip += 2;
            break;
        } else if ((unsigned int) (*(inst + 1)) == 0x09) {
            V_LOG("wbinvd");
            world->hregs.gcpu.eip += 2;
            break;
        }
        goto undef_inst;
        break;
    case 0x8e:
        h_perf_inc(H_PERF_PI_LOADSEG, 1);
        if (((unsigned int) (*(inst + 1)) <= 0xd7)
            && ((unsigned int) (*(inst + 1)) >= 0xd0)) {
            int reg = (unsigned int) (*(inst + 1)) & 0xf;
            unsigned int *target = &world->hregs.gcpu.eax;
            V_LOG("Load SS:");
            if (h_gdt_load
                (world, &world->hregs.gcpu.ss, *(target - reg), 0,
                    &world->gregs.ss, 1)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.eip += 2;
            break;
        } else if (((unsigned int) (*(inst + 1)) <= 0xdf)
            && ((unsigned int) (*(inst + 1)) >= 0xd8)) {
            int reg = (unsigned int) (*(inst + 1)) & 0xf;
            unsigned int *target = &world->hregs.gcpu.eax;
            V_LOG("Load DS:");
            if (h_gdt_load
                (world, &world->hregs.gcpu.ds,
                    *(target - (reg - 8)), 0, &world->gregs.ds, 0)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.eip += 2;
            break;
        }
        if (((unsigned int) (*(inst + 1)) <= 0xc7)
            && ((unsigned int) (*(inst + 1)) >= 0xc0)) {
            int reg = (unsigned int) (*(inst + 1)) & 0xf;
            unsigned int *target = &world->hregs.gcpu.eax;
            V_LOG("Load ES:");
            if (h_gdt_load
                (world, &world->hregs.gcpu.es, *(target - reg), 0,
                    &world->gregs.es, 0)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.eip += 2;
            break;
        }
        if (((unsigned int) (*(inst + 1)) <= 0xe7)
            && ((unsigned int) (*(inst + 1)) >= 0xe0)) {
            int reg = (unsigned int) (*(inst + 1)) & 0xf;
            unsigned int *target = &world->hregs.gcpu.eax;
            V_LOG("Load FS:");
            if (h_gdt_load
                (world, &world->hregs.gcpu.fs, *(target - reg), 0,
                    &world->gregs.fs, 0)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.eip += 2;
            break;
        }
        if (((unsigned int) (*(inst + 1)) <= 0xef)
            && ((unsigned int) (*(inst + 1)) >= 0xe8)) {
            int reg = (unsigned int) (*(inst + 1)) & 0xf;
            unsigned int *target = &world->hregs.gcpu.eax;
            V_LOG("Load GS:");
            if (h_gdt_load
                (world, &world->hregs.gcpu.gs,
                    *(target - (reg - 8)), 0, &world->gregs.gs, 0)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.eip += 2;
            break;
        }
        goto undef_inst;
        break;
    case 0xca:
        parac = ((unsigned int) (*(inst + 1)));
        V_EVENT("RETF with %x:", parac);
        h_do_return(world, parac, 0);
        break;
    case 0x16:
        h_perf_inc(H_PERF_PI_LOADSEG, 1);
        V_LOG("PUSH SS");
        if (g_get_current_ex_mode(world) == G_EX_MODE_16) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss = world->gregs.sstrue;
            ss = ss << 16;
            h_write_guest(world, sp - 4, ss);
            world->hregs.gcpu.esp -= 2;
            world->hregs.gcpu.eip++;
        } else if (g_get_current_ex_mode(world) == G_EX_MODE_32) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss = world->gregs.sstrue;
            h_write_guest(world, sp - 4, ss);
            world->hregs.gcpu.esp -= 4;
            world->hregs.gcpu.eip++;
        }
        break;
    case 0x17:
        h_perf_inc(H_PERF_PI_LOADSEG, 1);
        V_LOG("POP SS");
        if (g_get_current_ex_mode(world) == G_EX_MODE_16) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss;
            h_read_guest(world, sp, &ss);
            ss = ss & 0xffff;
            if (h_gdt_load
                (world, &world->hregs.gcpu.ss, ss, 0, &world->gregs.ss, 1)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.esp += 2;
            world->hregs.gcpu.eip++;
        } else if (g_get_current_ex_mode(world) == G_EX_MODE_32) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss;
            h_read_guest(world, sp, &ss);
            ss = ss & 0xffff;
            if (h_gdt_load
                (world, &world->hregs.gcpu.ss, ss, 0, &world->gregs.ss, 1)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.esp += 4;
            world->hregs.gcpu.eip++;
        }
        break;
    case 0x1f:
        h_perf_inc(H_PERF_PI_LOADSEG, 1);
        V_LOG("POP DS");
        if (g_get_current_ex_mode(world) == G_EX_MODE_16) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss;
            h_read_guest(world, sp, &ss);
            ss = ss & 0xffff;
            V_EVENT("new ds: %x", ss);
            if (h_gdt_load
                (world, &world->hregs.gcpu.ds, ss, 0, &world->gregs.ds, 1)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.esp += 2;
            world->hregs.gcpu.eip++;
        } else if (g_get_current_ex_mode(world) == G_EX_MODE_32) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss;
            h_read_guest(world, sp, &ss);
            ss = ss & 0xffff;
            V_EVENT("new ds: %x", ss);
            if (h_gdt_load
                (world, &world->hregs.gcpu.ds, ss, 0, &world->gregs.ds, 1)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.esp += 4;
            world->hregs.gcpu.eip++;
        }
        break;
    case 0x07:
        h_perf_inc(H_PERF_PI_LOADSEG, 1);
        V_LOG("POP ES");
        if (g_get_current_ex_mode(world) == G_EX_MODE_16) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss;
            h_read_guest(world, sp, &ss);
            ss = ss & 0xffff;
            V_EVENT("new es: %x", ss);
            if (h_gdt_load
                (world, &world->hregs.gcpu.es, ss, 0, &world->gregs.es, 1)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.esp += 2;
            world->hregs.gcpu.eip++;
        } else if (g_get_current_ex_mode(world) == G_EX_MODE_32) {
            unsigned int sp = g_get_sp(world);
            unsigned int ss;
            h_read_guest(world, sp, &ss);
            ss = ss & 0xffff;
            V_EVENT("new es: %x", ss);
            if (h_gdt_load
                (world, &world->hregs.gcpu.es, ss, 0, &world->gregs.es, 1)) {
                V_LOG("guest gp fault");
            }
            world->hregs.gcpu.esp += 4;
            world->hregs.gcpu.eip++;
        }
        break;
    case 0xcb:
        V_EVENT("RETF:");
        h_do_return(world, 0, 0);
        break;
    case 0x9c:
        h_perf_inc(H_PERF_PI_FLAGS, 1);
        V_EVENT("PUSHF:");
        if (world->gregs.mode == G_MODE_REAL) {
            unsigned int sp;
            unsigned int eflags = world->hregs.gcpu.eflags;
            world->hregs.gcpu.esp = world->hregs.gcpu.esp & 0xffff;
            sp = g_get_sp(world);
            if (v_int_enabled(world))
                eflags |= H_EFLAGS_IF;
            else
                eflags &= (~H_EFLAGS_IF);
            eflags |= world->gregs.eflags_real;
            if (!mod66) {
                eflags = (eflags << 16);
                world->hregs.gcpu.esp -= 2;
            } else {
                world->hregs.gcpu.esp -= 4;
                world->hregs.gcpu.eip++;
            }
            h_write_guest(world, sp - 4, eflags);
            world->hregs.gcpu.eip++;
            break;

        } else {
            unsigned int eflags = world->hregs.gcpu.eflags;
            int is16 = g_get_current_ex_mode(world) == G_EX_MODE_16;
            if (v_int_enabled(world))
                eflags |= H_EFLAGS_IF;
            else
                eflags &= (~H_EFLAGS_IF);
            eflags &= 0xffffcfff;
            eflags |= (world->gregs.iopl << 12);
            if (world->gregs.nt)
                eflags |= H_EFLAGS_NT;
            if ((is16 && !mod66) || (!is16 && mod66)) {
                unsigned int sp = g_get_sp(world);
                eflags = eflags << 16;
                h_write_guest(world, sp - 4, eflags);
                world->hregs.gcpu.esp -= 2;
                world->hregs.gcpu.eip++;
            } else {
                unsigned int sp = g_get_sp(world);
                h_write_guest(world, sp - 4, eflags);
                world->hregs.gcpu.esp -= 4;
                world->hregs.gcpu.eip++;
            }
            break;
        }
        break;
    case 0x9d:
        h_perf_inc(H_PERF_PI_FLAGS, 1);
        V_EVENT("POPF:");
        if (world->gregs.mode == G_MODE_REAL) {
            unsigned int sp;
            unsigned int eflags;
            world->hregs.gcpu.esp = world->hregs.gcpu.esp & 0xffff;
            sp = g_get_sp(world);
            if (!mod66) {
                h_read_guest(world, sp - 2, &eflags);
                eflags = (eflags >> 16);
                world->hregs.gcpu.eflags &= 0xfffff000;
            } else {
                h_read_guest(world, sp, &eflags);
                world->hregs.gcpu.esp += 2;
                world->hregs.gcpu.eip++;
                world->hregs.gcpu.eflags &= 0xffc3f000;
            }
            V_LOG("New eflags: %x", eflags);
            if (eflags & 0x200)
                v_enable_int(world);
            else
                v_disable_int(world);
            world->hregs.gcpu.eflags |=
                ((eflags & 0x3e0cd7) | (H_EFLAGS_IF | H_EFLAGS_RESERVED));
            world->gregs.eflags_real = eflags & 0xc000;
            world->hregs.gcpu.esp += 2;
            world->hregs.gcpu.eip++;
            break;

        } else {
            unsigned int eflags;
            int is16 = g_get_current_ex_mode(world) == G_EX_MODE_16;
            if ((is16 && !mod66) || (!is16 && mod66)) {
                unsigned int sp = g_get_sp(world);
                h_read_guest(world, sp, &eflags);
                eflags = eflags & 0xffff;
                world->hregs.gcpu.esp += 2;
                world->hregs.gcpu.eip++;
            } else {
                unsigned int sp = g_get_sp(world);
                h_read_guest(world, sp, &eflags);
                world->hregs.gcpu.esp += 4;
                world->hregs.gcpu.eip++;
            }
            world->hregs.gcpu.eflags =
                (world->hregs.
                gcpu.eflags & H_EFLAGS_RF) | (eflags & 0x3e0cd7) | (H_EFLAGS_IF
                | H_EFLAGS_RESERVED);
            V_LOG("new eflags = %x", eflags);
            if (world->gregs.ring == 0) {
                if (eflags & H_EFLAGS_IF)
                    v_enable_int(world);
                else
                    v_disable_int(world);
                world->gregs.iopl = (eflags & H_EFLAGS_IOPL_MASK) >> 12;
                world->gregs.nt = (eflags & H_EFLAGS_NT) >> 14;
            }
            break;
        }
        break;
    case 0xcf:
        V_ALERT("IRET:");
        h_do_return(world, 0, 1);
        break;
    case 0x64:
        if ((unsigned int) (*(inst + 1)) == 0x6f) {
            V_EVENT("OUTSD/OUTSW");
            world->hregs.gcpu.eip += 2;
        } else {
            world->status = VM_PAUSED;
        }
        break;
    case 0x6f:
        h_perf_inc(H_PERF_PI_IO, 1);
        V_ALERT("OUTSD/OUTSW");
        world->hregs.gcpu.eip += 1;
        break;
    case 0x6e:
        h_perf_inc(H_PERF_PI_IO, 1);
        V_ALERT("OUTSB");
        world->hregs.gcpu.eip += 1;
        break;
    case 0x6c:
        h_perf_inc(H_PERF_PI_IO, 1);
        V_ALERT("INSB");
        world->hregs.gcpu.eip += 1;
        break;
    case 0x6d:
        h_perf_inc(H_PERF_PI_IO, 1);
        V_ALERT("INSD/INSW");
        world->hregs.gcpu.eip += 1;
        break;
    case 0xf4:
        if (world->gregs.ring == 0) {
            V_VERBOSE("Processor requested halt");
            world->hregs.gcpu.eip += 1;
            world->status = VM_IDLE;
        } else {
            V_ERR("userspace hlt");
            world->status = VM_PAUSED;
        }
        break;
    case 0xea:
        V_EVENT("Jmp abs");
        world->gregs.zombie_jumped = 1;
        if (((world->gregs.cshigh & 0x00400000) && (!mod66))
            || ((!(world->gregs.cshigh & 0x00400000)) && (mod66))) {
            unsigned int off = *(unsigned int *) (inst + 1);
            unsigned int sel = *(unsigned short *) (inst + 5);
            unsigned int dummy;
            V_LOG("32 bit jump to %x:%x /", sel, off);
            if (mod66)
                world->hregs.gcpu.eip++;
            if (h_gdt_load(world, &dummy, sel, 1, &world->gregs.cstemp, 1)) {
                world->hregs.gcpu.eip += 7;
                h_task_switch(world, sel, 0, 0);
            } else {
                v_bt_reset(world);
                world->hregs.gcpu.cs = dummy;
                world->gregs.cs = world->gregs.cstemp;
                world->gregs.cshigh = world->gregs.cstemphigh;
                world->gregs.cstrue = world->gregs.cstemptrue;
                world->hregs.gcpu.eip = off;
            }
        } else {
            unsigned int off = *(unsigned short *) (inst + 1);
            unsigned int sel = *(unsigned short *) (inst + 3);
            unsigned int dummy;
            V_LOG("16 bit jump to %x:%x /", sel, off);
            if (mod66)
                world->hregs.gcpu.eip++;
            if (h_gdt_load(world, &dummy, sel, 1, &world->gregs.cstemp, 1)) {
                world->hregs.gcpu.eip += 5;
                h_task_switch(world, sel, 0, 0);
            } else {
                v_bt_reset(world);
                world->hregs.gcpu.cs = dummy;
                world->gregs.cs = world->gregs.cstemp;
                world->gregs.cshigh = world->gregs.cstemphigh;
                world->gregs.cstrue = world->gregs.cstemptrue;
                world->hregs.gcpu.eip = off;
            }
        }
        break;
    default:
      undef_inst:
        V_ERR("unsolved inst %x %x %x %x %x %x %x %x\n",
            (unsigned int) (*(inst + 0)), (unsigned int) (*(inst + 1)),
            (unsigned int) (*(inst + 2)), (unsigned int) (*(inst + 3)),
            (unsigned int) (*(inst + 4)), (unsigned int) (*(inst + 5)),
            (unsigned int) (*(inst + 6)), (unsigned int) (*(inst + 7)));
        world->status = VM_PAUSED;
        break;
    }
#ifdef DEBUG_CODECONTROL
    if (this_is_cli) {
        last_is_cli = 1;
    } else {
        last_is_cli = 0;
    }
#endif
    h_debug_sanity_check(world);
}

void
h_do_fail_inst(struct v_world *w, unsigned long ip)
{
    h_gpfault(w);
}

#ifdef QUICKPATH_GP_ENABLED

void
h_gp_fault_quickpath_postprocessing(struct v_world *world)
{
    void *cache = world->hregs.hcpu.switcher;
    int result;
    cache += sensitive_instruction_cache_offset;
    result = *((unsigned int *) (cache));
    if (result == 2 || result == 3) {
        int interrupt = *((unsigned int *) (cache + 20));
        if (!interrupt) {
            v_disable_int(world);
            world->gregs.eflags &= (~H_EFLAGS_IF);
        } else {
            v_enable_int(world);
            world->gregs.eflags |= (H_EFLAGS_IF);
        }
    }
    if (result == 3) {
        int nt = *((unsigned int *) (cache + 12));
        int iopl = *((unsigned int *) (cache + 16));
        world->gregs.nt = nt;
        world->gregs.iopl = iopl;
        world->gregs.ring = 3;
        h_update(cs, 1, 1);
        v_bt_reset(world);
    }
}

#endif

#define H_DO_INT(number) case number: asm volatile ("int $"#number); break
static void
h_do_int(unsigned int int_no)
{
    switch (int_no) {
        H_DO_INT(0x00);
        H_DO_INT(0x01);
        H_DO_INT(0x02);
        H_DO_INT(0x03);
        H_DO_INT(0x04);
        H_DO_INT(0x05);
        H_DO_INT(0x06);
        H_DO_INT(0x07);
        H_DO_INT(0x08);
        H_DO_INT(0x09);
        H_DO_INT(0x0a);
        H_DO_INT(0x0b);
        H_DO_INT(0x0c);
        H_DO_INT(0x0d);
        H_DO_INT(0x0e);
        H_DO_INT(0x0f);

        H_DO_INT(0x10);
        H_DO_INT(0x11);
        H_DO_INT(0x12);
        H_DO_INT(0x13);
        H_DO_INT(0x14);
        H_DO_INT(0x15);
        H_DO_INT(0x16);
        H_DO_INT(0x17);
        H_DO_INT(0x18);
        H_DO_INT(0x19);
        H_DO_INT(0x1a);
        H_DO_INT(0x1b);
        H_DO_INT(0x1c);
        H_DO_INT(0x1d);
        H_DO_INT(0x1e);
        H_DO_INT(0x1f);

        H_DO_INT(0x20);
        H_DO_INT(0x21);
        H_DO_INT(0x22);
        H_DO_INT(0x23);
        H_DO_INT(0x24);
        H_DO_INT(0x25);
        H_DO_INT(0x26);
        H_DO_INT(0x27);
        H_DO_INT(0x28);
        H_DO_INT(0x29);
        H_DO_INT(0x2a);
        H_DO_INT(0x2b);
        H_DO_INT(0x2c);
        H_DO_INT(0x2d);
        H_DO_INT(0x2e);
        H_DO_INT(0x2f);

        H_DO_INT(0x30);
        H_DO_INT(0x31);
        H_DO_INT(0x32);
        H_DO_INT(0x33);
        H_DO_INT(0x34);
        H_DO_INT(0x35);
        H_DO_INT(0x36);
        H_DO_INT(0x37);
        H_DO_INT(0x38);
        H_DO_INT(0x39);
        H_DO_INT(0x3a);
        H_DO_INT(0x3b);
        H_DO_INT(0x3c);
        H_DO_INT(0x3d);
        H_DO_INT(0x3e);
        H_DO_INT(0x3f);

        H_DO_INT(0x40);
        H_DO_INT(0x41);
        H_DO_INT(0x42);
        H_DO_INT(0x43);
        H_DO_INT(0x44);
        H_DO_INT(0x45);
        H_DO_INT(0x46);
        H_DO_INT(0x47);
        H_DO_INT(0x48);
        H_DO_INT(0x49);
        H_DO_INT(0x4a);
        H_DO_INT(0x4b);
        H_DO_INT(0x4c);
        H_DO_INT(0x4d);
        H_DO_INT(0x4e);
        H_DO_INT(0x4f);

        H_DO_INT(0x50);
        H_DO_INT(0x51);
        H_DO_INT(0x52);
        H_DO_INT(0x53);
        H_DO_INT(0x54);
        H_DO_INT(0x55);
        H_DO_INT(0x56);
        H_DO_INT(0x57);
        H_DO_INT(0x58);
        H_DO_INT(0x59);
        H_DO_INT(0x5a);
        H_DO_INT(0x5b);
        H_DO_INT(0x5c);
        H_DO_INT(0x5d);
        H_DO_INT(0x5e);
        H_DO_INT(0x5f);

        H_DO_INT(0x60);
        H_DO_INT(0x61);
        H_DO_INT(0x62);
        H_DO_INT(0x63);
        H_DO_INT(0x64);
        H_DO_INT(0x65);
        H_DO_INT(0x66);
        H_DO_INT(0x67);
        H_DO_INT(0x68);
        H_DO_INT(0x69);
        H_DO_INT(0x6a);
        H_DO_INT(0x6b);
        H_DO_INT(0x6c);
        H_DO_INT(0x6d);
        H_DO_INT(0x6e);
        H_DO_INT(0x6f);

        H_DO_INT(0x70);
        H_DO_INT(0x71);
        H_DO_INT(0x72);
        H_DO_INT(0x73);
        H_DO_INT(0x74);
        H_DO_INT(0x75);
        H_DO_INT(0x76);
        H_DO_INT(0x77);
        H_DO_INT(0x78);
        H_DO_INT(0x79);
        H_DO_INT(0x7a);
        H_DO_INT(0x7b);
        H_DO_INT(0x7c);
        H_DO_INT(0x7d);
        H_DO_INT(0x7e);
        H_DO_INT(0x7f);

        H_DO_INT(0x80);
        H_DO_INT(0x81);
        H_DO_INT(0x82);
        H_DO_INT(0x83);
        H_DO_INT(0x84);
        H_DO_INT(0x85);
        H_DO_INT(0x86);
        H_DO_INT(0x87);
        H_DO_INT(0x88);
        H_DO_INT(0x89);
        H_DO_INT(0x8a);
        H_DO_INT(0x8b);
        H_DO_INT(0x8c);
        H_DO_INT(0x8d);
        H_DO_INT(0x8e);
        H_DO_INT(0x8f);

        H_DO_INT(0x90);
        H_DO_INT(0x91);
        H_DO_INT(0x92);
        H_DO_INT(0x93);
        H_DO_INT(0x94);
        H_DO_INT(0x95);
        H_DO_INT(0x96);
        H_DO_INT(0x97);
        H_DO_INT(0x98);
        H_DO_INT(0x99);
        H_DO_INT(0x9a);
        H_DO_INT(0x9b);
        H_DO_INT(0x9c);
        H_DO_INT(0x9d);
        H_DO_INT(0x9e);
        H_DO_INT(0x9f);

        H_DO_INT(0xa0);
        H_DO_INT(0xa1);
        H_DO_INT(0xa2);
        H_DO_INT(0xa3);
        H_DO_INT(0xa4);
        H_DO_INT(0xa5);
        H_DO_INT(0xa6);
        H_DO_INT(0xa7);
        H_DO_INT(0xa8);
        H_DO_INT(0xa9);
        H_DO_INT(0xaa);
        H_DO_INT(0xab);
        H_DO_INT(0xac);
        H_DO_INT(0xad);
        H_DO_INT(0xae);
        H_DO_INT(0xaf);

        H_DO_INT(0xb0);
        H_DO_INT(0xb1);
        H_DO_INT(0xb2);
        H_DO_INT(0xb3);
        H_DO_INT(0xb4);
        H_DO_INT(0xb5);
        H_DO_INT(0xb6);
        H_DO_INT(0xb7);
        H_DO_INT(0xb8);
        H_DO_INT(0xb9);
        H_DO_INT(0xba);
        H_DO_INT(0xbb);
        H_DO_INT(0xbc);
        H_DO_INT(0xbd);
        H_DO_INT(0xbe);
        H_DO_INT(0xbf);

        H_DO_INT(0xc0);
        H_DO_INT(0xc1);
        H_DO_INT(0xc2);
        H_DO_INT(0xc3);
        H_DO_INT(0xc4);
        H_DO_INT(0xc5);
        H_DO_INT(0xc6);
        H_DO_INT(0xc7);
        H_DO_INT(0xc8);
        H_DO_INT(0xc9);
        H_DO_INT(0xca);
        H_DO_INT(0xcb);
        H_DO_INT(0xcc);
        H_DO_INT(0xcd);
        H_DO_INT(0xce);
        H_DO_INT(0xcf);

        H_DO_INT(0xd0);
        H_DO_INT(0xd1);
        H_DO_INT(0xd2);
        H_DO_INT(0xd3);
        H_DO_INT(0xd4);
        H_DO_INT(0xd5);
        H_DO_INT(0xd6);
        H_DO_INT(0xd7);
        H_DO_INT(0xd8);
        H_DO_INT(0xd9);
        H_DO_INT(0xda);
        H_DO_INT(0xdb);
        H_DO_INT(0xdc);
        H_DO_INT(0xdd);
        H_DO_INT(0xde);
        H_DO_INT(0xdf);

        H_DO_INT(0xe0);
        H_DO_INT(0xe1);
        H_DO_INT(0xe2);
        H_DO_INT(0xe3);
        H_DO_INT(0xe4);
        H_DO_INT(0xe5);
        H_DO_INT(0xe6);
        H_DO_INT(0xe7);
        H_DO_INT(0xe8);
        H_DO_INT(0xe9);
        H_DO_INT(0xea);
        H_DO_INT(0xeb);
        H_DO_INT(0xec);
        H_DO_INT(0xed);
        H_DO_INT(0xee);
        H_DO_INT(0xef);

        H_DO_INT(0xf0);
        H_DO_INT(0xf1);
        H_DO_INT(0xf2);
        H_DO_INT(0xf3);
        H_DO_INT(0xf4);
        H_DO_INT(0xf5);
        H_DO_INT(0xf6);
        H_DO_INT(0xf7);
        H_DO_INT(0xf8);
        H_DO_INT(0xf9);
        H_DO_INT(0xfa);
        H_DO_INT(0xfb);
        H_DO_INT(0xfc);
        H_DO_INT(0xfd);
        H_DO_INT(0xfe);
        H_DO_INT(0xff);
    }
}
