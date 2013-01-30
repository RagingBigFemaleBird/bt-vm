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
#ifndef G_BT_H
#define G_BT_H

struct v_world;
void g_tr_set_ip(struct v_world *, unsigned long);
unsigned long g_tr_get_ip(struct v_world *);
int g_tr_next(struct v_world *, unsigned int *, unsigned long *);
void g_tr_init(void);
unsigned int g_get_poi_key(struct v_world *);

#endif
