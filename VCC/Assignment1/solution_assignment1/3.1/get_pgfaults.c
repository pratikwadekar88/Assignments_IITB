#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include <linux/mm.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("memory stats sysfs");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

static struct proc_dir_entry *our_proc_file;
static const char procfs_name[] = "get_pgfaults";

static int open_count;

static int procfile_open(struct inode *inode, struct file *file_pointer) {
  if (open_count > 0) {
    pr_info("procfile_open: /proc/%s already opened\n", procfs_name);
    return -EBUSY;
  }
  pr_info("procfile_open: /proc/%s opened\n", procfs_name);
  open_count++;
  return 0;
}

static int procfile_release(struct inode *inode, struct file *file_pointer) {
  pr_info("procfile_release: /proc/%s released\n", procfs_name);
  open_count--;
  return 0;
}

static ssize_t procfile_read(struct file *file_pointer, char __user *buffer,
                             size_t buffer_length, loff_t *offset) {

  char t_buf[32];
  int len;

  static unsigned long events[NR_VM_EVENT_ITEMS];

  /* Get all vm events */
  all_vm_events(events);
  len = sprintf(t_buf, "%lu\n", events[PGFAULT]);

  if (len < 0) {
    pr_err("error in string building\n");
    return 0;
  }

  t_buf[len] = '\0';

  if (*offset >= len)
    return 0;
  else if (copy_to_user(buffer, t_buf, len) != 0)
    pr_err("copy_to_user failed\n");
  else
    *offset += len;

  return len;
}

static const struct proc_ops proc_file_fops = {
    .proc_read = procfile_read,
    .proc_open = procfile_open,
    .proc_release = procfile_release,
};

static int __init procfs_init(void) {
  our_proc_file = proc_create(procfs_name, 0644, NULL, &proc_file_fops);
  if (NULL == our_proc_file) {
    proc_remove(our_proc_file);
    pr_alert("Error: Could not initialize /proc/%s\n", procfs_name);
    return -ENOMEM;
  }

  pr_info("/proc/%s created\n", procfs_name);
  return 0;
}

static void __exit procfs_exit(void) {
  proc_remove(our_proc_file);
  pr_info("/proc/%s removed\n", procfs_name);
}

module_init(procfs_init);
module_exit(procfs_exit);
