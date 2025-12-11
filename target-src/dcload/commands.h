#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include "packet.h"

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int address;
	unsigned int size;
	unsigned char data[];
} command_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int base_address;
	unsigned int total_size;
	unsigned int window_size;
	unsigned int seq_num;
	unsigned char data[];
} command_window_t;

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int bitmap_offset;
	unsigned int bitmap[4];
} command_sack_t;

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
#define CMD_PING     "PING"
#define CMD_PONG     "PONG"
#define CMD_CWRITE   "CWRT"
#define CMD_CACK     "CACK"

#define COMMAND_LEN         12
#define COMMAND_WINDOW_LEN  24
#define COMMAND_SACK_LEN    24

#define SACK_BITMAP_BITS    128
#define MAX_WINDOW_PACKETS  128

extern unsigned int tool_ip;
extern unsigned char tool_mac[6];
extern unsigned short tool_port;
extern unsigned int tool_version;

#define DCTOOL_MAJOR ((tool_version & 0x00ff0000) >> 16)
#define DCTOOL_MINOR ((tool_version & 0x0000ff00) >> 8)
#define DCTOOL_PATCH (tool_version & 0x000000ff)

void cmd_reboot(void);
void cmd_execute(ether_header_t * ether, ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_loadbin(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_highspeed_partbin(udp_header_t * udp, unsigned int udp_data_size);
void cmd_partbin(command_t * command);
void cmd_donebin(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_sendbinq(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_sendbin(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_version(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_retval(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_maple(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_pmcr(ip_header_t * ip, udp_header_t * udp, command_t * command);

void cmd_wload(ip_header_t * ip, udp_header_t * udp, command_window_t * command);
void cmd_wpart(command_window_t * command);
void cmd_wsack(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_wdone(ip_header_t * ip, udp_header_t * udp, command_t * command);
void cmd_ping(ip_header_t * ip, udp_header_t * udp, command_t * command);

#endif
