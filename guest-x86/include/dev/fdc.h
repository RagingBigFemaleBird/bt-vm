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
#ifndef G_DEV_FDC_H
#define G_DEV_FDC_H

#define G_DEV_FLOPPY_DENSITY 2

#define G_FDC_DOR_DMA_ENABLE	0x8
#define G_FDC_DOR_MOTOR1	0x20
#define G_FDC_DOR_MOTOR0	0x10
#define G_FDC_DOR_RESET		0x4
#define G_FDC_DOR_SELECT	0x3

#define G_FDC_STAT_BUSY0	0x1
#define G_FDC_STAT_BUSY1	0x2
#define G_FDC_STAT_BUSYC	0x10
#define G_FDC_STAT_NODMA	0x20
#define G_FDC_STAT_DIR_FDC2SYS  0x40
#define G_FDC_STAT_READY	0x80

#define G_FDC_ST0_SUCCESS	(0x0 << 6)
#define G_FDC_ST0_FAIL		(0x1 << 6)
#define G_FDC_ST0_BAD		(0x2 << 6)
#define G_FDC_ST0_SIGNAL	(0x3 << 6)
#define G_FDC_ST0_SEEK		(0x1 << 5)
#define G_FDC_ST0_EQUIP		(0x1 << 4)
#define G_FDC_ST0_NOTREADY	(0x1 << 3)
#define G_FDC_ST0_HEAD		(0x1 << 2)
#define G_FDC_ST0_DEV0		(0x0)
#define G_FDC_ST0_DEV1		(0x1)

#define G_FDC_CMD_MASK		0x1f
#define G_FDC_CMD_DUMP		0x0e
#define G_FDC_CMD_SPECIFY	0x03
#define G_FDC_CMD_RECAL		0x07
#define G_FDC_CMD_ST3		0x04
#define G_FDC_CMD_READ		0x06
#define G_FDC_CMD_SEEK		0x0f

#define G_FDC_DMA_CHANNEL       0x2

#define G_FDC_CHS_TO_BLOCK(x, y, z) ((x) * 0x12 * G_DEV_FLOPPY_DENSITY * 2 + (y) * 0x12 * G_DEV_FLOPPY_DENSITY + (z) - 1)
#define G_FDC_BLOCK_TO_C(x) ((x) / (0x12 * G_DEV_FLOPPY_DENSITY * 2))
#define G_FDC_BLOCK_TO_H(x) (((x) / (0x12 * G_DEV_FLOPPY_DENSITY)) % 2)
#define G_FDC_BLOCK_TO_S(x) ((x) % (0x12 * G_DEV_FLOPPY_DENSITY) + 1)

struct v_world;

struct g_dev_fdc {
    int reset;
    int motor[4];
    int dma;
    int dir;
    int in_expect_bytes;
    int out_expect_bytes;
    int in_index;
    int out_index;
    int cmd;
    int rwstart;
    int rwcount;
    unsigned char bytes[32];
};

int g_fdc_handle_io(struct v_world *, unsigned int, unsigned int, void *);

#endif
