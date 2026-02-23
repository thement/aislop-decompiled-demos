CC      = gcc
CFLAGS  = -O2 -Wall
LDLIBS  = -lm
NASM    = nasm

SDL_CFLAGS := $(shell sdl-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl-config --libs 2>/dev/null)

all: tube tube_rewrite texgen tube_sdl tube_big tube_orig.com capture.com

tube: tube.c dosemu.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

tube_rewrite: tube_rewrite.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

texgen: texgen.c
	$(CC) $(CFLAGS) -o $@ $<

tube_sdl: tube_sdl.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(LDLIBS) $(SDL_LIBS)

tube_big: tube_big.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(LDLIBS) $(SDL_LIBS)

tube_orig.com: tube_orig.asm
	$(NASM) -f bin -o $@ $<

capture.com: tube_capture.asm
	$(NASM) -f bin -o $@ $<

clean:
	rm -f tube tube_rewrite texgen tube_sdl tube_big tube_orig.com capture.com

distclean: clean
	rm -f *.bmp *.BMP

.PHONY: all clean distclean
