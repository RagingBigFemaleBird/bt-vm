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
#include "guest/include/world.h"
#include "guest/include/cpu.h"
#include "guest/include/mm.h"
#include "vm/include/world.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "guest/include/setup.h"
#include <../arch/arm/include/asm/cacheflush.h>

unsigned char *g_disk_data = NULL;
unsigned long g_disk_length = 0;
unsigned char *g_initrd_data = NULL;
unsigned long g_initrd_length = 0;
unsigned long initrd_len;

void
g_inject_key(struct v_world *w, unsigned int key)
{
}

static void
  setup_tags(struct v_world *world, unsigned int start);
extern unsigned char *g_disk_data;
extern unsigned long g_disk_length;
int
g_init_image(struct v_world *world, g_addr_t address)
{
    V_ERR("Initializing image... %x", g_disk_length);
    unsigned int initrd_addr = 0x3000000;
    initrd_len = g_initrd_length;
    while (g_initrd_length != 0 && g_initrd_data != NULL) {
        struct v_page *mpage;
        void *virt;
        int to_copy =
            (g_initrd_length > H_PAGE_SIZE) ? H_PAGE_SIZE : g_initrd_length;
        mpage = h_p2mp(world, initrd_addr);
        virt = v_page_make_present(mpage);

        memcpy(virt, g_initrd_data, to_copy);
        initrd_addr += to_copy;
        g_initrd_data += to_copy;
        g_initrd_length -= to_copy;
    }
    while (g_disk_length != 0 && g_disk_data != NULL) {
        struct v_page *mpage;
        void *virt;
        int to_copy =
            (g_disk_length > H_PAGE_SIZE) ? H_PAGE_SIZE : g_disk_length;
        mpage = h_p2mp(world, address);
        virt = v_page_make_present(mpage);

        memcpy(virt, g_disk_data, to_copy);
        address += to_copy;
        g_disk_data += to_copy;
        g_disk_length -= to_copy;
/*        V_LOG("Boot data %lx, %lx, %lx, %lx", g_disk_data[0],
            g_disk_data[1], g_disk_data[2], g_disk_length);
        V_LOG("Boot sector copying %x, %x, %x, %x\n",
            *(unsigned int *) (virt + 0xc00),
            *(unsigned int *) (virt + 0xc01),
            *(unsigned int *) (virt + 0xc02), *(unsigned int *) (virt + 0xc03));
*/
    }
    flush_cache_all();
    v_page_unset_io(world, G_PA_BASE + 0x8000);
    setup_tags(world, G_PA_BASE + 0x100);
    flush_cache_all();
    return 0;
}

static int cheat_uncompress = 0;

int
g_omap_id_op(struct v_world *world, g_addr_t address)
{
    unsigned int ip = g_get_ip(world);
    int reg = 0;
    unsigned int *base = &world->hregs.gcpu.r0;
    if ((ip & 0x3) == 0) {
        void *virt;
        struct v_page *mpage;
        g_addr_t phys;
        unsigned int inst;

        phys = g_v2p(world, ip, 1);
        mpage = h_p2mp(world, phys);
        virt = v_page_make_present(mpage);
        inst = *(unsigned int *) ((unsigned int) virt + (ip & H_POFF_MASK));
        V_VERBOSE("inst is %x", inst);
        reg = (inst & 0xf000) >> 12;
    } else {
        V_ERR("Guest misaligned ip");
        world->status = VM_PAUSED;
        return 0;
    }

    world->hregs.gcpu.pc += 4;
    address = address & H_POFF_MASK;
    if (address == 0x204) {
        *(base + reg) = 0x2b942000;     //2b852 is omap4 4460. this is omap5
        return 1;
    }
    V_ERR("Something other than ID occurred at %x", address);
    world->status = VM_PAUSED;
    return 0;
}

static int currentposx = 0;
static int currentposy = 0;

int
g_serial_op(struct v_world *world, g_addr_t address)
{
    unsigned int ip = g_get_ip(world);
    int reg = 0;
    unsigned int *base = &world->hregs.gcpu.r0;
    if ((ip & 0x3) == 0) {
        void *virt;
        struct v_page *mpage;
        g_addr_t phys;
        unsigned int inst;

        phys = g_v2p(world, ip, 1);
        mpage = h_p2mp(world, phys);
        virt = v_page_make_present(mpage);
        inst = *(unsigned int *) ((unsigned int) virt + (ip & H_POFF_MASK));
        V_VERBOSE("serial inst is %x @ %x", inst, address);
        reg = (inst & 0xf000) >> 12;
    } else {
        V_ERR("Guest misaligned ip");
        world->status = VM_PAUSED;
        return 0;
    }

    world->hregs.gcpu.pc += 4;
    address = address & H_POFF_MASK;
    if (address == 0x018) {
        *(base + reg) = 0;
        return 1;
    }
/*    if (address == ((G_SERIAL_PAGE + G_SERIAL_OMAP) & H_POFF_MASK)) {
        *(base + reg) = 0;
        return 1;
    }
    if (address == ((G_SERIAL_PAGE + G_SERIAL_LSR) & H_POFF_MASK)) {
        *(base + reg) = LSR_THRE | LSR_TEMT;
        return 1;
    }
*/
    if (address == ((G_SERIAL_PAGE) & H_POFF_MASK)) {
        unsigned char c = *(unsigned char *) (base + reg);
        if (c >= ' ' && c <= 'z') {
            if (c == 'd' && !cheat_uncompress) {
                V_ERR("seems uncompression is done");
                cheat_uncompress = 1;
                world->gregs.cpsr = H_CPSR_SVC | H_CPSR_I | H_CPSR_F;
            }
            {
                struct v_page *mpage;
                void *virt;
                int offset = ((currentposy * 80) + currentposx) * 2;
                mpage = h_p2mp(world, G_SERIAL_BASE + offset);
                virt = v_page_make_present(mpage);
                if (c > 0x10) {
                    *(unsigned char *) (virt + (offset & H_POFF_MASK)) = c;
                }
                currentposx++;
                if (currentposx >= 80) {
                    currentposy++;
                    currentposx = 0;
                }
            }
        } else {
            if (c == 0x0d) {
                currentposx = 0;
                currentposy++;
            }
            if (currentposy > 100)
                currentposy = 0;
        }
        return 1;
    }
    if (address == 0xff0) {
        V_ERR("Read serial ff0");
        *(base + reg) = 0x0d;
        return 1;
    }
    if (address == 0xff4) {
        *(base + reg) = 0xf0;
        return 1;
    }
    if (address == 0xff8) {
        *(base + reg) = 0x05;
        return 1;
    }
    if (address == 0xffc) {
        *(base + reg) = 0xb1;
        return 1;
    }
    if (address == 0xfe0) {
        V_ERR("Read serial fe0");
        *(base + reg) = 0x11;
        return 1;
    }
    if (address == 0xfe4) {
        *(base + reg) = 0x10;
        return 1;
    }
    if (address == 0xfe8) {
        *(base + reg) = 0x04;
        return 1;
    }
    if (address == 0xfec) {
        *(base + reg) = 0x00;
        return 1;
    }
    if (address >= 0x20 && address <= 0x44) {
        return 1;
    }
    V_ERR("Something other than TX occurred at %x", address);
    world->status = VM_PAUSED;
    return 0;
}

int
g_other_op(struct v_world *world, g_addr_t address)
{
    unsigned int ip = g_get_ip(world);
    int reg = 0;
    unsigned int *base = &world->hregs.gcpu.r0;
    if ((ip & 0x3) == 0) {
        void *virt;
        struct v_page *mpage;
        g_addr_t phys;
        unsigned int inst;

        phys = g_v2p(world, ip, 1);
        mpage = h_p2mp(world, phys);
        virt = v_page_make_present(mpage);
        inst = *(unsigned int *) ((unsigned int) virt + (ip & H_POFF_MASK));
        V_ERR("other dev inst is %x @ %x", inst, address);
        reg = (inst & 0xf000) >> 12;
    } else {
        V_ERR("Guest misaligned ip");
        world->status = VM_PAUSED;
        return 0;
    }

    world->hregs.gcpu.pc += 4;
    address = address & H_POFF_MASK;
    *(base + reg) = 0;
    return 1;
}

int ack_pic_once = 0;

int
g_pic_op(struct v_world *world, g_addr_t address)
{
    unsigned int ip = g_get_ip(world);
    int reg = 0;
    unsigned int *base = &world->hregs.gcpu.r0;
    if ((ip & 0x3) == 0) {
        void *virt;
        struct v_page *mpage;
        g_addr_t phys;
        unsigned int inst;

        phys = g_v2p(world, ip, 1);
        mpage = h_p2mp(world, phys);
        virt = v_page_make_present(mpage);
        inst = *(unsigned int *) ((unsigned int) virt + (ip & H_POFF_MASK));
        V_LOG("PIC inst is %x @ %x", inst, address);
        reg = (inst & 0xf000) >> 12;
    } else {
        V_ERR("Guest misaligned ip");
        world->status = VM_PAUSED;
        return 0;
    }

    world->hregs.gcpu.pc += 4;
    address = address & H_POFF_MASK;
    if ((address & 0xfff) == 0x0) {
        int val = 0;
        if (ack_pic_once) {
            V_LOG("Poke int once");
            val = (1 << 4);
            ack_pic_once = 0;
        }
        *(base + reg) = val;
        return 1;
    }
    *(base + reg) = 0;
    V_LOG("Something PIC occurred at %x", address);
    return 1;
}

int tick = 0;
int
g_clock_op(struct v_world *world, g_addr_t address)
{
    unsigned int ip = g_get_ip(world);
    int reg = 0;
    unsigned int *base = &world->hregs.gcpu.r0;
    if ((ip & 0x3) == 0) {
        void *virt;
        struct v_page *mpage;
        g_addr_t phys;
        unsigned int inst;

        phys = g_v2p(world, ip, 1);
        mpage = h_p2mp(world, phys);
        virt = v_page_make_present(mpage);
        inst = *(unsigned int *) ((unsigned int) virt + (ip & H_POFF_MASK));
        V_VERBOSE("clock inst is %x @ %x", inst, address);
        reg = (inst & 0xf000) >> 12;
    } else {
        V_ERR("Guest misaligned ip");
        world->status = VM_PAUSED;
        return 0;
    }

    world->hregs.gcpu.pc += 4;
    address = address & H_POFF_MASK;
    if ((address & 0xfff) == 0x5c) {
        *(base + reg) = tick;
        tick += 0x1000;
        return 1;
    }
    *(base + reg) = 0;
    V_ERR("Something other than tick occurred at %x", address);
    return 1;
}

void
g_world_init(struct v_world *w, unsigned long pages)
{
    if (pages < 256) {
        V_ERR("guest too small\n");
        return;
    }
    w->gregs.mode = G_MODE_NO_MMU;
    w->gregs.disasm_vip = 0;
    w->gregs.cpsr = G_PRIV_RST | H_CPSR_I | H_CPSR_F;
    w->hregs.gcpu.r3 =
        w->hregs.gcpu.r4 =
        w->hregs.gcpu.r5 =
        w->hregs.gcpu.r6 =
        w->hregs.gcpu.r7 =
        w->hregs.gcpu.r8 =
        w->hregs.gcpu.r9 =
        w->hregs.gcpu.r10 = w->hregs.gcpu.r11 = w->hregs.gcpu.r12 = 0xdeadbeef;
    w->hregs.gcpu.r13 = G_PA_BASE;
    w->hregs.gcpu.r0 = 0;
    w->hregs.gcpu.r1 = 0x183;   //simulating pandaboard 2791
    w->hregs.gcpu.r2 = 0x1;
    v_page_set_io(w, G_PA_BASE + 0x8000, g_init_image, 0);
    v_page_set_io(w, G_SERIAL_PAGE, g_serial_op, 0);
    v_page_set_io(w, 0x10000000, g_clock_op, 0);
    v_page_set_io(w, 0x10140000, g_pic_op, 0);
    v_page_set_io(w, 0x10003000, g_pic_op, 0);
    v_page_set_io(w, 0x101e0000, g_pic_op, 0);
    v_page_set_io(w, 0x101e2000, g_pic_op, 0);
    v_page_set_io(w, 0x101e3000, g_pic_op, 0);
    v_page_set_io(w, 0x10130000, g_other_op, 0);
    v_page_set_io(w, 0x101f1000, g_serial_op, 0);
    v_page_set_io(w, 0x101f2000, g_serial_op, 0);
    v_page_set_io(w, 0x101f3000, g_serial_op, 0);
    v_page_set_io(w, 0x10100000, g_other_op, 0);
    v_page_set_io(w, 0x10110000, g_other_op, 0);
    v_page_set_io(w, 0x10120000, g_other_op, 0);
/*    v_page_set_io(w, 0x48020000, g_serial_op, 0);
    v_page_set_io(w, 0x4a002000, g_omap_id_op, 0);
    v_page_set_io(w, 0x4a306000, g_prcm_op, 0);
    v_page_set_io(w, 0x4a307000, g_prcm_op, 0);
    v_page_set_io(w, 0x48243000, g_prcm_op, 0);
    v_page_set_io(w, 0x4a008000, g_prcm_op, 0);
    v_page_set_io(w, 0x4a009000, g_prcm_op, 0);
    v_page_set_io(w, 0x4a004000, g_prcm_op, 0);
    v_page_set_io(w, 0x4a30a000, g_prcm_op, 0);
*/
    {
        struct v_page *mpage;
        void *virt;
        mpage = h_p2mp(w, G_SERIAL_BASE);
        virt = v_page_make_present(mpage);
        mpage = h_p2mp(w, G_SERIAL_BASE + 0x1000);
        virt = v_page_make_present(mpage);
        mpage = h_p2mp(w, G_SERIAL_BASE + 0x2000);
        virt = v_page_make_present(mpage);
        mpage = h_p2mp(w, G_SERIAL_BASE + 0x3000);
        virt = v_page_make_present(mpage);
    }
}

int
g_do_int(struct v_world *world, unsigned int param)
{
    return 0;
}

static struct tag *params;

/* ATAG setup */
static void
setup_start_tag(unsigned int start)
{
    params = (struct tag *) start;

    params->hdr.tag = ATAG_CORE;
    params->hdr.size = tag_size(tag_core);

    params->u.core.flags = 0;
    params->u.core.pagesize = H_PAGE_SIZE;
    params->u.core.rootdev = 0;

    params = tag_next(params);
}

static void
setup_commandline_tag(char *commandline)
{
    char *p;

    if (!commandline)
        return;

    /* eat leading white space */
    for (p = commandline; *p == ' '; p++);

    /* skip non-existent command lines so the kernel will still
     * use its default command line.
     */
    if (*p == '\0')
        return;

    params->hdr.tag = ATAG_CMDLINE;
    params->hdr.size = (sizeof(struct tag_header) + strlen(p) + 1 + 4) >> 2;

    strcpy(params->u.cmdline.cmdline, p);

    params = tag_next(params);
}

static void
setup_memory_tags(unsigned int start, unsigned int size)
{
    params->hdr.tag = ATAG_MEM;
    params->hdr.size = tag_size(tag_mem32);

    params->u.mem.start = start;
    params->u.mem.size = size;

    params = tag_next(params);
}

static void
setup_initrd_tag(unsigned int initrd_start, unsigned int initrd_end)
{
    /* an ATAG_INITRD node tells the kernel where the compressed
     * ramdisk can be found. ATAG_RDIMG is a better name, actually.
     */
    params->hdr.tag = ATAG_INITRD2;
    params->hdr.size = tag_size(tag_initrd);

    params->u.initrd.start = initrd_start;
    params->u.initrd.size = initrd_end - initrd_start;

    params = tag_next(params);
}

static void
setup_end_tag(void)
{
    params->hdr.tag = ATAG_NONE;
    params->hdr.size = 0;
}

static void
setup_tags(struct v_world *world, unsigned int start)
{
    void *virt;
    struct v_page *mpage = h_p2mp(world, start);
    virt =
        (void *) ((((unsigned int) (v_page_make_present(mpage))) & H_PFN_MASK) +
        (start & H_POFF_MASK));
    world->hregs.gcpu.r2 = start;
    setup_start_tag((unsigned int) virt);
    setup_memory_tags(G_PA_BASE, 0x4000000);
    setup_initrd_tag(0x3000000, 0x3000000 + initrd_len);
//    setup_commandline_tag("vram=1K mem=128M@0x80000000 console=ttyO2,115200n8 init=/linuxrc root=/dev/mmcblk0p2");
//    setup_commandline_tag("mem=64M@0x10000000 console=ttyMSM,115200n8 root=/dev/mmcblk0p2 debug earlyprintk");
    setup_commandline_tag
        ("mem=64M@0x00000000 console=ttyAMA0,38400n8 serialtty=ttyAMA0 root=/dev/ram0 init=/init debug earlyprintk");
    setup_end_tag();
}
