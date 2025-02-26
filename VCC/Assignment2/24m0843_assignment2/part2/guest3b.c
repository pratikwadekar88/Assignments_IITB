#include <stddef.h>
#include <stdint.h>

// Structure used to send consumption information back to the hypervisor.
// struct cons_info {
//     uint32_t buffer_addr; // Address where the consumer stores the five numbers
//     uint32_t count;       // The number of items consumed (always 5 here)
// } __attribute__((packed));

void __attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
    static volatile uint32_t consumer_buffer[5] __attribute__((aligned(16)));
    // static volatile struct cons_info info __attribute__((aligned(16)));

    /* Signal that we have loaded (do not remove!) */
    *(volatile long *)0x400 = 42;

    while (1) {
        uint32_t buf_addr = (uint32_t)consumer_buffer;

        // Send buffer address to hypervisor
        asm volatile(
            "outl %0, %1"
            :
            : "a"(buf_addr), "Nd"(0xE9)
            : "memory"
        );

        // Memory barrier to ensure data is read after hypercall
        asm volatile ("" ::: "memory");

        // // Process the data in the buffer
        // for (int i = 0; i < 5; i++) {
        //     volatile uint32_t value = consumer_buffer[i];
        // }

        // Prepare consumption info
        // info.buffer_addr = buf_addr;
        // info.count = 5;

        // uint32_t info_addr = (uint32_t)&info;

        // Send consumption info address to hypervisor
        asm volatile(
            "outl %0, %1"
            :
            : "a"(buf_addr), "Nd"(0xE9)
            : "memory"
        );
    }

    for (;;)
        asm("hlt" : : "a"(42) : "memory");
}
