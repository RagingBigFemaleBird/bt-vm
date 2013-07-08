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
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/aio.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include "host/include/mm.h"
#include "vm/include/mm.h"
#include "vm/include/perf.h"
#include "host/include/perf.h"
#include "guest/include/perf.h"
#include "host/include/cpu.h"
#include "vm/include/world.h"
#include "vm/include/bt.h"
#include "vm/include/logging.h"
#include "host/include/interrupt.h"

#define PROCFS_NAME "btc"

#define BTC_IOC_MAGIC 'v'

#define BTC_IOCRESET _IO(BTC_IOC_MAGIC, 0)

#define BTC_STATUS _IO(BTC_IOC_MAGIC, 1)
#define BTC_STEP _IO(BTC_IOC_MAGIC, 2)
#define BTC_BP  _IOW(BTC_IOC_MAGIC, 3, unsigned int)
#define BTC_KEYIN _IOW(BTC_IOC_MAGIC, 4, unsigned int)
#define BTC_UMOUNT _IOW(BTC_IOC_MAGIC, 5, unsigned int)
#define BTC_RUN _IOW(BTC_IOC_MAGIC, 6, unsigned int)
#define BTC_IOC_MAXNR 6

static struct v_world *w_list;
/*
static struct timer_list my_timer;
static struct workqueue_struct *btc_wq;
*/
static struct proc_dir_entry *btc_proc_file;
volatile int step = 0;
volatile int v_relocate = 0;
int stepping = 0;
volatile unsigned int time_up = 0;

void btc_work_func(struct work_struct *);
static DECLARE_DELAYED_WORK(btc_task, btc_work_func);
spinlock_t e_lock;

int poke = 0;

#ifdef CONFIG_ARM
#define REQUIRE_INITRD
#endif

int require_initrd =
#ifdef REQUIRE_INITRD
    1
#else
    0
#endif
    ;
extern unsigned char *g_disk_data;
extern unsigned long g_disk_length;
#ifdef CONFIG_ARM
extern unsigned char *g_initrd_data;
extern unsigned long g_initrd_length;
#endif
int len = 0;
int init = 1;

void *tempBuffer;

int
procfile_read(char *buffer, char **buffer_location, off_t offset,
    int buffer_length, int *eof, void *data)
{
    int ret;

    if (offset > 0) {
        ret = 0;
    } else {
        if (require_initrd) {
            require_initrd = 0;
            ret = sprintf(buffer, "Initrd done with len %x\n", len);
            len = 0;
            return ret;
        }
        if (w_list->status == VM_PAUSED) {
            w_list->status = VM_RUNNING;
            ret = sprintf(buffer, "VM 0 Status: Paused, changed to Running\n");
        } else {
#ifdef CONFIG_X86
            ret =
                sprintf(buffer, "VM 0 Status: Running @ %lx\n",
                g_get_ip(w_list));
#endif
#ifdef CONFIG_ARM
            ret =
                sprintf(buffer, "VM 0 Status: Running @ %lx, %x, %x\n",
                g_get_ip(w_list), w_list->hregs.gcpu.r0,
                w_list->hregs.gcpu.r14);
            poke++;
#endif
        }
    }

    return ret;
}


int
procfile_write(struct file *file, const char *buffer, unsigned long count,
    void *data)
{
#ifdef CONFIG_ARM
    if (require_initrd) {
        V_VERBOSE("Transferring initrd...");
        if (g_initrd_data == NULL) {
            if ((g_initrd_data = vmalloc(0x1000000)) == NULL)
                return -EFAULT;
            tempBuffer = g_initrd_data;
        }

        if (copy_from_user(&g_initrd_data[len], buffer, count))
            return -EFAULT;
        len += count;
        //arm debug
        g_initrd_length += count;
        return count;
    }
#endif
    V_VERBOSE("Transferring image...");
    if (g_disk_data == NULL) {
        if ((g_disk_data = vmalloc(
#ifdef CONFIG_ARM
                    5000000
#endif
#ifdef CONFIG_X86
                    512 * 2 * 254 * 254      /*max size*/
#endif
                )) == NULL)
            return -EFAULT;
        tempBuffer = g_disk_data;
    }

    if (copy_from_user(&g_disk_data[len], buffer, count))
        return -EFAULT;
    len += count;
#ifdef CONFIG_ARM
    //arm debug
    g_disk_length += count;
#endif
    return count;
}

void
btc_work_func(struct work_struct *work)
{
    unsigned long flags;
    struct v_world *curr;
    spin_lock_irqsave(&e_lock, flags);
    preempt_disable();
    curr = w_list;
    if (curr->status != VM_PAUSED) {
        if (!(curr->cpu_init_mask & (1 << host_processor_id()))) {
            preempt_enable();
            spin_unlock_irqrestore(&e_lock, flags);
            return;
        }
/*	if ((stepping % 150) == 2)
	    v_relocate = 1;
	if ((stepping % 150) == 3)
	    h_relocate_tables(w_list);
	if ((stepping % 150) == 4)
	    h_relocate_npage(w_list);
*/
        if (curr->relocate == 1) {
            w_list = v_relocate_world(curr);    /* do this properly */
            curr->relocate = 0;
        }
        V_LOG("Stepping %x: %p status %x", stepping++, w_list, w_list->status);
        poke++;
        time_up = 0;
        h_int_prepare();
        if (step) {
            v_switch_to(w_list);
            curr->status = VM_PAUSED;
        } else
            while ((curr->status != VM_PAUSED) && (!time_up)) {
                v_switch_to(w_list);
                if (curr->relocate || curr->status == VM_IDLE)
                    break;
            }
        h_int_restore();
    }
    preempt_enable();
    spin_unlock_irqrestore(&e_lock, flags);
}

/*
static void
my_timer_func(unsigned long ptr)
{
    queue_delayed_work(btc_wq, &btc_task, 0);
    my_timer.expires = jiffies + 1;
    add_timer(&my_timer);
}
*/

struct btc_dev {
    struct v_world *w;          /*world info */
    int vmas;                   /*mmap count */
    struct semaphore sem;
    struct cdev cdev;
};

void
btc_vma_open(struct vm_area_struct *vma)
{
    struct btc_dev *dev = vma->vm_private_data;

    dev->vmas++;
}

void
btc_vma_close(struct vm_area_struct *vma)
{
    struct btc_dev *dev = vma->vm_private_data;

    dev->vmas--;
}

int
btc_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    struct btc_dev *dev = vma->vm_private_data;
    struct page *page;

    V_ERR("Device mapped");
    down(&dev->sem);
    page = pfn_to_page(h_p2mp(dev->w,
#ifdef CONFIG_X86
            0xb8000
#endif
#ifdef CONFIG_ARM
            G_SERIAL_BASE
#endif
            + (vmf->pgoff * PAGE_SIZE))->mfn);
    get_page(page);
    vmf->page = page;
    up(&dev->sem);
    return 0;
}

struct vm_operations_struct btc_vm_ops = {
    .open = btc_vma_open,
    .close = btc_vma_close,
    .fault = btc_vma_fault,
};

extern unsigned int bpaddr;
extern int usermode_tests_reset;
extern unsigned int g_dev_floppy_density;

#ifdef CONFIG_X86
#ifdef HAVE_UNLOCKED_IOCTL
long
btc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
int
btc_ioctl(struct inode *inode, struct file *filp,
    unsigned int cmd, unsigned long arg)
#endif
#endif
#ifdef CONFIG_ARM
long
btc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{

    int err = 0, ret = 0;
    struct v_world *curr = w_list;

    if (_IOC_TYPE(cmd) != BTC_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > BTC_IOC_MAXNR)
        return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE, (void __user *) arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok(VERIFY_READ, (void __user *) arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    switch (cmd) {

    case BTC_STATUS:
        if (arg) {
            int i;
            V_ALERT("VM Status %x", curr->status);
            if (curr->status == VM_PAUSED)
                curr->status = VM_RUNNING;
            else
                curr->status = VM_PAUSED;
            if (init && curr->status == VM_RUNNING) {
                init = 0;
                g_disk_length = len;
            }
            for (i = 0; i < V_PERF_COUNT; i++) {
                V_ERR("VM Perf Counter %x, %lx", i, v_perf_get(i));
            }
            for (i = 0; i < H_PERF_COUNT; i++) {
                V_ERR("Host Perf Counter %x, %lx", i, h_perf_get(i));
            }
            for (i = 0; i < H_TSC_COUNT; i++) {
                V_ERR("Host TSC Counter %x, %llx", i, h_tsc_get(i));
            }
            for (i = 0; i < G_PERF_COUNT; i++) {
                V_ERR("Guest Perf Counter %x, %lx", i, g_perf_get(i));
            }
#ifdef BT_CACHE
            v_dump_pb_cache(w_list);
#endif
        }
        ret = curr->status;
        break;

    case BTC_STEP:
        if (arg) {
            V_ALERT("VM Step: %x", step);
            step = 1 - step;
        }
        ret = step;
        break;

    case BTC_BP:
        bpaddr = arg;
        V_ALERT("Set breakpoint at %x", bpaddr);
        ret = 0;
        break;

    case BTC_KEYIN:
        V_ERR("Keyboard input %lx", arg);
        g_inject_key(curr, arg);
        ret = 0;
        break;

    case BTC_RUN:
        btc_work_func(NULL);
        ret = 1;
        break;

    case BTC_UMOUNT:
        if (g_disk_data != NULL) {
            vfree(g_disk_data);
        }
#ifdef CONFIG_X86
        g_disk_data = NULL;
        g_disk_length = 0;
        g_dev_floppy_density = 5;
        g_fdc_eject(w_list);
        len = 0;
        ret = 0;
        usermode_tests_reset = 1;
#endif
        break;

    default:
        return -ENOTTY;
    }

    return ret;
}

int
btc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &btc_vm_ops;
    vma->vm_flags |=
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
        (VM_DONTEXPAND | VM_DONTDUMP)
#else
        VM_RESERVED
#endif
        ;
    vma->vm_private_data = filp->private_data;
    btc_vma_open(vma);
    return 0;
}

int
btc_open(struct inode *inode, struct file *filp)
{
    struct btc_dev *dev;
    dev = container_of(inode->i_cdev, struct btc_dev, cdev);

    dev->w = w_list;
    filp->private_data = dev;

    return 0;
}

int
btc_release(struct inode *inode, struct file *filp)
{
    return 0;
}

struct file_operations btc_fops = {
    .owner = THIS_MODULE,
    .mmap = btc_mmap,
    .open = btc_open,
    .release = btc_release,
#ifdef CONFIG_X86
#ifdef HAVE_UNLOCKED_IOCTL
    .unlocked_ioctl = btc_ioctl,
#else
    .ioctl = btc_ioctl,
#endif
#endif
#ifdef CONFIG_ARM
    .unlocked_ioctl = btc_ioctl,
#endif
};

int btc_major = 0;
struct btc_dev btc_device;

int
init_module(void)
{
    int result, err;
    dev_t dev = MKDEV(btc_major, 0);
    if (btc_major)
        result = register_chrdev_region(dev, 1, "btc");
    else {
        result = alloc_chrdev_region(&dev, 0, 1, "btc");
        btc_major = MAJOR(dev);
    }
    if (result < 0)
        return result;

    spin_lock_init(&e_lock);
    sema_init(&btc_device.sem, 1);

    cdev_init(&btc_device.cdev, &btc_fops);
    btc_device.cdev.owner = THIS_MODULE;
    btc_device.cdev.ops = &btc_fops;
    err = cdev_add(&btc_device.cdev, MKDEV(btc_major, 0), 1);

    if (err)
        V_ERR("Error creating device");

    V_EVENT("Initializing...\n");

/*
    btc_wq = create_workqueue("btc");
*/
    if (h_cpu_init())
        return -EIO;

    btc_proc_file = create_proc_entry(PROCFS_NAME, 0644, NULL);

    if (btc_proc_file == NULL) {
        V_ERR("Error: Could not initialize /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;
    }

    btc_proc_file->read_proc = procfile_read;
    btc_proc_file->write_proc = procfile_write;
    btc_proc_file->mode = S_IFREG | S_IRUGO;
    btc_proc_file->uid = 0;
    btc_proc_file->gid = 0;
    btc_proc_file->size = 37;

    V_LOG("/proc/%s created\n", PROCFS_NAME);

    w_list = v_create_world(
#ifdef CONFIG_X86
        G_CONFIG_MEM_PAGES      /* pages */
#endif
#ifdef CONFIG_ARM
        0x4000                  /* pages */
#endif
        );
    V_LOG("w = %p, w.trbase = %lx\n", w_list, w_list->htrbase);
    V_ERR("Initialization complete on CPU %x", host_processor_id());
/*
    init_timer(&my_timer);
    my_timer.function = (void *) (my_timer_func);
    my_timer.data = 0;
    my_timer.expires = jiffies + HZ / 2;
    add_timer(&my_timer);
*/
    return 0;
}

void
cleanup_module(void)
{
    v_destroy_world(w_list);
    vfree(tempBuffer);
    remove_proc_entry(PROCFS_NAME, NULL);
/*
    del_timer(&my_timer);
*/
    cdev_del(&btc_device.cdev);
    unregister_chrdev_region(MKDEV(btc_major, 0), 1);
    V_EVENT("Exit.\n");
}

MODULE_LICENSE("GPL");
