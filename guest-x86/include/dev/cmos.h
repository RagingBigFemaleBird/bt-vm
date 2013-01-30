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
#ifndef G_DEV_CMOS_H
#define G_DEV_CMOS_H

#define G_CMOS_REGS 13

struct g_dev_cmos {
    unsigned int port61;
    unsigned int cmd;
    unsigned int ff;
    unsigned int latch;
    unsigned int cmos_rtc_index;
    unsigned int cmos_reg[G_CMOS_REGS];
};

#endif
