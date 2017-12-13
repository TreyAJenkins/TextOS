#ifndef _KHOSTINFO_H
#define _KHOSTINFO_H

char CPUVendor[13];
char CPUName[17];

//bool CPU64(void);
void GetCPUVendor(void);
void GetCPUName(void);
bool isVM(void);
void GetCPUSpeed(void);

#endif
