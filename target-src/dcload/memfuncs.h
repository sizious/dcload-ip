#ifndef __MEMFUNCS_H__
#define __MEMFUNCS_H__

// See memfuncs.c file for details

void * memcpy_8bit(void *dest, const void *src, unsigned int len);
void * memcpy_16bit(void *dest, const void *src, unsigned int len);
void * memcpy_32bit(void *dest, const void *src, unsigned int len);
void * memcpy_64bit(void *dest, const void *src, unsigned int len);
void * memcpy_32Bytes(void *dest, const void *src, unsigned int len);
void * memset_zeroes_64bit(void *dest, unsigned int len);
int memcmp_16bit_eq(const void *str1, const void *str2, unsigned int count);
int memcmp_32bit_eq(const void *str1, const void *str2, unsigned int count);
void * SH4_aligned_memcpy(void *dest, void *src, unsigned int numbytes);

void * memcpy_32Bytes_4B(void *dest, const void *src, unsigned int len);
void * memcpy_64bit_4B(void *dest, const void *src, unsigned int len);
void * SH4_aligned_pktcpy(void *dest, void *src, unsigned int numbytes);

#endif
