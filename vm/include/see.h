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
#ifndef V_SEE_H
#define V_SEE_H

#define SEE_TOO_COMPLEX 0
#define SEE_UNSAFE 0
#define SEE_CONTEXTUAL 1
#define SEE_SAFE 2

struct addr_list {
    unsigned long addr;
    unsigned long bt;
    int has_bt;
    struct addr_list *next;
};

struct v_fc {
    unsigned long addr;
    unsigned char SEE_checked;
    unsigned char SEE_safe;
    unsigned int ex_mode;
    void *SEE_ws;
    struct v_fc *next_fc;
};

void SEE_verify(struct v_world *, struct v_fc *);

struct v_expression {
    int sym;                    //symbol number: this is a symbol and all other fields are invalid
    int p_exp;                  //pointer to another expression
    long c;                     //otherwise, a constant
    unsigned int op;            //operation (0 if end node)
    int p_exp2;                 //pointer to another expression (second oprand)
    long c2;                    //otherwise, a constant
};

struct v_cond {
    int p_exp;                  //expression
    unsigned int cond;
};

struct v_mem {
    int size;
    int idx_p_exp;              //indexed by this expression
    int val_p_exp;              //value by this expression
    int sym;
    int access;
};

struct v_reg {
    int p_exp;                  //pointer to exp
};

struct v_assignment {
    unsigned int target;
    long targetmem;
    unsigned int op1;
    long op1mem;
    unsigned int op2;
    long op2mem;
    unsigned int op;
    unsigned char update_flag;
};

struct v_control {
    unsigned int cond;
    unsigned int target;
    unsigned long target_ip;
    unsigned int id;
};

struct v_instruction {
    unsigned int type;
    unsigned long ip;
    union {
        struct v_assignment a;
        struct v_control c;
    } inst;
};

#endif
