#include <stdio.h>

int dropbox(char* method, char* data) {
  int a;
  asm volatile("int $0x80" : "=a" (a) : "0" (32), "b" ((int)method), "c" ((int)data));
  return a;
}


int main(int argc, char const *argv[]) {
    if (argc < 3) {
        printf("Usage: %s [METHOD] [DATA]\n", argv[0]);
        return 1;
    }
	int status = dropbox(argv[1], argv[2]);
    if (status == 0) {
        printf("Success, returned code %i\n", status);
    } else if (status == 2) {
        printf("Invalid method, returned code %i\n", status);
    } else {
        printf("Failed, returned code %i\n", status);
    }
	return 0;
}
