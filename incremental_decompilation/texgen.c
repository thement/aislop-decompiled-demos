/*
 * texgen — generate the tube demo's 256x256 procedural texture as a BMP.
 *
 * The texture is produced by a sequential PRNG that averages with
 * neighbours, giving smooth cloud-like noise with a horizontal brightness
 * gradient and perfect vertical symmetry at v=128.
 *
 * See TEXTURE.md for a detailed analysis of the algorithm and its output.
 *
 * Output: texture.bmp (256x256, 8-bit indexed with the demo's VGA palette)
 */
#include <stdio.h>
#include <stdint.h>

static uint8_t palette[768];   /* 256 x RGB, 6-bit VGA values */
static uint8_t texture[65536]; /* 256x256, row-major: texture[v*256+u] */

/* Warm orange 0-127, cool cyan 128-255 (same as the demo) */
static void generate_palette(void) {
	for (int i = 0; i < 128; i++) {
		uint8_t r = i / 2;
		palette[i*3+0] = r;
		palette[i*3+1] = (r * r) >> 6;
		palette[i*3+2] = 0;
	}
	for (int i = 128; i < 256; i++) {
		uint8_t d = 256 - i;
		palette[i*3+0] = 0;
		palette[i*3+1] = (d >> 1) & 0x3F;
		palette[i*3+2] = (d >> 2) & 0x3F;
	}
}

static void generate_texture(void) {
	/* Phase 1: identity fill */
	for (int i = 0; i < 65536; i++)
		texture[i] = i & 0xFF;

	/* Phase 2: sequential PRNG with neighbour averaging */
	uint16_t hash = 0;
	uint8_t accum = 0xC9;
	uint16_t idx = 0;
	do {
		hash = (uint16_t)((uint32_t)hash + idx);
		int rot = (uint8_t)idx & 15;
		if (rot)
			hash = (hash << rot) | (hash >> (16 - rot));

		int8_t tmp = (int8_t)(uint8_t)hash;
		int carry = (tmp >> 4) & 1;
		tmp >>= 5;
		uint16_t r = (uint16_t)accum + (uint16_t)(uint8_t)tmp + carry;
		accum = (uint8_t)r;
		carry = r > 0xFF;
		r = (uint16_t)accum + texture[(uint16_t)(idx + 255)] + carry;
		accum = (uint8_t)r >> 1;

		texture[idx] = accum;
		texture[idx ^ 0xFF00] = accum;
	} while (--idx);
}

/* ---- BMP writer (256x256, 8-bit indexed color) ---- */

static void write_le16(FILE *f, uint16_t v) {
	fputc(v & 0xFF, f);
	fputc(v >> 8, f);
}

static void write_le32(FILE *f, uint32_t v) {
	fputc(v & 0xFF, f);
	fputc((v >> 8) & 0xFF, f);
	fputc((v >> 16) & 0xFF, f);
	fputc((v >> 24) & 0xFF, f);
}

static void save_bmp(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return; }

	fputc('B', f); fputc('M', f);
	write_le32(f, 14 + 40 + 1024 + 256 * 256); /* file size */
	write_le32(f, 0);                            /* reserved */
	write_le32(f, 14 + 40 + 1024);              /* pixel data offset */

	write_le32(f, 40);                           /* header size */
	write_le32(f, 256); write_le32(f, 256);
	write_le16(f, 1); write_le16(f, 8);         /* planes, bpp */
	write_le32(f, 0); write_le32(f, 256 * 256); /* compression, image size */
	write_le32(f, 0); write_le32(f, 0);         /* resolution */
	write_le32(f, 256); write_le32(f, 0);       /* colors used, important */

	/* VGA palette (6-bit → 8-bit) */
	for (int i = 0; i < 256; i++) {
		fputc(palette[i*3+2] << 2, f);  /* B */
		fputc(palette[i*3+1] << 2, f);  /* G */
		fputc(palette[i*3+0] << 2, f);  /* R */
		fputc(0, f);
	}

	/* BMP rows are bottom-up */
	for (int y = 255; y >= 0; y--)
		fwrite(texture + y * 256, 1, 256, f);
	fclose(f);
}

int main(void) {
	generate_palette();
	generate_texture();
	save_bmp("texture.bmp");
	return 0;
}
