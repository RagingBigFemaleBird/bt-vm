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
#ifndef V_PERF_H
#define V_PERF_H

/*world switch*/
#define V_PERF_WS 0
/*break point tracing*/
#define V_PERF_BT 1
/*page fault*/
#define V_PERF_PF 2
/*privileged instructions*/
#define V_PERF_PI 3
/*BT privileged instructions*/
#define V_PERF_BT_F 4
/*BT pointer branches*/
#define V_PERF_BT_P 5

#define V_PERF_COUNT 6
void v_perf_inc(int, long);
void v_perf_init(void);
long v_perf_get(int);

#endif
