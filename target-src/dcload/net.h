#ifndef __NET_H__
#define __NET_H__

// Raw transmit buffer array size
// 1514 bytes is not a multiple of 8.
// Ethernet header (14) + ip header (20) + udp header (8) + command struct (12) = 54 bytes before command->data
// So to align the actual command->data to 8 bytes, need to align the array to 8 bytes and offset the start
// by 2 bytes. 1516 - 2 = 1514, and 54 bytes later (56 bytes from the start of the array) is aligned to 8 bytes since 56 = 8 * 7.
// Also: ip header is aligned to 8 bytes, udp/icmp header is aligned to 4 bytes, and command struct (or other payload header) is aligned to 4 bytes
#define RAW_TX_PKT_BUF_SIZE 1516

// Transmit buffer size
#define TX_PKT_BUF_SIZE 1514

// UDP Protocol Identifier
#define IP_UDP_PROTOCOL 17

// ICMP Protocol Identifier
#define IP_ICMP_PROTOCOL 1

// This is the only function that needs to be exported
void process_pkt(unsigned char *pkt);

extern const unsigned char broadcast[6]; // Used in DHCP code

extern unsigned char raw_pkt_buf[RAW_TX_PKT_BUF_SIZE];
extern unsigned char * pkt_buf;

// Defined in commands.c, not net.c
extern volatile unsigned int our_ip;

#endif
