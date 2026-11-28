/* Musicline Editor replayer: part effects. Port of ml_Player.PartEffects.cs (NostalgicPlayer, MIT). */
#include "ml_player.h"
#include "ml_parteffects.h"

void ml_play_part_fx(MlPlayer *p, MlVoice *v)
{
    v->play |= ML_PF_FIRST_ROW_TICK;
    v->effects1.effect &= (uint8_t)~ML_IE1_HOLD_SUSTAIN;
    v->vibrato = ML_PE_NONE;
    v->vibrato_note = 0;
    v->pt_vibrato_note = 0;
    v->tremolo = ML_PE_NONE;
    v->volume = ML_PE_NONE;
    v->volume_add = ML_PE_NONE;
    v->volume_slide = ML_PE_NONE;
    v->channel_volume_slide = ML_PE_NONE;
    v->master_volume_slide = ML_PE_NONE;
    v->pitch_slide = ML_PE_NONE;
    v->pt_pitch_slide = ML_PE_NONE;
    v->sample_offset = 0;
    v->restart = ML_RF_NONE;
    v->arpeggio &= (uint8_t)~(ML_AF_WILL_SET_NOTE | ML_AF_USE_WAVE_SAMPLE);

    /* TestAndClearArpFlag(OneStepPending) */
    {
        uint8_t was_set = (v->arpeggio & ML_AF_ONE_STEP_PENDING) != 0;
        v->arpeggio &= (uint8_t)~ML_AF_ONE_STEP_PENDING;
        if (was_set)
        {
            v->arpeggio = ML_AF_NONE;
            v->arpeggio_volume_slide = ML_AE_NONE;
            v->arpeggio_pitch_slide = ML_AE_NONE;
            v->arpeggio_pitch_slide_note = 0;
        }
    }

    if (v->part_note != 0)
    {
        v->pitch_slide_to_note = -1;
        v->pt_pitch_slide_to_note = -1;
    }

    for (int i = 0; i < 5; i++)
    {
        MlEff effect = v->part_effects[i];
        switch (effect.effect)
        {
        case ML_PE_SLIDE_UP: ml_pfx_slide_up(p, v, effect); break;
        case ML_PE_SLIDE_DOWN: ml_pfx_slide_down(p, v, effect); break;
        case ML_PE_PORTAMENTO: ml_pfx_portamento(p, v, effect); break;
        case ML_PE_INIT_INSTRUMENT_PORTAMENTO: ml_pfx_init_instrument_portamento(p, v); break;
        case ML_PE_PITCH_UP: ml_pfx_pitch_up(p, v, effect); break;
        case ML_PE_PITCH_DOWN: ml_pfx_pitch_down(p, v, effect); break;
        case ML_PE_VIBRATO_SPEED: ml_pfx_vibrato_speed(p, v, effect); break;
        case ML_PE_VIBRATO_UP: ml_pfx_vibrato_up(p, v, effect); break;
        case ML_PE_VIBRATO_DOWN: ml_pfx_vibrato_down(p, v, effect); break;
        case ML_PE_VIBRATO_WAVE: ml_pfx_vibrato_wave(p, v, effect); break;
        case ML_PE_SET_FINE_TUNE: ml_pfx_set_fine_tune(p, v, effect); break;
        case ML_PE_SET_INSTRUMENT_VOLUME: ml_pfx_set_instrument_volume(p, v, effect); break;
        case ML_PE_INSTRUMENT_VOLUME_SLIDE_UP: ml_pfx_instrument_volume_slide_up(p, v, effect); break;
        case ML_PE_INSTRUMENT_VOLUME_SLIDE_DOWN: ml_pfx_instrument_volume_slide_down(p, v, effect); break;
        case ML_PE_SET_SLIDE_INSTRUMENT_VOLUME: ml_pfx_set_slide_instrument_volume(p, v, effect); break;
        case ML_PE_SLIDE_TO_INSTRUMENT_VOLUME: ml_pfx_slide_to_instrument_volume(p, v, effect); break;
        case ML_PE_INSTRUMENT_VOLUME_ADD: ml_pfx_instrument_volume_add(p, v, effect); break;
        case ML_PE_INSTRUMENT_VOLUME_SUB: ml_pfx_instrument_volume_sub(p, v, effect); break;
        case ML_PE_TREMOLO_SPEED: ml_pfx_tremolo_speed(p, v, effect); break;
        case ML_PE_TREMOLO_UP: ml_pfx_tremolo_up(p, v, effect); break;
        case ML_PE_TREMOLO_DOWN: ml_pfx_tremolo_down(p, v, effect); break;
        case ML_PE_TREMOLO_WAVE: ml_pfx_tremolo_wave(p, v, effect); break;
        case ML_PE_SET_CHANNEL_VOLUME: ml_pfx_set_channel_volume(p, v, effect); break;
        case ML_PE_CHANNEL_VOLUME_SLIDE_UP: ml_pfx_channel_volume_slide_up(p, v, effect); break;
        case ML_PE_CHANNEL_VOLUME_SLIDE_DOWN: ml_pfx_channel_volume_slide_down(p, v, effect); break;
        case ML_PE_SET_SLIDE_CHANNEL_VOLUME: ml_pfx_set_slide_channel_volume(p, v, effect); break;
        case ML_PE_SLIDE_TO_CHANNEL_VOLUME: ml_pfx_slide_to_channel_volume(p, v, effect); break;
        case ML_PE_CHANNEL_VOLUME_ADD: ml_pfx_channel_volume_add(p, v, effect); break;
        case ML_PE_CHANNEL_VOLUME_SUB: ml_pfx_channel_volume_sub(p, v, effect); break;
        case ML_PE_SET_VOLUME_ALL_CHANNELS: ml_pfx_set_volume_all_channels(p, effect); break;
        case ML_PE_SET_MASTER_VOLUME: ml_pfx_set_master_volume(p, effect); break;
        case ML_PE_MASTER_VOLUME_SLIDE_UP: ml_pfx_master_volume_slide_up(p, v, effect); break;
        case ML_PE_MASTER_VOLUME_SLIDE_DOWN: ml_pfx_master_volume_slide_down(p, v, effect); break;
        case ML_PE_SET_SLIDE_MASTER_VOLUME: ml_pfx_set_slide_master_volume(p, v, effect); break;
        case ML_PE_SLIDE_TO_MASTER_VOLUME: ml_pfx_slide_to_master_volume(p, v, effect); break;
        case ML_PE_MASTER_VOLUME_ADD: ml_pfx_master_volume_add(p, v, effect); break;
        case ML_PE_MASTER_VOLUME_SUB: ml_pfx_master_volume_sub(p, v, effect); break;
        case ML_PE_SPEED_ONE_CHANNEL: ml_pfx_speed_one_channel(p, v, effect); break;
        case ML_PE_GROOVE_ONE_CHANNEL: ml_pfx_groove_one_channel(p, v, effect); break;
        case ML_PE_SPEED_ALL_CHANNELS: ml_pfx_speed_all_channels(p, v, effect); break;
        case ML_PE_GROOVE_ALL_CHANNELS: ml_pfx_groove_all_channels(p, v, effect); break;
        case ML_PE_ARPEGGIO_TABLE: ml_pfx_arpeggio_table(p, v, effect); break;
        case ML_PE_ARPEGGIO_TABLE_ONE_STEP: ml_pfx_arpeggio_table_one_step(p, v, effect); break;
        case ML_PE_HOLD_SUSTAIN: ml_pfx_hold_sustain(p, v, effect); break;
        case ML_PE_FILTER: ml_pfx_filter(p, effect); break;
        case ML_PE_SAMPLE_OFFSET: ml_pfx_sample_offset(p, v, effect); break;
        case ML_PE_RESTART_NO_VOLUME: ml_pfx_restart_no_volume(p, v); break;
        case ML_PE_WAVE_SAMPLE: ml_pfx_wave_sample(p, v, effect); break;
        case ML_PE_INIT_INSTRUMENT: ml_pfx_init_instrument(p, v); break;
        case ML_PE_PT_SLIDE_UP: ml_pfx_pt_slide_up(p, v, effect); break;
        case ML_PE_PT_SLIDE_DOWN: ml_pfx_pt_slide_down(p, v, effect); break;
        case ML_PE_PT_PORTAMENTO: ml_pfx_pt_portamento(p, v, effect); break;
        case ML_PE_PT_FINE_SLIDE_UP: ml_pfx_pt_fine_slide_up(p, v, effect); break;
        case ML_PE_PT_FINE_SLIDE_DOWN: ml_pfx_pt_fine_slide_down(p, v, effect); break;
        case ML_PE_PT_VOLUME_SLIDE_UP: ml_pfx_pt_volume_slide_up(p, v, effect); break;
        case ML_PE_PT_VOLUME_SLIDE_DOWN: ml_pfx_pt_volume_slide_down(p, v, effect); break;
        case ML_PE_PT_TREMOLO: ml_pfx_pt_tremolo(p, v, effect); break;
        case ML_PE_PT_TREMOLO_WAVE: ml_pfx_pt_tremolo_wave(p, v, effect); break;
        case ML_PE_PT_VIBRATO: ml_pfx_pt_vibrato(p, v, effect); break;
        case ML_PE_PT_VIBRATO_WAVE: ml_pfx_pt_vibrato_wave(p, v, effect); break;
        }
    }
}

void ml_pfx_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->pitch_slide = effect.effect;
    v->pitch_slide_type = ML_DIR_UPWARD;
    if (effect.argument != 0)
        v->pitch_slide_speed = effect.argument;
    v->pitch_slide_to_note = (int16_t)((59 * 32) + 32);
}

void ml_pfx_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->pitch_slide = effect.effect;
    v->pitch_slide_type = ML_DIR_DOWNWARD;
    if (effect.argument != 0)
        v->pitch_slide_speed = effect.argument;
    v->pitch_slide_to_note = 0;
}

void ml_pfx_portamento(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->pitch_slide = effect.effect;
    if (effect.argument != 0)
        v->pitch_slide_speed = effect.argument;

    uint16_t note = (uint16_t)(v->note + v->pitch_slide_note);
    uint16_t slide_to_note = v->part_note;
    if (slide_to_note != 0)
    {
        slide_to_note = (uint16_t)(slide_to_note << 5);
        v->part_note = 0;
        v->pitch_slide_to_note = (int16_t)slide_to_note;
        if (slide_to_note != note)
        {
            v->pitch_slide_type = slide_to_note < note ? ML_DIR_DOWNWARD : ML_DIR_UPWARD;
            return;
        }
        else
            v->pitch_slide_to_note = -1;
    }
    if (v->pitch_slide_to_note == -1)
        v->pitch_slide = ML_PE_NONE;
}

void ml_pfx_init_instrument_portamento(MlPlayer *p, MlVoice *v)
{
    (void)p;
    v->instrument_pitch_slide = ML_PE_NONE;
}

void ml_pfx_pitch_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->pitch_add = (int16_t)(v->pitch_add + effect.argument);
}

void ml_pfx_pitch_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->pitch_add = (int16_t)(v->pitch_add - effect.argument);
}

void ml_pfx_vibrato_speed(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->vibrato_command_speed = effect.argument;
}

void ml_pfx_vibrato_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    ml_pfx_vibrato(p, v, effect, ML_DIR_UPWARD);
}

void ml_pfx_vibrato_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    ml_pfx_vibrato(p, v, effect, ML_DIR_DOWNWARD);
}

void ml_pfx_vibrato(MlPlayer *p, MlVoice *v, MlEff effect, int direction)
{
    (void)p;
    v->vibrato = effect.effect;
    if (v->part_note != 0)
    {
        v->vibrato_direction = (uint8_t)direction;
        v->vibrato_counter = 0;
        v->vibrato_command_depth = 0;
        v->vibrato_command_delay = 0;
        v->vibrato_attack_speed = 0;
        v->vibrato_attack_length = 0;
    }
    if (effect.argument != 0)
        v->vibrato_depth = (uint16_t)(effect.argument << 8);
    v->vibrato_wave_type = v->part_vibrato_wave_type;
}

void ml_pfx_vibrato_wave(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument <= 3)
        v->vibrato_wave_type = v->part_vibrato_wave_type = effect.argument;
}

void ml_pfx_set_fine_tune(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    int8_t fine_tune = (int8_t)effect.argument;
    if (fine_tune < 0)
    {
        if (fine_tune < -31)
            fine_tune = -31;
    }
    else
    {
        if (fine_tune > 31)
            fine_tune = 31;
    }
    v->fine_tune = fine_tune;
    v->play |= ML_PF_KEEP_EFFECT_FINE_TUNE;
}

void ml_pfx_set_instrument_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->volume = effect.effect;
    v->volume_set = (uint16_t)(effect.argument << 4);
    if (v->volume_set > 1024)
        v->volume_set = 1024;
}

void ml_pfx_instrument_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->volume_slide = effect.effect;
    if (effect.argument != 0)
        v->volume_slide_speed = effect.argument;
}

void ml_pfx_instrument_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->volume_slide = effect.effect;
    if (effect.argument != 0)
        v->volume_slide_speed = effect.argument;
}

void ml_pfx_set_slide_instrument_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->volume_slide_to_volume = (uint16_t)(effect.argument << 4);
}

void ml_pfx_slide_to_instrument_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->volume_slide = effect.effect;
    if (effect.argument != 0)
        v->volume_slide_speed = effect.argument;

    uint16_t volume = v->volume1;
    v->volume_slide_volume = volume;
    uint16_t slide_to_volume = v->volume_slide_to_volume;
    if (slide_to_volume != volume)
    {
        v->volume_slide_type = slide_to_volume < volume ? ML_DIR_DOWNWARD : ML_DIR_UPWARD;
        v->volume_slide_to_volume_off = 0;
    }
    else
        v->volume_slide_to_volume_off = 1;
}

void ml_pfx_instrument_volume_add(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->volume_add = effect.effect;
    if (effect.argument != 0)
        v->volume_add_number = (int16_t)(effect.argument << 4);
}

void ml_pfx_instrument_volume_sub(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->volume_add = effect.effect;
    if (effect.argument != 0)
        v->volume_add_number = (int16_t)(-(effect.argument << 4));
}

void ml_pfx_tremolo_speed(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->tremolo_command_speed = effect.argument;
}

void ml_pfx_tremolo_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    ml_pfx_tremolo(p, v, effect, ML_DIR_UPWARD);
}

void ml_pfx_tremolo_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    ml_pfx_tremolo(p, v, effect, ML_DIR_DOWNWARD);
}

void ml_pfx_tremolo(MlPlayer *p, MlVoice *v, MlEff effect, int direction)
{
    (void)p;
    v->tremolo = effect.effect;
    if (v->part_note != 0)
    {
        v->tremolo_direction = (uint8_t)direction;
        v->tremolo_counter = 0;
        v->tremolo_command_depth = 0;
        v->tremolo_command_delay = 0;
        v->tremolo_attack_speed = 0;
        v->tremolo_attack_length = 0;
    }
    if (effect.argument != 0)
        v->tremolo_depth = (uint16_t)(effect.argument << 8);
    v->tremolo_wave_type = v->part_tremolo_wave_type;
}

void ml_pfx_tremolo_wave(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument <= 3)
        v->tremolo_wave_type = v->part_tremolo_wave_type = effect.argument;
}

void ml_pfx_set_channel_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->channel_volume = (uint16_t)(effect.argument << 4);
    if (v->channel_volume > 64 * 16)
        v->channel_volume = 64 * 16;
}

void ml_pfx_channel_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->channel_volume_slide = effect.effect;
    if (effect.argument != 0)
        v->channel_volume_slide_speed = effect.argument;
}

void ml_pfx_channel_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->channel_volume_slide = effect.effect;
    if (effect.argument != 0)
        v->channel_volume_slide_speed = effect.argument;
}

void ml_pfx_set_slide_channel_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->channel_volume_slide_to_volume = (uint16_t)(effect.argument << 4);
}

void ml_pfx_slide_to_channel_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->channel_volume_slide = effect.effect;
    if (effect.argument != 0)
        v->channel_volume_slide_speed = effect.argument;

    uint16_t volume = v->channel_volume;
    v->channel_volume_slide_volume = volume;
    uint16_t slide_to_volume = v->channel_volume_slide_to_volume;
    if (slide_to_volume != volume)
    {
        v->channel_volume_slide_type = slide_to_volume < volume ? ML_DIR_DOWNWARD : ML_DIR_UPWARD;
        v->channel_volume_slide_to_volume_off = 0;
    }
    else
        v->channel_volume_slide_to_volume_off = 1;
}

void ml_pfx_channel_volume_add(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->channel_volume_add_number = (int16_t)(effect.argument << 4);

    int16_t new_volume = (int16_t)(v->channel_volume + v->channel_volume_add_number);
    if (new_volume > 64 * 16)
        new_volume = 64 * 16;
    v->channel_volume = (uint16_t)new_volume;
}

void ml_pfx_channel_volume_sub(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->channel_volume_add_number = (int16_t)(effect.argument << 4);

    int16_t new_volume = (int16_t)(v->channel_volume - v->channel_volume_add_number);
    if (new_volume < 0)
        new_volume = 0;
    v->channel_volume = (uint16_t)new_volume;
}

void ml_pfx_set_volume_all_channels(MlPlayer *p, MlEff effect)
{
    uint16_t new_volume = (uint16_t)(effect.argument << 4);
    for (int i = 0; i < p->number_of_channels; i++)
        p->voices[i].channel_volume = new_volume;
}

void ml_pfx_set_master_volume(MlPlayer *p, MlEff effect)
{
    p->playing_info.master_volume = (uint16_t)(effect.argument << 4);
}

void ml_pfx_master_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->master_volume_slide = effect.effect;
    if (effect.argument != 0)
        v->master_volume_slide_speed = effect.argument;
}

void ml_pfx_master_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->master_volume_slide = effect.effect;
    if (effect.argument != 0)
        v->master_volume_slide_speed = effect.argument;
}

void ml_pfx_set_slide_master_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->master_volume_slide_to_volume = (uint16_t)(effect.argument << 4);
}

void ml_pfx_slide_to_master_volume(MlPlayer *p, MlVoice *v, MlEff effect)
{
    v->master_volume_slide = effect.effect;
    if (effect.argument != 0)
        v->master_volume_slide_speed = effect.argument;

    uint16_t volume = p->playing_info.master_volume;
    v->master_volume_slide_volume = volume;
    uint16_t slide_to_volume = v->master_volume_slide_to_volume;
    if (slide_to_volume != volume)
    {
        v->master_volume_slide_type = slide_to_volume < volume ? ML_DIR_DOWNWARD : ML_DIR_UPWARD;
        v->master_volume_slide_to_volume_off = 0;
    }
    else
        v->master_volume_slide_to_volume_off = 1;
}

void ml_pfx_master_volume_add(MlPlayer *p, MlVoice *v, MlEff effect)
{
    if (effect.argument != 0)
        v->master_volume_add_number = (int16_t)(effect.argument << 4);

    int16_t new_volume = (int16_t)(p->playing_info.master_volume + v->master_volume_add_number);
    if (new_volume > 64 * 16)
        new_volume = 64 * 16;
    p->playing_info.master_volume = (uint16_t)new_volume;
}

void ml_pfx_master_volume_sub(MlPlayer *p, MlVoice *v, MlEff effect)
{
    if (effect.argument != 0)
        v->master_volume_add_number = (int16_t)(effect.argument << 4);

    int16_t new_volume = (int16_t)(p->playing_info.master_volume - v->master_volume_add_number);
    if (new_volume < 0)
        new_volume = 0;
    p->playing_info.master_volume = (uint16_t)new_volume;
}

void ml_pfx_speed_one_channel(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    uint8_t new_speed = effect.argument;
    if (new_speed != 0)
    {
        if (new_speed > 31)
            new_speed = 31;

        v->speed_part = 1;
        v->speed = new_speed;
        if ((v->groove == 0) || !v->part_groove)
            v->speed_counter = new_speed;
    }
}

void ml_pfx_groove_one_channel(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    uint8_t new_groove = effect.argument;
    if (new_groove != 0)
    {
        if (new_groove > 31)
            new_groove = 31;

        v->groove_part = 1;
        v->groove = new_groove;
        if ((v->groove == 0) || v->part_groove)
            v->speed_counter = new_groove;
    }
}

void ml_pfx_speed_all_channels(MlPlayer *p, MlVoice *v, MlEff effect)
{
    uint8_t new_speed = effect.argument;
    if (new_speed != 0)
    {
        if (new_speed < 32)
        {
            p->playing_info.tune_speed = new_speed;

            for (int i = 0; i < p->number_of_channels; i++)
            {
                MlVoice *work = &p->voices[i];
                if (v->channel_number >= work->channel_number)
                {
                    if (work->speed_part == 0)
                    {
                        work->speed = new_speed;
                        if ((work->groove == 0) || !work->part_groove)
                            work->speed_counter = new_speed;
                    }
                }
                else
                    work->speed = new_speed;
            }
        }
        else
        {
            p->playing_info.tune_tempo = new_speed;
            ml_set_bpm_tempo(p, new_speed);
        }
    }
}

void ml_pfx_groove_all_channels(MlPlayer *p, MlVoice *v, MlEff effect)
{
    uint8_t new_groove = effect.argument;
    if (new_groove != 0)
    {
        new_groove &= 0x1f;
        p->playing_info.tune_groove = new_groove;

        for (int i = 0; i < p->number_of_channels; i++)
        {
            MlVoice *work = &p->voices[i];
            if (v->channel_number >= work->channel_number)
            {
                if (work->groove_part == 0)
                {
                    work->groove = new_groove;
                    if ((new_groove != 0) && !work->part_groove)
                        work->speed_counter = new_groove;
                }
            }
            else
                work->groove = new_groove;
        }
    }
}

void ml_pfx_arpeggio_table(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->arpeggio |= ML_AF_USE_TABLE;
    v->arpeggio_table = effect.argument;
}

void ml_pfx_arpeggio_table_one_step(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->arpeggio |= (ML_AF_ONE_STEP_PENDING | ML_AF_USE_TABLE);
    v->arpeggio_table = effect.argument;
}

void ml_pfx_hold_sustain(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->effects1.effect |= ML_IE1_HOLD_SUSTAIN;
    v->effects1.argument &= (uint8_t)~ML_IF_ENVELOPE_HOLD_SUSTAIN;
    if (effect.argument != 0)
        v->effects1.argument |= ML_IF_ENVELOPE_HOLD_SUSTAIN;
}

void ml_pfx_filter(MlPlayer *p, MlEff effect)
{
    ml_set_amiga_filter(p, effect.argument != 0);
}

void ml_pfx_sample_offset(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->sample_offset_active = 1;
    if (effect.argument != 0)
        v->sample_offset = effect.argument;
}

void ml_pfx_restart_no_volume(MlPlayer *p, MlVoice *v)
{
    (void)p;
    if (v->part_instrument == 0)
        v->restart = ML_RF_RESTART_FROM_PART_FX;
}

void ml_pfx_wave_sample(MlPlayer *p, MlVoice *v, MlEff effect)
{
    if (effect.argument != 0)
    {
        MlSample *sample = &p->song->smp[effect.argument];
        if (sample->data != NULL)
        {
            v->wave_sample = sample;
            v->arpeggio |= ML_AF_USE_WAVE_SAMPLE;
        }
    }
}

void ml_pfx_init_instrument(MlPlayer *p, MlVoice *v)
{
    (void)p;
    v->phase_init = 0;
    v->resonance_init = 0;
    v->filter_init = 0;
    v->transform_init = 0;
    v->mix_init = 0;
    v->loop_init = 0;
}

void ml_pfx_pt_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->pt_pitch_slide = effect.effect;
    v->pt_pitch_slide_type = ML_DIR_UPWARD;
    if (effect.argument != 0)
        v->pt_pitch_slide_speed = effect.argument;
    v->pt_pitch_slide_to_note = 106;
}

void ml_pfx_pt_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->pt_pitch_slide = effect.effect;
    v->pt_pitch_slide_type = ML_DIR_DOWNWARD;
    if (effect.argument != 0)
        v->pt_pitch_slide_speed = effect.argument;
    v->pt_pitch_slide_to_note = 3591;
}

void ml_pfx_pt_portamento(MlPlayer *p, MlVoice *v, MlEff effect)
{
    v->pitch_slide = ML_PE_PORTAMENTO;
    v->pt_pitch_slide = effect.effect;
    if (effect.argument != 0)
        v->pt_pitch_slide_speed2 = effect.argument;

    uint16_t period = (uint16_t)(ml_get_period(p, v) + v->pt_pitch_slide_note);
    if (v->part_note != 0)
    {
        uint16_t period2 = ml_get_period2(p, v, (uint16_t)(v->part_note << 5));
        v->part_note = 0;
        v->pt_pitch_slide_to_note = (int16_t)period2;
        if (period != period2)
        {
            v->pt_pitch_slide_type = period < period2 ? ML_DIR_DOWNWARD : ML_DIR_UPWARD;
            return;
        }
        v->pt_pitch_slide_to_note = -1;
    }
    if (v->pt_pitch_slide_to_note == -1)
    {
        v->pt_pitch_slide = ML_PE_NONE;
        v->pitch_slide = ML_PE_NONE;
    }
}

void ml_pfx_pt_fine_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->pt_pitch_add = (int16_t)(v->pt_pitch_add - (int16_t)(effect.argument & 0x0f));
}

void ml_pfx_pt_fine_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    if (effect.argument != 0)
        v->pt_pitch_add = (int16_t)(v->pt_pitch_add + (int16_t)(effect.argument & 0x0f));
}

void ml_pfx_pt_tremolo(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->tremolo = effect.effect;
    if (effect.argument != 0)
    {
        uint8_t tremolo_command = v->pt_tremolo_command;
        uint8_t argument = (uint8_t)(effect.argument & 0x0f);
        if (argument != 0)
            tremolo_command = (uint8_t)((tremolo_command & 0xf0) | argument);
        argument = (uint8_t)(effect.argument & 0xf0);
        if (argument != 0)
            tremolo_command = (uint8_t)((tremolo_command & 0x0f) | argument);
        v->pt_tremolo_command = tremolo_command;
    }
}

void ml_pfx_pt_tremolo_wave(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->pt_tremolo_wave_type = (uint8_t)(effect.argument & 0x0f);
}

void ml_pfx_pt_vibrato(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->vibrato = effect.effect;
    if (effect.argument != 0)
    {
        uint8_t vibrato_command = v->pt_vibrato_command;
        uint8_t argument = (uint8_t)(effect.argument & 0x0f);
        if (argument != 0)
            vibrato_command = (uint8_t)((vibrato_command & 0xf0) | argument);
        argument = (uint8_t)(effect.argument & 0xf0);
        if (argument != 0)
            vibrato_command = (uint8_t)((vibrato_command & 0x0f) | argument);
        v->pt_vibrato_command = vibrato_command;
    }
}

void ml_pfx_pt_vibrato_wave(MlPlayer *p, MlVoice *v, MlEff effect)
{
    (void)p;
    v->pt_vibrato_wave_type = (uint8_t)(effect.argument & 0x0f);
}

void ml_pfx_pt_volume_slide_up(MlPlayer *p, MlVoice *v, MlEff effect)
{
    ml_pfx_instrument_volume_slide_up(p, v, effect);
}

void ml_pfx_pt_volume_slide_down(MlPlayer *p, MlVoice *v, MlEff effect)
{
    ml_pfx_instrument_volume_slide_down(p, v, effect);
}
