
# dcload-ip 1.1.1 (with DHCP)

A Dreamcast ethernet loader originally by [Andrew Kieschnick](http://napalm-x.thegypsy.com/andrewk/dc/).

### Features

* Load  `elf`, `srec`, and `bin`
* PC I/O (read, write, etc to PC - compatible with original dcload)
* Exception handler
* Extremely fast (at least for me - I get 1130Kbyte/sec on 10mbit half-duplex)
* Now works on 100mbit (I get 2580Kbyte/sec on 100mbit half-duplex)
* Supports both the **Broadband Adapter** (HIT-0400) and **LAN Adapter** (HIT-0300)
  in a single binary
- DHCP support (use an IP address of 000.000.000.000 in `Makefile.cfg` to enable it)
- NTSC 480i, PAL 576i, and VGA display output modes supported

### Building

1. Edit `Makefile.cfg` for your system and network, and then run `make`.

NOTE: GCC 4.7.x users using the KOS 2.1.0 build environment must ensure
``USING_KOS_GCC`` is enabled in `Makefile.cfg`. Disabling that flag uses compile
options meant for a portable copy of GCC 9.2/Binutils 2.33.1 compiled with an
``sh4-elf-`` prefix, which won't work with the KOS 2.1.0 compiler.

### Installation

1. PC - run `make install` (installs dc-tool)
2. DC

 a. `cd make-cd`, edit `Makefile`, insert blank cd-r, run `make`. If
   `1st_read.bin` hasn't been built yet, this `Makefile` will build it  
 or  
 b. take `target-src/1st_read/1st_read.bin` and stuff it on a cd yourself
      (please use the `IP.BIN` from the `make-cd` directory if you are going
      to distribute either cds or cd images).

### On-screen display

* If you see the message `NO ETHERNET ADAPTER DETECTED!`, something has
  gone wrong. The background of the screen will be red.

* The correct display is something like:

  `dcload-ip 1.1.1 - with DHCP`  <- name/version  
  `Broadband Adapter (HIT-0400)`  <- adapter driver in use  
  `00:d0:f1:02:ab:dd`  <- dc hardware address  
  `192.168.001.004`  <- dc ip address
  `idle...`  <- status  

  The background of the screen will be blue.

* For the **Broadband Adapter** only: if the status line reports `link
  change...` and does not change back to `idle...` within a short period
  of time, you may have a cable problem. dcload-ip will not work while
  `link change...` is displayed, or before it is displayed the first time.
  The `link change...` message normally is seen when you start dcload-ip,
  when you execute `dc-tool -r`, and when you disconnect the ethernet cable.

* If an exception is caught while a loaded program is running, the screen
  will turn lighter blue and display the exception info. dcload-ip should be
  active again after that point.

### Testing

1. `cd example-src`
2. `dc-tool -x console-test` (tests some PC I/O)
3. `dc-tool -x exception-test` (generates an exception)
4. `dc-tool -x gethostinfo` (displays the Dreamcast's ip, and the ip and port of
   the dc-tool host)

### KOS GDB-over-dcload

To run a GNU debugger session over the dcload connection:

1. Build/obtain an sh-elf targetted GNU debugger
2. Put a `gdb_init()` call somewhere in the startup area of your
   KOS-based program
3. Build your program with the `-g` GCC switch to include debugging info
4. Launch your program using `dc-tool -g -x <prog.elf>`
5. Launch sh-elf-gdb and connect to the dc-tool using `target remote :2159`
6. Squash bugs

### Maple Passthrough

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

### Performance Counter Control

Newly added is the ability to control Dreamcast/SH7091 performance counters over
the network. These were a previously hidden aspect of the Dreamcast's CPU, and
this program uses one of them to keep track of DHCP lease time across loaded
programs. There are two of them, and they are both 48-bit. See perfctr.h/.c for
details (they are from https://github.com/Moopthehedgehog/DreamHAL).

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
  // Clear counter and enable
  void PMCR_Init(int which, unsigned short mode, unsigned char count_type);

  // Enable one or both of these "undocumented" performance counters.
  void PMCR_Enable(int which, unsigned short mode, unsigned char count_type, unsigned char reset_counter);

  // Disable, clear, and re-enable with new mode (or same mode)
  void PMCR_Restart(int which, unsigned short mode, unsigned char count_type);

  // Read a counter
  void PMCR_Read(int which, volatile unsigned int *out_array);

  // Stop counter(s) (without clearing)
  void PMCR_Stop(int which);

  // Disable counter(s) (without clearing)
  void PMCR_Disable(int which);
```
The command is the first letter of the function (capitalized) plus the counter
number, plus a byte with the mode (if applicable). Except restart--read is 'R',
so restart is 'B' (think reBoot).

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
- See perfctr.h for how to calculate time using the CPU/bus ratio method, in
addition to the available counter modes (and for loads of other information)
- PMCR_Init() and PMCR_Enable() will do nothing if the perf counter is already running!

### Notes

* You can use `arp` instead of setting the Dreamcast's IP in `Makefile.cfg`.
On Windows, you may use the `netsh` command which is more reliable (e.g. `netsh
  interface ip add neighbors "Ethernet" 192.168.10.1 AA-BB-CC-DD-EE-FF)`. In that
  case, don't forget to specify an IP address in the Ethernet card of your computer.
  Please set the Dreamcast's IP in `Makefile.cfg` to be in the range 169.254.xxx.xxx
  when using this method, as 0.0.0.0 is used by the DHCP protocol for network
  discovery purposes (actually the entire 0.x.x.x range is).
* Tested systems: Debian GNU/Linux 2.2-3.0, Cygwin, MinGW,
[DreamSDK](https://www.dreamsdk.org), Windows Subsystem for Linux v2 (Debian)
* Many, many bugs have been squashed and much of the code overhauled
* Patches and improvements are welcome; please raise an issue here in the "Issues"
section for that

### Credits

* [SiZiOUS](https://www.github.com/SiZiOUS) for maintaining this program
* rtl8139 code based on code by Dan Potter
* LAN Adapter driver is pulled from an early version of the KOS LA driver
* There are some various files from `newlib-1.8.2` here
* `video.s`, `maple.c`, and `maple.h` were written by Marcus Comstedt
* initial win32 porting and implementation of -t by Florian 'Proff' Schulze
* win32 bugfix by The Gypsy
* fixes for cygwin by Florian 'Proff' Schulze
* rx config bug pointed out by Slant
* horridly buggy nature of 1.0.1 + 1.0.2 pointed out by Dan Potter
* Fixes for `libbfd` segfaults by Atani
* Inspiration for `MAPL` packet by Tim Hentenaar
