#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

// Module description, author, and license information
MODULE_DESCRIPTION("LKM1");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

// Initialization function to print runnable processes
static __init int print_runnable_processes(void)
{
	struct task_struct *task; // Pointer to iterate over task structures
	pr_info("[LKM1] Runnable Processes:\n");
	pr_info("[LKM1] PID\tPROC\n");
	pr_info("[LKM1] ------------------\n");
	for_each_process(task) { // Iterate over each process in the system
		if (task_is_running(task)) { // Check if the process is runnable
			pr_info("[LKM1] %d\t%s\n", task -> pid, task->comm); // Print PID and process name
		}
	}
	return 0; // Return 0 to indicate successful initialization
}

// Exit function to clean up when the module is unloaded
static __exit void exit_module(void)
{
	pr_info("[LKM1] Module LKM1 Unloaded\n"); // Log message indicating module unload
}

// Specify initialization and exit functions
module_init(print_runnable_processes);
module_exit(exit_module);
