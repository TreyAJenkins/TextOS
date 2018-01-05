WARNINGS := -Wextra -Wshadow -Wpointer-arith -Wcast-align -Wwrite-strings -Wredundant-decls -Wnested-externs -Winline -Wuninitialized -Wstrict-prototypes -Wno-unused-parameter -Wno-cast-align

CC = i586-pc-TextOS-gcc
CFLAGS := -O0 -std=gnu99 -march=i586 $(WARNINGS) -ggdb3 -static -D_TEXTOS
LD = i586-pc-TextOS-gcc
LDFLAGS := -lc

SRCFILES := $(shell find . -type f -name '*.c')
OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))
DEPFILES := $(patsubst %.c,%.d,$(SRCFILES))

OUTNAME := $(shell basename "`pwd`")

all: $(OBJFILES)
	@$(LD) $(LDFLAGS) -o $(OUTNAME) $(OBJFILES)
	@mv -f $(OUTNAME) ../../../initrd/bin

clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) ../../../initrd/bin/$(OUTNAME))

-include $(DEPFILES)

%.o: %.c Makefile
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
