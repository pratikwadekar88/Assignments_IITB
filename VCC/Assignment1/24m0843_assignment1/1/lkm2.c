#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("List child processes of a given PID");
MODULE_AUTHOR("Pratik");
MODULE_LICENSE("GPL");

static int pid = 0; 

module_param(pid, int, S_IRUGO);

static const char *get_task_state_string(long state) {
    switch (state) {
        case TASK_RUNNING:
            return "TASK_RUNNING";
        case TASK_INTERRUPTIBLE:
            return "TASK_INTERRUPTIBLE";
        case TASK_UNINTERRUPTIBLE:
            return "TASK_UNINTERRUPTIBLE";
        case __TASK_STOPPED:
            return "TASK_STOPPED";
        case __TASK_TRACED:
            return "TASK_TRACED";
        default:
            return "TASK_UNKNOWN";
    }
}
static int __init lkm2_init(void)
{
    struct task_struct *task;
    struct task_struct *child;
    struct pid *pid_struct;
    int found = 0;

    if (pid <= 0) {
        pr_info("[LKM2] Invalid PID specified\n");
        return -1;
    }

    pid_struct = find_get_pid(pid); 
    if (!pid_struct) {
        pr_info("[LKM2] PID %d not found\n", pid);
        return -1;
    }

    task = get_pid_task(pid_struct, PIDTYPE_PID);  
    if (!task) {
        pr_info("[LKM2] Could not retrieve task for PID %d\n", pid);
        return -1;
    }

    pr_info("[LKM2] Child processes of PID %d:\n", pid);
    
    list_for_each_entry(child, &task->children, sibling) {
        pr_info("[LKM2] Child Process PID: %d, State: %s\n", child->pid, get_task_state_string(child->__state));
        found = 1;
    }

    if (!found) {
        pr_info("[LKM2] No child processes found for PID %d\n", pid);
    }

    return 0;
}

static void __exit lkm2_exit(void)
{
    pr_info("[LKM2] Module LKM2 Unloaded\n");
}

module_init(lkm2_init);
module_exit(lkm2_exit);
