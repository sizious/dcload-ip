#ifndef __ADAPTER_H__
#define __ADAPTER_H__

#define RX_PKT_BUF_SIZE 1514

// Defines a "network adapter". There will be one of these for each of the
// available drivers.
typedef struct {
	// Driver name
	const char	* name;

	// Mac address
	unsigned char	mac[6];

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

// Set this variable to non-zero if you want the loop to exit.
extern volatile unsigned char escape_loop;

// All adapter drivers should use this shared buffer to receive.
extern unsigned char current_pkt[RX_PKT_BUF_SIZE];

extern unsigned char tx_small_packet_zero_buffer[60];

#endif
