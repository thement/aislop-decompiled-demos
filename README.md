# detube0

Decompilation of a DOS demoscene tunnel effect (`tube_orig.asm`) into portable C using SDL 1.2.

The original is a 16-bit real-mode x86 COM program that renders a rotating 3D tunnel with texture mapping and motion blur using VGA mode 13h (320x200, 256 colors) and x87 FPU math.

## Building

```
make
```

Requires `libsdl1.2-dev` and a C compiler with math library support.

## Running

```
./tube_sdl
```

Press ESC or close the window to exit.
