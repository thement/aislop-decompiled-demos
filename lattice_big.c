/*
 * lattice_big.c - Arbitrary-resolution version of lattice_sdl.c
 *
 * Raymarched Schwarz P-surface (triply periodic minimal surface) lattice.
 * Original 256-byte intro by baze, decompiled to C with SDL1.2.
 *
 * Usage: ./lattice_big [width height]
 * Defaults to 320x200 if no arguments given.
 */

#include <SDL/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FPS      25
#define FRAME_MS (1000 / FPS)

#define EYE_VAL   331.0f
#define UV_SCALE  41
#define ZMOVE_INIT 968
#define EPSILON   0.09402f

static uint32_t palette[256];
static uint8_t  texture[65536];

static void init_palette(SDL_Surface *screen)
{
    for (int i = 0; i < 256; i++) {
        int r6 = i & 63;
        int g6 = ((i * i) / 64) & 63;
        palette[i] = SDL_MapRGB(screen->format,
                                (r6 << 2) | (r6 >> 4),
                                (g6 << 2) | (g6 >> 4),
                                0);
    }
}

static void init_texture(void)
{
    memset(texture, 0, sizeof(texture));

    uint8_t al = 0, dh = 0x03;
    uint8_t cf = 0;

    for (int iter = 0; iter < 65536; iter++) {
        uint16_t cx = (iter == 0) ? 0 : (uint16_t)(65536 - iter);
        uint8_t  cl = cx & 0xFF;
        uint16_t bx = cx;

        int shift = cl & 0x1F;
        for (int s = 0; s < shift; s++) {
            uint8_t new_cf = (dh >> 7) & 1;
            dh = (uint8_t)((dh << 1) | cf);
            cf = new_cf;
        }

        uint8_t ah = dh;
        int8_t  ah_s = ((int8_t)ah) >> 3;
        uint8_t cf_sar = (ah >> 2) & 1;

        uint16_t sum = (uint16_t)al + (uint16_t)(uint8_t)ah_s + cf_sar;
        cf = (uint8_t)(sum >> 8);
        al = (uint8_t)sum;

        sum = (uint16_t)al + (uint16_t)texture[(bx + 128) & 0xFFFF] + cf;
        cf = (uint8_t)(sum >> 8);
        al = (uint8_t)sum;

        cf = al & 1;
        al >>= 1;

        texture[bx] = al;
        bx ^= 0xFF00;
        texture[bx] = al;
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
    SDL_WM_SetCaption("Lattice", NULL);

    init_palette(screen);
    init_texture();

    float zmove_f = (float)ZMOVE_INIT;
    float speed_mult = 1.0f;
    int   screenshot_counter = 0;
    int   take_screenshot = 0;
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

        zmove_f -= speed_mult;
        float angle = zmove_f / 41.0f;
        float cosa  = cosf(angle);
        float sina  = sinf(angle);
        float cam_z = zmove_f / (float)M_PI;

        int pi = 0;
        for (int row = 0; row < H; row++) {
            for (int col = 0; col < W; col++, pi++) {
                /* Map output pixel to original coordinate space */
                float px_f = (col + 0.5f) / W * 320.0f - 160.0f;
                float py_f = (row + 0.5f) / H * 200.0f - 100.0f;

                float nx = px_f / EYE_VAL;
                float ny = py_f / EYE_VAL;
                float nz = 0.30102999566f;  /* log10(2) */

                /* First rotation: (nx, ny) plane */
                float x1 = nx * cosa + ny * sina;
                float y1 = ny * cosa - nx * sina;

                /* Second rotation: (nz, x1) plane */
                float rx = x1 * cosa + nz * sina;
                float rz = nz * cosa - x1 * sina;
                float ry = y1;

                float posX = 0.0f;
                float posY = 0.0f;
                float posZ = cam_z;
                int   steps_left = 0;

                for (int step = 0; step < 32; step++) {
                    float sdf = cosf(posZ) + cosf(posY) + cosf(posX)
                              + 0.69314718f;
                    int is_hit = (sdf < EPSILON);

                    posX += sdf * ry;
                    posY += sdf * rx;
                    posZ += sdf * rz;

                    if (is_hit) {
                        steps_left = 32 - step;
                        break;
                    }
                }

                int u_i = (int)lrintf(atan2f(posY, posX) * UV_SCALE);
                int v_i = (int)lrintf(posZ * UV_SCALE);
                uint16_t uv = (uint16_t)(((v_i & 0xFF) << 8) | (u_i & 0xFF));

                uint8_t tex_val = texture[uv];
                uint8_t neg_tex = (uint8_t)(-(int8_t)tex_val);
                uint8_t bright  = (uint8_t)(steps_left * 2);
                uint16_t product = (uint16_t)neg_tex * (uint16_t)bright;

                pixbuf[pi] = (uint8_t)(product >> 8);
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

        if (take_screenshot) {
            char fname[64];
            screenshot_counter++;
            snprintf(fname, sizeof(fname), "screenshot_%04d.bmp", screenshot_counter);
            SDL_SaveBMP(screen, fname);
            fprintf(stderr, "Saved %s\n", fname);
            take_screenshot = 0;
        }

        SDL_Flip(screen);

        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS)
            SDL_Delay(FRAME_MS - elapsed);
    }

    free(pixbuf);
    SDL_Quit();
    return 0;
}
