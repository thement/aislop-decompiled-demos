/*
 * lattice_sdl.c - SDL1.2 port of lattice.asm
 *
 * Raymarched Schwarz P-surface (triply periodic minimal surface) lattice.
 * Original 256-byte intro by baze, decompiled to C with SDL1.2.
 *
 * SDF: cos(x) + cos(y) + cos(z) + ln(2)
 * 32 sphere-tracing steps per pixel, brightness from remaining step count.
 */

#include <SDL/SDL.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define WIDTH    320
#define HEIGHT   200
#define FPS      25
#define FRAME_MS (1000 / FPS)

/*
 * Constants from the original, derived from instruction encodings:
 *   EYE    = 0x14B = 331  (focal length, self-referencing mov immediate)
 *   SCALE  = 41           (UV texture scale factor)
 *   ZMOVE  = 0x03C8 = 968 (initial camera Z, embedded in mov dx,3C8h)
 *   EPSILON ≈ 0.094       (float 0x3DC08E10 from instruction byte overlap)
 */
#define EYE_VAL   331.0f
#define UV_SCALE  41
#define ZMOVE_INIT 968
#define EPSILON   0.09402f

static uint32_t palette[256];
static uint8_t  texture[65536];

static void init_palette(SDL_Surface *screen)
{
    /*
     * VGA 6-bit palette: R = i & 63, G = (i*i/64) & 63, B = 0
     * Scaled to 8-bit for SDL display.
     */
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
    /*
     * Palette loop fills ES segment with zeros via stosb.
     * Then TEXTURE loop generates pseudo-random smoothed noise
     * with BH-mirroring, different PRNG from the tube demo.
     */
    memset(texture, 0, sizeof(texture));

    uint8_t al = 0, dh = 0x03;  /* DH=0x03 from palette loop (DX=0x3C9) */
    uint8_t cf = 0;

    for (int iter = 0; iter < 65536; iter++) {
        uint16_t cx = (iter == 0) ? 0 : (uint16_t)(65536 - iter);
        uint8_t  cl = cx & 0xFF;
        uint16_t bx = cx;

        /* rcl dh, cl - rotate DH left through carry by CL positions */
        int shift = cl & 0x1F;
        for (int s = 0; s < shift; s++) {
            uint8_t new_cf = (dh >> 7) & 1;
            dh = (uint8_t)((dh << 1) | cf);
            cf = new_cf;
        }

        uint8_t ah = dh;
        int8_t  ah_s = ((int8_t)ah) >> 3;    /* sar ah, 3 */
        uint8_t cf_sar = (ah >> 2) & 1;      /* CF from sar 3 */

        /* adc al, ah */
        uint16_t sum = (uint16_t)al + (uint16_t)(uint8_t)ah_s + cf_sar;
        cf = (uint8_t)(sum >> 8);
        al = (uint8_t)sum;

        /* adc al, [es:bx+128] */
        sum = (uint16_t)al + (uint16_t)texture[(bx + 128) & 0xFFFF] + cf;
        cf = (uint8_t)(sum >> 8);
        al = (uint8_t)sum;

        /* shr al, 1 */
        cf = al & 1;
        al >>= 1;

        texture[bx] = al;
        bx ^= 0xFF00;    /* not bh - mirror */
        texture[bx] = al;
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
    SDL_WM_SetCaption("Lattice", NULL);

    init_palette(screen);
    init_texture();

    int16_t zmove_val = ZMOVE_INIT;
    uint8_t pixbuf[WIDTH * HEIGHT];
    int     running = 1;

    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        float angle = (float)zmove_val / 41.0f;
        float cosa  = cosf(angle);
        float sina  = sinf(angle);
        float cam_z = (float)zmove_val / (float)M_PI;

        int pi = 0;
        for (int py = -100; py < 100; py++) {
            for (int px = -160; px < 160; px++, pi++) {
                /*
                 * Ray direction: normalized screen coords + fldlg2 as Z.
                 * fldlg2 = log10(2) ≈ 0.30103
                 */
                float nx = (float)px / EYE_VAL;
                float ny = (float)py / EYE_VAL;
                float nz = 0.30102999566f;  /* log10(2) */

                /* First rotation: (nx, ny) plane */
                float x1 = nx * cosa + ny * sina;
                float y1 = ny * cosa - nx * sina;

                /* Second rotation: (nz, x1) plane */
                float rx = x1 * cosa + nz * sina;
                float rz = nz * cosa - x1 * sina;
                float ry = y1;  /* unchanged */

                /*
                 * Raymarching: sphere-trace the Schwarz P-surface
                 *   SDF(p) = cos(p.x) + cos(p.y) + cos(p.z) + ln(2)
                 *
                 * Ray advances along swapped directions matching the
                 * original FPU stack layout:
                 *   posX += SDF * ry
                 *   posY += SDF * rx
                 *   posZ += SDF * rz
                 */
                float posX = 0.0f;
                float posY = 0.0f;
                float posZ = cam_z;
                int   steps_left = 0;

                for (int step = 0; step < 32; step++) {
                    float sdf = cosf(posZ) + cosf(posY) + cosf(posX)
                              + 0.69314718f;  /* ln(2) */
                    int is_hit = (sdf < EPSILON);

                    /* Original advances ray BEFORE jc check (FPU ops
                     * don't affect EFLAGS), so hit uses advanced pos. */
                    posX += sdf * ry;
                    posY += sdf * rx;
                    posZ += sdf * rz;

                    if (is_hit) {
                        steps_left = 32 - step;
                        break;
                    }
                }

                /*
                 * Texture mapping from hit position:
                 *   U = atan2(posY, posX) * 41
                 *   V = posZ * 41
                 * Combined as (V_low << 8) | U_low (overlapping fistp trick).
                 */
                int u_i = (int)lrintf(atan2f(posY, posX) * UV_SCALE);
                int v_i = (int)lrintf(posZ * UV_SCALE);
                uint16_t uv = (uint16_t)(((v_i & 0xFF) << 8) | (u_i & 0xFF));

                /*
                 * Brightness = steps_left * 2 (early hit = bright).
                 * Color = (-texture[uv]) * brightness / 256.
                 */
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

        for (int y = 0; y < HEIGHT; y++) {
            uint32_t *dst = pixels + y * pitch4;
            uint8_t  *src = pixbuf + y * WIDTH;
            for (int x = 0; x < WIDTH; x++)
                dst[x] = palette[src[x]];
        }

        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);
        SDL_Flip(screen);

        /* Camera moves forward each frame */
        zmove_val--;

        /* Frame rate limit: 25 fps */
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS)
            SDL_Delay(FRAME_MS - elapsed);
    }

    SDL_Quit();
    return 0;
}
