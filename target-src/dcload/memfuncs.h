#ifndef __MEMFUNCS_H__
#define __MEMFUNCS_H__

// See memfuncs.c file for details

void * memcpy_8bit(void *dest, const void *src, unsigned int len);
void * memcpy_16bit(void *dest, const void *src, unsigned int len);
void * memcpy_32bit(void *dest, const void *src, unsigned int len);
void * memcpy_64bit(void *dest, const void *src, unsigned int len);
void * memcpy_64bit_32Bytes(void *dest, const void *src, unsigned int len);

void * memset_zeroes_64bit(void *dest, unsigned int len);
void * memset_zeroes_32bit(void *dest, unsigned int len);

int memcmp_16bit_eq(const void *str1, const void *str2, unsigned int count);
int memcmp_32bit_eq(const void *str1, const void *str2, unsigned int count);

void * SH4_aligned_memcpy(void *dest, void *src, unsigned int numbytes);

void * memcpy_32bit_16Bytes(void *dest, const void *src, unsigned int len);
void * SH4_aligned_pktcpy(void *dest, void *src, unsigned int numbytes);

void * SH4_pkt_to_mem(void *dest, void *src, unsigned int numbytes_div_4);
void * SH4_mem_to_pkt(void *dest, void *src, unsigned int numbytes_div_4);

void * SH4_mem_to_pkt_movca(void *dest, void *src, unsigned int numbytes_div_4);
void * SH4_mem_to_pkt_16_movca(void *dest, void *src, unsigned int numbytes_div_16);

void * SH4_mem_to_pkt_X_movca(void *dest, void *src, unsigned int numbytes_div_4);
void * SH4_mem_to_pkt_X_movca_8(void *dest, void *src, unsigned int numbytes_div_8);
void * SH4_mem_to_pkt_X_movca_12(void *dest, void *src, unsigned int numbytes_div_12);
void * SH4_mem_to_pkt_X_movca_16(void *dest, void *src, unsigned int numbytes_div_16);
void * SH4_mem_to_pkt_X_movca_32(void *dest, void *src, unsigned int numbytes);


void * SH4_pkt_to_mem_X_movca(void *dest, void *src, unsigned int numbytes_div_4);
void * SH4_pkt_to_mem_X_movca_8(void *dest, void *src, unsigned int numbytes);
void * SH4_pkt_to_mem_X_movca_16(void *dest, void *src, unsigned int numbytes);
void * SH4_pkt_to_mem_X_movca_32(void *dest, void *src, unsigned int numbytes);
void * SH4_pkt_to_mem_X_movca_32_linear(void *dest, void *src, unsigned int numbytes);


void * SH4_pkt_to_mem_16(void *dest, void *src, unsigned int numbytes_div_16);
void * SH4_mem_to_pkt_16(void *dest, void *src, unsigned int numbytes_div_16);

// Write back cache block and invalidate it
static inline void CacheBlockPurge(unsigned char * __32_byte_base, unsigned int __32_byte_count)
{
	unsigned int __32_byte_ptr = (unsigned int)__32_byte_base;

	while(__32_byte_count)
	{
		asm volatile ("ocbp @%[ptr]\n"
			: // outputs
			: [ptr] "r" (__32_byte_ptr) // inputs
			: "memory" // clobbers memory
		);

		__32_byte_count -= 1;
		__32_byte_ptr += 32;
	}
}

// Write back cache block, don't invalidate it
static inline void CacheBlockWriteBack(unsigned char * __32_byte_base, unsigned int __32_byte_count)
{
	unsigned int __32_byte_ptr = (unsigned int)__32_byte_base;

	while(__32_byte_count)
	{
		asm volatile ("ocbwb @%[ptr]\n"
			: // outputs
			: [ptr] "r" (__32_byte_ptr) // inputs
			: "memory" // clobbers memory
		);

		__32_byte_count -= 1;
		__32_byte_ptr += 32;
	}
}

// Invalidate cache only, don't write the blocks back
static inline void CacheBlockInvalidate(unsigned char * __32_byte_base, unsigned int __32_byte_count)
{
	unsigned int __32_byte_ptr = (unsigned int)__32_byte_base;

	while(__32_byte_count)
	{
		asm volatile ("ocbi @%[ptr]\n"
			: // outputs
			: [ptr] "r" (__32_byte_ptr) // inputs
			: // clobbers
		);

		__32_byte_count -= 1;
		__32_byte_ptr += 32;
	}
}

#endif
