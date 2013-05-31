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
#ifndef G_DEV_DMA_H
#define G_DEV_DMA_H

struct v_world;
#define G_DMA_DIR_READ	2
#define G_DMA_DIR_WRITE	1

struct g_dev_dma {
    int ff;                     /*flip-flop */
    unsigned int address[8];
    unsigned int length[8];
    int mask[8];
    int dir[8];
};

int g_dma_handle_io(struct v_world *, unsigned int, unsigned int, void *);
unsigned int g_dma_transfer(struct v_world *, int, void *, unsigned int,
    int dir);

#endif
