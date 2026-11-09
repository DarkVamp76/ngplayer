/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Einheitliche Player-Schicht ueber MOD/S3M (XPlayer), XM (XXPlayer) und IT (ITPlayer). */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct Song Song;

/* Laedt Datei, erkennt das Format am Inhalt. NULL bei Fehler (err gefuellt). */
Song *song_open(const char *path, int rate, char *err, size_t errcap);
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
void song_set_mute(unsigned long long mask);
unsigned long long song_get_mute(void);
int song_chan_pan(const Song *s, int ch);                 /* 0 (links) .. 255 (rechts) */
/* Klartext-Name des Effekts bzw. des Volumenspalten-Effekts; NULL wenn keiner. *is_vol = 1 bei Volumenspalte */
const char *song_fx_name(const Song *s, const SongCell *c, int *is_vol);
