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
// SH4 performance counter notes:
//
// There are 2 performance counters that can measure elapsed time. They are each
// 48-bit counters per here:
// http://git.lpclinux.com/cgit/linux-2.6.28.2-lpc313x/plain/arch/sh/oprofile/op_model_sh7750.c
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
// See here for lxdream's PMU implementation (currently appears to only support
// mode 0x23, elapsed time):
// https://github.com/lxdream/lxdream/blob/master/src/sh4/pmm.c
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

// These bits' functions are currently unknown. They may simply be reserved:
#define PMCR_UNKNOWN_BIT_0200 0x0200
#define PMCR_UNKNOWN_BIT_0400 0x0400
#define PMCR_UNKNOWN_BIT_0800 0x0800
#define PMCR_UNKNOWN_BIT_1000 0x1000
// One of them might be "Count while CPU sleep"

// PMCR_CLOCK_TYPE sets the counters to count clock cycles instead of CPU/bus
// ratio cycles (where T = C x B / 24 and T is time, C is count, and B is time
// of one bus cycle). Note: B = 1/99753008 or so, but it may vary, as mine is
// actually 1/99749010-ish; the target frequency is probably meant to be 99.75MHz.
//
// Set that bit to 0 for CPU cycle counting where 1 count = 1 cycle, set it to
// 1 to use the above formula. Renesas documentation recommends using the ratio
// version (set the bit to 1) when user programs mess with clock frequencies.
// This file has some definitions later on to help with this.
#define PMCR_CLOCK_TYPE 0x0100

// Note: mode clear just clears the event mode if its inverted with '~', and
// event modes are listed below. Mode bits are probably just 0x00ff...
#define PMCR_MODE_CLEAR_INVERTED 0x003f
#define PMCR_CLEAR_COUNTER 0x2000
#define PMCR_PMST_BIT 0x4000
#define PMCR_ENABLE_BIT 0x8000
// PMST could mean PM START or PM STANDBY, not sure. It's consistently used to enable the counter, though, so I guess it's PM START.
// PMCR_CLEAR_COUNTER is write-only; it always reads back as 0

//
// --- Performance Counter Event Code Definitions ---
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
// See here for the linux kernel's implementation and exact hex values for each mode:
// https://github.com/torvalds/linux/blob/master/arch/sh/kernel/cpu/sh4/perf_event.c
//
// See here for a support document on Lauterbach's SH2, SH3, and SH4 debugger,
// which contains the exact units for each mode (e.g. which measure time and
// which just count): https://www.lauterbach.com/frames.html?home.html
// (It's in Downloads -> Trace32 Help System -> it's the file called "SH2, SH3
// and SH4 Debugger" with the filename debugger_sh4.pdf)
//

//
// Interestingly enough, it so happens that the SEGA Dreamcast's CPU seems to
// contain the same performance counter functionality as SH4 debug adapters. Neat!
//

//                MODE DEFINITION                  VALUE   MEASURMENT TYPE & NOTES
#define PMCR_INIT_NO_MODE                           0x00 // None; Just here to be complete
#define PMCR_OPERAND_READ_ACCESS_MODE               0x01 // Counts; With cache
#define PMCR_OPERAND_WRITE_ACCESS_MODE              0x02 // Counts; With cache
#define PMCR_UTLB_MISS_MODE                         0x03 // Counts
#define PMCR_OPERAND_CACHE_READ_MISS_MODE           0x04 // Counts
#define PMCR_OPERAND_CACHE_WRITE_MISS_MODE          0x05 // Counts
#define PMCR_INSTRUCTION_FETCH_MODE                 0x06 // Counts; With cache
#define PMCR_INSTRUCTION_TLB_MISS_MODE              0x07 // Counts
#define PMCR_INSTRUCTION_CACHE_MISS_MODE            0x08 // Counts
#define PMCR_ALL_OPERAND_ACCESS_MODE                0x09 // Counts
#define PMCR_ALL_INSTRUCTION_FETCH_MODE             0x0a // Counts
#define PMCR_ON_CHIP_RAM_OPERAND_ACCESS_MODE        0x0b // Counts
// No 0x0c
#define PMCR_ON_CHIP_IO_ACCESS_MODE                 0x0d // Counts
#define PMCR_OPERAND_ACCESS_MODE                    0x0e // Counts; With cache, counts both reads and writes
#define PMCR_OPERAND_CACHE_MISS_MODE                0x0f // Counts
#define PMCR_BRANCH_ISSUED_MODE                     0x10 // Counts; Not the same as branch taken!
#define PMCR_BRANCH_TAKEN_MODE                      0x11 // Counts
#define PMCR_SUBROUTINE_ISSUED_MODE                 0x12 // Counts; Issued a BSR, BSRF, JSR, JSR/N
#define PMCR_INSTRUCTION_ISSUED_MODE                0x13 // Counts
#define PMCR_PARALLEL_INSTRUCTION_ISSUED_MODE       0x14 // Counts
#define PMCR_FPU_INSTRUCTION_ISSUED_MODE            0x15 // Counts
#define PMCR_INTERRUPT_COUNTER_MODE                 0x16 // Counts
#define PMCR_NMI_COUNTER_MODE                       0x17 // Counts
#define PMCR_TRAPA_INSTRUCTION_COUNTER_MODE         0x18 // Counts
#define PMCR_UBC_A_MATCH_MODE                       0x19 // Counts
#define PMCR_UBC_B_MATCH_MODE                       0x1a // Counts
// No 0x1b-0x20
#define PMCR_INSTRUCTION_CACHE_FILL_MODE            0x21 // Time
#define PMCR_OPERAND_CACHE_FILL_MODE                0x22 // Time
#define PMCR_ELAPSED_TIME_MODE                      0x23 // Time; For 200MHz CPU: 5ns per count in 1 cycle = 1 count mode, or around 417.715ps per count (increments by 12) in CPU/bus ratio mode
#define PMCR_PIPELINE_FREEZE_BY_ICACHE_MISS_MODE    0x24 // Time
#define PMCR_PIPELINE_FREEZE_BY_DCACHE_MISS_MODE    0x25 // Time
// No 0x26
#define PMCR_PIPELINE_FREEZE_BY_BRANCH_MODE         0x27 // Time
#define PMCR_PIPELINE_FREEZE_BY_CPU_REGISTER_MODE   0x28 // Time
#define PMCR_PIPELINE_FREEZE_BY_FPU_MODE            0x29 // Time

//
// --- Performance Counter Support Definitions ---
//

// This definition can be passed to the init/enable/restart functions to use the
// 1 cycle = 1 count mode. This is how the timer can be made to run for 16.3 days.
#define PMCR_COUNT_CPU_CYCLES 1
// Likewise this uses the CPU/bus ratio method
#define PMCR_COUNT_RATIO_CYCLES 0

//
// --- Performance Counter Miscellaneous Definitions ---
//
// For convenience; assume stock bus clock of 99.75MHz
// (Bus clock is the external CPU clock, not the peripheral bus clock)

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
