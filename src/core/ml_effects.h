/* Musicline Editor replayer: effects (port of MusiclineEditorWorker Player.Effects.cs). */
#ifndef NG_ML_EFFECTS_H
#define NG_ML_EFFECTS_H
#include "ml_player.h"

/* Functions CALLED here that are defined in other C# parts (prototypes assumed):
     void ml_arpeggio_wait_start(MlPlayer *p, MlVoice *v, uint8_t sample_number);                      (Player.cs)
     void ml_play_instrument(MlPlayer *p, MlVoice *v);                                                  (Player.cs)
     void ml_fix_wave_length(MlPlayer *p, MlVoice *v, MlInst *inst, int sample_type, MlPtr sample_pointer); (Player.cs)
     void ml_move_loop(MlPlayer *p, MlVoice *v, MlChannel *channel);                                    (WaveFormEffects.cs; channel = &p->ch[i])
     void ml_transform_play(MlPlayer *p, MlVoice *v);                                                   (WaveFormEffects.cs)
     void ml_phase_play(MlPlayer *p, MlVoice *v);
     void ml_mix_play(MlPlayer *p, MlVoice *v);
     void ml_resonance_play(MlPlayer *p, MlVoice *v);
     void ml_filter_play(MlPlayer *p, MlVoice *v);
   IChannel.SetSample / SetLoop are done inline on p->ch[i] (static helpers in ml_effects.c). */

const int8_t *ml_get_wave_type_table(int wave_type);
void ml_play_effects(MlPlayer *p);
int ml_is_effect_buffer(MlVoice *v, const int8_t *sample_data);
uint16_t ml_get_period(MlPlayer *p, MlVoice *v);
uint16_t ml_get_period2(MlPlayer *p, MlVoice *v, uint16_t note);
void ml_slide_note(MlPlayer *p, MlVoice *v);
void ml_slide_arpeggio_note(MlPlayer *p, MlVoice *v);
void ml_slide_volume(MlPlayer *p, MlVoice *v);
void ml_slide_channel_volume(MlPlayer *p, MlVoice *v);
void ml_slide_master_volume(MlPlayer *p, MlVoice *v);
void ml_slide_arpeggio_volume(MlPlayer *p, MlVoice *v);
void ml_arpeggio_play(MlPlayer *p, MlVoice *v);
void ml_do_arpeggio_effect(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_arpeggio_slide_up(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_arpeggio_slide_down(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_arpeggio_set_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_arpeggio_slide_volume(MlPlayer *p, MlVoice *v, MlEff effect);
void ml_arpeggio_restart(MlPlayer *p, MlVoice *v);
void ml_envelope_play(MlPlayer *p, MlVoice *v);
void ml_tremolo_play(MlPlayer *p, MlVoice *v);
void ml_vibrato_play(MlPlayer *p, MlVoice *v);

#endif
