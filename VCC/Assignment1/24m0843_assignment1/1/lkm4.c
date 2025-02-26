#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/rmap.h>
#include <linux/pagemap.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pratik");
MODULE_DESCRIPTION("LKM4 - Memory Observation");

static int pid = -1;
module_param(pid, int, 0444);
MODULE_PARM_DESC(pid, "Process ID");
static inline pte_t *my_get_locked_pte(struct mm_struct *mm, unsigned long addr, spinlock_t **ptl){
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

static int __init lkm4_init(void) {
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    unsigned long virtual_size = 0;
    unsigned long physical_size = 0;
    struct maple_tree *mt;
    MA_STATE(mas, NULL, 0, 0);

    if (pid < 0) {
        pr_err("[LKM4] Invalid PID\n");
        return -EINVAL;
    }

    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        pr_err("[LKM4] Task not found\n");
        return -ESRCH;
    }

    mm = task->mm;
    if (!mm) {
        pr_err("[LKM4] Task has no memory structure\n");
        return -EFAULT;
    }

    mt = &mm->mm_mt;
    mas_set(&mas, 0);
    mas.tree = mt;

    down_read(&mm->mmap_lock);
    for (vma = mas_find(&mas, ULONG_MAX); vma; vma = mas_find(&mas, ULONG_MAX)) {
        unsigned long vma_size = vma->vm_end - vma->vm_start;
        virtual_size += vma_size;

        for (unsigned long addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            struct page *page;
            pte_t *pte;
            spinlock_t *ptl;

            pte = my_get_locked_pte(mm, addr, &ptl);
            if (!pte) {
                continue;
            }

            if (pte_present(*pte)) {
                page = pte_page(*pte);
                if (page) {
                    physical_size += PAGE_SIZE;
                }
            }
            pte_unmap_unlock(pte, ptl);
        }
    }
    up_read(&mm->mmap_lock);

    pr_info("[LKM4] Virtual Memory Size: %lu KiB\n", virtual_size >>10 );
    pr_info("[LKM4] Physical Memory Size: %lu KiB\n", physical_size>> 10 );

    return 0;
}
static void __exit lkm4_exit(void) {
    pr_info("[LKM4] Module LKM4 Unloaded\n");
}

module_init(lkm4_init);
module_exit(lkm4_exit);
