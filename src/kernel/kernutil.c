#include <kernel/kernutil.h>
#include <stdio.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <string.h>
#include <kernel/task.h>
#include <kernel/backtrace.h>

// Write a byte to the specified port
void outb(uint16 port, uint8 value)
{
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

void outw(uint16 port, uint16 value)
{
    asm volatile ("outw %1, %0" : : "dN" (port), "a" (value));
}

void outl(uint16 port, uint32 value)
{
    asm volatile ("outl %1, %0" : : "dN" (port), "a" (value));
}

uint8 inb(uint16 port)
{
   uint8 ret;
   asm volatile("inb %1, %0" : "=a" (ret) : "dN" (port));
   return ret;
}

uint16 inw(uint16 port)
{
   uint16 ret;
   asm volatile ("inw %1, %0" : "=a" (ret) : "dN" (port));
   return ret;
}

uint32 inl(uint16 port)
{
   uint32 ret;
   asm volatile ("inl %1, %0" : "=a" (ret) : "dN" (port));
   return ret;
}

extern bool kernel_paniced;
extern char _printk_buf[1024];

void panic(const char *fmt, ...) {
	asm volatile("cli");
	console_switch(&kernel_console);
	console_task = &kernel_task; // always has a console; this way, putchar() won't panic and cause a triple fault
	scrollback_reset();

	va_list args;
	int i;

	printk("\nPANIC: ");

	va_start(args, fmt);
	i = vsprintf(_printk_buf, fmt, args);
	va_end(args);

	if (i > 0) {
		size_t len = strlen(_printk_buf);
		for (size_t j = 0; j < len; j++) {
			putchar(_printk_buf[j]);
		}
	}

	printk("\nCurrent task: %u (%s)", current_task->id, current_task->name);

	kernel_paniced = true;
	update_statusbar();

	// Uncommenting cli+hlt makes scrollback after a panic impossible, but
	// on the other hand, it gives useful backtraces and debugging possibilities
	// in gdb post-panic.
	// HOWEVER, it also makes it a not-really-true panic, since interrupts are still working,
	// and tasks don't stop...!

	asm volatile("cli; 0: hlt ; jmp 0b");

	for(;;) { sleep(10000000); }
}

extern void panic_assert(const char *file, uint32 line, const char *desc) {
	/* Call panic() instead of doing this ourselves, so that breakpoints
	 * on panic() catches assertions as well */
	printk("\nAssertion failure! Backtrace:\n");
	print_backtrace();
	panic("Assertion failed: %s (%s:%d)\n", desc, file, line);
}

void reset(void) {
	/* Resets the CPU by causing a triple fault.
	 * More specifically, it creates a NULL IDT pointer, loads the "IDT", and causes an interrupt.
	 * There is, of course, no handler available to handle that interrupt, which eventually causes a triple fault. */
	struct idt_ptr p;
	memset(&p, 0, sizeof(struct idt_ptr));
	asm volatile("lidt (%0);"
		"int $3;"
		: : "r"(&p));
	for(;;); // To keep GCC quiet; the above WILL not return, but GCC doesn't believe that
}

void reboot(void) {
	/* Restarts the computer. Initially, this simply calls reset(); in the future,
	 * it will call the necessary cleanup functions, flush disk caches etc. */
	reset();
}

#define GP  0x107   /* x^8 + x^2 + x + 1 */
#define DI  0x07


static unsigned char crc8_table[256];     /* 8-bit table */
static int made_table=0;

int halt() {
	while (true) {
		sleep(100000);
		//asm volatile("sti; hlt");
		//YIELD;
	}
	return 0;
}

static void init_crc8()
     /*
      * Should be called before any other crc function.
      */
{
  int i,j;
  unsigned char crc;

  if (!made_table) {
    for (i=0; i<256; i++) {
      crc = i;
      for (j=0; j<8; j++)
        crc = (crc << 1) ^ ((crc & 0x80) ? DI : 0);
      crc8_table[i] = crc & 0xFF;
      /* printf("table[%d] = %d (0x%X)\n", i, crc, crc); */
    }
    made_table=1;
  }
}


unsigned char* crc8(unsigned char *crc, unsigned char m)
     /*
      * For a byte array whose accumulated crc value is stored in *crc, computes
      * resultant crc obtained by appending m to the byte array
      */
{
  if (!made_table)
    init_crc8();

  *crc = crc8_table[(*crc) ^ m];
  *crc &= 0xFF;
  return crc;
}



#define SLP_EN  (1 << 13)
void shutdown() // by Napalm and Falkman
{
    unsigned int i, j, len, count, found, *ptr, *rsdp = 0, *rsdt = 0, *facp = 0, *dsdt = 0;
    unsigned char *data, slp_typ[2];

    // find acpi RSDP table pointer
    for(ptr = (unsigned int *)0x000E0000; ptr < (unsigned int *)0x000FFFFF; ptr++){
        if(*ptr == " DSR" && *(ptr + 1) == " RTP"){ // "RSD PTR "
            if(crc8((unsigned char *)ptr, 20)) continue;
            rsdp = ptr;
            break;
        }
    }
    if(!rsdp) goto haltonly;

    // find RSDT table pointer
    ptr = (unsigned int *)*(ptr + 4);
    if(crc8((unsigned char *)ptr, *(ptr + 1))){
        printk("Error: RSDT checksum mismatch.\n");
        goto haltonly;
    }
    rsdt  = ptr;
    count = (*(ptr + 1) - 36) / 4;

    // find FACP table pointer
    ptr += 9; // skip RSDT entries
    for(i = 0; i < count; i++){
        for(j = 0; j < 24; j++){
            if(*(unsigned int *)*ptr == 'PCAF'){ // "FACP"
                facp = (unsigned int *)*ptr;
                i = count;
                break;
            }
        }
    }
    if(!facp){
        printk("Error: Could not find FACP table.\n");
        goto haltonly;
    }
    if(crc8((unsigned char *)facp, *(facp + 1))){
        printk("Error: FACP checksum mismatch.\n");
        goto haltonly;
    }

    // find DSDT table pointer
    ptr = (unsigned int *)*(facp+10); // DSDT address
    if(*ptr != 'TDSD'){ // "DSDT"
        printk("Error: Could not find DSDT table.\n");
        goto haltonly;
    }
    if(crc8((unsigned char *)ptr, *(ptr + 1))){
        printk("Error: DSDT checksum mistmatch.\n");
        goto haltonly;
    }
    dsdt = ptr;

    // Search DSDT byte-code for ACPI _S5 signature
    found = 0;
    len   = *(dsdt + 1) - 36;
    data  = (unsigned char *)(dsdt + 36);
    while(len--){
        if((*(unsigned int *)data & 0x00FFFFFF) == 0x0035535F){ // "_S5"
            data += 4;
            if(*data == 0x12){ // 0x012 = package opcode
                data += 3; // 0x0A = 8bit integer opcode
                slp_typ[0] = (*data == 0x0A) ? *++data : *data;
                data++;
                slp_typ[1] = (*data == 0x0A) ? *++data : *data;
                found = 1;
            }
            break;
        }
        data++;
    }
    if(!found) goto haltonly;

    // execute the actual shutdown and power-off
    outw(*(facp + 16), slp_typ[0] | SLP_EN);     // FACP[64] = PM1a_CNT_BLK
    if(*(facp + 17))
        outw(*(facp + 17), slp_typ[1] | SLP_EN); // FACP[68] = PM1b_CNT_BLK
    printk("Shutting down...\n");
    halt();

haltonly:
    printk("It is now safe to turn-off your computer.\n");
    halt();

}
