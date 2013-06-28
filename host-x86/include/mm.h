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
#include "guest/include/mm.h"

#define H_TRBASE_ORDER 0
#define H_PAGE_SIZE 4096
#define H_PAGE_SHIFT 12
#define H_PAGE_ALIGNED(x) ((x & H_PAGE_OFFSET ) == x)
#define H_PFN_MASK 0xfffff000
#define H_POFF_MASK 0xfff
#define H_PFN_MASK64 0xfffffffffffff000

#define H_PFN_NP	0xffffffff
#define h_p2page(phys) ((phys) >> H_PAGE_SHIFT)

#define H_MEM_POOL_DEFAULT_ORDER 10

#ifdef CONFIG_X86_PAE
#define H_MM_USE_PAE
#endif
#ifdef H_MM_USE_PAE
typedef unsigned long long h_addr_t;
#else
typedef unsigned int h_addr_t;
#endif

struct v_chunk;

struct h_chunk {
    struct page *p;
};

h_addr_t h_v2p(h_addr_t);

#define h_memcpy(dst, src, size) memcpy((dst), (src), (size))
#define h_memset(dst, src, size) memset((dst), (src), (size))

/* host raw memory allocator, returns virtual address, NULL if failed */
void *h_raw_malloc(unsigned long size);
void h_raw_dealloc(void *addr);

/* host raw memory page allocator, returns machine address, NULL if failed */
struct v_chunk *h_raw_palloc(unsigned int order);
void h_raw_depalloc(struct v_chunk *v);

/* allocate/deallocate virtual page for the specific physical page */
void *h_allocv(h_addr_t);
void h_deallocv(h_addr_t);

/* temporaryly allocate virtual page. the first address may fail as soon as the second call is made */
void *h_allocv_temp(h_addr_t);
void *h_allocv_temp2(h_addr_t);

/* set p2m mapping for the specified vm, p, length, m */
void h_set_p2m(struct v_world *, g_addr_t, unsigned long, h_addr_t);

/* set mapping specified by translation base, va, pa, attr */
void h_set_map(h_addr_t, h_addr_t, h_addr_t, h_addr_t, int);

void h_clear_page(void *);

void h_pin(h_addr_t);

struct v_page *h_p2mp(struct v_world *, h_addr_t);

h_addr_t h_get_map(h_addr_t, h_addr_t);
unsigned int h_check_bridge_pages(struct v_world *, h_addr_t);
void h_fault_bridge_pages(struct v_world *, h_addr_t);

/* read 4 bytes from guest memory */
int h_read_guest(struct v_world *, h_addr_t, unsigned int *);
/* write 4 bytes into guest memory */
int h_write_guest(struct v_world *, h_addr_t, unsigned int);
h_addr_t h_monitor_search_big_pages(struct v_world *, unsigned int, h_addr_t);
void h_monitor_setup_data_pages(struct v_world *, h_addr_t);
void h_virt_make_executable(h_addr_t, unsigned long);
#endif
