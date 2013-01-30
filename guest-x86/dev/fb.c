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
#include "guest/include/dev/fb.h"

unsigned char *
g_fb_dump_text(struct v_world *world)
{
    unsigned long i = 80 * 25;
    unsigned long j, k;
    unsigned char *c = (unsigned char *) (h_raw_malloc(i + 25));
    struct v_page *mpage;
    unsigned char *virt;
    mpage = h_p2mp(world, 0xb8000);
    if (c == NULL || mpage == NULL) {
        V_ERR("memalloc in g_fb_dump_text");
        return NULL;
    }
    virt = v_page_make_present(mpage);
    V_ERR("Framebuffer content:");
    k = 0;
    for (j = 0; j < i; j++) {
        if (j % 80 == 0)
            c[k++] = '\n';
        c[k++] = (virt[j * 2] == 0) ? (' ') : virt[j * 2];
    }
    c[k] = 0;
    V_ERR("%s", c);
    return c;
}

int
g_fb_init(struct v_world *world, unsigned long address)
{
    struct v_page *mpage;
    void *virt;
    mpage = h_p2mp(world, address);
    if (mpage == NULL) {
        V_ERR("Framebuffer init failed");
    } else {
        virt = v_page_make_present(mpage);
        h_memset(virt + 0xc00, 0x00200020, 512);
        V_ERR("Framebuffer init");
    }
    world->gregs.dev.fb.x = 0;
    world->gregs.dev.fb.y = 0;
    return 0;
}

int
g_fb_getx(struct v_world *world)
{
    return world->gregs.dev.fb.x;
}

int
g_fb_gety(struct v_world *world)
{
    return world->gregs.dev.fb.y;
}

void
g_fb_setx(struct v_world *world, int x)
{
    world->gregs.dev.fb.x = x;
}

void
g_fb_sety(struct v_world *world, int y)
{
    world->gregs.dev.fb.y = y;
}

void
g_fb_write(struct v_world *world, unsigned char cf, unsigned char cb)
{
    struct v_page *mpage;
    unsigned char *virt;
    mpage = h_p2mp(world, 0xb8000);
    if (mpage == NULL) {
        V_ERR("Framebuffer access failed");
    } else {
        virt = v_page_make_present(mpage);
        if (cf == 0x0a) {
            world->gregs.dev.fb.y++;
            if (world->gregs.dev.fb.y > 24) {
                V_ERR("Unimplemented fb scroll");
                world->gregs.dev.fb.y = 24;
            }
        } else if (cf == 0x0d) {
            world->gregs.dev.fb.x = 0;
        } else {
            virt[(world->gregs.dev.fb.y * 80 + world->gregs.dev.fb.x) * 2] = cf;
            virt[(world->gregs.dev.fb.y * 80 +
                    world->gregs.dev.fb.x) * 2 + 1] = cb;
            world->gregs.dev.fb.x++;
            if (world->gregs.dev.fb.x >= 80) {
                world->gregs.dev.fb.x = 0;
                world->gregs.dev.fb.y++;
                if (world->gregs.dev.fb.y > 24) {
                    V_ERR("Unimplemented fb scroll");
                    world->gregs.dev.fb.y = 24;
                }
            }
        }
    }
}
