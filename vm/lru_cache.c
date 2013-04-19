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
#include "vm/include/perf.h"
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
    void *body = cache->body;
    unsigned int unit_size =
        cache->payload_size + sizeof(struct lru_cache_entry);
    int position = key32 % cache->total;
    struct lru_cache_entry *entry =
        (struct lru_cache_entry *) (body + unit_size * position);
    if (entry->key32 == key32) {
        entry->timestamp = lru_stamp();
        return entry;
    }
    return NULL;
}

struct lru_cache_entry *
lru_cache_update32(struct lru_cache *cache, unsigned int key32, int *new_entry)
{
    struct lru_cache_entry *ret;
    void *body = cache->body;
    unsigned int unit_size =
        cache->payload_size + sizeof(struct lru_cache_entry);
    int position = key32 % cache->total;
    struct lru_cache_entry *entry;
    ret = lru_cache_find32(cache, key32);
    if (ret != NULL) {
        V_VERBOSE("Found cache");
        ret->frequency++;
        return ret;
    }
    *new_entry = 1;
    entry = (struct lru_cache_entry *) (body + unit_size * position);
    if (entry->key32 != 0)
        v_perf_inc(V_PERF_CONFLICT, 1);
    entry->key32 = key32;
    entry->timestamp = lru_stamp();
    entry->flags = 0;
    entry->frequency = 1;
    return entry;
}

struct lru_cache *
lru_cache_init(unsigned int total, unsigned int payload_size)
{
    unsigned int total_size =
        sizeof(struct lru_cache) + total * (payload_size +
        sizeof(struct lru_cache_entry));
    struct lru_cache *mem = (struct lru_cache *) h_raw_malloc(total_size);
    void *body;
    unsigned int unit_size = payload_size + sizeof(struct lru_cache_entry);
    int i;
    mem->used = 0;
    mem->total = total;
    mem->payload_size = payload_size;
    body = mem->body;
    for (i = 0; i < mem->total; i++) {
        ((struct lru_cache_entry *) (body + unit_size * i))->key32 = 0;
    }
    return mem;
}
