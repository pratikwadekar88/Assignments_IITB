#include <linux/ioctl.h>
#define IOCTL_NUM 100
#define DEVICE_NAME "chardev"

struct query_arg_t {
	unsigned long virt_addr;
	unsigned long phys_addr;
	char byte_val;
};


#define IOCTL_PHYS_TO_VIRT _IOWR(IOCTL_NUM, 0, struct query0_arg_t*)
#define IOCTL_WRITE_TO_PHYS _IOW(IOCTL_NUM, 1, struct query0_arg_t*)
