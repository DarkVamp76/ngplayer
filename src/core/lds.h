/* LOUDNESS Sound System (.lds, .ld0): AdLib music by Andras Molnar, used in Epic MegaGames' Tyrian among others. The player is a C port of the replayer of
 * the AdPlug project (which follows the original assembler player). GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t mod_misc, mod_vol, mod_ad, mod_sr, mod_wave, car_misc, car_vol, car_ad, car_sr, car_wave, feedback, keyoff, portamento, glide, finetune, vibrato, vibdelay,
            mod_trem, car_trem, tremwait, arpeggio, arp_tab[12];
    uint16_t start, size; uint8_t fms; uint16_t transp;
    uint8_t midinst, midvelo, midkey, midtrans, middum1, middum2;
} LdsPatch;

typedef struct { uint16_t patnum; uint8_t transpose; } LdsPos;

typedef struct LdsSong {
    int mode, tempo, pattlen, no_midi;
    uint16_t speed; uint8_t chandelay[9], regbd;
    unsigned npatch, npatch_alloc; LdsPatch *patch;     /* patch has at least 64 entries, the ones beyond npatch are empty */
    unsigned nposi; LdsPos *pos;               /* nposi * 9 */
    unsigned npat; uint16_t *pat;              /* the pattern words */
} LdsSong;

typedef struct { int kind, note, inst, cmd, param; } LdsCell;   /* kind 0 none, 1 note (note = semitone number, inst = patch), 2 command (cmd = high byte, param = low byte) */

LdsSong *lds_load(const uint8_t *buf, size_t len, int no_midi, int *err);   /* NULL on error, err < 0; the format has no signature */
void lds_free(LdsSong *s);
double lds_refresh(const LdsSong *s);                                      /* update rate in Hz */
int lds_cell(const LdsSong *s, int order, int row, int chan, LdsCell *c);   /* what a channel does in a row of an order; 1 if anything */

typedef void (*LdsWrite)(void *user, int reg, int val);
typedef struct LdsPlayer LdsPlayer;
LdsPlayer *lds_new(const LdsSong *s, LdsWrite wr, void *user);
void lds_free_player(LdsPlayer *p);
void lds_rewind(LdsPlayer *p);
int lds_update(LdsPlayer *p);                                               /* one tick; 0 at the end of the song or when it loops */
double lds_player_refresh(const LdsPlayer *p);                              /* the rate can change with the song's speed */
int lds_position(const LdsPlayer *p, int *order, int *row, int *speed);
