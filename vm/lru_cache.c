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
#include "host/include/mm.h"
#include "vm/include/lru_cache.h"
#include "vm/include/logging.h"
#include <linux/mm.h>

static int timestamp = 0;
static inline unsigned int
lru_stamp(void)
{
    return timestamp++;
}

struct lru_cache_entry *
lru_cache_find32(struct lru_cache *cache, unsigned int key32)
{
    int i;
    void *body = cache->body;
    unsigned int unit_size =
        cache->payload_size + sizeof(struct lru_cache_entry);
    for (i = 0; i < cache->used; i++) {
        if (((struct lru_cache_entry *) (body + unit_size * i))->key32 == key32) {
            ((struct lru_cache_entry *) (body + unit_size * i))->timestamp =
                lru_stamp();
            return ((struct lru_cache_entry *) (body + unit_size * i));
        }
    }
    return NULL;
}

struct lru_cache_entry *
lru_cache_update32(struct lru_cache *cache, unsigned int key32)
{
    int i, replace, replace_ts;
    struct lru_cache_entry *ret;
    void *body = cache->body;
    unsigned int unit_size =
        cache->payload_size + sizeof(struct lru_cache_entry);
    ret = lru_cache_find32(cache, key32);
    if (ret != NULL) {
        V_VERBOSE("Found cache");
        ret->frequency++;
        return ret;
    }
    if (cache->used < cache->total) {
        ((struct lru_cache_entry *) (body + unit_size * cache->used))->key32 =
            key32;
        ((struct lru_cache_entry *) (body +
                unit_size * cache->used))->timestamp = lru_stamp();
        ((struct lru_cache_entry *) (body + unit_size * cache->used))->flags =
            0;
        ((struct lru_cache_entry *) (body +
                unit_size * cache->used))->frequency = 1;
        V_VERBOSE("Used cache %x", cache->used);
        cache->used++;
        return (struct lru_cache_entry *) (body + unit_size * (cache->used -
                1));
    }
    replace = 0;
    replace_ts = 0xffffffff;
    for (i = 0; i < cache->total; i++) {
        V_VERBOSE("Timestamp for %x is %x", i,
            ((struct lru_cache_entry *) (body + unit_size * i))->timestamp);
        if (((struct lru_cache_entry *) (body + unit_size * i))->timestamp <
            replace_ts) {
            replace_ts =
                ((struct lru_cache_entry *) (body + unit_size * i))->timestamp;
            replace = i;
        }
    }
    V_VERBOSE("Replaced cache %x", replace);
    ((struct lru_cache_entry *) (body + unit_size * replace))->key32 = key32;
    ((struct lru_cache_entry *) (body + unit_size * replace))->timestamp =
        lru_stamp();
    ((struct lru_cache_entry *) (body + unit_size * replace))->flags = 0;
    ((struct lru_cache_entry *) (body + unit_size * replace))->frequency = 1;
    return (struct lru_cache_entry *) (body + unit_size * replace);
}

struct lru_cache *
lru_cache_init(unsigned int total, unsigned int payload_size)
{
    unsigned int total_size =
        sizeof(struct lru_cache) + total * (payload_size +
        sizeof(struct lru_cache_entry));
    struct lru_cache *mem = (struct lru_cache *) h_raw_malloc(total_size);
    mem->used = 0;
    mem->total = total;
    mem->payload_size = payload_size;
    return mem;
}
