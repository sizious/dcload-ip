#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef __MINGW32__	
#include <windows.h>
#endif

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
