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
#include "guest/include/dev/fdc.h"
#include "guest/include/dev/dma.h"

extern unsigned char *g_disk_data;

unsigned int g_dev_floppy_density = 2;

static void
g_fdc_run_cmd(struct v_world *world)
{
    struct g_dev_fdc *fdc_states = &world->gregs.dev.fdc;
    switch (fdc_states->cmd) {
    case G_FDC_CMD_READ:
        if (fdc_states->media_changed) {
            fdc_states->in_expect_bytes = 7;
            fdc_states->bytes[0] = G_FDC_ST0_SIGNAL;
            fdc_states->bytes[1] = 0;
            fdc_states->bytes[2] = 0;
            fdc_states->bytes[3] = G_FDC_BLOCK_TO_C(fdc_states->rwstart);
            fdc_states->bytes[4] = G_FDC_BLOCK_TO_H(fdc_states->rwstart);
            fdc_states->bytes[5] = G_FDC_BLOCK_TO_S(fdc_states->rwstart);
            fdc_states->bytes[6] = 2;
            g_pic_trigger(world, G_PIC_FDC_INT);
            break;
        }
        V_EVENT("FDC read head %x cyl %x sec %x count %x",
            fdc_states->bytes[2], fdc_states->bytes[1],
            fdc_states->bytes[3], fdc_states->bytes[5]);
        if ((fdc_states->bytes[0] >> 2) != fdc_states->bytes[2]
            || fdc_states->bytes[4] != 2 || fdc_states->bytes[6] != 0x1b
            || fdc_states->bytes[7] != 0xff) {
            V_ALERT("FDC read sanity check failed, %x, %x, %x, %x",
                fdc_states->bytes[0], fdc_states->bytes[4],
                fdc_states->bytes[6], fdc_states->bytes[7]);
        }
        fdc_states->rwstart =
            G_FDC_CHS_TO_BLOCK(fdc_states->bytes[1],
            fdc_states->bytes[2], fdc_states->bytes[3]);
        fdc_states->rwcount = fdc_states->bytes[5];
        fdc_states->rwstart +=
            g_dma_transfer(world, G_FDC_DMA_CHANNEL,
            &g_disk_data[fdc_states->rwstart * 512],
            fdc_states->rwcount * 512, 0) / 512;
        V_EVENT("DMA transfer complete, logical block at %x",
            fdc_states->rwstart);
        fdc_states->in_expect_bytes = 7;
        fdc_states->bytes[0] = G_FDC_ST0_SUCCESS;
        fdc_states->bytes[1] = 0;
        fdc_states->bytes[2] = 0;
        fdc_states->bytes[3] = G_FDC_BLOCK_TO_C(fdc_states->rwstart);
        fdc_states->bytes[4] = G_FDC_BLOCK_TO_H(fdc_states->rwstart);
        fdc_states->bytes[5] = G_FDC_BLOCK_TO_S(fdc_states->rwstart);
        fdc_states->bytes[6] = 2;
        g_pic_trigger(world, G_PIC_FDC_INT);
        break;
    case G_FDC_CMD_WRITE:
        if (fdc_states->media_changed) {
            fdc_states->in_expect_bytes = 7;
            fdc_states->bytes[0] = G_FDC_ST0_SIGNAL;
            fdc_states->bytes[1] = 0;
            fdc_states->bytes[2] = 0;
            fdc_states->bytes[3] = G_FDC_BLOCK_TO_C(fdc_states->rwstart);
            fdc_states->bytes[4] = G_FDC_BLOCK_TO_H(fdc_states->rwstart);
            fdc_states->bytes[5] = G_FDC_BLOCK_TO_S(fdc_states->rwstart);
            fdc_states->bytes[6] = 2;
            g_pic_trigger(world, G_PIC_FDC_INT);
            break;
        }
        V_EVENT("FDC write head %x cyl %x sec %x count %x",
            fdc_states->bytes[2], fdc_states->bytes[1],
            fdc_states->bytes[3], fdc_states->bytes[5]);
        if ((fdc_states->bytes[0] >> 2) != fdc_states->bytes[2]
            || fdc_states->bytes[4] != 2 || fdc_states->bytes[6] != 0x1b
            || fdc_states->bytes[7] != 0xff) {
            V_ALERT("FDC write sanity check failed, %x, %x, %x, %x",
                fdc_states->bytes[0], fdc_states->bytes[4],
                fdc_states->bytes[6], fdc_states->bytes[7]);
        }
        fdc_states->rwstart =
            G_FDC_CHS_TO_BLOCK(fdc_states->bytes[1],
            fdc_states->bytes[2], fdc_states->bytes[3]);
        fdc_states->rwcount = fdc_states->bytes[5];
        fdc_states->rwstart +=
            g_dma_transfer(world, G_FDC_DMA_CHANNEL,
            &g_disk_data[fdc_states->rwstart * 512],
            fdc_states->rwcount * 512, 1) / 512;
        V_EVENT("DMA transfer complete, logical block at %x",
            fdc_states->rwstart);
        fdc_states->in_expect_bytes = 7;
        fdc_states->bytes[0] = G_FDC_ST0_SUCCESS;
        fdc_states->bytes[1] = 0;
        fdc_states->bytes[2] = 0;
        fdc_states->bytes[3] = G_FDC_BLOCK_TO_C(fdc_states->rwstart);
        fdc_states->bytes[4] = G_FDC_BLOCK_TO_H(fdc_states->rwstart);
        fdc_states->bytes[5] = G_FDC_BLOCK_TO_S(fdc_states->rwstart);
        fdc_states->bytes[6] = 2;
        g_pic_trigger(world, G_PIC_FDC_INT);
        break;
    case G_FDC_CMD_SPECIFY:
        V_EVENT("FDC specify %x %x", fdc_states->bytes[0],
            fdc_states->bytes[1]);
        break;
    case G_FDC_CMD_SEEK:
        V_EVENT("FDC seek %x %x", fdc_states->bytes[0], fdc_states->bytes[1]);
        fdc_states->in_expect_bytes = 2;
        fdc_states->bytes[0] = G_FDC_ST0_SUCCESS | G_FDC_ST0_SEEK;
        fdc_states->bytes[1] = fdc_states->bytes[1];
        g_pic_trigger(world, G_PIC_FDC_INT);
        break;
    case G_FDC_CMD_ST3:
        V_EVENT("FDC st3 %x", fdc_states->bytes[0]);
        fdc_states->in_expect_bytes = 1;
        fdc_states->bytes[0] = 0;       /*st3 */
        break;
    case G_FDC_CMD_RECAL:
        V_EVENT("FDC recal %x", fdc_states->bytes[0]);
        fdc_states->in_expect_bytes = 2;
        fdc_states->bytes[0] = G_FDC_ST0_SUCCESS;
        fdc_states->bytes[1] = 0;
        g_pic_trigger(world, G_PIC_FDC_INT);
        break;
    }
}

void
g_fdc_eject(struct v_world *world)
{
    struct g_dev_fdc *fdc_states = &world->gregs.dev.fdc;
    fdc_states->media_changed = 1;
}

int
g_fdc_handle_io(struct v_world *world, unsigned int dir, unsigned int address,
    void *param)
{
    unsigned char *data = param;
    struct g_dev_fdc *fdc_states = &world->gregs.dev.fdc;
    switch (address) {
    case 0x3f2:
        if (dir == G_IO_OUT) {
            V_LOG("FDC DOR %x", *data);
            if (!((*data) & G_FDC_DOR_RESET)) {
                V_VERBOSE("FDC hold reset");
                fdc_states->reset = 1;
            }
            if (((*data) & G_FDC_DOR_RESET) && fdc_states->reset) {
                fdc_states->motor[0] = 0;
                fdc_states->motor[1] = 0;
                fdc_states->motor[2] = 0;
                fdc_states->motor[3] = 0;
                fdc_states->reset = 0;
                fdc_states->dma = 0;
                fdc_states->dir = 0;
                fdc_states->in_expect_bytes = 0;
                fdc_states->out_expect_bytes = 0;
                fdc_states->in_index = 0;
                fdc_states->out_index = 0;

                fdc_states->in_expect_bytes = 1;
                fdc_states->bytes[0] = G_FDC_ST0_SUCCESS;
                g_pic_trigger(world, G_PIC_FDC_INT);
            }
            if ((*data) & G_FDC_DOR_DMA_ENABLE) {
                V_VERBOSE("DMA enable");
                fdc_states->dma = 1;
            }
            if ((*data) & G_FDC_DOR_MOTOR0) {
                V_VERBOSE("motor 0 on");
                fdc_states->motor[0] = 1;
            }
            if ((*data) & G_FDC_DOR_MOTOR1) {
                V_VERBOSE("motor 1 on");
                fdc_states->motor[0] = 1;
            }

        } else
            goto io_not_handled;
        break;
    case 0x3f7:
        if (dir == G_IO_IN) {
            V_LOG("FDC DIR in");
            *data = 0;
            if (fdc_states->media_changed) {
                fdc_states->media_changed = 0;
                *data |= G_FDC_DIR_CHANGE;
            }
        } else if (dir == G_IO_OUT) {
            V_LOG("FDC DIR out %x", *data);
        }
        break;
    case 0x3f5:
        if (dir == G_IO_IN) {
            if (fdc_states->in_index >= fdc_states->in_expect_bytes)
                goto io_not_handled;
            *data = fdc_states->bytes[fdc_states->in_index];
            fdc_states->in_index++;
            if (fdc_states->in_index >= fdc_states->in_expect_bytes) {
                fdc_states->in_index = 0;
                fdc_states->in_expect_bytes = 0;
            }
            V_LOG("FDC in: %x", *data);
        } else if (dir == G_IO_OUT) {
            if (fdc_states->out_expect_bytes > 0) {
                fdc_states->bytes[fdc_states->out_index] = *data;
                fdc_states->out_index++;
                if (fdc_states->out_index >= fdc_states->out_expect_bytes) {
                    g_fdc_run_cmd(world);
                    fdc_states->out_expect_bytes = 0;
                }
            } else {
                fdc_states->cmd = (*data) & G_FDC_CMD_MASK;
                fdc_states->out_index = 0;
                switch (fdc_states->cmd) {
                case G_FDC_CMD_DUMP:
                    V_LOG("FDC dumpreg");
                    fdc_states->in_expect_bytes = 1;
                    fdc_states->bytes[0] = 0x80;        /*identify as 8272A */
                    break;
                case G_FDC_CMD_SPECIFY:
                    V_LOG("FDC specify");
                    fdc_states->out_expect_bytes = 2;
                    break;
                case G_FDC_CMD_RECAL:
                    V_LOG("FDC recal");
                    fdc_states->out_expect_bytes = 1;
                    break;
                case G_FDC_CMD_ST3:
                    V_LOG("FDC ST3");
                    fdc_states->out_expect_bytes = 1;
                    break;
                case G_FDC_CMD_READ:
                    V_LOG("FDC read");
                    fdc_states->out_expect_bytes = 8;
                    break;
                case G_FDC_CMD_SEEK:
                    V_LOG("FDC seek");
                    fdc_states->out_expect_bytes = 2;
                    break;
                default:
                    goto io_not_handled;
                }
            }
        } else
            goto io_not_handled;
        break;
    case 0x3f4:
        if (dir == G_IO_IN) {
            *data = 0;
            if (fdc_states->in_expect_bytes) {
                *data |= (G_FDC_STAT_DIR_FDC2SYS | G_FDC_STAT_BUSYC);
            }
            if (!fdc_states->dma) {
                *data |= G_FDC_STAT_NODMA;
            }
            *data |= G_FDC_STAT_READY;
            V_LOG("FDC stat: %x", *data);
        } else
            goto io_not_handled;
        break;
    default:
      io_not_handled:
        V_ERR("unhandled FDC IO %s port %x DATA=%x",
            (dir == G_IO_IN) ? "in" : "out", address, *(unsigned char *) param);

    }
    return 0;
}
