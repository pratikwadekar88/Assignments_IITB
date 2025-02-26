#include <linux/kernel.h>      // Core kernel functions and definitions (e.g., pr_info)
#include <linux/init.h>        // Macros for module initialization and cleanup (__init and __exit)
#include <linux/module.h>      // Essential module definitions and macros (e.g., MODULE_LICENSE)
#include <linux/sched.h>       // Process related definitions (e.g., task_struct)
#include <linux/pid.h>         // PID related helper functions (e.g., pid_task, find_vpid)
#include <linux/pgtable.h>     // Definitions for page table structures and macros (e.g., pgd_t, pte_t)
#include <linux/moduleparam.h> // Facilities for handling module parameters

/*
 * Module Metadata:
 *  - MODULE_DESCRIPTION: Brief description of what the module does.
 *  - MODULE_AUTHOR: The author or organization responsible for the module.
 *  - MODULE_LICENSE: The license under which the module is released. "GPL" is often required.
 */
MODULE_DESCRIPTION("LKM3 - Module to translate a virtual address to a physical address for a given process.");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

/*
 * Module Parameter: pid
 *
 * Purpose:
 *   Holds the Process ID (PID) of the process for which the virtual-to-physical address
 *   translation will be performed. The default value is set to 1 (commonly the init process).
 *
 * Details:
 *   - Declared as a static int with an initial value of 1.
 *   - The module_param macro makes 'pid' configurable at module load time.
 *   - MODULE_PARM_DESC provides a description for the parameter.
 *
 * Alternative:
 *   Use module_param_named() if you need the internal variable name to differ from the parameter name.
 */
static int pid = 1;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "PID of the process");

/*
 * Module Parameter: vaddr
 *
 * Purpose:
 *   Holds the virtual address (VA) in the process's address space that is to be translated
 *   into a physical address. It is declared as an unsigned long long integer.
 *
 * Details:
 *   - The default value is 0.
 *   - module_param makes it configurable at load time.
 *   - MODULE_PARM_DESC provides a description for the parameter.
 *
 * Note:
 *   - The type 'ullong' corresponds to unsigned long long.
 */
static unsigned long long int vaddr = 0;
module_param(vaddr, ullong, 0);
MODULE_PARM_DESC(vaddr, "Virtual address of the process");

/*
 * Function: print_physical_address
 *
 * Purpose:
 *   This initialization function is executed when the module is loaded.
 *   It performs a page table walk for the specified process (via 'pid') to translate
 *   the given virtual address ('vaddr') into the corresponding physical address.
 *
 * Process:
 *   1. Locate the task_struct corresponding to the given PID.
 *   2. Access the process's memory descriptor (mm_struct).
 *   3. Walk through the multi-level page table (PGD, P4D, PUD, PMD, PTE) to ensure
 *      that the page is present.
 *   4. Calculate the physical address by combining the page frame number (from the PTE)
 *      and the offset within the page.
 *
 * Parameters:
 *   - None explicitly passed to the function; it uses the module parameters 'pid' and 'vaddr'.
 *
 * Returns:
 *   0 upon completion.
 *
 * Kernel Constructs and Functions Used:
 *   - rcu_read_lock()/rcu_read_unlock(): Protects the page table traversal from concurrent modifications.
 *   - pid_task() and find_vpid(): To obtain the task_struct for the given PID.
 *   - pgd_offset(), p4d_offset(), pud_offset(), pmd_offset(), pte_offset_kernel():
 *     Macros to traverse through different levels of the page table hierarchy.
 *
 * Alternative:
 *   Depending on architecture, some systems might use different mechanisms or additional levels
 *   in the page table walk. The provided alternatives in the code comments show different ways to
 *   calculate the physical address.
 */
static __init int print_physical_address(void)
{
    // Pointer to the task_struct of the target process
    struct task_struct *task = NULL;
    // Pointer to the process's memory descriptor (mm_struct)
    struct mm_struct *mm;
    // Variables for each level of the page table hierarchy
    pgd_t* pgd;
    p4d_t* p4d;
    pud_t* pud;
    pmd_t* pmd;
    pte_t* pte;

    // Variable to hold the final calculated physical address
    unsigned long long int phy_addr;

    // Acquire an RCU read lock to protect against concurrent modifications during traversal.
    rcu_read_lock();

    // Find the task_struct for the given PID using the kernel's PID lookup functions.
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    
    // If the process is not found, log the information and exit.
    if (task == NULL) {
        pr_info("[LKM3] Process with PID: %d not found\n", pid);
        rcu_read_unlock();
        return 0;
    }

    // Obtain the memory descriptor (mm_struct) for the process.
    // The mm_struct contains information about the process's memory layout, including the page table.
    mm = task->mm;
    
    /*
     * Page Table Walk:
     * Each of the following steps navigates one level in the page table hierarchy.
     * At each level, we check whether the corresponding entry is valid, present, and not corrupted.
     */
    
    // Level 1: Page Global Directory (PGD)
    pgd = pgd_offset(mm, vaddr);
    if (pgd_none(*pgd) || !pgd_present(*pgd) || pgd_bad(*pgd)) {
        pr_info("[LKM3] Page not present: pgd missing\n");
        rcu_read_unlock();
        return 0;
    }

    // Level 2: Page 4th-level Directory (P4D)
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || !p4d_present(*p4d) || p4d_bad(*p4d)) {
        pr_info("[LKM3] Page not present: p4d missing\n");
        rcu_read_unlock();
        return 0;
    }
    
    // Level 3: Page Upper Directory (PUD)
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || !pud_present(*pud) || pud_bad(*pud)) {
        pr_info("[LKM3] Page not present: pud missing\n");
        rcu_read_unlock();
        return 0;
    }

    // Level 4: Page Middle Directory (PMD)
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || !pmd_present(*pmd) || pmd_bad(*pmd)) {
        pr_info("[LKM3] Page not present: pmd missing\n");
        rcu_read_unlock();
        return 0;
    }

    // Level 5: Page Table Entry (PTE)
    // pte_offset_kernel() is used for kernel space mappings; for user space mappings, pte_offset_map might be used.
    pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte) || !pte_present(*pte)) {
        pr_info("[LKM3] VA unmapped\n");
        rcu_read_unlock();
        return 0;
    }

    /*
     * Calculate the physical address:
     * - pte_pfn(*pte) extracts the page frame number from the PTE.
     * - PAGE_SHIFT is the number of bits to shift to convert the page frame number to an address.
     * - The offset within the page is obtained by masking 'vaddr' with ~PAGE_MASK.
     *
     * Alternative approaches (uncomment one as needed for your architecture):
     *   phy_addr = (pte_val(*pte) & PTE_PFN_MASK) | (vaddr & ~PAGE_MASK); // Works on x86_64
     *   phy_addr = (pte_val(*pte) & PTE_ADDR_MASK) | (vaddr & ~PAGE_MASK); // Works on arm64
     *   phy_addr = (__pte_to_phys(*pte)) | (vaddr & ~PAGE_MASK);           // Alternative for arm64
     */
    phy_addr = (pte_pfn(*pte) << PAGE_SHIFT) | (vaddr & ~PAGE_MASK);

    // Release the RCU read lock after finishing the page table walk.
    rcu_read_unlock();

    // Log the virtual and physical addresses in both hexadecimal and unsigned long long formats.
    pr_info("[LKM3] Virtual address: 0x%llx / %llu\n", vaddr, vaddr);
    pr_info("[LKM3] Physical address: 0x%llx / %llu\n", phy_addr, phy_addr);

    return 0;
}

/*
 * Function: exit_module
 *
 * Purpose:
 *   Cleanup function called when the module is unloaded.
 *   Logs a message indicating that the module is being removed.
 *
 * Parameters:
 *   - None.
 *
 * Returns:
 *   - Nothing (void function).
 */
static __exit void exit_module(void)
{
    pr_info("[LKM3] Module LKM3 Unloaded\n");
}

/*
 * Module Initialization and Exit Macros:
 *  - module_init() registers the initialization function to be executed upon module load.
 *  - module_exit() registers the cleanup function to be executed when the module is unloaded.
 */
module_init(print_physical_address);
module_exit(exit_module);
