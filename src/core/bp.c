/* SoundMon / BP SoundMon (Amiga, Brian Postma): loader + replayer, a port of the NostalgicPlayer SoundMonWorker and
   SoundMonIdentifier (MIT). Module types as in the C#: "BPSM" = 1.0, "V.2" = 1.1/2.0 or 2.1 (decided from the effects
   used), "V.3" = 2.2. Paula-like mixer in the style of fc.c / sa.c. C# integer semantics are kept (byte/ushort/sbyte
   wrap, truncating casts). Like the originals, voice 0 of a step plays in Paula channel 3 (steps stored reversed).
   Timing: the worker never sets PlayingFrequency, so Play() runs at the default 50 Hz (PAL vblank). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bp.h"

#define PAULA_PAL 3546895.0
#define BP_TICK_HZ 50.0
#define BP_SCAN_TICKS (50 * 60 * 60)                                /* sub-song scan gives up after one hour */

enum { T10 = 10, T11 = 11, T21 = 21, T22 = 22 };                  /* ModuleType SoundMon10 / 11 / 21 / 22 */

/* Optional (Containers/Optional.cs) */
enum {
    OPT_ARPEGGIO_ONCE = 0x0, OPT_SET_VOLUME = 0x1, OPT_SET_SPEED = 0x2, OPT_FILTER = 0x3, OPT_PORT_UP = 0x4,
    OPT_PORT_DOWN = 0x5, OPT_SET_REP_COUNT = 0x6 /* = Vibrato */, OPT_DBRA_REP_COUNT = 0x7 /* = Jump */,
    OPT_SET_AUTO_SLIDE = 0x8, OPT_SET_ARPEGGIO = 0x9, OPT_TRANSPOSE = 0xa, OPT_CHANGE_FX = 0xb,
    OPT_CHANGE_INVERSION = 0xd, OPT_RESET_ADSR = 0xe, OPT_CHANGE_NOTE = 0xf
};

/* ---------------------------------------------------------------- tables (Tables.cs) */
static const uint16_t bp_periods[84] = {
    6848, 6464, 6080, 5760, 5440, 5120, 4832, 4576, 4320, 4064, 3840, 3616,
    3424, 3232, 3040, 2880, 2720, 2560, 2416, 2288, 2160, 2032, 1920, 1808,
    1712, 1616, 1520, 1440, 1360, 1280, 1208, 1144, 1080, 1016,  960,  904,
     856,  808,  760,  720,  680,  640,  604,  572,  540,  508,  480,  452,
     428,  404,  380,  360,  340,  320,  302,  286,  270,  254,  240,  226,
     214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
     107,  101,   95,   90,   85,   80,   76,   72,   68,   64,   60,   57
};
static const int16_t bp_vibrato[8] = { 0, 64, 128, 64, 0, -64, -128, -64 };
static const uint8_t pan4[4] = { 0, 1, 1, 0 };                    /* 0 = left, 1 = right */

/* ---------------------------------------------------------------- song */
typedef struct {
    int synth;
    uint16_t volume;
    /* sample */
    uint16_t length, loop_start, loop_length;
    int8_t *adr;
    /* synth */
    uint8_t wave_table; uint16_t wave_length;
    uint8_t adsr_control, adsr_table; uint16_t adsr_length; uint8_t adsr_speed;
    uint8_t lfo_control, lfo_table, lfo_depth; uint16_t lfo_length, lfo_delay; uint8_t lfo_speed;
    uint8_t eg_control, eg_table, eg_init_value; uint16_t eg_length, eg_delay; uint8_t eg_speed;
    uint8_t fx_control, fx_speed, fx_delay;
    uint8_t mod_control, mod_table, mod_speed, mod_delay; uint16_t mod_length;
} BpInst;
typedef struct { uint16_t track; int8_t st, tr; } BpStep;
typedef struct { int8_t note; uint8_t inst, opt, data; } BpTrack;

struct BpSong {
    int type;
    char title[26 * 2 + 1];
    uint8_t wave_num;
    uint16_t step_num, track_num;
    BpInst inst[15];
    BpStep *steps[4];                                               /* [voice][step], voice reversed */
    BpTrack *tracks;                                                /* [track_num][16] */
    int8_t *wave; int wave_len;                                     /* wave_num * 64 */
    int8_t byte_before;
    int8_t *after; size_t after_len;                                /* dataAfterWaveTables */
    int nsub; int *sub_start;
};

/* ---------------------------------------------------------------- player */
typedef struct {                                                    /* BpCurrent */
    int restart, dma_restart, synth_mode;
    int synth_offset;
    uint16_t period;
    uint8_t volume, instrument, note, arp_value;
    int8_t auto_slide;
    uint8_t auto_arp;
    uint16_t play_length, eg_ptr, lfo_ptr, adsr_ptr, mod_ptr, eg_count, lfo_count, adsr_count;
    uint8_t mod_count, fx_count, old_eg_value, eg_control, lfo_control, adsr_control, mod_control, fx_control, vibrato;
} BpCur;

typedef struct {
    const int8_t *d; uint32_t len; double pos; int on;
    const int8_t *ld; uint32_t llen;                                /* SetLoop: plays when the current pass ends */
    uint32_t period; int volume;
} BpMix;

struct BpPlayer {
    const BpSong *s;
    int rate, sub;
    /* GlobalPlayingInfo */
    uint16_t bp_step;
    uint8_t vib_index, arp_count, bp_count, bp_delay;
    int8_t st, tr;
    uint8_t bp_pat_count, bp_rep_count;
    int repeat_active, next_step_flag;
    uint8_t next_step;
    int8_t *wave;                                                   /* WaveTables (copy, changed by the synth effects) */
    int8_t synth_buf[4][32];
    BpCur cur[4];
    int end_reached, ended, failed, amiga_filter;
    uint8_t *visited; int own_visited;
    int shown_pos, shown_row;
    BpMix m[4];
    double tick_left;
    double lb0, lb1, lb2, la1, la2, lz[2][4];
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

/* ---------------------------------------------------------------- identify (SoundMonIdentifier) */
static int is_repeat_count(uint8_t d) { return (d >= 1 && d <= 4) || d == 0xff; }

static int is_soundmon21(const uint8_t *b, size_t n) {
    Rd r = { b, n, 30, 0 };
    int number_of_steps = rd_b16(&r);
    r.pos = 512;
    uint16_t *step_tracks = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)(number_of_steps * 4 + 1));
    uint8_t *used = (uint8_t *)calloc(65536, 1);
    int res = 1;
    if (!step_tracks || !used) goto done;
    uint16_t max_track = 0;
    for (int i = 0; i < number_of_steps * 4; i++) {
        uint16_t t = rd_b16(&r);
        r.pos += 2;                                                     /* Seek(2, Current) */
        step_tracks[i] = t; used[t] = 1;
        if (t > max_track) max_track = t;
    }
    if (r.eof) goto done;
    {
        size_t tlen = (size_t)max_track * 16 * 3;
        uint8_t *track_data = (uint8_t *)calloc(tlen ? tlen : 1, 1);
        if (!track_data) goto done;
        int tracks_read = (int)(rd_read(&r, track_data, tlen) / (16 * 3));
        int vibrato_votes = 0, repeat_votes = 0, repeat_count_set = 0;
        /* C#: for (ushort i = 1; i <= tracksRead; ...) would never end with tracksRead = 65535 (int loop here) */
        for (int i = 1; i <= tracks_read; i++) {
            if (!used[i]) continue;
            int offset = (i - 1) * 16 * 3;
            for (int j = 0; j < 16; j++) {
                uint8_t opt = track_data[offset + j * 3 + 1] & 0x0f, od = track_data[offset + j * 3 + 2];
                if (opt >= OPT_RESET_ADSR) { free(track_data); goto done; }
                if (opt == OPT_SET_REP_COUNT && od != 0 && !is_repeat_count(od)) vibrato_votes++;
            }
        }
        for (int i = 0; i < number_of_steps; i++)
            for (int j = 0; j < 16; j++)
                for (int k = 0; k < 4; k++) {
                    uint16_t tn = step_tracks[i * 4 + k];
                    if (tn == 0 || tn > tracks_read) continue;
                    int offset = ((tn - 1) * 16 + j) * 3;
                    uint8_t opt = track_data[offset + 1] & 0x0f, od = track_data[offset + 2];
                    if (opt == OPT_SET_REP_COUNT && is_repeat_count(od)) repeat_count_set = 1;
                    else if (opt == OPT_DBRA_REP_COUNT && repeat_count_set) { repeat_votes++; repeat_count_set = 0; }
                }
        free(track_data);
        res = repeat_votes == 0 || repeat_votes < vibrato_votes;
    }
done:
    free(step_tracks); free(used);
    return res;
}

static int test_module(const uint8_t *b, size_t n) {
    if (!b || n < 512) return 0;
    if (!memcmp(b + 26, "BPSM", 4)) return T10;
    if (!memcmp(b + 26, "V.2", 3)) return is_soundmon21(b, n) ? T21 : T11;
    if (!memcmp(b + 26, "V.3", 3)) return T22;
    return 0;
}

int bp_probe(const uint8_t *b, size_t n) { return test_module(b, n) != 0; }

/* ---------------------------------------------------------------- Load */
void bp_free(BpSong *s) {
    if (!s) return;
    for (int i = 0; i < 15; i++) free(s->inst[i].adr);
    for (int i = 0; i < 4; i++) free(s->steps[i]);
    free(s->tracks); free(s->wave); free(s->after); free(s->sub_start); free(s);
}

static void read_title(BpSong *s, const uint8_t *b) {               /* ReadString(Amiga, 26): Latin-1 -> UTF-8, ends at NUL */
    char *o = s->title;
    for (int i = 0; i < 26 && b[i]; i++) {
        uint8_t c = b[i];
        if (c < 0x80) *o++ = (char)c;
        else { *o++ = (char)(0xc0 | c >> 6); *o++ = (char)(0x80 | (c & 0x3f)); }
    }
    *o = 0;
}

static int load(BpSong *s, const uint8_t *b, size_t n) {
    Rd r = { b, n, 0, 0 };
    int i, j;
    read_title(s, b);
    r.pos = 26;
    if (s->type == T10) { r.pos += 4; s->wave_num = 0; }
    else { r.pos += 3; s->wave_num = rd_u8(&r); }
    s->step_num = rd_b16(&r);
    if (r.eof) return 0;

    for (i = 0; i < 15; i++) {
        BpInst *in = &s->inst[i];
        if (s->type != T10) {
            if (rd_u8(&r) == 0xff) {
                in->synth = 1;
                if (s->type == T22) {
                    in->wave_table = rd_u8(&r);
                    in->wave_length = (uint16_t)(rd_b16(&r) * 2);
                    in->adsr_control = rd_u8(&r); in->adsr_table = rd_u8(&r);
                    in->adsr_length = rd_b16(&r); in->adsr_speed = rd_u8(&r);
                    in->lfo_control = rd_u8(&r); in->lfo_table = rd_u8(&r); in->lfo_depth = rd_u8(&r);
                    in->lfo_length = rd_b16(&r); in->lfo_delay = rd_u8(&r); in->lfo_speed = rd_u8(&r);
                    in->eg_control = rd_u8(&r); in->eg_table = rd_u8(&r); in->eg_init_value = 0;
                    in->eg_length = rd_b16(&r); in->eg_delay = rd_u8(&r); in->eg_speed = rd_u8(&r);
                    in->fx_control = rd_u8(&r); in->fx_speed = rd_u8(&r); in->fx_delay = rd_u8(&r);
                    in->mod_control = rd_u8(&r); in->mod_table = rd_u8(&r); in->mod_speed = rd_u8(&r); in->mod_delay = rd_u8(&r);
                    in->volume = rd_u8(&r);
                    in->mod_length = rd_b16(&r);
                } else {
                    in->wave_table = rd_u8(&r);
                    in->wave_length = (uint16_t)(rd_b16(&r) * 2);
                    in->adsr_control = rd_u8(&r); in->adsr_table = rd_u8(&r);
                    in->adsr_length = rd_b16(&r); in->adsr_speed = rd_u8(&r);
                    in->lfo_control = rd_u8(&r); in->lfo_table = rd_u8(&r); in->lfo_depth = rd_u8(&r);
                    in->lfo_length = rd_b16(&r); in->lfo_delay = rd_b16(&r); in->lfo_speed = rd_u8(&r);
                    in->eg_control = rd_u8(&r); in->eg_table = rd_u8(&r); in->eg_init_value = rd_u8(&r);
                    in->eg_length = rd_b16(&r); in->eg_delay = rd_b16(&r); in->eg_speed = rd_u8(&r);
                    in->fx_control = 0; in->fx_speed = 1; in->fx_delay = 0;
                    in->mod_control = 0; in->mod_table = 0; in->mod_speed = 1; in->mod_delay = 0;
                    in->volume = rd_u8(&r);
                    in->mod_length = 0;
                    r.pos += 6;                                         /* Seek(6, Current) */
                }
                if (r.eof) return 0;
                continue;
            }
            r.pos -= 1;                                                 /* sample, seek back */
        }
        in->synth = 0;
        r.pos += 24;                                                    /* name (UI only) */
        in->length = (uint16_t)(rd_b16(&r) * 2);
        in->loop_start = rd_b16(&r);
        in->loop_length = (uint16_t)(rd_b16(&r) * 2);
        in->volume = rd_b16(&r);
        in->adr = NULL;
        if (in->loop_start + in->loop_length > in->length)            /* fix for Karate.bp */
            in->loop_length = (uint16_t)(in->length - in->loop_start);
        if (r.eof) return 0;
    }

    for (j = 0; j < 4; j++) {
        s->steps[j] = (BpStep *)calloc((size_t)s->step_num + 1, sizeof(BpStep));
        if (!s->steps[j]) return 0;
    }
    s->track_num = 0;
    for (i = 0; i < s->step_num; i++) {
        for (j = 3; j >= 0; j--) {                                      /* first voice plays in the last channel */
            BpStep *st = &s->steps[j][i];
            st->track = rd_b16(&r);
            st->st = (int8_t)rd_u8(&r);
            st->tr = (int8_t)rd_u8(&r);
            if (st->track > s->track_num) s->track_num = st->track;
        }
        if (r.eof) return 0;
    }

    s->tracks = (BpTrack *)calloc((size_t)s->track_num * 16 + 1, sizeof(BpTrack));
    if (!s->tracks) return 0;
    for (i = 0; i < s->track_num; i++) {
        for (j = 0; j < 16; j++) {
            BpTrack *t = &s->tracks[(size_t)i * 16 + (size_t)j];
            t->note = (int8_t)rd_u8(&r);
            t->inst = rd_u8(&r);
            t->opt = t->inst & 0x0f;
            t->inst = (uint8_t)((t->inst & 0xf0) >> 4);
            t->data = rd_u8(&r);
        }
        if (r.eof) return 0;
    }
    /* the averaging effect on the first wave table reads the last byte of the tracks */
    s->byte_before = s->track_num > 0 ? (int8_t)s->tracks[((size_t)s->track_num - 1) * 16 + 15].data : 0;

    if (s->type != T10) {
        s->wave_len = s->wave_num * 64;
        s->wave = (int8_t *)calloc((size_t)s->wave_len + 1, 1);
        if (!s->wave) return 0;
        rd_read(&r, (uint8_t *)s->wave, (size_t)s->wave_len);
        if (r.eof) return 0;
    }

    size_t total = 0;
    for (i = 0; i < 15; i++) if (!s->inst[i].synth) total += s->inst[i].length;
    s->after = (int8_t *)malloc(total ? total : 1);
    if (!s->after) return 0;
    s->after_len = 0;
    for (i = 0; i < 15; i++) {
        BpInst *in = &s->inst[i];
        if (!in->synth && in->length != 0) {
            in->adr = (int8_t *)malloc(in->length);
            if (!in->adr) return 0;
            rd_read(&r, (uint8_t *)in->adr, in->length);
            if (r.eof) return 0;
            memcpy(s->after + s->after_len, in->adr, in->length);
            s->after_len += in->length;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------- mixer channel (IChannel) */
/* the C# mixer gets offsets / lengths outside the array in some broken cases (undefined there): clipped here */
static void ch_play_sample(BpMix *m, const int8_t *base, uint32_t base_len, uint32_t start, uint32_t len) {
    m->ld = NULL; m->llen = 0; m->pos = 0;
    if (!base || start >= base_len) { m->d = NULL; m->len = 0; m->on = 0; return; }
    if (len > base_len - start) len = base_len - start;
    m->d = base + start; m->len = len; m->on = len > 0;
}
static void ch_set_loop(BpMix *m, const int8_t *base, uint32_t base_len, uint32_t start, uint32_t len) {
    if (!base || start >= base_len) { m->ld = NULL; m->llen = 0; return; }
    if (len > base_len - start) len = base_len - start;
    m->ld = base + start; m->llen = len;
}
static void ch_mute(BpMix *m) { m->on = 0; }
static void ch_period(BpPlayer *p, int v, uint16_t period) { p->m[v].period = period; }   /* SetAmigaPeriod */

/* ---------------------------------------------------------------- helpers */
/* GetTableValue: reading outside the tables emulates the module memory around them */
static int8_t get_table_value(const BpPlayer *p, int offset) {
    const BpSong *s = p->s;
    if (offset < 0) return s->byte_before;
    if (offset >= s->wave_len) {
        size_t i = (size_t)(offset - s->wave_len);
        return i < s->after_len ? s->after[i] : 0;                     /* C#: dataAfterWaveTables[] throws past its end */
    }
    return p->wave[offset];
}
static void set_table_value(BpPlayer *p, int offset, int8_t v) {    /* C#: WaveTables[] write outside throws: skipped */
    if (offset >= 0 && offset < p->s->wave_len) p->wave[offset] = v;
}

static uint16_t get_period(int note) {
    int i = note + 36 - 1;
    if (i > 83) i = 83;
    if (i < 0) i = 0;
    return bp_periods[i];
}

static void set_volume(BpPlayer *p, int v, int volume) {
    p->m[v].volume = (uint16_t)(volume < 64 ? volume : 64);
}

static void mark_visited(BpPlayer *p, int pos) { if (pos >= 0 && pos < p->s->step_num) p->visited[pos] = 1; }
static int has_visited(const BpPlayer *p, int pos) { return pos >= 0 && pos < p->s->step_num ? p->visited[pos] : 0; }

/* ---------------------------------------------------------------- InitializeSound */
static void initialize_sound(BpPlayer *p, int start) {
    const BpSong *s = p->s;
    p->arp_count = 1; p->bp_count = 1; p->bp_delay = 6; p->bp_rep_count = 1;
    p->repeat_active = 0; p->vib_index = 0;
    p->bp_step = (uint16_t)start; p->bp_pat_count = 0;
    p->st = 0; p->tr = 0;
    p->next_step_flag = 0; p->next_step = 0;
    p->end_reached = 0;
    memset(p->cur, 0, sizeof p->cur);
    for (int i = 0; i < 4; i++) { p->cur[i].synth_offset = -1; p->cur[i].play_length = 1; }
    memset(p->synth_buf, 0, sizeof p->synth_buf);
    if (s->type != T10 && s->wave_len > 0) memcpy(p->wave, s->wave, (size_t)s->wave_len);
    p->shown_pos = start; p->shown_row = 0;
}

/* ---------------------------------------------------------------- synth */
static void save_synth_buffer(BpPlayer *p, int v, int wave_offset) {
    p->cur[v].synth_offset = wave_offset;
    for (int i = 0; i < 32; i++) p->synth_buf[v][i] = get_table_value(p, wave_offset + i);
}
static void restore_synth_buffer(BpPlayer *p, int v) {
    int wave_offset = p->cur[v].synth_offset;
    for (int i = 0; i < 32; i++) set_table_value(p, wave_offset + i, p->synth_buf[v][i]);
}

static uint16_t decrement_counter(const BpPlayer *p, uint16_t counter) {
    if (p->s->type == T22) return (uint8_t)(counter - 1);
    return (uint16_t)(counter - 1);
}

static void do_adsr(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    if (cur->adsr_control != 0) {
        cur->adsr_count = decrement_counter(p, cur->adsr_count);
        if (cur->adsr_count == 0) {
            cur->adsr_count = in->adsr_speed;
            int table_value = (uint8_t)(get_table_value(p, in->adsr_table * 64 + cur->adsr_ptr) + 128) >> 2;
            set_volume(p, v, (table_value * cur->volume) >> 6);
            cur->adsr_ptr++;
            if (cur->adsr_ptr == in->adsr_length) {
                cur->adsr_ptr = 0;
                if (cur->adsr_control == 1) cur->adsr_control = 0;
            }
        }
    }
}

static void do_lfo(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    if (cur->lfo_control != 0) {
        cur->lfo_count = decrement_counter(p, cur->lfo_count);
        if (cur->lfo_count == 0) {
            cur->lfo_count = in->lfo_speed;
            int table_value = get_table_value(p, in->lfo_table * 64 + cur->lfo_ptr);
            if (in->lfo_depth != 0) table_value /= in->lfo_depth;
            ch_period(p, v, (uint16_t)(cur->period + table_value));
            cur->lfo_ptr++;
            if (cur->lfo_ptr == in->lfo_length) {
                cur->lfo_ptr = 0;
                if (cur->lfo_control == 1) cur->lfo_control = 0;
            }
        }
    }
}

static void do_eg(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    if (cur->eg_control != 0) {
        cur->eg_count = decrement_counter(p, cur->eg_count);
        if (cur->eg_count == 0 && cur->synth_offset >= 0) {
            cur->eg_count = in->eg_speed;
            int old_value = cur->old_eg_value;
            int new_value = (uint8_t)(get_table_value(p, in->eg_table * 64 + cur->eg_ptr) + 128) >> 3;
            cur->old_eg_value = (uint8_t)new_value;
            if (new_value != old_value) {
                const int8_t *source = p->synth_buf[v];
                int source_offset = old_value;
                int dest_offset = cur->synth_offset + old_value;
                if (new_value < old_value) {
                    for (int j = old_value - new_value; j > 0; j--) {
                        --dest_offset; --source_offset;
                        set_table_value(p, dest_offset, source_offset < 32 ? source[source_offset] : 0);
                    }
                } else {
                    /* OldEgValue is <= 31 when the EG runs, so source[] (32 bytes) stays in range; guarded anyway */
                    for (int j = new_value - old_value; j > 0; j--) {
                        set_table_value(p, dest_offset, (int8_t)-(source_offset < 32 ? source[source_offset] : 0));
                        dest_offset++; source_offset++;
                    }
                }
            }
            cur->eg_ptr++;
            if (cur->eg_ptr == in->eg_length) {
                cur->eg_ptr = 0;
                if (cur->eg_control == 1) cur->eg_control = 0;
            }
        }
    }
}

static void averaging(BpPlayer *p, int v) {
    int synth_offset = p->cur[v].synth_offset;
    int last_val = get_table_value(p, synth_offset - 1);
    for (int i = 0; i < 32 / 2; i++) {
        int new_val = (get_table_value(p, synth_offset + 1) + last_val) >> 1;
        set_table_value(p, synth_offset++, (int8_t)new_val);
        last_val = (get_table_value(p, synth_offset + 1) + new_val) >> 1;
        set_table_value(p, synth_offset++, (int8_t)last_val);
    }
}

static void transform_byte(BpPlayer *p, int8_t source_val, int dest_offset, int8_t delta) {
    int8_t dest_val = get_table_value(p, dest_offset);
    if (source_val < dest_val) set_table_value(p, dest_offset, (int8_t)(dest_val - delta));
    else if (source_val > dest_val) set_table_value(p, dest_offset, (int8_t)(dest_val + delta));
}
static void transform2(BpPlayer *p, int v, int8_t delta) {
    int source_offset = 31, dest_offset = p->cur[v].synth_offset;
    for (int i = 0; i < 32; i++) { transform_byte(p, p->synth_buf[v][source_offset], dest_offset, delta); source_offset--; dest_offset++; }
}
static void transform3(BpPlayer *p, int v, int8_t delta) {
    int source_offset = 0, dest_offset = p->cur[v].synth_offset;
    for (int i = 0; i < 32; i++) { transform_byte(p, p->synth_buf[v][source_offset], dest_offset, delta); source_offset++; dest_offset++; }
}
static void transform4(BpPlayer *p, int v, int8_t delta) {
    int source_offset = p->cur[v].synth_offset + 64, dest_offset = p->cur[v].synth_offset;
    for (int i = 0; i < 32; i++) { transform_byte(p, get_table_value(p, source_offset), dest_offset, delta); source_offset++; dest_offset++; }
}

static void do_fx(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    switch (cur->fx_control) {
        case 1:
            cur->fx_count--;
            if (cur->fx_count == 0) { cur->fx_count = in->fx_speed; averaging(p, v); }
            break;
        case 2: transform2(p, v, (int8_t)in->fx_speed); break;
        case 3: case 5: transform3(p, v, (int8_t)in->fx_speed); break;
        case 4: transform4(p, v, (int8_t)in->fx_speed); break;
        case 6:
            cur->fx_count--;
            if (cur->fx_count == 0) {
                cur->fx_control = 0;
                cur->fx_count = 1;
                for (int i = 0; i < 32; i++) set_table_value(p, cur->synth_offset + i, get_table_value(p, cur->synth_offset + 64 + i));
            }
            break;
    }
}

static void do_mod(BpPlayer *p, BpCur *cur, const BpInst *in) {
    if (cur->mod_control != 0) {
        cur->mod_count--;
        if (cur->mod_count == 0) {
            cur->mod_count = in->mod_speed;
            set_table_value(p, cur->synth_offset + 32, get_table_value(p, in->mod_table * 64 + cur->mod_ptr));
            cur->mod_ptr++;
            if (cur->mod_ptr == in->mod_length) {
                cur->mod_ptr = 0;
                if (cur->mod_control == 1) cur->mod_control = 0;
            }
        }
    }
}

static void bp_synth(BpPlayer *p) {
    for (int i = 0; i < 4; i++) {
        BpCur *cur = &p->cur[i];
        if (cur->synth_mode) {
            /* instruments[cur.Instrument - 1]: Instrument is never 0 here (synth mode needs an instrument) */
            if (cur->instrument == 0 || cur->instrument > 15) continue;
            const BpInst *in = &p->s->inst[cur->instrument - 1];
            if (in->synth) {
                do_adsr(p, i, cur, in);
                do_lfo(p, i, cur, in);
                if (p->s->type == T22) {
                    if (cur->synth_offset >= 0) { do_eg(p, i, cur, in); do_fx(p, i, cur, in); do_mod(p, cur, in); }
                } else do_eg(p, i, cur, in);
            }
        }
    }
}

/* ---------------------------------------------------------------- retrig */
static void start_synth_sound(BpPlayer *p, int v, BpCur *cur, const BpInst *in, int wave_offset) {
    cur->play_length = (uint16_t)(in->wave_length / 2);
    if (cur->dma_restart) {
        BpMix *c = &p->m[v];
        ch_play_sample(c, p->wave, (uint32_t)p->s->wave_len, (uint32_t)wave_offset, in->wave_length);
        ch_set_loop(c, p->wave, (uint32_t)p->s->wave_len, (uint32_t)wave_offset, in->wave_length);
    }
}

static void initialize_synth_volume(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    if (in->adsr_control != 0) {
        int table_value = (uint8_t)(get_table_value(p, in->adsr_table * 64) + 128) >> 2;
        if (cur->volume == 0xff) cur->volume = (uint8_t)in->volume;
        set_volume(p, v, (table_value * cur->volume) >> 6);
    } else set_volume(p, v, cur->volume == 0xff ? in->volume : cur->volume);
}

static void play_synth(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    cur->synth_mode = 1;
    cur->eg_ptr = 0; cur->lfo_ptr = 0; cur->adsr_ptr = 0;
    cur->eg_count = (uint16_t)(in->eg_delay + 1);
    cur->lfo_count = (uint16_t)(in->lfo_delay + 1);
    cur->adsr_count = 1;
    cur->eg_control = in->eg_control;
    cur->lfo_control = in->lfo_control;
    cur->adsr_control = in->adsr_control;
    cur->old_eg_value = in->eg_init_value;
    int wave_offset = in->wave_table * 64;
    start_synth_sound(p, v, cur, in, wave_offset);
    initialize_synth_volume(p, v, cur, in);
    if (p->s->type == T11 || in->eg_control != 0) save_synth_buffer(p, v, wave_offset);
    if (in->eg_control != 0) {
        if (in->eg_init_value != 0) {
            int count = in->eg_init_value / 8;
            cur->old_eg_value = (uint8_t)count;
            for (int i = 0; i < count; i++) set_table_value(p, wave_offset + i, (int8_t)-get_table_value(p, wave_offset + i));
        }
    }
}

static void play_synth22(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    cur->synth_mode = 1;
    cur->eg_ptr = 0; cur->lfo_ptr = 0; cur->adsr_ptr = 0; cur->mod_ptr = 0;
    cur->eg_count = (uint8_t)(in->eg_delay + 1);
    cur->lfo_count = (uint8_t)(in->lfo_delay + 1);
    cur->adsr_count = 1;
    cur->mod_count = (uint8_t)(in->mod_delay + 1);
    cur->fx_count = (uint8_t)(in->fx_delay + 1);
    cur->fx_control = in->fx_control;
    cur->eg_control = in->eg_control;
    cur->lfo_control = in->lfo_control;
    cur->adsr_control = in->adsr_control;
    cur->mod_control = in->mod_control;
    cur->old_eg_value = 0;
    int wave_offset = in->wave_table * 64;
    start_synth_sound(p, v, cur, in, wave_offset);
    initialize_synth_volume(p, v, cur, in);
    if (cur->eg_control != 0 || cur->mod_control != 0 || cur->fx_control != 0) save_synth_buffer(p, v, wave_offset);
}

static void play_sample(BpPlayer *p, int v, BpCur *cur, const BpInst *in) {
    const BpSong *s = p->s;
    BpMix *c = &p->m[v];
    if (s->type != T10) {
        cur->synth_mode = 0;
        cur->lfo_control = 0;
        if (s->type != T22) { cur->adsr_control = 0; cur->synth_offset = -1; }
    }
    int has_loop = in->loop_length > 2;
    cur->play_length = has_loop ? (uint16_t)(in->loop_length / 2) : (uint16_t)1;
    if (in->adr == NULL) { ch_mute(c); return; }
    if (cur->dma_restart) {
        if (!has_loop) ch_play_sample(c, in->adr, in->length, 0, in->length);
        else {
            if (s->type == T10 || s->type == T11) ch_play_sample(c, in->adr, in->length, in->loop_start, in->loop_length);
            else ch_play_sample(c, in->adr, in->length, 0, (uint32_t)(in->loop_start + in->loop_length));
            ch_set_loop(c, in->adr, in->length, in->loop_start, in->loop_length);
        }
    }
    set_volume(p, v, cur->volume == 0xff ? in->volume : cur->volume);
}

static void play_it(BpPlayer *p, int v) {
    BpCur *cur = &p->cur[v];
    cur->restart = 0;
    if (p->s->type == T11 && cur->synth_offset >= 0) restore_synth_buffer(p, v);
    ch_period(p, v, cur->period);
    if (cur->instrument == 0) return;
    const BpInst *in = &p->s->inst[cur->instrument - 1];
    if (in->synth) {
        if (p->s->type == T22) play_synth22(p, v, cur, in);
        else play_synth(p, v, cur, in);
    } else play_sample(p, v, cur, in);
    cur->dma_restart = 0;
}

static void stop_voices_without_retrig(BpPlayer *p) {
    for (int i = 0; i < 4; i++) {
        BpCur *cur = &p->cur[i];
        if (cur->dma_restart) { cur->dma_restart = 0; ch_mute(&p->m[i]); }
    }
}

/* ---------------------------------------------------------------- optionals */
static void reset_adsr(BpCur *cur) {
    cur->adsr_ptr = 0;
    if (cur->adsr_control == 0) cur->adsr_control = 1;
}

static void do_arpeggio(BpPlayer *p, int v, BpCur *cur) {
    int note = cur->note;
    if (p->arp_count == 0) note += ((cur->arp_value & 0xf0) >> 4) + ((cur->auto_arp & 0xf0) >> 4);
    else if (p->arp_count == 1) note += (cur->arp_value & 0x0f) + (cur->auto_arp & 0x0f);
    uint16_t period = get_period((int8_t)note);
    ch_period(p, v, period);
    if (p->s->type == T21 || p->s->type == T22) cur->period = period;
}

static int do_optionals(BpPlayer *p, int v, uint8_t optional, uint8_t data) {
    const BpSong *s = p->s;
    BpCur *cur = &p->cur[v];
    int is_v1 = s->type == T10 || s->type == T11;
    switch (optional) {
        case OPT_ARPEGGIO_ONCE: cur->arp_value = data; break;
        case OPT_SET_VOLUME:
            cur->volume = data;
            if (s->type != T22 || !cur->synth_mode) set_volume(p, v, data);
            break;
        case OPT_SET_SPEED: p->bp_count = data; p->bp_delay = data; break;
        case OPT_FILTER: p->amiga_filter = data != 0; break;
        case OPT_PORT_UP:
            cur->period = (uint16_t)(cur->period - data);
            cur->arp_value = 0;
            if (s->type == T10) ch_period(p, v, cur->period);
            break;
        case OPT_PORT_DOWN:
            cur->period = (uint16_t)(cur->period + data);
            cur->arp_value = 0;
            if (s->type == T10) ch_period(p, v, cur->period);
            break;
        case OPT_SET_REP_COUNT:
            if (is_v1) {
                if (p->repeat_active) { p->repeat_active = 0; p->end_reached = 1; }
                p->bp_rep_count = data;
            } else cur->vibrato = data;
            break;
        case OPT_DBRA_REP_COUNT:
            if (is_v1) {
                p->bp_rep_count--;
                if (p->bp_rep_count != 0) {
                    p->bp_step = data;
                    p->repeat_active = p->bp_rep_count < 15;
                    return 1;
                }
                p->repeat_active = 0;
            } else { p->next_step = data; p->next_step_flag = 1; }
            break;
        case OPT_SET_AUTO_SLIDE: cur->auto_slide = (int8_t)data; break;
        case OPT_SET_ARPEGGIO:
            cur->auto_arp = data;
            if (!is_v1) reset_adsr(cur);
            break;
        case OPT_CHANGE_FX: if (s->type == T22) cur->fx_control = data; break;
        case OPT_CHANGE_INVERSION:
            if (s->type == T22) { cur->auto_arp = data; cur->fx_control = (uint8_t)(cur->fx_control ^ 1); reset_adsr(cur); }
            break;
        case OPT_RESET_ADSR: if (!is_v1) { cur->auto_arp = data; reset_adsr(cur); } break;
        case OPT_CHANGE_NOTE: if (!is_v1) cur->auto_arp = data; break;
    }
    return 0;
}

/* ---------------------------------------------------------------- BpNext */
static void bp_next(BpPlayer *p) {
    const BpSong *s = p->s;
    int is_v1 = s->type == T10 || s->type == T11;
    int position_changed = 0;
    int no_retrig_from = s->type == T22 ? OPT_CHANGE_INVERSION : s->type == T21 ? OPT_RESET_ADSR : 0x10;

    p->shown_pos = p->bp_step; p->shown_row = p->bp_pat_count;
    for (int i = 0; i < 4; i++) {
        BpCur *cur = &p->cur[i];
        /* steps[i][BpStep] past the step list (a 1.x repeat jump beyond the end) and tracks[TrackNumber - 1] with
           track 0 throw in C#: the song ends here */
        if (p->bp_step >= s->step_num) { p->failed = 1; return; }
        const BpStep *step = &s->steps[i][p->bp_step];
        p->st = step->st; p->tr = step->tr;
        if (step->track == 0 || step->track > s->track_num) { p->failed = 1; return; }
        const BpTrack *t = &s->tracks[((size_t)step->track - 1) * 16 + p->bp_pat_count];
        int8_t note = t->note;
        if (note != 0) {
            cur->auto_slide = 0;
            cur->auto_arp = 0;
            if (s->type == T22) cur->vibrato = 0;
            if (t->opt != OPT_TRANSPOSE || (t->data & 0xf0) == 0) note = (int8_t)(note + p->tr);
            cur->note = (uint8_t)note;
            cur->period = get_period(note);
            if (t->opt < no_retrig_from) { cur->restart = 1; cur->volume = 0xff; }
            uint8_t inst = t->inst;
            if (inst == 0) inst = cur->instrument;
            if (inst != 0 && (t->opt != OPT_TRANSPOSE || (t->data & 0x0f) == 0)) {
                inst = (uint8_t)(inst + (uint8_t)p->st);
                if (inst < 1 || inst > 15) inst = (uint8_t)(inst - (uint8_t)p->st);
            }
            int restart_sample;
            if (is_v1) restart_sample = cur->play_length == 1 || cur->instrument != inst;
            else if (s->type == T22 && t->opt >= OPT_CHANGE_INVERSION) restart_sample = 0;
            else restart_sample = !cur->synth_mode || cur->instrument != inst;
            if (restart_sample) { cur->instrument = inst; cur->dma_restart = 1; }
        }
        if (do_optionals(p, i, t->opt, t->data)) position_changed = 1;
    }

    if (p->next_step_flag) {
        p->next_step_flag = 0;
        p->bp_pat_count = 0;
        p->bp_step = (uint16_t)((p->bp_step & 0xff00) | p->next_step);
        if (p->bp_step >= s->step_num) p->bp_step = 0;
        position_changed = 1;
    } else {
        p->bp_pat_count++;
        if (p->bp_pat_count == 16) {
            p->bp_pat_count = 0;
            p->bp_step++;
            if (p->bp_step == s->step_num) p->bp_step = 0;
            position_changed = 1;
        }
    }

    if (position_changed && !p->repeat_active) {
        if (has_visited(p, p->bp_step)) p->end_reached = 1;
        mark_visited(p, p->bp_step);
    }
}

/* ---------------------------------------------------------------- BpMusic11 / BpMusic21 */
static void bp_music11(BpPlayer *p) {
    if (p->s->type == T11) bp_synth(p);
    p->arp_count--;
    for (int i = 0; i < 4; i++) {
        BpCur *cur = &p->cur[i];
        cur->period = (uint16_t)(cur->period + cur->auto_slide);
        if (cur->lfo_control == 0) ch_period(p, i, cur->period);
        if (cur->arp_value != 0 || cur->auto_arp != 0) do_arpeggio(p, i, cur);
    }
    if (p->arp_count == 0) p->arp_count = 3;
    p->bp_count--;
    if (p->bp_count == 0) {
        p->bp_count = p->bp_delay;
        bp_next(p);
        if (p->failed) return;
        for (int i = 0; i < 4; i++) if (p->cur[i].restart) play_it(p, i);
        stop_voices_without_retrig(p);
    }
}

static void bp_music21(BpPlayer *p) {
    p->arp_count = (uint8_t)((p->arp_count - 1) & 3);
    p->vib_index = (uint8_t)((p->vib_index + 1) & 7);
    for (int i = 0; i < 4; i++) {
        BpCur *cur = &p->cur[i];
        cur->period = (uint16_t)(cur->period + cur->auto_slide);
        if (cur->vibrato != 0) ch_period(p, i, (uint16_t)(cur->period + (bp_vibrato[p->vib_index] / cur->vibrato)));
        else ch_period(p, i, cur->period);
        if (cur->arp_value != 0 || cur->auto_arp != 0) do_arpeggio(p, i, cur);
    }
    bp_synth(p);
    p->bp_count--;
    if (p->bp_count == 0) {
        p->bp_count = p->bp_delay;
        bp_next(p);
        if (p->failed) return;
        for (int i = 0; i < 4; i++) {
            BpCur *cur = &p->cur[i];
            if (cur->restart && cur->synth_offset >= 0) {
                restore_synth_buffer(p, i);
                if (p->s->type == T22) cur->synth_offset = -1;
            }
        }
        for (int i = 0; i < 4; i++) if (p->cur[i].restart) play_it(p, i);
        stop_voices_without_retrig(p);
    }
}

/* ---------------------------------------------------------------- Play() */
static void play_tick(BpPlayer *p) {
    if (p->s->type == T10 || p->s->type == T11) bp_music11(p); else bp_music21(p);
    if (p->failed) { p->ended = 1; return; }
    if (p->end_reached) {                                               /* OnEndReached: the song is over (no restart) */
        p->ended = 1;
        p->end_reached = 0;
        mark_visited(p, p->bp_step);
    }
}

/* ---------------------------------------------------------------- player setup */
static int player_init(BpPlayer *p, const BpSong *s, int rate, uint8_t *visited) {
    memset(p, 0, sizeof *p);
    p->s = s; p->rate = rate;
    p->wave = (int8_t *)calloc((size_t)s->wave_len + 1, 1);
    if (!p->wave) return 0;
    if (visited) p->visited = visited;
    else {
        p->visited = (uint8_t *)calloc((size_t)s->step_num + 1, 1);
        if (!p->visited) { free(p->wave); return 0; }
        p->own_visited = 1;
    }
    return 1;
}
static void player_done(BpPlayer *p) {
    free(p->wave);
    if (p->own_visited) free(p->visited);
}

/* Sub-songs as ModulePlayerWithPositionDurationAgentBase finds them: play from a start position until the end is
   reached (a visited position, or a repeat count set inside a running loop), then start again at the first position
   not visited yet. The positions depend on all voices (jumps, repeat loops), so the whole player runs (no mixing). */
static int find_subsongs(BpSong *s) {
    uint8_t *vis = (uint8_t *)calloc((size_t)s->step_num + 1, 1);
    s->sub_start = (int *)malloc(sizeof(int) * ((size_t)s->step_num + 1));
    s->nsub = 0;
    if (!vis || !s->sub_start) { free(vis); return 0; }
    BpPlayer *p = (BpPlayer *)malloc(sizeof *p);
    if (!p || !player_init(p, s, 44100, vis)) { free(p); free(vis); return 0; }
    int start = 0;
    for (;;) {
        s->sub_start[s->nsub++] = start;
        initialize_sound(p, start);                                     /* InitDuration */
        mark_visited(p, start);
        p->ended = 0; p->failed = 0;
        for (long t = 0; t < BP_SCAN_TICKS && !p->ended; t++) play_tick(p);
        while (start < s->step_num && vis[start]) start++;
        if (start >= s->step_num) break;
    }
    player_done(p); free(p); free(vis);
    return s->nsub > 0;
}

BpSong *bp_load(const uint8_t *b, size_t n, int *err) {
    if (err) *err = 0;
    int type = test_module(b, n);
    if (!type) { if (err) *err = 1; return NULL; }
    BpSong *s = (BpSong *)calloc(1, sizeof *s);
    if (!s) { if (err) *err = 2; return NULL; }
    s->type = type;
    /* no steps: steps[i][0] in the first BpNext throws in C# */
    if (!load(s, b, n) || s->step_num == 0 || !find_subsongs(s)) { bp_free(s); if (err) *err = 3; return NULL; }
    return s;
}

/* ---------------------------------------------------------------- public */
BpPlayer *bp_new(const BpSong *s, int sub, int rate) {
    if (!s || rate <= 0 || sub < 0 || sub >= s->nsub) return NULL;
    BpPlayer *p = (BpPlayer *)malloc(sizeof *p);
    if (!p) return NULL;
    if (!player_init(p, s, rate, NULL)) { free(p); return NULL; }
    p->sub = sub;
    { double w0 = 6.283185307179586 * 3275.0 / rate, al = sin(w0) / (2.0 * 0.7071067811865476), co = cos(w0), a0 = 1.0 + al;
      p->lb0 = (1.0 - co) / 2.0 / a0; p->lb1 = (1.0 - co) / a0; p->lb2 = p->lb0; p->la1 = -2.0 * co / a0; p->la2 = (1.0 - al) / a0; }
    int start = s->sub_start[sub];
    initialize_sound(p, start);
    mark_visited(p, start);                                             /* the start position counts as visited */
    return p;
}

void bp_player_free(BpPlayer *p) { if (p) { player_done(p); free(p); } }

static inline void mix_advance(BpMix *m) {                          /* end of a pass: the loop, else stop */
    while (m->on && m->pos >= m->len) {
        if (m->llen > 0 && m->ld) { m->pos -= m->len; m->d = m->ld; m->len = m->llen; }
        else { m->on = 0; break; }
        if (m->pos < 0) m->pos = 0;
    }
}

int bp_render(BpPlayer *p, int16_t *out, int n) {
    if (!p || p->ended) return 0;
    for (int f = 0; f < n; f++) {
        if (p->tick_left <= 0) {
            play_tick(p);
            if (p->ended) { memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(n - f)); return f; }
            p->tick_left += p->rate / BP_TICK_HZ;
        }
        p->tick_left -= 1.0;
        float l = 0, rr = 0;
        for (int i = 0; i < 4; i++) {
            BpMix *m = &p->m[i];
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
            double per = m->period ? m->period : 428;
            m->pos += PAULA_PAL / (per * p->rate);
            if (p->mute >> i & 1) continue;
            float v = smp * (float)m->volume * (10.0f / 7.0f);
            if (p->sc) p->sc[si] = smp * (float)m->volume / (127.0f * 64.0f);
            if (pan4[i]) rr += v; else l += v;
        }
        {   /* LED filter (optional 3: data != 0 = on) */
            double x[2] = { l, rr };
            for (int sd = 0; sd < 2; sd++) {
                double *z = p->lz[sd];
                if (p->amiga_filter) {
                    double y = p->lb0 * x[sd] + p->lb1 * z[0] + p->lb2 * z[1] - p->la1 * z[2] - p->la2 * z[3];
                    z[1] = z[0]; z[0] = x[sd]; z[3] = z[2]; z[2] = y; x[sd] = y;
                } else { z[1] = z[0]; z[0] = x[sd]; z[3] = z[2]; z[2] = x[sd]; }
            }
            l = (float)x[0]; rr = (float)x[1];
        }
        { float a = l, b = rr; l = 0.866f * a + 0.5f * b; rr = 0.5f * a + 0.866f * b; }   /* uade's default stereo mix */
        p->sc_pos++;
        out[2 * f] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2 * f + 1] = (int16_t)(rr > 32767 ? 32767 : rr < -32768 ? -32768 : rr);
    }
    return n;
}

int bp_version(const BpSong *s) { return s ? s->type : 0; }
const char *bp_title(const BpSong *s) { return s ? s->title : ""; }
int bp_subsongs(const BpSong *s) { return s ? s->nsub : 0; }
int bp_channels(const BpSong *s) { (void)s; return 4; }
int bp_rows(const BpSong *s) { (void)s; return 16; }
int bp_positions(const BpSong *s, int sub) {
    if (!s || sub < 0 || sub >= s->nsub) return 0;
    return s->step_num;
}

void bp_pos(const BpPlayer *p, int *position, int *row) {
    if (position) *position = p ? p->shown_pos : 0;
    if (row) *row = p ? p->shown_row : 0;
}

int bp_cell(const BpSong *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg) {
    if (!s || sub < 0 || sub >= s->nsub || position < 0 || position >= s->step_num || row < 0 || row >= 16 || ch < 0 || ch >= 4) return 0;
    const BpStep *st = &s->steps[ch][position];
    if (st->track == 0 || st->track > s->track_num) return 0;
    const BpTrack *t = &s->tracks[((size_t)st->track - 1) * 16 + (size_t)row];
    int nt = 0, in = 0;
    if (t->note != 0) {                                                 /* as BpNext */
        int8_t n8 = t->note;
        if (t->opt != OPT_TRANSPOSE || (t->data & 0xf0) == 0) n8 = (int8_t)(n8 + st->tr);
        nt = n8;
        uint8_t i8 = t->inst;
        if (i8 != 0 && (t->opt != OPT_TRANSPOSE || (t->data & 0x0f) == 0)) {
            i8 = (uint8_t)(i8 + (uint8_t)st->st);
            if (i8 < 1 || i8 > 15) i8 = (uint8_t)(i8 - (uint8_t)st->st);
        }
        in = i8;
    }
    if (note) *note = nt;
    if (inst) *inst = in;
    if (fx) *fx = t->opt;
    if (arg) *arg = t->data;
    return 1;
}

void bp_set_mute(BpPlayer *p, unsigned mask) { if (p) p->mute = mask; }
void bp_set_scope(BpPlayer *p, float *buf, int len) {
    if (!p) return;
    if (!buf || len <= 0 || (len & (len - 1))) { p->sc = NULL; p->sc_len = 0; return; }
    p->sc = buf; p->sc_len = len;
}
unsigned bp_scope_pos(const BpPlayer *p) { return p ? p->sc_pos : 0; }
