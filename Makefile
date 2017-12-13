TARGET = build

WARNINGS := -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
				-Wwrite-strings -Wredundant-decls -Winline \
				-Wuninitialized -Wstrict-prototypes \
				-Wno-unused-parameter -Wno-cast-align -Wno-unused-function -Wno-unused-variable -Wstack-protector

#DISABLED WARNINGS: -Wnested-externs -Werror

PREFIX = /usr/local/cross
GCCINC = $(PREFIX)/lib/gcc/i586-pc-TextOS/4.9.2/include
TOOLCHAININC = $(PREFIX)/i586-pc-TextOS/include

BUILDID = $(shell tar -cf - src | md5sum | awk '{print toupper($$1)}')
#BUILDID = "12ABCDEF"

CC = ccache i586-pc-TextOS-gcc
CFLAGS := -O0 -fstack-protector-all -nostdlib -nostdinc -I./src/include -lc -MMD -MP -I$(GCCINC) -I$(TOOLCHAININC) -std=gnu99 -march=i586 $(WARNINGS) -ggdb3 -D__DYNAMIC_REENT__ -D_TEXTOS_KERNEL -fdiagnostics-color=always -DBUILDID="$(BUILDID)"
LD = i586-pc-TextOS-ld
NATIVECC = gcc # Compiler for the HOST OS, e.g. Linux, Mac OS X

PROJDIRS := src/kernel src/include src/lib
SRCFILES := $(shell find $(PROJDIRS) -type f -name '*.c')
HDRFILES := $(shell find $(PROJDIRS) -type f -name '*.h')
ASMFILES := $(shell find $(PROJDIRS) -type f -name '*.s')
OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))
OBJFILES += $(patsubst %.s,%.o,$(ASMFILES))
#DRVFILES := $(shell find src/Drivers -type f -name '*.o')

USERSPACEPROG := $(shell find src/userspace/ -maxdepth 3 -name 'Makefile' -exec dirname {} \;)
#DRIVERPROG := $(shell find src/Drivers -maxdepth 3 -name 'Makefile' -exec dirname {} \;)

DEPFILES    := $(patsubst %.c,%.d,$(SRCFILES))

# All files to end up in a distribution tarball
ALLFILES := $(SRCFILES) $(HDRFILES) $(AUXFILES) $(ASMFILES)

#QEMU := /usr/local/bin/qemu-system-i386
QEMU := qemu-system-i386

# Make sure this is blank if the host OS is not Linux / KVM is not supported
#KVM := -machine accel=kvm
KVM :=


all: $(OBJFILES)
	echo "GENERATING BUILD: ${BUILDID}"
	@set -e; if [ ! -d "initrd/bin" ]; then \
		mkdir -p initrd/bin initrd/etc; \
	fi
	#for prog in $(DRIVERPROG); do \
	#	make -C $$prog; \
	#done
	@$(LD) -T linker-kernel.ld -o kernel.bin ${OBJFILES}
	#@strip kernel.bin
	@cp kernel.bin isofiles/boot
	@set -e; for prog in $(USERSPACEPROG); do \
		make -C $$prog; \
	done
	@set -e; if [ ! -f "initrd/bin/lua" ]; then \
		cd contrib && bash lua.sh ; cd ..; \
	fi
#	@/opt/local/bin/ctags -R *
	@if [ -f "initrd/bin/eshell" ]; then \
		mv initrd/bin/eshell initrd/bin/sh; \
	fi
	@python2 misc/create_initrd.py > /dev/null # let stderr through!
	@mkisofs -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles 2>&1 | grep -vP 'GNU xorriso|^\s*$$' || true
	-@rm -f serial-output



clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) kernel.bin bootable.iso misc/initrd.img)
	@for prog in $(USERSPACEPROG); do \
		make -C $$prog clean; \
		rm -f initrd/bin/`basename "$$prog"` initrd/bin/tests/`basename "$$prog"`; \
	done
	@rm -f initrd/bin/sh
	-@rm -f serial-output

-include $(DEPFILES)

todolist:
	-@for file in $(ALLFILES); do fgrep -H -e TODO -e FIXME $$file; done; true
	@cat TODO

%.o: %.c Makefile
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@ #-fno-builtin

%.o: %.s Makefile
	@nasm -o $@ $< -f elf -F dwarf -g

nofat: all
	$(QEMU) -cdrom bootable.iso -monitor stdio -s -serial file:serial-output -m 64

#net: all
	#@bash net-scripts/prepare.sh
	#@sudo $(QEMU) -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -serial file:serial-output -d cpu_reset -m 64 -net nic,vlan=0,macaddr=00:aa:00:18:6c:00,model=rtl8139 -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=no $(KVM)

netdebug:
	bash net-scripts/prepare.sh
	sudo $(QEMU) -cdrom bootable.iso -monitor stdio -s -S -serial stdout -d cpu_reset -m 64 -net nic,vlan=0,macaddr=00:aa:00:18:6c:00,model=rtl8139 -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=no $(KVM)

run: all
	#@sudo $(QEMU) -cdrom bootable.iso -hda ext2-1kb.img -monitor stdio -s -serial file:serial-output -d cpu_reset -m 64 -boot d $(KVM)
	qemu-system-x86_64 -cdrom bootable.iso

bochs: all
	-@bochs -f TextOS.bochs -q

debug: all
	#@sudo $(QEMU) -cdrom bootable.iso -hda ext2-1kb.img -monitor stdio -s -S -serial file:serial-output -d cpu_reset -m 64 -boot d $(KVM)
	@$(QEMU) -cdrom bootable.iso -monitor stdio -s -S -serial file:serial-output -d cpu_reset -m 64 -boot d $(KVM)
