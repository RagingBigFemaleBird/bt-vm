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
#include "vm/include/world.h"
#include "vm/include/logging.h"

#define g_pt1_off(va) (((va) >> 22) << 2)
#define g_pt2_off(va) ((((va) << 10) >> 22) << 2)
#define G_PAGE_US	4
#define G_PAGE_RW	2
#define G_PAGE_P	1
#define G_PAGE_PS	0x80
#define G_PAGE_PCD	(1<<4)
#define G_PAGE_PWT	(1<<3)

unsigned int
g_v2attr(struct v_world *world, g_addr_t virt)
{
    int g_mode = world->gregs.mode;
    void *x;
    void *l1;
    void *l2;
    struct v_page *mpage;
    unsigned int ret;
    unsigned int v_attr;
    if (g_mode == G_MODE_REAL)
        return V_PAGE_USR | V_PAGE_W;
    if (g_mode == G_MODE_PE)
        return V_PAGE_USR | V_PAGE_W;
    mpage = h_p2mp(world, world->gregs.cr3);
    if (mpage == NULL) {
        return V_PAGE_NOMAP;
    }
    x = v_page_make_present(mpage);
    l1 = g_pt1_off((unsigned int) virt) + x;
    if ((*(unsigned int *) l1) & G_PAGE_PS) {
        unsigned int ret = (*(unsigned int *) l1) & 0x3fffff;
        unsigned int v_attr =
            ((ret & G_PAGE_US) ? V_PAGE_USR : V_PAGE_SYS) |
            ((ret & G_PAGE_RW) ? V_PAGE_W : 0) | ((ret & G_PAGE_PCD) ?
            V_PAGE_CD : 0) | ((ret & G_PAGE_PWT)
            ? V_PAGE_WBD : 0);
        if (!((*(unsigned int *) l1) & G_PAGE_P)) {
            h_free_va(mpage->mfn << H_PAGE_SHIFT);
            return V_PAGE_NOMAP;
        }
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return v_attr;
    }
    if (!((*(unsigned int *) l1) & G_PAGE_P))
        return V_PAGE_NOMAP;
    mpage = h_p2mp(world, *(unsigned int *) l1);
    l2 = v_page_make_present(mpage);
    l2 = l2 + g_pt2_off((unsigned int) virt);
    ret = (*(unsigned int *) l2) & 0xfff;
    v_attr =
        ((ret & G_PAGE_US) ? V_PAGE_USR : V_PAGE_SYS) | ((ret & G_PAGE_RW) ?
        V_PAGE_W : 0) | ((ret & G_PAGE_PCD)
        ? V_PAGE_CD : 0) | ((ret & G_PAGE_PWT) ? V_PAGE_WBD : 0);
    if (!((*(unsigned int *) l2) & G_PAGE_P)) {
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return V_PAGE_NOMAP;
    }
    h_free_va(mpage->mfn << H_PAGE_SHIFT);
    return v_attr;
}

g_addr_t
g_v2p(struct v_world * world, g_addr_t virt, unsigned int do_not_fault)
{
    int g_mode = world->gregs.mode;
    void *x;
    void *l1;
    void *l2;
    struct v_page *mpage;
    g_addr_t ret;
    if (g_mode == G_MODE_REAL || g_mode == G_MODE_PE)
        return virt;
    mpage = h_p2mp(world, world->gregs.cr3);
    if (mpage == NULL) {
        if (!do_not_fault) {
            world->status = VM_PAUSED;
            V_ERR
                ("Page fault: %lx out of phys range %lx, unimplemented",
                (unsigned long) virt, world->pa_top);
        }
        return 0xffffffff;
    }
    x = v_page_make_present(mpage);
    l1 = g_pt1_off((unsigned int) virt) + x;
    if ((*(unsigned int *) l1) & G_PAGE_PS) {
        unsigned int ret =
            ((*(unsigned int *) l1) & 0xffc00000) + (virt & 0x3fffff);
        if (!((*(unsigned int *) l1) & G_PAGE_P)) {
            if (!do_not_fault) {
                world->status = VM_PAUSED;
                V_ERR("Page fault lvl 1 %x, unimplemented",
                    *(unsigned int *) l1);
            }
            h_free_va(mpage->mfn << H_PAGE_SHIFT);
            return 0xffffffff;
        }
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return ret;
    }
    if (!((*(unsigned int *) l1) & G_PAGE_P)) {
        if (!do_not_fault) {
            world->status = VM_PAUSED;
            V_ERR("Page fault at lvl 1, %lx not exist, unimplemented",
                (unsigned long) virt);
        }
        return 0xffffffff;
    }
    mpage = h_p2mp(world, *(unsigned int *) l1);
    if (mpage == NULL) {
        if (!do_not_fault) {
            world->status = VM_PAUSED;
            V_ERR("Page fault: %lx no map, unimplemented",
                (unsigned long) virt);
        }
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return 0xffffffff;
    }
    l2 = v_page_make_present(mpage);
    l2 = l2 + g_pt2_off((unsigned int) virt);
    ret = ((*(unsigned int *) l2) & 0xfffff000) + (virt & 0xfff);
    if (!((*(unsigned int *) l2) & G_PAGE_P)) {
        if (!do_not_fault) {
            world->status = VM_PAUSED;
            V_ERR("Page fault, unimplemented at l2 %x", *(unsigned int *) l2);
        }
        h_free_va(mpage->mfn << H_PAGE_SHIFT);
        return 0xffffffff;
    }
    h_free_va(mpage->mfn << H_PAGE_SHIFT);
    return ret;
}

#define PT0_FAKE_VIRT 1

void
g_pagetable_map(struct v_world *world, g_addr_t virt)
{
    struct v_spt_info *spt = v_spt_get_by_spt(world, world->htrbase);
    if (spt != NULL) {
        int g_mode = world->gregs.mode;
        struct v_ptp_info *newp;
        struct v_ptp_info **p;
        void *x;
        void *l1;
        void *l2;
        struct v_page *mpage;
        unsigned int exist;
        if (g_mode == G_MODE_REAL || g_mode == G_MODE_PE)
            return;
        mpage = h_p2mp(world, world->gregs.cr3);
        if (mpage == NULL) {
            V_ERR("Bug in shadowmap: guest cr3 faulty");
            world->status = VM_PAUSED;
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
            V_LOG("Write protecting %x to %x", (unsigned int) mpage->mfn,
                mpage->attr);
        }

        p = &(mpage->ptp_list);
        exist = 0;
        while ((!exist) && (*p) != NULL) {
            if ((*p)->spt == spt && (*p)->gpt_level == 1
                && (*p)->vaddr == PT0_FAKE_VIRT)
                exist = 1;
            p = &((*p)->next);
        }
        if (!exist) {
            newp = h_valloc(sizeof(struct v_ptp_info));
            p = &(mpage->ptp_list);

            newp->vaddr = PT0_FAKE_VIRT;
            newp->spt = spt;
            newp->next = NULL;
            newp->gpt_level = 1;
            while (*p != NULL) {
                p = &((*p)->next);
            }
            (*p) = newp;
            V_LOG("marking mfn %x as pt1", (unsigned int) mpage->mfn);
        }

        x = v_page_make_present(mpage);
        l1 = g_pt1_off((unsigned int) virt) + x;
        if ((*(unsigned int *) l1) & G_PAGE_PS) {
            h_free_va(mpage->mfn << H_PAGE_SHIFT);
            return;
        }
        if (!((*(unsigned int *) l1) & G_PAGE_P))
            return;
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
            V_LOG("Write protecting %x to %x", (unsigned int) mpage->mfn,
                mpage->attr);
        }

        p = &(mpage->ptp_list);
        exist = 0;
        while ((!exist) && (*p) != NULL) {
            if ((*p)->spt == spt && (*p)->gpt_level == 1
                && (*p)->vaddr == (virt & H_PFN_MASK))
                exist = 1;
            p = &((*p)->next);
        }
        if (!exist) {
            newp = h_valloc(sizeof(struct v_ptp_info));
            p = &(mpage->ptp_list);

            newp->vaddr = (virt & H_PFN_MASK);
            newp->spt = spt;
            newp->next = NULL;
            newp->gpt_level = 1;
            while (*p != NULL) {
                p = &((*p)->next);
            }
            (*p) = newp;
            V_LOG("marking mfn %x as pt2", (unsigned int) mpage->mfn);
        }

    }
}
