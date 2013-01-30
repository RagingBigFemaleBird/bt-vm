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
#ifndef G_SEED_H
#define G_SEED_H

struct v_world;
void g_seed_set_ip(struct v_world *, unsigned long);
unsigned long g_seed_get_ip(struct v_world *);
int g_seed_next(struct v_world *, unsigned int *, unsigned long *, void *);
void g_seed_init(void);
void *g_seed_initws(struct v_world *world);
void g_seed_do_br(struct v_world *, void *);
int g_seed_execute(struct v_world *, void *);

#endif
