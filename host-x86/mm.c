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
#include <asm/io.h>
#include <linux/slab.h>
#include <../arch/x86/include/asm/highmem.h>
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/logging.h"
#include "host/include/cpu.h"

#define phys_to_page(phys)    (pfn_to_page((phys) >> PAGE_SHIFT))

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
h_pin(h_addr_t phys)
{
/*	struct page *page = phys_to_page(phys);
	get_page(page);
*/
}

void
h_unpin(h_addr_t phys)
{
/*	struct page *page = phys_to_page(phys);
	put_page(page);
*/
}

struct v_chunk *
h_raw_palloc(unsigned int order)
{
    struct page *p;
    struct v_chunk *v;
    h_addr_t phys;
    unsigned int pr;
    p = alloc_pages(GFP_ATOMIC, order);
    if (p == NULL) {
        V_ERR("Page allocation failure");
        return NULL;
    }
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
    h_addr_t phys;
    v->h.p = phys_to_page(v->phys);
    if (v->h.p == NULL) {
        V_ERR("page deallocation failure");
        return;
    }
    for (pr = (1 << v->order), phys = v->phys; pr > 0; phys += 0x1000, pr--) {
        h_unpin(phys);
    }
    __free_pages(v->h.p, v->order);
}

void *
h_allocv(h_addr_t phys)
{
    return kmap(phys_to_page(phys));
}

void
h_deallocv(h_addr_t phys)
{
    //kunmap(phys_to_page(phys));
}

void
h_set_p2m(struct v_world *world, g_addr_t pa, unsigned long pages, h_addr_t ma)
{
    struct v_page *pg;
    while (pages > 0) {
        if (ma == H_PFN_NP) {
            pg = &world->page_list[h_p2page(pa)];
            pg->attr |= V_PAGE_NOTPRESENT;
            pa += H_PAGE_SIZE;
            pages--;
            V_LOG("page %lx(%x) set to mfn NP\n", (unsigned long) h_p2page(pa),
                pg->attr);
        } else {
            pg = &world->page_list[h_p2page(pa)];
            pg->mfn = (ma >> H_PAGE_SHIFT);
            pg->attr &= (~V_PAGE_NOTPRESENT);
            V_LOG("page %lx(%x) set to mfn %x\n", (unsigned long) h_p2page(pa),
                pg->attr, (unsigned int) pg->mfn);
            pages--;
            pa += H_PAGE_SIZE;
            ma += H_PAGE_SIZE;
        }
    }
}

struct v_page *
h_p2mp(struct v_world *world, h_addr_t pa)
{
    struct v_page *pg;
    if (pa > world->pa_top)
        return NULL;
    pg = &world->page_list[h_p2page(pa)];
    return pg;
}

#define h_pt1_off(va) (((va) >> 22) << 2)
#define h_pt2_off(va) ((((va) << 10) >> 22) << 2)
#define H_PAGE_US	4
#define H_PAGE_RW	2
#define H_PAGE_P	1
#define H_PAGE_PS	0x80
#define H_PAGE_PCD	(1<<4)
#define H_PAGE_PWT	(1<<3)

#define h_pae_pdpte_off(va) (((va) >> 30) << 3)
#define h_pae_pt1_off(va) ((((va) & 0x3ffff000) >> 21) << 3)
#define h_pae_pt2_off(va) ((((va) & 0x1ff000) >> 12) << 3)

h_addr_t
h_v2p(h_addr_t virt)
{
    unsigned int cr3;
    h_addr_t ret;
    void *x, *l1, *l2;
    asm volatile ("movl %%cr3, %0":"=r" (cr3):);
    virt = virt & 0xffffffff;
    x = h_allocv(cr3);
#ifdef H_MM_USE_PAE
    {
        void *pdpte = h_pae_pdpte_off((unsigned int) virt) + x;
        pdpte += (cr3 & H_POFF_MASK);
        V_VERBOSE("h_v2p @ %llx cr3 = %x, pdpte %p: %llx", virt, cr3, pdpte,
            *(h_addr_t *) pdpte);
        if (!((*(h_addr_t *) pdpte) & H_PAGE_P)) {
            h_deallocv(cr3);
            return 0;
        }
        l1 = h_allocv(*(h_addr_t *) pdpte);
        l1 = l1 + h_pae_pt1_off(virt);
        V_VERBOSE("l1 %p: %llx", l1, *(h_addr_t *) l1);
        if ((*(h_addr_t *) l1) & H_PAGE_PS) {
            h_addr_t ret =
                ((*(h_addr_t *) l1) & 0x7fffffffffe00000) + (virt & 0x1fffff);
            if (!((*(h_addr_t *) l1) & H_PAGE_P)) {
                h_deallocv(cr3);
                return 0;
            }
            h_deallocv(cr3);
            return ret;
        }
        l2 = h_allocv(*(h_addr_t *) l1 & 0x7ffffffffffff000);
        l2 = l2 + h_pae_pt2_off(virt);
        V_VERBOSE("l2 %p: %llx", l2, *(h_addr_t *) l2);
        ret = ((*(h_addr_t *) l2) & 0x7ffffffffffff000) + (virt & 0xfff);
        if (!((*(h_addr_t *) l2) & H_PAGE_P)) {
            h_deallocv(*(h_addr_t *) l1);
            return 0;
        }
        h_deallocv(*(h_addr_t *) l1);
        return ret;
    }
#else
    l1 = h_pt1_off((unsigned int) virt) + x;
    if ((*(unsigned int *) l1) & H_PAGE_PS) {
        unsigned int ret =
            ((*(unsigned int *) l1) & 0xffc00000) + (virt & 0x3fffff);
        if (!((*(unsigned int *) l1) & H_PAGE_P)) {
            h_deallocv(cr3);
            return 0;
        }
        h_deallocv(cr3);
        return ret;
    }
    l2 = h_allocv(*(unsigned int *) l1);
    l2 = l2 + h_pt2_off((unsigned int) virt);
    ret = ((*(unsigned int *) l2) & H_PFN_MASK) + (virt & H_POFF_MASK);
    if (!((*(unsigned int *) l2) & H_PAGE_P)) {
        h_deallocv(*(unsigned int *) l1);
        return 0;
    }
    h_deallocv(*(unsigned int *) l1);
    return ret;
#endif
}

void
h_clear_page(void *va)
{
    va = (void *) (((unsigned int) va) & H_PFN_MASK);
    h_memset((void *) va, 0, 4096);
}

#ifndef H_MM_USE_PAE
static unsigned int
h_pt1_format(h_addr_t pa, int attr)
{
    unsigned int entry = (pa & H_PFN_MASK);
    entry = entry | H_PAGE_US;
    entry = entry | H_PAGE_RW;
    entry = entry | H_PAGE_P;
    return entry;
}

static unsigned int
h_pt2_format(h_addr_t pa, int attr)
{
    unsigned int entry = (pa & H_PFN_MASK);
    unsigned int priv = attr & V_PAGE_PRIV_MASK;
    if (priv == V_PAGE_USR || priv == V_PAGE_SYS)
        entry = entry | H_PAGE_US;
    if (attr & V_PAGE_W)
        entry = entry | H_PAGE_RW;
    if (!(attr & V_PAGE_NOMAP))
        entry = entry | H_PAGE_P;
    if (attr & V_PAGE_WBD)
        entry = entry | H_PAGE_PWT;
    if (attr & V_PAGE_CD)
        entry = entry | H_PAGE_PCD;
    return entry;
}

#else
static unsigned long long
h_pae_pdpte_format(h_addr_t pa, int attr)
{
    unsigned long long entry = (pa & 0xfffffffffffff000);
    entry = entry | H_PAGE_P;
    return entry;
}

static unsigned long long
h_pae_pt1_format(h_addr_t pa, int attr)
{
    unsigned long long entry = (pa & 0xfffffffffffff000);
    entry = entry | H_PAGE_US;
    entry = entry | H_PAGE_RW;
    entry = entry | H_PAGE_P;
    return entry;
}

static unsigned long long
h_pae_pt2_format(h_addr_t pa, int attr)
{
    unsigned long long entry = (pa & 0xfffffffffffff000);
    unsigned long long priv = attr & V_PAGE_PRIV_MASK;
    if (priv == V_PAGE_USR || priv == V_PAGE_SYS)
        entry = entry | H_PAGE_US;
    if (attr & V_PAGE_W)
        entry = entry | H_PAGE_RW;
    if (!(attr & V_PAGE_NOMAP))
        entry = entry | H_PAGE_P;
    if (attr & V_PAGE_WBD)
        entry = entry | H_PAGE_PWT;
    if (attr & V_PAGE_CD)
        entry = entry | H_PAGE_PCD;
    return entry;
}
#endif

void
h_set_map(h_addr_t trbase, h_addr_t va, h_addr_t pa, h_addr_t pages, int attr)
{
    va = va & H_PFN_MASK;
    pa = pa & H_PFN_MASK;
    /* va is 32 always bit */
    va = va & 0xffffffff;
    while (pages > 0) {
#ifdef H_MM_USE_PAE
        void *tr = h_allocv(trbase);
        h_addr_t *l0 = tr + (trbase & H_POFF_MASK) + h_pae_pdpte_off(va), *l1;
        void *l1v;
        if (!((*l0) & H_PAGE_P)) {
            struct v_chunk *c = h_raw_palloc(0);
            void *l1v = h_allocv(c->phys);
            h_clear_page(l1v);
            *l0 = h_pae_pdpte_format(c->phys, attr);
            h_deallocv(c->phys);
        }
        l1v = h_allocv(*l0);
        l1 = l1v + h_pae_pt1_off(va);
        if (h_pae_pt1_off(va) != h_pae_pt1_off(va - H_PAGE_SIZE)
            && pages >= (1 << 9)) {
            (*l1) = h_pae_pt1_format(pa, attr) | H_PAGE_PS;
            V_LOG("mapping large page %llx to %llx, l1 = %llx", va, pa, *l1);
            h_deallocv(*l0);
            h_deallocv(trbase);
            pages -= (1 << 9);
            va += H_PAGE_SIZE * (1 << 9);
            pa += H_PAGE_SIZE * (1 << 9);
            continue;
        }
        if (((*l1) & H_PAGE_P) == 0 || ((*l1) & H_PAGE_PS)) {
            struct v_chunk *c = h_raw_palloc(0);
            void *l2v = h_allocv(c->phys);
            h_addr_t *l2 = l2v + h_pae_pt2_off(va);
            h_clear_page(l2v);
            (*l1) = h_pae_pt1_format(c->phys, attr);
            (*l2) = h_pae_pt2_format(pa, attr);
            V_LOG("mapping %llx to %llx, l1 = %llx, l2= %llx\n", va, pa, *l1,
                *l2);
            h_deallocv(c->phys);
        } else {
            void *l2v = h_allocv((*l1));
            h_addr_t *l2 = l2v + h_pae_pt2_off(va);
            (*l2) = h_pae_pt2_format(pa, attr);
            V_LOG("mapping %llx to %llx, l1 = %llx, l2= %llx\n", va, pa, *l1,
                *l2);
            h_deallocv((*l1));
        }
        h_deallocv(*l0);
        h_deallocv(trbase);
        pages--;
        va += H_PAGE_SIZE;
        pa += H_PAGE_SIZE;
#else
        void *tr = h_allocv(trbase);
        h_addr_t *l1 = tr + h_pt1_off(va);
        if (h_pt1_off(va) != h_pt1_off(va - H_PAGE_SIZE) && pages >= (1 << 10)) {
            //large page possible
            (*l1) = h_pt1_format(pa, attr) | H_PAGE_PS;
            V_LOG("mapping large page %lx to %lx, l1 = %x", va, pa, *l1);
            h_deallocv(trbase);
            pages -= (1 << 10);
            va += H_PAGE_SIZE * (1 << 10);
            pa += H_PAGE_SIZE * (1 << 10);
            continue;
        }
        if (((*l1) & H_PAGE_P) == 0 || ((*l1) & H_PAGE_PS)) {
            struct v_chunk *c = h_raw_palloc(0);
            void *l2v = h_allocv(c->phys);
            h_addr_t *l2 = l2v + h_pt2_off(va);
            h_clear_page(l2v);
            (*l1) = h_pt1_format(c->phys, attr);
            (*l2) = h_pt2_format(pa, attr);
            V_LOG("mapping %lx to %lx, l1 = %x, l2= %x\n", va, pa, *l1, *l2);
            h_deallocv(c->phys);
        } else {
            void *l2v = h_allocv((*l1));
            h_addr_t *l2 = l2v + h_pt2_off(va);
            (*l2) = h_pt2_format(pa, attr);
            V_LOG("mapping %lx to %lx, l1 = %x, l2= %x\n", va, pa, *l1, *l2);
            h_deallocv((*l1));

        }
        h_deallocv(trbase);
        pages--;
        va += H_PAGE_SIZE;
        pa += H_PAGE_SIZE;
#endif
    }
}

h_addr_t
h_get_map(h_addr_t trbase, h_addr_t virt)
{
    void *x = h_allocv(trbase);
    void *l1 = h_pt1_off((unsigned int) virt) + x;
    void *l2;
    h_addr_t ret;
#ifdef H_MM_USE_PAE
    {
        void *l0;
        virt = virt & 0xffffffff;
        l0 = h_pae_pdpte_off(virt) + x + (trbase & H_POFF_MASK);
        if (!((*(h_addr_t *) l0) & H_PAGE_P)) {
            h_deallocv(trbase);
            return 0;
        }
        l1 = h_pae_pt1_off(virt) + h_allocv(*(h_addr_t *) l0);
        if ((*(h_addr_t *) l1) & H_PAGE_PS) {
            h_addr_t ret = *(h_addr_t *) l1;
            h_deallocv(trbase);
            return ret;
        }
        if (!((*(h_addr_t *) l1) & H_PAGE_P)) {
            h_deallocv(*(h_addr_t *) l0);
            h_deallocv(trbase);
            return 0;
        }
        l2 = h_allocv(*(h_addr_t *) l1);
        l2 = l2 + h_pae_pt2_off(virt);
        ret = *(h_addr_t *) l2;
        if (!((*(h_addr_t *) l2) & H_PAGE_P)) {
            ret = 0;
        }
        h_deallocv(*(h_addr_t *) l1);
        h_deallocv(*(h_addr_t *) l0);
        h_deallocv(trbase);
        return ret;
    }
#else
    if (!((*(h_addr_t *) l1) & H_PAGE_P)) {
        h_deallocv(trbase);
        return 0;
    }
    if ((*(h_addr_t *) l1) & H_PAGE_PS) {
        h_addr_t ret = *(h_addr_t *) l1;
        h_deallocv(trbase);
        return ret;
    }
    l2 = h_allocv(*(h_addr_t *) l1);
    l2 = l2 + h_pt2_off(virt);
    ret = *(h_addr_t *) l2;
    if (!((*(h_addr_t *) l2) & H_PAGE_P)) {
        h_deallocv(*(h_addr_t *) l1);
        h_deallocv(trbase);
        return 0;
    }
    h_deallocv(*(h_addr_t *) l1);
    h_deallocv(trbase);
    return ret;
#endif
}

void
h_fault_bridge_pages(struct v_world *w, h_addr_t virt)
{
    int i;
    virt = virt & H_PFN_MASK;
    if (virt == ((unsigned int) (w->hregs.hcpu.switcher) & H_PFN_MASK)) {
        h_relocate_npage(w);
        return;
    }
    if (virt == ((unsigned int) (w->hregs.gcpu.gdt.base) & H_PFN_MASK)) {
        h_relocate_tables(w);
        return;
    }
    if (virt == ((unsigned int) (w->hregs.gcpu.idt.base) & H_PFN_MASK)) {
        h_relocate_tables(w);
        return;
    }
    if (virt == ((unsigned int) (w) & H_PFN_MASK)) {
        w->relocate = 1;
        return;
    }
    for (i = 0; i < w->pool_count; i++) {
        if (w->host_pools[i].mon_virt != 0 && w->host_pools[i].mon_virt <= virt
            && w->host_pools[i].mon_virt + w->host_pools[i].total_size >=
            virt) {
            struct v_spt_info *p = w->spt_list;
            w->host_pools[i].mon_virt = 0;
            while (p != NULL) {
                p->mem_pool_mapped[i] = 0;
                p = p->next;
            }
            return;
        }
    }
}

unsigned int
h_check_bridge_pages(struct v_world *w, h_addr_t virt)
{
    int i;
    virt = virt & H_PFN_MASK;
    if (virt == ((h_addr_t) (w->hregs.hcpu.switcher) & H_PFN_MASK)) {
        return 1;
    }
    if (virt == ((h_addr_t) (w->hregs.gcpu.gdt.base) & H_PFN_MASK)) {
        return 1;
    }
    if (virt == ((h_addr_t) (w->hregs.gcpu.idt.base) & H_PFN_MASK)) {
        return 1;
    }
    if (virt == ((h_addr_t) (w) & H_PFN_MASK)) {
        return 1;
    }
    for (i = 0; i < w->pool_count; i++) {
        if (w->host_pools[i].mon_virt != 0 && w->host_pools[i].mon_virt <= virt
            && w->host_pools[i].mon_virt + w->host_pools[i].total_size >=
            virt) {
            return 1;
        }
    }
    return 0;
}

int
h_read_guest(struct v_world *world, h_addr_t addr, unsigned int *word)
{
    g_addr_t pa = g_v2p(world, addr, 0);
    void *virt;
    struct v_page *mpage;
    mpage = h_p2mp(world, pa);
    if (mpage == NULL) {
        V_ERR("Guest GP Fault during access\n");
        return 1;
    }
    if ((addr & 0xfff) < 0xffd) {
        //no page crossing
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        (*word) = (*(unsigned int *) (virt));
    } else {
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        h_memcpy(word, virt, 0xfff - (addr & 0xfff) + 1);
        pa = g_v2p(world, addr + 0x4, 0);
        mpage = h_p2mp(world, pa);
        if (mpage == NULL) {
            V_ERR("Guest GP Fault during access\n");
            return 1;
        }
        virt = v_page_make_present(mpage);
        h_memcpy((void *) (word) + 0xfff - (addr & 0xfff) + 1, virt,
            3 + (addr & 0xfff) - 0xfff);

    }
    return 0;
}

int
h_write_guest(struct v_world *world, h_addr_t addr, unsigned int word)
{
    g_addr_t pa = g_v2p(world, addr, 0);
    void *virt;
    struct v_page *mpage;
    mpage = h_p2mp(world, pa);
    if (mpage == NULL) {
        V_ERR("Guest GP Fault during write\n");
        return 1;
    }
    if ((addr & 0xfff) < 0xffd) {
        //no page crossing
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        (*(unsigned int *) (virt)) = word;
    } else {
        virt = v_page_make_present(mpage);
        virt = virt + (addr & 0xfff);
        h_memcpy(virt, &word, 0xfff - (addr & 0xfff) + 1);
        pa = g_v2p(world, addr + 0x4, 0);
        mpage = h_p2mp(world, pa);
        if (mpage == NULL) {
            V_ERR("Guest GP Fault during write\n");
            return 1;
        }
        virt = v_page_make_present(mpage);
        h_memcpy(virt, ((void *) (&word)) + 0xfff - (addr & 0xfff) + 1,
            3 + (addr & 0xfff) - 0xfff);

    }
    return 0;
}

h_addr_t
h_monitor_search_big_pages(struct v_world * world, unsigned int trbase,
    h_addr_t size)
{
    unsigned int ret;
    long long req = size;
    void *x, *l1;
#ifdef H_MM_USE_PAE
    void *l0;
    x = h_allocv(trbase);
    for (ret = 0xe0000000; ret < 0xf0000000; ret += 0x200000) {
        l0 = h_pae_pdpte_off(ret) + x + (trbase & H_POFF_MASK);
        if (!((*(h_addr_t *) l0) & H_PAGE_P)) {
            continue;
        }
        l1 = h_allocv(*(h_addr_t *) l0);
        l1 = h_pae_pt1_off((unsigned int) ret) + l1;
        if (!((*(unsigned int *) l1) & H_PAGE_PS)) {
            req -= 0x200000;
            if (req <= 0) {
                V_VERBOSE("Found %lx spacing in pt",
                    (unsigned long) (ret + 0x200000 - size));
                h_deallocv(trbase);
                return ret + 0x200000 - size;
            }
        } else {
            req = size;
        }
    }
#else
    x = h_allocv(trbase);
    for (ret = 0xe0000000; ret < 0xf0000000; ret += 0x400000) {
        l1 = h_pt1_off((unsigned int) ret) + x;
        if (!((*(unsigned int *) l1) & H_PAGE_PS)) {
            req -= 0x400000;
            if (req <= 0) {
                V_VERBOSE("Found %lx spacing in pt",
                    (unsigned long) (ret + 0x400000 - size));
                h_deallocv(trbase);
                return ret + 0x400000 - size;
            }
        } else {
            req = size;
        }
    }
#endif
    h_deallocv(trbase);
    return 0;
}

void
h_monitor_setup_data_pages(struct v_world *world, h_addr_t sptbase)
{
    struct v_spt_info *spt;
    int i;
    if ((spt = v_spt_get_by_spt(world, sptbase)) == NULL) {
        V_ERR("Cannot setup monitor pages. SPT bug?");
        return;
    }
    for (i = 0; i < world->pool_count; i++) {
        if (world->host_pools[i].mon_virt == 0) {
            world->host_pools[i].mon_virt =
                h_monitor_search_big_pages(world, sptbase,
                world->host_pools[i].total_size);
            if (world->host_pools[i].mon_virt == 0) {
                V_ERR("Cannot find suitable mapping @ guest world");
                return;
            }
        }
        if (spt->mem_pool_mapped[i] == 0) {
            spt->mem_pool_mapped[i] = 1;
            h_set_map(sptbase, world->host_pools[i].mon_virt,
                world->host_pools[i].phys,
                world->host_pools[i].total_size / H_PAGE_SIZE,
                V_PAGE_W | V_PAGE_VM);
        }
    }
}
