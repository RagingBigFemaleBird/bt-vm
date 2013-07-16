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
#include "guest/include/mm.h"
#include "guest/include/cpu.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "vm/include/world.h"

#define g_pt1_off(va) (((va) >> 20) << 2)
#define g_pt2_off(va) ((((va) << 12) >> 24) << 2)
#define G_PAGE_SAP0 0x400
#define G_PAGE_SAP1 0x800
#define G_PAGE_AP0	0x10
#define G_PAGE_AP1	0x20
#define G_PAGE_P	1
#define G_PAGE_L1P	1
#define G_PAGE_L1S	2
#define G_PAGE_L2P	2
#define G_PAGE_PS	0x80
#define G_PAGE_PCD	4
#define G_PAGE_PWT	8

unsigned int
g_v2attr(struct v_world *world, unsigned long virt)
{
    int g_mode = world->gregs.mode;
    void *x;
    void *l1;
    void *l2;
    struct v_page *mpage;
    unsigned int ret;
    unsigned int v_attr;
    unsigned int l1page = world->gregs.p15_trbase & H_PFN_MASK;
    unsigned int subpage = 0;
    if (g_mode == G_MODE_NO_MMU)
        return V_PAGE_SYS | V_PAGE_W;
    V_VERBOSE("v2attr %lx", virt);
    l1page = l1page + g_pt1_off(virt);
    mpage = h_p2mp(world, l1page);
    if (mpage == NULL) {
        V_ERR("Nomap");
        return V_PAGE_NOMAP;
    }
    x = v_page_make_present(mpage);
    l1 = (g_pt1_off((unsigned int) virt) & H_POFF_MASK) + x;
    V_VERBOSE("l1 %x @ %p", *(unsigned int *) l1, l1);
    if ((*(unsigned int *) l1) & G_PAGE_L1S) {
        unsigned int ret = (*(unsigned int *) l1) & 0xfffff;
        unsigned int v_attr =
            ((ret & G_PAGE_SAP1) ? V_PAGE_USR : V_PAGE_SYS) |
            ((ret & G_PAGE_SAP0) ? V_PAGE_W : 0) | ((ret & G_PAGE_PCD) ?
            V_PAGE_CD : 0) | ((ret & G_PAGE_PWT)
            ? V_PAGE_WBD : 0);
        V_VERBOSE("l1a %x", ret);
        if ((*(unsigned int *) l1) & G_PAGE_L1P) {
            /* 11 case, fault */
            h_free_va(mpage->mfn << H_PAGE_SHIFT);
            return V_PAGE_NOMAP;
        }
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return v_attr;
    }
    if (!((*(unsigned int *) l1) & G_PAGE_P))
        return V_PAGE_NOMAP;
    subpage = (*(unsigned int *) l1) & 0xc00;
    mpage = h_p2mp(world, *(unsigned int *) l1);
    l2 = v_page_make_present(mpage);
    l2 = l2 + g_pt2_off((unsigned int) virt) + subpage;
    ret = (*(unsigned int *) l2) & 0xfff;
    V_VERBOSE("l2a %x", ret);
    // ugly hack here: Guest uses TEX attr but we don't want to deal with this now
    v_attr =
        ((ret & G_PAGE_AP1) ? V_PAGE_USR : V_PAGE_SYS) | ((ret & G_PAGE_AP0)
        ? V_PAGE_W : V_PAGE_W) | ((ret & G_PAGE_PCD)
        ? V_PAGE_CD : 0) | ((ret & G_PAGE_PWT) ? V_PAGE_WBD : 0);
    if (ret == 0x4f) {
        v_attr = V_PAGE_SYS | V_PAGE_W;
    }
    if (!((*(unsigned int *) l2) & G_PAGE_L2P)) {
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return V_PAGE_NOMAP;
    }
    h_free_va(mpage->mfn << H_PAGE_SHIFT);
    return v_attr;
}

unsigned long
g_v2p(struct v_world *world, unsigned long virt, unsigned int do_not_fault)
{
    int g_mode = world->gregs.mode;
    void *x;
    void *l1;
    void *l2;
    struct v_page *mpage;
    unsigned int ret;
    unsigned int l1page = world->gregs.p15_trbase & H_PFN_MASK;
    unsigned int subpage = 0;
    if (g_mode == G_MODE_NO_MMU)
        return virt;
    V_VERBOSE("v2p %lx", virt);
    l1page = l1page + g_pt1_off(virt);
    mpage = h_p2mp(world, l1page);
    if (mpage == NULL) {
        V_ERR
            ("Page fault: %lx out of phys range %lx, unimplemented",
            virt, world->pa_top);
        if (!do_not_fault) {
            world->status = VM_PAUSED;
        }
        return 0xfffff000;
    }
    x = v_page_make_present(mpage);
    l1 = (g_pt1_off((unsigned int) virt) & H_POFF_MASK) + x;
    V_VERBOSE("l1 %p", l1);
    if ((*(unsigned int *) l1) & G_PAGE_L1S) {
        unsigned int ret =
            ((*(unsigned int *) l1) & 0xfff00000) + (virt & 0xfffff);
        V_VERBOSE("l1a %x", ret);
        if ((*(unsigned int *) l1) & G_PAGE_L1P) {
            V_VERBOSE("Page fault lvl 1 %x, unimplemented",
                *(unsigned int *) l1);
            h_free_va(mpage->mfn << H_PAGE_SHIFT);
            return 0xfffff000;
        }
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return ret;
    }
    if (!((*(unsigned int *) l1) & G_PAGE_P)) {
        V_LOG("Page fault at lvl 1, p not exist %lx", virt);
        return 0xfffff000;
    }
    subpage = (*(unsigned int *) l1) & 0xc00;
    mpage = h_p2mp(world, *(unsigned int *) l1);
    if (mpage == NULL) {
        V_ERR("Page fault: %lx no map, unimplemented", virt);
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return 0xfffff000;
    }
    l2 = v_page_make_present(mpage);
    l2 = l2 + g_pt2_off((unsigned int) virt) + subpage;
    ret = ((*(unsigned int *) l2) & 0xfffff000) + (virt & 0xfff);
    V_VERBOSE("l2a %x", ret);
    if (!((*(unsigned int *) l2) & G_PAGE_L2P)) {
        V_VERBOSE("Page fault, NP at l2 %x", *(unsigned int *) l2);
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return 0xfffff000;
    }
    h_free_va(mpage->mfn << H_PAGE_SHIFT);
    return ret;
}

void
g_pagetable_map(struct v_world *world, unsigned long virt)
{
    struct v_spt_info *spt = v_spt_get_by_spt(world, world->htrbase);
    if (spt != NULL) {
        int g_mode = world->gregs.mode;
        struct v_ptp_info *new;
        struct v_ptp_info **p;
        void *x;
        void *l1;
        void *l2;
        struct v_page *mpage;
        unsigned int exist;
        unsigned int l1page = world->gregs.p15_trbase & H_PFN_MASK;
        if (g_mode == G_MODE_NO_MMU)
            return;
        l1page = l1page + g_pt1_off(virt);
        mpage = h_p2mp(world, l1page);
        if (mpage == NULL) {
            V_ERR("Bug in shadowmap: guest ptbase faulty");

            return;
        }
        if ((mpage->attr & V_PAGE_TYPE_MASK) != V_PAGE_PAGETABLE) {
            mpage->attr &= (~V_PAGE_TYPE_MASK);
            mpage->attr |= V_PAGE_PAGETABLE;
            if (mpage->attr & V_PAGE_W) {
                mpage->attr &= (~V_PAGE_W);
            }
            v_spt_inv_page(world, mpage);
            h_new_trbase(world);
            V_LOG("Write protecting %lx to %x", virt, mpage->attr);
        }

        p = &(mpage->ptp_list);
        exist = 0;
        while ((!exist) && (*p) != NULL) {
            if ((*p)->spt == spt)
                exist = 1;
            p = &((*p)->next);
        }
        if (!exist) {
            new = h_valloc(sizeof(struct v_ptp_info));
            p = &(mpage->ptp_list);

            new->vaddr = virt;
            new->spt = spt;
            new->next = NULL;
            new->gpt_level = 1;
            while (*p != NULL) {
                p = &((*p)->next);
            }
            (*p) = new;
            V_EVENT("marking mfn %x as pt1", mpage->mfn);
        }

        x = v_page_make_present(mpage);
        l1 = (g_pt1_off((unsigned int) virt) & H_POFF_MASK) + x;
        if ((*(unsigned int *) l1) & G_PAGE_L1S) {
            h_free_va(mpage->mfn << H_PAGE_SHIFT);
            return;
        }
        if (!((*(unsigned int *) l1) & G_PAGE_P)) {
            h_free_va(mpage->mfn << H_PAGE_SHIFT);
            return;
        }
        mpage = h_p2mp(world, *(unsigned int *) l1);
        l2 = v_page_make_present(mpage);

        if ((mpage->attr & V_PAGE_TYPE_MASK) != V_PAGE_PAGETABLE) {
            mpage->attr &= (~V_PAGE_TYPE_MASK);
            mpage->attr |= V_PAGE_PAGETABLE;
            if (mpage->attr & V_PAGE_W) {
                mpage->attr &= (~V_PAGE_W);
            }
            v_spt_inv_page(world, mpage);
            h_new_trbase(world);
            V_LOG("Write protecting %lx to %x", virt, mpage->attr);
        }

        p = &(mpage->ptp_list);
        exist = 0;
        while ((!exist) && (*p) != NULL) {
            if ((*p)->spt == spt)
                exist = 1;
            p = &((*p)->next);
        }
        if (!exist) {
            new = h_valloc(sizeof(struct v_ptp_info));
            p = &(mpage->ptp_list);

            new->vaddr = virt;
            new->spt = spt;
            new->next = NULL;
            new->gpt_level = 1;
            while (*p != NULL) {
                p = &((*p)->next);
            }
            (*p) = new;
            V_EVENT("marking mfn %x as pt2", mpage->mfn);
        }
        h_free_va(mpage->mfn << H_PAGE_SHIFT);

    }
}
