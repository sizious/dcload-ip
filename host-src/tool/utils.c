#include <stdio.h>
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
