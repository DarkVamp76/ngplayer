/* Musicline Editor (Ch. Cyreus & J. Carehag, Amiga): module data and loader. The structures and the chunk layout follow the replayer ports of
   NostalgicPlayer (MIT, Polycode) and musicline_playback; the loader follows NostalgicPlayer's MusiclineEditorWorker.Loader. */
#ifndef NG_ML_H
#define NG_ML_H
#include <stdint.h>
#include <stddef.h>

/* SamplePointer: a sample array and a start offset (data_len = the array's length in bytes) */
typedef struct { int8_t *data; uint32_t data_len, start_offset; uint8_t has; } MlPtr;
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
    uint8_t sample_type; uint16_t sample_length, sample_repeat_length; int16_t fine_tune, semi_tone;
    int8_t *data; size_t data_len;                                                              /* sample bytes = SampleData (waves: 256 + 240 extra) */
    MlPtr sample_pointer, sample_repeat_pointer;
    size_t rep_off;
} MlSample;

typedef struct {
    char title[33];
    uint8_t sample_number, sample_type;
    uint16_t sample_length, sample_repeat_length; int16_t fine_tune, semi_tone;
    uint16_t sample_start, sample_end, sample_repeat_start, sample_repeat_len, volume;
    uint8_t transposable, slide_speed, effect1, effect2;
    uint16_t envelope_attack_length, envelope_decay_length, envelope_sustain_length, envelope_release_length;
    uint16_t envelope_attack_speed, envelope_decay_speed, envelope_sustain_speed, envelope_release_speed;
    uint16_t envelope_attack_volume, envelope_decay_volume, envelope_sustain_volume, envelope_release_volume;
    uint8_t vibrato_direction, vibrato_wave_type; uint16_t vibrato_speed, vibrato_delay, vibrato_attack_speed, vibrato_attack, vibrato_depth;
    uint8_t tremolo_direction, tremolo_wave_type; uint16_t tremolo_speed, tremolo_delay, tremolo_attack_speed, tremolo_attack, tremolo_depth;
    uint16_t arpeggio_table; uint8_t arpeggio_speed, arpeggio_groove;
    uint8_t env_tra_pha_fil_bits, transform_wave_numbers[5];
    uint16_t transform_start, transform_repeat, transform_repeat_end, transform_speed, transform_turns, transform_delay;
    uint16_t phase_start, phase_repeat, phase_repeat_end, phase_speed, phase_turns, phase_delay, phase_type;
    uint8_t mix_res_loo_bits, mix_wave_number;
    uint16_t mix_start, mix_repeat, mix_repeat_end, mix_speed, mix_turns, mix_delay;
    uint16_t resonance_start, resonance_repeat, resonance_repeat_end, resonance_speed, resonance_turns, resonance_delay;
    uint8_t mix_res_fil_boost, responance_amp;
    uint16_t filter_start, filter_repeat, filter_repeat_end, filter_speed, filter_turns, filter_delay;
    uint8_t filter_type;
    uint16_t loop_start, loop_repeat, loop_repeat_end, loop_length, loop_loop_step, loop_wait, loop_delay, loop_turns;
    MlPtr sample_pointer, sample_repeat_pointer;
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
