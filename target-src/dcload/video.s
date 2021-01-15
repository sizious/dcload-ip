
	! Video routines from Video example
	!

	.globl	_draw_string, _clrscr
	.globl	_get_font_address

	.text


	! Draw a text string on screen
	!
	! Assumes a 640*480 screen with RGB555 or RGB565 pixels

	! r4 = x
	! r5 = y
	! r6 = string
	! r7 = colour
_draw_string:
	mov.l	r14,@-r15
	sts	pr,r14
	mov.l	r13,@-r15
	mov.l	r12,@-r15
	mov.l	r11,@-r15
	mov.l	r10,@-r15
	mov	r4,r10
	mov	r5,r11
	mov	r6,r12
	mov	r7,r13
ds_loop:
	mov.b	@r12+,r6
	mov	r10,r4
	mov	r11,r5
	tst	r6,r6	! string is NUL terminated
	bt	ds_done
	extu.b	r6,r6	! undo sign-extension of char
	bsr	draw_char12
	mov	r13,r7
	bra	ds_loop
	add	#12,r10
ds_done:
	mov.l	@r15+,r10
	mov.l	@r15+,r11
	mov.l	@r15+,r12
	mov.l	@r15+,r13
	lds	r14,pr
	rts
	mov.l	@r15+,r14


	! Draw a "narrow" character on screen
	!
	! Assumes a 640*480 screen with RGB555 or RGB565 pixels

	! r4 = x
	! r5 = y
	! r6 = char
	! r7 = colour
draw_char12:
	! First get the address of the ROM font
	sts	pr,r3
	bsr	_get_font_address
	nop
	lds	r3,pr
	mov	r0,r2

	! Then, compute the destination address
	shll	r4
	mov	r5,r0
	shll2	r0
	add	r5,r0
	shll8	r0
	add	r4,r0
	mov.l	vrambase,r1
	add	r1,r0

	! Find right char in font
	mov	#32,r1
	cmp/gt	r1,r6
	bt	okchar1
	! <= 32 = space or unprintable
blank:
	mov	#72,r6	! Char # 72 in font is blank
	bra	decided
	shll2	r6
okchar1:
	mov	#127,r1
	cmp/ge	r1,r6
	bf/s	decided	! 33-126 = ASCII, Char # 1-94 in font
	add	#-32,r6
	cmp/gt	r1,r6
	bf	blank	! 127-159 = unprintable
	add	#-96,r6
	cmp/gt	r1,r6
	bt	blank	! 256- = ?
	! 160-255 = Latin 1, char # 96-191 in font
	add	#64,r6

	! Add offset of selected char to font addr
decided:
	mov	r6,r1
	shll2	r1
	shll	r1
	add	r6,r1
	shll2	r1
	add	r2,r1

	! Copy ROM data into cache so we can access it as bytes
	! Char data is 36 bytes, so we need to fetch two cache lines
	pref	@r1
	mov	r1,r2
	add	#32,r2
	pref	@r2

	mov	#24,r2	! char is 24 lines high
drawy:
	! Each pixel line is stored as 1.5 bytes, so we'll load
	! 3 bytes into r4 and draw two lines in one go
	mov.b	@r1+,r4
	shll8	r4
	mov.b	@r1+,r5
	extu.b	r5,r5
	or	r5,r4
	shll8	r4
	mov.b	@r1+,r5
	extu.b	r5,r5
	or	r5,r4
	shll8	r4
	! Even line
	mov	#12,r3
drawx1:
	rotl	r4
	bf/s	nopixel1
	dt	r3
	mov.w	r7,@r0	! Set pixel
nopixel1:
	bf/s	drawx1
	add	#2,r0
	mov.w	drawmod,r3
	dt	r2
	add	r3,r0
	! Odd line
	mov	#12,r3
drawx2:
	rotl	r4
	bf/s	nopixel2
	dt	r3
	mov.w	r7,@r0	! Set pixel
nopixel2:
	bf/s	drawx2
	add	#2,r0
	mov.w	drawmod,r3
	dt	r2
	bf/s	drawy
	add	r3,r0

	rts
	nop

drawmod:
	.word	2*(640-12)


	! Clear screen
	!
	! Assumes a 640*480 screen with RGB555 or RGB565 pixels

	! r4 = pixel colour
_clrscr:
	mov.l	vrambase,r0
	mov.l	clrcount,r1
clrloop:
	mov.w	r4,@r0	! clear one pixel
	dt	r1
	bf/s	clrloop
	add	#2,r0
	rts
	nop

	.align	2
vrambase:
	.long	0xa5000000
clrcount:
	.long	640*480


	! Return base address of ROM font
	!

_get_font_address:
	mov.l	syscall_b4,r0
	mov.l	@r0,r0
	jmp	@r0
	mov	#0,r1

	.align	2
syscall_b4:
	.long	0x8c0000b4


	.end
