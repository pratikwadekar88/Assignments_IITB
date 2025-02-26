#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "chardev.h"


int main(int argc, char *argv[]){

	if (argc < 2) {
        printf("Usage: %s <count>\n", argv[0]);
        return -1;
    }

    int count = atoi(argv[1]);  
	char* mem = malloc(count);

	for(int i = 0;i< count;i++)
	{
		mem[i] = 104+i;
	}
	

	int fd = open("/dev/chardev",O_RDWR);
	
	if(fd < 0){
		perror("Error in opening ioctl driver file\n");
		return -1;
	}
	
	for(int i = 0;i< count;i++)
	{
		printf("VA 0x%llx\tValue: %d\n",(long long unsigned int)mem + i, mem[i]);
	}
	
	unsigned long phys_addrs[count];
	struct query_arg_t translate_query;
	for(int i = 0;i< count;i++)
	{
		translate_query.virt_addr = (unsigned long)(mem+i);
		int ret = ioctl(fd,IOCTL_PHYS_TO_VIRT,&translate_query);
		if (ret != 0){
			perror("Error in ioctl\n");
			return -1;
	}
	    phys_addrs[i] = translate_query.phys_addr;
			
	}

	printf("VA to PA translation\n");
	for(int i = 0;i< count;i++)
	{
		printf("VA: 0x%llx\tPA: 0x%llx\n",(long long unsigned int)mem + i,(long long unsigned int)(phys_addrs)[i] );
	}

	for(int i = 0;i< count;i++)
	{
		printf("PA: 0x%llx\tValue: %d\n",(long long unsigned int)(phys_addrs)[i], mem[i]);
	}

	
	
	struct query_arg_t write_query;
	for(int i = 0;i<count;i++)
	{
		write_query.phys_addr = phys_addrs[i];
		write_query.byte_val = 53 + i;
	
		int ret = ioctl(fd,IOCTL_WRITE_TO_PHYS,&write_query);
		if (ret != 0){
			perror("Error in ioctl\n");
		return -1;
	}
	printf("VA: 0x%llx\tPA:0x%llx\tUpdated Value: %d\n",(long long unsigned int)(mem+i), (long long unsigned int)(phys_addrs)[i], mem[i]);

	}
	
	
	close(fd);
	free(mem);

	return 0;
}
