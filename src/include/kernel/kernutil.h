#ifndef _KERNUTIL_H
#define _KERNUTIL_H

#include <sys/types.h>

uint8  inb(uint16 port);
uint16 inw(uint16 port);
uint32 inl(uint16 port);

void outb(uint16 port, uint8 value);
void outw(uint16 port, uint16 value);
void outl(uint16 port, uint32 value);

void panic(const char *fmt, ...) __attribute__((noreturn));
void reset(void) __attribute__((noreturn));
void reboot(void) __attribute__((noreturn));

#define assert(b) ((b) ? (void)0 : panic_assert(__FILE__, __LINE__, #b))
extern void panic_assert(const char *file, uint32 line, const char *desc);

/* timer.c */
void timer_install(void);

void shutdown(void);
int halt(void);

#endif
