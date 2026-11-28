/* Musicline Editor replayer: wave form effects (loop, phase, mix, resonance, filter, transform, counters).
   Port of ml_Player.WaveFormEffects.cs (NostalgicPlayer, MIT, Polycode). */
#ifndef NG_ML_WAVEEFFECTS_H
#define NG_ML_WAVEEFFECTS_H
#include "ml_player.h"

/* Functions CALLED that are defined elsewhere (assumed prototypes):
     void ml_channel_set_sample(MlChannel *ch, const int8_t *data, uint32_t offset, uint32_t length);
         = C# IChannel.SetSample(sbyte[] data, uint startOffset, uint length), used as SetSample(Tables.ZeroSample, 0, 2)
   The C# IChannel parameter of MoveLoop is passed as MlChannel *ch (normally &p->ch[v->channel_number]). */

void ml_move_loop(MlPlayer *p, MlVoice *v, MlChannel *ch);
void ml_loop_counter(MlPlayer *p, MlVoice *v);
void ml_phase_play(MlPlayer *p, MlVoice *v);
void ml_phase_quick(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, MlPtr repeat_buffer, MlPtr phase_buffer);
void ml_phase_high(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, uint16_t previous_counter, MlPtr repeat_buffer, MlPtr phase_buffer);
void ml_phase_med(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, uint16_t previous_counter, MlPtr repeat_buffer, MlPtr phase_buffer);
void ml_phase_low(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, uint16_t previous_counter, MlPtr repeat_buffer, MlPtr phase_buffer);
void ml_phase_mova(MlPlayer *p, MlPtr repeat_buffer, MlPtr phase_buffer, uint32_t todo);
void ml_mix_play(MlPlayer *p, MlVoice *v);
void ml_resonance_play(MlPlayer *p, MlVoice *v);
void ml_filter_play(MlPlayer *p, MlVoice *v);
void ml_transform_play(MlPlayer *p, MlVoice *v);
void ml_one_way_counter(MlPlayer *p, MlCounter *counter_info);
void ml_counter(MlPlayer *p, MlCounter *counter_info);

#endif
