#ifndef _KTSA_H
#define _KTSA_H

struct ConfigString {
	char key[16];
	char value[48];
};

struct ConfigRoot {
	unsigned char header[10];
	unsigned int values;
	struct ConfigString object[128];
};

const char* ReadConfig(char* key);
int dropbox(char* nmethod, char* ndata);
void TSA(void);

#endif
