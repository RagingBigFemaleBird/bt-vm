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
#ifndef G_MM_H
#define G_MM_H
#include "vm/include/world.h"

#define G_PA_BASE 0x00000000
#define G_SERIAL_BASE 0x100b8000
#define G_SERIAL_PAGE 0x101f1000
typedef unsigned int g_addr_t;

unsigned long g_v2p(struct v_world *, unsigned long, unsigned int);
unsigned int g_v2attr(struct v_world *, unsigned long);
void g_pagetable_map(struct v_world *, unsigned long);

#endif
