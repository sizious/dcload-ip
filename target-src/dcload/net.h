#ifndef __NET_H__
#define __NET_H__

// Transmit buffer size
#define TX_PKT_BUF_SIZE 1514

// UDP Protocol Identifier
#define IP_UDP_PROTOCOL 17

// ICMP Protocol Identifier
#define IP_ICMP_PROTOCOL 1

// This is the only function that needs to be exported
void process_pkt(unsigned char *pkt);

extern const unsigned char broadcast[6]; // Used in DHCP code

extern unsigned char pkt_buf[TX_PKT_BUF_SIZE];

// Defined in commands.c, not net.c
extern volatile unsigned int our_ip;

#endif
