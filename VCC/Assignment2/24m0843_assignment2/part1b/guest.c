#include <stddef.h>
#include <stdint.h>
#define MAX_BUFFERS 20

static char buffers[MAX_BUFFERS][256];
static int buffer_index = 0;

static void outb(uint16_t port, uint8_t value)
{
	asm("outb %0,%1" : /* empty */ : "a"(value), "Nd"(port) : "memory");
}
static void outl(uint16_t port, uint32_t value)
{
	asm volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

void HC_print8bit(uint8_t val)
{
	outb(0xE9, val);
}

void HC_print32bit(uint32_t val)
{
	outl(0xE9, val);
}

uint32_t HC_numExits()
{
	uint32_t val = 0;
	asm volatile("inl %1, %0" : "=a"(val) : "Nd"(0xE9));
	return val;
}
void HC_printStr(char *str)
{
	uint32_t addr = (uint32_t)(uintptr_t)str;
	outl(0xE8, addr);
}

char *HC_numExitsByType()
{
	if (buffer_index >= MAX_BUFFERS)
	{
		return NULL;
	}
	char *buffer = buffers[buffer_index++];
	uint32_t buffer_addr = (uint32_t)(uintptr_t)buffer;
	uint32_t ret_value = 0;

	asm volatile("outl %0, %1" : : "a"(buffer_addr), "Nd"(0xE7) : "memory");
	asm volatile("inl %1, %0" : "=a"(ret_value) : "Nd"(0xE7) : "memory");
	return (char *)(uintptr_t)ret_value;
}
uint32_t HC_gvaToHva(uint32_t gva)
{
	uint32_t hva = 0;
	asm volatile("outl %0, %1" ::"a"(gva), "Nd"(0xE6) : "memory");
	asm volatile("inl %1, %0" : "=a"(hva) : "Nd"(0xE6) : "memory");
	return hva;
}
void
	__attribute__((noreturn))
	__attribute__((section(".start")))
	_start(void)
{
	const char *p;

	for (p = "Hello 695!\n"; *p; ++p)
		HC_print8bit(*p);

	/*----------Don't modify this section. We will use grading script---------*/
	/*---Your submission will fail the testcases if you modify this section---*/
	HC_print32bit(2048);
	HC_print32bit(4294967295);

	uint32_t num_exits_a, num_exits_b;
	num_exits_a = HC_numExits();

	char *str = "CS695 Assignment 2\n";
	HC_printStr(str);

	num_exits_b = HC_numExits();

	HC_print32bit(num_exits_a);
	HC_print32bit(num_exits_b);

	char *firststr = HC_numExitsByType();
	uint32_t hva;
	hva = HC_gvaToHva(1024);
	HC_print32bit(hva);
	hva = HC_gvaToHva(4294967295);
	HC_print32bit(hva);
	char *secondstr = HC_numExitsByType();

	HC_printStr(firststr);
	HC_printStr(secondstr);
	/*------------------------------------------------------------------------*/

	*(long *)0x400 = 42;

	for (;;)
		asm("hlt" : /* empty */ : "a"(42) : "memory");
}
