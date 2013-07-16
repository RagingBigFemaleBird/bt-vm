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
/* memory definitions and allocator */
#ifndef H_MM_H
#define H_MM_H
#include <../arch/arm/include/asm/page.h>
#include <asm-generic/memory_model.h>

#define H_TRBASE_ORDER 2
#define H_PAGE_SIZE 4096
#define H_PT_SIZE 1024
#define H_PAGE_SHIFT 12
#define H_PAGE_ALIGNED(x) ((x & H_PAGE_OFFSET ) == x)
#define H_PFN_MASK 0xfffff000
#define H_POFF_MASK 0xfff

#define H_PFN_NP	0xffffffff
#define h_p2page(phys) ((phys) >> H_PAGE_SHIFT)

#define H_MEM_POOL_DEFAULT_ORDER 10

struct v_world;

typedef unsigned int h_addr_t;

struct v_chunk;

struct h_chunk {
    struct page *p;
};

#define h_memcpy(dst, src, size) memcpy((dst), (src), (size))
#define h_memset(dst, src, size) memset((dst), (src), (size))

unsigned int h_v2p(unsigned int);

/* host raw memory allocator, returns virtual address, NULL if failed */
void *h_valloc(unsigned long size);
void h_vfree(void *addr);

/* host raw memory page allocator, returns machine address, NULL if failed */
struct v_chunk *h_palloc(unsigned int order);
void h_pfree(struct v_chunk *v);

/* allocate/deallocate virtual page for the specific physical page */
void *h_alloc_va(unsigned int phys);
void h_free_va(unsigned int phys);
void h_free_va_virt(unsigned int virt);

/* temporaryly allocate virtual page. the first address may fail as soon as the second call is made */
void *h_alloc_va_temp(unsigned int phys);
void *h_alloc_va_temp2(unsigned int phys);

/* set p2m mapping for the specified vm, p, length, m */
void h_set_p2m(struct v_world *, h_addr_t, unsigned long, h_addr_t);

/* set mapping specified by translation base, va, pa, attr */
void h_set_map(unsigned long, unsigned long, unsigned long, unsigned long, int);

void h_clear_page(void *);

void h_pin(unsigned long);

struct v_page *h_p2mp(struct v_world *, h_addr_t);

unsigned long h_get_map(unsigned long, unsigned long);
unsigned int h_check_bridge_pages(struct v_world *, unsigned long);
void h_fault_bridge_pages(struct v_world *, unsigned long);

void h_delete_trbase(struct v_world *);
void h_new_trbase(struct v_world *);
void h_inv_pagetables(struct v_world *, unsigned int);

h_addr_t h_monitor_search_big_pages(struct v_world *, unsigned int, h_addr_t);
void h_monitor_setup_data_pages(struct v_world *, h_addr_t);
void h_virt_make_executable(h_addr_t, unsigned long);

#endif
