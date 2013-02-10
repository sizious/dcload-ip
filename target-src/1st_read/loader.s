!   This file is part of dcload
!   Copyright (C) 2011-2013 Lawrence Sebald
!
!   This program is free software: you can redistribute it and/or modify
!   it under the terms of the GNU General Public License as published by
!   the Free Software Foundation; either version 2 of the License, or
!   (at your option) any later version.
!
!   This program is distributed in the hope that it will be useful,
!   but WITHOUT ANY WARRANTY; without even the implied warranty of
!   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
!   GNU General Public License for more details.
!
!   You should have received a copy of the GNU General Public License
!   along with this program.  If not, see <http://www.gnu.org/licenses/>.

!   This code is responsible for loading the main dcload binary into RAM at a
!   convenient location (0x8c004000) as well as the default exception handler
!   binary to its home (0x8c00f400).
    .text
    .balign     2
    .globl      start
start:
    ! Disable interrupts
    mov.l       init_sr, r0
    ldc         r0, sr
    mov.l       disable_cache, r4
    jsr         @r4
    nop
    ! Clear out the whole area
    mov.l       dcload_base, r2
    mov.l       dcload_max_sz, r1
    mov         #0, r0
cl_loop:
    dt          r1
    mov.l       r0, @r2
    bf/s        cl_loop
    add         #4, r2
    ! Copy the exception binary over to the appropriate location
    mov.l       exception_base, r2
    mov.l       exception_size, r1
    mov.l       exception_ptr, r0
ex_loop:
    mov.l       @r0+, r3
    dt          r1
    mov.l       r3, @r2
    bf/s        ex_loop
    add         #4, r2
    ! Copy the binary over to the appropriate location
    mov.l       dcload_base, r2
    mov.l       dcload_size, r1
    mov.l       dcload_ptr, r0
loop:
    mov.l       @r0+, r3
    dt          r1
    mov.l       r3, @r2
    bf/s        loop
    add         #4, r2
    ! Jump to the main dcload binary.
    mov.l       dcload_base, r4
    jmp         @r4
    nop

    .balign     4
init_sr:
    .long       0x500000f0
exception_size:
    .long       (exception_end - exception) >> 2
exception_ptr:
    .long       exception
exception_base:
    .long       0x8c00f400
dcload_size:
    .long       (dcload_end - dcload) >> 2
dcload_ptr:
    .long       dcload
dcload_base:
    .long       0x8c004000
disable_cache:
    .long       _disable_cache
dcload_max_sz:
    .long       (0x8c010000 - 0x8c004000) >> 2

! Include the binaries here, making sure they're aligned to 4 byte boundaries
! and that they're a multiple of 4 bytes in size.
    .section    .rodata
    .balign     4
dcload:
    .incbin     "dcload.bin"
    .balign     4
dcload_end:

    .section    .rodata
    .balign     4
exception:
    .incbin     "exception.bin"
    .balign     4
exception_end:
