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
#include "guest/include/mm.h"
#include "guest/include/cpu.h"
#include "host/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/logging.h"

void
g_get_sel(struct v_world *w, unsigned int sel, unsigned int *word1,
    unsigned int *word2)
{
    h_read_guest(w, w->gregs.gdt.base + (sel & 0xfff8), word1);
    h_read_guest(w, w->gregs.gdt.base + (sel & 0xfff8) + 4, word2);
}

unsigned long
g_get_ip(struct v_world *w)
{
    unsigned int base, limit;
    if (w->gregs.mode == G_MODE_REAL)
        return (w->hregs.gcpu.cs << 4) + (w->hregs.gcpu.eip);
    if (w->gregs.mode == G_MODE_PE) {
        if (w->hregs.gcpu.cs == 0x7e3) {
            void *gdt_entry = (void *) (w->hregs.gcpu.gdt.base);
            unsigned int p1, p2;
            gdt_entry += 0x7e0;
            p1 = (*(unsigned int *) gdt_entry) >> 16;
            gdt_entry += 4;
            p2 = ((*(unsigned int *) gdt_entry) & 0xf) << 16;
            return w->hregs.gcpu.eip + (p1 + p2);
        }
    }
    base = g_sel2base(w->gregs.cs, w->gregs.cshigh);
    limit = g_sel2limit(w->gregs.cs, w->gregs.cshigh);
    //todo: limit checks
    return w->hregs.gcpu.eip + base;
}

unsigned long
g_get_sp(struct v_world *w)
{
    if (w->gregs.mode == G_MODE_REAL)
        return (w->hregs.gcpu.ss << 4) + (w->hregs.gcpu.esp);
    if (w->gregs.mode == G_MODE_PE || w->gregs.mode == G_MODE_PG) {
        if (w->hregs.gcpu.cs == 0x7eb) {
            void *gdt_entry = (void *) (w->hregs.gcpu.gdt.base);
            unsigned int p1, p2;
            gdt_entry += 0x7e8;
            p1 = (*(unsigned int *) gdt_entry) >> 16;
            gdt_entry += 4;
            p2 = ((*(unsigned int *) gdt_entry) & 0xf) << 16;
            return w->hregs.gcpu.esp + (p1 + p2);
        }
        if (w->hregs.gcpu.ss & 0x4) {
            V_EVENT("LDT ss...");
            w->status = VM_PAUSED;
        } else {
            unsigned int base, limit;
            base = g_sel2base(w->gregs.ss, w->gregs.sshigh);
            limit = g_sel2limit(w->gregs.ss, w->gregs.sshigh);
            if (!(w->gregs.sshigh & 0x00400000))
                return base + (w->hregs.gcpu.esp & 0xffff);
            //todo: limit checks
            return base + w->hregs.gcpu.esp;
        }
    }
    V_ERR("NOT IMPLEMENTED getsp:\n");
    w->status = VM_PAUSED;
    return 0;
}

unsigned int
g_get_sel_ring(struct v_world *w, unsigned int sel)
{
    unsigned int word2;
    h_read_guest(w, w->gregs.gdt.base + (sel & 0xfff8) + 4, &word2);
    V_VERBOSE("sel word2 = %x", word2);
    return (word2 & 0x6000) >> 13;
}

unsigned int
g_get_current_ex_mode(struct v_world *w)
{
    if (w->gregs.cshigh & 0x400000)
        return G_EX_MODE_32;
    return G_EX_MODE_16;
}
