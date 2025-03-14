
dcloadip:
	cd ../target-src && make
	cp ../target-src/1st_read/loader.elf loader.elf
	/opt/toolchains/dc/bin/mkdcdisc -N -m -n $@ -e loader.elf -o $@.cdi 

.PHONY : clean
clean:
	rm -f dcloadip.cdi loader.elf
