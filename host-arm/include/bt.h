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
#ifndef H_BT_H
#define H_BT_H

struct v_world;

void h_set_bp(struct v_world *, unsigned long, unsigned int);
void h_clear_bp(struct v_world *, unsigned int);
void h_step_on(struct v_world *);
void h_step_off(struct v_world *);
void h_bt_reset(struct v_world *);
unsigned long h_get_bp(struct v_world *, int bp_number);
#define BPC_DISABLED 0
#define BPC_ENABLED 1
#define BPC_BAS_ANY 0x1e4
#define BPC_MISMATCH (1 << 22)

#endif
