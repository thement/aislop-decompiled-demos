/*
 * Tube demo — arbitrary resolution SDL1 viewer.
 *
 * Same effect as the original 320x200 demo, but rendered at any
 * resolution by sampling the continuous coordinate space with
 * floating-point pixel positions.
 *
 * Usage: ./tube_big [width height]    (default: 960x600)
 * Build: gcc -O2 -o tube_big tube_big.c -lm $(sdl-config --cflags --libs)
 */
#include <SDL/SDL.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* Original demo coordinate ranges */
#define ORIG_W    320.0
#define ORIG_H    200.0
#define ORIG_ROWS 160.0

#define EYE_DIST    160
#define TEX_SCALE   41
#define ANIM_SPEED  0x1.860052p-6f

static uint8_t palette[768];
static uint8_t texture[65536];

static int rw, rh;       /* render resolution */
static int tube_h;       /* tube band height in pixels */
static int tube_y0;      /* first tube row in screen */
static uint8_t *pixbuf;  /* tube_h * rw */
static uint32_t *fb;     /* rw * rh, 32-bit XRGB */

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

static void render_frame(double *angle, uint8_t *tex_phase) {
	*tex_phase += 8;
	uint16_t tex_ofs = ((uint16_t)*tex_phase << 8) | 1;
	*angle += ANIM_SPEED;
	double co = cos(*angle), sn = sin(*angle);

	int pi = 0;
	for (int y = 0; y < tube_h; y++) {
		double row = (y + 0.5) / tube_h * ORIG_ROWS - ORIG_ROWS / 2.0;
		for (int x = 0; x < rw; x++) {
			double col = (x + 0.5) / rw * ORIG_W - ORIG_W / 2.0;

			double y1 = col * co + row * sn;
			double z1 = row * co - col * sn;
			double p  = y1 * co + EYE_DIST * sn;
			double q  = EYE_DIST * co - y1 * sn;

			double radius = sqrt(p * p + z1 * z1);
			int16_t tu = (int16_t)lrint(atan2(p, z1) * TEX_SCALE);
			int16_t tv = (int16_t)lrint(q / radius * TEX_SCALE);
			uint16_t uv = (uint8_t)tu | ((uint16_t)(uint8_t)tv << 8);

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

	/* Convert tube band to 32-bit XRGB */
	uint32_t pal32[256];
	for (int i = 0; i < 256; i++)
		pal32[i] = ((uint32_t)(palette[i*3+0] << 2) << 16)
		         | ((uint32_t)(palette[i*3+1] << 2) << 8)
		         | ((uint32_t)(palette[i*3+2] << 2));

	for (int y = 0; y < tube_h; y++)
		for (int x = 0; x < rw; x++)
			fb[(tube_y0 + y) * rw + x] = pal32[pixbuf[y * rw + x]];

	/* Fade pixbuf */
	for (int i = 0; i < tube_h * rw; i++)
		pixbuf[i] = (uint8_t)((int8_t)pixbuf[i] >> 2);
}

int main(int argc, char **argv) {
	rw = 960;
	rh = 600;
	if (argc >= 3) {
		rw = atoi(argv[1]);
		rh = atoi(argv[2]);
		if (rw < 32) rw = 32;
		if (rh < 20) rh = 20;
	}

	/* Tube band occupies the central 80% of the height (160/200) */
	tube_h = rh * ORIG_ROWS / ORIG_H;
	tube_y0 = (rh - tube_h) / 2;

	pixbuf = calloc(tube_h * rw, 1);
	fb = calloc(rw * rh, sizeof(uint32_t));

	SDL_Init(SDL_INIT_VIDEO);
	SDL_Surface *screen = SDL_SetVideoMode(rw, rh, 32, SDL_SWSURFACE);
	SDL_WM_SetCaption("tube", NULL);

	generate_palette();
	generate_texture();

	double angle = 0.0;
	uint8_t tex_phase = 0xFF;
	int running = 1;

	while (running) {
		Uint32 t0 = SDL_GetTicks();

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT ||
			    (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
				running = 0;
		}

		render_frame(&angle, &tex_phase);

		SDL_LockSurface(screen);
		for (int y = 0; y < rh; y++)
			memcpy((uint8_t *)screen->pixels + y * screen->pitch,
			       fb + y * rw, rw * 4);
		SDL_UnlockSurface(screen);
		SDL_Flip(screen);

		Uint32 elapsed = SDL_GetTicks() - t0;
		if (elapsed < 40)
			SDL_Delay(40 - elapsed);
	}

	free(pixbuf);
	free(fb);
	SDL_Quit();
	return 0;
}
