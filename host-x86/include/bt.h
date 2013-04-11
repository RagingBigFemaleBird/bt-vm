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

#include "host/include/mm.h"

#define BT_CACHE_LEVEL3
#define BT_CACHE_THRESHOLD 0

struct v_world;

void h_set_bp(struct v_world *, h_addr_t, unsigned int);
void h_clear_bp(struct v_world *, unsigned int);
void h_step_on(struct v_world *);
void h_step_off(struct v_world *);
void h_bt_reset(struct v_world *world);

struct h_bt_cache {
    unsigned long addr;
    unsigned int dr7;
    unsigned int dr0;
    unsigned int dr1;
    unsigned int dr2;
    unsigned int dr3;
    struct v_poi *poi;
};

struct h_bt_pb_cache {
    unsigned long addr;
    struct v_poi *poi;
};

struct v_poi_cached_tree_plan;
struct v_poi_cached_tree_plan_container;

void h_bt_cache(struct v_world *, struct v_poi_cached_tree_plan *, int);
void h_bt_cache_restore(struct v_world *);
void h_bt_squash_pb(struct v_world *);
void h_bt_cache_direct(struct v_world *,
    struct v_poi_cached_tree_plan_container *);
void h_bt_exec_cache(struct v_world *,
    struct v_poi_cached_tree_plan_container *);

#endif
