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
#ifndef G_DEV_KB_H
#define G_DEV_KB_H

#define G_DEV_KB_BUFFER_SIZE 64
#define G_DEV_KB_STAT_OUT_READY 1
#define G_DEV_KB_CMD_CTR 0x20
#define G_DEV_KB_CMD_RESET 0xae
#define G_DEV_KB_CMD_LED 0xed
#define G_DEV_KB_ACK 0xfa
#define G_DEV_KB_KEY_RELEASE 0x80

struct v_world;

struct g_dev_kb {
    unsigned int buffer[G_DEV_KB_BUFFER_SIZE];
    unsigned int head;
    unsigned int tail;
    unsigned int cmd2;
};

void g_inject_key(struct v_world *, unsigned int);
void g_kb_init(struct v_world *);
int g_kb_handle_io(struct v_world *, unsigned int, unsigned int, void *);
int g_kb_has_key(struct v_world *);
int g_kb_get_key(struct v_world *);

#endif
