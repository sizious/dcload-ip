#ifndef __DCLOAD_H__
#define __DCLOAD_H__

//==============================================================================
// ---- Start of user-changeable definitions ----
//==============================================================================

// Desired on-screen refresh interval for DHCP lease time
// In seconds, minimum is 1 second.
// If you don't like it, set it to something like 100 seconds.
// If you really don't like it, set it to 1410902 and you'll never see it change.
#define ONSCREEN_DHCP_LEASE_TIME_REFRESH_INTERVAL 1

// Which perfcounter DCLOAD should use
// Valid values are 1 or 2 ONLY.
#define DCLOAD_PMCR 1

// Background color
// In RGB0555 format
// BBA default blue is 0x0010
// LAN default green is 0x0100
// Error default red is 0x2000
#define BBA_BG_COLOR 0x0010
#define LAN_BG_COLOR 0x0100
#define ERROR_BG_COLOR 0x2000

// String color (0xffff = white)
#define STR_COLOR 0xffff

// -- WARNING: --
//
// The below definitions are for configuring specific system functionality.
// Don't change them unless you know you need them. Overclocked machines would
// need to change the numbers described by comments marked 'OC' (no quotes).
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
//#define PERFCTR_DEBUG

// Use FPSCR PR=1, SZ=1 to improve double-precision loading performance (use 2x
// 64-bit "paired moves" via fmov.d versus 4x 32-bit "single moves" via fmov.s).
// Disabled by default because it's technically undefined behavior, and using it
// breaks compatibility with SH4A CPUs (lol). It also may not behave the same on
// all SH7091 CPUs (works on mine).
//#define UNDEFINED_DOUBLES

//==============================================================================
// ---- End of user-changeable definitions ----
//==============================================================================

#define LAN_MODEL 0300
#define BBA_MODEL 0400

// Globally-important variables
extern volatile unsigned char booted;
extern volatile unsigned char running;
extern volatile unsigned int global_bg_color;
extern volatile unsigned int installed_adapter;

// Called by asm functions
char * exception_code_to_string(unsigned int expevt);
void uint_to_string(unsigned int foo, unsigned char *bar);
void setup_video(unsigned int mode, unsigned int color);

// Called by other parts of dcload
void disp_info(void);
void disp_status(const char * status);
void clear_lines(unsigned int y, unsigned int n, unsigned int c);
void uint_to_string_dec(unsigned int foo, char *bar);

// Exported for bb->loop
void set_ip_dhcp(void);

// For dcload-crt0.s to interface with startup_support.c
void __call_builtin_sh_set_fpscr(unsigned int value);

// These definitions correspond to 'fbuffer_color_mode' in STARTUP_Init_Video()
// dcload only supports 16-bit color modes
#define FB_RGB0555 0
#define FB_RGB565 1

// To set video modes via startup_support.c
// dcload only supports 640x480 modes
void STARTUP_Init_Video(unsigned char fbuffer_color_mode);
void STARTUP_Set_Video(unsigned char fbuffer_color_mode);

#endif
