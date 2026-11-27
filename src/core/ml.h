/* Musicline Editor (Ch. Cyreus & J. Carehag, Amiga): module data and loader. The structures and the chunk layout follow the replayer ports of
   NostalgicPlayer (MIT, Polycode) and musicline_playback; the loader follows NostalgicPlayer's MusiclineEditorWorker.Loader. */
#ifndef NG_ML_H
#define NG_ML_H
#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t effect, argument; } MlEff;
typedef struct { uint8_t note, instrument; MlEff eff[5]; } MlPartLine;
typedef struct { MlPartLine line[128]; } MlPart;
typedef struct { int8_t transpose; uint8_t sample; MlEff eff[2]; } MlArpLine;
typedef struct { MlArpLine line[128]; } MlArp;

typedef struct {
    char title[33];
    uint16_t tempo; uint8_t speed, groove; uint16_t volume; uint8_t mixer_mode, channels;      /* mixer_mode: the 8 voice tunes */
    uint16_t *seq[8];                                                                         /* 256 steps per channel */
} MlTune;

typedef struct {
    char title[33];
    uint8_t sample_type; uint16_t length, repeat_length; int16_t fine_tune, semi_tone;
    int8_t *data; size_t data_len;                                                              /* sample bytes (waves: 256 + 240 extra) */
    size_t rep_off;                                                                             /* repeat start (bytes into data) */
} MlSample;

typedef struct {
    char title[33];
    uint8_t sample_number, sample_type;
    uint16_t sample_length, sample_repeat_length; int16_t fine_tune, semi_tone;
    uint16_t sample_start, sample_end, sample_repeat_start, sample_repeat_len, volume;
    uint8_t transposable, slide_speed, effect1, effect2;
    uint16_t env_attack_len, env_decay_len, env_sustain_len, env_release_len;
    uint16_t env_attack_speed, env_decay_speed, env_sustain_speed, env_release_speed;
    uint16_t env_attack_vol, env_decay_vol, env_sustain_vol, env_release_vol;
    uint8_t vib_dir, vib_wave; uint16_t vib_speed, vib_delay, vib_attack_speed, vib_attack, vib_depth;
    uint8_t trem_dir, trem_wave; uint16_t trem_speed, trem_delay, trem_attack_speed, trem_attack, trem_depth;
    uint16_t arp_table; uint8_t arp_speed, arp_groove;
    uint8_t env_tra_pha_fil_bits, transform_wave[5];
    uint16_t transform_start, transform_repeat, transform_repeat_end, transform_speed, transform_turns, transform_delay;
    uint16_t phase_start, phase_repeat, phase_repeat_end, phase_speed, phase_turns, phase_delay, phase_type;
    uint8_t mix_res_loo_bits, mix_wave_number;
    uint16_t mix_start, mix_repeat, mix_repeat_end, mix_speed, mix_turns, mix_delay;
    uint16_t res_start, res_repeat, res_repeat_end, res_speed, res_turns, res_delay;
    uint8_t mix_res_fil_boost, resonance_amp;
    uint16_t filter_start, filter_repeat, filter_repeat_end, filter_speed, filter_turns, filter_delay;
    uint8_t filter_type;
    uint16_t loop_start, loop_repeat, loop_repeat_end, loop_length, loop_step, loop_wait, loop_delay, loop_turns;
    int sample_index;                                                                          /* 1-based sample of the module (0 = none) */
    size_t sample_off, rep_off;                                                                /* start / repeat start (bytes into that sample) */
    int valid;
} MlInst;

typedef struct {
    MlTune *tune; int ntunes;
    MlPart *part[1024]; MlArp *arp[256];
    MlInst inst[256]; int ninst;                                                               /* 1-based; inst[0] is the empty instrument */
    MlSample smp[256]; int nsmp;                                                               /* 1-based */
    char name[256], author[256];
    int mixer_mode;                                                                            /* 8 voices */
    uint16_t zero_seq[256];                                                                    /* a channel without steps: stops at once (0x0060) */
} MlSong;

MlSong *ml_load(const uint8_t *b, size_t n, int *err);                                        /* NULL on error */
int ml_probe(const uint8_t *b, size_t n);
void ml_free(MlSong *m);
#endif
