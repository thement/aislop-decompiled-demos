CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
LDFLAGS = $(shell sdl-config --libs) -lm
SDLCFLAGS = $(shell sdl-config --cflags)

all: tube_sdl lattice_sdl puls_sdl tube_big lattice_big puls_big puls_parallel lattice_parallel

tube_sdl: tube_sdl.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

lattice_sdl: lattice_sdl.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

puls_sdl: puls_sdl.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

tube_big: tube_big.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

lattice_big: lattice_big.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

puls_big: puls_big.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

puls_parallel: puls_parallel.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS) -lpthread

lattice_parallel: lattice_parallel.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS) -lpthread

clean:
	rm -f tube_sdl lattice_sdl puls_sdl tube_big lattice_big puls_big puls_parallel lattice_parallel

.PHONY: all clean
