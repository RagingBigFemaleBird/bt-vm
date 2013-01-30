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
        struct v_chunk *v = h_raw_palloc(0);
        if (v == NULL) {
            V_ERR("page_make_present: Unrecoverable out of mem");
            return 0;
        }
        virt = h_allocv(v->phys);
        h_clear_page((unsigned int) virt);
        mpage->mfn = (v->phys >> H_PAGE_SHIFT);
        if ((mpage->attr & V_PAGE_TYPE_MASK) == 0)
            mpage->attr = V_PAGE_EXD;
        else
            mpage->attr &= (~V_PAGE_NOTPRESENT);
        V_LOG("virt = %p, phys = %lx, mpage = %p (%x) (%x)", virt,
            v->phys, mpage, mpage->attr, (unsigned int) mpage->mfn);
        return virt;
    }
    virt = h_allocv(mpage->mfn << H_PAGE_SHIFT);
    mpage->has_virt = 1;
    mpage->virt = virt;
    return virt;
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
        mpage->io_page_info = h_raw_malloc(sizeof(struct v_io_page_info));
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
        struct v_ptp_info *new;
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
            new = h_raw_malloc(sizeof(struct v_ptp_info));
            p = &(page->ptp_list);
            new->vaddr = address;
            new->spt = spt;
            new->next = NULL;
            new->gpt_level = 0;
            while (*p != NULL) {
                p = &((*p)->next);
            }
            (*p) = new;
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
            h_deallocv(mpage->mfn << H_PAGE_SHIFT);
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
    struct v_spt_info *new = h_raw_malloc(sizeof(struct v_spt_info));
    new->spt_paddr = spt;
    new->gpt_paddr = gpt;
    new->next = w->spt_list;
    new->inv_list = NULL;
    w->spt_list = new;
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
                struct v_inv_entry **p = &(spt->inv_list);
                struct v_inv_entry *new =
                    h_raw_malloc(sizeof(struct v_inv_entry));
                V_VERBOSE("registered at spt %lx", spt->spt_paddr);
                new->type = 0;
                new->page = mpage;
                new->next = NULL;
                while (*p != NULL) {
                    p = &((*p)->next);
                }
                (*p) = new;
            }
        }
        spt = spt->next;
    }
}
