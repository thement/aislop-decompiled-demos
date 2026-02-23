/*
 * Tube demo — decompiled from a 256-byte DOS COM file.
 *
 * Renders a rotating textured cylinder and saves 25 frames as BMP.
 * The original is a .COM for DOS real mode, using VGA mode 13h
 * (320x200, 256 colors). This C version reproduces it pixel-for-pixel.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define W     320
#define H     200
#define ROWS  160

/* Constants from the original binary's instruction encodings and data block */
#define EYE_DIST    160              /* camera distance from cylinder axis */
#define TEX_SCALE   41               /* texture coordinate multiplier */
#define ANIM_SPEED  0x1.860052p-6f   /* rotation per frame (radians) */

static uint8_t palette[768];         /* 256 x RGB, 6-bit VGA values */
static uint8_t texture[65536];       /* 256x256 procedural texture */
static uint8_t pixbuf[ROWS * W];     /* pixel accumulation buffer */
static uint8_t vga[W * H];

/* ---- BMP writer (8-bit indexed, 320x200) ---- */

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
	if (!f) return;

	fputc('B', f); fputc('M', f);
	write_le32(f, 14 + 40 + 1024 + W * H);   /* file size */
	write_le32(f, 0);                          /* reserved */
	write_le32(f, 14 + 40 + 1024);            /* pixel data offset */

	write_le32(f, 40);                         /* header size */
	write_le32(f, W); write_le32(f, H);
	write_le16(f, 1); write_le16(f, 8);       /* planes, bpp */
	write_le32(f, 0); write_le32(f, W * H);   /* compression, image size */
	write_le32(f, 0); write_le32(f, 0);       /* resolution */
	write_le32(f, 256); write_le32(f, 0);     /* colors used, important */

	for (int i = 0; i < 256; i++) {
		fputc(palette[i*3+2] << 2, f);         /* B (6-bit -> 8-bit) */
		fputc(palette[i*3+1] << 2, f);         /* G */
		fputc(palette[i*3+0] << 2, f);         /* R */
		fputc(0, f);
	}
	for (int y = H - 1; y >= 0; y--)
		fwrite(vga + y * W, 1, W, f);
	fclose(f);
}

/* ---- Palette: warm orange 0-127, cool cyan 128-255 ---- */

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

/* ---- 256x256 procedural texture with vertical symmetry ---- */

static void generate_texture(void) {
	for (int i = 0; i < 65536; i++)
		texture[i] = i & 0xFF;

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

/* ================================================================ */

int main(void)
{
	generate_palette();
	generate_texture();
	memset(vga, 0, sizeof(vga));
	memset(pixbuf, 0, sizeof(pixbuf));

	double angle = 0.0;
	uint8_t tex_phase = 0xFF;

	for (int frame = 0; frame < 25; frame++) {
		tex_phase += 8;
		uint16_t tex_ofs = ((uint16_t)tex_phase << 8) | 1;
		angle += ANIM_SPEED;
		double co = cos(angle), sn = sin(angle);

		int pi = 0;
		for (int row = -(ROWS/2); row < ROWS/2; row++) {
			for (int col = -(W/2); col < W/2; col++) {
				/* Two successive 2D rotations by the same angle */
				double y1 = col * co + row * sn;
				double z1 = row * co - col * sn;
				double p  = y1 * co + EYE_DIST * sn;
				double q  = EYE_DIST * co - y1 * sn;

				/* Cylindrical projection -> texture coordinates */
				double radius = sqrt(p * p + z1 * z1);
				int16_t tu = (int16_t)lrint(atan2(p, z1) * TEX_SCALE);
				int16_t tv = (int16_t)lrint(q / radius * TEX_SCALE);
				uint16_t uv = (uint8_t)tu | ((uint16_t)(uint8_t)tv << 8);

				/* Shading zone — bright/mid/dark based on address bits */
				uint8_t shade;
				uint16_t addr = (uint16_t)(tex_ofs + uv);
				if (((uint8_t)addr + (uint8_t)(addr >> 8)) & 64) {
					uv <<= 2;
					addr = (uint16_t)(tex_ofs + uv);
					if (((uint8_t)addr - (uint8_t)(addr >> 8)) & 0x80) {
						uv <<= 1;
						shade = (uint8_t)-48;
					} else {
						shade = (uint8_t)-16;
					}
				} else {
					shade = (uint8_t)-5;
				}

				shade += texture[(uint16_t)(tex_ofs + uv)];
				pixbuf[pi++] += shade;
			}
		}

		/* Copy to VGA framebuffer, then fade pixel buffer toward black */
		memcpy(vga + (H/2 - ROWS/2) * W, pixbuf, sizeof(pixbuf));
		for (int i = 0; i < ROWS * W; i++)
			pixbuf[i] = (uint8_t)((int8_t)pixbuf[i] >> 2);

		char fname[32];
		sprintf(fname, "rframe%03d.bmp", frame);
		save_bmp(fname);
		printf("saved %s\n", fname);
	}

	printf("done — 25 frames captured\n");
	return 0;
}
