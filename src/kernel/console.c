#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <kernel/console.h>
#include <kernel/kernutil.h>
#include <kernel/heap.h>
#include <kernel/list.h>
#include <kernel/timer.h>
#include <kernel/time.h>
#include <kernel/serial.h>

/* A character representing empty space on the screen */
static const uint16 blank = (0x7 << 8 /* grey on black */) | 0x20 /* space */;

// Pretend that the video RAM is 80*24, so that the first line is never
// accessed by anything but the status bar updater
static uint16 *videoram = (uint16 *) (0xb8000 + 2*80);

/* Used for double buffering when scrolling (due to a lack of memmove()) */
static uint16 *vram_buffer = NULL;

/* Used by the status bar ONLY */
static uint16 *real_vmem = (uint16 *)0xb8000;
static uint8 status_bgcolor = BLUE; /* not const, needs to be able to change */
static const uint8 status_fgcolor = WHITE;
static uint8 current_console_number = 0;
bool kernel_paniced = false;

/* TODO: MORE mutexes! */
//mutex_t *printk_mutex;

volatile console_t *current_console;

static void force_update_cursor(void);
//void redraw_screen(void);

/* A set of virtual consoles, accessed using Alt+F1, Alt+F2, ..., Alt+Fn */
#define NUM_VIRTUAL_CONSOLES 4
console_t *virtual_consoles[NUM_VIRTUAL_CONSOLES];
/* These are set up properly in kmain() */

#include <kernel/task.h>

/* I'm NOT happy about this mess, but it really should be set up statically...
   ... yet isn't; part is in kmain(), which dynamically allocates part of the
   kernel_console struct. */
extern list_t kernel_console_tasks;
static node_t tmp = {
	.next = NULL,
	.prev = NULL,
	.list = &kernel_console_tasks,
	.data = &kernel_task
};
list_t kernel_console_tasks = {
	.head = &tmp,
	.tail = &tmp,
	.count = 1,
	.mutex = NULL // set up ASAP in kmain
};
console_t kernel_console = {
	.tasks = &kernel_console_tasks,
	.active = true,
	.cursor = { .x = 0, .y = 0 },
};

/* Returns a key from the keyboard buffer, if possible. */
int getchar(void) {
	/* If no characters are available, loop until there's something. */
	assert(current_task->console != NULL);

	volatile struct ringbuffer *keybuffer = (volatile struct ringbuffer *) & current_task->console->keybuffer;
	while (keybuffer->counter == 0) {
		sleep(10);
	}

	assert(keybuffer->counter != 0);
	assert(keybuffer->read_ptr != NULL);
	unsigned char ret = *(keybuffer->read_ptr++);
	keybuffer->counter--;

	// Wrap the pointer if we've reached the end
	if (keybuffer->read_ptr >= keybuffer->data + KEYBUFFER_SIZE)
		keybuffer->read_ptr = keybuffer->data;

	if (ret == 4) // Used as EOF
		return -1;
	else
		return ret;
}

// NOTE: these only do half the work... they may well point outside of the buffer!
// This is fixed wherever they are used.
// cur_screen: the "current" 80x24 screen. Part (or all of it) may be undisplayed due to scrollback.
#define cur_screen(_con) ( (uint16 *)(_con->bufferptr + 80*24*(NUM_SCROLLBACK)) )
// cur_visible: what part of the screen is visible, period - possibly in scrollback (partially or fully)
#define cur_visible(_con) ( cur_screen(_con) - 80*(_con->current_position) )

// Switch the currently displayed console. Called when Alt+F1-F4 is pressed.
void console_switch(console_t *new) {
	assert(new != NULL);

	if (new == current_console)
		return;

	// Update the console number used in the status bar
	for (int i=0; i<NUM_VIRTUAL_CONSOLES; i++) {
		if (new == virtual_consoles[i]) {
			current_console_number = i;
			break;
		}
	}

	// Do the switch
	current_console->active = false;
	new->active = true;
	current_console = new;
	redraw_screen();
	force_update_cursor();
}

/* Creates a new console for the specified task */
console_t *console_create(void) {

	/* This function should only be used to set up the virtual consoles,
	 * at the moment! If the last of them is set up, well... */
	assert(virtual_consoles[NUM_VIRTUAL_CONSOLES - 1] == NULL);

	console_t *new = kmalloc(sizeof(console_t));
	memset(new, 0, sizeof(console_t));

	/* Set the new console up. This is done separately so that non-malloc'ed consoles
	 * can also be set up. */
	console_init(new);

	return new;
}

/* Initalize a console. Used during console creation; only called manually for static console_t's */
void console_init(console_t *new) {
	new->active = false;

	/* Set up the keyboard ring buffer for this console */
	new->keybuffer.read_ptr = new->keybuffer.data;
	new->keybuffer.write_ptr = new->keybuffer.data;
	new->keybuffer.counter = 0;

	/* Set up the tasks list. kmalloc should be available any time this is called */
	assert(new->tasks == NULL);
	new->tasks = list_create();

	/* Set up the scrollback buffer */
	new->buffer = kmalloc(CONSOLE_BUFFER_SIZE_BYTES);

	/* Copy the scrollback buffer content and cursor position from the currently displayed console */
	assert(current_console->buffer != NULL);
	memcpy(new->buffer, ((console_t *)current_console)->buffer, CONSOLE_BUFFER_SIZE_BYTES);
	new->bufferptr = new->buffer + (current_console->bufferptr - current_console->buffer);
	new->current_position = current_console->current_position;
	memcpy(& new->cursor, & ((console_t *)current_console)->cursor, sizeof(Point));

	/* Copy the color settings */
	new->text_color = current_console->text_color;
	new->back_color = current_console->back_color;
}

/* Destroy a console (free its memory, etc.) and switch to the previous one */
#if 0
void console_destroy(console_t *con) {
	assert(con->prev_console != NULL);
	console_t *prev = con->prev_console;

	/* Copy the new display back to the previous console */
	memcpy(prev->videoram, con->videoram, 80*25*2);
	memcpy(&prev->cursor, &con->cursor, sizeof(Point));

	if (current_console == con) {
		console_switch(con->prev_console);
	}

	kfree(con);
}
#endif

/* Syscall test function */
int puts(const char *s) {
	size_t len = strlen(s);

	for (size_t j = 0; j < len; j++) {
		putchar(s[j]);
	}

	update_cursor();

	return len;
}

/* Called by kmain() on boot. Creates the VRAM buffer (used in scroll() and maybe others). */
void init_video(void) {
	//printk_mutex = mutex_create();
	kernel_task.console = &kernel_console;
	current_console = &kernel_console;

	if (vram_buffer == NULL)
		vram_buffer = (uint16 *)kmalloc(80*24*2);

	clrscr();
}

void clrscr(void) {
	assert(console_task->console != NULL);

	// This takes care of the scrollback. Hacky, but simple.
	for (int i=0; i<23; i++)
		printk("\n");

	if (list_find_first(current_console->tasks, (void *)console_task) != NULL) {
		// If the task that's calling clrscr() has its console on display, also update the screen at once
		memsetw(vram_buffer, blank, 80*24);
		memsetw(videoram, blank, 80*24);
		update_statusbar();
	}

	Point *cursor = &console_task->console->cursor;
	cursor->x = 0;
	cursor->y = 0;
	update_cursor();
}

void cursor_left(void) {
	assert(console_task->console != NULL);
	Point *cursor = &console_task->console->cursor;
	if (cursor->x != 0)
		cursor->x--;

	update_cursor();
}

void set_text_color(int color) {
	assert(console_task->console != NULL);
	if (color >= 0 && color <= 15)
		console_task->console->text_color = color;
}

void set_back_color(int color) {
	assert(console_task->console != NULL);
	if (color >= 0 && color <= 15)
		console_task->console->back_color = color;
}

Point get_cursor(void) {
	assert(console_task->console != NULL);
	return console_task->console->cursor;
}

bool set_cursor(int x, int y) {
	assert(console_task->console != NULL);
	Point *cursor = &console_task->console->cursor;
	if (x < 0 || x > 79)
		return false;
	if (y < 0 || y > 23)
		return false;

	cursor->x = x;
	cursor->y = y;
	update_cursor();

	return true;
}

void cursor_right(void) {
	assert(console_task->console != NULL);
	Point *cursor = &console_task->console->cursor;
	if (cursor->x < 79)
		cursor->x++;
	else {
		cursor->x = 0;
		cursor->y++;
		scroll();
	}

	update_cursor();
}

// Move one line back in the scrollback buffer
void scrollback_up(void) {
	if (current_console->current_position >= MAX_SCROLLBACK) {
		return;
	}

	current_console->current_position++;
	redraw_screen();
	force_update_cursor();
}

// Move one screen back in the scrollback buffer
void scrollback_pgup(void) {
	if (current_console->current_position >= MAX_SCROLLBACK) {
		return;
	}
	else if (current_console->current_position + 24 >= MAX_SCROLLBACK) {
		current_console->current_position = MAX_SCROLLBACK;
	}
	else
		current_console->current_position += 24;

	redraw_screen();
	force_update_cursor();
}

// Move one line forward in the scrollback buffer
void scrollback_down(void) {
	if (current_console->current_position == 0) {
		return;
	}

	current_console->current_position--;
	redraw_screen();
	force_update_cursor();
}

// Move one screen forward in the scrollback buffer
void scrollback_pgdown(void) {
	if (current_console->current_position == 0) {
		return;
	}
	else if (current_console->current_position < 24) {
		current_console->current_position = 0;
	}
	else
		current_console->current_position -= 24;

	redraw_screen();
	force_update_cursor();
}

// Exit scrollback mode
void scrollback_reset(void) {
	current_console->current_position = 0;
	redraw_screen();
}

// Used by update_statusbar() to print text
static void puts_status(int x, const char *str) {
	size_t len = strlen(str);
	assert(x + len <= 80);
	for (size_t i = 0; i < len; i++) {
		//if(x + i == 79) break; // TODO: allow timer spinner
		real_vmem[x + i] = (status_bgcolor << BGCOLOR) | (status_fgcolor << FGCOLOR) | str[i];
	}
}

// Draw/update the status bar at the top of the screen
void update_statusbar(void) {
	if (kernel_paniced) {
		status_bgcolor = RED;
	}

	// Clear everything
	memsetw(real_vmem, (uint16)((status_bgcolor << BGCOLOR) | (status_fgcolor << FGCOLOR) | ' '), 80 - 1 /* TODO: allow timer spinner */);

	if (kernel_paniced) {
		puts_status(0, "  KERNEL PANIC    KERNEL PANIC    KERNEL PANIC    KERNEL PANIC    KERNEL PANIC  ");
		return;
	}

	puts_status(0, "[TextOS]");

	// Show the VC number
	char buf[32] = {0};
	sprintf(buf, "VC%u", current_console_number + 1); // convert to 1-indexed
	puts_status(12, buf);

	// Show scrollback status
	if (current_console->current_position != 0) {
		sprintf(buf, "Scrollback: %u line%c", current_console->current_position, (current_console->current_position > 1 ? 's' : 0));
		puts_status(16, buf);
	}

	// Show a clock
	Time t;
	get_time(&t);
	//t.hour; // defaulted at UTC, and there's no real TZ support in this OS!
	t.hour %= 24;
	sprintf(buf, "[%02d:%02d]", t.hour, t.minute);
	puts_status(73, buf);
}

// Copies the part of the screen that should be visible from the scrollback
// buffer to both the VRAM buffer and the actual video RAM
void redraw_screen(void) {
	// Find the pointer to the visible data, and wrap it if necessary (this is a ring buffer)
	uint16 *cur_vis = cur_visible(current_console);
	if (cur_vis >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
		cur_vis = current_console->buffer + (cur_vis - (current_console->buffer + CONSOLE_BUFFER_SIZE));
	}

	// More ring buffer stuff: if the entire screen isn't continuous in memory, copy
	// the two parts separately
	if (cur_vis + 80*24 >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
		// Copy part one
		uint32 copied = ((current_console->buffer + CONSOLE_BUFFER_SIZE) - cur_vis) * 2; // *2 to convert 16-bit chars to bytes
		memcpy(vram_buffer, cur_vis, copied);
		// Part two
		memcpy(((uint8 *)vram_buffer) + copied, current_console->buffer, 80*24*2 - copied);
	}
	else {
		// If it is continuous, just copy it.
		memcpy(vram_buffer, cur_vis, 80*24*2);
	}

	// In either case, update the screen with the new data
	memcpy(videoram, vram_buffer, 80*24*2);

	// Shouldn't be necessary (as the timer calls this), but up-to-date info is always nice anyway
	update_statusbar();
}

// Scroll the screen, if necessary. If not, this will just return.
void scroll(void) {
	console_t *con = console_task->console;
	if (con == NULL)
		con = &kernel_console;

	Point *cursor = &con->cursor;
	if (cursor->y <= 23)
		return;

	if (con->current_position != 0) {
		// We're in scrollback at the moment. Since a new line has shown up (or will, when this function is done),
		// we are now one position further back.
		con->current_position++;
		if (con->current_position > MAX_SCROLLBACK) {
			con->current_position = MAX_SCROLLBACK;
		}
	}

	// Move forward one line in the scrollback buffer, which causes the last line to "fall out"
	// Also, handle wrapping (this is a ring buffer)
	con->bufferptr += 80;
	if (con->bufferptr >= con->buffer + CONSOLE_BUFFER_SIZE) {
		con->bufferptr = con->buffer;
	}

	// Blank the last line on screen, handling the wrapping of the buffer if necessary
	if ((cur_screen(con) + 80*23 + 80 <= con->buffer + CONSOLE_BUFFER_SIZE)) {
		memsetw(cur_screen(con) + 80*23, blank, 80); // no wrap trouble, one line always fits
	}
	else {
		uint32 offset = (cur_screen(con) + 80*23 + 80) - (con->buffer + CONSOLE_BUFFER_SIZE);
		memsetw(con->buffer + offset, blank, 80);
	}

	redraw_screen();
	cursor->y = 23;
}

// Print a character to the current cursor location
int putchar(int c) {
	Point *cursor = NULL;

	console_t *con = console_task->console;
	if (con == NULL)
		con = &kernel_console;

	cursor = &con->cursor;
	assert(cursor != NULL);

	serial_send_byte(c);

	if (c == '\n') {
		// c == newline
		cursor->x = 0;
		cursor->y++;
	} else if (c == '\t') {
		printk("    ");
	} else if (c == 0x08) {
		// Backspace
		if (cursor->x > 0)
			cursor->x--;
		else {
			if (cursor->y > 0) {
				cursor->y--;
				cursor->x = 79;
			}
			// else: we can't do anything!
		}
	} else if (c >= 0x20) {
		// 0x20 is the lowest printable character (space)
		//assert(cursor->y <= 23 && cursor->x <= 79);
		if (cursor->y > 23)
			cursor->y = 23;
		if (cursor->x > 79)
			cursor->x = 79;
		const unsigned int offset = cursor->y*80 + cursor->x;
		uint16 color = (con->back_color << BGCOLOR) | (con->text_color << FGCOLOR);

		if (con != NULL) {
			// Find the MMIO address and take care of wrapping, as cur_screen() can point outside
			// the actual buffer. Ugly, yes.
			uint16 *addr = cur_screen(con) + offset;
			if (addr >= con->buffer + CONSOLE_BUFFER_SIZE) {
				addr = con->buffer + (addr - (con->buffer + CONSOLE_BUFFER_SIZE));
			}

			// Set it
			*addr = ((unsigned char)c) | color;
		}

		if (list_find_first(current_console->tasks, (void *)console_task) != NULL) {
			/* Also update the actual video ram if this console is currently displayed */
			if (con->current_position < 24 && (24UL - con->current_position) > cursor->y) {
				// In scrollback, but this line should still be on screen. < 24 because there's no chance it's on screen
				// if we're scrolled back a full screen or more. The rest checks whether the line is still on screen.
				uint32 sb_offset = 80*con->current_position;
				videoram[offset + sb_offset] = ((unsigned char)c) | color;
				vram_buffer[offset + sb_offset] = ((unsigned char)c) | color;
			}
		}

		if (cursor->x + 1 == 80) {
			// Wrap to the next line
			cursor->y++;
			cursor->x = 0;
		}
		else {
			// Don't wrap
			cursor->x++;
		}
	}

	scroll(); // Scroll down, if need be
//	update_cursor();

	return c;
}

void update_cursor(void) {
	// Moves the hardware cursor to the current position specified by the cursor struct
	if (list_find_first(current_console->tasks, (void *)console_task) == NULL) {
		/* The current task (console_task) isn't among this console's tasks.
		 * Don't update the cursor on screen now. */
		return;
	}

	//assert(console_task->console == current_console);

	Point *cursor = & ((console_t *)current_console)->cursor;
	assert(cursor != NULL);
	//assert(cursor->y <= 23);
	int y = cursor->y + current_console->current_position;
	uint16 loc = y * 80 + cursor->x + 80; /* + 80 due to the status bar */

	uint8 high = (uint8)((loc >> 8) & 0xff);
	uint8 low  = (uint8)(loc & 0xff);
	outb(0x3d4, 0xe);
	outb(0x3d5, high);
	outb(0x3d4, 0xf);
	outb(0x3d5, low);
}

/* Ugh, code duplication... */
static void force_update_cursor(void) {
	assert(current_console != NULL);

	Point *cursor = & ((console_t *)current_console)->cursor;
	assert(cursor != NULL);
	//assert(cursor->y <= 23);
	int y = cursor->y + current_console->current_position;
	uint16 loc = y * 80 + cursor->x + 80; /* + 80 due to the status bar */

	uint8 high = (uint8)((loc >> 8) & 0xff);
	uint8 low  = (uint8)(loc & 0xff);
	outb(0x3d4, 0xe);
	outb(0x3d5, high);
	outb(0x3d4, 0xf);
	outb(0x3d5, low);
}

/* The buffer used by printk */
char _printk_buf[1024];

size_t printc(int back_color, int text_color, const char *fmt, ...) {
	assert(console_task->console != NULL);
	int orig_text = console_task->console->text_color;
	int orig_back = console_task->console->back_color;

	//mutex_lock(printk_mutex);

	set_text_color(text_color);
	set_back_color(back_color);

	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(_printk_buf, fmt, args);
	va_end(args);

	if (i > 0) {
		size_t len = strlen(_printk_buf);
		for (size_t j = 0; j < len; j++) {
			putchar(_printk_buf[j]);
		}
	}
	update_cursor();

	set_text_color(orig_text);
	set_back_color(orig_back);

	//mutex_unlock(printk_mutex);

	return i;
}

size_t printk(const char *fmt, ...) {
	va_list args;
	int i;

	//mutex_lock(printk_mutex);

	va_start(args, fmt);
	i = vsprintf(_printk_buf, fmt, args);
	va_end(args);

	if (i > 0) {
		size_t len = strlen(_printk_buf);
		for (size_t j = 0; j < len; j++) {
			putchar(_printk_buf[j]);
		}
	}
	update_cursor();

	//mutex_unlock(printk_mutex);

	return i;
}

int sprintf(char *sprintf_buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(sprintf_buf,fmt,args);
	va_end(args);

	return i;
}

static void puts_manual(int x, int y, int bgcolor, int fgcolor, const char *str) {
	size_t len = strlen(str);
	assert(x + len <= 80);
	for (size_t i = 0; i < len; i++) {
		real_vmem[((y * 80) + x) + i] = (bgcolor << BGCOLOR) | (fgcolor << FGCOLOR) | str[i];
	}
}

void msgbox(int bgcolor, int fgcolor, int tcolor, const char* msg) {
	#define term_width 80 // The width of the terminal; default: 80
	#define term_height 23 // the height of the terminal; default: 23

	int msglength = strlen(msg);
	int msgstart = ((term_width / 2)) - ((msglength / 2));


	int boxwidth = msglength + 2;
	int boxstart = ((term_width / 2)) - ((boxwidth / 2));
	int boxend = ((term_width / 2)) + ((boxwidth / 2));


	int x, y;
	//Draw the box

	x = boxstart;
	y = (term_height / 2) - 0.5;

	for (int i = boxstart; i <= boxend; i++) { // Top row
		puts_manual(i, y, bgcolor, bgcolor, " ");
	}
	y++;
	puts_manual(x, y, bgcolor, bgcolor, " "); // Middle row
	for (int i = boxstart+1; i <= boxend-1; i++) {
		puts_manual(i, y, fgcolor, fgcolor, " ");
	}
	puts_manual(boxend, y, bgcolor, bgcolor, " ");
	y++;
	for (int i = boxstart; i <= boxend; i++) { // Bottom row
		puts_manual(i, y, bgcolor, bgcolor, " ");
	}

	//Insert the text
	x = msgstart;
	y = (term_height / 2);
	puts_manual(x, y, fgcolor, tcolor, msg);

}

void msgboxb(int bgcolor, int bgcolor2, int delay, int iter, int fgcolor, int tcolor, const char* msg) {
	#define term_width 80 // The width of the terminal; default: 80
	#define term_height 23 // the height of the terminal; default: 23

	int msglength = strlen(msg);
	int msgstart = ((term_width / 2)) - ((msglength / 2));


	int boxwidth = msglength + 2;
	int boxstart = ((term_width / 2)) - ((boxwidth / 2));
	int boxend = ((term_width / 2)) + ((boxwidth / 2));


	int x, y;
	//Draw the box

	x = boxstart;
	y = (term_height / 2) - 0.5;

	for (int i = boxstart; i <= boxend; i++) { // Top row
		puts_manual(i, y, bgcolor, bgcolor, " ");
	}
	y++;
	puts_manual(x, y, bgcolor, bgcolor, " "); // Middle row
	for (int i = boxstart+1; i <= boxend-1; i++) {
		puts_manual(i, y, fgcolor, fgcolor, " ");
	}
	puts_manual(boxend, y, bgcolor, bgcolor, " ");
	y++;
	for (int i = boxstart; i <= boxend; i++) { // Bottom row
		puts_manual(i, y, bgcolor, bgcolor, " ");
	}

	//Insert the text
	x = msgstart;
	y = (term_height / 2);
	puts_manual(x, y, fgcolor, tcolor, msg);


	//Flash the box
	for (int v = 0; v < iter; v++) {
		x = boxstart;
		y = (term_height / 2) - 0.5;

		for (int i = boxstart; i <= boxend; i++) { // Top row
			puts_manual(i, y, bgcolor, bgcolor, " ");
		}
		y++;
		puts_manual(x, y, bgcolor, bgcolor, " "); // Middle row
		puts_manual(boxend, y, bgcolor, bgcolor, " ");
		y++;
		for (int i = boxstart; i <= boxend; i++) { // Bottom row
			puts_manual(i, y, bgcolor, bgcolor, " ");
		}

		x = boxstart;
		y = (term_height / 2) - 0.5;
		sleep(delay / 2);

		for (int i = boxstart; i <= boxend; i++) { // Top row
			puts_manual(i, y, bgcolor2, bgcolor2, " ");
		}
		y++;
		puts_manual(x, y, bgcolor2, bgcolor2, " "); // Middle row
		puts_manual(boxend, y, bgcolor2, bgcolor2, " ");
		y++;
		for (int i = boxstart; i <= boxend; i++) { // Bottom row
			puts_manual(i, y, bgcolor2, bgcolor2, " ");
		}

		sleep(delay / 2);
	}
	redraw_screen();


}
