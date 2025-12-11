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

#include "config.h"

#ifdef WITH_BFD
#include <bfd.h>
#else
#ifdef MACOS
#include <libelf/libelf.h>
#else
#include <libelf.h>
#endif
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>
#ifndef __MINGW32__
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#endif

#include "syscalls.h"
#include "dc-io.h"
#include "commands.h"

#include "utils.h"

int _nl_msg_cat_cntr;

#define VERSION PACKAGE_VERSION
#define DCTOOL_LEGACY_SYSCALL_PORT 31313
#define DCTOOL_DEFAULT_SYSCALL_PORT 53535

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef HAVE_GETOPT
int opterr = 1,
    optind = 1,
    optopt,
    optreset;
char *optarg;

#define BADCH (int)'?'
#define BADARG (int)':'
#define EMSG ""

char *__progname = PACKAGE;

int getopt(int nargc, char *const *nargv, const char *ostr)
{
  extern char *__progname;
  static char *place = EMSG;
  char *oli;
  int ret;

  if (optreset || !*place)
  {
    optreset = 0;
    if (optind >= nargc || *(place = nargv[optind]) != '-')
    {
      place = EMSG;
      return (-1);
    }
    if (place[1] && *++place == '-')
    {
      ++optind;
      place = EMSG;
      return (-1);
    }
  }
  if ((optopt = (int)*place++) == (int)':' ||
      !(oli = strchr(ostr, optopt)))
  {
    if (optopt == (int)'-')
      return (-1);
    if (!*place)
      ++optind;
    if (opterr && *ostr != ':')
      (void)fprintf(stderr,
                    "%s: illegal option -- %c\n", __progname, optopt);
    return (BADCH);
  }
  if (*++oli != ':')
  {
    optarg = NULL;
    if (!*place)
      ++optind;
  }
  else
  {
    if (*place)
      optarg = place;
    else if (nargc <= ++optind)
    {
      place = EMSG;
      if (*ostr == ':')
        ret = BADARG;
      else
        ret = BADCH;
      if (opterr)
        (void)fprintf(stderr,
                      "%s: option requires an argument -- %c\n",
                      __progname, optopt);
      return (ret);
    }
    else
      optarg = nargv[optind];
    place = EMSG;
    ++optind;
  }
  return (optopt);
}
#else
extern char *optarg;
#endif

int gdb_socket_started = 0;
char *path = 0;
#ifdef __MINGW32__
#define bzero(b, len) (memset((b), '\0', (len)), (void)0)
SOCKET dcsocket_legacy = 0;
SOCKET dcsocket = 0;
SOCKET gdb_server_socket = 0;
SOCKET socket_fd = 0;
SOCKET global_socket = 0;
#else
int dcsocket_legacy = 0;
int dcsocket = 0;
int gdb_server_socket = -1;
int socket_fd = 0;
int global_socket = 0;
unsigned int nochroot = 0;
#endif

void cleanup(char **fnames)
{
  int counter = 0;

  for (; counter < 4; counter++)
  {
    if (fnames[counter] != 0)
    {
      free(fnames[counter]);
    }
  }

#ifdef __MINGW32__
  if (dcsocket)
  {
    closesocket(dcsocket);
  }

  if (dcsocket_legacy)
  {
    closesocket(dcsocket_legacy);
  }
#else
  if (dcsocket)
  {
    close(dcsocket);
  }

  if (dcsocket_legacy)
  {
    close(dcsocket_legacy);
  }
#endif

  if (gdb_socket_started)
  {
    gdb_socket_started = 0;

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

static inline unsigned int time_in_usec(void)
{
  struct timeval thetime;
  gettimeofday(&thetime, NULL);
  return (unsigned int)(thetime.tv_sec * 1000000) + (unsigned int)thetime.tv_usec;
}

#define BBA_MODEL 0400
#define LAN_MODEL 0300

unsigned int installed_adapter = 0;
unsigned int legacy = 0;
unsigned int force_legacy = 0;
unsigned int fast_mode = 0;

#define BBA_RX_FIFO_DELAY_TIME DREAMCAST_BBA_RX_FIFO_DELAY_TIME
#define LAN_RX_FIFO_DELAY_TIME DREAMCAST_LAN_RX_FIFO_DELAY_TIME

unsigned int rx_fifo_delay = 100000 / 51;

#define BBA_RX_FIFO_DELAY_COUNT DREAMCAST_BBA_RX_FIFO_DELAY_COUNT
#define LAN_RX_FIFO_DELAY_COUNT DREAMCAST_LAN_RX_FIFO_DELAY_COUNT

unsigned int rx_fifo_delay_count = 15;

unsigned int encoded_tool_ver = 0;

struct timeval starttime = {0}, endtime = {0};

typedef struct
{
  unsigned int rtt_min;
  unsigned int rtt_us;
  unsigned int rtt_dev;
  unsigned int cwnd;
  unsigned int ssthresh;
  unsigned int btl_bw;
  unsigned int inflight;
  unsigned int total_sent;
  unsigned int total_acked;
  unsigned int total_lost;
  unsigned int last_ack_time;
  unsigned int round_count;
  unsigned int state;
  unsigned int filled_pipe;
  unsigned int full_bw;
  unsigned int full_bw_count;
  unsigned int burst_tokens;
  unsigned int last_burst_time;
} cc_state_t;

#define CC_STATE_STARTUP 0
#define CC_STATE_DRAIN 1
#define CC_STATE_STEADY 2

static cc_state_t cc = {
    .rtt_min = 200,
    .rtt_us = 500,
    .rtt_dev = 150,
    .cwnd = 48,
    .ssthresh = 128,
    .btl_bw = 0,
    .inflight = 0,
    .total_sent = 0,
    .total_acked = 0,
    .total_lost = 0,
    .last_ack_time = 0,
    .round_count = 0,
    .state = CC_STATE_STARTUP,
    .filled_pipe = 0,
    .full_bw = 0,
    .full_bw_count = 0,
    .burst_tokens = 24,
    .last_burst_time = 0};

static inline void cc_on_ack(unsigned int bytes, unsigned int rtt_sample, unsigned int lost)
{
  if (rtt_sample > 0 && rtt_sample < 500000)
  {
    if (rtt_sample < cc.rtt_min)
      cc.rtt_min = rtt_sample;

    int err = (int)rtt_sample - (int)cc.rtt_us;
    cc.rtt_us += err >> 3;
    int abs_err = err < 0 ? -err : err;
    cc.rtt_dev += (abs_err - (int)cc.rtt_dev) >> 2;
  }

  cc.total_acked += bytes;
  cc.last_ack_time = time_in_usec();

  if (bytes > 0 && cc.rtt_us > 0)
  {
    unsigned int bw = (bytes * 1000000ULL) / cc.rtt_us;
    if (bw > cc.btl_bw)
    {
      cc.btl_bw = bw;
      cc.full_bw_count = 0;
    }
    else
    {
      cc.full_bw_count++;
    }

    if (cc.state == CC_STATE_STARTUP && cc.full_bw_count >= 4)
    {
      cc.state = CC_STATE_DRAIN;
      cc.filled_pipe = 1;
    }

    if (cc.state == CC_STATE_DRAIN && cc.inflight < cc.cwnd)
    {
      cc.state = CC_STATE_STEADY;
    }
  }

  if (lost > 0)
  {
    cc.total_lost += lost;
    unsigned int reduction = cc.cwnd >> 3;
    if (reduction < 1)
      reduction = 1;
    if (cc.cwnd > 8 + reduction)
      cc.cwnd -= reduction;
    else
      cc.cwnd = 8;
  }
  else if (bytes > 0)
  {
    if (cc.state == CC_STATE_STARTUP)
    {
      cc.cwnd += (bytes > cc.cwnd) ? cc.cwnd : bytes;
      if (cc.cwnd > 192)
        cc.cwnd = 192;
    }
    else if (cc.state == CC_STATE_STEADY && cc.inflight >= cc.cwnd * 2 / 3)
    {
      cc.cwnd += 2;
      if (cc.cwnd > 128)
        cc.cwnd = 128;
    }
    else if (cc.state == CC_STATE_STEADY && cc.cwnd < cc.ssthresh)
    {
      cc.cwnd++;
    }
  }
}

static inline unsigned int cc_timeout(void)
{
  unsigned int t = cc.rtt_us + (cc.rtt_dev << 2);
  if (t < 1500)
    t = 1500;
  if (t > 50000)
    t = 50000;
  return t;
}

static inline unsigned int cc_pacing_delay(unsigned int pkt_size)
{
  if (cc.btl_bw == 0)
    return 50;

  unsigned int delay = (pkt_size * 1000000ULL) / cc.btl_bw;

  if (cc.state == CC_STATE_STARTUP)
    delay = delay / 2;
  else if (cc.state == CC_STATE_DRAIN)
    delay = delay * 5 / 4;

  if (delay < 10)
    delay = 10;
  if (delay > 2000)
    delay = 2000;

  return delay;
}

static inline int cc_can_send(void)
{
  return cc.inflight < cc.cwnd;
}

#ifdef __MINGW32__
static inline void usleep_precise(unsigned int us)
{
  if (us >= 1000)
    Sleep(us / 1000);
  else
  {
    volatile unsigned int s = time_in_usec();
    while (time_in_usec() - s < us)
      ;
  }
}
#else
static inline void usleep_precise(unsigned int us)
{
  if (us >= 20)
  {
    struct timespec ts = {.tv_sec = us / 1000000, .tv_nsec = (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
  }
}
#endif

static int sock_poll(int timeout_ms)
{
#ifdef __MINGW32__
  fd_set rfds;
  struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
  FD_ZERO(&rfds);
  FD_SET(global_socket, &rfds);
  return select(0, &rfds, NULL, NULL, &tv);
#else
  struct pollfd pfd = {.fd = global_socket, .events = POLLIN};
  return poll(&pfd, 1, timeout_ms);
#endif
}

static inline int sock_poll_us(int timeout_us)
{
  int ms = (timeout_us + 999) / 1000;
  if (ms < 1)
    ms = 1;
  return sock_poll(ms);
}

static int sock_recv(unsigned char *buf)
{
  return recv(global_socket, (void *)buf, 2048, 0);
}

static int sock_recv_timed(unsigned char *buf, int timeout_us)
{
  if (sock_poll_us(timeout_us) > 0)
    return sock_recv(buf);
  return -1;
}

static inline int sock_recv_nonblock(unsigned char *buf)
{
  if (sock_poll(0) > 0)
    return sock_recv(buf);
  return -1;
}

static void sock_drain(unsigned char *buf)
{
  int drained = 0;
  while (sock_poll(0) > 0 && drained < 64)
  {
    sock_recv(buf);
    drained++;
  }
}

static int sock_send(const void *buf, int len)
{
  return send(global_socket, buf, len, 0);
}

#define MAX_CONNECT_RETRIES 20
#define CONNECT_TIMEOUT_US 150000

void make_encoded_tool_version()
{
  if (force_legacy)
    return;

  const char *v = VERSION;
  unsigned int major = 0, minor = 0, patch = 0;
  unsigned int *cur = &major;

  while (*v)
  {
    if (*v == '.')
    {
      cur = (cur == &major) ? &minor : &patch;
    }
    else if (*v >= '0' && *v <= '9')
    {
      *cur = *cur * 10 + (*v - '0');
    }
    v++;
  }

  encoded_tool_ver = htonl((major << 16) | (minor << 8) | patch);
}

int prepare_comms(unsigned char *buffer)
{
  if (!installed_adapter)
  {
    int retry_count = 0;
    int connected = 0;
    int flip = 0;

    make_encoded_tool_version();

    if (force_legacy)
    {
      global_socket = dcsocket_legacy;
    }
    else
    {
      global_socket = dcsocket;
    }

    printf("Connecting to Dreamcast...\n");

    while (!connected && retry_count < MAX_CONNECT_RETRIES)
    {
      sock_drain(buffer);

      send_cmd(CMD_VERSION, encoded_tool_ver, 0, NULL, 0);

      int rv = sock_recv_timed(buffer, CONNECT_TIMEOUT_US);
      if (rv > 0)
      {
        if (!memcmp(((command_t *)buffer)->id, CMD_VERSION, 4))
        {
          connected = 1;
          break;
        }
      }

      retry_count++;

      if (!force_legacy && retry_count > 1)
      {
        flip ^= 0x1;
        global_socket = flip ? dcsocket_legacy : dcsocket;
      }

      unsigned int backoff = 15000 * (1 << (retry_count > 4 ? 4 : retry_count));
      usleep_precise(backoff);
    }

    if (!connected)
    {
      fprintf(stderr, "Failed to connect to Dreamcast after %d attempts\n", MAX_CONNECT_RETRIES);
      return -1;
    }

    if (global_socket == dcsocket)
    {
#ifdef __MINGW32__
      closesocket(dcsocket_legacy);
#else
      close(dcsocket_legacy);
#endif
    }
    else
    {
#ifdef __MINGW32__
      closesocket(dcsocket);
#else
      close(dcsocket);
#endif
    }

    installed_adapter = ntohl(((command_t *)buffer)->address);

    if (installed_adapter == BBA_MODEL)
    {
      printf("%s\n", ((command_t *)buffer)->data);
      if (force_legacy)
        legacy = 1;

      if (!fast_mode)
      {
        rx_fifo_delay = BBA_RX_FIFO_DELAY_TIME;
        rx_fifo_delay_count = BBA_RX_FIFO_DELAY_COUNT;
      }
      else
      {
        rx_fifo_delay = 0;
      }

      cc.cwnd = 96;
      cc.ssthresh = 192;
      cc.rtt_min = 150;
      cc.btl_bw = 12000000;
      cc.burst_tokens = 32;
    }
    else if (installed_adapter == LAN_MODEL)
    {
      printf("%s\n", ((command_t *)buffer)->data);
      if (force_legacy)
        legacy = 1;

      if (!fast_mode)
      {
        rx_fifo_delay = LAN_RX_FIFO_DELAY_TIME;
        rx_fifo_delay_count = LAN_RX_FIFO_DELAY_COUNT;
      }
      else
      {
        rx_fifo_delay = 0;
      }

      cc.cwnd = 24;
      cc.ssthresh = 48;
      cc.rtt_min = 400;
      cc.btl_bw = 1200000;
      cc.burst_tokens = 12;
    }
    else
    {
      installed_adapter = BBA_MODEL;
      legacy = 1;
      cc.cwnd = 16;
      cc.burst_tokens = 8;
    }

    unsigned char ping_buf[64];
    unsigned int rtt_samples[12];
    int valid = 0;

    for (int i = 0; i < 12 && valid < 8; i++)
    {
      sock_drain(ping_buf);
      memcpy(ping_buf, CMD_PING, 4);
      ((command_t *)ping_buf)->address = 0;
      ((command_t *)ping_buf)->size = 0;

      unsigned int t0 = time_in_usec();
      sock_send(ping_buf, COMMAND_LEN);

      if (sock_recv_timed(ping_buf, 15000) > 0 && !memcmp(ping_buf, CMD_PONG, 4))
      {
        unsigned int rtt = time_in_usec() - t0;
        if (rtt < 100000)
          rtt_samples[valid++] = rtt;
      }
      usleep_precise(2000);
    }

    if (valid >= 3)
    {
      for (int i = 0; i < valid - 1; i++)
      {
        for (int j = i + 1; j < valid; j++)
        {
          if (rtt_samples[j] < rtt_samples[i])
          {
            unsigned int tmp = rtt_samples[i];
            rtt_samples[i] = rtt_samples[j];
            rtt_samples[j] = tmp;
          }
        }
      }

      cc.rtt_min = rtt_samples[0];
      cc.rtt_us = rtt_samples[valid / 2];
      cc.rtt_dev = (rtt_samples[valid - 1] - rtt_samples[0]) / 4;
      if (cc.rtt_dev < 100)
        cc.rtt_dev = 100;

      printf("RTT: min=%u median=%u dev=%u us, Window: %u\n", cc.rtt_min, cc.rtt_us, cc.rtt_dev, cc.cwnd);
    }
  }

  return 0;
}

int recv_data(void *data, unsigned int dcaddr, unsigned int total, unsigned int quiet)
{
  unsigned char buffer[2048];
  unsigned int payload_sz = legacy ? 1024 : 1440;
  unsigned int num_chunks = (total + payload_sz - 1) / payload_sz;

  if (prepare_comms(buffer) < 0)
    return -1;

  unsigned char *map = (unsigned char *)calloc(1, (num_chunks + 7) / 8);
  unsigned int received = 0;

  gettimeofday(&starttime, 0);
  sock_drain(buffer);

  if (!quiet)
  {
    send_cmd(CMD_SENDBIN, dcaddr, total, NULL, 0);
  }
  else
  {
    send_cmd(CMD_SENDBINQ, dcaddr, total, NULL, 0);
  }

  unsigned int deadline = time_in_usec() + cc_timeout() * (num_chunks + 20);
  unsigned int last_rx = time_in_usec();
  unsigned int request_cooldown = 0;

  while (received < num_chunks && time_in_usec() < deadline)
  {
    int rv = sock_recv_timed(buffer, cc_timeout() / 2);

    if (rv > 0)
    {
      last_rx = time_in_usec();

      if (!memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4))
        break;

      if (!memcmp(((command_t *)buffer)->id, CMD_SENDBIN, 4) ||
          !memcmp(((command_t *)buffer)->id, CMD_SENDBINQ, 4))
      {
        unsigned int addr = ntohl(((command_t *)buffer)->address);
        unsigned int sz = ntohl(((command_t *)buffer)->size);

        if (addr >= dcaddr && addr < dcaddr + total)
        {
          unsigned int idx = (addr - dcaddr) / payload_sz;
          unsigned int byte = idx >> 3;
          unsigned int bit = idx & 7;

          if (idx < num_chunks && !((map[byte] >> bit) & 1))
          {
            map[byte] |= (1U << bit);
            received++;
            memcpy((unsigned char *)data + (addr - dcaddr), buffer + 12, sz);
            cc_on_ack(sz, time_in_usec() - last_rx, 0);
          }
        }
      }
    }
    else
    {
      unsigned int now = time_in_usec();
      if (now - last_rx > cc_timeout() && now > request_cooldown)
      {
        for (unsigned int c = 0; c < num_chunks; c++)
        {
          unsigned int byte = c >> 3;
          unsigned int bit = c & 7;
          if (!((map[byte] >> bit) & 1))
          {
            unsigned int req_addr = dcaddr + c * payload_sz;
            unsigned int req_sz = (total - c * payload_sz >= payload_sz) ? payload_sz : (total - c * payload_sz);
            send_cmd(CMD_SENDBINQ, req_addr, req_sz, NULL, 0);
            request_cooldown = now + cc_timeout() / 3;
            break;
          }
        }
      }
    }
  }

  for (int retry = 0; retry < 12 && received < num_chunks; retry++)
  {
    for (unsigned int c = 0; c < num_chunks; c++)
    {
      unsigned int byte = c >> 3;
      unsigned int bit = c & 7;
      if (!((map[byte] >> bit) & 1))
      {
        unsigned int req_addr = dcaddr + c * payload_sz;
        unsigned int req_sz = (total - c * payload_sz >= payload_sz) ? payload_sz : (total - c * payload_sz);

        sock_drain(buffer);
        send_cmd(CMD_SENDBINQ, req_addr, req_sz, NULL, 0);

        for (int att = 0; att < 5; att++)
        {
          int rv = sock_recv_timed(buffer, cc_timeout());
          if (rv > 0)
          {
            unsigned int addr = ntohl(((command_t *)buffer)->address);
            unsigned int sz = ntohl(((command_t *)buffer)->size);
            if (addr == req_addr)
            {
              map[byte] |= (1U << bit);
              received++;
              memcpy((unsigned char *)data + (addr - dcaddr), buffer + 12, sz);
              break;
            }
          }
        }
      }
    }
    usleep_precise(cc_timeout() / 4);
  }

  gettimeofday(&endtime, 0);
  free(map);
  return 0;
}

typedef struct
{
  unsigned int seq;
  unsigned int offset;
  unsigned int size;
  unsigned int send_time;
  unsigned int acked;
  unsigned int retries;
} pkt_meta_t;

#define TX_RING_SIZE 256
#define TX_RING_MASK (TX_RING_SIZE - 1)

static int tx_pipeline(unsigned char *addr, unsigned int dcaddr, unsigned int size)
{
  unsigned char pkt[2048];
  unsigned char ack[2048];
  unsigned int payload_sz = legacy ? 1024 : 1440;
  unsigned int total_pkts = (size + payload_sz - 1) / payload_sz;

  pkt_meta_t ring[TX_RING_SIZE];
  unsigned int ring_head = 0;
  unsigned int ring_tail = 0;
  unsigned int next_seq = 0;
  unsigned int acked = 0;

  command_window_t *wpkt = (command_window_t *)pkt;
  memcpy(wpkt->id, CMD_WLOAD, 4);
  wpkt->base_address = htonl(dcaddr);
  wpkt->total_size = htonl(size);
  wpkt->window_size = htonl(cc.cwnd);
  wpkt->seq_num = 0;

  sock_drain(ack);

  unsigned int t0 = time_in_usec();
  int init_ok = 0;

  for (int attempt = 0; attempt < 8 && !init_ok; attempt++)
  {
    sock_send(pkt, COMMAND_WINDOW_LEN);
    if (sock_recv_timed(ack, cc_timeout()) > 0 && !memcmp(ack, CMD_WLOAD, 4))
      init_ok = 1;
    else
      usleep_precise(5000 * (1 << attempt));
  }

  if (!init_ok)
    return -1;

  cc_on_ack(1, time_in_usec() - t0, 0);
  gettimeofday(&starttime, 0);

  unsigned int last_sack_time = time_in_usec();
  unsigned int sack_interval_pkts = cc.cwnd / 4;
  if (sack_interval_pkts < 3)
    sack_interval_pkts = 3;
  if (sack_interval_pkts > 16)
    sack_interval_pkts = 16;

  unsigned int pkts_since_sack = 0;
  unsigned int consecutive_timeouts = 0;

  while (acked < total_pkts && consecutive_timeouts < 40)
  {
    unsigned int inflight = (ring_head - ring_tail) & TX_RING_MASK;
    cc.inflight = inflight;

    while (cc_can_send() && next_seq < total_pkts && inflight < TX_RING_SIZE - 1)
    {
      unsigned int seq = next_seq;
      unsigned int offset = seq * payload_sz;
      unsigned int chunk = (size - offset < payload_sz) ? (size - offset) : payload_sz;

      memcpy(wpkt->id, CMD_WPART, 4);
      wpkt->base_address = htonl(dcaddr);
      wpkt->total_size = htonl(chunk);
      wpkt->window_size = htonl(cc.cwnd);
      wpkt->seq_num = htonl(seq);
      memcpy(wpkt->data, addr + offset, chunk);

      sock_send(pkt, COMMAND_WINDOW_LEN + chunk);

      ring[ring_head].seq = seq;
      ring[ring_head].offset = offset;
      ring[ring_head].size = chunk;
      ring[ring_head].send_time = time_in_usec();
      ring[ring_head].acked = 0;
      ring[ring_head].retries = 0;
      ring_head = (ring_head + 1) & TX_RING_MASK;

      next_seq++;
      inflight++;
      pkts_since_sack++;
      cc.inflight = inflight;

      unsigned int pacing = cc_pacing_delay(chunk);
      if (pacing > 5)
        usleep_precise(pacing);
    }

    unsigned int now = time_in_usec();
    int need_sack = (pkts_since_sack >= sack_interval_pkts) ||
                    (next_seq >= total_pkts) ||
                    (!cc_can_send() && inflight > 0) ||
                    (now - last_sack_time > cc_timeout() / 3);

    if (need_sack && inflight > 0)
    {
      memcpy(pkt, CMD_WSACK, 4);
      ((command_t *)pkt)->address = htonl(ring[ring_tail].seq);
      ((command_t *)pkt)->size = 0;

      t0 = time_in_usec();
      sock_send(pkt, COMMAND_LEN);

      int got_sack = 0;
      for (int try = 0; try < 4 && !got_sack; try++)
      {
        int rv = sock_recv_timed(ack, cc_timeout());
        if (rv > 0 && !memcmp(ack, CMD_WSACK, 4))
        {
          got_sack = 1;
          unsigned int rtt = time_in_usec() - t0;

          command_sack_t *sack = (command_sack_t *)ack;
          unsigned int boff = ntohl(sack->bitmap_offset);
          unsigned int new_acks = 0;
          unsigned int bytes_acked = 0;

          for (int i = 0; i < 4; i++)
          {
            unsigned int bits = ntohl(sack->bitmap[i]);
            for (int b = 0; b < 32; b++)
            {
              unsigned int seq = boff + i * 32 + b;
              if (seq < total_pkts && (bits & (1U << b)))
              {
                unsigned int idx = ring_tail;
                while (idx != ring_head)
                {
                  if (ring[idx].seq == seq && !ring[idx].acked)
                  {
                    ring[idx].acked = 1;
                    new_acks++;
                    bytes_acked += ring[idx].size;
                    acked++;
                    break;
                  }
                  idx = (idx + 1) & TX_RING_MASK;
                }
              }
            }
          }

          while (ring_tail != ring_head && ring[ring_tail].acked)
            ring_tail = (ring_tail + 1) & TX_RING_MASK;

          cc_on_ack(bytes_acked, rtt, 0);
          consecutive_timeouts = 0;

          sack_interval_pkts = cc.cwnd / 4;
          if (sack_interval_pkts < 3)
            sack_interval_pkts = 3;
          if (sack_interval_pkts > 16)
            sack_interval_pkts = 16;
        }
      }

      if (!got_sack)
      {
        consecutive_timeouts++;
        cc_on_ack(0, 0, 1);

        unsigned int idx = ring_tail;
        int resent = 0;
        while (idx != ring_head && resent < 3)
        {
          if (!ring[idx].acked && ring[idx].retries < 5)
          {
            unsigned int offset = ring[idx].offset;
            unsigned int chunk = ring[idx].size;

            memcpy(wpkt->id, CMD_WPART, 4);
            wpkt->base_address = htonl(dcaddr);
            wpkt->total_size = htonl(chunk);
            wpkt->window_size = htonl(cc.cwnd);
            wpkt->seq_num = htonl(ring[idx].seq);
            memcpy(wpkt->data, addr + offset, chunk);

            sock_send(pkt, COMMAND_WINDOW_LEN + chunk);
            ring[idx].send_time = time_in_usec();
            ring[idx].retries++;
            resent++;
          }
          idx = (idx + 1) & TX_RING_MASK;
        }
      }

      pkts_since_sack = 0;
      last_sack_time = time_in_usec();
    }
  }

  memcpy(pkt, CMD_WDONE, 4);
  ((command_t *)pkt)->address = 0;
  ((command_t *)pkt)->size = htonl(acked);

  for (int i = 0; i < 6; i++)
  {
    sock_send(pkt, COMMAND_LEN);
    if (sock_recv_timed(ack, cc_timeout()) > 0)
      break;
    usleep_precise(3000 * (1 << i));
  }

  gettimeofday(&endtime, 0);

  return (acked == total_pkts) ? 0 : -1;
}

int send_data(unsigned char *addr, unsigned int dcaddr, unsigned int size)
{
  unsigned char buffer[2048] = {0};
  unsigned char *i = 0;
  unsigned int a = dcaddr;
  unsigned int count = 0;
  unsigned int total_sent = 0;
  unsigned int resend_attempts = 0;
  unsigned int payload_size = legacy ? 1024 : 1440;

  if (!size)
    return -1;

  if (prepare_comms(buffer) < 0)
    return -1;

  if (!legacy && !force_legacy && cc.filled_pipe)
  {
    int result = tx_pipeline(addr, dcaddr, size);
    if (result == 0)
      return 0;
  }

  sock_drain(buffer);

  int got_ack = 0;
  for (int attempts = 0; !got_ack && attempts < 20; attempts++)
  {
    send_cmd(CMD_LOADBIN, dcaddr, size, NULL, 0);
    if (sock_recv_timed(buffer, cc_timeout()) > 0 &&
        !memcmp(((command_t *)buffer)->id, CMD_LOADBIN, 4))
    {
      got_ack = 1;
    }
    else
    {
      usleep_precise(3000 * (1 << (attempts > 4 ? 4 : attempts)));
    }
  }

  if (!got_ack)
    return -1;

  gettimeofday(&starttime, 0);

  unsigned int burst_count = rx_fifo_delay_count;
  if (burst_count < 8)
    burst_count = 8;
  unsigned int burst_delay = rx_fifo_delay;
  if (burst_delay < 200)
    burst_delay = 200;
  unsigned int burst_sent = 0;
  unsigned int adaptive_burst = burst_count;

  for (i = addr; i < (addr + size); i += payload_size)
  {
    unsigned int chunk = ((addr + size - i) >= payload_size) ? payload_size : (addr + size - i);
    send_cmd(CMD_PARTBIN, dcaddr, chunk, i, chunk);
    dcaddr += payload_size;
    total_sent++;
    burst_sent++;

    if (burst_sent >= adaptive_burst)
    {
      usleep_precise(burst_delay);
      burst_sent = 0;

      if (total_sent > 100 && adaptive_burst < burst_count * 2)
        adaptive_burst++;
    }
  }

  usleep_precise(burst_delay * 2);

  got_ack = 0;
  for (int attempts = 0; !got_ack && attempts < 20; attempts++)
  {
    send_cmd(CMD_DONEBIN, 0, 0, NULL, 0);
    if (sock_recv_timed(buffer, cc_timeout()) > 0 &&
        !memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4))
    {
      got_ack = 1;
    }
    else
    {
      usleep_precise(2000 * (1 << (attempts > 4 ? 4 : attempts)));
    }
  }

  while (ntohl(((command_t *)buffer)->size) != 0 && resend_attempts < 50)
  {
    unsigned int resend_addr = ntohl(((command_t *)buffer)->address);
    unsigned int resend_size = ntohl(((command_t *)buffer)->size);

    send_cmd(CMD_PARTBIN, resend_addr, resend_size, addr + (resend_addr - a), resend_size);
    usleep_precise(burst_delay);
    cc_on_ack(0, 0, 1);

    got_ack = 0;
    for (int attempts = 0; !got_ack && attempts < 6; attempts++)
    {
      send_cmd(CMD_DONEBIN, 0, 0, NULL, 0);
      if (sock_recv_timed(buffer, cc_timeout()) > 0 &&
          !memcmp(((command_t *)buffer)->id, CMD_DONEBIN, 4))
      {
        got_ack = 1;
      }
      else
      {
        usleep_precise(2000 * (1 << attempts));
      }
    }

    if (!got_ack)
      break;

    resend_attempts++;
  }

  gettimeofday(&endtime, 0);
  return 0;
}

void usage(void)
{
  printf("\n%s %s by Andrew \"ADK\" Kieschnick\nAugmented by Moopthehedgehog\n\n", PACKAGE, VERSION);
  printf("-x <filename>  Upload and execute <filename>\n");
  printf("-u <filename>  Upload <filename>\n");
  printf("-d <filename>  Download to <filename>\n");
  printf("-a <address>   Set address to <address> (default: 0x0c010000)\n");
  printf("-s <size>      Set size to <size>\n");
  printf("-t <ip>:<port> Connect to <ip>:<port> (port optional, default: %s:53535)\n", DREAMCAST_IP);
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
  printf("-f             Disable FIFO delays for MUCH faster speeds (may increase packet loss)\n");
  printf("-h             Usage information (you\'re looking at it)\n\n");
}

#ifdef __MINGW32__
int start_ws()
{
  WSADATA wsaData;
  int failed = 0;
  failed = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (failed != NO_ERROR)
  {
    log_error("WSAStartup");
    return 1;
  }

  return 0;
}
#endif

unsigned int dcload_portnum = DCTOOL_DEFAULT_SYSCALL_PORT;

int open_sockets(char *hostname)
{
  struct sockaddr_in sin;
  struct sockaddr_in sin_legacy;
  struct hostent *host = 0;

  dcsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  dcsocket_legacy = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

#ifndef __MINGW32__
  if ((dcsocket < 0) || (dcsocket_legacy < 0))
  {
#else
  if ((dcsocket == INVALID_SOCKET) || (dcsocket_legacy == INVALID_SOCKET))
  {
#endif
    log_error("socket");
    return -1;
  }

  bzero(&sin, sizeof(sin));
  bzero(&sin_legacy, sizeof(sin_legacy));

  sin.sin_family = AF_INET;
  sin_legacy.sin_family = AF_INET;

  sin.sin_port = htons(dcload_portnum);
  sin_legacy.sin_port = htons(DCTOOL_LEGACY_SYSCALL_PORT);

  cleanup_ip_address(hostname);
  host = gethostbyname(hostname);

  if (!host)
  {
    log_error("gethostbyname");
    return -1;
  }

  memcpy((char *)&sin.sin_addr, host->h_addr, host->h_length);
  memcpy((char *)&sin_legacy.sin_addr, host->h_addr, host->h_length);

  if (connect(dcsocket_legacy, (struct sockaddr *)&sin_legacy, sizeof(sin_legacy)) < 0)
  {
    log_error("connect_legacy");
    return -1;
  }

  if (connect(dcsocket, (struct sockaddr *)&sin, sizeof(sin)) < 0)
  {
    log_error("connect");
    return -1;
  }

#ifdef __MINGW32__
  unsigned long flags = 1;
  int failed = 0;
  int failed_legacy = 0;
  failed = ioctlsocket(dcsocket, FIONBIO, &flags);
  failed_legacy = ioctlsocket(dcsocket_legacy, FIONBIO, &flags);
  if ((failed == SOCKET_ERROR) || (failed_legacy == SOCKET_ERROR))
  {
    log_error("ioctlsocket");
    return -1;
  }
#else
  fcntl(dcsocket, F_SETFL, O_NONBLOCK);
  fcntl(dcsocket_legacy, F_SETFL, O_NONBLOCK);
#endif

  int rcvbuf = 33554432;
  int sndbuf = 16777216;
#ifdef __MINGW32__
  setsockopt(dcsocket, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbuf, sizeof(rcvbuf));
  setsockopt(dcsocket, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, sizeof(sndbuf));
  setsockopt(dcsocket_legacy, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbuf, sizeof(rcvbuf));
  setsockopt(dcsocket_legacy, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, sizeof(sndbuf));
#else
  setsockopt(dcsocket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
  setsockopt(dcsocket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  setsockopt(dcsocket_legacy, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
  setsockopt(dcsocket_legacy, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
#endif

  return 0;
}

int recv_response(unsigned char *buffer, int timeout)
{
  return sock_recv_timed(buffer, timeout);
}

int send_command(char *command, unsigned int addr, unsigned int size, unsigned char *data, unsigned int dsize)
{
  unsigned char c_buff[2048];
  unsigned int tmp;
  int error = 0;

  memcpy(c_buff, command, 4);
  tmp = htonl(addr);
  memcpy(c_buff + 4, &tmp, 4);
  tmp = htonl(size);
  memcpy(c_buff + 8, &tmp, 4);
  if (data != 0)
    memcpy(c_buff + 12, data, dsize);

  error = sock_send(c_buff, 12 + dsize);

  if (error == -1)
  {
#ifndef __MINGW32__
    if (errno == EAGAIN)
      return 0;
#else
    if (WSAGetLastError() == WSAEWOULDBLOCK)
      return 0;
#endif

    return -1;
  }

  return 0;
}

unsigned int upload(char *filename, unsigned int address)
{
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
  if ((somebfd = bfd_openr(filename, 0)))
  {
    if (bfd_check_format(somebfd, bfd_object))
    {
      asection *section;

      printf("File format is %s, ", somebfd->xvec->name);
      address = somebfd->start_address;
      size = 0;
      printf("start address is 0x%08x\n", address);

      gettimeofday(&starttime, 0);

      for (section = somebfd->sections; section != NULL; section = section->next)
      {
        if ((section->flags & SEC_HAS_CONTENTS) && (section->flags & SEC_LOAD))
        {
          sectsize = bfd_section_size(section);
          printf("Section %s, ", section->name);
          printf("lma 0x%x, ", (unsigned int)section->lma);
          printf("size %d\n", sectsize);

          if (sectsize)
          {
            size += sectsize;
            inbuf = malloc(sectsize);
            bfd_get_section_contents(somebfd, section, inbuf, 0, sectsize);

            if (send_data(inbuf, section->lma, sectsize) == -1)
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
#else
  if (elf_version(EV_CURRENT) == EV_NONE)
  {
    fprintf(stderr, "libelf initialization error: %s\n", elf_errmsg(-1));
    return -1;
  }

  if ((inputfd = open(filename, O_RDONLY | O_BINARY)) < 0)
  {
    log_error(filename);
    return -1;
  }

  if (!(elf = elf_begin(inputfd, ELF_C_READ, NULL)))
  {
    fprintf(stderr, "Cannot read ELF file: %s\n", elf_errmsg(-1));
    return -1;
  }

  if (elf_kind(elf) == ELF_K_ELF)
  {
    if (!(ehdr = elf32_getehdr(elf)))
    {
      fprintf(stderr, "Unable to read ELF header: %s\n", elf_errmsg(-1));
      return -1;
    }

    address = ehdr->e_entry;
    printf("File format is ELF, start address is 0x%08x\n", address);

    if (elf_getshdrstrndx(elf, &index))
    {
      fprintf(stderr, "Unable to read section index: %s\n", elf_errmsg(-1));
      return -1;
    }

    gettimeofday(&starttime, 0);
    while ((section = elf_nextscn(elf, section)))
    {
      if (!(shdr = elf32_getshdr(section)))
      {
        fprintf(stderr, "Unable to read section header: %s\n", elf_errmsg(-1));
        return -1;
      }

      if (!(section_name = elf_strptr(elf, index, shdr->sh_name)))
      {
        fprintf(stderr, "Unable to read section name: %s\n", elf_errmsg(-1));
        return -1;
      }

      if (!shdr->sh_addr)
        continue;

      data = elf_getdata(section, NULL);
      if (!data->d_buf || !data->d_size)
        continue;

      printf("Section %s, lma 0x%08x, size %d\n", section_name,
             shdr->sh_addr, shdr->sh_size);
      size += shdr->sh_size;

      do
      {
        if (send_data(data->d_buf, shdr->sh_addr + data->d_off,
                      data->d_size) == -1)
          return -1;
      } while ((data = elf_getdata(section, data)));
    }

    elf_end(elf);
    close(inputfd);
    goto done_transfer;
  }
  else
  {
    elf_end(elf);
    close(inputfd);
  }
#endif
  inputfd = open(filename, O_RDONLY | O_BINARY);

  if (inputfd < 0)
  {
    log_error(filename);
    return -1;
  }

  printf("File format is raw binary, start address is 0x%08x\n", address);

  size = lseek(inputfd, 0, SEEK_END);
  lseek(inputfd, 0, SEEK_SET);

  inbuf = malloc(size);
  read(inputfd, inbuf, size);
  close(inputfd);

  if (send_data(inbuf, address, size) == -1)
    return -1;

done_transfer:
  stime = starttime.tv_sec + starttime.tv_usec / 1000000.0;
  etime = endtime.tv_sec + endtime.tv_usec / 1000000.0;

  printf("Transferred %d bytes at %f bytes / sec\n", size, (double)size / (etime - stime));
  fflush(stdout);

  return address;
}

int download(char *filename, unsigned int address,
             unsigned int size, unsigned int quiet)
{
  int outputfd;

  unsigned char *data;
  double stime, etime;

  outputfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);

  if (outputfd < 0)
  {
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

int execute(unsigned int address, unsigned int console, unsigned int cdfsredir)
{
  unsigned char buffer[2048];

  if (!legacy || force_legacy)
  {
    printf("Sending execute command (0x%08x, console=%d, cdfsredir=%d)...", address | 0xa0000000, console, cdfsredir);
  }
  else
  {
    printf("Sending execute command (0x%08x, console=%d, cdfsredir=%d)...", address, console, cdfsredir);
  }

  sock_drain(buffer);

  for (int attempts = 0; attempts < 20; attempts++)
  {
    send_cmd(CMD_EXECUTE, address, (cdfsredir << 1) | console, NULL, 0);
    if (sock_recv_timed(buffer, cc_timeout()) > 0 &&
        !memcmp(((command_t *)buffer)->id, CMD_EXECUTE, 4))
    {
      break;
    }
    usleep_precise(2000 * (1 << (attempts > 5 ? 5 : attempts)));
  }

  printf("executing\n");
  return 0;
}

#define CONSOLE_BATCH_MAX 4096
#define CONSOLE_BURST_THRESHOLD 32
#define CONSOLE_IDLE_BACKOFF_THRESHOLD 200
#define CONSOLE_MAX_POLL_MS 2
#define CONSOLE_RAPID_POLL_US 10
#define CONSOLE_HYPER_BURST 128

static inline unsigned int cmd_id(const unsigned char *b)
{
  return *(const unsigned int *)b;
}

int do_console(char *path, char *isofile)
{
  int isofd = 0;
  unsigned char buffer[2048];
  int rv;

  if (isofile)
  {
    isofd = open(isofile, O_RDONLY | O_BINARY);
    if (isofd < 0)
      log_error(isofile);
  }

#ifndef __MINGW32__
  if (!nochroot && path)
  {
    if (chroot(path))
      log_error(path);
  }
#endif

  static const unsigned int ID_EXIT = 0x54495845;
  static const unsigned int ID_WRIT = 0x54495257;
  static const unsigned int ID_WRTE = 0x45545257;
  static const unsigned int ID_FSTA = 0x41545346;
  static const unsigned int ID_READ = 0x44414552;
  static const unsigned int ID_OPEN = 0x4e45504f;
  static const unsigned int ID_CLOS = 0x534f4c43;
  static const unsigned int ID_CREA = 0x41455243;
  static const unsigned int ID_LINK = 0x4b4e494c;
  static const unsigned int ID_UNLK = 0x4b4c4e55;
  static const unsigned int ID_CHDR = 0x52444843;
  static const unsigned int ID_CHMD = 0x444d4843;
  static const unsigned int ID_LSEK = 0x4b45534c;
  static const unsigned int ID_TIME = 0x454d4954;
  static const unsigned int ID_STAT = 0x54415453;
  static const unsigned int ID_UTIM = 0x4d495455;
  static const unsigned int ID_BAD_ = 0x5f444142;
  static const unsigned int ID_OPND = 0x444e504f;
  static const unsigned int ID_CLSD = 0x44534c43;
  static const unsigned int ID_REDD = 0x44444552;
  static const unsigned int ID_CDFS = 0x53464443;
  static const unsigned int ID_GDBP = 0x50424447;
  static const unsigned int ID_RWND = 0x444e5752;
  static const unsigned int ID_CWRT = 0x54525743;

  unsigned int poll_us = CONSOLE_RAPID_POLL_US;
  unsigned int idle_count = 0;
  unsigned int burst_count = 0;
  unsigned int cwrt_burst = 0;
  unsigned int hyper_mode = 0;
  unsigned int last_activity = time_in_usec();

#define CATCH_ERR(x) \
  if (x)             \
  return -1

  while (1)
  {
    int batch_processed = 0;
    cwrt_burst = 0;

    int max_batch = hyper_mode ? CONSOLE_BATCH_MAX * 2 : CONSOLE_BATCH_MAX;

    for (int batch = 0; batch < max_batch; batch++)
    {
      rv = sock_recv_nonblock(buffer);
      if (rv <= 0)
        break;

      batch_processed++;
      idle_count = 0;
      last_activity = time_in_usec();

      unsigned int id = cmd_id(buffer);

      if (id == ID_EXIT)
        return -1;

      if (id == ID_CWRT)
      {
        dc_console_write(buffer);
        burst_count++;
        cwrt_burst++;

        if (cwrt_burst >= CONSOLE_HYPER_BURST)
        {
          hyper_mode = 1;
        }

        if (cwrt_burst >= 16)
        {
          console_tick();
          cwrt_burst = 0;
        }
        continue;
      }

      switch (id)
      {
      case ID_WRIT:
      case ID_WRTE:
        CATCH_ERR(dc_write(buffer));
        break;
      case ID_FSTA:
        CATCH_ERR(dc_fstat(buffer));
        break;
      case ID_READ:
        CATCH_ERR(dc_read(buffer));
        break;
      case ID_OPEN:
        CATCH_ERR(dc_open(buffer));
        break;
      case ID_CLOS:
        CATCH_ERR(dc_close(buffer));
        break;
      case ID_CREA:
        CATCH_ERR(dc_creat(buffer));
        break;
      case ID_LINK:
        CATCH_ERR(dc_link(buffer));
        break;
      case ID_UNLK:
        CATCH_ERR(dc_unlink(buffer));
        break;
      case ID_CHDR:
        CATCH_ERR(dc_chdir(buffer));
        break;
      case ID_CHMD:
        CATCH_ERR(dc_chmod(buffer));
        break;
      case ID_LSEK:
        CATCH_ERR(dc_lseek(buffer));
        break;
      case ID_TIME:
        CATCH_ERR(dc_time(buffer));
        break;
      case ID_STAT:
        CATCH_ERR(dc_stat(buffer));
        break;
      case ID_UTIM:
        CATCH_ERR(dc_utime(buffer));
        break;
      case ID_BAD_:
        break;
      case ID_OPND:
        CATCH_ERR(dc_opendir(buffer));
        break;
      case ID_CLSD:
        CATCH_ERR(dc_closedir(buffer));
        break;
      case ID_REDD:
        CATCH_ERR(dc_readdir(buffer));
        break;
      case ID_CDFS:
        CATCH_ERR(dc_cdfs_redir_read_sectors(isofd, buffer));
        break;
      case ID_GDBP:
        CATCH_ERR(dc_gdbpacket(buffer));
        break;
      case ID_RWND:
        CATCH_ERR(dc_rewinddir(buffer));
        break;
      default:
        break;
      }

      burst_count = 0;
    }

    console_tick();

    if (batch_processed == 0)
    {
      idle_count++;
      hyper_mode = 0;

      if (idle_count > CONSOLE_IDLE_BACKOFF_THRESHOLD)
      {
        poll_us = poll_us * 5 / 4;
        if (poll_us > CONSOLE_MAX_POLL_MS * 1000)
          poll_us = CONSOLE_MAX_POLL_MS * 1000;
      }

      int ms = (poll_us + 999) / 1000;
      if (ms < 1)
        ms = 1;
      sock_poll(ms);
    }
    else
    {
      poll_us = CONSOLE_RAPID_POLL_US;

      if (burst_count >= CONSOLE_BURST_THRESHOLD)
      {
        console_tick();
        burst_count = 0;
      }
    }
  }

  return 0;
}

int open_gdb_socket(int port)
{
  struct sockaddr_in server_addr;

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  gdb_server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef __MINGW32__
  if (gdb_server_socket == INVALID_SOCKET)
  {
#else
  if (gdb_server_socket < 0)
  {
#endif
    log_error("error creating gdb server socket");
    return -1;
  }

  const int enable_reuse_addr = 1;

#ifdef _WIN32
  int checkopt = setsockopt(gdb_server_socket, SOL_SOCKET, SO_REUSEADDR,
                            (char *)&enable_reuse_addr, sizeof(enable_reuse_addr));
#else
  int checkopt = setsockopt(gdb_server_socket, SOL_SOCKET, SO_REUSEADDR,
                            &enable_reuse_addr, sizeof(enable_reuse_addr));
#endif

#ifdef __MINGW32__
  if (checkopt == SOCKET_ERROR)
  {
#else
  if (checkopt < 0)
  {
#endif
    log_error("warning: failed to set gdb socket options");
  }

  int checkbind = bind(gdb_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
#ifdef __MINGW32__
  if (checkbind == SOCKET_ERROR)
  {
#else
  if (checkbind < 0)
  {
#endif
    log_error("error binding gdb server socket");
    return -1;
  }

  int checklisten = listen(gdb_server_socket, 0);
#ifdef __MINGW32__
  if (checklisten == SOCKET_ERROR)
  {
#else
  if (checklisten < 0)
  {
#endif
    log_error("error listening to gdb server socket");
    return -1;
  }

  return 0;
}

#ifdef __MINGW32__
#define AVAILABLE_OPTIONS "x:u:d:a:s:t:i:nlqhrgf"
#else
#define AVAILABLE_OPTIONS "x:u:d:a:s:t:m:c:i:nlqhrgf"
#endif

int main(int argc, char *argv[])
{
  unsigned int address = 0x0c010000;
  unsigned int size = 0;
  unsigned int console = 1;
  unsigned int quiet = 0;
  unsigned char command = 0;
  unsigned int cdfs_redir = 0;
  int someopt;

  char *filename = 0;
  char *isofile = 0;
  char *hostname = strdup(DREAMCAST_IP);
  char *cleanlist[4] = {0, 0, 0, 0};

  if (argc < 2)
  {
    usage();
    return 0;
  }

#ifdef __MINGW32__
  if (start_ws())
    return -1;
#endif

  someopt = getopt(argc, argv, AVAILABLE_OPTIONS);
  while (someopt > 0)
  {
    switch (someopt)
    {
    case 'x':
      if (command)
      {
        fprintf(stderr, "You can only specify one of -x, -u, -d, and -r\n");
        goto doclean;
      }
      command = 'x';
      filename = malloc(strlen(optarg) + 1);
      cleanlist[0] = filename;
      strcpy(filename, optarg);
      break;
    case 'u':
      if (command)
      {
        fprintf(stderr, "You can only specify one of -x, -u, -d, and -r\n");
        goto doclean;
      }
      command = 'u';
      filename = malloc(strlen(optarg) + 1);
      cleanlist[0] = filename;
      strcpy(filename, optarg);
      break;
    case 'd':
      if (command)
      {
        fprintf(stderr, "You can only specify one of -x, -u, -d, and -r\n");
        goto doclean;
      }
      command = 'd';
      filename = malloc(strlen(optarg) + 1);
      cleanlist[0] = filename;
      strcpy(filename, optarg);
      break;
#ifndef __MINGW32__
    case 'm':
      if (path)
      {
        fprintf(stderr, "-m and -c options are mutually exclusive, choose one\n");
        goto doclean;
      }
      nochroot = 1;
      path = realpath(optarg, NULL);
      if (path == NULL)
      {
        fprintf(stderr, "-m option with invalid path '%s'  \n", optarg);
        goto doclean;
      }
      set_mappath(path);
      cleanlist[1] = path;
      break;
    case 'c':
      if (path)
      {
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
      while ((hostname[portcheck] != '\0') && (hostname[portcheck] != ':'))
      {
        portcheck++;
      }

      if (hostname[portcheck] == ':')
      {
        hostname[portcheck++] = '\0';
        dcload_portnum = 0;

        while (hostname[portcheck] != '\0')
        {
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
      if (command)
      {
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
    case 'f':
      printf("Enabling fast transfer mode\n");
      fast_mode = 1;
      break;
    default:
      usage();
      goto doclean;
      break;
    }
    someopt = getopt(argc, argv, AVAILABLE_OPTIONS);
  }

  if (quiet)
    printf("Quiet download\n");

  if (cdfs_redir & (!console))
    console = 1;

  if (console & (command == 'x'))
    printf("Console enabled\n");

#ifndef __MINGW32__
  if (path)
  {
    if (nochroot)
    {
      printf("Mapping /pc/ to <%s>\n", path);
    }
    else
    {
      printf("Chrooting to <%s>\n", path);
    }
  }
#endif

  if (cdfs_redir & (command == 'x'))
    printf("Cdfs redirection enabled\n");

  if (open_sockets(hostname) < 0)
  {
    fprintf(stderr, "Error opening sockets\n");
    goto doclean;
  }

  switch (command)
  {
  case 'x':
    printf("Upload <%s>\n", filename);
    address = upload(filename, address);

    if (address == (unsigned int)-1)
      goto doclean;

    if (!legacy || force_legacy)
    {
      printf("Executing at <0x%x>\n", address | 0xa0000000);
    }
    else
    {
      printf("Executing at <0x%x>\n", address);
    }

    if (execute(address, console, cdfs_redir))
      goto doclean;
    if (console)
      do_console(path, isofile);
    break;
  case 'u':
    printf("Upload <%s> at <0x%x>\n", filename, address);
    if (upload(filename, address))
      goto doclean;
    break;
  case 'd':
    if (!size)
    {
      fprintf(stderr, "You must specify a size (-s <size>) with download (-d <filename>)\n");
      goto doclean;
    }
    printf("Download %d bytes at <0x%x> to <%s>\n", size, address, filename);
    if (download(filename, address, size, quiet) == -1)
      goto doclean;
    break;
  case 'r':
    printf("Resetting...\n");
    if (send_command(CMD_REBOOT, 0, 0, NULL, 0) == -1)
      goto doclean;
    break;
  default:
    usage();
    break;
  }

  cleanup(cleanlist);
  return 0;

doclean:
  cleanup(cleanlist);
  return -1;
}
