#include <stdio.h>
#ifdef __MINGW32__	
#include <windows.h>
#endif

void log_error( const char * prefix ) {
	perror( prefix );
	
#ifdef __MINGW32__	
	DWORD dwError = WSAGetLastError();
	if ( dwError ) {
      printf("WSAGetLastError: %d\n", dwError);
	}
#endif
}
