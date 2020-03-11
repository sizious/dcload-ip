/* Public Domain memcmp, memset, and memmove */
// GCC needs these functions (and memcpy) to exist somewhere when using -ffreestanding.
// These are just small ones that don't take up very much space. They aren't exactly the fastest
// ones as a result, but dcload has fast ones defined in adapter.h. :)

#include <string.h>

// dcload doesn't need a fancy one here (it already has two in adapter.h).
// Return values: -1 -> str1 is less, 0 -> equal, 1 -> str1 is greater.
int _DEFUN(memcmp, (str1, str2, count), _CONST _PTR str1 _AND _CONST _PTR str2 _AND size_t count)
{
  const unsigned char *s1 = (unsigned char *)str1;
  const unsigned char *s2 = (unsigned char *)str2;

  while (count--)
  {
    if (*s1++ != *s2++)
    {
      return s1[-1] < s2[-1] ? -1 : 1;
    }
  }

  return 0;
}

// Returns a pointer to dest
_PTR _DEFUN(memset, (dest, val, len), _PTR dest _AND int val _AND size_t len)
{
  unsigned char *ptr = (unsigned char*)dest;

  while (len--)
  {
    *ptr++ = val;
  }

  return dest;
}

// Returns a pointer to dest, and this *does* handle overlapping memory regions
_PTR _DEFUN(memmove, (dest, src, len), _PTR dest _AND _CONST _PTR src _AND size_t len)
{
  const char *s = (char *)src;
  char *d = (char *)dest;

  const char *nexts = s + len;
  char *nextd = d + len;

  if (d < s)
  {
    while (d != nextd)
    {
      *d++ = *s++;
    }
  }
  else
  {
    while (nextd != d)
    {
      *--nextd = *--nexts;
    }
  }
  return dest;
}
