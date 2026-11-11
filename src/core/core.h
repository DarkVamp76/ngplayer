/* Wiedergabe-Kern (Prototyp: MOD). GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t note, inst, eff, par, vol1; } XCell;   /* vol1: 0 = keine Volumenspalte, sonst Wert+1 */
typedef struct {
    uint32_t len, ls, le;      /* in Samples */
    int loop;                  /* Flag 8 */
    int is16;                  /* data zeigt auf int16_t, Laengen in Samples */
    int vol;                   /* 0..64 */
    uint32_t c2spd;
    int8_t *data;
    int pingpong;              /* XM: Loop-Typ 2 */
    int finetune, relnote, pan;/* XM: -128..127, -96..95, 0..255 */
} XSmp;
/* XM-Huellkurve */
typedef struct {
    int n;                     /* Punkte */
    uint16_t x[12]; uint8_t y[12];
    int sus, ls, le;           /* Sustain-/Loop-Punktindex */
    int on, susen, loopen;     /* Flags */
} XEnv;
typedef struct {
    uint8_t keymap[96];
    XEnv venv, penv;
    int vibtype, vibsweep, vibdepth, vibrate;
    int fadeout;
    int nsmp; int smp[16];     /* Indizes in XSong.xsmp */
} XInst;
typedef struct {
    char title[21];
    int nch, nord, npat, nsmp, xchn;
    uint8_t orders[256];
    int fmt;                   /* Format-ID wie im Original ([0x4e008]): 1 MOD, 2 S3M ... */
    int fast;                  /* Original [0x4e010] */
    int gvol;                  /* globale Lautstaerke 1..255 */
    int chpan[32];             /* Start-Pan je Kanal, -1 = Standard */
    int chen[32];              /* Kanal aktiv */
    XCell *pat;                /* npat * 64 * nch */
    XSmp smp[100];             /* 1..99 */
    int maxvol;                /* 0x40 */
    int speed, tempo;
    /* XM (fmt 4): Instrumente 1..ninst, Samples flach in xsmp[]; XCell.vol1 = roher Volumenspalten-Byte (0 = keiner) */
    XInst *inst; int ninst;
    XSmp *xsmp; int nxsmp;
    int linear;                /* XM: lineare Frequenztabelle */
    int restart;
    int *prows;                /* XM: Zeilen je Pattern (sonst 64) */
    int amiga;                 /* MOD: echtes Amiga-Format (M.K., M!K!, FLT4, 15 Samples) -> Paula-Tiefpass */
} XSong;

int  xmod_load(XSong *s, const uint8_t *buf, size_t len);   /* 0 = ok */
int  xxm_load(XSong *s, const uint8_t *buf, size_t len);    /* 0 = ok */
int  xs3m_load(XSong *s, const uint8_t *buf, size_t len);   /* 0 = ok */
int  stm_probe(const uint8_t *buf, size_t len);             /* 1 = Scream Tracker 2 file */
int  xstm_load(XSong *s, const uint8_t *buf, size_t len);   /* 0 = ok (STM -> S3M replay) */
int  ptm_probe(const uint8_t *buf, size_t len);             /* 1 = Poly Tracker file */
int  xptm_load(XSong *s, const uint8_t *buf, size_t len);   /* 0 = ok (PTM -> S3M replay) */
int  ult_probe(const uint8_t *buf, size_t len);             /* 1 = UltraTracker file */
int  xult_load(XSong *s, const uint8_t *buf, size_t len);   /* 0 = ok (ULT -> S3M replay) */
int  s669_probe(const uint8_t *buf, size_t len);            /* 1 = Composer 669 / UNIS 669 file */
int  xs669_load(XSong *s, const uint8_t *buf, size_t len);  /* 0 = ok (669 -> S3M replay) */
void xsong_free(XSong *s);

typedef struct XPlayer XPlayer;
typedef struct XXPlayer XXPlayer;
XXPlayer *xxm_new(const XSong *s, int rate);
void xxm_free(XXPlayer *p);
int xxm_render(XXPlayer *p, int16_t *out, int n);
int xxm_order(const XXPlayer *p);
XPlayer *xplay_new(const XSong *s, int rate);
void xplay_free(XPlayer *p);
/* rendert n Stereo-Frames (int16 interleaved); liefert 0 am Songende */
int  xplay_render(XPlayer *p, int16_t *out, int nframes);
int  xplay_order(const XPlayer *p);
int  xplay_get_lowpass(const XPlayer *p);   /* -1 = nicht verfuegbar (XM) */
void xplay_set_lowpass(XPlayer *p, int on); /* Paula-Tiefpass (Standard: nur bei Amiga-MODs an) */
void xplay_set_fixe8(XPlayer *p, int on);   /* 1 = S8x/E8x setzen echten Pan (Original: fest 170) */
