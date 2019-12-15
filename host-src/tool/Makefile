include ../../Makefile.cfg

CC		= $(HOSTCC)
CFLAGS	= $(HOSTCFLAGS) -DDCLOAD_VERSION=\"$(VERSION)\" -DDREAMCAST_IP=\"$(DREAMCAST_IP)\" -DHAVE_GETOPT -DSAVE_MY_FANS=$(SAVE_MY_FANS)
LDFLAGS = $(HOSTLDFLAGS)
INCLUDE =

ifeq ($(WITH_BFD),1)
  CFLAGS	+= -DWITH_BFD
  LDFLAGS	= -L$(BFDLIB) -lbfd -liberty -lintl -lz
  INCLUDE	+= -I$(BFDINCLUDE)
else
  LDFLAGS	= -L$(ELFLIB) -lelf
  INCLUDE	+= -I$(ELFINCLUDE)
endif

# Additional libraries for MinGW/MSYS
ifdef MINGW32
  LDFLAGS 	+= -lws2_32 -lwsock32 -liconv
endif

ifdef STANDALONE_BINARY
  LDFLAGS 	+= -static
endif

DCTOOL	= dc-tool-ip$(EXECUTABLEEXTENSION)

OBJECTS	= dc-tool.o syscalls.o unlink.o utils.o

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ -c $<

all: $(DCTOOL)

$(DCTOOL): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

.PHONY : install
install: $(DCTOOL) | $(TOOLINSTALLDIR)
	cp $(DCTOOL) $(TOOLINSTALLDIR)
$(TOOLINSTALLDIR):
	-mkdir $(TOOLINSTALLDIR)

.PHONY : clean
clean:
	rm -f $(OBJECTS)

.PHONY : distclean
distclean: clean
	rm -f $(DCTOOL)
