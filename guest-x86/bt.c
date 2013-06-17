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
#include "vm/include/bt.h"
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "vm/include/world.h"

struct g_op_table {
    unsigned char byte;
    unsigned char *name;
    unsigned char *paras;
    unsigned int escape_to_table:3;
    unsigned int mod_rm_ext:5;
    unsigned int prefix:1;
    unsigned int i64:1;
    unsigned int o64:1;
    unsigned int d64:1;
    unsigned int f64:1;
    unsigned int cb:1;
    unsigned int ub:1;
    unsigned int pb:1;
    unsigned int f:1;
    unsigned int fc:1;
    unsigned int fr:1;
    unsigned int invalid:1;
};

static struct g_op_table g_op_table_fast1[0xff];
static struct g_op_table g_op_table_fast2[0xff];

struct g_op_table g_op_table_slow1[] = {

    {0x80, "", "EbIb", 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x81, "", "EvIz", 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x82, "", "EbIb", 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x83, "", "EvIb", 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xc0, "", "EbIb", 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xc1, "", "EvIb", 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xc6, "", "EbIb", 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xc7, "", "EvIz", 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xd0, "", "Eb", 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xd1, "", "Ev", 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xd2, "", "Eb", 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xd3, "", "Ev", 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xf6, "", "Eb", 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xf7, "", "Eb", 0, 0x13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x8f, "", "Ev", 0, 0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xfe, "", "", 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xff, "", "", 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x0f, "2byte", "", 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x00, "add", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x01, "add", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x02, "add", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x03, "add", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x04, "add al,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x05, "add rax,", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x06, "push es", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x07, "pop es", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x10, "adc", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x11, "adc", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x12, "adc", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x13, "adc", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x14, "adc al,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x15, "adc rax,", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x16, "push ss", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0},
    {0x17, "pop ss", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0},

    {0x20, "and", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x21, "and", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x22, "and", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x23, "and", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x24, "and al,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x25, "and rax,", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x26, "ES:", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x27, "DAA", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x30, "xor", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x31, "xor", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x32, "xor", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x33, "xor", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x34, "xor al,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x35, "xor rax,", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x36, "SS:", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x37, "AAA", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x40, "inc eax", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x41, "inc ecx", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x42, "inc edx", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x43, "inc ebx", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x44, "inc esp", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x45, "inc ebp", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x46, "inc esi", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x47, "inc edi", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x50, "push rax", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x51, "push rcx", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x52, "push rdx", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x53, "push rbx", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x54, "push rsp", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x55, "push rbp", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x56, "push rsi", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x57, "push rdi", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},

    {0x60, "pusha", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x61, "popa", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x62, "bound", "GvMa", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x63, "arpl", "EwGw", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x63, "movsxd", "EwGw", 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
    {0x64, "FS:", "GvEv", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x65, "GS:", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x66, "altsize", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x67, "altaddr", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x70, "jo", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x71, "jno", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x72, "jb", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x73, "jnb", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x74, "je", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x75, "jne", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x76, "jbe", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x77, "ja", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},

    {0x84, "test", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x85, "test", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x86, "xchg", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x87, "xcgh", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x90, "nop", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x91, "xchg rax, rcx", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x92, "xchg rax, rdx", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x93, "xchg rax, rbx", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x94, "xchg rax, rsp", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x95, "xchg rax, rbp", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x96, "xchg rax, rsi", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x97, "xchg rax, rdi", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xa0, "mov al,", "Ob", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa1, "mov rax", "Ov", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa2, "mov ,al", "Ob", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa3, "mov ,rax", "Ov", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa4, "movsb", "XbYb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa5, "movs", "XvYv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa6, "cmpsb", "XbYb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa7, "cmps", "XvYv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xb0, "mov al,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb1, "mov cl,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb2, "mov dl,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb3, "mov bl,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb4, "mov ah,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb5, "mov ch,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb6, "mov dh,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb7, "mov bh,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xc2, "retn", "Iw", 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0},
    {0xc3, "retn", "", 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0},
    {0xc4, "les", "GzMp", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xc5, "lds", "GzMp", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xd4, "aam", "Ib", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xd5, "aad", "Ib", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xd7, "xlat", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xe0, "loopne", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0xe1, "loope", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0xe2, "loop", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0xe3, "jecxz", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0xe4, "in al,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xe5, "in eax,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xe6, "out ,al", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xe7, "out ,eax", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xf0, "lock", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xf2, "repne", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xf3, "rep", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xf4, "hlt", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xf5, "cmc", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x08, "or", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x09, "or", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x0a, "or", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x0b, "or", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x0c, "or", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x0d, "or", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x0e, "push cs", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x18, "sbb", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x19, "sbb", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x1a, "sbb", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x1b, "sbb", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x1c, "sbb", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x1d, "sbb", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x1e, "push ds", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x1f, "pop ds", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x28, "sub", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x29, "sub", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x2a, "sub", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x2b, "sub", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x2c, "sub", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x2d, "sub", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x2e, "CS:", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x2f, "das", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x38, "cmp", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x39, "cmp", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x3a, "cmp", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x3b, "cmp", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x3c, "cmp", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x3d, "cmp", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x3e, "DS:", "", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x3f, "aas", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x48, "dec eax", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x49, "dec ecx", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x4a, "dec edx", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x4b, "dec ebx", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x4c, "dec esp", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x4d, "dec ebp", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x4e, "dec esi", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x4f, "dec edi", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x58, "pop rax", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x59, "pop rcx", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x5a, "pop rdx", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x5b, "pop rbx", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x5c, "pop rsp", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x5d, "pop rbp", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x5e, "pop rsi", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x5f, "pop rdi", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},

    {0x68, "push", "Iz", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x69, "imul", "GvEvIz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x6a, "push", "Ib", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x6b, "imul", "GvEvIb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x6c, "insb ,dx", "Yb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x6d, "ins ,dx", "Yz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x6e, "outsb dx,", "Xb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x6f, "outs dx,", "Xz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x78, "js", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x79, "jns", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x7a, "jpe", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x7b, "jnp", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x7c, "jl", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x7d, "jge", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x7e, "jle", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x7f, "jg", "Jb", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},

    {0x88, "mov", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x89, "mov", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x8a, "mov", "GbEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x8b, "mov", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x8c, "mov", "EvSw", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0x8d, "lea", "GvM", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x8e, "mov", "SwEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x98, "cbw", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x99, "cwd", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x9a, "callf", "Ap", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x9b, "wait", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x9c, "pushf", "Fv", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0},
    {0x9d, "popf", "Fv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0x9e, "sahf", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x9f, "lahf", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xa8, "test al,", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa9, "test rax,", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xaa, "stosb ,al", "Yb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xab, "stosb ,rax", "Yv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xac, "lodsb al,", "Xb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xad, "lods rax,", "Xv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xae, "scasb al,", "Xb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xaf, "scas rax,", "Xv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xb8, "mov rax,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb9, "mov rcx,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xba, "mov rdx,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbb, "mov rbx,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbc, "mov rsp,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbd, "mov rbp,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbe, "mov rsi,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbf, "mov rdi,", "Iv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xc8, "enter", "IwIb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xc9, "leave", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0xca, "retf", "Iw", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0xcb, "retf", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0xcc, "int 3", "Iw", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0xcd, "int", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xce, "into", "", 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xcf, "iret", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},

    {0xe8, "call", "Jz", 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0},
    {0xe9, "jmp", "Jz", 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0},
    {0xea, "jmpf", "AP", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0},
    {0xeb, "jmps", "Jb", 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0},
    {0xec, "in al,dx", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xed, "in eax,dx", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xee, "out dx,al", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xef, "out dx,eax", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xf8, "clc", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xf9, "stc", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xfa, "cli", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xfb, "sti", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xfc, "cld", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xfd, "std", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

};

struct g_op_table g_op_table_slow2[] = {
    {0x00, "", "", 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x01, "", "", 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xba, "", "EvIb", 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x02, "lar", "GvEw", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x03, "lsl", "GvEw", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x05, "syscall", "", 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
    {0x06, "clts", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x07, "sysret", "", 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},

    {0x20, "mov", "RdCd", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x21, "mov", "RdDd", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x22, "mov", "CdRd", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x23, "mov", "DdRd", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x30, "wrmsr", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x31, "rdtsc", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x32, "rdmsr", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x33, "rdpmc", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x34, "sysenter", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x35, "sysexit", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x37, "getsec", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x08, "invd", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x09, "wbinvd", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x0b, "ud2", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0x0d, "nop", "Ev", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0x1f, "nop", "Ev", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x80, "jo", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x81, "jno", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x82, "jb", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x83, "jnb", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x84, "je", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x85, "jne", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x86, "jbe", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x87, "ja", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},

    {0x90, "seto", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x91, "setno", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x92, "setb", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x93, "setnb", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x94, "sete", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x95, "setne", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x96, "setbe", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x97, "seta", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},

    {0xa0, "push fs", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0xa1, "pop fs", "", 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0xa2, "cpuid", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0xa3, "bt", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa4, "shld", "EvGvIb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xa5, "shld ,cl", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xb0, "cmpxchg", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb1, "cmpxchg", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb2, "lss", "GvMp", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb3, "btr", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb4, "lfs", "GvMp", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb5, "lgs", "GvMp", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb6, "movzx", "GvEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xb7, "movzx", "GvEw", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xc0, "xadd", "EbGb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xc1, "xadd", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0x88, "js", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x89, "jns", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x8a, "jp", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x8b, "jnp", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x8c, "jl", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x8d, "jnl", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x8e, "jng", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0x8f, "jg", "Jz", 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},

    {0x98, "sets", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x99, "setns", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x9a, "setp", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x9b, "setnp", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x9c, "setl", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x9d, "setnl", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x9e, "setng", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x9f, "setg", "Eb", 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},

    {0xa8, "push gs", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  /* this is NOT handled */
    {0xa9, "pop gs", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xab, "bts", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xac, "shrd", "EvGvIb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xad, "shrd ,cl", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xaf, "imul", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

    {0xbb, "btc", "EvGv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbc, "bsf", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbd, "bsr", "GvEv", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbe, "movsx", "GvEb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xbf, "movsx", "GvEw", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

struct g_modrm_table {
    unsigned char byte;
    unsigned char *name;
    unsigned char *paras;
    unsigned int escape_to_table:3;
    unsigned int mod_rm_ext:4;
    unsigned int prefix:1;
    unsigned int i64:1;
    unsigned int o64:1;
    unsigned int d64:1;
    unsigned int f64:1;
    unsigned int cb:1;
    unsigned int ub:1;
    unsigned int pb:1;
    unsigned int f:1;
    unsigned int emem:1;
    unsigned int e11:1;
    unsigned int group;
    unsigned int fc:1;
    unsigned int fr:1;
    unsigned int invalid:1;
};

struct g_modrm_table g_modrm_table1[] = {
    {0x00, "add", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
    {0x08, "or", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
    {0x10, "adc", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
    {0x18, "sbb", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
    {0x20, "and", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
    {0x28, "sub", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
    {0x30, "xor", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
    {0x38, "cmp", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},

    {0x00, "pop", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x11, 0},

    {0x00, "rol", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 0},
    {0x08, "ror", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 0},
    {0x10, "rcl", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 0},
    {0x18, "rcr", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 0},
    {0x20, "shl", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 0},
    {0x28, "shr", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 0},
    {0x38, "sar", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 0},

    {0x00, "test", "Ib", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 0},
    {0x10, "not", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 0},
    {0x18, "neg", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 0},
    {0x20, "mul al,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 0},
    {0x28, "imul al,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 0},
    {0x30, "div al,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 0},
    {0x38, "idiv al,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 0},

    {0x00, "test", "Iz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x13, 0},
    {0x10, "not", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x13, 0},
    {0x18, "neg", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x13, 0},
    {0x20, "mul rax,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x13,
        0},
    {0x28, "imul rax,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x13,
        0},
    {0x30, "div rax,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x13,
        0},
    {0x38, "idiv rax,", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0x13,
        0},

    {0x00, "inc", "Eb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4, 0},
    {0x08, "dec", "Eb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4, 0},

    {0x00, "inc", "Ev", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 5, 0},
    {0x08, "dec", "Ev", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 5, 0},
    {0x10, "calln", "Ev", 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 5, 0},
    {0x18, "callf", "Ep", 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 5, 0},
    {0x20, "jmpn", "Ev", 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 5, 0},
    {0x28, "jmpf", "Ep", 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 5, 0},
    {0x30, "push", "Ev", 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 5, 0},

    {0x00, "sldt", "Zz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 6, 0},
    {0x08, "str", "Zz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 6, 0},
    {0x10, "lldt", "Ew", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 6, 0},
    {0x18, "ltr", "Ew", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 6, 0},
    {0x20, "verr", "Ew", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 6, 0},
    {0x28, "verw", "Ew", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 6, 0},

    {0x00, "sgdt", "Ms", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 7, 0},
    {0x08, "sidt", "Ms", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 7, 0},
    {0x10, "lgdt", "Ms", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7, 0},
    {0x18, "lidt", "Ms", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7, 0},
    {0x20, "smsw", "Zz", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 7, 0},
    {0x30, "lmsw", "Ew", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 7, 0},
    {0x38, "invlpg", "Mb", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 7, 0},

    {0x20, "bt", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 8, 0},
    {0x28, "bts", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 8, 0},
    {0x30, "btr", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 8, 0},
    {0x38, "btc", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 8, 0},

    {0x00, "mov", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 11, 0},
};

int g_tr_fault = 0;

void
g_tr_init(void)
{
    int i;
    for (i = 0; i < 0xff; i++) {
        g_op_table_fast1[i].invalid = 1;
        g_op_table_fast2[i].invalid = 1;
    }
    for (i = 0; i < sizeof(g_op_table_slow1) / sizeof(struct g_op_table); i++) {
        h_memcpy(&g_op_table_fast1[g_op_table_slow1[i].byte],
            &g_op_table_slow1[i], sizeof(struct g_op_table));
    }
    for (i = 0; i < sizeof(g_op_table_slow2) / sizeof(struct g_op_table); i++) {
        h_memcpy(&g_op_table_fast2[g_op_table_slow2[i].byte],
            &g_op_table_slow2[i], sizeof(struct g_op_table));
    }
}

void
g_tr_set_ip(struct v_world *world, unsigned long ip)
{
    struct v_page *mpage;
    void *virt;
    mpage = h_p2mp(world, g_v2p(world, ip, 1));
    g_tr_fault = 0;
    if (mpage == NULL) {
        g_tr_fault = 1;
        return;
    }
    world->gregs.disasm_ip = ip;
    virt = v_page_make_present(mpage);
    world->gregs.disasm_vip =
        (unsigned char *) ((unsigned int) (virt) + (ip & H_POFF_MASK));
}

unsigned long
g_tr_get_ip(struct v_world *world)
{
    return world->gregs.disasm_ip;
}

void
g_tr_debug_dump(struct v_world *world, unsigned int minus, unsigned int plus)
{
    unsigned char *i = world->gregs.disasm_vip - minus;
    V_VERBOSE("dump starting %x to %x", world->gregs.disasm_ip - minus,
        world->gregs.disasm_ip + plus);
    for (; i < world->gregs.disasm_vip + plus; i++) {
        V_VERBOSE("%x: %x", (unsigned int) i, *i);
    }
}

static unsigned char
g_tr_nextbyte(struct v_world *world)
{
    unsigned char ret = *world->gregs.disasm_vip;
    world->gregs.disasm_ip++;
    world->gregs.disasm_vip++;
    if ((world->gregs.disasm_ip & H_POFF_MASK) == 0x0) {
        g_tr_set_ip(world, world->gregs.disasm_ip);
    }
    return ret;
}

static unsigned int
g_get_op_size(unsigned char byte, unsigned int mode)
{
    switch (byte) {
    case 'b':
        return 1;
    case 'c':
        return (mode == G_EX_MODE_16) ? 1 : 2;
    case 'd':
        return 4;
    case 'p':
        return (mode == G_EX_MODE_16) ? 4 : 6;
    case 'q':
        return 8;
    case 's':
        return 6;
    case 'v':
        return (mode == G_EX_MODE_16) ? 2 : 4;
    case 'w':
        return 2;
    case 'z':
        return (mode == G_EX_MODE_16) ? 2 : 4;
    }
    return 0;
}

int
g_tr_next(struct v_world *world, unsigned int *type, unsigned long *b_target)
{
    volatile unsigned int has_modrm = 0;
    unsigned int i, has_sib = 0;
    unsigned int disp = 0;
    unsigned int imm = 0;
    unsigned int is_rjmp = 0;
    unsigned int joff = 0;
    unsigned int is_modrm_ext = 0;
    unsigned int check_sib_special = 0;
    unsigned char *para = NULL;
    struct g_modrm_table *use_this_table = NULL;
    int jmps;
    unsigned char byte, byte1 = 0;
    unsigned int mode = g_get_current_ex_mode(world);
    unsigned int addr_mode = mode, op_mode = mode;
    unsigned int saved_ip = g_tr_get_ip(world);
    struct g_op_table *table;
    int j;
    int modrm_count = sizeof(g_modrm_table1) / sizeof(struct g_modrm_table);
    unsigned int isSS = 0;
    *type = V_INST_U;
    byte = g_tr_nextbyte(world);
    table = g_op_table_fast1;
    if (g_tr_fault) {
        g_tr_set_ip(world, saved_ip);
        return 1;
    }
  skip_prefix:

    i = byte;
    if (table[i].invalid) {
        V_ERR("unrecognized opcode [%x]%x", byte1, byte);
        *type = V_INST_U;
        g_tr_set_ip(world, saved_ip);
        return 0;
    }
    byte1 = byte;
//                      printk("(%x)%s ", byte, table[i].name);
    if (table[i].escape_to_table == 1) {
        byte = g_tr_nextbyte(world);
        table = g_op_table_fast2;
        goto skip_prefix;
    }
    V_VERBOSE("decode %lx: %x %s", g_tr_get_ip(world) - 1, i, table[i].name);
    if (table[i].prefix) {
        if (byte == 0x67) {
            if (mode == G_EX_MODE_32)
                addr_mode = G_EX_MODE_16;
            else if (mode == G_EX_MODE_16)
                addr_mode = G_EX_MODE_32;
        }
        if (byte == 0x66) {
            if (mode == G_EX_MODE_32)
                op_mode = G_EX_MODE_16;
            else if (mode == G_EX_MODE_16)
                op_mode = G_EX_MODE_32;
        }
        byte = g_tr_nextbyte(world);
        if (g_tr_fault) {
            g_tr_set_ip(world, saved_ip);
            return 1;
        }
        goto skip_prefix;
    }
    para = table[i].paras;
    if (table[i].mod_rm_ext) {
        unsigned int s, reg, mod;
        is_modrm_ext = table[i].mod_rm_ext;
        byte = g_tr_nextbyte(world);
        if (g_tr_fault) {
            g_tr_set_ip(world, saved_ip);
            return 1;
        }
        mod = (byte & 0xc0) >> 6;
        reg = (byte & 0x38);
        for (s = 0; s < modrm_count; s++) {
            if (g_modrm_table1[s].group == is_modrm_ext
                && g_modrm_table1[s].byte == reg
                && ((mod != 3 && g_modrm_table1[s].emem)
                    || (mod == 3 && g_modrm_table1[s].e11))) {
                if (g_modrm_table1[s].paras != NULL) {
                    para = g_modrm_table1[s].paras;
                    use_this_table = &g_modrm_table1[s];
                }
                break;
            }
        }
    }
    j = 0;
    while (para[j] != 0) {
        switch (para[j]) {
        case 'E':
        case 'Q':
        case 'M':
            has_modrm = 1;
            break;
        case 'C':
        case 'D':
        case 'R':
        case 'S':
            has_modrm = 1;
            break;
        case 'J':
            is_rjmp = 1;
        case 'I':
            imm = g_get_op_size(para[j + 1], op_mode);
            break;
        case 'O':
            imm = (op_mode == G_EX_MODE_16) ? 2 : 4;
            break;
        case 'A':
            disp = (op_mode == G_EX_MODE_16) ? 2 : 4;
            imm = 2;
            break;
        }
        j += 2;
    }
    if (has_modrm || is_modrm_ext) {
        unsigned char mod, rm, reg;
        if (is_modrm_ext == 0) {
            byte = g_tr_nextbyte(world);
            if (g_tr_fault) {
                g_tr_set_ip(world, saved_ip);
                return 1;
            }
        }
        mod = (byte & 0xc0) >> 6;
        rm = (byte & 0x7);
        reg = (byte & 0x38) >> 3;
        V_VERBOSE("mod %x, reg %x, rm %x, mode %x", mod, reg, rm, addr_mode);
        if (byte1 == 0x8b && reg == 4) {
            isSS = 1;
            V_VERBOSE("encountered RAM");
        }
        if (addr_mode == G_EX_MODE_16) {
            if (mod == 1)
                disp = 1;
            if (mod == 2)
                disp = 2;
            if (mod == 0 && rm == 6)
                disp = 2;
        } else if (addr_mode == G_EX_MODE_32) {
            if (mod == 1)
                disp = 1;
            if (mod == 2)
                disp = 4;
            if (mod == 0 && rm == 5)
                disp = 4;
            if (mod < 3 && rm == 4) {
                has_sib = 1;
                if (mod == 0) {
                    check_sib_special = 1;
                }
            }
        }
    }
    if (has_sib)
        byte = g_tr_nextbyte(world);
    if (has_sib && check_sib_special && ((byte & 0x7) == 5)) {
        disp = 4;
    }
    for (j = 0; j < disp; j++)
        byte = g_tr_nextbyte(world);
    if (g_tr_fault) {
        g_tr_set_ip(world, saved_ip);
        return 1;
    }
    joff = 0;
    jmps = 0;
    for (j = 0; j < imm; j++) {
        byte = g_tr_nextbyte(world);
        if (is_rjmp)
            joff += (byte << (j * 8));
    }
    if (j == 1) {
        jmps = (int) (*(char *) (&joff));
    }
    if (j == 2) {
        jmps = (int) (*(short *) (&joff));
    }
    if (is_rjmp) {
//                              printk("ip %x, jmp offset %x", world->gregs.disasm_ip, jmps);
        *b_target =
            world->gregs.disasm_ip + ((j ==
                4) ? *(unsigned int *) (&joff) : jmps);
    }
    if (use_this_table != NULL) {
        if (use_this_table->cb)
            *type = V_INST_CB;
        else if (use_this_table->ub)
            *type = V_INST_UB;
        else if (use_this_table->f)
            *type = V_INST_F;
        else if (use_this_table->pb)
            *type = V_INST_PB;
        else
            *type = V_INST_I;
        if (isSS)
            *type |= V_INST_RAM;
        if (use_this_table->fc)
            *type |= V_INST_FC;
        else if (use_this_table->fr)
            *type |= V_INST_FR;
        return 0;
    }
    if (table[i].cb)
        *type = V_INST_CB;
    else if (table[i].ub)
        *type = V_INST_UB;
    else if (table[i].f)
        *type = V_INST_F;
    else if (table[i].pb)
        *type = V_INST_PB;
    else
        *type = V_INST_I;
    if (isSS)
        *type |= V_INST_RAM;
    if (table[i].fc)
        *type |= V_INST_FC;
    else if (table[i].fr)
        *type |= V_INST_FR;

//      printk("%s%sdisp%ximm%x",has_modrm?"m":"",has_sib?"s":"", disp, imm);
    return 0;
}

unsigned int
g_get_poi_key(struct v_world *w)
{
    return w->gregs.cr3;
}
