/* Musicline Editor replayer: part effects (port of ml_Player.PartEffects.cs). */
#ifndef NG_ML_PARTEFFECTS_H
#define NG_ML_PARTEFFECTS_H
#include "ml_player.h"

/* Functions called here that are defined in other C# parts (assumed prototypes):
   uint16_t ml_get_period(MlPlayer *p, MlVoice *v);                         // GetPeriod(voiceInfo)
   uint16_t ml_get_period2(MlPlayer *p, MlVoice *v, uint16_t note);         // GetPeriod2(voiceInfo, note)
   void     ml_set_bpm_tempo(MlPlayer *p, uint16_t tempo);                  // SetBpmTempo(newSpeed)
   void     ml_set_amiga_filter(MlPlayer *p, uint8_t on);                   // AmigaFilter = value (property setter)
*/
uint16_t ml_get_period(MlPlayer *p, MlVoice *v);
uint16_t ml_get_period2(MlPlayer *p, MlVoice *v, uint16_t note);
void ml_set_bpm_tempo(MlPlayer *p, uint16_t tempo);
void ml_set_amiga_filter(MlPlayer *p, uint8_t on);

void ml_play_part_fx(MlPlayer *p, MlVoice *v);
void ml_pfx_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_portamento(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_init_instrument_portamento(MlPlayer *p, MlVoice *v);
void ml_pfx_pitch_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pitch_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_vibrato_speed(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_vibrato_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_vibrato_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_vibrato(MlPlayer *p, MlVoice *v, MlEff effect, int direction);
void ml_pfx_vibrato_wave(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_set_fine_tune(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_set_instrument_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_instrument_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_instrument_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_set_slide_instrument_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_slide_to_instrument_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_instrument_volume_add(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_instrument_volume_sub(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_tremolo_speed(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_tremolo_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_tremolo_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_tremolo(MlPlayer *p, MlVoice *v, MlEff effect, int direction);
void ml_pfx_tremolo_wave(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_set_channel_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_channel_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_channel_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_set_slide_channel_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_slide_to_channel_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_channel_volume_add(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_channel_volume_sub(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_set_volume_all_channels(MlPlayer *p, MlEff effect);
void ml_pfx_set_master_volume(MlPlayer *p, MlEff effect);
void ml_pfx_master_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_master_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_set_slide_master_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_slide_to_master_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_master_volume_add(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_master_volume_sub(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_speed_one_channel(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_groove_one_channel(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_speed_all_channels(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_groove_all_channels(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_arpeggio_table(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_arpeggio_table_one_step(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_hold_sustain(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_filter(MlPlayer *p, MlEff effect);
void ml_pfx_sample_offset(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_restart_no_volume(MlPlayer *p, MlVoice *v);
void ml_pfx_wave_sample(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_init_instrument(MlPlayer *p, MlVoice *v);
void ml_pfx_pt_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_portamento(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_fine_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_fine_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_tremolo(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_tremolo_wave(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_vibrato(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_vibrato_wave(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_pfx_pt_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);

#endif
