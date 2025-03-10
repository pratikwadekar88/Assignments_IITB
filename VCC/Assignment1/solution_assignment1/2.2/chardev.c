#include <asm/syscall.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/syscalls.h>

#include "chardev.h"

#define FIRST_MINOR 0
#define MINOR_CNT 1

static dev_t dev;
static struct cdev c_dev;
static struct class *cl;

int change_parent(pid_t tpid) {

  struct task_struct *parent_task;

  rcu_read_lock();

  parent_task = pid_task(find_vpid(tpid), PIDTYPE_PID);

  if (parent_task == NULL) {
    printk(KERN_INFO "<%s> ioctl: change_parent: task %d not found\n",
           DEVICE_NAME, tpid);
    rcu_read_unlock();
    return -1;
  }

  list_del_init(&current->sibling);
  list_add_tail_rcu(&current->sibling, &parent_task->children);

  RCU_INIT_POINTER(current->real_parent, parent_task);
  RCU_INIT_POINTER(current->parent, parent_task);

  rcu_read_unlock();

  return 0;
}

int kill_all(pid_t tpid) {

  struct task_struct *parent_task;
  struct task_struct *child_task;
  struct pid *pid_child;
  struct pid *pid_parent;
  bool has_children = false;

  rcu_read_lock();

  parent_task = pid_task(find_vpid(tpid), PIDTYPE_PID);

  if (parent_task == NULL) {
    pr_info("<%s> kill_all: task %d not found\n", DEVICE_NAME, tpid);
    rcu_read_unlock();
    return -1;
  }

  list_for_each_entry_rcu(child_task, &parent_task->children, sibling) {
    pr_info("Killing child process: %d\n", child_task->pid);
    pid_child = find_get_pid(child_task->pid);
    send_sig(SIGTERM, child_task, 1);

    has_children = true;

    put_pid(pid_child);
  }

  if (has_children) {
    msleep(2000);
  }

  pr_info("Parent process termination: %d\n", parent_task->pid);
  pid_parent = find_get_pid(parent_task->pid);
  send_sig(SIGTERM, parent_task, 1);
  put_pid(pid_parent);

  rcu_read_unlock();

  return 0;
}

static long device_ioctl(struct file *file, unsigned int ioctl_num,
                         unsigned long ioctl_param) {
  pid_t pid = (pid_t)ioctl_param;

  switch (ioctl_num) {
  case IOCTL_SEND_SIGCHLD_TO_PID:
    return change_parent(pid);
    break;
  case IOCTL_SEND_EXIT:
    return kill_all(pid);
    break;
  default:
    pr_info("[CHAR_DEV] Invalid ioctl command\n");
    return -EINVAL;
  }
  return 0;
}

struct file_operations Fops = {.open = NULL,
                               .release = NULL,
                               .read = NULL,
                               .write = NULL,
                               .unlocked_ioctl = device_ioctl};

static int __init register_device(void) {
  int ret;
  struct device *dev_ret;

  if ((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "query_ioctl")) <
      0) {
    return ret;
  }

  cdev_init(&c_dev, &Fops);

  if ((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0) {
    return ret;
  }

  if (IS_ERR(cl = class_create(THIS_MODULE, "change_parent"))) {
    cdev_del(&c_dev);
    unregister_chrdev_region(dev, MINOR_CNT);
    return PTR_ERR(cl);
  }
  if (IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, DEVICE_NAME))) {
    class_destroy(cl);
    cdev_del(&c_dev);
    unregister_chrdev_region(dev, MINOR_CNT);
    return PTR_ERR(dev_ret);
  }

  return 0;
}

static void __exit dereg_device(void) {
  device_destroy(cl, dev);
  class_destroy(cl);
  cdev_del(&c_dev);
  unregister_chrdev_region(dev, MINOR_CNT);
}

module_init(register_device);
module_exit(dereg_device);

MODULE_DESCRIPTION("dr_doom_ioctl");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");
