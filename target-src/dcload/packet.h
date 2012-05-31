#ifndef __PACKET_H__
#define __PACKET_H__


typedef struct {
	unsigned char dest[6];
	unsigned char src[6];
	unsigned char type[2];
} ether_header_t;

typedef struct __attribute__ ((packed)) {
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

typedef struct __attribute__ ((packed)) {
	unsigned short src;
	unsigned short dest;
	unsigned short length;
	unsigned short checksum;
	unsigned char  data[1];
} udp_header_t;

typedef struct __attribute__ ((packed)) {
	unsigned char type;
	unsigned char code;
	unsigned short checksum;
	unsigned int misc;
} icmp_header_t;

typedef struct __attribute__ ((packed)) {
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

typedef struct __attribute__ ((packed)) {
	unsigned int src_ip;
	unsigned int dest_ip;
	unsigned char zero;
	unsigned char protocol;
	unsigned short udp_length;
	unsigned short src_port;
	unsigned short dest_port;
	unsigned short length;
	unsigned short checksum;
	unsigned char data[1];
} ip_udp_pseudo_header_t;

unsigned short checksum(unsigned short *buf, int count);
void make_ether(char *dest, char *src, ether_header_t *ether);
void make_ip(int dest, int src, int length, char protocol, ip_header_t *ip);
void make_udp(unsigned short dest, unsigned short src, unsigned char * data, int length, ip_header_t *ip, udp_header_t *udp);

#define ntohl bswap32
#define htonl bswap32
#define ntohs bswap16
#define htons bswap16

#define ETHER_H_LEN 14
#define IP_H_LEN    20
#define UDP_H_LEN   8
#define ICMP_H_LEN  8
#define ARP_H_LEN   28

#endif
