#ifndef __LAN_ADAPTER_H__
#define __LAN_ADAPTER_H__

/* This is based on the JLI EEPROM reader from FreeBSD. EEPROM in the
   Sega adapter is a bit simpler than what is described in the Fujitsu
   manual -- it appears to contain only the MAC address and not a base
   address like the manual says. EEPROM is read one bit (!) at a time
   through the EEPROM interface port. */
#define FE_B16_SELECT	0x20		/* EEPROM chip select */
#define FE_B16_CLOCK	0x40		/* EEPROM shift clock */
#define FE_B17_DATA	0x80		/* EEPROM data bit */

// clear ENA DLC, 100ns SRAM, 8-bit packet transfer bus mode, 4kB TX buffer (2kB per bank x 2 banks), 32kB external buffer memory
#define DLCR6_FLAGS 0x76
// NOTE: The LAN adapter's SRAM, M5M5278DVP-20L, is 32kB.
// Best datasheet I could find: http://www.bitsavers.org/components/mitsubishi/_dataBooks/1996_Mitsubishi_Memories_SRAM.pdf
// Looks like the SRAM access time is actually 20ns, which means we can use the "fast" SRAM time of 100ns instead of the
// "slow" SRAM time of 150ns.
// Also, because 2x2kB = 4kB is used for the TX buffer, 28kB is used for the RX buffer.

// For debugging LAN Adapter driver (may not compile on GCC < 9, and GCC 9 may need
// to use -Os or the binary will be too big)
//#define LAN_ADAPTER_DEBUG

int la_bb_detect(void);
int la_bb_init(void);
void la_bb_start(void);
void la_bb_stop(void);
int la_bb_tx(unsigned char *pkt, int len);
void la_bb_loop(int is_main_loop);

#endif
