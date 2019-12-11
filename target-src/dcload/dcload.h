#ifndef __DCLOAD_H__
#define __DCLOAD_H__

// ---- Start of user-changeable definitions ----

// Which perfcounter DCLOAD should use
// Valid values are 1 or 2 ONLY.
#define DCLOAD_PMCTR 1

// Set to 1 if the counters run fast
// Otherwise set to 0
#define FAST_COUNTER 0

// Background color
#define BG_COLOR 0x0010
// String color (0xffff = white)
#define STR_COLOR 0xffff

// Desired on-screen refresh interval for DHCP lease time
// In seconds, minimum is 1 second.
// If you don't like it, set it to something like 100 seconds.
// If you really don't like it, set it to 1407374 and you'll never see it change.
#define ONSCREEN_DHCP_LEASE_TIME_REFRESH_INTERVAL 1

// Overclocked machines would need to change this.
// Please keep the parentheses.
// 240MHz: #define SH4_FREQUENCY (240 * 1000 * 1000)
// 230MHz: #define SH4_FREQUENCY (230 * 1000 * 1000)
// 220MHz: #define SH4_FREQUENCY (220 * 1000 * 1000)
// 210MHz: #define SH4_FREQUENCY (210 * 1000 * 1000)
// Stock 200MHz: #define SH4_FREQUENCY (200 * 1000 * 1000)

#define SH4_FREQUENCY (200 * 1000 * 1000)

// ---- End of user-changeable definitions ----

// Globally-important variables
extern volatile unsigned char booted;
extern volatile unsigned char running;

// Called by asm functions
char * exception_code_to_string(unsigned int expevt);
void uint_to_string(unsigned int foo, unsigned char *bar);

void disp_info(void);
void disp_status(const char * status);
void clear_lines(unsigned int y, unsigned int n, unsigned int c);

void set_ip_dhcp(void); // Exported for bb->loop

#endif
