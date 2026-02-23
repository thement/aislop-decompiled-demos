# Texture analysis

The tube demo uses a 256x256 procedural texture stored as a flat
`uint8_t[65536]` array, addressed as `texture[v*256 + u]`.  It is
generated once at startup by a sequential PRNG that cannot be
parallelised — each iteration depends on the running `accum` value and
on a previously-written neighbour.  This document records the analysis
used to build a GPU-friendly approximation for the Shadertoy GLSL port.

## Generator algorithm

```c
static void generate_texture(void) {
    /* Phase 1: identity fill — texture[i] = i & 0xFF */
    for (int i = 0; i < 65536; i++)
        texture[i] = i & 0xFF;

    uint16_t hash = 0;
    uint8_t accum = 0xC9;   /* leaked from the palette loop */
    uint16_t idx = 0;

    /* Phase 2: 65 536 iterations (idx = 0, 65535, 65534, … 1) */
    do {
        /* 16-bit hash: add index, then rotate by (idx & 15) */
        hash = (uint16_t)((uint32_t)hash + idx);
        int rot = (uint8_t)idx & 15;
        if (rot)
            hash = (hash << rot) | (hash >> (16 - rot));

        /* Extract a small signed perturbation from the hash */
        int8_t tmp = (int8_t)(uint8_t)hash;
        int carry = (tmp >> 4) & 1;
        tmp >>= 5;                          /* range -4 … +3 */

        /* Accumulate perturbation, then average with neighbour */
        uint16_t r = (uint16_t)accum + (uint16_t)(uint8_t)tmp + carry;
        accum = (uint8_t)r;
        carry = r > 0xFF;
        r = (uint16_t)accum + texture[(uint16_t)(idx + 255)] + carry;
        accum = (uint8_t)r >> 1;            /* average → range 0–127 */

        /* Write to both halves (vertical symmetry) */
        texture[idx] = accum;
        texture[idx ^ 0xFF00] = accum;
    } while (--idx);
}
```

Key observations about the algorithm:

- **Iteration order** — `idx` decrements from 0 (wrapping to 65535) down
  to 1.  In row-major terms this visits the texels in reverse, from
  `(v=255, u=255)` down to `(v=0, u=1)`.

- **Neighbour read** — `texture[(idx + 255) & 0xFFFF]` reads the texel
  one column to the left (with wrap).  On the first iterations those
  neighbours still hold their Phase 1 identity values (`i & 0xFF`),
  which equal the column index `u`.  On later iterations the neighbour
  may already be overwritten.  This creates a dependency on both the
  `accum` state and on the spatial position.

- **Vertical symmetry** — the `idx ^ 0xFF00` write mirrors every texel
  across `v = 128`.  Row `v` and row `255 − v` are always identical.

- **Right shift** — the final `>> 1` means output values are at most 127.

## Statistical properties

### Global statistics

| Property | Value |
|---|---|
| Minimum | 17 |
| Maximum | 103 |
| Mean | 62.6 |
| Std. deviation | 19.6 |

The effective range is **17–103**, well within the 0–127 possible from
the `>> 1`.  No texel is ever 0 or 127.

### Spatial smoothness

Autocorrelation of the raw texture:

| Direction | Lag 1 | Lag 4 | Lag 16 |
|---|---|---|---|
| Horizontal | 0.997 | 0.984 | 0.857 |
| Vertical | 0.998 | 0.995 | 0.989 |

The texture is extremely smooth — neighbouring texels differ by at most
a few units.  Vertical correlation is even higher because the averaging
neighbour is horizontal (one column to the left), so the vertical
direction inherits smoothness from the mirror symmetry and from having
no direct vertical perturbation source.

### Horizontal gradient

Row averages are nearly constant (~62.6 for every row), but
column averages vary dramatically:

| Column u | Average |
|---|---|
| 0 | 32.4 |
| 16 | 25.3 (minimum region) |
| 32 | 38.4 |
| 48 | 52.1 |
| 64 | 67.1 |
| 80 | 81.2 |
| 96 | 89.1 |
| 112 | 93.9 (maximum region) |
| 128 | 91.6 |
| 144 | 74.4 |
| 160 | 65.7 |
| 176 | 64.0 |
| 192 | 64.5 |
| 208 | 62.7 |
| 224 | 52.6 |
| 240 | 47.1 |

This is the dominant structure: a broad brightness gradient across the
horizontal axis.  The dark band around `u ≈ 16` and bright band around
`u ≈ 112` arise because the neighbour read `texture[idx + 255]` pulls in
the Phase 1 identity value `u` for early iterations (before those cells
are overwritten), biasing the running average toward column position.

### Fourier decomposition of the column-average profile

A DFT of the 256-point column-average curve yields:

| Harmonic k | Amplitude | Phase (deg) |
|---|---|---|
| 0 (DC) | 62.58 | — |
| 1 | 25.65 | 176 |
| 2 | 10.94 | −104 |
| 3 | 2.76 | −177 |
| 4 | 2.10 | −58 |
| 5+ | < 1.6 | — |

Three harmonics reconstruct the profile to within ±4 texel values.
In GLSL:

```glsl
float a = fu * 6.28318530718 / 256.0;
float base = 62.58
    - 25.58 * cos(a) + 1.79 * sin(a)
    -  2.66 * cos(2.0*a) - 10.61 * sin(2.0*a)
    -  2.76 * cos(3.0*a) -  0.15 * sin(3.0*a);
```

### Noise component (after removing column mean)

| Property | Value |
|---|---|
| Std. deviation | 3.0 |
| Horizontal autocorrelation, lag 1 | 0.894 |
| Horizontal autocorrelation, lag 8 | 0.435 |
| Horizontal autocorrelation, lag 32 | −0.01 |
| Vertical autocorrelation, lag 1 | 0.889 |
| Vertical autocorrelation, lag 8 | 0.647 |
| Vertical autocorrelation, lag 32 | 0.273 |

The noise is small (stddev 3 on a gradient range of ~70) and has a
correlation length of roughly 7 texels horizontally and 10 vertically.
It decorrelates completely by lag 32.

## GLSL approximation

The sequential PRNG cannot run in a fragment shader (each texel depends
on all previous iterations).  The approximation in `tube_shadertoy.glsl`
uses:

1. **Fourier base** — the three-harmonic column-average profile above,
   giving the correct large-scale brightness gradient.

2. **Value noise** — two octaves of smoothstep-interpolated lattice
   noise at scales `/7` and `/4`, centered at zero and scaled to
   roughly match the ±3 stddev.  The vertical coordinate is mirrored
   at `v = 128` before lookup.

3. **Clamping** — `clamp(floor(base + n), 17.0, 103.0)` keeps the
   output in the observed range.

This produces a visually similar texture: a smooth, cloud-like pattern
with the correct brightness gradient, value range, and vertical symmetry.
The fine detail differs from the original (the hash lattice is different
from the sequential PRNG), but since the texture scrolls and fades
rapidly during animation, the difference is not perceptible.
