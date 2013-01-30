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
#ifndef G_DEV_DEV_H
#define G_DEV_DEV_H

#include "guest/include/dev/fb.h"
#include "guest/include/dev/kb.h"
#include "guest/include/dev/cmos.h"
#include "guest/include/dev/pic.h"
#include "guest/include/dev/fdc.h"
#include "guest/include/dev/dma.h"

struct g_devices {
    struct g_dev_fb fb;
    struct g_dev_cmos cmos;
    struct g_dev_pic pic;
    struct g_dev_fdc fdc;
    struct g_dev_dma dma;
    struct g_dev_kb kb;
};
#endif
