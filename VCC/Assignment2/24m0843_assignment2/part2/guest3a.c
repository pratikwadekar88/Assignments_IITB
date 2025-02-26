#include <stddef.h>
#include <stdint.h>

// The producer creates an array of 5 numbers and sends it to the hypervisor.
void __attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
    static volatile uint32_t producer_buffer[5] __attribute__((aligned(16)));
    static volatile uint32_t counter = 0;

    // Signal that we have loaded (do not remove!)
    *(volatile long *)0x400 = 42;

    while (1) {
        for (int i = 0; i < 5; i++) {
            producer_buffer[i] = counter + i;
        }
        counter += 5;

        uint32_t buf_addr = (uint32_t)producer_buffer;

        // Send the base address of the buffer to the hypervisor
        asm volatile ("outl %0, %1" : : "a"(buf_addr), "Nd"(0xE7) : "memory");
    }

    for (;;)
        asm("hlt" : : "a"(42) : "memory");
}
