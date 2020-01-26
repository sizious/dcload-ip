// This file just contains some C code that is required by crt0.S.
// Based on the DreamHAL one, but adapted for dcload purposes.
// --Moopthehedgehog

#include "dcload-syscall.h"

// crt0.S needs this to set FPSCR since GCC deprecated __set_fpscr and
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
