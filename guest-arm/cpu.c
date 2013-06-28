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
#include "guest/include/mm.h"
#include "guest/include/cpu.h"
#include "vm/include/logging.h"
#include "vm/include/world.h"

unsigned long
g_get_ip(struct v_world *w)
{
    return w->hregs.gcpu.pc;
}

unsigned long
g_get_sp(struct v_world *w)
{
    return w->hregs.gcpu.r13;
}

unsigned int
g_get_current_ex_mode(struct v_world *w)
{
    return G_EX_MODE_ARM;
}

unsigned int
g_get_poi_key(struct v_world *w)
{
    return w->hregs.gcpu.r13;
}
