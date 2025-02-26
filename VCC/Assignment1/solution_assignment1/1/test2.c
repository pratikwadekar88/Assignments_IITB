#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define SIZE (512 * 1024 * 1024)

int main() {
	printf("PID: %d\n", getpid());

    int *ptr = malloc(SIZE);
	if (ptr == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }

	memset(ptr, 0, SIZE);

	sleep(5);
	printf("Allocated %ld MiB\n", SIZE >> 20);
	printf("Press anything to exit...\n");
	getchar();

	free(ptr);
	printf("Freed memory\n");
	
	return EXIT_SUCCESS;
}