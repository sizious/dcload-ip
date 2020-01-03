#include "adapter.h"
#include "rtl8139.h"
#include "lan_adapter.h"

// Loop escape flag, used by all drivers.
volatile unsigned char escape_loop = 0;

// The currently configured driver.
adapter_t * bb;

// Packet receive buffer
unsigned char current_pkt[RX_PKT_BUF_SIZE]; // Here's a global array. Global packet receive buffer.

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
