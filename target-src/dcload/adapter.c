#include "adapter.h"
#include "rtl8139.h"
#include "lan_adapter.h"

// Loop escape flag, used by all drivers.
volatile unsigned char escape_loop = 0;

// The currently configured driver.
adapter_t * bb;

// Packet receive buffer
unsigned char current_pkt[RX_PKT_BUF_SIZE]; // Here's a global array. Global packet receive buffer.

// Used for padding since doing a padding memset isn't exactly safe without G2 locking like KOS.
// Triple buffering the data works for these small packets just fine. Small data therefore does this now:
// source --memcpy--> small packet buffer --memcpy--> area reserved for transmit (txdesc) --DMA--> inaccessible internal Realtek buffer --MII--> network!
// It only costs about 64 bytes to implement this (60 for buffer, 4 for function calls) after GCC's optimized it.
unsigned char tx_small_packet_zero_buffer[60]; // Here's a global array
// ...Which is to say, if you're looking for 60 bytes, this is NOT the place to get them from, sorry!!

int adapter_detect() {
	// Try the BBA first.
	if (adapter_bba.detect() >= 0) {
		bb = &adapter_bba;
	} else if (adapter_la.detect() >= 0) {
		bb = &adapter_la;
	} else {
		return -1;
	}

	// Initialize the chosen adapter.
	if (bb->init() < 0)
		return -1;

	escape_loop = 0;

	return 0;
}
