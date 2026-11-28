/* Musicline Editor replayer: effects (port of MusiclineEditorWorker Player.Effects.cs, NostalgicPlayer, MIT). */
#include <string.h>
#include "ml_player.h"
#include "ml_effects.h"

/* functions of the other parts (see ml_effects.h) */
void ml_arpeggio_wait_start(MlPlayer *p, MlVoice *v, uint8_t sample_number);
void ml_play_instrument(MlPlayer *p, MlVoice *v);
void ml_fix_wave_length(MlPlayer *p, MlVoice *v, MlInst *inst, int sample_type, MlPtr sample_pointer);
void ml_move_loop(MlPlayer *p, MlVoice *v, MlChannel *channel);
void ml_transform_play(MlPlayer *p, MlVoice *v);
void ml_phase_play(MlPlayer *p, MlVoice *v);
void ml_mix_play(MlPlayer *p, MlVoice *v);
void ml_resonance_play(MlPlayer *p, MlVoice *v);
void ml_filter_play(MlPlayer *p, MlVoice *v);

/* IChannel.SetSample / SetLoop on the Paula-like channel */
static void ch_set_sample(MlChannel *c, const int8_t *data, uint32_t off, uint32_t len)
{
    c->data = data + off; c->len = len;
}
static void ch_set_loop(MlChannel *c, const int8_t *data, uint32_t off, uint32_t len)
{
    c->loop_data = data + off; c->loop_len = len; c->loop_set = 1;
}

const int8_t *ml_get_wave_type_table(int wave_type)
{
    switch (wave_type)
    {
        case ML_WT_SINE: return ml_sine;
        case ML_WT_RAMP_DOWN: return ml_down_ramp;
        case ML_WT_SAWTOOTH: return ml_sawtooth;
        case ML_WT_SQUARE: return ml_square;
        default: return ml_sine;    /* C#: throws ArgumentOutOfRangeException */
    }
}

void ml_play_effects(MlPlayer *p)
{
    for (int i = 0; i < p->number_of_channels; i++)
    {
        MlVoice *v = &p->voices[i];
        MlChannel *channel = &p->ch[i];

        v->wave_sample_repeat_pointer = v->wave_sample_repeat_pointer_original;

        if (!(v->play & ML_PF_RETRIGGER))
        {
            ml_slide_volume(p, v);
            ml_slide_channel_volume(p, v);
            ml_slide_master_volume(p, v);
            ml_slide_arpeggio_volume(p, v);
            ml_slide_note(p, v);
            ml_slide_arpeggio_note(p, v);
            ml_arpeggio_play(p, v);
            ml_vibrato_play(p, v);
            ml_tremolo_play(p, v);
        }

        ml_envelope_play(p, v);
        ml_move_loop(p, v, channel);
        ml_transform_play(p, v);
        ml_phase_play(p, v);
        ml_mix_play(p, v);
        ml_resonance_play(p, v);
        ml_filter_play(p, v);

        if (v->wave_sample_repeat_pointer.has && ml_is_effect_buffer(v, v->wave_sample_repeat_pointer.data))
        {
            int8_t *source_buffer = v->wave_sample_repeat_pointer.data;
            int8_t *wave_buffer = v->wave_buffer;
            MlPtr new_pointer = { wave_buffer, 256, 0, 1 };

            v->wave_sample_repeat_pointer = new_pointer;

            if ((v->wave_or_sample != ML_ST_SAMPLE) || (v->effects1.effect & ML_IE1_LOOP))
                v->wave_sample_pointer = new_pointer;

            {
                uint32_t n = (uint32_t)v->wave_sample_repeat_length * 2;
                if (n > 256) n = 256;
                memmove(wave_buffer, source_buffer, n);
            }
        }

        if (!(v->play & ML_PF_RETRIGGER) && (v->effects1.effect & ML_IE1_LOOP))
        {
            ch_set_sample(channel, v->wave_sample_repeat_pointer.data, v->wave_sample_repeat_pointer.start_offset, (uint32_t)v->loop_length * 2U);
            ch_set_loop(channel, v->wave_sample_repeat_pointer.data, v->wave_sample_repeat_pointer.start_offset, (uint32_t)v->loop_length * 2U);
            v->wave_sample_repeat_length = v->loop_length;
        }

        if (!v->arpeggio_wait)
            v->part_note = 0;

        v->play &= ML_PF_RETRIGGER;
    }
}

int ml_is_effect_buffer(MlVoice *v, const int8_t *sample_data)
{
    return (sample_data == v->transform_wave_buffer) || (sample_data == v->phase_wave_buffer) ||
           (sample_data == v->mix_wave_buffer) || (sample_data == v->resonance_wave_buffer) ||
           (sample_data == v->filter_wave_buffer);
}

uint16_t ml_get_period(MlPlayer *p, MlVoice *v)
{
    return ml_get_period2(p, v, v->note);
}

uint16_t ml_get_period2(MlPlayer *p, MlVoice *v, uint16_t note)
{
    int16_t new_note;
    (void)p;

    new_note = (int16_t)(note + v->vibrato_note + v->pitch_slide_note + v->arpeggio_pitch_slide_note + v->semi_tone + v->fine_tune + v->pitch_add);

    if (v->transpose != 0)
        new_note = (int16_t)(new_note + v->transpose);

    if (new_note < -32)
        new_note = -32;
    else if (new_note > 5 * 12 * 32)
        new_note = 5 * 12 * 32;

    return ml_pal_pitch_table[new_note + 32];
}

void ml_slide_note(MlPlayer *p, MlVoice *v)
{
    if (v->pt_pitch_slide == ML_PE_NONE)
    {
        if ((v->pitch_slide != ML_PE_NONE) || (v->instrument_pitch_slide != ML_PE_NONE))
        {
            if (v->pitch_slide_to_note < 0)
                return;

            if (v->pitch_slide_type == ML_DIR_DOWNWARD)
            {
                v->pitch_slide_note = (uint16_t)(v->pitch_slide_note - v->pitch_slide_speed);

                int16_t note = (int16_t)(v->note + v->pitch_slide_note - v->pitch_slide_to_note);
                if (note <= 0)
                {
                    v->pitch_slide_note = (uint16_t)(v->pitch_slide_note - note);
                    v->pitch_slide_to_note = -1;
                }
            }
            else
            {
                v->pitch_slide_note = (uint16_t)(v->pitch_slide_note + v->pitch_slide_speed);

                int16_t note = (int16_t)(v->note + v->pitch_slide_note - v->pitch_slide_to_note);
                if (note >= 0)
                {
                    v->pitch_slide_note = (uint16_t)(v->pitch_slide_note - note);
                    v->pitch_slide_to_note = -1;
                }
            }
        }
    }
    else
    {
        if (!(v->play & ML_PF_FIRST_ROW_TICK) && (v->pt_pitch_slide_to_note >= 0))
        {
            uint16_t speed = v->pt_pitch_slide_speed;
            if (v->pt_pitch_slide == ML_PE_PT_PORTAMENTO)
                speed = v->pt_pitch_slide_speed2;

            if (v->pt_pitch_slide_type == ML_DIR_DOWNWARD)
            {
                v->pt_pitch_slide_note = (uint16_t)(v->pt_pitch_slide_note + speed);

                int16_t period = (int16_t)(ml_get_period(p, v) + v->pt_pitch_slide_note - v->pt_pitch_slide_to_note);
                if (period >= 0)
                {
                    v->pt_pitch_slide_note = (uint16_t)(v->pt_pitch_slide_note - period);
                    v->pt_pitch_slide_to_note = -1;
                }
            }
            else
            {
                v->pt_pitch_slide_note = (uint16_t)(v->pt_pitch_slide_note - speed);

                int16_t period = (int16_t)(ml_get_period(p, v) + v->pt_pitch_slide_note - v->pt_pitch_slide_to_note);
                if (period <= 0)
                {
                    v->pt_pitch_slide_note = (uint16_t)(v->pt_pitch_slide_note - period);
                    v->pt_pitch_slide_to_note = -1;
                }
            }
        }
    }
}

void ml_slide_arpeggio_note(MlPlayer *p, MlVoice *v)
{
    (void)p;
    if ((v->arpeggio_pitch_slide != ML_AE_NONE) && (v->arpeggio_pitch_slide_to_note >= 0))
    {
        if (v->arpeggio_pitch_slide_type == ML_DIR_DOWNWARD)
        {
            v->arpeggio_pitch_slide_note = (uint16_t)(v->arpeggio_pitch_slide_note - v->arpeggio_pitch_slide_speed);

            int16_t note = (int16_t)(v->arpeggio_calculated_note + v->arpeggio_pitch_slide_note - v->arpeggio_pitch_slide_to_note);
            if (note <= 0)
            {
                v->arpeggio_pitch_slide_note = (uint16_t)(v->arpeggio_pitch_slide_note - note);
                v->arpeggio_pitch_slide_to_note = -1;
            }
        }
        else
        {
            v->arpeggio_pitch_slide_note = (uint16_t)(v->arpeggio_pitch_slide_note + v->arpeggio_pitch_slide_speed);

            int16_t note = (int16_t)(v->arpeggio_calculated_note + v->arpeggio_pitch_slide_note - v->arpeggio_pitch_slide_to_note);
            if (note >= 0)
            {
                v->arpeggio_pitch_slide_note = (uint16_t)(v->arpeggio_pitch_slide_note - note);
                v->arpeggio_pitch_slide_to_note = -1;
            }
        }
    }
}

void ml_slide_volume(MlPlayer *p, MlVoice *v)
{
    int16_t volume;
    (void)p;

    if (v->volume_slide == ML_PE_SLIDE_TO_INSTRUMENT_VOLUME)
    {
        if (v->volume_slide_to_volume_off)
            return;

        if (v->volume_slide_type == ML_DIR_DOWNWARD)
        {
            volume = (int16_t)(v->volume1 - v->volume_slide_speed);
            if (volume < v->volume_slide_to_volume)
            {
                volume = (int16_t)v->volume_slide_to_volume;
                v->volume_slide_to_volume_off = 1;
            }
        }
        else
        {
            volume = (int16_t)(v->volume1 + v->volume_slide_speed);
            if (volume > v->volume_slide_to_volume)
            {
                volume = (int16_t)v->volume_slide_to_volume;
                v->volume_slide_to_volume_off = 1;
            }
        }

        v->volume1 = v->volume2 = v->volume3 = (uint16_t)volume;
    }
    else if ((v->volume_slide == ML_PE_INSTRUMENT_VOLUME_SLIDE_UP) || ((v->volume_slide == ML_PE_PT_VOLUME_SLIDE_UP) && !(v->play & ML_PF_FIRST_ROW_TICK)))
    {
        volume = (int16_t)(v->volume1 + v->volume_slide_speed);
        if (volume > 64 * 16)
            volume = 64 * 16;

        v->volume1 = v->volume2 = v->volume3 = (uint16_t)volume;
    }
    else if ((v->volume_slide == ML_PE_INSTRUMENT_VOLUME_SLIDE_DOWN) || ((v->volume_slide == ML_PE_PT_VOLUME_SLIDE_DOWN) && !(v->play & ML_PF_FIRST_ROW_TICK)))
    {
        volume = (int16_t)(v->volume1 - v->volume_slide_speed);
        if (volume < 0)
            volume = 0;

        v->volume1 = v->volume2 = v->volume3 = (uint16_t)volume;
    }
}

void ml_slide_channel_volume(MlPlayer *p, MlVoice *v)
{
    int16_t volume;
    (void)p;

    if (v->channel_volume_slide == ML_PE_SLIDE_TO_CHANNEL_VOLUME)
    {
        if (v->channel_volume_slide_to_volume_off)
            return;

        if (v->channel_volume_slide_type == ML_DIR_DOWNWARD)
        {
            volume = (int16_t)(v->channel_volume - v->channel_volume_slide_speed);
            if (volume < v->channel_volume_slide_to_volume)
            {
                volume = (int16_t)v->channel_volume_slide_to_volume;
                v->channel_volume_slide_to_volume_off = 1;
            }
        }
        else
        {
            volume = (int16_t)(v->channel_volume + v->channel_volume_slide_speed);
            if (volume > v->channel_volume_slide_to_volume)
            {
                volume = (int16_t)v->channel_volume_slide_to_volume;
                v->channel_volume_slide_to_volume_off = 1;
            }
        }

        v->channel_volume = (uint16_t)volume;
    }
    else if (v->channel_volume_slide == ML_PE_CHANNEL_VOLUME_SLIDE_UP)
    {
        volume = (int16_t)(v->channel_volume + v->channel_volume_slide_speed);
        if (volume > 64 * 16)
            volume = 64 * 16;

        v->channel_volume = (uint16_t)volume;
    }
    else if (v->channel_volume_slide == ML_PE_CHANNEL_VOLUME_SLIDE_DOWN)
    {
        volume = (int16_t)(v->channel_volume - v->channel_volume_slide_speed);
        if (volume < 0)
            volume = 0;

        v->channel_volume = (uint16_t)volume;
    }
}

void ml_slide_master_volume(MlPlayer *p, MlVoice *v)
{
    int16_t volume;

    if (v->master_volume_slide == ML_PE_SLIDE_TO_MASTER_VOLUME)
    {
        if (v->master_volume_slide_to_volume_off)
            return;

        if (v->master_volume_slide_type == ML_DIR_DOWNWARD)
        {
            volume = (int16_t)(p->playing_info.master_volume - v->master_volume_slide_speed);
            if (volume < v->master_volume_slide_to_volume)
            {
                volume = (int16_t)v->master_volume_slide_to_volume;
                v->master_volume_slide_to_volume_off = 1;
            }
        }
        else
        {
            volume = (int16_t)(p->playing_info.master_volume + v->master_volume_slide_speed);
            if (volume > v->master_volume_slide_to_volume)
            {
                volume = (int16_t)v->master_volume_slide_to_volume;
                v->master_volume_slide_to_volume_off = 1;
            }
        }

        p->playing_info.master_volume = (uint16_t)volume;
    }
    else if (v->master_volume_slide == ML_PE_MASTER_VOLUME_SLIDE_UP)
    {
        volume = (int16_t)(p->playing_info.master_volume + v->master_volume_slide_speed);
        if (volume > 64 * 16)
            volume = 64 * 16;

        p->playing_info.master_volume = (uint16_t)volume;
    }
    else if (v->master_volume_slide == ML_PE_MASTER_VOLUME_SLIDE_DOWN)
    {
        volume = (int16_t)(p->playing_info.master_volume - v->master_volume_slide_speed);
        if (volume < 0)
            volume = 0;

        p->playing_info.master_volume = (uint16_t)volume;
    }
}

void ml_slide_arpeggio_volume(MlPlayer *p, MlVoice *v)
{
    int16_t volume;
    (void)p;

    if (v->arpeggio_volume_slide == ML_AE_VOLUME_SLIDE_UP)
    {
        volume = (int16_t)(v->volume1 + v->arpeggio_volume_slide_speed);
        if (volume > 64 * 16)
            volume = 64 * 16;

        v->volume1 = v->volume2 = v->volume3 = (uint16_t)volume;
    }
    else if (v->arpeggio_volume_slide == ML_AE_VOLUME_SLIDE_DOWN)
    {
        volume = (int16_t)(v->volume1 - v->arpeggio_volume_slide_speed);
        if (volume < 0)
            volume = 0;

        v->volume1 = v->volume2 = v->volume3 = (uint16_t)volume;
    }
}

void ml_arpeggio_play(MlPlayer *p, MlVoice *v)
{
    if ((v->arpeggio & ML_AF_USE_TABLE) || (v->arpeggio & ML_AF_ENABLED))
    {
        v->arpeggio_speed_counter--;
        if (v->arpeggio_speed_counter != 0)
            return;

        MlInst *inst = v->instrument;

        uint8_t spd = (uint8_t)inst->arpeggio_speed;

        v->arpeggio_groove = !v->arpeggio_groove;
        if (v->arpeggio_groove && (inst->arpeggio_groove != 0))
            spd = inst->arpeggio_groove;

        v->arpeggio_speed_counter = spd;

        uint16_t arpeggio_number = inst->arpeggio_table;
        if (v->arpeggio & ML_AF_USE_TABLE)
            arpeggio_number = v->arpeggio_table;

        MlArp *arpeggio = (arpeggio_number < 256) ? p->song->arp[arpeggio_number] : NULL;
        if (arpeggio == NULL)
            return;

        for (;;)
        {
            uint8_t arpeggio_position = v->arpeggio_position++;
            v->arpeggio_position &= 0x7f;

            const MlArpLine *arpeggio_line = &arpeggio->line[arpeggio_position & 0x7f];

            if (v->arpeggio_wait && (arpeggio_line->transpose == 0))
                return;

            int8_t transpose = arpeggio_line->transpose;
            if (transpose == 61)
            {
                v->arpeggio = ML_AF_NONE;
                v->note = v->arpeggio_calculated_note;
                return;
            }

            if (transpose == 62)
            {
                if (arpeggio_line->sample != arpeggio_position)
                    v->arpeggio_position = arpeggio_line->sample;

                continue;
            }

            uint8_t sample_number = arpeggio_line->sample;
            if (sample_number == 0)
                sample_number = inst->sample_number;

            v->wave_sample_number = sample_number;
            v->restart = ML_RF_NONE;
            v->arpeggio_pitch_slide = ML_AE_NONE;
            v->arpeggio_volume_slide = ML_AE_NONE;

            for (int i = 0; i < 2; i++)
            {
                MlEff effect = arpeggio_line->eff[i];

                if (effect.effect == ML_AE_RESTART)
                    ml_arpeggio_restart(p, v);
                else
                    ml_do_arpeggio_effect(p, v, effect);
            }

            v->arpeggio &= (uint8_t)~ML_AF_NOTE_IS_ABSOLUTE;

            if (transpose == 0)
                return;

            if (transpose < 0)
                transpose = (int8_t)(transpose + (int8_t)(61 + v->arpeggio_note));
            else
                v->arpeggio |= ML_AF_NOTE_IS_ABSOLUTE;

            v->arpeggio_calculated_note = v->note = (uint16_t)(transpose * 32);
            v->arpeggio_pitch_slide_note = 0;

            if (v->arpeggio_wait)
            {
                v->arpeggio_wait = 0;
                ml_arpeggio_wait_start(p, v, arpeggio_line->sample);
                ml_play_instrument(p, v);
                return;
            }
            v->arpeggio_wait = 0;

            MlSample *sample;
            uint16_t repeat_length;

            if (v->restart & ML_RF_RESTART_FROM_ARP)
            {
                v->arpeggio |= ML_AF_WILL_SET_NOTE;

                if (arpeggio_line->sample != 0)
                {
                    sample = &p->song->smp[arpeggio_line->sample];
                    if (sample->data == NULL)
                        return;

                    v->wave_sample = sample;
                    v->arpeggio |= ML_AF_USE_WAVE_SAMPLE;
                }

                ml_play_instrument(p, v);
                return;
            }

            if (arpeggio_line->sample == 0)
            {
                v->semi_tone = (int16_t)(inst->semi_tone * 32);

                if (!(v->play & ML_PF_KEEP_EFFECT_FINE_TUNE))
                    v->fine_tune = inst->fine_tune;

                return;
            }

            v->play |= ML_PF_RETRIGGER;

            sample = &p->song->smp[arpeggio_line->sample];
            if (sample->data == NULL)
                return;

            v->semi_tone = (int16_t)(sample->semi_tone * 32);

            if (!(v->play & ML_PF_KEEP_EFFECT_FINE_TUNE))
                v->fine_tune = sample->fine_tune;

            v->wave_or_sample = sample->sample_type;
            if (v->wave_or_sample != ML_ST_SAMPLE)
            {
                int sample_type = inst->sample_type;
                if (sample_type == ML_ST_SAMPLE)
                    sample_type = ML_ST_WAVE64;

                v->wave_or_sample = (uint8_t)sample_type;

                MlPtr sp = { sample->data, (uint32_t)sample->data_len, 0, 1 };
                ml_fix_wave_length(p, v, inst, sample_type, sp);
            }
            else
            {
                if (sample->data != NULL)
                {
                    MlPtr sp = { sample->data, (uint32_t)sample->data_len, 0, 1 };
                    v->wave_sample_pointer = sp;
                }
                else
                    v->wave_sample_pointer = (MlPtr){ 0 };

                v->wave_sample_length = sample->sample_length;

                MlPtr repeat_pointer = sample->sample_repeat_pointer;
                repeat_length = sample->sample_repeat_length;

                if (repeat_length == 0)
                {
                    repeat_pointer = (MlPtr){ 0 };
                    repeat_length = 0;
                }

                v->wave_sample_repeat_pointer = repeat_pointer;
                v->wave_sample_repeat_pointer_original = repeat_pointer;
                v->wave_sample_repeat_length = repeat_length;
            }

            repeat_length = v->wave_sample_repeat_length;
            repeat_length = (uint16_t)(repeat_length - 8);
            if (repeat_length != 0)
            {
                repeat_length = (uint16_t)(repeat_length - 8);
                if (repeat_length != 0)
                {
                    repeat_length = (uint16_t)(repeat_length - 16);
                    if (repeat_length != 0)
                    {
                        repeat_length = (uint16_t)(repeat_length - 32);
                        if (repeat_length != 0)
                        {
                            repeat_length = (uint16_t)(repeat_length - 64);
                            if (repeat_length != 0)
                            {
                                v->effects2.effect = ML_IE2_NONE;
                                return;
                            }
                        }
                    }
                }
            }

            v->effects2.effect = inst->effect2;
            return;
        }
    }
}

void ml_do_arpeggio_effect(MlPlayer *p, MlVoice *v, MlEff effect)
{
    switch (effect.effect)
    {
        case ML_AE_SLIDE_UP:
            ml_arpeggio_slide_up(p, v, effect);
            break;
        case ML_AE_SLIDE_DOWN:
            ml_arpeggio_slide_down(p, v, effect);
            break;
        case ML_AE_SET_VOLUME:
            ml_arpeggio_set_volume(p, v, effect);
            break;
        case ML_AE_VOLUME_SLIDE_UP:
        case ML_AE_VOLUME_SLIDE_DOWN:
            ml_arpeggio_slide_volume(p, v, effect);
            break;
    }
}

void ml_arpeggio_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->arpeggio_pitch_slide = effect.effect;
    v->arpeggio_pitch_slide_type = ML_DIR_UPWARD;

    if (effect.argument != 0)
    {
        v->arpeggio_pitch_slide_speed = effect.argument;
        v->arpeggio_pitch_slide_to_note = (59 * 32) + 32;
    }
}

void ml_arpeggio_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->arpeggio_pitch_slide = effect.effect;
    v->arpeggio_pitch_slide_type = ML_DIR_DOWNWARD;

    if (effect.argument != 0)
    {
        v->arpeggio_pitch_slide_speed = effect.argument;
        v->arpeggio_pitch_slide_to_note = 0;
    }
}

void ml_arpeggio_set_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    uint16_t volume = (uint16_t)(effect.argument << 4);
    if (volume > 64 * 16)
        volume = 64 * 16;

    v->restart |= ML_RF_ARP_HAS_SET_VOLUME;
    v->volume1 = volume;
    v->volume2 = volume;
    v->volume3 = volume;
}

void ml_arpeggio_slide_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->arpeggio_volume_slide = effect.effect;

    if (effect.argument != 0)
        v->arpeggio_volume_slide_speed = effect.argument;
}

void ml_arpeggio_restart(MlPlayer *p, MlVoice *v)
{
    (void)p;
    v->restart |= ML_RF_RESTART_FROM_ARP;

    if (v->effects1.argument & ML_IF_PHASE_INIT)
        v->phase_init = 0;

    /* original bug kept: ResonanceInit is tested on Effect1 (PhaseStep) */
    if (v->effects1.argument & ML_IF_PHASE_STEP)
        v->resonance_init = 0;

    if (v->effects1.argument & ML_IF_FILTER_INIT)
        v->filter_init = 0;

    if (v->effects1.argument & ML_IF_TRANSFORM_INIT)
        v->transform_init = 0;

    /* same bug with MixInit */
    if (v->effects1.argument & ML_IF_ENVELOPE_HOLD_SUSTAIN)
        v->mix_init = 0;

    if (v->effects2.argument & ML_MF_LOOP_INIT)
        v->loop_init = 0;
}

void ml_envelope_play(MlPlayer *p, MlVoice *v)
{
    (void)p;
    if (v->effects1.effect & ML_IE1_ENVELOPE)
    {
        uint16_t *data_array = v->envelope_data;
        int index = 0;

        uint16_t data = data_array[index++];
        if (data == 0)
        {
            data = data_array[index++];
            if (data == 0)
            {
                data = data_array[index++];
                if (data == 0)
                {
                    if (v->effects1.argument & ML_IF_ENVELOPE_HOLD_SUSTAIN)
                    {
                        data = data_array[index + 7];
                        v->volume3 = (uint16_t)((data * v->volume2) >> 6);
                        if (v->volume3 > 1024)
                            v->volume3 = 1024;

                        return;
                    }

                    data = data_array[index++];
                    if (data == 0)
                    {
                        data = data_array[index + 7];
                        v->volume3 = (uint16_t)((data * v->volume2) >> 6);
                        if (v->volume3 > 1024)
                            v->volume3 = 1024;

                        return;
                    }
                }
            }
        }

        uint16_t volume = (uint16_t)(v->envelope_volume + (int16_t)data_array[index + 3]);
        v->envelope_volume = volume;

        volume >>= 8;

        data--;
        data_array[--index] = data;

        if (data == 0)
            volume = data_array[index + 8];

        v->volume3 = (uint16_t)((volume * v->volume2) >> 6);
        if (v->volume3 > 1024)
            v->volume3 = 1024;
    }
}

void ml_tremolo_play(MlPlayer *p, MlVoice *v)
{
    (void)p;
    if (v->tremolo == ML_PE_PT_VIBRATO)
    {
        if (v->play & ML_PF_FIRST_ROW_TICK)
            return;

        uint8_t position = (uint8_t)((v->pt_tremolo_position >> 2) & 0x1f);
        uint8_t wave_data;

        switch (v->pt_tremolo_wave_type)
        {
            case ML_PTW_SINE:
                wave_data = ml_pt_vibrato_table[position];
                break;

            case ML_PTW_RAMP_DOWN:
                position = (uint8_t)(position << 3);
                if (v->pt_tremolo_position < 0)
                    wave_data = (uint8_t)(255 - position);
                else
                    wave_data = position;
                break;

            default:
                wave_data = 255;
                break;
        }

        int tremolo_value = ((v->pt_tremolo_command & 15) * wave_data) >> 2;

        if (v->pt_tremolo_position < 0)
        {
            if (tremolo_value >= 0)
                tremolo_value = -tremolo_value;
        }
        else
        {
            if (tremolo_value < 0)
                tremolo_value = -tremolo_value;
        }

        int volume = v->volume1;
        if (volume != 0)
        {
            volume += tremolo_value;
            if (volume < 0)
                volume = 0;
            else if (volume > 64 * 16)
                volume = 64 * 16;
        }

        v->volume2 = (uint16_t)volume;
        v->volume3 = (uint16_t)volume;

        v->pt_tremolo_position = (int8_t)(v->pt_tremolo_position + (int8_t)((v->pt_tremolo_command >> 2) & 0x3c));
    }
    else
    {
        if (v->tremolo == ML_PE_NONE)
        {
            if (!(v->effects1.effect & ML_IE1_TREMOLO))
                return;

            if (v->tremolo_command_delay != 0)
            {
                v->tremolo_command_delay--;
                return;
            }
        }

        if (v->tremolo_attack_length == 0)
            v->tremolo_command_depth = v->tremolo_depth;
        else
        {
            v->tremolo_command_depth = (uint16_t)(v->tremolo_command_depth + v->tremolo_attack_speed);
            v->tremolo_attack_length--;

            if (v->tremolo_attack_length == 0)
                v->tremolo_command_depth = v->tremolo_depth;
        }

        uint16_t tremolo_counter = (uint16_t)(v->tremolo_counter >> 2);
        uint16_t tremolo_command_speed = v->tremolo_command_speed;
        uint16_t tremolo_command_depth = (uint16_t)(v->tremolo_command_depth >> 8);

        const int8_t *wave_type_table = ml_get_wave_type_table(v->tremolo_wave_type);
        int8_t wave_data = wave_type_table[tremolo_counter];

        if (v->tremolo_direction == ML_DIR_DOWNWARD)
            wave_data = (int8_t)-wave_data;

        int16_t tremolo_value = (int16_t)((wave_data * tremolo_command_depth) >> 1);
        if (tremolo_value < 0)
            tremolo_value = (int16_t)(tremolo_value + 16);

        int volume = v->volume1;
        if (v->volume1 != 0)
        {
            volume += tremolo_value;
            if (volume < 0)
                volume = 0;
            else if (volume > 64 * 16)
                volume = 64 * 16;
        }

        v->volume2 = (uint16_t)volume;
        v->volume3 = (uint16_t)volume;

        v->tremolo_counter = (uint16_t)((v->tremolo_counter + tremolo_command_speed) & 0x1ff);
    }
}

void ml_vibrato_play(MlPlayer *p, MlVoice *v)
{
    (void)p;
    if (v->vibrato == ML_PE_PT_VIBRATO)
    {
        if (v->play & ML_PF_FIRST_ROW_TICK)
            return;

        uint8_t position = (uint8_t)((v->pt_vibrato_position >> 2) & 0x1f);
        uint8_t wave_data;

        switch (v->pt_vibrato_wave_type)
        {
            case ML_PTW_SINE:
                wave_data = ml_pt_vibrato_table[position];
                break;

            case ML_PTW_RAMP_DOWN:
                position = (uint8_t)(position << 3);
                if (v->pt_vibrato_position < 0)
                    wave_data = (uint8_t)(255 - position);
                else
                    wave_data = position;
                break;

            default:
                wave_data = 255;
                break;
        }

        int vibrato_value = ((v->pt_vibrato_command & 15) * wave_data) >> 7;

        if (v->pt_vibrato_position < 0)
        {
            if (vibrato_value >= 0)
                vibrato_value = -vibrato_value;
        }
        else
        {
            if (vibrato_value < 0)
                vibrato_value = -vibrato_value;
        }

        v->pt_vibrato_note = (uint16_t)vibrato_value;

        v->pt_vibrato_position = (int8_t)(v->pt_vibrato_position + (int8_t)((v->pt_vibrato_command >> 2) & 0x3c));
    }
    else
    {
        if (v->vibrato == ML_PE_NONE)
        {
            if (!(v->effects1.effect & ML_IE1_VIBRATO))
                return;

            if (v->vibrato_command_delay != 0)
            {
                v->vibrato_command_delay--;
                return;
            }
        }

        if (v->vibrato_attack_length == 0)
            v->vibrato_command_depth = v->vibrato_depth;
        else
        {
            v->vibrato_command_depth = (uint16_t)(v->vibrato_command_depth + v->vibrato_attack_speed);
            v->vibrato_attack_length--;

            if (v->vibrato_attack_length == 0)
                v->vibrato_command_depth = v->vibrato_depth;
        }

        uint16_t vibrato_counter = (uint16_t)(v->vibrato_counter >> 2);
        uint16_t vibrato_command_speed = v->vibrato_command_speed;
        uint16_t vibrato_command_depth = (uint16_t)(v->vibrato_command_depth >> 8);

        const int8_t *wave_type_table = ml_get_wave_type_table(v->vibrato_wave_type);
        int8_t wave_data = wave_type_table[vibrato_counter];

        if (v->vibrato_direction == ML_DIR_DOWNWARD)
            wave_data = (int8_t)-wave_data;

        int16_t vibrato_note = (int16_t)((wave_data * vibrato_command_depth) >> 4);
        if (vibrato_note < 0)
            vibrato_note++;

        v->vibrato_note = (uint16_t)vibrato_note;

        v->vibrato_counter = (uint16_t)((v->vibrato_counter + vibrato_command_speed) & 0x1ff);
    }
}
