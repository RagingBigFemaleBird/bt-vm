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
#include "guest/include/dev/pic.h"
#include "host/include/perf.h"

void
g_pic_init(struct v_world *w)
{
    w->gregs.dev.pic.d0icw1 = w->gregs.dev.pic.d1icw1 =
        w->gregs.dev.pic.d0IRQ_mask = w->gregs.dev.pic.d1IRQ_mask =
        w->gregs.dev.pic.d0icw_expect = w->gregs.dev.pic.d1icw_expect =
        w->gregs.dev.pic.d0IRQ_srv = w->gregs.dev.pic.d1IRQ_srv =
        w->gregs.dev.pic.d1IRQ_req = 0;
    w->gregs.dev.pic.d0IRQ_req = 1;
    w->gregs.dev.pic.d0IRQ = 8;
    w->gregs.dev.pic.d1IRQ = 0xa0;
}

void
g_pic_serve(struct v_world *w)
{
    if (w->gregs.mode != G_MODE_REAL /* note: hack */  &&
        v_int_enabled(w) && (!(w->gregs.dev.pic.d0IRQ_mask & G_PIC_KB))
        && (w->gregs.dev.pic.d0IRQ_req & G_PIC_KB)
        && (!(w->gregs.dev.pic.d0IRQ_srv))) {
        V_EVENT("trigger kb interrupt");
        w->status = VM_RUNNING;
        w->gregs.dev.pic.d0IRQ_srv |= G_PIC_KB;
        w->gregs.has_errorc = 0;
        h_inject_int(w, w->gregs.dev.pic.d0IRQ + G_PIC_KB_INT);
        return;
    }
    if (w->gregs.mode != G_MODE_REAL /* note: hack */  &&
        v_int_enabled(w) && (!(w->gregs.dev.pic.d0IRQ_mask & G_PIC_FDC))
        && (w->gregs.dev.pic.d0IRQ_req & G_PIC_FDC)
        && (!(w->gregs.dev.pic.d0IRQ_srv))) {
        V_EVENT("trigger fdc interrupt");
        w->status = VM_RUNNING;
        w->gregs.dev.pic.d0IRQ_srv |= G_PIC_FDC;
        w->gregs.has_errorc = 0;
        h_inject_int(w, w->gregs.dev.pic.d0IRQ + G_PIC_FDC_INT);
        return;
    }
    if (w->gregs.dev.pic.expected_jiffies + 0x2000000LL > w->total_tsc) {
        return;
    }
    w->status = VM_RUNNING;
    if (w->gregs.mode != G_MODE_REAL /* note: hack */  &&
        v_int_enabled(w) && (!(w->gregs.dev.pic.d0IRQ_mask & G_PIC_TIMER))
        && (w->gregs.dev.pic.d0IRQ_req & G_PIC_TIMER)
        && (!(w->gregs.dev.pic.d0IRQ_srv))) {
        V_EVENT("trigger timer interrupt");
        w->gregs.dev.pic.expected_jiffies = w->total_tsc;
        w->gregs.dev.pic.d0IRQ_srv |= G_PIC_TIMER;
        w->gregs.has_errorc = 0;
        h_inject_int(w, w->gregs.dev.pic.d0IRQ + G_PIC_TIMER_INT);
        h_perf_inc(H_PERF_TICK, 1);
        return;
    }
}

void
g_pic_trigger(struct v_world *w, int irq_no)
{
    w->gregs.dev.pic.d0IRQ_req |= (1 << irq_no);
    V_LOG("PIC stats: req %x srv %x mask %x", w->gregs.dev.pic.d0IRQ_req,
        w->gregs.dev.pic.d0IRQ_srv, w->gregs.dev.pic.d0IRQ_mask);
}
