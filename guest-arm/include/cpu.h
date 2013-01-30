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

#define G_MODE_NO_MMU	0
#define G_MODE_MMU	    1

#define G_PRIV_USR	0
#define G_PRIV_FIQ	1
#define G_PRIV_IRQ	2
#define G_PRIV_SVC	3
#define G_PRIV_ABT	7
#define G_PRIV_UND	11
#define G_PRIV_SYS	15
#define G_PRIV_RST	14
#define G_PRIV_MASK 0xf

#define G_EX_MODE_ARM	1

#define G_EX_MODE_16	1
#define G_EX_MODE_32	2

#define G_MODE_REAL	0
#define G_MODE_PE	1
#define G_MODE_PG	2

/*
#define G_SERIAL_PAGE 0x48020000
#define G_SERIAL_OMAP 0x20
#define G_SERIAL_LSR (5 << 2)
#define LSR_THRE 0x20
#define LSR_TEMT 0x40
*/

struct v_page;

struct g_regs {
    struct v_page *io_page[32];
    unsigned int domain;
    unsigned int p15_domain;
    unsigned int p15_id;
    unsigned int p15_ctrl;
    unsigned int p15_trbase;
    unsigned int p15_trattr;
    unsigned int p14_didr;
    unsigned int p14_drar;
    unsigned int p14_dsar;
    unsigned int p14_dtrrx;
    unsigned int p14_dtrtx;
    unsigned int p15_vector;

    unsigned int p15_ifar;
    unsigned int p15_dfar;
    unsigned int p15_ifsr;
    unsigned int p15_dfsr;
    unsigned int mode;

    unsigned int cpsr;
    unsigned int r13_modes[0xf];
    unsigned int r14_modes[0xf];
    unsigned int spsr[0xf];

    unsigned int disasm_ip;     //bt use
    unsigned char *disasm_vip;
    unsigned int sdisasm_ip;    //seed use
    unsigned char *sdisasm_vip;
} __attribute__ ((__packed__));

struct v_world;
unsigned long g_get_ip(struct v_world *);
unsigned long g_get_sp(struct v_world *);
inline unsigned int g_get_current_ex_mode(struct v_world *);
unsigned int g_get_poi_key(struct v_world *);

#endif
