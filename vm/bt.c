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
#include "vm/include/bt.h"
#include "vm/include/perf.h"
#include "guest/include/world.h"
#include "guest/include/bt.h"
#include "guest/include/cpu.h"
#include "guest/include/mm.h"
#include "host/include/mm.h"
#include "host/include/bt.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "vm/include/see.h"
#include "host/include/perf.h"
#include "vm/include/world.h"
//#define __DEBUG_BT_CACHE

/**
 * @in addr virtual address
 * @return ipoi interrupted point of interest
 *
 */
struct v_poi *
v_find_ipoi(struct v_page *mpage, unsigned long addr, unsigned int key)
{
    struct v_ipoi *i = mpage->ipoi_list;
    while (i != NULL) {
        if (i->addr == addr && i->invalid == 0) {
            if (i->key != key) {
                V_VERBOSE("wrong key");
            } else {
                V_VERBOSE("found %lx key %x", i->addr, i->key);
                if (i->poi != NULL)
                    i->poi->expect = i->expect;
                i->invalid = 1;
                return i->poi;
            }
        }
        i = i->next;
    }
    return NULL;
}

/**
 * @in addr physical address
 * @return poi point of interest
 *
 */
struct v_poi *
v_find_poi(struct v_page *mpage, unsigned int addr)
{
    struct v_poi *i = mpage->poi_list;
    while (i != NULL) {
        if (i->addr == addr && i->invalid == 0)
            return i;
        i = i->next_poi;
    }
    return i;
}

/**
 * @in addr physical address
 * @return fc function call point
 *
 */
struct v_fc *
v_find_fc(struct v_page *mpage, unsigned int addr)
{
    struct v_fc *i = mpage->fc_list;
    while (i != NULL) {
        if (i->addr == addr)
            return i;
        i = i->next_fc;
    }
    return i;
}

/**
 * @in mpage machine page
 *
 * invalidates all poi associated with mpage
 */
void
v_translation_purge(struct v_page *mpage)
{
    struct v_poi *i = mpage->poi_list;
    /* todo: clear other POIs who link here */
    while (i != NULL) {
        i->invalid = 1;
        i = i->next_poi;
    }
}

/**
 *
 * set breakpoint
 */
static void
v_set_bp(struct v_world *world, struct v_poi *poi, unsigned int bp_number)
{
    h_set_bp(world, poi->addr, bp_number);
    ASSERT(!(poi->type & V_INST_I));
    world->bp_to_poi[bp_number] = poi;
    V_LOG("Setting breakpoint %x at %lx type %x", bp_number, poi->addr,
        poi->type);
}

#define V_BP_ARENA_SIZE 8192

static struct v_poi_tree v_bp_array[V_BP_ARENA_SIZE];

void
v_bt_reset(struct v_world *world)
{
    world->poi = NULL;
    h_bt_reset(world);
}

/**
 *
 * add interrupted point of interest
 */
void
v_add_ipoi(struct v_world *world, unsigned int addr, unsigned int key,
    struct v_poi *p)
{
    g_addr_t ip = g_get_ip(world);
    g_addr_t phys = g_v2p(world, ip, 1);
    struct v_page *mpage = h_p2mp(world, phys);
    struct v_ipoi *i;
    if (mpage == NULL) {
        V_ERR("Error: guest page fault @ add_ipoi\n");
        return;
    }
    i = mpage->ipoi_list;
    while (i != NULL) {
        if (i->addr == addr && i->invalid) {
            V_VERBOSE("reuse %lx", i->addr);
            break;
        }
        i = i->next;
    }
    if (i == NULL) {
        i = h_valloc(sizeof(struct v_ipoi));
        i->next = mpage->ipoi_list;
        mpage->ipoi_list = i;
    }
    i->addr = addr;
    i->key = key;
    i->poi = p;
    i->invalid = 0;
    if (p != NULL)
        i->expect = p->expect;
}

/**
 *
 * add point of interest
 */
struct v_poi *
v_add_poi(struct v_world *world, struct v_page *mpage, unsigned int addr,
    unsigned int type, unsigned int mode)
{
    struct v_poi *poi = v_mem_pool_alloc(world, sizeof(struct v_poi));
    poi->addr = addr;
    poi->type = type;
    poi->ex_mode = mode;
    poi->next_inst = NULL;
    poi->next_inst_taken = NULL;
    poi->next_poi = mpage->poi_list;
    poi->invalid = 0;
    poi->tree = NULL;
    poi->plan.valid = 0;
#ifdef BT_CACHE
    poi->cached_plan = NULL;
    poi->invalidate_cached_plan_count = 0;
    poi->cache_threshold = 0;
    poi->pb_cache.total = 0;
    poi->pb_cache.replace = 0;
#endif
#ifdef V_POI_PB_CACHED_POI
    poi->pb_cache_poi.total = 0;
    poi->pb_cache_poi.replace = 0;
#endif
    if (type & V_INST_PB)
        v_perf_inc(V_PERF_POI_PB, 1);
    if (type & V_INST_CB)
        v_perf_inc(V_PERF_POI_CB, 1);
    if (type & V_INST_F)
        v_perf_inc(V_PERF_POI_F, 1);
    mpage->poi_list = poi;
    return poi;
}

/**
 *
 * add function calls
 */
struct v_fc *
v_add_fc(struct v_page *mpage, unsigned int addr, unsigned int mode)
{
    struct v_fc *fc = h_valloc(sizeof(struct v_fc));
    fc->addr = addr;
    fc->ex_mode = mode;
    fc->SEE_checked = 0;
    fc->SEE_safe = SEE_TOO_COMPLEX;
    fc->SEE_ws = 0;
    fc->next_fc = mpage->fc_list;
    mpage->fc_list = fc;
    return fc;
}

/**
 * @in ip virtual address
 *
 * start translation at ip
 */
static struct v_poi *
v_translate(struct v_world *world, unsigned long ip)
{
    struct v_poi *poi = NULL;
    unsigned int i_type;
    unsigned long b_target;
    struct v_page *mpage;
    V_VERBOSE("translation starting %lx", ip);
    g_tr_set_ip(world, ip);
    do {
        ip = g_tr_get_ip(world);
        if (g_tr_next(world, &i_type, &b_target)) {
            V_ERR
                ("Guest translation caused an irrecoverable fault, guest probably crashed");
            world->status = VM_PAUSED;
            break;
        }
        if (i_type & V_INST_RAM) {
            V_EVENT("RAM");
        }
    } while (i_type & V_INST_I);
    mpage = h_p2mp(world, g_v2p(world, ip, 1));
    if (mpage != NULL) {
        poi = v_find_poi(mpage, ip);
        if (poi != NULL)
            return poi;
        poi = v_add_poi(world, mpage, ip, i_type, g_get_current_ex_mode(world));
        if ((mpage->attr & V_PAGE_TYPE_MASK) == V_PAGE_EXD) {
            mpage->attr &= (~V_PAGE_TYPE_MASK);
            mpage->attr |= V_PAGE_STEP;
            v_spt_inv_page(world, mpage);
            h_new_trbase(world);
            V_LOG("Write protecting %lx to %x", ip, mpage->attr);
        }
    }
    return poi;
}

/**
 * @in ip virtual address
 *
 * start translation at ip, mark the first instruction as a cue if possible
 */
static struct v_poi *
v_translate_cue(struct v_world *world, unsigned long ip)
{
    struct v_poi *poi = NULL;
    struct v_poi *cue = NULL;
    struct v_page *mpage;
    V_VERBOSE("translation_cue starting %lx", ip);
    poi = v_translate(world, ip);
    /* it's already marked, the very first instruction is not INST_I. */
    if (poi->addr == ip)
        return poi;
    mpage = h_p2mp(world, g_v2p(world, ip, 1));
    if (mpage != NULL) {
        cue = v_find_poi(mpage, ip);
        if (cue != NULL)
            return poi;
        cue =
            v_add_poi(world, mpage, ip, V_INST_I, g_get_current_ex_mode(world));
        cue->next_inst = poi;
        if ((mpage->attr & V_PAGE_TYPE_MASK) == V_PAGE_EXD) {
            mpage->attr &= (~V_PAGE_TYPE_MASK);
            mpage->attr |= V_PAGE_STEP;
            v_spt_inv_page(world, mpage);
            h_new_trbase(world);
            V_LOG("Write protecting %lx to %x", ip, mpage->attr);
        }
    }
    return poi;
}

/**
 *
 * check for existing poi
 */
static inline int
v_bp_tree_find_poi(struct v_poi_tree *tree, struct v_poi *poi, int max)
{
    int i;
    for (i = 0; i < max; i++) {
        if (tree[i].poi == poi)
            return i;
    }
    return -1;
}

/**
 *
 * construct breakpoint tree starting from poi
 */
static void
v_poi_construct_tree(struct v_world *world, struct v_poi *poi)
{
    unsigned int i_type, bp_i = 0;
    unsigned long b_target, ip;
    int j, current_bp_count = 0;
    ASSERT(poi->tree == NULL);
    v_bp_array[current_bp_count].poi = poi;
    current_bp_count++;
    V_VERBOSE("BT exploring at %lx", poi->addr);
    do {
        V_VERBOSE("item %x addr %lx", bp_i, v_bp_array[bp_i].poi->addr);
        if (v_bp_array[bp_i].poi->type & V_INST_CB) {
            struct v_poi *poi = v_bp_array[bp_i].poi;
            V_VERBOSE("expand %lx type CB", poi->addr);
            if (poi->next_inst_taken == NULL || poi->next_inst_taken->invalid) {
                g_tr_set_ip(world, poi->addr);
                g_tr_next(world, &i_type, &b_target);
                poi->next_inst_taken = v_translate(world, b_target);
                if (poi->next_inst_taken == NULL) {
                    V_ALERT
                        ("construction of poi tree failed at %lx", poi->addr);
                    return;
                }
            }
            j = v_bp_tree_find_poi(v_bp_array, poi->next_inst_taken,
                current_bp_count);
            if (j < 0) {
                j = current_bp_count;
                v_bp_array[current_bp_count].poi = poi->next_inst_taken;
                current_bp_count++;
            }
            v_bp_array[bp_i].u.inext_taken = j;
            V_VERBOSE("taken = %x", j);

            if (poi->next_inst == NULL || poi->next_inst->invalid) {
                g_tr_set_ip(world, poi->addr);
                g_tr_next(world, &i_type, &b_target);
                ip = g_tr_get_ip(world);
                poi->next_inst = v_translate(world, ip);
                if (poi->next_inst == NULL) {
                    V_ALERT
                        ("construction of poi tree failed at %lx", poi->addr);
                    return;
                }
            }
            j = v_bp_tree_find_poi(v_bp_array, poi->next_inst,
                current_bp_count);
            if (j < 0) {
                j = current_bp_count;
                v_bp_array[current_bp_count].poi = poi->next_inst;
                current_bp_count++;
            }
            v_bp_array[bp_i].inext = j;
            V_VERBOSE("not taken = %x", j);
        } else if (v_bp_array[bp_i].poi->type & V_INST_UB) {
            struct v_poi *poi = v_bp_array[bp_i].poi;
            V_VERBOSE("expand %lx type UB", poi->addr);
            if (poi->next_inst == NULL || poi->next_inst->invalid) {
                g_tr_set_ip(world, poi->addr);
                g_tr_next(world, &i_type, &b_target);
                poi->next_inst = v_translate(world, b_target);
                if (poi->next_inst == NULL) {
                    V_ALERT
                        ("construction of poi tree failed at %lx", poi->addr);
                    return;
                }
            }
            if (poi->type & V_INST_FC) {
                struct v_page *mpage;
                struct v_fc *pfc;
                unsigned long fc_target;
                g_tr_set_ip(world, poi->addr);
                g_tr_next(world, &i_type, &fc_target);
                mpage = h_p2mp(world, g_v2p(world, fc_target, 1));
                pfc = v_find_fc(mpage, fc_target);
                if (!pfc) {
                    V_VERBOSE("adding fc %lx", fc_target);
                    pfc =
                        v_add_fc(mpage, fc_target,
                        g_get_current_ex_mode(world));
                }
                if (!pfc->SEE_checked) {
                    V_LOG("Frequent reference to %lx", fc_target);
                    SEE_verify(world, pfc);
                }
                if (!pfc->SEE_checked) {
                    V_ALERT("SEE malfuntioned %lx", fc_target);
                    return;
                }
                if (pfc->SEE_safe != SEE_SAFE) {
                    /*checked and not safe */
                    j = v_bp_tree_find_poi(v_bp_array,
                        poi->next_inst, current_bp_count);
                    if (j < 0) {
                        j = current_bp_count;
                        v_bp_array[current_bp_count].poi = poi->next_inst;
                        current_bp_count++;
                    }
                    v_bp_array[bp_i].inext = j;
                    V_VERBOSE("not safe taken = %x", j);
                } else {
                    v_bp_array[bp_i].inext = -1;
                    V_VERBOSE("using safe function call %lx", fc_target);

                    if (poi->next_inst_taken == NULL
                        || poi->next_inst_taken->invalid) {
                        g_tr_set_ip(world, poi->addr);
                        g_tr_next(world, &i_type, &b_target);
                        poi->next_inst_taken =
                            v_translate(world, g_tr_get_ip(world));
                        if (poi->next_inst_taken == NULL) {
                            V_ALERT
                                ("construction of poi tree failed at %lx",
                                poi->addr);
                            return;
                        }
                    }
                    j = v_bp_tree_find_poi(v_bp_array,
                        poi->next_inst_taken, current_bp_count);
                    if (j < 0) {
                        j = current_bp_count;
                        v_bp_array[current_bp_count].poi = poi->next_inst_taken;
                        current_bp_count++;
                    }
                    v_bp_array[bp_i].u.inext_ret = j;
                    V_VERBOSE("ret = %x", j);
                }
            } else {
                j = v_bp_tree_find_poi(v_bp_array,
                    poi->next_inst, current_bp_count);
                if (j < 0) {
                    j = current_bp_count;
                    v_bp_array[current_bp_count].poi = poi->next_inst;
                    current_bp_count++;
                }
                v_bp_array[bp_i].inext = j;
                V_VERBOSE("taken = %x", j);
            }
        } else if (v_bp_array[bp_i].poi->type & V_INST_I) {
            struct v_poi *poi = v_bp_array[bp_i].poi;
            V_VERBOSE("expand %lx type I", poi->addr);
            if (poi->next_inst == NULL || poi->next_inst->invalid) {
                g_tr_set_ip(world, poi->addr);
                poi->next_inst = v_translate(world, poi->addr);
                if (poi->next_inst == NULL) {
                    V_ALERT
                        ("construction of poi tree failed at %lx", poi->addr);
                    return;
                }
            }
            j = v_bp_tree_find_poi(v_bp_array, poi->next_inst,
                current_bp_count);
            if (j < 0) {
                j = current_bp_count;
                v_bp_array[current_bp_count].poi = poi->next_inst;
                current_bp_count++;
            }
            v_bp_array[bp_i].inext = j;
            V_VERBOSE("taken = %x", j);
        }
        bp_i++;
    } while (bp_i < current_bp_count && current_bp_count < V_BP_ARENA_SIZE);
    if (current_bp_count == V_BP_ARENA_SIZE) {
        V_ALERT("translation exceeds arena %lx", poi->addr);
        return;
    }
    V_VERBOSE("explored %x items", current_bp_count);
    poi->tree = h_valloc(current_bp_count * sizeof(struct v_poi_tree));
    h_memcpy(poi->tree, v_bp_array,
        current_bp_count * sizeof(struct v_poi_tree));
    poi->tree_count = current_bp_count;
    for (j = 0; j < current_bp_count; j++) {
        poi->tree[j].poi->tree = poi->tree;
        poi->tree[j].poi->tree_count = current_bp_count;
    }
}

static void
v_poi_clear_flag0(struct v_poi_tree *tree, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        tree[i].flag[0] = 0;
    }
}

static void
v_poi_clear_flags(struct v_poi_tree *tree, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        tree[i].flag[0] = 0;
        tree[i].flag[1] = 0;
        tree[i].flag[2] = 0;
    }
}

static int
v_poi_run_coverage(struct v_poi_tree *tree, int i, int count)
{
    while (i < count) {
        if (tree[i].flag[0]) {
            return 0;
        }
        /* set visited flag */
        tree[i].flag[0] = 1;
        if (tree[i].poi->type & V_INST_UB) {
            if ((tree[i].poi->type & V_INST_FC)
                && tree[i].inext < 0) {
                /*safe function call */
                i = tree[i].u.inext_ret;
            } else {
                i = tree[i].inext;
            }
        } else if (tree[i].poi->type & V_INST_I) {
            i = tree[i].inext;
        } else if (tree[i].poi->type & V_INST_PB) {
            return 1;
        } else if (tree[i].poi->type & V_INST_F) {
            return 1;
        } else if (tree[i].poi->type & V_INST_U) {
            return 1;
        } else if (tree[i].poi->type & V_INST_CB) {
            tree[i].flag[1] = v_poi_run_coverage(tree, tree[i].inext, count);
            tree[i].flag[2] =
                v_poi_run_coverage(tree, tree[i].u.inext_taken, count);
            return tree[i].flag[1] + tree[i].flag[2];
        } else {
            V_ALERT("poi tree walk into unknown poi type");
            return 1;
        }
    }
    if (i >= count) {
        V_ALERT("poi tree walk out of coverage");
        return 1;
    }
    return 0;
}

static int
v_poi_tree_set_bps(struct v_world *world, struct v_poi_tree *tree, int i,
    int count, int bps)
{
    while (i < count) {
        if (tree[i].flag[0]) {
            return 0;
        }
        /* set visited flag */
        tree[i].flag[0] = 1;
        V_VERBOSE("at %lx type %x %x %x %x bps %x", tree[i].poi->addr,
            tree[i].poi->type, tree[i].flag[0], tree[i].flag[1],
            tree[i].flag[2], bps);
        if (tree[i].poi->type & V_INST_UB) {
            if ((tree[i].poi->type & V_INST_FC)
                && tree[i].inext < 0) {
                /*safe function call */
                i = tree[i].u.inext_ret;
            } else {
                i = tree[i].inext;
            }
        } else if (tree[i].poi->type & V_INST_I) {
            i = tree[i].inext;
        } else if (tree[i].poi->type & V_INST_PB) {
            ASSERT(bps >= 1);
            tree[i].poi->tree = tree;
            tree[i].poi->tree_count = count;
            v_set_bp(world, tree[i].poi, world->bpc);
            world->bpc++;
            return 1;
        } else if (tree[i].poi->type & V_INST_F) {
            ASSERT(bps >= 1);
            tree[i].poi->tree = tree;
            tree[i].poi->tree_count = count;
            v_set_bp(world, tree[i].poi, world->bpc);
            world->bpc++;
            return 1;
        } else if (tree[i].poi->type & V_INST_U) {
            ASSERT(bps >= 1);
            tree[i].poi->tree = tree;
            tree[i].poi->tree_count = count;
            v_set_bp(world, tree[i].poi, world->bpc);
            world->bpc++;
            return 1;
        } else if (tree[i].poi->type & V_INST_CB) {
            int k = 0, incomplete_left = 0;
            ASSERT(bps >= 1);
            if (tree[i].flag[1] + tree[i].flag[2] > 1 && bps == 1
                && (!tree[tree[i].inext].flag[0])
                && (!tree[tree[i].u.inext_taken].flag[0])) {
                /* we cannot branch if we have only 1 more quota and neither branch
                 * is visited yet */
                tree[i].poi->tree = tree;
                tree[i].poi->tree_count = count;
                v_set_bp(world, tree[i].poi, world->bpc);
                world->bpc++;
                return 1;
            }
            if (tree[i].flag[1]) {
                int m = bps;
                if (tree[i].flag[2]) {
                    /*if the right side has something to set, don't give full quota */
                    m = bps - 1;
                }
                if ((!tree[tree[i].u.inext_taken].flag[0]) &&
                    /*we have not visited the right path, and */
                    m < tree[i].flag[1]) {
                    /* even if we give all quota to the left side, we cannot
                     * exhaust the path */
                    if (tree[i].flag[2] == 0) {
                        /* therefore, give one less quota to the left because
                         * we are going to set at least one bp on the right,
                         * even if DFS search says nothing to set on the right */
                        m = bps - 1;
                    }
                    incomplete_left = 1;
                }
                k = v_poi_tree_set_bps(world, tree, tree[i].inext, count, m);
            }
            bps -= k;
            if (incomplete_left) {
                ASSERT(bps >= 1);
                i = tree[i].u.inext_taken;
                tree[i].poi->tree = tree;
                tree[i].poi->tree_count = count;
                v_set_bp(world, tree[i].poi, world->bpc);
                world->bpc++;
                k++;
            } else if (tree[i].flag[2]) {
                int m = bps;
                ASSERT(bps >= 1);
                k += v_poi_tree_set_bps(world, tree,
                    tree[i].u.inext_taken, count, m);
            }
            return k;
        } else {
            V_ALERT("poi tree walk into unknown poi type");
            return 1;
        }
    }
    if (i >= count) {
        V_ALERT("poi tree walk out of coverage");
        return 1;
    }
    return 0;
}

/**
 * plan breakpoints given poi (poi tree must be there)
 *
 */
void
v_poi_plan_bp(struct v_world *world, struct v_poi *poi, int bp_count)
{
    int i;
    struct v_poi_tree *tree = poi->tree;
    ASSERT(tree != NULL);
    ASSERT(bp_count >= 1);
    if (poi->plan.valid) {
        int j;
        ASSERT(bp_count >= poi->plan.count);
        for (j = 0; j < poi->plan.count; j++) {
            if (poi->plan.poi[j]->invalid) {
                poi->plan.valid = 0;
                goto rebuild_plan;
            }
            v_set_bp(world, poi->plan.poi[j], j);
        }
        world->current_valid_bps = j;
        return;
    }
  rebuild_plan:
    i = v_bp_tree_find_poi(tree, poi, poi->tree_count);
    if (i < 0) {
        V_ALERT("inconsistent tree");
        return;
    }
    V_VERBOSE("clear flags");
    v_poi_clear_flags(tree, poi->tree_count);
    V_VERBOSE("run coverage");
    v_poi_run_coverage(tree, i, poi->tree_count);
    /* flag 1-2 represents branch left-right diff */
    V_VERBOSE("clear flags");
    v_poi_clear_flag0(tree, poi->tree_count);
    world->bpc = 0;
    V_VERBOSE("set bps");
    v_poi_tree_set_bps(world, tree, i, poi->tree_count, bp_count);
    world->current_valid_bps = world->bpc;
    for (i = 0; i < world->bpc; i++) {
        poi->plan.poi[i] = world->bp_to_poi[i];
    }
    poi->plan.count = world->bpc;
    poi->plan.valid = 1;
}

#ifdef BT_CACHE

void
v_update_pb_cache(struct v_poi *poi, unsigned long dest)
{
    int i;
    struct v_poi_pb_cache *payload = &(poi->pb_cache);
    for (i = 0; i < payload->total; i++) {
        if (payload->targets[i] == dest)
            return;
    }
    if (payload->total >= V_POI_PB_CACHE_ENTIRES_TOTAL) {
        payload->targets[payload->replace] = dest;
        payload->replace++;
        if (payload->replace >= V_POI_PB_CACHE_ENTIRES_TOTAL)
            payload->replace = 0;
    } else {
        payload->targets[payload->total] = dest;
        payload->total++;
    }
}

void
v_dump_pb_cache(struct v_world *world)
{
}
#endif

#ifdef V_POI_PB_CACHED_POI

void
v_update_pb_cache_poi(struct v_world *world, struct v_poi *poi,
    unsigned long dest)
{
    int i;
    struct v_poi_pb_cache_poi *cache = &(poi->pb_cache_poi);
    struct v_poi *poi_dest;
    g_addr_t phys;
    struct v_page *mpage;
    phys = g_v2p(world, dest, 1);
    if (phys >= world->pa_top) {
        V_ERR("Error: guest page fault @ v_bt, crazy phys addr %lx, %x\n", dest,
            phys);
        return;
    }
    mpage = h_p2mp(world, phys);
    if ((poi_dest = v_find_poi(mpage, dest)) == NULL) {
        v_perf_inc(V_PERF_EVICT, 1);
        return;
    }
    for (i = 0; i < cache->total; i++) {
        if (cache->targets[i]->addr == dest)
            return;
    }
    V_VERBOSE("Updated poi %x addr %x with cache %x", poi, poi->addr,
        poi_dest->addr);
    if (cache->total >= V_POI_PB_CACHE_ENTIRES_TOTAL) {
        v_perf_inc(V_PERF_FULL, 1);
        cache->targets[cache->replace] = poi_dest;
        cache->replace++;
        if (cache->replace >= V_POI_PB_CACHE_ENTIRES_TOTAL)
            cache->replace = 0;
    } else {
        cache->targets[cache->total] = poi_dest;
        cache->total++;
    }
}

#endif

/**
 * @in addr virtual address
 * @in is_step world is stepping
 *
 * handles breakpoint exception for world
 */
void
v_do_bp(struct v_world *world, unsigned long addr, unsigned int is_step)
{
    struct v_page *mpage;
    g_addr_t ip, phys;
    unsigned int i;
    h_perf_tsc_begin(1);
    ip = g_get_ip(world);
    phys = g_v2p(world, ip, 1);
    mpage = h_p2mp(world, phys);
    h_perf_tsc_end(H_PERF_TSC_MAPPING, 1);
    if (mpage == NULL) {
        world->status = VM_PAUSED;
        V_ERR("Error: guest page fault @ v_do_bp, %x, %x\n", ip, phys);
        return;
    }
    if (!is_step) {
        world->poi = NULL;
        for (i = 0; i < world->current_valid_bps; i++) {
            h_clear_bp(world, i);
            if (world->bp_to_poi[i]->addr == ip) {
                world->poi = world->bp_to_poi[i];
            }
        }
        if (world->poi == NULL) {
            V_ERR
                ("Current IP %lx is not any of the BPs. Lost control of VM.",
                (unsigned long) ip);
            world->status = VM_PAUSED;
        }
    }
    if (is_step) {
        h_step_off(world);
        if (!(ip == world->poi->addr)) {
#ifdef V_POI_PB_CACHED_POI
            v_update_pb_cache_poi(world, world->poi, ip);
#endif
#ifdef BT_CACHE
            v_update_pb_cache(world->poi, ip);
#ifdef __DEBUG_BT_CACHE
            V_ERR("update %x is %x", world->poi->addr, ip);
#endif
            if (world->poi->invalidate_cached_plan_count > 0) {
                int r;
                for (r = 0; r < world->poi->invalidate_cached_plan_count; r++) {
                    if (world->poi->invalidate_cached_plan[r]->length <
                        BT_CACHE_CAPACITY) {
#ifdef __DEBUG_BT_CACHE
                        V_ERR("Invalidate %x",
                            world->poi->invalidate_cached_plan[r]);
#endif
                        world->poi->invalidate_cached_plan[r]->done = 0;
                    }
                }
            }
            V_VERBOSE("Update cache for %lx is %x", world->poi->addr, ip);
#endif
            world->poi = NULL;
            v_perf_inc(V_PERF_BT_P, 1);
        } else {
            V_VERBOSE("Step on: unnecessary function return breakpoints");
        }
    }
    /* if NULL, let v_bt() reselect a poi for us */
    if (world->poi == NULL)
        return;
    world->poi->expect = 1;
    if ((world->poi->type & V_INST_F) && ip == world->poi->addr) {
        h_perf_tsc_begin(1);
        h_do_fail_inst(world, ip);
        v_perf_inc(V_PERF_BT_F, 1);
        h_perf_tsc_end(H_PERF_TSC_MINUS_FI, 1);
        if (world->poi != NULL && world->poi->f_cache_poi == NULL) {
            ip = g_get_ip(world);
            phys = g_v2p(world, ip, 1);
            if (phys < world->pa_top) {
                mpage = h_p2mp(world, phys);
                if (mpage != NULL) {
                    world->poi->f_cache_poi =
                        v_find_poi(mpage, g_get_ip(world));
                }
            }
        }
        world->poi = NULL;
    } else if (((world->poi->type & V_INST_U)
            || (world->poi->type & V_INST_PB))
        && (ip == world->poi->addr)) {
        h_step_on(world);
        v_perf_inc(V_PERF_BT_PS, 1);
    } else {
        world->poi->expect = 0;
        if (world->poi->tree == NULL) {
            h_perf_tsc_begin(1);
            v_poi_construct_tree(world, world->poi);
            h_perf_tsc_end(H_PERF_TSC_TREE, 1);
            V_VERBOSE("Tree construction done");
        }
        h_perf_tsc_begin(1);
        v_poi_plan_bp(world, world->poi, H_DEBUG_MAX_BP);
        h_perf_tsc_end(H_PERF_TSC_PLAN, 1);
#ifdef BT_CACHE
        v_bt_cache(world);
#endif
    }
}

/**
 *
 * main function, initiate breakpoint tracing at current ip
 */
void
v_bt(struct v_world *world)
{
    g_addr_t ip, phys;
    struct v_page *mpage;
    ASSERT(world->poi == NULL);
    /*todo: h perf counters should not be here. */
    h_perf_tsc_begin(1);
    ip = g_get_ip(world);
    phys = g_v2p(world, ip, 1);
    h_perf_tsc_end(H_PERF_TSC_MAPPING, 1);
    if (phys >= world->pa_top) {
        world->status = VM_PAUSED;
        V_ERR("Error: guest page fault @ v_bt, crazy phys addr %x, %x\n", ip,
            phys);
        return;
    }
    mpage = h_p2mp(world, phys);
    if (mpage == NULL) {
        world->status = VM_PAUSED;
        V_ERR("Error: guest page fault @ v_bt, %x, %x\n", ip, phys);
        return;
    }

    if (world->find_poi) {
        world->find_poi = 0;
        world->poi = v_find_ipoi(mpage, ip, g_get_poi_key(world));
        if (world->poi != NULL) {
            V_VERBOSE("saved poi = %lx", world->poi->addr);
            if (world->poi->expect) {
                world->current_valid_bps = 1;
                v_set_bp(world, world->poi, 0);
            } else {
                if (world->poi->tree == NULL) {
                    h_perf_tsc_begin(1);
                    v_poi_construct_tree(world, world->poi);
                    h_perf_tsc_end(H_PERF_TSC_TREE, 1);
                    V_VERBOSE("Tree construction done");
                }
                h_perf_tsc_begin(1);
                v_poi_plan_bp(world, world->poi, H_DEBUG_MAX_BP);
                h_perf_tsc_end(H_PERF_TSC_PLAN, 1);
            }
            return;
        }
    }
    if ((world->poi = v_find_poi(mpage, ip)) == NULL) {
        struct v_poi *poi;
        h_perf_tsc_begin(1);
        poi = v_translate_cue(world, ip);
        h_perf_tsc_end(H_PERF_TSC_TRANSLATE, 1);
        world->poi = poi;
        if (world->poi == NULL) {
            world->status = VM_PAUSED;
            V_ERR("Internal error...");
        }
    }
    while (world->poi->next_inst != NULL && ((world->poi->type & V_INST_I)
            || (world->poi->type & V_INST_UB))) {
        world->poi = world->poi->next_inst;
    }
    if (world->poi->type & V_INST_CB) {
        /* it's a conditional branch. do it directly. save one world switch */
        world->poi->expect = 0;
        if (world->poi->tree == NULL) {
            h_perf_tsc_begin(1);
            v_poi_construct_tree(world, world->poi);
            h_perf_tsc_end(H_PERF_TSC_TREE, 1);
            V_VERBOSE("Tree construction done");
        }
        h_perf_tsc_begin(1);
        v_poi_plan_bp(world, world->poi, H_DEBUG_MAX_BP);
        h_perf_tsc_end(H_PERF_TSC_PLAN, 1);
    } else {
        world->poi->expect = 1;
        h_bt_reset(world);
        world->current_valid_bps = 1;
        v_set_bp(world, world->poi, 0);
    }
}

#ifdef BT_CACHE
void
_v_bt_cache(struct v_world *world, struct v_poi *poi, int depth,
    struct v_poi_cached_tree_plan_container *cache, int *cache_count)
{
    int i, j, total = 0, r;
    unsigned int cache_addr;
    struct v_poi *todo_poi[H_DEBUG_MAX_BP];
    if (depth >= BT_CACHE_LEVEL)
        return;
    if (*cache_count >= BT_CACHE_CAPACITY) {
        v_perf_inc(V_PERF_FULL, 1);
        return;
    }
    for (i = 0; i < *cache_count; i++) {
        if (cache->plan[i].addr == poi->addr)
            return;
    }
    cache_addr = poi->addr;
    while (poi->type & V_INST_I)
        poi = poi->next_inst;
    if (poi->type & V_INST_CB) {
        if (poi->tree == NULL) {
            h_perf_tsc_begin(1);
            v_poi_construct_tree(world, poi);
            h_perf_tsc_end(H_PERF_TSC_TREE, 1);
            V_VERBOSE("Tree construction done");
        }
        h_perf_tsc_begin(1);
        v_poi_plan_bp(world, poi, H_DEBUG_MAX_BP);
        h_perf_tsc_end(H_PERF_TSC_PLAN, 1);
        total = world->current_valid_bps;
        for (i = 0; i < total; i++) {
            todo_poi[i] = world->bp_to_poi[i];
        }
    }
    cache->plan[*cache_count].addr = cache_addr;
    cache->plan[*cache_count].plan = &poi->plan;
    cache->plan[*cache_count].poi = poi;
    *cache_count = (*cache_count) + 1;
    if (!(poi->type & V_INST_CB))
        return;
    for (i = 0; i < total; i++) {
        if (todo_poi[i]->type & V_INST_CB) {
            V_VERBOSE("Recursively do %lx", todo_poi[i]->addr);
            _v_bt_cache(world, todo_poi[i], depth + 1, cache, cache_count);
        }
        if (todo_poi[i]->type & V_INST_PB) {
            struct v_poi_pb_cache *payload = &(todo_poi[i]->pb_cache);
            if (todo_poi[i]->invalidate_cached_plan_count > 0) {
                for (r = 0; r < todo_poi[i]->invalidate_cached_plan_count; r++) {
                    if (todo_poi[i]->invalidate_cached_plan[r] == cache) {
                        goto found;
                    }
                }
            }
            if (todo_poi[i]->invalidate_cached_plan_count >=
                BT_CACHE_INVALIDATE_ENTRIES_COUNT) {
                for (r = 0; r < todo_poi[i]->invalidate_cached_plan_count; r++) {
                    todo_poi[i]->invalidate_cached_plan[r]->done = 0;
#ifdef __DEBUG_BT_CACHE
                    V_ERR("Replacement eviction %p",
                        todo_poi[i]->invalidate_cached_plan[r]);
#endif
                    v_perf_inc(V_PERF_EVICT, 1);
                }
                todo_poi[i]->invalidate_cached_plan_count = 0;
            }
            todo_poi[i]->invalidate_cached_plan[todo_poi
                [i]->invalidate_cached_plan_count++] = cache;
          found:
            for (j = 0; j < payload->total; j++) {
                unsigned long possible_ip = payload->targets[j];
                unsigned long phys = g_v2p(world, possible_ip, 1);
                struct v_page *mpage = h_p2mp(world, phys);
                struct v_poi *possible_poi;
                if (mpage == NULL)
                    continue;
#ifdef __DEBUG_BT_CACHE
                V_ERR("pb target %x", possible_ip);
#endif
                possible_poi = v_find_poi(mpage, possible_ip);
                if (possible_poi == NULL)
                    continue;
                V_VERBOSE("Found poi %lx for %lx", possible_poi->addr,
                    todo_poi[i]->addr);
                _v_bt_cache(world, possible_poi, depth + 1, cache, cache_count);
            }
        }
    }
}

#ifdef __DEBUG_BT_CACHE
unsigned int lastcache = 0;
#endif
void
v_bt_cache(struct v_world *world)
{
    int i, j, total, r;
    struct v_poi *save_poi[H_DEBUG_MAX_BP];
    struct v_poi_cached_tree_plan_container *cache;
    int cache_count = 0;
    h_perf_tsc_begin(2);
    if (world->poi->cache_threshold++ < BT_CACHE_THRESHOLD) {
        h_bt_cache(world, NULL, 0);
        return;
    }
    total = world->current_valid_bps;
    if (world->poi->cached_plan == NULL) {
        struct v_poi_cached_tree_plan *plan =
            h_valloc(sizeof(struct v_poi_cached_tree_plan) * BT_CACHE_CAPACITY);
        world->poi->cached_plan =
            h_valloc(sizeof(struct v_poi_cached_tree_plan_container));
        world->poi->cached_plan->plan = plan;
        world->poi->cached_plan->done = 0;
        world->poi->cached_plan->exec_cache = NULL;
    }
    cache = world->poi->cached_plan;
    if (cache->done) {
#ifdef BT_CACHE_LEVEL3
        h_bt_cache_direct(world, cache);
#else
        h_bt_cache(world, cache->plan, cache->length);
#endif
    } else {
#ifdef __DEBUG_BT_CACHE
        if (lastcache == cache)
            V_ERR("%p starting", cache);
#endif
        for (i = 0; i < total; i++) {
            save_poi[i] = world->bp_to_poi[i];
        }
        if (world->poi->type & V_INST_PB) {
            struct v_poi_pb_cache *payload = &(world->poi->pb_cache);
#ifdef __DEBUG_BT_CACHE
            lastcache = cache;
            if (lastcache == cache)
                V_ERR("Root pb %x", world->poi->addr);
#endif
            if (world->poi->invalidate_cached_plan_count > 0) {
                for (r = 0; r < world->poi->invalidate_cached_plan_count; r++) {
                    if (world->poi->invalidate_cached_plan[r] == cache) {
                        goto found2;
                    }
                }
            }
            if (world->poi->invalidate_cached_plan_count >=
                BT_CACHE_INVALIDATE_ENTRIES_COUNT) {
                for (r = 0; r < world->poi->invalidate_cached_plan_count; r++) {
#ifdef __DEBUG_BT_CACHE
                    V_ERR("Replacement eviction %p",
                        world->poi->invalidate_cached_plan[r]);
#endif
                    v_perf_inc(V_PERF_EVICT, 1);
                    world->poi->invalidate_cached_plan[r]->done = 0;
                }
                world->poi->invalidate_cached_plan_count = 0;
            }
            world->poi->invalidate_cached_plan[world->
                poi->invalidate_cached_plan_count++] = cache;
          found2:
            for (j = 0; j < payload->total; j++) {
                unsigned long possible_ip = payload->targets[j];
                unsigned long phys = g_v2p(world, possible_ip, 1);
                struct v_page *mpage = h_p2mp(world, phys);
                struct v_poi *possible_poi;
                if (mpage == NULL)
                    continue;
                possible_poi = v_find_poi(mpage, possible_ip);
                if (possible_poi == NULL)
                    continue;
                V_VERBOSE("Found poi %lx for %lx", possible_poi->addr,
                    world->poi->addr);
                _v_bt_cache(world, possible_poi, 0, cache, &cache_count);
            }
        }

        for (i = 0; i < total; i++) {
            if (save_poi[i]->type & V_INST_CB) {
                V_VERBOSE("(M)Recursively do %lx", save_poi[i]->addr);
                _v_bt_cache(world, save_poi[i], 0, cache, &cache_count);
            }
            if (save_poi[i]->type & V_INST_PB) {
                struct v_poi_pb_cache *payload = &(save_poi[i]->pb_cache);
#ifdef __DEBUG_BT_CACHE
                if (lastcache == cache)
                    V_ERR("pb %x", save_poi[i]->addr);
#endif
                if (save_poi[i]->invalidate_cached_plan_count > 0) {
                    for (r = 0; r < save_poi[i]->invalidate_cached_plan_count;
                        r++) {
                        if (save_poi[i]->invalidate_cached_plan[r] == cache) {
                            goto found;
                        }
                    }
                }
                if (save_poi[i]->invalidate_cached_plan_count >=
                    BT_CACHE_INVALIDATE_ENTRIES_COUNT) {
                    for (r = 0;
                        r < save_poi[i]->invalidate_cached_plan_count; r++) {
#ifdef __DEBUG_BT_CACHE
                        V_ERR("Replacement eviction %p",
                            save_poi[i]->invalidate_cached_plan[r]);
#endif
                        v_perf_inc(V_PERF_EVICT, 1);
                        save_poi[i]->invalidate_cached_plan[r]->done = 0;
                    }
                    save_poi[i]->invalidate_cached_plan_count = 0;
                }
                save_poi[i]->invalidate_cached_plan[save_poi
                    [i]->invalidate_cached_plan_count++] = cache;
              found:
                for (j = 0; j < payload->total; j++) {
                    unsigned long possible_ip = payload->targets[j];
                    unsigned long phys = g_v2p(world, possible_ip, 1);
                    struct v_page *mpage = h_p2mp(world, phys);
                    struct v_poi *possible_poi;
#ifdef __DEBUG_BT_CACHE
                    V_ERR("pb target %x", possible_ip);
#endif
                    if (mpage == NULL)
                        continue;
#ifdef __DEBUG_BT_CACHE
                    V_ERR("mpage %x", mpage);
#endif
                    possible_poi = v_find_poi(mpage, possible_ip);
                    if (possible_poi == NULL)
                        continue;
#ifdef __DEBUG_BT_CACHE
                    V_ERR("poi %x", possible_poi->type);
#endif
                    V_VERBOSE("Found poi %lx for %lx", possible_poi->addr,
                        save_poi[i]->addr);
                    _v_bt_cache(world, possible_poi, 0, cache, &cache_count);
                }
            }
        }
        h_bt_reset(world);
        for (i = 0; i < total; i++) {
            v_set_bp(world, save_poi[i], i);
        }
        world->current_valid_bps = total;
        h_bt_cache(world, cache->plan, cache_count);
#ifdef BT_CACHE_LEVEL3
        h_bt_exec_cache(world, cache);
#endif
#ifdef __DEBUG_BT_CACHE
        if (lastcache == cache)
            V_ERR("%x", cache_count);
#endif
        if (cache_count > 0) {
            cache->done = 1;
            cache->length = cache_count;
        }
#ifdef __DEBUG_BT_CACHE
        lastcache = cache;
#endif
    }
    h_perf_tsc_end(H_PERF_TSC_CACHE, 2);
}

#endif
