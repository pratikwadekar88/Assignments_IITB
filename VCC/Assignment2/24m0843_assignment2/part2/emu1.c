#include <stdio.h>
#include <sys/mman.h>
#include <pthread.h>
#include <linux/kvm.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/ioctl.h>
#define _GNU_SOURCE
#include <unistd.h>

#define KVM_DEVICE "/dev/kvm"
#define RAM_SIZE 512000000
#define CODE_START 0x1000
#define BINARY_FILE1 "guest1a.bin"
#define BINARY_FILE2 "guest1b.bin"

struct vm
{
    int dev_fd;
    int kvm_version;
    int vm_fd;
    struct kvm_userspace_memory_region mem;
    struct vcpu *vcpus;
    __u64 ram_size;
    __u64 ram_start;
    int vcpu_number;
};

struct vcpu
{
    int vcpu_id;
    int vcpu_fd;
    // pthread_t vcpu_thread;
    struct kvm_run *kvm_run;
    int kvm_run_mmap_size;
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    // void *(*vcpu_thread_func)(void *);
};
static int shared_value = 0;

void kvm_init(struct vm *vm1, struct vm *vm2)
{
    int dev_fd = open(KVM_DEVICE, O_RDWR);

    if (dev_fd < 0)
    {
        perror("open /dev/kvm");
        exit(1);
    }

    int kvm_version = ioctl(dev_fd, KVM_GET_API_VERSION, 0);

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

    vm1->dev_fd = dev_fd;
    vm2->dev_fd = dev_fd;
    vm1->kvm_version = kvm_version;
    vm2->kvm_version = kvm_version;
}

int kvm_create_vm(struct vm *vm, int ram_size)
{
    int ret = 0;
    vm->vm_fd = ioctl(vm->dev_fd, KVM_CREATE_VM, 0);

    if (vm->vm_fd < 0)
    {
        perror("can not create vm");
        return -1;
    }

    vm->ram_size = ram_size;
    vm->ram_start = (__u64)mmap(NULL, vm->ram_size,
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                                -1, 0);

    if ((void *)vm->ram_start == MAP_FAILED)
    {
        perror("can not mmap ram");
        return -1;
    }

    vm->mem.slot = 0;
    vm->mem.guest_phys_addr = 0;
    vm->mem.memory_size = vm->ram_size;
    vm->mem.userspace_addr = vm->ram_start;

    ret = ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &(vm->mem));

    if (ret < 0)
    {
        perror("can not set user memory region");
        return ret;
    }

    return ret;
}

void load_binary(struct vm *vm, char *binary_file)
{
    int fd = open(binary_file, O_RDONLY);

    if (fd < 0)
    {
        fprintf(stderr, "can not open binary file\n");
        exit(1);
    }

    int ret = 0;
    char *p = (char *)vm->ram_start;

    while (1)
    {
        ret = read(fd, p, 4096);
        if (ret <= 0)
        {
            break;
        }
        printf("VMFD: %d, Loaded Program with size: %d\n", vm->vm_fd, ret);
        p += ret;
    }
}

struct vcpu *kvm_init_vcpu(struct vm *vm, int vcpu_id)
{
    struct vcpu *vcpu = malloc(sizeof(struct vcpu));
    vcpu->vcpu_id = vcpu_id;
    vcpu->vcpu_fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, vcpu->vcpu_id);

    if (vcpu->vcpu_fd < 0)
    {
        perror("can not create vcpu");
        return NULL;
    }

    vcpu->kvm_run_mmap_size = ioctl(vm->dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);

    if (vcpu->kvm_run_mmap_size < 0)
    {
        perror("can not get vcpu mmsize");
        return NULL;
    }

    vcpu->kvm_run = mmap(NULL, vcpu->kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->vcpu_fd, 0);

    if (vcpu->kvm_run == MAP_FAILED)
    {
        perror("can not mmap kvm_run");
        return NULL;
    }

    // vcpu->vcpu_thread_func = fn;
    return vcpu;
}

void kvm_reset_vcpu(struct vcpu *vcpu)
{
    if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &(vcpu->sregs)) < 0)
    {
        perror("can not get sregs\n");
        exit(1);
    }

    vcpu->sregs.cs.selector = CODE_START;
    vcpu->sregs.cs.base = CODE_START * 16;
    vcpu->sregs.ss.selector = CODE_START;
    vcpu->sregs.ss.base = CODE_START * 16;
    vcpu->sregs.ds.selector = CODE_START;
    vcpu->sregs.ds.base = CODE_START * 16;
    vcpu->sregs.es.selector = CODE_START;
    vcpu->sregs.es.base = CODE_START * 16;
    vcpu->sregs.fs.selector = CODE_START;
    vcpu->sregs.fs.base = CODE_START * 16;
    vcpu->sregs.gs.selector = CODE_START;

    if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0)
    {
        perror("can not set sregs");
        exit(1);
    }

    vcpu->regs.rflags = 0x0000000000000002ULL;
    vcpu->regs.rip = 0;
    vcpu->regs.rsp = 0xffffffff;
    vcpu->regs.rbp = 0;

    if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0)
    {
        perror("KVM SET REGS\n");
        exit(1);
    }
}


void kvm_run_vm(struct vm *vm1, struct vm *vm2) {
    int ret;

    kvm_reset_vcpu(vm1->vcpus);
    kvm_reset_vcpu(vm2->vcpus);

    while (1) {
        sleep(1);
        ret = ioctl(vm1->vcpus->vcpu_fd, KVM_RUN, 0);
        if (ret < 0) {
            perror("KVM_RUN vm1");
            exit(1);
        }
        if (vm1->vcpus->kvm_run->exit_reason == KVM_EXIT_IO) {
            if (vm1->vcpus->kvm_run->io.direction == KVM_EXIT_IO_OUT &&
                vm1->vcpus->kvm_run->io.port == 0x10) {
                int produced = *(int *)((char *)vm1->vcpus->kvm_run +
                                        vm1->vcpus->kvm_run->io.data_offset);
                printf("VMFD: %d KVM_EXIT_IO\n", vm1->vm_fd);
                printf("VMFD: %d Produced value: %d\n", vm1->vm_fd, produced);
                shared_value = produced;
            } else {
                printf("VMFD: %d Unknown IO from vm1\n", vm1->vm_fd);
            }
        } else if (vm1->vcpus->kvm_run->exit_reason == KVM_EXIT_HLT) {
            printf("VMFD: %d Shutdown\n", vm1->vm_fd);
            break;
        } else {
            printf("VMFD: %d Unexpected exit reason: %d\n",
                   vm1->vm_fd, vm1->vcpus->kvm_run->exit_reason);
            exit(1);
        }
        
        ret = ioctl(vm2->vcpus->vcpu_fd, KVM_RUN, 0);
        if (ret < 0) {
            perror("KVM_RUN vm2 (input)");
            exit(1);
        }
        if (vm2->vcpus->kvm_run->exit_reason == KVM_EXIT_IO) {
            if (vm2->vcpus->kvm_run->io.direction == KVM_EXIT_IO_IN &&
                vm2->vcpus->kvm_run->io.port == 0x11) {
                printf("VMFD: %d KVM_EXIT_IO\n", vm2->vm_fd);
                printf("VMFD: %d Consuming the number\n", vm2->vm_fd);
                *(int *)((char *)vm2->vcpus->kvm_run +
                         vm2->vcpus->kvm_run->io.data_offset) = shared_value;
            } else {
                printf("VMFD: %d Unknown IO (input) from vm2\n", vm2->vm_fd);
            }
        } else if (vm2->vcpus->kvm_run->exit_reason == KVM_EXIT_HLT) {
            printf("VMFD: %d Shutdown\n", vm2->vm_fd);
            break;
        } else {
            printf("VMFD: %d Unexpected exit reason: %d\n",
                   vm2->vm_fd, vm2->vcpus->kvm_run->exit_reason);
            exit(1);
        }
        
        ret = ioctl(vm2->vcpus->vcpu_fd, KVM_RUN, 0);
        if (ret < 0) {
            perror("KVM_RUN vm2 (output)");
            exit(1);
        }
        if (vm2->vcpus->kvm_run->exit_reason == KVM_EXIT_IO) {
            if (vm2->vcpus->kvm_run->io.direction == KVM_EXIT_IO_OUT &&
                vm2->vcpus->kvm_run->io.port == 0x12) {
                int consumed = *(int *)((char *)vm2->vcpus->kvm_run +
                                        vm2->vcpus->kvm_run->io.data_offset);
                printf("VMFD: %d KVM_EXIT_IO\n", vm2->vm_fd);
                printf("VMFD: %d Consumed value: %d\n", vm2->vm_fd, consumed);
            } else {
                printf("VMFD: %d Unknown IO (output) from vm2\n", vm2->vm_fd);
            }
        } else if (vm2->vcpus->kvm_run->exit_reason == KVM_EXIT_HLT) {
            printf("VMFD: %d Shutdown\n", vm2->vm_fd);
            break;
        } else {
            printf("VMFD: %d Unexpected exit reason: %d\n",
                   vm2->vm_fd, vm2->vcpus->kvm_run->exit_reason);
            exit(1);
        }
    }
}

void kvm_clean_vm(struct vm *vm)
{
    close(vm->vm_fd);
    munmap((void *)vm->ram_start, vm->ram_size);
}

void kvm_clean_vcpu(struct vcpu *vcpu)
{
    munmap(vcpu->kvm_run, vcpu->kvm_run_mmap_size);
    close(vcpu->vcpu_fd);
}

void kvm_clean(struct vm *vm)
{
    assert(vm != NULL);
    close(vm->dev_fd);
    free(vm);
}

int main(int argc, char **argv)
{
    struct vm *vm1 = malloc(sizeof(struct vm));
    struct vm *vm2 = malloc(sizeof(struct vm));

    kvm_init(vm1, vm2);

    if (kvm_create_vm(vm1, RAM_SIZE) < 0)
    {
        fprintf(stderr, "create vm fault\n");
        return -1;
    }

    if (kvm_create_vm(vm2, RAM_SIZE) < 0)
    {
        fprintf(stderr, "create vm fault\n");
        return -1;
    }

    load_binary(vm1, BINARY_FILE1);
    load_binary(vm2, BINARY_FILE2);

    vm1->vcpu_number = 1;
    vm1->vcpus = kvm_init_vcpu(vm1, 0);

    vm2->vcpu_number = 1;
    vm2->vcpus = kvm_init_vcpu(vm2, 0);

    kvm_run_vm(vm1, vm2);

    kvm_clean_vm(vm1);
    kvm_clean_vm(vm2);

    kvm_clean_vcpu(vm1->vcpus);
    kvm_clean_vcpu(vm2->vcpus);
    kvm_clean(vm1);
    kvm_clean(vm2);
}
