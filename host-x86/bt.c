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
#include "host/include/mm.h"
#include "host/include/bt.h"
#include "host/include/cpu.h"
#include "vm/include/mm.h"
#include "vm/include/bt.h"
#include "guest/include/cpu.h"
#include "guest/include/mm.h"
#include "vm/include/logging.h"
#include "guest/include/dev/fb.h"
#include "vm/include/perf.h"
#include "host/include/perf.h"
#include "guest/include/bt.h"
#include "vm/include/world.h"

void
h_step_on(struct v_world *world)
{
    V_VERBOSE("(Step ON)");
    world->hregs.gcpu.eflags |= (H_EFLAGS_TF | H_EFLAGS_RF);
}

void
h_step_off(struct v_world *world)
{
    V_VERBOSE("(Step OFF)");
    world->hregs.gcpu.eflags &= (~H_EFLAGS_TF);
}

void
h_set_bp(struct v_world *world, h_addr_t addr, unsigned int bp_number)
{
    world->hregs.gcpu.eflags &= (~H_EFLAGS_RF);
    world->hregs.gcpu.dr7 |= (0x300 | (2 << (bp_number * 2)));
    V_VERBOSE("dr7 %x", world->hregs.gcpu.dr7);
    switch (bp_number) {
    case 0:
        world->hregs.gcpu.dr0 = addr;
        break;
    case 1:
        world->hregs.gcpu.dr1 = addr;
        break;
    case 2:
        world->hregs.gcpu.dr2 = addr;
        break;
    case 3:
        world->hregs.gcpu.dr3 = addr;
        break;
    }
}

void
h_clear_bp(struct v_world *world, unsigned int bp_number)
{
    world->hregs.gcpu.dr7 &= (~(3 << (bp_number * 2)));
    V_VERBOSE("clear bp %x dr7 %x", bp_number, world->hregs.gcpu.dr7);
}

void
h_bt_reset(struct v_world *world)
{
    int i;
    for (i = 0; i < H_DEBUG_MAX_BP; i++) {
        h_clear_bp(world, i);
    }
}
