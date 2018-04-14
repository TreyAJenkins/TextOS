#include <sys/types.h>
#include <inttypes.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernel/kernutil.h> /* inb, inw, outw */
#include <kernel/console.h> /* printing, scrolling etc. */
#include <stdio.h>
#include <kernel/timer.h>
#include <kernel/time.h>
#include <kernel/initrd.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/ata.h>
#include <kernel/partition.h>
#include <kernel/fat.h>
#include <kernel/ext2.h>
#include <kernel/pci.h>
#include <kernel/serial.h>
#include <kernel/elf.h>

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
		if (key == "KEYMAP") {
			return "/etc/drivers/keyboard/EN_US.kmp"; // Default Keymap
		}
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

char method[64] = {0};
char data[1024] = {0};
int flagup = 0;
int react = 0;
int callerid;
char callername[64];

void authenticate( void jmp(char), char* message) {
    console_t *old = current_console;

    console_t *virt = virtual_consoles[4];
    console_switch(virt);


    //char buf[80] = {0};
    //sprintf(buf, "TSA: %s [%i] -> %s\n", callername, callerid, message);

    printk("Allow \"%s\" (%i) to call '%s' in kernel?\n", callername, callerid, message);
    printk("[y/N] > ");

    unsigned char ch = getchar();
    if (ch == 'y' || ch == 'Y') {
        react = 1;
        jmp(data);
    } else {
        react = 2;
    }

    //msgboxc(RED, RED, WHITE, "SAMPLE MESSAGE");
    ///sleep(5000);
    //msgboxb(RED, BLACK, 1000, 5, BLACK, WHITE, trim(buf));

    //sleep(5000);

    console_switch(old);

    //jmp();
}

int dropbox(char* nmethod, char* ndata) {
    if (flagup) {
        return 1;
    }
    //printk("%s\n", nmethod);
    sprintf(method, "%s", nmethod);
    sprintf(data, "%s", ndata);
    //printk("%s\n", method);
    //strncpy(method, nmethod, 64);
    //strncpy(data, ndata, 1024);
    task_t *curtk = current_task;
    callerid = curtk->id;
    strcpy(callername, curtk->name);
    flagup = 1;

    while (react == 0) {
        sleep(500);
    }

    int reaction = react-1;
    react = 0;

    return reaction;
}

void TSA(void) {
	printk("\n\n");
	//printc(RED, WHITE, "                       TEXTOS USERSPACE ABSTRACTION LAYER                       \n\n");


	//loadKeymap("/etc/drivers/keyboard/EN_US.kbd", 1);
	char* kmp = ReadConfig("KEYMAP");
	printk("Loading Keymap: %s\n", kmp);
	loadKeymap(kmp, 0);


    while (true) {
        if (flagup) {
            printk("DATA RECEIVED [%s] FROM %s (%i)\n", method, callername, callerid);
            if (strcmp(trim(method), "PANIC") == 0) {
                //printk("PANIC!!");
                authenticate(panic, "PANIC");
                //panic("TSA TEST");
                //msgbox(RED, WHITE, BLACK, "TEXTOS TSA");
            } else if (strcmp(trim(method), "PRINTK") == 0) {
                printk("%s\n", data);
                react = 1;
            }
            flagup = 0;
        } else {
            sleep(500);
        }
    }
	return 0;
}
