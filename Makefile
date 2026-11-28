# ngPlayer - Makefile
CC      ?= gcc
CFLAGS  ?= -O2
SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS   ?= $(shell sdl2-config --libs)
CFLAGS  += -w -Isrc -Isrc/core -Ibuild $(SDL_CFLAGS)
LIBS     = $(SDL_LIBS) -lm $(if $(filter Linux,$(shell uname -s)),-lasound)
CORE     = $(addprefix src/core/,replay.c mod_load.c s3m_load.c stm_load.c ptm_load.c ult_load.c s669_load.c mtm_load.c asylum_load.c amf_load.c dsm_load.c sfx_load.c ahx.c med_load.c psm_load.c far_load.c mdl_load.c xm_load.c dbm_load.c xm_replay.c it_load.c dtm_load.c okt_load.c tp_common.c tp_2x3x.c tp_4x.c tp_5x6x.c aon.c sa.c fc.c bp.c sd1.c sd2.c umx.c gdm_load.c opl_replay.c opl/opl3.c ym7128b/YM7128B_emu.c adgold.c a2m_unpack.c a2m_load.c a2m_replay.c herad.c rad.c lds.c it_replay.c it_unpack.c period.c dmf_load.c dmf_replay.c tfmx_load.c tfmx_replay.c midi.c midi_hw.c ml_load.c ml_tables.c ml_play.c ml_effects.c ml_parteffects.c ml_waveeffects.c ml_replay.c)
SRC      = src/main.c src/gfx_gl.c src/song.c src/net.c $(CORE)
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
	$(CC) $(CFLAGS) -o $@ tools/render.c $(filter-out src/core/it_load.c src/core/dtm_load.c src/core/okt_load.c src/core/tp_common.c src/core/tp_2x3x.c src/core/tp_4x.c src/core/tp_5x6x.c src/core/aon.c src/core/sa.c src/core/fc.c src/core/bp.c src/core/sd1.c src/core/sd2.c src/core/umx.c src/core/gdm_load.c src/core/it_replay.c,$(CORE)) $(LIBS)

# Hilfswerkzeug: beliebiges Modul (alle Formate von song.c) -> WAV, fuer den Vergleich mit libopenmpt
build/songrender: tools/songrender.c src/song.c $(CORE)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/songrender.c src/song.c $(CORE) -lm $(if $(filter Linux,$(shell uname -s)),-lasound)

install: build/ngplayer
	install -Dm755 build/ngplayer $(DESTDIR)$(PREFIX)/bin/ngplayer
	install -Dm644 packaging/ngplayer.desktop $(DESTDIR)$(PREFIX)/share/applications/ngplayer.desktop
	install -Dm644 packaging/ngplayer-link.desktop $(DESTDIR)$(PREFIX)/share/applications/ngplayer-link.desktop
	install -Dm644 assets/ngplayer.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/ngplayer.svg
	install -Dm644 packaging/ngplayer-mime.xml $(DESTDIR)$(PREFIX)/share/mime/packages/ngplayer.xml
	for f in assets/mime/*.svg; do install -Dm644 $$f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/mimetypes/$$(basename $$f); done
	@if [ -f assets/soundfont/GeneralUser-GS.sf2 ]; then \
		install -Dm644 assets/soundfont/GeneralUser-GS.sf2 $(DESTDIR)$(PREFIX)/share/ngplayer/GeneralUser-GS.sf2; \
		install -Dm644 assets/soundfont/LICENSE-GeneralUser-GS.txt $(DESTDIR)$(PREFIX)/share/ngplayer/LICENSE-GeneralUser-GS.txt; \
	else echo "note: no SoundFont in assets/soundfont (make soundfont): MIDI files need one"; fi
	@if [ -z "$(DESTDIR)" ]; then \
		update-mime-database $(PREFIX)/share/mime >/dev/null 2>&1 || true; \
		update-desktop-database $(PREFIX)/share/applications >/dev/null 2>&1 || true; \
		gtk-update-icon-cache -q -t -f $(PREFIX)/share/icons/hicolor >/dev/null 2>&1 || true; \
	fi

clean:
	rm -rf build

# WebAssembly-Build (braucht Emscripten und das Kit-Repo 'ngweb' neben diesem Ordner)
NGWEB ?= ../ngweb
web: build/version.h
	$(MAKE) -f $(NGWEB)/ngweb.mk APPDIR=$(CURDIR) NAME=ngPlayer WEB_NATIVE=ngplayer WEB_COUNT=1 WEB_SOUNDFONT=GeneralUser-GS.sf2 WEB_EXPORTS=_main,_ng_web_add,_ng_web_ma_done,_ng_web_paste,_ng_web_parse_id,_ng_web_size,_ng_web_media HINT="Drop module files here (MOD, S3M, XM, IT, MIDI ... and 28 more formats)" HINTFULL="Drop MOD, S3M, STM, PTM, ULT, 669, MTM, MED, PSM, FAR, MDL, DBM, DSM, AMF, SFX, AHX, XM, IT, DMF, DTM, AMD, MAD, A2M, RAD, LDS, HSQ, SQX, SDB, AGD, HA2, MIDI (MID, KAR, RMI, M32) or TFMX files here (TFMX: mdat.* and smpl.* together)" SRC="$(SRC)"

# the SoundFont for MIDI files (GeneralUser GS, fetched and checked into assets/soundfont/, not kept in git)
soundfont:
	sh tools/fetch-soundfont.sh

.PHONY: all install clean FORCE web soundfont

build/nettest: tools/nettest.c src/net.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/nettest.c src/net.c
