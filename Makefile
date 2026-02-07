CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
LDFLAGS = $(shell sdl-config --libs) -lm
SDLCFLAGS = $(shell sdl-config --cflags)

tube_sdl: tube_sdl.c
	$(CC) $(CFLAGS) $(SDLCFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f tube_sdl

.PHONY: clean
