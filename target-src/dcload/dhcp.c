// As much as I'd love to put my name up here for 2019 copyright, uh, Internet
// pseudonyms and all that. ;)
//
// So consider the code in dhcp.h & dhcp.c that is not obviously marked
// 'KallistiOS stuff', which is specifically KOS-licensed, to be in the public
// domain. It would always be great to give credit back to the original source!!
//
// Note that dcload-ip is actually GPLv2 licensed, and both public domain and
// KOS-licensed code are compatible with that.
//
// --Moopthehedgehog

//
// Useful references:
//
// Handshake overview: https://ns1.com/resources/dhcp-protocol
// Packet types: https://kikobeats.github.io/server-sandbox/03.%20Services/DHCP/01.%20Introduction.html
// Dreamcast BBA chipset info: https://wiki.osdev.org/RTL8139
// Linux driver for RTL8139: https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/realtek/8139too.c
// Packet Formats (includes DHCPOFFER): https://en.wikipedia.org/wiki/Dynamic_Host_Configuration_Protocol#Discovery
//

#include <string.h>
#include "packet.h"
#include "net.h"
#include "adapter.h"
#include "dhcp.h"
#include "perfctr.h"
#include "memfuncs.h"
#include "dcload.h"

// Need to uniquely identify renewal in build_send_dhcp_packet(),
// so for internal purposes use an invalid DHCP type for that.
#define DHCP_RENEW_TYPE 0
// For a list of all valid DHCP message type values, see:
// https://www.iana.org/assignments/bootp-dhcp-parameters/bootp-dhcp-parameters.xhtml#message-type-53
// Some of them are implemented in dhcp.h by virtue of KOS.

#define DHCP_DEST_PORT 67
#define DHCP_SOURCE_PORT 68

// DHCP Discover & Request packets are 342 bytes
// Old versions of Windows apparently used 576-byte frames, but we ain't old Windows!
#define DHCP_PKT_LEN 300
// ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + DHCP_DISCOVER_LEN = 342 bytes

// Number of times to retry DHCP handshake if get a NAK
// The max theoretical here is 255, but in practice there isn't nearly enough
// stack space to get remotely close to that.
#define DHCP_NAK_NEST_MAX 5

static unsigned int get_some_time(int which);
static void build_send_dhcp_packet(unsigned char kind);
static int kos_net_dhcp_fill_options(unsigned char *bbmac, dhcp_pkt_t *req, uint8 msgtype);
static int kos_net_dhcp_get_message_type(dhcp_pkt_t *pkt, int len);
static uint32 kos_net_dhcp_get_32bit(dhcp_pkt_t *pkt, uint8 opt, int len); // Returns BE networking packet values converted to LE format (well, uint32 format, which is endian-agnostic from C code perspective)

static unsigned int dhcpoffer_server_ip_from_pkt = 0; // LE
static unsigned int dhcpoffer_ip_from_pkt = 0; // LE
static unsigned int dhcpoffer_xid = 0; // BE

volatile unsigned int dhcp_lease_time = 0; // LE
static unsigned int renewal_increment = 0;

static unsigned char dhcp_acked = 0;
static unsigned char dhcp_renewal = 0;
static unsigned char dhcp_renewal_nak = 0;
static unsigned char dhcp_nest_counter = 0;
unsigned char dhcp_nest_counter_maxed = 0;

static unsigned char router_mac[6] = {0}; // BE

static volatile unsigned int time_array[2] = {0};

#define DHCP_TX_PKT_BUF_SIZE TX_PKT_BUF_SIZE
#define dhcp_pkt_buf pkt_buf
// Save 1.5kB of space by reusing an existing packet buffer!

// Small optimization: both TX packets are 342 bytes, round to nearest multiple of 8 bytes for zeroing
// Don't forget tx buffer is offset by 2, so 344, which is actually exactly a multiple of 8. Perfect!
#define DHCP_TX_PKT_BUF_ZEROING_SIZE 344

// Used as a kind-of pseudo-RNG
static unsigned int get_some_time(int which)
{

	if(which == 1)
	{
		PMCR_Read(1, time_array);
	}
	else if(which == 2)
	{
		PMCR_Read(2, time_array);
	}

	// Invalid counter selection returns 0
	// The low 16 bits really don't matter when seconds are the major unit, and
	// shifting by 16 makes the value fit nicely into a 32-bit unsigned int.
	//return (unsigned int)(time_var >> 16);

	// Sane thing as above comment discusses.
	// little endian
	// GCC should use SH4 'xtrct' for this
	return (time_array[1] << 16 | time_array[0] >> 16);
}

// Steps 1 & 3 and part of 5 are handled here, called by dhcp_go() and dhcp_renew()
static void build_send_dhcp_packet(unsigned char kind)
{
	// Keep these local please.
	ether_header_t *dhcp_ether_header = (ether_header_t *)dhcp_pkt_buf;
	ip_header_t *dhcp_ip_header = (ip_header_t *)(dhcp_pkt_buf + ETHER_H_LEN);
	udp_header_t *dhcp_udp_header = (udp_header_t *)(dhcp_pkt_buf + ETHER_H_LEN + IP_H_LEN);

	unsigned char * dhcp_out_pkt = dhcp_pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN;
	unsigned char * dhcp_out_mac = (unsigned char *)broadcast; // So that GCC doesn't warn about discarding 'const'
	unsigned int dhcp_out_ip = 0xffffffff; // 255.255.255.255 -- broadcast IP, doesn't need to be byteswapped :)

	// Make the packet data
	kos_net_dhcp_fill_options(bb->mac, (dhcp_pkt_t*)dhcp_out_pkt, kind);

	if(kind == DHCP_MSG_DHCPDISCOVER)
	{
		bb->start(); // Accept broadcast packets and physical match packets (definitely need these!
		// It's already run by rtl_init in bb_init, but it doesn't do anything to have it here just in case for future DHCP renewal purposes)

		// Useful documentation about RTL8139 from OSDev Wiki (from the link at the top of this file):
		// AB - Accept Broadcast: Accept broadcast packets sent to mac ff:ff:ff:ff:ff:ff
		// APM - Accept Physical Match: Accept packets send to NIC's MAC address.
		//
		// Also, Fun Fact: Write bit 0x01 to the RxConfig register to turn the Dreamcast into a wireshark-like packet sniffer :)
		// Might need to use 0x0f to get all the packet options (and 0x3f to capture error and runt packets). Neat!
	}
	else if(kind == DHCP_MSG_DHCPREQUEST)
	{
		// Don't need to do anything
	}
	else // Assume renewal
	{
		bb->start(); // Accept broadcast packets and physical match packets (definitely need these! It's already run by rtl_init in bb_init, but it doesn't do anything to have it here just in case for future DHCP renewal purposes)

		dhcp_out_mac = router_mac;
		dhcp_out_ip = dhcpoffer_server_ip_from_pkt;
	}

	make_ether(dhcp_out_mac, bb->mac, dhcp_ether_header);
	make_ip(dhcp_out_ip, our_ip, UDP_H_LEN + DHCP_PKT_LEN, IP_UDP_PROTOCOL, dhcp_ip_header, 0); // IP header: dest & src IP addr, header length (hardcoded), packet length excluding ethernet header (IP header size hardcoded), IP protocol type, IP packet ID
	make_udp(DHCP_DEST_PORT, DHCP_SOURCE_PORT, DHCP_PKT_LEN, dhcp_ip_header, dhcp_udp_header);
	bb->tx(dhcp_pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + DHCP_PKT_LEN);
}

// Steps 2 & 4 are handled here, and this is called from net.c
int handle_dhcp_reply(unsigned char *routersrcmac, dhcp_pkt_t* pkt_data, unsigned short len)
{
	int msg_type = kos_net_dhcp_get_message_type(pkt_data, len);

	if(msg_type == DHCP_MSG_DHCPOFFER) // DHCP OFFER is 342 bytes
	{
		// Parse offer message. We only really care about IP address

		dhcpoffer_server_ip_from_pkt = kos_net_dhcp_get_32bit(pkt_data, DHCP_OPTION_SERVER_ID, len); // Need server ip to identify to server that it has been chosen (mandatory)
		dhcpoffer_ip_from_pkt = ntohl(pkt_data->yiaddr); // This is where we get our IP address from
		dhcpoffer_xid = pkt_data->xid; // This transaction ID is needed to identify that this is still all part of the same handshake. Don't byteswap it.

		memcpy(router_mac, routersrcmac, 6); // Save router's mac address for renewal
		// No need to byteswap since this is never used in little endian code

		// Other fields like subnet, etc. will be in here, but don't need them (get them via kos_net_dhcp_get_32bit(), since they're in the options area)
		// Any options acquired in the dhcp_pkt_t need to be "ntohl()"-ified to be usable as standard LE data
		// Bear in mind that actually capturing a DHCP Offer packet is not a super easy thing to do without using a mirrored port on a smart or managed ethernet switch

		return 0; // success
	}
	else if(msg_type == DHCP_MSG_DHCPACK) // DHCP ACK is 342 bytes
	{
		// Verify that IP address matches and we're all done with this handshake!
		if( (pkt_data->xid == dhcpoffer_xid) && ((dhcp_renewal == 1) || (ntohl(pkt_data->yiaddr) == dhcpoffer_ip_from_pkt)) )
		{
			//
			// Ideally there would be an ARP done here to ensure that the provided IP address is not already taken
			// This matters more for large networks, or if there is a risk of a wrongly-configured static-ip client
			// that for some reason has an IP in the DHCP range. A cause of this could also be if DHCP reservations
			// aren't working right on the network, etc.
			//

			if(dhcp_renewal)
			{
				// Refresh IP with result from renewal
				dhcpoffer_ip_from_pkt = ntohl(pkt_data->yiaddr); // This is where we get our IP address from for renewal
			}

			// Get the lease time from ACK
			// dhcp_lease_time is usable in little endian
			dhcp_lease_time = kos_net_dhcp_get_32bit(pkt_data, DHCP_OPTION_IP_LEASE_TIME, len);
			// Set PMCR for calculating renewal
			// Reset 48-bit perfcounter to 0; renewal is calculated by elapsed time
			// BUS_RATIO_COUNTER is set in dcload.h
#ifndef BUS_RATIO_COUNTER
			PMCR_Restart(DCLOAD_PMCR, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
#else
			PMCR_Restart(DCLOAD_PMCR, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_RATIO_CYCLES);
#endif
			dhcp_acked = 1;

			return 0; // success
		}

		// Else, uh, well, that's not good. dhcp_acked stays 0.
		// Should probably run the discover process again here...
		// Or it means the router's screwing up.
	}
	else if(msg_type == DHCP_MSG_DHCPNAK)
	{
		// This might happen
		// If it does, try DHCP discovery again...

		if(dhcp_renewal == 1) // If we got here during renewal process, it means the IP is no longer valid.
		{
			// It's not an error, but it means we need to go through dhcp discovery again.
			// Exiting gracefully here allows that to happen without killing stack usage due to recursion.
			// dhcp_acked will be 0, which will cause dhcp_renew to return -2 to dcload.c's set_ip_dhcp(),
			// and that will start the discovery process.
			dhcp_renewal_nak = 1;
			return 0;
		}

		if(dhcp_nest_counter == DHCP_NAK_NEST_MAX) // Is this the 'max'th time we've been here?
		{
			// Bail, this is crazy. No IP address. This will disable DHCP mode since we didn't get a lease time.
			dhcp_nest_counter_maxed = 1;
			dhcp_nest_counter = 0;
			return 0;
		}

		int dhcp_result = dhcp_go((unsigned int*)&our_ip); // So that GCC doesn't warn about volatile
		if(!dhcp_result) // If the router responds with a lot of NAKs, well, hope there's enough stack space for all the nested functions...
		{
			return 0; // success
		}
		// Or don't do anything and get stuck with no IP address.
		// This means that the IP address in the request message is not or is no longer valid
	}

	return -1; // Something's up
}

// DHCP process:
//
// Step 1: DHCP DISCOVER packet
// STEP 2: Wait for DHCP OFFER packet from router
// STEP 3: DHCP REQUEST packet
// STEP 4: Wait for DHCP ACK from router
// STEP 5+: DHCP renewal

int dhcp_go(unsigned int *dhcp_ip_address_buffer) // Address buffer comes in as little endian
{
	dhcp_acked = 0;
	dhcp_nest_counter++;

 	build_send_dhcp_packet(DHCP_MSG_DHCPDISCOVER);
	bb->loop(0); // Wait for DHCP OFFER packet
 	build_send_dhcp_packet(DHCP_MSG_DHCPREQUEST);
	bb->loop(0); // Wait for DHCP ACK (or NAK...)

	if(dhcp_acked && dhcp_nest_counter)
	{
		dhcp_nest_counter = 0; // To ensure we only write the IP address once (part 1/2)

		*dhcp_ip_address_buffer = dhcpoffer_ip_from_pkt;
		// And now we have an IP from DHCP. YES!!
		return 0;
	}
	else if(dhcp_acked && (!dhcp_nest_counter)) // To ensure we only write the IP address once (part 2/2)
	{
		return 0;
	}
	else if(dhcp_nest_counter_maxed)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

int dhcp_renew(unsigned int *dhcp_ip_address_buffer)
{
	dhcp_acked = 0;
	dhcp_renewal = 1;
	dhcp_renewal_nak = 0;

	build_send_dhcp_packet(DHCP_RENEW_TYPE);
	bb->loop(0); // Wait for DHCP ACK

	dhcp_renewal = 0; // Done with the renewal function, whatever the outcome

	if(dhcp_renewal_nak)
	{
		return -2;
	}
	else if(dhcp_acked)
	{
		*dhcp_ip_address_buffer = dhcpoffer_ip_from_pkt;
		// And now we have an IP from DHCP. YES!!
		return 0;
	}
	else
	{
		return -1;
	}
}

// The following functions are adapted from KOS, so they need to be licensed properly

//==============================================================================
// START KOS STUFF
//==============================================================================

/* KallistiOS 2.1.0

   kernel/net/net_dhcp.c
   Copyright (C) 2008, 2009, 2013 Lawrence Sebald

	 Redistribution and use in source and binary forms, with or without
	 modification, are permitted provided that the following conditions
	 are met:
	 1. Redistributions of source code must retain the above copyright
	    notice, this list of conditions and the following disclaimer.
	 2. Redistributions in binary form must reproduce the above copyright
	    notice, this list of conditions and the following disclaimer in the
	    documentation and/or other materials provided with the distribution.
	 3. Neither the name of Cryptic Allusion nor the names of its contributors
	    may be used to endorse or promote products derived from this software
	    without specific prior written permission.

	 THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
	 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	 ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
	 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
	 OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
	 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
	 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
	 OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
	 SUCH DAMAGE.
*/

// For serverid and reqip, this does htonl() on them
// Heavily modified from KOS
static int kos_net_dhcp_fill_options(unsigned char *bbmac, dhcp_pkt_t *req, uint8 msgtype)
{
		// Zero out the TX packet buffer, that way we don't need to explicitly set zeros later
		memset_zeroes_64bit(raw_pkt_buf, DHCP_TX_PKT_BUF_ZEROING_SIZE/8);

		uint32 serverid = 0, reqip = 0;
    int pos = 0;

		// Fill in common parameters
		req->op = DHCP_OP_BOOTREQUEST;
		req->htype = DHCP_HTYPE_10MB_ETHERNET;
		req->hlen = DHCP_HLEN_ETHERNET;

		if(msgtype == DHCP_MSG_DHCPDISCOVER)
		{
			/* Fill in the initial DHCPDISCOVER packet */
	    //req->hops = 0;
	    req->xid = htonl(get_some_time(DCLOAD_PMCR) ^ 0xDEADBEEF);
	    //req->secs = 0;
	    //req->flags = 0; // 0x0000 want unicast response, 0x8000 want broadcast response
	    //req->ciaddr = 0;
	    //req->yiaddr = 0;
	    //req->siaddr = 0;
	    //req->giaddr = 0;
		}
		else if(msgtype == DHCP_MSG_DHCPREQUEST)
		{
			/* Fill in the DHCP request */
			//req->hops = 0;
			req->xid = dhcpoffer_xid; // Should match xid from DHCP DISCOVER
			//req->secs = 0;
			//req->flags = 0; // 0x0000 want unicast response, 0x8000 want broadcast response
			//req->ciaddr = 0;
			//req->yiaddr = 0;
			//req->siaddr = 0;
			//req->giaddr = 0;

			serverid = dhcpoffer_server_ip_from_pkt;
			reqip = dhcpoffer_ip_from_pkt;
		}
		else // Assume renewal (msgtype == 0)
		{
			dhcpoffer_xid = (get_some_time(DCLOAD_PMCR) ^ 0xDEADBEEF) + renewal_increment; // It's a DHCP Request with a unique transaction ID
			renewal_increment += 0x1000; // This just ensures DHCP renewal transaction IDs can't be the same within the uptime of the machine.
			/* Fill in the DHCP renewal request */
			//req->hops = 0;
			req->xid = dhcpoffer_xid;
			//req->secs = 0;
			//req->flags = 0; // 0x0000 want unicast response, 0x8000 want broadcast response
			req->ciaddr = htonl(our_ip); // This is also filled out with the IP to renew, not just the option field;
			//req->yiaddr = 0;
			//req->siaddr = 0;
			//req->giaddr = 0;

			reqip = our_ip;
		}

		memcpy(req->chaddr, bbmac, DHCP_HLEN_ETHERNET);
    //memset(req->chaddr + DHCP_HLEN_ETHERNET, 0, sizeof(req->chaddr) - DHCP_HLEN_ETHERNET);
    //memset(req->sname, 0, sizeof(req->sname));
    //memset(req->file, 0, sizeof(req->file));

		/* DHCP Magic Cookie */
    req->options[pos++] = 0x63;
    req->options[pos++] = 0x82;
    req->options[pos++] = 0x53;
    req->options[pos++] = 0x63;

    /* Message Type: DHCPDISCOVER or DHCPREQUEST */
    req->options[pos++] = DHCP_OPTION_MESSAGE_TYPE;
    req->options[pos++] = 1; /* Length = 1 */
		if(!msgtype) // msgtype of 0 means DHCP renewal to this fill function
		{
			req->options[pos++] = DHCP_MSG_DHCPREQUEST;
		}
		else // DHCP DISCOVER or DHCP REQUEST
		{
			req->options[pos++] = msgtype;
		}


    /* Max Message Length: 1500 octets */
    req->options[pos++] = DHCP_OPTION_MAX_MESSAGE;
    req->options[pos++] = 2; /* Length = 2 */
    req->options[pos++] = (1500 >> 8) & 0xFF;
    req->options[pos++] = (1500 >> 0) & 0xFF;

    /* Host Name: dcload-ip */
    req->options[pos++] = DHCP_OPTION_HOST_NAME;
    req->options[pos++] = 9; /* Length = 9 */
    memcpy((char *)req->options + pos, "dcload-ip", 9);
    pos += 9;

    /* Client Identifier: The network adapter's MAC address */
    req->options[pos++] = DHCP_OPTION_CLIENT_ID;
    req->options[pos++] = 1 + DHCP_HLEN_ETHERNET; /* Length = 7 */
    req->options[pos++] = DHCP_HTYPE_10MB_ETHERNET;
    memcpy(req->options + pos, bbmac, DHCP_HLEN_ETHERNET);
    pos += DHCP_HLEN_ETHERNET;

    /* Parameters requested: Subnet, Router, DNS, Broadcast, MTU */
    req->options[pos++] = DHCP_OPTION_PARAMETER_REQUEST;
    req->options[pos++] = 5; /* Length = 5 */
    req->options[pos++] = DHCP_OPTION_SUBNET_MASK;
    req->options[pos++] = DHCP_OPTION_ROUTER;
    req->options[pos++] = DHCP_OPTION_DOMAIN_NAME_SERVER;
    req->options[pos++] = DHCP_OPTION_BROADCAST_ADDR;
    req->options[pos++] = DHCP_OPTION_INTERFACE_MTU;

    if(serverid) {
        /* Add the Server identifier option */
        req->options[pos++] = DHCP_OPTION_SERVER_ID;
        req->options[pos++] = 4; /* Length = 4 */
        req->options[pos++] = (serverid >> 24) & 0xFF;
        req->options[pos++] = (serverid >> 16) & 0xFF;
        req->options[pos++] = (serverid >>  8) & 0xFF;
        req->options[pos++] = (serverid >>  0) & 0xFF;
    }

    if(reqip) {
        /* Add the requested IP address option */
        req->options[pos++] = DHCP_OPTION_REQ_IP_ADDR;
        req->options[pos++] = 4; /* Length = 4 */
        req->options[pos++] = (reqip >> 24) & 0xFF;
        req->options[pos++] = (reqip >> 16) & 0xFF;
        req->options[pos++] = (reqip >>  8) & 0xFF;
        req->options[pos++] = (reqip >>  0) & 0xFF;
    }

    /* The End */
    req->options[pos++] = DHCP_OPTION_END;

    return pos;
}

// Modified very slightly from KOS
static int kos_net_dhcp_get_message_type(dhcp_pkt_t *pkt, int len)
{
    int i;

    len -= DHCP_H_LEN;

    /* Read each byte of the options field looking for the message type option.
       when we find it, return the message type. */
    for(i = 4; i < len;) {
        if(pkt->options[i] == DHCP_OPTION_MESSAGE_TYPE) {
            return pkt->options[i + 2];
        }
        else if((pkt->options[i] == DHCP_OPTION_PAD) ||
                (pkt->options[i] == DHCP_OPTION_END)) {
            ++i;
        }
        else {
            i += pkt->options[i + 1] + 2;
        }
    }

    return -1;
}

static uint32 kos_net_dhcp_get_32bit(dhcp_pkt_t *pkt, uint8 opt, int len)
{
    int i;

    len -= DHCP_H_LEN;

    /* Read each byte of the options field looking for the specified option,
       return it when found. */
		// This is doing the BE->LE byteswap already in an endian-agnostic way.
    for(i = 4; i < len;) {
        if(pkt->options[i] == opt) {
            return (pkt->options[i + 2] << 24) | (pkt->options[i + 3] << 16) |
                   (pkt->options[i + 4] << 8) | (pkt->options[i + 5]);
        }
        else if(pkt->options[i] == DHCP_OPTION_PAD) {
            ++i;
        }
        else if(pkt->options[i] == DHCP_OPTION_END) {
            break;
        }
        else {
            i += pkt->options[i + 1] + 2;
        }
    }

    return 0;
}

//==============================================================================
// END KOS STUFF
//==============================================================================
