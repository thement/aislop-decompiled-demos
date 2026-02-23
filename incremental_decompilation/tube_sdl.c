/*
 * Tube demo — SDL1 real-time viewer.
 *
 * Renders the same rotating textured cylinder as tube_rewrite.c,
 * displayed in a 640x400 window (2x nearest-neighbor scaling).
 *
 * Build: gcc -O2 -o tube_sdl tube_sdl.c -lm $(sdl-config --cflags --libs)
 */
#include <SDL/SDL.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define W     320
#define H     200
#define SCALE 2
#define ROWS  160

#define EYE_DIST    160
#define TEX_SCALE   41
#define ANIM_SPEED  0x1.860052p-6f

static uint8_t palette[768];
static uint8_t texture[65536];
static uint8_t pixbuf[ROWS * W];
static uint8_t vga[W * H];

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
	for (int row = -(ROWS/2); row < ROWS/2; row++) {
		for (int col = -(W/2); col < W/2; col++) {
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

	memcpy(vga + (H/2 - ROWS/2) * W, pixbuf, sizeof(pixbuf));
	for (int i = 0; i < ROWS * W; i++)
		pixbuf[i] = (uint8_t)((int8_t)pixbuf[i] >> 2);
}

static void blit_2x(SDL_Surface *screen) {
	/* Build 32-bit RGBX palette lookup */
	uint32_t pal32[256];
	for (int i = 0; i < 256; i++)
		pal32[i] = ((uint32_t)(palette[i*3+0] << 2) << 16)
		         | ((uint32_t)(palette[i*3+1] << 2) << 8)
		         | ((uint32_t)(palette[i*3+2] << 2));

	SDL_LockSurface(screen);
	uint32_t *dst = screen->pixels;
	int pitch = screen->pitch / 4;
	for (int y = 0; y < H; y++) {
		uint32_t *row0 = dst + (y * SCALE) * pitch;
		uint32_t *row1 = row0 + pitch;
		for (int x = 0; x < W; x++) {
			uint32_t c = pal32[vga[y * W + x]];
			row0[x * SCALE]     = c;
			row0[x * SCALE + 1] = c;
			row1[x * SCALE]     = c;
			row1[x * SCALE + 1] = c;
		}
	}
	SDL_UnlockSurface(screen);
	SDL_Flip(screen);
}

int main(void) {
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Surface *screen = SDL_SetVideoMode(W * SCALE, H * SCALE, 32,
	                                       SDL_SWSURFACE);
	SDL_WM_SetCaption("tube", NULL);

	generate_palette();
	generate_texture();
	memset(vga, 0, sizeof(vga));
	memset(pixbuf, 0, sizeof(pixbuf));

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
		blit_2x(screen);

		Uint32 elapsed = SDL_GetTicks() - t0;
		if (elapsed < 40)
			SDL_Delay(40 - elapsed);
	}

	SDL_Quit();
	return 0;
}
