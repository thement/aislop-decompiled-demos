/*
 * tube_sdl.c - SDL1.2 port of tube_orig.asm
 *
 * DOS demoscene tunnel effect: rotating 3D tunnel with texture mapping
 * and motion blur. Original by baze, decompiled to C with SDL1.2.
 */

#include <SDL/SDL.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define WIDTH   320
#define HEIGHT  200
#define VIEW_W  320
#define VIEW_H  160
#define FPS     25
#define FRAME_MS (1000 / FPS)

/* Rotation speed per frame: float 0x3CC30041 ≈ 0.023804 rad/frame */
#define ANGLE_INC 0.023804f

/* Scale factor for tunnel UV mapping (embedded as int16 = 41 in original) */
#define UV_SCALE  41.0f

static uint32_t palette[256];
static uint8_t  texture[65536];
static int8_t   pixbuf[VIEW_W * VIEW_H];

static void init_palette(SDL_Surface *screen)
{
    /*
     * Colors 0-127: warm gradient (orange/yellow)
     *   R = i/2, G = (i/2)^2 / 64, B = 0  (6-bit VGA values)
     *
     * Colors 128-255: cool gradient (cyan/blue)
     *   R = 0, G = (256-i)/2, B = (256-i)/4  (6-bit VGA values)
     */
    for (int i = 0; i < 128; i++) {
        int r6 = i >> 1;
        int g6 = (r6 * r6) >> 6;
        palette[i] = SDL_MapRGB(screen->format,
                                (r6 << 2) | (r6 >> 4),
                                (g6 << 2) | (g6 >> 4),
                                0);
    }
    for (int i = 128; i < 256; i++) {
        int v  = 256 - i;
        int g6 = (v >> 1) & 63;
        int b6 = v >> 2;
        palette[i] = SDL_MapRGB(screen->format,
                                0,
                                (g6 << 2) | (g6 >> 4),
                                (b6 << 2) | (b6 >> 4));
    }
}

static void init_texture(void)
{
    /*
     * The palette loop (PAL3) initializes texture[i] = i & 0xFF for all 64K.
     * Then the TEX loop overwrites with pseudo-random smoothed values,
     * with BH-mirroring (NOT BH) for symmetry.
     */
    for (int i = 0; i < 65536; i++)
        texture[i] = i & 0xFF;

    uint16_t ax = 0;
    uint8_t  dl = 0xC9; /* DL state carried from palette loop */

    for (int iter = 0; iter < 65536; iter++) {
        uint16_t cx = (iter == 0) ? 0 : (uint16_t)(65536 - iter);
        uint8_t  cl = cx & 0xFF;
        uint16_t bx = cx;

        ax += cx;

        /* ROL AX, CL (shift masked to 4 bits for 16-bit operand on 8086) */
        int shift = cl & 0x0F;
        if (shift)
            ax = (uint16_t)((ax << shift) | (ax >> (16 - shift)));

        uint8_t al = ax & 0xFF;
        int8_t  dh_s = ((int8_t)al) >> 5;           /* SAR AL, 5 */
        uint8_t cf = (al >> 4) & 1;                  /* CF from SAR 5 */

        /* ADC DL, DH */
        uint16_t sum = (uint16_t)dl + (uint16_t)(uint8_t)dh_s + cf;
        cf = (uint8_t)(sum >> 8);
        dl = (uint8_t)sum;

        /* ADC DL, texture[(BX+255) & 0xFFFF] */
        sum = (uint16_t)dl + (uint16_t)texture[(bx + 255) & 0xFFFF] + cf;
        dl = (uint8_t)sum;

        /* SHR DL, 1 */
        dl >>= 1;

        texture[bx] = dl;
        bx ^= 0xFF00;  /* NOT BH - mirror */
        texture[bx] = dl;
    }
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
    SDL_WM_SetCaption("Tube", NULL);

    init_palette(screen);
    init_texture();
    memset(pixbuf, 0, sizeof(pixbuf));

    float   angle     = 0.0f;
    uint8_t bh_scroll = 0;       /* texture scroll offset (high byte of BX) */
    int     running   = 1;

    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        /* Event handling */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        /* Per-frame updates: advance angle and texture scroll */
        angle     += ANGLE_INC;
        bh_scroll += 8;
        uint16_t bx = ((uint16_t)bh_scroll << 8) | 1; /* BL=1 from TEX loop */

        float cosa = cosf(angle);
        float sina = sinf(angle);

        /* Render tunnel: 320x160 viewport, centered at (0,0) */
        int pi = 0;
        for (int py = -80; py < 80; py++) {
            for (int px = -160; px < 160; px++, pi++) {
                float x = (float)px;
                float y = (float)py;
                float z = 160.0f;    /* focal length / eye distance */

                /* First rotation: (X, Y) plane */
                float x1 = x * cosa + y * sina;
                float y1 = y * cosa - x * sina;

                /* Second rotation: (Z, X1) plane */
                float x2 = x1 * cosa + z * sina;
                float z2 = z  * cosa - x1 * sina;

                /* Tunnel mapping: angular + radial coordinates */
                float dist = sqrtf(x2 * x2 + y1 * y1);
                if (dist < 0.001f) dist = 0.001f;

                float u_f = atan2f(x2, y1) * UV_SCALE;
                float v_f = (z2 / dist)    * UV_SCALE;

                /*
                 * fistp stores with round-to-nearest (default FPU mode).
                 * The second store overlaps the first by one byte, so we
                 * get: si = (V_low << 8) | U_low
                 */
                int   u_i = (int)lrintf(u_f);
                int   v_i = (int)lrintf(v_f);
                uint16_t si = (uint16_t)(((v_i & 0xFF) << 8) | (u_i & 0xFF));

                /*
                 * Three-band color selection based on UV patterns:
                 *   Band 1: (low+high) & 64 == 0 → base -5, texture at si
                 *   Band 2: (low-high) non-negative → base -16, texture at si*4
                 *   Band 3: fallback → base -48, texture at si*8
                 */
                int8_t  color;
                uint16_t tidx;
                uint16_t tmp;

                tmp = bx + si;
                uint8_t mixed = (uint8_t)((tmp & 0xFF) + ((tmp >> 8) & 0xFF));
                if ((mixed & 64) == 0) {
                    color = -5;
                    tidx = (bx + si) & 0xFFFF;
                } else {
                    si <<= 2;
                    tmp = bx + si;
                    uint8_t sub = (uint8_t)((tmp & 0xFF) - ((tmp >> 8) & 0xFF));
                    if (!(sub & 0x80)) {
                        color = -16;
                        tidx = (bx + si) & 0xFFFF;
                    } else {
                        si <<= 1;
                        color = -48;
                        tidx = (bx + si) & 0xFFFF;
                    }
                }

                color = (int8_t)((uint8_t)color + texture[tidx]);
                pixbuf[pi] = (int8_t)(pixbuf[pi] + color);
            }
        }

        /* Blit pixel buffer to screen surface, centered vertically */
        if (SDL_MUSTLOCK(screen))
            SDL_LockSurface(screen);

        uint32_t *pixels = (uint32_t *)screen->pixels;
        int pitch4 = screen->pitch / 4;
        int y_off  = (HEIGHT - VIEW_H) / 2;

        for (int row = 0; row < y_off; row++)
            memset(pixels + row * pitch4, 0, WIDTH * 4);
        for (int row = y_off + VIEW_H; row < HEIGHT; row++)
            memset(pixels + row * pitch4, 0, WIDTH * 4);

        for (int y = 0; y < VIEW_H; y++) {
            uint32_t *dst = pixels + (y + y_off) * pitch4;
            int8_t   *src = pixbuf + y * VIEW_W;
            for (int x = 0; x < VIEW_W; x++)
                dst[x] = palette[(uint8_t)src[x]];
        }

        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);
        SDL_Flip(screen);

        /* Motion blur: arithmetic shift right by 2 (fade to black) */
        for (int i = VIEW_W * VIEW_H - 1; i >= 0; i--)
            pixbuf[i] >>= 2;

        /* Frame rate limit: 25 fps */
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS)
            SDL_Delay(FRAME_MS - elapsed);
    }

    SDL_Quit();
    return 0;
}
