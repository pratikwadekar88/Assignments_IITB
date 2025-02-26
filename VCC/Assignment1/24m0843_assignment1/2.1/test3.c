#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/ioctl_vtop"
#define IOCTL_GET_PHYS_ADDR _IOWR('p', 1, unsigned long[2])
#define IOCTL_WRITE_PHYS_VAL _IOW('p', 2, unsigned long[2])

int main(int argc, char *argv[]) {
    int fd, i;
    unsigned long count ; 
    unsigned char *memory_block;
    unsigned long *phys_addresses;
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <count>\n", argv[0]);
        return EXIT_FAILURE;
    }
    count = strtoul(argv[1], NULL, 10);
    // Open the device
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    // Step 1: Allocate a memory block
    memory_block = malloc(count);
    if (!memory_block) {
        perror("Failed to allocate memory");
        close(fd);
        return EXIT_FAILURE;
    }

    // Step 2: Assign values to the allocated memory
    for (i = 0; i < count; i++) {
        memory_block[i] = 104 + i;
    }

    // Print virtual addresses and values
    printf("Virtual Addresses and Values:\n");
    for (i = 0; i < count; i++) {
        printf("VA: %p  Value: %u\n", (void *)&memory_block[i], memory_block[i]);
    }

    // Step 3: Get physical addresses using ioctl
    phys_addresses = malloc(count * sizeof(unsigned long));
    if (!phys_addresses) {
        perror("Failed to allocate memory for physical addresses");
        free(memory_block);
        close(fd);
        return EXIT_FAILURE;
    }

    printf("VA to PA translation\n");
    for (i = 0; i < count; i++) {
        unsigned long args[2] = {getpid(), (unsigned long)&memory_block[i]};
        if (ioctl(fd, IOCTL_GET_PHYS_ADDR, args) == -1) {
            perror("Failed to get physical address");
            free(memory_block);
            free(phys_addresses);
            close(fd);
            return EXIT_FAILURE;
        }
        phys_addresses[i] = args[0];
        printf("VA: %p -> PA: %lu\n", (void *)&memory_block[i], phys_addresses[i]);
    }

    // Step 4: Update values in physical memory using ioctl
    for (i = 0; i < count; i++) {
        unsigned long args[2] = {phys_addresses[i], 53 + i};
        if (ioctl(fd, IOCTL_WRITE_PHYS_VAL, args) == -1) {
            perror("Failed to write to physical address");
            free(memory_block);
            free(phys_addresses);
            close(fd);
            return EXIT_FAILURE;
        }
    }

    // Step 5: Verify the updated values
    printf("\nUpdated Virtual Addresses and Values:\n");
    for (i = 0; i < count; i++) {
        printf("VA: %p, Updated Value: %u\n", (void *)&memory_block[i], memory_block[i]);
    }

    // Cleanup
    free(memory_block);
    free(phys_addresses);
    close(fd);

    return EXIT_SUCCESS;
}
