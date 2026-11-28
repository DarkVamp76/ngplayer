/* SidMon 1.0 (Amiga; ripped player + data: found by searching the 68k init / play code, like NostalgicPlayer): loader + replayer
   (port of the NostalgicPlayer SidMon10Worker, MIT) with a Paula-like mixer. */
#ifndef NG_SD1_H
#define NG_SD1_H
#include <stddef.h>
#include <stdint.h>

typedef struct Sd1Song Sd1Song;
typedef struct Sd1Player Sd1Player;

int  sd1_probe(const uint8_t *b, size_t n);                    /* 1 = a SidMon 1.0 module */
Sd1Song *sd1_load(const uint8_t *b, size_t n, int *err);         /* NULL on error */
void sd1_free(Sd1Song *s);
const char *sd1_title(const Sd1Song *s);                         /* UTF-8, "" if none */
int  sd1_subsongs(const Sd1Song *s);                             /* songs found as NostalgicPlayer does */
Sd1Player *sd1_new(const Sd1Song *s, int sub, int rate);
void sd1_player_free(Sd1Player *p);
int  sd1_render(Sd1Player *p, int16_t *out, int n);              /* n stereo frames; frames written, 0 at the end of the song */
int  sd1_channels(const Sd1Song *s);                             /* 4 */
int  sd1_positions(const Sd1Song *s, int sub);                   /* positions of the song */
void sd1_pos(const Sd1Player *p, int *position, int *row);      /* position and row being played */
int  sd1_rows(const Sd1Song *s, int position);                   /* rows of the tracks at that position */
int  sd1_cell(const Sd1Song *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg);
     /* note with the position transpose (0 = none, 1 = lowest), inst 1.. (0 = none), fx / arg = effect and its data;
        0 if out of range or nothing there */
void sd1_set_mute(Sd1Player *p, unsigned mask);                  /* bit n = channel n silent */
void sd1_set_scope(Sd1Player *p, float *buf, int len);           /* ring buffer buf[ch * len + (pos & (len - 1))], -1..1 */
unsigned sd1_scope_pos(const Sd1Player *p);
void sd1_set_fast(Sd1Player *p, int on);                         /* 1 = no mixing, only the player ticks (seeking) */
#endif
