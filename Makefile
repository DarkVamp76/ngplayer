# ngPlayer - Makefile
CC      ?= gcc
CFLAGS  ?= -O2
SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS   ?= $(shell sdl2-config --libs)
CFLAGS  += -w -Isrc -Isrc/core $(SDL_CFLAGS)
LIBS     = $(SDL_LIBS) -lm
CORE     = $(addprefix src/core/,replay.c mod_load.c s3m_load.c xm_load.c xm_replay.c it_load.c it_replay.c it_unpack.c period.c)
SRC      = src/main.c src/song.c src/net.c $(CORE)
PREFIX  ?= /usr/local

all: build/ngplayer

build/ngplayer: $(SRC) $(wildcard src/*.h src/*.inc src/core/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

# Hilfswerkzeug: Modul -> WAV (ohne SDL, fuer Tests)
build/ngrender: tools/render.c $(CORE)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/render.c $(filter-out src/core/it_load.c src/core/it_replay.c,$(CORE)) $(LIBS)

install: build/ngplayer
	install -Dm755 build/ngplayer $(DESTDIR)$(PREFIX)/bin/ngplayer
	install -Dm644 packaging/ngplayer.desktop $(DESTDIR)$(PREFIX)/share/applications/ngplayer.desktop
	install -Dm644 assets/ngplayer.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/ngplayer.svg

clean:
	rm -rf build

.PHONY: all install clean

build/nettest: tools/nettest.c src/net.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/nettest.c src/net.c
