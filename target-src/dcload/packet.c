//#include <string.h>
#include "packet.h"
#include "memfuncs.h"

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
	int pseudocount = sizeof(ip_udp_pseudo_header_t)/2;

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

void make_ip(int dest, int src, int length, char protocol, ip_header_t *ip)
{
	ip->version_ihl = 0x45;
	ip->tos = 0;
	ip->length = htons(20 + length);
	ip->packet_id = 0;
	ip->flags_frag_offset = htons(0x4000);
	ip->ttl = 0x40;
	ip->protocol = protocol;
	ip->checksum = 0;
	ip->src = htonl(src);
	ip->dest = htonl(dest);

	ip->checksum = checksum((unsigned short *)ip, sizeof(ip_header_t)/2, 0);
}

// sizeof(ip_udp_pseudo_header_t) is 20
static unsigned char pseudo_array[sizeof(ip_udp_pseudo_header_t)]; // Here's a global array (not really global, but... search terms)

// UDP packet length should always be an even number. It's the length of the UDP payload data specified by the 'data' variable.
void make_udp(unsigned short dest, unsigned short src, unsigned char * data, int length, ip_header_t *ip, udp_header_t *udp, int data_already_in_packet)
{
	ip_udp_pseudo_header_t * pseudo = (ip_udp_pseudo_header_t*)pseudo_array;

	udp->src = htons(src);
	udp->dest = htons(dest);
	udp->length = htons(length + 8);
	udp->checksum = 0;
	// This only happens when needing to copy data from rx to tx (loadbin, donebin, retval, execute)
	if(!data_already_in_packet) // TODO do we even need two packet buffers? dcload only ever works on one packet at a time... except maybe maple?
	{
//		memcpy(udp->data, data, length);
		// Command struct and udp header are both aligned to 4 bytes, and are each more than 4 bytes.
		// So memcpy_32bit can be used to copy 4 bytes over, which will align to 8 bytes, and then the heavy-duty copy can occur.
		// udp->data and data are already of type (unsigned char*)
		memcpy_32bit(udp->data, data, 4/4);
		SH4_aligned_memcpy(udp->data + 4, data + 4, length - 4);
	}

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
