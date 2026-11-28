/* Future Composer (Amiga, FC 1.4 "FC14" and FC 1.0-1.3 "SMOD"): loader + replayer (port of the NostalgicPlayer
   FutureComposerWorker and the FutureComposer13Format converter, MIT) with a Paula-like mixer. */
#ifndef NG_FC_H
#define NG_FC_H
#include <stddef.h>
#include <stdint.h>

typedef struct FcSong FcSong;
typedef struct FcPlayer FcPlayer;

int  fc_probe(const uint8_t *b, size_t n);                     /* 1 = FC 1.4 or FC 1.3 */
FcSong *fc_load(const uint8_t *b, size_t n, int *err);          /* SMOD is converted to FC14 in memory first; NULL on error */
void fc_free(FcSong *s);
int  fc_version(const FcSong *s);                               /* 13 or 14: what the file was */
int  fc_subsongs(const FcSong *s);                              /* songs found as NostalgicPlayer does (unvisited positions) */
FcPlayer *fc_new(const FcSong *s, int sub, int rate);
void fc_player_free(FcPlayer *p);
int  fc_render(FcPlayer *p, int16_t *out, int n);               /* n stereo frames; frames written, 0 at the end of the song */
int  fc_channels(const FcSong *s);                              /* 4 */
int  fc_positions(const FcSong *s, int sub);                    /* sequence length (positions are absolute, all songs) */
void fc_pos(const FcPlayer *p, int *position, int *row);       /* position and row of voice 0 */
int  fc_rows(const FcSong *s);                                  /* rows per pattern (32) */
int  fc_cell(const FcSong *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg);   /* 0 if out of range */
void fc_set_mute(FcPlayer *p, unsigned mask);                   /* bit n = channel n silent */
void fc_set_scope(FcPlayer *p, float *buf, int len);            /* ring buffer buf[ch * len + (pos & (len - 1))], -1..1 */
unsigned fc_scope_pos(const FcPlayer *p);
#endif
