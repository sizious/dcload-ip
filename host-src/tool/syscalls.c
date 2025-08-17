/*
 * This file is part of the dcload Dreamcast ethernet loader
 *
 * Copyright (C) 2001 Andrew Kieschnick <andrewk@austin.rr.com>
 * Copyright (C) 2013 Lawrence Sebald
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#ifdef __MINGW32__
#include <windows.h>
#include <limits.h>
#else
#include <netinet/in.h>
#endif
#include "syscalls.h"
#include "dc-io.h"
#include "dcload-types.h"
#include "commands.h"

#include "utils.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef MAX_OPEN_DIRS
#define MAX_OPEN_DIRS   16
#endif

/* Sigh... KOS treats anything under 100 as invalid for a dirent from dcload, so
   we need to offset by a bit. This aught to do. */
#define DIRENT_OFFSET   1337
#define MAX_PATH_LEN 4096

#ifdef _WIN32
#define realpath(N,R) _fullpath((R),(N),PATH_MAX)
#endif

static DIR *opendirs[MAX_OPEN_DIRS];
static char *mappath = NULL;
static int mappatlen = -1;

static char path_work_buffer[MAX_PATH_LEN];
static char path_result_buffer[MAX_PATH_LEN];
void set_mappath(char *path) {
  mappath = path;
  mappatlen = strlen(mappath);
  strcpy(path_work_buffer, mappath);
}

/**
 * map_path - Wrapper method for use in chroot and mapping modes.
 *          If the static mappath hasn't been set chroot mode is assumed,
 *          otherwise paths are checked with realpath and mapped to the
 *          mappath directory.
 *
 * @param path The path to map as seen from the Dreamcast (ie. /pc/<path> )
 * @param check_only_dirname If true, realpath is called on the dirname of the path
 * @return The mapped path or NULL if the resolved path is outside of the
 *         mappath directory.
 */
static inline char *map_path(char *path, int check_only_dirname) {
  if (!mappath)
    return path;

  if (check_only_dirname) {
    // dirname alters the input string, so use a copy
    char dnamebuf[MAX_PATH_LEN]; 
    strcpy(dnamebuf, path);
    strcpy(path_work_buffer + mappatlen, dirname(dnamebuf));
  } else {
    strcpy(path_work_buffer + mappatlen, path);
  }
  if (realpath(path_work_buffer, path_result_buffer) == NULL) {
    printf("Failed to map path '%s' with error: %s\n", path_work_buffer,
           strerror(errno));
    return NULL;
  }
  
  if (strncmp(mappath, path_result_buffer, mappatlen) != 0) {
      printf("Requested path:\n\t%s\n"
        "is outside of basepath:\n\t%s\n",
        mappath, path_result_buffer);
        return NULL;
  }
  if (check_only_dirname) {
    // append the basename of the path to the result buffer
    int reslen = strlen(path_result_buffer);
    path_result_buffer[reslen] = '/';
    strcpy(path_result_buffer + reslen +1, basename(path));
  }
  return path_result_buffer;
}

/* syscalls for dcload-ip
 *
 * 1. receive all parameters from dc
 * 2. get any data from dc using recv_data (dc passes address/size of buffer)
 * 3. send any data to dc using send_data (dc passess address/size of buffer)
 * 4. send return value to dc
 */

unsigned int dc_order(unsigned int x)
{
    if (x == htonl(x))
	return (x << 24) | ((x << 8) & 0xff0000) | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
    else
	return x;
}

int dc_fstat(unsigned char * buffer)
{
    struct stat filestat;
    int retval;
    dcload_stat_t dcstat;
    command_3int_t *command = (command_3int_t *)buffer;
    /* value0 = fd, value1 = addr, value2 = size */

    retval = fstat(ntohl(command->value0), &filestat);

    dcstat.st_dev = dc_order(filestat.st_dev);
    dcstat.st_ino = dc_order(filestat.st_ino);
    dcstat.st_mode = dc_order(filestat.st_mode);
    dcstat.st_nlink = dc_order(filestat.st_nlink);
    dcstat.st_uid = dc_order(filestat.st_uid);
    dcstat.st_gid = dc_order(filestat.st_gid);
    dcstat.st_rdev = dc_order(filestat.st_rdev);
    dcstat.st_size = dc_order(filestat.st_size);
#ifndef __MINGW32__
    dcstat.st_blksize = dc_order(filestat.st_blksize);
    dcstat.st_blocks = dc_order(filestat.st_blocks);
#endif
    dcstat.st_atime_priv = dc_order(filestat.st_atime);
    dcstat.st_mtime_priv = dc_order(filestat.st_mtime);
    dcstat.st_ctime_priv = dc_order(filestat.st_ctime);

    send_data((unsigned char *)&dcstat, ntohl(command->value1), ntohl(command->value2));

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_write(unsigned char * buffer)
{
    unsigned char *data;
    int retval;
    command_3int_t *command = (command_3int_t *)buffer;
    /* value0 = fd, value1 = addr, value2 = size */

    data = malloc(ntohl(command->value2));

    recv_data(data, ntohl(command->value1), ntohl(command->value2), 1);

    // Check for exception messages. This compare is pretty quick, so it
    // shouldn't slow anything down unless someone is really pelting the console
    // hard.. Although, in that case printf() will probably become a big
    // bottleneck before this memcmp() ever does...
    if(!(memcmp(data, CMD_EXCEPTION, 4)))
    {
      // Exception data starts with "EXPT"
      exception_struct_t *exception_frame = (exception_struct_t*)data;
      unsigned int *exception_frame_uints = (unsigned int*)data;

      printf("\n\n");
      printf("%s", exception_code_to_string(exception_frame->expt_code));
      for(unsigned int regdump = 0; regdump < 66; regdump++)
      {
        printf("%s", exception_label_array[regdump]);
        printf(": 0x%x\n", exception_frame_uints[regdump + 2]);
      }

      // Write out to a file as well
      // It will end up in the working directory of the terminal
      int out_file = open("dcload_exception_dump.bin", O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
      retval = write(out_file, data, ntohl(command->value2));
      close(out_file);
    }
    else
    {
      retval = write(ntohl(command->value0), data, ntohl(command->value2));
    }

    if(send_command(CMD_RETVAL, retval, retval, NULL, 0) == -1) {
        free(data);
        return -1;
    }

    free(data);
    return 0;
}

int dc_read(unsigned char * buffer)
{
    unsigned char *data;
    int retval;
    command_3int_t *command = (command_3int_t *)buffer;
    /* value0 = fd, value1 = addr, value2 = size */

    data = malloc(ntohl(command->value2));
    retval = read(ntohl(command->value0), data, ntohl(command->value2));

    send_data(data, ntohl(command->value1), ntohl(command->value2));

    if(send_command(CMD_RETVAL, retval, retval, NULL, 0)) {
        free(data);
        return -1;
    }

    free(data);
    return 0;
}

int dc_open(unsigned char * buffer)
{
  int retval;
  int ourflags = 0;
  command_2int_string_t *command = (command_2int_string_t *)buffer;
  /* value0 = flags value1 = mode string = name */

  /* translate flags */

  if (ntohl(command->value0) & 0x0001)
    ourflags |= O_WRONLY;
  if (ntohl(command->value0) & 0x0002)
    ourflags |= O_RDWR;
  if (ntohl(command->value0) & 0x0008)
    ourflags |= O_APPEND;
  if (ntohl(command->value0) & 0x0200)
    ourflags |= O_CREAT;
  if (ntohl(command->value0) & 0x0400)
    ourflags |= O_TRUNC;
  if (ntohl(command->value0) & 0x0800)
    ourflags |= O_EXCL;
  retval = open(map_path(command->string, 1), ourflags | O_BINARY,
                ntohl(command->value1));

  send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

  return 0;
}

int dc_close(unsigned char * buffer)
{
    int retval;
    command_int_t *command = (command_int_t *)buffer;

    retval = close(ntohl(command->value0));

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_creat(unsigned char * buffer)
{
    int retval;
    command_int_string_t *command = (command_int_string_t *)buffer;

  retval = creat(map_path(command->string, 1), ntohl(command->value0));
  send_cmd(CMD_RETVAL, retval, retval, NULL, 0);
  return 0;
}

int dc_link(unsigned char *buffer) {
  int retval;
  command_string_t *command = (command_string_t *)buffer;
  char local_buffer[MAX_PATH_LEN];

  const char *local_ref = map_path(command->string, 0);
  if (mappath) {
    strcpy(local_buffer, local_ref);
  }

#ifdef __MINGW32__
  /* Copy the file on Windows */
  retval =
      CopyFileA(local_buffer,
                map_path(&command->string[strlen(command->string) + 1], 1), 0);
#else
  retval = link(local_buffer,
                map_path(&command->string[strlen(command->string) + 1], 1));
#endif

  send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

  return 0;
}
int dc_unlink(unsigned char * buffer)
{
    int retval;
    command_string_t *command = (command_string_t *)buffer;

    retval = unlink(command->string);

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_chdir(unsigned char * buffer)
{
    int retval;
    command_string_t *command = (command_string_t *)buffer;

  retval = chdir(map_path(command->string, 0));

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_chmod(unsigned char * buffer)
{
    int retval;
    command_int_string_t *command = (command_int_string_t *)buffer;

  retval = chmod(map_path(command->string, 0), ntohl(command->value0));

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_lseek(unsigned char * buffer)
{
    int retval;
    command_3int_t *command = (command_3int_t *)buffer;

    retval = lseek(ntohl(command->value0), ntohl(command->value1), ntohl(command->value2));

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_time(unsigned char * buffer)
{
    time_t t = time(NULL);

    send_cmd(CMD_RETVAL, t, t, NULL, 0);

    return 0;
}

int dc_stat(unsigned char * buffer)
{
    struct stat filestat;
    int retval;
    dcload_stat_t dcstat;
    command_2int_string_t *command = (command_2int_string_t *)buffer;

  retval = stat(map_path(command->string, 0), &filestat);

    dcstat.st_dev = dc_order(filestat.st_dev);
    dcstat.st_ino = dc_order(filestat.st_ino);
    dcstat.st_mode = dc_order(filestat.st_mode);
    dcstat.st_nlink = dc_order(filestat.st_nlink);
    dcstat.st_uid = dc_order(filestat.st_uid);
    dcstat.st_gid = dc_order(filestat.st_gid);
    dcstat.st_rdev = dc_order(filestat.st_rdev);
    dcstat.st_size = dc_order(filestat.st_size);
#ifndef __MINGW32__
    dcstat.st_blksize = dc_order(filestat.st_blksize);
    dcstat.st_blocks = dc_order(filestat.st_blocks);
#endif
    dcstat.st_atime_priv = dc_order(filestat.st_atime);
    dcstat.st_mtime_priv = dc_order(filestat.st_mtime);
    dcstat.st_ctime_priv = dc_order(filestat.st_ctime);

    send_data((unsigned char *)&dcstat, ntohl(command->value0), ntohl(command->value1));

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_utime(unsigned char * buffer)
{
    struct utimbuf tbuf;
    int retval;
    command_3int_string_t *command = (command_3int_string_t *)buffer;

    if (ntohl(command->value0)) {
	tbuf.actime = ntohl(command->value1);
	tbuf.modtime = ntohl(command->value2);

	retval = utime(command->string, &tbuf);
    } else {
	retval = utime(command->string, 0);
    }
    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_opendir(unsigned char * buffer)
{
    DIR *somedir;
    command_string_t *command = (command_string_t *)buffer;
    int i;

    /* Find an open entry */
    for(i = 0; i < MAX_OPEN_DIRS; ++i) {
        if(!opendirs[i])
            break;
    }

    if(i < MAX_OPEN_DIRS) {
    if (!(opendirs[i] = opendir(map_path(command->string, 0))))
            i = 0;
        else
            i += DIRENT_OFFSET;
    }
    else {
        i = 0;
    }

    send_cmd(CMD_RETVAL, (unsigned int)i, (unsigned int)i, NULL, 0);

    return 0;
}

int dc_closedir(unsigned char * buffer)
{
    int retval;
    command_int_t *command = (command_int_t *)buffer;
    uint32_t i = ntohl(command->value0);


    if(i >= DIRENT_OFFSET && i < MAX_OPEN_DIRS + DIRENT_OFFSET) {
        retval = closedir(opendirs[i - DIRENT_OFFSET]);
        opendirs[i - DIRENT_OFFSET] = NULL;
    }
    else {
        retval = -1;
    }

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_readdir(unsigned char * buffer)
{
    struct dirent *somedirent;
    dcload_dirent_t dcdirent;
    command_3int_t *command = (command_3int_t *)buffer;
    uint32_t i = ntohl(command->value0);

    if(i >= DIRENT_OFFSET && i < MAX_OPEN_DIRS + DIRENT_OFFSET)
        somedirent = readdir(opendirs[i - DIRENT_OFFSET]);
    else
        somedirent = NULL;

    if (somedirent) {
#if defined (__APPLE__) || defined (__NetBSD__) || defined (__FreeBSD__) || defined (__OpenBSD__)
	dcdirent.d_ino = dc_order(somedirent->d_fileno);
	dcdirent.d_off = dc_order(0);
	dcdirent.d_reclen = dc_order(somedirent->d_reclen);
	dcdirent.d_type = dc_order(somedirent->d_type);
#else
	dcdirent.d_ino = dc_order(somedirent->d_ino);
# if defined(_WIN32) || defined(__CYGWIN__)
	dcdirent.d_off = dc_order(0);
	dcdirent.d_reclen = dc_order(0);
	dcdirent.d_type = dc_order(0);
# else
	dcdirent.d_off = dc_order(somedirent->d_off);
	dcdirent.d_reclen = dc_order(somedirent->d_reclen);
	dcdirent.d_type = dc_order(somedirent->d_type);
# endif
#endif
	strcpy(dcdirent.d_name, somedirent->d_name);

	send_data((unsigned char *)&dcdirent, ntohl(command->value1), ntohl(command->value2));
	send_cmd(CMD_RETVAL, 1, 1, NULL, 0);
	return 0;
    }

    send_cmd(CMD_RETVAL, 0, 0, NULL, 0);

    return 0;
}

int dc_rewinddir(unsigned char * buffer)
{
    int retval;
    command_int_t *command = (command_int_t *)buffer;
    uint32_t i = ntohl(command->value0);


    if(i >= DIRENT_OFFSET && i < MAX_OPEN_DIRS + DIRENT_OFFSET) {
        rewinddir(opendirs[i - DIRENT_OFFSET]);
        opendirs[i - DIRENT_OFFSET] = NULL;
        retval = 0;
    }
    else {
        retval = -1;
    }

    send_cmd(CMD_RETVAL, retval, retval, NULL, 0);

    return 0;
}

int dc_cdfs_redir_read_sectors(int isofd, unsigned char * buffer)
{
    int start;
    unsigned char * buf;
    command_3int_t *command = (command_3int_t *)buffer;

    start = ntohl(command->value0) - 150;

    lseek(isofd, start * 2048, SEEK_SET);

    buf = malloc(ntohl(command->value2));

    read(isofd, buf, ntohl(command->value2));

    send_data(buf, ntohl(command->value1), ntohl(command->value2));

    send_cmd(CMD_RETVAL, 0, 0, NULL, 0);

    free(buf);
    return 0;
}

#define GDBBUFSIZE 1024
#ifdef __MINGW32__
extern SOCKET gdb_server_socket;
extern SOCKET socket_fd;
#else
extern int gdb_server_socket;
extern int socket_fd;
#endif

int dc_gdbpacket(unsigned char * buffer)
{
    size_t in_size, out_size;
    static char gdb_buf[GDBBUFSIZE];
    int retval = 0;

#ifdef __MINGW32__
	if (gdb_server_socket == INVALID_SOCKET) {
#else
	if (gdb_server_socket < 0) {
#endif
        send_cmd(CMD_RETVAL, -1, -1, NULL, 0);
    }

    if (socket_fd == 0) {
	printf( "waiting for gdb client connection...\n" );
	socket_fd = accept( gdb_server_socket, NULL, NULL );
#ifdef __MINGW32__
	if ( socket_fd != INVALID_SOCKET)
#endif
	if ( socket_fd == 0) {
	    log_error("error accepting gdb server connection");
	    return -1;
	}
    }

    command_2int_string_t *command = (command_2int_string_t *)buffer;
    /* value0 = in_size, value1 = out_size, string = packet */

    in_size = ntohl(command->value0);
    out_size = ntohl(command->value1);

    if (in_size)
	send(socket_fd, command->string, in_size, 0);

    if (out_size) {
	retval = recv(socket_fd, gdb_buf, out_size > GDBBUFSIZE ? GDBBUFSIZE : out_size, 0);

	if (retval == 0)
	socket_fd = -1;
    }
#ifdef __MINGW32__
	if(retval == SOCKET_ERROR) {
	fprintf(stderr, "Got socket error: %d\n", WSAGetLastError());
	return -1;
	}
#else
    if(retval == -1) {
        fprintf(stderr, "Got socket error: %s\n", strerror(errno));
        return -1;
    }
#endif
    send_cmd(CMD_RETVAL, retval, retval, (unsigned char *)gdb_buf, retval);

    return 0;
}
