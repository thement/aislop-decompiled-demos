/*
 * puls_sdl.c - SDL1.2 port of puls.asm
 *
 * Raymarched implicit surface lattice (octahedra, bars, bolts).
 * Original 256-byte intro by Rrrola (Riverwash 2009), decompiled to C.
 *
 * Binary search raymarching: doubles step in empty space, halves on hit.
 * Scene: green/orange octahedra + bars + sliding bolts, with AO shading.
 * Fisheye lens: z = 0.336 - x*x - y*y
 */

#include <SDL/SDL.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define WIDTH    320
#define HEIGHT   200
#define FPS      25
#define FRAME_MS (1000 / FPS)

#define MAXSTEPSHIFT 6
#define MAXITERS     26
#define BASECOLOR    (-34)
#define BLOWUP       86

/* word[100h] = 0x13B0 (from instruction bytes: mov al,13h / push bx) */
#define WORD_100H    0x13B0
/* byte[100h] = 0xB0 */
#define BYTE_100H    0xB0
/* float[100h] = -0.0008052 (IEEE754 of the same instruction bytes) */
#define FLOAT_100H   (-0.0008052f)

static uint32_t palette[256];

/*
 * Palette: simulate the exact VGA DAC output from the assembly.
 * The loop writes to port 3C9h (DAC data), auto-incrementing the
 * color index. After 768 bytes (256 colors * RGB), it wraps.
 * The outer loop overwrites the palette many times; only the final
 * state matters.
 */
static void init_palette(SDL_Surface *screen)
{
    uint8_t vga[768];
    memset(vga, 0, sizeof(vga));

    /* First loop: bx=0, cx=255..1. All outputs 0 (bl=0).
     * First write to port 3C8h (index=0), rest to 3C9h. */
    int dac = 254 % 768;

    /* Outer loop: bx from 0xFFFF down to 1. 3 bytes per bx. */
    int8_t al = 0;
    for (int bx = 0xFFFF; bx >= 1; bx--) {
        int8_t bl = (int8_t)(bx & 0xFF);
        for (int cl = 3; cl >= 1; cl--) {
            if (cl < 3)
                al = bl;            /* P: mov al, bl */
            /* else al carries from previous (enters at Q, not P) */

            uint8_t tv = (uint8_t)bl & (uint8_t)cl;
            int popc = 0;
            for (uint8_t v = tv; v; v &= v - 1) popc++;

            if (popc & 1) {         /* parity odd: square + shift */
                int16_t ax = (int16_t)al * (int16_t)al;
                ax = (int16_t)((uint16_t)ax >> 7);
                al = (int8_t)(ax & 0xFF);
            }
            {                       /* E: imul bl → output ah */
                int16_t ax = (int16_t)al * (int16_t)bl;
                al = (int8_t)(ax >> 8);
            }
            vga[dac] = (uint8_t)al;
            dac = (dac + 1) % 768;
        }
    }

    for (int i = 0; i < 256; i++) {
        int r6 = vga[i * 3 + 0] & 0x3F;
        int g6 = vga[i * 3 + 1] & 0x3F;
        int b6 = vga[i * 3 + 2] & 0x3F;
        palette[i] = SDL_MapRGB(screen->format,
                                (r6 << 2) | (r6 >> 4),
                                (g6 << 2) | (g6 >> 4),
                                (b6 << 2) | (b6 >> 4));
    }
}

/*
 * Binary search ray intersection.
 *
 * Unbounded binary search: start with smallest step (dir >> MAXSTEPSHIFT).
 * On miss: double step (stepshift--). On hit: halve step (stepshift++).
 * Stop when stepshift reaches MAXSTEPSHIFT again (converged) or
 * after MAXITERS misses (gave up).
 *
 * Tests 4 implicit surfaces per probe:
 *   hue 0: octahedra at (0.5,0.5,0.5) offsets with +r
 *   hue 1: octahedra at (0,0,0) with -r
 *   hue 2: bars (far from bolt locus, with extra width)
 *   hue 3: bolts (near bolt locus, full hitlimit)
 *
 * Returns palette color index.
 */
static uint8_t intersect(int16_t dir[3], int16_t orig[3], int16_t r_val)
{
    int stepshift = MAXSTEPSHIFT;
    int16_t hit_flag = 0;           /* 0 = miss, -1 = hit */
    int8_t  ah = -(int8_t)MAXITERS;
    uint8_t al = 0;

    for (;;) {
        /* Advance origin: o += (d >> stepshift) XOR hit_flag */
        for (int i = 0; i < 3; i++) {
            int16_t step = dir[i] >> stepshift;
            step ^= hit_flag;
            orig[i] += step;
        }

        al = 0xFF;  /* salc: CF set from advance loop carry */

        /* Hitlimit: inflated by BLOWUP/stepshift ("ambient occlusion") */
        uint16_t cx = ((uint16_t)BLOWUP << 8) | (uint16_t)(uint8_t)stepshift;
        cx >>= stepshift;
        uint16_t hitlimit = ((uint16_t)(((cx >> 8) + 37) & 0xFF) << 8)
                           | (cx & 0xFF);

        int16_t temp[3];
        int16_t r_mem = r_val;
        int16_t dx_acc = 0;
        int     any_hit = 0;

        /* ---- Octahedra tests (hue 0, 1) ---- */
        for (int oct = 0; oct < 2; oct++) {
            dx_acc = r_mem;
            r_mem = -r_mem;

            for (int i = 0; i < 3; i++) {
                /* bp = 0.5 if al odd, 0 if even; then bp -= origin */
                int16_t bp = (al & 1) ? (int16_t)0x8000 : (int16_t)0;
                bp -= orig[i];
                if (bp < 0) bp = -bp;
                bp = (int16_t)((uint16_t)bp >> 1);
                dx_acc += bp;
                temp[i] = bp;
            }

            any_hit = ((uint16_t)dx_acc < hitlimit);

            /* inc ax: al++, carry to ah if al wraps */
            uint16_t ax = ((uint16_t)(uint8_t)ah << 8) | al;
            ax++;
            al = ax & 0xFF;
            ah = (int8_t)(ax >> 8);

            if (any_hit)
                goto adjust;
            /* jpe O: al=0 → PE → second octahedron; al=1 → PO → bars */
        }

        /* ---- Bars / bolts tests (hue 2, 3) ----
         * dx_acc = octahedron_1_dist. r_mem back to +r_val.
         * Bolt proximity: |sum(v2) - 3*r - 0.375| < 1/26 */
        dx_acc -= r_mem;

        { /* inc ax: al = 2 */
            uint16_t ax = ((uint16_t)(uint8_t)ah << 8) | al;
            ax++;
            al = ax & 0xFF;
            ah = (int8_t)(ax >> 8);
        }

        dx_acc -= r_mem;
        dx_acc -= 0x6000;          /* sub dh, 60h: -0.375 in >>16 */

        int32_t bolt_full = (int32_t)dx_acc * 13;
        int bolt_overflow = (bolt_full < -32768 || bolt_full > 32767);

        int16_t extra_width;
        if (bolt_overflow) {
            /* Far from bolt surface → hue 2 (bars), extra width */
            extra_width = WORD_100H;
        } else {
            /* Near bolt surface → hue 3 (bolts), no extra width */
            uint16_t ax = ((uint16_t)(uint8_t)ah << 8) | al;
            ax++;
            al = ax & 0xFF;
            ah = (int8_t)(ax >> 8);
            int16_t ax_s = (int16_t)ax;
            extra_width = (ax_s < 0) ? -1 : 0;
        }

        /* B loop: bar/bolt distance = sum(|temp[i-1] - temp[i]|) */
        dx_acc = extra_width;
        {
            int16_t bp = temp[2];
            for (int i = 0; i < 3; i++) {
                bp = (int16_t)(bp - temp[i]);
                if (bp < 0) bp = -bp;
                dx_acc += bp;
                bp = temp[i];
            }
        }

        any_hit = ((uint16_t)dx_acc < hitlimit);

    adjust:
        if (any_hit) {
            hit_flag = -1;
            stepshift++;
        } else {
            hit_flag = 0;
            if (stepshift > 0) stepshift--;
        }

        if (stepshift >= MAXSTEPSHIFT) break;

        ah += (int8_t)(hit_flag & 0xFF);  /* hit: ah--, miss: ah+=0 */
        if (ah == 0) break;
    }

    /* Color = (misses - stepshift) * 4 + hue + constant */
    ah -= (int8_t)stepshift;
    uint8_t color = (uint8_t)ah * 4 + al;
    color += (uint8_t)(MAXITERS * 4 + BASECOLOR);
    return color;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Surface *screen = SDL_SetVideoMode(WIDTH, HEIGHT, 32,
                                           SDL_SWSURFACE | SDL_DOUBLEBUF);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_WM_SetCaption("Puls", NULL);

    init_palette(screen);

    uint16_t T = 0;
    uint8_t  pixbuf[WIDTH * HEIGHT];
    int      running = 1;

    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        /* ===== Frame constants ===== */
        T += 88;
        int16_t T_signed = (int16_t)T;

        float sin_T = sinf((float)T_signed);
        float cos_T = cosf((float)T_signed);

        /* r = 5040 * sin(T * -0.0008052) - pulsation amplitude */
        float r_f = (float)WORD_100H * sinf((float)T_signed * FLOAT_100H);
        int16_t r_val = (int16_t)lrintf(r_f);

        /* ===== Pixel loop ===== */
        /*
         * Original uses a 32-bit counter starting at 0x9FCE0000,
         * incrementing by 0xCCCD per pixel. x = bits[8:23], y = bits[16:31].
         * Screen starts at pixel offset 544 from the counter origin.
         */
        for (int row = 0; row < HEIGHT; row++) {
            for (int col = 0; col < WIDTH; col++) {
                int p = 544 + row * WIDTH + col;
                uint32_t ctr = 0x9FCE0000u + (uint32_t)p * 0xCCCDu;
                int16_t x_int = (int16_t)((ctr >> 8) & 0xFFFF);
                int16_t y_int = (int16_t)(ctr >> 16);

                /* Fisheye: z = 0.33594 - x*x - y*y (int16 scale) */
                int16_t z_int = (int16_t)(0x5600
                    - (int16_t)((int32_t)x_int * x_int >> 16)
                    - (int16_t)((int32_t)y_int * y_int >> 16));

                /* Rotate direction (z,x,y) by angle T, three passes */
                float d[3] = {(float)z_int, (float)x_int, (float)y_int};
                for (int pass = 0; pass < 3; pass++) {
                    float t0 = d[0], t2 = d[2];
                    d[0] = d[1];
                    d[1] = t0 * cos_T - t2 * sin_T;
                    d[2] = t0 * sin_T + t2 * cos_T;
                }

                /* Truncate direction to int16 (fistp word) */
                int16_t dir[3];
                for (int i = 0; i < 3; i++) {
                    long v = lrintf(d[i]);
                    if (v > 32767) v = 32767;
                    if (v < -32768) v = -32768;
                    dir[i] = (int16_t)v;
                }

                /* Ray origin: base = T*10, with offsets in high byte */
                int16_t base = (int16_t)((int)T_signed * 10);
                int16_t orig[3];
                orig[0] = base;
                orig[1] = (int16_t)((uint16_t)base + 0xB000u);
                orig[2] = (int16_t)((uint16_t)base + 0x6000u);

                pixbuf[row * WIDTH + col] = intersect(dir, orig, r_val);
            }
        }

        /* Blit to screen */
        if (SDL_MUSTLOCK(screen))
            SDL_LockSurface(screen);

        uint32_t *pixels = (uint32_t *)screen->pixels;
        int pitch4 = screen->pitch / 4;

        for (int y = 0; y < HEIGHT; y++) {
            uint32_t *dst = pixels + y * pitch4;
            uint8_t  *src = pixbuf + y * WIDTH;
            for (int x = 0; x < WIDTH; x++)
                dst[x] = palette[src[x]];
        }

        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);
        SDL_Flip(screen);

        /* Frame rate limit */
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS)
            SDL_Delay(FRAME_MS - elapsed);
    }

    SDL_Quit();
    return 0;
}
