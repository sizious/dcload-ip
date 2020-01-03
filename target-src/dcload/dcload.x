/* Sega Dreamcast linker script */

OUTPUT_FORMAT("elf32-shl", "elf32-shl",
	      "elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(start)
 SEARCH_DIR(/usr/local/dcdev/sh-elf/lib);
/* Do we need any of these for elf?
   __DYNAMIC = 0;    */

MEMORY
{
  ram (rwx) : ORIGIN = 0x8c004000, LENGTH = 0xb400
}

SECTIONS
{
  /* Read-only sections, merged into text segment: */
/*  . = 0x1000;*/
  .interp     : { *(.interp) 	}
  .hash          : { *(.hash)		}
  .dynsym        : { *(.dynsym)		}
  .dynstr        : { *(.dynstr)		}
  .gnu.version   : { *(.gnu.version)	}
  .gnu.version_d   : { *(.gnu.version_d)	}
  .gnu.version_r   : { *(.gnu.version_r)	}
  .rel.init      : { *(.rel.init)	}
  .rela.init     : { *(.rela.init)	}
  .rel.text      :
    {
      *(.rel.text)
      *(.rel.text.*)
      *(.rel.gnu.linkonce.t*)
    }
  .rela.text     :
    {
      *(.rela.text)
      *(.rela.text.*)
      *(.rela.gnu.linkonce.t*)
    }
  .rel.fini      : { *(.rel.fini)	}
  .rela.fini     : { *(.rela.fini)	}
  .rel.rodata    :
    {
      *(.rel.rodata)
      *(.rel.rodata.*)
      *(.rel.gnu.linkonce.r*)
    }
  .rela.rodata   :
    {
      *(.rela.rodata)
      *(.rela.rodata.*)
      *(.rela.gnu.linkonce.r*)
    }
  .rel.data      :
    {
      *(.rel.data)
      *(.rel.data.*)
      *(.rel.gnu.linkonce.d*)
    }
  .rela.data     :
    {
      *(.rela.data)
      *(.rela.data.*)
      *(.rela.gnu.linkonce.d*)
    }
  .rel.ctors     : { *(.rel.ctors)	}
  .rela.ctors    : { *(.rela.ctors)	}
  .rel.dtors     : { *(.rel.dtors)	}
  .rela.dtors    : { *(.rela.dtors)	}
  .rel.got       : { *(.rel.got)		}
  .rela.got      : { *(.rela.got)		}
  .rel.sdata     :
    {
      *(.rel.sdata)
      *(.rel.sdata.*)
      *(.rel.gnu.linkonce.s*)
    }
  .rela.sdata     :
    {
      *(.rela.sdata)
      *(.rela.sdata.*)
      *(.rela.gnu.linkonce.s*)
    }
  .rel.sbss      : { *(.rel.sbss)		}
  .rela.sbss     : { *(.rela.sbss)	}
  .rel.bss       : { *(.rel.bss)		}
  .rela.bss      : { *(.rela.bss)		}
  .rel.plt       : { *(.rel.plt)		}
  .rela.plt      : { *(.rela.plt)		}
  .init          :
  {
    KEEP (*(.init))
  } =0
  .plt      : { *(.plt)	}
  .text      :
  {
    *(.text)
    *(.text.*)
    *(.stub)
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)
    *(.gnu.linkonce.t*)
  } =0
  _etext = .;
  PROVIDE (etext = .);
  .fini   ALIGN(4):
  {
    KEEP (*(.fini))
  } =0
  .rodata    ALIGN(4): { *(.rodata) *(.rodata.*) *(.gnu.linkonce.r*) }
  .rodata1   ALIGN(4): { *(.rodata1) }
  /* Adjust the address for the data segment.  We want to adjust up to
     the same address within the page on the next page up.  */
	/* Actually, we don't. There's no elf loader, so there can be only one PT_LOAD
	   area (more if the PT_LOAD areas are directly adjacent, which we need here) */
  /* . = ALIGN(128) + (. & (128 - 1)); */
  .data   ALIGN(4):
  {
    *(.data)
    *(.data.*)
    *(.gnu.linkonce.d*)
    SORT(CONSTRUCTORS)
  }
  .data1   ALIGN(4): { *(.data1) }
  .eh_frame   ALIGN(4): { *(.eh_frame) }
  .gcc_except_table   ALIGN(4): { *(.gcc_except_table) }
  .ctors   ALIGN(4):
  {
    ___ctors = .;
    /* gcc uses crtbegin.o to find the start of
       the constructors, so we make sure it is
       first.  Because this is a wildcard, it
       doesn't matter if the user does not
       actually link against crtbegin.o; the
       linker won't look for a file to match a
       wildcard.  The wildcard also means that it
       doesn't matter which directory crtbegin.o
       is in.  */
    KEEP (*crtbegin.o(.ctors))
    /* We don't want to include the .ctor section from
       from the crtend.o file until after the sorted ctors.
       The .ctor section from the crtend file contains the
       end of ctors marker and it must be last */
    KEEP (*(EXCLUDE_FILE (*crtend.o ) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    ___ctors_end = .;
  }
   .dtors   ALIGN(4):
  {
    ___dtors = .;
    KEEP (*crtbegin.o(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.o ) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
    ___dtors_end = .;
  }
  .got       ALIGN(4): { *(.got.plt) *(.got) }
  .dynamic   ALIGN(4): { *(.dynamic) }
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata   ALIGN(4):
  {
    *(.sdata)
    *(.sdata.*)
    *(.gnu.linkonce.s.*)
  }
	/* Longword-align (4-byte align) _edata since it gets zeroed by a
	longword-aligned instruction. */
	. = ALIGN(32 / 8);
  _edata = .;
  PROVIDE (edata = .);
  __bss_start = .;
  .sbss   ALIGN(4):
  {
    *(.dynsbss)
    *(.sbss)
    *(.sbss.*)
    *(.scommon)
  }
  .bss   ALIGN(4):
  {
   *(.dynbss)
   *(.bss)
   *(.bss.*)
   *(COMMON)
   /* Align here to ensure that the .bss section occupies space up to
      _end.  Align after .bss to ensure correct alignment even if the
      .bss section disappears because there are no input sections.  */
   . = ALIGN(32 / 8);
  }
  . = ALIGN(32 / 8);
  _end = .;
  PROVIDE (end = .);
  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
/*  .stack 0x8c00f400 : { _stack = .; *(.stack) }*/
  /* These must appear regardless of  .  */
_stack = 0x8c00f400;

/* ASSERT triggers when condition is FALSE */
/* Stack calculation will wrap around if ram region overflows, so only one
   error displays at a time :) */
ASSERT(( (_stack - _end) > 800 ), "Error: Not enough stack space: need at least 800 bytes")
ASSERT(( _end <= (ORIGIN(ram) + LENGTH(ram)) ), "Error: Region 'ram' overflowed: try '-Os' or shrink code size")
/* For some reason LD's normal overflow checks don't run when sections are
   explicitly aligned, so these asserts are here to make up for it. */
}
