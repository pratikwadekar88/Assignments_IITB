#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("Usage: %s <number_of_pages> <stride>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int num_pages = atoi(argv[1]);
    int stride = atoi(argv[2]);

    if (num_pages <= 0 || stride <= 0) {
        perror("ERROR: num_pages and stride must be positive integers\n");
        return EXIT_FAILURE;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) {
        perror("sysconf");
        return EXIT_FAILURE;
    }

    printf("PID: %d, Page Size: %ld bytes\n", getpid(), page_size);

    size_t size = num_pages * page_size;
    void *ptr = malloc(size);
    if (ptr == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    printf("Allocated %d pages (%ld bytes) at %p (%ld)\n", num_pages, size, ptr, ptr);

    for (size_t offset = 0; offset < size; offset += stride)
        *((char *)ptr + offset) = (unsigned char)(offset & 0xFF);

    printf("Wrote to memory\n");

    printf("Press anything to exit...\n");
    getchar();

    free(ptr);
    printf("Freed memory\n");

    return EXIT_SUCCESS;
}