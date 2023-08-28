/*
 * This file is part of the dcload Dreamcast serial loader
 *
 * Copyright (C) 2001 Andrew Kieschnick <andrewk@napalm-x.com>
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

#ifdef __MINGW32__
#include <_mingw.h>
#include <windows.h>
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
