#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/device.h>
#include <asm/io.h>

#define DEVICE_NAME "ioctl_vtop"
#define CLASS_NAME "ioctl_class"

#define IOCTL_GET_PHYS_ADDR _IOWR('p', 1, unsigned long[2]) 
#define IOCTL_WRITE_PHYS_VAL _IOW('p', 2, unsigned long[2])

static int major_number;
static struct class *vtop_class = NULL;
static struct device *vtop_device = NULL;

static long vtop_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    unsigned long user_data[2];
    struct task_struct *task;
    struct mm_struct *mm;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long pid, vaddr, physical_address;

    switch (cmd) {
    case IOCTL_GET_PHYS_ADDR:   
        if (copy_from_user(&user_data, (unsigned long __user *)arg, sizeof(user_data))) {
            return -EFAULT;
        }

        pid = user_data[0];
        vaddr = user_data[1];

        task = pid_task(find_vpid(pid), PIDTYPE_PID);
        if (!task) {
            pr_info("[vtop_mapper] Task with PID %lu not found\n", pid);
            return -ESRCH;
        }

        mm = task->mm;
        if (!mm) {
            pr_info("[vtop_mapper] No memory descriptor for PID %lu\n", pid);
            return -EFAULT;
        }

        pgd = pgd_offset(mm, vaddr);
        if (pgd_none(*pgd) || pgd_bad(*pgd))
            return -EFAULT;

        p4d = p4d_offset(pgd, vaddr);
        if (p4d_none(*p4d) || p4d_bad(*p4d))
            return -EFAULT;

        pud = pud_offset(p4d, vaddr);
        if (pud_none(*pud) || pud_bad(*pud))
            return -EFAULT;

        pmd = pmd_offset(pud, vaddr);
        if (pmd_none(*pmd) || pmd_bad(*pmd))
            return -EFAULT;

        pte = pte_offset_kernel(pmd, vaddr);
        if (!pte || pte_none(*pte))
            return -EFAULT;

        physical_address = (pte_pfn(*pte) << PAGE_SHIFT) | (vaddr & ~PAGE_MASK);

        if (copy_to_user((unsigned long __user *)arg, &physical_address, sizeof(physical_address))) {
            return -EFAULT;
        }

        pr_info("[vtop_mapper] PID: %lu, VA: %lu -> PA: %lu\n", pid, vaddr, physical_address);
        return 0;

    case IOCTL_WRITE_PHYS_VAL:
        if (copy_from_user(&user_data, (unsigned long __user *)arg, sizeof(user_data))) {
            return -EFAULT;
        }

        physical_address = user_data[0];
        unsigned long value = user_data[1];

        void __iomem *mapped_address = ioremap(physical_address, sizeof(value));
        if (!mapped_address) {
            pr_info("[vtop_mapper] Failed to map physical address: %lu\n", physical_address);
            return -ENOMEM;
        }

        iowrite32(value, mapped_address);
        iounmap(mapped_address);

        pr_info("[vtop_mapper] Wrote value %lu to physical address %lu\n", value, physical_address);
        return 0;

    default:
        return -EINVAL;
    }
}

static int vtop_open(struct inode *inodep, struct file *filep) {
    return 0;
}

static int vtop_release(struct inode *inodep, struct file *filep) {
    return 0;
}

static struct file_operations fops = {
    .open = vtop_open,
    .release = vtop_release,
    .unlocked_ioctl = vtop_ioctl,
};

static int __init vtop_init(void) {
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        pr_alert("[vtop_mapper] Failed to register a major number\n");
        return major_number;
    }

    vtop_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(vtop_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        pr_alert("[vtop_mapper] Failed to register device class\n");
        return PTR_ERR(vtop_class);
    }

    vtop_device = device_create(vtop_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(vtop_device)) {
        class_destroy(vtop_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        pr_alert("[vtop_mapper] Failed to create the device\n");
        return PTR_ERR(vtop_device);
    }

    pr_info("[vtop_mapper] Device initialized successfully\n");
    return 0;
}

static void __exit vtop_exit(void) {
    device_destroy(vtop_class, MKDEV(major_number, 0));
    class_unregister(vtop_class);
    class_destroy(vtop_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    pr_info("[vtop_mapper] Module unloaded\n");
}

module_init(vtop_init);
module_exit(vtop_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pratik");
MODULE_DESCRIPTION("Virtual to Physical Address Mapping Kernel Module with Write Support");
MODULE_VERSION("1.1");
