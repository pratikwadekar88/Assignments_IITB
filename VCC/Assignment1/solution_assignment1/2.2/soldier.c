#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Include header or define the IOCTL call interface and device name
#include "chardev.h"

#define DEVICE_PATH "/dev/chardev"

//**************************************************

int open_driver(const char* driver_name) {

    int fd_driver = open(driver_name, O_RDWR);
    if (fd_driver == -1) {
        perror("ERROR: could not open driver");
    }

	return fd_driver;
}

void close_driver(const char* driver_name, int fd_driver) {

    int result = close(fd_driver);
    if (result == -1) {
        perror("ERROR: could not close driver");
    }
}

int main(int argc, char** argv) {

    if (argc != 2) {
        printf("Usage: %s <parent_pid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    pid_t parent_pid = atoi(argv[1]);

    // open ioctl driver
	int fd = open_driver(DEVICE_PATH);
    
    // call ioctl with parent pid as argument to change the parent
    printf("[CHILD]: soldier %d changing its parent\n", getpid());
    fflush(stdout);
    
	ioctl(fd,IOCTL_SEND_SIGCHLD_TO_PID, parent_pid);
	
    // close ioctl driver
	close_driver(DEVICE_PATH, fd);

    while(1)
    {
        sleep(1);
    }


#ifdef DEBUG
	printf("EXITING SOLDIER\n");
#endif
	return EXIT_SUCCESS;
}


