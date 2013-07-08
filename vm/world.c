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
#include "host/include/mm.h"
#include "host/include/perf.h"
#include "host/include/interrupt.h"
#include "vm/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/logging.h"
#include "host/include/cpu.h"
#include "vm/include/lru_cache.h"

/**
 *
 * find a new shared virtual address for struct v_world
 */
struct v_world *
v_relocate_world(struct v_world *w)
{
    struct v_chunk *wc = h_raw_palloc(0);
    struct v_world *world = h_allocv(wc->phys);
    struct v_chunk v;
    v.order = 0;
    v.phys = h_v2p((h_addr_t) w);
    h_relocate_world(w, world);
    h_memcpy(world, w, sizeof(struct v_world));
    h_raw_depalloc(&v);
    return world;
}

/**
 *
 * a whole new world
 */
struct v_world *
v_create_world(unsigned long pages)
{
    struct v_chunk *trbase = h_raw_palloc(H_TRBASE_ORDER);
    struct v_chunk *w = h_raw_palloc(0);
    void *htrv;
    struct v_world *world = h_allocv(w->phys);
    struct v_page *pg;
    unsigned long i;

    world->status = VM_PAUSED;
    world->relocate = 0;
    v_disable_int(world);
    world->htrbase = (long int) (trbase->phys);
    for (i = 0; i < (1 << H_TRBASE_ORDER); i++) {
        htrv = h_allocv(world->htrbase + i * H_PAGE_SIZE);
        h_clear_page(htrv);
        h_deallocv(world->htrbase + i * H_PAGE_SIZE);
    }
    V_EVENT(">World created, TrBase = %lx, ", world->htrbase);

    pg = h_raw_malloc(sizeof(struct v_page) * (pages));

    V_LOG("pagelist %p to %p, ", pg, pg + pages);
    for (i = 0; i < pages; i++) {
        pg[i].has_virt = 0;
        pg[i].mfn = 0;
        pg[i].attr = V_PAGE_NOTPRESENT | V_PAGE_EXD;
        pg[i].poi_list = NULL;
        pg[i].ipoi_list = NULL;
        pg[i].fc_list = NULL;
        pg[i].ptp_list = NULL;
        pg[i].io_page_info = NULL;
    }
    world->page_list = pg;
    world->pages = pages;
    world->pa_top = pages * H_PAGE_SIZE;
    V_LOG("max pa = %lx, ", world->pa_top);
    h_world_init(world);
    g_world_init(world, pages);
    world->poi = NULL;
    world->spt_list = NULL;
    world->pool_count = 0;
    world->monitor_buffer_start = 0;
    world->monitor_buffer_end = 0;
    world->cpu_init_mask = 1 << (host_processor_id());
#ifdef BT_CACHE
    world->pb_cache = lru_cache_init(2047, sizeof(struct cache_target_payload));
#endif
    h_init_int();

    V_EVENT("World initialization complete<\n");

    return world;
}

void
v_destroy_world(struct v_world *world)
{
/*    int i;
    struct v_chunk chunk;
    chunk.order = 0;
    chunk.phys = h_v2p((h_addr_t) world);
    for (i = 0; i < world->pages; i++) {
    }
*/
    h_raw_dealloc(world->page_list);
//    h_raw_depalloc(&chunk);
}

/**
 *
 * switch to world
 */
int
v_switch_to(struct v_world *world)
{
    h_cpu_save(world);
    do {
        if (g_in_priv(world)) {
            if (world->poi == NULL) {
                h_perf_tsc_begin(0);
                v_bt(world);
#ifdef BT_CACHE
                v_bt_cache(world);
#endif
                h_perf_tsc_end(H_PERF_TSC_BT, 0);
            }
        } else {
            v_bt_reset(world);
        }
    } while (world->npage(world->htrbase, world)
        && (world->status == VM_RUNNING));
    return 0;
}

/**
 *
 * do interrupt for world, with param
 */
int
v_do_int(struct v_world *world, unsigned int param)
{
    return g_do_int(world, param);
}
