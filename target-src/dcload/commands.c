#include "commands.h"
#include <string.h>
#include <unistd.h>
// #include "packet.h" // in header now
#include "adapter.h"
#include "cdfs.h"
#include "dcload.h"
#include "disable.h"
#include "go.h"
#include "maple.h"
#include "net.h"
#include "scif.h"
#include "syscalls.h"
#include "video.h"

#include "memfuncs.h"
#include "perfctr.h"

__attribute__((aligned(4))) volatile unsigned int our_ip =
    0; // To be clear, this needs to be zero for init. Make that explicit here. Also, this value
       // should be kept LE.
unsigned int tool_ip = 0;
unsigned char tool_mac[6] = {0};
unsigned short tool_port = 0;
unsigned int tool_version = 0;

static unsigned int cached_dest = 0;
static int payload1024 = 0;

#define min(a, b) ((a) < (b) ? (a) : (b))

// This giant array keeps track of how many kB have been received, relative to start address of the
// transmitted data's destination. Each packet has a maximum payload size of 1440, and the nearest
// multiple of 1440 > 16MB is 16,784,640, which would be 11651 map indices. 11651 is not a multiple
// of 8, but 11656 is, so we can just use that. The multiple of 8 requirement is because
// memset_zeroes_64bit() is used as the only memset in this entire program, as it is the smallest
// way to set the most data.
#define BIN_INFO_MAP_SIZE 11656
// This used to be 16384 for 1024-byte payload size, but by doing it this way instead we can save
// almost 5kB from the file size and increase the data per packet by 1.4x. We can also set a legacy
// check to use 1024-byte packets for compatibility with old versions of dc-tool (if for some reason
// someone needs that), although the maximum size for such legacy uses would be limited to 11MB. I
// think the gains made with the new version are definitely worth it.

typedef struct {
    unsigned int load_address;
    unsigned int load_size;
    unsigned char map[BIN_INFO_MAP_SIZE];
} bin_info_t;

// Align huge map array to 8 bytes (it's already after 2x unsigned ints)
__attribute__((aligned(
    8))) static bin_info_t bin_info; // Here's a global array. This one is massive, but please don't
                                     // shrink it. It's meant to act as a map where each 1024B maps
                                     // into 16MB RAM, and 1024B fits into a packet...

void cmd_reboot(void) {
    booted = 0;
    running = 0;

    //    CacheBlockPurge((void*)0x0c004000, 1536);
    asm volatile("nop\n\t" : : : "memory"); // memory barrier for GCC
    disable_cache();
    go(0xac004000);
}

void cmd_execute(ether_header_t *ether, ip_header_t *ip, udp_header_t *udp, command_t *command) {
    if(!running) {
        bb->stop(); // Disable packet RX

        tool_ip = ntohl(ip->src);
        tool_port = ntohs(udp->src);
        memcpy(tool_mac, ether->src, 6);
        our_ip = ntohl(ip->dest);

        unsigned int cmd_size = ntohl(command->size);

        unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
        command_t *response = (command_t *)buffer;
        memcpy(response, command, COMMAND_LEN);

        make_ip(tool_ip, our_ip, UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL,
                (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
        make_udp(tool_port, ntohs(udp->dest), COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN),
                 (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
        bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

        if(!booted)
            disp_info();
        else
            disp_status("executing...");

        if(cmd_size & 1)
            *((volatile unsigned int *)0xac004004) = 0xdeadbeef; /* enable console */
        else
            *((volatile unsigned int *)0xac004004) = 0xfeedface; /* disable console */

        if(cmd_size >> 1)
            cdfs_redir_enable();

        running = 1;

        //        CacheBlockPurge((void*)0x0c004000, 1536);
        asm volatile("nop\n\t" : : : "memory"); // memory barrier for GCC
        disable_cache();
        go(ntohl(command->address) | 0xa0000000);
    }
}

void cmd_loadbin(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    bin_info.load_address = ntohl(command->address);
    bin_info.load_size = ntohl(command->size);

    // Legacy check for versions < 2.0.0
    if(DCTOOL_MAJOR < 2) {
        if(bin_info.load_size > (BIN_INFO_MAP_SIZE * 1024)) {
            // Send error, exit, and bail
            write(1, "ERROR: Size >11656KB (legacy mode)\r\n", 37);
            dcexit();
            bb->start(); // dcexit calls RX stop, so need to re-enable that

            return;
        }
    }
    else {
        // Max size check (16MB, RAM size)
        if(bin_info.load_size > 16777216) {
            // Send error, exit, and bail
            write(1, "ERROR: Size >16MB\r\n", 20);
            dcexit();
            bb->start(); // dcexit calls RX stop, so need to re-enable that

            return;
        }
    }

    // Zero out the received packet map
    memset_zeroes_64bit(bin_info.map, BIN_INFO_MAP_SIZE / 8);

    our_ip = ntohl(ip->dest);

    unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
    command_t *response = (command_t *)buffer;
    memcpy(response, command, COMMAND_LEN);

    // Check for P0, P1, or P3, all of which could be cacheable and would need OCBP or OCBWB
    // Faster to check for neither P2 nor P4
    unsigned int cacheable_check = bin_info.load_address >> 29;
    if((cacheable_check != 0x5) && (cacheable_check != 0x7)) {
        cached_dest = 1;
    }
    else {
        cached_dest = 0;
    }

    // Set up partbin to have as small a conditional as possible
    if(DCTOOL_MAJOR < 2) {
        payload1024 = 1;
    }
    else {
        payload1024 = 0;
    }

    make_ip(ntohl(ip->src), our_ip, UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL,
            (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
    make_udp(ntohs(udp->src), ntohs(udp->dest), COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN),
             (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
    bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

    if(!running) {
        if(!booted)
            disp_info();
        disp_status("receiving data...");
    }
}

void cmd_partbin(command_t *command) {
    int index = 0;
    unsigned int cmd_addr = ntohl(command->address);
    unsigned int cmd_size = ntohl(command->size);

    // Thanks to packet buffer alignment, command->data is guaranteed to be 8-byte aligned.
    // If the destination address is 8-byte aligned, this will be a rocket.
    //    memcpy((unsigned char *)cmd_addr, command->data, ntohl(command->size));

	// cmd_addr needs to honor whatever dc-tool sends. Use P0 addresses for cache boost when writing to RAM.
	// Something great for alignment reasons is that, in addition to being the max payload size, 1440 bytes is an even multiple of 32 bytes.
	SH4_aligned_memcpy((void*)cmd_addr, to_p1(command->data), cmd_size);
	if(cached_dest)
	{
		CacheBlockPurge((void*)cmd_addr, (cmd_size + 31)/32 + 2); // +1 for misalignment, +1 again for prefetch
	}
	// Ensure physical memory is actually written to from the cache, since we don't know how it might be used.
	// Purge instead of writeback to avoid cache conflicts/trashing.

    // Legacy check for versions < 2.0.0
    if(__builtin_expect(payload1024, 0)) {
        index = (cmd_addr - bin_info.load_address) / 1024; // /1024 = >> 10
    }
    else {
        index = (cmd_addr - bin_info.load_address) / 1440; // /1440 = 64-bit multiplication trick
    }

    bin_info.map[index] = 1;
}

void cmd_donebin(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    unsigned int i;
    unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
    command_t *response = (command_t *)buffer;
    memcpy(response, command, COMMAND_LEN);

    unsigned int map_index_verify, payload_size;

    // Legacy check for versions < 2.0.0
    // Need to hardcode these divides so that GCC can optimize them out (and
    // thankfully it is able to do so in these two scenarios, as it can convert
    // them into 64-bit multiplication)
    if(DCTOOL_MAJOR < 2) {
        map_index_verify = (bin_info.load_size + 1023) / 1024;
        payload_size = 1024;
    }
    else {
        map_index_verify = (bin_info.load_size + 1439) / 1440;
        payload_size = 1440;
    }

    for(i = 0; i < map_index_verify; i++)
        if(!bin_info.map[i])
            break;

    if(i == map_index_verify) {
        response->address = 0;
        response->size = 0;
    }
    else {
        response->address = htonl(bin_info.load_address + i * payload_size);
        response->size = htonl(min(bin_info.load_size - i * payload_size, payload_size));
    }

    make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL,
            (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
    make_udp(ntohs(udp->src), ntohs(udp->dest), COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN),
             (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
    bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

    if(!running) {
        if(!booted)
            disp_info();
        disp_status("idle...");
    }
}

void cmd_sendbinq(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    our_ip = ntohl(ip->dest);

    unsigned int payload_size, numpackets, i;
    unsigned int bytes_thistime;

    unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
    command_t *response = (command_t *)buffer;

    unsigned int cmd_addr = ntohl(command->address);
    unsigned int bytes_left = ntohl(command->size);

    // Legacy check for versions < 2.0.0
    // Need to hardcode these divides so that GCC can optimize them out (and
    // thankfully it is able to do so in these two scenarios, as it can convert
    // them into 64-bit multiplication)
    if(DCTOOL_MAJOR < 2) {
        payload_size = 1024;
        numpackets = (bytes_left + 1023) / 1024;
    }
    else {
        payload_size = 1440;
        numpackets = (bytes_left + 1439) / 1440;
    }

    unsigned int ip_src = ntohl(ip->src);
    unsigned short udp_src = ntohs(udp->src);
    unsigned short udp_dest = ntohs(udp->dest);

    memcpy(response->id, CMD_SENDBIN, 4);

    for(i = 0; i < numpackets; i++) {
        if(bytes_left >= payload_size)
            bytes_thistime = payload_size;
        else
            bytes_thistime = bytes_left;
        bytes_left -= bytes_thistime;

        // By aligning the transmit buffer, response->data is always aligned to 8 bytes.
        // 'cmd_addr' may or may not be, but if it is, this will be a rocket.
        //        memcpy(response->data, (void*)cmd_addr, bytes_thistime);

        // cmd_addr needs to honor whatever dc-tool sends. Use P1 addresses for cache boost 
        // when reading from RAM.
        // Something great for alignment reasons is that, in addition to being the max payload 
        // size, 1440 bytes is an even multiple of 32 bytes.
        SH4_aligned_memcpy(to_p1(response->data), (void*)cmd_addr, bytes_thistime);
        response->address = htonl(cmd_addr);
        response->size = htonl(bytes_thistime);
        make_ip(ip_src, our_ip, UDP_H_LEN + COMMAND_LEN + bytes_thistime, IP_UDP_PROTOCOL,
                (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
        make_udp(udp_src, udp_dest, COMMAND_LEN + bytes_thistime,
                 (ip_header_t *)(pkt_buf + ETHER_H_LEN),
                 (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
        bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + bytes_thistime);
        cmd_addr += bytes_thistime;
    }

    memcpy(response->id, CMD_DONEBIN, 4);
    response->address = 0;
    response->size = 0;
    make_ip(ip_src, our_ip, UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL,
            (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
    make_udp(udp_src, udp_dest, COMMAND_LEN, (ip_header_t *)(pkt_buf + ETHER_H_LEN),
             (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
    bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);
}

void cmd_sendbin(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    if(!running) {
        if(!booted)
            disp_info();
        disp_status("sending data...");
    }

    cmd_sendbinq(ip, udp, command);

    if(!running) {
        disp_status("idle...");
    }
}

void cmd_version(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    int datalength, j;
    unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
    command_t *response = (command_t *)buffer;

    // Address field isn't used in the command, so dc-tool stuffs its version in here now
    // Added in 2.0.0 for dc-load to know what version of dc-tool is being used
    // Format is a uint, encoded like this: (major << 16) | (minor << 8) | patch
    // Old versions of dc-tool will have a version of 0, so it's easy to check for them
    // (and they all expect a packet payload size of 1024 for TX/RX; this version command
    // was added when packet sizes switched to 1440 bytes of payload data)
    tool_version = ntohl(
        command->address); // This global variable is used in the major/minor/patch version macros.

    // Legacy check for >= 2.0.0
    if(DCTOOL_MAJOR >= 2) {
        // New versions of the program get the syscall port from dctool instead of hardcoding 31313.
        // That way if standards ever change, this program won't have to and the dctool server will
        // deal with it. This global variable is initted to the legacy port by default, so if dctool
        // is old it'll still work, too.
        dcload_syscall_port = ntohs(udp->dest);
    }
    else {
        // In case dc-tool -l is used after a non-legacy usage
        dcload_syscall_port = 31313;
    }

    datalength = strlen("dcload-ip " DCLOAD_VERSION
                        " using "); // no '+1' because adapter name will be appended
    memcpy(response, command, COMMAND_LEN);
    memcpy(response->data, "dcload-ip " DCLOAD_VERSION " using ", datalength);

    // Append adapter type
    j = strlen(bb->name) + 1;
    // the 'data' member is an unsigned char pointer
    memcpy(response->data + datalength, bb->name, j);
    datalength += j;

    response->size = htonl(datalength);
    // Stuff the adapter type inside the otherwise unused address field. :)
    // Added in version 2.0.0 for dc-tool-ip to be able to do performance tuning
    // based on which adapter is installed.
    response->address = htonl(installed_adapter);

    make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + datalength, IP_UDP_PROTOCOL,
            (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
    make_udp(ntohs(udp->src), dcload_syscall_port, COMMAND_LEN + datalength,
             (ip_header_t *)(pkt_buf + ETHER_H_LEN),
             (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
    bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + datalength);
}

void cmd_retval(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    if(running) {
        bb->stop(); // Disable packet RX

        unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
        command_t *response = (command_t *)buffer;
        memcpy(response, command, COMMAND_LEN);

        make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN, IP_UDP_PROTOCOL,
                (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
        make_udp(ntohs(udp->src), ntohs(udp->dest), COMMAND_LEN,
                 (ip_header_t *)(pkt_buf + ETHER_H_LEN),
                 (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
        bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN);

        syscall_retval = ntohl(command->address);
        syscall_data = command->data;
        escape_loop = 1;
    }
}

void cmd_maple(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    char *res;
    int i;
    unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
    command_t *response = (command_t *)buffer;

    memcpy(response, command, COMMAND_LEN);

    do {
        res = maple_docmd(command->data[0], command->data[1], command->data[2], command->data[3],
                          command->data + 4);
    } while(*res == MAPLE_RESPONSE_AGAIN);

	/* Send response back over socket */
	i = ((res[0] < 0) ? 4 : ((res[3] + 1) << 2));
	response->size = htonl(i);
	// By aligning the transmit buffer, response->data is always aligned to 8 bytes.
	// 'res' may or may not be, but if it is, this will be a rocket.
  //	memcpy(response->data, res, i);
	SH4_aligned_memcpy(to_p1(response->data), to_p1(res), i);

    make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + i, IP_UDP_PROTOCOL,
            (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
    make_udp(ntohs(udp->src), ntohs(udp->dest), COMMAND_LEN + i,
             (ip_header_t *)(pkt_buf + ETHER_H_LEN),
             (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
    bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + i);
}

// The 6 performance counter control functions are:
/*
    // (I) Clear counter and enable
    void PMCR_Init(unsigned char which, unsigned char mode, unsigned char count_type);

    // (E) Enable one or both of these "undocumented" performance counters
    void PMCR_Enable(unsigned char which, unsigned char mode, unsigned char count_type, unsigned
   char reset_counter);

    // (B) Disable, clear, and re-enable with new mode (or same mode)
    void PMCR_Restart(unsigned char which, unsigned char mode, unsigned char count_type);

    // (R) Read a counter
    unsigned long long int PMCR_Read(unsigned char which);

    // (G) Get a counter's current configuration
    unsigned short PMCR_Get_Config(unsigned char which);

    // (S) Stop counter(s) (without clearing)
    void PMCR_Stop(unsigned char which);

    // (D) Disable counter(s) (without clearing)
    void PMCR_Disable(unsigned char which);
*/
// The command is the first letter of the function name (capitalized), followed by each of the
// function's parameters. Note that restart's command letter is 'B'--read is 'R', so restart is 'B'
// (think reBoot). The command letters are included in parentheses for each function in the above
// comment block.
//
// Sending command data of 'D' 0x1 (2 bytes) disables ('D') perf counter 1 (0x1)
// Sending command data 'E' 0x3 0x23 0x0 0x1 (5 bytes) enables ('E') both perf counters (0x3) to
// elapsed time mode (0x23) where count is 1 cpu cycle = 1 count (0x0) and continue the counter from
// its current value (0x1) Sending command data 'B' 0x2 0x23 0x1 (4 bytes) restarts ('B') perf
// counter 2 (0x2) to elapsed time mode (0x23) and count is CPU/bus ratio method (0x1)
// ...
// etc.
//
// Notes:
// - Remember to disable before leaving DCLOAD to execute a program if needed.
// - See perfctr.h for how to calculate time using the CPU/bus ratio method.
// - PMCR_Init() and PMCR_Enable() will do nothing if the perf counter is already running!

static char *ok_message = "OK";
static char *invalid_read_message = "PMCR: chan 1 or 2 only.";
static char *invalid_function_message = "PMCR: I, E, B, R, G, S, or D only.";
static char *invalid_option_message = "PMCR: chan 1, 2, or 3 (both) only.";
static char *invalid_mode_message = "PMCR modes: 0x1-0x29.";
static char *invalid_count_type_message = "PMCR count: 0 or 1 only.";
static char *invalid_reset_type_message = "PMCR reset: 0 or 1 only.";
static volatile unsigned int read_array[2] = {0};
static volatile unsigned char getconfig_array[2] = {0};

void cmd_pmcr(ip_header_t *ip, udp_header_t *udp, command_t *command) {
    char *out_message = ok_message;
    unsigned int i = 3;
    unsigned char invalid_pmcr = 0, invalid_mode = 0, invalid_count = 0, invalid_reset = 0;

    unsigned char read = 0;

    unsigned char *buffer = pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
    command_t *response = (command_t *)buffer;

    // Size is 2, 4, or 5 bytes depending on the command. Easy!
    // No need for address, it's not used here so it can be whatever.
    // Size isn't actually checked, either...
    memcpy(response, command, COMMAND_LEN);

    if((!command->data[1]) || (command->data[1] > 3)) {
        invalid_pmcr = 1;
    }
    else if(command->data[0] == 'I') { // Init
        if((!command->data[2]) || (command->data[2] > 0x29)) {
            invalid_mode = 1;
        }
        else if(command->data[3] > 1) {
            invalid_count = 1;
        }
        else {
            PMCR_Init(command->data[1], command->data[2], command->data[3]);
        }
    }
    else if(command->data[0] == 'E') { // Enable
        if((!command->data[2]) || (command->data[2] > 0x29)) {
            invalid_mode = 1;
        }
        else if(command->data[3] > 1) {
            invalid_count = 1;
        }
        else if(command->data[4] > 1) {
            invalid_reset = 1;
        }
        else {
            PMCR_Enable(command->data[1], command->data[2], command->data[3], command->data[4]);
        }
    }
    else if(command->data[0] == 'B') { // Restart
        if((!command->data[2]) || (command->data[2] > 0x29)) {
            invalid_mode = 1;
        }
        else if(command->data[3] > 1) {
            invalid_count = 1;
        }
        else {
            PMCR_Restart(command->data[1], command->data[2], command->data[3]);
        }
    }
    else if(command->data[0] == 'R') { // Read
        if((!command->data[1]) || (command->data[1] > 2)) {
            out_message = invalid_read_message;
            i = 24;
        } else {
            PMCR_Read(command->data[1], read_array);
            read = 1;
        }
    }
    else if(command->data[0] == 'G') { // Get Config
        if((!command->data[1]) || (command->data[1] > 2)) {
            out_message = invalid_read_message;
            i = 24;
        }
        else {
            *(unsigned short *)getconfig_array = PMCR_Get_Config(command->data[1]);
            read = 2;
        }
    }
    else if(command->data[0] == 'S') { // Stop
        PMCR_Stop(command->data[1]);
    }
    else if(command->data[0] == 'D') { // Disable
        PMCR_Disable(command->data[1]);
    }
    else { // Respond with invalid perfcounter option
        out_message = invalid_function_message;
        i = 35;
    }

    // Error and read flag checks
    if(invalid_pmcr) {
        out_message = invalid_option_message;
        i = 35;
    }
    else if(invalid_mode) {
        out_message = invalid_mode_message;
        i = 22;
    }
    else if(invalid_count) {
        out_message = invalid_count_type_message;
        i = 25;
    }
    else if(invalid_reset) {
        out_message = invalid_reset_type_message;
        i = 25;
    }

    if(read == 1) {// This will send little endian perf counter value as the response.
        i = 8;                            // 64-bit value is 8 bytes
        out_message = (char *)read_array; // C lets you do this :)
    }
    else if(read == 2)                  // Send little endian config data as response
    {
        i = 2; // Config is 2 bytes
        out_message = (char *)getconfig_array;
    }
    // Make and send response

    memcpy(response->data, out_message, i);
    response->size = htonl(i);

    // make_ether was run in net.c already
    make_ip(ntohl(ip->src), ntohl(ip->dest), UDP_H_LEN + COMMAND_LEN + i, IP_UDP_PROTOCOL,
            (ip_header_t *)(pkt_buf + ETHER_H_LEN), ip->packet_id);
    make_udp(ntohs(udp->src), ntohs(udp->dest), COMMAND_LEN + i,
             (ip_header_t *)(pkt_buf + ETHER_H_LEN),
             (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN));
    bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + COMMAND_LEN + i);
}

/*
// command_t struct here For reference

typedef struct {
    unsigned char id[4];
    unsigned int address;
    unsigned int size;
    unsigned char data[];
} command_t;
*/
