/* SoundMon / BP SoundMon (Amiga, Brian Postma; "BPSM" 1.0, "V.2" 1.1/2.0 and 2.1, "V.3" 2.2): loader + replayer
   (port of the NostalgicPlayer SoundMonWorker / SoundMonIdentifier, MIT) with a Paula-like mixer. */
#ifndef NG_BP_H
#define NG_BP_H
#include <stddef.h>
#include <stdint.h>

typedef struct BpSong BpSong;
typedef struct BpPlayer BpPlayer;

int  bp_probe(const uint8_t *b, size_t n);                     /* 1 = a SoundMon module */
BpSong *bp_load(const uint8_t *b, size_t n, int *err);          /* NULL on error */
void bp_free(BpSong *s);
int  bp_version(const BpSong *s);                               /* how it is played: 10 ("BPSM"), 11 ("V.2" as 1.1/2.0),
                                                                   21 ("V.2" as 2.1, detected from the effects), 22 ("V.3") */
const char *bp_title(const BpSong *s);                          /* UTF-8 */
int  bp_subsongs(const BpSong *s);                              /* songs found as NostalgicPlayer does (unvisited positions) */
BpPlayer *bp_new(const BpSong *s, int sub, int rate);
void bp_player_free(BpPlayer *p);
int  bp_render(BpPlayer *p, int16_t *out, int n);               /* n stereo frames; frames written, 0 at the end of the song */
int  bp_channels(const BpSong *s);                              /* 4 */
int  bp_positions(const BpSong *s, int sub);                    /* step list length (positions are absolute, all songs) */
void bp_pos(const BpPlayer *p, int *position, int *row);       /* step and row being played */
int  bp_rows(const BpSong *s);                                  /* rows per track (16) */
int  bp_cell(const BpSong *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg);
     /* ch = Paula channel (step voice 3 - ch, as the player does); note with the step transpose (1 = period 856),
        inst 1..15 with the sound transpose (0 = none), fx = optional 0..15, arg = its data; 0 if out of range */
void bp_set_mute(BpPlayer *p, unsigned mask);                   /* bit n = channel n silent */
void bp_set_scope(BpPlayer *p, float *buf, int len);            /* ring buffer buf[ch * len + (pos & (len - 1))], -1..1 */
unsigned bp_scope_pos(const BpPlayer *p);
#endif
