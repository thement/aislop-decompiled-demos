// Tube demo — Shadertoy GLSL port (single pass, no buffers needed)
//
// Paste into the Image tab of a new Shadertoy shader. No Buffer A,
// no iChannel setup — just works.
//
// The temporal trail effect is reconstructed by computing the last
// 8 frames and chaining fade+accumulate. The fade (arithmetic right
// shift by 2) decays contributions by 4x per frame, so 8 frames of
// history gives <0.002% error vs the feedback buffer version.

#define ANIM_SPEED 0.0238037873  // 0x1.860052p-6
#define EYE_DIST   160.0
#define TEX_SCALE  41.0
#define ORIG_W     320.0
#define ORIG_H     200.0
#define ORIG_ROWS  160.0
#define HISTORY    8

// Lattice hash for value noise
float hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Value noise: smooth interpolation of random lattice values
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), f.x),
               mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), f.x), f.y);
}

// 256x256 procedural texture with vertical symmetry.
// The original sequential PRNG produces a strong horizontal brightness
// gradient (Fourier harmonics 1-3) plus small smooth noise (stddev ~3).
// Values range 17-103. Perfectly symmetric at v=128.
float procTex(int u, int v) {
    int sv = v < 128 ? v : 255 - v;
    float fu = float(u);

    // Column-average profile from Fourier analysis of actual texture
    float a = fu * 6.28318530718 / 256.0;
    float base = 62.58
        - 25.58 * cos(a) + 1.79 * sin(a)
        -  2.66 * cos(2.0*a) - 10.61 * sin(2.0*a)
        -  2.76 * cos(3.0*a) -  0.15 * sin(3.0*a);

    // Small smooth noise (correlation length ~7 texels, stddev ~3)
    vec2 p = vec2(fu, float(sv));
    float n = (vnoise(p / 7.0) - 0.5) * 5.0
            + (vnoise(p / 4.0 + 50.0) - 0.5) * 2.0;

    return clamp(floor(base + n), 17.0, 103.0);
}

// Signed-byte arithmetic right shift by 2 (the original fade)
float fade(float v) {
    float s = v >= 128.0 ? v - 256.0 : v;
    return mod(floor(s * 0.25), 256.0);
}

// Compute shade value for one frame at a given pixel
float computeShade(int frame, float col, float row) {
    float angle = float(frame + 1) * ANIM_SPEED;
    int tex_phase = (frame * 8 + 7) & 0xFF;
    int tex_ofs = (tex_phase << 8) | 1;

    float co = cos(angle), sn = sin(angle);
    float y1 = col * co + row * sn;
    float z1 = row * co - col * sn;
    float p  = y1 * co + EYE_DIST * sn;
    float q  = EYE_DIST * co - y1 * sn;

    float radius = sqrt(p * p + z1 * z1);
    int tu = int(round(atan(p, z1) * TEX_SCALE));
    int tv = int(round(q / radius * TEX_SCALE));
    int uv = (tu & 0xFF) | ((tv & 0xFF) << 8);

    int shade;
    int addr = (tex_ofs + uv) & 0xFFFF;
    if ((((addr & 0xFF) + ((addr >> 8) & 0xFF)) & 64) != 0) {
        uv = (uv << 2) & 0xFFFF;
        addr = (tex_ofs + uv) & 0xFFFF;
        if ((((addr & 0xFF) - ((addr >> 8) & 0xFF)) & 0x80) != 0) {
            uv = (uv << 1) & 0xFFFF;
            shade = 208;  // (uint8_t)-48
        } else {
            shade = 240;  // (uint8_t)-16
        }
    } else {
        shade = 251;  // (uint8_t)-5
    }

    int texAddr = (tex_ofs + uv) & 0xFFFF;
    return float((shade + int(procTex(texAddr & 0xFF, (texAddr >> 8) & 0xFF))) & 0xFF);
}

// VGA palette: warm orange 0-127, cool cyan 128-255
vec3 palette(float idx) {
    if (idx < 128.0) {
        float r = floor(idx * 0.5);
        return vec3(r, floor(r * r / 64.0), 0.0) / 63.0;
    } else {
        float d = 256.0 - idx;
        return vec3(0.0, floor(d * 0.5), floor(d * 0.25)) / 63.0;
    }
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;

    // Tube band: central 80% of screen
    float bandH = res.y * (ORIG_ROWS / ORIG_H);
    float bandY0 = (res.y - bandH) * 0.5;
    float py = fragCoord.y;

    if (py < bandY0 || py >= bandY0 + bandH) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Map pixel to original coordinate space
    float col = fragCoord.x / res.x * ORIG_W - ORIG_W * 0.5;
    float row = (py - bandY0) / bandH * ORIG_ROWS - ORIG_ROWS * 0.5;

    // Replay last HISTORY frames: oldest first, chain fade + accumulate
    int frame = iFrame;
    int oldest = max(frame - HISTORY + 1, 0);
    float accum = 0.0;

    for (int f = oldest; f <= frame; f++) {
        accum = fade(accum);
        accum = mod(accum + computeShade(f, col, row), 256.0);
    }

    fragColor = vec4(palette(accum), 1.0);
}
