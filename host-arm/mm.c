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
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <asm/io.h>
#include <linux/slab.h>
#include <../arch/arm/include/asm/highmem.h>
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/logging.h"
#include "host/include/cpu.h"
#include "guest/include/mm.h"
#include <../arch/arm/include/asm/page.h>
#include <../arch/arm/include/asm/memory.h>
#include <asm/tlbflush.h>
#include <../arch/arm/include/asm/cacheflush.h>

void
h_memcpy(void *dst, void *src, int size)
{
    memcpy(dst, src, size);
}

void *
h_raw_malloc(unsigned long size)
{
    return kmalloc(size, GFP_ATOMIC);
}

void
h_raw_dealloc(void *addr)
{
    kfree(addr);
}

void
h_pin(unsigned long phys)
{
//    struct page *page = phys_to_page(phys);
//    get_page(page);
}

void
h_unpin(unsigned long phys)
{
//    struct page *page = phys_to_page(phys);
//    put_page(page);
}

struct v_chunk *
h_raw_palloc(unsigned int order)
{
    struct page *p;
    struct v_chunk *v;
    unsigned int phys;
    unsigned int pr;
    p = alloc_pages(GFP_ATOMIC, order);
    if (p == NULL)
        return NULL;
    v = h_raw_malloc(sizeof(struct v_chunk));
    v->h.p = p;
    v->phys = page_to_phys(p);
    v->order = order;
    for (pr = (1 << order), phys = v->phys; pr > 0; phys += 0x1000, pr--) {
        h_pin(phys);
    }
    return v;
}

void
h_raw_depalloc(struct v_chunk *v)
{
    unsigned int pr;
    unsigned int phys;
    for (pr = (1 << v->order), phys = v->phys; pr > 0; phys += 0x1000, pr--) {
        h_unpin(phys);
    }
    __free_pages(v->h.p, v->order);
}

void *
h_allocv(g_addr_t phys)
{
    V_VERBOSE("allocv for %x", phys);
    return kmap(phys_to_page(phys));
}

void
h_deallocv(g_addr_t phys)
{
    V_VERBOSE("deallocv for %x", phys);
    kunmap(phys_to_page(phys));
}

void
h_deallocv_virt(g_addr_t virt)
{
    V_VERBOSE("deallocv for %x", virt_to_phys(virt));
    kunmap(phys_to_page(virt_to_phys((void *) (virt))));
}

void
h_set_p2m(struct v_world *world, h_addr_t pa, unsigned long pages, h_addr_t ma)
{
    struct v_page *pg;
    while (pages > 0) {
        if (ma == H_PFN_NP) {
            pg = &world->page_list[h_p2page(pa - G_PA_BASE)];
//            if ((pa & H_PFN_MASK) >= 0x40304000 && (pa & H_PFN_MASK) <= 0x4030d000) pg = world->gregs.io_page[(pa & 0xf000) >> 12];
            pg->attr |= V_PAGE_NOTPRESENT;
            pa += H_PAGE_SIZE;
            pages--;
            V_LOG("page %lx(%x) set to mfn NP\n", h_p2page(pa - G_PA_BASE),
                pg->attr);
        } else {
            pg = &world->page_list[h_p2page(pa - G_PA_BASE)];
//            if ((pa & H_PFN_MASK) >= 0x40304000 && (pa & H_PFN_MASK) <= 0x4030d000) pg = world->gregs.io_page[(pa & 0xf000) >> 12];
            pg->mfn = (ma >> H_PAGE_SHIFT);
            pg->attr &= (~V_PAGE_NOTPRESENT);
            V_LOG("page %lx(%x) set to mfn %x\n", h_p2page(pa - G_PA_BASE),
                pg->attr, pg->mfn);
            pages--;
            pa += H_PAGE_SIZE;
            ma += H_PAGE_SIZE;
        }
    }
}

extern void debug_dump(struct v_world *world);

struct v_page *
h_p2mp(struct v_world *world, h_addr_t pa)
{
    struct v_page *pg;
    if ((pa & H_PFN_MASK) == G_SERIAL_BASE)
        return world->gregs.io_page[0x13];
    if ((pa & H_PFN_MASK) == G_SERIAL_BASE + 0x1000)
        return world->gregs.io_page[0x14];
    if ((pa & H_PFN_MASK) == G_SERIAL_BASE + 0x2000)
        return world->gregs.io_page[0x15];
    if ((pa & H_PFN_MASK) == G_SERIAL_BASE + 0x3000)
        return world->gregs.io_page[0x16];
    if ((pa & H_PFN_MASK) == G_SERIAL_PAGE)
        return world->gregs.io_page[0];
    if ((pa & H_PFN_MASK) == 0x10009000)
        return world->gregs.io_page[0];
    if ((pa & H_PFN_MASK) == 0x10000000)
        return world->gregs.io_page[1];
    if ((pa & H_PFN_MASK) == 0x10140000)
        return world->gregs.io_page[2]; //PIC
    if ((pa & H_PFN_MASK) == 0x10003000)
        return world->gregs.io_page[3]; //PIC
    if ((pa & H_PFN_MASK) == 0x101e0000)
        return world->gregs.io_page[4]; //PIC
    if ((pa & H_PFN_MASK) == 0x101e2000)
        return world->gregs.io_page[5]; //PIC
    if ((pa & H_PFN_MASK) == 0x101e3000)
        return world->gregs.io_page[6]; //PIC
    if ((pa & H_PFN_MASK) == 0x10130000)
        return world->gregs.io_page[7]; //net
    if ((pa & H_PFN_MASK) == 0x101f1000)
        return world->gregs.io_page[9]; //net
    if ((pa & H_PFN_MASK) == 0x101f2000)
        return world->gregs.io_page[0xa];       //net
    if ((pa & H_PFN_MASK) == 0x101f3000)
        return world->gregs.io_page[0xb];       //net
    if ((pa & H_PFN_MASK) == 0x10100000)
        return world->gregs.io_page[8]; //net
    if ((pa & H_PFN_MASK) == 0x10110000)
        return world->gregs.io_page[8]; //net
    if ((pa & H_PFN_MASK) == 0x10120000)
        return world->gregs.io_page[8]; //net
    if (pa >= 0x10000000 && pa < 0xfffff000)
        return world->gregs.io_page[8];
/*    if ((pa & H_PFN_MASK) == 0x48020000) return world->gregs.io_page[0];
    if ((pa & H_PFN_MASK) == 0x4a002000) return world->gregs.io_page[1];
    if ((pa & H_PFN_MASK) == 0x4a306000) return world->gregs.io_page[2];
    if ((pa & H_PFN_MASK) == 0x4a307000) return world->gregs.io_page[3];
    if ((pa & H_PFN_MASK) == 0x48243000) return world->gregs.io_page[0xe];
    if ((pa & H_PFN_MASK) == 0x4a008000) return world->gregs.io_page[0xf];
    if ((pa & H_PFN_MASK) == 0x4a009000) return world->gregs.io_page[0x10];
    if ((pa & H_PFN_MASK) == 0x4a004000) return world->gregs.io_page[0x11];
    if ((pa & H_PFN_MASK) == 0x4a30a000) return world->gregs.io_page[0x12];
    if ((pa & H_PFN_MASK) == 0x48281000) return world->gregs.io_page[0x17];
    if ((pa & H_PFN_MASK) == 0x48241000) return world->gregs.io_page[0x18];
    if ((pa & H_PFN_MASK) == 0x48240000) return world->gregs.io_page[0x19];
    if ((pa & H_PFN_MASK) == 0x0) return world->gregs.io_page[0x0];
    if ((pa & H_PFN_MASK) >= 0x40304000 && (pa & H_PFN_MASK) <= 0x4030d000) return world->gregs.io_page[(pa & 0xf000) >> 12];
*/
    if (pa < G_PA_BASE || pa > world->pa_top) {
        return NULL;
    }
    pg = &world->page_list[h_p2page(pa - G_PA_BASE)];
    return pg;
}

#define h_pt1_off(va) (((va) >> 20) << 2)
#define h_pt2_off(va) ((((va) << 12) >> 24) << 2)
#define H_PAGE_AP0	0x10
#define H_PAGE_AP1	0x20
#define H_PAGE_P	1
#define H_PAGE_L1P	(1 | 0x10)
#define H_PAGE_L1S	2
#define H_PAGE_L2P	2
#define H_PAGE_PS	0x80
#define H_PAGE_PCD	4
#define H_PAGE_PWT	(8 | 0x440)
#define H_PAGE_L1UWR (3 << 10)

extern struct h_cpu hostcpu;

unsigned int
h_v2p(unsigned int virt)
{
    void *l1 =
        h_allocv(h_pt1_off((unsigned int) virt) +
        (hostcpu.p15_trbase & H_PFN_MASK)) +
        (h_pt1_off((unsigned int) virt) & H_POFF_MASK);
    void *ls = l1;
    void *l2;
    unsigned int ret;
    V_LOG("h_v2p: %x l1@%x: %x", virt, (unsigned int) l1, *(unsigned int *) l1);
    if ((*(unsigned int *) l1) & H_PAGE_L1S) {
        unsigned int ret =
            (*(unsigned int *) l1 & 0xfff00000) + (virt & 0xfffff);
        if ((*(unsigned int *) l1) & H_PAGE_L1P) {
            h_deallocv_virt((unsigned int) ls);
            return 0;
        }
        h_deallocv_virt((unsigned int) ls);
        return ret;
    }
    if (!((*(unsigned int *) l1) & H_PAGE_L1P)) {
        h_deallocv_virt((unsigned int) ls);
        return 0;
    }
    l2 = h_allocv(*(unsigned int *) l1) + ((*(unsigned int *) l1) & 0xc00);
    l2 = l2 + h_pt2_off((unsigned int) virt);
    ret = (*(unsigned int *) l2 & H_PFN_MASK) + (virt & H_POFF_MASK);
    if (!((*(unsigned int *) l2) & H_PAGE_L2P)) {
        h_deallocv(*(unsigned int *) l1 + ((*(unsigned int *) l1) & 0xc00));
        return 0;
    }
    h_deallocv(*(unsigned int *) l1 + ((*(unsigned int *) l1) & 0xc00));
    V_LOG("l2@%x: %x", (unsigned int) l2, *(unsigned int *) l2);
    return ret;
}

void
h_clear_page(unsigned long va)
{
    va = va & H_PFN_MASK;
    memset((void *) va, 0, H_PAGE_SIZE);
    clean_dcache_area((void *) (va), H_PAGE_SIZE);
}

void
h_clear_pagetable(unsigned long va)
{
    va = va & H_PFN_MASK;
    memset((void *) va, 0, H_PT_SIZE);
    clean_dcache_area((void *) (va), H_PT_SIZE);
}

static unsigned int
h_pt1_format(unsigned long pa, int attr)
{
    unsigned int entry = (pa & 0xfffffc00);
    entry = entry | (hostcpu.domain << 5);
    entry = entry | H_PAGE_L1P;
    return entry;
}

static unsigned int
h_pt2_format(unsigned long pa, int attr)
{
    unsigned int entry = (pa & H_PFN_MASK);
    unsigned int priv = attr & V_PAGE_PRIV_MASK;
    if (priv == V_PAGE_USR || priv == V_PAGE_SYS) {
        entry = entry | H_PAGE_AP1;
        if (attr & V_PAGE_W)
            entry = entry | H_PAGE_AP0;
    } else
        entry = entry | H_PAGE_AP0;
    if (!(attr & V_PAGE_WBD))
        entry = entry | H_PAGE_PWT;
    if (!(attr & V_PAGE_CD))
        entry = entry | H_PAGE_PCD;
    if (!(attr & V_PAGE_NOMAP))
        entry = entry | H_PAGE_L2P;
    return entry;
}

inline void
h_dcache_clean(unsigned int addr)
{
    clean_dcache_area((void *) (addr), 4);
}

void
h_set_map(unsigned long trbase, unsigned long va, unsigned long pa,
    unsigned long pages, int attr)
{
    va = va & H_PFN_MASK;
    pa = pa & H_PFN_MASK;
    while (pages > 0) {
        unsigned int *l1 =
            h_allocv(trbase + h_pt1_off(va)) +
            (h_pt1_off((unsigned int) va) & H_POFF_MASK);
        unsigned int *ls = l1;
        unsigned int subpage = 0;       //(va >> 20) & 0x3;
        if ((*(l1 - subpage) & H_PAGE_L1P) == 0) {
            struct v_chunk *c = h_raw_palloc(0);
            void *l2v = h_allocv(c->phys);
            h_clear_page((unsigned int) l2v);
            (*(l1 - subpage)) = h_pt1_format(c->phys, attr);
            h_dcache_clean((unsigned int) (l1 - subpage));
            h_deallocv_virt((unsigned int) l2v);
        }
        if (((*l1) & H_PAGE_L1P) == 0) {
            unsigned int phys_big = (*(l1 - subpage)) & H_PFN_MASK;
            void *l2v = h_allocv(phys_big) + (subpage << 10);
            unsigned int *l2 = l2v + h_pt2_off(va);
            h_clear_pagetable((unsigned int) l2v);
            (*l1) = h_pt1_format(phys_big + (subpage << 10), attr);
            (*l2) = h_pt2_format(pa, attr);
            V_LOG("mapping %lx to %lx, %p|l1 = %x, %p|l2= %x\n", va,
                pa, l1, *l1, l2, *l2);
            h_dcache_clean((unsigned int) (l1));
            h_dcache_clean((unsigned int) (l2));
            h_deallocv(phys_big);
        } else {
            unsigned int phys_big = (*(l1 - subpage)) & H_PFN_MASK;
            void *l2v = h_allocv(phys_big) + (subpage << 10);
            unsigned int *l2 = l2v + h_pt2_off(va);
            (*l2) = h_pt2_format(pa, attr);
            h_dcache_clean((unsigned int) (l2));
            V_LOG("mapping %lx to %lx, %px|l1 = %x, %px|l2= %x\n",
                va, pa, l1, *l1, l2, *l2);
            h_deallocv(*l1);
        }
        h_deallocv_virt((unsigned int) ls);
        pages--;
        va += H_PAGE_SIZE;
        pa += H_PAGE_SIZE;
    }
}

unsigned long
h_get_map(unsigned long trbase, unsigned long virt)
{
    void *l1 =
        h_allocv(h_pt1_off((unsigned int) virt) + trbase) +
        (h_pt1_off((unsigned int) virt) & H_POFF_MASK);
    void *ls = l1;
    void *l2;
    unsigned int ret;
    if ((*(unsigned int *) l1) & H_PAGE_L1S) {
        unsigned int ret = *(unsigned int *) l1 & 0xfff00fff;
        if ((*(unsigned int *) l1) & H_PAGE_L1P) {
            h_deallocv_virt((unsigned int) ls);
            return 0;
        }
        h_deallocv_virt((unsigned int) ls);
        return ret;
    }
    if (!((*(unsigned int *) l1) & H_PAGE_L1P)) {
        h_deallocv_virt((unsigned int) ls);
        return 0;
    }
    l2 = h_allocv(*(unsigned int *) l1) + ((*(unsigned int *) l1) & 0xc00);
    l2 = l2 + h_pt2_off((unsigned int) virt);
    ret = *(unsigned int *) l2;
    if (!((*(unsigned int *) l2) & H_PAGE_L2P)) {
        h_deallocv(*(unsigned int *) l1 + ((*(unsigned int *) l1) & 0xc00));
        return 0;
    }
    h_deallocv(*(unsigned int *) l1 + ((*(unsigned int *) l1) & 0xc00));
    return ret;
}

void
h_fault_bridge_pages(struct v_world *w, unsigned long virt)
{
    virt = virt & H_PFN_MASK;
    if (virt == ((unsigned int) (w->hregs.hcpu.switcher) & H_PFN_MASK)) {
        h_relocate_npage(w);
        return;
    }
    if (virt == ((unsigned int) (w) & H_PFN_MASK)) {
        w->relocate = 1;
        return;
    }
}

unsigned int
h_check_bridge_pages(struct v_world *w, unsigned long virt)
{
    virt = virt & H_PFN_MASK;
    if (virt == ((unsigned int) (w->hregs.hcpu.switcher) & H_PFN_MASK)) {
        V_ERR("Conflicting npage");
        return 1;
    }
    if (virt == ((unsigned int) (w) & H_PFN_MASK)) {
        V_ERR("Conflicting world page");
        return 1;
    }
    return 0;
}

void
h_delete_trbase(struct v_world *world)
{
    void *htrv;
    unsigned int i, j;
    struct v_chunk v;
    v.order = H_TRBASE_ORDER;
    v.h.p = phys_to_page(world->htrbase);
    v.phys = world->htrbase;
    for (i = 0; i < (1 << H_TRBASE_ORDER); i++) {

        htrv = h_allocv(world->htrbase + i * H_PAGE_SIZE);
        for (j = 0; j < H_PAGE_SIZE; j += 4) {
            if ((*(unsigned int *) (htrv + j)) & 0x1) {
                struct v_chunk v;
                v.order = 0;
                v.h.p = phys_to_page(*(unsigned int *) (htrv + j));
                v.phys = *(unsigned int *) (htrv + j) & 0xfffff000;
                h_raw_depalloc(&v);
            }
        }
        h_deallocv(world->htrbase + i * H_PAGE_SIZE);
    }
    h_raw_depalloc(&v);
}

void
h_inv_pagetables(struct v_world *world, unsigned int virt)
{
    unsigned int phys = g_v2p(world, virt, 1);
    struct v_page *mpage = h_p2mp(world, phys);
    if (mpage == NULL)
        return;
    v_spt_inv_page(world, mpage);
}


void
h_inv_pagetable(struct v_world *world, struct v_spt_info *spt,
    unsigned long virt, unsigned int level)
{
    void *htrv;
    unsigned int i, j;
    void (*npage) (unsigned long, struct v_world *) =
        world->hregs.hcpu.switcher;
    for (i = 0; i < (1 << H_TRBASE_ORDER); i++) {

        htrv = h_allocv(spt->spt_paddr + i * H_PAGE_SIZE);
        for (j = 0; j < H_PAGE_SIZE; j += 4) {
            if ((*(unsigned int *) (htrv + j)) & 0x1) {
                struct v_chunk v;
                v.order = 0;
                v.h.p = phys_to_page(*(unsigned int *) (htrv + j));
                v.phys = *(unsigned int *) (htrv + j) & 0xfffff000;
                h_raw_depalloc(&v);
            }
        }
        h_clear_page((long int) htrv);
        h_deallocv(spt->spt_paddr + i * H_PAGE_SIZE);
    }
    h_set_map(spt->spt_paddr, (long unsigned int) world,
        h_v2p((unsigned int) world), 1, V_PAGE_W | V_PAGE_VM);
    V_LOG("world data at %x, ", h_v2p((unsigned int) world));
    h_set_map(spt->spt_paddr, (long unsigned int) npage,
        h_v2p((unsigned int) npage), 1, V_PAGE_W | V_PAGE_VM);
    V_LOG("neutral page at %x\n", h_v2p((unsigned int) npage));
}

void
h_inv_spt(struct v_world *world, struct v_spt_info *spt)
{
    struct v_inv_entry **p = &(spt->inv_list);
    while ((*p) != NULL) {
        struct v_inv_entry *psave = (*p);
        struct v_ptp_info **pl = &((*p)->page->ptp_list);
        V_EVENT("invalidating mfn %x", (*p)->page->mfn);
        while (*pl != NULL) {
            if ((*pl)->spt == spt) {
                struct v_ptp_info *next = (*pl)->next;
                V_LOG(", va %x %s", (*pl)->vaddr,
                    (*pl)->gpt_level == 0 ? "" : "pagetable");
                if ((*pl)->gpt_level == 0)
                    h_set_map(spt->spt_paddr, (*pl)->vaddr, 0, 1, V_PAGE_NOMAP);
                else
                    h_inv_pagetable(world, spt, (*pl)->vaddr, (*pl)->gpt_level);
                h_raw_dealloc((*pl));
                (*pl) = next;
            } else
                pl = &((*pl)->next);
        }
        *p = (*p)->next;
        h_raw_dealloc(psave);
    }
}

void
h_new_trbase(struct v_world *world)
{
    struct v_chunk *trbase;
    struct v_spt_info *spt;
    void *htrv;
    void (*npage) (unsigned long, struct v_world *) =
        world->hregs.hcpu.switcher;
    int i;
    unsigned int p15 =
        (world->gregs.mode ==
        G_MODE_MMU) ? (world->gregs.p15_trbase & H_PFN_MASK) : 1;
    if ((spt = v_spt_get_by_gpt(world, p15)) == NULL) {
        trbase = h_raw_palloc(H_TRBASE_ORDER);
        world->htrbase = (long int) (trbase->phys);
        V_EVENT("new base: %x from %x", world->htrbase, p15);
        for (i = 0; i < (1 << H_TRBASE_ORDER); i++) {
            htrv = h_allocv(world->htrbase + i * H_PAGE_SIZE);
            h_clear_page((long int) htrv);
            h_deallocv(world->htrbase + i * H_PAGE_SIZE);
        }
        h_set_map(world->htrbase, (long unsigned int) world,
            h_v2p((unsigned int) world), 1, V_PAGE_W | V_PAGE_VM);
        V_LOG("world data at %x, ", h_v2p((unsigned int) world));
        h_set_map(world->htrbase, (long unsigned int) npage,
            h_v2p((unsigned int) npage), 1, V_PAGE_W | V_PAGE_VM);
        V_LOG("neutral page at %x\n", h_v2p((unsigned int) npage));
        v_spt_add(world, world->htrbase, p15);
    } else {
        V_LOG("spt found at %x for %x", spt->spt_paddr, p15);
        world->htrbase = spt->spt_paddr;
        h_inv_spt(world, spt);
    }

}
