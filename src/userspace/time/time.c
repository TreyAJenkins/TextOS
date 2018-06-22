#include <stdio.h>
#include <time.h>

int dropbox(char* method, char* data) {
  int a;
  asm volatile("int $0x80" : "=a" (a) : "0" (32), "b" ((int)method), "c" ((int)data));
  return a;
}

int setTZ(char* offset) {
    int status = dropbox("SetTimeOffset", offset);
    if (status == 0) {
        printf("Success, returned code %i\n", status);
    } else if (status == 2) {
        printf("Invalid method, returned code %i\n", status);
    } else {
        printf("Failed, returned code %i\n", status);
    }
}

int main(int argc, char const *argv[]) {
    if (argc > 1) {
        setTZ(argv[1]);
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char s[64];
    strftime(s, sizeof(s), "%c", tm);
    printf("%s\n", s);

	return 0;
}
