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
#ifndef H_PERF_H
#define H_PERF_H

/*I/O instructions*/
#define H_PERF_IO 0
#define H_PERF_TICK 1
#define H_PERF_PI_INTSTATE 2
#define H_PERF_CACHE 3
#define H_PERF_TLB 4

#define H_PERF_TSC_GUEST 0
#define H_PERF_TSC_BT 1
#define H_PERF_TSC_PF 2
#define H_PERF_TSC_PI 3
#define H_PERF_TSC_MAPPING 4
#define H_PERF_TSC_PLAN 5
#define H_PERF_TSC_TRANSLATE 6
#define H_PERF_TSC_MINUS_FI 7
#define H_PERF_TSC_TREE 8
#define H_PERF_TSC_CACHE 9

#define H_PERF_COUNT 5
#define H_TSC_COUNT 10

void h_perf_inc(int, long long);
void h_perf_init(void);
long h_perf_get(int);
void h_perf_tsc_begin(int);
void h_perf_tsc_end(int, int);
long long h_tsc_get(int);
long long h_perf_tsc_read(void);

#endif
