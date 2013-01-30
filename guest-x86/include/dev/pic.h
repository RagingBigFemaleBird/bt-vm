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
#ifndef G_DEV_PIC_H
#define G_DEV_PIC_H

struct v_world;

#define G_PIC_OCW3 0x08
#define G_PIC_ICW1_ICW1 0x10
#define G_PIC_ICW1_ICW3 0x2
#define G_PIC_ICW1_ICW4 0x1

#define G_PIC_TIMER_INT 0
#define G_PIC_TIMER (1 << G_PIC_TIMER_INT)
#define G_PIC_FDC_INT 6
#define G_PIC_FDC (1 << G_PIC_FDC_INT)
#define G_PIC_KB_INT 1
#define G_PIC_KB (1 << G_PIC_KB_INT)

struct g_dev_pic {
    unsigned char d0icw1;
    unsigned char d1icw1;
    unsigned char d0icw_expect;
    unsigned char d1icw_expect;
    unsigned char d0IRQ;
    unsigned char d1IRQ;
    unsigned char d0IRQ_mask;
    unsigned char d1IRQ_mask;
    unsigned char d0IRQ_req;
    unsigned char d0IRQ_srv;
    unsigned char d1IRQ_req;
    unsigned char d1IRQ_srv;
    unsigned char d0in20;
    unsigned char d1in20;
    long long expected_jiffies;
};

void g_pic_init(struct v_world *);
void g_pic_serve(struct v_world *);
void g_pic_trigger(struct v_world *, int);

#endif
