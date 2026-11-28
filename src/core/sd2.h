/* SidMon 2.0 (Amiga; "SIDMON II - THE MIDI VERSION" at offset 58): loader + replayer
   (port of the NostalgicPlayer SidMon20Worker, MIT) with a Paula-like mixer. */
#ifndef NG_SD2_H
#define NG_SD2_H
#include <stddef.h>
#include <stdint.h>

typedef struct Sd2Song Sd2Song;
typedef struct Sd2Player Sd2Player;

int  sd2_probe(const uint8_t *b, size_t n);                    /* 1 = a SidMon 2.0 module */
Sd2Song *sd2_load(const uint8_t *b, size_t n, int *err);         /* NULL on error */
void sd2_free(Sd2Song *s);
const char *sd2_title(const Sd2Song *s);                         /* UTF-8, "" if none */
int  sd2_subsongs(const Sd2Song *s);                             /* songs found as NostalgicPlayer does */
Sd2Player *sd2_new(const Sd2Song *s, int sub, int rate);
void sd2_player_free(Sd2Player *p);
int  sd2_render(Sd2Player *p, int16_t *out, int n);              /* n stereo frames; frames written, 0 at the end of the song */
int  sd2_channels(const Sd2Song *s);                             /* 4 */
int  sd2_positions(const Sd2Song *s, int sub);                   /* positions of the song */
void sd2_pos(const Sd2Player *p, int *position, int *row);      /* position and row being played */
int  sd2_rows(const Sd2Song *s, int position);                   /* rows of the tracks at that position */
int  sd2_cell(const Sd2Song *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg);
     /* note with the position transpose (0 = none, 1 = lowest), inst 1.. (0 = none), fx / arg = effect and its data;
        0 if out of range or nothing there */
void sd2_set_mute(Sd2Player *p, unsigned mask);                  /* bit n = channel n silent */
void sd2_set_scope(Sd2Player *p, float *buf, int len);           /* ring buffer buf[ch * len + (pos & (len - 1))], -1..1 */
unsigned sd2_scope_pos(const Sd2Player *p);
void sd2_set_fast(Sd2Player *p, int on);                         /* 1 = no mixing, only the player ticks (seeking) */
#endif
