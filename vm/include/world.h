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
#ifndef V_WORLD_H
#define V_WORLD_H

#include "host/include/world.h"
#include "guest/include/world.h"
#include "vm/include/bt.h"
#include "vm/include/mm.h"

#define VM_PAUSED 2
#define VM_IDLE 1
#define VM_RUNNING 0
#define ASSERT(x) if (!(x)) V_ERR("Assertion failure %s: %d", __FILE__, __LINE__);

struct lru_cache;

struct v_world {
    //every cpu status in the view of host machine
    struct h_regs hregs;

    //every cpu status in the view of guest machine
    struct g_regs gregs;

    struct h_map hm;
    struct g_map cm;

    struct v_page *page_list;
    unsigned int pages;
    int (*npage) (unsigned long, struct v_world *);
    unsigned long pa_top;

    unsigned int int_enable;

    unsigned long htrbase;
    struct v_poi *poi;
    int find_poi;
    struct v_spt_info *spt_list;
    unsigned int current_valid_bps;
    struct v_poi *bp_to_poi[H_DEBUG_MAX_BP];
    unsigned int status;
    unsigned int relocate;
    struct v_mem_pool host_pools[V_MM_MAX_POOL];
    unsigned int pool_count;
#ifdef BT_CACHE
    struct lru_cache *pb_cache;
#endif
};

struct v_world *v_create_world(unsigned long);
void v_destroy_world(struct v_world *);
int v_switch_to(struct v_world *);
struct v_world *v_relocate_world(struct v_world *);
#define v_disable_int(world) (world)->int_enable = 0
#define v_enable_int(world) (world)->int_enable = 1
#define v_int_enabled(world) ((world)->int_enable)

int v_do_int(struct v_world *, unsigned int);

#endif
