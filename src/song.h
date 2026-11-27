/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Einheitliche Player-Schicht ueber MOD/S3M (XPlayer), XM (XXPlayer) und IT (ITPlayer). */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct Song Song;

/* Laedt Datei, erkennt das Format am Inhalt. NULL bei Fehler (err gefuellt). */
Song *song_open(const char *path, int rate, char *err, size_t errcap);
Song *song_open_sub(const char *path, int rate, int sub, char *err, size_t errcap);  /* sub: Subsong (nur TFMX, sonst ignoriert) */
int song_subsongs(const Song *s);   /* Anzahl Subsongs (mind. 1) */
int song_subsong(const Song *s);    /* aktueller Subsong, 0-basiert */
void song_close(Song *s);
/* rendert n Stereo-Frames; liefert 0 am Songende */
int song_render(Song *s, int16_t *out, int n);

const char *song_title(const Song *s);
const char *song_format(const Song *s);      /* "MOD", "S3M", "XM", "IT" */
int song_channels(const Song *s);
int song_order(const Song *s);               /* aktuelle Order, -1 unbekannt */
int song_norders(const Song *s);

/* ---- Pattern-Anzeige: einheitliche, fertig formatierte Zellen ---- */
typedef struct { int order, pattern, row, nrows; } SongPos;
void song_pos(const Song *s, SongPos *out);              /* aktuell gespielte Zeile */
int  song_pattern_rows(const Song *s, int pattern);     /* Zeilen des Patterns */
int  song_order_pattern(const Song *s, int order);      /* Pattern-Nummer, -1 = Marker/Ende */

/* fxclass: 0 leer, 1 Lautstaerke, 2 Tonhoehe, 3 Panorama, 4 Ablauf (Sprung/Tempo), 5 Sonstiges */
typedef struct {
    char note[4];     /* "C-4", "C#4", "===" Off, "^^^" Cut, "~~~" Fade, "..." leer */
    char inst[3];     /* 2 Zeichen oder ".." */
    char vol[4];      /* Volumenspalte, 3 Zeichen oder "..." */
    char fx[4];       /* Effekt: Buchstabe + 2 Hex/Dezimalzeichen oder "..." */
    uint8_t has_note, fxclass, volclass;
} SongCell;
void song_cell(const Song *s, int pattern, int row, int ch, SongCell *out);

/* ---- Scopes pro Stimme: Ringpuffer buf[kanal*len + pos], len = Zweierpotenz, buf fuer 64 Kanaele ---- */
void song_scope_enable(Song *s, float *buf, int len);
unsigned song_scope_pos(const Song *s);    /* Anzahl bisher geschriebener Frames (Schreibposition = pos & (len-1)) */

/* ---- Kanalzustand fuer die Uebersicht ---- */
/* Kanal-Stummschaltung: Bit n = Kanal n (gilt fuer das laufende und alle folgenden Stuecke) */
/* Paula-Tiefpass: -1 = fuer dieses Format nicht vorgesehen, 0 = aus, 1 = an */
int  song_paula_filter(const Song *s);
void song_toggle_paula_filter(Song *s);
void song_set_mute(unsigned long long mask);
unsigned long long song_get_mute(void);
int song_chan_pan(const Song *s, int ch);                 /* 0 (links) .. 255 (rechts) */
/* Klartext-Name des Effekts bzw. des Volumenspalten-Effekts; NULL wenn keiner. *is_vol = 1 bei Volumenspalte */
const char *song_fx_name(const Song *s, const SongCell *c, int *is_vol);

/* ---- Info window: what the file says about itself ---- */
#define SONG_INFO_LINES 6
typedef struct { char title[48], format[56], tracker[64], voice_unit[16]; int voices; char extra[SONG_INFO_LINES][88]; int nextra; } SongInfo;
void song_info(const Song *s, SongInfo *out);

/* ---- MIDI files (desktop and web): channels, notes for the roll, fast seeking. The view grid is 4/4 measures of 16 rows (orders = measures). ---- */
int  song_is_midi(const Song *s);
int  song_is_fm_s3m(const Song *s);
int  song_fm_voices(const Song *s, int *digital_ch);      /* FM channel count (+ digital channels) */
int  song_fm_in_sample_format(const Song *s);             /* FM in a format that can also hold samples */
void song_midi_idle(Song *s);                                        /* the player is not rendering (pause, end): the MIDI hardware gets its panic */
const char *song_midi_standard(const Song *s, int *known);          /* "MT-32", "GS", "XG", "GM2", "GM": what the SysEx messages of the file say it was written for (known = 0: none named) */
const char *song_midi_lcd(const Song *s);                              /* the MT-32 display text the file sends ("Willy Beamish"), "" if there is none */
typedef struct { float level, out; int program, bank, drum, volume, pan, used; int expr, mod, reverb, chorus, sustain; float bend; } SongMidiChan;      /* level 0..1, CC 7, CC 10 (0..127), used = the file has notes on it */
void song_midi_chan(const Song *s, int ch, SongMidiChan *out);
typedef struct { double t0, t1; uint8_t ch, key, vel; } SongMidiNote;
int  song_midi_notes(const Song *s, double t0, double t1, SongMidiNote *out, int max);          /* the notes that sound at some time in [t0, t1] */
double song_midi_grid_sec(const Song *s, double grid_row);        /* seconds at the grid position order * 16 + row (+ fraction) */
double song_midi_length(const Song *s);
int  song_midi_seek_order(Song *s, int order, double *sec);        /* 1 = done (sec = where it is now); the song is not re-opened */
int  song_midi_seek_by(Song *s, double delta_sec, double *sec);    /* 1 = done, 2 = past the end (the caller goes to the next track) */
const char *song_midi_program_name(int program, int drum);      /* "Acoustic Grand Piano", ... ("Drum Kit" for the drum channel) */
