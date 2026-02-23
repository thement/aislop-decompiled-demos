#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dosemu.h"

#define SCREEN  160
#define PIXBUF  0x204
#define TEXUV   0x1FC
#define EYE     0x1D2

int main(void)
{
	dos_t state;
	memset(&state, 0, sizeof(state));
	dos_t *s = &state;

	/* load original COM binary — puts code/data at 0x100 */
	FILE *bin = fopen("tube_orig.com", "rb");
	if (!bin) { perror("tube_orig.com"); return 1; }
	fread(s->mem + 0x100, 1, 256, bin);
	fclose(bin);

	s->sp = 0xFFFE;
	int frame_count = 0;

	/* ---- mov al, 13h ---- */
	asm_mov_i(s, AL, 0x13);
	/* ---- int 10h (set mode 13h: clear screen) ---- */
	memset(s->vga, 0, 65536);

	/* ---- push word 0A000h / pop es ---- */
	asm_push_i(s, 0xA000);
	/* pop es — just consume stack, ES is virtual */
	asm_pop_val(s);

	/* ---- mov ax, cs ---- */
	s->ax = 0; /* cs_val = 0 for emulation */
	/* ---- add ah, 10h ---- */
	asm_add_i(s, AH, 0x10);
	/* ---- mov fs, ax ---- */
	/* fs is virtual, fsmem[] is separate */

	/* ---- xor cx, cx ---- */
	asm_xor(s, CX, CX);

PAL1:
	/* mov dx, 3C8h */
	asm_mov_i(s, DX, 0x3C8);
	/* mov ax, cx */
	asm_mov(s, AX, CX);
	/* out dx, al */
	asm_out(s, s->dx, s->al);
	/* inc dx */
	asm_inc(s, DX);
	/* sar al, 1 */
	asm_sar(s, AL, 1);
	/* js PAL2 */
	if (s->sf) goto PAL2;
	/* out dx, al */
	asm_out(s, s->dx, s->al);
	/* mul al */
	asm_mul(s, AL);
	/* shr ax, 6 */
	asm_shr(s, AX, 6);
	/* out dx, al */
	asm_out(s, s->dx, s->al);
PAL2:
	/* mov al, 0 */
	asm_mov_i(s, AL, 0);
	/* out dx, al */
	asm_out(s, s->dx, s->al);
	/* jns PAL3 */
	if (!s->sf) goto PAL3;
	/* sub al, cl */
	asm_sub(s, AL, CL);
	/* shr al, 1 */
	asm_shr(s, AL, 1);
	/* out dx, al */
	asm_out(s, s->dx, s->al);
	/* shr al, 1 */
	asm_shr(s, AL, 1);
	/* out dx, al */
	asm_out(s, s->dx, s->al);
PAL3:
	/* mov bx, cx */
	asm_mov(s, BX, CX);
	/* mov [fs:bx], bl */
	asm_mov_m_r(s, SEG_FS, s->bx, BL);
	/* loop PAL1 */
	if (--s->cx) goto PAL1;

TEX:
	/* mov bx, cx */
	asm_mov(s, BX, CX);
	/* add ax, cx */
	asm_add(s, AX, CX);
	/* rol ax, cl */
	asm_rol(s, AX, s->cl);
	/* mov dh, al */
	asm_mov(s, DH, AL);
	/* sar dh, 5 */
	asm_sar(s, DH, 5);
	/* adc dl, dh */
	asm_adc(s, DL, DH);
	/* adc dl, [fs:bx+255] */
	asm_adc_r_m(s, DL, SEG_FS, (uint16_t)(s->bx + 255));
	/* shr dl, 1 */
	asm_shr(s, DL, 1);
	/* mov [fs:bx], dl */
	asm_mov_m_r(s, SEG_FS, s->bx, DL);
	/* not bh */
	asm_not(s, BH);
	/* mov [fs:bx], dl */
	asm_mov_m_r(s, SEG_FS, s->bx, DL);
	/* loop TEX */
	if (--s->cx) goto TEX;

	/* ---- fninit ---- */
	asm_fninit(s);
	/* ---- fldz ---- */
	asm_fldz(s);

MAIN:
	/* add bh, 8 */
	asm_add_i(s, BH, 8);
	/* mov di, PIXBUF */
	asm_mov_i(s, DI, PIXBUF);
	/* fadd dword [byte di-PIXBUF+TEXUV-4] */
	asm_fadd_m32(s, SEG_DS, (uint16_t)(s->di - PIXBUF + TEXUV - 4));
	/* push di */
	asm_push(s, DI);

	/* mov dx, -80 */
	asm_mov_i(s, DX, (uint16_t)(int16_t)-80);

TUBEY:
	/* mov bp, -160 */
	asm_mov_i(s, BP, (uint16_t)(int16_t)-160);

TUBEX:
	/* mov si, TEXUV */
	asm_mov_i(s, SI, TEXUV);
	/* fild word [byte si-TEXUV+EYE] */
	asm_fild_m16(s, SEG_DS, (uint16_t)(s->si - TEXUV + EYE));

	/* mov [si], bp */
	asm_mov_m_r(s, SEG_DS, s->si, BP);
	/* fild word [si] */
	asm_fild_m16(s, SEG_DS, s->si);
	/* mov [si], dx */
	asm_mov_m_r(s, SEG_DS, s->si, DX);
	/* fild word [si] */
	asm_fild_m16(s, SEG_DS, s->si);

	/* mov cl, 2 */
	asm_mov_i(s, CL, 2);

ROTATE:
	/* fld st3 */
	asm_fld_st(s, 3);
	/* fsincos */
	asm_fsincos(s);
	/* fld st2 */
	asm_fld_st(s, 2);
	/* fmul st0, st1 */
	asm_fmul_st(s, 0, 1);
	/* fld st4 */
	asm_fld_st(s, 4);
	/* fmul st0, st3 */
	asm_fmul_st(s, 0, 3);
	/* fsubp st1, st0 */
	asm_fsubp_st(s, 1, 0);
	/* fxch st0, st3 */
	asm_fxch(s, 3);
	/* fmulp st2, st0 */
	asm_fmulp_st(s, 2, 0);
	/* fmulp st3, st0 */
	asm_fmulp_st(s, 3, 0);
	/* faddp st2, st0 */
	asm_faddp_st(s, 2, 0);
	/* fxch st0, st2 */
	asm_fxch(s, 2);
	/* loop ROTATE */
	if (--s->cx) goto ROTATE;

	/* fld st1 */
	asm_fld_st(s, 1);
	/* fmul st0, st0 */
	asm_fmul_st(s, 0, 0);
	/* fld st1 */
	asm_fld_st(s, 1);
	/* fmul st0, st0 */
	asm_fmul_st(s, 0, 0);
	/* faddp st1, st0 */
	asm_faddp_st(s, 1, 0);
	/* fsqrt */
	asm_fsqrt(s);
	/* fdivp st3, st0 */
	asm_fdivp_st(s, 3, 0);
	/* fpatan */
	asm_fpatan(s);
	/* fimul word [si-4] */
	asm_fimul_m16(s, SEG_DS, (uint16_t)(s->si - 4));
	/* fistp word [si] */
	asm_fistp_m16(s, SEG_DS, s->si);
	/* fimul word [si-4] */
	asm_fimul_m16(s, SEG_DS, (uint16_t)(s->si - 4));
	/* fistp word [si+1] */
	asm_fistp_m16(s, SEG_DS, (uint16_t)(s->si + 1));
	/* mov si, [si] */
	asm_mov_r_m(s, SI, SEG_DS, s->si);

	/* lea ax, [bx+si] */
	asm_lea(s, AX, (uint16_t)(s->bx + s->si));
	/* add al, ah */
	asm_add(s, AL, AH);
	/* and al, 64 */
	asm_and_i(s, AL, 64);
	/* mov al, -5 */
	asm_mov_i(s, AL, (uint8_t)-5);
	/* jz STORE */
	if (s->zf) goto STORE;

	/* shl si, 2 */
	asm_shl(s, SI, 2);
	/* lea ax, [bx+si] */
	asm_lea(s, AX, (uint16_t)(s->bx + s->si));
	/* sub al, ah */
	asm_sub(s, AL, AH);
	/* mov al, -16 */
	asm_mov_i(s, AL, (uint8_t)-16);
	/* jns STORE */
	if (!s->sf) goto STORE;

	/* shl si, 1 */
	asm_shl(s, SI, 1);
	/* mov al, -48 */
	asm_mov_i(s, AL, (uint8_t)-48);

STORE:
	/* add al, [fs:bx+si] */
	asm_add_r_m(s, AL, SEG_FS, (uint16_t)(s->bx + s->si));
	/* add [di], al */
	asm_add_m_r(s, SEG_DS, s->di, AL);
	/* inc di */
	asm_inc(s, DI);

	/* inc bp */
	asm_inc(s, BP);
	/* cmp bp, 160 */
	asm_cmp_i(s, BP, 160);
	/* jnz TUBEX */
	if (!s->zf) goto TUBEX;

	/* inc dx */
	asm_inc(s, DX);
	/* cmp dx, byte 80 */
	asm_cmp_i(s, DX, 80);
	/* jnz TUBEY */
	if (!s->zf) goto TUBEY;

	/* pop si */
	asm_pop(s, SI);
	/* mov di, (100-SCREEN/2)*320 */
	asm_mov_i(s, DI, (100 - SCREEN/2) * 320);
	/* mov ch, (SCREEN/2)*320/256 */
	asm_mov_i(s, CH, (SCREEN/2) * 320 / 256);
	/* rep movsw */
	asm_rep_movsw(s);

	/* mov ch, SCREEN*320/256 */
	asm_mov_i(s, CH, SCREEN * 320 / 256);

BLUR:
	/* dec si */
	asm_dec(s, SI);
	/* sar byte [si], 2 */
	asm_sar_m8(s, SEG_DS, s->si, 2);
	/* loop BLUR */
	if (--s->cx) goto BLUR;

	/* ---- frame capture (in place of "in al, 60h") ---- */
	{
		char fname[32];
		sprintf(fname, "cframe%03d.bmp", frame_count);
		save_bmp(s, fname);
		printf("saved %s\n", fname);
	}
	frame_count++;
	if (frame_count >= 25)
		goto DONE;
	/* jmp MAIN */
	goto MAIN;

DONE:
	printf("done — %d frames captured\n", frame_count);
	return 0;
}
