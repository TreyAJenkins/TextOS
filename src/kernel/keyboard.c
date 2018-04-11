#include <sys/types.h>
#include <kernel/interrupts.h>
#include <kernel/kernutil.h> /* panic */
#include <kernel/console.h>
#include <kernel/heap.h>
#include <string.h>
#include <stdlib.h>
#include <kernel/vfs.h>

/* The different modifier keys we support */
#define MOD_NONE  0
#define MOD_CTRL  (1 << 0)
#define MOD_SHIFT (1 << 1)
#define MOD_ALT   (1 << 2)

/* The modifier keys currently pressed */
static unsigned char mod_keys = 0;

/* Set up the keyboard handler */
void init_keyboard(void) {
	register_interrupt_handler(IRQ1, keyboard_callback);
}

struct Keymap {
    unsigned char header[10];
    unsigned char name[6];
	unsigned char kbdmix[128];
	unsigned char kbdse_shift[128];
	unsigned char kbdse_alt[128];
};

/* A US keymap, courtesy of Bran's tutorial */
unsigned char kbdmix[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '+', /*'´' */0, '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '<',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '-',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,  '<',
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};

unsigned char kbdse_shift[128] = {
    0,  27, '!', '\"', '#', 0 /* shift+4 */, '%', '&', '/', '(',	/* 9 */
  ')', '=', '?', '`', '\b',	/* Backspace */
  '\t',			/* Tab */

 'Q', 'W', 'E', 'R',   /* 19 */
  'T', 'Y', 'U', 'I', 'O', 'P', 'A', 'A', '\n', /* Enter key */
    0,          /* 29   - Control */
  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'O', /* 39 */
 '\'', '>',   0,        /* Left shift */
 '*', 'Z', 'X', 'C', 'V', 'B', 'N',            /* 49 */
  'M', ';', ':', '_',   0,              /* Right shift */

  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   '>',
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};

unsigned char kbdse_alt[128] = {
    0,  27, 0 /*alt+1*/, '@', 0, '$', 0, 0, '{', '[',	/* 9 */
  ']', '}', '\\', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,  '|',
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};

int loadKeymap(char* path, int v) {
	struct Keymap* keymap = kmalloc(sizeof(struct Keymap));

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (v == 1)
			printk("[KBD] Invalid path to keymap!\n");
		kfree(keymap);
		return 1;
	}

	read(fd, keymap, sizeof(struct Keymap));
	close(fd);

	if (strncmp(keymap->header, "TEXTOSKMP", 9) == 0) {
		if (v == 1)
			printk("[KBD] Valid keymap file\n");
	} else {
		if (v == 1)
			printk("[KBD] INVAID KEYMAP FILE: %s\n", keymap->header);
		kfree(keymap);
		return 2;
	}

	if (v == 1)
		printk("[KBD] Keymap: %s\n", keymap->name);

	if (v == 1)
		printk("[KBD] Updating keymap ... ");

	//msgboxb(BLUE, RED, 250, 30, BLACK, RED, "UPDATING KEYMAP");

	for (size_t i = 0; i < 128; i++) {
		kbdmix[i] = keymap->kbdmix[i];
		kbdse_shift[i] = keymap->kbdse_shift[i];
		kbdse_alt[i] = keymap->kbdse_alt[i];
	}

	if (v == 1)
		printc(BLACK, GREEN, "[SUCCESS]\n");

	kfree(keymap);

	return 0;
}

uint32 keyboard_callback(uint32 esp) {
	/*
	 * Note: This code ignores escaped scancodes (0xe0 0x*) for now.
	 * After looking through a table of possibilities, none of them
	 * matter much! For instance, they are used to differ between
	 * left and right ctrl/alt, Keypad enter and return...
	 * Since there's no current support for arrow keys/the keypad,
	 * ignoring the 0xe0 byte means nothing bad.
	 */

	unsigned char scancode = inb(0x60);
	unsigned char c = 0;
	if (scancode == 0xe0)
		return esp; // For now

	if (mod_keys == (MOD_CTRL | MOD_ALT) && scancode == 0xd3) {
		// Ctrl+Alt+Delete!
		// I'm not sure about the proper keycode here.
		// 0xd3 is sent when Fn+backspace is released (0x53 on press).
		// There doesn't appear to BE a keycode sent on keydown with ctrl+alt+fn pressed.
		reset();
	}

	/* TODO: this is meant to be rather temporary. Perhaps implement proper hotkey hooks here? */
	if (mod_keys == MOD_ALT && (scancode >= 0x3b && scancode <= 0x3e)) {
		/* Alt + Fn (F1 through F4, inclusive) */
		/* Switch to virtual console 0 - 3 */

		console_t *virt = virtual_consoles[scancode - 0x3b];
		if (virt == NULL) {
			/* This console hasn't been set up yet! */
			return esp;
		}
		console_switch(virt);

		return esp;
	}
	if (mod_keys == MOD_ALT && scancode == 0x01) { // Switch to diagnostics console
		console_t *virt = virtual_consoles[4];
		if (virt == NULL) {
			return esp;
		}
		console_switch(virt);

		return esp;
	}

#if 0
	if (scancode & 0x80)
	//	printk("keyup  : 0x%02x\n", scancode & ~0x80);
		printk("keyup  : %i\n", scancode & ~0x80);
	//else
		//printk("keydown: 0x%02x\n", scancode);
#endif


	/*
	 * Check for modifier keycodes. If present, toggle their state (if necessary).
	 */
	switch (scancode) {
		case 0x2a: /* shift down */
		case 0x36: /* right shift down */
			mod_keys |= MOD_SHIFT;
			return esp;
			break;
		case 0xaa: /* shift up */
		case 0xb6: /* right shift up */
			mod_keys &= ~MOD_SHIFT;
			return esp;
			break;

		case 0x1d: /* ctrl down */
			mod_keys |= MOD_CTRL;
			return esp;
			break;
		case 0x9d: /* ctrl up */
			mod_keys &= ~MOD_CTRL;
			return esp;
			break;

		case 0x38: /* alt down */
			mod_keys |= MOD_ALT;
			return esp;
			break;
		case 0xb8: /* alt up */
			mod_keys &= ~MOD_ALT;
			return esp;
			break;

		default:
			break;
	}

	if (mod_keys == MOD_SHIFT && scancode == 0x48) {
		// Shift + arrow up
		scrollback_up();
		return esp;
	}
	else if (mod_keys == MOD_SHIFT && scancode == 0x50) {
		// Shift + arrow down
		scrollback_down();
		return esp;
	}
	else if (mod_keys == (MOD_SHIFT | MOD_ALT) && scancode == 0x48) {
		// Shift + alt + arrow up
		scrollback_pgup();
		return esp;
	}
	else if (mod_keys == (MOD_SHIFT | MOD_ALT) && scancode == 0x50) {
		// Shift + alt + arrow down
		scrollback_pgdown();
		return esp;
	}

	if (mod_keys == MOD_NONE && !(scancode & 0x80)) {
		// No modifiers
		c = kbdmix[scancode];
	}
	else if (mod_keys == MOD_SHIFT && !(scancode & 0x80)) {
		// Shift + key
		c = kbdse_shift[scancode];
	}
	else if (mod_keys == MOD_ALT && !(scancode & 0x80)) {
		// Alt + key
		c = kbdse_alt[scancode];
	}
	else if (mod_keys == MOD_CTRL && scancode == 0x20) {
		// Ctrl-D
		c = 4; // ASCII End of Transmission, good enough
	}
	else if ( !(scancode & 0x80) ) { // scancode isn't simply a supported key being released
		printk("Not implemented (scancode = 0x%02x)\n", scancode);
		return esp;
	}
	else if (scancode & 0x80) {
		// Key was released
		return esp;
	}

	/* Add the key to the current console's ring buffer */
	if (c == 0)
		return esp;

	assert(current_console != NULL);

	struct ringbuffer *keybuffer = (struct ringbuffer *)&current_console->keybuffer;
	assert(keybuffer != NULL);
	assert(keybuffer->write_ptr != NULL);

	if (keybuffer->counter == KEYBUFFER_SIZE)
		panic("Keyboard ring buffer full! This shouldn't happen without bugs somewhere...");

	*(keybuffer->write_ptr++) = c;
	keybuffer->counter++;

	/* Wrap the write pointer */
	if (keybuffer->write_ptr >= keybuffer->data + KEYBUFFER_SIZE)
		keybuffer->write_ptr = keybuffer->data;

	return esp;
}
