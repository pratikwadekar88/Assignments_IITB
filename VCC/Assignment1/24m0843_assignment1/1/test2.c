#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <size in MB>\n", argv[0]);
        return 0;
    }

    char *endptr;
    long size_mb = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || errno != 0 || size_mb <= 0) {
        fprintf(stderr, "Invalid size in_MB\n");
        return 0;
    }

    size_t size = size_mb * 1024 * 1024;

    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap failed");
        return EXIT_FAILURE;
    }

    printf("Allocated %zu bytes (%ld MB) at %p. PID: %d\n", size, size_mb, addr, getpid());

    if (madvise(addr, size, MADV_HUGEPAGE) != 0) {
        perror("madvise failed");
        munmap(addr, size);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < size; i += 4096) { 
        ((char *)addr)[i] = 'a';
    }

    printf("Press CTL+C to exit...\n");
    pause(); 

    if (munmap(addr, size) == -1) {
        perror("munmap failed");
        return 0;
    }

    return 0;
}

