#include <linux/kernel.h>      // Core kernel functions and macros (e.g., pr_info)
#include <linux/init.h>        // Macros for module initialization and cleanup (__init and __exit)
#include <linux/module.h>      // Essential module definitions and macros (e.g., MODULE_LICENSE)
#include <linux/sched.h>       // Process-related definitions (e.g., task_struct)
#include <linux/sched/signal.h>// Access to signal-related functions and macros for tasks
#include <linux/pgtable.h>     // Definitions for page table structures and walking the page tables
#include <linux/moduleparam.h> // Facilities for handling module parameters
#include <linux/sched/mm.h>    // Definitions related to a process's memory management (mm_struct)
#include <linux/mm.h>          // General memory management definitions and functions

/*
 * Module Metadata:
 *   MODULE_DESCRIPTION: Brief description of the module's functionality.
 *   MODULE_AUTHOR: Identifies the author or organization.
 *   MODULE_LICENSE: Specifies the licensing; "GPL" ensures compatibility with many kernel symbols.
 */
MODULE_DESCRIPTION("LKM4 - Module to calculate virtual and physical memory sizes for a given process.");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

/*
 * Module Parameter: pid
 *
 * Purpose:
 *   Specifies the Process ID (PID) of the process whose memory allocation details
 *   will be examined. By default, it is set to 1 (commonly the init process).
 *
 * Details:
 *   - Declared as a static integer with an initial value of 1.
 *   - The module_param macro makes 'pid' configurable at module load time.
 *   - MODULE_PARM_DESC provides a description for the parameter.
 *
 * Alternative:
 *   Use module_param_named() if you need the external parameter name to differ from the internal variable name.
 */
static int pid = 1;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "PID of the process");

/*
 * Function: get_mapped_size
 *
 * Purpose:
 *   Calculates the total size (in bytes) of memory pages that are mapped (i.e., present)
 *   within a given Virtual Memory Area (VMA) of a process.
 *
 * Parameters:
 *   - mm: Pointer to the process's memory descriptor (mm_struct), which contains all VMAs.
 *   - vma: Pointer to a specific Virtual Memory Area (vm_area_struct) within the process.
 *
 * Process:
 *   The function iterates from the start (vma->vm_start) to the end (vma->vm_end) of the VMA,
 *   stepping through one page (PAGE_SIZE) at a time.
 *
 *   For each virtual address:
 *     1. It performs a page table walk (PGD, P4D, PUD, PMD, and finally PTE) to locate the page entry.
 *     2. If at any level the entry is missing or invalid, it skips the current address.
 *     3. If a valid Page Table Entry (PTE) is found and the page is present, the function increments
 *        the 'size' by PAGE_SIZE.
 *
 * Returns:
 *   The total mapped size (in bytes) for the specified VMA.
 *
 * Alternative:
 *   Depending on the architecture or needs, you might consider using existing kernel functions that
 *   sum up mapped memory, but this explicit page table walk provides precise control and insight.
 */
unsigned long get_mapped_size(struct mm_struct *mm, struct vm_area_struct *vma) 
{
    unsigned long size = 0;  // Accumulator for mapped memory size (in bytes)
    pgd_t *pgd;              // Pointer to Page Global Directory entry
    p4d_t *p4d;              // Pointer to Page 4th-level Directory entry
    pud_t *pud;              // Pointer to Page Upper Directory entry
    pmd_t *pmd;              // Pointer to Page Middle Directory entry
    pte_t *pte;              // Pointer to Page Table Entry

    // Iterate through the VMA one page at a time.
    for (unsigned long vaddr = vma->vm_start; vaddr < vma->vm_end; vaddr += PAGE_SIZE) {
        // Check for proper alignment of the virtual address.
        if (vaddr & (PAGE_SIZE - 1))
            pr_info("[LKM4] Error in iteration: vaddr 0x%lx not aligned to PAGE_SIZE\n", vaddr);

        // Start of page table walk: Level 1 (PGD)
        pgd = pgd_offset(mm, vaddr);
        if (pgd_none(*pgd) || pgd_bad(*pgd))
            continue;  // Skip if PGD is not present or is corrupted

        // Level 2: P4D (Page 4th-level Directory)
        p4d = p4d_offset(pgd, vaddr);
        if (p4d_none(*p4d) || p4d_bad(*p4d))
            continue;  // Skip if P4D is not present or is corrupted

        // Level 3: PUD (Page Upper Directory)
        pud = pud_offset(p4d, vaddr);
        if (pud_none(*pud) || pud_bad(*pud)) 
            continue;  // Skip if PUD is not present or is corrupted

        // Level 4: PMD (Page Middle Directory)
        pmd = pmd_offset(pud, vaddr);
        if (pmd_none(*pmd) || pmd_bad(*pmd))
            continue;  // Skip if PMD is not present or is corrupted

        // Level 5: PTE (Page Table Entry)
        // pte_offset_kernel is used for kernel address space; for user space mappings, consider pte_offset_map.
        pte = pte_offset_kernel(pmd, vaddr);
        if (pte) {
            // Unmap the PTE mapping if necessary (this ensures consistency in certain architectures)
            pte_unmap(*pte);
            
            // If the PTE indicates the page is present, add PAGE_SIZE to the accumulated size.
            if (pte_present(*pte))
                size += PAGE_SIZE;
        }
    }
    return size;
}

/*
 * Function: print_alloc_size
 *
 * Purpose:
 *   This is the module's initialization function. When the module is loaded, it:
 *     1. Locates the process specified by the module parameter 'pid'.
 *     2. Retrieves the process's memory descriptor (mm_struct).
 *     3. Calculates the virtual memory size using the total_vm field.
 *     4. Estimates the physical memory size using get_mm_rss (a fast but less precise measure).
 *     5. Computes a precise physical memory size by iterating through each VMA and summing the
 *        sizes of mapped pages.
 *     6. Logs these memory size values (in KiB).
 *
 * Parameters:
 *   - None explicitly passed; relies on the module parameter 'pid'.
 *
 * Returns:
 *   0 upon successful completion.
 *
 * Alternative:
 *   - The virtual memory size can be computed iteratively by summing each VMA's range.
 *   - The physical memory size can be determined using different counters in mm->rss_stat,
 *     though get_mm_rss() encapsulates these counts.
 */
static int __init print_alloc_size(void)
{
    struct task_struct *task;     // Pointer to the process's task_struct
    struct mm_struct *mm;         // Pointer to the process's memory descriptor
    unsigned long virtual_size = 0; // Virtual memory size (in KiB)
    unsigned long physical_size = 0; // Physical memory size (in KiB)

    // Look up the task_struct for the specified PID.
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (task == NULL) {
        pr_info("[LKM4] Process with PID: %d not found\n", pid);
        return 0;
    }

    // Get the memory descriptor for the process.
    mm = task->mm;
    if (mm != NULL) {
        /*
         * Calculate virtual memory size:
         *   - mm->total_vm holds the total number of pages allocated.
         *   - Shifting left by PAGE_SHIFT converts it to bytes.
         *   - Dividing by 1024 converts bytes to KiB.
         *
         * Alternative:
         *   The virtual memory size could also be computed by iterating over each VMA and summing
         *   (vma->vm_end - vma->vm_start) for all VMAs.
         */
        virtual_size = (mm->total_vm << PAGE_SHIFT) / 1024;
        pr_info("[LKM4] Virtual Memory Size: %lu KiB\n", virtual_size);

        /*
         * Calculate estimated physical memory size:
         *   - get_mm_rss(mm) returns the resident set size (number of resident pages).
         *   - Converting this to bytes and then to KiB.
         *
         * Alternative:
         *   One can also sum up the various page counts (file-backed, anonymous, shared memory)
         *   available in mm->rss_stat. This is essentially what get_mm_rss() does internally.
         */
        physical_size = (get_mm_rss(mm) << PAGE_SHIFT) / 1024;
        pr_info("[LKM4] Estimated Physical Memory Size: %lu KiB\n", physical_size);

        /*
         * Calculate precise physical memory size (this method is more accurate but slower):
         *   - Iterate over each VMA in the process's address space.
         *   - For each VMA, use get_mapped_size() to sum the sizes of pages that are actually mapped.
         *   - Reset physical_size to zero before performing the precise calculation.
         *
         *   The VMA_ITERATOR macro along with for_each_vma simplifies iterating over all VMAs.
         */
        {
            struct vm_area_struct *vma; // Pointer for iterating through each VMA
            VMA_ITERATOR(iter, mm, 0);  // Macro to initialize the VMA iterator
            physical_size = 0;          // Reset precise physical size accumulator

            // Iterate through each VMA and add its mapped size.
            for_each_vma(iter, vma) {
                physical_size += get_mapped_size(mm, vma);
            }
        }
		

        // Convert precise physical size from bytes to KiB.
        physical_size /= 1024;
        pr_info("[LKM4] Precise Physical Memory Size: %lu KiB\n", physical_size);
    } else {
        pr_info("[LKM4] mm is NULL\n");
    }

    return 0;
}

/*
 * Function: exit_module
 *
 * Purpose:
 *   This is the module's cleanup function. When the module is unloaded,
 *   it logs that the module has been removed.
 *
 * Parameters:
 *   - None.
 *
 * Returns:
 *   - Nothing (void function).
 */
static void __exit exit_module(void)
{
    pr_info("[LKM4] Module LKM4 Unloaded\n");
}

/*
 * Module Initialization and Exit Macros:
 *   - module_init() registers the initialization function (print_alloc_size) to be called when the module is loaded.
 *   - module_exit() registers the cleanup function (exit_module) to be called when the module is unloaded.
 */
module_init(print_alloc_size);
module_exit(exit_module);
