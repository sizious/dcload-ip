#include <string.h>
#include "packet.h"
#include "net.h"
#include "adapter.h"
#include "rtl8139.h"
#include "dcload.h"

#include "dhcp.h"
#include "memfuncs.h"

static rtl_status_t rtl = {0};
static volatile unsigned char rtl_link_up = 0;
static volatile unsigned char rtl_is_copying = 0;

/* 8, 16, and 32 bit access to the PCI I/O space (configured by GAPS) */
static vuc * const nic8 = REGC(0xa1001700);
static vus * const nic16 = REGS(0xa1001700);
static vul * const nic32 = REGL(0xa1001700);

/* 8, 16, and 32 bit access to the PCI MEMMAP space (configured by GAPS) */
static vuc * const mem8 = REGC(0xa1840000);
static vus * const mem16 = REGS(0xa1840000);
static vul * const mem32 = REGL(0xa1840000);

static int bb_detect()
{
	// This pointer's data is always aligned to 4 bytes--just look at the register address!
	const char *str = (char*)REGC(0xa1001400);
	if (!memcmp_32bit_eq(str, GAPSPCI_ID, 16/4))
	{
		global_bg_color = BBA_BG_COLOR;
		installed_adapter = BBA_MODEL;
		return 0;
	}
	else
	{
		return -1;
	}
}

static void rtl_reset()
{
	/* Soft-reset the chip */
	nic8[RT_CHIPCMD] = RT_CMD_RESET;

	/* Wait for it to come back */
	while (nic8[RT_CHIPCMD] & RT_CMD_RESET);
}

static void rtl_init()
{
	unsigned int tmp;

	/* Read MAC address */
	// Don't need to do anything with the eeprom if we're just reading it.
	tmp = nic32[RT_IDR0];
	rtl.mac[0] = tmp & 0xff;
	rtl.mac[1] = (tmp >> 8) & 0xff;
	rtl.mac[2] = (tmp >> 16) & 0xff;
	rtl.mac[3] = (tmp >> 24) & 0xff;
	tmp = nic32[RT_IDR0+1];
	rtl.mac[4] = tmp & 0xff;
	rtl.mac[5] = (tmp >> 8) & 0xff;
	memcpy(adapter_bba.mac, rtl.mac, 6);

	/* Soft-reset the chip to clear any garbage from power on */
	rtl_reset();

// Why reset it twice? is this a GAPS thing?
	/* Do another reset */
	rtl_reset();

	/* Setup Rx and Tx buffers */
	nic32[RT_RXBUF/4] = 0x01840000;
	nic32[RT_TXADDR0/4 + 0] = 0x01846000;
	nic32[RT_TXADDR0/4 + 1] = 0x01846800;
	nic32[RT_TXADDR0/4 + 2] = 0x01847000;
	nic32[RT_TXADDR0/4 + 3] = 0x01847800;

	/* Enable receive and transmit functions */
	nic8[RT_CHIPCMD] = RT_CMD_RX_ENABLE | RT_CMD_TX_ENABLE;

	/* Set Rx FIFO threshold to 16 bytes, Rx size to 16k+16, 1024 byte DMA burst */
//	nic32[RT_RXCONFIG/4] = 0x00000e00; // (1<<7 = 0x80) for nowrap or bit 7 = 0 for wrap, 1024 byte dma burst (6<<8 = 0x600) is undocumented in 8139C datasheet, but it's in the 8139D datasheet: https://www.cs.usfca.edu/~cruse/cs326f04/RTL8139D_DataSheet.pdf
	// Why only 16k + 16? let's do 32k + 16.
	//nic32[RT_RXCONFIG/4] = 0x00001600;
	nic32[RT_RXCONFIG/4] = 0x0000f600; // whole packet DMA threshhold, DMA 1024 burst, 32K + 16 (supposedly there's a bug with 64K on DC? maybe it's with forced nowrap + GAPS?)

	/* Set Tx 1024 byte DMA burst */
	nic32[RT_TXCONFIG/4] = 0x03000600; // Set IFG to NOT violate 802.3 standard
	// Found a bug: this was 00000600 before. 1024 byte DMA burst is 0x600 (hex), not 600 (dec). See pgs. 20-21 of RTL8139 datasheet: http://realtek.info/pdf/rtl8139cp.pdf

	/* Enable writing to the config registers */
	nic8[RT_CFG9346] = 0xc0;

	/* Disable power management (zeroes are otherwise default values) */
	nic8[RT_CONFIG1] = 0;

	/* Enable FIFO auto-clear */
	nic8[RT_CONFIG4] |= 0x80;

	/* Switch back to normal operation mode */
	nic8[RT_CFG9346] = 0;

	/* Enable receiving broadcast and physical match packets */
	nic32[RT_RXCONFIG/4] |= 0x0000000a;

	/* Filter out all multicast packets */
	nic32[RT_MAR0/4 + 0] = 0;
	nic32[RT_MAR0/4 + 1] = 0;

	/* Disable all multi-interrupts */
	nic16[RT_MULTIINTR/2] = 0;

	/* clear all interrupts */
	nic16[RT_INTRSTATUS/2] = 0xffff;

	/* Reset RXMISSED counter */
	nic32[RT_RXMISSED/4] = 0;

	/* Enable RX/TX once more */
	nic8[RT_CHIPCMD] = RT_CMD_RX_ENABLE | RT_CMD_TX_ENABLE;

	/* Enable auto-negotiation and restart that process */
	nic16[RT_MII_BMCR/2] |= 0x9200;

	/* Set the driver-loaded bit */
	nic8[RT_CONFIG1] |= 0x20;

	/* Initialize status vars */
	rtl.cur_tx = 0;
	rtl.cur_rx = 0;
}

static int bb_init()
{
	vuc * const g28 = REGC(0xa1000000);
	vus * const g216 = REGS(0xa1000000);
	vul * const g232 = REGL(0xa1000000);

	int i;

	/* Initialize the "GAPS" PCI glue controller.
	It ain't pretty but it works. */
	g232[0x1418/4] = 0x5a14a501;                /* M */
	i = 10000;
	while ((!(g232[0x1418/4] & 1)) && (i > 0))
		i--;
	if (!(g232[0x1418/4] & 1)) {
		return -1;
	}
	g232[0x1420/4] = 0x01000000;
	g232[0x1424/4] = 0x01000000;
	g232[0x1428/4] = 0x01840000;                /* DMA Base */
	g232[0x142c/4] = 0x01840000 + 32*1024;      /* DMA End */
	g232[0x1414/4] = 0x00000001;
	g232[0x1434/4] = 0x00000001;

	/* Configure PCI bridge (very very hacky). If we wanted to be proper,
	we ought to implement a full PCI subsystem. In this case that is
	ridiculous for accessing a single card that will probably never
	change. Considering that the DC is now out of production officially,
	there is a VERY good chance it will never change. */
	g216[0x1606/2] = 0xf900;
	g232[0x1630/4] = 0x00000000;
	g28[0x163c] = 0x00;
	g28[0x160d] = 0xf0;
	(void)g216[0x04/2];
	g216[0x1604/2] = 0x0006;
	g232[0x1614/4] = 0x01000000;
	(void)g28[0x1650];

	rtl_init();

	return 0;
}

static void bb_stop(void)
{
	nic32[RT_RXCONFIG/4] &= 0xfffffff5;
}

static void bb_start(void)
{
	nic32[RT_RXCONFIG/4] |= 0x0000000a;
}

static vuc * const txdesc[4] = {
	REGC(0xa1846000),
	REGC(0xa1846800),
	REGC(0xa1847000),
	REGC(0xa1847800)
};

static int bb_tx(unsigned char * pkt, int len) // pg. 15 in RTL8139C datasheet: http://realtek.info/pdf/rtl8139cp.pdf
{
	while (!(nic32[RT_TXSTATUS0/4 + rtl.cur_tx] & 0x2000)) { // While tx is not complete (checking OWN)
		if (nic32[RT_TXSTATUS0/4 + rtl.cur_tx] & 0x40000000) { // Check for abort
			nic32[RT_TXCONFIG/4] |= 0x1; // Found another bug: (nic32[RT_TXSTATUS0/4 + rtl.cur_tx] |= 1; // <-- If abort, set descriptor size to 1) should be RT_TXCONFIG register |= 1, which clears abort state and retransmits, see pg 21 of RTL8139C or pg 17 of RTL8139D datasheet
		}
	}

	if (len < 60) { /* 8139 doesn't auto-pad */
		// So pad it.
		//
		// We absolutely need to pad this, otherwise prior packets can leak into the
		// padding. It's called "EtherLeak," and the RTL8139 is apparently a poster
		// child chipset for the issues that come from lacking auto-pad.
		//
		memset_zeroes_64bit(raw_tx_small_packet_zero_buffer, 64/8); // Zero out our small packet buffer
//		memcpy(tx_small_packet_zero_buffer, pkt, len); // Copy the data to the small packet buffer
		// Copy data up to 8 byte alignment, then do 8-byte aligned copy
		memcpy_16bit(tx_small_packet_zero_buffer, pkt, 6/2);
		SH4_aligned_memcpy(tx_small_packet_zero_buffer + 6, pkt + 6, len - 6);
		len = 60;
		pkt = tx_small_packet_zero_buffer; // Small packet buffer has the data and padding bytes
		// NOTE: The reason this is hardcoded to 60 is because the minimum frame
		// size allowed is 46 bytes + 14 byte ethernet header.
	}
/*
//-->
	else
	{
		// Align packet to 4 bytes for G2 transfer
		memcpy_16bit(raw_pkt_buf, pkt, (len + (len%2))/2);
		pkt = raw_pkt_buf;
	}
*/

//-->	SH4_aligned_pktcpy((unsigned char*)txdesc[rtl.cur_tx], pkt, len); // Copy 4-byte aligned packet to txdesc buffer for sending
	// Typecast here just removes a warning.
	memcpy((unsigned char*)txdesc[rtl.cur_tx], pkt, len); // Copy packet to txdesc buffer for sending

	nic32[RT_TXSTATUS0/4 + rtl.cur_tx] = len; // Set len (SIZE field), destructively zeroing out all other R/W settings. OWN needs to be cleared by software; it does here.
	// Supposedly software writes don't impact the read-only bits. This zeroing also sets FIFO TX threshold to 8 bytes. Finally, writing to the status register triggers the packet send.
	rtl.cur_tx = (rtl.cur_tx + 1) % 4; // Move to next txdesc buffer

	return 1;
}

static void pktcpy(unsigned char *dest, unsigned char *src, int n)
{
	if (n > RX_PKT_BUF_SIZE)
		return;

	if ((unsigned int)(src + n) < (unsigned int)(0xa1840000 + RX_BUFFER_LEN))
	{
		memcpy(dest, src, n);
//-->		SH4_aligned_pktcpy(dest, src, n);
	}
	else // Wrap
	{
		memcpy(dest, src, (0xa1840000 + RX_BUFFER_LEN) - (unsigned int)src);
		memcpy(dest + ((0xa1840000 + RX_BUFFER_LEN) - (unsigned int)src), (unsigned char *)0xa1840000, (unsigned int)(src + n) - (0xa1840000 + RX_BUFFER_LEN));
//-->		SH4_aligned_pktcpy(dest, src, (0xa1840000 + RX_BUFFER_LEN) - (unsigned int)src);
//-->		SH4_aligned_pktcpy(dest + ((0xa1840000 + RX_BUFFER_LEN) - (unsigned int)src), (unsigned char *)0xa1840000, (unsigned int)(src + n) - (0xa1840000 + RX_BUFFER_LEN));
	}
}

static int bb_rx()
{
	int processed;
	unsigned int rx_status;
	int rx_size, pkt_size, ring_offset;
	int i;
	unsigned char *pkt;

	processed = 0;

	/* While we have frames left to process... */
	while (!(nic8[RT_CHIPCMD] & 1)) {

		/* Get frame size and status */
		ring_offset = rtl.cur_rx % RX_BUFFER_LEN;
		rx_status = mem32[0x0000/4 + ring_offset/4];
		rx_size = (rx_status >> 16) & 0xffff;
		pkt_size = rx_size - 4;

		/* apparently this means the rtl8139 is still copying */
		if (rx_size == 0xfff0) {
			rtl_is_copying = 1; // Really don't want to run a DHCP renewal while data is in flight...
			break;
		}
		rtl_is_copying = 0;

		if ((rx_status & 1) && (pkt_size <= RX_PKT_BUF_SIZE)) {
			pkt = (unsigned char*)(mem8 + 0x0000 + ring_offset + 4); // + 4 to skip the status byte
//			pkt = (unsigned char*)(mem8 + 0x0000 + ring_offset);
			// Actually, keep the status byte; we can use it to use the fast memcpy for aligning the buffer
			// This saves precious space as it means we don't need to make a fast memmove to handle the
			// overlapping memory region.

			// This means the first two buffer bytes will have some garbage (well, it'll be the status half of the
			// status longword), but that doesn't matter because those are just offset bytes to align the packet
			// and don't otherwise matter.

			// ... Or just copy the data as normal to &(raw_current_pkt[4]) and then just move it up 2...

			pktcpy(current_pkt, pkt, pkt_size);
//-->			pktcpy(&(raw_current_pkt[4]), pkt, pkt_size);
		//	pktcpy(raw_current_pkt, pkt, rx_size); // Use rx_size instead of pkt_size

			// Move packet up 2 bytes in buffer to 4-Byte-align ip header, udp header, command struct
			// ...And now memcpy_16bit() can be used to do 2 bytes at a time and negate the need for memmove.
			// Using both SH4_aligned_pktcpy for pktcpy and this 16bit memcpy is faster than the fast memcpy in memcpy.S
//-->			memcpy_16bit(current_pkt, &(raw_current_pkt[4]), (pkt_size + (pkt_size%2))/2); // if pkt_size is odd, add 1.
		//	memmove(current_pkt, &(raw_current_pkt[4]), pkt_size);

			process_pkt(current_pkt);
		}

		// Align next packet to 4-bytes (also add 4 to account for transmit status; the 4 extra bytes included in rx_size are the CRC according to RealTek)
		rtl.cur_rx = (rtl.cur_rx + rx_size + 4 + 3) & ~3;
		nic16[RT_RXBUFTAIL/2] = rtl.cur_rx - 16; // Why 16? NetBSD and Linux do this, too. Status is 4, CRC appended is 4, what's the other 8? Ethernet preamble?
		// Things don't work if this isn't 16, anyways (I tried changing it). Maybe this is 16 for DMA reasons?
		// RealTek does it here: https://www.cs.usfca.edu/~cruse/cs326f04/RTL8139_ProgrammersGuide.pdf

		// Ack it
		i = nic16[RT_INTRSTATUS/2];
		if (i & RT_INT_RX_ACK)
			nic16[RT_INTRSTATUS/2] = RT_INT_RX_ACK;

		processed++;
	}

	return processed;
}

static void bb_loop(int is_main_loop)
{
	unsigned int intr;
//	int i;

	intr = 0;

	if(is_main_loop)
	{
		if(!(booted || running))
		{
			disp_info();
		}

		// Need to wait for a link change before it's OK to do anything
		rtl_link_up = 0;
	}

	// OMG this is polling the network adapter. Well, ok then.
	while(!escape_loop)
	{

		/* Check interrupt status */
		if (nic16[RT_INTRSTATUS/2] != intr)
		{
			intr = nic16[RT_INTRSTATUS/2];
			nic16[RT_INTRSTATUS/2] = intr & ~RT_INT_RX_ACK;
		}

		/* Did we receive some data? */
		if (intr & RT_INT_RX_ACK)
		{
			//i = bb_rx();
			bb_rx();
		}

		/* link change */
		if (__builtin_expect(intr & RT_INT_RXFIFO_UNDERRUN, 0))
		{

			if (booted && (!running))
			{
				disp_status("link change...");
			}

			nic16[RT_MII_BMCR/2] = 0x9200;

			/* wait for valid link */
			while (!(nic16[RT_MII_BMSR/2] & 0x20));

			/* wait for the additional link change interrupt that is coming */
			while (!(nic16[RT_INTRSTATUS/2] & RT_INT_RXFIFO_UNDERRUN));
			nic16[RT_INTRSTATUS/2] = RT_INT_RXFIFO_UNDERRUN;

			if (booted && (!running))
			{
				disp_status("idle...");
			}

			rtl_link_up = 1; // Good to go!
		}

		/* Rx FIFO overflow */
		if (intr & RT_INT_RXFIFO_OVERFLOW)
		{
			/* must clear Rx Buffer Overflow too for some reason */
			// It's an errata (hardware bug/quirk), this needs to be done.
			nic16[RT_INTRSTATUS/2] = RT_INT_RXBUF_OVERFLOW;
		}

		/* Rx Buffer overflow */
		if (intr & RT_INT_RXBUF_OVERFLOW)
		{
/*
			// Update CAPR
			rtl.cur_rx = nic16[RT_RXBUFHEAD];
			nic16[RT_RXBUFTAIL] = rtl.cur_rx - 16;
			rtl.cur_rx = 0;

			// Disable receive
			nic8[RT_CHIPCMD] = RT_CMD_TX_ENABLE;

			// Wait for it
			while ( !(nic8[RT_CHIPCMD] & RT_CMD_RX_ENABLE))
				nic8[RT_CHIPCMD] = RT_CMD_TX_ENABLE | RT_CMD_RX_ENABLE; // Weirdly, keep spamming re-enable receive

			// Re-set RXCONFIG
			nic32[RT_RXCONFIG/4] = 0x0000f60a;

			// disable interrupts
			nic16[RT_INTRSTATUS/2] = 0xffff;
	*/
			// NetBSD, FreeBSD, and OpenBSD all just do a full re-init if this happens.
			rtl_init();
		}

		if(is_main_loop && rtl_link_up && (!rtl_is_copying)) // Only want this to run in main loop
		{
			// Do we need to renew our IP address?
			// This will override set_ip_from_file() if the ip is in the 0.0.0.0/8 range
			set_ip_dhcp();
		}

	}
	escape_loop = 0;
}

// Pull together all the goodies
adapter_t adapter_bba = {
	"Broadband Adapter (HIT-0400)",
	{ 0 },                // Mac address
	bb_detect,
	bb_init,
	bb_start,
	bb_stop,
	bb_loop,
	bb_tx
};
