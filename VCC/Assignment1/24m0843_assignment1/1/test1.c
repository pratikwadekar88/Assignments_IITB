#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define PAGE_SIZE 4096 

int main(int argc, char *argv[]) {
    pid_t pid = getpid();
    printf("PID of the process: %d\n", pid);

    if (argc != 3) {
        printf("Usage: %s <num_pages> <stride_bytes>\n", argv[0]);
        return 1;
    }

    size_t num_pages = atoi(argv[1]);    
    size_t stride_bytes = atoi(argv[2]); 

    if (num_pages <= 0 || stride_bytes <= 0) {
        printf("Both number of pages and stride size must be greater than 0.\n");
        return 1;
    }

    size_t total_memory = num_pages * PAGE_SIZE;

    void *memory = malloc(total_memory);
    if (memory == NULL) {
        perror("malloc failed");
        return 1;
    }

    printf("Allocated %zu bytes in memory (%.2f MB)\n", total_memory, (double)total_memory / (1024 * 1024));


    uint8_t *current_address = (uint8_t *)memory;
    size_t writes = 0;

    for (size_t i = 0; i < num_pages; i++) {
        *current_address = 1; 
        writes++;

        current_address += stride_bytes;

        if (current_address >= (uint8_t *)memory + total_memory) {
            break; 
        }
    }

    printf("Finished writing with stride of %zu bytes.\n", stride_bytes);
    printf("Total writes: %zu\n", writes);

    printf("Press CTL+C to exit...\n");
    pause(); 

    free(memory);

    return 0;
}
