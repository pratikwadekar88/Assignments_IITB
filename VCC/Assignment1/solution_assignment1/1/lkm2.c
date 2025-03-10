#include <linux/init.h>   // Macros used for module initialization and exit
#include <linux/kernel.h> // Core kernel definitions and functions
#include <linux/module.h> // Needed by all modules for module macros and functions
#include <linux/sched.h> // Definitions for task_struct and process-related functions

/*
 * Module Metadata:
 * - MODULE_DESCRIPTION: A brief description of what this module does.
 * - MODULE_AUTHOR: The author or organization that wrote the module.
 * - MODULE_LICENSE: The license under which the module is released.
 *   (GPL is often required if the module uses certain kernel symbols.)
 */
MODULE_DESCRIPTION(
    "LKM2 - Module to list child processes of a given parent process.");
MODULE_AUTHOR("CS695");
MODULE_LICENSE("GPL");

/*
 * Module parameter: pid
 *
 * Purpose:
 *   This variable holds the Process ID (PID) of the parent process whose child
 * processes will be listed when the module is loaded. By default, it is set to
 * 1 (typically the init process).
 *
 * Details:
 *   - 'static int pid = 1;' declares a static integer with an initial value
 * of 1.
 *   - 'module_param(pid, int, 0);' makes 'pid' configurable at module load
 * time. The third argument (0) defines the permission bits for sysfs (0 means
 * no sysfs entry).
 *   - MODULE_PARM_DESC provides a description of the parameter.
 *
 * Alternative:
 *   You could also use module_param_named() if you want the parameter name in
 * the module to be different from the variable name.
 */
static int pid = 1;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "PID of the parent process");

/*
 * Function: task_state
 *
 * Purpose:
 *   Converts a numeric task state (as defined in the kernel) into a
 * human-readable string.
 *
 * Parameters:
 *   - state: a long integer representing the state of a task (process). The
 * state is usually obtained from fields like tsk->__state in the task_struct.
 *
 * Returns:
 *   A pointer to a constant string that describes the state (for example,
 * "TASK_RUNNING").
 *
 * Note:
 *   - This function handles only a few specific states.
 *   - In a more complete implementation, you might want to add more cases or
 * use a lookup table.
 *
 * Alternative:
 *   You could consider using macros or helper functions from the kernel (if
 * available) that already provide more detailed state information.
 */
char *task_state(long state) {
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
    return "OTHER STATE";
  }
}

/*
 * Function: print_child_processes
 *
 * Purpose:
 *   This is the module's initialization function. When the module is loaded,
 * this function:
 *     - Acquires an RCU (Read-Copy Update) read lock to safely traverse process
 * lists.
 *     - Finds the parent process corresponding to the provided PID.
 *     - Iterates over the list of child processes of the parent and logs each
 * child's PID and state.
 *
 * Parameters:
 *   - None directly; it uses the module parameter 'pid' defined above.
 *
 * Returns:
 *   0 on successful completion.
 *
 * Key Variables:
 *   - struct task_struct *p_task:
 *       Points to the parent process (the process with PID equal to the module
 * parameter).
 *
 *   - struct task_struct *tsk:
 *       Used as an iterator to go through each child process in the parent's
 * children list.
 *
 * Kernel Constructs Used:
 *   - rcu_read_lock()/rcu_read_unlock():
 *       These are used to protect the read-side critical section when accessing
 * the process list, ensuring consistency with concurrent updates.
 *
 *   - pid_task() and find_vpid():
 *       - find_vpid(pid) converts the numeric PID into a kernel internal struct
 * pid.
 *       - pid_task() retrieves the task_struct pointer for that PID.
 *
 *   - list_for_each_entry_rcu():
 *       This macro safely iterates over a list (here, the children list)
 * protected by RCU.
 *
 * Alternative:
 *   - Instead of manually traversing the children list, you could explore
 * helper functions or iterators provided by the kernel for process traversal,
 * though list_for_each_entry_rcu is common and appropriate here.
 *
 * NOTE:
 *   The code calls a function (or macro) task_state_index(tsk) that is not
 * defined in this snippet. It appears intended to extract or compute the state
 * index for the child process. One might consider simply using 'tsk->__state'
 * directly (or encapsulate that logic) if no transformation is needed.
 */
static int print_child_processes(void) {
  // Pointer to the parent process (whose children will be printed)
  struct task_struct *p_task = NULL;
  // Iterator pointer for traversing each child process in the parent's children
  // list
  struct task_struct *tsk = NULL;

  // Acquire RCU read lock to safely traverse the task list
  rcu_read_lock();

  // Retrieve the task_struct pointer for the parent process using its PID.
  // find_vpid(pid) returns a pointer to the kernel's internal representation of
  // the PID, and pid_task() fetches the corresponding task_struct.
  p_task = pid_task(find_vpid(pid), PIDTYPE_PID);

  // If the parent process is not found, log this information and exit the
  // function.
  if (p_task == NULL) {
    pr_info("[LKM2] Parent Process with PID: %d not found\n", pid);
    rcu_read_unlock();
    return 0;
  }

  // Iterate over each child process in the parent's children list.
  // 'children' is the head of a list containing all child processes.
  // 'sibling' is the list field within task_struct used for linking siblings.
  list_for_each_entry_rcu(tsk, &p_task->children,
                          sibling) { // Sibling pointer is next and prev pointer
                                     // in doubly linked list
    /*
     * The following log line prints:
     * - The child's PID (tsk->pid)
     * - The raw state (tsk->__state)
     * - An index/state value using task_state_index(tsk) (this function/macro
     * is expected to convert the task_struct into an index; however, it is not
     * defined in this snippet)
     * - A human-readable state string derived from the index (via task_state)
     *
     * If task_state_index() is not defined, consider using tsk->__state
     * directly: pr_info("[LKM2] Child Process PID: %d, State: %ld/%s\n",
     * tsk->pid, tsk->__state, task_state(tsk->__state));
     */
    pr_info("[LKM2] Child Process PID: %d, State: %d/%d/%s\n",
            tsk->pid,              // Child process ID
            tsk->__state,          // Raw state value from task_struct
            task_state_index(tsk), // (Undeclared function/macro) Expected to
                                   // produce a state index/value
            task_state(task_state_index(
                tsk))); // Converts the state index to a human-readable string
  }

  // Release the RCU read lock once we are done traversing the process list.
  rcu_read_unlock();
  return 0;
}

/*
 * Function: exit_module
 *
 * Purpose:
 *   This is the module's exit (cleanup) function. It is called when the module
 * is unloaded from the kernel. Here, it simply logs that the module has been
 * unloaded.
 *
 * Parameters:
 *   - None.
 *
 * Returns:
 *   - Nothing (void function).
 *
 * Note:
 *   In more complex modules, this function might perform additional cleanup
 * operations such as freeing allocated resources or unregistering callbacks.
 */
static void __exit exit_module(void) {
  pr_info("[LKM2] Module LKM2 Unloaded\n");
}

/*
 * Module Initialization and Exit Macros:
 *
 * - module_init() registers the initialization function to be called when the
 * module is loaded.
 * - module_exit() registers the cleanup function to be called when the module
 * is removed.
 */
module_init(print_child_processes);
module_exit(exit_module);
