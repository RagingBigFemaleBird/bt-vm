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
#include "host/include/interrupt.h"
#include "vm/include/logging.h"

unsigned char
h_in_port(unsigned int port_no)
{
    unsigned char ret;
    asm volatile ("mov %0, %%edx"::"r" (port_no):"edx");
    asm volatile ("in %%dx, %%al":::"eax");
    asm volatile ("movb %%al, %0":"=r" (ret));
    return ret;
}

void
h_out_port(unsigned int port_no, unsigned char byte)
{
    asm volatile ("mov %0, %%edx"::"r" (port_no):"edx");
    asm volatile ("movb %0, %%al"::"r" (byte):"eax");
    asm volatile ("out %al, %dx");
}

void
h_init_int(void)
{
    unsigned char mask1, mask2;
    mask1 = h_in_port(H_PORT_8259A_MASK1);
    mask2 = h_in_port(H_PORT_8259A_MASK2);
    V_LOG("8259A status: %x, %x", mask1, mask2);
}

unsigned char h_8259_m1;
unsigned char h_8259_m2;
unsigned int h_apic_timer;
//extern unsigned int time_slice;
unsigned int flags;

void
h_int_prepare(void)
{
    h_8259_m1 = h_in_port(H_PORT_8259A_MASK1);
    h_8259_m2 = h_in_port(H_PORT_8259A_MASK2);
    h_out_port(H_PORT_8259A_MASK1, 0x00);
    h_out_port(H_PORT_8259A_MASK2, 0x00);
    asm volatile ("pushf");
    asm volatile ("pop %0":"=r" (flags));
    asm volatile ("cli");
    //      h_apic_timer = *(volatile unsigned int*)(0xffffb390);
    //      time_slice = 0x400;
    //      *(volatile unsigned int*)(0xffffb380) = 256;
//    *(volatile unsigned int *) (0xffffb360) |= 0x10000;
}

void
h_int_restore(void)
{
    h_out_port(H_PORT_8259A_MASK1, h_8259_m1);
    h_out_port(H_PORT_8259A_MASK2, h_8259_m2);
    //      if (h_apic_timer !=0) *(volatile unsigned int*)(0xffffb380) = h_apic_timer;
    //      asm volatile ("int $0xef");
    //      *(volatile unsigned int*)(0xffffb0b0) = 0;
    asm volatile ("push %0"::"r" (flags));
    asm volatile ("popf");
//    *(volatile unsigned int *) (0xffffb360) &= 0xfffeffff;
}
