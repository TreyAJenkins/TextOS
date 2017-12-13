
#include <kernel/console.h>
#include <kernel/time.h>
#include <cpuid.h>
#include <string.h>
#include <stdint.h>

char CPUVendor[13];
char CPUName[17];
double CPUSpeed;

void GetCPUVendor(void) {
  int eax, ebx, ecx, edx;
  __cpuid (0 /* vendor string */, eax, ebx, ecx, edx);
  //printk ("EAX: %x\nEBX: %x\nECX: %x\nEDX: %x\n", eax, ebx, ecx, edx);
  int j;
  CPUVendor[12] = '\0';
  for(j = 0; j < 4; j++) {
      CPUVendor[j] = ebx >> (8 * j);
      CPUVendor[j + 4] = edx >> (8 * j);
      CPUVendor[j + 8] = ecx >> (8 * j);
  }
  //printk("CPU Vendor: %s\n", string);
}

void GetCPUName(void) {
    unsigned long extended, eax, ebx, ecx, edx, unused;
    __cpuid(0x80000000, extended, unused, unused, unused);
    CPUName[16] = '\0';
    if(extended >= 0x80000002) {
        unsigned int k;
        for(k = 0x80000002; k <= 0x80000002; k++) {
            __cpuid(k, eax, ebx, ecx, edx);
            //printk ("EAX: %x\nEBX: %x\nECX: %x\nEDX: %x\n", eax, ebx, ecx, edx);

            int l;
            for(l = 0; l < 4; l++) {
                CPUName[l] = eax >> (8 * l);
                CPUName[l + 4] = ebx >> (8 * l);
                CPUName[l + 8] = ecx >> (8 * l);
                CPUName[l + 12] = edx >> (8 * l);
                //printk("CPU i%i: %s\n", l, CPUName);
            }
        }
    }
}

bool isVM(void) {
    if (strstr(CPUName, "QEMU") != NULL) {
        return true;
    } else if (strstr(CPUName, "VM") != NULL) {
        return true;
    } else {
        return false;
    }
}

void GetCPUSpeed(void) {
    int xor = 0;
    int start = kern_time();
    double iterations;
    while (start >= kern_time()) {
        iterations++;
        xor = xor^xor;
    }
    double opps = (iterations);
    printk("Start: %i | End: %i | Iterations: %d | OPS: %d\n", start, kern_time(), iterations, opps);
    CPUSpeed = opps;
}
