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
#include "host/include/perf.h"

long h_perf_counters[H_PERF_COUNT];
long long h_tsc_counters[H_TSC_COUNT];

void
h_perf_inc(int index, long long value)
{
    h_perf_counters[index] += value;
}

long
h_perf_get(int index)
{
    return h_perf_counters[index];
}

long long
h_tsc_get(int index)
{
    return h_tsc_counters[index];
}

void
h_perf_init(void)
{
    int i;
    for (i = 0; i < H_PERF_COUNT; i++) {
        h_perf_counters[i] = 0;
    }
}

static long long volatile last_tsc[3];

long long
h_perf_tsc_read(void)
{
    union {
        struct {
            unsigned long low;
            unsigned long high;
        } part;
        unsigned long long full;
    } ret;
    asm volatile ("rdtsc":"=a" (ret.part.low), "=d"(ret.part.high));
    return ret.full;
}

void
h_perf_tsc_begin(int tscidx)
{
    last_tsc[tscidx] = h_perf_tsc_read();
}

void
h_perf_tsc_end(int index, int tscidx)
{
    unsigned long long this_tsc = h_perf_tsc_read();
    h_tsc_counters[index] += (this_tsc - last_tsc[tscidx]);
}
