/* Reality ADlib Tracker (RAD): file format 1.0 and 2.1. The player is a C port of the public domain RAD2 replayer by Shayde / Reality (the version used by
 * the AdPlug project), including its tune validation. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define RAD_TRACKS 100
#define RAD_CHANNELS 9
#define RAD_LINES 64
#define RAD_RIFFTRACKS 10
#define RAD_INSTRUMENTS 127

typedef struct { uint8_t feedback[2], panning[2], algorithm, detune, volume, riffspeed; const uint8_t *riff; uint8_t operators[4][5]; char name[256]; } RadInst;

typedef struct RadSong {
    uint8_t *data; size_t size;            /* a copy of the file, one zero byte longer (some old tunes have a truncated last note) */
    int version;                           /* 1 or 2 */
    int use_opl3;                          /* version 2 plays on the OPL3, version 1 on OPL2 */
    float hertz;                           /* update rate */
    int speed;                             /* initial speed */
    const uint8_t *desc;                   /* description (zero terminated) or NULL */
    RadInst inst[RAD_INSTRUMENTS]; int ninst;
    const uint8_t *order; int norder;      /* order list */
    const uint8_t *tracks[RAD_TRACKS]; int ntracks;
    const uint8_t *riffs[RAD_RIFFTRACKS][RAD_CHANNELS];
} RadSong;

typedef struct { int note, octave, inst, effect, param; } RadCell;   /* note 1..12 (C#..C) 15 = key off, 0 none; octave 0..7 */

int rad_probe(const uint8_t *buf, size_t len);
RadSong *rad_load(const uint8_t *buf, size_t len, int *err);        /* NULL on error, err < 0 */
void rad_free(RadSong *s);
int rad_order_track(const RadSong *s, int order);                   /* track number of an order list entry, -1 = jump marker / none */
int rad_cell(const RadSong *s, int track, int line, int chan, RadCell *c);   /* 1 if the track has a note there */

typedef void (*RadWrite)(void *user, int reg, int val);             /* reg 0..0x1FF: bank 1 registers are 0x100 + reg */
typedef struct RadPlayer RadPlayer;
RadPlayer *rad_new(const RadSong *s, RadWrite wr, void *user);
void rad_free_player(RadPlayer *p);
void rad_rewind(RadPlayer *p);
int rad_update(RadPlayer *p);                                       /* one tick; 0 when the tune starts to repeat */
int rad_position(const RadPlayer *p, int *order, int *line, int *speed);
