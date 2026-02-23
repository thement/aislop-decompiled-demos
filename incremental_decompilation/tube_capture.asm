[org 100h]
[segment .text]

SCREEN	equ	160
PIXBUF	equ	600h
PALBUF	equ	PIXBUF + 320 * SCREEN

	mov	al,13h
	int	10h

	push	word 0A000h
	pop	es
	mov	ax,cs
	add	ah,10h
	mov	fs,ax

	xor	cx,cx
PAL1	mov	dx,3C8h
	mov	ax,cx
	out	dx,al
	inc	dx
	sar	al,1
	js	PAL2
	out	dx,al
	mul	al
	shr	ax,6
	out	dx,al
PAL2	mov	al,0
	out	dx,al
	jns	PAL3
	sub	al,cl
	shr	al,1
	out	dx,al
	shr	al,1
	out	dx,al
PAL3	mov	bx,cx
	mov	[fs:bx],bl
	loop	PAL1

TEX	mov	bx,cx
	add	ax,cx
	rol	ax,cl
	mov	dh,al
	sar	dh,5
	adc	dl,dh
	adc	dl,[fs:bx+255]
	shr	dl,1
	mov	[fs:bx],dl
	not	bh
	mov	[fs:bx],dl
	loop	TEX

	fninit
	fldz

MAIN	add	bh,8
	mov	di,PIXBUF
	fadd	dword [di-PIXBUF+TEXUV-4]
	push	di

	mov	dx,-80
TUBEY	mov	bp,-160
TUBEX	mov	si,TEXUV
	fild	word [byte si-TEXUV+EYE]

	mov	[si],bp
	fild	word [si]
	mov	[si],dx
	fild	word [si]

	mov	cl,2
ROTATE	fld	st3
	fsincos
	fld	st2
	fmul	st0,st1
	fld	st4
	fmul	st0,st3
	fsubp	st1,st0
	fxch	st0,st3
	fmulp	st2,st0
	fmulp	st3,st0
	faddp	st2,st0
	fxch	st0,st2
	loop	ROTATE

	fld	st1
	fmul	st0,st0
	fld	st1
	fmul	st0,st0
	faddp	st1,st0
	fsqrt
	fdivp	st3,st0
	fpatan
	fimul	word [si-4]
	fistp	word [si]
	fimul	word [si-4]
	fistp	word [si+1]
	mov	si,[si]

	lea	ax,[bx+si]
	add	al,ah
	and	al,64
	mov	al,-5
	jz	STORE

	shl	si,2
	lea	ax,[bx+si]
	sub	al,ah
	mov	al,-16
	jns	STORE

	shl	si,1
	mov	al,-48
STORE	add	al,[fs:bx+si]
	add	[di],al
	inc	di

	inc	bp
	cmp	bp,160
EYE	equ	$-2
	jnz	TUBEX

	inc	dx
	cmp	dx,byte 80
	jnz	TUBEY

	pop	si
	mov	di,(100-SCREEN/2)*320
	mov	ch,(SCREEN/2)*320/256
	rep	movsw

	mov	ch,SCREEN*320/256
BLUR	dec	si
	sar	byte [si],2
	loop	BLUR

; --- frame capture (replaces keyboard check) ---
	call	save_frame
	inc	byte [frame_count]
	cmp	byte [frame_count],25
	jae	.exit
	jmp	MAIN
.exit:
	mov	al,03h
	int	10h
	int	20h

; --- original data block (referenced by FPU code) ---
	db	41,0,0C3h,3Ch
TEXUV	db	"baze"

; --- capture state ---
frame_count	db	0
fname		db	'frame000.bmp',0

; --- BMP file header (14 bytes) ---
bmp_filehdr:
	db	'B','M'
	dd	65078		; file size = 14+40+1024+64000
	dd	0		; reserved
	dd	1078		; pixel data offset = 14+40+1024

; --- BMP info header (40 bytes) ---
bmp_infohdr:
	dd	40		; header size
	dd	320		; width
	dd	200		; height
	dw	1		; color planes
	dw	8		; bits per pixel
	dd	0		; compression = BI_RGB
	dd	64000		; image size = 320*200
	dd	0		; X pixels/meter
	dd	0		; Y pixels/meter
	dd	256		; colors used
	dd	0		; important colors

; --- save current frame as BMP ---
save_frame:
	pusha

	; update filename digits from frame_count
	mov	al,[frame_count]
	aam			; AH=al/10  AL=al%10
	add	ax,3030h	; convert to ASCII
	mov	[fname+7],al	; ones digit
	mov	[fname+6],ah	; tens digit

	; read VGA palette into PALBUF (256 entries, BGRA)
	xor	al,al
	mov	dx,3C7h
	out	dx,al		; start reading from color 0
	mov	dx,3C9h
	mov	di,PALBUF
	mov	cx,256
.rpal:
	in	al,dx		; R (6-bit)
	shl	al,2
	mov	ah,al
	in	al,dx		; G (6-bit)
	shl	al,2
	mov	bl,al
	in	al,dx		; B (6-bit)
	shl	al,2
	mov	[di],al		; store B
	mov	[di+1],bl	; store G
	mov	[di+2],ah	; store R
	mov	byte [di+3],0	; padding
	add	di,4
	loop	.rpal

	; create file
	mov	ah,3Ch
	xor	cx,cx
	mov	dx,fname
	int	21h
	jc	.ret
	mov	bp,ax		; save file handle

	; write BMP file header (14 bytes)
	mov	bx,bp
	mov	ah,40h
	mov	cx,14
	mov	dx,bmp_filehdr
	int	21h

	; write BMP info header (40 bytes)
	mov	bx,bp
	mov	ah,40h
	mov	cx,40
	mov	dx,bmp_infohdr
	int	21h

	; write palette (1024 bytes)
	mov	bx,bp
	mov	ah,40h
	mov	cx,1024
	mov	dx,PALBUF
	int	21h

	; write pixel data bottom-to-top (200 rows x 320 bytes)
	mov	si,199
.row:
	push	ds
	push	si
	mov	ax,320
	mul	si		; AX = row * 320
	mov	dx,ax		; DS:DX = buffer for int 21h
	push	word 0A000h
	pop	ds		; DS = video memory segment
	mov	bx,bp
	mov	ah,40h
	mov	cx,320
	int	21h
	pop	si
	pop	ds
	dec	si
	jns	.row

	; close file
	mov	bx,bp
	mov	ah,3Eh
	int	21h

.ret:
	popa
	ret
