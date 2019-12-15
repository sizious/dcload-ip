#include <string.h>
#include "commands.h"
//#include "packet.h" // in header now
#include "net.h"
#include "video.h"
#include "adapter.h"
#include "syscalls.h"
#include "cdfs.h"
#include "dcload.h"
#include "go.h"
#include "disable.h"
#include "scif.h"
#include "maple.h"

#include "perfctr.h"

volatile unsigned int our_ip = 0; // To be clear, this needs to be zero for init. Make that explicit here. Also, this value should be kept LE.
unsigned int tool_ip = 0;
unsigned char tool_mac[6] = {0};
unsigned short tool_port = 0;

#define min(a, b) ((a) < (b) ? (a) : (b))

#define BIN_INFO_MAP_SIZE 16384

typedef struct {
	unsigned int load_address;
	unsigned int load_size;
	unsigned char map[BIN_INFO_MAP_SIZE];
} bin_info_t;

static bin_info_t bin_info; // Here's a global array. This one is massive, but please don't shrink it. It's meant to act as a map where each 1024B maps into 16MB RAM, and 1024B fits into a packet...

//unsigned char buffer[COMMAND_LEN + 1024]; /* buffer for response */
// pkt_buf is plenty big enough. The headers take up 42 bytes of 1514. :) Needs to be a local variable, though.

void cmd_reboot(void)
{
	booted = 0;
	running = 0;

	disable_cache();
	asm volatile ("" : : : "memory"); // Flush all registers to RAM
	go(0x8c004000);
}

void cmd_execute(ether_header_t * ether, ip_header_t * ip, udp_header_t * udp, command_t * command)
{
	if (!running) {
		tool_ip = ntohl(ip->src);
		tool_port = ntohs(udp->src);
		memcpy(tool_mac, ether->src, 6);
		our_ip = ntohl(ip->dest);

		make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
		make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) command, COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 0);
		bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

		if (!booted)
			disp_info();
		else
			disp_status("executing...");

		if (ntohl(command->size)&1)
			*((unsigned int *)0x8c004004) = 0xdeadbeef; /* enable console */
		else
			*((unsigned int *)0x8c004004) = 0xfeedface; /* disable console */
		if (ntohl(command->size)>>1)
			cdfs_redir_enable();

		bb->stop();

		running = 1;

		disable_cache();
		asm volatile ("" : : : "memory"); // Flush all registers to RAM
		go(ntohl(command->address));
	}
}

void cmd_loadbin(ip_header_t * ip, udp_header_t * udp, command_t * command)
{
	bin_info.load_address = ntohl(command->address);
	bin_info.load_size = ntohl(command->size);
	memset(bin_info.map, 0, BIN_INFO_MAP_SIZE);

	our_ip = ntohl(ip->dest);

	make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
	make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) command, COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 0);
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

	if (!running) {
		if (!booted)
			disp_info();
		disp_status("receiving data...");
	}
}

void cmd_partbin(command_t * command)
{
	int index = 0;

	memcpy((unsigned char *)ntohl(command->address), command->data, ntohl(command->size));

	index = (ntohl(command->address) - bin_info.load_address) >> 10;
	bin_info.map[index] = 1;
}

void cmd_donebin(ip_header_t * ip, udp_header_t * udp, command_t * command)
{
	unsigned int i;

	for(i = 0; i < (bin_info.load_size + 1023)/1024; i++)
		if (!bin_info.map[i])
			break;
	if ( i == (bin_info.load_size + 1023)/1024 ) {
		command->address = htonl(0);
		command->size = htonl(0);
	} else {
		command->address = htonl( bin_info.load_address + i * 1024);
		command->size = htonl(min(bin_info.load_size - i * 1024, 1024));
	}

	make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
	make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) command, COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 0);
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

	if (!running) {
		if (!booted)
			disp_info();
		disp_status("idle...");
	}
}

void cmd_sendbinq(ip_header_t * ip, udp_header_t * udp, command_t * command)
{
	int numpackets, i;
	unsigned char *ptr;
	unsigned int bytes_left;
	unsigned int bytes_thistime;

	unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
	command_t * response = (command_t *)buffer;

	bytes_left = ntohl(command->size);
	numpackets = (ntohl(command->size)+1023) / 1024;
	ptr = (unsigned char *)ntohl(command->address);

	memcpy(response->id, CMD_SENDBIN, 4);
	for(i = 0; i < numpackets; i++) {
		if (bytes_left >= 1024)
			bytes_thistime = 1024;
		else
			bytes_thistime = bytes_left;
		bytes_left -= bytes_thistime;

		response->address = htonl((unsigned int)ptr);
		memcpy(response->data, ptr, bytes_thistime);
		response->size = htonl(bytes_thistime);
		make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + bytes_thistime, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
		make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) response, COMMAND_LEN + bytes_thistime, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 1); // 1
		bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + bytes_thistime);
		ptr += bytes_thistime;
	}

	memcpy(response->id, CMD_DONEBIN, 4);
	response->address = htonl(0);
	response->size = htonl(0);
	make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
	make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) response, COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 1); // 1
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);
}

void cmd_sendbin(ip_header_t * ip, udp_header_t * udp, command_t * command)
{
	our_ip = ntohl(ip->dest);

	if (!running) {
		if (!booted)
			disp_info();
		disp_status("sending data...");
	}

	cmd_sendbinq(ip, udp, command);

	if (!running) {
		disp_status("idle...");
	}
}

void cmd_version(ip_header_t * ip, udp_header_t * udp, command_t * command)
{
	int i;
	unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
	command_t * response = (command_t *)buffer;

	i = strlen("DCLOAD-IP " DCLOAD_VERSION) + 1;
	memcpy(response, command, COMMAND_LEN);
	//strcpy(response->data, "DCLOAD-IP " DCLOAD_VERSION); // There is no strcpy
	memcpy(response->data, "DCLOAD-IP " DCLOAD_VERSION, i);
	response->size = htonl(i);
	make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + i, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
	make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) response, COMMAND_LEN + i, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 1); // 1
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + i);
}

void cmd_retval(ip_header_t * ip, udp_header_t * udp, command_t * command)
{
	if (running) {
		make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
		make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) command, COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 0);
		bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

		bb->stop();

		syscall_retval = ntohl(command->address);
		syscall_data = command->data;
		escape_loop = 1;
	}
}

void cmd_maple(ip_header_t * ip, udp_header_t * udp, command_t * command) {
	char *res;
	//char *buf = (char *)(udp->data + 4);
	int i;
	unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
	command_t * response = (command_t *)buffer;

	memcpy(response, command, COMMAND_LEN);

	do {
		res = maple_docmd(command->data[0], command->data[1], command->data[2], command->data[3], command->data + 4);
	} while (*res == MAPLE_RESPONSE_AGAIN);

	/* Send response back over socket */
	i = ((res[0] < 0) ? 4 : ((res[3] + 1) << 2));
	response->size = htonl(i);
	memcpy(response->data, res, i);
	make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + i, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
	make_udp(ntohs(udp->src), ntohs(udp->dest), (unsigned char *)response, COMMAND_LEN + i, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 1); // 1
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + i);
}

// The 6 perfctr functions are:
/*
	// Clear counter and enable
	void PMCR_Init(int which, unsigned short mode, unsigned char count_type);

	// Enable one or both of these "undocumented" performance counters. Does not clear counter(s).
	void PMCR_Enable(int which, unsigned short mode, unsigned char count_type);

	// Disable, clear, and re-enable with new mode (or same mode)
	void PMCR_Restart(int which, unsigned short mode, unsigned char count_type);

	// Read a counter
	// out_array is specifically uint32 out_array[2] -- 48-bit value needs a 64-bit storage unit
	void PMCR_Read(int which, volatile unsigned int *out_array);

	// Clear counter(s) -- only works when disabled!
	void PMCR_Clear(int which);

	// Disable counter(s) without clearing
	void PMCR_Disable(int which);
*/
// The command is the first letter of the function (capitalized) plus the counter number, plus a byte with the mode (if applicable)
// Except restart--read is 'R', so restart is 'B' (think reBoot)
//
// Sending command data of 'D' 0x1 (2 bytes) disables ('D') perf counter 1 (0x1)
// Sending command data 'I' 0x3 0x23 0x1 (4 bytes) inits ('I') both perf counters (0x3) to elapsed time mode (0x23) and count is 1 cpu cycle = 1 count (0x1)
// Sending command data 'B' 0x2 0x23 0x0 (4 bytes) restarts ('B') perf counter 2 (0x2) to elapsed time mode (0x23) and count is CPU/bus ratio method (0x0)
// ...
// etc.
//
// Notes:
// - Remember to disable before leaving DCLOAD to execute a program, if needed.
// - See perfctr.h for how to calculate time using the CPU/bus ratio method.
// - PMCR_Init() and PMCR_Enable() will do nothing if the perf counter is already running!

static char * ok_message = "OK";
static char * invalid_read_message = "PMCR read: 1 or 2 only.";
static char * invalid_function_message = "PMCR: I, E, B, R, C, or D only.";
static char * invalid_option_message = "PMCR: 1, 2, or 3 only.";
static char * invalid_mode_message = "PMCR modes: 0x1-0x29.";
static char * invalid_count_type_message = "PMCR count: 0 or 1 only.";
static volatile unsigned int read_array[2] = {0};

void cmd_pmcr(ip_header_t * ip, udp_header_t * udp, command_t * command) {
	unsigned int i;
	int invalid_pmcr = 0, invalid_mode = 0, invalid_count = 0;

//	unsigned long long int read = 0;
	unsigned char read = 0;

	unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
	command_t * response = (command_t *)buffer;

	// Size is 2 or 4 bytes depending on the command. Easy!
	// No need for address, it's not used here so it can be whatever.
	memcpy(response, command, COMMAND_LEN);

	char * out_message = ok_message;
	i = 3;

	if((!command->data[1]) || (command->data[1] > 3))
	{
		invalid_pmcr = 1;
	}
	else if(command->data[0] == 'I') // Init
	{
		if((!command->data[2]) || (command->data[2] > 0x29))
		{
			invalid_mode = 1;
		}
		else if(command->data[3] > 1)
		{
			invalid_count = 1;
		}
		else
		{
			PMCR_Init(command->data[1], command->data[2], command->data[3]);
		}
	}
	else if(command->data[0] == 'E') // Enable
	{
		if((!command->data[2]) || (command->data[2] > 0x29))
		{
			invalid_mode = 1;
		}
		else if(command->data[3] > 1)
		{
			invalid_count = 1;
		}
		else
		{
			PMCR_Enable(command->data[1], command->data[2], command->data[3]);
		}
	}
	else if(command->data[0] == 'B') // Restart
	{
		if((!command->data[2]) || (command->data[2] > 0x29))
		{
			invalid_mode = 1;
		}
		else if(command->data[3] > 1)
		{
			invalid_count = 1;
		}
		else
		{
			PMCR_Restart(command->data[1], command->data[2], command->data[3]);
		}
	}
	else if(command->data[0] == 'R') // Read
	{
		if((!command->data[1]) || (command->data[1] > 2))
		{
			out_message = invalid_read_message;
			i = 24;
		}
		else
		{
			//read = PMCR_Read(command->data[1]);
			PMCR_Read(command->data[1], read_array);
			read = 1;
		}
	}
	else if(command->data[0] == 'C') // Clear
	{
		PMCR_Clear(command->data[1]);
	}
	else if(command->data[0] == 'D') // Disable
	{
		PMCR_Disable(command->data[1]);
	}
	else // Respond with invalid perfcounter option
	{
		out_message = invalid_function_message;
		i = 32;
	}

	// Error and read flag checks
	if(invalid_pmcr)
	{
		out_message = invalid_option_message;
		i = 23;
	}
	else if(invalid_mode)
	{
		out_message = invalid_mode_message;
		i = 22;
	}
	else if(invalid_count)
	{
		out_message = invalid_count_type_message;
		i = 25;
	}

	if(read) // This will send little endian perf counter value as the response.
	{
		i = 8; // 64-bit value is 8 bytes
		//out_message = (char*)&read; // C lets you do this :)
		out_message = (char*)read_array; // C lets you do this :)
	}
	// Make and send response

	memcpy(response->data, out_message, i);
	response->size = htonl(i);

	// make_ether was run in net.c already
	make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + i, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
	make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) response, COMMAND_LEN + i, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 1); // 1
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + i);
}

/*
//
// Counter disable (only) version
//

static char * ok_message = ": OK";
static char * invalid_option_message = " : PMCR D1, D2, or D3 only.";

void cmd_pmcr(ip_header_t * ip, udp_header_t * udp, command_t * command) {
	unsigned int i;
	unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
	command_t * response = (command_t *)buffer;

	// Size is 2 bytes. Easy!
	// No need for address, it's not used here so it can be whatever.
	memcpy(response, command, COMMAND_LEN);

	char * out_message = ok_message;
	i = 5;

	if((command->data[0] != 'D') || (!command->data[1]) || (command->data[1] > 3))
	{
		out_message = invalid_option_message;
		i = 28;
	}
	else
	{
		PMCR_Disable(command->data[1]);
	}

	// Make and send response
	// Append response string to received command
	memcpy(&(response->data[2]), out_message, i);
	response->size = htonl(i + 2);

	// make_ether was run in net.c already
	make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + i, IP_UDP_PROTOCOL, (ip_header_t *)(pkt_buf + ETHER_H_LEN));
	make_udp(ntohs(udp->src), ntohs(udp->dest),(unsigned char *) response, COMMAND_LEN + i, (ip_header_t *)(pkt_buf + ETHER_H_LEN), (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN), 1); // 1
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + i);
}
*/
/*
// command_t struct here For reference

typedef struct __attribute__ ((packed)) {
	unsigned char id[4];
	unsigned int address;
	unsigned int size;
	unsigned char data[];
} command_t;
*/
