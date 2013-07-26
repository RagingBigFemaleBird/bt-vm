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
#include "vm/include/world.h"
#include "guest/include/mm.h"
#include "guest/include/cpu.h"
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"

/**
 * @in attr Page type attribute
 * @return Page permission attribute
 */
static unsigned int
v_pagetype2permission(unsigned int attr)
{
    switch (attr & V_PAGE_TYPE_MASK) {
    case V_PAGE_ASSIST:
    case V_PAGE_BT:
    case V_PAGE_EXD:
    case V_PAGE_PAGETABLE:
        return 0;
    case V_PAGE_NOFAIL:
    case V_PAGE_STEP:
        return H_EXD_CAPABLE ? V_PAGE_EX : 0;
    }
    return 0;
}

/**
 * @in mpage machine page
 * @return virtual address
 *
 * allocate physical address in host space, if not present already
 */
void *
v_page_make_present(struct v_page *mpage)
{
    void *virt;
    if (mpage->has_virt) {
        return mpage->virt;
    }
    if (mpage->attr & V_PAGE_NOTPRESENT) {
        struct v_chunk *v = h_palloc(0);
        if (v == NULL) {
            V_ERR("page_make_present: Unrecoverable out of mem");
            return 0;
        }
        virt = h_alloc_va(v->phys);
        h_clear_page(virt);
        mpage->mfn = (v->phys >> H_PAGE_SHIFT);
        if ((mpage->attr & V_PAGE_TYPE_MASK) == 0)
            mpage->attr = V_PAGE_EXD;
        else
            mpage->attr &= (~V_PAGE_NOTPRESENT);
        V_LOG("virt = %p, phys = %llx, mpage = %p (%x) (%x)", virt,
            (unsigned long long) (v->phys), mpage, mpage->attr,
            (unsigned int) mpage->mfn);
        return virt;
    }
    virt = h_alloc_va(mpage->mfn << H_PAGE_SHIFT);
    mpage->has_virt = 1;
    mpage->virt = virt;
    return virt;
}

void
v_validate_guest_virt(struct v_world *world, g_addr_t addr)
{
    if (h_get_map(world->htrbase, addr) == 0) {
        v_pagefault(world, addr, V_MM_FAULT_NP);
    }
}

/**
 * @in delay TODO: delays in calling handler. 0 is instant
 *
 * hook phys with io handler
 */
void
v_page_set_io(struct v_world *world, g_addr_t phys,
    int (*handler) (struct v_world *, g_addr_t), unsigned int delay)
{
    struct v_page *mpage = h_p2mp(world, phys);
    if (mpage == NULL) {
        V_ERR("page_set_io: unknown phys address %lx", (unsigned long) phys);
        return;
    }
    if (mpage->io_page_info == NULL) {
        mpage->io_page_info = h_valloc(sizeof(struct v_io_page_info));
    }
    if (mpage->io_page_info == NULL) {
        V_ERR("page_set_io: unrecoverable out of mem");
        return;
    }
    mpage->io_page_info->handler = handler;
    mpage->io_page_info->delay = delay;
    mpage->attr &= (~V_PAGE_TYPE_MASK);
    mpage->attr |= V_PAGE_IO;
}

void
v_page_unset_io(struct v_world *world, g_addr_t phys)
{
    struct v_page *mpage = h_p2mp(world, phys);
    mpage->attr &= (~V_PAGE_TYPE_MASK);
    mpage->attr |= V_PAGE_EXD;
}

/**
 * @in address virtual address
 *
 * shadow page table: register mapping: address -> page
 */
void
v_page_map(struct v_world *world, struct v_page *page, h_addr_t address)
{
    struct v_spt_info *spt = v_spt_get_by_spt(world, world->htrbase);
    address = address & H_PFN_MASK;
    if (spt != NULL) {
        struct v_ptp_info *new_item;
        struct v_ptp_info **p = &(page->ptp_list);
        int exist = 0;
        while (*p != NULL && (!exist)) {
            if ((*p)->vaddr == address && (*p)->spt == spt
                && (*p)->gpt_level == 0)
                exist = 1;
            p = &((*p)->next);
        }
        if (!exist) {
            V_VERBOSE("registered addr %lx -> %x", (unsigned long) address,
                (unsigned int) page->mfn);
            new_item = h_valloc(sizeof(struct v_ptp_info));
            p = &(page->ptp_list);
            new_item->vaddr = address;
            new_item->spt = spt;
            new_item->gpt_level = 0;
            new_item->next = page->ptp_list;
            page->ptp_list = new_item;
        }
    }
}

/**
 * @in address virtual address
 * @in reason combination of _NP, _W, _EX
 *
 * handles page fault in guest
 */
int
v_pagefault(struct v_world *world, g_addr_t address, int reason)
{
    V_EVENT("Page fault at %lx due to %s %s %s\n", (unsigned long) address,
        (reason & V_MM_FAULT_NP) ? "page not present" : "",
        (reason & V_MM_FAULT_W) ? "write protect" : "",
        (reason & V_MM_FAULT_EX) ? "exec" : "");
    if (reason & V_MM_FAULT_NP) {
        g_addr_t pa;
        unsigned int g_attr;
        struct v_page *mpage;
        pa = g_v2p(world, address, 1);
        g_attr = g_v2attr(world, address);
        // guest fault here
        mpage = h_p2mp(world, pa);
        if (mpage == NULL) {
            V_LOG("guest fault, mapping does not exist");
            return reason;
        }
        // TODO: handle user mode access violation
        if (mpage->attr & V_PAGE_NOTPRESENT) {
            V_LOG("allocating new page\n");
            v_page_make_present(mpage);
            h_free_va_mpage(mpage);
        }
        mpage->attr = mpage->attr & (~V_PAGE_ACCESS_MASK);
        mpage->attr |= (g_attr & V_PAGE_PRIV_MASK);
        mpage->attr |= v_pagetype2permission(mpage->attr);
        /* if we are at basic pages, W permission is always welcome */
        if ((mpage->attr & V_PAGE_TYPE_MASK) == V_PAGE_EXD)
            mpage->attr |= (g_attr & V_PAGE_W);
        if ((mpage->attr & V_PAGE_TYPE_MASK) == V_PAGE_IO) {
            if (mpage->io_page_info->delay == 0) {
                int ret;
                mpage->attr |= V_PAGE_NOMAP;
                ret = (*(mpage->io_page_info->handler)) (world, address);
                if (ret)
                    return V_MM_FAULT_HANDLED;
            } else {
                mpage->attr |= (g_attr & V_PAGE_W);
            }
            V_LOG("IO Fault");
        }
        V_LOG("Access rights: %x, mfn %x\n", mpage->attr,
            (unsigned int) mpage->mfn);
        v_page_map(world, mpage, address);      //registers mapping: address to mpage
        h_set_map(world->htrbase, address, mpage->mfn << H_PAGE_SHIFT, 1,
            mpage->attr);
        g_pagetable_map(world, address);

    } else if (reason & V_MM_FAULT_W) {
        g_addr_t pa;
        unsigned int g_attr;
        struct v_page *mpage;
        if (h_check_bridge_pages(world, address)) {
            h_fault_bridge_pages(world, address);
            return V_MM_FAULT_HANDLED;
        }
        pa = g_v2p(world, address, 1);
        g_attr = g_v2attr(world, address);
        // guest fault here
        mpage = h_p2mp(world, pa);
        if (mpage == NULL) {
            V_LOG("guest fault, mapping does not exist");
            return reason;
        }
        mpage->attr = mpage->attr & (~V_PAGE_ACCESS_MASK);
        mpage->attr |= (g_attr & V_PAGE_PRIV_MASK);
        mpage->attr |= v_pagetype2permission(mpage->attr);
        // TODO: handle user mode access violation
        if (mpage->attr & V_PAGE_NOTPRESENT) {
            V_ERR("bug\n");
            world->status = VM_PAUSED;
        } else {
            V_LOG("permission %x vs. %x", mpage->attr, g_attr);
            if ((g_attr & V_PAGE_W)
                && (mpage->attr & V_PAGE_TYPE_MASK) != V_PAGE_EXD) {
                V_LOG("restoring wp pages");
                if ((mpage->attr & V_PAGE_TYPE_MASK) == V_PAGE_STEP)
                    v_translation_purge(mpage);
                if ((mpage->attr & V_PAGE_TYPE_MASK) == V_PAGE_PAGETABLE) {
                    v_spt_inv_page(world, mpage);
                    h_new_trbase(world);
                    V_LOG("spt %lx (%x) changed", (unsigned long) address,
                        (unsigned int) mpage->mfn);
                }
                mpage->attr &= (~V_PAGE_TYPE_MASK);
                mpage->attr |= V_PAGE_EXD;
                mpage->attr |= V_PAGE_W;
                h_set_map(world->htrbase, address,
                    mpage->mfn << H_PAGE_SHIFT, 1, mpage->attr);
            } else {
                V_LOG("Guest W fault\n");
                return reason;
            }
        }
    } else {
        if (h_check_bridge_pages(world, address)) {
            h_fault_bridge_pages(world, address);
            return V_MM_FAULT_HANDLED;
        }
        return reason;
    }
    return V_MM_FAULT_HANDLED;
}

/**
 * @in spt shadow page table
 * @in cpt cached page table
 *
 * add a new (spt, cpt) tuple
 */
void
v_spt_add(struct v_world *w, h_addr_t spt, g_addr_t gpt)
{
    struct v_spt_info *new_item = h_valloc(sizeof(struct v_spt_info));
    int i;
    for (i = 0; i < V_MM_MAX_POOL; i++) {
        new_item->mem_pool_mapped[i] = 0;
    }
    new_item->spt_paddr = spt;
    new_item->gpt_paddr = gpt;
    new_item->next = w->spt_list;
    new_item->inv_list = NULL;
    w->spt_list = new_item;
}

struct v_spt_info *
v_spt_get_by_spt(struct v_world *w, h_addr_t spt)
{
    struct v_spt_info *p = w->spt_list;
    while (p != NULL) {
        if (p->spt_paddr == spt)
            return p;
        p = p->next;
    }
    return NULL;
}

struct v_spt_info *
v_spt_get_by_gpt(struct v_world *w, g_addr_t gpt)
{
    struct v_spt_info *p = w->spt_list;
    while (p != NULL) {
        if (p->gpt_paddr == gpt)
            return p;
        p = p->next;
    }
    return NULL;
}

/**
 * @in mpage machine page
 *
 * invalidates all registered mappings with mpage
 * this implies two things:
 * 1. any registered va->mpage mapping in any spt is no longer good
 * 2. any spt where mpage was part of a page table is invalid
 */
void
v_spt_inv_page(struct v_world *world, struct v_page *mpage)
{
    struct v_spt_info *spt = world->spt_list;
    V_VERBOSE("attempt to invalidate mpage %x", (unsigned int) mpage->mfn);
    while (spt != NULL) {
        struct v_ptp_info *check = mpage->ptp_list;
        int exist = 0;
        /* check: Do we have any spt associated with mpage? */
        while (check != NULL && (!exist)) {
            if (check->spt == spt)
                exist = 1;
            check = check->next;
        }
        if (exist) {
            struct v_inv_entry *check1 = spt->inv_list;
            int exist1 = 0;
            /* check: Do we already have it on invalidation list? */
            while (check1 != NULL && (!exist1)) {
                if (check1->page == mpage)
                    exist1 = 1;
                check1 = check1->next;
            }
            if (!exist1) {
                struct v_inv_entry *new_item =
                    h_valloc(sizeof(struct v_inv_entry));
                V_VERBOSE("registered at spt %lx", spt->spt_paddr);
                new_item->type = 0;
                new_item->page = mpage;
                new_item->next = spt->inv_list;
                spt->inv_list = new_item;
            }
        }
        spt = spt->next;
    }
}

void
v_mem_pool_create(struct v_world *world, unsigned int unit_size,
    unsigned int order)
{
    struct v_chunk *chunk = h_palloc(order);
    void *virt = h_alloc_va(chunk->phys);
    unsigned int size = (H_PAGE_SIZE) * (1 << order);
    unsigned int count = size / unit_size;
    unsigned int bitmap_size = V_MM_POOL_BITMAP_SIZE(count);
    V_VERBOSE("Creating pool of size %x count %x", size, count);
    h_memset(virt, 0, bitmap_size);
    world->host_pools[world->pool_count].virt = (h_addr_t) (virt);
    world->host_pools[world->pool_count].mon_virt = 0;
    world->host_pools[world->pool_count].phys = chunk->phys;
    world->host_pools[world->pool_count].unit_size = unit_size;
    world->host_pools[world->pool_count].total_size = size;
    world->host_pools[world->pool_count].max_count =
        (size - bitmap_size) / unit_size;
    world->host_pools[world->pool_count].alloc_hint = 0;
    world->pool_count++;
    h_monitor_setup_data_pages(world, world->htrbase);
}

void *
v_mem_pool_alloc(struct v_world *world, unsigned int unit_size)
{
    int i;
  again:
    for (i = 0; i < world->pool_count; i++) {
        if (world->host_pools[i].unit_size == unit_size) {
            unsigned int start = world->host_pools[i].alloc_hint / 32;
            unsigned int k = start;
            do {
                unsigned int *p =
                    (unsigned int *) (world->host_pools[i].virt) + k;
                if (*p != 0xffffffff) {
                    unsigned int r = *p;
                    unsigned int b = 0;
                    while (r & 1) {
                        b++;
                        r >>= 1;
                    }
                    b = b + k * 32;
                    if (b < world->host_pools[i].max_count) {
                        *p = *p | (1 << b);
                        V_VERBOSE("Pool alloc %x(of %x)", b, i);
                        world->host_pools[i].alloc_hint = b;
                        return (void *) (world->host_pools[i].virt +
                            V_MM_POOL_BITMAP_SIZE(world->
                                host_pools[i].total_size /
                                world->host_pools[i].unit_size) +
                            world->host_pools[i].unit_size * b);
                    }
                }
                k++;
                if (k > world->host_pools[i].max_count / 32) {
                    k = 0;
                }
            } while (k != start);
        }
    }
    if (world->pool_count >= V_MM_MAX_POOL)
        return NULL;
    V_VERBOSE("New pool");
    v_mem_pool_create(world, unit_size, H_MEM_POOL_DEFAULT_ORDER);
    goto again;
}
