// As much as I'd love to put my name up here for 2019 copyright, uh, Internet
// pseudonyms and all that. ;)
//
// So consider the perfctr.h & perfctr.c set of files in the public domain. It
// would always be great to give credit back to the original source!!
//
// Note that DCLOAD-IP is actually GPLv2 licensed, though public domain code is
// absolutely compatible with that.
//
// --Moopthehedgehog

//
// -- General SH4 performance Counter Notes --
//
// There are 2 performance counters that can measure elapsed time. They are each
// 48-bit counters. They are part of the so-called "ASE" subsystem, which you can
// read about in chapter 13 of the "SuperH™ (SH) 32-bit RISC series SH-4, ST40
// system architecture, volume 1: system":
// https://www.st.com/content/ccc/resource/technical/document/user_manual/36/75/05/ac/e8/7e/42/2d/CD00147163.pdf/files/CD00147163.pdf/jcr:content/translations/en.CD00147163.pdf
//
// They can count cycles, so that's 200MHz a.k.a. 5 ns increments. At 5 ns
// increments, a 48-bit cycle counter can run continuously for almost 16.3 days.
// It's actually closer to 16 days, 7 hours, 55 minutes, and 2 seconds, depending
// on how close the bus clock is to 99.75MHz.
// Side note: apparently they don't have an overflow interrupt.
//
// The two counters are functionally identical. I would recommend using the
// PMCR_Init() function to start one (or both) up the first time.
//
// -- Configuration Address Info --
//
// Addresses for these counters can be easily seen here, in lxdream's source code:
// https://github.com/lutris/lxdream/blob/master/src/sh4/sh4mmio.h
//
// They are also on display in the Linux kernel, but at the time of writing appear
// to be set incorrectly (the clock mode at bit 0x100 is never set or cleared,
// for example, so they're at the mercy of whatever the hardware defaults are):
// http://git.lpclinux.com/cgit/linux-2.6.28.2-lpc313x/plain/arch/sh/oprofile/op_model_sh7750.c
// https://github.com/torvalds/linux/blob/master/arch/sh/kernel/cpu/sh4/perf_event.c
// ...It also appears as though they may not be handling bus ratio mode correctly,
// which appears to be the default mode on the Dreamcast in all my tests.
//
// You can also find these addresses by ripping a copy of Virtua Fighter 3 that
// you own for Dreamcast and looking at the raw byte code (or a raw disassembly)
// of its main program binary. It would appear as though they were timing a loop
// with the low half of perf counter 1 in elapsed time mode. Definitely seems
// like a good thing to do when targeting 60fps! Shenmue Disc 4 also uses the
// same configuration, but what's being timed is not as clear.
//
// Another place you can actually find both control addresses 0xFF00008x and all
// data addresses 0xFF10000x is in binaries of ancient, freely available versions
// of CodeScape. Literally all you need to do is open an SH7750-related DLL in a
// hex editor and do a search to find the control register addresses, and the
// data addresses are equally plain to see in any relevant performance profiling
// firmware. There's no effort or decryption required to find them whatsoever;
// all you need is an old trial version and a hex editor.
//
// However, something even better than all of that is if you search for "SH4
// 0xFF000084" (without quotes) online you'll find an old forum where some logs
// were posted of the terminal/command prompt output from some STMicro JTAG tool,
// which not only has the address registers but also clearly characterizes their
// size as 16-bit: https://www.multimediaforum.de/threads/36260834-alice-hsn-3800tw-usb-jtag-ft4232h/page2
//
// -- Event Mode Info --
//
// Specific information on each of these modes can be found in the document titled
// "SuperH™ Family E10A-USB Emulator: Additional Document for User’s Manual:
// Supplementary Information on Using the SH7750R Renesas Microcomputer Development Environment System"
// which is available on Renesas's website, in the "Documents" section of the
// E10A-USB product page:
// https://www.renesas.com/us/en/products/software-tools/tools/emulator/e10a-usb.html
// At the time of writing (12/2019), the E10A-USB adapter is still available
// for purchase, and it is priced around $1200 (USD).
//
// Appendix C of the "ST40 Micro Toolset Manual" also has these modes documented:
// https://www.st.com/content/ccc/resource/technical/document/user_manual/c5/98/11/89/50/68/41/66/CD17379953.pdf/files/CD17379953.pdf/jcr:content/translations/en.CD17379953.pdf
//
// See here for the hexadecimal values corresponding to each mode (pg. 370):
// http://www.macmadigan.com/BusaECU/Renesas%20documents/Hitachi_codescape_CS40_light_userguides.pdf
// You can also find the same "Counter Description Table" in user's guide PDFs
// bundled in ancient demo versions of CodeScape 3 from 2000 (e.g.
// CSDemo_272.exe), which can still be found in the Internet Archive.
// http://web.archive.org/web/*/http://codescape.com/dl/CSDemo/*
//
// See here for a support document on Lauterbach's SH2, SH3, and SH4 debugger,
// which contains units for each mode (e.g. which measure time and which just
// count): https://www.lauterbach.com/frames.html?home.html (It's in Downloads
// -> Trace32 Help System -> it's the file called "SH2, SH3 and SH4 Debugger"
// with the filename debugger_sh4.pdf)
//

#ifndef __PERFCTR_H__
#define __PERFCTR_H__

//
// --- Performance Counter Registers ---
//

// These registers are 16 bits only and configure the performance counters
#define PMCR1_CTRL_REG 0xFF000084
#define PMCR2_CTRL_REG 0xFF000088

// These registers are 32-bits each and hold the high low parts of each counter
#define PMCTR1H_REG 0xFF100004
#define PMCTR1L_REG 0xFF100008

#define PMCTR2H_REG 0xFF10000C
#define PMCTR2L_REG 0xFF100010

//
// --- Performance Counter Configuration Flags ---
//

// These bits' functions are currently unknown. They may simply be reserved.
// It's possible that there's a [maybe expired?] patent that details the
// configuration registers. I haven't been able to find one, though. Places to
// check would be Google Patents and the Japanese Patent Office--maybe someone
// else can find something?
#define PMCR_UNKNOWN_BIT_0200 0x0200 // <-- subroutine tracing?
#define PMCR_UNKNOWN_BIT_0400 0x0400
#define PMCR_UNKNOWN_BIT_0800 0x0800
#define PMCR_UNKNOWN_BIT_1000 0x1000
// One of them might be "Count while CPU sleep"? Not sure

// PMCR_MODE_CLEAR_INVERTED just clears the event mode if it's inverted with
// '~', and event modes are listed below. Mode bits may just be 0x00ff, but it's
// possible that bits 0x40 and 0x80 are also unknown bits, so I'm not taking
// any chances.
#define PMCR_MODE_CLEAR_INVERTED 0x003f

// PMCR_CLOCK_TYPE sets the counters to count clock cycles instead of CPU/bus
// ratio cycles (where T = C x B / 24 and T is time, C is count, and B is time
// of one bus cycle). Note: B = 1/99753008 or so, but it may vary, as mine is
// actually 1/99749010-ish; the target frequency is probably meant to be 99.75MHz.
//
// See the ST40 or Renesas SH7750R documents described in the above "Event Mode
// Info" section for more details about that formula.
//
// Set PMCR_CLOCK_TYPE to 0 for CPU cycle counting where 1 count = 1 cycle, or
// set it to 1 to use the above formula. Renesas documentation recommends using
// the ratio version (set the bit to 1) when user programs mess with clock
// frequencies. This file has some definitions later on to help with this.
#define PMCR_CLOCK_TYPE 0x0100
#define PMCR_CLOCK_TYPE_SHIFT 8

// PMCR_CLEAR_COUNTER is write-only; it always reads back as 0. It also only
// works when the timer is disabled/stopped. It does what the name suggests:
// when this bit is written to, the counter's high and low registers are wiped.
#define PMCR_CLEAR_COUNTER 0x2000
#define PMCR_CLEAR_COUNTER_SHIFT 13

// PMST appears to mean PM START. It's consistently used to enable the counter,
// though, so I guess it's PM START. I'm just calling it PMST here for lack of
// a better name, since this is what the Linux kernel and lxdream call it.
#define PMCR_PMST_BIT 0x4000
#define PMCR_PMST_SHIFT 14

// Enable the perf counter
#define PMCR_ENABLE_BIT 0x8000
#define PMCR_ENABLE_SHIFT 15
// You know what? These bits might be backwards. Need to do some more testing to
// figure out what happens with different combinations
// It appears that it takes both PMST and ENABLE to start the counter running.
// Disabling either stops the counter.
// Perhaps it's a 2-bit mode: 1-1 is run, 1-0 and 0-1 are ??? and 0-0 is off

// So maybe we need this?
#define PMCR_RUN_BITS 0xC000
#define PMCR_RUN_SHIFT 14

//
// --- Performance Counter Event Code Definitions ---
//
// Interestingly enough, it so happens that the SEGA Dreamcast's CPU seems to
// contain the same performance counter functionality as SH4 debug adapters for
// the SH7750R. Awesome!
//

//                MODE DEFINITION                  VALUE   MEASURMENT TYPE & NOTES
#define PMCR_INIT_NO_MODE                           0x00 // None; Just here to be complete
#define PMCR_OPERAND_READ_ACCESS_MODE               0x01 // Quantity; With cache
#define PMCR_OPERAND_WRITE_ACCESS_MODE              0x02 // Quantity; With cache
#define PMCR_UTLB_MISS_MODE                         0x03 // Quantity
#define PMCR_OPERAND_CACHE_READ_MISS_MODE           0x04 // Quantity
#define PMCR_OPERAND_CACHE_WRITE_MISS_MODE          0x05 // Quantity
#define PMCR_INSTRUCTION_FETCH_MODE                 0x06 // Quantity; With cache
#define PMCR_INSTRUCTION_TLB_MISS_MODE              0x07 // Quantity
#define PMCR_INSTRUCTION_CACHE_MISS_MODE            0x08 // Quantity
#define PMCR_ALL_OPERAND_ACCESS_MODE                0x09 // Quantity
#define PMCR_ALL_INSTRUCTION_FETCH_MODE             0x0a // Quantity
#define PMCR_ON_CHIP_RAM_OPERAND_ACCESS_MODE        0x0b // Quantity
// No 0x0c
#define PMCR_ON_CHIP_IO_ACCESS_MODE                 0x0d // Quantity
#define PMCR_OPERAND_ACCESS_MODE                    0x0e // Quantity; With cache, counts both reads and writes
#define PMCR_OPERAND_CACHE_MISS_MODE                0x0f // Quantity
#define PMCR_BRANCH_ISSUED_MODE                     0x10 // Quantity; Not the same as branch taken!
#define PMCR_BRANCH_TAKEN_MODE                      0x11 // Quantity
#define PMCR_SUBROUTINE_ISSUED_MODE                 0x12 // Quantity; Issued a BSR, BSRF, JSR, JSR/N
#define PMCR_INSTRUCTION_ISSUED_MODE                0x13 // Quantity
#define PMCR_PARALLEL_INSTRUCTION_ISSUED_MODE       0x14 // Quantity
#define PMCR_FPU_INSTRUCTION_ISSUED_MODE            0x15 // Quantity
#define PMCR_INTERRUPT_COUNTER_MODE                 0x16 // Quantity
#define PMCR_NMI_COUNTER_MODE                       0x17 // Quantity
#define PMCR_TRAPA_INSTRUCTION_COUNTER_MODE         0x18 // Quantity
#define PMCR_UBC_A_MATCH_MODE                       0x19 // Quantity
#define PMCR_UBC_B_MATCH_MODE                       0x1a // Quantity
// No 0x1b-0x20
#define PMCR_INSTRUCTION_CACHE_FILL_MODE            0x21 // Cycles
#define PMCR_OPERAND_CACHE_FILL_MODE                0x22 // Cycles
#define PMCR_ELAPSED_TIME_MODE                      0x23 // Cycles; For 200MHz CPU: 5ns per count in 1 cycle = 1 count mode, or around 417.715ps per count (increments by 12) in CPU/bus ratio mode
#define PMCR_PIPELINE_FREEZE_BY_ICACHE_MISS_MODE    0x24 // Cycles
#define PMCR_PIPELINE_FREEZE_BY_DCACHE_MISS_MODE    0x25 // Cycles
// No 0x26
#define PMCR_PIPELINE_FREEZE_BY_BRANCH_MODE         0x27 // Cycles
#define PMCR_PIPELINE_FREEZE_BY_CPU_REGISTER_MODE   0x28 // Cycles
#define PMCR_PIPELINE_FREEZE_BY_FPU_MODE            0x29 // Cycles

//
// --- Performance Counter Support Definitions ---
//

// This definition can be passed as the init/enable/restart functions'
// count_type parameter to use the 1 cycle = 1 count mode. This is how the
// counter can be made to run for 16.3 days.
#define PMCR_COUNT_CPU_CYCLES 0
// Likewise this uses the CPU/bus ratio method
#define PMCR_COUNT_RATIO_CYCLES 1

//
// --- Performance Counter Miscellaneous Definitions ---
//
// For convenience; assume stock bus clock of 99.75MHz
// (Bus clock is the external CPU clock, not the peripheral bus clock)
//

#define PMCR_SH4_CPU_FREQUENCY 199500000
#define PMCR_CPU_CYCLES_MAX_SECONDS 1410902
#define PMCR_SH4_BUS_FREQUENCY 99750000
#define PMCR_SH4_BUS_FREQUENCY_SCALED 2394000000 // 99.75MHz x 24
#define PMCR_BUS_RATIO_MAX_SECONDS 117575

//
// --- Performance Counter Functions ---
//
// See perfctr.c file for more details about each function and some more usage notes.
//
// Note: PMCR_Init() and PMCR_Enable() will do nothing if the perf counter is already running!
//

// Clear counter and enable
void PMCR_Init(int which, unsigned short mode, unsigned char count_type);

// Enable one or both of these "undocumented" performance counters. Does not clear counter(s).
void PMCR_Enable(int which, unsigned short mode, unsigned char count_type);

// Disable, clear, and re-enable with new mode (or same mode)
void PMCR_Restart(int which, unsigned short mode, unsigned char count_type);

// Read a counter
// out_array is specifically uint32 out_array[2] -- 48-bit value needs a 64-bit storage unit
void PMCR_Read(int which, volatile unsigned int *out_array);

// Clear counter(s) -- only works when disabled!
void PMCR_Clear(int which);

// Disable counter(s) without clearing
void PMCR_Disable(int which);

#endif /* __PERFCTR_H__ */
