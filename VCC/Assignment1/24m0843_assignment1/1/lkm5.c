#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/pid.h>
#include <linux/highmem.h>
#include <linux/huge_mm.h>

MODULE_DESCRIPTION("Module to get THP Count of given Process ID");
MODULE_AUTHOR("Pratik");
MODULE_LICENSE("GPL");

static int pid = 0;

module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "process id");

#define THP_SIZE (2 * 1024 * 1024) 

static int print_memory_usage(void)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    struct vma_iterator vmi;
    unsigned long thp_size = 0;
    unsigned int thp_count = 0;
    unsigned long addr;

    if (pid <= 0) {
        pr_err("[LKM4] Invalid PID: %d\n", pid);
        return -EINVAL;
    }
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        pr_err("[LKM4] No task found with PID=%d\n", pid);
        return -ESRCH;
    }
    mm = task->mm;
    if (!mm) {
        pr_err("[LKM4] No memory mappings for the given process (pid=%d).\n", pid);
        return -ENOMEM;
    }

    down_read(&mm->mmap_lock);
    vma_iter_init(&vmi, mm, 0);

    for_each_vma(vmi, vma) {
        for (addr = vma->vm_start; addr < vma->vm_end; addr += HPAGE_PMD_SIZE) {
            pud_t *pud = pud_offset(p4d_offset(pgd_offset(mm, addr), addr), addr);
            if (!pud || pud_none(*pud))
                continue;

            pmd_t *pmd = pmd_offset(pud, addr);
            if (!pmd || pmd_none(*pmd))
                continue;

            if (pmd_trans_huge(*pmd)) {
                thp_count++;
                thp_size += HPAGE_PMD_SIZE;
            }
        }
    }

    up_read(&mm->mmap_lock);
    pr_info("[LKM4] Total THP Size: %lu KiB, THP Count: %u\n", thp_size / 1024, thp_count);
    return 0;
}

static void km_exit(void)
{
    pr_info("[LKM4] Module Unloaded\n");
}

module_init(print_memory_usage);
module_exit(km_exit);
