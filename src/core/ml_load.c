/* Musicline Editor loader (see ml.h) */
#include <stdlib.h>
#include <string.h>
#include "ml.h"

typedef struct { const uint8_t *b; size_t n, p; int eof; } R;
static uint8_t u8(R *r) { if (r->p >= r->n) { r->eof = 1; return 0; } return r->b[r->p++]; }
static uint16_t u16(R *r) { uint16_t v = (uint16_t)(u8(r) << 8); return v | u8(r); }
static uint32_t u32(R *r) { uint32_t v = (uint32_t)u16(r) << 16; return v | u16(r); }
static void skip(R *r, size_t k) { if (r->p + k > r->n) { r->p = r->n; r->eof = 1; } else r->p += k; }
static void str(R *r, char *d, size_t k) { for (size_t i = 0; i < k; i++) { uint8_t c = u8(r); if (i < 32) d[i] = (char)c; } d[32 < k ? 32 : k] = 0; d[32] = 0; }

int ml_probe(const uint8_t *b, size_t n) { return n >= 16 && !memcmp(b, "MLEDMODL", 8); }

static int read_version(R *r) {
    uint32_t len = u32(r);
    if (len != 0x56455253u) {                                   /* very old files have no MODL length: VERS directly */
        skip(r, len);
        if (r->p + 4 > r->n || memcmp(r->b + r->p, "VERS", 4)) return 0;
        skip(r, 4);
    }
    len = u32(r); skip(r, len);
    return !r->eof;
}

static int load_tune(R *r, MlSong *m) {
    MlTune t; memset(&t, 0, sizeof t);
    str(r, t.title, 32);
    t.tempo = u16(r); t.speed = u8(r); t.groove = u8(r); t.volume = u16(r); t.mixer_mode = u8(r) != 0; t.channels = u8(r);
    if (r->eof) return 0;
    if (t.channels == 0) return 1;                              /* Fletch Dances: many sub-songs without channels */
    if (t.channels > 8) return 0;
    uint32_t sz[8]; for (int i = 0; i < t.channels; i++) sz[i] = u32(r);
    if (r->eof) return 0;
    for (int i = 0; i < t.channels; i++) {
        if (!sz[i]) { t.seq[i] = m->zero_seq; continue; }
        uint16_t *s = (uint16_t *)malloc(512);
        if (!s) return 0;
        for (int k = 0; k < 256; k++) s[k] = 0x0060;
        uint32_t cnt = sz[i] / 2; if (cnt > 256) { free(s); return 0; }
        for (uint32_t k = 0; k < cnt; k++) s[k] = u16(r);
        if (r->eof) { free(s); return 0; }
        t.seq[i] = s;
    }
    MlTune *nt = (MlTune *)realloc(m->tune, sizeof(MlTune) * (size_t)(m->ntunes + 1));
    if (!nt) return 0;
    m->tune = nt; m->tune[m->ntunes++] = t;
    return 1;
}

static int load_part(R *r, MlSong *m) {
    MlPart *p = (MlPart *)calloc(1, sizeof *p); if (!p) return 0;
    unsigned num = u16(r); int line = 0;
    for (;;) {
        uint8_t flag = u8(r);
        if (flag == 0xFF) break;
        if (r->eof || line == 128) { free(p); return 0; }
        MlPartLine *pl = &p->line[line++];
        if (flag & 1) { pl->note = u8(r); pl->instrument = u8(r); if (r->eof) { free(p); return 0; } }
        for (int i = 0; i < 5; i++) if (flag & (2 << i)) { pl->eff[i].effect = u8(r); pl->eff[i].argument = u8(r); if (r->eof) { free(p); return 0; } }
    }
    if (num >= 1024) { free(p); return 0; }
    free(m->part[num]); m->part[num] = p;
    return 1;
}

static int load_arpg(R *r, MlSong *m, uint32_t len) {
    MlArp *a = (MlArp *)calloc(1, sizeof *a); if (!a) return 0;
    unsigned num = u16(r); uint32_t total = len >= 2 ? (len - 2) / 6 : 0;
    if (total > 128 || num >= 256) { free(a); return 0; }
    for (uint32_t l = 0; l < total; l++) {
        MlArpLine *al = &a->line[l];
        al->transpose = (int8_t)u8(r); al->sample = u8(r);
        for (int i = 0; i < 2; i++) { al->eff[i].effect = u8(r); al->eff[i].argument = u8(r); }
        if (r->eof) { free(a); return 0; }
    }
    free(m->arp[num]); m->arp[num] = a;
    return 1;
}

static int load_inst(R *r, MlSong *m) {
    if (m->ninst >= 255) return 0;
    MlInst *s = &m->inst[m->ninst + 1]; memset(s, 0, sizeof *s);
    str(r, s->title, 32);
    s->sample_number = u8(r); s->sample_type = u8(r);
    if (r->eof) return 0;
    skip(r, 4); s->sample_length = u16(r); if (r->eof) return 0;
    skip(r, 4); s->sample_repeat_length = u16(r);
    s->fine_tune = (int16_t)u16(r); s->semi_tone = (int16_t)u16(r);
    s->sample_start = u16(r); s->sample_end = u16(r); s->sample_repeat_start = u16(r); s->sample_repeat_len = u16(r);
    s->volume = u16(r); s->transposable = u8(r) != 0; s->slide_speed = u8(r); s->effect1 = u8(r); s->effect2 = u8(r);
    if (r->eof) return 0;
    s->envelope_attack_length = u16(r); s->envelope_decay_length = u16(r); s->envelope_sustain_length = u16(r); s->envelope_release_length = u16(r);
    s->envelope_attack_speed = u16(r); s->envelope_decay_speed = u16(r); s->envelope_sustain_speed = u16(r); s->envelope_release_speed = u16(r);
    s->envelope_attack_volume = u16(r); s->envelope_decay_volume = u16(r); s->envelope_sustain_volume = u16(r); s->envelope_release_volume = u16(r);
    if (r->eof) return 0;
    s->vibrato_direction = u8(r) != 0 ? 0 : 1;            /* nonzero = Upward (0), zero = Downward (1) */ s->vibrato_wave_type = u8(r); s->vibrato_speed = u16(r); s->vibrato_delay = u16(r); s->vibrato_attack_speed = u16(r); s->vibrato_attack = u16(r); s->vibrato_depth = u16(r);
    if (r->eof) return 0;
    s->tremolo_direction = u8(r) != 0 ? 0 : 1; s->tremolo_wave_type = u8(r); s->tremolo_speed = u16(r); s->tremolo_delay = u16(r); s->tremolo_attack_speed = u16(r); s->tremolo_attack = u16(r); s->tremolo_depth = u16(r);
    if (r->eof) return 0;
    s->arpeggio_table = u16(r); s->arpeggio_speed = u8(r); s->arpeggio_groove = u8(r);
    if (r->eof) return 0;
    s->env_tra_pha_fil_bits = u8(r); for (int i = 0; i < 5; i++) s->transform_wave_numbers[i] = u8(r);
    s->transform_start = u16(r); s->transform_repeat = u16(r); s->transform_repeat_end = u16(r); s->transform_speed = u16(r); s->transform_turns = u16(r); s->transform_delay = u16(r);
    if (r->eof) return 0;
    s->phase_start = u16(r); s->phase_repeat = u16(r); s->phase_repeat_end = u16(r); s->phase_speed = u16(r); s->phase_turns = u16(r); s->phase_delay = u16(r); s->phase_type = u16(r);
    if (r->eof) return 0;
    s->mix_res_loo_bits = u8(r); s->mix_wave_number = u8(r);
    s->mix_start = u16(r); s->mix_repeat = u16(r); s->mix_repeat_end = u16(r); s->mix_speed = u16(r); s->mix_turns = u16(r); s->mix_delay = u16(r);
    if (r->eof) return 0;
    s->resonance_start = u16(r); s->resonance_repeat = u16(r); s->resonance_repeat_end = u16(r); s->resonance_speed = u16(r); s->resonance_turns = u16(r); s->resonance_delay = u16(r);
    s->mix_res_fil_boost = u8(r); s->responance_amp = u8(r);
    if (r->eof) return 0;
    s->filter_start = u16(r); s->filter_repeat = u16(r); s->filter_repeat_end = u16(r); s->filter_speed = u16(r); s->filter_turns = u16(r); s->filter_delay = u16(r);
    skip(r, 1); s->filter_type = u8(r);
    if (r->eof) return 0;
    s->loop_start = u16(r); s->loop_repeat = u16(r); s->loop_repeat_end = u16(r); s->loop_length = u16(r); s->loop_loop_step = u16(r); s->loop_wait = u16(r); s->loop_delay = u16(r); s->loop_turns = u16(r);
    if (r->eof) return 0;
    s->valid = 1; m->ninst++;
    return 1;
}

static int delta_decrunch(const uint8_t *in, uint32_t inlen, uint8_t cmd, int8_t *out, size_t outcap) {
    uint32_t rp = 0; size_t wp = 0;
#define WB(v) do { if (wp >= outcap) return 0; out[wp++] = (int8_t)(v); } while (0)
    while (rp < inlen) {
        uint8_t d = in[rp++];
        if (d == cmd) {
            if (rp + 3 > inlen) return 0;
            uint8_t begin = in[rp++], hi = in[rp++], lo = in[rp++];
            int dl = (hi << 8) | lo; int8_t sb = (int8_t)begin;
            WB(sb);
            while (dl) {
                if (rp >= inlen) return 0;
                d = in[rp++];
                int hv = d >> 4; if (hv & 8) hv |= 0xF0; sb = (int8_t)(sb + (int8_t)hv); WB(sb); dl--;
                if (dl) { int lv = d & 15; if (lv & 8) lv |= 0xF0; sb = (int8_t)(sb + (int8_t)lv); WB(sb); dl--; }
            }
        } else WB(d);
    }
#undef WB
    return 1;
}

static int load_smpl(R *r, MlSong *m, uint32_t len) {
    if (m->nsmp >= 255 || len < 50) return 0;
    MlSample *s = &m->smp[m->nsmp + 1]; memset(s, 0, sizeof *s);
    uint32_t orig = u32(r); uint8_t dcmd = u8(r); skip(r, 1);
    str(r, s->title, 32); if (r->eof) return 0;
    skip(r, 1); s->sample_type = u8(r);
    uint32_t ptr = u32(r); s->sample_length = u16(r); uint32_t rptr = u32(r); s->sample_repeat_length = u16(r);
    s->fine_tune = (int16_t)u16(r); s->semi_tone = (int16_t)u16(r);
    if (r->eof) return 0;
    size_t extra = orig == 256 ? 240 : 0; uint32_t slen = len - 50;
    if (orig > (16u << 20) || r->p + slen > r->n) return 0;
    s->data = (int8_t *)calloc(1, orig + extra + slen + 4); s->data_len = orig + extra;
    if (!s->data) return 0;
    if (slen != orig) { if (!delta_decrunch(r->b + r->p, slen, dcmd, s->data, orig + extra)) { free(s->data); s->data = NULL; return 0; } }
    else memcpy(s->data, r->b + r->p, slen);
    r->p += slen;
    m->nsmp++;
    if (orig == 256) {                                           /* a wave: -128 -> -127 and the 240 extra bytes (the sample at half rate) */
        for (int i = 0; i < 256; i++) if (s->data[i] == -128) s->data[i] = -127;
        for (int i = 0, j = 0; i < 240; i++, j += 2) s->data[256 + i] = s->data[j];
    }
    /* the instruments that use this sample (they come before the SMPL chunks) */
    for (int i = 1; i <= m->ninst; i++) if (m->inst[i].sample_number == m->nsmp) { m->inst[i].sample_index = m->nsmp; m->inst[i].sample_off = (size_t)m->inst[i].sample_start * 2; m->inst[i].rep_off = (size_t)m->inst[i].sample_repeat_start * 2;
        MlPtr a = { s->data, (uint32_t)s->data_len, (uint32_t)(m->inst[i].sample_start * 2u), 1 }, b = { s->data, (uint32_t)s->data_len, (uint32_t)(m->inst[i].sample_repeat_start * 2u), 1 };
        m->inst[i].sample_pointer = a; m->inst[i].sample_repeat_pointer = b; }
    long ro = (long)rptr - (long)ptr; if (ro < 0) ro = 0;
    if ((size_t)s->sample_length * 2 > s->data_len) s->sample_length = (uint16_t)(s->data_len / 2);
    if ((size_t)ro + (size_t)s->sample_repeat_length * 2 > s->data_len) s->sample_repeat_length = (uint16_t)(((long)s->data_len - ro) / 2);
    s->rep_off = (size_t)ro;
    { MlPtr a = { s->data, (uint32_t)s->data_len, 0, 1 }, b = { s->data, (uint32_t)s->data_len, (uint32_t)ro, 1 }; s->sample_pointer = a; s->sample_repeat_pointer = b; }
    return 1;
}

static void info_str(R *r, char *d, size_t cap) {
    size_t i = 0;
    for (;;) { uint8_t c = u8(r); if (r->eof || !c) break; if (i + 1 < cap) d[i++] = (char)c; }
    d[i] = 0;
}

static int load_info(R *r, MlSong *m) {
    char tmp[256];
    info_str(r, m->name, sizeof m->name); if (r->eof) return 0;
    info_str(r, m->author, sizeof m->author); if (r->eof) return 0;
    info_str(r, tmp, sizeof tmp); info_str(r, tmp, sizeof tmp);          /* date, duration */
    for (int i = 0; i < 5; i++) info_str(r, tmp, sizeof tmp);
    return 1;
}

MlSong *ml_load(const uint8_t *b, size_t n, int *err) {
    int e = 1; if (!err) err = &e;
    if (!ml_probe(b, n)) { *err = 1; return NULL; }
    MlSong *m = (MlSong *)calloc(1, sizeof *m); if (!m) { *err = 2; return NULL; }
    for (int i = 0; i < 256; i++) m->zero_seq[i] = 0x0060;
    R r = { b, n, 8, 0 };
    if (!read_version(&r)) { *err = 3; ml_free(m); return NULL; }
    for (;;) {
        if (r.p + 8 > r.n) break;
        char mark[5]; memcpy(mark, r.b + r.p, 4); mark[4] = 0; r.p += 4;
        uint32_t len = u32(&r);
        int ok = 1;
        if (!strcmp(mark, "TUNE")) ok = load_tune(&r, m);
        else if (!strcmp(mark, "PART")) { ok = load_part(&r, m); if (r.p & 1) skip(&r, 1); }
        else if (!strcmp(mark, "ARPG")) ok = load_arpg(&r, m, len);
        else if (!strcmp(mark, "INST")) ok = load_inst(&r, m);
        else if (!strcmp(mark, "SMPL")) ok = load_smpl(&r, m, len);
        else if (!strcmp(mark, "INFO")) ok = load_info(&r, m);
        else ok = 0;
        if (!ok) { *err = 4; ml_free(m); return NULL; }
    }
    if (!m->ntunes) { *err = 5; ml_free(m); return NULL; }
    for (int i = 0; i < m->ntunes; i++) if (m->tune[i].mixer_mode) m->mixer_mode = 1;
    return m;
}

void ml_free(MlSong *m) {
    if (!m) return;
    for (int i = 0; i < m->ntunes; i++) for (int c = 0; c < m->tune[i].channels; c++) if (m->tune[i].seq[c] != m->zero_seq) free(m->tune[i].seq[c]);
    free(m->tune);
    for (int i = 0; i < 1024; i++) free(m->part[i]);
    for (int i = 0; i < 256; i++) free(m->arp[i]);
    for (int i = 1; i <= m->nsmp; i++) free(m->smp[i].data);
    free(m);
}
