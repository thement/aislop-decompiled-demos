/*
 * puls_parallel.c - Multi-threaded version of puls_big.c
 *
 * Raymarched implicit surface lattice (octahedra, bars, bolts).
 * Original 256-byte intro by Rrrola (Riverwash 2009), decompiled to C.
 *
 * Usage: ./puls_parallel [width height [precision]]
 *   width height  - window size (default 320x200)
 *   precision     - raymarching precision 0-8 (default: auto from resolution)
 *
 * Set THREADS env var to control thread count (default 16).
 */

#include <SDL/SDL.h>
#include <math.h>
#include <pthread.h>
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

/* ===== Threading ===== */

/* Per-frame constants shared by all threads (read-only during render) */
typedef struct {
    int       W, H;
    int       maxstepshift, maxiters;
    uint8_t  *pixbuf;
    float     sin_T, cos_T;
    int16_t   r_val;
    int16_t   T_signed;
    int       quit;
} frame_params_t;

typedef struct {
    int              id;
    int              nthreads;
    frame_params_t  *fp;
    pthread_barrier_t *bar_start;
    pthread_barrier_t *bar_done;
} worker_t;

static void render_rows(const frame_params_t *fp, int row_begin, int row_end)
{
    int W = fp->W;
    int H = fp->H;
    float sin_T = fp->sin_T;
    float cos_T = fp->cos_T;
    int16_t r_val = fp->r_val;
    int16_t T_signed = fp->T_signed;
    int maxstepshift = fp->maxstepshift;
    int maxiters = fp->maxiters;
    uint8_t *pixbuf = fp->pixbuf;

    int16_t base = (int16_t)((int)T_signed * 10);

    for (int row = row_begin; row < row_end; row++) {
        float py_f = (row + 0.5f) / H * 200.0f - 100.0f;
        for (int col = 0; col < W; col++) {
            float px_f = (col + 0.5f) / W * 320.0f - 160.0f;

            int16_t x_int = (int16_t)lrintf(px_f * 204.0f);
            int16_t y_int = (int16_t)lrintf(py_f * 256.0f);

            int16_t z_int = (int16_t)(0x5600
                - (int16_t)((int32_t)x_int * x_int >> 16)
                - (int16_t)((int32_t)y_int * y_int >> 16));

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

            int16_t orig[3];
            orig[0] = base;
            orig[1] = (int16_t)((uint16_t)base + 0xB000u);
            orig[2] = (int16_t)((uint16_t)base + 0x6000u);

            pixbuf[row * W + col] = intersect(dir, orig, r_val,
                                               maxstepshift, maxiters);
        }
    }
}

static void *worker_func(void *arg)
{
    worker_t *w = (worker_t *)arg;

    for (;;) {
        pthread_barrier_wait(w->bar_start);

        if (w->fp->quit)
            break;

        int H = w->fp->H;
        int row_begin = w->id * H / w->nthreads;
        int row_end   = (w->id + 1) * H / w->nthreads;
        render_rows(w->fp, row_begin, row_end);

        pthread_barrier_wait(w->bar_done);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int W = 320, H = 200;
    int precision = -1;

    if (argc >= 3) {
        W = atoi(argv[1]);
        H = atoi(argv[2]);
        if (W <= 0 || H <= 0) {
            fprintf(stderr,
                "Usage: %s [width height [precision]]\n"
                "  precision 0-8 (default: auto from resolution)\n"
                "  Set THREADS env var for thread count (default 16)\n", argv[0]);
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

    if (precision < 0) {
        int maxdim = W > H ? W : H;
        precision = 0;
        while ((320 << precision) < maxdim && precision < 8)
            precision++;
    }

    int maxstepshift = BASE_MAXSTEPSHIFT + precision;
    int maxiters     = BASE_MAXITERS + precision;
    if (maxstepshift > 14) maxstepshift = 14;

    int nthreads = 16;
    const char *env_threads = getenv("THREADS");
    if (env_threads) {
        nthreads = atoi(env_threads);
        if (nthreads < 1) nthreads = 1;
        if (nthreads > 256) nthreads = 256;
    }
    /* Don't use more threads than rows */
    if (nthreads > H) nthreads = H;

    fprintf(stderr, "puls_parallel: %dx%d, precision=%d (maxstepshift=%d, maxiters=%d), %d threads\n",
            W, H, precision, maxstepshift, maxiters, nthreads);

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

    uint8_t *pixbuf = (uint8_t *)malloc((size_t)W * H);
    if (!pixbuf) {
        fprintf(stderr, "Out of memory\n");
        SDL_Quit();
        return 1;
    }

    /* Shared frame parameters */
    frame_params_t fp = {
        .W = W, .H = H,
        .maxstepshift = maxstepshift, .maxiters = maxiters,
        .pixbuf = pixbuf,
        .quit = 0
    };

    /* Create barriers: nthreads workers + 1 main thread */
    pthread_barrier_t bar_start, bar_done;
    pthread_barrier_init(&bar_start, NULL, nthreads + 1);
    pthread_barrier_init(&bar_done,  NULL, nthreads + 1);

    /* Spawn worker threads */
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * nthreads);
    worker_t  *workers = (worker_t *)malloc(sizeof(worker_t) * nthreads);

    for (int i = 0; i < nthreads; i++) {
        workers[i].id        = i;
        workers[i].nthreads  = nthreads;
        workers[i].fp        = &fp;
        workers[i].bar_start = &bar_start;
        workers[i].bar_done  = &bar_done;
        pthread_create(&threads[i], NULL, worker_func, &workers[i]);
    }

    uint16_t T = 0;
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

        /* Set frame params (workers are idle, waiting on bar_start) */
        fp.sin_T    = sinf((float)T_signed);
        fp.cos_T    = cosf((float)T_signed);
        fp.T_signed = T_signed;

        float r_f = (float)WORD_100H * sinf((float)T_signed * FLOAT_100H);
        fp.r_val = (int16_t)lrintf(r_f);

        /* Release workers */
        pthread_barrier_wait(&bar_start);

        /* Wait for all workers to finish rendering */
        pthread_barrier_wait(&bar_done);

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

    /* Signal workers to quit */
    fp.quit = 1;
    pthread_barrier_wait(&bar_start);

    for (int i = 0; i < nthreads; i++)
        pthread_join(threads[i], NULL);

    pthread_barrier_destroy(&bar_start);
    pthread_barrier_destroy(&bar_done);
    free(threads);
    free(workers);
    free(pixbuf);
    SDL_Quit();
    return 0;
}
