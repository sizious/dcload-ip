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

void log_error( const char * prefix ) {
	perror( prefix );

#ifdef __MINGW32__
	DWORD dwError = WSAGetLastError();
	if ( dwError ) {
		TCHAR *err = NULL;
		FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
			NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&err, 0, NULL);
		fprintf(stderr, "WSAGetLastError: %d / %s\n", dwError, err);
		LocalFree(err);
	}
#endif
}

void cleanup_ip_address( char *hostname ) {
	int bufsize = strlen(hostname) + 1;

	char *buf = malloc(bufsize);
	memset(buf, '\0', bufsize);

	int i = 0,
		j = 0,
		leading_zero = 1,
		has_value = 0;

	while(i < strlen(hostname)) {
		char c = hostname[i];

		switch(c) {
			case '0':
				if (!leading_zero) {
					buf[j++] = c;
					has_value = 1;
				}
				break;
			case '.':
				leading_zero = 1;
				if (!has_value) {
					buf[j++] = '0';
				}
				buf[j++] = '.';
				has_value = 0;
				break;
			default:
				leading_zero = 0;
				buf[j++] = c;
				has_value = 1;
				break;
		}

		i++;
	}

	if (!has_value) {
		buf[j++] = '0';
	}

	strcpy(hostname, buf);
	free(buf);
}

/* converts expevt value to description, used by dc-tool exception processing */
char * exception_code_to_string(unsigned int expevt)
{
	switch(expevt) {
	case 0x1e0:
		return "User break\n";
		break;
	case 0x0e0:
		return "Address error (read)\n";
		break;
	case 0x040:
		return "TLB miss exception (read)\n";
		break;
	case 0x0a0:
		return "TLB protection violation exception (read)\n";
		break;
	case 0x180:
		return "General illegal instruction exception\n";
		break;
	case 0x1a0:
		return "Slot illegal instruction exception\n";
		break;
	case 0x800:
		return "General FPU disable exception\n";
		break;
	case 0x820:
		return "Slot FPU disable exception\n";
		break;
	case 0x100:
		return "Address error (write)\n";
		break;
	case 0x060:
		return "TLB miss exception (write)\n";
		break;
	case 0x0c0:
		return "TLB protection violation exception (write)\n";
		break;
	case 0x120:
		return "FPU exception\n";
		break;
	case 0x080:
		return "Initial page write exception\n";
		break;
	case 0x160:
		return "Unconditional trap (TRAPA)\n";
		break;
	default:
		return "Unknown exception\n";
		break;
	}
}

// Exception label array
const char * const exception_label_array[66] = {"PC  ", "PR  ", "SR  ", "GBR ", \
"VBR ", "DBR ", "MACH", "MACL", "R0B0", "R1B0", "R2B0", "R3B0", "R4B0", "R5B0", \
"R6B0", "R7B0", "R0B1", "R1B1", "R2B1", "R3B1", "R4B1", "R5B1", "R6B1", "R7B1", \
"R8  ", "R9  ", "R10 ", "R11 ", "R12 ", "R13 ", "R14 ", "R15 ", "FPSC", "FR0 ", \
"FR1 ", "FR2 ", "FR3 ", "FR4 ", "FR5 ", "FR6 ", "FR7 ", "FR8 ", "FR9 ", "FR10", \
"FR11", "FR12", "FR13", "FR14", "FR15", "FPUL", "XF0 ", "XF1 ", "XF2 ", "XF3 ", \
"XF4 ", "XF5 ", "XF6 ", "XF7 ", "XF8 ", "XF9 ", "XF10", "XF11", "XF12", "XF13", \
"XF14", "XF15"};

#ifdef __RT_MINGW_ORG__

/*
 * Compatibility layer for original, legacy MinGW/MSYS environment.
 * This allow toolchains built on MinGW-w64/MSYS2 to be usable with MinGW/MSYS.
 * Mainly, this is for linking 'dc-tool' with 'libbfd'. Declaring these
 * functions will let us to build 'dc-tool' even if 'sh-elf' toolchain was built
 * on MinGW-w64/MSYS2.
 */

// Thanks to Dietrich Epp
// See: https://stackoverflow.com/a/40160038
int vasprintf(char **strp, const char *fmt, va_list ap) {    
    int len = _vscprintf(fmt, ap);
    if (len == -1) {
        return -1;
    }
    size_t size = (size_t)len + 1;
    char *str = malloc(size);
    if (!str) {
        return -1;
    }    
    int r = __mingw_vsnprintf(str, len + 1, fmt, ap);
    if (r == -1) {
        free(str);
        return -1;
    }
    *strp = str;
    return r;
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
