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
#ifndef H_CPU_H
#define H_CPU_H

#define H_EXD_CAPABLE 0
#define H_EFLAGS_TF	(1 << 8)
#define H_EFLAGS_IF	(1 << 9)
#define H_EFLAGS_NT	(1 << 14)
#define H_EFLAGS_RF	(1 << 16)
#define H_EFLAGS_VM	(1 << 17)
#define H_EFLAGS_AC	(1 << 18)
#define H_EFLAGS_VIF	(1 << 19)
#define H_EFLAGS_VIP	(1 << 20)
#define H_EFLAGS_ID	(1 << 21)
#define H_EFLAGS_CF	(1)
#define H_EFLAGS_PF	(1 << 2)
#define H_EFLAGS_AF	(1 << 4)
#define H_EFLAGS_ZF	(1 << 6)
#define H_EFLAGS_SF	(1 << 7)
#define H_EFLAGS_DF	(1 << 10)
#define H_EFLAGS_OF	(1 << 11)
#define H_EFLAGS_RESERVED	(1 << 1)
#define H_EFLAGS_IOPL_MASK	(3 << 12)

#define H_CR0_WP        (1 << 16)

#define H_CR4_PGE       (1 << 7)
#define H_CR4_PAE       (1 << 5)
#define H_CR4_PSE       (1 << 4)

#define H_DR7_GE 	(1 << 9)
#define H_DR7_LE	(1 << 8)
#define H_DR7_GD	(1 << 13)
#define H_DR7_ALLINST   (0)

#define H_CR0_TS	(1 << 3)

struct h_regs;
struct v_world;

struct h_desc_table {
    unsigned short limit;
    unsigned int base;
} __attribute__ ((__packed__));

struct h_tr_table {
    unsigned int prev;
    unsigned int esp0;
    unsigned int ss0;
    unsigned int esp1;
    unsigned int ss1;
    unsigned int esp2;
    unsigned int ss2;
    unsigned int cr3;
    unsigned int eip;
    unsigned int eflags;
    unsigned int eax;
    unsigned int ecx;
    unsigned int edx;
    unsigned int ebx;
    unsigned int esp;
    unsigned int ebp;
    unsigned int esi;
    unsigned int edi;
    unsigned int es;
    unsigned int cs;
    unsigned int ss;
    unsigned int ds;
    unsigned int fs;
    unsigned int gs;
    unsigned int ldt;
    unsigned short reserved;
    unsigned short iomap;
} __attribute__ ((__packed__));

struct h_cpu {
    unsigned int cr0;
    unsigned int cr3;
    unsigned int cr4;

    unsigned int _mointor_stacks[8];

    unsigned int gs;
    unsigned int fs;
    unsigned int es;
    unsigned int ds;

    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;

    unsigned int temp;

    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;

    unsigned int save_esp;
    unsigned int intr;
    unsigned int errorc;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
    unsigned int esp;
    unsigned int ss;

    unsigned int v86es;
    unsigned int v86ds;
    unsigned int v86fs;
    unsigned int v86gs;

    unsigned int cpuid0;
    unsigned int cpuid1;
    unsigned int cpuid2;
    unsigned int cpuid3;
    unsigned short dummy0;
    struct h_desc_table gdt;
    unsigned short dummy1;
    struct h_desc_table idt;
    unsigned int ldt;
    unsigned int tr;
    struct h_tr_table trsave;
    unsigned int dr0;
    unsigned int dr1;
    unsigned int dr2;
    unsigned int dr3;
    unsigned int dr7;
    void (*switcher) (unsigned long trbase, struct v_world * w);
    unsigned int page_fault_addr;

} __attribute__ ((__packed__));

int h_cpu_init(void);

struct v_world;
/*world switch function */
void h_switcher(unsigned long, struct v_world *);
int h_switch_to(unsigned long, struct v_world *);
void h_cpu_save(struct v_world *);
void h_delete_trbase(struct v_world *);
void h_new_trbase(struct v_world *);
void h_inject_int(struct v_world *, unsigned int);

void h_gpfault(struct v_world *);
void h_do_fail_inst(struct v_world *, unsigned long);
void monitor_fault_entry_check(void);
#endif
