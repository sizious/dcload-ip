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
// There are 2 performance counters that can measure elapsed time. They are each 48-bit counters per here:
// http://git.lpclinux.com/cgit/linux-2.6.28.2-lpc313x/plain/arch/sh/oprofile/op_model_sh7750.c
//
// They count cycles, so that's 200MHz a.k.a. 5 ns increments. At 5 ns increments,
// a 48-bit cycle counter can run continuously for 16.3 days.
// Side note: apparently they don't have an overflow interrupt.
//
// Documented in lxdream:
/* Performance counter values (undocumented) */
/*
// Note: LONG here is 32-bit, so unsigned int

	MMIO_REGION_BEGIN( 0xFF100000, PMM, "Performance monitoring" )
	    LONG_PORT( 0x004, PMCTR1H, PORT_MR, 0, "Performance counter 1 High" )
	    LONG_PORT( 0x008, PMCTR1L, PORT_MR, 0, "Performance counter 1 Low" )
	    LONG_PORT( 0x00C, PMCTR2H, PORT_MR, 0, "Performance counter 2 High" )
	    LONG_PORT( 0x010, PMCTR2L, PORT_MR, 0, "Performance counter 2 Low" )
	MMIO_REGION_END


// Note: WORD here is a 16-bit word, so unsigned short

	MMIO_REGION_BEGIN( 0xFF000000, MMU, "MMU Registers" )
		...
		WORD_PORT( 0x084, PMCR1, PORT_MRW, 0, "Performance counter control 1" )
	  WORD_PORT( 0x088, PMCR2, PORT_MRW, 0, "Performance counter control 2" )
	MMIO_REGION_END

	#define PMCR_CLKF  0x0100
	#define PMCR_PMCLR 0x2000
	#define PMCR_PMST  0x4000
	#define PMCR_PMEN  0x8000
	#define PMCR_RUNNING (PMCR_PMST|PMCR_PMEN)

// See here for lxdream's PMU implementation (only supports 0x23, elapsed time, currently--which is the one we want!)
// https://github.com/lxdream/lxdream/blob/master/src/sh4/pmm.c
// See here for linux kernel's implementation:
// https://github.com/torvalds/linux/blob/master/arch/sh/kernel/cpu/sh4/perf_event.c

// Performance counter available options (as documented in Linux kernel):

 * There are a number of events supported by each counter (33 in total).
 * Since we have 2 counters, each counter will take the event code as it
 * corresponds to the PMCR PMM setting. Each counter can be configured
 * independently.
 *
 *	Event Code	Description
 *	----------	-----------
 *
 *	0x01		Operand read access
 *	0x02		Operand write access
 *	0x03		UTLB miss
 *	0x04		Operand cache read miss
 *	0x05		Operand cache write miss
 *	0x06		Instruction fetch (w/ cache)
 *	0x07		Instruction TLB miss
 *	0x08		Instruction cache miss
 *	0x09		All operand accesses
 *	0x0a		All instruction accesses
 *	0x0b		OC RAM operand access
 *	0x0d		On-chip I/O space access
 *	0x0e		Operand access (r/w)
 *	0x0f		Operand cache miss (r/w)
 *	0x10		Branch instruction
 *	0x11		Branch taken
 *	0x12		BSR/BSRF/JSR
 *	0x13		Instruction execution
 *	0x14		Instruction execution in parallel
 *	0x15		FPU Instruction execution
 *	0x16		Interrupt
 *	0x17		NMI
 *	0x18		trapa instruction execution
 *	0x19		UBCA match
 *	0x1a		UBCB match
 *	0x21		Instruction cache fill
 *	0x22		Operand cache fill
 *	0x23		Elapsed time
 *	0x24		Pipeline freeze by I-cache miss
 *	0x25		Pipeline freeze by D-cache miss
 *	0x27		Pipeline freeze by branch instruction
 *	0x28		Pipeline freeze by CPU register
 *	0x29		Pipeline freeze by FPU
*/

// Note that DHCP renewal, lease time, and transaction ID are using perf counter 1.

// The two counters are functionally identical, though, so perf counter 2 is
// totally free for use. I would recommend using the init_pmctr function to
// start it up the first time. But, hey! Do whatever--it's your SH4, after all.

#ifndef __PERFCTR_H__
#define __PERFCTR_H__

#define PMCR1_CTRL_REG 0xFF000084
#define PMCR2_CTRL_REG 0xFF000088

// I have not yet seen an example of what PMCR_CLKF does. Nothing uses it.
// CLKF --probably--> CLOCK FREQ
#define PMCR_CLOCK_FREQ 0x0100

// These may simply be reserved
#define UNKNOWN_BIT_0 0x0200
#define UNKNOWN_BIT_1 0x0400
#define UNKNOWN_BIT_2 0x0800
#define UNKNOWN_BIT_3 0x1000

// Note: mode clear just clears the event mode if its inverted with '~', and event modes are listed above.
#define PMCR_MODE_CLEAR_INVERTED 0x003f
#define PMCR_CLEAR_COUNTER 0x2000
#define PMCR_PMST_BIT 0x4000
#define PMCR_ENABLE_BIT 0x8000
// PMST could mean PM START or PM STANDBY, not sure. It's consistently used to enable the counter, though, so I guess it's PM START.

#define PMCR_ELAPSED_TIME_MODE 0x23

#define PMCTR1H_REG 0xFF100004
#define PMCTR1L_REG 0xFF100008

#define PMCTR2H_REG 0xFF10000C
#define PMCTR2L_REG 0xFF100010

// Performance Counter Functions
// See perfctr.c file for more details about each function and some more usage notes.

// (init and enable will do nothing if perfcounter is already running!)
void init_pmctr(int which, unsigned short mode); // Clear existing counter and enable
void enable_pmctr(int which, unsigned short mode); // Enable one or both of these "undocumented" performance counters. Does not clear counter(s).
void restart_pmctr(int which, unsigned short mode); // Disable, clear, and re-enable with new mode (or same mode)

void read_pmctr(int which, volatile unsigned int *out_array); // 48-bit value needs a 64-bit storage unit

void clear_pmctr(int which); // Only when disabled
void disable_pmctr(int which); // Remember to disable before leaving DCLOAD to execute a program. Does not clear counter(s).

#endif /* __PERFCTR_H__ */
