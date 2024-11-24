/*
 * This file is part of the dcload Dreamcast serial loader
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef __MINGW32__
#include <_mingw.h>
#include <windows.h>

/* Detect MinGW/MSYS vs. MinGW-w64/MSYS2 */
# ifdef __MINGW64_VERSION_MAJOR
#  define __RT_MINGW_W64__
# else
#  define __RT_MINGW_ORG__
# endif

#endif /* __MINGW32__ */

/*
 * Compatibility layer ('shim') for the original, legacy MinGW/MSYS environment.
 * This allow toolchains built on MinGW-w64/MSYS2 to be usable with MinGW/MSYS.
 * Mainly, this is for linking 'dc-tool' with 'libbfd'. Declaring these
 * functions will let us to build 'dc-tool' even if 'sh-elf' toolchain was built
 * on MinGW-w64/MSYS2.
 *
 * Of course this will work if the compiler used on MinGW-w64/MSYS2 and
 * MinGW/MSYS are in the same family (e.g., GCC 9.x on both environments).
 */
#ifdef __RT_MINGW_ORG__

// See: https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-crt/stdio/mingw_vasprintf.c
int vasprintf(char ** __restrict__ ret,
              const char * __restrict__ format,
              va_list ap) {
  int len;
  /* Get Length */
  len = __mingw_vsnprintf(NULL,0,format,ap);
  if (len < 0) return -1;
  /* +1 for \0 terminator. */
  *ret = malloc(len + 1);
  /* Check malloc fail*/
  if (!*ret) return -1;
  /* Write String */
  __mingw_vsnprintf(*ret,len+1,format,ap);
  /* Terminate explicitly */
  (*ret)[len] = '\0';
  return len;
}

// Thanks to Dietrich Epp
// See: https://stackoverflow.com/a/40160038
int __cdecl __MINGW_NOTHROW libintl_asprintf(char **strp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl __MINGW_NOTHROW libintl_vasprintf(char **restrict strp,
                                              const char *restrict fmt,
                                              va_list arg ) {
    return vasprintf(strp, fmt, arg);
}

// See: https://stackoverflow.com/a/60380005
int __cdecl __MINGW_NOTHROW __ms_vsnprintf(char *buffer, size_t count, const char *format, va_list argptr) {
    return __mingw_vsnprintf(buffer, count, format, argptr);
}

// Thanks to Kenji Uno and god
// See: https://github.com/HiraokaHyperTools/libacrt_iob_func
// See: https://stackoverflow.com/a/30894349
FILE * __cdecl __MINGW_NOTHROW _imp____acrt_iob_func(int handle) {
    switch (handle) {
        case 0: return stdin;
        case 1: return stdout;
        case 2: return stderr;
    }
    return NULL;
}

#endif /* __RT_MINGW_ORG__ */
