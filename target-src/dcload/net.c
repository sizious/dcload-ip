#include <string.h>
#include "commands.h"
#include "packet.h"
#include "adapter.h"
#include "scif.h"
#include "net.h"
#include "dhcp.h"
#include "memfuncs.h"
#include "syscalls.h"

static void process_broadcast(unsigned char *pkt);
static void process_icmp(ether_header_t *ether, ip_header_t *ip, icmp_header_t *icmp);
static void process_udp(ether_header_t *ether, ip_header_t *ip, udp_header_t *udp);
static void process_mine(unsigned char *pkt);

const unsigned char broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

__attribute__((aligned(32))) unsigned char raw_pkt_buf[RAW_TX_PKT_BUF_SIZE];
__attribute__((aligned(2))) unsigned char * pkt_buf = &(raw_pkt_buf[2]);

static void process_broadcast(unsigned char *pkt)
{
	ether_header_t *ether_header = (ether_header_t *)pkt;
	arp_header_t *arp_header = (arp_header_t *)(pkt + ETHER_H_LEN);

	if (UNLIKELY(ether_header->type[1] != 0x06))
		return;

	if (UNLIKELY(arp_header->hw_addr_space != 0x0100))
		return;

	if (UNLIKELY(arp_header->proto_addr_space != 0x0008))
		return;

	if (UNLIKELY(arp_header->opcode != 0x0100))
		return;

	if (UNLIKELY(our_ip == 0))
		return;

	unsigned int ip = htonl(our_ip);
	if (*(unsigned int *)arp_header->proto_target != ip)
		return;

	memcpy_16bit(ether_header->dest, ether_header->src, 3);
	memcpy_16bit(ether_header->src, bb->mac, 3);

	arp_header->opcode = 0x0200;
	memcpy_16bit(arp_header->hw_target, arp_header->hw_sender, 5);

	memcpy_16bit(arp_header->hw_sender, bb->mac, 3);
	*(unsigned int *)arp_header->proto_sender = ip;

	bb->tx(pkt, ETHER_H_LEN + ARP_H_LEN);
}

static void process_icmp(ether_header_t *ether, ip_header_t *ip, icmp_header_t *icmp)
{
	if (icmp->type == 8)
	{
		unsigned short ip_length = ntohs(ip->length);
		unsigned char ip_ihl = ip->version_ihl & 0x0f;
		unsigned short i = icmp->checksum;
		icmp->checksum = 0;
		icmp->checksum = checksum((unsigned short *)icmp, ip_length/2 - 2*ip_ihl, ip_length%2);
		if (i != icmp->checksum)
			return;

		icmp->type = 0;
		memcpy_16bit(ether->dest, ether->src, 6/2);
		memcpy_16bit(ether->src, bb->mac, 6/2);
		ip->dest = ip->src;
		ip->src = htonl(our_ip);

		ip->checksum = 0;
		ip->checksum = checksum((unsigned short *)ip, 2*ip_ihl, 0);
		icmp->checksum = 0;
		icmp->checksum = checksum((unsigned short *)icmp, ip_length/2 - 2*ip_ihl, ip_length%2);

		bb->tx((unsigned char *)ether, ETHER_H_LEN + ip_length);
	}
}

#define CMD_PBIN_ID 0x4e494250
#define CMD_WPAR_ID 0x52415057
#define CMD_WSAK_ID 0x4b415357
#define CMD_DBIN_ID 0x4e494244
#define CMD_RETV_ID 0x56544552
#define CMD_LBIN_ID 0x4e49424c
#define CMD_WLOD_ID 0x444f4c57
#define CMD_WDON_ID 0x4e4f4457
#define CMD_PING_ID 0x474e4950
#define CMD_SBIQ_ID 0x51494253
#define CMD_SBIN_ID 0x4e494253
#define CMD_EXEC_ID 0x43455845
#define CMD_VERS_ID 0x53524556
#define CMD_RBOT_ID 0x544f4252
#define CMD_MAPL_ID 0x4c50414d
#define CMD_PMCR_ID 0x52434d50
#define CMD_CACK_ID 0x4b434143

static void process_udp(ether_header_t *ether, ip_header_t *ip, udp_header_t *udp)
{
	ip_udp_pseudo_header_t *pseudo;
	unsigned short i;
	unsigned short udp_data_length = ntohs(udp->length) - UDP_H_LEN;

	command_t *command = (command_t *)udp->data;
	unsigned int pkt_id = *(unsigned int*)command->id;

	if (__builtin_expect(pkt_id == CMD_PBIN_ID, 1))
	{
		cmd_partbin(command);
		return;
	}

	if (__builtin_expect(pkt_id == CMD_WPAR_ID, 0))
	{
		cmd_wpart((command_window_t *)command);
		return;
	}

	pseudo = (ip_udp_pseudo_header_t *)to_p1(pseudo_array);
	pseudo->src_ip = ip->src;
	pseudo->dest_ip = ip->dest;
	pseudo->zero = 0;
	pseudo->protocol = ip->protocol;
	pseudo->udp_length = udp->length;
	pseudo->src_port = udp->src;
	pseudo->dest_port = udp->dest;
	pseudo->length = udp->length;
	pseudo->checksum = 0;

	if (udp->checksum != 0)
		i = checksum_udp((unsigned short *)pseudo, (unsigned short *)udp->data, udp_data_length/2, udp_data_length%2);
	else
		i = 0;

	if (udp->checksum == 0xffff)
		udp->checksum = 0;

	if (__builtin_expect(i != udp->checksum, 0))
		return;

	dhcp_pkt_t *udp_pkt_data = (dhcp_pkt_t*)udp->data;
	if (__builtin_expect(udp_pkt_data->op == DHCP_OP_BOOTREPLY, 0))
	{
		if (!handle_dhcp_reply(ether->src, udp_pkt_data, udp_data_length))
			escape_loop = 1;
		return;
	}

	make_ether(ether->src, ether->dest, (ether_header_t *)pkt_buf);

	switch (pkt_id)
	{
		case CMD_WSAK_ID: cmd_wsack(ip, udp, command); break;
		case CMD_DBIN_ID: cmd_donebin(ip, udp, command); break;
		case CMD_RETV_ID: cmd_retval(ip, udp, command); break;
		case CMD_LBIN_ID: cmd_loadbin(ip, udp, command); break;
		case CMD_WLOD_ID: cmd_wload(ip, udp, (command_window_t *)command); break;
		case CMD_WDON_ID: cmd_wdone(ip, udp, command); break;
		case CMD_PING_ID: cmd_ping(ip, udp, command); break;
		case CMD_SBIQ_ID: cmd_sendbinq(ip, udp, command); break;
		case CMD_SBIN_ID: cmd_sendbin(ip, udp, command); break;
		case CMD_EXEC_ID: cmd_execute(ether, ip, udp, command); break;
		case CMD_VERS_ID: cmd_version(ip, udp, command); break;
		case CMD_RBOT_ID: cmd_reboot(); break;
		case CMD_MAPL_ID: cmd_maple(ip, udp, command); break;
		case CMD_PMCR_ID: cmd_pmcr(ip, udp, command); break;
		case CMD_CACK_ID: console_handle_ack(ntohl(command->address), ntohl(command->size)); break;
		default: break;
	}
}

static void process_mine(unsigned char *pkt)
{
	ether_header_t *ether_header = (ether_header_t *)pkt;
	ip_header_t *ip_header = (ip_header_t *)(pkt + ETHER_H_LEN);

	if (UNLIKELY(ether_header->type[1] != 0x00))
	{
		if (ether_header->type[1] == 0x06)
			process_broadcast(pkt);
		return;
	}

	unsigned char ip_ihl = ip_header->version_ihl & 0x0f;
	unsigned int hdr_offset = ETHER_H_LEN + (ip_ihl << 2);

	if (LIKELY(ip_header->protocol == IP_UDP_PROTOCOL))
	{
		command_t *cmd = (command_t *)(pkt + hdr_offset + UDP_H_LEN);
		unsigned int cmd_id = *(unsigned int *)cmd->id;
		
		if (LIKELY(cmd_id == CMD_PBIN_ID))
		{
			cmd_partbin(cmd);
			return;
		}
		
		if (cmd_id == CMD_WPAR_ID)
		{
			cmd_wpart((command_window_t *)cmd);
			return;
		}
		
		if (UNLIKELY(ip_header->flags_frag_offset & 0xff3f))
			return;

		unsigned short saved_cksum = ip_header->checksum;
		ip_header->checksum = 0;
		unsigned short computed = checksum((unsigned short *)ip_header, 2*ip_ihl, 0);
		ip_header->checksum = saved_cksum;

		if (UNLIKELY(saved_cksum != computed))
			return;

		process_udp(ether_header, ip_header, (udp_header_t *)(pkt + hdr_offset));
	}
	else if (ip_header->protocol == IP_ICMP_PROTOCOL)
	{
		if (UNLIKELY(ip_header->flags_frag_offset & 0xff3f))
			return;

		unsigned short saved_cksum = ip_header->checksum;
		ip_header->checksum = 0;
		unsigned short computed = checksum((unsigned short *)ip_header, 2*ip_ihl, 0);
		ip_header->checksum = saved_cksum;

		if (UNLIKELY(saved_cksum != computed))
			return;

		process_icmp(ether_header, ip_header, (icmp_header_t *)(pkt + hdr_offset));
	}
}

void process_pkt(unsigned char *pkt)
{
	ether_header_t *ether_header = (ether_header_t *)pkt;

	if (UNLIKELY(bb == (adapter_t *)0))
		return;

	if (UNLIKELY(ether_header->type[0] != 0x08))
		return;

	if (memcmp_eq_6(ether_header->dest, bb->mac))
	{
		process_mine(pkt);
		return;
	}

	if (memcmp_eq_6(ether_header->dest, broadcast))
	{
		process_broadcast(pkt);
		return;
	}
}
