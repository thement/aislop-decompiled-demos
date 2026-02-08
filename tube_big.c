/*
 * tube_big.c - Arbitrary-resolution version of tube_sdl.c
 *
 * DOS demoscene tunnel effect: rotating 3D tunnel with texture mapping
 * and motion blur. Original by baze, decompiled to C with SDL1.2.
 *
 * Usage: ./tube_big [width height]
 * Defaults to 320x200 if no arguments given.
 */

#include <SDL/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FPS     25
#define FRAME_MS (1000 / FPS)

/* Rotation speed per frame: float 0x3CC30041 ≈ 0.023804 rad/frame */
#define ANGLE_INC 0.023804f

/* Scale factor for tunnel UV mapping (embedded as int16 = 41 in original) */
#define UV_SCALE  41.0f

static uint32_t palette[256];
static uint8_t  texture[65536];

static void init_palette(SDL_Surface *screen)
{
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
    for (int i = 0; i < 65536; i++)
        texture[i] = i & 0xFF;

    uint16_t ax = 0;
    uint8_t  dl = 0xC9;

    for (int iter = 0; iter < 65536; iter++) {
        uint16_t cx = (iter == 0) ? 0 : (uint16_t)(65536 - iter);
        uint8_t  cl = cx & 0xFF;
        uint16_t bx = cx;

        ax += cx;

        int shift = cl & 0x0F;
        if (shift)
            ax = (uint16_t)((ax << shift) | (ax >> (16 - shift)));

        uint8_t al = ax & 0xFF;
        int8_t  dh_s = ((int8_t)al) >> 5;
        uint8_t cf = (al >> 4) & 1;

        uint16_t sum = (uint16_t)dl + (uint16_t)(uint8_t)dh_s + cf;
        cf = (uint8_t)(sum >> 8);
        dl = (uint8_t)sum;

        sum = (uint16_t)dl + (uint16_t)texture[(bx + 255) & 0xFFFF] + cf;
        dl = (uint8_t)sum;

        dl >>= 1;

        texture[bx] = dl;
        bx ^= 0xFF00;
        texture[bx] = dl;
    }
}

int main(int argc, char *argv[])
{
    int W = 320, H = 200;
    if (argc >= 3) {
        W = atoi(argv[1]);
        H = atoi(argv[2]);
        if (W <= 0 || H <= 0) {
            fprintf(stderr, "Usage: %s [width height]\n", argv[0]);
            return 1;
        }
    }
    int VIEW_H = H * 4 / 5;

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
    SDL_WM_SetCaption("Tube", NULL);

    init_palette(screen);
    init_texture();

    int8_t *pixbuf = (int8_t *)calloc((size_t)W * VIEW_H, 1);
    if (!pixbuf) {
        fprintf(stderr, "Out of memory\n");
        SDL_Quit();
        return 1;
    }

    float   angle     = 0.0f;
    uint8_t bh_scroll = 0;
    float   scroll_acc = 0.0f;
    float   speed_mult = 1.0f;
    int     screenshot_counter = 0;
    int     take_screenshot = 0;
    int     running   = 1;

    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_PLUS: case SDLK_EQUALS:
                    speed_mult *= 1.25f;
                    if (speed_mult > 16.0f) speed_mult = 16.0f;
                    break;
                case SDLK_MINUS:
                    speed_mult *= 0.8f;
                    if (speed_mult < 0.0f) speed_mult = 0.0f;
                    break;
                case SDLK_s:
                    take_screenshot = 1;
                    break;
                default: break;
                }
            }
        }

        angle += ANGLE_INC * speed_mult;
        scroll_acc += 8.0f * speed_mult;
        bh_scroll += (uint8_t)scroll_acc;
        scroll_acc -= (int)scroll_acc;
        uint16_t bx = ((uint16_t)bh_scroll << 8) | 1;

        float cosa = cosf(angle);
        float sina = sinf(angle);

        /* Render tunnel */
        int pi = 0;
        for (int row = 0; row < VIEW_H; row++) {
            for (int col = 0; col < W; col++, pi++) {
                /* Map output pixel to original coordinate space */
                float px_f = (col + 0.5f) / W * 320.0f - 160.0f;
                float py_f = (row + 0.5f) / VIEW_H * 160.0f - 80.0f;

                float x = px_f;
                float y = py_f;
                float z = 160.0f;

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

                int   u_i = (int)lrintf(u_f);
                int   v_i = (int)lrintf(v_f);
                uint16_t si = (uint16_t)(((v_i & 0xFF) << 8) | (u_i & 0xFF));

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
        int y_off  = (H - VIEW_H) / 2;

        for (int row = 0; row < y_off; row++)
            memset(pixels + row * pitch4, 0, W * 4);
        for (int row = y_off + VIEW_H; row < H; row++)
            memset(pixels + row * pitch4, 0, W * 4);

        for (int y = 0; y < VIEW_H; y++) {
            uint32_t *dst = pixels + (y + y_off) * pitch4;
            int8_t   *src = pixbuf + y * W;
            for (int x = 0; x < W; x++)
                dst[x] = palette[(uint8_t)src[x]];
        }

        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);

        if (take_screenshot) {
            char fname[64];
            screenshot_counter++;
            snprintf(fname, sizeof(fname), "screenshot_%04d.bmp", screenshot_counter);
            SDL_SaveBMP(screen, fname);
            fprintf(stderr, "Saved %s\n", fname);
            take_screenshot = 0;
        }

        SDL_Flip(screen);

        /* Motion blur: arithmetic shift right by 2 (fade to black) */
        for (int i = W * VIEW_H - 1; i >= 0; i--)
            pixbuf[i] >>= 2;

        /* Frame rate limit: 25 fps */
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS)
            SDL_Delay(FRAME_MS - elapsed);
    }

    free(pixbuf);
    SDL_Quit();
    return 0;
}
