#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pratik");
MODULE_DESCRIPTION("Kernel Module to Check Virtual Address Mapping");

static unsigned long pid;
static unsigned long vaddr;

module_param(pid, ulong, S_IRUGO);
module_param(vaddr, ulong, S_IRUGO);

static int __init lkm3_init(void) {
    struct task_struct *task;
    struct mm_struct *mm;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long physical_address;
    
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        pr_info("[LKM3] Task with PID %lu not found\n", pid);
        return 0;
    }
    mm = task->mm;
    if (!mm) {
        pr_info("[LKM3] No memory descriptor for PID %lu\n", pid);
        return 0;
    }
    pr_info("[LKM3] Virtual address: %lu\n", vaddr);

    pgd = pgd_offset(mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        pr_info("[LKM3] VA not mapped: Invalid PGD\n");
        return 0;
    }

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        pr_info("[LKM3] VA not mapped: Invalid P4D\n");
        return 0;
    }

    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        pr_info("[LKM3] VA not mapped: Invalid PUD\n");
        return 0;
    }

    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        pr_info("[LKM3] VA not mapped: Invalid PMD\n");
        return 0;
    }
    pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte)) {
        pr_info("[LKM3] VA not mapped: Invalid PTE\n");
        return 0;
    }
    physical_address = pte_pfn(*pte) << PAGE_SHIFT;
    pr_info("[LKM3] Physical address: %lu\n", physical_address);
    return 0;
}
static void __exit lkm3_exit(void) {
    pr_info("[LKM3] Module LKM3 Unloaded\n");
}
module_init(lkm3_init);
module_exit(lkm3_exit);
