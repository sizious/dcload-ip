#ifndef __COMMANDS_H__
#define __COMMANDS_H__

struct _command_t {
	unsigned char id[4];
	unsigned int address;
	unsigned int size;
	unsigned char data[];
} __attribute__ ((packed));

typedef struct _command_t command_t;

typedef struct __attribute__ ((packed)) {
	unsigned char id[4];
	unsigned int base_address;
	unsigned int total_size;
	unsigned int window_size;
	unsigned int seq_num;
	unsigned char data[];
} command_window_t;

typedef struct __attribute__ ((packed)) {
	unsigned char id[4];
	unsigned int bitmap_offset;
	unsigned int bitmap[4];
} command_sack_t;

typedef struct __attribute__ ((packed)) {
	unsigned char id[4];
	unsigned int count;
	unsigned int rtt_us;
	unsigned char ops[];
} command_batch_t;

typedef struct __attribute__ ((packed)) {
	unsigned char opcode;
	unsigned char len;
	unsigned char payload[];
} batch_op_t;

#define CMD_EXECUTE  "EXEC"
#define CMD_LOADBIN  "LBIN"
#define CMD_PARTBIN  "PBIN"
#define CMD_DONEBIN  "DBIN"
#define CMD_SENDBIN  "SBIN"
#define CMD_SENDBINQ "SBIQ"
#define CMD_VERSION  "VERS"
#define CMD_RETVAL   "RETV"
#define CMD_REBOOT   "RBOT"
#define CMD_MAPLE    "MAPL"
#define CMD_PMCR     "PMCR"

#define CMD_WLOAD    "WLOD"
#define CMD_WPART    "WPAR"
#define CMD_WSACK    "WSAK"
#define CMD_WDONE    "WDON"
#define CMD_BATCH    "BTCH"
#define CMD_BRSP     "BRSP"
#define CMD_PING     "PING"
#define CMD_PONG     "PONG"
#define CMD_CWRITE   "CWRT"
#define CMD_CACK     "CACK"

#define WINDOW_SIZE_BBA     32
#define WINDOW_SIZE_LAN     8
#define SACK_BITMAP_BITS    128

#define BATCH_OP_WRITE      0x01
#define BATCH_OP_READ       0x02
#define BATCH_OP_OPEN       0x03
#define BATCH_OP_CLOSE      0x04
#define BATCH_OP_STAT       0x05
#define BATCH_OP_LSEEK      0x06

#define COMMAND_LEN         12
#define COMMAND_WINDOW_LEN  24
#define COMMAND_SACK_LEN    24
#define COMMAND_BATCH_LEN   12

#endif
