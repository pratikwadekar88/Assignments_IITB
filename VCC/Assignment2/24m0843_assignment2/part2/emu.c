// emu.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <linux/kvm.h>

#define KVM_DEVICE "/dev/kvm"
#define RAM_SIZE 0x400000 // 4 MB
#define CODE_START 0x1000
#define BINARY_FILE1 "guesta.bin"
#define BINARY_FILE2 "guestb.bin"

struct vm;
struct vcpu;

struct vcpu {
    int vcpu_id;
    int vcpu_fd;
    struct kvm_run *kvm_run;
    int kvm_run_mmap_size;
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    int initialized; // 0 if not initialized, 1 if initialized
};

struct vm {
    int dev_fd;
    int kvm_version;
    int vm_fd;
    struct kvm_userspace_memory_region mem;
    struct vcpu *vcpus;
    uint64_t ram_size;
    uint64_t ram_start;
    int vcpu_number;
};

// Function prototypes
void kvm_init(struct vm *vm1, struct vm *vm2);
int kvm_create_vm(struct vm *vm, uint64_t ram_size);
void load_binary(struct vm *vm, const char *binary_file);
struct vcpu *kvm_init_vcpu(struct vm *vm, int vcpu_id);
void kvm_reset_vcpu(struct vcpu *vcpu);
void handle_io_exit(struct vm *vm);
int run_vm_until_io(struct vm *vm);
void kvm_run_vm(struct vm *vm1, struct vm *vm2);
void kvm_clean_vm(struct vm *vm);
void kvm_clean_vcpu(struct vcpu *vcpu);
void kvm_clean(struct vm *vm);

int main(int argc, char **argv) {
    struct vm *vm1 = malloc(sizeof(struct vm));
    struct vm *vm2 = malloc(sizeof(struct vm));

    kvm_init(vm1, vm2);

    if (kvm_create_vm(vm1, RAM_SIZE) < 0) {
        fprintf(stderr, "create vm1 fault\n");
        return -1;
    }

    if (kvm_create_vm(vm2, RAM_SIZE) < 0) {
        fprintf(stderr, "create vm2 fault\n");
        return -1;
    }

    load_binary(vm1, BINARY_FILE1);
    load_binary(vm2, BINARY_FILE2);

    vm1->vcpu_number = 1;
    vm1->vcpus = kvm_init_vcpu(vm1, 0);

    vm2->vcpu_number = 1;
    vm2->vcpus = kvm_init_vcpu(vm2, 0);

    kvm_run_vm(vm1, vm2);

    kvm_clean_vcpu(vm1->vcpus);
    kvm_clean_vcpu(vm2->vcpus);

    kvm_clean_vm(vm1);
    kvm_clean_vm(vm2);

    kvm_clean(vm1);
    kvm_clean(vm2);

    return 0;
}

void kvm_init(struct vm *vm1, struct vm *vm2) {
    int dev_fd = open(KVM_DEVICE, O_RDWR);

    if (dev_fd < 0) {
        perror("open /dev/kvm");
        exit(1);
    }

    int kvm_version = ioctl(dev_fd, KVM_GET_API_VERSION, 0);

    if (kvm_version < 0) {
        perror("KVM_GET_API_VERSION");
        exit(1);
    }

    if (kvm_version != KVM_API_VERSION) {
        fprintf(stderr, "Got KVM api version %d, expected %d\n",
                kvm_version, KVM_API_VERSION);
        exit(1);
    }

    vm1->dev_fd = dev_fd;
    vm2->dev_fd = dev_fd;
    vm1->kvm_version = kvm_version;
    vm2->kvm_version = kvm_version;
}

int kvm_create_vm(struct vm *vm, uint64_t ram_size) {
    int ret = 0;
    vm->vm_fd = ioctl(vm->dev_fd, KVM_CREATE_VM, 0);

    if (vm->vm_fd < 0) {
        perror("can not create vm");
        return -1;
    }

    vm->ram_size = ram_size;

    vm->ram_start = (uint64_t)mmap(NULL, vm->ram_size,
                                   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                                   -1, 0);

    if ((void *)vm->ram_start == MAP_FAILED) {
        perror("can not mmap ram");
        return -1;
    }

    vm->mem.slot = 0;
    vm->mem.flags = 0;
    vm->mem.guest_phys_addr = 0;
    vm->mem.memory_size = vm->ram_size;
    vm->mem.userspace_addr = vm->ram_start;

    ret = ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &(vm->mem));

    if (ret < 0) {
        perror("can not set user memory region");
        return ret;
    }

    return ret;
}

void load_binary(struct vm *vm, const char *binary_file) {
    int fd = open(binary_file, O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "can not open binary file: %s\n", binary_file);
        exit(1);
    }

    ssize_t ret = 0;
    char *p = (char *)vm->ram_start;

    while ((ret = read(fd, p, 4096)) > 0) {
        printf("VMFD: %d, Loaded Program with size: %ld\n", vm->vm_fd, ret);
        p += ret;
    }

    if (ret < 0) {
        perror("read binary file");
        exit(1);
    }

    close(fd);
}

struct vcpu *kvm_init_vcpu(struct vm *vm, int vcpu_id) {
    struct vcpu *vcpu = malloc(sizeof(struct vcpu));
    vcpu->vcpu_id = vcpu_id;
    vcpu->vcpu_fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, vcpu->vcpu_id);

    if (vcpu->vcpu_fd < 0) {
        perror("can not create vcpu");
        return NULL;
    }

    vcpu->kvm_run_mmap_size = ioctl(vm->dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);

    if (vcpu->kvm_run_mmap_size < 0) {
        perror("can not get vcpu mmap size");
        return NULL;
    }

    vcpu->kvm_run = mmap(NULL, vcpu->kvm_run_mmap_size,
                         PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->vcpu_fd, 0);

    if (vcpu->kvm_run == MAP_FAILED) {
        perror("can not mmap kvm_run");
        return NULL;
    }

    vcpu->initialized = 0;
    return vcpu;
}


void kvm_reset_vcpu(struct vcpu *vcpu) {
    if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &(vcpu->sregs)) < 0) {
        perror("can not get sregs");
        exit(1);
    }

    // Set up 16-bit real mode
    vcpu->sregs.cs.selector = 0;
    vcpu->sregs.cs.base = 0;
    vcpu->sregs.ds.selector = 0;
    vcpu->sregs.ds.base = 0;
    vcpu->sregs.es.selector = 0;
    vcpu->sregs.es.base = 0;
    vcpu->sregs.fs.selector = 0;
    vcpu->sregs.fs.base = 0;
    vcpu->sregs.gs.selector = 0;
    vcpu->sregs.gs.base = 0;
    vcpu->sregs.ss.selector = 0;
    vcpu->sregs.ss.base = 0;

    if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &(vcpu->sregs)) < 0) {
        perror("can not set sregs");
        exit(1);
    }

    vcpu->regs.rflags = 0x00000002;
    vcpu->regs.rip = 0;
    vcpu->regs.rsp = 0x200000; // Stack pointer at top of allocated memory
    vcpu->regs.rbp = 0;        // Corrected line

    if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0) {
        perror("can not set regs");
        exit(1);
    }
}

void handle_io_exit(struct vm *vm) {
    struct kvm_run *kvm_run = vm->vcpus->kvm_run;
    uint16_t port = kvm_run->io.port;
    uint8_t direction = kvm_run->io.direction;
    uint32_t size = kvm_run->io.size;
    uint64_t data_offset = kvm_run->io.data_offset;
    uint8_t *data = ((uint8_t *)kvm_run) + data_offset;

    if (direction == KVM_EXIT_IO_OUT) {
        // Handle output operations
        uint32_t value = 0;
        if (size == 1) {
            value = *data;
        } else if (size == 2) {
            value = *(uint16_t *)data;
        } else if (size == 4) {
            value = *(uint32_t *)data;
        }

        printf("VMFD: %d KVM_EXIT_IO\n", vm->vm_fd);
        printf("VMFD: %d out port: %d, data: %d\n", vm->vm_fd, port, value);
    } else if (direction == KVM_EXIT_IO_IN) {
        // Handle input operations if necessary
        memset(data, 0, size); // Return zeroed data
    } else {
        fprintf(stderr, "Unknown I/O direction: %d\n", direction);
        exit(1);
    }
}

int run_vm_until_io(struct vm *vm) {
    int ret;

    // Initialize vCPU if not already done
    if (!vm->vcpus->initialized) {
        kvm_reset_vcpu(vm->vcpus);
        vm->vcpus->initialized = 1;
        printf("VMFD: %d started running\n", vm->vm_fd);
    }

    while (1) {
        ret = ioctl(vm->vcpus->vcpu_fd, KVM_RUN, 0);

        if (ret < 0) {
            perror("KVM_RUN failed");
            exit(1);
        }

        printf("VMFD: %d stopped running - exit reason: %d\n",
               vm->vm_fd, vm->vcpus->kvm_run->exit_reason);

        switch (vm->vcpus->kvm_run->exit_reason) {
            case KVM_EXIT_IO:
                // Handle I/O exits
                handle_io_exit(vm);

                // Return control to switch VMs on KVM_EXIT_IO
                return 1;

            case KVM_EXIT_HLT:
                // VM has halted
                printf("VMFD: %d KVM_EXIT_HLT, VM halted\n", vm->vm_fd);
                return 0; // Stop running this VM

            default:
                // Handle other exits or terminate on unhandled exits
                fprintf(stderr, "Unhandled exit reason: %d\n",
                        vm->vcpus->kvm_run->exit_reason);
                exit(1);
        }
    }
}

void kvm_run_vm(struct vm *vm1, struct vm *vm2) {
    int vm1_running = 1;
    int vm2_running = 1;
    int current_vm = 1; // 1 for vm1, 2 for vm2

    while (vm1_running || vm2_running) {
        sleep(1);
        if (current_vm == 1 && vm1_running) {
            vm1_running = run_vm_until_io(vm1);
            if (vm2_running) {
                current_vm = 2; // Switch to vm2
            }
        } else if (current_vm == 2 && vm2_running) {
            vm2_running = run_vm_until_io(vm2);
            if (vm1_running) {
                current_vm = 1; // Switch back to vm1
            }
        } else {
            // If one VM has halted, continue with the other
            if (vm1_running) {
                current_vm = 1;
            } else if (vm2_running) {
                current_vm = 2;
            } else {
                break; // Both VMs have halted
            }
        }
    }
}

void kvm_clean_vm(struct vm *vm) {
    close(vm->vm_fd);
    munmap((void *)vm->ram_start, vm->ram_size);
}

void kvm_clean_vcpu(struct vcpu *vcpu) {
    munmap(vcpu->kvm_run, vcpu->kvm_run_mmap_size);
    close(vcpu->vcpu_fd);
    free(vcpu);
}

void kvm_clean(struct vm *vm) {
    // Device file descriptor is shared; only close once
    static int dev_fd_closed = 0;
    if (!dev_fd_closed) {
        close(vm->dev_fd);
        dev_fd_closed = 1;
    }
    free(vm);
}
