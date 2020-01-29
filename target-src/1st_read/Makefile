include ../../Makefile.cfg

TARGET = 1st_read.bin

all: rm-elf $(TARGET)

.PHONY : asm_only
asm_only:
	$(TARGETCC) $(TARGETCFLAGS) -Wa,-I../dcload -Wa,-adghlmns=loader.asm -c loader.s

.PHONY : clean
clean:
	-rm -f $(TARGET) loader.bin loader.elf loader.o *.asm *.map

.PHONY : distclean
distclean: clean

.PHONY : rm-elf
rm-elf:
	-rm -f $(TARGET) loader.elf

loader.elf: loader.s disable.s ../dcload/dcload.bin ../dcload/exception.bin
	$(TARGETCC) $(TARGETCFLAGS) -o $@ loader.s disable.s -nostartfiles \
	    -nostdlib -Ttext=0x8c010000 -Wl,-Map=loader.map -Wa,-I../dcload

loader.bin: loader.elf
	$(TARGETOBJCOPY) -O binary $^ $@

# 1st_read.bin
$(TARGET): loader.bin
	$(KOS_BASE)/utils/scramble/scramble $< $@
