/* Sonic Arranger (Amiga, .sa): loader + replayer (port of the NostalgicPlayer SonicArrangerWorker, MIT) with a Paula-like mixer.
   Loads the editor format (SOARV1.0, and @OARV1.0 packed with lh.library) and the "final" format (replay code + data block). */
#ifndef NG_SA_H
#define NG_SA_H
#include <stddef.h>
#include <stdint.h>

typedef struct SaSong SaSong;
typedef struct SaPlayer SaPlayer;

int  sa_probe(const uint8_t *b, size_t n);                    /* 1 = Sonic Arranger module */
SaSong *sa_load(const uint8_t *b, size_t n, int *err);         /* NULL on error */
void sa_free(SaSong *s);
int  sa_subsongs(const SaSong *s);                             /* number of songs the player offers */
SaPlayer *sa_new(const SaSong *s, int sub, int rate);          /* InitSound(sub) */
void sa_player_free(SaPlayer *p);
int  sa_render(SaPlayer *p, int16_t *out, int n);              /* n stereo frames; frames written, 0 at the end of the song */
int  sa_channels(const SaSong *s);                             /* 4 */
const char *sa_title(const SaSong *s);
int  sa_positions(const SaSong *s, int sub);                   /* positions of that song */
void sa_pos(const SaPlayer *p, int *position, int *row);      /* current position (relative to the song start) and row */
int  sa_cell(const SaSong *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg);   /* 0 if out of range */
int  sa_rows(const SaSong *s);                                 /* rows per track (of the first song) */
void sa_set_mute(SaPlayer *p, unsigned mask);                  /* bit n = channel n silent */
void sa_set_scope(SaPlayer *p, float *buf, int len);           /* ring buffer buf[ch * len + (pos & (len - 1))], -1..1 */
unsigned sa_scope_pos(const SaPlayer *p);
#endif
