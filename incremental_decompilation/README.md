# detube

Decompilation of a 256-byte DOS demoscene effect (a rotating textured
cylinder) from x86 assembly into idiomatic C, with real-time SDL viewers
and a Shadertoy GLSL port.

See [HOWTO.md](HOWTO.md) for a step-by-step guide on applying this
process to other sizecoded demos.

## Files

### Assembly (original demo)

| File | Description |
|---|---|
| `tube_orig.asm` | Original 256-byte demo source (NASM, press ESC to exit) |
| `tube_capture.asm` | Modified version that saves 25 frames as BMP then exits |

### C decompilation

| File | Description |
|---|---|
| `dosemu.h` | Generic real-mode DOS emulation header (reusable for other demos) |
| `tube.c` | 1:1 asm-to-C translation using dosemu.h — every instruction maps to one `asm_*()` call with `goto` control flow |
| `tube_rewrite.c` | Final idiomatic C — fully self-contained, no external dependencies beyond libc and libm |
| `texgen.c` | Standalone texture generator — writes `texture.bmp` (see [TEXTURE.md](TEXTURE.md)) |

### Viewers

| File | Description |
|---|---|
| `tube_sdl.c` | SDL1 real-time viewer, 640x400 (2x nearest-neighbor), 25 FPS |
| `tube_big.c` | SDL1 viewer at arbitrary resolution (default 960x600, pass `width height` as arguments) |
| `tube_shadertoy.glsl` | GLSL fragment shader for [Shadertoy](https://www.shadertoy.com) (single-pass, no buffers needed) |

## Build

Requires `gcc`, `nasm`, and SDL 1.2 development headers.

```
make            # build everything
make tube_sdl   # just the SDL viewer
make clean
```

## Run

```bash
# Real-time SDL viewer (2x scaled)
./tube_sdl

# Arbitrary resolution viewer
./tube_big              # default 960x600
./tube_big 1920 1080    # custom size

# Generate reference frames (BMP files)
./tube_rewrite

# Run original demo in DOSBox (headless)
SDL_VIDEODRIVER=dummy dosbox capture.com
```
