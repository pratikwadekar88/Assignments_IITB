#include <linux/ioctl.h>

#define IOCTL_NUM 100
#define DEVICE_NAME "chardev"

#define IOCTL_SEND_SIGCHLD_TO_PID _IOW(IOCTL_NUM, 0, unsigned long)
#define IOCTL_SEND_EXIT _IOW(IOCTL_NUM, 1, unsigned long)
