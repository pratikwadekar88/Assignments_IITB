#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <linux/kvm.h>

/* CR0 bits */
#define CR0_PE 1u
#define CR0_MP (1U << 1)
#define CR0_EM (1U << 2)
#define CR0_TS (1U << 3)
#define CR0_ET (1U << 4)
#define CR0_NE (1U << 5)
#define CR0_WP (1U << 16)
#define CR0_AM (1U << 18)
#define CR0_NW (1U << 29)
#define CR0_CD (1U << 30)
#define CR0_PG (1U << 31)

/* CR4 bits */
#define CR4_VME 1
#define CR4_PVI (1U << 1)
#define CR4_TSD (1U << 2)
#define CR4_DE (1U << 3)
#define CR4_PSE (1U << 4)
#define CR4_PAE (1U << 5)
#define CR4_MCE (1U << 6)
#define CR4_PGE (1U << 7)
#define CR4_PCE (1U << 8)
#define CR4_OSFXSR (1U << 8)
#define CR4_OSXMMEXCPT (1U << 10)
#define CR4_UMIP (1U << 11)
#define CR4_VMXE (1U << 13)
#define CR4_SMXE (1U << 14)
#define CR4_FSGSBASE (1U << 16)
#define CR4_PCIDE (1U << 17)
#define CR4_OSXSAVE (1U << 18)
#define CR4_SMEP (1U << 20)
#define CR4_SMAP (1U << 21)

#define EFER_SCE 1
#define EFER_LME (1U << 8)
#define EFER_LMA (1U << 10)
#define EFER_NXE (1U << 11)

/* 32-bit page directory entry bits */
#define PDE32_PRESENT 1
#define PDE32_RW (1U << 1)
#define PDE32_USER (1U << 2)
#define PDE32_PS (1U << 7)

/* 64-bit page * entry bits */
#define PDE64_PRESENT 1
#define PDE64_RW (1U << 1)
#define PDE64_USER (1U << 2)
#define PDE64_ACCESSED (1U << 5)
#define PDE64_DIRTY (1U << 6)
#define PDE64_PS (1U << 7)
#define PDE64_G (1U << 8)

#define BUFFER_SIZE 20
struct vm
{
	int dev_fd;
	int vm_fd;
	char *mem;
};

struct vcpu
{
	int vcpu_fd;
	struct kvm_run *kvm_run;
};

/* Data from sched.txt */
char sched_order[100];

uint32_t prod_p = -1;
uint32_t cons_p = -1;
uint32_t hv_shared_buffer[BUFFER_SIZE];
uint32_t prod_prod_p, prod_cons_p, cons_cons_p, cons_prod_p, prod_buffer_addr, cons_buffer_addr;
void vm_init(struct vm *vm, size_t mem_size)
{
	int kvm_version;
	struct kvm_userspace_memory_region memreg;

	vm->dev_fd = open("/dev/kvm", O_RDWR);
	if (vm->dev_fd < 0)
	{
		perror("open /dev/kvm");
		exit(1);
	}

	kvm_version = ioctl(vm->dev_fd, KVM_GET_API_VERSION, 0);
	if (kvm_version < 0)
	{
		perror("KVM_GET_API_VERSION");
		exit(1);
	}

	if (kvm_version != KVM_API_VERSION)
	{
		fprintf(stderr, "Got KVM api version %d, expected %d\n",
				kvm_version, KVM_API_VERSION);
		exit(1);
	}

	vm->vm_fd = ioctl(vm->dev_fd, KVM_CREATE_VM, 0);
	if (vm->vm_fd < 0)
	{
		perror("KVM_CREATE_VM");
		exit(1);
	}

	if (ioctl(vm->vm_fd, KVM_SET_TSS_ADDR, 0xfffbd000) < 0)
	{
		perror("KVM_SET_TSS_ADDR");
		exit(1);
	}

	vm->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (vm->mem == MAP_FAILED)
	{
		perror("mmap mem");
		exit(1);
	}

	madvise(vm->mem, mem_size, MADV_MERGEABLE);

	memreg.slot = 0;
	memreg.flags = 0;
	memreg.guest_phys_addr = 0;
	memreg.memory_size = mem_size;
	memreg.userspace_addr = (unsigned long)vm->mem;
	if (ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &memreg) < 0)
	{
		perror("KVM_SET_USER_MEMORY_REGION");
		exit(1);
	}
}

void vcpu_init(struct vm *vm, struct vcpu *vcpu)
{
	int vcpu_mmap_size;

	vcpu->vcpu_fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, 0);
	if (vcpu->vcpu_fd < 0)
	{
		perror("KVM_CREATE_VCPU");
		exit(1);
	}

	vcpu_mmap_size = ioctl(vm->dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (vcpu_mmap_size <= 0)
	{
		perror("KVM_GET_VCPU_MMAP_SIZE");
		exit(1);
	}

	vcpu->kvm_run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE,
						 MAP_SHARED, vcpu->vcpu_fd, 0);
	if (vcpu->kvm_run == MAP_FAILED)
	{
		perror("mmap kvm_run");
		exit(1);
	}
}


uint32_t isEmpty()
{
	return (cons_p == (uint32_t)-1);
}
uint32_t queueSize()
{
	if (isEmpty())
		return 0;

	if (prod_p >= cons_p)
		return prod_p - cons_p + 1;
	else
		return (BUFFER_SIZE - cons_p) + (prod_p + 1);
}

void print_hv_buffer(void)
{
	if (cons_p == -1)
	{
		printf("HYPVSR : []\n");
		return;
	}
	printf("HYPVSR: [");
	if (prod_p >= cons_p)
	{
		/* No wrap–around: indices cons_p ... prod_p are valid */
		for (int32_t i = cons_p; i <= prod_p; i++)
		{
			printf("%u ", hv_shared_buffer[i]);
		}
	}
	else
	{
		for (int32_t i = cons_p; i < BUFFER_SIZE; i++)
		{
			printf("%u ", hv_shared_buffer[i]);
		}
		for (int32_t i = 0; i <= prod_p; i++)
		{
			printf("%u ", hv_shared_buffer[i]);
		}
	}
	printf("\b]\n");
}

void setup_connection(struct vm *vm1, struct vm *vm2, struct vcpu *vcpu1, struct vcpu *vcpu2)
{
	if (ioctl(vcpu1->vcpu_fd, KVM_RUN, 0) < 0)
	{
		perror("KVM_RUN");
		exit(1);
	}
	memcpy(&prod_cons_p, (char *)vcpu1->kvm_run + vcpu1->kvm_run->io.data_offset, sizeof(uint32_t));
	if (ioctl(vcpu1->vcpu_fd, KVM_RUN, 0) < 0)
	{
		perror("KVM_RUN");
		exit(1);
	}
	memcpy(&prod_prod_p, (char *)vcpu1->kvm_run + vcpu1->kvm_run->io.data_offset, sizeof(uint32_t));
	if (ioctl(vcpu1->vcpu_fd, KVM_RUN, 0) < 0)
	{
		perror("KVM_RUN");
		exit(1);
	}

	char *data = (char *)vcpu1->kvm_run + vcpu1->kvm_run->io.data_offset;
	memcpy(&prod_buffer_addr, data, sizeof(prod_buffer_addr));
	// memcpy(&prod_buffer_addr,(char *)vcpu1->kvm_run + vcpu1->kvm_run->io.data_offset,sizeof(uint32_t));

	if (ioctl(vcpu2->vcpu_fd, KVM_RUN, 0) < 0)
	{
		perror("KVM_RUN");
		exit(1);
	}
	memcpy(&cons_cons_p, (char *)vcpu2->kvm_run + vcpu2->kvm_run->io.data_offset, sizeof(uint32_t));
	if (ioctl(vcpu2->vcpu_fd, KVM_RUN, 0) < 0)
	{
		perror("KVM_RUN");
		exit(1);
	}
	memcpy(&cons_prod_p, (char *)vcpu2->kvm_run + vcpu2->kvm_run->io.data_offset, sizeof(uint32_t));
	if (ioctl(vcpu2->vcpu_fd, KVM_RUN, 0) < 0)
	{
		perror("KVM_RUN");
		exit(1);
	}
	data = (char *)vcpu2->kvm_run + vcpu2->kvm_run->io.data_offset;
	memcpy(&cons_buffer_addr, data, sizeof(cons_buffer_addr));
	// memcpy(&cons_buffer_addr,(char *)vcpu2->kvm_run + vcpu2->kvm_run->io.data_offset,sizeof(uint32_t));
}

int run_vm(struct vm *vm1, struct vm *vm2, struct vcpu *vcpu1, struct vcpu *vcpu2, size_t sz)
{
	struct kvm_regs regs;
	struct vm *vm = NULL;
	struct vcpu *vcpu = NULL;
	int sched_index = 0;
	uint32_t change = 1;
	uint64_t memval = 0;
	setup_connection(vm1, vm2, vcpu1, vcpu2);
	printf("HYPVSR: []\n");
	for (; sched_order[sched_index] != '\0'; sched_index++)
	{
		if (change)
		{
			char current = sched_order[sched_index];
			if (current == '1')
			{
				vm = vm1;
				vcpu = vcpu1;
			}
			else if (current == '2')
			{
				vm = vm2;
				vcpu = vcpu2;
			}
			else
			{
				continue;
			}
		}
		else
		{
			sched_index--;
		}

		if (ioctl(vcpu->vcpu_fd, KVM_RUN, 0) < 0)
		{
			perror("KVM_RUN");
			exit(1);
		}
		if (vcpu->kvm_run->exit_reason == KVM_EXIT_HLT)
			goto check;
		if (vcpu->kvm_run->exit_reason != KVM_EXIT_IO &&
			vcpu->kvm_run->exit_reason != KVM_EXIT_MMIO)
		{
			fprintf(stderr, "Unexpected hypercall, got %d\n", vcpu->kvm_run->exit_reason);
			exit(1);
		}

		if (vcpu->kvm_run->io.port == 0xE8)
		{
			*((uint32_t *)(vm->mem + prod_cons_p)) = cons_p;
			*((uint32_t *)(vm->mem + prod_prod_p)) = prod_p;

			memcpy(&vm->mem[prod_buffer_addr], hv_shared_buffer, sizeof(hv_shared_buffer));
			change = 0;
			continue;
		}
		else if (vcpu->kvm_run->io.port == 0xE9)
		{

			uint32_t prev_prod_p = isEmpty() ? prod_p : prod_p + 1;
			uint32_t prev_cnt = queueSize();

			if (prod_p == -1 && cons_p == -1)
				prev_cnt = 0;
			if (prev_prod_p == -1)
				prev_prod_p = 0;

			cons_p = *((uint32_t *)(vm->mem + prod_cons_p));
			prod_p = *((uint32_t *)(vm->mem + prod_prod_p));

			memcpy(hv_shared_buffer, &vm->mem[prod_buffer_addr], sizeof(hv_shared_buffer));
			uint32_t new_cnt = queueSize();

			uint32_t produced_cnt = new_cnt - prev_cnt;
			if (produced_cnt > 0)
			{
				printf("VMFD: %d Produced %u Values: ", vm->vm_fd, produced_cnt);
				for (uint32_t i = 0; i < produced_cnt; i++)
				{
					uint32_t index = (prev_prod_p + i) % BUFFER_SIZE;
					printf("%d ", hv_shared_buffer[index]);
				}
				printf("\n");
			}
			else
				printf("VMFD: %d Produced %u Values\n", vm->vm_fd, 0);

			print_hv_buffer();
			change = 1;
			continue;
		}
		else if (vcpu->kvm_run->io.port == 0xEA)
		{
			*((uint32_t *)(vm->mem + cons_cons_p)) = cons_p;
			*((uint32_t *)(vm->mem + cons_prod_p)) = prod_p;

			memcpy(&vm->mem[cons_buffer_addr], hv_shared_buffer, sizeof(hv_shared_buffer));
			change = 0;
			continue;
		}
		else if (vcpu->kvm_run->io.port == 0xEB)
		{
			uint32_t prev_cnt = queueSize();
			uint32_t prev_cons_p = cons_p;

			cons_p = *((uint32_t *)(vm->mem + cons_cons_p));
			prod_p = *((uint32_t *)(vm->mem + cons_prod_p));

			uint32_t new_count = queueSize();
			uint32_t consumed_count = (prev_cnt >= new_count) ? prev_cnt - new_count : 0;
			if (consumed_count > 0)
			{
				printf("VMFD: %d Consumed %u Values: ", vm->vm_fd, consumed_count);
				for (uint32_t i = 0; i < consumed_count; i++)
				{
					uint32_t index = (prev_cons_p + i) % BUFFER_SIZE;
					printf(" %d", hv_shared_buffer[index]);
				}
				printf("\n");
			}
			else
				printf("VMFD: %d Consumed %u Values: [ ]\n", vm->vm_fd, 0);
			print_hv_buffer();
			change = 1;
			continue;
		}
		else
		{
			fprintf(stderr, "Unexpected port %x\n", vcpu->kvm_run->io.port);
			exit(1);
		}
	}
	printf("Shutting down VMs...\n");

check:
	if (ioctl(vcpu->vcpu_fd, KVM_GET_REGS, &regs) < 1)
	{
		perror("KVM_GET_REGS");
		exit(2);
	}

	if (regs.rax != 43)
	{
		printf("Wrong result: {E,R,}AX is %lld\n", regs.rax);
		return 1;
	}

	memcpy(&memval, &vm->mem[0x401], sz);
	if (memval != 43)
	{
		printf("Wrong result: memory at 0x401 is %lld\n",
			   (unsigned long long)memval);
		return 1;
	}
	return 1;
}

static void setup_protected_mode(struct kvm_sregs *sregs)
{
	struct kvm_segment seg = {
		.base = 0,
		.limit = 0xffffffff,
		.selector = 1 << 3,
		.present = 1,
		.type = 11, /* Code: execute, read, accessed */
		.dpl = 0,
		.db = 1,
		.s = 1, /* Code/data */
		.l = 0,
		.g = 1, /* 4KB granularity */
	};

	sregs->cr0 |= CR0_PE; /* enter protected mode */

	sregs->cs = seg;

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 2 << 3;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = seg;
}

extern const unsigned char guest4a[], guest4a_end[];
extern const unsigned char guest4b[], guest4b_end[];

int run_protected_mode1(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
	{
		perror("KVM_GET_SREGS");
		exit(1);
	}

	setup_protected_mode(&sregs);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
	{
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;
	regs.rsp = 2 << 20;

	if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &regs) < 0)
	{
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, guest4a, guest4a_end - guest4a);
	printf("VMFD: %d Loaded Program with size: %ld", vm->vm_fd, guest4a_end - guest4a);
	return 0;
}

int run_protected_mode2(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
	{
		perror("KVM_GET_SREGS");
		exit(1);
	}

	setup_protected_mode(&sregs);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
	{
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;
	regs.rsp = 2 << 20;

	if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &regs) < 0)
	{
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, guest4b, guest4b_end - guest4b);
	printf("VMFD: %d Loaded Program with size: %ld", vm->vm_fd, guest4b_end - guest4b);
	return 0;
}

void read_sched_file(char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		fprintf(stderr, "Error in opening file");
		exit(EXIT_FAILURE);
	}
	int rc = read(fd, sched_order, 100);
	if (rc < 0)
	{
		fprintf(stderr, "Error in opening file");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	struct vm vm1, vm2;
	struct vcpu vcpu1, vcpu2;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s filename\n",
				argv[0]);
		return 1;
	}
	read_sched_file(argv[1]);

	vm_init(&vm1, 0x200000);
	vm_init(&vm2, 0x200000);
	vcpu_init(&vm1, &vcpu1);
	vcpu_init(&vm2, &vcpu2);
	run_protected_mode1(&vm1, &vcpu1);
	run_protected_mode2(&vm2, &vcpu2);
	return run_vm(&vm1, &vm2, &vcpu1, &vcpu2, 4);
}
