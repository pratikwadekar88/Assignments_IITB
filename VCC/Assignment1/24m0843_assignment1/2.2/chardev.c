#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/sched/signal.h>
#include <linux/delay.h>

#define DEVICE_NAME "chardev"
#define IOCTL_CHANGE_PARENT _IOW('a', 1, int)
#define IOCTL_TERMINATE_ALL _IOW('a', 3,int)
static dev_t dev_num;
static struct cdev chardev_cdev;
static struct class *chardev_class;

static int change_parent(int new_parent_pid) {
    struct task_struct *new_parent_task;
    struct task_struct *current_task = current;


    rcu_read_lock();
    new_parent_task = get_pid_task(find_vpid(new_parent_pid), PIDTYPE_PID);
    if (!new_parent_task) {
        rcu_read_unlock();
        pr_err("Invalid pid: %d\n", new_parent_pid);
        return -EINVAL;
    }

    get_task_struct(new_parent_task); 
    rcu_read_unlock();

    raw_spin_lock(&current_task->pi_lock);      
    raw_spin_lock(&new_parent_task->pi_lock);   

    list_del_rcu(&current_task->sibling);

    current_task->real_parent = new_parent_task;
    current_task->parent = new_parent_task;

    list_add_rcu(&current_task->sibling, &new_parent_task->children);

    raw_spin_unlock(&new_parent_task->pi_lock);
    raw_spin_unlock(&current_task->pi_lock);

    put_task_struct(new_parent_task); 
    pr_info("Changed parent of process %d to %d\n", current_task->pid, new_parent_pid);

    return 0;
}

static long kill_children(int new_parent_pid){
    struct task_struct *new_parent_task;  
    struct task_struct *child_task;

    rcu_read_lock();
    new_parent_task = get_pid_task(find_vpid(new_parent_pid), PIDTYPE_PID);
    if (!new_parent_task) {
        rcu_read_unlock();
        pr_err("Invalid pid: %d\n", new_parent_pid);
        return -EINVAL;
    }

    get_task_struct(new_parent_task); 
    rcu_read_unlock();
    list_for_each_entry(child_task, &new_parent_task->children, sibling) {
        pr_info("Killing child PID: %d\n", child_task->pid);

        if (send_sig(SIGTERM, child_task, 0) < 0) {
            pr_err("Failed to send SIGKILL to child PID: %d\n", child_task->pid);
        }

    }
    //Waiting for 1 sec so that all the children terminates and everything sync properly.
    msleep(1000);
    //Sending SIGTERM to parent
    if (send_sig(SIGTERM, new_parent_task, 0)) {                                                                           
             pr_err("[chardev]: Failed to send SIGTERM to control station PID:  %d\n", new_parent_task->pid);                           
         }else {                                                                                                            
            pr_info("[chardev]: Sent SIGTERM to control station PID: %d\n", new_parent_task->pid);                                     
        }      
    pr_info("All child processes killed and cleaned up\n");
    return 0;
}

static long chardev_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    int ret = 0;
    int new_parent_pid;

    switch (cmd) {
        case IOCTL_CHANGE_PARENT:
            if (copy_from_user(&new_parent_pid, (int __user *)arg, sizeof(int))) {
                pr_err("Failed to copy data from user space\n");
                return -EFAULT;
            }
            pr_info("Parent ID : %d\n", new_parent_pid);
            ret = change_parent(new_parent_pid);
            break;
        case IOCTL_TERMINATE_ALL:
            if (copy_from_user(&new_parent_pid, (int __user *)arg, sizeof(int))) {
                pr_err("Failed to copy data from user space\n");
                return -EFAULT;
            }
            ret = kill_children(new_parent_pid);
            break;
        default:
            pr_err("Invalid IOCTL command\n");
            return -EINVAL;
    }

    return ret;
}

static struct file_operations chardev_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = chardev_ioctl,
};

static int __init chardev_init(void) {
    int ret;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to allocate device number\n");
        return ret;
    }

    cdev_init(&chardev_cdev, &chardev_fops);
    ret = cdev_add(&chardev_cdev, dev_num, 1);
    if (ret < 0) {
        pr_err("Failed to add cdev\n");
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    chardev_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(chardev_class)) {
        pr_err("Failed to create class\n");
        cdev_del(&chardev_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(chardev_class);
    }

    if (!device_create(chardev_class, NULL, dev_num, NULL, DEVICE_NAME)) {
        pr_err("Failed to create device\n");
        class_destroy(chardev_class);
        cdev_del(&chardev_cdev);
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }

    pr_info("chardev module loaded successfully\n");
    return 0;
}

static void __exit chardev_exit(void) {
    device_destroy(chardev_class, dev_num);
    class_destroy(chardev_class);
    cdev_del(&chardev_cdev);
    unregister_chrdev_region(dev_num, 1);
    pr_info("chardev module unloaded\n");
}

module_init(chardev_init);
module_exit(chardev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pratik");
MODULE_DESCRIPTION("Character device driver for process parent change");
MODULE_VERSION("1.0");
