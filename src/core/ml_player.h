/* Musicline Editor replayer: player state, enums and shared declarations.
   Port of the NostalgicPlayer MusiclineEditor worker (MIT, Polycode) to C. The names follow the C# members mechanically:
   CamelCase -> snake_case (PartPositionWork -> part_position_work), enum members -> ML_<ENUM>_<UPPER_SNAKE> (see below). */
#ifndef NG_ML_PLAYER_H
#define NG_ML_PLAYER_H
#include "ml.h"

/* PartEffect */
#define ML_PE_NONE 0x00
#define ML_PE_SLIDE_UP 0x01
#define ML_PE_SLIDE_DOWN 0x02
#define ML_PE_PORTAMENTO 0x03
#define ML_PE_INIT_INSTRUMENT_PORTAMENTO 0x04
#define ML_PE_PITCH_UP 0x05
#define ML_PE_PITCH_DOWN 0x06
#define ML_PE_VIBRATO_SPEED 0x07
#define ML_PE_VIBRATO_UP 0x08
#define ML_PE_VIBRATO_DOWN 0x09
#define ML_PE_VIBRATO_WAVE 0x0a
#define ML_PE_SET_FINE_TUNE 0x0b
#define ML_PE_SET_INSTRUMENT_VOLUME 0x10
#define ML_PE_INSTRUMENT_VOLUME_SLIDE_UP 0x11
#define ML_PE_INSTRUMENT_VOLUME_SLIDE_DOWN 0x12
#define ML_PE_SET_SLIDE_INSTRUMENT_VOLUME 0x13
#define ML_PE_SLIDE_TO_INSTRUMENT_VOLUME 0x14
#define ML_PE_INSTRUMENT_VOLUME_ADD 0x15
#define ML_PE_INSTRUMENT_VOLUME_SUB 0x16
#define ML_PE_TREMOLO_SPEED 0x17
#define ML_PE_TREMOLO_UP 0x18
#define ML_PE_TREMOLO_DOWN 0x19
#define ML_PE_TREMOLO_WAVE 0x1a
#define ML_PE_SET_CHANNEL_VOLUME 0x20
#define ML_PE_CHANNEL_VOLUME_SLIDE_UP 0x21
#define ML_PE_CHANNEL_VOLUME_SLIDE_DOWN 0x22
#define ML_PE_SET_SLIDE_CHANNEL_VOLUME 0x23
#define ML_PE_SLIDE_TO_CHANNEL_VOLUME 0x24
#define ML_PE_CHANNEL_VOLUME_ADD 0x25
#define ML_PE_CHANNEL_VOLUME_SUB 0x26
#define ML_PE_SET_VOLUME_ALL_CHANNELS 0x27
#define ML_PE_SET_MASTER_VOLUME 0x30
#define ML_PE_MASTER_VOLUME_SLIDE_UP 0x31
#define ML_PE_MASTER_VOLUME_SLIDE_DOWN 0x32
#define ML_PE_SET_SLIDE_MASTER_VOLUME 0x33
#define ML_PE_SLIDE_TO_MASTER_VOLUME 0x34
#define ML_PE_MASTER_VOLUME_ADD 0x35
#define ML_PE_MASTER_VOLUME_SUB 0x36
#define ML_PE_SPEED_ONE_CHANNEL 0x40
#define ML_PE_GROOVE_ONE_CHANNEL 0x41
#define ML_PE_SPEED_ALL_CHANNELS 0x42
#define ML_PE_GROOVE_ALL_CHANNELS 0x43
#define ML_PE_ARPEGGIO_TABLE 0x44
#define ML_PE_ARPEGGIO_TABLE_ONE_STEP 0x45
#define ML_PE_HOLD_SUSTAIN 0x46
#define ML_PE_FILTER 0x47
#define ML_PE_SAMPLE_OFFSET 0x48
#define ML_PE_RESTART_NO_VOLUME 0x49
#define ML_PE_WAVE_SAMPLE 0x4a
#define ML_PE_INIT_INSTRUMENT 0x4b
#define ML_PE_PT_SLIDE_UP 0xe1
#define ML_PE_PT_SLIDE_DOWN 0xe2
#define ML_PE_PT_PORTAMENTO 0xe3
#define ML_PE_PT_FINE_SLIDE_UP 0xe4
#define ML_PE_PT_FINE_SLIDE_DOWN 0xe5
#define ML_PE_PT_VOLUME_SLIDE_UP 0xe6
#define ML_PE_PT_VOLUME_SLIDE_DOWN 0xe7
#define ML_PE_PT_TREMOLO 0xe8
#define ML_PE_PT_TREMOLO_WAVE 0xe9
#define ML_PE_PT_VIBRATO 0xea
#define ML_PE_PT_VIBRATO_WAVE 0xeb
/* ArpeggioEffect */
#define ML_AE_NONE 0
#define ML_AE_SLIDE_UP 1
#define ML_AE_SLIDE_DOWN 2
#define ML_AE_SET_VOLUME 3
#define ML_AE_VOLUME_SLIDE_UP 4
#define ML_AE_VOLUME_SLIDE_DOWN 5
#define ML_AE_RESTART 6
/* ArpeggioFlag */
#define ML_AF_NONE 0
#define ML_AF_ENABLED (1u << 0)
#define ML_AF_WILL_SET_NOTE (1u << 1)
#define ML_AF_USE_TABLE (1u << 2)
#define ML_AF_USE_WAVE_SAMPLE (1u << 3)
#define ML_AF_ONE_STEP_PENDING (1u << 4)
#define ML_AF_NOTE_IS_ABSOLUTE (1u << 5)
/* InstrumentEffect1 */
#define ML_IE1_NONE 0
#define ML_IE1_ENVELOPE (1u << 0)
#define ML_IE1_VIBRATO (1u << 1)
#define ML_IE1_TREMOLO (1u << 2)
#define ML_IE1_ARPEGGIO (1u << 3)
#define ML_IE1_LOOP (1u << 4)
#define ML_IE1_LOOP_STOP (1u << 5)
#define ML_IE1_HOLD_SUSTAIN (1u << 6)
#define ML_IE1_WAVE_SAMPLE_LOOP (1u << 7)
/* InstrumentEffect2 */
#define ML_IE2_NONE 0
#define ML_IE2_TRANSFORM (1u << 0)
#define ML_IE2_PHASE (1u << 1)
#define ML_IE2_MIX (1u << 2)
#define ML_IE2_RESONANCE (1u << 3)
#define ML_IE2_FILTER (1u << 4)
/* PlayFlag */
#define ML_PF_NONE 0
#define ML_PF_RETRIGGER (1u << 0)
#define ML_PF_FIRST_ROW_TICK (1u << 1)
#define ML_PF_KEEP_EFFECT_FINE_TUNE (1u << 2)
/* RestartFlag */
#define ML_RF_NONE 0
#define ML_RF_RESTART_FROM_PART_FX (1u << 0)
#define ML_RF_RESTART_FROM_ARP (1u << 1)
#define ML_RF_ARP_HAS_SET_VOLUME (1u << 2)
/* PhaseType */
#define ML_PH_OLD 0
#define ML_PH_HIGH 1
#define ML_PH_MED 2
#define ML_PH_LOW 3
/* PtWaveType */
#define ML_PTW_SINE 0
#define ML_PTW_RAMP_DOWN 1
#define ML_PTW_SQUARE 2
/* WaveType */
#define ML_WT_SINE 0
#define ML_WT_RAMP_DOWN 1
#define ML_WT_SAWTOOTH 2
#define ML_WT_SQUARE 3
/* SequenceCommand */
#define ML_SC_NONE 0
#define ML_SC_END 1
#define ML_SC_JUMP 2
#define ML_SC_WAIT 3
/* Direction */
#define ML_DIR_UPWARD 0
#define ML_DIR_DOWNWARD 1
/* SampleType */
#define ML_ST_SAMPLE 0
#define ML_ST_WAVE16 1
#define ML_ST_WAVE32 2
#define ML_ST_WAVE64 3
#define ML_ST_WAVE128 4
#define ML_ST_WAVE256 5
/* FilterType */
#define ML_FT_NORMAL 0
#define ML_FT_RESONANCE 1
/* BoostFlag */
#define ML_BF_NONE 0
#define ML_BF_FILTER (1u << 0)
#define ML_BF_RESONANCE (1u << 1)
#define ML_BF_MIX (1u << 2)
/* MixFlag */
#define ML_MF_NONE 0
#define ML_MF_MIX_INIT (1u << 0)
#define ML_MF_MIX_STEP (1u << 1)
#define ML_MF_MIX_BUFF (1u << 2)
#define ML_MF_MIX_COUNTER (1u << 3)
#define ML_MF_RESONANCE_INIT (1u << 4)
#define ML_MF_RESONANCE_STEP (1u << 5)
#define ML_MF_LOOP_INIT (1u << 6)
#define ML_MF_LOOP_STEP (1u << 7)
/* InstrumentFlag */
#define ML_IF_NONE 0
#define ML_IF_ENVELOPE_HOLD_SUSTAIN (1u << 0)
#define ML_IF_TRANSFORM_INIT (1u << 1)
#define ML_IF_TRANSFORM_STEP (1u << 2)
#define ML_IF_PHASE_INIT (1u << 3)
#define ML_IF_PHASE_STEP (1u << 4)
#define ML_IF_PHASE_FILL (1u << 5)
#define ML_IF_FILTER_INIT (1u << 6)
#define ML_IF_FILTER_STEP (1u << 7)

typedef struct { uint16_t counter; int16_t speed; uint16_t repeat, repeat_end; int16_t turns; uint16_t delay; uint8_t step; uint16_t save_counter; } MlCounter;

/* VoiceInfo */
typedef struct {
    int channel_number;
    uint8_t voice_off;
    uint8_t speed;
    uint8_t groove;
    uint8_t speed_part;
    uint8_t groove_part;
    uint8_t tune_speed;
    uint8_t tune_groove;
    uint8_t speed_counter;
    uint8_t arpeggio_speed_counter;
    uint8_t part_groove;
    uint8_t arpeggio_groove;
    uint8_t wave_sample_number;
    uint8_t wave_sample_number_old;
    MlInst *instrument;
    MlSample *wave_sample;
    uint8_t part_note;
    uint8_t part_instrument;
    MlEff part_effects[5];
    uint8_t arpeggio;
    uint8_t arpeggio_position;
    uint8_t arpeggio_table;
    uint8_t arpeggio_wait;
    uint8_t arpeggio_note;
    uint8_t arpeggio_volume_slide;
    uint8_t arpeggio_pitch_slide;
    uint8_t arpeggio_pitch_slide_type;
    uint16_t arpeggio_calculated_note;
    uint8_t tune_position;
    uint8_t part_position;
    uint8_t part_position_work;
    uint8_t tune_jump_counter;
    int16_t tune_jump_position;
    uint8_t part_jump_counter;
    MlPtr wave_sample_repeat_pointer_original;
    MlPtr wave_sample_pointer;
    uint16_t wave_sample_length;
    MlPtr wave_sample_repeat_pointer;
    uint16_t wave_sample_repeat_length;
    uint16_t volume1;
    uint16_t volume2;
    uint16_t volume3;
    uint16_t note;
    uint16_t period1;
    uint16_t period2;
    int16_t transpose;
    int16_t semi_tone;
    int16_t fine_tune;
    uint8_t sample_offset_active;
    uint8_t sample_offset;
    uint8_t old_instrument;
    uint8_t restart;
    uint8_t volume_add;
    uint8_t volume_slide;
    uint8_t channel_volume_slide;
    uint8_t master_volume_slide;
    uint16_t volume_set;
    uint16_t channel_volume;
    int16_t volume_add_number;
    int16_t channel_volume_add_number;
    int16_t master_volume_add_number;
    uint16_t volume_slide_speed;
    uint16_t channel_volume_slide_speed;
    uint16_t master_volume_slide_speed;
    uint16_t volume_slide_volume;
    uint16_t channel_volume_slide_volume;
    uint16_t master_volume_slide_volume;
    uint16_t volume_slide_to_volume;
    uint16_t channel_volume_slide_to_volume;
    uint16_t master_volume_slide_to_volume;
    uint8_t volume_slide_type;
    uint8_t channel_volume_slide_type;
    uint8_t master_volume_slide_type;
    uint8_t volume_slide_to_volume_off;
    uint8_t channel_volume_slide_to_volume_off;
    uint8_t master_volume_slide_to_volume_off;
    uint8_t volume;
    uint8_t instrument_pitch_slide;
    uint8_t mix_res_fil_boost;
    uint8_t transpose_number;
    uint16_t part_number;
    uint8_t pitch_slide;
    uint8_t pitch_slide_type;
    uint16_t pitch_slide_speed;
    uint16_t pitch_slide_note;
    int16_t pitch_slide_to_note;
    int16_t pitch_add;
    uint16_t arpeggio_volume_slide_speed;
    uint16_t arpeggio_pitch_slide_speed;
    int16_t arpeggio_pitch_slide_to_note;
    uint16_t arpeggio_pitch_slide_note;
    uint8_t pt_pitch_slide;
    uint8_t pt_pitch_slide_type;
    uint16_t pt_pitch_slide_speed;
    uint16_t pt_pitch_slide_speed2;
    uint16_t pt_pitch_slide_note;
    int16_t pt_pitch_slide_to_note;
    int16_t pt_pitch_add;
    MlEff effects1;
    MlEff effects2;
    uint16_t envelope_volume;
    uint16_t envelope_data[12];
    uint8_t play;
    uint8_t wave_or_sample;
    uint8_t phase_init;
    uint8_t filter_init;
    uint8_t transform_init;
    uint8_t tune_wait;
    uint8_t vibrato;
    uint8_t vibrato_direction;
    uint8_t vibrato_wave_type;
    uint8_t part_vibrato_wave_type;
    uint16_t vibrato_counter;
    uint16_t vibrato_command_speed;
    uint16_t vibrato_command_depth;
    uint16_t vibrato_command_delay;
    uint16_t vibrato_attack_speed;
    uint16_t vibrato_attack_length;
    uint16_t vibrato_depth;
    uint16_t vibrato_note;
    int8_t pt_tremolo_position;
    uint8_t pt_tremolo_command;
    uint8_t pt_tremolo_wave_type;
    int8_t pt_vibrato_position;
    uint8_t pt_vibrato_command;
    uint8_t pt_vibrato_wave_type;
    uint16_t pt_vibrato_note;
    uint8_t tremolo;
    uint8_t tremolo_direction;
    uint8_t tremolo_wave_type;
    uint8_t part_tremolo_wave_type;
    uint16_t tremolo_counter;
    uint16_t tremolo_command_speed;
    uint16_t tremolo_command_depth;
    uint16_t tremolo_command_delay;
    uint16_t tremolo_attack_speed;
    uint16_t tremolo_attack_length;
    uint16_t tremolo_depth;
    int8_t filter_last_sample;
    int8_t resonance_last_sample;
    uint8_t filter_last_init;
    uint8_t resonance_last_init;
    uint8_t resonance_amp;
    uint8_t resonance_init;
    int phase_type;
    uint8_t filter_type;
    MlCounter transform_counter;
    uint16_t transform_speed;
    MlCounter phase_counter;
    uint16_t phase_speed;
    MlCounter mix_counter;
    uint16_t mix_speed;
    MlCounter resonance_counter;
    uint16_t resonance_speed;
    MlCounter filter_counter;
    uint16_t filter_speed;
    uint8_t mix_wave_number;
    uint8_t mix_init;
    uint8_t transform_wave_sample_numbers[6];
    uint8_t loop_init;
    uint16_t loop_repeat;
    uint16_t loop_repeat_end;
    uint16_t loop_length;
    int loop_step;
    uint16_t loop_wait;
    int16_t loop_wait_counter;
    int16_t loop_delay;
    int16_t loop_turns;
    uint16_t loop_counter;
    uint16_t loop_counter_save;
    uint16_t loop_wave_sample_counter_max;
    int16_t loop_speed;
    MlPtr loop_wave_sample;
    int8_t transform_wave_buffer[256];
    int8_t phase_wave_buffer[256];
    int8_t mix_wave_buffer[256];
    int8_t resonance_wave_buffer[256];
    int8_t filter_wave_buffer[256];
    int8_t wave_buffer[256];
} MlVoice;

/* GlobalPlayingInfo */
typedef struct { uint16_t tune_tempo; uint8_t tune_speed, tune_groove, tune_position; uint16_t master_volume; uint8_t current_speed; } MlGlobal;

/* what DmaPlay hands to the mixer: one Paula-like channel */
typedef struct {
    uint16_t volume, period;                          /* Amiga volume 0..64 and period */
    const int8_t *data; uint32_t len;                 /* the sample that is playing (len in bytes) */
    const int8_t *loop_data; uint32_t loop_len;       /* the loop that follows (loop_len 0 = none) */
    uint8_t trigger;                                  /* set when PlaySample was called: restart the sample */
    uint8_t loop_set;                                 /* set when SetLoop was called */
    uint8_t sample_number;
} MlChannel;

typedef struct MlPlayer {
    MlSong *song; MlTune *cur;
    MlGlobal playing_info;
    MlVoice voices[8]; int number_of_channels;
    uint8_t playing_enabled;
    MlChannel ch[8];
    uint32_t end_mask;                                /* channels that reached their end (OnEndReached) */
    uint16_t tempo;                                   /* SetBpmTempo: the tick rate is tempo * 2 / 5 Hz */
    uint8_t amiga_filter;                             /* the A500 LED filter (part effect Filter) */
    uint8_t restart;                                  /* all voices are off: the song starts again */
} MlPlayer;

/* tables (ml_tables.c) */
/* tables of ml_tables.c */
extern const int8_t ml_zero_sample[2];
extern const int8_t ml_sine[128];
extern const int8_t ml_down_ramp[128];
extern const int8_t ml_sawtooth[128];
extern const int8_t ml_square[128];
extern const uint8_t ml_pt_vibrato_table[32];
extern const uint16_t ml_pal_pitch_table[1984];
extern const uint16_t ml_resonance_list[256];
extern const uint16_t ml_resample_list[256];
extern const uint16_t ml_filter_list[256];
extern const uint16_t ml_resonance_filter_list[256];
extern const uint16_t ml_sizer_offset16[15];
extern const uint16_t ml_sizer_offset32[31];
extern const uint16_t ml_sizer_offset64[63];
extern const uint16_t ml_sizer_offset128[127];
extern const uint16_t ml_sizer_offset256[255];
extern const uint8_t ml_sizer_table16[120];
extern const uint8_t ml_sizer_table32[496];
extern const uint8_t ml_sizer_table64[2016];
extern const uint8_t ml_sizer_table128[8128];
extern const uint8_t ml_sizer_table256[32640];

#endif
