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
#ifndef H_WORLD_H
#define H_WORLD_H
#include "host/include/cpu.h"
#include "guest/include/cpu.h"

#define H_DEBUG_MAX_BP 3
#define h_debug_bp_count() 3

struct v_world;

struct h_regs {
    struct h_cpu hcpu;
    struct h_cpu gcpu;
    void *fpu;
    int fpusaved;
};
struct h_map {
};

void h_world_init(struct v_world *);
void h_relocate_world(struct v_world *, struct v_world *);
void h_relocate_tables(struct v_world *);
void h_relocate_npage(struct v_world *);

#endif
