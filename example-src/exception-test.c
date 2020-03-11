// Normally the correct header to use is "dcload-syscalls.h"
// This is just here for the purpose of this function, which is to force an FPU exception.
#include "dcload-syscall.h"

void main(void)
{
    __call_builtin_sh_set_fpscr(0x40001 | (1 << 10) ); // enable FPU exception (divide by 0)
    asm volatile ("fldi0 fr0\n\t" // load 0.0f
    "fldi1 fr1\n\t" // load 1.0f
    "fdiv fr0, fr1\n\t" // divide by 0
  );
}
