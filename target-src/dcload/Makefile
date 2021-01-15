include ../../Makefile.cfg

CC	= $(TARGETCC)
CFLAGS	= $(TARGETCFLAGS) -DDCLOAD_VERSION=\"$(VERSION)\" -DDREAMCAST_IP=\"$(DREAMCAST_IP)\" -DEXCEPTION_SECONDS=$(EXCEPTION_SECONDS) -Wall -Wextra -ffreestanding -fno-zero-initialized-in-bss -fno-common -fomit-frame-pointer -fno-strict-aliasing -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions -fno-delete-null-pointer-checks -fno-stack-protector -fno-stack-check -fno-merge-constants -fno-merge-all-constants -std=gnu11
INCLUDE	= -I../../target-inc

OBJCOPY	= $(TARGETOBJCOPY)

DCLOBJECTS	= dcload-crt0.o disable.o startup_support.o go.o video.o memcpy.o memcmp.o memfuncs.o packet.o net.o adapter.o rtl8139.o lan_adapter.o dhcp.o dcload.o perfctr.o cdfs_redir.o cdfs_syscalls.o syscalls.o maple.o commands.o
EXCOBJECTS	= exception.o

%.o : %.c
	$(CC) $(CFLAGS) $(INCLUDE) -Wa,-adghlmns=$*.asm -o $@ -c $<
%.o : %.s
	$(CC) $(CFLAGS) $(INCLUDE) -Wa,-adghlmns=$*.asm -o $@ -c $<
%.o : %.S
	$(CC) $(CFLAGS) $(INCLUDE) -Wa,-adghlmns=$*.asm -o $@ -c $<
%.bin: %
	$(OBJCOPY) -O binary $< $@

all: dcload.bin exception.bin

dcload: $(DCLOBJECTS)
	$(CC) $(CFLAGS) -Wl,--warn-common -Wl,--no-undefined -Wl,-Map,dcload.map -Wl,-Tdcload.x -nostartfiles -nostdlib -static -Wl,-z,now $^ -o $@ -lgcc

exception: $(EXCOBJECTS)
	$(CC) $(CFLAGS) -Wl,-Ttext=0x8c00f400 -Wl,-Map,exception.map -nostartfiles -nostdlib -static -Wl,-z,now $^ -o $@

.PHONY : clean
clean:
	rm -f $(DCLOBJECTS) $(EXCOBJECTS) dcload exception *.asm *.map

.PHONY : distclean
distclean: clean
	rm -f dcload.bin exception.bin
