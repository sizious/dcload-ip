CDRECORD	= wodim dev=0,0,0 speed=8
DD		= dd
CP		= cp
MKISOFS		= genisoimage
CAT		= cat
TOUCH		= touch
1ST_READ	= ../target-src/1st_read/1st_read.bin

dctoolip: 1st_read.bin
	/opt/toolchains/dc/bin/mkdcdisc -n $@ -e $< -N -o $@.cdi -v 3 -m -d romdisk


1st_read.bin: $(1ST_READ)
	$(CP) $(1ST_READ) 1st_read.bin


$(1ST_READ):
	cd ../target-src && make


.PHONY : clean
clean:
	rm -f dctoolip.cdi 1st_read.bin

