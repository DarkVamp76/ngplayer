/* DMF-Datenmodell + Loader (X-Tracker, D-LUSiON), nach oeffentlicher Formatbeschreibung. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t inst, note, vol;                 /* note: 0 keine, 1..108 Note, 129..236 gepuffert, 255 Cut */
    uint8_t has;                             /* Bit0 inst, 1 note, 2 vol, 3 Instrument-Fx, 4 Noten-Fx, 5 Volumen-Fx (wie im Format) */
    uint8_t ie_cmd, ie_par, ne_cmd, ne_par, ve_cmd, ve_par;
} DMFCell;

typedef struct {
    int nrows, beat, ntr;                    /* ntr = Kanaele dieses Patterns */
    DMFCell *cell;                           /* nrows * 32 (Zeile r, Kanal c: cell[r*32+c]) */
    uint8_t *gcmd, *gpar;                    /* globale Spur je Zeile (Tempo) */
} DMFPat;

typedef struct {
    char name[256];
    uint32_t len, lb, le, c3spd;             /* Frames */
    int vol, flags, is16, loop, compressed;
    void *data;                              /* int8_t oder int16_t */
} DMFSmp;

typedef struct {
    int version; char title[31], composer[21];
    int nch, npat, nord, nsmp, loop_start, loop_end;
    uint16_t *order;
    DMFPat *pat;
    DMFSmp *smp;                             /* 1-basiert: smp[1..nsmp] */
    char *msg;
} DMFSong;

DMFSong *dmf_load(const uint8_t *b, size_t n, int *err);   /* NULL bei Fehler, *err gesetzt */
void dmf_free(DMFSong *s);

typedef struct DMFPlayer DMFPlayer;
DMFPlayer *dmp_new(const DMFSong *s, int rate);
void dmp_free(DMFPlayer *p);
int dmp_render(DMFPlayer *p, int16_t *out, int nframes);          /* liefert 0 am Songende */
void dmp_set_scope(DMFPlayer *p, float *buf, int len);
unsigned dmp_scope_pos(const DMFPlayer *p);
int dmp_chan_pan(const DMFPlayer *p, int ch);
void dmp_pos(const DMFPlayer *p, int *ord, int *pat, int *row);
