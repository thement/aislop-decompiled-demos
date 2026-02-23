# How to decompile a 256-byte DOS demo to idiomatic C

A step-by-step process for turning a sizecoded .COM demoscene effect into
clean, readable C that reproduces the original pixel-for-pixel. Each step
is verified by comparing rendered frames against DOSBox reference output.

## Prerequisites

- The original demo as a NASM source file (called `demo.asm` below)
- `dosemu.h` from this repository — a reusable real-mode DOS emulation header
- DOSBox (or dosemu2) to run .COM files and produce reference frames
- `gcc`, `cmp` for building and verifying the C output

## Step 0: Capture reference frames

Most demos run in an infinite loop waiting for a keypress. To get reference
output, modify the demo to save each frame as a BMP file and exit after a
fixed number of frames.

Find where the demo's main loop restarts (typically after a keyboard check
like `in al, 60h` or a `jmp` back to the top). Replace that with a call to
a frame-saving routine, a frame counter, and an exit condition.

Build and run in DOSBox (use a short output filename — DOS has an 8.3
filename limit):

```
nasm -f bin -o capture.com demo_capture.asm
SDL_VIDEODRIVER=dummy dosbox capture.com
```

`SDL_VIDEODRIVER=dummy` runs DOSBox headlessly, which is useful for
automated capture on a server or in a script.

### BMP writer assembly

A reusable routine for saving VGA mode 13h frames (320x200, 256 colors) as
8-bit indexed BMP files from real-mode DOS. It reads the palette back from
the VGA DAC, writes a standard BMP (14-byte file header, 40-byte info
header, 1024-byte palette, 64000 bytes of pixel data bottom-to-top), and
uses only DOS `int 21h` file I/O.

Embed these data structures and the `save_frame` routine after the demo's
code. The main loop calls `save_frame`, increments a counter, and exits
when enough frames have been captured.

```asm
PALBUF	equ	<address>	; 1024 bytes of scratch space for palette

; --- frame capture (replaces keyboard check) ---
	call	save_frame
	inc	byte [frame_count]
	cmp	byte [frame_count],25
	jae	.exit
	jmp	MAIN
.exit:
	mov	al,03h
	int	10h
	int	20h

; --- capture state ---
frame_count	db	0
fname		db	'frame000.bmp',0

; --- BMP file header (14 bytes) ---
bmp_filehdr:
	db	'B','M'
	dd	65078		; file size = 14+40+1024+64000
	dd	0		; reserved
	dd	1078		; pixel data offset = 14+40+1024

; --- BMP info header (40 bytes) ---
bmp_infohdr:
	dd	40		; header size
	dd	320		; width
	dd	200		; height
	dw	1		; color planes
	dw	8		; bits per pixel
	dd	0		; compression = BI_RGB
	dd	64000		; image size = 320*200
	dd	0		; X pixels/meter
	dd	0		; Y pixels/meter
	dd	256		; colors used
	dd	0		; important colors

; --- save current frame as BMP ---
save_frame:
	pusha

	; update filename digits from frame_count
	mov	al,[frame_count]
	aam			; AH=al/10  AL=al%10
	add	ax,3030h	; convert to ASCII
	mov	[fname+7],al	; ones digit
	mov	[fname+6],ah	; tens digit

	; read VGA palette into PALBUF (256 entries, BGRA)
	xor	al,al
	mov	dx,3C7h
	out	dx,al		; start reading from color 0
	mov	dx,3C9h
	mov	di,PALBUF
	mov	cx,256
.rpal:
	in	al,dx		; R (6-bit)
	shl	al,2
	mov	ah,al
	in	al,dx		; G (6-bit)
	shl	al,2
	mov	bl,al
	in	al,dx		; B (6-bit)
	shl	al,2
	mov	[di],al		; store B
	mov	[di+1],bl	; store G
	mov	[di+2],ah	; store R
	mov	byte [di+3],0	; padding
	add	di,4
	loop	.rpal

	; create file
	mov	ah,3Ch
	xor	cx,cx
	mov	dx,fname
	int	21h
	jc	.ret
	mov	bp,ax		; save file handle

	; write BMP file header (14 bytes)
	mov	bx,bp
	mov	ah,40h
	mov	cx,14
	mov	dx,bmp_filehdr
	int	21h

	; write BMP info header (40 bytes)
	mov	bx,bp
	mov	ah,40h
	mov	cx,40
	mov	dx,bmp_infohdr
	int	21h

	; write palette (1024 bytes)
	mov	bx,bp
	mov	ah,40h
	mov	cx,1024
	mov	dx,PALBUF
	int	21h

	; write pixel data bottom-to-top (200 rows x 320 bytes)
	mov	si,199
.row:
	push	ds
	push	si
	mov	ax,320
	mul	si		; AX = row * 320
	mov	dx,ax		; DS:DX = buffer for int 21h
	push	word 0A000h
	pop	ds		; DS = video memory segment
	mov	bx,bp
	mov	ah,40h
	mov	cx,320
	int	21h
	pop	si
	pop	ds
	dec	si
	jns	.row

	; close file
	mov	bx,bp
	mov	ah,3Eh
	int	21h

.ret:
	popa
	ret
```

Key details:
- The demo's pixel buffer address may need to be moved to make room for
  the save routine and BMP headers.
- Palette is read back from the VGA DAC (port 3C7h for read address,
  3C9h for data), converted from 6-bit to 8-bit, and stored in BGRA order.
- Pixel data is read directly from VGA segment A000h, written bottom-to-top
  as BMP requires.

## Step 1: Create a 1:1 C emulation

**`dosemu.h`** already exists in this repository. It defines the machine
state (register unions, flags, 64K memory segments, VGA palette, x87 FPU
stack) and provides `asm_*()` functions for common x86 instructions. If
the demo uses instructions not yet covered, add them to `dosemu.h`.

Create **`demo.c`** — a line-by-line translation of `demo.asm` where each
instruction maps to exactly one `asm_*()` call and all control flow uses
`goto`. The program loads the original .COM binary at offset 0x100 (so
the data block is accessible) and writes its own BMP frames for comparison.

The prompt for this step:

> Create demo.c using dosemu.h. Replicate each assembly instruction from
> demo.asm 1:1 using `asm_*()` calls with `goto` for control flow. Add
> any missing instructions to dosemu.h. Write reference frames as BMP files.

Build and verify:

```bash
gcc -O2 -o demo demo.c -lm
./demo
# Compare against DOSBox reference frames
for i in $(seq 0 24); do
    cmp cframe$(printf %03d $i).bmp FRAME$(printf %03d $i).BMP || echo "MISMATCH $i"
done
```

Every frame must match byte-for-byte before proceeding.

## Step 2: Incrementally replace asm calls with C

The prompt:

> Rewrite the assembly instruction calls one by one, replacing each
> `asm_*()` call with equivalent inline C. Test after every change.

This is the core of the process. Each step replaces a category of
`asm_*()` calls with direct C, tests, and commits. A good order:

1. **Simple data movement** — `asm_mov`, `asm_lea`, `asm_not`, memory
   loads/stores become direct assignments.
2. **Stack and string ops** — `asm_push`/`asm_pop` become local variables,
   `asm_rep_movsw` becomes a loop or `memcpy`.
3. **FPU instructions** — `asm_f*` become direct `ST()` macro operations
   and `sin`/`cos`/`sqrt`/`atan2`/`lrint` calls.
4. **Flag-dependent arithmetic** — `asm_sar`+`js`, `asm_cmp`+`jnz`,
   `asm_adc` chains, shifts, and multiplies become inline C with explicit
   carry/flag variables where needed.
5. **I/O ports** — `asm_out` for palette setup becomes direct array writes.
6. **Remove dosemu.h** — inline all remaining helpers, replace the
   emulation struct with standalone arrays and variables.

**Test after every change:**

```bash
gcc -O2 -o demo_rewrite demo_rewrite.c -lm && ./demo_rewrite > /dev/null && \
for i in $(seq 0 24); do
    cmp cframe$(printf %03d $i).bmp rframe$(printf %03d $i).bmp || exit 1
done && echo "OK"
```

## Step 3: Restructure into idiomatic C

The prompt:

> Keep rewriting until it looks like clean, idiomatic C. Always verify
> that the output frames still match the reference.

This transforms the register-name-laden C into readable code:

1. **Replace `goto` with structured loops** — identify loop patterns in the
   label/goto structure and convert them to `for`, `while`, or `do-while`.

2. **Eliminate dead stores** — remove memory writes that are immediately
   overwritten or never read. Replace memory round-trips (write then
   read-back) with direct expressions.

3. **Remove the emulation struct** — split into static global arrays
   (palette, texture, framebuffer), plain local variables, and extracted
   functions (e.g. `generate_palette()`, `generate_texture()`).

4. **Derive closed-form math from FPU stack operations** — trace through
   all push/pop/ST() operations to discover the underlying algorithm. For
   example, a series of fsincos/fmul/fsub/fadd may turn out to be two 2D
   rotations followed by a cylindrical projection. Replace the stack
   simulation with named variables.

5. **Final cleanup** — meaningful variable names, symbolic constants
   (`W`/`H`/`ROWS`), comments explaining the algorithm.

## Step 4: Extract constants and remove binary dependency

The prompt:

> Improve readability: hardcode the magic constants from the binary,
> remove the file dependency, and clean up the code.

Sizecoded demos embed constants in creative ways — as instruction
immediates, overlapping data blocks, or even by reinterpreting instruction
bytes as data. Extract these into `#define`s:

- **Instruction immediates** — a `cmp reg, N` instruction's operand may
  double as a constant read elsewhere (via `equ $-2` pointing into the
  instruction encoding).
- **Data blocks** — raw bytes after the code, read as integers or IEEE 754
  floats by the FPU.
- **C99 hex float literals** (`0x1.ABCDp-6f`) give bit-exact representation
  of float constants extracted from the binary.

**Always verify extracted constants with a test program.** Read the actual
bytes from the .COM file and compare against your hardcoded values. Manual
hex analysis is error-prone — a single byte-order or offset mistake will
produce wrong constants that compile fine but produce different output.

## Step 5: Create real-time SDL viewers

Once you have a verified `demo_rewrite.c`, you can create interactive
viewers that render the demo in real time.

### demo_sdl.c — pixel-exact SDL viewer

A straightforward port: replace the BMP writer with SDL video output,
run the same rendering code in a loop, and display at 2x nearest-neighbor
scaling (640x400 for a 320x200 demo).

The prompt:

> Create demo_sdl.c based on demo_rewrite.c. Use SDL 1.2 to display the
> demo in real time at 2x nearest-neighbor scaling (640x400), capped at
> 25 FPS. Replace the BMP writer with SDL surface blitting.

Key points:
- Use `SDL_SetVideoMode(640, 400, 32, SDL_SWSURFACE)` for a 32-bit surface.
- Render into a 320x200 byte buffer, then blit with pixel doubling.
- Cap the frame rate with `SDL_GetTicks()` / `SDL_Delay()`.
- Handle `SDL_QUIT` and ESC key to exit cleanly.

Build:

```bash
gcc -O2 $(sdl-config --cflags) -o demo_sdl demo_sdl.c -lm $(sdl-config --libs)
```

### demo_big.c — arbitrary resolution viewer

This version maps each output pixel to the original coordinate space
using floating-point math, allowing rendering at any resolution.

The prompt:

> Create demo_big.c based on demo_sdl.c. Instead of rendering at 320x200
> and scaling, compute each pixel by mapping its screen position to the
> original coordinate space with floating-point math. Accept width and
> height as command-line arguments (default 960x600).

Key changes from demo_sdl.c:
- Map each pixel `(x, y)` to original coordinates:
  ```c
  double col = (x + 0.5) / width * ORIG_W - ORIG_W / 2.0;
  double row = (y + 0.5) / tube_h * ORIG_ROWS - ORIG_ROWS / 2.0;
  ```
- The tube occupies the central 80% of the screen height
  (`ORIG_ROWS / ORIG_H = 160 / 200`), with black bars above and below.
- All integer texture/shading arithmetic stays the same — only the
  pixel-to-world mapping changes.

Build:

```bash
gcc -O2 $(sdl-config --cflags) -o demo_big demo_big.c -lm $(sdl-config --libs)
./demo_big              # default 960x600
./demo_big 1920 1080    # custom resolution
```

## Gotchas

Common pitfalls when decompiling sizecoded demos:

- **Register state leaks between sections.** Sizecoded demos never waste
  bytes zeroing registers. The value left in a register by one section
  (e.g. `dl = 0xC9` after a palette I/O loop) may be a critical initial
  value for the next section (e.g. a texture PRNG seed). Rewriting one
  section without preserving leaked register state will silently break
  everything downstream.

- **Implicit values from loop termination.** After a `loop` instruction
  (which decrements CX), CX is 0 — but other registers may hold values
  derived from the last iteration. For example, if a loop copies CL to BL,
  then BL=1 after the last iteration (CX was 1 before the final decrement).
  These implicit values get used without comment in the rest of the code.

- **Iteration order matters in self-modifying data.** A texture generation
  PRNG that reads neighboring texels will produce different results depending
  on whether those neighbors have already been overwritten. A
  `do { ... } while(--idx)` with `uint16_t idx = 0` iterates
  0, 65535, 65534, ..., 1 — not 0, 1, 2, ....

- **Constants embedded in instructions.** `EYE equ $-2` makes a constant
  alias the immediate operand of a preceding `cmp` instruction. The FPU
  then reads that address as a 16-bit integer. This is a standard
  size-coding trick — one sequence of bytes serves as both an instruction
  and a data constant.

- **Overlapping data.** A 4-byte float and a 2-byte integer may share the
  same starting address. The low 2 bytes of the float ARE the integer.
  Getting either one wrong will break different parts of the rendering.

## Housekeeping: Makefile and .gitignore

Create a `Makefile` with targets for all programs:

```makefile
CC      = gcc
CFLAGS  = -O2 -Wall
LDLIBS  = -lm
NASM    = nasm

SDL_CFLAGS := $(shell sdl-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl-config --libs 2>/dev/null)

all: demo demo_rewrite demo_sdl demo_big demo_orig.com capture.com

demo: demo.c dosemu.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

demo_rewrite: demo_rewrite.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

demo_sdl: demo_sdl.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(LDLIBS) $(SDL_LIBS)

demo_big: demo_big.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(LDLIBS) $(SDL_LIBS)

demo_orig.com: demo.asm
	$(NASM) -f bin -o $@ $<

capture.com: demo_capture.asm
	$(NASM) -f bin -o $@ $<

clean:
	rm -f demo demo_rewrite demo_sdl demo_big demo_orig.com capture.com

distclean: clean
	rm -f *.bmp *.BMP

.PHONY: all clean distclean
```

Create a `.gitignore` to keep build outputs and frame captures out of
version control:

```
# Build outputs
demo
demo_rewrite
demo_sdl
demo_big
*.com

# Frame captures
*.bmp
*.BMP
```

## File overview

| File | Purpose |
|---|---|
| `demo.asm` | Original demo source |
| `demo_capture.asm` | Modified demo that saves 25 BMP frames then exits |
| `dosemu.h` | Real-mode DOS emulation header (reusable, extend as needed) |
| `demo.c` | 1:1 asm-to-C translation using dosemu.h and `goto` |
| `demo_rewrite.c` | Final idiomatic C, fully self-contained |
| `demo_sdl.c` | Real-time SDL1 viewer with 2x scaling |
| `demo_big.c` | Arbitrary-resolution SDL1 viewer |
| `Makefile` | Build system for all targets |
| `.gitignore` | Excludes binaries and BMP captures |
