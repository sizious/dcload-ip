#ifndef __ADAPTER_H__
#define __ADAPTER_H__

// Raw receive buffer array size
// 1514 bytes is not a multiple of 8.
// Ethernet header (14) + ip header (20) + udp header (8) + command struct (12) = 54 bytes before command->data
// So to align the actual command->data to 8 bytes, need to align the array to 8 bytes and offset the start
// by 2 bytes. 1516 - 2 = 1514, and 54 bytes later (56 bytes from the start of the array) is aligned to 8 bytes since 56 = 8 * 7.
// Also: ip header is aligned to 8 bytes, udp/icmp header is aligned to 4 bytes, and command struct (or other payload header) is aligned to 4 bytes
// Use 1518 because the max data copied over G2 is 1516 by pkt_to_mem, then with the 2-byte shift is 1518.
// But SH4_mem_to_pkt() reads 4 bytes beyond the end of a size, so (1514 + 3)/4 becomes 1516/4, which will read 1520 bytes.
// So we end up with 1520 to account for that. Plus 1520 is the nearest multiple of 8 greater than 1514, too.
// And then you throw caching in, with does things in 32-byte blocks, so we need to extend to the nearest multiple of 32 >1514, or 1536,
// otherwise cache operations will spill over onto adjacent data, which can easily corrupt things.
#define RAW_RX_PKT_BUF_SIZE 1536

// Receive buffer size
#define RX_PKT_BUF_SIZE 1514

// Defines a "network adapter". There will be one of these for each of the
// available drivers.
typedef struct {
	// Driver name
	const char	* name;

	// Mac address
	unsigned char	mac[6];

	// 2 padding bytes to keep function pointers aligned to 4 bytes
	unsigned char pad[2];

	// Check to see if we have one
	int	(*detect)();

	// Initialize the adapter
	int	(*init)();

	// Start network I/O
	void	(*start)();

	// Stop network I/O
	void	(*stop)();

	// Poll for I/O
	void	(*loop)(int is_main_loop);

	// Transmit a packet on the adapter
	int	(*tx)(unsigned char * pkt, int len);
} adapter_t;

// Detect which adapter we are using and init our structs.
int adapter_detect();

// The configured adapter, to be used in all other funcs.
extern adapter_t * bb;
extern adapter_t adapter_la;
extern adapter_t adapter_bba;

// Set this variable to non-zero if you want the loop to exit.
extern volatile unsigned char escape_loop;

// All adapter drivers should use this shared buffer to receive.
extern __attribute__((aligned(32))) unsigned char raw_current_pkt[RAW_RX_PKT_BUF_SIZE];
extern __attribute__((aligned(2))) unsigned char * current_pkt;

/*
// This is useful code to use the perf counters to time stuff (cycle count)
// Using the DCLOAD PMCR in CPU Cycle mode, 1 count = 1 cycle = roughly 5ns (really 1/199.5MHz)

#define LOOP_TIMING

#ifdef LOOP_TIMING
#include "perfctr.h"
#include "video.h"
static unsigned int first_array[2] = {0};
static unsigned int second_array[2] = {0};
static char uint_string_array[9] = {0};
#endif

#ifdef LOOP_TIMING
    PMCR_Read(DCLOAD_PMCR, first_array);
#endif

#ifdef LOOP_TIMING
		PMCR_Read(DCLOAD_PMCR, second_array);
		unsigned int loop_difference = (unsigned int)(*(unsigned long long int*)second_array - *(unsigned long long int*)first_array);

		clear_lines(222, 24, global_bg_color);
		uint_to_string(loop_difference, (unsigned char*)uint_string_array);
		draw_string(30, 222, uint_string_array, STR_COLOR);
#endif
*/

#endif
