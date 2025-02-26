#include <linux/kernel.h>       // Core kernel functions and macros (e.g., pr_info)
#include <linux/init.h>         // Macros for module initialization and cleanup (__init and __exit)
#include <linux/module.h>       // Essential module definitions and macros (e.g., MODULE_LICENSE)
#include <linux/sched.h>        // Process-related definitions (e.g., task_struct)
#include <linux/sched/signal.h> // Provides access to task signal structures and helper macros
#include <linux/pgtable.h>      // Definitions for page table structures and helper functions/macros
#include <linux/moduleparam.h>  // Facilities for handling module parameters
#include <linux/sched/mm.h>     // Definitions related to a process's memory management (mm_struct)
#include <linux/mm.h>           // General memory management definitions and functions
#include <linux/pagewalk.h>     // Interfaces for walking through page tables
#include <linux/huge_mm.h>      // Definitions and helpers for huge page support (THP)
#include <linux/pgtable.h>      // (Repeated) Provides page table macros; can be used for clarity

/*
 * Module Metadata:
 *   MODULE_DESCRIPTION: Brief description of the module's functionality.
 *   MODULE_AUTHOR: Identifies the author or organization.
 *   MODULE_LICENSE: Specifies the license under which the module is distributed.
 */
MODULE_DESCRIPTION("LKM5 - Module to gather Transparent Huge Pages (THP) statistics for a given process.");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

/*
 * Module Parameter: pid
 *
 * Purpose:
 *   Specifies the Process ID (PID) of the process whose THP statistics will be gathered.
 *   By default, it is set to 1 (commonly the init process).
 *
 * Details:
 *   - Declared as a static integer with an initial value of 1.
 *   - module_param makes it configurable at module load time.
 *   - MODULE_PARM_DESC provides a description for this parameter.
 *
 * Alternative:
 *   Use module_param_named() if the external parameter name should differ from the internal variable name.
 */
static int pid = 1;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "PID of the process");

/*
 * Structure: thp_stats
 *
 * Purpose:
 *   Holds the statistics for Transparent Huge Pages (THP) within a Virtual Memory Area (VMA).
 *
 * Members:
 *   - thp_count: The number of THP pages found.
 *   - thp_size: The cumulative size (in bytes) of THP pages found.
 */
struct thp_stats {
    unsigned long thp_count; // Count of THP pages encountered
    unsigned long thp_size;  // Total size of THP pages (in bytes)
};

/*
 * Function: thp_gather_stats
 *
 * Purpose:
 *   Walks through a given VMA (Virtual Memory Area) for a process and gathers statistics
 *   about Transparent Huge Pages (THP). For each page in the VMA, it checks whether a THP is present.
 *
 * Parameters:
 *   - mm: Pointer to the process's memory descriptor (mm_struct), which holds all VMAs.
 *   - vma: Pointer to a specific Virtual Memory Area (vm_area_struct) in the process.
 *   - thp: Pointer to a thp_stats structure where the THP count and size will be accumulated.
 *
 * Process:
 *   - Iterates over the VMA in steps of PAGE_SIZE or HPAGE_PMD_SIZE (if a THP is detected).
 *   - For each virtual address within the VMA, it performs a page table walk (PGD → P4D → PUD → PMD).
 *   - Checks if the PMD entry is present and whether it represents a THP using pmd_trans_huge().
 *   - If a THP is found:
 *       - Increments the THP count.
 *       - Adds the size of a huge page (HPAGE_PMD_SIZE) to thp_size.
 *       - Advances the virtual address by HPAGE_PMD_SIZE.
 *   - Otherwise, it advances by PAGE_SIZE.
 *
 * Alternative:
 *   - An alternative approach (commented out) uses get_user_pages_remote() to retrieve pages,
 *     then checks PageTransHuge() on the page. This method is less direct and may involve extra overhead.
 */
static void thp_gather_stats(struct mm_struct *mm, struct vm_area_struct *vma, struct thp_stats *thp)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;

    // Iterate from the start to the end of the VMA.
    for (unsigned long vaddr = vma->vm_start; vaddr < vma->vm_end; ) {
        // Check if the virtual address is properly aligned to PAGE_SIZE.
        if (vaddr & (PAGE_SIZE - 1))
            pr_info("[LKM5] Error in iteration: vaddr 0x%lx not aligned\n", vaddr);

        int thp_present = 0;

        /* 
         * Alternative approach (commented out):
         * Using get_user_pages_remote() to retrieve the page and then check for THP.
         *
         * struct page *page;
         * if (!(get_user_pages_remote(mm, vaddr, 1, 0, &page, &vma, NULL) <= 0)) {
         *     if (PageTransHuge(page)) {
         *         thp->thp_size += HPAGE_PMD_SIZE;
         *         thp->thp_count++;
         *         vaddr += HPAGE_PMD_SIZE;
         *         put_page(page);
         *         continue;
         *     }
         *     put_page(page);
         *     vaddr += PAGE_SIZE;
         * }
         */

        // Begin page table walk to check for THP:
        pgd = pgd_offset(mm, vaddr);
        if (pgd_none(*pgd) || pgd_bad(*pgd))
            continue;

        p4d = p4d_offset(pgd, vaddr);
        if (p4d_none(*p4d) || p4d_bad(*p4d))
            continue;

        pud = pud_offset(p4d, vaddr);
        if (pud_none(*pud) || pud_bad(*pud))
            continue;

        pmd = pmd_offset(pud, vaddr);
        // If the PMD entry is not present, skip to the next iteration.
        if (!pmd_present(*pmd))
            continue;

        // Check if the PMD entry represents a Transparent Huge Page.
        if (pmd && pmd_trans_huge(*pmd))
            thp_present = 1;

        if (thp_present) {
            // If THP is present, accumulate its size and count.
            thp->thp_size += HPAGE_PMD_SIZE;
            thp->thp_count++;
            // Advance by the size of a huge page.
            vaddr += HPAGE_PMD_SIZE;
        } else {
            // Otherwise, advance by one normal page.
            vaddr += PAGE_SIZE;
        }
    }
}

/*
 * Function: print_thp_size
 *
 * Purpose:
 *   This is the module's initialization function. When the module is loaded, it:
 *     1. Checks if Transparent Huge Pages (THP) are supported.
 *     2. Locates the process specified by the module parameter 'pid'.
 *     3. Iterates over all VMAs of the process to gather THP statistics.
 *     4. Sums up the total THP size (in KiB) and count.
 *     5. Logs the gathered THP statistics.
 *
 * Parameters:
 *   - None explicitly passed; the function uses the module parameter 'pid'.
 *
 * Returns:
 *   0 upon successful completion.
 *
 * Alternative:
 *   - If THP is not supported (checked via has_transparent_hugepage()), the function logs a message and exits.
 */
static int __init print_thp_size(void)
{
    struct task_struct *task; // Pointer to the process's task_struct
    struct mm_struct *mm;     // Pointer to the process's memory descriptor

    unsigned long local_thp_size = 0;  // Accumulator for total THP size (in bytes)
    unsigned long local_thp_count = 0; // Accumulator for total THP count

    // Check if Transparent Huge Pages are supported on this system.
    if (!has_transparent_hugepage()) {
        pr_info("[LKM5] Transparent hugepage is not supported\n");
        return 0;
    }

    // Retrieve the task_struct for the specified PID.
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (task == NULL) {
        pr_info("[LKM5] Process with PID: %d not found\n", pid);
        return 0;
    }
    mm = task->mm;
    if (mm != NULL) {
        struct vm_area_struct *vma;

        // Use VMA_ITERATOR to iterate over all VMAs of the process.
        VMA_ITERATOR(iter, mm, 0);

        // For each VMA in the process, gather THP statistics.
        for_each_vma(iter, vma) {
            struct thp_stats thp;
            // Initialize the thp_stats structure to zero.
            memset(&thp, 0, sizeof(thp));

            // Gather THP statistics for the current VMA.
            thp_gather_stats(mm, vma, &thp);
            // Accumulate the results into local variables.
            local_thp_count += thp.thp_count;
            local_thp_size += thp.thp_size;
        }

        // Convert the total THP size from bytes to KiB.
        local_thp_size /= 1024;

        // Log the total THP size and count.
        pr_info("[LKM5] THP Size: %lu KiB, THP count: %lu\n", local_thp_size, local_thp_count);
    }
    else {
        pr_info("[LKM5] mm is NULL\n");
    }

    return 0;
}

/*
 * Function: exit_module
 *
 * Purpose:
 *   This is the module's cleanup function. It is called when the module is unloaded.
 *   It logs a message indicating that the module is being removed.
 *
 * Parameters:
 *   - None.
 *
 * Returns:
 *   - Nothing (void function).
 */
static void __exit exit_module(void)
{
    pr_info("[LKM5] Module LKM5 Unloaded\n");
}

/*
 * Module Initialization and Exit Macros:
 *   - module_init() registers the initialization function (print_thp_size) to be called when the module is loaded.
 *   - module_exit() registers the cleanup function (exit_module) to be called when the module is unloaded.
 */
module_init(print_thp_size);
module_exit(exit_module);
