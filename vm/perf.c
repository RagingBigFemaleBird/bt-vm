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
#include "vm/include/perf.h"
#include "host/include/perf.h"

long v_perf_counters[V_PERF_COUNT];

void
v_perf_inc(int index, long value)
{
    v_perf_counters[index] += value;
}

long
v_perf_get(int index)
{
    return v_perf_counters[index];
}

void
v_perf_init(void)
{
    int i;
    for (i = 0; i < V_PERF_COUNT; i++) {
        v_perf_counters[i] = 0;
    }
    h_perf_init();
}
