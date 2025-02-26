#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/vmstat.h>

#define PROC_NAME "get_pgfaults"

static int pgfaults_show(struct seq_file *m, void *v)
{
    unsigned long event_counts[NR_VM_EVENT_ITEMS]; 
    unsigned long pgfaults = 0;

    all_vm_events(event_counts);

    pgfaults = event_counts[PGFAULT];

    seq_printf(m, "Page faults: %lu\n", pgfaults);

    return 0;
}

static int pgfaults_open(struct inode *inode, struct file *file)
{
    return single_open(file, pgfaults_show, NULL);
}

static const struct proc_ops pgfaults_proc_ops = {
    .proc_open    = pgfaults_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init pgfaults_init(void)
{
    if (!proc_create(PROC_NAME, 0444, NULL, &pgfaults_proc_ops)) {
        pr_err("Failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }
    pr_info("/proc/%s created\n", PROC_NAME);
    return 0;
}

static void __exit pgfaults_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);
    pr_info("/proc/%s removed\n", PROC_NAME);
}

module_init(pgfaults_init);
module_exit(pgfaults_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pratik");
MODULE_DESCRIPTION("Kernel module to display total page faults via /proc/get_pgfaults");




//cat /proc/get_pgfaults && cat /proc/vmstat| grep pgfault