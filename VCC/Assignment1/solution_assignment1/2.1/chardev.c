#include <asm/io.h>     // For low-level I/O functions such as ioremap/iounmap.
#include <linux/cdev.h> // For character device registration (cdev structure, cdev_init, cdev_add, etc.).
#include <linux/device.h> // For creating device files in /dev and sysfs class support.
#include <linux/errno.h> // Standard error codes.
#include <linux/fs.h> // Filesystem support (registering device numbers, file operations, etc.)
#include <linux/highmem.h> // For mapping high memory pages (kmap, kunmap, etc.).
#include <linux/init.h>    // Macros used for module initialization and cleanup.
#include <linux/kernel.h> // Provides kernel macros and functions (e.g., pr_info).
#include <linux/mm_types.h> // Memory management type definitions (e.g., mm_struct).
#include <linux/module.h> // Core header for loading LKMs into the kernel.
#include <linux/pgtable.h> // Definitions for walking the page table (pgd_t, pte_t, etc.).
#include <linux/sched.h> // Defines task_struct and other scheduling functions.
#include <linux/sched/signal.h> // For iterating over tasks and signal handling.

#include "chardev.h" // Local header. Must define DEVICE_NAME and the structure query_arg_t.

/*
 * Define the first minor number and number of minors.
 * This driver only supports one minor device.
 */
#define FIRST_MINOR 0
#define MINOR_CNT 1

// Global variables for device registration
static dev_t
    dev; // Variable to store the allocated device number (major + minor).
static struct cdev c_dev; // Character device structure representing our device.
static struct class
    *cl; // Pointer to our device class (for sysfs and udev integration).

/*
 * Function: page_table_walk
 * -------------------------
 * Description:
 *   This function performs a manual page table walk for a given virtual address
 * to determine the corresponding physical address in a process's address space.
 *
 * Parameters:
 *   - virt_addr: The virtual address that needs to be translated.
 *   - mm: Pointer to the memory descriptor (mm_struct) of the process.
 *
 * Returns:
 *   - The physical address corresponding to the given virtual address.
 *   - If any level of the page table is not present or is invalid, it logs a
 * message and returns 0.
 *
 * Process:
 *   1. Retrieve the PGD (Page Global Directory) entry using pgd_offset().
 *   2. Retrieve the P4D (Page 4th-level Directory) entry using p4d_offset().
 *   3. Retrieve the PUD (Page Upper Directory) entry using pud_offset().
 *   4. Retrieve the PMD (Page Middle Directory) entry using pmd_offset().
 *   5. Retrieve the PTE (Page Table Entry) using pte_offset_kernel().
 *   6. Compute the physical address by combining the page frame number and the
 * page offset.
 */
static unsigned long page_table_walk(unsigned long virt_addr,
                                     struct mm_struct *mm) {
  pgd_t *pgd;
  p4d_t *p4d;
  pud_t *pud;
  pmd_t *pmd;
  pte_t *pte;
  unsigned long long int phy_addr;

  // Get the PGD entry for the virtual address.
  pgd = pgd_offset(mm, virt_addr);
  if (pgd_none(*pgd) || pgd_bad(*pgd)) {
    pr_info("[CHAR_DEV] Page not present (PGD level)\n");
    return 0;
  }

  // Get the P4D entry. Note: On some architectures, the P4D level is folded
  // into PGD.
  p4d = p4d_offset(pgd, virt_addr);
  if (p4d_none(*p4d) || p4d_bad(*p4d)) {
    pr_info("[CHAR_DEV] Page not present (P4D level)\n");
    return 0;
  }

  // Get the PUD entry.
  pud = pud_offset(p4d, virt_addr);
  if (pud_none(*pud) || pud_bad(*pud)) {
    pr_info("[CHAR_DEV] Page not present (PUD level)\n");
    return 0;
  }

  // Get the PMD entry.
  pmd = pmd_offset(pud, virt_addr);
  if (pmd_none(*pmd) || pmd_bad(*pmd)) {
    pr_info("[CHAR_DEV] Page not present (PMD level)\n");
    return 0;
  }

  // Get the PTE entry. pte_offset_kernel() returns a pointer to the PTE.
  pte = pte_offset_kernel(pmd, virt_addr);
  if (pte_none(*pte)) {
    pr_info("[CHAR_DEV] Page not present (PTE level)\n");
    return 0;
  }

  // Calculate the physical address:
  //   - pte_pfn(*pte) extracts the page frame number.
  //   - << PAGE_SHIFT converts the PFN to a physical address.
  //   - (virt_addr & ~PAGE_MASK) computes the offset within the page.
  phy_addr = (pte_pfn(*pte) << PAGE_SHIFT) | (virt_addr & ~PAGE_MASK);
  return phy_addr;
}

/*
 * Function: device_ioctl
 * ----------------------
 * Description:
 *   Handles IOCTL calls issued to the character device. Two IOCTL commands are
 *   supported:
 *     1. IOCTL_PHYS_TO_VIRT: Convert a virtual address to a physical address.
 *     2. IOCTL_WRITE_TO_PHYS: Write a byte to a specified physical address.
 *
 * Parameters:
 *   - file: Pointer to the file structure (not used explicitly here).
 *   - ioctl_num: The IOCTL command number.
 *   - ioctl_param: A user-space pointer to a structure (of type query_arg_t)
 * that contains the necessary parameters.
 *
 * Returns:
 *   0 on success or a negative error code on failure.
 *
 * Note:
 *   The structure query_arg_t must be defined in "chardev.h" and is expected to
 * contain:
 *     - virt_addr: Virtual address (input for IOCTL_PHYS_TO_VIRT).
 *     - phys_addr: To store the resulting physical address.
 *     - byte_val: The value to write (for IOCTL_WRITE_TO_PHYS).
 */
static long device_ioctl(struct file *file, unsigned int ioctl_num,
                         unsigned long ioctl_param) {
  // Obtain the current process's memory descriptor.
  struct mm_struct *mm = current->mm;
  // Create a temporary structure to hold IOCTL arguments.
  struct query_arg_t unused_arg;
  struct query_arg_t *arg = &unused_arg;

  // Copy the query_arg_t structure from user space into kernel space.
  if (copy_from_user(arg, (struct query_arg_t *)ioctl_param,
                     sizeof(struct query_arg_t)))
    return -EACCES;

  // Switch based on the IOCTL command received.
  switch (ioctl_num) {
  case IOCTL_PHYS_TO_VIRT: {
    // For IOCTL_PHYS_TO_VIRT:
    //   - The user provides a virtual address in arg->virt_addr.
    //   - We call page_table_walk() to get the corresponding physical address.
    unsigned long virt_addr = arg->virt_addr;
    unsigned long phy_addr = page_table_walk(virt_addr, mm);
    arg->phys_addr = phy_addr; // Store the result in the structure.

    // Copy the updated structure back to user space.
    if (copy_to_user((struct query_arg_t *)ioctl_param, arg,
                     sizeof(struct query_arg_t)))
      return -EACCES;
    break;
  }
  case IOCTL_WRITE_TO_PHYS: {
    /* For IOCTL_WRITE_TO_PHYS:
     *   - The user provides:
     *       arg->phys_addr: The physical address where data will be written.
     *       arg->byte_val:  The byte value to write.
     *
     *   The function converts the physical address to a virtual address using
     *   phys_to_virt() and writes the value.
     *
     * Alternative methods (commented out) include using kmap/kunmap for high
     * memory.
     */
    *(char *)phys_to_virt(arg->phys_addr) = arg->byte_val;

    // Alternative Method 1 (commented out):
    // void* vaddr = (void *)((unsigned long)kmap_local_pfn(arg->phys_addr >>
    // PAGE_SHIFT) |
    //                         (arg->phys_addr & ~PAGE_MASK));
    // *(char*)vaddr = arg->byte_val;
    // kunmap_local(vaddr);

    // Alternative Method 2 (commented out):
    // struct page* page = pfn_to_page(arg->phys_addr >> PAGE_SHIFT);
    // void* vaddr = (void *)((unsigned long)kmap(page) | (arg->phys_addr &
    // ~PAGE_MASK));
    // *(char*)vaddr = arg->byte_val;
    // kunmap(page);
    break;
  }
  default:
    // If an unsupported IOCTL command is provided, return an error.
    return -EINVAL;
  }
  return 0;
}

/*
 * File operations structure (fops)
 * ----------------------------------
 * This structure defines the operations supported by the character device.
 * Only the unlocked_ioctl field is implemented here; open, release, read, and
 * write are left as NULL since they are not needed in this example.
 */
struct file_operations fops = {
    .open = NULL,                   // Device open: no custom action required.
    .release = NULL,                // Device close: no custom cleanup needed.
    .read = NULL,                   // Read operation: not implemented.
    .write = NULL,                  // Write operation: not implemented.
    .unlocked_ioctl = device_ioctl, // IOCTL handler: handles custom commands.
};

/*
 * Function: register_device
 * -------------------------
 * Description:
 *   Module initialization function that registers the character device with the
 * kernel. It performs the following steps:
 *     1. Allocates a device number (major and minor) using
 * alloc_chrdev_region().
 *     2. Initializes the character device structure using cdev_init().
 *     3. Adds the character device to the kernel with cdev_add().
 *     4. Creates a device class in sysfs using class_create().
 *     5. Creates a device file (node) in /dev using device_create().
 *
 * Returns:
 *   0 on success, or a negative error code on failure.
 *
 * Kernel-level functions explained:
 *   - alloc_chrdev_region():
 *       Allocates a range of char device numbers for the driver.
 *   - cdev_init():
 *       Initializes a cdev structure by setting its file operations.
 *   - cdev_add():
 *       Adds the initialized cdev structure to the kernel, making it active.
 *   - class_create():
 *       Creates a device class under /sys/class; this is used by udev to create
 * device nodes.
 *   - device_create():
 *       Creates an actual device file in /dev that applications can interact
 * with.
 */
static int __init register_device(void) {
  int ret;
  struct device *dev_ret;

  // Allocate a range of device numbers for the character device.
  if ((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "query_ioctl")) <
      0) {
    return ret;
  }

  // Initialize the cdev structure with our file operations.
  cdev_init(&c_dev, &fops);

  // Add the cdev structure to the kernel so that it becomes operational.
  if ((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0) {
    return ret;
  }

  // Create a device class for the device. This class is used in sysfs.
  if (IS_ERR(cl = class_create(THIS_MODULE, "addr_translate"))) {
    cdev_del(&c_dev);
    unregister_chrdev_region(dev, MINOR_CNT);
    return PTR_ERR(cl);
  }

  // Create a device node in /dev with the name defined in DEVICE_NAME.
  if (IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, DEVICE_NAME))) {
    class_destroy(cl);
    cdev_del(&c_dev);
    unregister_chrdev_region(dev, MINOR_CNT);
    return PTR_ERR(dev_ret);
  }

  return 0;
}

/*
 * Function: dereg_device
 * ----------------------
 * Description:
 *   Module exit function that cleans up and unregisters the character device.
 *   It reverses all the operations performed in register_device():
 *     1. Removes the device file using device_destroy().
 *     2. Destroys the device class with class_destroy().
 *     3. Deletes the cdev from the kernel using cdev_del().
 *     4. Unregisters the allocated device numbers with
 * unregister_chrdev_region().
 */
static void __exit dereg_device(void) {
  device_destroy(cl, dev); // Remove the device node from /dev.
  class_destroy(cl);       // Destroy the sysfs class.
  cdev_del(&c_dev);        // Remove the cdev from the kernel.
  unregister_chrdev_region(dev,
                           MINOR_CNT); // Free the allocated device numbers.
}

// Register the module initialization and cleanup functions.
module_init(register_device);
module_exit(dereg_device);

// Module metadata for information displayed by tools like modinfo.
MODULE_DESCRIPTION("2_I");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

