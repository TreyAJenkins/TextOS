#include <sys/types.h>
#include <inttypes.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernel/kernutil.h> /* inb, inw, outw */
#include <kernel/console.h> /* printing, scrolling etc. */
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <stdio.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/heap.h>
#include <kernel/vmm.h>
#include <kernel/time.h>
#include <kernel/multiboot.h>
#include <kernel/initrd.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/kshell.h>
#include <kernel/ata.h>
#include <kernel/partition.h>
#include <kernel/fat.h>
#include <kernel/ext2.h>
#include <kernel/pci.h>
#include <kernel/net/rtl8139.h>
#include <kernel/net/nethandler.h>
#include <kernel/net/arp.h>
#include <kernel/net/ipicmp.h>
#include <kernel/serial.h>
#include <kernel/elf.h>
#include <kernel/fpu.h>
#include <kernel/hostinfo.h>
#include <nucleus.h>


#define STRINGIFY(x) #x
#define MACRO(x)     STRINGIFY(x)


char license[1200] = "MIT License\n"
"\n"
"Copyright (c) 2015 Trey Jenkins\n"
"\n"
"Permission is hereby granted, free of charge, to any person obtaining a copy\n"
"of this software and associated documentation files (the \"Software\"), to deal\n"
"in the Software without restriction, including without limitation the rights\n"
"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
"copies of the Software, and to permit persons to whom the Software is\n"
"furnished to do so, subject to the following conditions:\n"
"\n"
"The above copyright notice and this permission notice shall be included in all\n"
"copies or substantial portions of the Software.\n"
"\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
"SOFTWARE.\n";

/* kheap.c */
extern uint32 placement_address;

/* console.c */
extern const uint16 blank;

/* fat.c */
extern list_t *fat32_partitions;

extern nethandler_t *nethandler_arp;
extern nethandler_t *nethandler_icmp;

extern volatile list_t ready_queue;

char *kernel_cmdline = NULL;
bool quiet = false;
//char *rootdev = NULL;

extern heap_t *kheap;
extern task_t *reaper_task;

extern uint32 end; // defined in linker.ld

void reaper_func(void *data, uint32 length);

//Configuration file
struct ConfigString {
	char key[16];
	char value[48];
};

struct ConfigRoot {
	unsigned char header[10];
	unsigned int values;
	struct ConfigString object[128];
};

const char* ReadConfig(char* key) {

	struct ConfigRoot* config = kmalloc(sizeof(struct ConfigRoot));

	int fd = open("kernel.cfg", O_RDONLY);
	if (fd < 0) {
		//Invalid path
		kfree(config);
		return "";
	}

	read(fd, config, sizeof(struct ConfigRoot));
	close(fd);

	if (strncmp(config->header, "TEXTOSCFG", 9) == 0) {
		//VALID CONFIG FILE
	} else {
		//INVALID CONFIG FILE
		kfree(config);
		return "";
	}

	for (unsigned int i = 0; i < config->values; i++) {
		if (strcmp(config->object[i].key, key) == 0) {
			const char* val = config->object[i].value;
			kfree(config);
			return val;
		}
	}

	kfree(config);

	return "";
}

int TSA() {
	//loadKeymap("/etc/drivers/keyboard/EN_US.kbd", 1);
	char* kmp = ReadConfig("KEYMAP");
	printk("Loading Keymap: %s\n", kmp);
	loadKeymap(kmp, 0);

	//char* cshell = ReadConfig("SHELL");
	//if (strcmp(cshell, "~FSTEST") == 0) {
		//fstest();
	//}


	printk("All initialization complete!\n\n");
	return 0;
}

int endian() {
    volatile uint32_t i=0x01234567;
    // return 0 for big endian, 1 for little endian.
    return (*((uint8_t*)(&i))) == 0x67;
}


void kmain(multiboot_info_t *mbd, unsigned int magic, uint32 init_esp0) {

	placement_address = (uint32)&end;

	/* This must be done before anything below (GDTs, etc.), since kmalloc() may overwrite the initrd otherwise! */
	uint32 initrd_location = *((uint32 *)mbd->mods_addr);
	uint32 initrd_end = *((uint32 *)(mbd->mods_addr + 4));
	if (initrd_end > placement_address)
		placement_address = initrd_end;

	kernel_console.tasks->mutex = mutex_create();
	ready_queue.mutex = mutex_create();

	/* Set up the kernel console keybuffer, to prevent panics on keyboard input.
	 * The kernel console isn't dynamically allocated, so this can be done
	 * this early without problems. */
	kernel_console.keybuffer.read_ptr = kernel_console.keybuffer.data;
	kernel_console.keybuffer.write_ptr = kernel_console.keybuffer.data;
	kernel_console.keybuffer.counter = 0;

	/* Set up the scrollback buffer for the kernel console */
	kernel_console.buffer = kmalloc(CONSOLE_BUFFER_SIZE_BYTES);
	kernel_console.bufferptr = kernel_console.buffer;
	kernel_console.current_position = 0;
	memsetw(kernel_console.buffer, (0x7 << 8 /* grey on black */) | 0x20 /* space */, CONSOLE_BUFFER_SIZE);

	/* Set up kernel console colors */
	kernel_console.text_color = LIGHT_GREY;
	kernel_console.back_color = BLACK;

	/* This should be done EARLY on, since many other things will fail (possibly even panic() output) otherwise.
	 * NOT earlier than the kernel console setup, though! */
	init_video();

	if (magic != 0x2BADB002) {
		panic("Invalid magic received from bootloader!");
	}

	if (mbd->mods_count == 0) {
		panic("initrd.img not loaded! Make sure the GRUB config contains a \"module\" line.\nSystem halted.");
	}

	// Parse the kernel command line
	if (mbd->flags & (1 << 2)) {
		// Duplicate it, so that we know that the address will be mapped when paging is enabled
		assert(mbd->cmdline != 0);
		kernel_cmdline = strdup((char *)mbd->cmdline);

		char *p = strstr(kernel_cmdline, "quiet");
		if (p) {
			if (*(p+5) == ' ' || *(p+5) == 0) {}
				quiet = true;
		}
	}

	//printk("Little Endian: %i\n", endian());

	if (mbd->flags & 1) {
		unsigned int ram = (mbd->mem_lower) + (mbd->mem_upper);
		printk("Memory available: %u\n", ram);

		if (!quiet)
			printk("Memory info: %u kiB lower, %u kiB upper\n", mbd->mem_lower, mbd->mem_upper);
	}
	else
		panic("mbd->flags bit 0 is unset!");

	if(!quiet)
		printk("Kernel command line: %s\n", kernel_cmdline);

	// Ensure that the CPU has the necessary features: FPU and MMX support.
	// Since there are no CPUs with MMX but withut x87 FPU as far as I know,
	// this only checks for MMX support.
	// CPUID support is assumed; it was added in the Pentium (and some 486 CPUs),
	// which is about as far back as I'm willing to go. I've always had the Pentium
	// in mind when developing.
	int edx;
	asm volatile("movl $1, %%eax;"
			     "cpuid;"
				 " mov %%edx, %[edxout]"
				 : [edxout] "=m"(edx)
				 :
				 : "eax", "ebx", "ecx", "edx");

	if ((edx & (1 << 23)) == 0)
		panic("TextOS requires MMX support! Halting.");

	if ((edx & (1 << 24)) == 0)
		panic("Your CPU doesn't support the FXSAVE/FXRSTOR instructions!");

#define do_init(str, func) do { if (!quiet) printk(str); func; if (!quiet) printc(BLACK, GREEN, "done\n"); } while(0);

	/* Time to get started initializing things! */
	do_init("Initializing serial port... ", init_serial());
	do_init("Initializing GDTs... ", gdt_install());
	do_init("Initializing IDTs... ", idt_install());
	do_init("Initializing ISRs and enabling interrupts... ", enable_interrupts());
	do_init("Initializing keyboard... ", init_keyboard());
	do_init("Initializing the PIT... ", timer_install());
	do_init("Initializing the FPU... ", fpu_init());

	/* Initialize the initrd */
	/* (do this before paging, so that it doesn't end up in the kernel heap) */
	init_initrd(initrd_location);

	if (!(mbd->flags & (1 << 5)))
		panic("No kernel symbols!");

	load_kernel_symbols((void *)mbd->u.elf_sec.addr, mbd->u.elf_sec.num, mbd->u.elf_sec.size, mbd->u.elf_sec.shndx);

	/* Set up paging and the kernel heap */
	if (!quiet)
		printk("Initializing paging and setting up the kernel heap... ");

	//mbd->flags &= ~(1<<6); // Uncomment to test with no memory map
	if (mbd->flags & (1 << 6)) {
		// If the memory map is available
		init_paging(mbd->mmap_addr, mbd->mmap_length, mbd->mem_upper);
	}
	else
		init_paging(0, 0, mbd->mem_upper);

	if (!quiet)
		printc(BLACK, GREEN, "done\n");

	do_init("Detecting and initializing PCI devices... ", init_pci());
	do_init("Initializing syscalls... ", init_syscalls());
	do_init("Initializing multitasking and setting up the kernel task... ", init_tasking(init_esp0));

	//printk("TextOS Version: %s\n", trim(MACRO(BUILDID)));

	GetCPUVendor();
	GetCPUName();
	printk("Processor: %s (%s)\n", trim(CPUName), trim(CPUVendor));

	//if (!quiet)
	//GetCPUSpeed();
	//printk("CPU Speed: %d\n", GetCPUSpeed());

	//Initialize Nucleus
	if (!quiet) printk("Initializing Nucleus:\n");
	extern long AuthToken;
	extern bool OpenToken;
	if (!quiet) printk("  [*] Seeding the PRNG ... ");
	SetSeed(AuthToken, (long) gettickcount()); // Seed the PRNG with the tick count
	if (!quiet) printc(BLACK, GREEN, "[SUCCESS]\n");
	if (!quiet) printk("  [*] Generating an Authentication Token ... ");
	GenerateToken();
	if (!quiet) printc(BLACK, GREEN, "[SUCCESS]\n");
	if (!quiet) printk("  [*] Initializing the Stack Smashing Detector ... ");
	InitSSP();
	if (!quiet) printc(BLACK, GREEN, "[SUCCESS]\n");
	if (!quiet) printk("\n");
	if (quiet) printk("Nucleus initialized successfully\n");


#if 1
	do_init("Detecting ATA devices and initializing them... ", ata_init());
	do_init("Parsing MBRs... ", for (int i=0; i<3; i++) parse_mbr(&devices[i]));

	/* Detect FAT and ext2 filesystems on all partitions */
	for (int disk = 0; disk < 4; disk++) {

		if (!devices[disk].exists || devices[disk].is_atapi)
			continue;

		for (int part = 0; part < 4; part++) {
			if (devices[disk].partition[part].exists &&
					(devices[disk].partition[part].type == PART_FAT32 ||
					 devices[disk].partition[part].type == PART_FAT32_LBA))
			{
				fat_detect(&devices[disk], part);
			}
			else if (devices[disk].partition[part].exists && devices[disk].partition[part].type == PART_LINUX) {
				ext2_detect(&devices[disk], part);
			}
		}
	}
#endif

#if 0
	// DON'T ENABLE THIS without double-checking the disk images!!!
	memset(buf, 0, 512);
	char *b2 = kmalloc(32768);
	assert(b2 != NULL);
	uint32 start_t = gettickcount();
	for (uint64 i = 0; i < 1000; i++) {
		assert(buf != NULL);
		ata_write(ata_dev, i * (32768/512), (uint8 *)b2, (32768 / 512));
		//assert(buf != NULL);
		//if (*buf == 0)
			//continue;
		//printk("LBA%u: \"%s\"\n", i, (char *)buf);
		//assert(buf != NULL);
	}
	uint32 end_t = gettickcount();
	uint32 d = end_t - start_t;
	d *= 10;
	printk("Writing 64000 sectors took %u ms\n", d);
#endif

	printk("Mounting filesystems... ");
	if (fs_mount())
		printc(BLACK, GREEN, "done\n");
	else
		printc(BLACK, RED, "failed!\n");


#if 1
	if (!quiet)
		printk("Initializing RTL8139 network adapter... ");
	if (init_rtl8139()) {
		if (!quiet)
			printc(BLACK, GREEN, "done\n");

		printk("Starting network data handlers... ");
		nethandler_arp = nethandler_create("nethandler_arp", arp_handle_packet);
		nethandler_icmp = nethandler_create("nethandler_icmp", handle_icmp);
		if (nethandler_arp && nethandler_icmp)
			printc(BLACK, GREEN, "done\n");
		else
			printc(BLACK, RED, "failed!\n");
	}
	else if (!quiet)
		printc(BLACK, RED, "none found!\n");
#endif

	virtual_consoles[0] = &kernel_console;
	assert(virtual_consoles[0]->active == true);


#if 1
	/* Set up the virtual consoles (Alt+F1 through F4 at the time of writing) */
	assert(NUM_VIRTUAL_CONSOLES >= 2); /* otherwise this loop will cause incorrect array access */
	for (int i=1 /* sic! */; i < NUM_VIRTUAL_CONSOLES; i++) {
		virtual_consoles[i] = console_create();
		/* node_t *new_node = */list_append(virtual_consoles[i]->tasks, create_task(&kshell, "kshell", virtual_consoles[i], NULL, 0));
		//((task_t *)new_node->data)->console = &virtual_consoles[i];
		assert(virtual_consoles[i]->active == false);
	}

	//console_switch(&virtual_consoles[0]);
#endif



	/* Hack-setup a kernel shell on the kernel console */
	assert(virtual_consoles[0] == &kernel_console);
	/*task_t *kernel_shell =*/
	create_task(&TSA, "TSA", virtual_consoles[0], NULL, 0);

	create_task(&kshell, "kshell", virtual_consoles[0], NULL, 0);

	halt();

	printk("\n\n");
	printk("kmain() done; running infinite loop\n");
	for(;;);
}
