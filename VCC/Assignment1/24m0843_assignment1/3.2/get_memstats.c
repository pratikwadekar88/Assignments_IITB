#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/pagemap.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pratik");
MODULE_DESCRIPTION("Kernel Module to Expose Memory Statistics");

static int pid = -1;
static char unit = 'B';
static struct kobject *mem_stats_kobj;

static inline pte_t *my_get_locked_pte(struct mm_struct *mm, unsigned long addr, spinlock_t **ptl)
{
    pte_t *ptep;
    pmd_t *pmd;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;

    pgd = pgd_offset(mm, addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        return NULL;

    p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d))
        return NULL;

    pud = pud_offset(p4d, addr);
    if (pud_none(*pud) || pud_bad(*pud))
        return NULL;

    pmd = pmd_offset(pud, addr);
    if (pmd_none(*pmd) || pmd_bad(*pmd))
        return NULL;

    ptep = pte_offset_map_lock(mm, pmd, addr, ptl);
    return ptep;
}

static ssize_t pid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", pid);
}

static ssize_t pid_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    int new_pid;
    if (kstrtoint(buf, 10, &new_pid) == 0)
        pid = new_pid;
    return count;
}

static ssize_t unit_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%c\n", unit);
}

static ssize_t unit_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    if (buf[0] == 'B' || buf[0] == 'K' || buf[0] == 'M')
        unit = buf[0];
    return count;
}

static ssize_t virtmem_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    struct maple_tree *mt;
    MA_STATE(mas, NULL, 0, 0);
    unsigned long virtual_size = 0;

    if (pid < 0)
        return sprintf(buf, "-1\n");

    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task || !task->mm)
        return sprintf(buf, "-1\n");

    mm = task->mm;
    mt = &mm->mm_mt;
    mas_set(&mas, 0);
    mas.tree = mt;

    down_read(&mm->mmap_lock);
    for (vma = mas_find(&mas, ULONG_MAX); vma; vma = mas_find(&mas, ULONG_MAX))
        virtual_size += vma->vm_end - vma->vm_start;
    up_read(&mm->mmap_lock);

    switch (unit) {
    case 'K':
        virtual_size >>= 10;
        break;
    case 'M':
        virtual_size >>= 20;
        break;
    }

    return sprintf(buf, "%lu\n", virtual_size);
}

static ssize_t physmem_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    struct maple_tree *mt;
    MA_STATE(mas, NULL, 0, 0);
    unsigned long physical_size = 0;

    if (pid < 0)
        return sprintf(buf, "-1\n");

    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task || !task->mm)
        return sprintf(buf, "-1\n");

    mm = task->mm;
    mt = &mm->mm_mt;
    mas_set(&mas, 0);
    mas.tree = mt;

    down_read(&mm->mmap_lock);
    for (vma = mas_find(&mas, ULONG_MAX); vma; vma = mas_find(&mas, ULONG_MAX)) {
        for (unsigned long addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            struct page *page;
            pte_t *pte;
            spinlock_t *ptl;

            pte = my_get_locked_pte(mm, addr, &ptl);
            if (!pte)
                continue;

            if (pte_present(*pte)) {
                page = pte_page(*pte);
                if (page)
                    physical_size += PAGE_SIZE;
            }

            pte_unmap_unlock(pte, ptl);
        }
    }
    up_read(&mm->mmap_lock);

    switch (unit) {
    case 'K':
        physical_size >>= 10;
        break;
    case 'M':
        physical_size >>= 20;
        break;
    }

    return sprintf(buf, "%lu\n", physical_size);
}

static struct kobj_attribute pid_attr = __ATTR(pid, 0644, pid_show, pid_store);
static struct kobj_attribute unit_attr = __ATTR(unit, 0644, unit_show, unit_store);
static struct kobj_attribute virtmem_attr = __ATTR(virtmem, 0444, virtmem_show, NULL);
static struct kobj_attribute physmem_attr = __ATTR(physmem, 0444, physmem_show, NULL);

static int __init get_memstats_init(void)
{
    int ret;

    mem_stats_kobj = kobject_create_and_add("mem_stats", kernel_kobj);
    if (!mem_stats_kobj)
        return -ENOMEM;

    ret = sysfs_create_file(mem_stats_kobj, &pid_attr.attr);
    if (ret)
        goto error;

    ret = sysfs_create_file(mem_stats_kobj, &unit_attr.attr);
    if (ret)
        goto error;

    ret = sysfs_create_file(mem_stats_kobj, &virtmem_attr.attr);
    if (ret)
        goto error;

    ret = sysfs_create_file(mem_stats_kobj, &physmem_attr.attr);
    if (ret)
        goto error;

    return 0;

error:
    kobject_put(mem_stats_kobj);
    return ret;
}

static void __exit get_memstats_exit(void)
{
    kobject_put(mem_stats_kobj);
}

module_init(get_memstats_init);
module_exit(get_memstats_exit);
