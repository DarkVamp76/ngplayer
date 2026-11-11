# ngPlayer - Makefile
CC      ?= gcc
CFLAGS  ?= -O2
SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS   ?= $(shell sdl2-config --libs)
CFLAGS  += -w -Isrc -Isrc/core -Ibuild $(SDL_CFLAGS)
LIBS     = $(SDL_LIBS) -lm
CORE     = $(addprefix src/core/,replay.c mod_load.c s3m_load.c xm_load.c xm_replay.c it_load.c it_replay.c it_unpack.c period.c dmf_load.c dmf_replay.c tfmx_load.c tfmx_replay.c)
SRC      = src/main.c src/song.c src/net.c $(CORE)
PREFIX  ?= /usr/local

all: build/ngplayer

VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

# erzeugt build/version.h aus 'git describe' (nur neu geschrieben, wenn sich der Inhalt aendert)
build/version.h: FORCE
	@mkdir -p build
	@echo '#define NGPLAYER_VERSION "$(VERSION)"' > $@.tmp; if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

FORCE:

build/ngplayer: $(SRC) build/version.h $(wildcard src/*.h src/*.inc src/core/*.h)
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

# WebAssembly-Build (braucht Emscripten und das Kit-Repo 'ngweb' neben diesem Ordner)
NGWEB ?= ../ngweb
web: build/version.h
	$(MAKE) -f $(NGWEB)/ngweb.mk APPDIR=$(CURDIR) NAME=ngPlayer WEB_EXPORTS=_main,_ng_web_add,_ng_web_ma_done,_ng_web_paste,_ng_web_parse_id,_ng_web_size HINT="Drop MOD, S3M, XM, IT, DMF or TFMX files here (TFMX: mdat.* and smpl.* together)" SRC="$(SRC)"

.PHONY: all install clean FORCE web

build/nettest: tools/nettest.c src/net.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/nettest.c src/net.c
