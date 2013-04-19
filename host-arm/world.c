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
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/logging.h"
#include "host/include/cpu.h"
#include <asm/tlbflush.h>
#include <../arch/arm/include/asm/cacheflush.h>

extern struct h_cpu hostcpu;

void
h_world_init(struct v_world *world)
{
    int i;
    world->npage = h_switch_to;
    world->hregs.hcpu.switcher = h_switcher;

    h_pin(h_v2p((long unsigned int) world));
    h_pin(h_v2p((long unsigned int) h_switcher));

    h_set_map(world->htrbase, (long unsigned int) world,
        h_v2p((unsigned int) world), 1, V_PAGE_W | V_PAGE_VM);
    V_LOG("world data at %lx, pa %x", world, h_v2p((unsigned int) world));
    h_set_map(world->htrbase, (long unsigned int) world->hregs.hcpu.switcher,
        h_v2p((unsigned int) world->hregs.hcpu.switcher), 1,
        V_PAGE_W | V_PAGE_VM);
    V_LOG("neutral page at %p, pa %lx\n", world->hregs.hcpu.switcher,
        h_v2p((unsigned int) world->hregs.hcpu.switcher));
    world->hregs.gcpu.pc = G_PA_BASE + 0x8000;
    world->hregs.gcpu.cpsr = H_CPSR_USR;
    world->gregs.p15_ctrl = 0;
    world->pa_top += G_PA_BASE;

    for (i = 0; i < 32; i++) {
        world->gregs.io_page[i] = h_raw_malloc(sizeof(struct v_page));
        world->gregs.io_page[i]->has_virt = 0;
        world->gregs.io_page[i]->mfn = 0;
        world->gregs.io_page[i]->attr = V_PAGE_NOTPRESENT | V_PAGE_EXD;
        world->gregs.io_page[i]->poi_list = NULL;
        world->gregs.io_page[i]->ipoi_list = NULL;
        world->gregs.io_page[i]->fc_list = NULL;
        world->gregs.io_page[i]->ptp_list = NULL;
        world->gregs.io_page[i]->io_page_info = NULL;
    }
}

void
h_relocate_npage(struct v_world *w)
{
    struct v_spt_info *spt = w->spt_list;
    unsigned int oldnp = (unsigned int) (w->hregs.hcpu.switcher);
    struct v_chunk *chunk = h_raw_palloc(0);
    unsigned int phys = chunk->phys;
    void *virt = h_allocv(phys);

    h_memcpy(virt, w->hregs.hcpu.switcher, 4096);

    w->hregs.hcpu.switcher = virt;

    if (spt == NULL) {
        h_set_map(w->htrbase, oldnp, 0, 1, V_PAGE_NOMAP);
        h_set_map(w->htrbase, (unsigned long) w->hregs.hcpu.switcher,
            h_v2p((unsigned long) w->hregs.hcpu.switcher), 1,
            V_PAGE_W | V_PAGE_VM);
    } else
        while (spt != NULL) {
            h_set_map(spt->spt_paddr, oldnp, 0, 1, V_PAGE_NOMAP);
            h_set_map(spt->spt_paddr, (unsigned long) w->hregs.hcpu.switcher,
                h_v2p((unsigned long) w->hregs.hcpu.switcher), 1,
                V_PAGE_W | V_PAGE_VM);
            spt = spt->next;
        }
}

void
h_relocate_world(struct v_world *w, struct v_world *w_new)
{
    struct v_spt_info *spt = w->spt_list;

    V_ERR("world data change from %x to %x ", h_v2p((unsigned int) w),
        h_v2p((unsigned int) w_new));

    if (spt == NULL) {
        h_set_map(w->htrbase, (long unsigned int) w, 0, 1, V_PAGE_NOMAP);
        h_set_map(w->htrbase, (long unsigned int) w_new,
            h_v2p((unsigned int) w_new), 1, V_PAGE_W | V_PAGE_VM);
    } else
        while (spt != NULL) {
            h_set_map(spt->spt_paddr, (long unsigned int) w, 0, 1,
                V_PAGE_NOMAP);
            h_set_map(spt->spt_paddr, (long unsigned int) w_new,
                h_v2p((unsigned int) w_new), 1, V_PAGE_W | V_PAGE_VM);
            spt = spt->next;
        }
}
