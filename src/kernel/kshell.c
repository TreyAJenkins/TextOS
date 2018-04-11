#include <sys/types.h>
#include <kernel/kshell.h>
#include <kernel/console.h>
#include <kernel/keyboard.h>
#include <kernel/heap.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/task.h>
#include <kernel/list.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/elf.h>
#include <kernel/ata.h>
#include <kernel/fat.h>
#include <kernel/ext2.h>
#include <path.h>
#include <kernel/vfs.h>
#include <stdio.h>
#include <sys/time.h>

#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/timer.h>
#include <stdlib.h>
#include <nucleus.h>

/* for ls_initrd() */
#include <kernel/initrd.h>

/* for lspci() */
#include <kernel/pci.h>

#define MAX_PATH 1024 // TODO: move this

void heaptest(void *data, uint32 length);

extern volatile list_t ready_queue;

static void infinite_loop(void *data, uint32 length) {
	for(;;);
}

static void PRNG16(void) {
    printk("PRNG[16] %i\n", crand(16));
}

static void PRNG64(void) {
    printk("PRNG[64] %i\n", crand(64));
}

static void testSSP(void) {
	char buff[10] = {0};
	strcpy(buff, "This String Will Overflow the Buffer");
	printk("%s\n", buff);
}

static mutex_t *test_mutex = NULL;
static void mutex_test(void *data, uint32 length) {
   if (test_mutex == NULL)
		   test_mutex = mutex_create();

   while (true) {
		   mutex_lock(test_mutex);
		   printk("locked mutex in pid %d @ %u\n", getpid(), gettickcount());
		   sleep(500);
		   mutex_unlock(test_mutex);
		   printk("unlocked mutex in pid %d @ %u\n", getpid(), gettickcount());
   }
}

extern list_t *pci_devices;
static void lspci(void *data, uint32 length) {
	assert(pci_devices != NULL);

	printk("VENDOR DEVICE CLASS IRQ BAR0     BAR1     BAR2     BAR3     BAR4     BAR5\n");
	list_foreach(pci_devices, it) {
		pci_device_t *dev = (pci_device_t *)it->data;
		printk("0x%04x 0x%04x %02x/%02x %02d  %08x %08x %08x %08x %08x %08x\n",
				dev->vendor_id, dev->device_id, dev->classcode, dev->subclasscode, dev->irq,
				dev->bar[0].address, dev->bar[1].address, dev->bar[2].address, dev->bar[3].address, dev->bar[4].address, dev->bar[5].address);
	}
}

static void spamtest(void *data, uint32 length) {
	int pid = getpid();
	while (true) {
		assert(pid == getpid());
		printk("spamtest: PID %u ticks %u\n", pid, gettickcount());
	}
}

static void create_pagefault(void *data, uint32 length) {
	uint32 *pf = (uint32 *)0xffff0000;
	*pf = 10;
}

static void create_pagefault_delay(void *data, uint32 length) {
	sleep(2000);
	create_pagefault(NULL, 0);
}

static void atabench(void *data, uint32 length) {
	char *buf = kmalloc(64*512);
	int start = gettickcount();
	ata_device_t *dev = &devices[0];
#define NUM_READS 10000
	for (int i = 0; i < NUM_READS; i++) {
		ata_read(dev, 0, buf, 64);
	}

	int end = gettickcount();

	printk("Reading %d sectors (64 at a time) took %u ms\n", 64*NUM_READS, 10 * (end - start));
}


static void cat(void *data, uint32 length) {
	char path[1024] = {0};
	strcpy(path, current_task->pwd);
	path_join(path, (char *)data);
	int fd = open(path, O_RDONLY);
	assert(fd >= 0);
	char buf[512] = {0};

	int r = 0;
	do {
		memset(buf, 0, 512);
		r = read(fd, buf, 511);
		printk("%s", buf);

	} while (r > 0);

	return;
}


static void lsk(void *data, uint32 length) {
	DIR *dir = opendir(current_task->pwd);
	if (!dir) {
		printk("ls: unable to opendir(%s) (PWD)!\n", current_task->pwd);
		return;
	}
	struct dirent *dirent;
	struct stat st;
	while ((dirent = readdir(dir)) != NULL) {
		char fullpath[1024] = {0};
		strcpy(fullpath, current_task->pwd);
		path_join(fullpath, dirent->d_name);

		stat(fullpath, &st);

		char name[32] = {0};
		if (strlen(dirent->d_name) > 31) {
			strlcpy(name, dirent->d_name, 29);
			strlcat(name, "...", 32);
		}
		else
			strcpy(name, dirent->d_name);

#if 1
		char perm_str[11] = "-rwxrwxrwx";
		if (st.st_mode & 040000)
			perm_str[0] = 'd';

		for (int i=0; i<9; i++) {
			if (!(st.st_mode & (1 << i))) {
				perm_str[9 - i] = '-';
			}
		}

		uint32 sz = st.st_size / 1024;
		char s[16] = {0};
		if (sz == 0) {
			sprintf(s, "  <1k");
		}
		else
			sprintf(s, "% 4uk", sz);

		if (st.st_mode & 040000)
			printk("%31s %5s        %s\n", name, "<DIR>", perm_str);
		else
			printk("%31s        %s %s\n", name, s, perm_str);


#endif
	}

	closedir(dir);
}

static void pwd(void *data, uint32 length) {
	printk("%s\n", current_task->pwd);
}

static void cd(void *data, uint32 length) {
	int ret;
	if ((ret = chdir(data)) != 0)
		printk("Unable to chdir() to %s: errno %d\n", data, -ret);
}

static void print_1_sec(void *data, uint32 length) {
	for (int i=0; i < 10; i++) {
		printk("print_1_sec: loop %d of 10\n", i+1);
		sleep(1000);
	}
	printk("print_1_sec exiting\n");
}

static void fill_scrollback(void *data, uint32 length) {
	for (int i=1; i <= MAX_SCROLLBACK * 2; i++) {
		printk("%d\n", i);
		if (i > 300)
			delay(40);
	}
}

static void fpu_task(void *data, uint32 length) {
	asm volatile("fldpi");
}

static void test_dfault(void *data, uint32 length) {
	asm volatile("movl $0xfabcabcd, %esp;"
	"popl %eax;");
	//asm volatile("int $8");
}

static void test_stackof(void *data, uint32 length) {
	asm volatile("0: push %eax; jmp 0b");
}

static void kernel_test(void *data, uint32 length) {
	printk("Kernel mode printk(), ticks = %u\n", gettickcount());
}

static void user_stress_elf(void *data, uint32 length) {
    uint32 start = gettickcount();
	//panic("TODO: fix ATA + FAT multitasking safety and re-enable user_stress_elf");
#if 1
	char *path = strdup("/test");
    for (int i=0; i < 2500; i++) {
		create_task_elf(path, (console_t *)data, path, strlen(path));
        YIELD;
    }
	kfree(path);
#endif
    printk("ran for %u ticks\n", gettickcount() - start);
}

static void test_write(void *data, uint32 length) {
	int fd = open("/mm.mp3", 0);
	assert(fd >= 0);
	struct stat st;
	fstat(fd, &st);
	char *buf2 = kmalloc(st.st_size);
	assert(buf2 != NULL);
	uint32 start_t = gettickcount();
	int ret = read(fd, buf2, st.st_size);
	assert(ret == st.st_size);
	uint32 end_t = gettickcount();
	printk("read took %u ms. starting write...\n", (end_t - start_t) * 10);
	ata_device_t *ata_dev = &devices[0];
	int sect = st.st_size / 512;
	if (st.st_size % 512)
		sect += 1;
	start_t = gettickcount();
	ata_write(ata_dev, 0, (uint8 *)buf2, sect);
	end_t = gettickcount();
	printk("write of %u bytes (%d sectors) took %u ms\n", (uint32)(st.st_size), sect, (end_t - start_t) * 10);
}


static void kernel_stress(void *data, uint32 length) {
	for (int i=0; i < 1000; i++) {
		create_task(kernel_test, "kernel_test", (console_t *)data, NULL, 0);
		YIELD;
	}
}

static void divzero(void *data, uint32 length) {
	printk("in divzero; dividing now\n");
	asm volatile("mov $10, %%eax; mov $0, %%ebx; div %%ebx;" : : : "%eax", "%ebx", "%edx");
	printk("divzero: after dividing\n");
}

static void delaypanic(void *data, uint32 length) {
	sleep(5000);
	panic("delaypanic()");
}

static void testbench(void *data, uint32 length) {
	/* An extremely simple "benchmark" to test approx. how much CPU time a task is getting */
	uint32 start_tick = gettickcount();
	printk("start testbench at tick %u...\n", start_tick);
	for (int i = 100000000; i != 0; i--) {
		int a = i * 10 + 4;
		i |= (a & 0); /* nop */
	}
	uint32 end_tick = gettickcount();
	printk("finish testbench at tick %u; time taken: %u ticks (%u ms)\n", end_tick, (end_tick - start_tick), (end_tick - start_tick)*10);
}

static void permaidle(void *data, uint32 length) {
	printk("permaidle task launched. no further output will be generated\n");
	for(;;) {
		sleep(100000);
	}
}

uint32 pmm_bytes_used(void);

static void free_mem(void *data, uint32 length) {
	printk("Free RAM: %u kbytes\n", pmm_bytes_free() / 1024);
	printk("Used RAM: %u kbytes\n", pmm_bytes_used() / 1024);
	printk("kheap used: %u bytes\n", kheap_used_bytes());
}

int initrd_read(int fd, void *buf, size_t length);
int initrd_open(uint32 dev, const char *path, int mode);
int initrd_close(int fd);
DIR *initrd_opendir(mountpoint_t *mp, const char *path);
struct dirent *initrd_readdir(DIR *dir);
int initrd_closedir(DIR *dir);

static void initrd_test(void) {
	/* TODO */
	uint32 deventry = 0xffffffff;
	for (int i=0; i < MAX_DEVS; i++) {
		if (devtable[i] == (void *)0xffffffff) {
			deventry = i;
		}
	}
	assert(deventry != 0xffffffff);
	DIR *dir = NULL;
	list_foreach(mountpoints, it) {
		mountpoint_t *mp = (mountpoint_t *)it->data;
		if (mp->dev == deventry) {
			dir = initrd_opendir(mp, "/");
			break;
		}
	}
	assert(dir != NULL);

	struct dirent *dent = NULL;
	while ((dent = initrd_readdir(dir)) != NULL) {
		printk("initrd: found %s, inode %u\n", dent->d_name, dent->d_ino);
	}
	initrd_closedir(dir);

	int fd = initrd_open(0, "initrd_test.txt", 0);
	assert(fd >= 0);
	char buf[512] = {0};
	int r = initrd_read(fd, buf, 511);
	assert(r > 10);
	printk("Read: %s\n", buf);

	initrd_close(fd);
}

static void sleep_test(void *data, uint32 length) {
	printk("sleep_test: sleeping for 20 seconds\n");
	sleep(20000);
	printk("sleep test done\n");
}

static void utime(void *data, uint32 length) {
	struct timeval tv;
	gettimeofday(&tv, NULL);

	printk("%d\n", tv.tv_sec);
}

extern list_t *ext2_partitions;
char *ext2_read_file(ext2_partition_t *part, uint32 inode_num, uint32 *size);
uint16 internet_checksum(void *data, uint32 len);

// Debugging helpers (for setting breakpoints post-boot, in simics)
int shells_started = 0;
void boot_finished(void) {
	return;
}
bool tracing = false;

void trace(void) {
	tracing = true;
	start_leak_trace();
	return;
}

void kshell(void *data, uint32 length) {
	unsigned char *buf = kmalloc(1024);
	memset(buf, 0, 1024);
	char *last_cmd = kmalloc(1024);
	memset(last_cmd, 0, 1024);

	task_t *task = NULL;


	/* Make sure the current code spawns a new task for the kernel f */
	assert(current_task != &kernel_task);
	assert(strlen((char *)current_task->name) >= 8 && strncmp((char *)current_task->name, "[kshell]", 8) == 0);

	// Hack: start the userspace shell automatically
	if (current_task->console == &kernel_console) {
		task = create_task_elf("/bin/sh", current_task->console, (char *)"/bin/sh", strlen("/bin/sh"));
	}

	shells_started++;

	if (shells_started == 3) {
		sleep(500);
		boot_finished(); // No-op to help debugging
	}

	while (true) {
		// Wait until the "child" task is still running
		while (task != NULL) {
			if (does_task_exist(task) == false || task->state == TASK_EXITING || task->state == TASK_DEAD) {
				//task = NULL;
				break;
			}
			sleep(30);
		}

		if (tracing) {
			// Wait until the task has been killed, or false positives will be reported
			while (does_task_exist(task)) { sleep(30); }
			task = NULL;

			tracing = false;
			stop_leak_trace();
		}
		else
			task = NULL;

		printc(BLACK, GREEN, "kshell ");
		printc(BLACK, RED, "# ");
		unsigned char ch;
		uint32 i = 0;
		while ( (ch = getchar()) != '\n') {
			assert(i < 1023);

			if (ch != 0x08) {
				/* if not backspace */
				buf[i++] = ch;
			}
			else if (i != 0) {
				/* backspace */
				i--;
			}
			else
				continue;

			putchar(ch);

			if (ch == 0x08) {
				putchar(' '); cursor_left();
			}

			update_cursor();
		}
		buf[i] = 0;

		putchar('\n');

		/* The console to give new tasks */
		console_t *con = current_task->console;

		char *p = trim((char *)buf);
		if (strcmp(p, "!!") == 0 && *last_cmd != 0) {
			/* fetch the last command run */
			strlcpy(p, last_cmd, 1024);
		}

		if (*p == 0)
			continue;



		if (strcmp(p, "help") == 0 || strncmp(p, "help ", 5) == 0) {
			printk("TextOS kernel shell help\n\nAvailable commands:\n");

			printk("!!               - re-execute last command\n");
			printk("clear            - clear the screen\n");
			printk("exit             - exit the shell\n");
			printk("free             - display how much memory is used/free\n");
			printk("heaptest         - heap stress test\n");
			printk("help             - this help screen\n");
			printk("kill <pid>       - kill a process\n");
			printk("loadKeymap <KMP> - load a keymap\n");
			printk("kshell           - start a nested kernel shell\n");
			printk("lsk              - list files (in-kernel)\n");
			printk("lspci            - print the PCI device database\n");
			printk("print_heap       - print the kernel heap index (used/free areas)\n");
			printk("ps               - show processes\n");
			printk("pwd              - print the current working directory\n");
			printk("reboot           - restart the system cleanly (not yet! calls reset)\n");
			printk("reset            - cause a triple fault immediately\n");
			printk("shutdown         - shutdown the computer\n");
			printk("testbench        - run a simple test benchmark in-kernel\n");
			printk("testbench_task   - run a simple test benchmark as a task in-kernel\n");
			printk("uptime           - show the current system uptime\n");

			printk("Additional commands may exist on the file system!\n");

			if (strcmp(p, "help all") != 0)
				printk("Type \"help all\" to also display more obscure testing commands\n");

			if (strcmp(p, "help all") == 0) {
				printk("\nTesting commands:\n");
				printk("delaypanic       - cause a kernel panic after a delay\n");
				printk("divzero          - divide by zero in-kernel\n");
				printk("divzero_task     - divide by zero in a task\n");
				printk("fill_scrollback  - fill the scrollback buffer\n");
				printk("fpu_task         - create a task that uses the FPU\n");
				printk("infloop_task     - start a task that loops forever\n");
				printk("kernel_stress    - process starting stress test (kernel mode)\n");
				printk("mutex_test       - test kernel mutexes\n");
				printk("pagefault        - create a page fault\n");
				printk("pagefault_delay  - create a page fault after a delay\n");
				printk("permaidle        - start a task that sleeps forever\n");
				printk("print_1_sec      - print stuff once a second for 10 seconds\n");
				printk("sleeptest        - start a task that sleeps for a while\n");
				printk("spamtest         - print a lot of text forever\n");
				printk("test_dfault      - cause a double fault\n");
				printk("test_stackof     - cause a kernel stack overflow\n");
				printk("user_stress_elf  - stress test the ELF loader\n");
				printk("PRNG(16/64)      - test the PRNG included with nucleus\n");
				printk("testSSP			 - test the stack smashing detector included with nucleus\n");
				printk("trace			 - start a leak trace\n");
			}
		}
		else if (strcmp(p, "trace") == 0) {
			trace();
		}
		else if (strcmp(p, "testSSP") == 0) {
			testSSP();
		}
		else if (strcmp(p, "shutdown") == 0) {
			shutdown();
		}
		else if (strcmp(p, "heaptest") == 0) {
			task = create_task(&heaptest, "heaptest", con, NULL, 0);
		}
		else if (strcmp(p, "hloop") == 0) {
			while (true) {
				task = create_task(&heaptest, "heaptest", con, NULL, 0);
				while (does_task_exist(task))
					sleep(500);
			}
		}
		else if (strcmp(p, "free") == 0) {
			task = create_task(&free_mem, "free", con, NULL, 0);
		}
		else if (strcmp(p, "delaypanic") == 0) {
			task = create_task(&delaypanic, "delaypanic", con, NULL, 0);
		}
		else if (strcmp(p, "test_write") == 0) {
			task = create_task(&test_write, "test_write", con, NULL, 0);
		}
		else if (strcmp(p, "fill_scrollback") == 0) {
			task = create_task(&fill_scrollback, "fill_scrollback", con, NULL, 0);
		}
		else if (strcmp(p, "print_heap") == 0) {
			validate_heap_index(true);
		}
		else if (strcmp(p, "clear") == 0) {
			clrscr();
		}
		else if (strcmp(p, "reboot") == 0) {
			reboot();
		}
		else if (strcmp(p, "exit") == 0) {
			break;
		}
		else if (strcmp(p, "utime") == 0) {
			utime(NULL, 0);
		}
		else if (strcmp(p, "print_1_sec") == 0) {
			task = create_task(&print_1_sec, "print_1_sec", con, NULL, 0);
		}
		else if (strcmp(p, "lspci") == 0) {
			task = create_task(&lspci, "lspci", con, NULL, 0);
		}
		else if (strcmp(p, "sleeptest") == 0) {
			task = create_task(&sleep_test, "sleeptest", con, NULL, 0);
		}
		else if (strcmp(p, "fpu_task") == 0) {
			task = create_task(&fpu_task, "fpu_task", con, NULL, 0);
		}
		else if (strcmp(p, "permaidle") == 0) {
			task = create_task(&permaidle, "permaidle", con, NULL, 0);
		}
		else if (strcmp(p, "pagefault") == 0) {
			task = create_task(&create_pagefault, "create_pagefault", con, NULL, 0);
		}
		else if (strcmp(p, "mutex_test") == 0) {
			task = create_task(&mutex_test, "mutex_test", con, NULL, 0);
		}
		else if (strcmp(p, "test_dfault") == 0) {
			task = create_task(&test_dfault, "test_dfault", con, NULL, 0);
		}
		else if (strcmp(p, "test_stackof") == 0) {
			task = create_task(&test_stackof, "test_stackof", con, NULL, 0);
		}
		else if (strcmp(p, "spamtest") == 0) {
			task = create_task(&spamtest, "spamtest", con, NULL, 0);
		}
		else if (strcmp(p, "pagefault_delay") == 0) {
			task = create_task(&create_pagefault_delay, "create_pagefault_delay", con, NULL, 0);
		}
		else if (strcmp(p, "atabench") == 0) {
			task = create_task(&atabench, "atabench", con, NULL, 0);
		}
		else if (strcmp(p, "uptime") == 0) {
			uint32 up = uptime();
			uint32 ticks = gettickcount();
			printk("Uptime: %u seconds (%u ticks)\n", up, ticks);
		}
		else if (strcmp(p, "infloop_task") == 0) {
			task = create_task(&infinite_loop, "infinite_loop", con, NULL, 0);
		}
		else if (strcmp(p, "reset") == 0) {
			reset();
		}
		else if (strcmp(p, "PRNG16") == 0) {
			PRNG16();
		}
		else if (strcmp(p, "PRNG64") == 0) {
			PRNG64();
		} else if (strcmp(p, "ps") == 0) {
			INTERRUPT_LOCK;
			node_t *cur_task_node = ready_queue.head;
			int n = 0;
			printk("%5s %6s %10s %10s %6s %s\n", "PID", "RSS", "STACK_BTM", "PAGEDIR", "STATE", "NAME");
			while (cur_task_node != NULL) {
				task_t *cur_task = (task_t *)cur_task_node->data;
				n++;
				const char *state_str;
				switch(cur_task->state) {
					case TASK_RUNNING:
						state_str = "RUN";
						break;
					case TASK_WAKING_UP:
					case TASK_SLEEPING:
						state_str = "SLEEP";
						break;
					case TASK_IOWAIT:
						state_str = "IOWAIT";
						break;
					case TASK_EXITING:
						state_str = "EXIT";
						break;
					case TASK_IDLE:
						state_str = "IDLE";
						break;
					case TASK_DEAD:
						state_str = "ZOMB";
						break;
					default:
						state_str = "UNKN.";
						break;
				}

				if (cur_task->state != TASK_DEAD) {
					assert(cur_task != NULL);
					assert(cur_task->mm != NULL);
					printk("% 5d % 5dk 0x%08x 0x%08x %06s %s\n", cur_task->id, cur_task->mm->frames_used * 4, cur_task->stack, cur_task->mm->page_directory, state_str, cur_task->name);
				}
				else {
					assert(cur_task != NULL);
					printk("% 5d ?????? 0x%08x NO DIR     %06s %s\n", cur_task->id, cur_task->stack, state_str, cur_task->name);
				}

				cur_task_node = cur_task_node->next;
			}
			printk("%d tasks running\n", n);
			INTERRUPT_UNLOCK;
		}
		else if (strcmp(p, "divzero") == 0) {
			divzero(NULL, 0);
		}
		else if (strcmp(p, "lsk") == 0) {
			lsk(NULL, 0);
		}
		else if (strcmp(p, "initrd_test") == 0) {
			initrd_test();
		}
		else if (strcmp(p, "pwd") == 0) {
			pwd(NULL, 0);
		}
		else if (strcmp(p, "divzero_task") == 0) {
			task = create_task(&divzero, "divzero", con, NULL, 0);
		}
		else if (strncmp(p, "loadKeymap ", 11) == 0) {
			p += 11;

			char out[32 + strlen(p)];
			strcpy(out, "/etc/drivers/keyboard/");
			strlcat(out, p, sizeof(out));
			strlcat(out, ".kmp", sizeof(out));
			printk("Loading %s\n", out);
			loadKeymap(out, 1);
			//loadKeymap("/etc/drivers/keyboard/EN_US.kmp", 1);
		} else if (strcmp(p, "loadKeymap") == 0) { printk("error: argument must be a keymap file\n"); }
		else if (strncmp(p, "kill ", 5) == 0) {
			p += 5;

			/* p now points to the argument */
			int pid = atoi(p);
			assert(pid > 0);
			if (kill_pid(pid)) {
				printk("killed task with PID %d\n", pid);
			}
			else {
				printk("unable to kill task with PID %d; task not found?\n", pid);
			}
		}
		else if (strncmp(p, "cksum ", 6) == 0) {
			p += 6;
			int inode = atoi(p);
			if (inode < 2) { printk("error: argument must be inode number for a file name\n"); goto exit_loop; }
			ext2_partition_t *tmp = (ext2_partition_t *)ext2_partitions->head->data;
			uint32 size;
			char *file_data = ext2_read_file(tmp, inode, &size);
			printk("cksum for inode %u = %04x\n", inode, internet_checksum(file_data, size));
			kfree(file_data);
		}
		else if (strncmp(p, "cd ", 3) == 0) {
			p += 3;
			cd(p, strlen(p));
		}
		else if (strcmp(p, "testbench") == 0) {
			testbench(NULL, 0);
		}
		else if (strcmp(p, "testbench_task") == 0) {
			task = create_task(&testbench, "testbench", con, NULL, 0);
		}
		else if (strcmp(p, "user_stress_elf") == 0) {
			task = create_task(&user_stress_elf, "user_stress_elf", con, con, 0);
		}
		else if (strcmp(p, "kernel_stress") == 0) {
			/* launch a kernel mode test task */
			task = create_task(&kernel_stress, "kernel_stress", con, con, 0);
		}
		else if (strcmp(p, "kshell") == 0) {
			/* Heh. For testing only, really... Subshells aren't high in priority for the kernel shell. */
			task = create_task(&kshell, "kshell (nested)", con, NULL, 0);
		}
		else {
			static const char _PATH[] = "/bin:/bin/tests:/initrd/bin:/initrd/bin/tests:/fat/bin:/";
			char PATH[sizeof(_PATH)] = {0};

			strcpy(PATH, _PATH); // strtok_r will modify this!

			char cmd[256];
			strlcpy(cmd, p, 256);
			char *c = strchr(cmd, ' ');
			if (c)
				*c = 0;

			char *token, *tmp;
			for (token = strtok_r(PATH, ":", &tmp); token != NULL; token = strtok_r(NULL, ":", &tmp)) {
				DIR *dir = opendir(token);
				if (!dir)
					continue;
				struct dirent *dent;
				while ((dent = readdir(dir)) != NULL) {
					if (stricmp(dent->d_name, cmd) == 0) {
						// Found it!
						char path[1024] = {0};
						strlcpy(path, token, 1024);
						path_join(path, dent->d_name);
						task = create_task_elf(path, con, p, strlen(p));

						closedir(dir);
						dir = NULL;
						goto exit_loop;
					}
				}
				if (dir) {
					closedir(dir);
					dir = NULL;
				}
			}
			printk("No such command: %s\n", p);
exit_loop:
			;
		}

		strlcpy(last_cmd, p, 1024);
	}
}

/* Used for heap debugging only. Verifies that the area from /p/ to /p + size/
 * is filled with 0xaa bytes (to make sure the area isn't overwritten by something). */
static void verify_area(void *in_p, uint32 size) {
	unsigned char *ptr;
	for (ptr = in_p; ptr < ((unsigned char *)in_p + size); ptr++) {
		if (*ptr != 0xaa) {
			print_heap_index();
			panic("Data at %p isn't 0xaa (memory corruption occured)! Heap index for just now is above.\n", ptr);
		}
	}
}

void heaptest(void *data, uint32 length) {
	/**********************************
	 *** HEAP DEBUGGING AND TESTING ***
	 **********************************/

#define TEST_1_LOOPS 0
#define TEST_2_LOOPS 10

	uint32 start_time = gettickcount();

	print_heap_index();

	void *a = kmalloc(8);
	void *b = kmalloc(8);
	void *c = kmalloc(8);

	assert(IS_DWORD_ALIGNED(a));
	assert(IS_DWORD_ALIGNED(b));
	assert(IS_DWORD_ALIGNED(c));

	memset(a, 0xab, 6);
	memcpy(b, a, 6);

	printk("\na: %p", a);
	printk(", b: %p\n", b);
	printk("c: %p\n", c);

	printk("\n");
	//print_heap_index();

	printk("Freeing c...\n");
	kfree(c);
	//print_heap_index();
	printk("Freeing a...\n");
	kfree(a);
	//print_heap_index();
	printk("Freeing b...\n");
	kfree(b);
	//print_heap_index();

	printk("Testing page alignment...");

	void *initial = kmalloc(15); /* to make sure the allocations aren't aligned by chance */
	assert(IS_DWORD_ALIGNED(initial));
	printk("Initial: %p\n", initial);
//	print_heap_index();
	void *unaligned = kmalloc(14);
	printk("Unaligned: %p\n", unaligned);
	assert(IS_DWORD_ALIGNED(unaligned));
//	print_heap_index();
	void *aligned = kmalloc_a(16);
	assert(IS_PAGE_ALIGNED(aligned));

	printk("Aligned: %p\n", aligned);
//	print_heap_index();

	printk("Freeing them all...\n");
	kfree(initial);
	kfree(unaligned);
	kfree(aligned);
//	print_heap_index();

	/* The highest address allocated in the stress tests; stored for testing purposes, of course */
	void *max_alloc = NULL;

	/* account for the overhead and a tiny bit more */
	uint32 free_ = pmm_bytes_free();
	free_ -= ((free_/1024/128) + 24) * (sizeof(area_header_t) + sizeof(area_footer_t));

	/* This MUST be const and MUST NOT change (especially not being INCREASED in size) later!
	 * If this is increased, accessing p[] outside of NUM-1 will cause invalid memory accesses (duh!) */
	const uint32 NUM = ((free_ / 1024 / 128) - 8); /* we allocate 128k at a time; reduce slighly to not run out of frames */

	/* Make sure it didn't overflow or anything... not exactly a rigorous test; whatever */
	assert (NUM > 0 && NUM < 0xffffff00);

	void **p = kmalloc(sizeof(void *) * NUM);
	memset(p, 0, sizeof(void *) * NUM);

	/* store the alloc'ed size of all areas; we can't read the header safely, because alloc() may resize the area AFTER allocation!
	 * this would make verification of contents complain in error, because the NEW space wouldn't be filled with test bytes! */
	uint32 *alloced_size = kmalloc(sizeof(uint32) * NUM);
	memset(alloced_size, 0, sizeof(uint32) * NUM);

	uint32 total = 0;
	for (int x=0; x < TEST_1_LOOPS; x++) {
		total = 0;

		for (uint32 i = 0; i < NUM; i++) {
			uint32 sz = RAND_RANGE(4, 65535);
			p[i] = kmalloc(sz);
			assert(IS_DWORD_ALIGNED(p[i]));
			if (p[i] > max_alloc) max_alloc = p[i];
			total += sz;
			printk("alloc #%d (%d bytes, data block starts at %p)\n", i, sz, p[i]);

			validate_heap_index(false);
			//print_heap_index();
		}
		printk("%d allocs done, in total %d bytes (%d kiB)\n", NUM, total, total/1024);

		/* Free one in "the middle" */
		if (NUM > 100) {
			kfree((void *)p[100]);
			p[100] = NULL;
		}

		validate_heap_index(false);

		for (uint32 i = 0; i < NUM; i++) {
			kfree((void *)p[i]);
			printk("just freed block %d (header at 0x%p)\n", i, (uint32)p[i] - sizeof(area_header_t));
			p[i] = 0;
			validate_heap_index(false);
			//print_heap_index();
		}
		printk("%d frees done\n", NUM);

	}
	validate_heap_index(false);
//	print_heap_index();

/****************************
  *** STRESS TEST PART II ***
  ***************************/

	uint32 num_allocs = 0; /* just a stats variable, to print later */
	uint32 bytes_allocated = 0;
	uint32 mbytes_allocated = 0;
	uint32 mem_in_use = 0;

	srand(1234567);
	printk("Running the large stress test\n");

	for(int outer=1; outer <= TEST_2_LOOPS; outer++) {
		printk("\nloop %d/%d\n", outer, TEST_2_LOOPS);

		/*
		 * This code is a damn mess, but i won't bother making this easily readable,
		 * as it's temporary.
		 * It randomizes whether it should allocate or free, and whether the allocations
		 * should be page-aligned or not.
		 * It then fills its allocated space with a bit pattern, and ensures that
		 * it doesn't get overwritten.
		 */

		for (uint32 i=0; i < NUM; i++) {
			assert(p[i] == NULL);
			assert(alloced_size[i] == 0);
		}

		mem_in_use = 0;
		for (uint32 i = 0; i < NUM; i++) {
			//printk("inner, i = %u (out of %u)\n", i, NUM);

			if (num_allocs % 50 == 0)
				printk(".");
	//		print_heap_index();
			uint32 r = RAND_RANGE(1,10);
			if (r >= 6) {
				//printk("r>=6, allocating\n");
				uint32 r2 =RAND_RANGE(0,NUM-1);
				if (p[r2] != NULL) {
					/* This area was used already; don't overwrite it, or we can't free it! */
					//printk("p[r2=%u] = %p, skipping\n", r2, p[r2]);
					continue;
				}
				uint32 r3 = RAND_RANGE(8,3268); // bytes to allocate
				if (r3 > 3200)
						r3 *= 200; /* test */

				uint8 align = RAND_RANGE(1, 10); /* align ~1/10 allocs */
				//printk("alloc %d bytes%s", r3, align == 1 ? ", page aligned" : "");
				if (align == 1) {
					p[r2] = kmalloc_a(r3);
					assert(IS_PAGE_ALIGNED(p[r2]));
				}
				else {
					p[r2] = kmalloc(r3);
					assert(IS_DWORD_ALIGNED(p[r2]));
				}
				//printk(" at 0x%p\n", p[r2]);

				/* store the alloc size */
				assert(r3 > 0);
				alloced_size[r2] = r3;

				num_allocs++;
				bytes_allocated += r3;
				if (bytes_allocated >= 1024*1024) {
					mbytes_allocated += bytes_allocated/(1024*1024);
					bytes_allocated %= 1024*1024;
				}

				assert(r3 != 0 && alloced_size[r2] != 0);

				/* fill the allocation with a bit pattern, to ensure that nothing overwrites it */
				//printk("memsetting p[r2]=%p to p[r2]+r3=%p to 0xaa; p itself is %p\n", p[r2], (char*)p[r2] + r3, p);
				memset(p[r2], 0xaa, r3);

				if (p[r2] > max_alloc) max_alloc = p[r2];

				mem_in_use += r3 + sizeof(area_header_t) + sizeof(area_footer_t);
				//printk("mem in use: %d bytes (after alloc)\n", mem_in_use);
			}
			else {
				//printk("r < 6, freeing (if possible)\n");
				uint32 r2 = RAND_RANGE(0, NUM-1);
				if (p[r2] != NULL) {
					//printk("p[r2] != NULL ( p[r2=%u] = %p, alloced_size[r2] = %u, trying free\n", r2, p[r2], alloced_size[r2]);
					mem_in_use -= alloced_size[r2];

					assert(alloced_size[r2] != 0);
					verify_area(p[r2], alloced_size[r2]);
					//printk("freeing memory at %p (%d bytes)\n", p[r2], alloced_size[r2]);
					//printk("mem in use: %d bytes (after free)\n", mem_in_use);
					alloced_size[r2] = 0;
					kfree((void *)p[r2]);
					p[r2] = 0;
				}
			}
			validate_heap_index(false);
		} // end inner loop

		// Clean up
		for (uint32 i = 0; i < NUM; i++) {
			if (p[i] != NULL) {
				assert(alloced_size[i] != 0);
				mem_in_use -= alloced_size[i];
				//printk("mem in use: %d bytes (after free)\n", mem_in_use);
				verify_area(p[i], alloced_size[i]);
				kfree((void *)p[i]);
				p[i] = 0;
				alloced_size[i] = 0;
			}
		}
		validate_heap_index(false);
	} // end outer loop

	kfree(p);
	kfree(alloced_size);

	printk("\n");
	//print_heap_index();
	printk("ALL DONE! max_alloc = %p; total number of allocations: %d\n", max_alloc, num_allocs);
	printk("Total allocated: (approx.) %u MiB\n", mbytes_allocated);
	printk("heaptest took %u ticks\n", gettickcount() - start_time);

#if 0
	printk("Benchmarking...\n");
	void **mem = kmalloc(3250 * sizeof(void *));
	memset(mem, 0, sizeof(void *) * 3250);
	/* alloc BEFORE the benchmark! */
	uint32 start_ticks = gettickcount();
	for (int i = 0; i < 3250; i++) {
		mem[i] = kmalloc(16);
	}
	for (int i = 0; i < 3250; i++) {
		kfree(mem[i]);
		mem[i] = NULL;
	}
	uint32 end_ticks = gettickcount();
	kfree(mem);
	mem = 0;
	printk("Time taken: %d ticks (%d ms)\n", end_ticks - start_ticks, (end_ticks - start_ticks) * 10);
#endif
}
