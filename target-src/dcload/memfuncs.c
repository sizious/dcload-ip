
#include "memfuncs.h"

//
// Special functions to speed up packet processing
//
// These all require appropriate alignment of source and destination.
//
// Using __attribute__((aligned(*size*))) where size is 2, 4, or 8 is very
// important, otherwise these functions *will* crash. If the crash is in a
// critical pathway, the exception handler may also crash and you'll need to use
// a slow-motion camera to see where the program counter (PC) is referring to as
// the faulting instruction site (if you're lucky enough to even have that show
// up at all).
//

static unsigned int memdiff(const void *dst, const void *src) {
    return ((unsigned int)dst & 0x1fffffff) - ((unsigned int)src & 0x1fffffff);
}

//
// From DreamHAL
//

// 8-bit (1 bytes at a time)
// Len is (# of total bytes/1), so it's "# of 8-bits"
// Source and destination buffers must both be 1-byte aligned (aka no alignment)
void *memcpy_8bit(void *dest, const void *src, unsigned int len) {
    if(!len) {
        return dest;
    }

    const char *s = (char *)src;
    char *d = (char *)dest;

    unsigned int diff = memdiff(d, s + 1); // extra offset because input gets incremented
                                           // before output is calculated
    // Underflow would be like adding a negative offset

    // Can use 'd' as a scratch reg now
    asm volatile(
        "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
        ".align 2\n"
        "0:\n\t"
        "dt %[size]\n\t"                           // (--len) ? 0 -> T : 1 -> T (EX 1)
        "mov.b @%[in]+, %[scratch]\n\t"            // scratch = *(s++) (LS 1/2)
        "bf.s 0b\n\t"                              // while(s != nexts) aka while(!T) (BR 1/2)
        " mov.b %[scratch], @(%[offset], %[in])\n" // *(datatype_of_s*) ((char*)s + diff) = scratch,
                                                   // where src + diff = dest (LS 1)
        :
        [in] "+&r"((unsigned int)s), [scratch] "=&r"((unsigned int)d), [size] "+&r"(len) // outputs
        : [offset] "z"(diff)                                                             // inputs
        : "t", "memory"                                                                  // clobbers
    );

    return dest;
}

// 16-bit (2 bytes at a time)
// Len is (# of total bytes/2), so it's "# of 16-bits"
// Source and destination buffers must both be 2-byte aligned

void *memcpy_16bit(void *dest, const void *src, unsigned int len) {
    if(!len) {
        return dest;
    }

    const unsigned short *s = (unsigned short *)src;
    unsigned short *d = (unsigned short *)dest;

    unsigned int diff = memdiff(d, s + 1); // extra offset because input gets incremented
                                           // before output is calculated
    // Underflow would be like adding a negative offset

    // Can use 'd' as a scratch reg now
    asm volatile(
        "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
        ".align 2\n"
        "0:\n\t"
        "dt %[size]\n\t"                           // (--len) ? 0 -> T : 1 -> T (EX 1)
        "mov.w @%[in]+, %[scratch]\n\t"            // scratch = *(s++) (LS 1/2)
        "bf.s 0b\n\t"                              // while(s != nexts) aka while(!T) (BR 1/2)
        " mov.w %[scratch], @(%[offset], %[in])\n" // *(datatype_of_s*) ((char*)s + diff) = scratch,
                                                   // where src + diff = dest (LS 1)
        :
        [in] "+&r"((unsigned int)s), [scratch] "=&r"((unsigned int)d), [size] "+&r"(len) // outputs
        : [offset] "z"(diff)                                                             // inputs
        : "t", "memory"                                                                  // clobbers
    );

    return dest;
}

// 32-bit (4 bytes at a time)
// Len is (# of total bytes/4), so it's "# of 32-bits"
// Source and destination buffers must both be 4-byte aligned

void *memcpy_32bit(void *dest, const void *src, unsigned int len) {
    if(!len) {
        return dest;
    }

    const unsigned int *s = (unsigned int *)src;
    unsigned int *d = (unsigned int *)dest;

    unsigned int diff = memdiff(d, s + 1); // extra offset because input gets incremented
                                           // before output is calculated
    // Underflow would be like adding a negative offset

    // Can use 'd' as a scratch reg now
    asm volatile(
        "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
        ".align 2\n"
        "0:\n\t"
        "dt %[size]\n\t"                           // (--len) ? 0 -> T : 1 -> T (EX 1)
        "mov.l @%[in]+, %[scratch]\n\t"            // scratch = *(s++) (LS 1/2)
        "bf.s 0b\n\t"                              // while(s != nexts) aka while(!T) (BR 1/2)
        " mov.l %[scratch], @(%[offset], %[in])\n" // *(datatype_of_s*) ((char*)s + diff) = scratch,
                                                   // where src + diff = dest (LS 1)
        :
        [in] "+&r"((unsigned int)s), [scratch] "=&r"((unsigned int)d), [size] "+&r"(len) // outputs
        : [offset] "z"(diff)                                                             // inputs
        : "t", "memory"                                                                  // clobbers
    );

    return dest;
}

// 64-bit (8 bytes at a time)
// Len is (# of total bytes/8), so it's "# of 64-bits"
// Source and destination buffers must both be 8-byte aligned

void *memcpy_64bit(void *dest, const void *src, unsigned int len) {
    if(!len) {
        return dest;
    }

    const _Complex float *s = (_Complex float *)src;
    _Complex float *d = (_Complex float *)dest;

    _Complex float double_scratch;

    unsigned int diff = memdiff(d, s + 1); // extra offset because input gets incremented
                                           // before output is calculated
    // Underflow would be like adding a negative offset

    asm volatile(
        "fschg\n\t"
        "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
        ".align 2\n"
        "0:\n\t"
        "dt %[size]\n\t"                              // (--len) ? 0 -> T : 1 -> T (EX 1)
        "fmov.d @%[in]+, %[scratch]\n\t"              // scratch = *(s++) (LS 1/2)
        "bf.s 0b\n\t"                                 // while(s != nexts) aka while(!T) (BR 1/2)
        " fmov.d %[scratch], @(%[offset], %[in])\n\t" // *(datatype_of_s*) ((char*)s + diff) =
                                                      // scratch, where src + diff = dest (LS 1)
        "fschg\n"
        : [in] "+&r"((unsigned int)s), [scratch] "=&d"(double_scratch), [size] "+&r"(len) // outputs
        : [offset] "z"(diff)                                                              // inputs
        : "t", "memory" // clobbers
    );

    return dest;
}

// 32 Bytes at a time
// Len is (# of total bytes/32), so it's "# of 32 Bytes"
// Source and destination buffers must both be 8-byte aligned

void *memcpy_64bit_32Bytes(void *dest, const void *src, unsigned int len) {
    void *ret_dest = dest;

    if(!len) {
        return ret_dest;
    }

    _Complex float double_scratch;
    _Complex float double_scratch2;
    _Complex float double_scratch3;
    _Complex float double_scratch4;

    asm volatile(
        "fschg\n\t" // Switch to pair move mode (FE)
        "clrs\n"    // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
        ".align 2\n"
        "1:\n\t"
        // *dest++ = *src++
        "fmov.d @%[in]+, %[scratch]\n\t"   // (LS)
        "fmov.d @%[in]+, %[scratch2]\n\t"  // (LS)
        "fmov.d @%[in]+, %[scratch3]\n\t"  // (LS)
        "add #32, %[out]\n\t"              // (EX)
        "fmov.d @%[in]+, %[scratch4]\n\t"  // (LS)
        "dt %[size]\n\t"                   // while(--len) (EX)
        "fmov.d %[scratch4], @-%[out]\n\t" // (LS)
        "fmov.d %[scratch3], @-%[out]\n\t" // (LS)
        "fmov.d %[scratch2], @-%[out]\n\t" // (LS)
        "fmov.d %[scratch], @-%[out]\n\t"  // (LS)
        "bf.s 1b\n\t"                      // (BR)
        " add #32, %[out]\n\t"             // (EX)
        "fschg\n"                          // Switch back to single move mode (FE)
        : [in] "+&r"((unsigned int)src), [out] "+&r"((unsigned int)dest), [size] "+&r"(len),
          [scratch] "=&d"(double_scratch), [scratch2] "=&d"(double_scratch2),
          [scratch3] "=&d"(double_scratch3), [scratch4] "=&d"(double_scratch4) // outputs
        :                                                                      // inputs
        : "t", "memory"                                                        // clobbers
    );

    return ret_dest;
}

// Set 8 bytes of 0 at a time
// Len is (# of total bytes/8), so it's "# of 64-bits"
// Destination must be 8-byte aligned

void *memset_zeroes_64bit(void *dest, unsigned int len) {
    if(!len) {
        return dest;
    }

    _Complex float * d = to_p1(dest);
    _Complex float *nextd = d + len;

    asm volatile(
        "fldi0 fr0\n\t"
        "fldi0 fr1\n\t"
        "fschg\n\t"      // Switch to pair move mode (FE)
        "dt %[size]\n\t" // Decrement and test size here once to prevent extra jump (EX 1)
        "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy Rn
        ".align 2\n"
        "1:\n\t"
        // *--nextd = val
        "fmov.d DR0, @-%[out]\n\t"                           // (LS 1/1)
        "bf.s 1b\n\t"                                        // (BR 1/2)
        " dt %[size]\n\t"                                    // (--len) ? 0 -> T : 1 -> T (EX 1)
        "fschg\n"                                            // Switch back to single move mode (FE)
        : [out] "+r"((unsigned int)nextd), [size] "+&r"(len) // outputs
        :                                                    // inputs
        : "t", "fr0", "fr1", "memory"                        // clobbers
    );

    return dest;
}

// Equality-only version of memcmp_16bit
// 'count' is number of sets of 16-bits (2 bytes), or total bytes to compare / 2
// Returns 0 if equal, -1 if unequal (-1 is true in C)
int memcmp_16bit_eq(const void *str1, const void *str2, unsigned int count) {
    const unsigned short *s1 = (unsigned short *)str1;
    const unsigned short *s2 = (unsigned short *)str2;

    while(count--) {
        if(*s1++ != *s2++) {
            return -1;
        }
    }
    return 0;
}

// Equality-only version of memcmp_32bit
// 'count' is number of sets of 32-bits (4 bytes), or total bytes to compare / 4
// Returns 0 if equal, -1 if unequal (-1 is true in C)
int memcmp_32bit_eq(const void *str1, const void *str2, unsigned int count) {
    const unsigned int *s1 = (unsigned int *)str1;
    const unsigned int *s2 = (unsigned int *)str2;

    while(count--) {
        if(*s1++ != *s2++) {
            return -1;
        }
    }
    return 0;
}

// General-purpose memcpy function to call
// After modifying dcload-ip accordingly, it can be safely assumed that all src
// pointers are aligned to 8 bytes.
// 'numbytes' is total number of bytes to copy.
void *SH4_aligned_memcpy(void *dest, void *src, unsigned int numbytes) {
    void *returnval = dest;
    unsigned int offset = 0;

    if((char *)src == (char *)dest) {
        // Lol.
        return returnval;
    }

    while(numbytes) {
        if( // Check 8-byte alignment for 32-byte copy
            (!(((unsigned int)src | (unsigned int)dest) & 0x07)) && (numbytes >= 32)) {
            memcpy_64bit_32Bytes(dest, src, numbytes >> 5);
            offset = numbytes & -32;
            dest = (char *)dest + offset;
            src = (char *)src + offset;
            numbytes -= offset;
        }
        else if( // Check 8-byte alignment for 64-bit copy
                   //	  if( // Check 8-byte alignment for 64-bit copy
            (!(((unsigned int)src | (unsigned int)dest) & 0x07)) && (numbytes >= 8)) {
            memcpy_64bit(dest, src, numbytes >> 3);
            offset = numbytes & -8;
            dest = (char *)dest + offset;
            src = (char *)src + offset;
            numbytes -= offset;
        } else if( // Check 4-byte alignment
            (!(((unsigned int)src | (unsigned int)dest) & 0x03)) && (numbytes >= 4)) {
            memcpy_32bit(dest, src, numbytes >> 2);
            offset = numbytes & -4;
            dest = (char *)dest + offset;
            src = (char *)src + offset;
            numbytes -= offset;
        }
        else if( // Check 2-byte alignment
            (!(((unsigned int)src | (unsigned int)dest) & 0x01)) && (numbytes >= 2)) {
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

// Offset RAM buffer to BBA
// 32 bytes per loop, takes full numbytes
void *SH4_mem_to_pkt_X_movca_32(void *dest, void *src, unsigned int numbytes) {
    void *ret_dest = dest;

    if(!numbytes) {
        return ret_dest;
    }

    const unsigned int *s = (unsigned int *)src;
    unsigned int *d = (unsigned int *)dest;

    unsigned int scratch_reg;
    unsigned int scratch_reg2;
    unsigned int scratch_reg3;
    unsigned int scratch_reg4;
    unsigned int scratch_reg5;
    unsigned int scratch_reg6;
    unsigned int scratch_reg7;
    unsigned int scratch_reg8;
    unsigned int scratch_reg9;
    unsigned int scratch_reg10;

    asm volatile(
        "clrs\n\t" // Align for parallelism (CO 1)

        "mov %[in], %[scratch_A]\n\t" // Need to pref 2nd block ASAP (MT 0)
        "add #32, %[scratch_A]\n\t" // set up address in next block (EX 1) - flow dependency special
                                    // case

        "mov %[out], %[out2]\n\t" // preload 'out2' (MT 0)
        "pref @%[scratch_A]\n\t"  // pref 2nd block now (LS 1)

        "add #4, %[out2]\n\t" // offset out2 (EX 1)
        "clrt\n\t"            // clear the T bit (MT 1)

        "mov #-5, %[scratch_B]\n\t"       // integer divide numbytes by 32 (1/2) (EX 1)
        "mov.l @%[in]+, %[scratch_Z]\n\t" // load up first longword (leading zeros, or garbage
                                          // padding, + 2 bytes of packet data) (LS 1/2)

        "add #31, %[size]\n\t" // Add extra 31 to account for all data in the last 32 bytes if size
                               // is not an exact multiple of 32 (EX 1)
        "bf.s 1f\n\t"          // Branch to ensure dual-issuing is aligned as desired (BR 1/2)
        " shld %[scratch_B], %[size]\n" // integer divide numbytes by 32, -5 means shift right by 5
                                        // (2/2) (EX 1)
        // When writing to the transmit buffer, we can write some extra garbage; it's 2kB and we
        // give the RTL a length parameter anyways.
        ".align 5\n" // Align to 32 bytes
        "1:\n\t"
        "mov.l @%[in]+, %[scratch_A]\n\t" // Read 32-bit int A (LS 1/2)
        "dt %[size]\n\t"                  // (--numbytes_div_32 == 0) ? 1 -> T : 0 -> T (EX 1)

        "mov.l @%[in]+, %[scratch_B]\n\t"     // Read 32-bit int B (LS 1/2)
        "mov %[scratch_Z], %[scratch_R0]\n\t" // Copy scratch_Z into scratch_R0 for next pass (MT 0)

        "mov.l @%[in]+, %[scratch_C]\n\t" // Read 32-bit int C (LS 1/2)
        // Do this: 0xAAAA[AAAA:RRRR]RRRR -> 0xAAAARRRR with scratch_A:scratch_R0 -> scratch_R0 to
        // interleave lower 16 bits of scratch_A with prior value The result of this is equivalent
        // to "shll16 %[scratch_A]", "shlr16 %[scratch_R0]", "extu.w %[scratch_R0]", "add
        // %[scratch_A], %[scratch_R0]" except without destroying scratch_A.
        "xtrct %[scratch_A], %[scratch_R0]\n\t" // (EX 1)

        "mov.l @%[in]+, %[scratch_D]\n\t"      // Read 32-bit int D (LS 1/2)
        "xtrct %[scratch_B], %[scratch_A]\n\t" // (EX 1)

        "mov.l @%[in]+, %[scratch_W]\n\t"      // Read 32-bit int W (LS 1/2)
        "xtrct %[scratch_C], %[scratch_B]\n\t" // (EX 1)

        "mov.l @%[in]+, %[scratch_X]\n\t"      // Read 32-bit int X (LS 1/2)
        "xtrct %[scratch_D], %[scratch_C]\n\t" // (EX 1)

        "mov.l @%[in]+, %[scratch_Y]\n\t"      // Read 32-bit int Y (LS 1/2)
        "xtrct %[scratch_W], %[scratch_D]\n\t" // (EX 1)

        "mov.l @%[in]+, %[scratch_Z]\n\t"      // Read 32-bit int Z (LS 1/2)
        "xtrct %[scratch_X], %[scratch_W]\n\t" // (EX 1)

        // movca.l only writes to a cache block, whether it's read into opcache or not. So it
        // removes the step of opcache reading. It behaves like a normal mov.l for storing to
        // write-through/uncached memory. It also only writes data in R0... We do know that output
        // is always 32-byte aligned at this position on the bus, so we can optimize and use 1x
        // movca.l with 7x mov.l to minimize instruction count and register moves.
        "movca.l %[scratch_R0], @%[out]\n\t"   // Write data to out 0 R0 (LS 3-7)
        "xtrct %[scratch_Y], %[scratch_X]\n\t" // (EX 1)

        "mov %[in], %[scratch_R0]\n\t" // Reuse R0 - To make base of next cache block (MT 0)
        "add #28, %[scratch_R0]\n\t"   // Reuse R0 (EX 1) -- flow dependency special case

        "pref @%[scratch_R0]\n\t" // Reuse R0 - To prefetch the next src block here (LS 1)
        "xtrct %[scratch_Z], %[scratch_Y]\n\t" // (EX 1)

        "mov.l %[scratch_A], @%[out2]\n\t" // Write data to out2 4 A (LS 1)
        "add #8, %[out]\n\t"               // (EX 1)

        "mov.l %[scratch_B], @%[out]\n\t" // Write data to out 8 B (LS 1)
        "add #8, %[out2]\n\t"             // (EX 1)

        "mov.l %[scratch_C], @%[out2]\n\t" // Write data to out2 12 C (LS 1)
        "add #8, %[out]\n\t"               // (EX 1)

        "mov.l %[scratch_D], @%[out]\n\t" // Write data to out 16 D (LS 1)
        "add #8, %[out2]\n\t"             // (EX 1)

        "mov.l %[scratch_W], @%[out2]\n\t" // Write data to out2 20 W (LS 1)
        "add #8, %[out]\n\t"               // (EX 1)

        "mov.l %[scratch_X], @%[out]\n\t" // Write data to out 24 X (LS 1)
        "add #8, %[out2]\n\t"             // (EX 1)

        "mov.l %[scratch_Y], @%[out2]\n\t" // Write data to out2 28 Y (LS 1)
        "add #8, %[out]\n\t"               // out is now 32 from prev out (EX 1)

        "mov %[out2], %[scratch_R0]\n\t" // Reuse R0 - Need to purge the cache block just written to
                                         // because 0x81848000 is volatile and used for both reads
                                         // and writes (MT 0)
        "add #-28, %[scratch_R0]\n\t"    // Reuse R0 (EX 1) -- flow dependency special case

        "ocbp @%[scratch_R0]\n\t" // Reuse R0 (LS 1-5)
        "bf.s 1b\n\t"             // (BR 1/2)
        " add #8, %[out2]\n"      // out2 is now 32 from prev out2 and offset by 4 from out (EX 1)

        : [in] "+&r"((unsigned int)s), [out] "+&r"((unsigned int)d),
          [scratch_R0] "=&z"(scratch_reg), [scratch_A] "=&r"(scratch_reg2),
          [scratch_B] "=&r"(scratch_reg3), [scratch_C] "=&r"(scratch_reg4),
          [scratch_D] "=&r"(scratch_reg5), [scratch_W] "=&r"(scratch_reg6),
          [scratch_X] "=&r"(scratch_reg7), [scratch_Y] "=&r"(scratch_reg8),
          [scratch_Z] "=&r"(scratch_reg9), [out2] "=&r"(scratch_reg10), [size] "+&r"(numbytes)
        :               // inputs
        : "t", "memory" // clobbers
    );

    return ret_dest;
}
