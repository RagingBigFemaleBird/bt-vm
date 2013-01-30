#ifndef G_MM_H
#define G_MM_H

struct v_world;

typedef unsigned int g_addr_t;

g_addr_t g_v2p(struct v_world *, g_addr_t, unsigned int);
unsigned int g_v2attr(struct v_world *, g_addr_t);
void g_pagetable_map(struct v_world *, g_addr_t);

#endif
