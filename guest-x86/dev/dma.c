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
#include "guest/include/world.h"
#include "guest/include/cpu.h"
#include "guest/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "guest/include/dev/dma.h"

unsigned int
g_dma_transfer(struct v_world *world, int channel, void *buffer,
    unsigned int count, int dir)
{
    struct g_dev_dma *dma_states = &world->gregs.dev.dma;
    struct v_page *mpage;
    void *virt;
    unsigned int rem;
    if (count > dma_states->length[channel]) {
        count = dma_states->length[channel] + 1;
    }
    rem = count;
    while (rem > 0) {
        unsigned to_copy =
            H_PAGE_SIZE - (dma_states->address[channel] & H_POFF_MASK);
        if (to_copy > rem) {
            to_copy = rem;
        }
        mpage = h_p2mp(world, dma_states->address[channel]);
        virt =
            v_page_make_present(mpage) +
            (dma_states->address[channel] & H_POFF_MASK);
        V_EVENT("dma from %p to %p len %x", buffer, virt, to_copy);
        if (dir) {
            h_memcpy(buffer, virt, to_copy);
        } else {
            h_memcpy(virt, buffer, to_copy);
        }
        rem -= to_copy;
        dma_states->address[channel] += to_copy;
        buffer += to_copy;
    }
    return count;
}

int
g_dma_handle_io(struct v_world *world, unsigned int dir, unsigned int address,
    void *param)
{
    unsigned char *data = param;
    struct g_dev_dma *dma_states = &world->gregs.dev.dma;
    switch (address) {
    case 0x0a:
        dma_states->mask[(*data) & 0x3] = (*data) >> 2;
        if (dma_states->mask[(*data) & 0x3] == 0) {
            V_EVENT("DMA transfer set up at %x len %x",
                dma_states->address[(*data) & 0x3],
                dma_states->length[(*data) & 0x3]);
        }
        break;
    case 0x0c:
        dma_states->ff = 0;
        break;
    case 0x0b:
        dma_states->dir[(*data) & 0x3] = ((*data) >> 2) & 0x3;
        if (!((*data) >> 4)) {
            V_ALERT("cannot handle dma state %x", *data);
        }
        break;
    case 0x81:
        dma_states->address[2] =
            (dma_states->address[2] & 0xffff) + ((*data) << 16);
        break;
    case 0x4:
        if (dma_states->ff) {
            dma_states->address[2] =
                (dma_states->address[2] & 0xff00ff) + ((*data) << 8);
        } else {
            dma_states->address[2] =
                (dma_states->address[2] & 0xffff00) + ((*data));
        }
        dma_states->ff ^= 1;
        break;
    case 0x5:
        if (dma_states->ff) {
            dma_states->length[2] =
                (dma_states->length[2] & 0x00ff) + ((*data) << 8);
        } else {
            dma_states->length[2] =
                (dma_states->length[2] & 0xff00) + ((*data));
        }
        dma_states->ff ^= 1;
        break;
    default:
        V_ERR("unhandled DMA IO %s port %x DATA=%x",
            (dir == G_IO_IN) ? "in" : "out", address, *(unsigned char *) param);

    }
    return 0;
}
