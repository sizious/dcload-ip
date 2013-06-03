#ifndef __COMMANDS_H__
#define __COMMANDS_H__

typedef struct __attribute__ ((packed)) {
	unsigned char id[4];
	unsigned int address;
	unsigned int size;
	unsigned char data[1];
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

#define COMMAND_LEN  12

extern unsigned int tool_ip;
extern unsigned char tool_mac[6];
extern unsigned short tool_port;

#endif
