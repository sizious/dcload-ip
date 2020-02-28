#include "adapter.h"
#include "rtl8139.h"
#include "lan_adapter.h"

// Loop escape flag, used by all drivers.
volatile unsigned char escape_loop = 0;

// The currently configured driver.
adapter_t * bb;

// Packet receive buffer
__attribute__((aligned(8))) unsigned char raw_current_pkt[RAW_RX_PKT_BUF_SIZE]; // Here's a global array. Global packet receive buffer.
// Need to offset the packet by 2 for the command->data after headers to always be aligned to 8 bytes
// The performance gains are well worth the 2 wasted bytes.
unsigned char * current_pkt = &(raw_current_pkt[2]);

// Used for padding since doing a padding memset isn't exactly safe without G2 locking like KOS.
// Triple buffering the data works for these small packets just fine. Small data therefore does this now:
// source --memcpy--> small packet buffer --memcpy--> area reserved for transmit (txdesc) --DMA--> inaccessible internal Realtek buffer --MII--> network!
// It only costs 64 bytes to implement this after GCC's optimized it.
__attribute__((aligned(8))) unsigned char raw_tx_small_packet_zero_buffer[64]; // Here's a global array
// alignment trick for packet buffering copies
unsigned char * tx_small_packet_zero_buffer = &(raw_tx_small_packet_zero_buffer[2]);
// ...Which is to say, if you're looking for 64 bytes, this is NOT the place to get them from, sorry!!
// NOTE: 64 is used instead of 60 (IEEE minimum packet length) because a properly aligned 64-byte buffer
// can have a lot of nice optimizations done to work with it.

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
