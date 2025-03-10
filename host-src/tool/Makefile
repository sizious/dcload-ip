include ../../Makefile.cfg

CC		= $(HOSTCC)
CFLAGS	= $(HOSTCFLAGS) -DDCLOAD_VERSION=\"$(VERSION)\" -DDREAMCAST_IP=\"$(DREAMCAST_IP)\" -DHAVE_GETOPT -DSAVE_MY_ONLYFANS=$(SAVE_MY_ONLYFANS) -DDREAMCAST_BBA_RX_FIFO_DELAY_COUNT=$(DREAMCAST_BBA_RX_FIFO_DELAY_COUNT) -DDREAMCAST_BBA_RX_FIFO_DELAY_TIME=$(DREAMCAST_BBA_RX_FIFO_DELAY_TIME) -DDREAMCAST_LAN_RX_FIFO_DELAY_COUNT=$(DREAMCAST_LAN_RX_FIFO_DELAY_COUNT) -DDREAMCAST_LAN_RX_FIFO_DELAY_TIME=$(DREAMCAST_LAN_RX_FIFO_DELAY_TIME)
LDFLAGS = $(HOSTLDFLAGS)
INCLUDE =

# Adding static flag if asked
ifeq ($(STANDALONE_BINARY),1)
  LDFLAGS += -static
endif

# Determine if we need zlib or not in that context for building dc-tool
# The purpose of that is just to have '-lz' once at the end of the command line
ZLIB_REQUIRED := 0

# Linking with 'libelf' or 'libbfd' (sh-elf), depending of 'Makefile.cfg'
ifeq ($(WITH_BFD),1)
  # Starting from Binutils 2.40, SFrame is required when using BFD
  LIBSFRAME	=
  ifneq ("$(wildcard $(TARGETPREFIX)/lib/libsframe.a)","")
    LIBSFRAME = -lsframe
  endif
  
  CFLAGS	+= -DWITH_BFD
  LDFLAGS	+= -L$(BFDLIB) -lbfd $(LIBSFRAME) -liberty -lintl
  INCLUDE	+= -I$(BFDINCLUDE)
  
  ZLIB_REQUIRED := 1
else
  LDFLAGS	+= -L$(ELFLIB) -lelf
  INCLUDE	+= -I$(ELFINCLUDE)
endif

ifeq ($(MACOS),1)
	CFLAGS += -DMACOS
endif

# Additional libraries for MinGW/MSYS or MinGW-w64/MSYS2
ifdef MINGW32
  LDFLAGS	+= -lws2_32 -lwsock32 -liconv
  ZLIB_REQUIRED := 1
endif

# Add zlib to the command line end... if required
ifeq ($(ZLIB_REQUIRED),1)
  LDFLAGS	+= -lz
endif

DCTOOL	= dc-tool-ip$(EXECUTABLEEXTENSION)

OBJECTS	= dc-tool.o syscalls.o unlink.o utils.o shim.o

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
