#ifndef __DCLOAD_SYSCALLS_H__
#define __DCLOAD_SYSCALLS_H__

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <utime.h>
#include <stdarg.h>
#include <dirent.h>
#include <string.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

int link(const char *oldpath, const char *newpath);
int read(int file, void *buf, size_t len);
off_t lseek(int filedes, off_t offset, int dir);
int write(int file, const void *buf, size_t len);
int close(int file);
int fstat(int file, struct stat *buf);
int open(const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
int unlink(const char *path);
void exit(int status);
int stat(const char *path, struct stat *buf);
int chmod(const char *path, mode_t mode);
int utime(const char *path, struct utimbuf *buf);
int chdir(const char *path);
time_t time(time_t *t);
void assign_wrkmem(unsigned char *wrkmem);
int gethostinfo(unsigned int *ip, unsigned int *port);

#endif
