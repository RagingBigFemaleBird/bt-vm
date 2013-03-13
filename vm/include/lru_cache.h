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
#ifndef LRU_CACHE_H
#define LRU_CACHE_H

struct lru_cache_entry {
    union {
        unsigned int key32;
        struct {
            unsigned int key64_low;
            unsigned int key64_high;
        } key64;
    };
    unsigned int timestamp;
    unsigned int flags;
    unsigned int frequency;
    unsigned char payload[0];
};

struct lru_cache {
    unsigned int used;
    unsigned int total;
    unsigned int payload_size;
    struct lru_cache_entry body[0];
};

struct lru_cache_entry *lru_cache_find32(struct lru_cache *, unsigned int);
struct lru_cache_entry *lru_cache_update32(struct lru_cache *, unsigned int,
    int *);
struct lru_cache *lru_cache_init(unsigned int, unsigned int);

#endif
