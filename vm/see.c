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
#include "vm/include/bt.h"
#include "guest/include/world.h"
#include "guest/include/seed.h"
#include "guest/include/bt.h"
#include "guest/include/cpu.h"
#include "guest/include/mm.h"
#include "host/include/mm.h"
#include "host/include/bt.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "vm/include/see.h"
#include "vm/include/world.h"

/* this checker has false positives
 * this is the best performance a checker can get
 *  on a single function (not caching
 *  the result of this)
 */

void
SEE_verify1(struct v_world *w, struct v_fc *fc)
{
    unsigned int i_type;
    unsigned long b_target;
    unsigned long ip = fc->addr, last_ip;
    unsigned long save_ip = g_seed_get_ip(w);
    int s, ret;
    struct addr_list *head = NULL;
    struct addr_list *p = NULL;
    g_seed_set_ip(w, ip);
    fc->SEE_checked = 1;
    fc->SEE_safe = 0;
    V_ALERT("verify %lx", ip);
    fc->SEE_ws = g_seed_initws(w);
  do_again:
    do {
        last_ip = ip;
        ret = g_seed_next(w, &i_type, &b_target, fc->SEE_ws);
        if (ret > 0) {
            V_ERR("Guest translation fault");
            return;
        }
        if (ret < 0) {
            goto reselect;
        }
        if (i_type & V_INST_UB) {
            if (i_type & V_INST_FC) {
                struct v_page *mpage = h_p2mp(w, g_v2p(w, b_target, 1));
                struct v_fc *pfc;
                if (mpage == NULL) {
                    V_ERR("phys invalid");
                    return;
                }
                pfc = v_find_fc(mpage, b_target);
                if (pfc && pfc->SEE_checked && !pfc->SEE_safe) {
                    V_ERR("checked and not safe %lx", b_target);
                    return;
                }
                if (!pfc) {
                    V_ERR("add");
                    pfc = v_add_fc(mpage, b_target, g_get_current_ex_mode(w));
                }
                if (!(pfc->SEE_checked && pfc->SEE_safe)) {
                    SEE_verify(w, pfc);
                    if (!pfc->SEE_safe)
                        return;
                }
                V_ERR("checked and safe %lx", b_target);
            } else {
                struct addr_list *al = head;
                while (al != NULL) {
                    if (al->addr == last_ip) {
                        goto reselect;
                    }
                    al = al->next;
                }
                al = h_valloc(sizeof(struct addr_list));
                al->addr = last_ip;
                al->next = head;
                al->has_bt = 0;
                head = al;
                g_seed_set_ip(w, b_target);
            }
        } else if ((i_type & V_INST_F) || (i_type & V_INST_U)) {
            V_ERR("Inst at %lx has %x", g_seed_get_ip(w), i_type);
            return;
        } else if (i_type & V_INST_CB) {
            struct addr_list *al = head;
            while (al != NULL) {
                if (al->addr == last_ip) {
                    goto reselect;
                }
                al = al->next;
            }
            al = h_valloc(sizeof(struct addr_list));
            al->addr = last_ip;
            al->bt = b_target;
            al->next = head;
            al->has_bt = 1;
            head = al;
        }
        ip = g_seed_get_ip(w);
    } while (!(i_type & V_INST_FR));
  reselect:
    p = head;
    s = 0;
    while (p != NULL) {
        if (p->has_bt) {
            s = 1;
            ip = p->bt;
            p->has_bt = 0;
            g_seed_set_ip(w, ip);
            break;
        }
        p = p->next;
    }
    if (!s) {
        g_seed_do_br(w, fc->SEE_ws);
        fc->SEE_safe = g_seed_execute(w, fc->SEE_ws);
        if (fc->SEE_safe == SEE_SAFE) {
            V_ALERT("safe %lx", fc->addr);
        }
        if (fc->SEE_safe == SEE_TOO_COMPLEX) {
            V_ALERT("too complex %lx", fc->addr);
        }
        if (fc->SEE_safe == SEE_CONTEXTUAL) {
            V_ALERT("contextual %lx", fc->addr);
        }
        g_seed_set_ip(w, save_ip);
        return;
    }
    goto do_again;
}

/* this checker has false positives
 * this is the best performance a checker can get
 *  on a single function (not caching
 *  the result of this)
 */

void
SEE_verify(struct v_world *w, struct v_fc *fc)
{
    unsigned int i_type;
    unsigned long b_target;
    unsigned long ip = fc->addr, last_ip;
    unsigned long save_ip = g_tr_get_ip(w);
    int s;
    struct addr_list *head = NULL;
    struct addr_list *p = NULL;
    g_tr_set_ip(w, ip);
    fc->SEE_checked = 1;
    fc->SEE_safe = 0;
    V_ALERT("verify %lx", ip);

  do_again:
    do {
        last_ip = ip;
        if (g_tr_next(w, &i_type, &b_target)) {
            V_ERR("Guest translation fault");
            w->status = VM_PAUSED;
            return;
        }
        if (i_type & V_INST_RAM) {
            V_ALERT("explicit return address modifier");
            return;
        }
        if (i_type & V_INST_UB) {
            if (i_type & V_INST_FC) {
                struct v_page *mpage = h_p2mp(w, g_v2p(w, b_target, 1));
                struct v_fc *pfc;
                V_VERBOSE("function call to %lx", b_target);
                if (mpage == NULL) {
                    V_ERR("phys invalid");
                    return;
                }
                pfc = v_find_fc(mpage, b_target);
                if (pfc && pfc->SEE_checked && !pfc->SEE_safe) {
                    V_ALERT("checked and not safe, %lx", b_target);
                    return;
                }
                if (!pfc) {
                    V_VERBOSE("add");
                    pfc = v_add_fc(mpage, b_target, g_get_current_ex_mode(w));
                }
                if (!(pfc->SEE_checked && pfc->SEE_safe)) {
                    SEE_verify(w, pfc);
                    if (!pfc->SEE_safe)
                        return;
                }
            } else {
                struct addr_list *al = head;
                while (al != NULL) {
                    if (al->addr == last_ip) {
                        goto reselect;
                    }
                    al = al->next;
                }
                al = h_valloc(sizeof(struct addr_list));
                al->addr = last_ip;
                al->next = head;
                al->has_bt = 0;
                head = al;
                g_tr_set_ip(w, b_target);
            }
        } else if (((i_type & V_INST_PB) && (!(i_type & V_INST_FR)))
            || (i_type & V_INST_F) || (i_type & V_INST_U)) {
            V_ALERT("Inst at %lx has %x", g_tr_get_ip(w), i_type);
            return;
        } else if (i_type & V_INST_CB) {
            struct addr_list *al = head;
            while (al != NULL) {
                if (al->addr == last_ip) {
                    goto reselect;
                }
                al = al->next;
            }
            al = h_valloc(sizeof(struct addr_list));
            al->addr = last_ip;
            al->bt = b_target;
            al->next = head;
            al->has_bt = 1;
            head = al;
        }
        ip = g_tr_get_ip(w);
    } while (!(i_type & V_INST_FR));
  reselect:
    p = head;
    s = 0;
    while (p != NULL) {
        if (p->has_bt) {
            s = 1;
            ip = p->bt;
            p->has_bt = 0;
            g_tr_set_ip(w, ip);
            break;
        }
        p = p->next;
    }
    if (!s) {
        fc->SEE_safe = SEE_SAFE;
        V_ALERT("safe %lx", fc->addr);
        g_tr_set_ip(w, save_ip);
        return;
    }
    goto do_again;
}
