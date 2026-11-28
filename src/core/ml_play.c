/* Musicline Editor replayer: play loop, sequencer, instrument start, period.
   Port of NostalgicPlayer's ml_Player.cs (MIT, Polycode) to C. */
#include "ml_play.h"
#include "ml_parteffects.h"

/* the C# base class InstrumentSampleBase, as a value copy of the eight shared fields */
typedef struct {
    uint8_t sample_type;
    MlPtr sample_pointer, sample_repeat_pointer;
    uint16_t sample_length, sample_repeat_length;
    int16_t fine_tune, semi_tone;
} SampBase;

static const MlPartLine zero_line = {0};

void ml_stop_play(MlPlayer *p)
{
    int i;
    for (i = 0; i < 8; i++)
        p->voices[i].wave_sample_number_old = 0;
    p->playing_enabled = 0;
}

void ml_play_module(MlPlayer *p)
{
    if (p->playing_enabled) {
        ml_play_tune(p);
        ml_play_effects(p);
        ml_period_calculation(p);
        ml_period_volume_play(p);
        ml_dma_play(p);
    }
}

void ml_dma_play(MlPlayer *p)
{
    int i;
    for (i = 0; i < p->number_of_channels; i++) {
        MlVoice *v = &p->voices[i];
        MlChannel *channel = &p->ch[i];
        if (v->play & ML_PF_RETRIGGER) {
            uint8_t retrig = 0;
            uint8_t sample_number = v->wave_sample_number;
            uint8_t old_sample_number = v->wave_sample_number_old;
            v->wave_sample_number_old = sample_number;
            if ((v->wave_or_sample == ML_ST_SAMPLE) || (sample_number != old_sample_number))
                retrig = 1;
            /* Dma1 */
            v->play = (uint8_t)(v->play & ~ML_PF_RETRIGGER);
            channel->volume = (uint16_t)((((v->volume3 * v->channel_volume) / 1024) * p->playing_info.master_volume) / 16384);
            channel->period = v->period2;
            if (retrig) {
                if (v->wave_sample_pointer.has) {
                    uint32_t start_offset = v->wave_sample_pointer.start_offset;
                    uint32_t length = v->wave_sample_length * 2U;
                    if ((start_offset + length) >= v->wave_sample_pointer.data_len)
                        length = (uint32_t)(v->wave_sample_pointer.data_len - start_offset);
                    channel->data = v->wave_sample_pointer.data + start_offset;
                    channel->len = length;
                    channel->trigger = 1;
                    channel->sample_number = sample_number;
                }
            } else {
                channel->sample_number = sample_number;
            }
            /* Dma2 */
            if (v->wave_sample_repeat_length != 0) {
                uint32_t start_offset = v->wave_sample_repeat_pointer.start_offset;
                uint32_t length = v->wave_sample_repeat_length * 2U;
                if ((start_offset + length) >= v->wave_sample_repeat_pointer.data_len)
                    length = (uint32_t)(v->wave_sample_repeat_pointer.data_len - start_offset);
                channel->loop_data = v->wave_sample_repeat_pointer.data + start_offset;
                channel->loop_len = length;
                channel->loop_set = 1;
            }
        }
    }
}

void ml_play_tune(MlPlayer *p)
{
    int i;
    for (i = 0; i < p->number_of_channels; i++)
        ml_play_voice(p, &p->voices[i], p->cur->seq[i]);
}

void ml_play_voice(MlPlayer *p, MlVoice *v, const uint16_t *sequence)
{
    uint8_t spd;
    int loop_error;
    if (v->voice_off)
        return;
    v->speed_counter--;
    if (v->speed_counter > 0)
        return;
    v->part_position_work = v->part_position;
    spd = v->speed;
    v->part_groove = !v->part_groove;
    if (v->part_groove && (v->groove != 0))
        spd = v->groove;
    v->speed_counter = spd;
    v->speed_part = 0;
    v->groove_part = 0;
    if (p->playing_info.current_speed != spd)
        p->playing_info.current_speed = spd;
    loop_error = 256;
    while (--loop_error > 0) {
        uint8_t tune_position = v->tune_position;
        uint16_t sequence_data = sequence[tune_position];
        if ((sequence_data & 0x20) != 0) {
            if (ml_parse_control_command(p, v, sequence_data))
                break;
        } else {
            if (ml_parse_part(p, v, sequence_data))
                break;
        }
    }
}

uint8_t ml_parse_control_command(MlPlayer *p, MlVoice *v, uint16_t sequence_data)
{
    uint8_t sequence_argument = (uint8_t)(sequence_data & 0x1f);
    int command = (sequence_data & 0xc0) >> 6;
    switch (command) {
        case ML_SC_END:
            v->voice_off = 1;
            ml_on_end_reached(p, v->channel_number);
            return 1;
        case ML_SC_JUMP: {
            uint8_t target = (uint8_t)((sequence_data & 0xff00) >> 8);
            if (v->tune_jump_counter != 0) {
                /* TNE: TuneJumpPosition has been added to prevent Sonde Indure infinity looping */
                if ((v->tune_jump_position != -1) && (v->tune_position != v->tune_jump_position)) {
                    v->tune_position++;
                    return 0;
                }
                v->tune_jump_counter--;
                if (v->tune_jump_counter != 0)
                    v->tune_position = target;
                else
                    v->tune_position++;
                return 0;
            }
            if (v->tune_position > target) {
                v->tune_jump_position = v->tune_position;
                v->tune_jump_counter = sequence_argument;
                v->tune_position = target;
                if (sequence_argument == 0)
                    ml_on_end_reached(p, v->channel_number);
            } else {
                v->tune_position++;
                v->tune_jump_position = -1;
            }
            return 0;
        }
        case ML_SC_WAIT: {
            uint8_t wait_count;
            if (v->tune_wait != 0) {
                v->tune_wait--;
                if (v->tune_wait != 0)
                    return 1;
                v->tune_position++;
                return 0;
            }
            wait_count = (uint8_t)((sequence_data & 0xff00) >> 8);
            v->tune_wait = wait_count;
            if (wait_count == 0) {
                v->tune_position++;
                return 0;
            }
            v->pt_pitch_slide = 0;
            v->pitch_slide = 0;
            v->volume_slide = ML_PE_NONE;
            v->part_note = 0;
            v->part_instrument = 0;
            if (sequence_argument != 0) {
                v->speed = sequence_argument;
                v->speed_counter = sequence_argument;
            }
            return 1;
        }
        default:
            return ml_parse_part(p, v, sequence_data);
    }
}

uint8_t ml_parse_part(MlPlayer *p, MlVoice *v, uint16_t sequence_data)
{
    MlPart *part;
    v->part_number = (uint16_t)(((sequence_data << 2) & 0x300) | ((sequence_data & 0xff00) >> 8));
    v->transpose_number = (uint8_t)((sequence_data & 0x1f) - 16);
    part = p->song->part[v->part_number];
    for (;;) {
        uint8_t part_position = v->part_position;
        const MlPartLine *line;
        uint8_t jump_position;
        int i;
        v->part_position++;
        v->part_position &= 0x7f;
        if (v->part_position == 0) {
            v->tune_position++;
            v->speed = p->playing_info.tune_speed;
            v->groove = p->playing_info.tune_groove;
        }
        line = part ? &part->line[part_position] : &zero_line;
        v->part_note = line->note;
        v->part_instrument = line->instrument;
        for (i = 0; i < 5; i++)
            v->part_effects[i] = line->eff[i];
        if (v->part_note == 61) {
            if (part_position == 0) {
                ml_stop_play(p);
                ml_on_end_reached(p, v->channel_number);
                return 1;
            }
            v->part_position = 0;
            v->part_position_work = 0;
            v->speed = p->playing_info.tune_speed;
            v->groove = p->playing_info.tune_groove;
            v->speed_counter = v->speed;
            v->tune_position++;
            return 0;
        }
        jump_position = (uint8_t)(v->part_note & 0x7f);
        if ((v->part_note & 0x80) != 0) {
            if (v->part_jump_counter != 0) {
                v->part_jump_counter--;
                if (v->part_jump_counter != 0) {
                    v->part_position = jump_position;
                    v->part_position_work = jump_position;
                }
            } else {
                if (part_position > jump_position) {
                    v->part_jump_counter = v->part_instrument;
                    v->part_position = jump_position;
                    v->part_position_work = jump_position;
                    if (v->part_jump_counter == 0)
                        ml_on_end_reached(p, v->channel_number);
                }
            }
        } else {
            if (jump_position != 0)
                v->transpose = (int16_t)((int8_t)v->transpose_number * 32);
            ml_check_instrument(p, v);
            return 1;
        }
    }
}

void ml_check_instrument(MlPlayer *p, MlVoice *v)
{
    if (v->part_instrument != 0) {
        MlInst *inst = &p->song->inst[v->part_instrument];
        if (inst->valid) {
            v->instrument = inst;
            if (v->part_instrument != v->old_instrument) {
                v->arpeggio = ML_AF_NONE;
                v->instrument_pitch_slide = ML_PE_NONE;
                v->old_instrument = v->part_instrument;
            }
        }
    }
    ml_play_part_fx(p, v);
    ml_play_arpeggio(p, v);
    ml_play_instrument(p, v);
}

void ml_play_arpeggio(MlPlayer *p, MlVoice *v)
{
    MlInst *inst;
    uint16_t arpeggio_number;
    MlArp *arpeggio;
    if ((v->part_note == 0) || (v->instrument == NULL))
        return;
    inst = v->instrument;
    v->arpeggio_wait = 0;
    if (!(v->arpeggio & ML_AF_USE_TABLE) && !(v->arpeggio & ML_AF_ENABLED)) {
        if (!(inst->effect1 & ML_IE1_ARPEGGIO))
            return;
        v->arpeggio |= ML_AF_ENABLED;
    }
    if ((v->restart == ML_RF_NONE) && (v->part_instrument == 0))
        return;
    v->arpeggio |= ML_AF_WILL_SET_NOTE;
    arpeggio_number = inst->arpeggio_table;
    if (v->arpeggio & ML_AF_USE_TABLE)
        arpeggio_number = v->arpeggio_table;
    arpeggio = arpeggio_number < 256 ? p->song->arp[arpeggio_number] : NULL;
    if (arpeggio == NULL)
        return;
    v->arpeggio_position = 0;
    v->arpeggio_wait = 0;
    v->arpeggio_volume_slide = ML_AE_NONE;
    v->arpeggio_pitch_slide = ML_AE_NONE;
    v->arpeggio_pitch_slide_note = 0;
    v->arpeggio_speed_counter = inst->arpeggio_speed;
    for (;;) {
        uint8_t arpeggio_position = v->arpeggio_position++;
        const MlArpLine *arpeggio_line;
        int8_t transpose;
        uint8_t sample_number;
        int i;
        v->arpeggio_position &= 0x7f;
        arpeggio_line = &arpeggio->line[arpeggio_position];
        v->arpeggio_note = v->part_note;
        transpose = arpeggio_line->transpose;
        if (transpose == 0) {
            v->arpeggio_wait = 1;
            return;
        }
        if (transpose == 61) {
            v->effects1.effect = (uint8_t)(v->effects1.effect & ~ML_IE1_ARPEGGIO);
            return;
        }
        if (transpose == 62)
            continue;
        sample_number = arpeggio_line->sample;
        if (sample_number == 0)
            sample_number = inst->sample_number;
        v->wave_sample_number = sample_number;
        for (i = 0; i < 2; i++)
            ml_do_arpeggio_effect(p, v, arpeggio_line->eff[i]);
        if (transpose < 0)
            transpose = (int8_t)(transpose + (int8_t)(61 + v->arpeggio_note));
        else
            v->arpeggio |= ML_AF_NOTE_IS_ABSOLUTE;
        v->arpeggio_calculated_note = v->note = (uint16_t)(transpose * 32);
        ml_arpeggio_wait_start(p, v, arpeggio_line->sample);
        return;
    }
}

void ml_arpeggio_wait_start(MlPlayer *p, MlVoice *v, uint8_t sample_number)
{
    MlSample *sample;
    if (sample_number == 0)
        return;
    sample = &p->song->smp[sample_number];
    if (!sample->data)
        return;
    v->wave_sample = sample;
    v->arpeggio |= ML_AF_USE_WAVE_SAMPLE;
}

void ml_play_instrument(MlPlayer *p, MlVoice *v)
{
    MlInst *inst = v->instrument;
    SampBase inst_samp;
    uint8_t sample_type;
    MlPtr sample_pointer;
    uint8_t flag;
    int16_t volume;

    if (v->arpeggio_wait || (inst == NULL))
        return;
    inst_samp.sample_type = inst->sample_type;
    inst_samp.sample_pointer = inst->sample_pointer;
    inst_samp.sample_repeat_pointer = inst->sample_repeat_pointer;
    inst_samp.sample_length = inst->sample_length;
    inst_samp.sample_repeat_length = inst->sample_repeat_length;
    inst_samp.fine_tune = inst->fine_tune;
    inst_samp.semi_tone = inst->semi_tone;
    sample_type = inst->sample_type;
    if ((v->restart & (ML_RF_RESTART_FROM_PART_FX | ML_RF_RESTART_FROM_ARP)) != 0)
        v->wave_sample_number = inst->sample_number;
    else {
        if (v->part_instrument == 0)
            goto play_note;
        if ((v->pitch_slide == ML_PE_PORTAMENTO) || (v->part_note == 0)) {
            v->volume1 = (uint16_t)(inst->volume * 16);
            goto play_note;
        }
        if (v->arpeggio & ML_AF_USE_WAVE_SAMPLE) {
            MlSample *ws = v->wave_sample;
            if (ws == NULL)
                return;
            inst_samp.sample_type = ws->sample_type;
            inst_samp.sample_pointer = ws->sample_pointer;
            inst_samp.sample_repeat_pointer = ws->sample_repeat_pointer;
            inst_samp.sample_length = ws->sample_length;
            inst_samp.sample_repeat_length = ws->sample_repeat_length;
            inst_samp.fine_tune = ws->fine_tune;
            inst_samp.semi_tone = ws->semi_tone;
            if (inst_samp.sample_type == ML_ST_SAMPLE)
                sample_type = inst_samp.sample_type;
        } else
            v->wave_sample_number = inst->sample_number;
    }
    if (!inst_samp.sample_pointer.has)
        return;
    sample_pointer = inst_samp.sample_pointer;
    if (!inst->transposable)
        v->transpose = 0;
    flag = inst->env_tra_pha_fil_bits;
    if (v->effects1.effect & ML_IE1_HOLD_SUSTAIN) {
        flag = (uint8_t)(flag & ~ML_IF_ENVELOPE_HOLD_SUSTAIN);
        v->effects1.argument = (uint8_t)(v->effects1.argument & ML_IF_ENVELOPE_HOLD_SUSTAIN);
        flag = (uint8_t)(flag | v->effects1.argument);
    }
    v->effects1.argument = flag;
    v->effects2.argument = inst->mix_res_loo_bits;
    v->effects1.effect = inst->effect1;
    v->effects2.effect = inst->effect2;
    v->wave_or_sample = sample_type;
    if (sample_type != ML_ST_SAMPLE) {
        /* Wave */
        ml_fix_wave_length(p, v, inst, sample_type, sample_pointer);
    } else {
        /* Normal sample */
        uint16_t sample_length = inst_samp.sample_length;
        MlPtr repeat_pointer;
        uint16_t repeat_length;
        if (v->sample_offset_active) {
            uint16_t offset = (uint16_t)(v->sample_offset << 7);
            if (offset < sample_length) {
                sample_length = (uint16_t)(sample_length - offset);
                sample_pointer.start_offset += offset * 2U;
            } else
                sample_length = 1;
        }
        v->wave_sample_pointer = sample_pointer;
        v->wave_sample_length = sample_length;
        repeat_pointer = inst_samp.sample_repeat_pointer;
        repeat_length = inst_samp.sample_repeat_length;
        if (!(v->arpeggio & ML_AF_USE_WAVE_SAMPLE) && !(inst->effect1 & ML_IE1_WAVE_SAMPLE_LOOP)) {
            /* No loop */
            repeat_pointer = (MlPtr){0};
            repeat_length = 0;
        }
        v->wave_sample_repeat_pointer = repeat_pointer;
        v->wave_sample_repeat_pointer_original = repeat_pointer;
        v->wave_sample_repeat_length = repeat_length;
    }
    v->restart = (uint8_t)(v->restart & (ML_RF_RESTART_FROM_PART_FX | ML_RF_RESTART_FROM_ARP | ML_RF_ARP_HAS_SET_VOLUME));
    if (v->restart == ML_RF_NONE)
        v->volume1 = (uint16_t)(inst->volume * 16);
play_note:
    volume = (int16_t)(v->volume != ML_PE_NONE ? v->volume_set : v->volume1);
    if (v->volume_add != ML_PE_NONE) {
        volume = (int16_t)(volume + v->volume_add_number);
        if (volume < 0)
            volume = 0;
        else if (volume > 64 * 16)
            volume = 64 * 16;
    }
    v->volume1 = v->volume2 = v->volume3 = (uint16_t)volume;
    if (v->pitch_slide == ML_PE_PORTAMENTO)
        return;
    if ((inst->slide_speed != 0) && (v->part_note != 0)) {
        uint16_t new_note;
        v->pitch_slide_speed = inst->slide_speed;
        new_note = (uint16_t)(v->note + v->pitch_slide_note);
        v->pitch_slide_to_note = (int16_t)(v->part_note << 5);
        v->pitch_slide_type = v->pitch_slide_to_note < new_note ? ML_DIR_DOWNWARD : ML_DIR_UPWARD;
        if (v->instrument_pitch_slide != ML_PE_NONE) {
            ml_inst_play(p, v, inst);
            return;
        }
        v->instrument_pitch_slide = ML_PE_PORTAMENTO;
    }
    {
        /* TestAndClearArpFlag(WillSetNote) */
        uint8_t was_set = (v->arpeggio & ML_AF_WILL_SET_NOTE) != 0;
        v->arpeggio = (uint8_t)(v->arpeggio & ~ML_AF_WILL_SET_NOTE);
        if (!was_set) {
            if (v->part_note == 0)
                return;
            v->note = (uint16_t)(v->part_note << 5);
            v->arpeggio_volume_slide = ML_AE_NONE;
            v->arpeggio_pitch_slide = ML_AE_NONE;
            v->arpeggio_pitch_slide_note = 0;
        }
    }
    v->semi_tone = (int16_t)(inst_samp.semi_tone * 32);
    if (!(v->play & ML_PF_KEEP_EFFECT_FINE_TUNE))
        v->fine_tune = inst_samp.fine_tune;
    v->pt_pitch_slide_note = 0;
    v->pitch_slide_note = 0;
    v->pt_vibrato_note = 0;
    v->pt_tremolo_position = 0;
    v->pt_vibrato_position = 0;
    v->vibrato_note = 0;
    v->pt_pitch_add = 0;
    v->pitch_add = 0;
    if ((v->part_instrument != 0) || ((v->restart & (ML_RF_RESTART_FROM_PART_FX | ML_RF_RESTART_FROM_ARP)) != 0))
        ml_inst_play(p, v, inst);
}

void ml_fix_wave_length(MlPlayer *p, MlVoice *v, MlInst *inst, uint8_t sample_type, MlPtr sample_pointer)
{
    uint16_t sample_length = inst->sample_length;
    (void)p;
    switch (sample_type) {
        case ML_ST_WAVE16:
            sample_pointer.start_offset += 256 + 128 + 64 + 32;
            sample_length >>= 4;
            break;
        case ML_ST_WAVE32:
            sample_pointer.start_offset += 256 + 128 + 64;
            sample_length >>= 3;
            break;
        case ML_ST_WAVE64:
            sample_pointer.start_offset += 256 + 128;
            sample_length >>= 2;
            break;
        case ML_ST_WAVE128:
            sample_pointer.start_offset += 256;
            sample_length >>= 1;
            break;
    }
    v->wave_sample_pointer = sample_pointer;
    v->wave_sample_repeat_pointer = sample_pointer;
    v->wave_sample_repeat_pointer_original = sample_pointer;
    v->wave_sample_length = sample_length;
    v->wave_sample_repeat_length = sample_length;
}

void ml_inst_play(MlPlayer *p, MlVoice *v, MlInst *inst)
{
    uint16_t repeat_length;
    if ((v->pitch_slide != ML_PE_PORTAMENTO) && !v->arpeggio_wait) {
        v->play |= ML_PF_RETRIGGER;
        if ((v->vibrato == ML_PE_NONE) && (v->effects1.effect & ML_IE1_VIBRATO)) {
            v->vibrato_counter = 0;
            v->vibrato_command_depth = 0;
            v->vibrato_command_speed = inst->vibrato_speed;
            v->vibrato_command_delay = inst->vibrato_delay;
            v->vibrato_attack_speed = inst->vibrato_attack_speed;
            v->vibrato_attack_length = inst->vibrato_attack;
            v->vibrato_depth = inst->vibrato_depth;
            v->vibrato_wave_type = inst->vibrato_wave_type;
            v->vibrato_direction = inst->vibrato_direction;
        } else if (!(v->effects1.effect & ML_IE1_VIBRATO)) {
            /* TNE: This fix has been added, so Fletch Dance won't make some funny sounds */
            v->vibrato_command_speed = 0;
        }
        if ((v->tremolo == ML_PE_NONE) && (v->effects1.effect & ML_IE1_TREMOLO)) {
            v->tremolo_counter = 0;
            v->tremolo_command_depth = 0;
            v->tremolo_command_speed = inst->tremolo_speed;
            v->tremolo_command_delay = inst->tremolo_delay;
            v->tremolo_attack_speed = inst->tremolo_attack_speed;
            v->tremolo_attack_length = inst->tremolo_attack;
            v->tremolo_depth = inst->tremolo_depth;
            v->tremolo_wave_type = inst->tremolo_wave_type;
            v->tremolo_direction = inst->tremolo_direction;
        }
        if (v->effects1.effect & ML_IE1_ENVELOPE) {
            v->envelope_volume = 0;
            v->envelope_data[0] = inst->envelope_attack_length;
            v->envelope_data[1] = inst->envelope_decay_length;
            v->envelope_data[2] = inst->envelope_sustain_length;
            v->envelope_data[3] = inst->envelope_release_length;
            v->envelope_data[4] = inst->envelope_attack_speed;
            v->envelope_data[5] = inst->envelope_decay_speed;
            v->envelope_data[6] = inst->envelope_sustain_speed;
            v->envelope_data[7] = inst->envelope_release_speed;
            v->envelope_data[8] = inst->envelope_attack_volume;
            v->envelope_data[9] = inst->envelope_decay_volume;
            v->envelope_data[10] = inst->envelope_sustain_volume;
            v->envelope_data[11] = inst->envelope_release_volume;
        }
        if (v->effects2.effect & ML_IE2_PHASE) {
            MlCounter *counter = &v->phase_counter;
            uint8_t init = 0, check = 0;
            v->phase_type = inst->phase_type;
            counter->step = (v->effects1.argument & ML_IF_PHASE_STEP) != 0;
            if (counter->step) {
                v->phase_speed = inst->phase_turns;
                counter->turns = 0;
                check = 1;
            } else {
                counter->turns = (int16_t)inst->phase_turns;
                if (!(v->effects1.argument & ML_IF_PHASE_INIT)) {
                    v->phase_init = 0;
                    init = 1;
                } else
                    check = 1;
            }
            if (check) {
                uint8_t old_init = v->phase_init;
                v->phase_init = v->part_instrument;
                if (old_init == v->part_instrument)
                    counter->delay = inst->phase_delay;
                else
                    init = 1;
            }
            if (init) {
                counter->counter = inst->phase_start;
                counter->speed = (int16_t)inst->phase_speed;
                counter->repeat = inst->phase_repeat;
                counter->repeat_end = inst->phase_repeat_end;
                if (inst->phase_start > inst->phase_repeat)
                    counter->speed = (int16_t)-counter->speed;
                counter->delay = inst->phase_delay;
            }
        }
        v->mix_res_fil_boost = inst->mix_res_fil_boost;
        if (v->effects2.effect & ML_IE2_RESONANCE) {
            MlCounter *counter = &v->resonance_counter;
            uint8_t init = 0, check = 0;
            v->resonance_amp = inst->responance_amp;
            counter->step = (v->effects2.argument & ML_MF_RESONANCE_STEP) != 0;
            if (counter->step) {
                v->resonance_speed = inst->resonance_turns;
                counter->turns = 0;
                check = 1;
            } else {
                counter->turns = (int16_t)inst->resonance_turns;
                if (!(v->effects2.argument & ML_MF_RESONANCE_INIT)) {
                    v->resonance_init = 0;
                    init = 1;
                } else
                    check = 1;
            }
            if (check) {
                uint8_t old_init = v->resonance_init;
                v->resonance_init = v->part_instrument;
                if (old_init == v->part_instrument)
                    counter->delay = inst->resonance_delay;
                else
                    init = 1;
            }
            if (init) {
                v->resonance_last_init = 1;
                counter->counter = inst->resonance_start;
                counter->speed = (int16_t)inst->resonance_speed;
                counter->repeat = inst->resonance_repeat;
                counter->repeat_end = inst->resonance_repeat_end;
                if (inst->resonance_start > inst->resonance_repeat)
                    counter->speed = (int16_t)-counter->speed;
                counter->delay = inst->resonance_delay;
            }
        }
        if (v->effects2.effect & ML_IE2_FILTER) {
            MlCounter *counter = &v->filter_counter;
            uint8_t init = 0, check = 0;
            v->filter_type = inst->filter_type;
            counter->step = (v->effects1.argument & ML_IF_FILTER_STEP) != 0;
            if (counter->step) {
                v->filter_speed = inst->filter_turns;
                counter->turns = 0;
                check = 1;
            } else {
                counter->turns = (int16_t)inst->filter_turns;
                if (!(v->effects1.argument & ML_IF_FILTER_INIT)) {
                    v->filter_init = 0;
                    init = 1;
                } else
                    check = 1;
            }
            if (check) {
                uint8_t old_init = v->filter_init;
                v->filter_init = v->part_instrument;
                if (old_init == v->part_instrument)
                    counter->delay = inst->filter_delay;
                else
                    init = 1;
            }
            if (init) {
                v->filter_last_init = 1;
                counter->counter = inst->filter_start;
                counter->speed = (int16_t)inst->filter_speed;
                counter->repeat = inst->filter_repeat;
                counter->repeat_end = inst->filter_repeat_end;
                if (inst->filter_start > inst->filter_repeat)
                    counter->speed = (int16_t)-counter->speed;
                counter->delay = inst->filter_delay;
            }
        }
        if (v->effects2.effect & ML_IE2_MIX) {
            MlCounter *counter = &v->mix_counter;
            uint8_t init = 0, check = 0;
            v->mix_wave_number = inst->mix_wave_number;
            counter->step = (v->effects2.argument & ML_MF_MIX_STEP) != 0;
            if (counter->step) {
                v->mix_speed = inst->mix_turns;
                counter->turns = 0;
                check = 1;
            } else {
                counter->turns = (int16_t)inst->mix_turns;
                if (!(v->effects2.argument & ML_MF_MIX_INIT)) {
                    v->mix_init = 0;
                    init = 1;
                } else
                    check = 1;
            }
            if (check) {
                uint8_t old_init = v->mix_init;
                v->mix_init = v->part_instrument;
                if (old_init == v->part_instrument)
                    counter->delay = inst->mix_delay;
                else
                    init = 1;
            }
            if (init) {
                counter->counter = inst->mix_start;
                counter->speed = (int16_t)inst->mix_speed;
                counter->repeat = inst->mix_repeat;
                counter->repeat_end = inst->mix_repeat_end;
                if (inst->mix_start > inst->mix_repeat)
                    counter->speed = (int16_t)-counter->speed;
                counter->delay = inst->mix_delay;
            }
        }
        if (v->effects2.effect & ML_IE2_TRANSFORM) {
            MlCounter *counter = &v->transform_counter;
            uint8_t init = 0, check = 0;
            v->transform_wave_sample_numbers[0] = inst->sample_number;
            v->transform_wave_sample_numbers[1] = inst->transform_wave_numbers[0];
            v->transform_wave_sample_numbers[2] = inst->transform_wave_numbers[1];
            v->transform_wave_sample_numbers[3] = inst->transform_wave_numbers[2];
            v->transform_wave_sample_numbers[4] = inst->transform_wave_numbers[3];
            v->transform_wave_sample_numbers[5] = inst->transform_wave_numbers[4];
            counter->step = (v->effects1.argument & ML_IF_TRANSFORM_STEP) != 0;
            if (counter->step) {
                v->transform_speed = inst->transform_turns;
                counter->turns = 0;
                check = 1;
            } else {
                counter->turns = (int16_t)inst->transform_turns;
                if (!(v->effects1.argument & ML_IF_TRANSFORM_INIT)) {
                    v->transform_init = 0;
                    init = 1;
                } else
                    check = 1;
            }
            if (check) {
                uint8_t old_init = v->transform_init;
                v->transform_init = v->part_instrument;
                if (old_init == v->part_instrument)
                    counter->delay = inst->transform_delay;
                else
                    init = 1;
            }
            if (init) {
                counter->counter = inst->transform_start;
                counter->speed = (int16_t)inst->transform_speed;
                counter->repeat = inst->transform_repeat;
                counter->repeat_end = inst->transform_repeat_end;
                if (inst->transform_start > inst->transform_repeat)
                    counter->speed = (int16_t)-counter->speed;
                counter->delay = inst->transform_delay;
            }
        }
        if (v->effects1.effect & ML_IE1_LOOP) {
            if ((inst->sample_type != ML_ST_SAMPLE) || (inst->loop_length == 0))
                v->effects1.effect = (uint8_t)(v->effects1.effect & ~ML_IE1_LOOP);
            else {
                uint8_t init = 0, part_init = 0, check = 0;
                if (v->effects2.argument & ML_MF_LOOP_STEP) {
                    v->loop_speed = (int16_t)inst->loop_turns;
                    v->loop_turns = 0;
                    check = 1;
                } else {
                    v->loop_turns = (int16_t)inst->loop_turns;
                    if (!(v->effects2.argument & ML_MF_LOOP_INIT)) {
                        v->loop_init = 0;
                        init = 1;
                    } else
                        check = 1;
                }
                if (check) {
                    uint8_t old_init = v->loop_init;
                    v->loop_init = v->part_instrument;
                    if (old_init == v->part_instrument) {
                        v->play = (uint8_t)(v->play & ~ML_PF_RETRIGGER);
                        part_init = 1;
                    } else
                        init = 1;
                }
                if (init) {
                    MlSample *sample = &p->song->smp[inst->sample_number];
                    v->loop_wave_sample = sample->sample_pointer;
                    v->loop_counter = inst->loop_start;
                    v->loop_counter_save = inst->loop_start;
                    v->loop_wave_sample_counter_max = (uint16_t)(sample->sample_length - inst->loop_length);
                    v->loop_length = inst->loop_length;
                    v->loop_repeat_end = inst->loop_repeat_end;
                    v->loop_wait = inst->loop_wait;
                    v->loop_step = inst->loop_loop_step;
                    v->loop_repeat = inst->loop_repeat;
                    if (inst->loop_start > inst->loop_repeat)
                        v->loop_step = -v->loop_step;
                }
                if (init || part_init) {
                    MlPtr np;
                    v->loop_delay = (int16_t)inst->loop_delay;
                    v->loop_wait_counter = 0;
                    np.data = v->loop_wave_sample.data;
                    np.data_len = v->loop_wave_sample.data_len;
                    np.start_offset = v->loop_wave_sample.start_offset + (v->loop_counter_save * 2U);
                    np.has = 1;
                    v->wave_sample_pointer = np;
                    v->wave_sample_repeat_pointer = v->wave_sample_pointer;
                    v->wave_sample_repeat_pointer_original = v->wave_sample_pointer;
                    v->wave_sample_length = v->loop_length;
                    v->wave_sample_repeat_length = v->loop_length;
                }
            }
        }
    }
    repeat_length = v->wave_sample_repeat_length;
    repeat_length = (uint16_t)(repeat_length - 8);
    if (repeat_length != 0) {
        repeat_length = (uint16_t)(repeat_length - 8);
        if (repeat_length != 0) {
            repeat_length = (uint16_t)(repeat_length - 16);
            if (repeat_length != 0) {
                repeat_length = (uint16_t)(repeat_length - 32);
                if (repeat_length != 0) {
                    repeat_length = (uint16_t)(repeat_length - 64);
                    if (repeat_length != 0)
                        v->effects2.effect = ML_IE2_NONE;
                }
            }
        }
    }
}

void ml_period_calculation(MlPlayer *p)
{
    int i;
    for (i = 0; i < p->number_of_channels; i++)
        ml_period(p, &p->voices[i]);
}

void ml_period(MlPlayer *p, MlVoice *v)
{
    int16_t note;
    uint16_t period;
    (void)p;
    note = (int16_t)(v->note + v->vibrato_note + v->pitch_slide_note + v->arpeggio_pitch_slide_note + v->semi_tone + v->fine_tune + v->pitch_add);
    if (!(v->arpeggio & ML_AF_NOTE_IS_ABSOLUTE) && (v->transpose != 0))
        note = (int16_t)(note + v->transpose);
    if (note < -32)
        note = -32;
    else if (note > 5 * 12 * 32)
        note = 5 * 12 * 32;
    period = ml_pal_pitch_table[note + 32];
    period = (uint16_t)(period + (uint16_t)(v->pt_pitch_slide_note + v->pt_vibrato_note + v->pt_pitch_add));
    if (period < 106)
        period = 106;
    else if (period > 3591)
        period = 3591;
    v->period1 = period;
    v->period2 = period;
}

void ml_period_volume_play(MlPlayer *p)
{
    int i;
    for (i = 0; i < p->number_of_channels; i++) {
        MlVoice *v = &p->voices[i];
        MlChannel *channel = &p->ch[i];
        if (!(v->play & ML_PF_RETRIGGER)) {
            channel->period = v->period2;
            channel->volume = (uint16_t)((((v->volume3 * v->channel_volume) / 1024) * p->playing_info.master_volume) / 16384);
        }
    }
}
