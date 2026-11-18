/* HERAD (Herbulot AdLib System) music files: .HSQ / .SQX (packed), .SDB, .AGD (AdLib Gold, OPL3), .HA2. The loader and the player follow the
 * HERAD player of the AdPlug project (Stas'M, Jepael, thatdutchguy, CHE@TER and others). GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define HERAD_MAX_TRACKS 21
#define HERAD_INST_SIZE  40
#define HERAD_MEASURE_TICKS 96
#define HERAD_REFRESH    200.299

typedef struct { uint16_t size; uint8_t *data; } HeradTrack;

typedef struct { uint32_t tick; uint8_t kind, a, b; } HeradEv;   /* kind: 1 note on (a note, b velocity), 2 note off, 3 program change, 4 aftertouch, 5 pitch bend; sorted by tick */

typedef struct HeradSong {
    int comp;                      /* 0 none, 1 HSQ, 2 SQX */
    int agd, v2;                   /* AGD = OPL3 with 18 voices; version 2 */
    int ntracks, ninst;
    uint16_t speed;
    HeradTrack track[HERAD_MAX_TRACKS];
    uint8_t (*inst)[HERAD_INST_SIZE];
    uint32_t total_ticks;          /* length of the longest track in ticks, including a trailing delay */
    uint32_t last_tick;            /* tick of the last event of any track */
    HeradEv *ev[HERAD_MAX_TRACKS]; int nev[HERAD_MAX_TRACKS];
    uint8_t gold[32];              /* AGD: header bytes 0x34..0x53 - the stereo switch function byte, then the 31 YM7128B registers */
} HeradSong;

int herad_probe(const uint8_t *buf, size_t len);                    /* 1 = the data look like a HERAD file (compressed or not) */
HeradSong *herad_load(const uint8_t *buf, size_t len, int *err);    /* NULL on error, err < 0 */
void herad_free(HeradSong *s);

/* The events of one track at one tick (for a pattern view): the note (kind 0 none, 1 on, 2 off), a program change (-1 none), a pitch bend (-1) and an aftertouch (-1) */
typedef struct { int note_kind, note, vel, prog, bend, touch; } HeradCell;
void herad_cell(const HeradSong *s, int track, uint32_t tick, HeradCell *c);

/* chip 0 / 1: OPL3 bank, 2: AdLib Gold control register (4 volume L, 5 volume R, 6 bass, 7 treble, 8 switch functions), 3: YM7128B surround register */
typedef void (*HeradWrite)(void *user, int chip, int reg, int val);
typedef struct HeradPlayer HeradPlayer;
HeradPlayer *herad_new(const HeradSong *s, HeradWrite wr, void *user);
void herad_free_player(HeradPlayer *p);
void herad_rewind(HeradPlayer *p);                                  /* resets the chip registers the player owns and restarts the song */
int herad_update(HeradPlayer *p);                                   /* one tick at 200.299 Hz; 0 at the end of the song */
int herad_channel(const HeradPlayer *p, int track);                 /* OPL3 channel (0..17) a track plays on now, -1 = none; AGD voices move between channels */
unsigned herad_channel_mask(const HeradPlayer *p, int track);       /* all OPL3 channels the track uses (two for a 4-operator voice) as a bit mask */
int herad_position(const HeradPlayer *p, int *measure, int *tick);   /* measure = tick / 96, tick within the measure; the tick counts like the HeradEv ticks */
