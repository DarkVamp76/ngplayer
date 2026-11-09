/* TFMX (The Final Musicsystem eXtended, Chris Huelsbeck): Datenmodell, Loader, Replayer.
 * GPL-3.0-or-later. Die Semantik der Makro-/Pattern-/Trackstep-Befehle folgt dem
 * ScummVM-Decoder (audio/mods/tfmx.cpp, GPL-3.0-or-later); Code und Datenstrukturen sind eigene Arbeit. */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint16_t start, end, tempo; } TfmxSub;

typedef struct {
    uint8_t *mdat; size_t nmdat;             /* Songdatei (mdat.* / *.tfx), Kopie */
    int8_t  *smpl; size_t nsmpl;             /* Sampledatei (smpl.* / *.sam), Kopie */
    char text[6][41];                        /* 6 Textzeilen des Headers */
    TfmxSub sub[32];
    uint32_t ts, pt, mt;                     /* Offsets Trackstep-, Pattern-, Makrotabelle */
    int npat, nmac, ntrk;                    /* Eintraege bzw. Trackstep-Zeilen */
    int pal;                                 /* Header-Flag PAL (Bit 1) */
    int v1;                                  /* frueher TFMX-Replayer (ungeskalte Effekte, andere Ausfuehrungsreihenfolge) */
} TfmxSong;

/* Fehler: -1 kein TFMX, -2 beschaedigt/abgeschnitten, -3 Speicher */
TfmxSong *tfmx_load(const uint8_t *mdat, size_t nm, const uint8_t *smpl, size_t ns, int *err);
void tfmx_free(TfmxSong *s);
int tfmx_subsong_count(const TfmxSong *s);                /* benutzbare Subsongs (mind. 1) */

/* Dateinamen: mdat.NAME + smpl.NAME  oder  NAME.tfx + NAME.sam (Gross-/Kleinschreibung egal).
 * tfmx_partner: path = Songdatei -> out = Sampledatei. 0 ok, 1 Songname, aber Partner fehlt, -1 kein Songname. */
int tfmx_partner(const char *path, char *out, size_t cap);
int tfmx_is_sample_name(const char *path);                /* 1 bei smpl.* / *.sam */
int tfmx_is_song_name(const char *path);                  /* 1 bei mdat.* / *.tfx */

typedef struct TfmxPlayer TfmxPlayer;
TfmxPlayer *tfp_new(const TfmxSong *s, int sub, int rate);
void tfp_free(TfmxPlayer *p);
int  tfp_render(TfmxPlayer *p, int16_t *out, int nframes); /* Stereo; liefert 0 am Songende */
void tfp_set_interp(TfmxPlayer *p, int on);                  /* lineare Interpolation (Standard: an) */
void tfp_set_lowpass(TfmxPlayer *p, int on);                 /* Paula-Tiefpass ca. 4,9 kHz (Standard: an) */
void tfp_set_scope(TfmxPlayer *p, float *buf, int len);    /* 4 Kanaele, Ring pro Kanal */
unsigned tfp_scope_pos(const TfmxPlayer *p);
int  tfp_chan_pan(const TfmxPlayer *p, int ch);            /* 0 links, 1 rechts (Amiga: 0,3 links; 1,2 rechts) */
int  tfp_pos(const TfmxPlayer *p);                         /* aktuelle Trackstep-Zeile */
