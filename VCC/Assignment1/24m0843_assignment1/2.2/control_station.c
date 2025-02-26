#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Include header or define the IOCTL call interface and devide name
#define IOCTL_TERMINATE_ALL _IOW('a', 3, int)
//**************************************************

void sigchld_handler(int sig) {
    int status;                                          
    pid_t soldier_pid;
    while ((soldier_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[PARENT]: Soldier process %d terminated\n", soldier_pid);
        fflush(stdout);
    }
    if (soldier_pid == -1 && errno != ECHILD) {
        perror("waitpid");
    } 
}

void sigterm_handler(int sig) {
    printf("[PARENT]: Control station %d exiting\n", getpid());
    fflush(stdout);
    
    exit(0);  
}

int open_driver(const char* driver_name) {

    int fd_driver = open(driver_name, O_RDWR);
    if (fd_driver == -1) {
        perror("ERROR: could not open driver");
    }

	return fd_driver;
}

void close_driver( int fd_driver) {

    int result = close(fd_driver);
    if (result == -1) {
        perror("ERROR: could not close driver");
    }
}

int main(int argc, char** argv) {

    if (argc != 2) {
        printf("Usage: %s <sleep>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int s_d = atoi(argv[1]);

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    struct sigaction  sa_term;
    sa_term.sa_handler = sigterm_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = SA_RESTART;
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        perror("sigaction (SIGTERM)");
        exit(1);
    }   

    // open ioctl driver
    int fd = open_driver("/dev/chardev");
    printf("Control station process %u started\n", getpid());

    sleep(s_d); // Wait for all child process until Emergency is initiated. 


    printf("[PARENT]: Emergency Emergency!\n");
    // call ioctl with pid as argument to kill all the child process and itself
    int pid = getpid();
     if (ioctl(fd, IOCTL_TERMINATE_ALL,&pid) == -1) {
        perror("ioctl failed to kill children");
        close_driver(fd);
        return EXIT_FAILURE;
    }

    // close ioctl driver
     close_driver(fd); 
    
    return 0;

}