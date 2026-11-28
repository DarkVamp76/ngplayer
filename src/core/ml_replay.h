/* Musicline Editor: the player (ml_play.c, ml_effects.c, ml_parteffects.c, ml_waveeffects.c) driven by a tick clock and a Paula-like mixer. */
#ifndef NG_ML_REPLAY_H
#define NG_ML_REPLAY_H
#include "ml_player.h"

typedef struct {
    MlPlayer pl;
    int rate, sub;
    double tick_left;                                 /* output frames until the next tick */
    struct { double pos; const int8_t *d; uint32_t len; const int8_t *ld; uint32_t llen; uint8_t on; } m[8];
    float lp[2];                                      /* the A500 filter */
    unsigned long long mute;
    float *sc; int sc_len; unsigned sc_pos;             /* scope ring buffer: sc[channel * sc_len + pos] (sc_len a power of two) */
    long long frames; int ended;
} MlRender;

MlRender *ml_render_new(MlSong *s, int sub, int rate);
int  ml_render(MlRender *r, int16_t *out, int n);       /* n stereo frames; 0 at the end of the song */
void ml_render_free(MlRender *r);

/* called by the player parts */
void ml_on_end_reached(MlPlayer *p, int ch);
void ml_set_bpm_tempo(MlPlayer *p, uint16_t tempo);
void ml_set_amiga_filter(MlPlayer *p, uint8_t on);
void ml_channel_set_sample(MlChannel *ch, const int8_t *data, uint32_t offset, uint32_t length);
#endif
