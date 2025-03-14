/*
 * dc-tool, a tool for use with the dcload ethernet loader
 *
 * Copyright (C) 2001 Andrew Kieschnick <andrewk@austin.rr.com>
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

#include "config.h" // needed for newer BFD library

#ifdef WITH_BFD
#include <bfd.h>
#else
#ifdef MACOS
#include <libelf/libelf.h>
#else
#include <libelf.h>
#endif
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>
#ifndef __MINGW32__
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "commands.h"
#include "dc-io.h"
#include "syscalls.h"

#include "utils.h"

int _nl_msg_cat_cntr;

#define DEBUG(x, ...) \
    fprintf(stderr, "DEBUG: ");  \
    fprintf(stderr, x, __VA_ARGS__)

#define CatchError(x)  \
    if(x)  \
        return -1;

#define VERSION PACKAGE_VERSION
#define DCTOOL_LEGACY_SYSCALL_PORT  31313
// Really should be using ports in the range 49152-65535, so dcload v2 does.
#define DCTOOL_DEFAULT_SYSCALL_PORT 53535

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef HAVE_GETOPT
/* The following code for getopt is from the libc-source of FreeBSD,
 * it might be changed a little bit.
 * Florian Schulze (florian.proff.schulze@gmx.net)
 */

/*
 * Copyright (c) 1987, 1993, 1994
 * The Regents of the University of California. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "@(#)getopt.c 8.3 (Berkeley) 4/27/95";
#endif
static const char rcsid[] = "$FreeBSD$";
#endif /* LIBC_SCCS and not lint */

int opterr = 1, /* if error message should be printed */
    optind = 1, /* index into parent argv vector */
    optopt,     /* character checked for validity */
    optreset;   /* reset getopt */
char *optarg;   /* argument associated with option */

#define BADCH (int)'?'
#define BADARG (int)':'
#define EMSG ""

char *__progname = PACKAGE;

/*
 * getopt -- Parse argc/argv argument vector.
 */
int getopt(int nargc, char *const *nargv, const char *ostr) {
    extern char *__progname;
    static char *place = EMSG; /* option letter processing */
    char *oli;                 /* option letter list index */
    int ret;

    if(optreset || !*place) { /* update scanning pointer */
        optreset = 0;
        if(optind >= nargc || *(place = nargv[optind]) != '-') {
            place = EMSG;
            return (-1);
        }
        if(place[1] && *++place == '-') { /* found "--" */
            ++optind;
            place = EMSG;
            return (-1);
        }
    } /* option letter okay? */
    if((optopt = (int)*place++) == (int)':' || !(oli = strchr(ostr, optopt))) {
        /*
         * if the user didn't specify '-' as an option,
         * assume it means -1.
         */
        if(optopt == (int)'-')
            return (-1);
        if(!*place)
            ++optind;
        if(opterr && *ostr != ':')
            (void)fprintf(stderr, "%s: illegal option -- %c\n", __progname, optopt);
        return (BADCH);
    }
    if(*++oli != ':') { /* don't need argument */
        optarg = NULL;
        if(!*place)
            ++optind;
    }
    else {       /* need an argument */
        if(*place) /* no white space */
            optarg = place;
        else if(nargc <= ++optind) { /* no arg */
            place = EMSG;
            if(*ostr == ':')
                ret = BADARG;
            else
                ret = BADCH;
            if(opterr)
                (void)fprintf(stderr, "%s: option requires an argument -- %c\n", __progname,
                              optopt);
            return (ret);
        }
        else /* white space */
            optarg = nargv[optind];
        place = EMSG;
        ++optind;
    }
    return (optopt); /* dump back option letter */
}
#else
// This is defined elsewhere if the above #if isn't used...
extern char *optarg;
#endif

int gdb_socket_started = 0;
#ifdef __MINGW32__
#define bzero(b, len) (memset((b), '\0', (len)), (void)0)
/* Winsock SOCKET is defined as an unsigned int, so -1 won't work here */
SOCKET dcsocket_legacy = 0;
SOCKET dcsocket = 0;
SOCKET gdb_server_socket = 0;
SOCKET socket_fd = 0;     // For GDB
SOCKET global_socket = 0; // Stores whichever global socket gets used
#else
int dcsocket_legacy = 0;
int dcsocket = 0;
int gdb_server_socket = -1;
int socket_fd = 0;     // For GDB
int global_socket = 0; // Stores whichever global socket gets used
unsigned int nochroot = 0;
char *path = 0;
#endif

void cleanup(char **fnames) {
    int counter = 0;

    for(; counter < 4; counter++) {
        if(fnames[counter] != 0) {
            free(fnames[counter]);
        }
    }

#ifdef __MINGW32__
    if(dcsocket) {
        closesocket(dcsocket);
    }

    if(dcsocket_legacy) {
        closesocket(dcsocket_legacy);
    }
#else
    if(dcsocket) {
        close(dcsocket);
    }

    if(dcsocket_legacy) {
        close(dcsocket_legacy);
    }
#endif

    // Handle GDB
    if(gdb_socket_started) {
        gdb_socket_started = 0;

        // Send SIGTERM to the GDB Client, telling remote DC program has ended
        char gdb_buf[16];
        strcpy(gdb_buf, "+$X0f#ee\0");

#ifdef __MINGW32__
        send(socket_fd, gdb_buf, strlen(gdb_buf), 0);
        sleep(1);
        closesocket(socket_fd);
        closesocket(gdb_server_socket);
#else
        write(socket_fd, gdb_buf, strlen(gdb_buf));
        sleep(1);
        close(socket_fd);
        close(gdb_server_socket);
#endif
    }

#ifdef __MINGW32__
    WSACleanup();
#endif
}

unsigned int time_in_usec() {
    struct timeval thetime;

    gettimeofday(&thetime, NULL);

    return (unsigned int)(thetime.tv_sec * 1000000) + (unsigned int)thetime.tv_usec;
}

/* 250000 = 0.25 seconds */
#define PACKET_TIMEOUT 250000
struct timeval starttime = {0}, endtime = {0};

// Adapter type detection
// Each adapter needs different values for things like Dreamcast RX FIFO sizes
#define BBA_MODEL 0400
#define LAN_MODEL 0300

unsigned int installed_adapter = 0;
unsigned int legacy =
    0; // To know if this should use old 1024-byte sizes for packets or new 1440-sizes
unsigned int force_legacy = 0; // To force dcload and dc-tool into legacy mode with -l flag

// How long to wait for DC to empty its RX FIFO, in microseconds
#define BBA_RX_FIFO_DELAY_TIME DREAMCAST_BBA_RX_FIFO_DELAY_TIME
#define LAN_RX_FIFO_DELAY_TIME DREAMCAST_LAN_RX_FIFO_DELAY_TIME

unsigned int rx_fifo_delay =
    PACKET_TIMEOUT / 51; // Default for compatibility with old dcload-ip versions

#define BBA_RX_FIFO_DELAY_COUNT DREAMCAST_BBA_RX_FIFO_DELAY_COUNT
#define LAN_RX_FIFO_DELAY_COUNT DREAMCAST_LAN_RX_FIFO_DELAY_COUNT
// Number of packets to send before waiting for DC to empty its RX FIFO

unsigned int rx_fifo_delay_count = 15; // Default for compatibility with old dcload-ip versions

// Get the version of dc-tool encoded in a uint as (major << 16) | (minor << 8) | patch,
// since this program is more likely to get patch version bumps than either of the other two.
// Presumably a max version of 255.255.255 is OK. :P
unsigned int encoded_tool_ver = 0;

void make_encoded_tool_version() {
    if(!force_legacy) { // Force legacy forces 1024-size packets by using the legacy system
        int i = 1, c = 0; // Indices
        unsigned char *ver_uchar = (unsigned char *)&encoded_tool_ver;
        unsigned short *ver_ushort = (unsigned short *)&encoded_tool_ver;

        // 1 byte per version unit
        while(VERSION[c] != '\0') {
            if(VERSION[c] == '.') {
                c++;
                i++;
            }
            else {
                ver_uchar[i] *= 10;
                ver_uchar[i] += VERSION[c] - '0';
                c++;
            }
        }

        encoded_tool_ver = ntohl(encoded_tool_ver);
    }
    // force_legacy forces the version to 0, causing dcload and dc-tool to use legacy 1024-size mode
}

// Both send_data() and recv_data() use this to set up communications between dc-tool and dc-load
int prepare_comms(unsigned char *buffer) {
    if(!installed_adapter) { // This is how we know that this function has run before
        // First check for the type of adapter installed (only need to do this once)
        make_encoded_tool_version(); // Only need to set encoded dc-tool version once, and it's only
                                     // needed for this version info handshake

        // Unless 'force_legacy' is set, first we use the v2.0.0+ socket to determine whether or not
        // dcload is v2.0.0+ or legacy: Legacy dcload will reply on 31313. Note that response port
        // is not the primary means of version detection, so, in the event that something ever
        // changes about this in the future, ports won't come back to bite anyone.
        if(force_legacy) {
            global_socket = dcsocket_legacy;
        }
        else {
            global_socket = dcsocket;
        }

        int flip = 0;

        do {
            // Stuff the encoded dc-tool version into the address field
            // dcload v2.0.0 will know what to do with this; prior versions will ignore it
            send_cmd(CMD_VERSION, encoded_tool_ver, 0, NULL, 0);
        } while(recv_response(buffer, PACKET_TIMEOUT) == -1);

        while(memcmp(((command_t *)buffer)->id, CMD_VERSION, 4)) {
            printf("prepare_comms: No response to CMD_VERSION, retrying... %c%c%c%c\n", buffer[0],
                   buffer[1], buffer[2], buffer[3]);
            do {
                // Alternate checking each socket
                flip ^= 0x1;
                if(flip) {
                    global_socket = dcsocket_legacy;
                } else {
                    global_socket = dcsocket;
                }
                send_cmd(CMD_VERSION, encoded_tool_ver, 0, NULL, 0);
            } while(recv_response(buffer, PACKET_TIMEOUT) == -1);
        }

        // Close the socket we don't need, then set parameters
        if(global_socket == dcsocket) {
#ifdef __MINGW32__
            closesocket(dcsocket_legacy);
#else
            close(dcsocket_legacy);
#endif
        }
        else { // global_socket == dcsocket_legacy
#ifdef __MINGW32__
            closesocket(dcsocket);
#else
            close(dcsocket);
#endif
        }

        // As of version 2.0.0 the 'version' command now stuffs a numeric adapter
        // type into the previously unused command->address field :)
        installed_adapter = ntohl(((command_t *)buffer)->address);

        if(installed_adapter == BBA_MODEL) {
            printf("%s\n", ((command_t *)buffer)->data);
            if(force_legacy) {
                printf("Forcing 1024-byte payloads...\n");
                legacy = 1;
            }

            rx_fifo_delay = BBA_RX_FIFO_DELAY_TIME;        // microseconds
            rx_fifo_delay_count = BBA_RX_FIFO_DELAY_COUNT; // packets per burst
        } else if(installed_adapter == LAN_MODEL) {
            printf("%s\n", ((command_t *)buffer)->data);
            if(force_legacy) {
                printf("Forcing 1024-byte payloads...\n");
                legacy = 1;
            }

            rx_fifo_delay = LAN_RX_FIFO_DELAY_TIME;        // microseconds
            rx_fifo_delay_count = LAN_RX_FIFO_DELAY_COUNT; // packets per burst
        }
        else {                                            // legacy dcload has 0 there
            // Alternatively, it means someone is using an old version of dcload-ip and really needs
            // to upgrade.
            printf("Unknown adapter or old version of dcload-ip detected.\nDefaulting to legacy "
                   "Broadband Adapter settings...\n");
            installed_adapter = BBA_MODEL;
            legacy = 1;
            // Default rx_fifo_delay and rx_fifo_delay_count are already set for legacy
        }
    }

    return 0;
}

/* receive total bytes from dc and store in data */
int recv_data(void *data, unsigned int dcaddr, unsigned int total, unsigned int quiet) {
    unsigned char buffer[2048];
    unsigned char *i;
    int c;
    int packets = 0;
    unsigned int start;
    int retval;

    // v2.0.0: set up the socket, do version and adapter identification, set globals
    prepare_comms(buffer);

    // old 1024 sizes
    // This if() looks awful because some ARM chips don't have integer divide, so
    // hardcoding 1024 and 1440 sizes removes those divides. This is because GCC
    // has some tricks for certain "nice" numbers (I don't know if there's an
    // official GCC term for them), and both 1024 and 1440 count as nice numbers.
    if(legacy) {
        unsigned char *map = (unsigned char *)malloc((total + 1023) / 1024);
        memset(map, 0, (total + 1023) / 1024);

        // Start thropughput timer
        gettimeofday(&starttime, 0);

        // Receive the data!

        if(!quiet) {
            send_cmd(CMD_SENDBIN, dcaddr, total, NULL, 0);
        }
        else {
            send_cmd(CMD_SENDBINQ, dcaddr, total, NULL, 0);
        }

        start = time_in_usec();

        while(((time_in_usec() - start) < PACKET_TIMEOUT) &&
              (packets < ((total + 1023) / 1024 + 1))) {
            memset(buffer, 0, 2048);

            while(((retval = recv(global_socket, (void *)buffer, 2048, 0)) == -1) &&
                  ((time_in_usec() - start) < PACKET_TIMEOUT))
                ;

            if(retval > 0) {
                start = time_in_usec();
                if(memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4)) {
                    if(((ntohl(((command_t *)buffer)->address) - dcaddr) / 1024) >=
                       ((total + 1024) / 1024)) {
                        printf("Obviously bad packet, avoiding segfault\n");
                        fflush(stdout);
                    }
                    else {
                        map[(ntohl(((command_t *)buffer)->address) - dcaddr) / 1024] = 1;
                        i = data + (ntohl(((command_t *)buffer)->address) - dcaddr);

                        memcpy(i, buffer + 12, ntohl(((command_t *)buffer)->size));
                    }
                }
                packets++;
            }
        }

        for(c = 0; c < (total + 1023) / 1024; c++) {
            if(!map[c]) {
                if((total - c * 1024) >= 1024) {
                    send_cmd(CMD_SENDBINQ, dcaddr + c * 1024, 1024, NULL, 0);
                }
                else {
                    send_cmd(CMD_SENDBINQ, dcaddr + c * 1024, total - c * 1024, NULL, 0);
                }

                start = time_in_usec();
                while(((retval = recv(global_socket, (void *)buffer, 2048, 0)) == -1) &&
                      ((time_in_usec() - start) < PACKET_TIMEOUT))
                    ;

                if(retval > 0) {
                    start = time_in_usec();

                    if(memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4)) {
                        map[(ntohl(((command_t *)buffer)->address) - dcaddr) / 1024] = 1;
                        /* printf("recv_data: got chunk for %p, %d bytes\n",
                        (void *)ntohl(((command_t *)buffer)->address), ntohl(((command_t
                        *)buffer)->size)); */
                        i = data + (ntohl(((command_t *)buffer)->address) - dcaddr);

                        memcpy(i, buffer + 12, ntohl(((command_t *)buffer)->size));
                    }

                    // Get the DONEBIN
                    while(((retval = recv(global_socket, (void *)buffer, 2048, 0)) == -1) &&
                          ((time_in_usec() - start) < PACKET_TIMEOUT))
                        ;
                }

                // Force us to go back and recheck
                // XXX This should be improved after recv_data can return errors.
                c = -1;
            }
        }

        gettimeofday(&endtime, 0);

        free(map);
    } else // New 1440 sizes
    {
        unsigned char *map = (unsigned char *)malloc((total + 1439) / 1440);
        memset(map, 0, (total + 1439) / 1440);

        // Start thropughput timer
        gettimeofday(&starttime, 0);

        // Receive the data!

        if(!quiet) {
            send_cmd(CMD_SENDBIN, dcaddr, total, NULL, 0);
        }
        else {
            send_cmd(CMD_SENDBINQ, dcaddr, total, NULL, 0);
        }

        start = time_in_usec();

        while(((time_in_usec() - start) < PACKET_TIMEOUT) &&
              (packets < ((total + 1439) / 1440 + 1))) {
            memset(buffer, 0, 2048);

            while(((retval = recv(global_socket, (void *)buffer, 2048, 0)) == -1) &&
                  ((time_in_usec() - start) < PACKET_TIMEOUT))
                ;

            if(retval > 0) {
                start = time_in_usec();
                if(memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4)) {
                    if(((ntohl(((command_t *)buffer)->address) - dcaddr) / 1440) >=
                       ((total + 1440) / 1440)) {
                        printf("Obviously bad packet, avoiding segfault\n");
                        fflush(stdout);
                    }
                    else {
                        map[(ntohl(((command_t *)buffer)->address) - dcaddr) / 1440] = 1;
                        i = data + (ntohl(((command_t *)buffer)->address) - dcaddr);

                        memcpy(i, buffer + 12, ntohl(((command_t *)buffer)->size));
                    }
                }
                packets++;
            }
        }

        for(c = 0; c < (total + 1439) / 1440; c++)
            if(!map[c]) {
                if((total - c * 1440) >= 1440) {
                    send_cmd(CMD_SENDBINQ, dcaddr + c * 1440, 1440, NULL, 0);
                }
                else {
                    send_cmd(CMD_SENDBINQ, dcaddr + c * 1440, total - c * 1440, NULL, 0);
                }

                start = time_in_usec();
                while(((retval = recv(global_socket, (void *)buffer, 2048, 0)) == -1) &&
                      ((time_in_usec() - start) < PACKET_TIMEOUT))
                    ;

                if(retval > 0) {
                    start = time_in_usec();

                    if(memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4)) {
                        map[(ntohl(((command_t *)buffer)->address) - dcaddr) / 1440] = 1;
                        /* printf("recv_data: got chunk for %p, %d bytes\n",
                        (void *)ntohl(((command_t *)buffer)->address), ntohl(((command_t
                        *)buffer)->size)); */
                        i = data + (ntohl(((command_t *)buffer)->address) - dcaddr);

                        memcpy(i, buffer + 12, ntohl(((command_t *)buffer)->size));
                    }

                    // Get the DONEBIN
                    while(((retval = recv(global_socket, (void *)buffer, 2048, 0)) == -1) &&
                          ((time_in_usec() - start) < PACKET_TIMEOUT))
                        ;
                }

                // Force us to go back and recheck
                // XXX This should be improved after recv_data can return errors.
                c = -1;
            }

        gettimeofday(&endtime, 0);

        free(map);
    }

    return 0;
}

/* send size bytes to dc from addr to dcaddr*/
int send_data(unsigned char *addr, unsigned int dcaddr, unsigned int size) {
    unsigned char buffer[2048] = {0};
    unsigned char *i = 0;
    unsigned int a = dcaddr;
    unsigned int start = 0;
    unsigned int count = 0;

    if(!size)
        return -1;

    // v2.0.0: Set up the socket, do version and adapter identification, set globals
    prepare_comms(buffer);

    // Send the data!
    do {
        send_cmd(CMD_LOADBIN, dcaddr, size, NULL, 0);
    } while(recv_response(buffer, PACKET_TIMEOUT) == -1);

    while(memcmp(((command_t *)buffer)->id, CMD_LOADBIN, 4)) {
        printf("send_data: error in response to CMD_LOADBIN, retrying... %c%c%c%c\n", buffer[0],
               buffer[1], buffer[2], buffer[3]);
        do
            send_cmd(CMD_LOADBIN, dcaddr, size, NULL, 0);
        while(recv_response(buffer, PACKET_TIMEOUT) == -1);
    }

    // Start throughput timer
    gettimeofday(&starttime, 0);

    // old 1024 sizes
    if(legacy) {
        for(i = addr; i < (addr + size); i += 1024) {
            if((addr + size - i) >= 1024) {
                send_cmd(CMD_PARTBIN, dcaddr, 1024, i, 1024);
            }
            else {
                send_cmd(CMD_PARTBIN, dcaddr, (addr + size) - i, i, (addr + size) - i);
            }

            dcaddr += 1024;

            /* give the DC a chance to empty its rx fifo
             * this prevents buffer overflows and dropped packets
             */
            count++;
            if(count == rx_fifo_delay_count) {
                start = time_in_usec();
                while((time_in_usec() - start) < rx_fifo_delay)
                    ;
                count = 0;
            }
        }
    }
    else { // 1440 sizes
        for(i = addr; i < (addr + size); i += 1440) {
            if((addr + size - i) >= 1440) {
                send_cmd(CMD_PARTBIN, dcaddr, 1440, i, 1440);
            }
            else {
                send_cmd(CMD_PARTBIN, dcaddr, (addr + size) - i, i, (addr + size) - i);
            }

            dcaddr += 1440;

            /* give the DC a chance to empty its rx fifo
             * this prevents buffer overflows and dropped packets
             */
            count++;
            if(count == rx_fifo_delay_count) {
                start = time_in_usec();
                while((time_in_usec() - start) < rx_fifo_delay)
                    ;
                count = 0;
            }
        }
    }

    // Finish up sending and check for dropped packets

    start = time_in_usec();
    /* delay a bit to try to make sure all data goes out before CMD_DONEBIN */
    while((time_in_usec() - start) < PACKET_TIMEOUT / 10)
        ; // 25ms

    do
        send_cmd(CMD_DONEBIN, 0, 0, NULL, 0);
    while(recv_response(buffer, PACKET_TIMEOUT) == -1);

    while(memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4)) {
        printf("send_data: error in response to CMD_DONEBIN, retrying...\n");

        do
            send_cmd(CMD_LOADBIN, dcaddr, size, NULL, 0);
        while(recv_response(buffer, PACKET_TIMEOUT) == -1);
    }

    while(ntohl(((command_t *)buffer)->size) != 0) {
        /*    printf("%d bytes at 0x%x were missing, resending\n", ntohl(((command_t
         * *)buffer)->size),ntohl(((command_t *)buffer)->address)); */
        send_cmd(
            CMD_PARTBIN, ntohl(((command_t *)buffer)->address), ntohl(((command_t *)buffer)->size),
            addr + (ntohl(((command_t *)buffer)->address) - a), ntohl(((command_t *)buffer)->size));

        do
            send_cmd(CMD_DONEBIN, 0, 0, NULL, 0);
        while(recv_response(buffer, PACKET_TIMEOUT) == -1);

        while(memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4)) {
            printf("send_data: error in response to CMD_DONEBIN, retrying...\n");

            do
                send_cmd(CMD_LOADBIN, dcaddr, size, NULL, 0);
            while(recv_response(buffer, PACKET_TIMEOUT) == -1);
        }
    }

    gettimeofday(&endtime, 0);

    return 0;
}

void usage(void) {
    printf("\n%s %s by Andrew \"ADK\" Kieschnick\nAugmented by Moopthehedgehog\n\n", PACKAGE,
           VERSION);
    printf("-x <filename>  Upload and execute <filename>\n");
    printf("-u <filename>  Upload <filename>\n");
    printf("-d <filename>  Download to <filename>\n");
    printf("-a <address>   Set address to <address> (default: 0x0c010000)\n");
    printf("-s <size>      Set size to <size>\n");
    printf("-t <ip>:<port> Connect to <ip>:<port> (port optional, default: %s:53535)\n",
           DREAMCAST_IP);
    printf("-n             Do not attach console and fileserver\n");
    printf("-q             Do not clear screen before download\n");
#ifndef __MINGW32__
    printf("-m <path>      Map /pc/ on KOS side to <path> (no chroot or super-user requirement)\n");
    printf("-c <path>      Chroot to <path> (must be super-user)\n");
#endif
    printf("-i <isofile>   Enable cdfs redirection using iso image <isofile>\n");
    printf("-r             Reset (only works when dcload is in control)\n");
    printf("-g             Start a GDB server\n");
    printf("-l             Force legacy 1024-byte payload size (dcload-ip v2+ only)\n");
    printf("-h             Usage information (you\'re looking at it)\n\n");
}

/* Got to make sure WinSock is initalized */
#ifdef __MINGW32__
int start_ws() {
    WSADATA wsaData;
    int failed = 0;
    failed = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if(failed != NO_ERROR) {
        log_error("WSAStartup");
        return 1;
    }

    return 0;
}
#endif

// dcload v2.0.0+ UDP port number
unsigned int dcload_portnum = DCTOOL_DEFAULT_SYSCALL_PORT;

// Legacy and new mode sockets
int open_sockets(char *hostname) {
    struct sockaddr_in sin;
    struct sockaddr_in sin_legacy;
    struct hostent *host = 0;

    dcsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    dcsocket_legacy = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

#ifndef __MINGW32__
    if((dcsocket < 0) || (dcsocket_legacy < 0)) {
#else
    if((dcsocket == INVALID_SOCKET) || (dcsocket_legacy == INVALID_SOCKET)) {
#endif
        log_error("socket");
        return -1;
    }

    bzero(&sin, sizeof(sin));
    bzero(&sin_legacy, sizeof(sin_legacy));

    sin.sin_family = AF_INET;
    sin_legacy.sin_family = AF_INET;

    // So, dcload is actually the server in this client-server setup
    // However, dc-tool gets to pick the port

    sin.sin_port = htons(dcload_portnum);
    sin_legacy.sin_port = htons(DCTOOL_LEGACY_SYSCALL_PORT);

    // On many platforms, leading-zeros in an octet cause the octet to be
    // interpreted as octal, so they should always be removed before trying to
    // connect.
    // --tsowell

    // Try to remove leading zeros from hostname...
    cleanup_ip_address(hostname);
    host = gethostbyname(hostname);

    if(!host) {
        // definitely, we can't do nothing
        log_error("gethostbyname");
        return -1;
    }

    memcpy((char *)&sin.sin_addr, host->h_addr, host->h_length);
    memcpy((char *)&sin_legacy.sin_addr, host->h_addr, host->h_length);

    // Connect legacy port first so that v2.0.0+ port won't conflict
    if(connect(dcsocket_legacy, (struct sockaddr *)&sin_legacy, sizeof(sin_legacy)) < 0) {
        log_error("connect_legacy");
        return -1;
    }

    // Connect v2.0.0+ port
    if(connect(dcsocket, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        log_error("connect");
        return -1;
    }

#ifdef __MINGW32__
    unsigned long flags = 1;
    int failed = 0;
    int failed_legacy = 0;
    failed = ioctlsocket(dcsocket, FIONBIO, &flags);
    failed_legacy = ioctlsocket(dcsocket_legacy, FIONBIO, &flags);
    if((failed == SOCKET_ERROR) || (failed_legacy == SOCKET_ERROR)) {
        log_error("ioctlsocket");
        return -1;
    }
#else
    fcntl(dcsocket, F_SETFL, O_NONBLOCK);
    fcntl(dcsocket_legacy, F_SETFL, O_NONBLOCK);
#endif

    return 0;
}

int recv_response(unsigned char *buffer, int timeout) {
    int start = time_in_usec();
    int rv = -1;
#if(SAVE_MY_FANS != 0)
    struct timespec pausetime = {0}, pauseremain = {0};
#endif

    while(((time_in_usec() - start) < timeout) && (rv == -1)) {
        rv = recv(global_socket, (void *)buffer, 2048, 0);
        // 100Mbit/s is 10 nanoseconds, but that's reportedly a little slow.
        // 5 is better, but still a bit slow. So let's do 1 nanosecond.
        // There's no picosecond sleep, so this is about as good as it gets.
#if(SAVE_MY_FANS != 0)
        pausetime.tv_nsec = SAVE_MY_FANS; // Now it's configurable from Makefile.cfg
        nanosleep(&pausetime, &pauseremain);
#endif
    }

    return rv;
}

int send_command(char *command, unsigned int addr, unsigned int size, unsigned char *data,
                 unsigned int dsize) {
    unsigned char c_buff[2048];
    unsigned int tmp;
    int error = 0;

    memcpy(c_buff, command, 4);
    tmp = htonl(addr);
    memcpy(c_buff + 4, &tmp, 4);
    tmp = htonl(size);
    memcpy(c_buff + 8, &tmp, 4);
    if(data != 0)
        memcpy(c_buff + 12, data, dsize);

    error = send(global_socket, (void *)c_buff, 12 + dsize, 0);

    if(error == -1) {
#ifndef __MINGW32__
        if(errno == EAGAIN)
            return 0;
        fprintf(stderr, "error: %s\n", strerror(errno));
#else
        /* WSAEWOULDBLOCK is a non-fatal error,  so continue */
        if(WSAGetLastError() == WSAEWOULDBLOCK)
            return 0;

        fprintf(stderr, "error: %d\n", WSAGetLastError());
#endif

        return -1;
    }

    return 0;
}

unsigned int upload(char *filename, unsigned int address) {
    int inputfd;
    int size = 0;
    int sectsize;
    unsigned char *inbuf;

    double stime, etime;
#ifdef WITH_BFD
    bfd *somebfd;
#else
    Elf *elf;
    Elf32_Ehdr *ehdr;
    Elf32_Shdr *shdr;
    Elf_Scn *section = NULL;
    Elf_Data *data;
    char *section_name;
    size_t index;
#endif

#ifdef WITH_BFD
    if((somebfd = bfd_openr(filename, 0))) {
        if(bfd_check_format(somebfd, bfd_object)) {
            /* try bfd first */
            asection *section;

            printf("File format is %s, ", somebfd->xvec->name);
            address = somebfd->start_address;
            size = 0;
            printf("start address is 0x%08x\n", address);

            gettimeofday(&starttime, 0);

            for(section = somebfd->sections; section != NULL; section = section->next) {
                if((section->flags & SEC_HAS_CONTENTS) && (section->flags & SEC_LOAD)) {
                    sectsize = bfd_section_size(section);
                    printf("Section %s, ", section->name);
                    printf("lma 0x%x, ", (unsigned int)section->lma);
                    printf("size %d\n", sectsize);

                    if(sectsize) {
                        size += sectsize;
                        inbuf = malloc(sectsize);
                        bfd_get_section_contents(somebfd, section, inbuf, 0, sectsize);

                        if(send_data(inbuf, section->lma, sectsize) == -1)
                            return -1;

                        free(inbuf);
                    }
                }
            }

            bfd_close(somebfd);
            goto done_transfer;
        }

        bfd_close(somebfd);
    }
#else  /* !WITH_BFD -- use libelf */
    if(elf_version(EV_CURRENT) == EV_NONE) {
        fprintf(stderr, "libelf initialization error: %s\n", elf_errmsg(-1));
        return -1;
    }

    if((inputfd = open(filename, O_RDONLY | O_BINARY)) < 0) {
        log_error(filename);
        return -1;
    }

    if(!(elf = elf_begin(inputfd, ELF_C_READ, NULL))) {
        fprintf(stderr, "Cannot read ELF file: %s\n", elf_errmsg(-1));
        return -1;
    }

    if(elf_kind(elf) == ELF_K_ELF) {
        if(!(ehdr = elf32_getehdr(elf))) {
            fprintf(stderr, "Unable to read ELF header: %s\n", elf_errmsg(-1));
            return -1;
        }

        address = ehdr->e_entry;
        printf("File format is ELF, start address is 0x%08x\n", address);

        /* Retrieve the index of the ELF section containing the string table of
           section names */
        if(elf_getshdrstrndx(elf, &index)) {
            fprintf(stderr, "Unable to read section index: %s\n", elf_errmsg(-1));
            return -1;
        }

        gettimeofday(&starttime, 0);
        while((section = elf_nextscn(elf, section))) {
            if(!(shdr = elf32_getshdr(section))) {
                fprintf(stderr, "Unable to read section header: %s\n", elf_errmsg(-1));
                return -1;
            }

            if(!(section_name = elf_strptr(elf, index, shdr->sh_name))) {
                fprintf(stderr, "Unable to read section name: %s\n", elf_errmsg(-1));
                return -1;
            }

            if(!shdr->sh_addr)
                continue;

            /* Check if there's some data to upload. */
            data = elf_getdata(section, NULL);
            if(!data->d_buf || !data->d_size)
                continue;

            printf("Section %s, lma 0x%08x, size %d\n", section_name, shdr->sh_addr, shdr->sh_size);
            size += shdr->sh_size;

            do {
                if(send_data(data->d_buf, shdr->sh_addr + data->d_off, data->d_size) == -1)
                    return -1;
            } while((data = elf_getdata(section, data)));
        }

        elf_end(elf);
        close(inputfd);
        goto done_transfer;
    } else {
        elf_end(elf);
        close(inputfd);
    }
#endif /* WITH_BFD */
    /* if all else fails, send raw bin */
    inputfd = open(filename, O_RDONLY | O_BINARY);

    if(inputfd < 0) {
        log_error(filename);
        return -1;
    }

    printf("File format is raw binary, start address is 0x%08x\n", address);

    size = lseek(inputfd, 0, SEEK_END);
    lseek(inputfd, 0, SEEK_SET);

    inbuf = malloc(size);
    read(inputfd, inbuf, size);
    close(inputfd);

    // v2.0.0+ Data transfer timekeeping is inside send_data() now
    if(send_data(inbuf, address, size) == -1)
        return -1;

done_transfer:
    stime = starttime.tv_sec + starttime.tv_usec / 1000000.0;
    etime = endtime.tv_sec + endtime.tv_usec / 1000000.0;

    printf("Transferred %d bytes at %f bytes / sec\n", size, (double)size / (etime - stime));
    fflush(stdout);

    return address;
}

int download(char *filename, unsigned int address, unsigned int size, unsigned int quiet) {
    int outputfd;

    unsigned char *data;
    double stime, etime;

    outputfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);

    if(outputfd < 0) {
        log_error(filename);
        return -1;
    }

    data = malloc(size);

    recv_data(data, address, size, 0);

    printf("Received %d bytes\n", size);

    stime = starttime.tv_sec + starttime.tv_usec / 1000000.0;
    etime = endtime.tv_sec + endtime.tv_usec / 1000000.0;

    printf("Transferred at %f bytes / sec\n", (double)size / (etime - stime));
    fflush(stdout);

    write(outputfd, data, size);

    close(outputfd);
    free(data);

    return 0;
}

int execute(unsigned int address, unsigned int console, unsigned int cdfsredir) {
    unsigned char buffer[2048];

    if(!legacy || force_legacy) {// dcload-ip v2+ conditions
        printf("Sending execute command (0x%08x, console=%d, cdfsredir=%d)...",
               address | 0xa0000000, console, cdfsredir);
    }
    else {
        printf("Sending execute command (0x%08x, console=%d, cdfsredir=%d)...", address, console,
               cdfsredir);
    }

    do
        send_cmd(CMD_EXECUTE, address, (cdfsredir << 1) | console, NULL, 0);
    while(recv_response(buffer, PACKET_TIMEOUT) == -1);

    printf("executing\n");
    return 0;
}

int do_console(char *path, char *isofile) {
    int isofd = 0;
    unsigned char buffer[2048];
    struct timespec time = {0}, remain = {0};

    if(isofile) {
        isofd = open(isofile, O_RDONLY | O_BINARY);
        if(isofd < 0)
            log_error(isofile);
    }

#ifndef __MINGW32__
    if (!nochroot && path) {
        if (chroot(path))
            log_error(path);
    }
#endif

    while(1) {
        fflush(stdout);

        while(recv_response(buffer, PACKET_TIMEOUT) == -1)
#if (SAVE_MY_FANS != 0)
        nanosleep(&time, &remain); /* Sleep for 0ns, which is just going to yield the thread. */
#else
        ; /* Spin thread until a packet arrives. */
#endif

        if(!(memcmp(buffer, CMD_EXIT, 4)))
            return -1;
        if(!(memcmp(buffer, CMD_FSTAT, 4)))
            CatchError(dc_fstat(buffer));
        if(!(memcmp(buffer, CMD_WRITE_OLD, 4)))
            CatchError(dc_write(buffer));
        if(!(memcmp(buffer, CMD_WRITE, 4)))
            CatchError(dc_write(buffer));
        if(!(memcmp(buffer, CMD_READ, 4)))
            CatchError(dc_read(buffer));
        if(!(memcmp(buffer, CMD_OPEN, 4)))
            CatchError(dc_open(buffer));
        if(!(memcmp(buffer, CMD_CLOSE, 4)))
            CatchError(dc_close(buffer));
        if(!(memcmp(buffer, CMD_CREAT, 4)))
            CatchError(dc_creat(buffer));
        if(!(memcmp(buffer, CMD_LINK, 4)))
            CatchError(dc_link(buffer));
        if(!(memcmp(buffer, CMD_UNLINK, 4)))
            CatchError(dc_unlink(buffer));
        if(!(memcmp(buffer, CMD_CHDIR, 4)))
            CatchError(dc_chdir(buffer));
        if(!(memcmp(buffer, CMD_CHMOD, 4)))
            CatchError(dc_chmod(buffer));
        if(!(memcmp(buffer, CMD_LSEEK, 4)))
            CatchError(dc_lseek(buffer));
        if(!(memcmp(buffer, CMD_TIME, 4)))
            CatchError(dc_time(buffer));
        if(!(memcmp(buffer, CMD_STAT, 4)))
            CatchError(dc_stat(buffer));
        if(!(memcmp(buffer, CMD_UTIME, 4)))
            CatchError(dc_utime(buffer));
        if(!(memcmp(buffer, CMD_BAD, 4)))
            fprintf(stderr, "command 15 should not happen... (but it did)\n");
        if(!(memcmp(buffer, CMD_OPENDIR, 4)))
            CatchError(dc_opendir(buffer));
        if(!(memcmp(buffer, CMD_CLOSEDIR, 4)))
            CatchError(dc_closedir(buffer));
        if(!(memcmp(buffer, CMD_READDIR, 4)))
            CatchError(dc_readdir(buffer));
        if(!(memcmp(buffer, CMD_CDFSREAD, 4)))
            CatchError(dc_cdfs_redir_read_sectors(isofd, buffer));
        if(!(memcmp(buffer, CMD_GDBPACKET, 4)))
            CatchError(dc_gdbpacket(buffer));
        if(!(memcmp(buffer, CMD_REWINDDIR, 4)))
            CatchError(dc_rewinddir(buffer));
    }

    return 0;
}

int open_gdb_socket(int port) {
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    gdb_server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef __MINGW32__
    if(gdb_server_socket == INVALID_SOCKET) {
#else
    if(gdb_server_socket < 0) {
#endif
        log_error("error creating gdb server socket");
        return -1;
    }

    const int enable_reuse_addr = 1;
    int checkopt = setsockopt(gdb_server_socket, SOL_SOCKET, SO_REUSEADDR, &enable_reuse_addr,
                              sizeof(enable_reuse_addr));
#ifdef __MINGW32__
    if(checkopt == SOCKET_ERROR) {
#else
    if(checkopt < 0) {
#endif
        log_error("warning: failed to set gdb socket options");
    }

    int checkbind = bind(gdb_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
#ifdef __MINGW32__
    if(checkbind == SOCKET_ERROR) {
#else
    if(checkbind < 0) {
#endif
        log_error("error binding gdb server socket");
        return -1;
    }

    int checklisten = listen(gdb_server_socket, 0);
#ifdef __MINGW32__
    if(checklisten == SOCKET_ERROR) {
#else
    if(checklisten < 0) {
#endif
        log_error("error listening to gdb server socket");
        return -1;
    }

    return 0;
}

#ifdef __MINGW32__
#define AVAILABLE_OPTIONS		"x:u:d:a:s:t:m:i:nlqhrg"
#else
#define AVAILABLE_OPTIONS		"x:u:d:a:s:t:m:c:i:nlqhrg"
#endif

int main(int argc, char *argv[]) {
    unsigned int address = 0x0c010000;
    unsigned int size = 0;
    unsigned int console = 1;
    unsigned int quiet = 0;
    unsigned char command = 0;
    unsigned int cdfs_redir = 0;
    int someopt;

    /* Dynamically allocated, so it should be freed */
    char *filename = 0;
    char *isofile = 0;
    char *hostname = strdup(DREAMCAST_IP);
    char *cleanlist[4] = {0, 0, 0, 0};

    if(argc < 2) {
        usage();
        return 0;
    }

#ifdef __MINGW32__
    if(start_ws())
        return -1;
#endif

    someopt = getopt(argc, argv, AVAILABLE_OPTIONS);
    while(someopt > 0) {
        switch(someopt) {
        case 'x':
            if(command) {
                fprintf(stderr, "You can only specify one of -x, -u, -d, and -r\n");
                goto doclean;
            }
            command = 'x';
            filename = malloc(strlen(optarg) + 1);
            cleanlist[0] = filename;
            strcpy(filename, optarg);
            break;
        case 'u':
            if(command) {
                fprintf(stderr, "You can only specify one of -x, -u, -d, and -r\n");
                goto doclean;
            }
            command = 'u';
            filename = malloc(strlen(optarg) + 1);
            cleanlist[0] = filename;
            strcpy(filename, optarg);
            break;
        case 'd':
            if(command) {
                fprintf(stderr, "You can only specify one of -x, -u, -d, and -r\n");
                goto doclean;
            }
            command = 'd';
            filename = malloc(strlen(optarg) + 1);
            cleanlist[0] = filename;
            strcpy(filename, optarg);
            break;
        case 'm':
            if(path) {
                fprintf(stderr, "-m and -c options are mutually exclusive, choose one\n");
                goto doclean;
            }
            nochroot = 1;
            path = realpath(optarg, NULL);
            if(path == NULL) {
                fprintf(stderr, "-m option with invalid path '%s'  \n", optarg);
                goto doclean;
            }
            set_mappath(path);
            cleanlist[1] = path;
            break;
#ifndef __MINGW32__
        case 'c':
            if(path) {
                fprintf(stderr, "-m and -c options are mutually exclusive, choose one\n");
                goto doclean;
            }
            nochroot = 0;
            path = malloc(strlen(optarg) + 1);
            cleanlist[1] = path;
            strcpy(path, optarg);
            break;
#endif
        case 'i':
            cdfs_redir = 1;
            isofile = malloc(strlen(optarg) + 1);
            cleanlist[2] = isofile;
            strcpy(isofile, optarg);
            break;
        case 'a':
            address = strtoul(optarg, NULL, 0);
            break;
        case 's':
            size = strtoul(optarg, NULL, 0);
            break;
        case 't':
            hostname = malloc(strlen(optarg) + 1);
            cleanlist[3] = hostname;
            strcpy(hostname, optarg);

            unsigned int portcheck = 0;
            while((hostname[portcheck] != '\0') && (hostname[portcheck] != ':')) {
                portcheck++;
            }

            if(hostname[portcheck] == ':') // We have a dcload IP port override
            {
                // Null-terminate the IP string by overwriting the ':'
                hostname[portcheck++] = '\0';
                dcload_portnum = 0; // clear the v2.0.0+ default port

                // Fill in port override
                while(hostname[portcheck] != '\0') {
                    dcload_portnum *= 10;
                    dcload_portnum += hostname[portcheck++] - '0';
                }
            }

            break;
        case 'n':
            console = 0;
            break;
        case 'l':
            force_legacy = 1;
            break;
        case 'q':
            quiet = 1;
            break;
        case 'h':
            usage();
            cleanup(cleanlist);
            return 0;
            break;
        case 'r':
            if(command) {
                fprintf(stderr, "You can only specify one of -x, -u, -d, and -r\n");
                goto doclean;
            }
            command = 'r';
            break;
        case 'g':
            printf("Starting a GDB server on port 2159\n");
            open_gdb_socket(2159);
            gdb_socket_started = 1;
            break;
        default:
            /* The user obviously mistyped something */
            usage();
            goto doclean;
            break;
        }
        someopt = getopt(argc, argv, AVAILABLE_OPTIONS);
    }

    if(quiet)
        printf("Quiet download\n");

    if(cdfs_redir & (!console))
        console = 1;

    if(console & (command == 'x'))
        printf("Console enabled\n");

    if(path) {
        if(nochroot) {
            printf("Mapping /pc/ to <%s>\n", path);
        } else {
            printf("Chrooting to <%s>\n", path);
        }
    }

    if(cdfs_redir & (command == 'x'))
        printf("Cdfs redirection enabled\n");

    if(open_sockets(hostname) < 0) // Random port socket for dcload >= 2.0.0
    {
        fprintf(stderr, "Error opening sockets\n");
        goto doclean;
    }

    switch(command) {
    case 'x':
        printf("Upload <%s>\n", filename);
        address = upload(filename, address);

        if(address == -1)
            goto doclean;

        if(!legacy || force_legacy) // force_legacy is only valid for dcload-ip v2+
        {
            // Supposed to use the uncached area for this kind of thing, which dcload v2.0.0+ does
            printf("Executing at <0x%x>\n", address | 0xa0000000);
        }
        else { // legacy = 1
            printf("Executing at <0x%x>\n", address);
        }

        if(execute(address, console, cdfs_redir))
            goto doclean;
        if(console)
            do_console(path, isofile);
        break;
    case 'u':
        printf("Upload <%s> at <0x%x>\n", filename, address);
        if(upload(filename, address))
            goto doclean;
        break;
    case 'd':
        if(!size) {
            fprintf(stderr, "You must specify a size (-s <size>) with download (-d <filename>)\n");
            goto doclean;
        }
        printf("Download %d bytes at <0x%x> to <%s>\n", size, address, filename);
        if(download(filename, address, size, quiet) == -1)
            goto doclean;
        break;
    case 'r':
        printf("Resetting...\n");
        if(send_command(CMD_REBOOT, 0, 0, NULL, 0) == -1)
            goto doclean;
        break;
    default:
        usage();
        break;
    }

    cleanup(cleanlist);
    return 0;

/* Failed (I hate gotos...) */
doclean:
    cleanup(cleanlist);
    return -1;
}
