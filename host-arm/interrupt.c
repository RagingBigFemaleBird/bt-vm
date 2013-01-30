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
#include "host/include/interrupt.h"
#include "vm/include/logging.h"

unsigned char
h_in_port(unsigned int port_no)
{
    return 0;
}

void
h_out_port(unsigned int port_no, unsigned char byte)
{
}

void
h_init_int(void)
{
}

void
h_int_prepare(void)
{
}

void
h_int_restore(void)
{
}
