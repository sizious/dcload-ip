/*
   DC "Lan Adapter" driver for dc-load-ip

   Copyright (C)2002,2003 Dan Potter (NEW BSD LICENSE)
 */

#include <string.h>
#include "packet.h"
#include "net.h"
#include "adapter.h"
#include "lan_adapter.h"
#include "dcload.h" // clear_lines is in here
#include "video.h" // for draw_string

#include "dhcp.h"
#include "memfuncs.h"

// Here's a datasheet for the FUJITSU MB86967 chip:
// https://pdf1.alldatasheet.com/datasheet-pdf/view/61702/FUJITSU/MB86967.html
// Fun fact: Sega did not physically wire the upper 8 bits of the data transfer
// bus, so although the MB86967 can do 16-bit data transfers, the pins are just
// not hooked up to do it! So packets need to be transfered one byte at a time
// to and from the LAN Adapter.
//
// Also, the LAN Adapter operates in ISA bus mode, not PC Card mode.
//
// --Moopthehedgehog

// TEMP
//#define LAN_LOOP_TIMING
//#define LAN_TX_LOOP_TIMING
//#define LAN_RX_LOOP_TIMING
//#define LAN_FULL_TRIP_TIMING

#ifdef LAN_LOOP_TIMING
#include "perfctr.h"
static char uint_string_array[11] = {0};
#endif
// end TEMP

adapter_t adapter_la = {
	"LAN Adapter (HIT-0300)",
	{ 0 },		// Mac address
	{ 0 },		// 2-byte alignment pad
	la_bb_detect,
	la_bb_init,
	la_bb_start,
	la_bb_stop,
	la_bb_loop,
	la_bb_tx
};

static volatile unsigned char lan_link_up = 0;
// This needs to persist across program loads and resets, so it needs to go in .data section
static volatile unsigned char first_transmit = 2;
static int bb_started = 0;

static void net_strobe_eeprom(void);
static void net_read_eeprom(uint8 *data);
static void net_sleep_ms(int ms);
static int la_bb_rx(void);

// This is in dhcp.h now
//typedef unsigned char uint8;
// Still need these for this file only
typedef volatile unsigned int vuint32;
typedef volatile unsigned short vuint16;
typedef volatile unsigned char vuint8;

/* Some basic macros to assist with register access */
#undef REGL
#undef REGS
#undef REGC
#define REGL(a) (vuint32 *)(a)
#define REGS(a) (vuint16 *)(a)
#define REGC(a) (vuint8 *)(a)

static vuint8 *xpc = REGC(0xa0600000);
//static vuint16 *xps = REGS(0xa0600000);
//static vuint32 *xpl = REGL(0xa0600000);
#define REG(x) ( xpc[(x)*4 + 0x400] )
#define REGW(x) ( xpc[(x)*4 + 0x400] )

static void net_strobe_eeprom(void)
{
	REGW(16) = FE_B16_SELECT;
	REGW(16) = FE_B16_SELECT | FE_B16_CLOCK;
	REGW(16) = FE_B16_SELECT | FE_B16_CLOCK;
	REGW(16) = FE_B16_SELECT;
}

static void net_read_eeprom(uint8 *data)
{
	//uint8 save16, save17;
	uint8 val, n, bit;

	/* Save the current value of the EEPROM registers */
//	save16 = REG(16);
//	save17 = REG(17);

	/* Read bytes from EEPROM, two per iteration */
	for (n=0; n<3; n++) {
		/* Reset the EEPROM interface */
		REGW(16) = 0;
		REGW(17) = 0;

		/* Start EEPROM access */
		REGW(16) = FE_B16_SELECT;
		REGW(17) = FE_B17_DATA;
		net_strobe_eeprom();

		/* Pass the iteration count as well as a READ command */
		val = 0x80 | n;
		for (bit=0x80; bit != 0x00; bit>>=1) {
			REGW(17) = (val & bit) ? FE_B17_DATA : 0;
			net_strobe_eeprom();
		}
		REGW(17) = 0;

		/* Read a byte */
		val = 0;
		for (bit=0x80; bit != 0x00; bit>>=1) {
			net_strobe_eeprom();
			if (REG(17) & FE_B17_DATA)
				val |= bit;
		}
		*data++ = val;

		/* Read one more byte */
		val = 0;
		for (bit=0x80; bit != 0x00; bit>>=1) {
			net_strobe_eeprom();
			if (REG(17) & FE_B17_DATA)
				val |= bit;
		}
		*data++ = val;
	}
}

/* To select a register bank */
#define SETBANK(x) do { \
	int i = (REG(7) & ~0x0c) | ((x) << 2); \
	REGW(7) = i; \
} while(0)

/* Libdream-style sleep function */
// In units of milliseconds, max 245 (about 0.21 secs)
static void net_sleep_ms(int ms)
{
	int i, cnt;
	vuint32 *a05f688c = (vuint32*)0xa05f688c;

	cnt = 0x1800 * 0x58e * ms / 1000;
	for (i=0; i<cnt; i++)
		(void)*a05f688c;
}

#ifdef LAN_ADAPTER_DEBUG

static void disp_status2(const char * status)
{
	clear_lines(222, 24, LAN_BG_COLOR);
	draw_string(30, 222, status, STR_COLOR);
}
#define DEBUG(s) disp_status2(s)

/* #define DEBUG(s) scif_puts(s) */

#else

#define DEBUG(s)

#endif

/* Reset the lan adapter and verify that it's there and alive */
int la_bb_detect(void)
{
	uint8 type;

	DEBUG("bb_detect entered\r\n");

	if (bb_started > 0) {
		DEBUG("bb_detect exited, already started\r\n");
		return 0;
	}

	if (bb_started != 0) {
		clear_lines(168, 24, LAN_BG_COLOR);
		draw_string(30, 168, "bb_detect() called out of sequence!", 0xf800);
		for (;;)
			;
	}

	/* Reset the interface */
	xpc[0x0480] = 0;
	xpc[0x0480] = 1;
	xpc[0x0480] = 0;

	/* Give it a few ms to come back */
	net_sleep_ms(100);

	/* Read the chip type and verify it */
	type = (REG(7) >> 6) & 3;
	if (type != 2) {
		DEBUG("bb_detect exited, detection failed\r\n");
		return -1;
	}

	/* That should do */
	DEBUG("bb_detect exited, success\r\n");
	bb_started = 1;
	global_bg_color = LAN_BG_COLOR;
	installed_adapter = LAN_MODEL;
	return 0;
}

/* Reset the lan adapter and set it up for send/receive */
int la_bb_init(void)
{
	uint8 i;
	//uint8 type;
	uint8 mac[6];

	DEBUG("bb_init started\r\n");

	if (bb_started == 2) {
		DEBUG("bb_init exited, already done\r\n");
		return 0;
	}

	// Alright, let's actually do the LAN adapter right this time.
	// --Moopthehedgehog

	/* Read the EEPROM data */
	// Gets the mac address
	net_read_eeprom(mac);

	// ENA disable REG(6)
	// Need to set 0x80 bit to 1 to init transmit and receive buffers
	// Also needed to access node ID and multicast regs
	net_sleep_ms(2);
	REGW(6) = 0x80 | DLCR6_FLAGS; // DLCR6 bit 7 ENA DLC
  net_sleep_ms(2); // only really need 200us

	// Disable interrupts since dcload doesn't use them
	REGW(2) = 0x00; // DLCR2
	REGW(3) = 0x00; // DLCR3

	/* Clear interrupt status */
	REGW(0) = 0xff; // DLCR0
	REGW(1) = 0xff; // DLCR1

	/* Power down chip (standby) */
	net_sleep_ms(2);
	REGW(7) = REG(7) & 0xdf; // DLCR7
	net_sleep_ms(2);

	/* Power up the chip */
	// And otherwise set the default values
	net_sleep_ms(2);
	REGW(7) = (REG(7) & 0xc0) | 0x20;
	net_sleep_ms(2);

	/* Select BANK0 and write the MAC address */
	SETBANK(0);
	net_sleep_ms(2);
	for (i=0; i<6; i++) {
		REGW(i+8) = mac[i];
	}

	/* Copy it into the adapter structure for dcload */
	memcpy(adapter_la.mac, mac, 6);

	/* Clear the multicast address */
	SETBANK(1);
	for (i=0; i<6; i++)
		REGW(i+8) = 0x0;

	/* Select the BMPR bank for normal operation */
	SETBANK(2);

	// Configure transmit mode reg to defaults (LBC normal mode (= 1), CNTRL to 1)
	// Set to half duplex, since most things use auto-negotiation now and there's
	// no way to autonegotiate 10Mbps full-duplex (detecting NLPs just falls back
	// to legacy 10Mbps half-duplex). Technically dcload does half-duplex anyways,
	// so this could be set to full-duplex and be fine, but let's just do it the
	// right way. We get error detection and the like in half-duplex mode that we
	// don't get in full-duplex, in any case.
	REGW(4) = 0x06; // DLCR4 (bit 0 is duplex, set 1 for full-duplex)

	// Don't receive packets yet
	REGW(5) = 0;

	REGW(10) = 0; // Clear number of packets in transmit queue
	REGW(11) = 0x01; // Skip packet causing 16 COLs, don't do anything if one happens
	REGW(12) = 0;
	REGW(13) = 0; // Don't change base address, all other params should be 0 (DMA burst is single)
	REGW(14) = 0x01; // Filter packets transmitted from self
	REGW(15) = 0; // Writing 0 to this one does nothing (using full duplex, now, so SQE doesn't matter. Polarity is auto-corrected and LINK FAIL is checked by la_bb_loop())

	/* Enable transmitter / receiver */
	net_sleep_ms(2);
	REGW(6) = DLCR6_FLAGS;
	net_sleep_ms(2);

	/* Set non-promiscuous/normal mode (use 0x03 for promiscuous) */
	REGW(5) = 0x02;

	// Reset first_transmit tracker
	first_transmit = 1;

	DEBUG("bb_init exited, success\r\n");
	bb_started = 2;
	return 0;
}

/* Start lan adapter */
void la_bb_start(void)
{
	DEBUG("bb_start entered\r\n");

	if (bb_started != 3) {
		DEBUG("bb_start exited, state != 3\r\n");
		return;
	}

	/* Set non-promiscuous/normal mode (use 0x03 for promiscuous) */
	REGW(5) = (REG(5) & ~0x83) | 0x02;

	bb_started = 2;
	DEBUG("bb_start exited\r\n");
}

/* Stop lan adapter */
void la_bb_stop(void)
{
	DEBUG("bb_stop entered\r\n");

	if (bb_started != 2) {
		DEBUG("bb_stop exited, state != 2\r\n");
		return;
	}

	/* Make sure we aren't transmitting currently */
/*
	while (REG(10) & 0x7f)
		net_sleep_ms(2);
*/
	while(!(REG(0) & 0x80)) // wait for any prior transmit of all packets in transmit buffer to finish
	{
		net_sleep_ms(2);
	}
	// We could reset first_transmit, or we could leave the TMT OK bit high for the next time (better)

	/* Disable all receive */
	REGW(5) = (REG(5) & ~0x83);

	bb_started = 3;
	DEBUG("bb_stop exited\r\n");
}

// For stats
//static unsigned int total_pkts_rx = 0, total_pkts_tx = 0;

// Display stats if so desired
/* static void draw_total() {
	char buffer[16];

	uint_to_string(total_pkts_rx, buffer);
	clear_lines(120, 24, LAN_BG_COLOR);
	draw_string(0, 120, "RX: ", 0xffe0);
	draw_string(4*12, 120, buffer, 0xffff);

	uint_to_string(total_pkts_tx, buffer);
	clear_lines(144, 24, LAN_BG_COLOR);
	draw_string(0, 144, "TX: ", 0xffe0);
	draw_string(4*12, 144, buffer, 0xffff);
} */

/* Transmit a packet */
/* Note that it's technically possible to queue up more than one packet
   at a time for transmission, but this is the simple way. */
int la_bb_tx(unsigned char *pkt, int len)
{
	int i;
/*	char buffer[16]; */

	DEBUG("bb_tx entered\r\n");
	// transmission CAN still occur if RX is stopped (bb_started == 3)
	if (bb_started < 2) {
		clear_lines(168, 24, LAN_BG_COLOR);
		draw_string(30, 168, "bb_tx() called out of sequence!", 0xf800);
		for (;;)
			;
	}

	len &= 0x07ff; // max length is 11 bits, meaning 4095-byte packets...but it does
	// not really matter since a standard packet maxes at a grand total of 1514 bytes anyways.

	/* Wait for queue to empty */
 // Not necessary in dual-bank mode
/*
	while (REG(10) & 0x7f)
		net_sleep_ms(2);
*/

	/* clear_lines(192, 24, LAN_BG_COLOR);
	uint_to_string(len, buffer);
	draw_string(0, 192, buffer, 0xffff); */

	unsigned char *copyback_pkt = (unsigned char*)((unsigned int)pkt & 0x1fffffff);

// Tx time
#ifdef LAN_TX_LOOP_TIMING
		unsigned long long int first_array = PMCR_RegRead(DCLOAD_PMCR);
#endif

	/* Is the length less than the minimum? */
	if(len < 60)
	{
		/* Poke the length */
		REGW(8) = 60; // Low byte (LE)
		REGW(8) = 00; // High byte (LE)

		/* Write the packet */
		for (i=0; i<len; i++)
		{
			//REGW(8) = pkt[i];
			REGW(8) = copyback_pkt[i];
		}

		// Pad with zeroes to 60 bytes
		for (i=len; i<60; i++)
		{
			REGW(8) = 0x00;
		}
		// NOTE: The reason this is hardcoded to 60 is because the minimum frame
		// size allowed is 46 bytes + 14 byte ethernet header. Well, it's actually 64,
		// but the NIC is configured to auto-append a 4-byte CRC.
	}
	else
	{
		/* Poke the length */
		REGW(8) = (len & 0x00ff);
		REGW(8) = (len & 0x0700) >> 8;

		/* Write the packet */
		for (i=0; i<len; i++)
		{
			//REGW(8) = pkt[i];
			REGW(8) = copyback_pkt[i];
		}
	}

// Tx time end
#ifdef LAN_TX_LOOP_TIMING
		unsigned long long int second_array = PMCR_RegRead(DCLOAD_PMCR);
		unsigned int loop_difference = (unsigned int)(second_array - first_array);

		clear_lines(222, 24, global_bg_color);
		uint_to_string_dec(loop_difference, (char*)uint_string_array);
		draw_string(30, 222, uint_string_array, STR_COLOR);
#endif

	// This will be blocked on the first transmit because setting ENA DLC (REG(6) bit 0x80) clears TMT OK...
	// So keep track of whether this is the first transmit or not
	if(__builtin_expect(first_transmit, 0)) // if(first_transmit), but telling GCC we usually expect first_transmit to be 0
	{
		first_transmit = 0;
	}
	else
	{
		while(!(REG(0) & 0x80)) // wait for prior transmit (in the other bank, since dcload uses dual-bank mode) to finish
		{
			net_sleep_ms(2);
		}
		REGW(0) = 0x80; // clear transmit in progress
	}

	/* Start the transmitter */
	REGW(10) = 0x80 | 1;	/* 1 packet, 0x80 = start */

// For stats
//	total_pkts_tx++;
	/* if (!running)
		draw_total(); */

	DEBUG("bb_tx exited\r\n");
	return 1;
}

/* Check for received packets */
static int la_bb_rx(void)
{
	int i, len, count;
	unsigned short status;

	DEBUG("bb_rx entered\r\n");

	if (bb_started != 2) {
		clear_lines(168, 24, LAN_BG_COLOR);
		draw_string(30, 168, "bb_rx() called out of sequence!", 0xf800);
		for (;;)
			;
	}

	for (count = 0; ; count++)
	{

		/* Is the buffer empty? */
		if(REG(5) & 0x40)
		{
			DEBUG("bb_rx exited, no more packets\r\n");
			return count;
		}

// Full loop timing
#ifdef LAN_FULL_TRIP_TIMING
    unsigned long long int first_array1 = PMCR_RegRead(DCLOAD_PMCR);
#endif

		/* Get the receive status byte */
		status = REG(8);
		(void)REG(8);

		/* Get the packet length */
		// This value is always in bytes
		len = REG(8);
		len |= REG(8) << 8;

		/* Check for errors */
		if(__builtin_expect((status & 0x3e) != 0x20, 0))
		{
			DEBUG("bb_rx exited: error\r\n");
			return -1;
		}

		/* Read the packet */
		if(__builtin_expect(len > RX_PKT_BUF_SIZE, 0))
		{
			DEBUG("bb_rx exited: big packet\r\n");
			return -2;
		}

		unsigned char *copyback_current_pkt = (unsigned char*)((unsigned int)current_pkt & 0x1fffffff); // copyback pkt in cached memory area

// Rx time
#ifdef LAN_RX_LOOP_TIMING
			unsigned long long int first_array = PMCR_RegRead(DCLOAD_PMCR);
#endif

		// This loop is dumb, but we are able to max out the LAN adapter with it, so that's neat
		for (i=0; i<len; i++)
		{
			//current_pkt[i] = REG(8);
			copyback_current_pkt[i] = REG(8);
		}
		// Ensure cached data is written to memory
		CacheBlockWriteBack((unsigned char*) ((unsigned int)raw_current_pkt & 0x1fffffe0), (2 + len + 31)/32);

// Rx time end
#ifdef LAN_RX_LOOP_TIMING
		unsigned long long int second_array = PMCR_RegRead(DCLOAD_PMCR);
		unsigned int loop_difference = (unsigned int)(second_array - first_array);

		clear_lines(246, 24, global_bg_color);
		uint_to_string_dec(loop_difference, (char*)uint_string_array);
		draw_string(30, 246, uint_string_array, STR_COLOR);
#endif

		/* Submit it for processing */
		//process_pkt(current_pkt);
		process_pkt(copyback_current_pkt);

// For stats
//		total_pkts_rx++;
		/* if (!running)
			draw_total(); */

#ifdef LAN_FULL_TRIP_TIMING
		unsigned long long int second_array1 = PMCR_RegRead(DCLOAD_PMCR);
		unsigned int loop_difference1 = (unsigned int)(second_array1 - first_array1);

		clear_lines(412, 24, global_bg_color);
		uint_to_string_dec(loop_difference1, (char*)uint_string_array);
		draw_string(30, 412, uint_string_array, STR_COLOR);
#endif
	}

	return count;
}

#ifdef LAN_ADAPTER_DEBUG
static char reg_agg_temp[9] = {0};
#endif

/* Loop doing something interesting */
void la_bb_loop(int is_main_loop)
{
	int result;
	int link_change_message = 0;

	DEBUG("bb_loop entered\r\n");

	if(is_main_loop)
	{
		if(!(booted || running))
		{
			disp_info();
		}
		// This adapter does not support autonegotiation, so this delay ensures that
		// the other end of the link (which probably does support it) has time to
		// realize this device uses NLPs instead of FLPs. It takes 50-150ms for this
		// device to enter LINK FAIL, so wait 150ms to start in link fail.
		net_sleep_ms(150);

		// In the event a user actually connects to something NOT using autonegotiation,
		// set lan_link_up to 0 so that this loop can work with those, too.
		lan_link_up = 0;
	}

	while (!escape_loop)
	{
		/* Check for received packets */
		if(REG(1) & 0x80) // Do we have a packet in the receive buffer?
		{
			result = la_bb_rx();
			if(__builtin_expect((result < 0) && booted && (!running), 0))
			{
				clear_lines(320, 24, LAN_BG_COLOR);
				draw_string(30, 320, "receive error!", 0xffff);
			}
			REGW(1) = 0x80; // clear packet ready
			// Hardware will re-set this bit if there's another packet in the receive buffer
		}

		// Check for ethernet cable
		// 10Base-T heartbeat signal is this chip's LINK FAIL mechanism, not the "carrier sense" stuff
		if(__builtin_expect(REG(15) & 0x40, 0))
		{
			if ((!link_change_message) && booted && (!running))
			{
				disp_status("link change...");
				link_change_message = 1; // Don't need to keep re-printing this every loop
			}

			// CLR link fail, if it comes back up we're still in link fail state
			REGW(15) = 0x40;

			// No link
			lan_link_up = 0;
		}
		else if(__builtin_expect(!lan_link_up, 0))
		{
			// There is an excellent thread on U-Boot's mailing list about how long to
			// wait for autonegotiation to complete:
			// https://lists.denx.de/pipermail/u-boot/2009-February/047056.html
			// They ultimately use 4.5 seconds (see later in the thread) to match
			// the Linux kernel, as the Linux kernel's e1000 driver defines
			// PHY_AUTO_NEG_LIMIT as 45. The e1000 driver uses 100msec sleeps in a
			// loop that iterates PHY_AUTO_NEG_LIMIT times as a timeout while checking
			// for autonegotiation to complete (or not).
			// Well, ok, and then they go and increase it to 8 seconds:
			// https://lists.denx.de/pipermail/u-boot/2015-August/223424.html
			// But they also have a variable for 2 seconds, PHY_FORCE_TIME. Since we
			// are actually forcing a link speed, we should probably thus use the 2.0
			// seconds described in the mailing list (interestingly, it does not appear
			// that U-Boot actually uses PHY_FORCE_TIME).

			unsigned int autoneg_wait_time = 20; // every 10 is one second
			for(unsigned int tenthsecs = 0; tenthsecs < autoneg_wait_time; tenthsecs++)
			{
				net_sleep_ms(115); // 0.1 sec
			}

			if (booted && (!running))
			{
				disp_status("idle...");
				link_change_message = 0;
			}

			// Good to go!
			lan_link_up = 1;
		}

#ifdef LAN_ADAPTER_DEBUG
		// BMPR15, DLCR4, DLCR1, DLCR0
		unsigned int reg_aggregate = ((unsigned int)(REG(15)) << 24) | ((unsigned int)(REG(4)) << 16) | ((unsigned int)(REG(1)) << 8) | REG(0);
		clear_lines(198, 24, LAN_BG_COLOR);
		uint_to_string(reg_aggregate, (unsigned char*)reg_agg_temp);
		draw_string(126, 198, reg_agg_temp, STR_COLOR);
#endif

		if(is_main_loop && lan_link_up) // Only want this to run in main loop
		{
			// Do we need to renew our IP address?
			// This will override set_ip_from_file() if the ip is in the 0.0.0.0/8 range
			set_ip_dhcp();
		}
	}

	DEBUG("bb_loop exited\r\n");

	escape_loop = 0;
}
