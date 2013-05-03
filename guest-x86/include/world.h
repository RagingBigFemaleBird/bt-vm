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
#ifndef G_WORLD_H
#define G_WORLD_H
#include "guest/include/cpu.h"

struct g_map {
};

struct v_world;

#define g_in_priv(world) (world->gregs.mode!=G_MODE_REAL && (world)->gregs.ring == 0)
void g_world_init(struct v_world *, unsigned long);
int g_do_int(struct v_world *, unsigned int);
int g_do_io(struct v_world *, unsigned int, unsigned int, void *);

#define G_DEV_FLOPPY_DENSITY 2
#define G_CONFIG_MEM_PAGES 0x15000

#define G_IO_IN 0
#define G_IO_OUT 1
#endif
