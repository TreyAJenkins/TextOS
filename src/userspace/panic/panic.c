#include <stdio.h>

int dropbox(char* method, char* data)
{
  int a;
  asm volatile("int $0x80" : "=a" (a) : "0" (32), "b" ((int)method), "c" ((int)data));
  return a;
}


int main(int argc, char const *argv[]) {
	dropbox("PANIC", "");
	return 0;
}
