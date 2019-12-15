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

void PMCR_Init(int which, unsigned short mode, unsigned char count_type) // Will do nothing if perfcounter is already running!
{
	// Only need one of them, and there are 2
	// Don't do anything if being asked to enable an already-enabled counter
	if( (which == 1) && ((!pmcr_enabled) || (pmcr_enabled == 2)) )
	{
		// counter 1
		PMCR_Clear(1);
		PMCR_Enable(1, mode, count_type);
	}
	else if( (which == 2) && ((!pmcr_enabled) || (pmcr_enabled == 1)) )
	{
		// counter 2
		PMCR_Clear(2);
		PMCR_Enable(2, mode, count_type);
	}
	else if( (which == 3) && (!pmcr_enabled) )
	{	// Both
		PMCR_Clear(3);
		PMCR_Enable(3, mode, count_type);
	}
}

// Enable "undocumented" performance counters (well, they were undocumented at one point. They're documented now!)
void PMCR_Enable(int which, unsigned short mode, unsigned char count_type) // Will do nothing if perfcounter is already running!
{
	// Only need one of them, and there are 2
	// Don't do anything if being asked to enable an already-enabled counter
	if(count_type > 1)
	{
		return;
	}

	if( (which == 1) && ((!pmcr_enabled) || (pmcr_enabled == 2)) )
	{
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);

		// Clear the mode bits, default to 1 count = 1 cycle
		pmcr1_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_CLOCK_TYPE);

		// Set mode, switch to CPU/Bus ratio for counting if set (saves an if() this way)
		pmcr1_ctrl |= mode | (count_type << PMCR_CLOCK_TYPE_SHIFT);

		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl | PMCR_ENABLE_BIT | PMCR_PMST_BIT;

		pmcr_enabled += 1;
	}
	else if( (which == 2) && ((!pmcr_enabled) || (pmcr_enabled == 1)) )
	{
		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);

		// Clear the mode bits, default to 1 count = 1 cycle
		pmcr2_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_CLOCK_TYPE);

		// Set mode, switch to CPU/Bus ratio for counting if set (saves an if() this way)
		pmcr2_ctrl |= mode | (count_type << PMCR_CLOCK_TYPE_SHIFT);

		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl | PMCR_ENABLE_BIT | PMCR_PMST_BIT;

		pmcr_enabled += 2;
	}
	else if( (which == 3) && (!pmcr_enabled) )
	{
		// Both
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);

		// Clear the mode bits, default to 1 count = 1 cycle
		pmcr1_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_CLOCK_TYPE);
		pmcr2_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_CLOCK_TYPE);

		// Set mode, switch to CPU/Bus ratio for counting if set (saves an if() this way)
		pmcr1_ctrl |= mode | (count_type << PMCR_CLOCK_TYPE_SHIFT);
		pmcr2_ctrl |= mode | (count_type << PMCR_CLOCK_TYPE_SHIFT);

		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl | PMCR_ENABLE_BIT | PMCR_PMST_BIT;
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
void PMCR_Read(int which, volatile unsigned int *out_array)
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
void PMCR_Restart(int which, unsigned short mode, unsigned char count_type)
{
	if( (which == 1) && (pmcr_enabled & 0x1) )
 	{
 		// counter 1
		PMCR_Disable(1);
		PMCR_Clear(1);
		PMCR_Enable(1, mode, count_type);
 	}
	else if( (which == 2) && (pmcr_enabled & 0x2) )
 	{
 		// counter 2
		PMCR_Disable(2);
		PMCR_Clear(2);
		PMCR_Enable(2, mode, count_type);
 	}
	else if( (which == 3) && (pmcr_enabled == 3) )
 	{
		// Both
		PMCR_Disable(3);
		PMCR_Clear(3);
		PMCR_Enable(3, mode, count_type);
 	}
}

// Clearing only works when the counter is disabled.
void PMCR_Clear(int which)
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
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);

		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl | PMCR_CLEAR_COUNTER;
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl | PMCR_CLEAR_COUNTER;
	}
}

// Note that disabling does NOT clear the counter.
void PMCR_Disable(int which)
{
	if( (which == 1) && (pmcr_enabled & 0x1) )
	{
		// counter 1
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		pmcr1_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT);
		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl;

		pmcr_enabled -= 1;
	}
	else if( (which == 2) && (pmcr_enabled & 0x2) )
	{
		// counter 2
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);
		pmcr2_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT);
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl;

		pmcr_enabled -= 2;
	}
	else if( (which == 3) && (pmcr_enabled == 3) )
	{
		// Both
		unsigned short pmcr1_ctrl = *((volatile unsigned short*)PMCR1_CTRL_REG);
		unsigned short pmcr2_ctrl = *((volatile unsigned short*)PMCR2_CTRL_REG);

		pmcr1_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT);
		pmcr2_ctrl &= ~(PMCR_MODE_CLEAR_INVERTED | PMCR_ENABLE_BIT);

		*((volatile unsigned short*)PMCR1_CTRL_REG) = pmcr1_ctrl;
		*((volatile unsigned short*)PMCR2_CTRL_REG) = pmcr2_ctrl;

		pmcr_enabled = 0;
	}
}
