include ../../Makefile.cfg

CC	= $(HOSTCC)
CFLAGS	= $(HOSTCFLAGS) -DDREAMCAST_IP=\"$(DREAMCAST_IP)\" -DHAVE_GETOPT

ifdef WITH_BFD
	CFLAGS	+= -DWITH_BFD
	LDFLAGS	= $(HOSTLDFLAGS) $(BFDLIB)
	INCLUDE	= $(BFDINCLUDE)
else
	LDFLAGS	= $(HOSTLDFLAGS) $(ELFLIB) -lelf
	INCLUDE	= $(ELFINCLUDE)
endif

DCTOOL	= dc-tool$(EXECUTABLEEXTENSION)

OBJECTS	= dc-tool.o syscalls.o unlink.o

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ -c $< 

all: $(DCTOOL)

$(DCTOOL): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

.PHONY : install
install: $(DCTOOL)
	-mkdir $(TOOLINSTALLDIR)
	cp $(DCTOOL) $(TOOLINSTALLDIR)

.PHONY : clean
clean:
	rm -f $(OBJECTS)

.PHONY : distclean
distclean: clean 
	rm -f $(DCTOOL)
