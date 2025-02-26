#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("profcs stats sysfs");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

#define SYSFS_DIR "mem_stats"

struct sysfs_entry {
    struct kobject kobj;
    int pid;
    char unit;
};

static unsigned long convert_unit(unsigned long size_in_bytes, char unit) 
{
    switch (unit) {
        case 'K':
            return size_in_bytes >> 10;
        case 'M':
            return size_in_bytes >> 20;
        case 'B':
        default:
            return size_in_bytes;
    }
}

static ssize_t pid_show(struct kobject *kobj, struct kobj_attribute *attr,
                        char *buf) 
{
    struct sysfs_entry *cur_entry = container_of(kobj, struct sysfs_entry, kobj);
    if (cur_entry->pid == -1) 
        goto out;

    struct task_struct *task = pid_task(find_vpid(cur_entry->pid), PIDTYPE_PID);
    if (!task) {
        cur_entry->pid = -1;
        goto out;
    }

out:
    return sysfs_emit(buf, "%d\n", cur_entry->pid);
}

static ssize_t pid_store(struct kobject *kobj, struct kobj_attribute *attr,
                         const char *buf, size_t count) 
{
    struct sysfs_entry *cur_entry = container_of(kobj, struct sysfs_entry, kobj);

    if (kstrtoint(buf, 10, &cur_entry->pid) < 0) 
        return -EINVAL;
    
    if (cur_entry->pid < -1) {
        cur_entry->pid = -1;
        return -EINVAL;
    }
    
    struct task_struct *task = pid_task(find_vpid(cur_entry->pid), PIDTYPE_PID);
    if (!task) {
        cur_entry->pid = -1;
        return -EINVAL;
    }

    return count;
}

unsigned long get_mapped_size(struct mm_struct* mm, struct vm_area_struct* vma) 
{
    unsigned long size = 0;
	pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    
    for (unsigned long vaddr = vma->vm_start; vaddr < vma->vm_end; vaddr += PAGE_SIZE) {
        if (vaddr & (PAGE_SIZE - 1))
            pr_info("[LKM4] Error in iteration\n");
        
		/* Page table walk */
		pgd = pgd_offset(mm, vaddr);
		if (pgd_none(*pgd) || pgd_bad(*pgd))
			continue;

		p4d = p4d_offset(pgd, vaddr);
		if (p4d_none(*p4d) || p4d_bad(*p4d))
			continue;

		pud = pud_offset(p4d, vaddr);
		if(pud_none(*pud) || pud_bad(*pud)) 
			continue;

		pmd = pmd_offset(pud, vaddr);
		if(pmd_none(*pmd) || pmd_bad(*pmd))
			continue;

		pte = pte_offset_kernel(pmd, vaddr);
		if(pte) {
			pte_unmap(*pte);
			
			if (pte_present(*pte))
				size += PAGE_SIZE;
		}
    }
    return size;
}

static ssize_t virtmem_show(struct kobject *kobj, struct kobj_attribute *attr,
                            char *buf) 
{
    struct sysfs_entry *cur_entry = container_of(kobj, struct sysfs_entry, kobj);
    if (cur_entry->pid == -1) 
        goto out;

    rcu_read_lock();
    struct task_struct *task = pid_task(find_vpid(cur_entry->pid), PIDTYPE_PID);
    if (!task) {
        cur_entry->pid = -1;
        goto out;
    }

	struct mm_struct *mm;
	unsigned long virtmem = 0;

	mm = task->mm;
    if (!mm)
        goto out;

    virtmem = (mm->total_vm << PAGE_SHIFT);

    rcu_read_unlock();
    return sysfs_emit(buf, "%lu\n", convert_unit(virtmem, cur_entry->unit));

out:
    rcu_read_unlock();
    return sysfs_emit(buf, "-1\n");
}

static ssize_t physmem_show(struct kobject *kobj, struct kobj_attribute *attr,
                            char *buf) 
{
    struct sysfs_entry *cur_entry = container_of(kobj, struct sysfs_entry, kobj);
    if (cur_entry->pid == -1) 
        goto out;

    rcu_read_lock();
    struct task_struct *task = pid_task(find_vpid(cur_entry->pid), PIDTYPE_PID);
    if (!task) {
        cur_entry->pid = -1;
        goto out;
    }

	struct mm_struct *mm;
	unsigned long physmem = 0;

	mm = task->mm;
    if (!mm)
        goto out;

    struct vm_area_struct *vma;
    VMA_ITERATOR(iter, mm, 0);

    for_each_vma(iter, vma) {
        physmem += get_mapped_size(mm, vma);
    }

    rcu_read_unlock();
    return sysfs_emit(buf, "%lu\n", convert_unit(physmem, cur_entry->unit));

out:
    rcu_read_unlock();
    return sysfs_emit(buf, "-1\n");
}

static ssize_t unit_show(struct kobject *kobj, struct kobj_attribute *attr,
                         char *buf) 
{
    struct sysfs_entry *cur_entry = container_of(kobj, struct sysfs_entry, kobj);
    return sysfs_emit(buf, "%c\n", cur_entry->unit);
}

static ssize_t unit_store(struct kobject *kobj, struct kobj_attribute *attr,
                          const char *buf, size_t count) 
{
    struct sysfs_entry *cur_entry = container_of(kobj, struct sysfs_entry, kobj);
    if (count == 2 && (buf[0] == 'B' || buf[0] == 'K' || buf[0] == 'M')) {
        cur_entry->unit = buf[0];
        return count;
    }
    return -EINVAL;
}

static struct kobj_attribute pid_attr = __ATTR(pid, 0664, pid_show, pid_store);
static struct kobj_attribute virtmem_attr = __ATTR(virtmem, 0444, virtmem_show, NULL);
static struct kobj_attribute physmem_attr = __ATTR(physmem, 0444, physmem_show, NULL);
static struct kobj_attribute unit_attr = __ATTR(unit, 0664, unit_show, unit_store);

static struct attribute *memstats_attrs[] = {
    &pid_attr.attr,
    &virtmem_attr.attr,
    &physmem_attr.attr,
    &unit_attr.attr,
    NULL,
};

ATTRIBUTE_GROUPS(memstats);

static const struct kobj_type memstats_ktype = {
    .sysfs_ops = &kobj_sysfs_ops,
    .default_groups = memstats_groups,
};

struct sysfs_entry *entry = NULL;

static int __init get_memstats_init(void) 
{
    int ret;

    if ((entry = kzalloc(sizeof(struct sysfs_entry), GFP_KERNEL)) == NULL) {
        pr_err("/sys/kernel/%s : Failed to allocate memory for sysfs entry\n", SYSFS_DIR);
        return -ENOMEM;
    }

    /* Initializing sysfs entry */
    entry->pid = -1;
    entry->unit = 'B';

    ret = kobject_init_and_add(&entry->kobj, &memstats_ktype, kernel_kobj, "%s", SYSFS_DIR);
    if (ret) {
        pr_err("/sys/kernel/%s : Failed to create kobject\n", SYSFS_DIR);
        return -ENOMEM;
    }

    return 0;
}

static void __exit get_memstats_exit(void) 
{
    kobject_put(&entry->kobj);
    kfree(entry);
}

module_init(get_memstats_init);
module_exit(get_memstats_exit);
