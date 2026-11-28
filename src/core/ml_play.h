/* Musicline Editor replayer: play loop, sequencer, instrument start, period (port of ml_Player.cs). */
#ifndef NG_ML_PLAY_H
#define NG_ML_PLAY_H
#include "ml_player.h"

/* Functions called here that are defined in other C# parts or elsewhere (assumed prototypes):
   void ml_play_effects(MlPlayer *p);                                       // PlayEffects()                       (Effects part)
   void ml_do_arpeggio_effect(MlPlayer *p, MlVoice *v, MlEff effect);       // DoArpeggioEffect(voiceInfo, effect) (Effects part)
   void ml_on_end_reached(MlPlayer *p, int channel);                        // OnEndReached(channel)               (mixer/glue)
   void ml_play_part_fx(MlPlayer *p, MlVoice *v);                           // PlayPartFx(voiceInfo)               (ml_parteffects.h)
*/
void ml_play_effects(MlPlayer *p);
void ml_do_arpeggio_effect(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_on_end_reached(MlPlayer *p, int channel);

void ml_stop_play(MlPlayer *p);
void ml_play_module(MlPlayer *p);
void ml_dma_play(MlPlayer *p);
void ml_play_tune(MlPlayer *p);
void ml_play_voice(MlPlayer *p, MlVoice *v, const uint16_t *sequence);
uint8_t ml_parse_control_command(MlPlayer *p, MlVoice *v, uint16_t sequence_data);
uint8_t ml_parse_part(MlPlayer *p, MlVoice *v, uint16_t sequence_data);
void ml_check_instrument(MlPlayer *p, MlVoice *v);
void ml_play_arpeggio(MlPlayer *p, MlVoice *v);
void ml_arpeggio_wait_start(MlPlayer *p, MlVoice *v, uint8_t sample_number);
void ml_play_instrument(MlPlayer *p, MlVoice *v);
void ml_fix_wave_length(MlPlayer *p, MlVoice *v, MlInst *inst, uint8_t sample_type, MlPtr sample_pointer);
void ml_inst_play(MlPlayer *p, MlVoice *v, MlInst *inst);
void ml_period_calculation(MlPlayer *p);
void ml_period(MlPlayer *p, MlVoice *v);
void ml_period_volume_play(MlPlayer *p);

#endif
