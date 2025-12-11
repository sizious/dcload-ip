#include "packet.h"
#include "memfuncs.h"

// IP checksum with 8-word loop unrolling and deferred carry folding
unsigned short checksum(unsigned short *buf, int count, int is_odd)
{
	register unsigned long sum = 0;
	register unsigned long t;

	// Process 8 words at a time for better pipeline utilization
	while (count >= 8) {
		t = buf[0]; sum += t;
		t = buf[1]; sum += t;
		t = buf[2]; sum += t;
		t = buf[3]; sum += t;
		t = buf[4]; sum += t;
		t = buf[5]; sum += t;
		t = buf[6]; sum += t;
		t = buf[7]; sum += t;
		buf += 8;
		count -= 8;
	}

	// Handle remaining words
	while (count--)
		sum += *buf++;

	// Handle odd byte
	if (is_odd)
		sum += (unsigned short)(*((unsigned char*)buf));

	// Fold 32-bit sum to 16 bits
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

// UDP checksum with unrolled pseudo-header and data loops
unsigned short checksum_udp(unsigned short *buf_pseudo, unsigned short *buf_data, int datacount, int is_odd)
{
	register unsigned long sum = 0;
	register unsigned long t;

	// Unroll pseudo-header (always 10 words = 20 bytes)
	sum += buf_pseudo[0];
	sum += buf_pseudo[1];
	sum += buf_pseudo[2];
	sum += buf_pseudo[3];
	sum += buf_pseudo[4];
	sum += buf_pseudo[5];
	sum += buf_pseudo[6];
	sum += buf_pseudo[7];
	sum += buf_pseudo[8];
	sum += buf_pseudo[9];

	// Process data 8 words at a time
	while (datacount >= 8) {
		t = buf_data[0]; sum += t;
		t = buf_data[1]; sum += t;
		t = buf_data[2]; sum += t;
		t = buf_data[3]; sum += t;
		t = buf_data[4]; sum += t;
		t = buf_data[5]; sum += t;
		t = buf_data[6]; sum += t;
		t = buf_data[7]; sum += t;
		buf_data += 8;
		datacount -= 8;
	}

	// Handle remaining words
	while (datacount--)
		sum += *buf_data++;

	// Handle odd byte
	if (is_odd)
		sum += (unsigned short)(*((unsigned char*)buf_data));

	// Fold 32-bit sum to 16 bits
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

void make_ether(unsigned char *dest, unsigned char *src, ether_header_t *ether)
{
	memcpy_16bit(ether->dest, dest, 6/2);
	memcpy_16bit(ether->src, src, 6/2);
	ether->type[0] = 8;
	ether->type[1] = 0;
}

void make_ip(int dest, int src, int length, char protocol, ip_header_t *ip, unsigned short pkt_id)
{
	ip->version_ihl = 0x45;
	ip->tos = 0;
	ip->length = htons(20 + length);
	ip->packet_id = pkt_id;
	ip->flags_frag_offset = htons(0x4000);
	ip->ttl = 64; // 0x40 is a hop count of 64...
	ip->protocol = protocol;
	ip->checksum = 0;
	ip->src = htonl(src);
	ip->dest = htonl(dest);

	ip->checksum = checksum((unsigned short *)ip, IP_H_LEN/2, 0);
}

__attribute__((aligned(4))) unsigned char pseudo_array[PSEUDO_H_LEN]; // Here's a global array (not really global, but... search terms)

// UDP packet length should always be an even number. It's the length of the UDP payload data specified by the 'data' variable.
void make_udp(unsigned short dest, unsigned short src, int length, ip_header_t *ip, udp_header_t *udp)
{
	ip_udp_pseudo_header_t * pseudo = (ip_udp_pseudo_header_t*)pseudo_array;

	udp->src = htons(src);
	udp->dest = htons(dest);
	udp->length = htons(length + UDP_H_LEN);
	udp->checksum = 0;

	pseudo->src_ip = ip->src;
	pseudo->dest_ip = ip->dest;
	pseudo->zero = 0;
	pseudo->protocol = ip->protocol;
	pseudo->udp_length = udp->length;
	pseudo->src_port = udp->src;
	pseudo->dest_port = udp->dest;
	pseudo->length = udp->length;
	pseudo->checksum = 0;

	udp->checksum = checksum_udp((unsigned short *)pseudo, (unsigned short *)udp->data, length/2, length%2);
	if (udp->checksum == 0)
		udp->checksum = 0xffff;
}
