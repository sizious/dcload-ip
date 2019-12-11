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

// See perfctr.h for more of my notes and documentation on these counters.
#include "perfctr.h"

static unsigned char pmcr_enabled = 0;

//
// Initialize performance counters. It's just a clear -> enable.
// It's good practice to clear a counter before starting it for the first time.
//
// Also: Disabling and re-enabling the counters doesn't reset them; the clearing
// needs to happen while a counter is disabled to reset it.
//
// I don't know if you can disable and re-enable with a different mode without
// explicitly clearing and have it keep going, continuing from where it left off.
// It might actually behave that way.
//

void init_pmctr(int which, unsigned short mode) // Will do nothing if perfcounter is already running!
{
	// Only need one of them, and there are 2
	// Don't do anything if being asked to enable an already-enabled counter
	if( (which == 1) && ((!pmcr_enabled) || (pmcr_enabled == 2)) )
	{
		// counter 1
		clear_pmctr(1);
		enable_pmctr(1, mode);
	}
	else if( (which == 2) && ((!pmcr_enabled) || (pmcr_enabled == 1)) )
	{
		// counter 2
		clear_pmctr(2);
		enable_pmctr(2, mode);
	}
	else if( (which == 3) && (!pmcr_enabled) )
	{	// Both
		clear_pmctr(3);
		enable_pmctr(3, mode);
	}
}

// Enable "undocumented" performance counters (well, they were undocumented at one point. They're documented now!)
void enable_pmctr(int which, unsigned short mode) // Will do nothing if perfcounter is already running!
{
	// Only need one of them, and there are 2
	// Don't do anything if being asked to enable an already-enabled counter
	if( (which == 1) && ((!pmcr_enabled) || (pmcr_enabled == 2)) )
	{
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		pmcr1_ctrl &= ~PMCR_MODE_CLEAR_INVERTED; // Assume all other bits are reserved, clear the mode bits
		pmcr1_ctrl |= mode; // Want elapsed time mode
		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl | PMCR_ENABLE_BIT | PMCR_PMST_BIT;

		pmcr_enabled += 1;
	}
	else if( (which == 2) && ((!pmcr_enabled) || (pmcr_enabled == 1)) )
	{
		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);
		pmcr2_ctrl &= ~PMCR_MODE_CLEAR_INVERTED; // Assume all other bits are reserved, clear the mode bits
		pmcr2_ctrl |= mode; // Want elapsed time mode
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl | PMCR_ENABLE_BIT | PMCR_PMST_BIT;

		pmcr_enabled += 2;
	}
	else if( (which == 3) && (!pmcr_enabled) )
	{	// Both
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		pmcr1_ctrl &= ~PMCR_MODE_CLEAR_INVERTED; // Assume all other bits are reserved, clear the mode bits
		pmcr1_ctrl |= mode; // Want elapsed time mode
		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl | PMCR_ENABLE_BIT | PMCR_PMST_BIT;

		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);
		pmcr2_ctrl &= ~PMCR_MODE_CLEAR_INVERTED; // Assume all other bits are reserved, clear the mode bits
		pmcr2_ctrl |= mode; // Want elapsed time mode
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl | PMCR_ENABLE_BIT | PMCR_PMST_BIT;

		pmcr_enabled = 3;
	}
}

// For reference:
// #define PMCTR1H_REG 0xFF100004
// #define PMCTR1L_REG 0xFF100008

// #define PMCTR2H_REG 0xFF10000C
// #define PMCTR2L_REG 0xFF100010

static const unsigned int pmcr1_regh = PMCTR1H_REG;
static const unsigned int pmcr1_regl = PMCTR1L_REG;

static const unsigned int pmcr2_regh = PMCTR2H_REG;
static const unsigned int pmcr2_regl = PMCTR2L_REG;

// Sorry, can only read one counter at a time!
// out_array should be an array consisting of 2x unsigned ints.
void read_pmctr(int which, volatile unsigned int *out_array)
{
 // if pmcr is not enabled, this function will just return 0

	// little endian (big endian would need to flip [0] and [1])

	// Note: These reads really do need to be done in assembly: unfortunately it
	// appears that using C causes GCC to insert a branch right smack in between
	// the high and low reads of perf counter 2 (with a nop, so it's literally
	// delaying the reads by several cycles!), which is totally insane. Doing it
	// the assembly way ensures that nothing ridiculous like that happens. It's
	// also portable between versions of GCC that do put the nonsensical branch in.
	//
	// One thing that would be nice is if SH4 had the movi20s instruction to make
	// absolute addresses in 3 cycles, but only the SH2A has that... :(
	if( (which == 1) && (pmcr_enabled & 0x1) )
	{
		// counter 1
//		out_array[1] = *((volatile unsigned int*)PMCTR1H_REG) & 0xffff;
//		out_array[0] = *((volatile unsigned int*)PMCTR1L_REG);
		asm volatile("mov.l %[reg1h],r1\n\t" // load counter address (high)
								 "mov.l %[reg1l],r2\n\t" // load counter address (low)
								 "mov.l @r1,r1\n\t" // read counter (high)
								 "mov.l @r2,r2\n\t" // read counter (low)
								 "extu.w r1,r1\n\t" // zero-extend high, aka high & 0xffff
								 "mov.l r1,%[outh]\n\t" // get data to memory
								 "mov.l r2,%[outl]\n\t" // get data to memory
		: [outh] "=m" (out_array[1]), [outl] "=m" (out_array[0])
		: [reg1h] "m" (pmcr1_regh), [reg1l] "m" (pmcr1_regl) // SH4 can't mov an immediate longword into a register...
		: // no clobbers
		);
	}
	else if( (which == 2) && (pmcr_enabled & 0x2) )
	{
		// counter 2
//		out_array[1] = *((volatile unsigned int*)PMCTR2H_REG) & 0xffff;
//		out_array[0] = *((volatile unsigned int*)PMCTR2L_REG);
		asm volatile("mov.l %[reg2h],r1\n\t" // load counter address (high)
								 "mov.l %[reg2l],r2\n\t" // load counter address (low)
								 "mov.l @r1,r1\n\t" // read counter (high)
								 "mov.l @r2,r2\n\t" // read counter (low)
								 "extu.w r1,r1\n\t" // zero-extend high, aka high & 0xffff
								 "mov.l r1,%[outh]\n\t" // get data to memory
								 "mov.l r2,%[outl]\n\t" // get data to memory
		: [outh] "=m" (out_array[1]), [outl] "=m" (out_array[0])
		: [reg2h] "m" (pmcr2_regh), [reg2l] "m" (pmcr2_regl) // SH4 can't mov an immediate longword into a register...
		: // no clobbers
		);
	}
	else if(!pmcr_enabled)
	{
		out_array[1] = 0;
		out_array[0] = 0;
	}
	else // Invalid
	{
		out_array[1] = 0xffff;
		out_array[0] = 0xffffffff;
	}

// This is how the counter registers are meant to be read, and is done above:
//	result = ((unsigned long long int)(result_h & 0xffff) << 32) | (result_l);
// return result;
// return *((unsigned long long int*) out_array); // Return 64-bit int on a system with only 32-bit general registers...
}

// Reset counter to 0 and start it again
// NOTE: It does not appear to be possible to clear a counter while it is running.
void restart_pmctr(int which, unsigned short mode)
{
	if( (which == 1) && (pmcr_enabled & 0x1) )
 	{
 		// counter 1
		disable_pmctr(1);
		clear_pmctr(1);
		enable_pmctr(1, mode);
 	}
	else if( (which == 2) && (pmcr_enabled & 0x2) )
 	{
 		// counter 2
		disable_pmctr(2);
		clear_pmctr(2);
		enable_pmctr(2, mode);
 	}
	else if( (which == 3) && (pmcr_enabled == 3) )
 	{
		// Both
		disable_pmctr(3);
		clear_pmctr(3);
		enable_pmctr(3, mode);
 	}
}

// Clearing only works when the counter is disabled.
void clear_pmctr(int which)
{
	if( (which == 1) && ((!pmcr_enabled) || (pmcr_enabled == 2)) )
	{
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl | PMCR_CLEAR_COUNTER;
	}
	else if( (which == 2) && ((!pmcr_enabled) || (pmcr_enabled == 1)) )
	{
		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl | PMCR_CLEAR_COUNTER;
	}
	else if( (which == 3) && (!pmcr_enabled) )
	{
		// Both
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl | PMCR_CLEAR_COUNTER;

		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl | PMCR_CLEAR_COUNTER;
	}
}

// Remember to disable before leaving DCLOAD to execute a program
// Note that disabling does NOT clear the counter.
void disable_pmctr(int which)
{
	if( (which == 1) && (pmcr_enabled & 0x1) )
	{
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		pmcr1_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT); // Assume all other bits are reserved
		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl;

		pmcr_enabled -=1;
	}
	else if( (which == 2) && (pmcr_enabled & 0x2) )
	{
		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);
		pmcr2_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT); // Assume all other bits are reserved
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl;

		pmcr_enabled -= 2;
	}
	else if( (which == 3) && (pmcr_enabled == 3) )
	{
		// Both
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		pmcr1_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT); // Assume all other bits are reserved
		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl;

		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);
		pmcr2_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT); // Assume all other bits are reserved
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl;

		pmcr_enabled = 0;
	}
}
