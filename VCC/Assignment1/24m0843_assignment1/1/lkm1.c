#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/signal.h> 

MODULE_DESCRIPTION("List all running or runnable processes");
MODULE_AUTHOR("Pratik");
MODULE_LICENSE("GPL");

static int __init lkm1_init(void)
{
    struct task_struct *task;

    pr_info("[LKM1] Runnable Processes:\n");
    pr_info("[LKM1] PID       PROC\n");
    pr_info("[LKM1] ------------------\n");

    for_each_process(task) {
        if (task_is_running(task)) {
            pr_info("[LKM1] %d       %s\n", task->pid, task->comm);
        }
    }
    return 0;
}

static void __exit lkm1_exit(void)
{
    pr_info("[LKM1] Module LKM1 Unloaded\n");
}

module_init(lkm1_init);
module_exit(lkm1_exit);
