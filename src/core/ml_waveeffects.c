/* Musicline Editor replayer: wave form effects. Port of ml_Player.WaveFormEffects.cs (NostalgicPlayer, MIT, Polycode). */
#include <stdint.h>
#include "ml_player.h"
#include "ml_waveeffects.h"

void ml_channel_set_sample(MlChannel *ch, const int8_t *data, uint32_t offset, uint32_t length);

#define LD16(x) ((int16_t)(x))
#define U16(x) ((uint16_t)(x))

static MlPtr new_ptr(int8_t *arr) { MlPtr r; r.data = arr; r.data_len = 256; r.start_offset = 0; r.has = 1; return r; }

void ml_move_loop(MlPlayer *p, MlVoice *v, MlChannel *ch)
{
    MlPtr sample;
    uint16_t previous_counter;
    if (v->effects1.effect & ML_IE1_LOOP) {
        if (!(v->effects2.argument & ML_MF_LOOP_STEP)) {
            v->loop_delay = LD16(v->loop_delay - 1);
            if (v->loop_delay >= 0) {
                sample = v->loop_wave_sample;
                sample.start_offset += v->loop_counter_save * 2U;
                v->wave_sample_repeat_pointer = sample;
                v->wave_sample_repeat_pointer_original = sample;
                return;
            }
            v->loop_delay = 0;
            if (v->loop_wait != 0) {
                v->loop_wait_counter = LD16(v->loop_wait_counter - 1);
                if (v->loop_wait_counter >= 0)
                    return;
                v->loop_wait_counter = (int16_t)v->loop_wait;
            }
            v->loop_counter_save = v->loop_counter;
            ml_loop_counter(p, v);
        } else {
            if (!(v->effects2.argument & ML_MF_LOOP_INIT)) {
                if (v->part_note != 0) {
                    v->loop_counter_save = v->loop_counter;
                    ml_loop_counter(p, v);
                }
            } else {
                if (v->part_note != 0)
                    v->loop_counter_save = v->loop_counter;
                ml_loop_counter(p, v);
            }
        }
        previous_counter = v->loop_counter_save;
        if (v->effects2.argument & ML_MF_LOOP_STEP) {
            v->loop_delay = LD16(v->loop_delay - 1);
            if (v->loop_delay < 0) {
                v->loop_delay = 0;
                if (v->loop_wait != 0) {
                    v->loop_wait_counter = LD16(v->loop_wait_counter - 1);
                    if (v->loop_wait_counter >= 0)
                        goto no_step;
                    v->loop_wait_counter = (int16_t)v->loop_wait;
                }
                {
                    int step = v->loop_step;
                    int counter;
                    int16_t speed;
                    if (step < 0)
                        step = -step;
                    counter = v->loop_counter_save;
                    speed = (int8_t)v->loop_speed;
                    if (speed < 0) {
                        counter += speed * step;
                        if (counter < 0)
                            v->loop_counter_save = 0;
                        else
                            v->loop_counter_save = (uint16_t)counter;
                    } else {
                        counter += speed * step;
                        if (counter > v->loop_wave_sample_counter_max)
                            v->loop_counter_save = v->loop_wave_sample_counter_max;
                        else
                            v->loop_counter_save = (uint16_t)counter;
                    }
                }
            }
        }
no_step:
        sample = v->loop_wave_sample;
        sample.start_offset += previous_counter * 2U;
        v->wave_sample_repeat_pointer = sample;
        v->wave_sample_repeat_pointer_original = sample;
        if ((v->loop_turns >= 0) || !(v->effects1.effect & ML_IE1_LOOP_STOP))
            return;
        v->effects1.effect = (uint8_t)(v->effects1.effect & ~ML_IE1_LOOP);
        ml_channel_set_sample(ch, ml_zero_sample, 0, 2);
    }
}

void ml_loop_counter(MlPlayer *p, MlVoice *v)
{
    int counter;
    (void)p;
    if (v->loop_turns < 0)
        return;
    counter = v->loop_counter;
    if (v->loop_repeat < v->loop_repeat_end) {
        if (v->loop_step < 0) {
            counter += v->loop_step;
            if (counter >= v->loop_repeat) {
                v->loop_counter = (uint16_t)counter;
                return;
            }
        } else {
            counter += v->loop_step;
            if (counter <= v->loop_repeat_end) {
                v->loop_counter = (uint16_t)counter;
                return;
            }
        }
    } else {
        if (v->loop_step < 0) {
            counter += v->loop_step;
            if (counter >= v->loop_repeat_end) {
                v->loop_counter = (uint16_t)counter;
                return;
            }
        } else {
            counter += v->loop_step;
            if (counter <= v->loop_repeat) {
                v->loop_counter = (uint16_t)counter;
                return;
            }
        }
    }
    if (v->loop_turns != 0) {
        v->loop_turns = LD16(v->loop_turns - 1);
        if (v->loop_turns == 0)
            v->loop_turns = -1;
    }
    counter -= v->loop_step;
    v->loop_step = -v->loop_step;
    v->loop_counter = (uint16_t)counter;
}

void ml_phase_play(MlPlayer *p, MlVoice *v)
{
    if (v->effects2.effect & ML_IE2_PHASE) {
        MlCounter *counter_info = &v->phase_counter;
        uint16_t previous_counter;
        uint32_t repeat_length;
        const uint8_t *sizer_table;
        const uint16_t *sizer_offset_table;
        MlPtr repeat_buffer, phase_buffer;
        uint32_t todo, left_todo;
        uint16_t sizer_offset;

        if (v->effects1.argument & ML_IF_PHASE_STEP) {
            if (!(v->effects1.argument & ML_IF_PHASE_INIT)) {
                if (v->part_note == 0)
                    goto no_count;
                counter_info->save_counter = counter_info->counter;
            } else {
                if (v->part_note != 0)
                    counter_info->save_counter = counter_info->counter;
            }
        } else
            counter_info->save_counter = counter_info->counter;
        ml_counter(p, counter_info);
no_count:
        previous_counter = counter_info->save_counter;
        if (v->effects1.argument & ML_IF_PHASE_STEP) {
            if (counter_info->delay != 0)
                counter_info->delay--;
            else {
                int16_t speed = (int8_t)v->phase_speed;
                if (speed < 0) {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter - speed);
                    if ((int16_t)counter_info->save_counter > 512)
                        counter_info->save_counter = 512;
                } else {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter - speed);
                    if ((int16_t)counter_info->save_counter < 2)
                        counter_info->save_counter = 2;
                }
            }
        }
        repeat_length = v->wave_sample_repeat_length * 2U;
        if (repeat_length > 128) {
            previous_counter = U16(previous_counter + 1);
            previous_counter >>= 1;
            sizer_table = ml_sizer_table256;
            sizer_offset_table = ml_sizer_offset256;
        } else if (repeat_length > 64) {
            previous_counter = U16(previous_counter + 3);
            previous_counter >>= 2;
            sizer_table = ml_sizer_table128;
            sizer_offset_table = ml_sizer_offset128;
        } else if (repeat_length > 32) {
            previous_counter = U16(previous_counter + 7);
            previous_counter >>= 3;
            sizer_table = ml_sizer_table64;
            sizer_offset_table = ml_sizer_offset64;
        } else if (repeat_length > 16) {
            previous_counter = U16(previous_counter + 15);
            previous_counter >>= 4;
            sizer_table = ml_sizer_table32;
            sizer_offset_table = ml_sizer_offset32;
        } else {
            previous_counter = U16(previous_counter + 31);
            previous_counter >>= 5;
            sizer_table = ml_sizer_table16;
            sizer_offset_table = ml_sizer_offset16;
        }
        repeat_buffer = v->wave_sample_repeat_pointer;
        phase_buffer = new_ptr(v->phase_wave_buffer);
        v->wave_sample_repeat_pointer = phase_buffer;
        if ((v->effects1.effect & ML_IE1_LOOP) || (v->wave_or_sample != ML_ST_SAMPLE))
            v->wave_sample_pointer = phase_buffer;
        if ((previous_counter >= repeat_length) || (previous_counter == 0)) {
            ml_phase_mova(p, repeat_buffer, phase_buffer, repeat_length);
            return;
        }
        todo = previous_counter;
        left_todo = repeat_length - previous_counter;
        sizer_offset = sizer_offset_table[previous_counter - 1];
        switch (v->phase_type) {
            case ML_PH_LOW:
                ml_phase_low(p, v, sizer_table, sizer_offset, todo, left_todo, previous_counter, repeat_buffer, phase_buffer);
                break;
            case ML_PH_HIGH:
                ml_phase_high(p, v, sizer_table, sizer_offset, todo, left_todo, previous_counter, repeat_buffer, phase_buffer);
                break;
            case ML_PH_MED:
                ml_phase_med(p, v, sizer_table, sizer_offset, todo, left_todo, previous_counter, repeat_buffer, phase_buffer);
                break;
            default:
                ml_phase_quick(p, v, sizer_table, sizer_offset, todo, left_todo, repeat_buffer, phase_buffer);
                break;
        }
    }
}

void ml_phase_quick(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, MlPtr repeat_buffer, MlPtr phase_buffer)
{
    MlPtr phase_copy = phase_buffer;
    uint8_t index = 0;
    uint32_t i;
    (void)p;
    for (i = 0; i < todo; i++) {
        index = sizer_table[sizer_offset++];
        phase_buffer.data[phase_buffer.start_offset++] = repeat_buffer.data[repeat_buffer.start_offset + index];
    }
    if (v->effects1.argument & ML_IF_PHASE_FILL) {
        for (i = 0; i < left_todo; i++)
            phase_buffer.data[phase_buffer.start_offset++] = phase_copy.data[phase_copy.start_offset++];
    } else {
        if (left_todo > 0) {
            int8_t sample = repeat_buffer.data[repeat_buffer.start_offset + index];
            for (i = 0; i < left_todo; i++)
                phase_buffer.data[phase_buffer.start_offset++] = sample;
        }
    }
}

void ml_phase_high(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, uint16_t previous_counter, MlPtr repeat_buffer, MlPtr phase_buffer)
{
    uint16_t sizer_offset_copy = sizer_offset;
    MlPtr repeat_copy = repeat_buffer;
    uint8_t index = 0;
    uint32_t i;
    (void)p;
    for (i = 0; i < todo; i++) {
        int16_t sample1, sample2;
        int8_t output;
        index = sizer_table[sizer_offset++];
        sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
        sample2 = repeat_copy.data[repeat_copy.start_offset++];
        output = (int8_t)(((sample1 + sample2) + (sample1 * 2)) >> 2);
        phase_buffer.data[phase_buffer.start_offset++] = output;
    }
    if (v->effects1.argument & ML_IF_PHASE_FILL) {
        if (left_todo > 0) {
            sizer_offset = sizer_offset_copy;
            do {
                uint32_t counter = previous_counter < left_todo ? previous_counter : left_todo;
                for (i = 0; i < counter; i++) {
                    int16_t sample1, sample2;
                    int8_t output;
                    index = sizer_table[sizer_offset++];
                    sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
                    sample2 = repeat_copy.data[repeat_copy.start_offset++];
                    output = (int8_t)(((sample1 + sample2) + (sample1 * 2)) >> 2);
                    phase_buffer.data[phase_buffer.start_offset++] = output;
                }
                left_todo -= counter;
            } while (left_todo > 0);
        }
    } else {
        if (left_todo > 0) {
            int16_t sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
            sample1 = (int16_t)(sample1 + (int16_t)(sample1 * 2));
            for (i = 0; i < left_todo; i++) {
                int16_t sample2 = repeat_copy.data[repeat_copy.start_offset++];
                int8_t output = (int8_t)((sample1 + sample2) >> 2);
                phase_buffer.data[phase_buffer.start_offset++] = output;
            }
        }
    }
}

void ml_phase_med(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, uint16_t previous_counter, MlPtr repeat_buffer, MlPtr phase_buffer)
{
    uint16_t sizer_offset_copy = sizer_offset;
    MlPtr repeat_copy = repeat_buffer;
    uint8_t index = 0;
    uint32_t i;
    (void)p;
    for (i = 0; i < todo; i++) {
        int16_t sample1, sample2;
        int8_t output;
        index = sizer_table[sizer_offset++];
        sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
        sample2 = repeat_copy.data[repeat_copy.start_offset++];
        output = (int8_t)((sample1 + sample2) >> 1);
        phase_buffer.data[phase_buffer.start_offset++] = output;
    }
    if (v->effects1.argument & ML_IF_PHASE_FILL) {
        if (left_todo > 0) {
            do {
                uint32_t counter = previous_counter < left_todo ? previous_counter : left_todo;
                sizer_offset = sizer_offset_copy;
                for (i = 0; i < counter; i++) {
                    int16_t sample1, sample2;
                    int8_t output;
                    index = sizer_table[sizer_offset++];
                    sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
                    sample2 = repeat_copy.data[repeat_copy.start_offset++];
                    output = (int8_t)((sample1 + sample2) >> 1);
                    phase_buffer.data[phase_buffer.start_offset++] = output;
                }
                left_todo -= counter;
            } while (left_todo > 0);
        }
    } else {
        if (left_todo > 0) {
            int16_t sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
            for (i = 0; i < left_todo; i++) {
                int16_t sample2 = repeat_copy.data[repeat_copy.start_offset++];
                int8_t output = (int8_t)((sample1 + sample2) >> 1);
                phase_buffer.data[phase_buffer.start_offset++] = output;
            }
        }
    }
}

void ml_phase_low(MlPlayer *p, MlVoice *v, const uint8_t *sizer_table, uint16_t sizer_offset, uint32_t todo, uint32_t left_todo, uint16_t previous_counter, MlPtr repeat_buffer, MlPtr phase_buffer)
{
    uint16_t sizer_offset_copy = sizer_offset;
    MlPtr repeat_copy = repeat_buffer;
    uint8_t index = 0;
    uint32_t i;
    (void)p;
    for (i = 0; i < todo; i++) {
        int16_t sample1, sample2;
        int8_t output;
        index = sizer_table[sizer_offset++];
        sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
        sample2 = repeat_copy.data[repeat_copy.start_offset++];
        output = (int8_t)(((sample1 + sample2) + (sample2 * 2)) >> 2);
        phase_buffer.data[phase_buffer.start_offset++] = output;
    }
    if (v->effects1.argument & ML_IF_PHASE_FILL) {
        if (left_todo > 0) {
            sizer_offset = sizer_offset_copy;
            do {
                uint32_t counter = previous_counter < left_todo ? previous_counter : left_todo;
                for (i = 0; i < counter; i++) {
                    int16_t sample1, sample2;
                    int8_t output;
                    index = sizer_table[sizer_offset++];
                    sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
                    sample2 = repeat_copy.data[repeat_copy.start_offset++];
                    output = (int8_t)(((sample1 + sample2) + (sample2 * 2)) >> 2);
                    phase_buffer.data[phase_buffer.start_offset++] = output;
                }
                left_todo -= counter;
            } while (left_todo > 0);
        }
    } else {
        if (left_todo > 0) {
            int16_t sample1 = repeat_buffer.data[repeat_buffer.start_offset + index];
            for (i = 0; i < left_todo; i++) {
                int16_t sample2 = repeat_copy.data[repeat_copy.start_offset++];
                int8_t output = (int8_t)(((sample1 + sample2) + (sample2 * 2)) >> 2);
                phase_buffer.data[phase_buffer.start_offset++] = output;
            }
        }
    }
}

void ml_phase_mova(MlPlayer *p, MlPtr repeat_buffer, MlPtr phase_buffer, uint32_t todo)
{
    uint32_t i;
    (void)p;
    for (i = 0; i < todo; i++)
        phase_buffer.data[phase_buffer.start_offset++] = repeat_buffer.data[repeat_buffer.start_offset++];
}

/* the C# local function Loop() of MixPlay */
static void mix_loop(const int8_t *source1, uint32_t source1_offset, const int8_t *source2, uint32_t source2_offset, int8_t *destination, uint32_t destination_offset, uint32_t count, int boost)
{
    uint32_t i;
    for (i = 0; i < count; i++) {
        int16_t val = (int16_t)((source1[source1_offset++] + source2[source2_offset++]) >> boost);
        destination[destination_offset++] = (int8_t)val;
    }
}

void ml_mix_play(MlPlayer *p, MlVoice *v)
{
    if (v->effects2.effect & ML_IE2_MIX) {
        MlCounter *counter_info = &v->mix_counter;
        uint16_t previous_counter;
        uint32_t repeat_length;
        uint16_t length;
        MlPtr repeat_buffer, mix_buffer, repeat2_buffer, dest_buffer;
        int boost;

        if (v->effects2.argument & ML_MF_MIX_STEP) {
            if (!(v->effects2.argument & ML_MF_MIX_INIT)) {
                if (v->part_note == 0)
                    goto no_count;
                counter_info->save_counter = counter_info->counter;
            } else {
                if (v->part_note != 0)
                    counter_info->save_counter = counter_info->counter;
            }
        } else
            counter_info->save_counter = counter_info->counter;
        if (v->effects2.argument & ML_MF_MIX_COUNTER)
            ml_one_way_counter(p, counter_info);
        else
            ml_counter(p, counter_info);
no_count:
        previous_counter = counter_info->save_counter;
        if (v->effects2.argument & ML_MF_MIX_STEP) {
            if (counter_info->delay != 0)
                counter_info->delay--;
            else {
                int16_t speed = (int8_t)v->mix_speed;
                if (speed < 0) {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if ((int16_t)counter_info->save_counter < 0)
                        counter_info->save_counter = 0;
                } else {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if (counter_info->save_counter > 510)
                        counter_info->save_counter = 510;
                }
            }
        }
        repeat_length = v->wave_sample_repeat_length * 2U;
        if (repeat_length > 128) {
            previous_counter >>= 1;
            length = 0;
        } else if (repeat_length > 64) {
            previous_counter >>= 2;
            length = 256;
        } else if (repeat_length > 32) {
            previous_counter >>= 3;
            length = 256 + 128;
        } else if (repeat_length > 16) {
            previous_counter >>= 4;
            length = 256 + 128 + 64;
        } else {
            previous_counter >>= 5;
            length = 256 + 128 + 64 + 32;
        }
        repeat_buffer = v->wave_sample_repeat_pointer;
        mix_buffer = new_ptr(v->mix_wave_buffer);
        if (!(v->effects2.argument & ML_MF_MIX_BUFF)) {
            mix_buffer = repeat_buffer;
            if (v->mix_wave_number != 0) {
                mix_buffer = p->song->smp[v->mix_wave_number].sample_repeat_pointer;
                mix_buffer.start_offset += length;
            }
        }
        repeat2_buffer = mix_buffer;
        dest_buffer = new_ptr(v->mix_wave_buffer);
        v->wave_sample_repeat_pointer = dest_buffer;
        if (v->wave_or_sample != ML_ST_SAMPLE)
            v->wave_sample_pointer = dest_buffer;
        mix_buffer.start_offset += previous_counter;
        repeat_length -= previous_counter;
        boost = (v->mix_res_fil_boost & ML_BF_MIX) ? 0 : 1;
        mix_loop(repeat_buffer.data, repeat_buffer.start_offset, mix_buffer.data, mix_buffer.start_offset, dest_buffer.data, dest_buffer.start_offset, repeat_length, boost);
        if (previous_counter != 0)
            mix_loop(repeat_buffer.data, repeat_buffer.start_offset + repeat_length, repeat2_buffer.data, repeat2_buffer.start_offset, dest_buffer.data, dest_buffer.start_offset + repeat_length, previous_counter, boost);
    }
}

void ml_resonance_play(MlPlayer *p, MlVoice *v)
{
    if (v->effects2.effect & ML_IE2_RESONANCE) {
        MlCounter *counter_info = &v->resonance_counter;
        uint16_t previous_counter;
        MlPtr repeat_buffer, resonance_buffer;
        uint32_t repeat_length;
        int8_t last_sample;
        int16_t sample_state;
        uint16_t damping_coef;
        int16_t velocity;
        uint16_t cutoff_period;
        int8_t output;
        int boost;
        uint32_t i;

        if (v->effects2.argument & ML_MF_RESONANCE_STEP) {
            if (!(v->effects2.argument & ML_MF_RESONANCE_INIT)) {
                if (v->part_note == 0)
                    goto no_count;
                counter_info->save_counter = counter_info->counter;
            } else {
                if (v->part_note != 0)
                    counter_info->save_counter = counter_info->counter;
            }
        } else
            counter_info->save_counter = counter_info->counter;
        ml_counter(p, counter_info);
no_count:
        previous_counter = counter_info->save_counter;
        if (v->effects2.argument & ML_MF_RESONANCE_STEP) {
            if (counter_info->delay != 0)
                counter_info->delay--;
            else {
                int16_t speed = (int8_t)v->resonance_speed;
                if (speed < 0) {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if ((int16_t)counter_info->save_counter < 0)
                        counter_info->save_counter = 0;
                } else {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if (counter_info->save_counter > 510)
                        counter_info->save_counter = 510;
                }
            }
        }
        repeat_buffer = v->wave_sample_repeat_pointer;
        resonance_buffer = new_ptr(v->resonance_wave_buffer);
        v->wave_sample_repeat_pointer = resonance_buffer;
        if ((v->effects1.effect & ML_IE1_LOOP) || (v->wave_or_sample != ML_ST_SAMPLE))
            v->wave_sample_pointer = resonance_buffer;
        repeat_length = v->wave_sample_repeat_length * 2U;
        last_sample = v->resonance_last_sample;
        if (v->resonance_last_init) {
            v->resonance_last_init = 0;
            last_sample = (int8_t)(repeat_buffer.data[repeat_buffer.start_offset + repeat_length - 1] >> 2);
        }
        sample_state = (int16_t)(last_sample * 128);
        previous_counter &= 0xfffe;
        damping_coef = 0x8000;
        velocity = 0;
        cutoff_period = ml_resonance_list[previous_counter / 2];
        output = last_sample;
        damping_coef = (uint16_t)(damping_coef - ml_resample_list[v->resonance_amp]);
        damping_coef = (uint16_t)((int32_t)((uint32_t)damping_coef * 0xe666u) >> 16);
        boost = (v->mix_res_fil_boost & ML_BF_RESONANCE) ? 6 : 7;
        for (i = 0; i < repeat_length; i++) {
            int acceleration = (((repeat_buffer.data[repeat_buffer.start_offset++] * 32) - sample_state) * 128) / cutoff_period;
            velocity = (int16_t)(velocity + acceleration);
            sample_state = (int16_t)(sample_state + velocity);
            output = (int8_t)(sample_state >> boost);
            resonance_buffer.data[resonance_buffer.start_offset++] = output;
            velocity = (int16_t)((velocity * (int16_t)damping_coef) >> 15);
        }
        v->resonance_last_sample = output;
    }
}

void ml_filter_play(MlPlayer *p, MlVoice *v)
{
    if (v->effects2.effect & ML_IE2_FILTER) {
        MlCounter *counter_info = &v->filter_counter;
        uint16_t previous_counter;
        MlPtr repeat_buffer, filter_buffer;
        uint32_t repeat_length;
        int8_t last_sample;
        uint32_t i;

        if (v->effects1.argument & ML_IF_FILTER_STEP) {
            if (!(v->effects1.argument & ML_IF_FILTER_INIT)) {
                if (v->part_note == 0)
                    goto no_count;
                counter_info->save_counter = counter_info->counter;
            } else {
                if (v->part_note != 0)
                    counter_info->save_counter = counter_info->counter;
            }
        } else
            counter_info->save_counter = counter_info->counter;
        ml_counter(p, counter_info);
no_count:
        previous_counter = counter_info->save_counter;
        if (v->effects1.argument & ML_IF_FILTER_STEP) {
            if (counter_info->delay != 0)
                counter_info->delay--;
            else {
                int16_t speed = (int8_t)v->filter_speed;
                if (speed < 0) {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if ((int16_t)counter_info->save_counter < 0)
                        counter_info->save_counter = 0;
                } else {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if (counter_info->save_counter > 510)
                        counter_info->save_counter = 510;
                }
            }
        }
        repeat_buffer = v->wave_sample_repeat_pointer;
        filter_buffer = new_ptr(v->filter_wave_buffer);
        v->wave_sample_repeat_pointer = filter_buffer;
        if ((v->effects1.effect & ML_IE1_LOOP) || (v->wave_or_sample != ML_ST_SAMPLE))
            v->wave_sample_pointer = filter_buffer;
        repeat_length = v->wave_sample_repeat_length * 2U;
        last_sample = v->filter_last_sample;
        if (v->filter_type == ML_FT_NORMAL) {
            int16_t sample_state;
            uint16_t cutoff_coef, damping_coef;
            int16_t velocity;
            int8_t output;
            int boost;
            if (v->filter_last_init) {
                v->filter_last_init = 0;
                last_sample = repeat_buffer.data[repeat_buffer.start_offset + repeat_length - 1];
            }
            sample_state = (int16_t)(last_sample * 128);
            previous_counter &= 0xfffe;
            cutoff_coef = ml_filter_list[previous_counter / 2];
            damping_coef = (uint16_t)(0x8000 - cutoff_coef);
            cutoff_coef >>= 1;
            damping_coef = (uint16_t)((((int16_t)damping_coef) * (int16_t)0xf000) >> 16);
            velocity = 0;
            output = last_sample;
            boost = (v->mix_res_fil_boost & ML_BF_FILTER) ? 6 : 7;
            for (i = 0; i < repeat_length; i++) {
                int acceleration = (((repeat_buffer.data[repeat_buffer.start_offset++] * 128) - sample_state) * cutoff_coef) >> 14;
                velocity = (int16_t)(velocity + acceleration);
                sample_state = (int16_t)(sample_state + velocity);
                output = (int8_t)(sample_state >> boost);
                filter_buffer.data[filter_buffer.start_offset++] = output;
                velocity = (int16_t)((velocity * (int16_t)damping_coef) >> 15);
            }
            v->filter_last_sample = output;
        } else {
            int16_t sample_state;
            uint16_t cutoff_coef, damping_coef;
            int16_t velocity;
            int8_t output;
            int boost;
            if (v->filter_last_init) {
                v->filter_last_init = 0;
                last_sample = (int8_t)(repeat_buffer.data[repeat_buffer.start_offset + repeat_length - 1] >> 1);
            }
            sample_state = (int16_t)(last_sample * 128);
            previous_counter &= 0xfffe;
            cutoff_coef = ml_resonance_filter_list[previous_counter / 2];
            damping_coef = (uint16_t)(0x8000 - cutoff_coef);
            cutoff_coef >>= 1;
            damping_coef = (uint16_t)((int32_t)((uint32_t)damping_coef * 0xe666u) >> 16);
            velocity = 0;
            output = last_sample;
            boost = (v->mix_res_fil_boost & ML_BF_FILTER) ? 6 : 7;
            for (i = 0; i < repeat_length; i++) {
                int acceleration = (((repeat_buffer.data[repeat_buffer.start_offset++] * 64) - sample_state) * cutoff_coef) >> 14;
                velocity = (int16_t)(velocity + acceleration);
                sample_state = (int16_t)(sample_state + velocity);
                output = (int8_t)(sample_state >> boost);
                filter_buffer.data[filter_buffer.start_offset++] = output;
                velocity = (int16_t)((velocity * (int16_t)damping_coef) >> 15);
            }
            v->filter_last_sample = output;
        }
    }
}

void ml_transform_play(MlPlayer *p, MlVoice *v)
{
    if (v->effects2.effect & ML_IE2_TRANSFORM) {
        MlCounter *counter_info = &v->transform_counter;
        uint32_t repeat_length = v->wave_sample_repeat_length * 2U;
        uint16_t length = 0;
        uint16_t previous_counter;
        int16_t octave_size, remainder;
        uint16_t octave_index;
        int interpolation_factor, sample_number_index;
        uint8_t sample_number;
        MlPtr source_buffer1, source_buffer2, transform_buffer;
        uint32_t i;

        if (repeat_length != 256) {
            length = 256;
            if (repeat_length != 128) {
                length = 256 + 128;
                if (repeat_length != 64) {
                    length = 256 + 128 + 64;
                    if (repeat_length != 32)
                        length = 256 + 128 + 64 + 32;
                }
            }
        }
        if (v->effects1.argument & ML_IF_TRANSFORM_STEP) {
            if (!(v->effects1.argument & ML_IF_TRANSFORM_INIT)) {
                if (v->part_note == 0)
                    goto no_count;
                counter_info->save_counter = counter_info->counter;
            } else {
                if (v->part_note != 0)
                    counter_info->save_counter = counter_info->counter;
            }
        } else
            counter_info->save_counter = counter_info->counter;
        ml_counter(p, counter_info);
no_count:
        previous_counter = counter_info->save_counter;
        if (v->effects1.argument & ML_IF_TRANSFORM_STEP) {
            if (counter_info->delay != 0)
                counter_info->delay--;
            else {
                int16_t speed = (int8_t)v->transform_speed;
                if (speed < 0) {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if ((int16_t)counter_info->save_counter < 0)
                        counter_info->save_counter = 0;
                } else {
                    counter_info->save_counter = (uint16_t)(counter_info->save_counter + speed);
                    if (counter_info->save_counter > 510)
                        counter_info->save_counter = 510;
                }
            }
        }
        previous_counter >>= 1;
        octave_size = 256;
        remainder = (int16_t)previous_counter;
        octave_index = 0;
        remainder = (int16_t)(remainder - octave_size);
        if (remainder > 0) {
            octave_index++;
            previous_counter = (uint16_t)remainder;
            remainder = (int16_t)(remainder - octave_size);
            if (remainder > 0) {
                octave_index++;
                previous_counter = (uint16_t)remainder;
                remainder = (int16_t)(remainder - octave_size);
                if (remainder > 0) {
                    octave_index++;
                    previous_counter = (uint16_t)remainder;
                    remainder = (int16_t)(remainder - octave_size);
                    if (remainder > 0) {
                        octave_index++;
                        previous_counter = (uint16_t)remainder;
                        remainder = (int16_t)(remainder - octave_size);
                        if (remainder > 0) {
                            octave_index++;
                            previous_counter = (uint16_t)remainder;
                        }
                    }
                }
            }
        }
        interpolation_factor = previous_counter;
        sample_number_index = octave_index;
        sample_number = v->transform_wave_sample_numbers[sample_number_index++];
        if (sample_number == 0)
            return;
        source_buffer1 = p->song->smp[sample_number].sample_repeat_pointer;
        source_buffer1.start_offset += length;
        if (octave_index == 0)
            source_buffer1 = v->wave_sample_repeat_pointer;
        sample_number = v->transform_wave_sample_numbers[sample_number_index];
        if ((sample_number == 0) || (sample_number >= p->song->nsmp))
            return;
        source_buffer2 = p->song->smp[sample_number].sample_repeat_pointer;
        source_buffer2.start_offset += length;
        if (source_buffer2.start_offset >= source_buffer2.data_len)
            return;
        transform_buffer = new_ptr(v->transform_wave_buffer);
        v->wave_sample_repeat_pointer = transform_buffer;
        if ((v->effects1.effect & ML_IE1_LOOP) || (v->wave_or_sample != ML_ST_SAMPLE))
            v->wave_sample_pointer = transform_buffer;
        for (i = 0; i < repeat_length; i++) {
            int sample1 = source_buffer1.data[source_buffer1.start_offset++];
            int sample2 = source_buffer2.data[source_buffer2.start_offset++];
            int8_t dest_sample = (int8_t)((((sample2 - sample1) * interpolation_factor) >> 8) + sample1);
            transform_buffer.data[transform_buffer.start_offset++] = dest_sample;
        }
    }
}

void ml_one_way_counter(MlPlayer *p, MlCounter *counter_info)
{
    uint16_t counter = counter_info->counter;
    (void)p;
    if (counter_info->step || (counter_info->delay == 0)) {
        counter = (uint16_t)(counter + counter_info->speed);
        counter &= 0x1ff;
        counter_info->counter = counter;
    } else
        counter_info->delay--;
}

void ml_counter(MlPlayer *p, MlCounter *counter_info)
{
    int16_t counter = (int16_t)counter_info->counter;
    (void)p;
    if (counter_info->step || (counter_info->delay == 0)) {
        if (counter_info->turns < 0)
            return;
        if (counter_info->repeat < counter_info->repeat_end) {
            if (counter_info->speed < 0) {
                counter = (int16_t)(counter + counter_info->speed);
                if (counter >= counter_info->repeat) {
                    counter_info->counter = (uint16_t)counter;
                    return;
                }
            } else {
                counter = (int16_t)(counter + counter_info->speed);
                if (counter <= counter_info->repeat_end) {
                    counter_info->counter = (uint16_t)counter;
                    return;
                }
            }
        } else {
            if (counter_info->speed < 0) {
                counter = (int16_t)(counter + counter_info->speed);
                if (counter >= counter_info->repeat_end) {
                    counter_info->counter = (uint16_t)counter;
                    return;
                }
            } else {
                counter = (int16_t)(counter + counter_info->speed);
                if (counter <= counter_info->repeat) {
                    counter_info->counter = (uint16_t)counter;
                    return;
                }
            }
        }
        if (counter_info->turns != 0) {
            counter_info->turns = (int16_t)(counter_info->turns - 1);
            if (counter_info->turns == 0)
                counter_info->turns = -1;
        }
        counter = (int16_t)(counter - counter_info->speed);
        counter_info->speed = (int16_t)-counter_info->speed;
        counter_info->counter = (uint16_t)counter;
    } else
        counter_info->delay--;
}
