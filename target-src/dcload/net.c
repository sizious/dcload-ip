#include <string.h>
#include "commands.h"
#include "packet.h"
#include "adapter.h"
#include "scif.h"
#include "net.h"
#include "dhcp.h"
#include "memfuncs.h"

static void process_broadcast(unsigned char *pkt);
static void process_icmp(ether_header_t *ether, ip_header_t *ip, icmp_header_t *icmp);
static void process_udp(ether_header_t *ether, ip_header_t *ip, udp_header_t *udp);
static void process_mine(unsigned char *pkt);

const unsigned char broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

// Packet transmit buffer
__attribute__((aligned(32))) unsigned char raw_pkt_buf[RAW_TX_PKT_BUF_SIZE]; // Here's a global array. Global packet transmit buffer.
// Need to offset the packet by 2 for the command->data after headers to always be aligned to 8 bytes
// The performance gains are well worth the 2 wasted bytes.
__attribute__((aligned(2))) unsigned char * pkt_buf = &(raw_pkt_buf[2]);

static void process_broadcast(unsigned char *pkt) // arp request
{
	ether_header_t *ether_header = (ether_header_t *)pkt;
	arp_header_t *arp_header = (arp_header_t *)(pkt + ETHER_H_LEN);

	if (ether_header->type[1] != 0x06) /* ARP */
		return;

	/* hardware address space = ethernet */
	if (arp_header->hw_addr_space != 0x0100)
		return;

	/* protocol address space = IP */
	if (arp_header->proto_addr_space != 0x0008)
		return;

	if (arp_header->opcode == 0x0100) /* arp request */
	{
		if (our_ip == 0) /* return if we don't know our ip */
			return;

		// NOTE: using the 16-bit memcmp here is faster than the 32-bit one.
		// This is because by the time arp_header->proto_target is manually aligned
		// to 4 bytes, memcmp_16bit_eq would already be done the comparison operation.
//		__attribute__((aligned(2))) unsigned int ip = htonl(our_ip);
//		if (!memcmp_16bit_eq(arp_header->proto_target, &ip, 4/2)) /* for us */

		// Well, with the shift-by-2 trick this field is aligned to 4 bytes now.
		__attribute__((aligned(4))) unsigned int ip = htonl(our_ip);
		if (!memcmp_32bit_eq(arp_header->proto_target, &ip, 4/4)) /* for us */
		{
			/* put src hw address into dest hw address */
			memcpy_16bit(ether_header->dest, ether_header->src, 6/2);
			/* put our hw address into src hw address */
			memcpy_16bit(ether_header->src, bb->mac, 6/2);

			/* arp reply (in already byte-swapped format) */
			arp_header->opcode = 0x0200;
			/* swap sender and target addresses */
			// Copy both mac and ip of remote computer from sender to target fields in one go
			memcpy_16bit(arp_header->hw_target, arp_header->hw_sender, 10/2);

			/* put our hw address into sender hw address */
			memcpy_16bit(arp_header->hw_sender, bb->mac, 6/2);
			// put our ip into sender ip (note: this is 16-bit aligned even with shift-by-2 trick)
			memcpy_16bit(arp_header->proto_sender, &ip, 4/2);

			/* transmit */
			bb->tx(pkt, ETHER_H_LEN + ARP_H_LEN);
		}
	}
}

static void process_icmp(ether_header_t *ether, ip_header_t *ip, icmp_header_t *icmp)
{
	if (icmp->type == 8) /* echo request */
	{
		// dcload-ip only supports echo (ping), so the verification checksum can be
		// computed here. This way only ping packets are checked while others are
		// discarded without wasting time on them. This step is necessary to ensure
		// malformed packets aren't sent across the network.
		/* check icmp checksum */
		unsigned short ip_length = ntohs(ip->length);
		unsigned char ip_ihl = ip->version_ihl & 0x0f;
		unsigned short i = icmp->checksum;
		icmp->checksum = 0;
		icmp->checksum = checksum((unsigned short *)icmp, ip_length/2 - 2*ip_ihl, ip_length%2);
		if (i != icmp->checksum)
			return;

		// Now make and send the reply
		// Just use the receive buffer since ping is an echo
		/* set echo reply */
		icmp->type = 0;
		/* swap src and dest hw addresses */
		memcpy_16bit(ether->dest, ether->src, 6/2);
		memcpy_16bit(ether->src, bb->mac, 6/2);
		/* swap src and dest ip addresses */
//		__attribute__((aligned(4))) unsigned int ip_buf = htonl(our_ip);
//		memcpy_16bit(&ip->dest, &ip->src, 4/2);
//		memcpy_16bit(&ip->src, &ip_buf, 4/2);
		// ip header is aligned to 4 bytes now with the shift-by-2 trick
		// So at long last we can just do this. Finally.
		ip->dest = ip->src;
		ip->src = htonl(our_ip);

		/* recompute ip header checksum */
		ip->checksum = 0;
		ip->checksum = checksum((unsigned short *)ip, 2*ip_ihl, 0);
		/* recompute icmp checksum */
		icmp->checksum = 0;
		icmp->checksum = checksum((unsigned short *)icmp, ip_length/2 - 2*ip_ihl, ip_length%2);

		/* transmit */
		bb->tx((unsigned char *)ether, ETHER_H_LEN + ip_length);
	}
}

static void process_udp(ether_header_t *ether, ip_header_t *ip, udp_header_t *udp)
{
	ip_udp_pseudo_header_t *pseudo;
	unsigned short i;
	// Note that UDP's length field actually includes the UDP header, which is UDP_H_LEN
	unsigned short udp_data_length = ntohs(udp->length) - UDP_H_LEN;

	pseudo = (ip_udp_pseudo_header_t *)((unsigned int)pseudo_array & 0x1fffffff); // global small pseudo header array
	pseudo->src_ip = ip->src;
	pseudo->dest_ip = ip->dest;
	pseudo->zero = 0;
	pseudo->protocol = ip->protocol;
	pseudo->udp_length = udp->length;
	pseudo->src_port = udp->src;
	pseudo->dest_port = udp->dest;
	pseudo->length = udp->length;
	pseudo->checksum = 0;

	/* checksum == 0 means no checksum */
	if (udp->checksum != 0)
		i = checksum_udp((unsigned short *)pseudo, (unsigned short *)udp->data, udp_data_length/2, udp_data_length%2); // integer divide; need to round up to next even number
	else
		i = 0;
	/* checksum == 0xffff means checksum was really 0 */
	if (udp->checksum == 0xffff)
		udp->checksum = 0;

	if (__builtin_expect(i != udp->checksum, 0))
	{
		/*    scif_puts("UDP CHECKSUM BAD\n"); */
		return;
	}

	// Handle receipt of DHCP packets that are directed to this system
	dhcp_pkt_t *udp_pkt_data = (dhcp_pkt_t*)udp->data;
	if(__builtin_expect(udp_pkt_data->op == DHCP_OP_BOOTREPLY, 0)) // DHCP ACK or DHCP OFFER
	{
		if(!handle_dhcp_reply(ether->src, udp_pkt_data, udp_data_length)) // -8 because udp->length includes 8-byte udp header
		{ // -1 is true in C
			// If we got a DHCP packet that belongs to some other machine, e.g. some machine requires a broadcasted address instead of a unicasted one,
			// don't escape the loop since we didn't get the packet we needed.
			escape_loop = 1;
		}
	}
	else
	{
		command_t *command = (command_t *)udp->data;

		// Only one of these will ever match at a time. What we can do is set this variable to 0 after compare succeeds.
		// There is no id of 0, as they're all 4-character, non-null-terminated strings.
		// Unfortunately packet headers are 42 bytes, which is NOT a multiple of 4. It is a multiple of 2, though, so we can do this without crashing:
//		__attribute__((aligned(4))) unsigned int pkt_match_id = ((unsigned int) *(unsigned short*)&command->id[2] << 16) | (unsigned int) *(unsigned short*)command->id;

		// We can do this now that the receive packet buffer has been aligned.
		// All command structs are now aligned on a 4-byte boundary thanks to the shift-by-2 trick
		unsigned int pkt_match_id = *(unsigned int*)command->id;

		// This one is the most likely to be called the most often, so put it first and tell GCC it's likely to be called
		if (__builtin_expect((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_PARTBIN, 4/4)), 1))
		{
			// Handle legacy packets and v2.0.0+ packets <= 1460 bytes
			cmd_partbin(command);
			pkt_match_id = 0;
		}

		// Make ethernet header in transmit packet buffer since all below functions have a response packet
		// (except reboot)
		make_ether(ether->src, ether->dest, (ether_header_t *)pkt_buf);

		// Next likely to be called most often (e.g. during maple <--> PC comms)
		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_MAPLE, 4/4)))
		{
			cmd_maple(ip, udp, command);
			pkt_match_id = 0;
		}

		// Next likely to be called most often (e.g. using PC to do perf counting)
		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_PMCR, 4/4)))
		{
			cmd_pmcr(ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_DONEBIN, 4/4)))
		{
			cmd_donebin(ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_RETVAL, 4/4)))
		{
			cmd_retval(ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_LOADBIN, 4/4)))
		{
			cmd_loadbin(ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_SENDBINQ, 4/4)))
		{
			cmd_sendbinq(ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_SENDBIN, 4/4)))
		{
			cmd_sendbin(ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_EXECUTE, 4/4)))
		{
			cmd_execute(ether, ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_VERSION, 4/4)))
		{
			cmd_version(ip, udp, command);
			pkt_match_id = 0;
		}

		if ((pkt_match_id) && (!memcmp_32bit_eq(&pkt_match_id, CMD_REBOOT, 4/4)))
		{
			// This function does not return
			cmd_reboot();
		}
	}
}

static void process_mine(unsigned char *pkt)
{
	ether_header_t *ether_header = (ether_header_t *)pkt;
	ip_header_t *ip_header = (ip_header_t *)(pkt + ETHER_H_LEN);
	icmp_header_t *icmp_header;
	udp_header_t *udp_header;

	if(__builtin_expect(ether_header->type[1] != 0x00, 0))
	{
		// Sometimes we get a directed ARP packet, like in the case of pings.
		// We can respond to those.
		if(ether_header->type[1] == 0x06)
		{
			process_broadcast(pkt);
		}
		return;
	}

	/* ignore fragmented packets */

	if(__builtin_expect(ip_header->flags_frag_offset & 0xff3f, 0))
		return;

	unsigned char ip_ihl = ip_header->version_ihl & 0x0f;

	/* check ip header checksum */
	unsigned short i = ip_header->checksum;
	ip_header->checksum = 0;
	ip_header->checksum = checksum((unsigned short *)ip_header, 2*ip_ihl, 0); // 2*ip_ihl because unsigned shorts
	if (i != ip_header->checksum)
		return;

	if(__builtin_expect(ip_header->protocol == IP_UDP_PROTOCOL, 1))
	{
		/* udp */
		udp_header = (udp_header_t *)(pkt + ETHER_H_LEN + 4*ip_ihl);
		process_udp(ether_header, ip_header, udp_header);
	}
	else if(__builtin_expect(ip_header->protocol == IP_ICMP_PROTOCOL, 0))
	{
		/* icmp */
		icmp_header = (icmp_header_t *)(pkt + ETHER_H_LEN + 4*ip_ihl);
		process_icmp(ether_header, ip_header, icmp_header);
	}
}

void process_pkt(unsigned char *pkt)
{
	ether_header_t *ether_header = (ether_header_t *)pkt;

	if (ether_header->type[0] != 0x08)
		return;

	// Destination ethernet header is the first thing in the packet, so it's always aligned to 2 bytes
	if (!memcmp_16bit_eq(ether_header->dest, bb->mac, 6/2))
	{
		process_mine(pkt);
		return;
	}

	if (!memcmp_16bit_eq(ether_header->dest, broadcast, 6/2))
	{
		process_broadcast(pkt);
		return;
	}
}
