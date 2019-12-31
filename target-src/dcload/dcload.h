#ifndef __DCLOAD_H__
#define __DCLOAD_H__

// ---- Start of user-changeable definitions ----

// Desired on-screen refresh interval for DHCP lease time
// In seconds, minimum is 1 second.
// If you don't like it, set it to something like 100 seconds.
// If you really don't like it, set it to 1410902 and you'll never see it change.
#define ONSCREEN_DHCP_LEASE_TIME_REFRESH_INTERVAL 1

// Which perfcounter DCLOAD should use
// Valid values are 1 or 2 ONLY.
#define DCLOAD_PMCR 1

// Background color
#define BG_COLOR 0x0010

// String color (0xffff = white)
#define STR_COLOR 0xffff

// -- WARNING: --
//
// The below definitions are for configuring specific system functionality.
// Don't change them unless you know you need them. Overclocked machines would
// need to change the numbers described by comments marked OC.
//

// OC: Overclocked machines would need to change this.
// Please keep the parentheses.
// 240MHz: #define SH4_FREQUENCY (240 * 1000 * 1000)
// 230MHz: #define SH4_FREQUENCY (230 * 1000 * 1000)
// 220MHz: #define SH4_FREQUENCY (220 * 1000 * 1000)
// 210MHz: #define SH4_FREQUENCY (210 * 1000 * 1000)
// Stock 200MHz: #define SH4_FREQUENCY (200 * 1000 * 1000)
// NOTE: For stock, (199 * 5000 * 1000) is more accurate.

// I measured mine; it's pretty close to 199.5MHz
#define SH4_FREQUENCY (199496956)

// Uncomment this if the counters use the CPU/bus ratio for timing, otherwise
// leave it commented out. Only set this if you know you need it.
//#define BUS_RATIO_COUNTER

#ifndef BUS_RATIO_COUNTER
// This would run for 16.3 days (1410902 seconds) if used.
#define PERFCOUNTER_SCALE SH4_FREQUENCY
// SH4 frequency as a double-precision constant
// OC: Overclocked machines would need to change this. Unfortunately we can't
// cast SH4_FREQUENCY to double since many compilers for the Dreamcast use
// m4-single-only, which forces doubles into single-precision floats, so this
// needs to be hardcoded. It also needs to be stored as separate 32-bit halves
// because of the way the SH4 handles double-precision in little-endian mode...
#define PERFCOUNTER_SCALE_DOUBLE_HIGH 0x41a7c829
#define PERFCOUNTER_SCALE_DOUBLE_LOW 0xf8000000
#else
// Experimentally derived ratio-mode perf counter division value, see:
// https://dcemulation.org/phpBB/viewtopic.php?p=1057114#p1057114
// This would run for 1 day 8 hours and 40 minutes (117575 seconds) if used.
// OC: Overclocked Dreamcasts may need to change this. This is just the bus
// clock (nominally 99.75MHz, documented as the "100MHz" bus clock) * 24.
#define PERFCOUNTER_SCALE 2393976245
#endif

// Enable for perf counter debugging printouts, placed under the disp_status area
// It's perf high stuck to perf low, and then the contents of the pmcr reg in use
// (i.e. the reg specified by DCLOAD_PMCR)
#define PERFCTR_DEBUG

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

// Exported for bb->loop
void set_ip_dhcp(void);

#endif
