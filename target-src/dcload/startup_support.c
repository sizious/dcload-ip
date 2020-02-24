// This file just contains some C code that is required by dcload-crt0.s and
// startup routines. Based on the DreamHAL startup_support.c, but specially
// adapted and simplified for dcload to use.
//
// DreamHAL: https://github.com/Moopthehedgehog/DreamHAL/
//
// --Moopthehedgehog

#include "dcload.h"
#include <stdint.h>

// dcload-crt0.s needs this to set FPSCR since GCC deprecated __set_fpscr and
// __get_fpscr and replaced them with builtins.
//
// void __builtin_sh_set_fpscr(uint32_t val) doesn't affect SZ, PR, and FR,
// unlike the old void __set_fpscr(uint32_t val) macro.
//
// uint32_t __builtin_sh_get_fpscr(void), on the other hand, behaves the same as
// the old uint32_t __get_fpscr(void) macro.
//
// Also: the old macros were in libgcc, and the new ones are not (yay!).
//

#if __GNUC__ <= 4
extern void __set_fpscr(unsigned int value);

void __call_builtin_sh_set_fpscr(unsigned int value)
{
  __set_fpscr(value);
}

#else

void __call_builtin_sh_set_fpscr(unsigned int value)
{
  __builtin_sh_set_fpscr(value);
}

#endif


// These get set by STARTUP_Init_Video() for use only by STARTUP_Set_Video()
static volatile uint32_t cable_mode = 0;
static volatile uint32_t video_region = 0;

// Video mode is automatically determined based on cable type and console region
// This sets up everything related to Dreamcast video modes.
// The framebuffer address will always be 0xa5000000 after this runs.
void STARTUP_Init_Video(unsigned char fbuffer_color_mode)
{
  // Set cable type to hardware pin setting
  // Need to read port 8 and 9 data (bits 8 & 9 in PDTRA), so set them as input
  // direction via PCTRA (necessary per SH7750 hardware manual):
  *(volatile uint32_t*)0xff80002c = ( (*(volatile uint32_t*)0xff80002c) & 0xfff0ffff ) | 0x000a0000;

  // According to the BootROM, cable data is on PORT8/9 GPIO pins.
  // Read them and then write them to somewhere in AICA memory (refer to notes
  // section for an explanation and a theory as to why this might be necessary):
  cable_mode = (uint32_t)( (*(volatile uint16_t*)0xff800030) & 0x300 );
  *(volatile uint32_t*)0xa0702c00 = ( (*(volatile uint32_t*)0xa0702c00) & 0xfffffcff ) | cable_mode;
  // Per the SH7750 manual, there are 16 data regs, hence a 16-bit read should
  // be used on PDTRA.

  // Store video output region (0 = NTSC, 1 = PAL)
  video_region = (*(uint8_t*)0x8c000074) - 0x30;

  // Reset graphics subsystem (PVR2), but keep the graphics memory bus on ..or
  // else things will hang when writing to graphics memory since it got disabled!
  *(volatile uint32_t*)0xa05f8008 = 0x00000003;
  // Re-enable PVR, TA
  *(volatile uint32_t*)0xa05f8008 = 0x00000000;

  STARTUP_Set_Video(fbuffer_color_mode);
}

// The framebuffer address will always be 0xa5000000 after this runs.
void STARTUP_Set_Video(unsigned char fbuffer_color_mode)
{
  uint32_t horiz_active_area = 640;
  uint32_t vert_active_area = 480;
  // {RGB0555, RGB565} = 2Bpp, {RGB888} = 3Bpp, {RGB0888} = 4Bpp
  uint32_t bpp_mode_size = fbuffer_color_mode + 1 + (0x1 ^ ((fbuffer_color_mode & 0x1) | (fbuffer_color_mode >> 1))); // Add another 1 only if 0b00

  if(!cable_mode) // VGA 640x480 @ 60Hz
  {
    // Set registers the same way that the BootROM does
    *(volatile uint32_t*)0xa05f80e8 = 0x00160008;
    *(volatile uint32_t*)0xa05f8044 = 0x00800000 | (fbuffer_color_mode << 2);

    *(volatile uint32_t*)0xa05f804c = (horiz_active_area * bpp_mode_size) / 8; // for PVR to know active area width
    *(volatile uint32_t*)0xa05f8040 = 0x00000000; // Border color in RGB0888 format
    *(volatile uint32_t*)0xa05f805c = (1 << 20) | ((vert_active_area - 1) << 10) | (((horiz_active_area * bpp_mode_size) / 4) - 1); // progressive scan has a 1 since no lines are skipped
    *(volatile uint32_t*)0xa05f80ec = 0x000000a8;
    *(volatile uint32_t*)0xa05f80f0 = 0x00280028;
    *(volatile uint32_t*)0xa05f80c8 = 0x03450000;
    *(volatile uint32_t*)0xa05f80cc = 0x00150208;
    *(volatile uint32_t*)0xa05f80d0 = 0x00000100;
    *(volatile uint32_t*)0xa05f80d4 = 0x007e0345;
    *(volatile uint32_t*)0xa05f80d8 = 0x020c0359;
    *(volatile uint32_t*)0xa05f80dc = 0x00280208;
    *(volatile uint32_t*)0xa05f80e0 = 0x03f1933f;

    uint32_t scan_area_size = horiz_active_area * vert_active_area;
    uint32_t scan_area_size_bytes = scan_area_size * bpp_mode_size; // This will always be divisible by 4

    // Reset framebuffer address
    *(volatile uint32_t*)0xa05f8050 = 0x00000000; // BootROM sets this to 0x00200000 (framebuffer base is 0xa5000000 + this)
    *(volatile uint32_t*)0xa05f8054 = 0x00000000; // Same for progressive, resetting the offset gets us 2MB VRAM back after BootROM is done with it

    // zero out framebuffer area
    for(uint32_t pixel_or_two = 0; pixel_or_two < scan_area_size_bytes; pixel_or_two += 4)
    {
      *(uint32_t*)(0xa5000000 + pixel_or_two) = 0;
    }

    // re-enable video
    *(volatile uint32_t*)0xa05f80e8 &= ~8;
    *(volatile uint32_t*)0xa05f8044 |= 1;
  }
  else if(!video_region) // NTSC (480i)
  {
    // Set registers the same way that the BootROM does
    *(volatile uint32_t*)0xa05f80e8 = 0x00160008;
    *(volatile uint32_t*)0xa05f8044 = 0x00000000 | (fbuffer_color_mode << 2);

    *(volatile uint32_t*)0xa05f804c = (horiz_active_area * bpp_mode_size) / 8; // for PVR to know active area width
    *(volatile uint32_t*)0xa05f8040 = 0x00000000; // Border color in RGB0888 format
    *(volatile uint32_t*)0xa05f805c = ((((horiz_active_area * bpp_mode_size) / 4) + 1) << 20) | (((vert_active_area / 2) - 1) << 10) | (((horiz_active_area * bpp_mode_size) / 4) - 1);
    *(volatile uint32_t*)0xa05f80ec = 0x000000a4;
    *(volatile uint32_t*)0xa05f80f0 = 0x00120012;
    *(volatile uint32_t*)0xa05f80c8 = 0x03450000;
    *(volatile uint32_t*)0xa05f80cc = 0x00150104;
    *(volatile uint32_t*)0xa05f80d0 = 0x00000150;
    *(volatile uint32_t*)0xa05f80d4 = 0x007e0345;
    *(volatile uint32_t*)0xa05f80d8 = 0x020c0359;
    *(volatile uint32_t*)0xa05f80dc = 0x00240204;
    *(volatile uint32_t*)0xa05f80e0 = 0x07d6c63f;

    uint32_t scan_area_size = horiz_active_area * vert_active_area;
    uint32_t scan_area_size_bytes = scan_area_size * bpp_mode_size; // This will always be divisible by 4

    // Reset framebuffer address
    *(volatile uint32_t*)0xa05f8050 = 0x00000000; // BootROM sets this to 0x00200000 (framebuffer base is 0xa5000000 + this)
    *(volatile uint32_t*)0xa05f8054 = horiz_active_area * bpp_mode_size; // This is for interlaced, resetting the offset gets us 2MB VRAM back after BootROM is done with it

    // zero out framebuffer area
    for(uint32_t pixel_or_two = 0; pixel_or_two < scan_area_size_bytes; pixel_or_two += 4)
    {
      *(uint32_t*)(0xa5000000 + pixel_or_two) = 0;
    }

    // re-enable video
    *(volatile uint32_t*)0xa05f80e8 &= ~8;
    *(volatile uint32_t*)0xa05f8044 |= 1;
  }
  else // PAL (576i)
  {
    #ifdef PAL_EXTRA_LINES
      // Interlaced PAL can actually have 528 active vertical lines on the Dreamcast (48 more than NTSC!)
      // In theory a developer could hide a secret message for PAL players in this area
      vert_active_area += 48;
    #endif

    // Set registers the same way that the BootROM does
    *(volatile uint32_t*)0xa05f80e8 = 0x00160008;
    *(volatile uint32_t*)0xa05f8044 = 0x00000000 | (fbuffer_color_mode << 2);

    *(volatile uint32_t*)0xa05f804c = (horiz_active_area * bpp_mode_size) / 8; // for PVR to know active area width
    *(volatile uint32_t*)0xa05f8040 = 0x00000000; // Border color in RGB0888 format
    *(volatile uint32_t*)0xa05f805c = ((((horiz_active_area * bpp_mode_size) / 4) + 1) << 20) | (((vert_active_area / 2) - 1) << 10) | (((horiz_active_area * bpp_mode_size) / 4) - 1);
    *(volatile uint32_t*)0xa05f80ec = 0x000000ae;
    *(volatile uint32_t*)0xa05f80f0 = 0x002e002d;
    *(volatile uint32_t*)0xa05f80c8 = 0x034b0000;
    *(volatile uint32_t*)0xa05f80cc = 0x00150136;
    *(volatile uint32_t*)0xa05f80d0 = 0x00000190;
    *(volatile uint32_t*)0xa05f80d4 = 0x008d034b;
    *(volatile uint32_t*)0xa05f80d8 = 0x0270035f;
    *(volatile uint32_t*)0xa05f80dc = 0x002c026c;
    *(volatile uint32_t*)0xa05f80e0 = 0x07d6a53f;

    uint32_t scan_area_size = horiz_active_area * vert_active_area;
    uint32_t scan_area_size_bytes = scan_area_size * bpp_mode_size; // This will always be divisible by 4

    // Reset framebuffer address
    *(volatile uint32_t*)0xa05f8050 = 0x00000000; // BootROM sets this to 0x00200000 (framebuffer base is 0xa5000000 + this)
    *(volatile uint32_t*)0xa05f8054 = horiz_active_area * bpp_mode_size; // This is for interlaced, resetting the offset gets us 2MB VRAM back after BootROM is done with it

    // zero out framebuffer area
    for(uint32_t pixel_or_two = 0; pixel_or_two < scan_area_size_bytes; pixel_or_two += 4)
    {
      *(uint32_t*)(0xa5000000 + pixel_or_two) = 0;
    }

    // re-enable video
    *(volatile uint32_t*)0xa05f80e8 &= ~8;
    *(volatile uint32_t*)0xa05f8044 |= 1;
  }
}
