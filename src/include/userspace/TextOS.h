#if _TEXTOS_USERSPACE

#include "types.h"

struct stat;

#define DECL_SYSCALL0(fn, ret) ret fn(void);
#define DECL_SYSCALL1(fn, ret, p1) ret fn(p1);
#define DECL_SYSCALL2(fn, ret, p1,p2) ret fn(p1,p2);
#define DECL_SYSCALL3(fn, ret, p1,p2,p3) ret fn(p1,p2,p3);
#define DECL_SYSCALL4(fn, ret, p1,p2,p3,p4) ret fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn, ret, p1,p2,p3,p4,p5) ret fn(p1,p2,p3,p4,p5);

#define DEFN_SYSCALL0(fn, ret, num) \
ret fn(void) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num)); \
  return a; \
}

#define DEFN_SYSCALL1(fn, ret, num, P1) \
ret fn(P1 p1) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1)); \
  return a; \
}

#define DEFN_SYSCALL2(fn, ret, num, P1, P2) \
ret fn(P1 p1, P2 p2) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2)); \
  return a; \
}

#define DEFN_SYSCALL3(fn, ret, num, P1, P2, P3) \
ret fn(P1 p1, P2 p2, P3 p3) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d"((ret)p3)); \
  return a; \
}

#define DEFN_SYSCALL4(fn, ret, num, P1, P2, P3, P4) \
ret fn(P1 p1, P2 p2, P3 p3, P4 p4) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d" ((ret)p3), "S" ((ret)p4)); \
  return a; \
}

#define DEFN_SYSCALL5(fn, ret, num) \
ret fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d" ((ret)p3), "S" ((ret)p4), "D" ((ret)p5)); \
  return a; \
}

DECL_SYSCALL0(_exit, int);
DECL_SYSCALL1(puts, int, const char *);
DECL_SYSCALL1(sleep, int, uint32);
DECL_SYSCALL0(getchar, int);
DECL_SYSCALL1(putchar, int, int);
DECL_SYSCALL2(open, int, const char *, int);
DECL_SYSCALL3(read, int, int, void *, int);
DECL_SYSCALL1(close, int, int);
DECL_SYSCALL1(malloc, void *, size_t);
DECL_SYSCALL1(free, int, void *);
DECL_SYSCALL2(stat, int, const char *, struct stat *);
DECL_SYSCALL1(chdir, int, const char *);
DECL_SYSCALL3(write, int, int, const void *, int);
typedef sint64 off_t;
DECL_SYSCALL3(lseek, off_t, int, off_t, int);
DECL_SYSCALL2(fstat, int, int, struct stat *);
DECL_SYSCALL0(getpid, int);
DECL_SYSCALL1(sbrk, void *, sint32);

// TODO: move these defines
#define	SEEK_SET 0
#define	SEEK_CUR 1
#define	SEEK_END 2

#endif // _TEXTOS_USERSPACE, the below is done in user AND kernel space

DEFN_SYSCALL0(_exit, int, 0);
DEFN_SYSCALL1(puts, int, 1, const char *);
DEFN_SYSCALL1(sleep, int, 2,uint32);
DEFN_SYSCALL0(getchar, int, 3);
DEFN_SYSCALL1(putchar, int, 4, int);
DEFN_SYSCALL2(open, int, 5, const char *, int);
DEFN_SYSCALL3(read, int, 6, int, void *, int);
DEFN_SYSCALL1(close, int, 7, int);
DEFN_SYSCALL1(malloc, void *, 8, size_t);
DEFN_SYSCALL1(free, int, 9, void *);
DEFN_SYSCALL2(stat, int, 10, const char *, struct stat *);
DEFN_SYSCALL1(chdir, int, 11, const char *);
DEFN_SYSCALL3(write, int, 12, int, const void *, int);
/* lseek is syscall 13! */
DEFN_SYSCALL2(fstat, int, 14, int, struct stat *);
DEFN_SYSCALL0(getpid, int, 15);
DEFN_SYSCALL1(sbrk, void *, 16, sint32);

#if _TEXTOS_USERSPACE

// We can't use DEFN_SYSCALL3 because of the 64-bit parameter and return
off_t lseek(int fd, off_t offset, int whence) {
	union {
		off_t o64;
		uint32 u32[2];
	} arg, ret;
	arg.o64 = offset;
	asm volatile("int $0x80" : "=a" (ret.u32[0]), "=d" (ret.u32[1]) : "0" (13), "b" (fd), "c" (arg.u32[0]), "d"(arg.u32[1]), "S"(whence));
	return ret.o64;
}

#endif
