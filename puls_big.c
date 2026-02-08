/*
 * puls_big.c - Arbitrary-resolution version of puls_sdl.c
 *
 * Raymarched implicit surface lattice (octahedra, bars, bolts).
 * Original 256-byte intro by Rrrola (Riverwash 2009), decompiled to C.
 *
 * Usage: ./puls_big [width height [precision]]
 *   width height  - window size (default 320x200)
 *   precision     - raymarching precision 0-8 (default: auto from resolution)
 *                   0 = original quality, each +1 doubles convergence fineness
 *                   auto: 0 for <=320, 1 for <=640, 2 for <=1280, etc.
 */

#include <SDL/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FPS      25
#define FRAME_MS (1000 / FPS)

#define BASE_MAXSTEPSHIFT 6
#define BASE_MAXITERS     26
#define BASECOLOR    (-34)
#define BLOWUP       86

#define WORD_100H    0x13B0
#define BYTE_100H    0xB0
#define FLOAT_100H   (-0.0008052f)

static uint32_t palette[256];

static void init_palette(SDL_Surface *screen)
{
    uint8_t vga[768];
    memset(vga, 0, sizeof(vga));

    int dac = 254 % 768;

    int8_t al = 0;
    for (int bx = 0xFFFF; bx >= 1; bx--) {
        int8_t bl = (int8_t)(bx & 0xFF);
        for (int cl = 3; cl >= 1; cl--) {
            if (cl < 3)
                al = bl;

            uint8_t tv = (uint8_t)bl & (uint8_t)cl;
            int popc = 0;
            for (uint8_t v = tv; v; v &= v - 1) popc++;

            if (popc & 1) {
                int16_t ax = (int16_t)al * (int16_t)al;
                ax = (int16_t)((uint16_t)ax >> 7);
                al = (int8_t)(ax & 0xFF);
            }
            {
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
 * Binary search ray intersection with configurable precision.
 *
 * maxstepshift: convergence depth (original=6, higher=finer surface edges)
 * maxiters:     iteration budget  (original=26, increase with maxstepshift)
 *
 * Increasing both by the same amount D gives D extra levels of binary
 * subdivision at the surface while keeping the same color/AO range.
 * int16 direction vectors support up to maxstepshift ~14 before
 * fine steps degenerate to ±1.
 */
static uint8_t intersect(int16_t dir[3], int16_t orig[3], int16_t r_val,
                          int maxstepshift, int maxiters)
{
    /*
     * Always start at BASE_MAXSTEPSHIFT (6), not maxstepshift.
     * The original algorithm ramps stepshift DOWN from 6→0 (coarse
     * exploration) then back UP from 0→6 (convergence).  Starting
     * at a higher maxstepshift would waste D extra miss-iterations
     * ramping down from 6+D to 6 before any useful exploration,
     * starving the coarse phase and producing wrong colors.
     * The extra precision levels (6..maxstepshift) are reached
     * naturally during the convergence ramp-up.
     */
    int stepshift = BASE_MAXSTEPSHIFT;
    int16_t hit_flag = 0;
    int8_t  ah = -(int8_t)maxiters;
    uint8_t al = 0;

    for (;;) {
        for (int i = 0; i < 3; i++) {
            int16_t step = dir[i] >> stepshift;
            step ^= hit_flag;
            orig[i] += step;
        }

        al = 0xFF;

        /* Hitlimit: inflated by BLOWUP/stepshift ("ambient occlusion").
         * The formula naturally extends to larger stepshift values:
         * at stepshift=14 the hitlimit converges to ~9472, providing
         * a tighter detection zone for finer convergence. */
        uint16_t cx = ((uint16_t)BLOWUP << 8) | (uint16_t)(uint8_t)stepshift;
        cx >>= stepshift;
        uint16_t hitlimit = ((uint16_t)(((cx >> 8) + 37) & 0xFF) << 8)
                           | (cx & 0xFF);

        int16_t temp[3];
        int16_t r_mem = r_val;
        int16_t dx_acc = 0;
        int     any_hit = 0;

        for (int oct = 0; oct < 2; oct++) {
            dx_acc = r_mem;
            r_mem = -r_mem;

            for (int i = 0; i < 3; i++) {
                int16_t bp = (al & 1) ? (int16_t)0x8000 : (int16_t)0;
                bp -= orig[i];
                if (bp < 0) bp = -bp;
                bp = (int16_t)((uint16_t)bp >> 1);
                dx_acc += bp;
                temp[i] = bp;
            }

            any_hit = ((uint16_t)dx_acc < hitlimit);

            uint16_t ax = ((uint16_t)(uint8_t)ah << 8) | al;
            ax++;
            al = ax & 0xFF;
            ah = (int8_t)(ax >> 8);

            if (any_hit)
                goto adjust;
        }

        dx_acc -= r_mem;

        {
            uint16_t ax = ((uint16_t)(uint8_t)ah << 8) | al;
            ax++;
            al = ax & 0xFF;
            ah = (int8_t)(ax >> 8);
        }

        dx_acc -= r_mem;
        dx_acc -= 0x6000;

        int32_t bolt_full = (int32_t)dx_acc * 13;
        int bolt_overflow = (bolt_full < -32768 || bolt_full > 32767);

        int16_t extra_width;
        if (bolt_overflow) {
            extra_width = WORD_100H;
        } else {
            uint16_t ax = ((uint16_t)(uint8_t)ah << 8) | al;
            ax++;
            al = ax & 0xFF;
            ah = (int8_t)(ax >> 8);
            int16_t ax_s = (int16_t)ax;
            extra_width = (ax_s < 0) ? -1 : 0;
        }

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

        if (stepshift >= maxstepshift) break;

        ah += (int8_t)(hit_flag & 0xFF);
        if (ah == 0) break;
    }

    ah -= (int8_t)stepshift;
    uint8_t color = (uint8_t)ah * 4 + al;
    color += (uint8_t)(maxiters * 4 + BASECOLOR);
    return color;
}

int main(int argc, char *argv[])
{
    int W = 320, H = 200;
    int precision = -1;  /* -1 = auto */

    if (argc >= 3) {
        W = atoi(argv[1]);
        H = atoi(argv[2]);
        if (W <= 0 || H <= 0) {
            fprintf(stderr,
                "Usage: %s [width height [precision]]\n"
                "  precision 0-8 (default: auto from resolution)\n", argv[0]);
            return 1;
        }
    }
    if (argc >= 4) {
        precision = atoi(argv[3]);
        if (precision < 0 || precision > 8) {
            fprintf(stderr, "Precision must be 0-8\n");
            return 1;
        }
    }

    /* Auto-detect precision from resolution */
    if (precision < 0) {
        int maxdim = W > H ? W : H;
        precision = 0;
        while ((320 << precision) < maxdim && precision < 8)
            precision++;
    }

    int maxstepshift = BASE_MAXSTEPSHIFT + precision;
    int maxiters     = BASE_MAXITERS + precision;

    /* Cap maxstepshift at 14: beyond that, int16 dir>>shift degenerates to ±1 */
    if (maxstepshift > 14) maxstepshift = 14;

    fprintf(stderr, "puls_big: %dx%d, precision=%d (maxstepshift=%d, maxiters=%d)\n",
            W, H, precision, maxstepshift, maxiters);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Surface *screen = SDL_SetVideoMode(W, H, 32,
                                           SDL_SWSURFACE | SDL_DOUBLEBUF);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_WM_SetCaption("Puls", NULL);

    init_palette(screen);

    uint16_t T = 0;
    uint8_t *pixbuf = (uint8_t *)malloc((size_t)W * H);
    if (!pixbuf) {
        fprintf(stderr, "Out of memory\n");
        SDL_Quit();
        return 1;
    }
    int running = 1;

    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        T += 88;
        int16_t T_signed = (int16_t)T;

        float sin_T = sinf((float)T_signed);
        float cos_T = cosf((float)T_signed);

        float r_f = (float)WORD_100H * sinf((float)T_signed * FLOAT_100H);
        int16_t r_val = (int16_t)lrintf(r_f);

        for (int row = 0; row < H; row++) {
            for (int col = 0; col < W; col++) {
                /* Map output pixel to original coordinate space */
                float px_f = (col + 0.5f) / W * 320.0f - 160.0f;
                float py_f = (row + 0.5f) / H * 200.0f - 100.0f;

                /*
                 * Scale to match original int16 coordinate ranges.
                 * Original: x spans ~-32768..32767 over 320 px → ~204.8 per px
                 *           y spans ~-25600..25600 over 200 px → ~256 per px
                 */
                int16_t x_int = (int16_t)lrintf(px_f * 204.0f);
                int16_t y_int = (int16_t)lrintf(py_f * 256.0f);

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

                int16_t dir[3];
                for (int i = 0; i < 3; i++) {
                    long v = lrintf(d[i]);
                    if (v > 32767) v = 32767;
                    if (v < -32768) v = -32768;
                    dir[i] = (int16_t)v;
                }

                int16_t base = (int16_t)((int)T_signed * 10);
                int16_t orig[3];
                orig[0] = base;
                orig[1] = (int16_t)((uint16_t)base + 0xB000u);
                orig[2] = (int16_t)((uint16_t)base + 0x6000u);

                pixbuf[row * W + col] = intersect(dir, orig, r_val,
                                                   maxstepshift, maxiters);
            }
        }

        /* Blit to screen */
        if (SDL_MUSTLOCK(screen))
            SDL_LockSurface(screen);

        uint32_t *pixels = (uint32_t *)screen->pixels;
        int pitch4 = screen->pitch / 4;

        for (int y = 0; y < H; y++) {
            uint32_t *dst = pixels + y * pitch4;
            uint8_t  *src = pixbuf + y * W;
            for (int x = 0; x < W; x++)
                dst[x] = palette[src[x]];
        }

        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);
        SDL_Flip(screen);

        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS)
            SDL_Delay(FRAME_MS - elapsed);
    }

    free(pixbuf);
    SDL_Quit();
    return 0;
}
