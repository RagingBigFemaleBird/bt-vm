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
#include "host/include/bt.h"
#include "vm/include/bt.h"
#include "vm/include/world.h"
#include "vm/include/logging.h"

unsigned int bpaddr = 0;
extern struct h_cpu hostcpu;

void
h_step_on(struct v_world *world)
{
    int i;
    for (i = 0; i < hostcpu.number_of_breakpoints; i++) {
        world->hregs.gcpu.bvr[i] = 0;
        world->hregs.gcpu.bcr[i] = BPC_DISABLED;
    }
    V_LOG("(Step ON)");
    world->hregs.gcpu.bcr[hostcpu.number_of_breakpoints] =
        BPC_BAS_ANY | BPC_ENABLED | BPC_MISMATCH;
    world->hregs.gcpu.bvr[hostcpu.number_of_breakpoints] = g_get_ip(world);
}

void
h_step_off(struct v_world *world)
{
    V_LOG("(Step OFF)");
    world->hregs.gcpu.bcr[hostcpu.number_of_breakpoints] = BPC_DISABLED;

}

void
h_set_bp(struct v_world *world, unsigned long addr, unsigned int bp_number)
{
    world->hregs.gcpu.bcr[bp_number] = BPC_BAS_ANY | BPC_ENABLED;
    world->hregs.gcpu.bvr[bp_number] = addr;
    V_LOG("Setting BP %x to %x", bp_number, addr);
}

void
h_clear_bp(struct v_world *world, unsigned int bp_number)
{
    world->hregs.gcpu.bvr[bp_number] = 0;
    world->hregs.gcpu.bcr[bp_number] = BPC_DISABLED;
}

unsigned long
h_get_bp(struct v_world *world, int bp_number)
{
    return world->hregs.gcpu.bvr[bp_number];
}

void
h_bt_reset(struct v_world *world)
{
    int i;
    for (i = 0; i <= hostcpu.number_of_breakpoints; i++) {
        h_clear_bp(world, i);
    }
}
