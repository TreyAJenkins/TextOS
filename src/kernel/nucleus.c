// TreyCorp Nucleus Security Suite
#include <kernel/kernutil.h>
#include <kernel/timer.h>
#include <kernel/backtrace.h>
#include <kernel/console.h>
#include <string.h>
#include <stdint.h>


long AuthToken = 118999;
bool OpenToken = false;
bool VFuse = false;

bool FuseBlown(void) {
    return VFuse;
}
int RequestToken(void) {
    if (OpenToken && !FuseBlown()) {
        OpenToken = false;
        return AuthToken;
    } else {
        return 0;
    }
}
bool ValidateToken(int token) {
    if (!FuseBlown()) {
        if (token == AuthToken) {
            return true;
        } else {
            VFuse = true; //This function should NEVER be called by anything other than the kernel, so blow the fuse
            return false;
        }
    }
    return false;
}


//Partition: Random Number Generator
unsigned long seed = 7253; //Default seed, this will be overridden on boot
unsigned long random_seed = TIMER_HZ; //This will eventually store the last generated random number

void SetSeed(int token, long pseed) {
    if (ValidateToken(token)) {
        seed = pseed;
    }
}

int crand(int max) {
	random_seed = random_seed+seed * 1103515245 +12345;
	return (unsigned int)(random_seed / 65536) % (max+1);
}
//End Partition

//Partition: Stack Smashing Protection
#if UINT32_MAX == UINTPTR_MAX
#define STACK_CHK_GUARD 0xe2dee396
#else
#define STACK_CHK_GUARD 0x595e9fbd94fda766
#endif
uintptr_t __stack_chk_guard = STACK_CHK_GUARD;

void __stack_chk_fail(void) {
    if (!OpenToken) {
        printk("\n-------------------------------------------\n");
        print_backtrace();
        panic("TreyCorp Nucleus: STACK SMASHING DETECTED!");
    } else {
        OpenToken = false;
    }
}
//End Partition

//Partition: Init
int GenerateToken(void) {
    if (!FuseBlown()) {
        AuthToken = crand(4000000);
    }
    return 0;
}
int InitSSP(void) {
    return 0;
}
//End Partition
