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
#ifndef G_CPU_H
#define G_CPU_H

#define G_EX_MODE_16	1
#define G_EX_MODE_32	2

#define G_MODE_REAL	0
#define G_MODE_PE	1
#define G_MODE_PG	2

#include "guest/include/dev/dev.h"

struct g_desc_table {
    unsigned short limit;
    unsigned int base;
} __attribute__ ((__packed__));

struct g_tr_table {
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

struct g_regs {
    unsigned int cr0;
    unsigned int cr2;
    unsigned int cr3;
    unsigned int cr4;
/* reg must be followed by reghigh and regtrue */
    unsigned int gs;
    unsigned int gshigh;
    unsigned int gstrue;
    unsigned int fs;
    unsigned int fshigh;
    unsigned int fstrue;
    unsigned int es;
    unsigned int eshigh;
    unsigned int estrue;
    unsigned int ds;
    unsigned int dshigh;
    unsigned int dstrue;

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
    unsigned int cshigh;
    unsigned int cstrue;

    unsigned int cstemp;
    unsigned int cstemphigh;
    unsigned int cstemptrue;

    unsigned int eflags;
    unsigned int esp;
    unsigned int ss;
    unsigned int sshigh;
    unsigned int sstrue;

    unsigned int v86es;
    unsigned int v86ds;
    unsigned int v86fs;
    unsigned int v86gs;

    unsigned int cpuid0;
    unsigned int cpuid1;
    unsigned int cpuid2;
    unsigned int cpuid3;
    unsigned short dummy0;
    struct g_desc_table gdt;
    unsigned short dummy1;
    struct g_desc_table idt;
    unsigned int ldt;
    unsigned int ldt_hid;
    unsigned int ldt_hidhigh;
    unsigned int ldt_true;

    unsigned int tr;
    unsigned int tr_hid;
    unsigned int tr_hidhigh;
    unsigned int tr_true;

    unsigned int trtemp;
    unsigned int trtemphigh;
    unsigned int trtemptrue;

    struct g_tr_table trsave;
    unsigned int mode;
    unsigned int ring;
    unsigned int iopl;
    unsigned int nt;
    unsigned int rf;
    unsigned int eflags_real;
    unsigned int has_errorc;
    unsigned int disasm_ip;     //bt use
    unsigned char *disasm_vip;
    unsigned int sdisasm_ip;    //seed use
    unsigned char *sdisasm_vip;
    unsigned int zombie_cs;     //special treatment when mode changing not yet long jumped, and saves v86 cs
    unsigned int zombie_ss;     //saves v86 ss
    unsigned int zombie_jumped;
    unsigned int fast_iret_possible;
    unsigned int gdt_protected;
    struct g_devices dev;
} __attribute__ ((__packed__));

struct v_world;
unsigned long g_get_ip(struct v_world *);
unsigned long g_get_sp(struct v_world *);
unsigned int g_get_sel_ring(struct v_world *, unsigned int);
void g_get_sel(struct v_world *, unsigned int, unsigned int *, unsigned int *);
unsigned int g_get_current_ex_mode(struct v_world *);

#define g_sel2base(word1, word2) ((((word1) & 0xffff0000) >> 16) + ((word2) & 0xff000000) + (((word2) & 0xff) << 16))
#define g_sel2limit(word1, word2) ((word2 & 0x00800000)?(((((word2) & 0xf0000) + ((word1) & 0xffff)) << 12) + 0xfff):(((word2) & 0xf0000) + ((word1) & 0xffff)))
#endif
