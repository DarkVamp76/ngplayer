/* SPDX-License-Identifier: GPL-3.0-or-later */
/* AHX (Abyss' Highest eXperience) synthesizer module player. The engine is adapted from the HivelyTracker replayer
 * (BSD-3-Clause, Pete Gordon), see ahx.c. Only AHX files ("THX" + revision 0/1) are accepted. */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct AhxPlayer AhxPlayer;

int  ahx_probe(const uint8_t *buf, size_t len);                    /* 1 = AHX file */
AhxPlayer *ahx_new(const uint8_t *buf, size_t len, int rate);      /* NULL on error */
void ahx_free(AhxPlayer *p);
int  ahx_render(AhxPlayer *p, int16_t *out, int nframes);          /* stereo frames; 0 at the end of the song */
const char *ahx_title(const AhxPlayer *p);
int  ahx_channels(const AhxPlayer *p);                             /* 4 */
int  ahx_npos(const AhxPlayer *p);                                 /* positions (orders) */
int  ahx_tracklen(const AhxPlayer *p);                             /* rows per track */
void ahx_pos(const AhxPlayer *p, int *pos, int *row);              /* position now playing */
/* One cell of the position/track table: note 0 = none, 1..60 = C-1..B-5 (before the position's transpose); returns 0 if out of range */
int  ahx_cell(const AhxPlayer *p, int pos, int row, int ch, int *note, int *inst, int *fx, int *par);
void ahx_set_scope(AhxPlayer *p, float *buf, int len);
unsigned ahx_scope_pos(const AhxPlayer *p);
void ahx_set_mute(AhxPlayer *p, unsigned mask);
int  ahx_chan_pan(const AhxPlayer *p, int ch);                     /* 0 (left) .. 255 (right) */
