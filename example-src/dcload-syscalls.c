#include "dcload-syscalls.h"
#include "dcload-syscall.h"

int link(const char *oldpath, const char *newpath) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pclinknr, oldpath, newpath);
    else
        return -1;
}

int read(int file, void *buf, size_t len) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcreadnr, file, buf, len);
    else
        return -1;
}

off_t lseek(int filedes, off_t offset, int dir) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pclseeknr, filedes, offset, dir);
    else
        return -1;
}

int write(int file, const void *buf, size_t len) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcwritenr, file, buf, len);
    else
        return -1;
}

int close(int file) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcclosenr, file);
    else
        return -1;
}

int fstat(int file, struct stat *buf) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcfstatnr, file, buf);
    else
        return -1;
}

int open(const char *path, int flags, ...) {
    va_list ap;

    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE) {
        va_start(ap, flags);
        int return_value = dcloadsyscall(pcopennr, path, flags, va_arg(ap, int));
        va_end(ap);
        return return_value;
    } else
        return -1;
}

int creat(const char *path, mode_t mode) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pccreatnr, path, mode);
    else
        return -1;
}

int unlink(const char *path) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcunlinknr, path);
    else
        return -1;
}

void exit(int status) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        dcloadsyscall(pcexitnr);
    __exit(status);
}

int stat(const char *path, struct stat *buf) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcstatnr, path, buf);
    else
        return -1;
}

int chmod(const char *path, mode_t mode) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcchmodnr, path, mode);
    else
        return -1;
}

int utime(const char *path, struct utimbuf *buf) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcutimenr, path, buf);
    else
        return -1;
}

int chdir(const char *path) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcchdirnr, path);
    else
        return -1;
}

time_t time(time_t *t) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pctimenr, t);
    else
        return -1;
}

void assign_wrkmem(unsigned char *wrkmem) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        dcloadsyscall(pcassignwrkmem, wrkmem);
}

int gethostinfo(unsigned int *ip, unsigned int *port) {
    if(*DCLOADMAGICADDR == DCLOADMAGICVALUE)
        return dcloadsyscall(pcgethostinfo, ip, port);
    else
        return -1;
}
