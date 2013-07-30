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

struct v_world;

#define H_EXD_CAPABLE 0

#define H_CPSR_I	0x80
#define H_CPSR_F	0x40
#define H_CPSR_USR	0x10
#define H_CPSR_FIQ	0x11
#define H_CPSR_IRQ	0x12
#define H_CPSR_SVC	0x13
#define H_CPSR_ABT	0x17
#define H_CPSR_UND	0x1b
#define H_CPSR_SYS	0x1f
#define H_CPSR_Z    0x40000000
#define H_CPSR_C    0x20000000
#define H_CPSR_N    0x80000000
#define H_CPSR_V    0x10000000

#define P15_CTRL_V (1 << 13)
#define P15_CTRL_C (1 << 2)
#define P15_CTRL_M (1 << 0)

struct h_regs;
struct v_world;

struct h_cpu {
    void (*switcher) (unsigned long, struct v_world *);
    unsigned int domain;
    void *p_drar;
    unsigned int p15_id;
    unsigned int p15_ctrl;
    unsigned int p15_trbase;
    unsigned int p15_trctl;
    unsigned int p15_trattr;
    unsigned int p14_didr;
    unsigned int p14_dscr;
    unsigned int p14_drar;
    unsigned int p14_dsar;
    unsigned int p14_dtrrx;
    unsigned int p14_dtrtx;
    unsigned int p15_vector;
    unsigned int p15_asid;

    unsigned int r0;
    unsigned int r1;
    unsigned int r2;
    unsigned int r3;
    unsigned int r4;
    unsigned int r5;
    unsigned int r6;
    unsigned int r7;
    unsigned int r8;
    unsigned int r9;
    unsigned int r10;
    unsigned int r11;
    unsigned int r12;
    unsigned int r13;
    unsigned int r14;

    unsigned int usr_r13;
    unsigned int usr_r14;
    unsigned int spsr_save;
    unsigned int reason;

    unsigned int r13_fiq;
    unsigned int r14_fiq;
    unsigned int r13_irq;
    unsigned int r14_irq;
    unsigned int r13_abt;
    unsigned int r14_abt;
    unsigned int r13_und;
    unsigned int r14_und;
    unsigned int r13_svc;
    unsigned int r14_svc;

    unsigned int cpsr;
    unsigned int pc;

    unsigned int save_sp;

    unsigned int number_of_breakpoints;
    unsigned int bcr[6];
    unsigned int bvr[6];
//
} __attribute__ ((__packed__));

int h_cpu_init(void);
#define host_processor_id() smp_processor_id()

/* switch to this translation base */
struct v_world;
void h_switcher(unsigned long, struct v_world *);
int h_switch_to(unsigned long, struct v_world *);
void h_cpu_save(struct v_world *);
int h_access_guest(struct v_world *, unsigned int, unsigned int *);

void h_do_fail_inst(struct v_world *, unsigned long);
void h_save_fpu(struct v_world *);
void h_restore_fpu(struct v_world *);

#endif
