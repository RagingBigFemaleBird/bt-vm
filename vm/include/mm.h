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

#ifndef V_MM_H
#define V_MM_H

#include "guest/include/mm.h"
#include "host/include/mm.h"
#include "vm/include/bt.h"

#define V_CONFIG_BT	0       /* binary translation on: the guest may recoginize itself running in simulation if this is set to 1 */
#define V_PAGE_ACCESS_MASK 0xe1f

#define V_PAGE_WBD	(1<<9)
#define V_PAGE_CD	(1<<10)
#define V_PAGE_NOMAP	(1<<11)
#define V_PAGE_EX	4
#define V_PAGE_W	8       /* page access attributes are for setmap's convenience only. may change if another mapping happens */

#define V_PAGE_VM	0
#define V_PAGE_SYS	1
#define V_PAGE_USR	2
#define V_PAGE_PRIV_MASK	3
#define V_PAGE_NOTPRESENT	(1<<4)

#define V_PAGE_TYPE_MASK (7<<5)

#define V_PAGE_EXD	(0<<5)  /* user data page, user code page,  or kernel data page */
#define V_PAGE_PAGETABLE (1<<5) /* guest page table, must be ro */
#define V_PAGE_ASSIST	(2<<5)  /* kernel code page that requires debug breakpoint support (does not exist if H_EXD_CAPABLE=0) */
#define V_PAGE_STEP 	(3<<5)  /* kernel code page that requires debug stepping support */
#define V_PAGE_BT	(4<<5)  /* kernel code page that has been binary translated (does not exist if V_CONFIG_BT=0) */
#define V_PAGE_NOFAIL	(5<<5)  /* pages that cannot silently fail */
#define V_PAGE_IO	(6<<5)  /* IO pages */

struct v_io_page_info {
    unsigned int delay;
    int (*handler) (struct v_world *, g_addr_t);
};

struct v_page {
    h_addr_t mfn;
    unsigned int attr;
    struct v_poi *poi_list;
    struct v_ipoi *ipoi_list;
    struct v_fc *fc_list;
    struct v_io_page_info *io_page_info;
    struct v_ptp_info *ptp_list;
    unsigned int has_virt;
    void *virt;
};

struct h_chunk;

struct v_chunk {
    struct h_chunk h;
    h_addr_t virt;              /* 0 if unmapped */
    h_addr_t phys;
    unsigned int order;
};

/* translation cache */

struct v_page_cache {
    g_addr_t vaddr;
    g_addr_t paddr;
    h_addr_t maddr;
    unsigned int type;
};

struct v_inv_entry {
    unsigned int type;
    struct v_page *page;
    struct v_inv_entry *next;
};

struct v_spt_info {
    h_addr_t spt_paddr;
    g_addr_t gpt_paddr;
    struct v_inv_entry *inv_list;
    struct v_spt_info *next;
};

struct v_ptp_info {
    struct v_spt_info *spt;
    g_addr_t vaddr;
    unsigned int gpt_level;     //0 for now
    struct v_ptp_info *next;
};

#define V_MM_FAULT_HANDLED 0
#define V_MM_FAULT_NP	1
#define V_MM_FAULT_W	2
#define V_MM_FAULT_EX	4

int v_pagefault(struct v_world *, g_addr_t, int);
void *v_page_make_present(struct v_page *);
void v_page_set_io(struct v_world *, g_addr_t,
    int (*handler) (struct v_world *, g_addr_t), unsigned int);
void v_page_unset_io(struct v_world *, g_addr_t);
void v_spt_add(struct v_world *, h_addr_t, g_addr_t);
struct v_spt_info *v_spt_get_by_spt(struct v_world *, h_addr_t);
struct v_spt_info *v_spt_get_by_gpt(struct v_world *, g_addr_t);
void v_spt_inv_page(struct v_world *, struct v_page *);
void v_validate_guest_virt(struct v_world *, g_addr_t);
#endif
