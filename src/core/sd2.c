/* SidMon 2.0 (Amiga, "SIDMON II - THE MIDI VERSION" at offset 58): loader + replayer, a port of the NostalgicPlayer
   SidMon20Worker (MIT). Paula-like mixer in the style of bp.c. C# integer semantics are kept (byte/ushort/short wrap,
   truncating casts). Sample negation changes the sample data while playing: every player has its own copy of the
   sample data and of the negation state, so several players do not interfere.
   Timing: the worker never sets PlayingFrequency, so Play() runs at the default 50 Hz (PAL vblank).
   Sub-songs: the worker is a ModulePlayerWithSubSongDurationAgentBase without its own SubSongs, so there is one song;
   it ends when the last position is done (endReached). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sd2.h"

#define PAULA_PAL 3546895.0
#define SD2_TICK_HZ 50.0
#define SD2_SCAN_TICKS (50 * 60 * 60)                               /* duration scan gives up after one hour */

enum { ENV_DONE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };  /* EnvelopeState */

/* ---------------------------------------------------------------- tables (Tables.cs) */
#define NUM_PERIODS 73
static const uint16_t sd2_periods[NUM_PERIODS] = {
       0,
                      5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616,
    3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1808,
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  904,
     856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453,
     428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,
     214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
     107,  101,   95
};
static const uint8_t pan4[4] = { 0, 1, 1, 0 };                    /* 0 = left, 1 = right */

/* ---------------------------------------------------------------- song */
typedef struct {                                                    /* Instrument */
    uint8_t waveform_list_number, waveform_list_length, waveform_list_speed, waveform_list_delay;
    uint8_t arpeggio_number, arpeggio_length, arpeggio_speed, arpeggio_delay;
    uint8_t vibrato_number, vibrato_length, vibrato_speed, vibrato_delay;
    int8_t pitch_bend_speed; uint8_t pitch_bend_delay;
    uint8_t attack_max, attack_speed, decay_min, decay_speed, sustain_time, release_min, release_speed;
} Sd2Inst;
typedef struct { uint32_t length, loop_start, loop_length, data_off; } Sd2Sample;
typedef struct {                                                    /* SampleNegateInfo */
    uint32_t start_offset, end_offset;
    uint16_t loop_index, status;
    int16_t speed;
    int32_t position;
    uint16_t index;
    int16_t do_negation;
} Sd2Neg;
typedef struct { uint8_t track_number; int8_t note_transpose, instrument_transpose; } Sd2Seq;
typedef struct { uint32_t off, len; } Sd2Track;                     /* off = file offset of the track data */

struct Sd2Song {
    uint8_t *mem; size_t mem_len;                                   /* the module as loaded (the lists and tracks are read from it) */
    int number_of_positions;                                        /* the real count (the C# keeps one less) */
    uint8_t start_speed;
    uint32_t waveform_off, arpeggios_off, vibratoes_off;            /* list blocks (16 bytes per list) */
    Sd2Seq *sequences[4];
    Sd2Track *tracks; int num_tracks;
    Sd2Inst *instruments; int num_instruments;
    Sd2Sample *samples; Sd2Neg *negate; int num_samples;
    int8_t *sample_data; uint32_t sample_bytes;
    uint16_t *rows;                                                 /* rows played per position (scan) */
};

/* ---------------------------------------------------------------- player */
typedef struct {                                                    /* VoiceInfo */
    int sequence_list;                                              /* channel */
    const int8_t *sample_data; uint32_t sample_length;
    uint16_t sample_period;
    int16_t sample_volume;
    int envelope_state;
    uint16_t sustain_counter;
    int instrument;                                                 /* index into the player's instruments */
    const int8_t *loop_sample; uint32_t loop_sample_len, loop_offset, loop_length;
    uint16_t original_note;
    uint16_t wave_list_delay; int16_t wave_list_offset;
    uint16_t arpeggio_delay; int16_t arpeggio_offset;
    uint16_t vibrato_delay; int16_t vibrato_offset;
    uint16_t current_note, current_instrument, current_effect, current_effect_arg;
    uint16_t pitch_bend_counter;
    int16_t instrument_transpose, pitch_bend_value;
    uint16_t note_slide_note; int16_t note_slide_speed;
    int track_position, current_track;
    uint16_t empty_notes_counter;
    int16_t note_transpose;
    uint16_t current_sample;
} Sd2Voice;

typedef struct {
    const int8_t *d; uint32_t len; double pos; int on;
    const int8_t *ld; uint32_t llen;                                /* SetLoop / SetSample: plays when the current pass ends */
    uint32_t period; int volume;                                    /* volume 0..256 (IChannel.SetVolume) */
} Sd2Mix;

struct Sd2Player {
    const Sd2Song *s;
    int rate, fast;
    /* GlobalPlayingInfo */
    int current_position;                                           /* sbyte in C# (fine up to 128 positions) */
    uint8_t current_row, pattern_length, current_rast, current_rast2, speed;
    Sd2Voice v[4];
    Sd2Inst *instruments;                                           /* copy: effect 5 changes them */
    Sd2Neg *negate;                                                 /* copy */
    int8_t *sample_data;                                            /* copy: DoNegation changes it */
    int end_reached, ended;
    int shown_pos, shown_row, rows_played;
    uint16_t *rows_out;                                             /* the scan records the rows per position here */
    Sd2Mix m[4];
    double tick_left;
    unsigned mute;
    float *sc; int sc_len; unsigned sc_pos;
};

/* ---------------------------------------------------------------- stream (ModuleStream; EndOfStream = a read came up short) */
typedef struct { const uint8_t *b; size_t n; long long pos; int eof; } Rd;

static size_t rd_read(Rd *r, uint8_t *dst, size_t cnt) {
    size_t got = 0;
    if (r->pos >= 0 && (unsigned long long)r->pos < r->n) { got = r->n - (size_t)r->pos; if (got > cnt) got = cnt; if (dst) memcpy(dst, r->b + r->pos, got); }
    if (dst && got < cnt) memset(dst + got, 0, cnt - got);
    r->pos += (long long)cnt;
    if (r->pos > (long long)r->n) r->pos = (long long)r->n;
    if (got < cnt) r->eof = 1;
    return got;
}
static uint8_t rd_u8(Rd *r) { uint8_t x; rd_read(r, &x, 1); return x; }
static uint16_t rd_b16(Rd *r) { uint8_t x[2]; rd_read(r, x, 2); return (uint16_t)(x[0] << 8 | x[1]); }
static uint32_t rd_b32(Rd *r) { uint8_t x[4]; rd_read(r, x, 4); return (uint32_t)x[0] << 24 | (uint32_t)x[1] << 16 | (uint32_t)x[2] << 8 | x[3]; }

/* ---------------------------------------------------------------- identify */
int sd2_probe(const uint8_t *b, size_t n) {
    return b && n >= 86 && !memcmp(b + 58, "SIDMON II - THE MIDI VERSION", 28);
}

/* ---------------------------------------------------------------- Load */
void sd2_free(Sd2Song *s) {
    if (!s) return;
    for (int i = 0; i < 4; i++) free(s->sequences[i]);
    free(s->mem); free(s->tracks); free(s->instruments); free(s->samples); free(s->negate);
    free(s->sample_data); free(s->rows); free(s);
}

typedef struct { uint32_t offset, length; } BlockInfo;

/* LoadSingleList: only checked here, the lists are read from the module memory (see list_byte) */
static int load_single_list(const Sd2Song *s, BlockInfo bi) {
    if (bi.length % 16 != 0) return 0;
    return (unsigned long long)bi.offset + bi.length <= s->mem_len;
}

static int load(Sd2Song *s, const uint8_t *b, size_t n) {
    Rd r = { b, n, 0, 0 };
    int i, j;
    /* LoadSongData */
    r.pos = 2;                                                          /* skip MIDI mode */
    uint8_t number_of_positions = (uint8_t)(rd_u8(&r) + 1);
    s->start_speed = rd_u8(&r);
    uint16_t number_of_samples = (uint16_t)(rd_b16(&r) / 64);
    uint32_t offset = 58;
    offset += rd_b32(&r);                                               /* ID length */
    offset += rd_b32(&r);                                               /* song length length */
    BlockInfo blk[10];                                                  /* position table, note transpose, instrument transpose,
                                                                           instruments, waveform list, arpeggios, vibratoes,
                                                                           sample info, track table, tracks */
    for (i = 0; i < 10; i++) {
        uint32_t length = rd_b32(&r);
        blk[i].offset = offset; blk[i].length = length;
        offset += length;
    }
    if (r.eof) return 0;

    /* LoadLists */
    if (!load_single_list(s, blk[4]) || !load_single_list(s, blk[5]) || !load_single_list(s, blk[6])) return 0;
    s->waveform_off = blk[4].offset; s->arpeggios_off = blk[5].offset; s->vibratoes_off = blk[6].offset;

    /* LoadSequences */
    if (blk[0].length != blk[1].length || blk[1].length != blk[2].length) return 0;
    if (blk[0].length != (uint32_t)number_of_positions * 4) return 0;
    if (number_of_positions == 0) return 0;                             /* C#: no sequences, FindNote throws */
    {
        size_t len = blk[0].length;
        uint8_t *buf = (uint8_t *)malloc(len * 3);
        if (!buf) return 0;
        r.pos = blk[0].offset; if (rd_read(&r, buf, len) < len) { free(buf); return 0; }
        r.pos = blk[1].offset; rd_read(&r, buf + len, len); if (r.eof) { free(buf); return 0; }
        r.pos = blk[2].offset; rd_read(&r, buf + 2 * len, len); if (r.eof) { free(buf); return 0; }
        int start_offset = 0;
        for (i = 0; i < 4; i++, start_offset += number_of_positions) {
            s->sequences[i] = (Sd2Seq *)calloc(number_of_positions, sizeof(Sd2Seq));
            if (!s->sequences[i]) { free(buf); return 0; }
            for (j = 0; j < number_of_positions; j++) {
                Sd2Seq *q = &s->sequences[i][j];
                q->track_number = buf[start_offset + j];
                q->note_transpose = (int8_t)buf[len + (size_t)start_offset + (size_t)j];
                q->instrument_transpose = (int8_t)buf[2 * len + (size_t)start_offset + (size_t)j];
            }
        }
        free(buf);
    }
    s->number_of_positions = number_of_positions;

    /* LoadTracks */
    {
        uint32_t number_of_tracks = blk[8].length / 2;
        uint16_t *track_offset_table = (uint16_t *)malloc(sizeof(uint16_t) * ((size_t)number_of_tracks + 1));
        if (!track_offset_table) return 0;
        r.pos = blk[8].offset;
        for (uint32_t t = 0; t < number_of_tracks; t++) track_offset_table[t] = rd_b16(&r);
        if (r.eof) { free(track_offset_table); return 0; }
        s->tracks = (Sd2Track *)calloc((size_t)number_of_tracks + 1, sizeof(Sd2Track));
        if (!s->tracks) { free(track_offset_table); return 0; }
        s->num_tracks = (int)number_of_tracks;
        for (uint32_t t = 0; t < number_of_tracks; t++) {
            uint32_t track_length = (t == number_of_tracks - 1 ? blk[9].length : track_offset_table[t + 1]) - track_offset_table[t];
            if (track_length > n) { free(track_offset_table); return 0; }  /* C#: new byte[negative / huge] throws */
            r.pos = (long long)blk[9].offset + track_offset_table[t];
            if (rd_read(&r, NULL, track_length) < track_length) { free(track_offset_table); return 0; }
            s->tracks[t].off = blk[9].offset + track_offset_table[t];
            s->tracks[t].len = track_length;
        }
        free(track_offset_table);
    }
    /* the stream position is the start of the sample data (the C# "make it even" check is "% 1", never true) */
    uint32_t sample_offset = (uint32_t)r.pos;

    /* LoadInstruments */
    if (blk[3].length % 32 != 0) return 0;
    r.pos = blk[3].offset;
    s->num_instruments = (int)(blk[3].length / 32);
    if (s->num_instruments == 0) return 0;                              /* C#: InitializeSound uses instruments[0] */
    s->instruments = (Sd2Inst *)calloc((size_t)s->num_instruments, sizeof(Sd2Inst));
    if (!s->instruments) return 0;
    for (i = 0; i < s->num_instruments; i++) {
        Sd2Inst *in = &s->instruments[i];
        in->waveform_list_number = rd_u8(&r); in->waveform_list_length = rd_u8(&r);
        in->waveform_list_speed = rd_u8(&r); in->waveform_list_delay = rd_u8(&r);
        in->arpeggio_number = rd_u8(&r); in->arpeggio_length = rd_u8(&r);
        in->arpeggio_speed = rd_u8(&r); in->arpeggio_delay = rd_u8(&r);
        in->vibrato_number = rd_u8(&r); in->vibrato_length = rd_u8(&r);
        in->vibrato_speed = rd_u8(&r); in->vibrato_delay = rd_u8(&r);
        in->pitch_bend_speed = (int8_t)rd_u8(&r); in->pitch_bend_delay = rd_u8(&r);
        r.pos += 2;
        in->attack_max = rd_u8(&r); in->attack_speed = rd_u8(&r);
        in->decay_min = rd_u8(&r); in->decay_speed = rd_u8(&r);
        in->sustain_time = rd_u8(&r);
        in->release_min = rd_u8(&r); in->release_speed = rd_u8(&r);
        if (r.eof) return 0;
        r.pos += 9;
    }

    /* LoadSamples */
    s->num_samples = number_of_samples;
    s->samples = (Sd2Sample *)calloc((size_t)number_of_samples + 1, sizeof(Sd2Sample));
    s->negate = (Sd2Neg *)calloc((size_t)number_of_samples + 1, sizeof(Sd2Neg));
    if (!s->samples || !s->negate) return 0;
    r.pos = blk[7].offset;
    uint32_t total = 0;
    for (i = 0; i < number_of_samples; i++) {
        Sd2Sample *sm = &s->samples[i];
        Sd2Neg *ng = &s->negate[i];
        r.pos += 4;                                                     /* sample data pointer */
        sm->length = (uint32_t)rd_b16(&r) * 2;
        sm->loop_start = (uint32_t)rd_b16(&r) * 2;
        sm->loop_length = (uint32_t)rd_b16(&r) * 2;
        if (sm->loop_start > sm->length) { sm->loop_start = 0; sm->loop_length = 0; }
        else if (sm->loop_start + sm->loop_length > sm->length) sm->loop_length = sm->length - sm->loop_start;
        ng->start_offset = (uint32_t)rd_b16(&r) * 2;
        ng->end_offset = (uint32_t)rd_b16(&r) * 2;
        ng->loop_index = rd_b16(&r);
        ng->status = rd_b16(&r);
        ng->speed = (int16_t)rd_b16(&r);
        ng->position = (int32_t)rd_b32(&r);
        ng->index = rd_b16(&r);
        ng->do_negation = (int16_t)rd_b16(&r);
        if (r.eof) return 0;
        r.pos += 4;
        if (rd_read(&r, NULL, 32) < 32) return 0;                       /* name (UI only) */
        sm->data_off = total;
        total += sm->length;
    }
    s->sample_bytes = total;
    s->sample_data = (int8_t *)calloc((size_t)total + 1, 1);
    if (!s->sample_data) return 0;
    r.pos = sample_offset;
    for (i = 0; i < number_of_samples; i++) {
        Sd2Sample *sm = &s->samples[i];
        if (rd_read(&r, (uint8_t *)s->sample_data + sm->data_off, sm->length) < sm->length) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- module memory helpers */
/* The lists are 16 bytes each and lie one after the other (waveforms, arpeggios, vibratoes) as the original replayer
   has them in memory. The C# keeps separate 16-byte arrays and throws past them (a list number beyond the block or an
   offset > 15); here such reads go on in the module memory as on the Amiga, 0 past the end of the file. */
static uint8_t mem_byte(const Sd2Song *s, unsigned long long off) { return off < s->mem_len ? s->mem[off] : 0; }
static uint8_t waveform_value(const Sd2Song *s, int number, int offset) { return mem_byte(s, (unsigned long long)s->waveform_off + (unsigned)number * 16u + (unsigned)offset); }
static int8_t arpeggio_value(const Sd2Song *s, int number, int offset) { return (int8_t)mem_byte(s, (unsigned long long)s->arpeggios_off + (unsigned)number * 16u + (unsigned)offset); }
static int8_t vibrato_value(const Sd2Song *s, int number, int offset) { return (int8_t)mem_byte(s, (unsigned long long)s->vibratoes_off + (unsigned)number * 16u + (unsigned)offset); }

/* Tables.Periods[i]: the C# throws outside the table; -1 here and the caller leaves the period alone */
static int period_at(int i) { return i >= 0 && i < NUM_PERIODS ? sd2_periods[i] : -1; }

/* ---------------------------------------------------------------- mixer channel (IChannel) */
static void ch_play_sample(Sd2Mix *m, const int8_t *base, uint32_t base_len, uint32_t start, uint32_t len) {
    m->ld = NULL; m->llen = 0; m->pos = 0;
    if (!base || start >= base_len) { m->d = NULL; m->len = 0; m->on = 0; return; }
    if (len > base_len - start) len = base_len - start;
    m->d = base + start; m->len = len; m->on = len > 0;
}
static void ch_set_loop(Sd2Mix *m, const int8_t *base, uint32_t base_len, uint32_t start, uint32_t len) {
    if (!base || start >= base_len) { m->ld = NULL; m->llen = 0; return; }
    if (len > base_len - start) len = base_len - start;
    m->ld = base + start; m->llen = len;
}
static void ch_mute(Sd2Mix *m) { m->on = 0; }
static int ch_is_active(const Sd2Mix *m) { return m->on && m->d && m->len > 0; }
static void ch_set_period(Sd2Mix *m, uint16_t period) { m->period = period; }          /* SetAmigaPeriod */
static void ch_set_volume(Sd2Mix *m, uint16_t volume) { m->volume = volume > 256 ? 256 : volume; }  /* SetVolume 0..256 */
static void ch_set_amiga_volume(Sd2Mix *m, uint16_t volume) { m->volume = (volume > 64 ? 64 : volume) * 4; }

/* ---------------------------------------------------------------- sample access */
static const Sd2Sample *get_sample(const Sd2Player *p, unsigned n, const int8_t **data) {
    /* samples[n]: the C# throws for a sample number past the samples; an empty sample here */
    if (n >= (unsigned)p->s->num_samples) { *data = NULL; return NULL; }
    const Sd2Sample *sm = &p->s->samples[n];
    *data = sm->length ? p->sample_data + sm->data_off : NULL;
    return sm;
}

/* ---------------------------------------------------------------- FindNote / InitializeSound */
static void find_note(Sd2Player *p, Sd2Voice *v) {
    const Sd2Seq *sequence = &p->s->sequences[v->sequence_list][p->current_position];
    v->current_track = sequence->track_number;                          /* checked when the track is read */
    v->track_position = 0;
    v->note_transpose = sequence->note_transpose;
    v->instrument_transpose = sequence->instrument_transpose;
    v->empty_notes_counter = 0;
}

static void initialize_sound(Sd2Player *p) {
    const Sd2Song *s = p->s;
    p->current_position = 0; p->current_row = 0; p->pattern_length = 64;
    p->current_rast = 0; p->current_rast2 = 0; p->speed = s->start_speed;
    for (int i = 0; i < 4; i++) {
        Sd2Voice *v = &p->v[i];
        memset(v, 0, sizeof *v);
        v->sequence_list = i;
        v->envelope_state = ENV_ATTACK;
        v->instrument = 0;
        uint8_t sample_number = waveform_value(s, p->instruments[0].waveform_list_number, 0);
        const int8_t *data;
        const Sd2Sample *sm = get_sample(p, sample_number, &data);
        v->current_sample = sample_number;
        v->sample_data = data; v->sample_length = sm ? sm->length : 0;
        v->loop_sample = data; v->loop_sample_len = sm ? sm->length : 0;
        v->loop_offset = sm ? sm->loop_start : 0; v->loop_length = sm ? sm->loop_length : 0;
        find_note(p, v);
    }
    p->end_reached = 0;
    p->shown_pos = 0; p->shown_row = 0; p->rows_played = 0;
}

/* ---------------------------------------------------------------- DoNegation */
static void do_negation(Sd2Player *p) {
    Sd2Neg *working_samples[4];
    for (int i = 0; i < 4; i++) {
        uint16_t sample_number = p->v[i].current_sample;
        working_samples[i] = NULL;
        if (sample_number >= p->s->num_samples) continue;               /* C#: throws */
        Sd2Neg *negate_info = &p->negate[sample_number];
        working_samples[i] = negate_info;
        if (negate_info->do_negation == 0) {
            negate_info->do_negation = -1;
            if (negate_info->index == 0) {
                negate_info->index = negate_info->loop_index;
                if (negate_info->status != 0) {
                    const Sd2Sample *sm = &p->s->samples[sample_number];
                    uint32_t end_offset = negate_info->end_offset - 1;
                    int position = (int)((long long)negate_info->start_offset + negate_info->position);
                    if (position >= 0 && (uint32_t)position < sm->length) {    /* C#: throws outside the sample */
                        int8_t *d = p->sample_data + sm->data_off;
                        d[position] = (int8_t)~d[position];
                    }
                    negate_info->position += negate_info->speed;
                    if (negate_info->position < 0) {
                        if (negate_info->status == 2) negate_info->position = (int32_t)end_offset;
                        else {
                            negate_info->position += -negate_info->speed;
                            negate_info->speed = (int16_t)-negate_info->speed;
                        }
                    } else {
                        if ((long long)negate_info->position > (long long)end_offset) {
                            if (negate_info->status == 1) negate_info->position = 0;
                            else {
                                negate_info->position += -negate_info->speed;
                                negate_info->speed = (int16_t)-negate_info->speed;
                            }
                        }
                    }
                }
            } else {
                negate_info->index++;
                negate_info->index &= 0x1f;
            }
        }
    }
    for (int i = 0; i < 4; i++) if (working_samples[i]) working_samples[i]->do_negation = 0;
}

/* ---------------------------------------------------------------- GetNote */
/* Reading the track: the first byte of a row reads as -1 past the end of the track (as the C#); the bytes after it
   (the C# throws there) continue in the module memory as on the Amiga */
static uint8_t track_byte(const Sd2Player *p, const Sd2Voice *v, int pos) {
    if (v->current_track >= p->s->num_tracks) return 0xff;             /* C#: tracks[] throws */
    const Sd2Track *t = &p->s->tracks[v->current_track];
    if (pos >= 0 && (uint32_t)pos < t->len) return p->s->mem[t->off + (uint32_t)pos];
    return mem_byte(p->s, (unsigned long long)t->off + (unsigned)pos);
}
static int track_len(const Sd2Player *p, const Sd2Voice *v) {
    return v->current_track < p->s->num_tracks ? (int)p->s->tracks[v->current_track].len : 0;
}

static void get_note2(Sd2Player *p, Sd2Voice *v) {
    v->current_note = 0; v->current_instrument = 0; v->current_effect = 0; v->current_effect_arg = 0;
    if (v->empty_notes_counter == 0) {
        int8_t track_value = (int8_t)(v->track_position < track_len(p, v) ? track_byte(p, v, v->track_position++) : 0xff);
        if (track_value == 0) {
            v->current_effect = track_byte(p, v, v->track_position++);
            v->current_effect_arg = track_byte(p, v, v->track_position++);
            return;
        }
        if (track_value > 0) {
            if (track_value >= 0x70) {
                v->current_effect = (uint8_t)track_value;
                v->current_effect_arg = track_byte(p, v, v->track_position++);
                return;
            }
            v->current_note = (uint16_t)track_value;
            track_value = (int8_t)track_byte(p, v, v->track_position++);
            if (track_value >= 0) {
                if (track_value >= 0x70) {
                    v->current_effect = (uint8_t)track_value;
                    v->current_effect_arg = track_byte(p, v, v->track_position++);
                    return;
                }
                v->current_instrument = (uint16_t)track_value;
                track_value = (int8_t)track_byte(p, v, v->track_position++);
                if (track_value >= 0) {
                    v->current_effect = (uint8_t)track_value;
                    v->current_effect_arg = track_byte(p, v, v->track_position++);
                    return;
                }
            }
        }
        v->empty_notes_counter = (uint16_t)(~track_value);
    } else v->empty_notes_counter--;
}

static void get_note(Sd2Player *p, Sd2Voice *v) {
    get_note2(p, v);
    if (v->current_note != 0) v->current_note = (uint16_t)(v->current_note + v->note_transpose);
}

/* ---------------------------------------------------------------- PlayVoice */
static void play_voice(Sd2Player *p, Sd2Voice *v, Sd2Mix *channel) {
    const Sd2Song *s = p->s;
    v->pitch_bend_value = 0;
    if (v->current_note != 0) {
        v->sample_volume = 0;
        v->wave_list_delay = 0; v->wave_list_offset = 0;
        v->arpeggio_delay = 0; v->arpeggio_offset = 0;
        v->vibrato_delay = 0; v->vibrato_offset = 0;
        v->pitch_bend_counter = 0;
        v->note_slide_speed = 0;
        v->envelope_state = ENV_ATTACK;
        v->sustain_counter = 0;

        uint16_t instrument = v->current_instrument;
        if (instrument != 0) {
            instrument = (uint16_t)(instrument - 1 + v->instrument_transpose);
            if (instrument < s->num_instruments) {
                v->instrument = instrument;
                v->current_sample = waveform_value(s, p->instruments[instrument].waveform_list_number, 0);
                const int8_t *data;
                const Sd2Sample *sm = get_sample(p, v->current_sample, &data);
                v->sample_data = data; v->sample_length = sm ? sm->length : 0;
                v->loop_sample = data; v->loop_sample_len = sm ? sm->length : 0;
                v->loop_offset = sm ? sm->loop_start : 0; v->loop_length = sm ? sm->loop_length : 0;
            }
        }

        int note = v->current_note + arpeggio_value(s, p->instruments[v->instrument].arpeggio_number, 0);
        if (note >= 0 && note < NUM_PERIODS) {
            v->original_note = (uint16_t)note;
            v->sample_period = sd2_periods[note];
            if (v->sample_length != 0 && v->sample_data) {
                ch_play_sample(channel, v->sample_data, v->sample_length, 0, v->sample_length);
                if (v->loop_length > 2) ch_set_loop(channel, v->loop_sample, v->loop_sample_len, v->loop_offset, v->loop_length);
            } else ch_mute(channel);
        } else ch_mute(channel);
        ch_set_period(channel, v->sample_period);
    }
}

/* ---------------------------------------------------------------- effects */
static void do_adsr_curve(Sd2Player *p, Sd2Voice *v, Sd2Mix *channel) {
    const Sd2Inst *instrument = &p->instruments[v->instrument];
    switch (v->envelope_state) {
        case ENV_ATTACK:
            v->sample_volume = (int16_t)(v->sample_volume + instrument->attack_speed);
            if (v->sample_volume >= instrument->attack_max) {
                v->sample_volume = instrument->attack_max;
                v->envelope_state = ENV_DECAY;
            }
            break;
        case ENV_DECAY:
            if (instrument->decay_speed == 0) v->envelope_state = ENV_SUSTAIN;
            else {
                v->sample_volume = (int16_t)(v->sample_volume - instrument->decay_speed);
                if (v->sample_volume <= instrument->decay_min) {
                    v->sample_volume = instrument->decay_min;
                    v->envelope_state = ENV_SUSTAIN;
                }
            }
            break;
        case ENV_SUSTAIN:
            if (v->sustain_counter == instrument->sustain_time) v->envelope_state = ENV_RELEASE;
            else v->sustain_counter++;
            break;
        case ENV_RELEASE:
            if (instrument->release_speed == 0) v->envelope_state = ENV_DONE;
            else {
                v->sample_volume = (int16_t)(v->sample_volume - instrument->release_speed);
                if (v->sample_volume <= instrument->release_min) {
                    v->sample_volume = instrument->release_min;
                    v->envelope_state = ENV_DONE;
                }
            }
            break;
    }
    ch_set_volume(channel, (uint16_t)v->sample_volume);
}

static void do_wave_form(Sd2Player *p, Sd2Voice *v, Sd2Mix *channel) {
    const Sd2Inst *instrument = &p->instruments[v->instrument];
    if (instrument->waveform_list_length != 0) {
        if (v->wave_list_delay == instrument->waveform_list_delay) {
            v->wave_list_delay = (uint16_t)(v->wave_list_delay - instrument->waveform_list_speed);
            if (v->wave_list_offset == instrument->waveform_list_length) v->wave_list_offset = -1;
            v->wave_list_offset++;
            int8_t wave_form_value = (int8_t)waveform_value(p->s, instrument->waveform_list_number, v->wave_list_offset);
            if (wave_form_value >= 0) {
                v->current_sample = (uint16_t)wave_form_value;
                const int8_t *data;
                const Sd2Sample *sm = get_sample(p, v->current_sample, &data);
                v->loop_sample = data; v->loop_sample_len = sm ? sm->length : 0;
                v->loop_offset = sm ? sm->loop_start : 0; v->loop_length = sm ? sm->loop_length : 0;
                if (ch_is_active(channel)) {                            /* SetSample + SetLoop: from the end of the current pass */
                    ch_set_loop(channel, v->loop_sample, v->loop_sample_len, v->loop_offset, v->loop_length);
                }
            } else v->wave_list_offset--;
        } else v->wave_list_delay++;
    }
}

static void do_arpeggio(Sd2Player *p, Sd2Voice *v) {
    const Sd2Inst *instrument = &p->instruments[v->instrument];
    if (instrument->arpeggio_length != 0) {
        if (v->arpeggio_delay == instrument->arpeggio_delay) {
            v->arpeggio_delay = (uint16_t)(v->arpeggio_delay - instrument->arpeggio_speed);
            if (v->arpeggio_offset == instrument->arpeggio_length) v->arpeggio_offset = -1;
            v->arpeggio_offset++;
            int8_t arpeggio_val = arpeggio_value(p->s, instrument->arpeggio_number, v->arpeggio_offset);
            int16_t new_note = (int16_t)(v->original_note + arpeggio_val);
            int per = period_at(new_note);
            if (per >= 0) v->sample_period = (uint16_t)per;
        } else v->arpeggio_delay++;
    }
}

static void do_vibrato(Sd2Player *p, Sd2Voice *v) {
    const Sd2Inst *instrument = &p->instruments[v->instrument];
    if (instrument->vibrato_length != 0) {
        if (v->vibrato_delay == instrument->vibrato_delay) {
            v->vibrato_delay = (uint16_t)(v->vibrato_delay - instrument->vibrato_speed);
            if (v->vibrato_offset == instrument->vibrato_length) v->vibrato_offset = -1;
            v->vibrato_offset++;
            int8_t vibrato_val = vibrato_value(p->s, instrument->vibrato_number, v->vibrato_offset);
            v->sample_period = (uint16_t)(v->sample_period + vibrato_val);
        } else v->vibrato_delay++;
    }
}

static void do_pitch_bend(Sd2Player *p, Sd2Voice *v) {
    const Sd2Inst *instrument = &p->instruments[v->instrument];
    if (instrument->pitch_bend_speed != 0) {
        if (v->pitch_bend_counter == instrument->pitch_bend_delay)
            v->pitch_bend_value = (int16_t)(v->pitch_bend_value + instrument->pitch_bend_speed);
        else v->pitch_bend_counter++;
    }
}

static void do_note_slide(Sd2Voice *v) {
    if (v->current_effect != 0 && v->current_effect < 0x70 && v->current_effect_arg != 0) {
        int per = period_at(v->current_effect);                         /* C#: Periods[0x49..0x6f] throws */
        if (per >= 0) {
            v->note_slide_note = (uint16_t)per;
            int direction = v->note_slide_note - v->sample_period;
            if (direction == 0) return;
            int16_t effect_arg = (int16_t)v->current_effect_arg;
            if (direction < 0) effect_arg = (int16_t)-effect_arg;
            v->note_slide_speed = effect_arg;
        }
    }
    int16_t speed = v->note_slide_speed;
    if (speed != 0) {
        v->sample_period = (uint16_t)(v->sample_period + speed);
        if (speed < 0) {
            if (v->sample_period <= v->note_slide_note) { v->sample_period = v->note_slide_note; v->note_slide_speed = 0; }
        } else {
            if (v->sample_period >= v->note_slide_note) { v->sample_period = v->note_slide_note; v->note_slide_speed = 0; }
        }
    }
}

static void do_sound_tracker(Sd2Player *p, Sd2Voice *v, Sd2Mix *channel) {
    uint16_t effect = v->current_effect;
    if (effect >= 0x70) {
        effect &= 0x0f;
        if (p->current_rast != 0 || effect >= 5) {
            switch (effect) {
                case 0x0: {                                             /* arpeggio */
                    uint8_t arp_tab[4] = { (uint8_t)(v->current_effect_arg >> 4), 0, (uint8_t)(v->current_effect_arg & 0x0f), 0 };
                    uint8_t arp_value = arp_tab[p->current_rast2 & 3];
                    int per = period_at(v->original_note + arp_value);
                    if (per >= 0) v->sample_period = (uint16_t)per;
                    break;
                }
                case 0x1: v->pitch_bend_value = (int16_t)-(int)v->current_effect_arg; break;   /* pitch up */
                case 0x2: v->pitch_bend_value = (int16_t)v->current_effect_arg; break;         /* pitch down */
                case 0x3:                                               /* volume up */
                    if (v->envelope_state == ENV_DONE) {
                        if (p->current_rast == 0 && v->current_instrument != 0) v->sample_volume = p->instruments[v->instrument].attack_speed;
                        int16_t volume = (int16_t)(v->sample_volume + v->current_effect_arg * 4);
                        if (volume >= 256) volume = 255;
                        v->sample_volume = volume;
                    }
                    break;
                case 0x4:                                               /* volume down */
                    if (v->envelope_state == ENV_DONE) {
                        if (p->current_rast == 0 && v->current_instrument != 0) v->sample_volume = p->instruments[v->instrument].attack_speed;
                        int16_t volume = (int16_t)(v->sample_volume - v->current_effect_arg * 4);
                        if (volume < 0) volume = 0;
                        v->sample_volume = volume;
                    }
                    break;
                case 0x5: {                                             /* set ADSR attack (changes the instrument) */
                    Sd2Inst *instrument = &p->instruments[v->instrument];
                    instrument->attack_max = (uint8_t)v->current_effect_arg;
                    instrument->attack_speed = (uint8_t)v->current_effect_arg;
                    break;
                }
                case 0x6:                                               /* set pattern length */
                    if (v->current_effect_arg != 0) p->pattern_length = (uint8_t)v->current_effect_arg;
                    break;
                case 0xc: {                                             /* volume change */
                    uint16_t volume = v->current_effect_arg;
                    ch_set_amiga_volume(channel, (uint16_t)(volume > 64 ? 64 : volume));
                    volume = (uint16_t)(volume * 4);
                    if (volume >= 255) volume = 255;
                    v->sample_volume = (int16_t)volume;
                    break;
                }
                case 0xf: {                                             /* speed change */
                    uint8_t speed = (uint8_t)(v->current_effect_arg & 0x0f);
                    if (speed != 0 && speed != p->speed) p->speed = speed;
                    break;
                }
            }
        }
    }
}

static void do_effect(Sd2Player *p, Sd2Voice *v, Sd2Mix *channel) {
    do_adsr_curve(p, v, channel);
    do_wave_form(p, v, channel);
    do_arpeggio(p, v);
    do_sound_tracker(p, v, channel);
    do_vibrato(p, v);
    do_pitch_bend(p, v);
    do_note_slide(v);
    v->sample_period = (uint16_t)(v->sample_period + v->pitch_bend_value);
    if (v->sample_period < 95) v->sample_period = 95;
    else if (v->sample_period > 5760) v->sample_period = 5760;
    ch_set_period(channel, v->sample_period);
}

/* ---------------------------------------------------------------- PlayIt */
static void play_it(Sd2Player *p) {
    const Sd2Song *s = p->s;
    p->current_rast2++;
    if (p->current_rast2 == 3) p->current_rast2 = 0;
    p->current_rast++;
    if (p->current_rast >= p->speed) {
        p->current_rast = 0;
        p->current_rast2 = 0;
        p->shown_pos = p->current_position; p->shown_row = p->current_row;
        for (int i = 0; i < 4; i++) get_note(p, &p->v[i]);
        for (int i = 0; i < 4; i++) play_voice(p, &p->v[i], &p->m[i]);
        do_negation(p);
        p->current_row++;
        p->rows_played++;
        if (p->current_row == p->pattern_length) {
            p->current_row = 0;
            if (p->rows_out && p->current_position < s->number_of_positions)
                p->rows_out[p->current_position] = (uint16_t)p->rows_played;
            p->rows_played = 0;
            if (p->current_position == s->number_of_positions - 1) {
                p->current_position = -1;
                p->end_reached = 1;
            }
            p->current_position++;
            for (int i = 0; i < 4; i++) find_note(p, &p->v[i]);
        }
    }
    for (int i = 0; i < 4; i++) do_effect(p, &p->v[i], &p->m[i]);
    if (p->current_rast != 0) do_negation(p);
}

/* ---------------------------------------------------------------- Play() */
static void play_tick(Sd2Player *p) {
    play_it(p);
    if (p->end_reached) {                                               /* OnEndReached: the song is over (no restart) */
        p->ended = 1;
        p->end_reached = 0;
    }
}

/* ---------------------------------------------------------------- player setup */
static Sd2Player *player_alloc(const Sd2Song *s, int rate) {
    Sd2Player *p = (Sd2Player *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->s = s; p->rate = rate;
    p->instruments = (Sd2Inst *)malloc(sizeof(Sd2Inst) * (size_t)s->num_instruments);
    p->negate = (Sd2Neg *)malloc(sizeof(Sd2Neg) * ((size_t)s->num_samples + 1));
    p->sample_data = (int8_t *)malloc((size_t)s->sample_bytes + 1);
    if (!p->instruments || !p->negate || !p->sample_data) {
        free(p->instruments); free(p->negate); free(p->sample_data); free(p); return NULL;
    }
    memcpy(p->instruments, s->instruments, sizeof(Sd2Inst) * (size_t)s->num_instruments);
    memcpy(p->negate, s->negate, sizeof(Sd2Neg) * ((size_t)s->num_samples + 1));
    memcpy(p->sample_data, s->sample_data, (size_t)s->sample_bytes + 1);
    initialize_sound(p);
    return p;
}

void sd2_player_free(Sd2Player *p) {
    if (!p) return;
    free(p->instruments); free(p->negate); free(p->sample_data); free(p);
}

/* InitDuration / duration scan of the one song: also records how many rows each position plays (pattern view) */
static void scan_song(Sd2Song *s) {
    for (int i = 0; i < s->number_of_positions; i++) s->rows[i] = 64;
    Sd2Player *p = player_alloc(s, 44100);
    if (!p) return;
    p->rows_out = s->rows;
    for (long t = 0; t < SD2_SCAN_TICKS && !p->ended; t++) play_tick(p);
    sd2_player_free(p);
}

Sd2Song *sd2_load(const uint8_t *b, size_t n, int *err) {
    if (err) *err = 0;
    if (!sd2_probe(b, n)) { if (err) *err = 1; return NULL; }
    Sd2Song *s = (Sd2Song *)calloc(1, sizeof *s);
    if (!s) { if (err) *err = 2; return NULL; }
    s->mem = (uint8_t *)malloc(n);
    if (!s->mem) { free(s); if (err) *err = 2; return NULL; }
    memcpy(s->mem, b, n); s->mem_len = n;
    if (!load(s, b, n)) { sd2_free(s); if (err) *err = 3; return NULL; }
    s->rows = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)s->number_of_positions);
    if (!s->rows) { sd2_free(s); if (err) *err = 2; return NULL; }
    scan_song(s);
    return s;
}

/* ---------------------------------------------------------------- public */
Sd2Player *sd2_new(const Sd2Song *s, int sub, int rate) {
    if (!s || rate <= 0 || sub != 0) return NULL;
    Sd2Player *p = player_alloc(s, rate);
    return p;
}

static inline void mix_advance(Sd2Mix *m) {                         /* end of a pass: the loop, else stop */
    while (m->on && m->pos >= m->len) {
        if (m->llen > 0 && m->ld) { m->pos -= m->len; m->d = m->ld; m->len = m->llen; }
        else { m->on = 0; break; }
        if (m->pos < 0) m->pos = 0;
    }
}

static inline double mix_step(const Sd2Player *p, const Sd2Mix *m) {
    double per = m->period ? m->period : 428;
    return PAULA_PAL / (per * p->rate);
}

int sd2_render(Sd2Player *p, int16_t *out, int n) {
    if (!p || p->ended) return 0;
    int f = 0;
    while (f < n) {
        if (p->tick_left <= 0) {
            for (int i = 0; i < 4; i++) mix_advance(&p->m[i]);          /* IsActive as of now */
            play_tick(p);
            if (p->ended) { memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(n - f)); return f; }
            p->tick_left += p->rate / SD2_TICK_HZ;
        }
        if (p->fast) {                                                  /* seeking: the voices only move on, silence */
            int k = (int)ceil(p->tick_left);
            if (k < 1) k = 1;
            if (k > n - f) k = n - f;
            for (int i = 0; i < 4; i++) {
                Sd2Mix *m = &p->m[i];
                if (!m->on) continue;
                m->pos += mix_step(p, m) * k;
                mix_advance(m);
                if (m->on && m->len > 0 && m->pos >= m->len) m->pos = fmod(m->pos, (double)m->len);
            }
            memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)k);
            p->tick_left -= k;
            f += k;
            continue;
        }
        p->tick_left -= 1.0;
        float l = 0, rr = 0;
        for (int i = 0; i < 4; i++) {
            Sd2Mix *m = &p->m[i];
            size_t si = p->sc ? (size_t)i * (size_t)p->sc_len + (p->sc_pos & (unsigned)(p->sc_len - 1)) : 0;
            if (p->sc) p->sc[si] = 0;
            mix_advance(m);
            if (!m->on || !m->d || m->len == 0) continue;
            uint32_t ip = (uint32_t)m->pos;
            if (ip >= m->len) ip = m->len - 1;
            int s0 = m->d[ip], s1;
            if (ip + 1 < m->len) s1 = m->d[ip + 1];
            else if (m->llen > 0 && m->ld) s1 = m->ld[0];
            else s1 = 0;
            float fr = (float)(m->pos - (double)ip);
            float smp = (float)s0 + (float)(s1 - s0) * fr;
            m->pos += mix_step(p, m);
            if (p->mute >> i & 1) continue;
            float vol = (float)m->volume / 4.0f;                        /* 0..256 -> Amiga 0..64 */
            float v = smp * vol * (10.0f / 7.0f);
            if (p->sc) p->sc[si] = smp * vol / (127.0f * 64.0f);
            if (pan4[i]) rr += v; else l += v;
        }
        { float a = l, b = rr; l = 0.866f * a + 0.5f * b; rr = 0.5f * a + 0.866f * b; }   /* uade's default stereo mix */
        p->sc_pos++;
        out[2 * f] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2 * f + 1] = (int16_t)(rr > 32767 ? 32767 : rr < -32768 ? -32768 : rr);
        f++;
    }
    return n;
}

const char *sd2_title(const Sd2Song *s) { (void)s; return ""; }    /* the format has no song name */
int sd2_subsongs(const Sd2Song *s) { return s ? 1 : 0; }
int sd2_channels(const Sd2Song *s) { (void)s; return 4; }
int sd2_positions(const Sd2Song *s, int sub) {
    if (!s || sub != 0) return 0;
    return s->number_of_positions;
}

void sd2_pos(const Sd2Player *p, int *position, int *row) {
    if (position) *position = p ? p->shown_pos : 0;
    if (row) *row = p ? p->shown_row : 0;
}

int sd2_rows(const Sd2Song *s, int position) {
    if (!s || position < 0 || position >= s->number_of_positions) return 0;
    return s->rows[position];
}

int sd2_cell(const Sd2Song *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg) {
    if (note) *note = 0;
    if (inst) *inst = 0;
    if (fx) *fx = 0;
    if (arg) *arg = 0;
    if (!s || sub != 0 || position < 0 || position >= s->number_of_positions || row < 0 || row >= 256 || ch < 0 || ch >= 4) return 0;
    /* decode the track as GetNote does, row by row from the start of the track (FindNote) */
    Sd2Player tmp;                                                      /* only s and the voice are used by get_note2 */
    memset(&tmp, 0, sizeof tmp);
    tmp.s = s;
    Sd2Voice *v = &tmp.v[0];
    const Sd2Seq *q = &s->sequences[ch][position];
    v->current_track = q->track_number;
    v->note_transpose = q->note_transpose;
    v->instrument_transpose = q->instrument_transpose;
    for (int r = 0; r <= row; r++) get_note2(&tmp, v);
    int nt = 0, in = 0;
    if (v->current_note != 0) nt = v->current_note + v->note_transpose;
    if (v->current_instrument != 0) {                                   /* as PlayVoice (only used with a note) */
        uint16_t i16 = (uint16_t)(v->current_instrument - 1 + v->instrument_transpose);
        if (i16 < s->num_instruments) in = i16 + 1;
    }
    if (note) *note = nt;
    if (inst) *inst = in;
    if (fx) *fx = v->current_effect;
    if (arg) *arg = v->current_effect_arg;
    return nt || in || v->current_effect || v->current_effect_arg;
}

void sd2_set_mute(Sd2Player *p, unsigned mask) { if (p) p->mute = mask; }
void sd2_set_scope(Sd2Player *p, float *buf, int len) {
    if (!p) return;
    if (!buf || len <= 0 || (len & (len - 1))) { p->sc = NULL; p->sc_len = 0; return; }
    p->sc = buf; p->sc_len = len;
}
unsigned sd2_scope_pos(const Sd2Player *p) { return p ? p->sc_pos : 0; }
void sd2_set_fast(Sd2Player *p, int on) { if (p) p->fast = on ? 1 : 0; }
