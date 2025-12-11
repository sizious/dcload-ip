#ifndef __SYSCALLS_H__
#define __SYSCALLS_H__

#define SYSCALL_TIMEOUT_SECS 3
#define SYSCALL_MAX_RETRIES 5
#define SYSCALL_WRITE_TIMEOUT_SECS 2
#define SYSCALL_WRITE_MAX_RETRIES 3

#define CMD_EXIT     "DC00"
#define CMD_FSTAT    "DC01"
#define CMD_WRITE_OLD    "DD02"
#define CMD_WRITE    "DC02"
#define CMD_READ     "DC03"
#define CMD_OPEN     "DC04"
#define CMD_CLOSE    "DC05"
#define CMD_CREAT    "DC06"
#define CMD_LINK     "DC07"
#define CMD_UNLINK   "DC08"
#define CMD_CHDIR    "DC09"
#define CMD_CHMOD    "DC10"
#define CMD_LSEEK    "DC11"
#define CMD_TIME     "DC12"
#define CMD_STAT     "DC13"
#define CMD_UTIME    "DC14"
#define CMD_BAD      "DC15"
#define CMD_OPENDIR  "DC16"
#define CMD_CLOSEDIR "DC17"
#define CMD_READDIR  "DC18"
#define CMD_CDFSREAD "DC19"
#define CMD_GDBPACKET "DC20"
#define CMD_REWINDDIR "DC21"
#define CMD_BATCH    "BTCH"
#define CMD_BRSP     "BRSP"
#define CMD_CWRITE   "CWRT"
#define CMD_CACK     "CACK"
#define CMD_CSYNC    "CSYN"

#define CONSOLE_RING_SIZE    1024
#define CONSOLE_MAX_INLINE   1380
#define CONSOLE_CREDIT_INIT  128
#define CONSOLE_CREDIT_LOW   24
#define CONSOLE_COALESCE_US  50
#define CONSOLE_BURST_MAX    12
#define CONSOLE_DRAIN_SPINS  2
#define CONSOLE_NOBLOCK_THRESH 512
#define CONSOLE_FIRE_FORGET   1
#define CONSOLE_DROP_OLDEST   1
#define CONSOLE_MIN_HEADROOM  256
#define CONSOLE_CREDIT_BOOST  192

extern unsigned short dcload_syscall_port;

extern unsigned int syscall_retval;
extern unsigned char* syscall_data;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int value0;
	unsigned int value1;
	unsigned int value2;
} command_3int_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int value0;
	unsigned int value1;
	unsigned char string[1];
} command_2int_string_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int value0;
} command_int_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int value0;
	unsigned char string[1];
} command_int_string_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned char string[1];
} command_string_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int value0;
	unsigned int value1;
	unsigned int value2;
	unsigned char string[1];
} command_3int_string_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int seq;
	unsigned int ack_seq;
	unsigned short credits;
	unsigned short len;
	unsigned char data[];
} command_console_t;

#define CONSOLE_CMD_LEN 16

#define BATCH_OP_WRITE      0x01
#define BATCH_OP_READ       0x02
#define BATCH_OP_OPEN       0x03
#define BATCH_OP_CLOSE      0x04
#define BATCH_OP_STAT       0x05
#define BATCH_OP_LSEEK      0x06
#define MAX_BATCH_OPS       16

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int count;
	unsigned int flags;
	unsigned char ops[];
} command_batch_t;

typedef struct __attribute__ ((packed, aligned(2))) {
	unsigned char opcode;
	unsigned char len;
	unsigned char payload[];
} batch_op_t;

void build_send_packet(int command_len);
void dcexit(void);

int write_buffered(int fd, const void *buf, unsigned int count);
int flush_write_buffer(void);
void console_drain(void);
void console_pump(void);
int console_credits_available(void);
void console_handle_ack(unsigned int ack_seq, unsigned int credits);

#endif
