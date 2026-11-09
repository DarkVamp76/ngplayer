/* IT-Datenmodell + Loader (Impulse Tracker, nach Spezifikation). GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

enum { IT_HAS_NOTE = 1, IT_HAS_INST = 2, IT_HAS_VOL = 4, IT_HAS_CMD = 8 };
typedef struct { uint8_t note, inst, vol, cmd, par, has; } ITCell;   /* note: 0..119, 254 = Cut, 255 = Off, 253 = Fade; cmd: 1=A .. 26=Z */

typedef struct {
    uint8_t flg, num, lpb, lpe, slb, sle;
    int16_t y[25]; uint16_t tick[25];     /* Volume/Pan: 0..64 bzw. -32..32; Pitch/Filter: -32..32 */
} ITEnv;                                   /* flg: 1 an, 2 Loop, 4 Sustain, 0x80 Filter (nur Pitch) */

typedef struct {
    int nna, dct, dca, fadeout, pps, ppc, gbv, dfp, rv, rp, ifc, ifr;
    char name[27];
    uint8_t note_map[120], smp_map[120];
    ITEnv venv, penv, pitchenv;
} ITInst;

typedef struct {
    char name[27];
    uint32_t len, lb, le, slb, sle, c5spd;
    int flags;                 /* IT-Flags: 2 16 Bit, 8 komprimiert, 16 Loop, 32 Sustain-Loop, 64 Pingpong, 128 Sustain-Pingpong */
    int is16, loop, sloop, pingpong, spingpong;
    int gvl, vol, dfp, cvt;
    int vis, vid, vir, vit;    /* Auto-Vibrato: Speed, Depth, Rate (Sweep), Typ */
    void *data;                /* int8_t bzw. int16_t, vorzeichenbehaftet, len Samples (links bzw. mono) */
    void *data2;               /* Stereo-Samples (IT-Flag 4, OpenMPT-Erweiterung): rechter Kanal, sonst NULL */
} ITSmp;

typedef struct {
    char title[27];
    int nord, ninst, nsmp, npat;
    int flags;                 /* 1 Stereo, 4 Instrumente, 8 linear, 16 alte Effekte, 32 Gxx-Link */
    int cwt, cmwt;
    int gv, mv, speed, tempo, sep, pwd;
    uint8_t chpan[64], chvol[64];
    uint8_t orders[256];
    ITInst *inst;              /* 1..ninst (Index 0 unbenutzt) */
    ITSmp *smp;                /* 1..nsmp */
    ITCell **pat;              /* npat Zeiger (NULL = leer 64 Zeilen), rows[] */
    int *rows;
    int nch;                   /* hoechster benutzter Kanal + 1 */
} ITSong;

ITSong *it_load(const uint8_t *buf, size_t len, int *err);   /* NULL bei Fehler, err < 0 */
void it_free(ITSong *s);

typedef struct ITPlayer ITPlayer;
ITPlayer *itp_new(const ITSong *s, int rate);
void itp_free(ITPlayer *p);
int itp_render(ITPlayer *p, int16_t *out, int nframes);   /* liefert 0 am Songende */
