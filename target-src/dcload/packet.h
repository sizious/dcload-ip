#ifndef __PACKET_H__
#define __PACKET_H__

#include "bswap.h"

typedef struct __attribute__ ((packed, aligned(2))) {
	unsigned char dest[6];
	unsigned char src[6];
	unsigned char type[2];
} ether_header_t;

#define ETHER_H_LEN 14

typedef struct __attribute__ ((packed, aligned(8))) {
	unsigned char version_ihl;
	unsigned char tos;
	unsigned short length;
	unsigned short packet_id;
	unsigned short flags_frag_offset;
	unsigned char ttl;
	unsigned char protocol;
	unsigned short checksum;
	unsigned int src;
	unsigned int dest;
} ip_header_t;

#define IP_H_LEN 20

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned short src;
	unsigned short dest;
	unsigned short length;
	unsigned short checksum;
	unsigned char  data[]; // Make flexible array member
} udp_header_t;

#define UDP_H_LEN 8

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned char type;
	unsigned char code;
	unsigned short checksum;
	unsigned int misc;
} icmp_header_t;

#define ICMP_H_LEN 8

typedef struct __attribute__ ((packed, aligned(8))) {
	unsigned short hw_addr_space;
	unsigned short proto_addr_space;
	unsigned char hw_addr_len;
	unsigned char proto_addr_len;
	unsigned short opcode;
	unsigned char hw_sender[6];
	unsigned char proto_sender[4];
	unsigned char hw_target[6];
	unsigned char proto_target[4];
} arp_header_t;

#define ARP_H_LEN 28

typedef struct __attribute__ ((packed, aligned(4))) {
	unsigned int src_ip;
	unsigned int dest_ip;
	unsigned char zero;
	unsigned char protocol;
	unsigned short udp_length;
	unsigned short src_port;
	unsigned short dest_port;
	unsigned short length;
	unsigned short checksum;
} ip_udp_pseudo_header_t;

#define PSEUDO_H_LEN 20

// For is_odd, pass length%2 where datacount is length/2 (remember: integer divides)
unsigned short checksum(unsigned short *buf, int count, int is_odd);
unsigned short checksum_udp(unsigned short *buf_pseudo, unsigned short *buf_data, int datacount, int is_odd);

void make_ether(unsigned char *dest, unsigned char *src, ether_header_t *ether);
void make_ip(int dest, int src, int length, char protocol, ip_header_t *ip, unsigned short pkt_id);
void make_udp(unsigned short dest, unsigned short src, int length, ip_header_t *ip, udp_header_t *udp);

#define ntohl bswap32
#define htonl bswap32
#define ntohs bswap16
#define htons bswap16

extern __attribute__((aligned(4))) unsigned char pseudo_array[PSEUDO_H_LEN];

#endif
