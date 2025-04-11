
# dcload-ip 2.0.2

A Dreamcast ethernet loader originally by [Andrew Kieschnick](http://napalm-x.thegypsy.com/andrewk/dc/). This program is part of [KallistiOS](http://gamedev.allusion.net/softprj/kos/).

This **special version** has been updated/overhauled by **Moopthehedgehog** and is maintained by [Mickaël Cardoso](https://sizious.com/) and other contributors. This fork is a most avanced/experimental version over the regular [dcload-ip](https://github.com/kallistios/dcload-ip).

## Features

* Load  `elf`, `srec`, and `bin`
* PC I/O (read, write, etc to PC - compatible with original dcload)
* Exception handler
* Extremely fast
* Works on 100mbit
* Supports both the **Broadband Adapter** (HIT-0400) and **LAN Adapter** (HIT-0300) in a single binary
* DHCP support (use an IP address of 0.0.0.0 in `Makefile.cfg` to enable it)
* NTSC 480i, PAL 576i, and VGA display output modes supported
* Dumping exceptions over the network if the dcload console is enabled

## Building

You will need a modern toolchain, if you don't have one, you can build/obtain it using the `dc-chain` utility included in KallistiOS.

Edit `Makefile.cfg` for your system and network and then run `make`.

## Installation

1. PC - run `make install` (installs `dc-tool-ip`, by default in `/opt/toolchains/dc/bin`)
2. DC

 a. `cd make-cd`, edit `Makefile`, insert blank CD-R, run `make`. If
   `1st_read.bin` hasn't been built yet, this `Makefile` will build it  
 or  
 b. take `target-src/1st_read/1st_read.bin` and stuff it on a cd yourself
      (please use the `IP.BIN` from the `make-cd` directory if you are going
      to distribute either cds or cd images).

## On-screen display

* If you see the message `NO ETHERNET ADAPTER DETECTED!`, something has
  gone wrong. The background of the screen will be red.

* The correct display is something like:

```
  dcload-ip 2.0.2  <- name/version
  Broadband Adapter (HIT-0400)  <- adapter driver in use  
  00:d0:de:ad:be:ef  <- dc hardware address  
  192.168.1.92  <- dc ip address  
  idle...  <- status  
```

  The background of the screen will be blue (Broadband Adapter) or green (LAN Adapter).

* If the status line reports `link change...` and does not change back to
  `idle...` within a short period of time, you may have a cable problem.
  dcload-ip will not work while `link change...` is displayed, or before it is
  displayed the first time. The `link change...` message normally is seen when
  you start dcload-ip, when you execute `dc-tool -r`, and when you disconnect
  the ethernet cable.

* If an exception is caught while a loaded program is running, the screen
  will turn lighter blue and display the exception info for a time set by
  `EXCEPTION_SECONDS` in Makefile.cfg (default is 15 seconds). dcload-ip should
  be active again after that point. See the Exception Dumping section of this
  README for what happens if an exception occurs while the dc-tool console is used.

## Testing

1. `cd example-src`
2. `dc-tool -x console-test` (tests some PC I/O)
3. `dc-tool -x exception-test` (generates an exception)
4. `dc-tool -x gethostinfo` (displays the Dreamcast's ip, and the ip and port of
   the dc-tool host)

## KOS GDB-over-dcload

To run a GNU Debugger (GDB) session over the dcload connection:

1. Build/obtain a sh-elf targetted GNU debugger; you can make one using the `dc-chain` utility included in KallistiOS
2. Put a `gdb_init()` call somewhere in the startup area of your KOS-based program (usually, the `main()` function is a good candidate as it's the entry point for your program)
3. Build your program with the `-g` GCC switch to include debugging info (e.g., `kos-cc -g myprog.c`)
4. Launch your program using `dc-tool -g -x <prog.elf>`
5. Launch `sh-elf-gdb` and connect to the dc-tool using `target remote :2159` (always this port)
6. Squash bugs!

## Maple Passthrough

You can send packets to various maple devices attached to the Dreamcast by using
the MAPL command. Simply send a command packet to the Dreamcast that is formatted
as follows:

```
typedef struct __attribute__ ((packed)) {
	unsigned char id[4]; // MAPL
	unsigned int address; // Ignored, set to 0
	unsigned int size; // The length of Maple packet data (the data[] member)
	unsigned char data[];
} command_t;
```

Maple command data format:

- Maple Command (1 byte)  
- Maple Port # (1 byte)  
- Maple Slot # (1 byte)  
- Maple data in 4-byte increments (1 byte)  
- Any data to be sent with the command (multiple of 4 bytes)  

You will get a similarly formatted response in return.

## Performance Counter Control

Newly added is the ability to control Dreamcast/SH7091 performance counters over
the network. These were a previously hidden aspect of the Dreamcast's CPU, and
this program uses one of them to keep track of DHCP lease time across loaded
programs. There are two of them, and they are both 48-bit. See perfctr.h/.c for
details (they are from [DreamHAL](https://github.com/sega-dreamcast/dreamhal)).

Similarly to the Maple packets, the format of a performance counter control packet
is as follows:

```
typedef struct __attribute__ ((packed)) {
	unsigned char id[4]; // PMCR
	unsigned int address; // Ignored, set to 0
	unsigned int size; // Length of data[]. This field isn't actually checked, though...
	unsigned char data[];
} command_t;
```

For anyone making a program to take advantage of this functionality, the packet
payload data essentially just needs to conform to this struct, and "id" should
be "PMCR" (without null-termination).

Regarding the data field, it should be formatted according to the following information:

The 6 performance counter control functions are:
```
	// (I) Clear counter and enable
	void PMCR_Init(unsigned char which, unsigned char mode, unsigned char count_type);

	// (E) Enable one or both of these "undocumented" performance counters
	void PMCR_Enable(unsigned char which, unsigned char mode, unsigned char count_type, unsigned char reset_counter);

	// (B) Disable, clear, and re-enable with new mode (or same mode)
	void PMCR_Restart(unsigned char which, unsigned char mode, unsigned char count_type);

	// (R) Read a counter
	unsigned long long int PMCR_Read(unsigned char which);

	// (G) Get a counter's current configuration
	unsigned short PMCR_Get_Config(unsigned char which);

	// (S) Stop counter(s) (without clearing)
	void PMCR_Stop(unsigned char which);

	// (D) Disable counter(s) (without clearing)
	void PMCR_Disable(unsigned char which);
```
The command is the first letter of the function name (capitalized), followed by
each of the function's parameters. Note that restart's command letter is 'B'--read
is 'R', so restart is 'B' (think reBoot). The command letters are included in
parentheses for each function in the above comment block.

- Sending command data of 'D' 0x1 (2 bytes) disables ('D') perf counter 1 (0x1)   
- Sending command data 'E' 0x3 0x23 0x0 0x1 (5 bytes) enables ('E') both perf
counters (0x3) to elapsed time mode (0x23) where count is 1 cpu cycle = 1 count
(0x0) and continue the counter from its current value (0x1)
- Sending command data 'B' 0x2 0x23 0x1 (4 bytes) restarts ('B') perf counter 2
(0x2) to elapsed time mode (0x23) and count is CPU/bus ratio method (0x1)  
...  
etc.

Notes:
- Remember to disable before leaving dcload-ip to execute a program if dcload's
counter is needed by that program.
- See `perfctr.h` for how to calculate time using the CPU/bus ratio method, in
addition to the available counter modes (and for loads of other information)
- `PMCR_Init()` and `PMCR_Enable()` will do nothing if the perf counter is already running!

## Exception Dumping

Another new feature is the ability to send a full register dump to a host PC
running `dc-tool-ip` with the console enabled (i.e. not invoked with the `-n`
command line option).

In the event of an exception, dcload will print a full register dump on the
screen and to the dc-tool console. It will also make a file called
`dcload_exception_dump.bin` in the directory that the terminal is currently in.

The format of the dump binary is as follows:
```
// Exception struct
struct _exception_struct_t {
	unsigned char id[4]; // EXPT
	unsigned int expt_code; // Exception code
	unsigned int pc;
	unsigned int pr;
	unsigned int sr;
	unsigned int gbr;
	unsigned int vbr;
	unsigned int dbr;
	unsigned int mach;
	unsigned int macl;
	unsigned int r0b0;
	unsigned int r1b0;
	unsigned int r2b0;
	unsigned int r3b0;
	unsigned int r4b0;
	unsigned int r5b0;
	unsigned int r6b0;
	unsigned int r7b0;
	unsigned int r0b1;
	unsigned int r1b1;
	unsigned int r2b1;
	unsigned int r3b1;
	unsigned int r4b1;
	unsigned int r5b1;
	unsigned int r6b1;
	unsigned int r7b1;
	unsigned int r8;
	unsigned int r9;
	unsigned int r10;
	unsigned int r11;
	unsigned int r12;
	unsigned int r13;
	unsigned int r14;
	unsigned int r15;
	unsigned int fpscr;
	unsigned int fr0;
	unsigned int fr1;
	unsigned int fr2;
	unsigned int fr3;
	unsigned int fr4;
	unsigned int fr5;
	unsigned int fr6;
	unsigned int fr7;
	unsigned int fr8;
	unsigned int fr9;
	unsigned int fr10;
	unsigned int fr11;
	unsigned int fr12;
	unsigned int fr13;
	unsigned int fr14;
	unsigned int fr15;
	unsigned int fpul;
	unsigned int xf0;
	unsigned int xf1;
	unsigned int xf2;
	unsigned int xf3;
	unsigned int xf4;
	unsigned int xf5;
	unsigned int xf6;
	unsigned int xf7;
	unsigned int xf8;
	unsigned int xf9;
	unsigned int xf10;
	unsigned int xf11;
	unsigned int xf12;
	unsigned int xf13;
	unsigned int xf14;
	unsigned int xf15;
} __attribute__ ((__packed__));
```

## Notes

* You can use `arp` instead of setting the Dreamcast's IP in `Makefile.cfg`.
  On Windows, you may use the `netsh` command which is more reliable (e.g. `netsh
  interface ip add neighbors "Ethernet" 192.168.10.1 AA-BB-CC-DD-EE-FF)`. In that
  case, don't forget to specify an IP address in the Ethernet card of your computer.
  Please set the Dreamcast's IP in `Makefile.cfg` to be in the range `169.254.xxx.xxx`
  when using this method, as `0.0.0.0` is used by the DHCP protocol for network
  discovery purposes (actually the entire `0.x.x.x` range is).
* Tested systems: Debian GNU/Linux 2.2-3.0, Cygwin, MinGW,
[DreamSDK](https://www.dreamsdk.org), Windows Subsystem for Linux v2 (Debian)
* Many, many bugs have been squashed and much of the code overhauled
* Patches and improvements are welcome; please raise an issue here in the "Issues"
section for that

## Credits

* RTL8139 code based on code by Megan Potter
* LAN Adapter driver code, originally derived from early KOS, majorly overhauled by Moopthehedgehog
* DHCP support, exception dumping, perf counters, performance improvements by Moopthehedgehog
* DHCP retry functionality by Eric Fradella (darcagn)
* There are some various files from `newlib-1.8.2` here
* Some critical files (`video.s`, `maple.c` and `maple.h`) were written by Marcus Comstedt
* Initial win32 porting and implementation of `-t` by Florian 'Proff' Schulze
* Win32 bugfix by The Gypsy
* Fixes for Cygwin by Florian 'Proff' Schulze
* Lot of various fixes by Mickaël Cardoso (SiZiOUS)
* RX config bug pointed out by Slant
* Horridly buggy nature of 1.0.1 + 1.0.2 pointed out by Megan Potter
* Fixes for `libbfd` segfaults by Atani
* Inspiration for `MAPL` packet by Tim Hentenaar
* Contributions by Harley Laue, Sam Steele, Christian Groessler, Protofall, Thomas Sowell,
  Luke Benstead, [Matt Phillips](https://github.com/BigEvilCorporation), Andy Barajas,
  snickerbockers, maddiebaka, Paul Cercueil, Daniel Fairchild
* Major contributions by KallistiOS team: Lawrence Sebald, Donald Haase, Falco Girgis
