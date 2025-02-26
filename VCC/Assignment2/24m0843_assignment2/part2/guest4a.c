#include <stddef.h>
#include <stdint.h>
#define BUFFER_SIZE 20

uint32_t prod_p = 0;
uint32_t cons_p = 0;
uint32_t buffer_base_addr;

static volatile uint32_t local_buffer[BUFFER_SIZE] __attribute__((aligned(16)));

static void outl(uint16_t port, uint32_t *value)
{
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}
uint32_t get_random()
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (((uint64_t)hi << 32) | lo) % 11;
}
uint32_t isEmpty()
{
    return (cons_p == (uint32_t)-1);
}

uint32_t isFull()
{
    if (isEmpty())
    {
        return 0;
    }
    return ((prod_p + 1) % BUFFER_SIZE) == cons_p;
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

uint32_t enqueue(uint32_t item)
{
    if (isFull())
    {
        return -1;
    }

    if (isEmpty())
    {
        cons_p = 0;
        prod_p = 0;
    }
    else
    {
        prod_p = (prod_p + 1) % BUFFER_SIZE;
    }

    local_buffer[prod_p] = item;
    return 0;
}
// uint32_t count =1;
void produce()
{
    uint32_t items = get_random();
    if (items > BUFFER_SIZE - queueSize())
        items = BUFFER_SIZE - queueSize();
    for (uint32_t i = 0; i < items; i++)
    {
        uint32_t item = get_random();
        enqueue(item);
    }
}
void __attribute__((noreturn)) __attribute__((section(".start"))) _start(void)
{

    buffer_base_addr = (uint32_t)local_buffer;
    outl(0xEA, (uint32_t *)&cons_p);
    outl(0xEA, (uint32_t *)&prod_p);
    asm volatile("outl %0, %1" : : "a"(buffer_base_addr), "Nd"(0xEA) : "memory");

    while (1)
    {
        outl(0xE8, (uint32_t *)&buffer_base_addr);
        produce();
        outl(0xE9, (uint32_t *)&buffer_base_addr);
    }
    for (;;)
    {
        asm("hlt");
    }
}
