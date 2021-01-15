#ifndef __NET_H__
#define __NET_H__

// Raw transmit buffer array size
// 1514 bytes is not a multiple of 8.
// Ethernet header (14) + ip header (20) + udp header (8) + command struct (12) = 54 bytes before command->data
// So to align the actual command->data to 8 bytes, need to align the array to 8 bytes and offset the start
// by 2 bytes. 1516 - 2 = 1514, and 54 bytes later (56 bytes from the start of the array) is aligned to 8 bytes since 56 = 8 * 7.
// Also: ip header is aligned to 8 bytes, udp/icmp header is aligned to 4 bytes, and command struct (or other payload header) is aligned to 4 bytes
// But SH4_mem_to_pkt() reads 4 bytes beyond the end of a size, so (1514 + 3)/4 becomes 1516/4, which will read 1520 bytes.
// So we end up with 1520 to account for that. Plus 1520 is the nearest multiple of 8 greater than 1514, too.
// And then you throw caching in, with does things in 32-byte blocks, so we need to extend to the nearest multiple of 32 >1514, or 1536,
// otherwise cache operations will spill over onto adjacent data, which can easily corrupt things.
#define RAW_TX_PKT_BUF_SIZE 1536

// Transmit buffer size
#define TX_PKT_BUF_SIZE 1514

// UDP Protocol Identifier
#define IP_UDP_PROTOCOL 17

// ICMP Protocol Identifier
#define IP_ICMP_PROTOCOL 1

// This is the only function that needs to be exported
void process_pkt(unsigned char *pkt);

extern const unsigned char broadcast[6]; // Used in DHCP code

extern __attribute__((aligned(32))) unsigned char raw_pkt_buf[RAW_TX_PKT_BUF_SIZE];
extern __attribute__((aligned(2))) unsigned char * pkt_buf;

// Defined in commands.c, not net.c
extern __attribute__((aligned(4))) volatile unsigned int our_ip;

#endif
