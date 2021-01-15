#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include "packet.h"

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char id[4];
	unsigned int address;
	unsigned int size;
	unsigned char data[]; // Make flexible array member
} command_t;

#define CMD_EXECUTE  "EXEC" /* execute */
#define CMD_LOADBIN  "LBIN" /* begin receiving binary */
#define CMD_PARTBIN  "PBIN" /* part of a binary */
#define CMD_DONEBIN  "DBIN" /* end receiving binary */
#define CMD_SENDBIN  "SBIN" /* send a binary */
#define CMD_SENDBINQ "SBIQ" /* send a binary, quiet */
#define CMD_VERSION  "VERS" /* send version info */
#define CMD_RETVAL   "RETV" /* return value */
#define CMD_REBOOT   "RBOT" /* reboot */
#define CMD_MAPLE    "MAPL" /* Maple packet */
#define CMD_PMCR 		 "PMCR" /* Performance counter packet */

#define COMMAND_LEN  12

extern unsigned int tool_ip;
extern unsigned char tool_mac[6];
extern unsigned short tool_port;
// Format is a uint, encoded like this: (major << 16) | (minor << 8) | patch
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

#endif
