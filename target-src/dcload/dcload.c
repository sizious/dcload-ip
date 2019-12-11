/*
 * dcload, a Dreamcast ethernet loader
 *
 * Copyright (C) 2001 Andrew Kieschnick <andrewk@austin.rr.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* uncomment following line to enable crappy screensaver (it just blanks) */
/* #define SCREENSAVER */

#include "dcload.h"
#include "scif.h"
#include "video.h"
#include "packet.h"
#include "commands.h"
#include "adapter.h"
#include "net.h"
#include "cdfs.h"
#include "maple.h"

#include "dhcp.h"
#include "perfctr.h"

// Modify the branding slightly to prevent confusing it with stock DCLoad-IP
#define NAME "dcload-ip " DCLOAD_VERSION " - with DHCP"

#define VIDMODEREG (volatile unsigned int *)0xa05f8044
#define VIDBORDER (volatile unsigned int *)0xa05f8040

// Virtua Fighter 3 uses a counter in elapsed time mode for something
// See https://dcemulation.org/phpBB/viewtopic.php?f=29&t=104406

// Don't touch these.
#ifndef FAST_COUNTER
// This would run for 16.3 days (1407374 seconds) if true
#define PERFCOUNTER_SCALE SH4_FREQUENCY
#else
// But it seems to not be the case for some reason
// Experimentally derived perf counter division value, see:
// https://dcemulation.org/phpBB/viewtopic.php?p=1057114#p1057114
// This would run for 1 day 8 hours and 40 minutes (117576 seconds) if true
#define PERFCOUNTER_SCALE 2393976245
#endif

// Scale up the onscreen refresh interval
#define ONSCREEN_REFRESH_SCALED ((unsigned long long int)ONSCREEN_DHCP_LEASE_TIME_REFRESH_INTERVAL * (unsigned long long int)PERFCOUNTER_SCALE)

// Volatile informs GCC not to cache the variable in a register.
// Most descriptions of volatile are that the data might be modified by an
// interrupt, is in a hardware register, or may be modified by processes outside
// this code (e.g. think a memory location accessed by different threads on
// multiple cores), so don't do any fancy optimizations with it. Internally,
// that means "don't optimize this variable that lives in memory by caching it
// in a register for later access."
//
// See https://blog.regehr.org/archives/28 for a great discussion on volatile
// variables.
//
// The reason I'm using it here is so that this program doesn't get wrecked by
// processes that are able to return to DCLOAD. The go.s file also wipes out all
// registers and the stack, so GCC needs to be told not to put long-term
// variables there. It does mean the memory footprint increases a tiny bit, but
// it's not huge.
volatile unsigned char booted = 0;
volatile unsigned char running = 0;

static volatile unsigned int current_counter_array[2] = {0};
//static volatile unsigned long long int old_dhcp_lease_updater = 0; // To update lease time display
static volatile unsigned int old_dhcp_lease_updater_array[2] = {0}; // To update lease time display
static volatile unsigned char dont_renew = 0;

/* converts expevt value to description, used by exception handler */
char * exception_code_to_string(unsigned int expevt)
{
	switch(expevt) {
	case 0x1e0:
		return "User break";
		break;
	case 0x0e0:
		return "Address error (read)";
		break;
	case 0x040:
		return "TLB miss exception (read)";
		break;
	case 0x0a0:
		return "TLB protection violation exception (read)";
		break;
	case 0x180:
		return "General illegal instruction exception";
		break;
	case 0x1a0:
		return "Slot illegal instruction exception";
		break;
	case 0x800:
		return "General FPU disable exception";
		break;
	case 0x820:
		return "Slot FPU disable exception";
		break;
	case 0x100:
		return "Address error (write)";
		break;
	case 0x060:
		return "TLB miss exception (write)";
		break;
	case 0x0c0:
		return "TLB protection violation exception (write)";
		break;
	case 0x120:
		return "FPU exception";
		break;
	case 0x080:
		return "Initial page write exception";
		break;
	case 0x160:
		return "Unconditional trap (TRAPA)";
		break;
	default:
		return "Unknown exception";
		break;
	}
}

// NOTE: this is hex, but I don't want to change this function name since it's
// hardcoded into various asm files
void uint_to_string(unsigned int foo, unsigned char *bar)
{
	char hexdigit[17] = "0123456789abcdef";
	int i;

	for(i=7; i>=0; i--) {
		bar[i] = hexdigit[(foo & 0x0f)];
		foo = foo >> 4;
	}
	bar[8] = 0;
}

// 'bar' buffer is assumed to be large enough.
// The biggest decimal number is 4294967295, which is 10 characters‬ (excluding null term).
// So the buffer should be able to hold 11 characters.
static void uint_to_string_dec(unsigned int foo, char *bar)
{
	char decdigit[11] = "0123456789";
	int i;

	for(i=9; i>=0; i--) {
		if((!foo) && (i < 9))
		{
			bar[i] = ' '; // pad with space
		}
		else
		{
			bar[i] = decdigit[(foo % 10)];
		}
		foo /= 10; // C functions are pass-by-value
	}
	bar[10] = 0; // Null term
}

// For mac address string printing (mac address is in network byteorder)
static void uchar_to_string_hex(unsigned int foo, char *bar)
{
	char hexdigit[17] = "0123456789abcdef";

	bar[1] = hexdigit[foo & 0x0f];
	bar[0] = hexdigit[foo >> 4];
}

// For IP string printing (IP address is in host byteorder)
static void uchar_to_string_dec(unsigned int foo, char *bar)
{
	char decdigit[11] = "0123456789";

	bar[2] = decdigit[foo % 10]; // Ones
	bar[1] = decdigit[(foo / 10) % 10]; // Tens
	bar[0] = decdigit[foo / 100]; // Hundreds
	// Max is 255, anyways.
}

/* set n lines starting at line y to value c */
void clear_lines(unsigned int y, unsigned int n, unsigned int c)
{
	unsigned short * vmem = (unsigned short *)(0xa5000000 + y*640*2);
	n = n * 640;
	while (n-- > 0)
		*vmem++ = c;
}

// I've seen this used before, but I can't remember what used it.
// Bear in mind this function takes up about 144 bytes just sitting here.
// That can increase to 160 bytes under certain alignment conditions.
void draw_progress(unsigned int current, unsigned int total)
{
// A hexadecimal progress indicator...
//	unsigned char current_string[9];
//	unsigned char total_string[9];

//	uint_to_string(total, total_string);
//	uint_to_string(current, current_string);
//	clear_lines(120, 24, BG_COLOR);
//	draw_string(30, 174, "(", STR_COLOR);
//	draw_string(42, 174, current_string, STR_COLOR);
//	draw_string(138, 174, "/", STR_COLOR);
//	draw_string(150, 174, total_string, STR_COLOR);
//	draw_string(246, 174, ")", STR_COLOR);

	// Well, we can do decimal now.
	char current_string[11];
	char total_string[11];

	uint_to_string_dec(total, total_string);
	uint_to_string_dec(current, current_string);
	clear_lines(120, 24, BG_COLOR);
	draw_string(30, 174, "(", STR_COLOR);
	draw_string(42, 174, current_string, STR_COLOR);
	draw_string(162, 174, "/", STR_COLOR);
	draw_string(174, 174, total_string, STR_COLOR);
	draw_string(294, 174, ")", STR_COLOR);
}

// called by exception.s
void setup_video(unsigned int mode, unsigned int color)
{
	init_video(check_cable(), mode);
	clrscr(color);
}

static void error_bb(char *msg)
{
	setup_video(0, 0x2000); // Red screen
	draw_string(30, 54, NAME, STR_COLOR);
	draw_string(30, 78, msg, STR_COLOR);
	while(1);
}

static char *mac_string = "de:ad:be:ef:ba:be";
static char *ip_string = "000.000.000.000";

void disp_info(void)
{
	int c;
	unsigned char *ip = (unsigned char *)&our_ip;

	setup_video(0, BG_COLOR);
	draw_string(30, 54, NAME, STR_COLOR);
	draw_string(30, 78, bb->name, STR_COLOR);
	draw_string(30, 102, mac_string, STR_COLOR);
	for(c = 0; c < 4; c++)
		uchar_to_string_dec(ip[3-c], &ip_string[c*4]);
	draw_string(30, 126, ip_string, STR_COLOR);

	booted = 1;
}

void disp_status(const char * status) {
	clear_lines(150, 24, BG_COLOR);
	draw_string(30, 150, status, STR_COLOR);
}

// The C language technically requires that all uninitialized global variables
// get initted to 0 via .bss, which is something I really don't like relying on.
// 'our_ip' is declared in commands.c, of all places, and is not given an
// explicit initializer (so I gave it one because it really does need one)...
// In commands.c it also looks like 'our_ip' gets overwritten by the destination
// IP of dcload-ip command packets routed to the Dreamcast. Thankfully, this is
// fine when that destination address will always match the DHCP address.
// It does also allow the "arp" trick to continue functioning if needed, but the
// IP address for arp is not allowed to be 0.x.x.x any more, as mentioned in
// various other comments in this file.

// This function looks to be the primary initializer of the our_ip variable...
static void set_ip_from_file(void)
{
	unsigned char i, c;
	unsigned char *ip = (unsigned char *)&our_ip;

	if(ip[3] != 0)
	{
		// We probably got back here from an exception or something. Original IP's still valid!
		return;
	}

	i = 0;
	c = 0;

// So THAT'S why 192.168.0.x never worked!!
/*
	while(DREAMCAST_IP[c] != 0) {
		if (DREAMCAST_IP[c] != '.') {
			ip[i] *= 10;
			ip[i] += DREAMCAST_IP[c] - '0';
		}
		else
			i++;
		c++;
	}

	our_ip = ntohl(our_ip);
*/

	// Any IP will work now, but the string must be 16 chars, including null term.
	// So format an IP in Makefile.cfg with leading zeros in each octet like this:
	// 192.168.001.058 or 010.000.076.002, etc.

	while(c < 15) {
		if (DREAMCAST_IP[c] != '.') {
			ip[i] *= 10;
			ip[i] += DREAMCAST_IP[c] - '0';
		}
		else
			i++;
		c++;
	}

	// IP is currently stored, for example, ip[0] 192-168-0-11 ip[3] (this is big endian data on a little endian system), which is what networking packets expect
	our_ip = ntohl(our_ip);
	// Now it's stored ip[0] 11-0-168-192 ip[3] (this is little endian data on a little endian system), which make_ip handles when building packets.
}

static const char *waiting_string = "Waiting for IP..."; // Waiting for IP indicator. 15 visible characters to match IP address string's visible 15 characters
static const char *dhcp_mode_string = " (DHCP Mode)"; // Indicator that DHCP is active
static const char *dhcp_timeout_string = " (DHCP Timed Out!)"; // DHCP timeout indicator
static const char *dhcp_lease_string = "DHCP Lease Time (sec): "; // DHCP lease time
static char dhcp_lease_time_string[11] = {0}; // For converting lease time to seconds. 10 characters + null term. Max lease is theoretically 4294967295, but really is 1407374 due to perf counters.

static void update_ip_display(unsigned char *new_ip, const char *mode_string)
{
	int c;

	clear_lines(126, 24, BG_COLOR);
	for(c = 0; c < 4; c++)
	{
		uchar_to_string_dec(new_ip[3-c], &ip_string[c*4]);
	}
	draw_string(30, 126, ip_string, STR_COLOR);
	draw_string(210, 126, mode_string, STR_COLOR);
}

// Not merging this with update_ip_display because GCC might inline them and
// cause a triple-nested if(), which will then break things.
static void dhcp_waiting_mode_display(void)
{
	clear_lines(126, 24, BG_COLOR);
	draw_string(30, 126, waiting_string, STR_COLOR);
	draw_string(234, 126, dhcp_mode_string, STR_COLOR);
}

static void update_lease_time_display(unsigned int new_time)
{
	// Casting to char gets rid of GCC warning.
	uint_to_string_dec(new_time, dhcp_lease_time_string);
	clear_lines(448, 24, BG_COLOR);
	draw_string(30, 448, dhcp_lease_string, STR_COLOR);
	draw_string(306, 448, dhcp_lease_time_string, STR_COLOR);
}

// Magic IP range to enable DHCP mode is 0.0.0.0/8, aka 0.x.x.x
// This range is reserved for source addresses only, per IETF.
// See https://en.wikipedia.org/wiki/IPv4#Special-use_addresses for details.
// DHCP clients have a source address of 0.0.0.0 and broadcast DHCP discover
// to 255.255.255.255 in order to initiate the DHCP process.

// This means, to use ARP to assign an IP address, use something like the
// 169.254.0.0/16 (a.k.a. 169.254.x.x) range. This range is specifically meant
// for that kind of direct link between two hosts.
void set_ip_dhcp(void)
{
	// Normally this would be if(!booted). However, this branch is not likely to
	// run frequently at all, so tell GCC to minimize its footprint in set_ip_dhcp()
	// with this macro. Ergo, we expect !booted to evaluate to 0.
	if(__builtin_expect(!booted, 0))
	{
		disp_info();
	}

	unsigned char *ip = (unsigned char *)&our_ip;

	// Check renewal condition. Only matters if dhcp_lease_time has been set before.
	// The counter is 48-bit, so the max lease time allowed is 1,407,374 seconds. That's about 16.3 days.

	// SH4 dmulu.l is 32 bit * 32 bit --> 64 bit
	// GCC knows to use dmulu.l based on this code.
	// read_pmctr(DCLOAD_PMCTR) * 5 is seconds * 10^9, but that's a 64-bit multiply... Hmm...
	unsigned long long int long_dhcp_lease_time = (unsigned long long int)dhcp_lease_time * (unsigned long long int)(PERFCOUNTER_SCALE); // Multiplying is always better than dividing if possible.
//	unsigned long long int current_counter = read_pmctr(DCLOAD_PMCTR); // Multiplied instead of divided to convert seconds into counter frequency units
	read_pmctr(DCLOAD_PMCTR, current_counter_array);
	// By scaling up we can at least ensure that, internally, seconds comparisons will be accurate. Displaying it to the user is not quite as straightforward.

	// This counts up from 0, displays in the disp_status area
	// Uh, but it's a 64-bit divide now...
	//uint_to_string_dec(*current_counter / 200000000ULL, dhcp_lease_time_string);
//	uint_to_string_dec(current_counter_array[0], dhcp_lease_time_string);
//	uint_to_string(current_counter_array[1], (unsigned char*)dhcp_lease_time_string);
//	uint_to_string(current_counter_array[0], (unsigned char*)dhcp_lease_time_string);
//	disp_status(dhcp_lease_time_string);

	// Check if lease is still active, renewal threshold is at 50% lease time. '>> 1' is '/2'.
	// NOTE: GCC apparently can't handle the concept of dividing 64-bit numbers on SH4, even by a power of two.
	// It adds over 1kB of extra code at the mere sight of it, weirdly, so we do the shift manually here.
	unsigned long long int *current_counter = (unsigned long long int*)current_counter_array;
	unsigned long long int *old_dhcp_lease_updater = (unsigned long long int*)old_dhcp_lease_updater_array;

	if(dhcp_lease_time && (!dont_renew) && ((long_dhcp_lease_time >> 1) < (*current_counter)))
	{
		dhcp_lease_time = 0; // This disables DHCP renewal unless it gets updated with a valid value. Its dual-purpose is a renewal code enabler.
//		old_dhcp_lease_updater = 0;
		old_dhcp_lease_updater_array[0] = 0;
		old_dhcp_lease_updater_array[1] = 0;
		// Renewal needed
		dhcp_waiting_mode_display();

		disp_status("DHCP renewing...");
		int renew_result = dhcp_renew((unsigned int*)&our_ip); // So that GCC doesn't warn about volatile

		if(renew_result == -2)
		{
			// Not an error, it just means IP was no longer valid. Time to get a new one!
			// We're supposed to wait until 87.5% of the lease time has elapsed to do a new discover.
			dont_renew = 1;
		}
		else if(renew_result == -1)
		{
			// Errored out (ACK was invalid/didn't pass validation steps in
			// handle_dhcp_reply()), so IP is invalid. Therefore set it to
			// 255.255.255.255 so that the dhcp discover process won't occur.
			// With a lease time of 0 and an IP of 0xffffffff, DHCP renewal and
			// discovery will both be disabled, effectively disabling DHCP entirely.
			our_ip = 0xffffffff;

			// Update IP, change DHCP active indicator to timeout.
			update_ip_display(ip, dhcp_timeout_string);
			// Display lease time of 0
			update_lease_time_display(dhcp_lease_time);

			// At this point DHCP is disabled.
			// Don't need the counter anymore.
			disable_pmctr(DCLOAD_PMCTR);

			// Early return because there's no point in continuing.
			// dhcp_renew() runs dhcp_go() if it gets a NAK, and we probably got here
			// because of dhcp_nest_counter_maxed
			return;
		}
		else
		{
			// Not in waiting mode any more
			update_ip_display(ip, dhcp_mode_string);
			// Didn't time out! Display new lease time.
			update_lease_time_display(dhcp_lease_time);
		}

		disp_status("idle..."); // Staying consistent with DCLOAD conventions
	}
	// Only update if we need to. bb->loop could very easily be faster than 1 Hz.
	// Also: When int size is 32-bit, 'ULL' indicates a constant is actually
	// supposed to be stored in an unsigned long long int (64-bit, in SH4's case).
//	else if( (long_dhcp_lease_time >= current_counter) && (current_counter > (old_dhcp_lease_updater + ONSCREEN_REFRESH_SCALED - 1ULL)) )
	else if( (long_dhcp_lease_time >= (*current_counter)) && ((*current_counter) > ((*old_dhcp_lease_updater) + ONSCREEN_REFRESH_SCALED - 1ULL)) )
	{
//		old_dhcp_lease_updater = current_counter;
		old_dhcp_lease_updater_array[0] = current_counter_array[0];
		old_dhcp_lease_updater_array[1] = current_counter_array[1];

		// SH4 can only do 32 bit / 32 bit --> 32 bit
		// Need to somehow divide the (long_dhcp_lease_time - current_counter) difference, which is 64-bit, without 64-bit divide...
		unsigned long long int difference = long_dhcp_lease_time - (*current_counter);
#ifndef FAST_COUNTER
		unsigned int remaining_lease = (unsigned int)((difference * 43980ULL) >> 43); // <-- Assumes perfcounter values are supposed to be divided by 200,000,000
#else
		unsigned int remaining_lease = (unsigned int)((difference * 58788ULL) >> 47); // <-- Assumes perfcounter values are supposed to be divided by experimentally-derived 2,393,976,245
#endif
		// For some reason this crazy number, if the perf counter is "running fast" is only off by one second every 117576 seconds, which is the entire range at that speed.
		update_lease_time_display(remaining_lease);

		// MATH!
		// This is accurate to within 15 seconds. Meaning, the on-screen display
		// will be faster than actual time by less than one second per day. If the
		// lease time were 1407374 seconds, the display counter would go up to 1407359.
		// (Ignoring the fact that lease renewal threshold is at 50% lease time.)
		// The trick to this is finding a fraction suitably close to 1/200000000
		// but the denominator is a power of two. 43980 is the largest numerator
		// that can multiply the max 48-bit number and still be within 64-bits
		// while having a power-of-two denominator that keeps the fraction ~5x10^-9.
		// The maximum number that can multiply the maximum 48-bit number and stay
		// in 64-bits is 2^16 (64 - 48 = 16), or 65535.
		//
		// Now what GCC does here is really neat: it multiplies the upper 32 bits of
		// 'difference' by 43980, and then 32-bit x 32-bit --> 64-bit multiplies
		// 43980 by the lower 32 bits. Then, it adds the result of the upper 32-bit
		// multiply with the upper 32-bits of the 64-bit result from the lower 32-bits
		// multiply. Then it shifts that sum right by 11 instead of 43, and uses that
		// register. This is one of my favorite tricks, and I'm glad to see GCC doing
		// it here. :)
		//
		// Note:
		// Loss of accuracy due to shifting the counter frequency happens, for example
		// doing >> 16 instead of the weird math trick would be off by 49664 cycles
		// per 200000000, or 0.0248% of 1 second per second. This would mean that
		// if the DHCP lease time is the max allowed (16.3 days), renewal will happen
		// about 350 seconds, or about 5 minutes, 50 seconds, early. You'd see it as
		// the on-screen timer will be too fast by 1 second every 1 hour, 6 minutes,
		// and 40 seconds. Crazy math trick is much more accurate since 200000000 is
		// not a very nice number to work with in base-2.
	}

	// We're supposed to wait until 87.5% of the lease time has elapsed to do a new discover,
	// if we even need to do a new discover (due to getting a NAK during renewal).
	// GCC will almost certainly optimize this into the conditional. This is just way more readable.
	// Also: best variable name ever, lol.
//	unsigned int eighty_seven_point_five = (long_dhcp_lease_time >> 1) + (long_dhcp_lease_time >> 2) + (long_dhcp_lease_time >> 3); // 0.5 + 0.25 + 0.125 = 0.875, or 87.5%
	unsigned long long int eighty_seven_point_five = (long_dhcp_lease_time >> 1) + (long_dhcp_lease_time >> 2) + (long_dhcp_lease_time >> 3); // 0.5 + 0.25 + 0.125 = 0.875, or 87.5%
	// This checks if set_ip_from_file() gave us a DHCP-mode IP (0.x.x.x) or a static
	// IP of some sort. It also checks if the DHCP lease expired per above. DOUBLE WHAMMY!!
	// Only need to check the first octet of IP since it's a /8 range
	// Also check if renew was NAK'd, and if it was, are we at the 87.5% threshold for a new discover?
	if( (ip[3] == 0) || (dont_renew && (eighty_seven_point_five < (*current_counter))) )
	{
		dont_renew = 0;
		dhcp_waiting_mode_display();

		// Wait until DHCP assigns an IP address. This should not take very long.
		// If it takes over 10 seconds, DHCP is probably not working.
		disp_status("Acquiring new IP address via DHCP...");
		int dhcp_result = dhcp_go((unsigned int*)&our_ip); // So that GCC doesn't warn about volatile
		if((dhcp_result == -1) || dhcp_nest_counter_maxed)
		{
			// IP will be 0.0.0.0. Use the old ARP method at this point.
			// Since dhcp_lease_time wasn't set, renewal won't happen; no worries there.

			// So set IP to 255.255.255.255, which means that an ACK was received but
			// it was invalid/didn't pass the validation checks in handle_dhcp_reply()
			our_ip = 0xffffffff;

			// Change DHCP active indicator to timeout.
			update_ip_display(ip, dhcp_timeout_string);

			// At this point DHCP is disabled.
			// Don't need the counter anymore.
			disable_pmctr(DCLOAD_PMCTR);
		}
		else
		{
 			// Else we didn't time out and probably got an address, hurray!
			// Overwrite displayed IP with DHCP-provided IP
			update_ip_display(ip, dhcp_mode_string);
			// Display time until IP lease ends.
			update_lease_time_display(dhcp_lease_time);
		}

		disp_status("idle..."); // Staying consistent with DCLOAD conventions
	}

	// DCLoad-IP can now keep on going as normal.
}

int main(void)
{
	unsigned char start;

	running = 0;

	/*    scif_init(115200); */

	if (adapter_detect() < 0)
		error_bb("NO ETHERNET ADAPTER DETECTED!");

	for(start = 0; start < 6; start++)
		uchar_to_string_hex(bb->mac[start], mac_string + start*3);

	set_ip_from_file();

	cdfs_redir_save(); /* will only save value once */
	cdfs_redir_disable();

	maple_init();

	if (!booted) {
		disp_info();
	} else {
		booted = 0;
	}
	/*
		scif_puts(NAME);
		scif_puts("\n");
	*/

	// Enable pmcr1 if it's not enabled
	// (Don't worry, this won't run again if it's already enabled; that would accidentally reset the lease timer!)
	init_pmctr(DCLOAD_PMCTR, PMCR_ELAPSED_TIME_MODE);

	while (1) {
		*VIDBORDER = 0;

		if (booted) {
			disp_status("idle...");
		}

		bb->loop(1); // Identify that this bb->loop is the main loop for set_ip_dhcp()
	}
}
