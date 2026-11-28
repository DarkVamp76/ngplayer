/* Art Of Noise (Amiga, .aon 4 voices / .aon8 8 voices): loader + replayer (port of the NostalgicPlayer ArtOfNoiseWorker, MIT) with a Paula-like mixer. */
#ifndef NG_AON_H
#define NG_AON_H
#include <stddef.h>
#include <stdint.h>

typedef struct AonSong AonSong;
typedef struct AonPlayer AonPlayer;

int  aon_probe(const uint8_t *b, size_t n);                    /* 1 = Art Of Noise module (4 or 8 voices) */
AonSong *aon_load(const uint8_t *b, size_t n, int *err);       /* NULL on error */
void aon_free(AonSong *s);
AonPlayer *aon_new(const AonSong *s, int rate);                /* start of the song */
void aon_player_free(AonPlayer *p);
int  aon_render(AonPlayer *p, int16_t *out, int n);            /* n stereo frames; frames written, 0 at the end of the song */
int  aon_channels(const AonSong *s);                           /* 4 or 8 */
const char *aon_title(const AonSong *s);
const char *aon_author(const AonSong *s);
int  aon_positions(const AonSong *s);                          /* length of the position list */
void aon_pos(const AonPlayer *p, int *position, int *pattern, int *row);
int  aon_cell(const AonSong *s, int pattern, int row, int ch, int *note, int *inst, int *fx, int *arg);   /* 0 if out of range */
void aon_set_mute(AonPlayer *p, unsigned mask);                /* bit n = channel n silent */
void aon_set_scope(AonPlayer *p, float *buf, int len);         /* ring buffer buf[ch * len + (pos & (len - 1))], -1..1 */
unsigned aon_scope_pos(const AonPlayer *p);
int  aon_position_pattern(const AonSong *s, int position);   /* the pattern of a position, -1 if none */
#endif
