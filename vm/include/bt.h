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
#ifndef V_BT_H
#define V_BT_H

#define V_INST_I	(1 << 1)
#define V_INST_UB	(1 << 2)
#define V_INST_CB	(1 << 3)
#define V_INST_PB	(1 << 4)
#define V_INST_F	(1 << 5)
#define V_INST_U	(1 << 6)
#define V_INST_FC       (1 << 7)
#define V_INST_FR	(1 << 8)
#define V_INST_RAM      (1 << 9)

#define SE_THRESHOLD	1

struct v_poi_tree;

struct v_poi_tree_plan {
    unsigned char valid;
    unsigned char count;
    struct v_poi *poi[8];       /*todo: this constant */
};
#define BT_CACHE
#define BT_CACHE_LEVEL 16
#define BT_CACHE_CAPACITY 16

struct v_poi_cached_tree_plan {
    unsigned long addr;
    struct v_poi *poi;
    struct v_poi_tree_plan *plan;
};

struct v_poi {
    unsigned int type;
    unsigned int ex_mode;
    unsigned long addr;
    int invalid;
    unsigned int ref_count;
    struct v_poi_tree *tree;
    struct v_poi_tree_plan plan;
    int tree_count;
    int expect;
    struct v_poi *next_poi;
    struct v_poi *next_inst;
    struct v_poi *next_inst_taken;
};

struct v_ipoi {
    unsigned long addr;
    unsigned int key;
    int expect;
    struct v_poi *poi;
    int invalid;
    struct v_ipoi *next;
};

struct v_poi_tree {
    struct v_poi *poi;
    int inext;
    union {
        int inext_taken;
        int inext_ret;
    } u;
    unsigned char flag[3];
};

struct v_world;
struct v_page;
void v_bt(struct v_world *);
void v_do_bp(struct v_world *, unsigned long, unsigned int);
void v_translation_purge(struct v_page *);
struct v_poi *v_find_poi(struct v_page *, unsigned int);
struct v_poi *v_add_poi(struct v_page *, unsigned int, unsigned int,
    unsigned int);
void v_add_ipoi(struct v_world *, unsigned intr, unsigned int, struct v_poi *);
struct v_fc *v_find_fc(struct v_page *, unsigned int);
struct v_fc *v_add_fc(struct v_page *, unsigned int, unsigned int);
void v_bt_reset(struct v_world *);
#ifdef BT_CACHE
void v_bt_cache(struct v_world *world);
#endif
#endif
