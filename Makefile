CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
LDFLAGS = $(shell sdl-config --libs) -lm
SDLCFLAGS = $(shell sdl-config --cflags)

all: tube_sdl lattice_sdl

tube_sdl: tube_sdl.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

lattice_sdl: lattice_sdl.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f tube_sdl lattice_sdl

.PHONY: all clean
