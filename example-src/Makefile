include ../Makefile.cfg

CC	= $(TARGETCC)
INCLUDE	= -I../target-inc
CFLAGS	= $(TARGETCFLAGS) -Wall -Wextra -Wno-unused -ffreestanding -fno-zero-initialized-in-bss -fomit-frame-pointer -fno-strict-aliasing -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions -fno-delete-null-pointer-checks -fno-stack-protector -fno-stack-check -fmerge-constants -fmerge-all-constants -std=gnu11
OBJCOPY	= $(TARGETOBJCOPY)

OBJECTS	= crt0.o startup_support.o dcload-syscall.o dcload-syscalls.o memcpy.o

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -Wa,-adghlmns=$*.asm -o $@ -c $<

.S.o:
	$(CC) $(CFLAGS) $(INCLUDE) -Wa,-adghlmns=$*.asm -o $@ -c $<

.s.o:
	$(CC) $(CFLAGS) $(INCLUDE) -Wa,-adghlmns=$*.asm -o $@ -c $<

all: console-test.bin exception-test.bin gethostinfo.bin

console-test.bin: console-test
	$(OBJCOPY) -O binary $< $@

exception-test.bin: exception-test
	$(OBJCOPY) -O binary $< $@

gethostinfo.bin: gethostinfo
	$(OBJCOPY) -O binary $< $@

console-test: $(OBJECTS) console-test.o
	$(CC) $(CFLAGS) -Wl,--warn-common -Wl,--no-undefined -Wl,-Map,console-test.map -Wl,-Tdc$(TARGETCCVER).x -nostartfiles -nostdlib -static -Wl,-z,now $^ -o $@ -lgcc

gethostinfo: $(OBJECTS) gethostinfo.o
	$(CC) $(CFLAGS) -Wl,--warn-common -Wl,--no-undefined -Wl,-Map,gethostinfo.map -Wl,-Tdc$(TARGETCCVER).x -nostartfiles -nostdlib -static -Wl,-z,now $^ -o $@ -lgcc

exception-test: $(OBJECTS) exception-test.o
	$(CC) $(CFLAGS) -Wl,--warn-common -Wl,--no-undefined -Wl,-Map,exception-test.map -Wl,-Tdc$(TARGETCCVER).x -nostartfiles -nostdlib -static -Wl,-z,now $^ -o $@ -lgcc

.PHONY : clean
clean:
	rm -f *.o console-test exception-test gethostinfo *.map *.asm

.PHONY : distclean
distclean: clean
	rm -f *.bin
