/* AdLib (Yamaha OPL2) FM music: loaders and replayer for the AMUSIC Adlib Tracker (.amd) and the Mlat Adlib Tracker / Mad Tracker 2 (.mad).
 * The chip is Nuked-OPL3 (src/core/opl, LGPL-2.1+). GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t note, command, inst, p1, p2; } OplCell;    /* note 1..96, 127 = key off; effect parameters as two digits */

typedef struct OplSong {
    char title[25], author[25], kind[24];     /* kind: "AMUSIC Adlib Tracker" or "Mlat Adlib Tracker" */
    int ninst; char instname[26][24]; uint8_t inst[26][11]; /* OPL instrument: connection/feedback, then modulator and carrier registers */
    int length, nop, nrows, nchan;            /* orders, patterns, rows per pattern, channels (9) */
    uint8_t order[256];
    uint16_t *trackord;                       /* nop * nchan: track number + 1, 0 = empty */
    OplCell *tracks;                          /* ntracks * nrows */
    int ntracks;
    int decimal;                              /* effect parameters are decimal (AMD) */
    int initspeed, bpm; double fixed_refresh; /* MAD: timer byte */
    int is_mad;
} OplSong;

int opl_probe(const uint8_t *buf, size_t len);                /* 1 = AMD or MAD file */
OplSong *opl_load(const uint8_t *buf, size_t len, int *err);  /* NULL on error, err < 0 */
void opl_free(OplSong *s);

typedef struct OplPlayer OplPlayer;
OplPlayer *opl_new(const OplSong *s, int rate);
struct A2mSong; OplPlayer *opl_new_a2m(const struct A2mSong *s, int rate);   /* Adlib Tracker 2: OPL3, 18 channels */
struct HeradSong; OplPlayer *opl_new_herad(const struct HeradSong *s, int rate);   /* HERAD: OPL2, or OPL3 with 18 channels for AGD */
struct RadSong; OplPlayer *opl_new_rad(const struct RadSong *s, int rate);       /* Reality ADlib Tracker: OPL2 (version 1) or OPL3 (version 2) */
void opl_pfree(OplPlayer *p);
int opl_render(OplPlayer *p, int16_t *out, int nframes);      /* 0 at the end of the song */
int opl_position(const OplPlayer *p, int *order, int *row, int *speed);
void opl_set_mute(OplPlayer *p, unsigned mask);                /* bit n = FM channel n is muted */
void opl_set_scope(OplPlayer *p, float *buf, int len);         /* scope ring buffer: buf[channel * len + pos], len a power of two, nine channels */
unsigned opl_scope_pos(const OplPlayer *p);
