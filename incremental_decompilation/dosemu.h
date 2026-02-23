#ifndef DOSEMU_H
#define DOSEMU_H

#include <stdint.h>
#include <string.h>
#include <math.h>

/* --- register identifiers --- */
typedef enum {
	AL, AH, BL, BH, CL, CH, DL, DH,   /* 8-bit  (0-7)  */
	AX, BX, CX, DX, SI, DI, BP, SP     /* 16-bit (8-15) */
} reg_t;

/* --- segment identifiers --- */
typedef enum { SEG_DS, SEG_ES, SEG_FS } seg_t;

/* --- machine state --- */
typedef struct {
	/* general-purpose registers (little-endian unions) */
	union { uint16_t ax; struct { uint8_t al, ah; }; };
	union { uint16_t bx; struct { uint8_t bl, bh; }; };
	union { uint16_t cx; struct { uint8_t cl, ch; }; };
	union { uint16_t dx; struct { uint8_t dl, dh; }; };
	uint16_t si, di, bp, sp;

	/* flags */
	int cf, zf, sf, of;

	/* memory */
	uint8_t mem[65536];        /* DS segment (code+data+pixbuf) */
	uint8_t vga[65536];        /* ES segment (A000h video mem)  */
	uint8_t fsmem[65536];      /* FS segment (texture data)     */

	/* VGA DAC palette */
	uint8_t pal[768];          /* 256 * RGB, 6-bit values       */
	int     pal_widx;          /* write index * 3 + component   */
	int     pal_ridx;          /* read  index * 3 + component   */

	/* x87 FPU */
	double fpu[8];
	int    fpu_top;

	/* stack lives in mem[] at high addresses */
} dos_t;

/* ================================================================ */
/*  register access helpers                                         */
/* ================================================================ */

static inline int reg_is8(reg_t r) { return r <= DH; }

static inline uint16_t reg_get(dos_t *s, reg_t r) {
	switch (r) {
	case AL: return s->al; case AH: return s->ah;
	case BL: return s->bl; case BH: return s->bh;
	case CL: return s->cl; case CH: return s->ch;
	case DL: return s->dl; case DH: return s->dh;
	case AX: return s->ax; case BX: return s->bx;
	case CX: return s->cx; case DX: return s->dx;
	case SI: return s->si; case DI: return s->di;
	case BP: return s->bp; case SP: return s->sp;
	}
	return 0;
}

static inline void reg_set(dos_t *s, reg_t r, uint16_t v) {
	switch (r) {
	case AL: s->al = v; break; case AH: s->ah = v; break;
	case BL: s->bl = v; break; case BH: s->bh = v; break;
	case CL: s->cl = v; break; case CH: s->ch = v; break;
	case DL: s->dl = v; break; case DH: s->dh = v; break;
	case AX: s->ax = v; break; case BX: s->bx = v; break;
	case CX: s->cx = v; break; case DX: s->dx = v; break;
	case SI: s->si = v; break; case DI: s->di = v; break;
	case BP: s->bp = v; break; case SP: s->sp = v; break;
	}
}

/* ================================================================ */
/*  memory access                                                   */
/* ================================================================ */

static inline uint8_t *seg_ptr(dos_t *s, seg_t seg) {
	switch (seg) {
	case SEG_DS: return s->mem;
	case SEG_ES: return s->vga;
	case SEG_FS: return s->fsmem;
	}
	return s->mem;
}

static inline uint8_t  mem_r8 (dos_t *s, seg_t g, uint16_t a)
	{ return seg_ptr(s,g)[a]; }
static inline uint16_t mem_r16(dos_t *s, seg_t g, uint16_t a)
	{ uint8_t *p=seg_ptr(s,g); return p[a]|((uint16_t)p[(uint16_t)(a+1)]<<8); }
static inline void mem_w8 (dos_t *s, seg_t g, uint16_t a, uint8_t  v)
	{ seg_ptr(s,g)[a]=v; }
static inline void mem_w16(dos_t *s, seg_t g, uint16_t a, uint16_t v)
	{ uint8_t *p=seg_ptr(s,g); p[a]=v&0xFF; p[(uint16_t)(a+1)]=v>>8; }

static inline float mem_rf32(dos_t *s, seg_t g, uint16_t a) {
	uint32_t v = seg_ptr(s,g)[a]
		| ((uint32_t)seg_ptr(s,g)[(uint16_t)(a+1)]<<8)
		| ((uint32_t)seg_ptr(s,g)[(uint16_t)(a+2)]<<16)
		| ((uint32_t)seg_ptr(s,g)[(uint16_t)(a+3)]<<24);
	float f; memcpy(&f, &v, 4); return f;
}

/* ================================================================ */
/*  flag helpers                                                    */
/* ================================================================ */

static inline void flags8(dos_t *s, uint8_t r, int cf) {
	s->cf = cf; s->zf = (r==0); s->sf = (r>>7)&1;
}
static inline void flags16(dos_t *s, uint16_t r, int cf) {
	s->cf = cf; s->zf = (r==0); s->sf = (r>>15)&1;
}

/* ================================================================ */
/*  integer instructions                                            */
/* ================================================================ */

/* mov reg, reg */
static inline void asm_mov(dos_t *s, reg_t d, reg_t src) {
	reg_set(s, d, reg_get(s, src));
}

/* mov reg, imm */
static inline void asm_mov_i(dos_t *s, reg_t d, uint16_t v) {
	reg_set(s, d, v);
}

/* mov reg, [seg:addr] */
static inline void asm_mov_r_m(dos_t *s, reg_t d, seg_t g, uint16_t a) {
	if (reg_is8(d)) reg_set(s, d, mem_r8(s,g,a));
	else            reg_set(s, d, mem_r16(s,g,a));
}

/* mov [seg:addr], reg (auto size) */
static inline void asm_mov_m_r(dos_t *s, seg_t g, uint16_t a, reg_t src) {
	if (reg_is8(src)) mem_w8(s,g,a, (uint8_t)reg_get(s,src));
	else              mem_w16(s,g,a, reg_get(s,src));
}

/* xor reg, reg */
static inline void asm_xor(dos_t *s, reg_t d, reg_t src) {
	uint16_t r = reg_get(s,d) ^ reg_get(s,src);
	reg_set(s,d,r);
	if (reg_is8(d)) flags8(s,(uint8_t)r,0); else flags16(s,r,0);
	s->of = 0;
}

/* add reg, reg */
static inline void asm_add(dos_t *s, reg_t d, reg_t src) {
	uint16_t a = reg_get(s,d), b = reg_get(s,src);
	if (reg_is8(d)) {
		uint16_t r = (uint8_t)a + (uint8_t)b;
		reg_set(s,d,(uint8_t)r);
		flags8(s,(uint8_t)r, r>0xFF);
	} else {
		uint32_t r = (uint32_t)a + b;
		reg_set(s,d,(uint16_t)r);
		flags16(s,(uint16_t)r, r>0xFFFF);
	}
}

/* add reg, imm */
static inline void asm_add_i(dos_t *s, reg_t d, uint16_t v) {
	uint16_t a = reg_get(s,d);
	if (reg_is8(d)) {
		uint16_t r = (uint8_t)a + (uint8_t)v;
		reg_set(s,d,(uint8_t)r);
		flags8(s,(uint8_t)r, r>0xFF);
	} else {
		uint32_t r = (uint32_t)a + v;
		reg_set(s,d,(uint16_t)r);
		flags16(s,(uint16_t)r, r>0xFFFF);
	}
}

/* add reg, [seg:addr] */
static inline void asm_add_r_m(dos_t *s, reg_t d, seg_t g, uint16_t a) {
	uint16_t dv = reg_get(s,d);
	if (reg_is8(d)) {
		uint16_t r = (uint8_t)dv + mem_r8(s,g,a);
		reg_set(s,d,(uint8_t)r);
		flags8(s,(uint8_t)r, r>0xFF);
	} else {
		uint32_t r = (uint32_t)dv + mem_r16(s,g,a);
		reg_set(s,d,(uint16_t)r);
		flags16(s,(uint16_t)r, r>0xFFFF);
	}
}

/* add [seg:addr], reg (byte) */
static inline void asm_add_m_r(dos_t *s, seg_t g, uint16_t a, reg_t src) {
	if (reg_is8(src)) {
		uint16_t r = mem_r8(s,g,a) + (uint8_t)reg_get(s,src);
		mem_w8(s,g,a,(uint8_t)r);
		flags8(s,(uint8_t)r, r>0xFF);
	} else {
		uint32_t r = (uint32_t)mem_r16(s,g,a) + reg_get(s,src);
		mem_w16(s,g,a,(uint16_t)r);
		flags16(s,(uint16_t)r, r>0xFFFF);
	}
}

/* sub reg, reg */
static inline void asm_sub(dos_t *s, reg_t d, reg_t src) {
	uint16_t a = reg_get(s,d), b = reg_get(s,src);
	if (reg_is8(d)) {
		uint16_t r = (uint8_t)a - (uint8_t)b;
		reg_set(s,d,(uint8_t)r);
		flags8(s,(uint8_t)r, (uint8_t)a < (uint8_t)b);
	} else {
		uint32_t r = (uint32_t)a - b;
		reg_set(s,d,(uint16_t)r);
		flags16(s,(uint16_t)r, a < b);
	}
}

/* cmp reg, reg */
static inline void asm_cmp(dos_t *s, reg_t a, reg_t b) {
	uint16_t av = reg_get(s,a), bv = reg_get(s,b);
	if (reg_is8(a)) {
		uint8_t r = (uint8_t)av - (uint8_t)bv;
		flags8(s, r, (uint8_t)av < (uint8_t)bv);
	} else {
		uint16_t r = av - bv;
		flags16(s, r, av < bv);
	}
}

/* cmp reg, imm */
static inline void asm_cmp_i(dos_t *s, reg_t a, uint16_t v) {
	uint16_t av = reg_get(s,a);
	if (reg_is8(a)) {
		uint8_t r = (uint8_t)av - (uint8_t)v;
		flags8(s, r, (uint8_t)av < (uint8_t)v);
	} else {
		uint16_t r = av - v;
		flags16(s, r, av < v);
	}
}

/* and reg, imm */
static inline void asm_and_i(dos_t *s, reg_t d, uint16_t v) {
	uint16_t r = reg_get(s,d) & v;
	reg_set(s,d,r);
	if (reg_is8(d)) flags8(s,(uint8_t)r,0); else flags16(s,r,0);
	s->of = 0;
}

/* adc reg, reg */
static inline void asm_adc(dos_t *s, reg_t d, reg_t src) {
	uint16_t a = reg_get(s,d), b = reg_get(s,src);
	if (reg_is8(d)) {
		uint16_t r = (uint8_t)a + (uint8_t)b + s->cf;
		reg_set(s,d,(uint8_t)r);
		flags8(s,(uint8_t)r, r>0xFF);
	} else {
		uint32_t r = (uint32_t)a + b + s->cf;
		reg_set(s,d,(uint16_t)r);
		flags16(s,(uint16_t)r, r>0xFFFF);
	}
}

/* adc reg, [seg:addr] */
static inline void asm_adc_r_m(dos_t *s, reg_t d, seg_t g, uint16_t a) {
	uint16_t dv = reg_get(s,d);
	if (reg_is8(d)) {
		uint16_t r = (uint8_t)dv + mem_r8(s,g,a) + s->cf;
		reg_set(s,d,(uint8_t)r);
		flags8(s,(uint8_t)r, r>0xFF);
	} else {
		uint32_t r = (uint32_t)dv + mem_r16(s,g,a) + s->cf;
		reg_set(s,d,(uint16_t)r);
		flags16(s,(uint16_t)r, r>0xFFFF);
	}
}

/* shr reg, count */
static inline void asm_shr(dos_t *s, reg_t d, int cnt) {
	uint16_t v = reg_get(s,d);
	if (reg_is8(d)) {
		uint8_t x = (uint8_t)v;
		int cf = cnt ? (x >> (cnt-1)) & 1 : s->cf;
		x >>= cnt;
		reg_set(s,d,x);
		flags8(s,x,cf);
	} else {
		int cf = cnt ? (v >> (cnt-1)) & 1 : s->cf;
		v >>= cnt;
		reg_set(s,d,v);
		flags16(s,v,cf);
	}
}

/* shl reg, count */
static inline void asm_shl(dos_t *s, reg_t d, int cnt) {
	uint16_t v = reg_get(s,d);
	if (reg_is8(d)) {
		uint16_t x = (uint8_t)v;
		int cf = cnt ? (x >> (8-cnt)) & 1 : s->cf;
		x <<= cnt;
		reg_set(s,d,(uint8_t)x);
		flags8(s,(uint8_t)x,cf);
	} else {
		uint32_t x = v;
		int cf = cnt ? (x >> (16-cnt)) & 1 : s->cf;
		x <<= cnt;
		reg_set(s,d,(uint16_t)x);
		flags16(s,(uint16_t)x,cf);
	}
}

/* sar reg, count */
static inline void asm_sar(dos_t *s, reg_t d, int cnt) {
	uint16_t v = reg_get(s,d);
	if (reg_is8(d)) {
		int8_t x = (int8_t)(uint8_t)v;
		int cf = cnt ? (x >> (cnt-1)) & 1 : s->cf;
		x >>= cnt;
		reg_set(s,d,(uint8_t)x);
		flags8(s,(uint8_t)x,cf);
	} else {
		int16_t x = (int16_t)v;
		int cf = cnt ? (x >> (cnt-1)) & 1 : s->cf;
		x >>= cnt;
		reg_set(s,d,(uint16_t)x);
		flags16(s,(uint16_t)x,cf);
	}
}

/* sar byte [seg:addr], count */
static inline void asm_sar_m8(dos_t *s, seg_t g, uint16_t a, int cnt) {
	int8_t x = (int8_t)mem_r8(s,g,a);
	int cf = cnt ? (x >> (cnt-1)) & 1 : s->cf;
	x >>= cnt;
	mem_w8(s,g,a,(uint8_t)x);
	flags8(s,(uint8_t)x,cf);
}

/* rol reg, count */
static inline void asm_rol(dos_t *s, reg_t d, int cnt) {
	uint16_t v = reg_get(s,d);
	if (reg_is8(d)) {
		uint8_t x = (uint8_t)v;
		cnt &= 7;
		x = (x << cnt) | (x >> (8-cnt));
		reg_set(s,d,x);
		s->cf = x & 1;
	} else {
		cnt &= 15;
		v = (v << cnt) | (v >> (16-cnt));
		reg_set(s,d,v);
		s->cf = v & 1;
	}
}

/* inc reg (does NOT change CF) */
static inline void asm_inc(dos_t *s, reg_t d) {
	uint16_t v = reg_get(s,d) + 1;
	reg_set(s,d,v);
	if (reg_is8(d)) { s->zf=((uint8_t)v==0); s->sf=((uint8_t)v>>7)&1; }
	else            { s->zf=(v==0);           s->sf=(v>>15)&1;         }
}

/* dec reg (does NOT change CF) */
static inline void asm_dec(dos_t *s, reg_t d) {
	uint16_t v = reg_get(s,d) - 1;
	reg_set(s,d,v);
	if (reg_is8(d)) { s->zf=((uint8_t)v==0); s->sf=((uint8_t)v>>7)&1; }
	else            { s->zf=(v==0);           s->sf=(v>>15)&1;         }
}

/* not reg (no flags) */
static inline void asm_not(dos_t *s, reg_t d) {
	reg_set(s, d, ~reg_get(s,d));
}

/* mul src8: AX = AL * src8 ;  mul src16: DX:AX = AX * src16 */
static inline void asm_mul(dos_t *s, reg_t src) {
	if (reg_is8(src)) {
		uint16_t r = (uint16_t)s->al * (uint8_t)reg_get(s,src);
		s->ax = r;
		s->cf = s->of = (s->ah != 0);
	} else {
		uint32_t r = (uint32_t)s->ax * reg_get(s,src);
		s->ax = (uint16_t)r;
		s->dx = (uint16_t)(r >> 16);
		s->cf = s->of = (s->dx != 0);
	}
}

/* lea dst, value (no flags) */
static inline void asm_lea(dos_t *s, reg_t d, uint16_t v) {
	reg_set(s,d,v);
}

/* cbw: sign-extend AL -> AX */
static inline void asm_cbw(dos_t *s) {
	s->ax = (uint16_t)(int16_t)(int8_t)s->al;
}

/* ================================================================ */
/*  stack                                                           */
/* ================================================================ */

static inline void asm_push(dos_t *s, reg_t r) {
	s->sp -= 2;
	mem_w16(s, SEG_DS, s->sp, reg_get(s,r));
}

static inline void asm_push_i(dos_t *s, uint16_t v) {
	s->sp -= 2;
	mem_w16(s, SEG_DS, s->sp, v);
}

static inline void asm_pop(dos_t *s, reg_t r) {
	reg_set(s, r, mem_r16(s, SEG_DS, s->sp));
	s->sp += 2;
}

static inline uint16_t asm_pop_val(dos_t *s) {
	uint16_t v = mem_r16(s, SEG_DS, s->sp);
	s->sp += 2;
	return v;
}

/* ================================================================ */
/*  string ops                                                      */
/* ================================================================ */

/* rep movsw: DS:SI -> ES:DI, CX words */
static inline void asm_rep_movsw(dos_t *s) {
	while (s->cx) {
		mem_w16(s, SEG_ES, s->di, mem_r16(s, SEG_DS, s->si));
		s->si += 2;
		s->di += 2;
		s->cx--;
	}
}

/* ================================================================ */
/*  I/O ports (VGA palette)                                         */
/* ================================================================ */

static inline void asm_out(dos_t *s, uint16_t port, uint8_t val) {
	if (port == 0x3C8) {
		/* DAC write address */
		s->pal_widx = val * 3;
	} else if (port == 0x3C9) {
		/* DAC data write */
		s->pal[s->pal_widx] = val & 0x3F;
		s->pal_widx++;
		if (s->pal_widx >= 768) s->pal_widx = 0;
	}
}

static inline uint8_t asm_in(dos_t *s, uint16_t port) {
	if (port == 0x3C7) {
		return 0; /* not really used */
	} else if (port == 0x3C9) {
		uint8_t v = s->pal[s->pal_ridx];
		s->pal_ridx++;
		if (s->pal_ridx >= 768) s->pal_ridx = 0;
		return v;
	}
	return 0;
}

/* out to port 3C7h sets read address */
static inline void asm_out_3c7(dos_t *s, uint8_t val) {
	s->pal_ridx = val * 3;
}

/* ================================================================ */
/*  x87 FPU                                                         */
/* ================================================================ */

#define ST(i) s->fpu[(s->fpu_top + (i)) & 7]

static inline void fpu_push(dos_t *s, double v) {
	s->fpu_top = (s->fpu_top - 1) & 7;
	ST(0) = v;
}
static inline double fpu_pop(dos_t *s) {
	double v = ST(0);
	s->fpu_top = (s->fpu_top + 1) & 7;
	return v;
}

static inline void asm_fninit(dos_t *s) {
	s->fpu_top = 0;
	memset(s->fpu, 0, sizeof(s->fpu));
}

static inline void asm_fldz(dos_t *s)  { fpu_push(s, 0.0); }

static inline void asm_fld_st(dos_t *s, int i) {
	double v = ST(i);
	fpu_push(s, v);
}

/* fadd dword [seg:addr] — st0 += float32 from memory */
static inline void asm_fadd_m32(dos_t *s, seg_t g, uint16_t a) {
	ST(0) += (double)mem_rf32(s, g, a);
}

/* fild word [seg:addr] — push int16 from memory */
static inline void asm_fild_m16(dos_t *s, seg_t g, uint16_t a) {
	fpu_push(s, (double)(int16_t)mem_r16(s, g, a));
}

/* fmul st(d), st(src) */
static inline void asm_fmul_st(dos_t *s, int d, int src) {
	ST(d) *= ST(src);
}

/* fmulp st(d), st0 — st(d) *= st0, pop */
static inline void asm_fmulp_st(dos_t *s, int d, int src) {
	ST(d) *= ST(src);
	fpu_pop(s);
}

/* fsubp st(d), st0 — st(d) = st(d) - st0, pop */
static inline void asm_fsubp_st(dos_t *s, int d, int src) {
	ST(d) -= ST(src);
	fpu_pop(s);
}

/* faddp st(d), st0 — st(d) += st0, pop */
static inline void asm_faddp_st(dos_t *s, int d, int src) {
	ST(d) += ST(src);
	fpu_pop(s);
}

/* fdivp st(d), st0 — st(d) /= st0, pop */
static inline void asm_fdivp_st(dos_t *s, int d, int src) {
	ST(d) /= ST(src);
	fpu_pop(s);
}

/* fxch st0, st(i) */
static inline void asm_fxch(dos_t *s, int i) {
	double t = ST(0); ST(0) = ST(i); ST(i) = t;
}

/* fsincos: replace st0 with sin, push cos */
/* result: st0 = cos(θ), st1 = sin(θ) */
static inline void asm_fsincos(dos_t *s) {
	double theta = ST(0);
	ST(0) = sin(theta);
	fpu_push(s, cos(theta));
}

/* fsqrt: st0 = sqrt(st0) */
static inline void asm_fsqrt(dos_t *s) { ST(0) = sqrt(ST(0)); }

/* fpatan: st1 = atan2(st1, st0), pop st0 */
static inline void asm_fpatan(dos_t *s) {
	double x = ST(0), y = ST(1);
	fpu_pop(s);
	ST(0) = atan2(y, x);
}

/* fimul word [seg:addr]: st0 *= int16 from memory */
static inline void asm_fimul_m16(dos_t *s, seg_t g, uint16_t a) {
	ST(0) *= (double)(int16_t)mem_r16(s, g, a);
}

/* fistp word [seg:addr]: store st0 as int16 and pop */
static inline void asm_fistp_m16(dos_t *s, seg_t g, uint16_t a) {
	int16_t v = (int16_t)lrint(ST(0));
	mem_w16(s, g, a, (uint16_t)v);
	fpu_pop(s);
}

/* ================================================================ */
/*  BMP writer (320x200 8-bit indexed)                              */
/* ================================================================ */

#include <stdio.h>

static inline void write_le16(FILE *f, uint16_t v) {
	fputc(v & 0xFF, f); fputc(v >> 8, f);
}
static inline void write_le32(FILE *f, uint32_t v) {
	fputc(v&0xFF,f); fputc((v>>8)&0xFF,f);
	fputc((v>>16)&0xFF,f); fputc((v>>24)&0xFF,f);
}

static inline void save_bmp(dos_t *s, const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) return;

	/* file header (14) */
	fputc('B',f); fputc('M',f);
	write_le32(f, 14+40+1024+64000);   /* file size   */
	write_le32(f, 0);                   /* reserved    */
	write_le32(f, 14+40+1024);          /* data offset */

	/* info header (40) */
	write_le32(f, 40);
	write_le32(f, 320);
	write_le32(f, 200);
	write_le16(f, 1);     /* planes */
	write_le16(f, 8);     /* bpp    */
	write_le32(f, 0);     /* compression */
	write_le32(f, 64000); /* image size  */
	write_le32(f, 0);     /* xppm */
	write_le32(f, 0);     /* yppm */
	write_le32(f, 256);   /* colors used */
	write_le32(f, 0);     /* important   */

	/* palette: 256 entries, BGRA, 6-bit scaled to 8-bit */
	for (int i = 0; i < 256; i++) {
		uint8_t r = s->pal[i*3+0] << 2;
		uint8_t g = s->pal[i*3+1] << 2;
		uint8_t b = s->pal[i*3+2] << 2;
		fputc(b,f); fputc(g,f); fputc(r,f); fputc(0,f);
	}

	/* pixel data bottom-to-top */
	for (int y = 199; y >= 0; y--)
		fwrite(s->vga + y*320, 1, 320, f);

	fclose(f);
}

#endif /* DOSEMU_H */
