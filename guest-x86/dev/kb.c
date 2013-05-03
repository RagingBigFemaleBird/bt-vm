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
#include "guest/include/dev/kb.h"

struct g_kb_scancode {
    unsigned int key;
    unsigned char scan;
    unsigned char shift;
    unsigned char ctrl;
    unsigned char alt;
};
static int req_shift = 0x100;
static int req_ctrl = 0x200;
static int req_alt = 0x400;

static struct g_kb_scancode scancode[] = {
    {'1', 2, 0, 0, 0},
    {'2', 3, 0, 0, 0},
    {'3', 4, 0, 0, 0},
    {'4', 5, 0, 0, 0},
    {'5', 6, 0, 0, 0},
    {'6', 7, 0, 0, 0},
    {'7', 8, 0, 0, 0},
    {'8', 9, 0, 0, 0},
    {'9', 0xa, 0, 0, 0},
    {'0', 0xb, 0, 0, 0},
    {'!', 2, 1, 0, 0},
    {'@', 3, 1, 0, 0},
    {'#', 4, 1, 0, 0},
    {'$', 5, 1, 0, 0},
    {'%', 6, 1, 0, 0},
    {'^', 7, 1, 0, 0},
    {'&', 8, 1, 0, 0},
    {'*', 9, 1, 0, 0},
    {'(', 0xa, 1, 0, 0},
    {')', 0xb, 1, 0, 0},
    {'-', 0xc, 0, 0, 0},
    {'=', 0xd, 0, 0, 0},
    {'q', 0x10, 0, 0, 0},
    {'w', 0x11, 0, 0, 0},
    {'e', 0x12, 0, 0, 0},
    {'r', 0x13, 0, 0, 0},
    {'t', 0x14, 0, 0, 0},
    {'y', 0x15, 0, 0, 0},
    {'u', 0x16, 0, 0, 0},
    {'i', 0x17, 0, 0, 0},
    {'o', 0x18, 0, 0, 0},
    {'p', 0x19, 0, 0, 0},
    {'[', 0x1a, 0, 0, 0},
    {']', 0x1b, 0, 0, 0},
    {'a', 0x1e, 0, 0, 0},
    {'s', 0x1f, 0, 0, 0},
    {'d', 0x20, 0, 0, 0},
    {'f', 0x21, 0, 0, 0},
    {'g', 0x22, 0, 0, 0},
    {'h', 0x23, 0, 0, 0},
    {'j', 0x24, 0, 0, 0},
    {'k', 0x25, 0, 0, 0},
    {'l', 0x26, 0, 0, 0},
    {';', 0x27, 0, 0, 0},
    {'z', 0x2c, 0, 0, 0},
    {'x', 0x2d, 0, 0, 0},
    {'c', 0x2e, 0, 0, 0},
    {'v', 0x2f, 0, 0, 0},
    {'b', 0x30, 0, 0, 0},
    {'n', 0x31, 0, 0, 0},
    {'m', 0x32, 0, 0, 0},
    {'/', 0x35, 0, 0, 0},
    {'\n', 0x1c, 0, 0, 0},
    {0x107, 0xe, 0, 0, 0},      /* backspace */
    {' ', 0x39, 0, 0, 0},
    {'.', 0x34, 0, 0, 0},
    {',', 0x33, 0, 0, 0},
    {'>', 0x34, 1, 0, 0},
    {'<', 0x33, 1, 0, 0},
    {'\\', 43, 0, 0, 0},
    {'|', 43, 1, 0, 0},
};

static unsigned int
g_kb_findsc(int key)
{
    int i = 0;
    for (i = 0; i < sizeof(scancode) / sizeof(struct g_kb_scancode); i++) {
        if (scancode[i].key == key) {
            return scancode[i].scan +
                ((scancode[i].shift) ? req_shift : 0) +
                ((scancode[i].ctrl) ? req_ctrl : 0) +
                ((scancode[i].alt) ? req_alt : 0);
        }
    }
    return 0;
}

void
g_kb_init(struct v_world *w)
{
    w->gregs.dev.kb.head = w->gregs.dev.kb.tail = w->gregs.dev.kb.cmd2 = 0;
}

static void
kb_push_buffer(struct v_world *w, unsigned char x)
{
    if (w->gregs.dev.kb.head == w->gregs.dev.kb.tail + 1
        || (w->gregs.dev.kb.head == 0
            && w->gregs.dev.kb.tail + 1 == G_DEV_KB_BUFFER_SIZE)) {
        /* we are full */
        return;
    }
    w->gregs.dev.kb.buffer[w->gregs.dev.kb.tail] = x;
    w->gregs.dev.kb.tail++;
    if (w->gregs.dev.kb.tail >= G_DEV_KB_BUFFER_SIZE) {
        w->gregs.dev.kb.tail = 0;
    }
}

void
g_inject_key(struct v_world *w, unsigned int k)
{
    unsigned int key = g_kb_findsc(k);
    if (key == 0) {
        return;
    }
    if (key & req_shift) {
        kb_push_buffer(w, 0x2a);
    }

    kb_push_buffer(w, key);
    kb_push_buffer(w, key + G_DEV_KB_KEY_RELEASE);

    if (key & req_shift) {
        kb_push_buffer(w, 0x2a + G_DEV_KB_KEY_RELEASE);
    }

    g_pic_trigger(w, G_PIC_KB_INT);
}

int
g_kb_handle_io(struct v_world *world, unsigned int dir, unsigned int address,
    void *param)
{
    unsigned char *data = param;
    struct g_dev_kb *kb = &world->gregs.dev.kb;
    switch (address) {
    case 0x60:
        if (dir == G_IO_IN) {
            V_LOG("KB data in");
            *(unsigned char *) param = kb->buffer[kb->head];
            kb->head++;
            if (kb->head >= G_DEV_KB_BUFFER_SIZE) {
                kb->head = 0;
            }
        } else if (dir == G_IO_OUT) {
            V_LOG("KB cmd out %x", *data);
            if (kb->cmd2) {
                if (kb->cmd2 == G_DEV_KB_CMD_LED) {
                    /*ignoring for now */
                    kb->cmd2 = 0;
                    kb_push_buffer(world, G_DEV_KB_ACK);
                    g_pic_trigger(world, G_PIC_KB_INT);
                } else
                    goto io_not_handled;
            } else {
                switch (*data) {
                case G_DEV_KB_CMD_RESET:
                    g_pic_trigger(world, G_PIC_KB_INT);
                    break;
                case G_DEV_KB_CMD_LED:
                    kb->cmd2 = G_DEV_KB_CMD_LED;
                    break;
                default:
                    goto io_not_handled;
                }
                kb_push_buffer(world, G_DEV_KB_ACK);
                g_pic_trigger(world, G_PIC_KB_INT);
            }
        } else
            goto io_not_handled;
        break;
    case 0x64:
        if (dir == G_IO_IN) {
            V_LOG("KB stat");
            if (kb->head == kb->tail) {
                *data = 0;
            } else {
                *data = G_DEV_KB_STAT_OUT_READY;
            }
        } else
            goto io_not_handled;
        break;
    default:
      io_not_handled:
        V_ALERT("unhandled KB IO %s port %x DATA=%x",
            (dir == G_IO_IN) ? "in" : "out", address, *(unsigned char *) param);

    }
    return 0;
}

int
g_kb_has_key(struct v_world *world)
{
    struct g_dev_kb *kb = &world->gregs.dev.kb;
    if (kb->head == kb->tail) {
        return 0;
    }
    return 1;
}

int
g_kb_get_key(struct v_world *world)
{
    struct g_dev_kb *kb = &world->gregs.dev.kb;
    if (kb->head == kb->tail) {
        return 0;
    }
    kb->head = kb->tail;        /* clear entire key buffer for now */
    /* anything will return ENTER key for now */
    return 0x1c0d;
}
