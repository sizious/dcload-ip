
#include "memfuncs.h"
#include <string.h>

// Special functions to speed up packet processing
//
// These all require appropriate alignment of source and destination.
//
// Using __attribute__((aligned(*size*))) where size is 2, 4, or 8 is very
// important, otherwise these functions *will* crash. If the crash is in a
// critical pathway, the exception handler may also crash and you'll need to use
// a slow-motion camera to see where the program counter (PC) is referring to as
// the faulting instruction site.
//

// memcpy_16bit() and memcpy_32bit() are
// adapted from KNNSpeed's "AVX Memmove":
// https://github.com/KNNSpeed/AVX-Memmove
// V1.3875, 1/4/2020

// 'len' is number of bytes to copy
void * memcpy_8bit(void *dest, const void *src, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  unsigned int scratch_reg;

  asm volatile (
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = *src++
    "mov.b @%[in]+, %[scratch]\n\t" // (LS)
    "nop\n\t" // (MT)
    "dt %[size]\n\t" // while(len--) (EX)
    "mov.b %[scratch], @%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #1, %[out]\n\t" // (EX)
    : [in] "+&r" ((unsigned int)src), [out] "+r" ((unsigned int)dest), [size] "+&r" (len), [scratch] "=&r" (scratch_reg) // outputs
    : // inputs
    : "t", "memory" // clobbers
  );

  return ret_dest;
}

// 'len' is number of sets of 16-bits (2 bytes), or total bytes to copy / 2
void * memcpy_16bit(void *dest, const void *src, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  unsigned int scratch_reg;

  asm volatile (
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = *src++
    "mov.w @%[in]+, %[scratch]\n\t" // (LS)
    "nop\n\t" // (MT)
    "dt %[size]\n\t" // while(len--) (EX)
    "mov.w %[scratch], @%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #2, %[out]\n\t" // (EX)
    : [in] "+&r" ((unsigned int)src), [out] "+r" ((unsigned int)dest), [size] "+&r" (len), [scratch] "=&r" (scratch_reg) // outputs
    : // inputs
    : "t", "memory" // clobbers
  );

  return ret_dest;
}

// 'len' is number of sets of 32-bits (4 bytes), or total bytes to copy / 4
void * memcpy_32bit(void *dest, const void *src, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  unsigned int scratch_reg;

  asm volatile (
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = *src++
    "mov.l @%[in]+, %[scratch]\n\t" // (LS)
    "nop\n\t" // (MT)
    "dt %[size]\n\t" // while(len--) (EX)
    "mov.l %[scratch], @%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #4, %[out]\n\t" // (EX)
    : [in] "+&r" ((unsigned int)src), [out] "+r" ((unsigned int)dest), [size] "+&r" (len), [scratch] "=&r" (scratch_reg) // outputs
    : // inputs
    : "t", "memory" // clobbers
  );

  return ret_dest;
}

// From DreamHAL: https://github.com/Moopthehedgehog/DreamHAL

// 64-bit (8 bytes at a time)
// Len is (# of total bytes/8), so it's "# of 64-bits"
// Source and destination buffers must both be 8-byte aligned

void * memcpy_64bit(void *dest, const void *src, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  asm volatile (
    "fschg\n\t" // Switch to pair move mode (FE)
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = *src++
    "fmov.d @%[in]+, DR0\n\t" // (LS)
    "nop\n\t" // (MT)
    "dt %[size]\n\t" // while(len--) (EX)
    "fmov.d DR0, @%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #8, %[out]\n\t" // (EX)
    "fschg\n" // Switch back to single move mode (FE)
    : [in] "+&r" ((unsigned int)src), [out] "+r" ((unsigned int)dest), [size] "+&r" (len) // outputs
    : // inputs
    : "t", "dr0", "memory" // clobbers
  );

  return ret_dest;
}

// 32 Bytes at a time
// Len is (# of total bytes/32), so it's "# of 32 Bytes"
// Source and destination buffers must both be 8-byte aligned to not crash.
// NOTE: Store Queues are better, but require 32-byte alignment; this is just here
// for if you really need it (dcload can make use of it for sure).
// Prefetching has been removed from this function, as it requires 32-byte alignment.
// That's a lot of space wasted for 32-byte alignment, versus the 2 bytes lost in the packet buffers for 8-byte alignment.
// dcload is also a very space-constrained program...
void * memcpy_32Bytes(void *dest, const void *src, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  asm volatile (
    "fschg\n\t" // Switch to pair move mode (FE)
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = *src++
    "fmov.d @%[in]+, DR0\n\t" // (LS)
    "fmov.d @%[in]+, DR2\n\t" // (LS)
    "fmov.d @%[in]+, DR4\n\t" // (LS)
		"add #32, %[out]\n\t" // (EX)
    "fmov.d @%[in]+, DR6\n\t" // (LS)
		"dt %[size]\n\t" // while(len--) (EX)
    "fmov.d DR6, @-%[out]\n\t" // (LS)
    "fmov.d DR4, @-%[out]\n\t" // (LS)
    "fmov.d DR2, @-%[out]\n\t" // (LS)
    "fmov.d DR0, @-%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #32, %[out]\n\t" // (EX)
    "fschg\n" // Switch back to single move mode (FE)
    : [in] "+&r" ((unsigned int)src), [out] "+&r" ((unsigned int)dest), [size] "+&r" (len) // outputs
    : // inputs
    : "t", "dr0", "dr2", "dr4", "dr6", "memory" // clobbers
  );

  return ret_dest;
}

// Set 8 bytes of 0 at a time
// Len is (# of total bytes/8), so it's "# of 64-bits"
// Destination must be 8-byte aligned

void * memset_zeroes_64bit(void *dest, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  asm volatile (
    "fldi0 fr0\n\t"
    "fldi0 fr1\n\t"
    "fschg\n\t" // Switch to pair move mode (FE)
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = val
    "dt %[size]\n\t" // while(len--) (EX)
    "fmov.d DR0, @%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #8, %[out]\n\t" // (EX)
    "fschg\n" // Switch back to single move mode (FE)
    : [out] "+r" ((unsigned int)dest), [size] "+&r" (len) // outputs
    : // no inputs
    : "t", "fr0", "fr1", "memory" // clobbers
  );

  return ret_dest;
}

// memcmp_16bit_eq() and memcmp_32bit_eq() are
// adapted from KNNSpeed's "AVX Memmove":
// https://github.com/KNNSpeed/AVX-Memmove
// V1.3875, 1/4/2020

// Equality-only version of memcmp_16bit
// 'count' is number of sets of 16-bits (2 bytes), or total bytes to compare / 2
// Returns 0 if equal, -1 if unequal (-1 is true in C)
int memcmp_16bit_eq(const void *str1, const void *str2, unsigned int count)
{
  const unsigned short *s1 = (unsigned short*)str1;
  const unsigned short *s2 = (unsigned short*)str2;

  while (count--)
  {
    if (*s1++ != *s2++)
    {
      return -1;
    }
  }
  return 0;
}

// Equality-only version of memcmp_32bit
// 'count' is number of sets of 32-bits (4 bytes), or total bytes to compare / 4
// Returns 0 if equal, -1 if unequal (-1 is true in C)
int memcmp_32bit_eq(const void *str1, const void *str2, unsigned int count)
{
  const unsigned int *s1 = (unsigned int*)str1;
  const unsigned int *s2 = (unsigned int*)str2;

  while (count--)
  {
    if (*s1++ != *s2++)
    {
      return -1;
    }
  }
  return 0;
}

// SH4_aligned_memcpy() is based on
// AVX_memcpy() from KNNSpeed's "AVX Memmove":
// https://github.com/KNNSpeed/AVX-Memmove
// V1.3875, 1/4/2020

// General-purpose memcpy function to call
// After modifying dcload-ip accordingly, it can be safely assumed that all src
// pointers are aligned to 8 bytes.
// 'numbytes' is total number of bytes to copy.
void * SH4_aligned_memcpy(void *dest, void *src, unsigned int numbytes)
{
  void * returnval = dest;
	unsigned int offset = 0;

  if((char*)src == (char*)dest)
  {
    // Lol.
    return returnval;
  }

	while(numbytes)
	{
		if( // Check 8-byte alignment for 32-byte copy (no prefetching)
				( !( ((unsigned int)src | (unsigned int)dest) & 0x07) )
				&&
				(numbytes >= 32)
	    )
	  {
	    memcpy_32Bytes(dest, src, numbytes >> 5);
			offset = numbytes & -32;
			dest = (char *)dest + offset;
	    src = (char *)src + offset;
			numbytes -= offset;
	  }
		else if( // Check 8-byte alignment for 64-bit copy
//	  if( // Check 8-byte alignment for 64-bit copy
	      ( !( ((unsigned int)src | (unsigned int)dest) & 0x07) )
	      &&
				(numbytes >= 8)
	    )
	  {
	    memcpy_64bit(dest, src, numbytes >> 3);
			offset = numbytes & -8;
			dest = (char *)dest + offset;
      src = (char *)src + offset;
			numbytes -= offset;
	  }
		else if( // Check 4-byte alignment
				( !( ((unsigned int)src | (unsigned int)dest) & 0x03) )
				&&
				(numbytes >= 4)
			)
		{
			memcpy_32bit(dest, src, numbytes >> 2);
			offset = numbytes & -4;
			dest = (char *)dest + offset;
      src = (char *)src + offset;
			numbytes -= offset;
		}
		else if( // Check 2-byte alignment
				( !( ((unsigned int)src | (unsigned int)dest) & 0x01) )
				&&
				(numbytes >= 2)
			)
		{
			memcpy_16bit(dest, src, numbytes >> 1);
			offset = numbytes & -2;
			dest = (char *)dest + offset;
      src = (char *)src + offset;
			numbytes -= offset;
		}
		else if(numbytes) // No alignment? Well, that really stinks!
		{
			memcpy_8bit(dest, src, numbytes);
			numbytes = 0;
		}
	}

  return returnval;
}

// Same thing but for BBA pktcpy, which can't do 8-byte accesses

void * memcpy_32Bytes_4B(void *dest, const void *src, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  unsigned int scratch_reg;
  unsigned int scratch_reg2;
  unsigned int scratch_reg3;
  unsigned int scratch_reg4;
  unsigned int scratch_reg5;
  unsigned int scratch_reg6;
  unsigned int scratch_reg7;
  unsigned int scratch_reg8;

  asm volatile (
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = *src++
    "mov.l @%[in]+, %[scratch]\n\t" // (LS)
    "mov.l @%[in]+, %[scratch2]\n\t" // (LS)
    "mov.l @%[in]+, %[scratch3]\n\t" // (LS)
    "mov.l @%[in]+, %[scratch4]\n\t" // (LS)
    "mov.l @%[in]+, %[scratch5]\n\t" // (LS)
    "mov.l @%[in]+, %[scratch6]\n\t" // (LS)
    "mov.l @%[in]+, %[scratch7]\n\t" // (LS)
		"add #32, %[out]\n\t" // (EX)
    "mov.l @%[in]+, %[scratch8]\n\t" // (LS)
		"dt %[size]\n\t" // while(len--) (EX)
    "mov.l %[scratch8], @-%[out]\n\t" // (LS)
    "mov.l %[scratch7], @-%[out]\n\t" // (LS)
    "mov.l %[scratch6], @-%[out]\n\t" // (LS)
    "mov.l %[scratch5], @-%[out]\n\t" // (LS)
    "mov.l %[scratch4], @-%[out]\n\t" // (LS)
    "mov.l %[scratch3], @-%[out]\n\t" // (LS)
    "mov.l %[scratch2], @-%[out]\n\t" // (LS)
    "mov.l %[scratch], @-%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #32, %[out]\n\t" // (EX)
    : [in] "+&r" ((unsigned int)src), [out] "+&r" ((unsigned int)dest), [size] "+&r" (len),
    [scratch] "=&r" (scratch_reg), [scratch2] "=&r" (scratch_reg2), [scratch3] "=&r" (scratch_reg3), [scratch4] "=&r" (scratch_reg4),
    [scratch5] "=&r" (scratch_reg5), [scratch6] "=&r" (scratch_reg6), [scratch7] "=&r" (scratch_reg7), [scratch8] "=&r" (scratch_reg8) // outputs
    : // inputs
    : "t", "memory" // clobbers
  );

  return ret_dest;
}

// 'len' is number of sets of 8 bytes, or total bytes to copy / 8
void * memcpy_64bit_4B(void *dest, const void *src, unsigned int len)
{
  void * ret_dest = dest;

  if(!len)
  {
    return ret_dest;
  }

  unsigned int scratch_reg;
  unsigned int scratch_reg2;

  asm volatile (
    "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
  "1:\n\t"
    // *dest++ = *src++
    "mov.l @%[in]+, %[scratch]\n\t" // (LS)
    "add #8, %[out]\n\t" // (EX)
    "mov.l @%[in]+, %[scratch2]\n\t" // (LS)
    "dt %[size]\n\t" // while(len--) (EX)
    "mov.l %[scratch2], @-%[out]\n\t" // (LS)
    "mov.l %[scratch], @-%[out]\n\t" // (LS)
    "bf.s 1b\n\t" // (BR)
    " add #8, %[out]\n\t" // (EX)
    : [in] "+&r" ((unsigned int)src), [out] "+r" ((unsigned int)dest), [size] "+&r" (len), [scratch] "=&r" (scratch_reg), [scratch2] "=&r" (scratch_reg2) // outputs
    : // inputs
    : "t", "memory" // clobbers
  );

  return ret_dest;
}

// 4-byte aligned analog of SH4_aligned_memcpy
void * SH4_aligned_pktcpy(void *dest, void *src, unsigned int numbytes)
{
  void * returnval = dest;
	unsigned int offset = 0;

  if((char*)src == (char*)dest)
  {
    // Lol.
    return returnval;
  }

	while(numbytes)
	{
    if( // Check 4-byte alignment for 32-byte copy (no prefetching)
        ( !( ((unsigned int)src | (unsigned int)dest) & 0x03) )
        &&
        (numbytes >= 32)
      )
    {
      memcpy_32Bytes_4B(dest, src, numbytes >> 5);
      offset = numbytes & -32;
      dest = (char *)dest + offset;
      src = (char *)src + offset;
      numbytes -= offset;
    }
    else if( // Check 4-byte alignment for 8 bytes at a time
        ( !( ((unsigned int)src | (unsigned int)dest) & 0x03) )
        &&
        (numbytes >= 8)
      )
    {
      memcpy_64bit_4B(dest, src, numbytes >> 3);
      offset = numbytes & -8;
      dest = (char *)dest + offset;
      src = (char *)src + offset;
      numbytes -= offset;
    }
 		else if( // Check 4-byte alignment
				( !( ((unsigned int)src | (unsigned int)dest) & 0x03) )
				&&
				(numbytes >= 4)
			)
		{
			memcpy_32bit(dest, src, numbytes >> 2);
			offset = numbytes & -4;
			dest = (char *)dest + offset;
      src = (char *)src + offset;
			numbytes -= offset;
		}
		else if( // Check 2-byte alignment
				( !( ((unsigned int)src | (unsigned int)dest) & 0x01) )
				&&
				(numbytes >= 2)
			)
		{
			memcpy_16bit(dest, src, numbytes >> 1);
			offset = numbytes & -2;
			dest = (char *)dest + offset;
      src = (char *)src + offset;
			numbytes -= offset;
		}
		else if(numbytes) // No alignment? Well, that really stinks!
		{
			memcpy_8bit(dest, src, numbytes);
			numbytes = 0;
		}
	}

  return returnval;
}
