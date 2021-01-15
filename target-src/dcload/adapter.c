#include "adapter.h"
#include "rtl8139.h"
#include "lan_adapter.h"

// Loop escape flag, used by all drivers.
volatile unsigned char escape_loop = 0;

// The currently configured driver.
adapter_t * bb;

// Packet receive buffer
__attribute__((aligned(32))) unsigned char raw_current_pkt[RAW_RX_PKT_BUF_SIZE]; // Here's a global array. Global packet receive buffer.
// Need to offset the packet by 2 for the command->data after headers to always be aligned to 8 bytes
// The performance gains are well worth the 2 wasted bytes.
__attribute__((aligned(2))) unsigned char * current_pkt = &(raw_current_pkt[2]);

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
