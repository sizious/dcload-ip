// ---- perfctr.h - SH7750/SH7091 Performance Counter Module Header ----
//
// This file is part of the DreamHAL project, a hardware abstraction library
// primarily intended for use on the SH7091 found in hardware such as the SEGA
// Dreamcast game console.
//
// The performance counter module is hereby released into the public domain in
// the hope that it may prove useful. Now go profile some code and hit 60 fps! :)
//
// This file has been adapted to meet the specific needs of dcload. Namely, the
// PMCR_Read() function uses an array to store values. This allows data to persist
// across program loads, since the entirety of dcload is technically "volatile."
//
// --Moopthehedgehog
//

#ifndef __PERFCTR_H__
#define __PERFCTR_H__

//
// -- General SH4 Performance Counter Notes --
//
// There are 2 performance counters that can measure elapsed time. They are each
// 48-bit counters. They are part of the so-called "ASE" subsystem, which you can
// read about in chapter 13 of the "SuperH™ (SH) 32-bit RISC series SH-4, ST40
// system architecture, volume 1: system":
// https://www.st.com/content/ccc/resource/technical/document/user_manual/36/75/05/ac/e8/7e/42/2d/CD00147163.pdf/files/CD00147163.pdf/jcr:content/translations/en.CD00147163.pdf
//
// They can count cycles, so that's 199.5MHz (not 200MHz!!) a.k.a. roughly 5 ns
// increments. At 5 ns increments, a 48-bit cycle counter can run continuously
// for 16.33 days. It's actually 16 days, 7 hours, 55 minutes, and 2 seconds,
// depending on how close the bus clock is to 99.75MHz. There is also a second
// mode that counts cycles according to a ratio between the CPU frequency and
// the system bus clock, and it increments the counter by 12 every bus cycle.
// This second mode is detailed in the description for PMCR_CLOCK_TYPE in this
// file, and it is recommended for use when the CPU frequency is not a runtime
// constant.
//
// Side note: The counters don't have an overflow interrupt or overflow bit.
// (I did actually run one to 48-bit overflow in elapsed time mode using the
// ratio method to check this. They don't appear to sign-extend the upper 16
// bits in elapsed time mode, either.)
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
// size as 16-bit:
// https://www.multimediaforum.de/threads/36260834-alice-hsn-3800tw-usb-jtag-ft4232h/page2
//
// -- Event Mode Info --
//
// Specific information on each counter mode can be found in the document titled
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
// with the filename debugger_sh4.pdf).
//

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

// These bits' functions are currently unknown, but they may simply be reserved.
// It's possible that there's a [maybe expired?] patent that details the
// configuration registers, though I haven't been able to find one. Places to
// check would be Google Patents and the Japanese Patent Office--maybe someone
// else can find something?
//
// Some notes:
// Writing 1 to all of these bits reads back as 0, so it looks like they aren't
// config bits. It's possible they are write-only like the stop bit, though,
// or that they're just reserved-write-0-only. It appears that they are always
// written with zeros in software that uses them, so that's confirmed safe to do.
//
// Also, after running counter 1 to overflow, it appears there's no overflow bit
// (maybe the designers thought 48-bits would be so much to count to that they
// didn't bother implementing one?). The upper 16-bits of the counter high
// register are also not sign-extension bits. They may be a hidden config area,
// but probably not because big endian mode would swap the byte order.
#define PMCR_UNKNOWN_BIT_0040 0x0040
#define PMCR_UNKNOWN_BIT_0080 0x0080
#define PMCR_UNKNOWN_BIT_0200 0x0200
#define PMCR_UNKNOWN_BIT_0400 0x0400
#define PMCR_UNKNOWN_BIT_0800 0x0800
#define PMCR_UNKNOWN_BIT_1000 0x1000

// PMCR_MODE_CLEAR_INVERTED just clears the event mode if it's inverted with
// '~', and event modes are listed below.
#define PMCR_MODE_CLEAR_INVERTED 0x003f

// PMCR_CLOCK_TYPE sets the counters to count clock cycles or CPU/bus ratio mode
// cycles (where T = C x B / 24 and T is time, C is count, and B is time
// of one bus cycle). Note: B = 1/99753008 or so, but it may vary, as mine is
// actually 1/99749010-ish; the target frequency is probably meant to be 99.75MHz.
//
// See the ST40 or Renesas SH7750R documents described in the above "Event Mode
// Info" section for more details about that formula.
//
// Set PMCR_CLOCK_TYPE to 0 for CPU cycle counting, where 1 count = 1 cycle, or
// set it to 1 to use the above formula. Renesas documentation recommends using
// the ratio version (set the bit to 1) when user programs alter CPU clock
// frequencies. This header has some definitions later on to help with this.
#define PMCR_CLOCK_TYPE 0x0100
#define PMCR_CLOCK_TYPE_SHIFT 8

// PMCR_STOP_COUNTER is write-only, as it always reads back as 0. It does what
// the name suggests: when this bit is written to, the counter stops. However,
// if written to while the counter is disabled or stopped, the counter's high
// and low registers are reset to 0.
//
// Using PMCR_STOP_COUNTER to stop the counter has the effect of holding the
// data in the data registers while stopped, unlike PMCR_DISABLE_COUNTER, and
// this bit needs to be written to again (e.g. on next start) in order to
// actually clear the counter data for another run. If not explicitly cleared,
// the counter will continue from where it left off before being stopped.
#define PMCR_STOP_COUNTER 0x2000
#define PMCR_RESET_COUNTER_SHIFT 13

// Bits 0xC000 both need to be set to 1 for the counters to actually begin
// counting. I have seen that the Linux kernel actually separates them out into
// two separate labelled bits (PMEN and PMST) for some reason, however they do
// not appear to do anything separately. Perhaps this is a two-bit mode where
// 1-1 is run, 1-0 and 0-1 are ???, and 0-0 is off.
#define PMCR_RUN_COUNTER 0xC000
#define PMCR_RUN_SHIFT 14
// Interestingly, the output here writes 0x6000 to the counter config registers,
// which would be the "PMST" bit and the "RESET" bit:
// https://www.multimediaforum.de/threads/36260834-alice-hsn-3800tw-usb-jtag-ft4232h/page2

// To disable a counter, just write 0 to its config register. This will not
// reset the counter to 0, as that requires an explicit clear via setting the
// PMCR_STOP_COUNTER bit. What's odd is that a disabled counter's data
// registers read back as all 0, but re-enabling it without a clear will
// continue from the last value before disabling.
#define PMCR_DISABLE_COUNTER 0x0000

// These definitions merely separate out the two PMCR_RUN_COUNTER bits, and
// they are included here for documentation purposes.

// PMST may mean PMCR START. It's consistently used to enable the counter.
// I'm just calling it PMST here for lack of a better name, since this is what
// the Linux kernel and lxdream call it. It could also have something to do with
// a mode specific to STMicroelectronics.
#define PMCR_PMST_BIT 0x4000
#define PMCR_PMST_SHIFT 14

// Likewise PMEN may mean PMCR ENABLE
#define PMCR_PMEN_BIT 0x8000
#define PMCR_PMEN_SHIFT 15

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

// These definitions are for the enable function and specify whether to reset
// a counter to 0 or to continue from where it left off
#define PMCR_CONTINUE_COUNTER 0
#define PMCR_RESET_COUNTER 1

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
void PMCR_Init(unsigned char which, unsigned char mode, unsigned char count_type);

// Enable one or both of these "undocumented" performance counters
void PMCR_Enable(unsigned char which, unsigned char mode, unsigned char count_type, unsigned char reset_counter);

// Disable, clear, and re-enable with new mode (or same mode)
void PMCR_Restart(unsigned char which, unsigned char mode, unsigned char count_type);

// Read a counter
// out_array is specifically uint32 out_array[2] -- 48-bit value needs a 64-bit storage unit
// Return value of 0xffffffffffff means invalid 'which'
void PMCR_Read(unsigned char which, volatile unsigned int *out_array);

// Get a counter's current configuration
// Return value of 0xffff means invalid 'which'
unsigned short PMCR_Get_Config(unsigned char which);

// Stop counter(s) (without clearing)
void PMCR_Stop(unsigned char which);

// Disable counter(s) (without clearing)
void PMCR_Disable(unsigned char which);

// TODO TEMP
unsigned long long int PMCR_RegRead(unsigned char which);

#endif /* __PERFCTR_H__ */
