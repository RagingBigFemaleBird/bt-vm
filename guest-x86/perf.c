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
#include "guest/include/perf.h"

long g_perf_counters[G_PERF_COUNT];

void
g_perf_inc(int index, long value)
{
    g_perf_counters[index] += value;
}

long
g_perf_get(int index)
{
    return g_perf_counters[index];
}

void
g_perf_init(void)
{
    int i;
    for (i = 0; i < G_PERF_COUNT; i++) {
        g_perf_counters[i] = 0;
    }
}
