#include "packet.h"
#include "memfuncs.h"

// The two checksums here are different because the UDP one needs a "pseudo-header," while the IP one doesn't
unsigned short checksum(unsigned short *buf, int count, int is_odd)
{
	unsigned long sum = 0;

	while (count--) {
		sum += *buf++;
		if (sum & 0xffff0000) {
			sum &= 0xffff;
			sum++;
		}
	}

	if(is_odd)
	{
		sum += (unsigned short)(*((unsigned char*)buf)); // The sum is a little-endian sum, so an odd byte will be an 8-bit int

		if (sum & 0xffff0000) {
			sum &= 0xffff;
			sum++;
		}
	}

	return ~(sum & 0xffff);
}

// Pass odd as length%2 where datacount is length/2.
unsigned short checksum_udp(unsigned short *buf_pseudo, unsigned short *buf_data, int datacount, int is_odd)
{
	unsigned long sum = 0;
	int pseudocount = PSEUDO_H_LEN/2;

	while (pseudocount--) {
		sum += *buf_pseudo++;
		if (sum & 0xffff0000) {
			sum &= 0xffff;
			sum++;
		}
	}

	while (datacount--) {
		sum += *buf_data++;
		if (sum & 0xffff0000) {
			sum &= 0xffff;
			sum++;
		}
	}

	if(is_odd)
	{
		sum += (unsigned short)(*((unsigned char*)buf_data)); // The sum is a little-endian sum, so an odd byte will be an 8-bit int

		if (sum & 0xffff0000) {
			sum &= 0xffff;
			sum++;
		}
	}

	return ~(sum & 0xffff);
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
	ip_udp_pseudo_header_t * pseudo = (ip_udp_pseudo_header_t*)((unsigned int)pseudo_array & 0x1fffffff);

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
