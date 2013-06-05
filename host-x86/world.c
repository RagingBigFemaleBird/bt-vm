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
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/logging.h"
#include "host/include/cpu.h"

extern struct h_cpu hostcpu;
extern void trap_start(void);
extern void h_switcher(unsigned long trbase, struct v_world *w);

static void
h_monitor_fault_check_fixup(struct v_world *world)
{
    int monitor_offset;
    int monitor_stack_offset;
    unsigned int h_switcher_page = (unsigned int) world->hregs.hcpu.switcher;
    monitor_offset =
        (unsigned int) monitor_fault_entry_check - (unsigned int) h_switcher;
    monitor_stack_offset =
        offsetof(struct v_world, hregs) + offsetof(struct h_regs,
        gcpu) + offsetof(struct h_cpu, intr) + (unsigned int) world;
    *(unsigned int *) (h_switcher_page + monitor_offset + 2) =
        monitor_stack_offset;
    *(unsigned int *) (h_switcher_page + monitor_offset + 6 + 2 + 1) =
        monitor_stack_offset - 8 * 4 - 12;
    V_ERR("fixing monitor stack check @%x to %x",
        h_switcher_page + monitor_offset, monitor_stack_offset);
}

void
h_world_init(struct v_world *world)
{
    struct v_chunk *table = h_raw_palloc(1);
    void *va = h_allocv(table->phys);
    void *vb = h_allocv(table->phys + H_PAGE_SIZE);
    void *old = (void *) hostcpu.gdt.base;
    void *new;
    unsigned int eflags;
    void (*trap) (void) = trap_start;
    unsigned int *intr;
    int i;
    unsigned int traddr;
    struct h_regs *h = &world->hregs;
    world->npage = h_switch_to;
    world->hregs.hcpu.switcher = h_switcher;
    h_monitor_fault_check_fixup(world);
    h_pin(h_v2p((h_addr_t) world));

    h_set_map(world->htrbase, (h_addr_t) world,
        h_v2p((unsigned int) world), 1, V_PAGE_W | V_PAGE_VM);
    V_ERR("world data at %p, ", world);
    h_set_map(world->htrbase, (h_addr_t) world->hregs.hcpu.switcher,
        h_v2p((unsigned int) world->hregs.hcpu.switcher), 1,
        V_PAGE_W | V_PAGE_VM);
    V_ERR("neutral page at %p\n", world->hregs.hcpu.switcher);
    V_ERR("Check map %llx -> %llx",
        (unsigned long long) ((h_addr_t) (world->hregs.hcpu.switcher)),
        (unsigned long long) h_get_map(world->htrbase,
            (h_addr_t) (world->hregs.hcpu.switcher)));

    world->hregs.gcpu.idt.base = (unsigned int) va;
    world->hregs.gcpu.idt.limit = 0x7ff;
    world->hregs.gcpu.gdt.base = (unsigned int) vb;
    world->hregs.gcpu.gdt.limit = 0x7ff;
    h_memset((void *) world->hregs.gcpu.gdt.base, 0, hostcpu.gdt.limit + 1);
    new = (void *) (world->hregs.gcpu.gdt.base);
    traddr = (unsigned int) (&world->hregs.gcpu.trsave);
    new = new + 0x7d8;          //modify IDT & init cs if you change this
    *(unsigned int *) new =
        ((sizeof(struct h_tr_table)) & 0x0000ffff) | (traddr << 16);
    new = new + 4;
    *(unsigned int *) new =
        0x00008b00 | (traddr & 0xff000000) | (traddr >> 16 << 24 >> 24);
    new = new + 4;

    *(unsigned int *) new = 0x0000ffff;
    new = new + 4;
    *(unsigned int *) new = 0x008ffa00;
    new = new + 4;
    *(unsigned int *) new = 0x0000ffff;
    new = new + 4;
    *(unsigned int *) new = 0x008ff200;
    new = new + 4;

    *(unsigned int *) new = 0x0000ffff;
    new = new + 4;
    *(unsigned int *) new = 0x00cf9a00;
    new = new + 4;
    *(unsigned int *) new = 0x0000ffff;
    new = new + 4;
    *(unsigned int *) new = 0x00cf9200;

    intr = (void *) (world->hregs.gcpu.idt.base);
    old = trap;
    V_ERR("trap return starting at %p(%x)\n", old, *(unsigned int *) old);
    for (i = 0; i < 256; i++) {
        unsigned int low, high;
        low = ((unsigned int) old & 0xffff);
        high = ((unsigned int) old & 0xffff0000);
        low = ((unsigned int) low | 0x7f00000);
        high = ((unsigned int) high | 0x8e00);
        (*intr) = low;
        intr++;

        (*intr) = high;
        intr++;
        old += 4;
        if (i % 7 == 6)
            old += 4;
    }

    h_memcpy(&(world->hregs.hcpu), &hostcpu, sizeof(struct h_cpu));
    asm volatile ("pushf");
    asm volatile ("pop %0":"=r" (eflags));
    eflags = H_EFLAGS_VM | H_EFLAGS_IF;
    eflags &= (~H_EFLAGS_RF);
    h->gcpu.eax = h->gcpu.ebx = h->gcpu.ecx =
        h->gcpu.edx = h->gcpu.esi = h->gcpu.edi = h->gcpu.ebp =
        h->gcpu.errorc = h->gcpu.save_esp = h->gcpu.intr = 0xdeadbeef;

    h->gcpu.v86ds = 0;
    h->gcpu.v86es = 0;
    h->gcpu.v86fs = 0;
    h->gcpu.v86gs = 0;

    world->hregs.gcpu.eflags = eflags;
/*	for testing:
	asm volatile ("call skipahead");
	asm volatile ("deadlock:");
	asm volatile ("jmp deadlock");
	asm volatile ("mov $0, %eax");
	asm volatile ("mov %ax, %ds");
	asm volatile ("skipahead:");
	asm volatile ("pop %0":"=r"(eflags));
*/
    world->hregs.gcpu.ds = 0;
    world->hregs.gcpu.es = 0;
    world->hregs.gcpu.fs = 0;
    world->hregs.gcpu.gs = 0;

    world->hregs.gcpu.eip = 0x7c00;
    world->hregs.gcpu.cs = 0;   //use 0x7e3 for testing;
    world->hregs.gcpu.esp = 0x7c00;
    world->hregs.gcpu.ss = 0;   //use 0x7eb for testing;
    world->hregs.gcpu.ldt = 0;
    world->hregs.gcpu.tr = 0x7d8;
    world->hregs.gcpu.trsave.ss0 = 0x7f8;
    world->hregs.gcpu.trsave.esp0 = (unsigned int) (&world->hregs.gcpu.cpuid0);
    world->hregs.gcpu.trsave.iomap = sizeof(struct h_tr_table) + 1;
    world->hregs.gcpu.dr7 = 0x700;

    world->hregs.hcpu.switcher = h_switcher;

    table = h_raw_palloc(0);
    world->hregs.fpu = h_allocv(table->phys);
    asm volatile ("fxsave (%0)"::"r" (world->hregs.fpu + 512));
    world->hregs.fpusaved = 0;

    V_ERR("tables at %x, phys %llx\n", world->hregs.gcpu.gdt.base,
        (unsigned long long) h_v2p(world->hregs.gcpu.idt.base));
    h_set_map(world->htrbase, (h_addr_t) world->hregs.gcpu.idt.base,
        h_v2p(world->hregs.gcpu.idt.base), 1, V_PAGE_W | V_PAGE_VM);

    h_set_map(world->htrbase, (h_addr_t) world->hregs.gcpu.gdt.base,
        h_v2p(world->hregs.gcpu.gdt.base), 1, V_PAGE_W | V_PAGE_VM);
}

void
h_relocate_npage(struct v_world *w)
{
    struct v_spt_info *spt = w->spt_list;
    unsigned int oldnp = (unsigned int) (w->hregs.hcpu.switcher);
    void (*trap) (void);
    unsigned int *intr;
    unsigned int i;
    void *old;
    struct v_chunk *chunk = h_raw_palloc(0);
    unsigned int phys = chunk->phys;
    void *virt = h_allocv(phys);
    h_memcpy(virt, w->hregs.hcpu.switcher, 4096);
    w->hregs.hcpu.switcher = virt;

    h_monitor_fault_check_fixup(w);

    trap =
        (void (*)(void)) ((((unsigned int) trap_start) & H_POFF_MASK) +
        (((unsigned int) virt) & H_PFN_MASK));
    asm volatile ("movl $restoreCS, %0":"=r" (hostcpu.eip));
    hostcpu.eip =
        (hostcpu.eip & H_POFF_MASK) + (((unsigned int) virt) & H_PFN_MASK);
    w->hregs.hcpu.eip = hostcpu.eip;

    if (spt == NULL) {
        h_set_map(w->htrbase, oldnp, 0, 1, V_PAGE_NOMAP);
        h_set_map(w->htrbase, (unsigned long) w->hregs.hcpu.switcher,
            h_v2p((h_addr_t) w->hregs.hcpu.switcher), 1, V_PAGE_W | V_PAGE_VM);
    } else
        while (spt != NULL) {
            h_set_map(spt->spt_paddr, oldnp, 0, 1, V_PAGE_NOMAP);
            h_set_map(spt->spt_paddr, (unsigned long) w->hregs.hcpu.switcher,
                h_v2p((h_addr_t) w->hregs.hcpu.switcher), 1,
                V_PAGE_W | V_PAGE_VM);
            spt = spt->next;
        }
    intr = (void *) (w->hregs.gcpu.idt.base);
    old = trap;
    V_ERR("trap return starting at %p(%x)\n", old, *(unsigned int *) old);
    for (i = 0; i < 256; i++) {
        unsigned int low, high;
        low = ((unsigned int) old & 0xffff);
        high = ((unsigned int) old & 0xffff0000);
        low = ((unsigned int) low | 0x7f00000);
        high = ((unsigned int) high | 0x8e00);
        (*intr) = low;
        intr++;

        (*intr) = high;
        intr++;
        old += 4;
        if (i % 7 == 6)
            old += 4;
    }
}

void
h_relocate_tables(struct v_world *w)
{
    struct v_spt_info *spt = w->spt_list;
    struct v_chunk *table = h_raw_palloc(1);
    void *vidt = h_allocv(table->phys);
    void *vgdt = h_allocv(table->phys + H_PAGE_SIZE);
    struct v_chunk v;
    v.order = 1;
    v.phys = h_v2p((h_addr_t) w->hregs.gcpu.idt.base);
    h_memcpy(vgdt, (void *) (w->hregs.gcpu.gdt.base),
        w->hregs.gcpu.gdt.limit + 1);
    h_memcpy(vidt, (void *) (w->hregs.gcpu.idt.base),
        w->hregs.gcpu.idt.limit + 1);
    if (spt == NULL) {
        h_set_map(w->htrbase, (unsigned long) w->hregs.gcpu.idt.base, 0,
            1, V_PAGE_NOMAP);
        h_set_map(w->htrbase, (unsigned long) w->hregs.gcpu.gdt.base, 0,
            1, V_PAGE_NOMAP);
        h_set_map(w->htrbase, (unsigned long) vgdt,
            h_v2p((h_addr_t) vgdt), 1, V_PAGE_W | V_PAGE_VM);
        h_set_map(w->htrbase, (unsigned long) vidt,
            h_v2p((h_addr_t) vidt), 1, V_PAGE_W | V_PAGE_VM);
    } else
        while (spt != NULL) {
            h_set_map(spt->spt_paddr,
                (unsigned long) w->hregs.gcpu.idt.base, 0, 1, V_PAGE_NOMAP);
            h_set_map(spt->spt_paddr,
                (unsigned long) w->hregs.gcpu.gdt.base, 0, 1, V_PAGE_NOMAP);
            h_set_map(spt->spt_paddr, (unsigned long) vgdt,
                h_v2p((h_addr_t) vgdt), 1, V_PAGE_W | V_PAGE_VM);
            h_set_map(spt->spt_paddr, (unsigned long) vidt,
                h_v2p((h_addr_t) vidt), 1, V_PAGE_W | V_PAGE_VM);
            spt = spt->next;
        }
    w->hregs.gcpu.gdt.base = (unsigned int) vgdt;
    w->hregs.gcpu.idt.base = (unsigned int) vidt;
    h_raw_depalloc(&v);
}

void
h_relocate_world(struct v_world *w, struct v_world *w_new)
{
    struct v_spt_info *spt = w->spt_list;
    void *new;
    unsigned int traddr;

    V_ERR("world data change from %x to %x ",
        (unsigned int) h_v2p((h_addr_t) w),
        (unsigned int) h_v2p((h_addr_t) w_new));

    new = (void *) (w->hregs.gcpu.gdt.base);
    traddr = (unsigned int) (&w_new->hregs.gcpu.trsave);
    new = new + 0x7d8;          //modify IDT & init cs if you change this
    *(unsigned int *) new =
        ((sizeof(struct h_tr_table)) & 0x0000ffff) | (traddr << 16);
    new = new + 4;
    *(unsigned int *) new =
        0x00008b00 | (traddr & 0xff000000) | (traddr >> 16 << 24 >> 24);
    new = new + 4;

    if (w->gregs.mode == G_MODE_REAL)
        w->hregs.gcpu.trsave.esp0 = (unsigned int) (&w_new->hregs.gcpu.cpuid0);
    else
        w->hregs.gcpu.trsave.esp0 = (unsigned int) (&w_new->hregs.gcpu.v86es);

    if (spt == NULL) {
        h_set_map(w->htrbase, (long unsigned int) w, 0, 1, V_PAGE_NOMAP);
        h_set_map(w->htrbase, (long unsigned int) w_new,
            h_v2p((h_addr_t) w_new), 1, V_PAGE_W | V_PAGE_VM);
    } else {
        while (spt != NULL) {
            h_set_map(spt->spt_paddr, (long unsigned int) w, 0, 1,
                V_PAGE_NOMAP);
            h_set_map(spt->spt_paddr, (long unsigned int) w_new,
                h_v2p((h_addr_t) w_new), 1, V_PAGE_W | V_PAGE_VM);
            spt = spt->next;
        }
    }
    h_monitor_fault_check_fixup(w);
}
