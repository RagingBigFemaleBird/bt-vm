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
#include "guest/include/bt.h"
#include "guest/include/seed.h"
#include "guest/include/dev/fb.h"
#include "guest/include/dev/fdc.h"
#include "guest/include/dev/dma.h"
#include "vm/include/world.h"
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/logging.h"
#include "guest/include/perf.h"

unsigned char *g_disk_data = NULL;
unsigned long g_disk_length = 0;

static int
g_init_boot_sector(struct v_world *world, g_addr_t address)
{
    if (g_disk_length != 0 && g_disk_data != NULL) {
        struct v_page *mpage;
        void *virt;
        mpage = h_p2mp(world, address);
        virt = v_page_make_present(mpage);
        h_memcpy(virt + 0xc00, g_disk_data, 512);
        V_LOG("Boot data %x, %x, %x, %lx", g_disk_data[0],
            g_disk_data[1], g_disk_data[2], g_disk_length);
        V_LOG("Boot sector init complete %x, %x, %x, %x\n",
            *(unsigned int *) (virt + 0xc00),
            *(unsigned int *) (virt + 0xc01),
            *(unsigned int *) (virt + 0xc02), *(unsigned int *) (virt + 0xc03));
        v_page_unset_io(world, 0x7c00);
    }
    return 0;
}

void
g_bios_init(struct v_world *world)
{
    struct v_page *mpage;
    void *virt;
    unsigned char disk_param[] = {
        0xdf, 0x02, 0x25, 0x02, 0x0f, 0x1b, 0xff, 0x54, 0xf6, 0x0f,
        0x08, 0xe9
    };
    V_ERR("BIOS area init");
    mpage = h_p2mp(world, 0);
    if (mpage == NULL) {
        V_ERR("BIOS area init failed");
    } else {
        virt = v_page_make_present(mpage);
        *(unsigned short *) (virt + 0x78) = 0xefc7;     //disk param
        *(unsigned short *) (virt + 0x78) = 0xf000;     //disk param
        *(unsigned char *) (virt + 0x44a) = 80;
        *(unsigned char *) (virt + 0x484) = 25;
        *(unsigned char *) (virt + 0x485) = 0x10;
    }
    mpage = h_p2mp(world, 0xfe000);
    if (mpage == NULL) {
        V_ERR("BIOS area init failed");
    } else {
        virt = v_page_make_present(mpage);
        h_memcpy(virt + 0xfc7, disk_param, 12); //disk param
    }
}

void
g_world_init(struct v_world *w, unsigned long pages)
{
    int i;
    if (pages < 256) {
        V_ERR("guest too small\n");
        return;
    }
    w->gregs.mode = G_MODE_REAL;
    w->gregs.cr0 = 0x10;
    w->gregs.cr3 = 0x0;
    w->gregs.ring = 0;
    w->gregs.iopl = 0;
    w->gregs.ldt = w->gregs.ldt_true = 0;
    w->gregs.cstrue = 0x7e3;
    w->gregs.sstrue = 0x7eb;
    w->gregs.ds = w->gregs.dstrue = 0;
    w->gregs.es = w->gregs.estrue = 0;
    w->gregs.fs = w->gregs.fstrue = 0;
    w->gregs.gs = w->gregs.gstrue = 0;

    v_page_set_io(w, 0x7c00, g_init_boot_sector, 0);

    g_tr_init();
    g_seed_init();

    /* device inits */
    for (i = 0; i < G_CMOS_REGS; i++) {
        w->gregs.dev.cmos.cmos_reg[i] = 0;
    }
    w->gregs.dev.cmos.cmos_reg[0x0a] = 0x20;
    w->gregs.dev.cmos.port61 = 0x20;
    w->gregs.dev.cmos.latch = 0;
    w->gregs.dev.cmos.ff = 0;
    w->gregs.dev.cmos.cmd = 0xff;

    g_fb_init(w, 0xb8000);
    g_fb_init(w, 0xb9000);
    g_fb_init(w, 0xba000);
    g_fb_init(w, 0xbb000);
    g_pic_init(w);
    g_bios_init(w);
    g_kb_init(w);
}

int
g_do_io(struct v_world *world, unsigned int dir, unsigned int address,
    void *param)
{
    if (address < 0xff) {
        g_perf_inc(address, 1);
    }
    switch (address) {
    case 0x20:
        if (dir != G_IO_OUT)
            goto io_not_handled;
        world->gregs.dev.pic.d0icw1 = *(unsigned char *) param;
        if (world->gregs.dev.pic.d0icw1 & G_PIC_ICW1_ICW1) {
            world->gregs.dev.pic.d0icw_expect = 2;
        } else if (world->gregs.dev.pic.d0icw1 & G_PIC_OCW3) {
            world->gregs.dev.pic.d0in20 =
                (world->gregs.dev.pic.d0icw1 & 1) ? world->gregs.dev.pic.
                d0IRQ_req : world->gregs.dev.pic.d0IRQ_srv;
        } else {
            V_EVENT("ACK %x", world->gregs.dev.pic.d0icw1);
            world->gregs.dev.pic.d0IRQ_req ^= world->gregs.dev.pic.d0IRQ_srv;
            world->gregs.dev.pic.d0IRQ_srv = 0;
        }
        break;
    case 0x21:
        if (dir == G_IO_IN) {
            *(unsigned char *) param = world->gregs.dev.pic.d0IRQ_mask;
            break;
        }
        if (world->gregs.dev.pic.d0icw_expect == 2) {
            world->gregs.dev.pic.d0IRQ = *(unsigned char *) param;
            V_EVENT("PIC irq redirected to %x", world->gregs.dev.pic.d0IRQ);
            if (world->gregs.dev.pic.d0icw1 & G_PIC_ICW1_ICW3) {
                world->gregs.dev.pic.d0icw_expect = 0;
            } else {
                world->gregs.dev.pic.d0icw_expect++;
            }
        } else if (world->gregs.dev.pic.d0icw_expect == 3) {
            if (world->gregs.dev.pic.d0icw1 & G_PIC_ICW1_ICW4) {
                world->gregs.dev.pic.d0icw_expect++;
            } else {
                world->gregs.dev.pic.d0icw_expect = 0;
            }
        } else if (world->gregs.dev.pic.d0icw_expect == 4) {
            world->gregs.dev.pic.d0icw_expect = 0;
        } else if (world->gregs.dev.pic.d0icw_expect == 0) {
            world->gregs.dev.pic.d0IRQ_mask = *(unsigned char *) param;
            V_EVENT("PIC irq mask: %x", world->gregs.dev.pic.d0IRQ_mask);
        } else {
            V_ERR("PIC runs into unknown state");
        }
        break;
    case 0x43:
        if (dir == G_IO_OUT) {
            world->gregs.dev.cmos.cmd = *(unsigned char *) param;
        } else
            goto io_not_handled;
        break;
    case 0x40:
        if (dir == G_IO_OUT) {
            if (world->gregs.dev.cmos.cmd == 0x34) {
                if (world->gregs.dev.cmos.ff) {
                    world->gregs.dev.cmos.latch &= 0xff;
                    world->gregs.dev.cmos.latch +=
                        ((*(unsigned char *) param) << 8);
                } else {
                    world->gregs.dev.cmos.latch &= 0xff00;
                    world->gregs.dev.cmos.latch += (*(unsigned char *) param);
                }
                world->gregs.dev.cmos.ff ^= 1;
                V_ALERT("LATCH set %d(%x)",
                    world->gregs.dev.cmos.latch, world->gregs.dev.cmos.latch);
            } else
                goto io_not_handled;
        } else if (dir == G_IO_IN) {
            if (world->gregs.dev.cmos.cmd == 0x00) {
                if (world->gregs.dev.cmos.ff) {
                    *(unsigned char *) param = world->gregs.dev.cmos.latch >> 8;
                } else {
                    *(unsigned char *) param =
                        world->gregs.dev.cmos.latch & 0xff;
                }
                world->gregs.dev.cmos.ff ^= 1;
                V_ALERT("LATCH read %d(%x)",
                    world->gregs.dev.cmos.latch, world->gregs.dev.cmos.latch);
            } else
                goto io_not_handled;
        } else
            goto io_not_handled;
        break;
    case 0x61:
        if (dir == G_IO_IN) {
            if (world->gregs.dev.cmos.port61) {
                (*(unsigned char *) param) = 0x0;       /* return status not ready */
                world->gregs.dev.cmos.port61--;
            } else {
                (*(unsigned char *) param) = 0x20;      /* return status good */
            }
        } else if (dir == G_IO_OUT) {
            V_EVENT("Reset port 61");
            world->gregs.dev.cmos.port61 = 5;   /* hack: port 61 takes 5 reads to reset */
        }
        break;
    case 0x60:
    case 0x64:
        return g_kb_handle_io(world, dir, address, param);
        break;
    case 0x80:
        break;
    case 0x70:
        if (dir != G_IO_OUT)
            goto io_not_handled;
        world->gregs.dev.cmos.cmos_rtc_index = *(unsigned char *) param;
        break;
    case 0x71:
        switch (world->gregs.dev.cmos.cmos_rtc_index) {
        case 0x0a:
            if (dir == G_IO_OUT)
                world->gregs.dev.cmos.cmos_reg[world->gregs.dev.cmos.
                    cmos_rtc_index] = *(unsigned char *) param;
            else if (dir == G_IO_IN) {
                *(unsigned char *) param =
                    world->gregs.dev.cmos.cmos_reg[world->gregs.dev.cmos.
                    cmos_rtc_index];
                world->gregs.dev.cmos.cmos_reg[world->gregs.dev.cmos.cmos_rtc_index] ^= 0x80;   /* do alternating ready/not ready to fool linux init */
            } else
                goto io_not_handled;
            break;
        case 0x10:
            if (dir == G_IO_OUT)
                goto io_not_handled;
            else if (dir == G_IO_IN) {
                *(unsigned char *) param = 0x40;        /*hardcoded: first drive 1.44M, second drive none */
            } else
                goto io_not_handled;
            break;
        case 0x12:
            if (dir == G_IO_OUT)
                goto io_not_handled;
            else if (dir == G_IO_IN) {
                *(unsigned char *) param = 0x00;        /*hardcoded: no hard drives */
            } else
                goto io_not_handled;
            break;
        default:
            V_ERR("cmos index %x", world->gregs.dev.cmos.cmos_rtc_index);
            goto io_not_handled;
        }
        break;
    case 0x3f0:
    case 0x3f1:
    case 0x3f2:
    case 0x3f3:
    case 0x3f4:
    case 0x3f5:
    case 0x3f6:
    case 0x3f7:
        return g_fdc_handle_io(world, dir, address, param);
        break;
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
    case 0x8:
    case 0x9:
    case 0xa:
    case 0xb:
    case 0xc:
    case 0xd:
    case 0xe:
    case 0xf:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
    case 0x88:
    case 0x89:
    case 0x8a:
    case 0x8b:
    case 0x8c:
    case 0x8d:
    case 0x8e:
    case 0x8f:
        return g_dma_handle_io(world, dir, address, param);
        break;
    case 0xcf8:
    case 0xcf9:
    case 0xcfa:
    case 0xcfb:
        if (dir == G_IO_OUT) {  /*do nothing */
        } else if (dir == G_IO_IN) {
            *(unsigned char *) param = 0xff;    /*try to curb any pci probing */
        }
        break;
    default:
      io_not_handled:
        V_ERR("unhandled IO %s port %x DATA=%x",
            (dir == G_IO_IN) ? "in" : "out", address, *(unsigned char *) param);
    }
    return 0;
}

int
g_do_int(struct v_world *world, unsigned int param)
{
    unsigned int to_read;
    unsigned int addr;
    unsigned int drive;
    unsigned int block;
    struct v_page *mpage;
    void *virt;
    if (world->gregs.mode == G_MODE_REAL) {
        switch (param) {
        case 0x11:
            V_EVENT("get equipment list");
            world->hregs.gcpu.eax = 0x123;      // floppy disk installed 1, math, vga color, dma support
            break;
        case 0x10:
            if ((world->hregs.gcpu.eax & 0xff00) == 0x0e00) {
                g_fb_write(world, world->hregs.gcpu.eax & 0xff,
                    world->hregs.gcpu.eax & 0xff);
                V_LOG("%c", world->hregs.gcpu.eax & 0xff);
            } else if ((world->hregs.gcpu.eax & 0xff00) == 0x0300 && (world->hregs.gcpu.ebx & 0xff00) == 0) {   /*page 0 only */
                world->hregs.gcpu.eax = 0;
                world->hregs.gcpu.ecx = 0x0607;
                world->hregs.gcpu.edx =
                    (g_fb_gety(world) << 8) + g_fb_getx(world);
            } else if ((world->hregs.gcpu.eax & 0xff00) == 0x1200) {
                switch (world->hregs.gcpu.ebx & 0xff) {
                case 0x10:
                    V_EVENT("return video config");
                    world->hregs.gcpu.ebx = 3;  //return 256K mem
                    world->hregs.gcpu.ecx = 9;  //return default feature bits for evga color
                    break;
                default:
                    V_ERR("Unhandled INT 10, 12h: %x",
                        world->hregs.gcpu.ebx & 0xff);
                }

            } else if ((world->hregs.gcpu.eax) == 0x1a00) {
                V_EVENT("get display combination mode");
                world->hregs.gcpu.eax = 0x1a;
                world->hregs.gcpu.ebx = 0x0008; //return supported, mode color vga
            } else if ((world->hregs.gcpu.eax & 0xff00) == 0x0f00) {
                V_EVENT("return current video mode");
                world->hregs.gcpu.eax = 0x5003; // 80 columns, mode 03(vga color)
                world->hregs.gcpu.ebx = 0;      //page 0

            } else if ((world->hregs.gcpu.eax & 0xff00) == 0x1300) {
                int update = world->hregs.gcpu.eax & 1;
                int attr = world->hregs.gcpu.eax & 2;
                int to_write, i;
                unsigned int addr =
                    ((world->hregs.gcpu.v86es & 0xffff) << 4) +
                    (world->hregs.gcpu.ebp & 0xffff);
                int savex = 0, savey = 0;
                if ((world->hregs.gcpu.ebx & 0xff00) != 0) {
                    V_ERR
                        ("Unhandled write string: INT %x, EBX %x",
                        param, world->hregs.gcpu.ebx);
                    break;
                }
                if (!update) {
                    savex = g_fb_getx(world);
                    savey = g_fb_gety(world);
                }
                g_fb_setx(world, world->hregs.gcpu.edx & 0xff);
                g_fb_sety(world, (world->hregs.gcpu.edx >> 8) & 0xff);
                for (i = 0; i < (world->hregs.gcpu.ecx & 0xffff); i++) {
                    if (h_read_guest(world, addr, &to_write)) {
                        V_ERR("Guest unrecoverable fault");
                        world->status = VM_PAUSED;
                        break;
                    }
                    if (attr) {
                        g_fb_write(world,
                            to_write & 0xff, (to_write >> 8) & 0xff);
                        addr += 2;
                    } else {
                        g_fb_write(world,
                            to_write & 0xff, world->hregs.gcpu.ebx & 0xff);
                        addr++;
                    }
                }
                if (!update) {
                    g_fb_setx(world, savex);
                    g_fb_sety(world, savey);
                }

            } else
                V_ERR("Unhandled guest interrupt: %x, EAX %x",
                    param, world->hregs.gcpu.eax);

            break;
        case 0x16:
            if ((world->hregs.gcpu.eax & 0xff00) == 0) {
                world->hregs.gcpu.eax = 0x1c0d; /* auto return ENTER on key */
            } else if ((world->hregs.gcpu.eax & 0xff00) == 0x100) {
                world->hregs.gcpu.eflags &= (~H_EFLAGS_ZF);     /* auto return HAS KEY */
            } else if ((world->hregs.gcpu.eax & 0xff00) == 0x300) {
                V_EVENT("set typematic rate");  /* auto return HAS KEY */
            } else
                V_ERR("Unhandled guest interrupt: %x, EAX %x",
                    param, world->hregs.gcpu.eax);
            break;
        case 0x13:
            switch ((world->hregs.gcpu.eax & 0xff00) >> 8) {
            case 0:            /*disk reset */
                V_EVENT("reset disk %x", world->hregs.gcpu.edx);
                if ((world->hregs.gcpu.edx & 0xff) == 0) {
                    world->hregs.gcpu.eflags &= (~H_EFLAGS_CF);
                } else
                    world->hregs.gcpu.eflags |= H_EFLAGS_CF;
                break;
            case 0x15:         /*disk type */
                world->hregs.gcpu.eflags &= (~H_EFLAGS_CF);
                if ((world->hregs.gcpu.edx & 0xff) == 0) {
                    world->hregs.gcpu.eax = 0x0100;
                } else {        /* no other drives */

                    world->hregs.gcpu.eax = 0x0000;
                }
                break;
            case 2:
                if ((world->hregs.gcpu.ecx & 0xff) > 18) {      // linux way of probing disk geometry...
                    world->hregs.gcpu.eflags |= H_EFLAGS_CF;
                    break;
                }
                to_read = (world->hregs.gcpu.eax & 0xff);
                addr = (world->hregs.gcpu.v86es << 4) +
                    (world->hregs.gcpu.ebx & 0xffff);
                drive = (world->hregs.gcpu.edx & 0xff);
                block =
                    ((world->hregs.gcpu.ecx & 0xff00) >> 8) *
                    36 +
                    ((world->hregs.gcpu.edx & 0xff00) >> 8) *
                    18 + (world->hregs.gcpu.ecx & 0xff) - 1;
                world->hregs.gcpu.eflags &= (~H_EFLAGS_CF);
                while (to_read > 0) {
                    V_LOG(" READ block %x to %x:\n", block, addr);
                    mpage = h_p2mp(world, addr);
                    virt = v_page_make_present(mpage);
                    V_LOG("Sector data %x, %x, %x, %x",
                        g_disk_data[block * 512],
                        g_disk_data[block * 512 + 1],
                        g_disk_data[block * 512 + 2],
                        g_disk_data[block * 512 + 3]);
                    h_memcpy((void
                            *) (((unsigned int) (virt) &
                                0xfffff000) +
                            (addr & 0xfff)), &g_disk_data[block * 512], 512);
                    to_read--;
                    addr += 512;
                    block++;
                }
                break;
            default:
                V_ERR("Unhandled guest interrupt: %x, EAX %x",
                    param, world->hregs.gcpu.eax);
            }
            break;
        case 0x15:
            switch ((world->hregs.gcpu.eax & 0xff00) >> 8) {
            case 0x87:
                {
                    unsigned int src, dst;
                    void *virt2;
                    struct v_page *mpage2;
                    addr =
                        (world->hregs.gcpu.v86es << 4) +
                        (world->hregs.gcpu.esi & 0xffff);
                    to_read = (world->hregs.gcpu.ecx & 0xffff) * 2;
                    h_read_guest(world, addr + 0x12, &src);
                    h_read_guest(world, addr + 0x1a, &dst);
                    src &= 0xffffff;
                    dst &= 0xffffff;
                    V_ALERT
                        ("protected mode memcpy: descriptor %x, %x to %x, len %x",
                        addr, src, dst, to_read);
                    if ((((src & H_POFF_MASK) != 0)
                            || ((dst & H_POFF_MASK) != 0))
                        && ((src & H_POFF_MASK) + to_read >
                            0x1000 || (dst & H_POFF_MASK) + to_read > 0x1000)) {
                        V_ERR("unaligned protected mode memcpy");
                        break;
                    }
                    while (to_read > 0) {
                        mpage = h_p2mp(world, src);
                        virt = v_page_make_present(mpage) + (src & H_POFF_MASK);
                        mpage2 = h_p2mp(world, dst);
                        virt2 = v_page_make_present(mpage2)
                            + (dst & H_POFF_MASK);
                        V_LOG("copying %x %x %x %x",
                            *(unsigned short *) (virt),
                            *(unsigned short *) (virt +
                                2),
                            *(unsigned short *) (virt +
                                4), *(unsigned short *) (virt + 6));
                        h_memcpy(virt2, virt,
                            (to_read > 0x1000) ? 0x1000 : to_read);
                        if (to_read <= 0x1000)
                            to_read = 0;
                        else
                            to_read -= 0x1000;
                        src += 0x1000;
                        dst += 0x1000;
                    }
                    world->hregs.gcpu.eflags &= (~H_EFLAGS_CF);
                    break;
                }
            case 0xc0:
                V_EVENT("get system config");
                world->hregs.gcpu.eflags |= H_EFLAGS_CF;        //return not supported
                world->hregs.gcpu.eax = 0x8600;
                break;
            case 0x88:
                world->hregs.gcpu.eflags &= ~(H_EFLAGS_CF);     /* return 8MB */
                world->hregs.gcpu.eax = 8192 - 1024;
                V_ALERT("INT: Get memory map, 80");
                break;
            case 0xe8:
                if ((world->hregs.gcpu.eax & 0xffff) == 0xe820
                    && world->hregs.gcpu.edx == 0x534d4150) {
                    unsigned int addr =
                        ((world->hregs.gcpu.v86es & 0xffff) << 4) +
                        (world->hregs.gcpu.esi & 0xffff);
                    int err = 0;
                    world->hregs.gcpu.eax = 0x534d4150;
                    world->hregs.gcpu.ebx = 0;
                    world->hregs.gcpu.ecx = 0x14;
                    world->hregs.gcpu.eflags &= (~H_EFLAGS_CF);
                    err |= h_write_guest(world, addr, 0);
                    err |= h_write_guest(world, addr + 4, 0);
                    err |= h_write_guest(world, addr + 8, 0x800000);    /* return 8MB */
                    err |= h_write_guest(world, addr + 12, 0);
                    err |= h_write_guest(world, addr + 16, 0);
                    if (err) {
                        V_ERR("Guest unrecoverable fault");
                        world->status = VM_PAUSED;
                    }
                    V_ALERT("INT: Get memory map, e820");
                } else if ((world->hregs.gcpu.eax & 0xffff) == 0xe801) {
                    world->hregs.gcpu.eflags |= H_EFLAGS_CF;    /* return not supported */
                    V_ALERT("INT: Get memory map, e801");
                } else
                    V_ERR
                        ("Unhandled guest interrupt: %x, EAX %x EDX %x",
                        param, world->hregs.gcpu.eax, world->hregs.gcpu.edx);
                break;
            default:
                V_ERR("Unhandled guest interrupt: %x, EAX %x",
                    param, world->hregs.gcpu.eax);
            }
            break;
        case 0x19:
            V_ERR("Guest reboot\n");
            world->status = VM_PAUSED;
            break;
        case 0x12:
            V_EVENT("get memory size");
            world->hregs.gcpu.eax = 8192;       /*return 8MB */
            break;
        default:
            V_ERR("Unhandled guest interrupt: %x, EAX %x", param,
                world->hregs.gcpu.eax);
        }
        return 0;
    }
    return 1;
}
