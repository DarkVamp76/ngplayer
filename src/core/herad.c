/* HERAD (Herbulot AdLib System): unpackers (HSQ, SQX), loader and player. Port of the HERAD player of the AdPlug project. GPL-3.0-or-later */
#include "herad.h"
#include <stdlib.h>
#include <string.h>

#define MIN_SIZE   6
#define MAX_SIZE   75775
#define HEAD_SIZE  52
#define NUM_VOICES 9
#define NUM_NOTES  12
#define BEND_CENTER 0x40
#define NOTE_OFF 0
#define NOTE_ON 1
#define NOTE_UPDATE 2
#define INSTMODE_KMAP (-1)

static const uint8_t slot_offset[NUM_VOICES] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
static const uint16_t FNum[NUM_NOTES] = { 343, 364, 385, 408, 433, 459, 486, 515, 546, 579, 614, 650 };
static const uint8_t fine_bend[NUM_NOTES + 1] = { 19, 21, 21, 23, 25, 26, 27, 29, 31, 33, 35, 36, 37 };
static const uint8_t coarse_bend[10] = { 0, 5, 10, 15, 20, 0, 6, 12, 18, 24 };

/* instrument parameters, by byte position in the 40 byte record */
enum { I_MODE, I_VOICE, I_MOD_KSL, I_MOD_MUL, I_FEEDBACK, I_MOD_A, I_MOD_S, I_MOD_EG, I_MOD_D, I_MOD_R, I_MOD_OUT, I_MOD_AM, I_MOD_VIB, I_MOD_KSR, I_CON,
       I_CAR_KSL, I_CAR_MUL, I_PAN, I_CAR_A, I_CAR_S, I_CAR_EG, I_CAR_D, I_CAR_R, I_CAR_OUT, I_CAR_AM, I_CAR_VIB, I_CAR_KSR, I_MC_FB_AT, I_MOD_WAVE, I_CAR_WAVE,
       I_MC_MOD_OUT_VEL, I_MC_CAR_OUT_VEL, I_MC_FB_VEL, I_MC_SLIDE_COARSE, I_MC_TRANSPOSE, I_MC_SLIDE_DUR, I_MC_SLIDE_RANGE, I_DUMMY, I_MC_MOD_OUT_AT, I_MC_CAR_OUT_AT };

static unsigned rd16(const uint8_t *d, size_t n, size_t pos) { return pos + 1 < n ? d[pos] | (d[pos + 1] << 8) : 0; }

/* ---------------- HSQ ---------------- */
static int is_hsq(const uint8_t *d, size_t n) {
    if (n < MIN_SIZE || d[2] != 0) return 0;
    if (rd16(d, n, 3) != n) return 0;
    uint8_t sum = 0; for (int i = 0; i < MIN_SIZE; i++) sum = (uint8_t)(sum + d[i]);
    return sum == 0xAB;
}

typedef struct { const uint8_t *s; size_t n, pos; uint32_t queue; uint8_t *out; size_t o, cap; } Un;

static int hsq_bit(Un *u) {
    if (u->queue == 1) { u->queue = rd16(u->s, u->n, u->pos) | 0x10000; u->pos += 2; }
    int b = (int)(u->queue & 1); u->queue >>= 1; return b;
}
static void un_copy(Un *u, int offset, unsigned count) {
    while (count--) {
        if (u->o >= u->cap) return;
        long from = (long)u->o + offset;
        u->out[u->o] = from >= 0 ? u->out[from] : 0; u->o++;
    }
}

static long hsq_decompress(const uint8_t *data, size_t n, uint8_t *out, size_t cap) {
    Un u = { data, n, 6, 1, out, 0, cap };
    unsigned out_size = rd16(data, n, 0);
    while (u.pos <= n + 4 && u.o < cap) {
        if (hsq_bit(&u)) { if (u.pos < n && u.o < cap) out[u.o++] = data[u.pos]; u.pos++; continue; }
        unsigned count; int offset;
        if (hsq_bit(&u)) {
            count = rd16(data, n, u.pos); offset = (int)(count >> 3) - 8192; count &= 7; u.pos += 2;
            if (!count) { count = u.pos < n ? data[u.pos] : 0; u.pos++; }
            if (!count) break;
        } else {
            count = (unsigned)hsq_bit(&u) << 1; count += (unsigned)hsq_bit(&u);
            offset = (int)(u.pos < n ? data[u.pos] : 0) - 256; u.pos++;
        }
        un_copy(&u, offset, count + 2);
    }
    (void)out_size;
    return (long)out_size;
}

/* ---------------- SQX ---------------- */
static int is_sqx(const uint8_t *d, size_t n) {
    if (n < MIN_SIZE) return 0;
    if (d[2] > 2 || d[3] > 2 || d[4] > 2) return 0;
    if (d[5] == 0 || d[5] > 15) return 0;
    return 1;
}

typedef struct { const uint8_t *s; size_t n, pos; uint16_t queue; } Sq;
static int sqx_bit(Sq *q) {                                  /* the queue holds a marker bit above the data; an empty queue is refilled with 16 bits */
    int bit = q->queue & 1; q->queue >>= 1;
    if (q->queue == 0) {
        q->queue = (uint16_t)rd16(q->s, q->n, q->pos); q->pos += 2;
        int carry = bit; bit = q->queue & 1; q->queue >>= 1;
        if (carry) q->queue |= 0x8000;
    }
    return bit;
}

static long sqx_decompress(const uint8_t *data, size_t n, uint8_t *out, size_t cap) {
    Sq q = { data, n, 6, 1 }; Un u = { data, n, 6, 1, out, 0, cap };
    if (n >= 2) { /* the first two bytes are the unpacked size, copied to the output buffer by the original; not needed here */ }
    for (;;) {
        if (u.o >= cap || q.pos > n + 4) break;
        int bit = sqx_bit(&q);
        int mode = !bit ? data[2] : (!sqx_bit(&q) ? data[3] : data[4]);
        if (mode == 0) { if (q.pos < n) out[u.o++] = data[q.pos]; q.pos++; continue; }
        unsigned count; int offset;
        if (mode == 1) {
            count = (unsigned)sqx_bit(&q) << 1; count |= (unsigned)sqx_bit(&q);
            offset = (int)(q.pos < n ? data[q.pos] : 0) - 256; q.pos++; count += 2;
        } else {
            unsigned b = data[5]; count = rd16(data, n, q.pos);
            offset = (int16_t)((count >> b) - (1u << (16 - b))); count &= (1u << b) - 1; q.pos += 2;
            if (!count) { count = q.pos < n ? data[q.pos] : 0; q.pos++; }
            if (!count) break;
            count += 2;
        }
        un_copy(&u, offset, count);
    }
    return (long)u.o;
}

/* ---------------- loader ---------------- */
static int valid_event(const HeradTrack *t, uint16_t *offset, int v2) {
    uint16_t sz = t->size;
#define BYTE(o) ((o) < sz ? t->data[o] : 0)
    while (*offset < sz && (t->data[(*offset)++] & 0x80));
    if (*offset >= sz) return 0;
    uint8_t status = BYTE(*offset); (*offset)++;
    if (status < 0x80) return 0;
    else if (status < 0x90 && v2) { uint8_t c = BYTE(*offset); (*offset)++; if (c > 0x7F) return 0; }
    else if (status < 0xC0) { uint8_t c = BYTE(*offset); (*offset)++; if (c > 0x7F) return 0; c = BYTE(*offset); (*offset)++; if (c > 0x7F) return 0; }
    else if (status < 0xF0) { uint8_t c = BYTE(*offset); (*offset)++; if (c > 0x7F) return 0; }
    else if (status == 0xFF) *offset = sz;
#undef BYTE
    return 1;
}

static int valid_tracks(const HeradSong *s) {
    for (int i = 0; i < s->ntracks; i++) {
        uint16_t o1 = 0, o2 = 0; const HeradTrack *t = &s->track[i];
        while (o1 < t->size || o2 < t->size) {
            if (o1 < t->size && !valid_event(t, &o1, 0)) return 1;
            if (o2 < t->size && !valid_event(t, &o2, 1)) return 2;
        }
    }
    return 0;
}

static uint32_t get_ticks(const HeradTrack *t, uint16_t *pos) {
    uint32_t r = 0;
    do { r <<= 7; r |= t->data[*pos] & 0x7F; } while ((t->data[(*pos)++] & 0x80) && *pos < t->size);
    return r;
}

static void song_ticks(HeradSong *s) {
    s->total_ticks = 0; s->last_tick = 0;
    for (int i = 0; i < s->ntracks; i++) {
        HeradTrack *t = &s->track[i]; uint16_t pos = 0; uint32_t j = 0; int cap = 64, n = 0;
        HeradEv *ev = malloc(cap * sizeof *ev); if (!ev) continue;
        while (pos < t->size) {
            j += get_ticks(t, &pos);
            if (pos >= t->size) break;
            uint8_t st = t->data[pos++]; uint8_t a = pos < t->size ? t->data[pos] : 0, b = pos + 1 < t->size ? t->data[pos + 1] : 0; int kind = 0;
            switch (st & 0xF0) {
            case 0x80: kind = 2; pos += s->v2 ? 1 : 2; break;
            case 0x90: kind = 1; pos += 2; break;
            case 0xA0: case 0xB0: pos += 2; break;
            case 0xC0: kind = 3; pos++; break;
            case 0xD0: kind = 4; pos++; break;
            case 0xE0: kind = 5; pos++; break;
            default: pos = t->size; break;
            }
            if (kind) {
                if (n == cap) { cap *= 2; HeradEv *r = realloc(ev, cap * sizeof *ev); if (!r) break; ev = r; }
                if (j > s->last_tick) s->last_tick = j; ev[n].tick = j; ev[n].kind = (uint8_t)kind; ev[n].a = a; ev[n].b = b; n++;
            }
        }
        s->ev[i] = ev; s->nev[i] = n;
        if (j > s->total_ticks) s->total_ticks = j;
    }
}

void herad_cell(const HeradSong *s, int track, uint32_t tick, HeradCell *c) {
    c->note_kind = 0; c->note = c->vel = 0; c->prog = c->bend = c->touch = -1;
    if (track < 0 || track >= s->ntracks || !s->ev[track]) return;
    const HeradEv *ev = s->ev[track]; int lo = 0, hi = s->nev[track];
    while (lo < hi) { int m = (lo + hi) / 2; if (ev[m].tick < tick) lo = m + 1; else hi = m; }
    for (int k = lo; k < s->nev[track] && ev[k].tick == tick; k++) {
        switch (ev[k].kind) {
        case 1: c->note_kind = 1; c->note = ev[k].a; c->vel = ev[k].b; break;
        case 2: if (c->note_kind != 1) { c->note_kind = 2; c->note = ev[k].a; } break;
        case 3: c->prog = ev[k].a; break;
        case 4: c->touch = ev[k].a; break;
        case 5: c->bend = ev[k].a; break;
        }
    }
}

int herad_probe(const uint8_t *b, size_t n) {
    if (n < MIN_SIZE || n > MAX_SIZE) return 0;
    if (is_hsq(b, n)) return 1;
    if (n < HEAD_SIZE) return 0;
    unsigned o = rd16(b, n, 2), io = rd16(b, n, 0);
    return (o == 0x32 || o == 0x52) && io <= n && io >= o;
}

void herad_free(HeradSong *s) {
    if (!s) return;
    for (int i = 0; i < HERAD_MAX_TRACKS; i++) { free(s->track[i].data); free(s->ev[i]); }
    free(s->inst); free(s);
}

HeradSong *herad_load(const uint8_t *buf, size_t len, int *err) {
    int e = 0; if (!err) err = &e; *err = 0;
    if (len < MIN_SIZE) { *err = -1; return NULL; }
    if (len > MAX_SIZE) { *err = -2; return NULL; }
    uint8_t *data = NULL; size_t size = len; int comp = 0;
    if (is_hsq(buf, len) || is_sqx(buf, len)) {
        comp = is_hsq(buf, len) ? 1 : 2;
        uint8_t *out = calloc(1, MAX_SIZE + 64); if (!out) { *err = -5; return NULL; }
        long r = comp == 1 ? hsq_decompress(buf, len, out, MAX_SIZE) : sqx_decompress(buf, len, out, MAX_SIZE);
        if (r < 0 || r > MAX_SIZE) { free(out); *err = -3; return NULL; }
        data = out; size = (size_t)r;
    } else { data = malloc(len); if (!data) { *err = -5; return NULL; } memcpy(data, buf, len); }

    HeradSong *s = calloc(1, sizeof *s); if (!s) { free(data); *err = -5; return NULL; }
    s->comp = comp;
    if (size < HEAD_SIZE) { *err = -4; goto fail; }
    unsigned io = rd16(data, size, 0);
    if (size < io) { *err = -4; goto fail; }
    s->ninst = (int)(uint8_t)((size - io) / HERAD_INST_SIZE);
    if (s->ninst == 0) { *err = -4; goto fail; }
    unsigned first = rd16(data, size, 2);
    if (first != 0x32 && first != 0x52) { *err = -4; goto fail; }
    s->agd = first == 0x52;
    if (s->agd && size >= 0x54) memcpy(s->gold, data + 0x34, 32);
    s->speed = (uint16_t)rd16(data, size, 0x32);
    if (s->speed == 0) { *err = -4; goto fail; }
    s->ntracks = 0;
    for (int i = 0; i < HERAD_MAX_TRACKS; i++) { if (rd16(data, size, 2 + i * 2) == 0) break; s->ntracks++; }
    for (int i = 0; i < s->ntracks; i++) {
        unsigned off = rd16(data, size, 2 + i * 2) + 2;
        unsigned next = i < HERAD_MAX_TRACKS - 1 ? rd16(data, size, 2 + (i + 1) * 2) + 2 : io;
        if (next <= 2) next = io;
        if (next < off || next > size) { *err = -4; goto fail; }
        s->track[i].size = (uint16_t)(next - off); s->track[i].data = malloc(next - off ? next - off : 1);
        if (!s->track[i].data) { *err = -5; goto fail; }
        memcpy(s->track[i].data, data + off, next - off);
    }
    s->inst = malloc((size_t)s->ninst * HERAD_INST_SIZE); if (!s->inst) { *err = -5; goto fail; }
    s->v2 = 0;
    for (int i = 0; i < s->ninst; i++) {
        memcpy(s->inst[i], data + io + (size_t)i * HERAD_INST_SIZE, HERAD_INST_SIZE);
        if (!s->v2 && (int8_t)s->inst[i][I_MODE] == INSTMODE_KMAP) s->v2 = 1;
    }
    if (!s->v2) s->v2 = valid_tracks(s) == 1;                 /* a version 1 event parser that fails means version 2 */
    song_ticks(s);
    free(data);
    return s;
fail:
    free(data); herad_free(s); return NULL;
}

/* ---------------- player ---------------- */
typedef struct { uint16_t pos; uint32_t counter; uint16_t ticks; } Trk;
typedef struct { uint8_t program, playprog, note; int keyon; uint8_t bend, slide_dur; } Chn;

struct HeradPlayer {
    const HeradSong *s; HeradWrite wr; void *user;
    int songend; int16_t wTime; uint32_t ticks_pos;
    Trk trk[HERAD_MAX_TRACKS]; Chn chn[HERAD_MAX_TRACKS];
    /* AGD: the driver hands out the OPL3 channels itself, at every program change (see alloc_voice) */
    int vch[HERAD_MAX_TRACKS], pair[HERAD_MAX_TRACKS]; unsigned vmask[HERAD_MAX_TRACKS];
    unsigned used0, used1, op4;                      /* channels in use in bank 0 and 1, the 4-operator enable register */
};

static void W(HeradPlayer *p, int chip, int reg, int val) { p->wr(p->user, chip, reg, val); }
#define PI(p, c, k) ((p)->s->inst[(p)->chn[c].playprog][k])
static int vc(const HeradPlayer *p, int t) { return p->vch[t]; }        /* OPL3 channel 0..17 of a track; the first pair channel of a 4-operator voice */

HeradPlayer *herad_new(const HeradSong *s, HeradWrite wr, void *user) {
    HeradPlayer *p = calloc(1, sizeof *p); if (!p) return NULL;
    p->s = s; p->wr = wr; p->user = user; return p;
}
void herad_free_player(HeradPlayer *p) { free(p); }
int herad_channel(const HeradPlayer *p, int t) { return t >= 0 && t < p->s->ntracks && t < (p->s->agd ? 18 : 9) ? p->vch[t] : -1; }
unsigned herad_channel_mask(const HeradPlayer *p, int t) {
    int c = herad_channel(p, t); if (c < 0) return 0;
    return p->pair[t] ? (1u << c) | (1u << (c + 3)) : 1u << c;
}

void herad_rewind(HeradPlayer *p) {
    const HeradSong *s = p->s;
    p->wTime = 0; p->songend = 0; p->ticks_pos = ~0u; p->used0 = p->used1 = p->op4 = 0;
    for (int i = 0; i < s->ntracks; i++) {
        p->trk[i].pos = 0; p->trk[i].counter = 0; p->trk[i].ticks = 0;
        p->chn[i].program = 0; p->chn[i].playprog = 0; p->chn[i].note = 0; p->chn[i].keyon = 0; p->chn[i].bend = BEND_CENTER; p->chn[i].slide_dur = 0;
        p->vch[i] = i; p->pair[i] = 0; p->vmask[i] = 0;
    }
    W(p, 0, 1, 32); W(p, 0, 0xBD, 0); W(p, 0, 8, 64);
    if (s->agd) {
        W(p, 1, 5, 1); W(p, 1, 4, 0);
        /* what the AGD driver does when it starts a song: stereo processor settings, then the stereo mode and the YM7128B registers stored in the file header */
        W(p, 2, 4, 0xFE); W(p, 2, 5, 0xFF); W(p, 2, 6, 0xFB); W(p, 2, 7, 0xF7);
        W(p, 2, 8, s->gold[0]);
        for (int i = 0; i < 31; i++) W(p, 3, i, s->gold[1 + i]);
    }
}

/* ---- channel allocation of the AGD driver ----
 * A track gets its OPL3 channel when it gets its instrument. An instrument of mode 4 is a 4-operator voice: it takes a channel pair (n, n+3), the first free of
 * 0/3, 1/4, 2/5 in bank 0, then in bank 1. Other instruments take the highest free one of channels 8, 7, 6 (these cannot be paired) in bank 0, then bank 1,
 * and only when those are all taken one channel of the pairs, preferring a pair that is half used already, so that whole pairs stay free. */
static void alloc_release(HeradPlayer *p, int t) {
    unsigned m = p->vmask[t]; if (!m) return;
    if (m & 0x8000) p->used1 &= ~(m & 0x7FFF); else p->used0 &= ~m;
    p->vmask[t] = 0;
}
static void alloc_take(HeradPlayer *p, int t, int bank, int ch) {
    unsigned bit = 1u << ch;
    if (bank) { p->used1 |= bit; p->vmask[t] = bit | 0x8000; } else { p->used0 |= bit; p->vmask[t] = bit; }
    p->vch[t] = bank * 9 + ch; p->pair[t] = 0;
}
static void alloc_half(HeradPlayer *p, int t, int bank, unsigned used, int i) {      /* the free one of the pair i, i + 3 */
    alloc_take(p, t, bank, (used & (1u << i)) ? i + 3 : i);
}
static void alloc_voice(HeradPlayer *p, int t, int four_op) {
    alloc_release(p, t);
    if (four_op) {
        static const unsigned pm[3] = { 0x09, 0x12, 0x24 };
        for (int bank = 0; bank < 2; bank++) for (int i = 0; i < 3; i++) {
            unsigned used = bank ? p->used1 : p->used0;
            if (!(used & pm[i])) {
                if (bank) { p->used1 |= pm[i]; p->vmask[t] = pm[i] | 0x8000; } else { p->used0 |= pm[i]; p->vmask[t] = pm[i]; }
                p->vch[t] = bank * 9 + i; p->pair[t] = 1; return;
            }
        }                                                          /* no pair free: the voice is allocated like a plain one */
    }
    p->pair[t] = 0;
    for (int bank = 0; bank < 2; bank++) {
        unsigned fr = ~(bank ? p->used1 : p->used0) & 0x1C0;
        if (fr) { alloc_take(p, t, bank, fr & 0x100 ? 8 : fr & 0x80 ? 7 : 6); return; }
    }
    {
        unsigned f1 = ~p->used1, d1 = (f1 ^ (f1 >> 3)) & 7;
        if (d1) { alloc_half(p, t, 1, p->used1, d1 & 1 ? 0 : d1 & 2 ? 1 : 2); return; }
        unsigned d0 = (p->used0 ^ (p->used0 >> 3)) & 7;
        if (d0) { alloc_half(p, t, 0, p->used0, d0 & 1 ? 0 : d0 & 2 ? 1 : 2); return; }
        if (f1 & 0x3F) { int i = (f1 & 0x24) ? 2 : (f1 & 0x12) ? 1 : 0; alloc_half(p, t, 1, p->used1, i); return; }
        unsigned f0 = ~p->used0; { int i = (f0 & 0x24) ? 2 : (f0 & 0x12) ? 1 : 0; alloc_half(p, t, 0, p->used0, i); }
    }
}

static void set_freq(HeradPlayer *p, int t, int oct, unsigned freq, int on) {
    int c = vc(p, t), chip = c >= NUM_VOICES, v = c % NUM_VOICES;
    W(p, chip, 0xA0 + v, freq & 0xFF);
    W(p, chip, 0xB0 + v, ((freq >> 8) & 3) | ((oct & 7) << 2) | ((on ? 1 : 0) << 5));
}

/* the registers of one OPL3 channel from one instrument */
static void put_inst(HeradPlayer *p, int c, int i) {
    const uint8_t *n = p->s->inst[i]; int chip = c >= NUM_VOICES, v = c % NUM_VOICES, so = slot_offset[v], agd = p->s->agd;
    W(p, chip, 0x20 + so, (n[I_MOD_MUL] & 15) | ((n[I_MOD_KSR] & 1) << 4) | ((n[I_MOD_EG] > 0) << 5) | ((n[I_MOD_VIB] & 1) << 6) | ((n[I_MOD_AM] & 1) << 7));
    W(p, chip, 0x23 + so, (n[I_CAR_MUL] & 15) | ((n[I_CAR_KSR] & 1) << 4) | ((n[I_CAR_EG] > 0) << 5) | ((n[I_CAR_VIB] & 1) << 6) | ((n[I_CAR_AM] & 1) << 7));
    W(p, chip, 0x40 + so, (n[I_MOD_OUT] & 63) | ((n[I_MOD_KSL] & 3) << 6));
    W(p, chip, 0x43 + so, (n[I_CAR_OUT] & 63) | ((n[I_CAR_KSL] & 3) << 6));
    W(p, chip, 0x60 + so, (n[I_MOD_D] & 15) | ((n[I_MOD_A] & 15) << 4));
    W(p, chip, 0x63 + so, (n[I_CAR_D] & 15) | ((n[I_CAR_A] & 15) << 4));
    W(p, chip, 0x80 + so, (n[I_MOD_R] & 15) | ((n[I_MOD_S] & 15) << 4));
    W(p, chip, 0x83 + so, (n[I_CAR_R] & 15) | ((n[I_CAR_S] & 15) << 4));
    W(p, chip, 0xC0 + v, (n[I_CON] > 0 ? 0 : 1) | ((n[I_FEEDBACK] & 7) << 1) | ((agd ? (n[I_PAN] == 0 || n[I_PAN] > 3 ? 3 : n[I_PAN]) : 0) << 4));
    W(p, chip, 0xE0 + so, n[I_MOD_WAVE] & (agd ? 7 : 3));
    W(p, chip, 0xE3 + so, n[I_CAR_WAVE] & (agd ? 7 : 3));
}

static int second_inst(const HeradPlayer *p, int t) {           /* the instrument of the second pair of a 4-operator voice, -1 if there is none */
    if (!p->pair[t]) return -1;
    int i = p->chn[t].playprog + 1; return i < p->s->ninst ? i : -1;
}

static void change_program(HeradPlayer *p, int t, int i) {
    const HeradSong *s = p->s;
    if (s->v2 && (int8_t)s->inst[i][I_MODE] == INSTMODE_KMAP) return;
    if (s->agd) alloc_voice(p, t, (int8_t)s->inst[i][I_MODE] == 4 && i + 1 < s->ninst);
    int c = vc(p, t);
    put_inst(p, c, i);
    if (!s->agd) return;
    if (p->pair[t]) {                                           /* 4 operators: enable the pair, program its second channel from the next instrument */
        int v = c % NUM_VOICES, bit = v + (c >= NUM_VOICES ? 3 : 0);
        p->op4 |= 1u << bit; W(p, 1, 4, (int)p->op4);
        put_inst(p, c + 3, i + 1);
    } else if (slot_offset[c % NUM_VOICES] < 0x10) {            /* channels 0..5 may be half of a pair: make sure the pair is off */
        int bit = (slot_offset[c % NUM_VOICES] & 3) + (c >= NUM_VOICES ? 3 : 0);
        p->op4 &= ~(1u << bit); W(p, 1, 4, (int)p->op4);
    }
}

static unsigned macro_level(int sens, unsigned level) {         /* output level change from velocity / aftertouch, sens -4..4 */
    unsigned o;
    if (sens < 0) { o = level >> (sens + 4); if (o > 63) o = 63; }
    else { o = (0x80 - level) >> (4 - sens); if (o > 63) o = 63; }
    return o;
}
static void out_mod(HeradPlayer *p, int c, int i, int sens, unsigned level) {
    if (sens < -4 || sens > 4) return;
    const uint8_t *n = p->s->inst[i]; unsigned out = macro_level(sens, level) + n[I_MOD_OUT]; if (out > 63) out = 63;
    W(p, c >= NUM_VOICES, 0x40 + slot_offset[c % NUM_VOICES], (out & 63) | ((n[I_MOD_KSL] & 3) << 6));
}
static void out_car(HeradPlayer *p, int c, int i, int sens, unsigned level) {
    if (sens < -4 || sens > 4) return;
    const uint8_t *n = p->s->inst[i]; unsigned out = macro_level(sens, level) + n[I_CAR_OUT]; if (out > 63) out = 63;
    W(p, c >= NUM_VOICES, 0x43 + slot_offset[c % NUM_VOICES], (out & 63) | ((n[I_CAR_KSL] & 3) << 6));
}
static void macro_mod_output(HeradPlayer *p, int t, int i, int sens, unsigned level) { out_mod(p, vc(p, t), i, sens, level); }
static void macro_car_output(HeradPlayer *p, int t, int i, int sens, unsigned level) { out_car(p, vc(p, t), i, sens, level); }
static void macro_feedback(HeradPlayer *p, int t, int i, int sens, unsigned level) {
    if (sens < -6 || sens > 6) return;
    const uint8_t *n = p->s->inst[i]; unsigned fb;
    if (sens < 0) { fb = level >> (sens + 7); if (fb > 7) fb = 7; } else { fb = (0x80 - level) >> (7 - sens); if (fb > 7) fb = 7; }
    fb = (uint8_t)(fb + n[I_FEEDBACK]); if (fb > 7) fb = 7;
    int agd = p->s->agd, c = vc(p, t);
    W(p, c >= NUM_VOICES, 0xC0 + (c % NUM_VOICES), (n[I_CON] > 0 ? 0 : 1) | ((fb & 7) << 1) | ((agd ? (n[I_PAN] == 0 || n[I_PAN] > 3 ? 3 : n[I_PAN]) : 0) << 4));
}

static void play_note(HeradPlayer *p, int t, uint8_t note, int state) {
    const uint8_t *n = p->s->inst[p->chn[t].playprog];
    if (n[I_MC_TRANSPOSE] != 0) {
        uint8_t tran = n[I_MC_TRANSPOSE], diff = (uint8_t)(tran - 0x31);
        note = (p->s->v2 && diff < 0x60) ? (uint8_t)(diff + 0x18) : (uint8_t)(note + tran);
    }
    note = (uint8_t)(note - 24);
    if (state != NOTE_UPDATE && note >= 0x60) note = 0;
    int8_t oct = (int8_t)(note / NUM_NOTES), key = (int8_t)(note % NUM_NOTES);
    if (state != NOTE_UPDATE && n[I_MC_SLIDE_DUR]) p->chn[t].slide_dur = state == NOTE_ON ? n[I_MC_SLIDE_DUR] : 0;
    uint8_t bend = p->chn[t].bend; int16_t amount, detune = 0; uint8_t lo, hi;
    if (!(n[I_MC_SLIDE_COARSE] & 1)) {
        if ((int)bend - BEND_CENTER < 0) {
            amount = (int16_t)(BEND_CENTER - bend); lo = (uint8_t)(amount >> 5); hi = (uint8_t)((amount << 3) & 0xFF);
            key = (int8_t)(key - lo);
            if (key < 0) { key = (int8_t)(key + NUM_NOTES); oct--; }
            if (oct < 0) { key = 0; oct = 0; }
            detune = (int16_t)(-1 * ((fine_bend[key] * hi) >> 8));
        } else {
            amount = (int16_t)(bend - BEND_CENTER); lo = (uint8_t)(amount >> 5); hi = (uint8_t)((amount << 3) & 0xFF);
            key = (int8_t)(key + lo);
            if (key >= NUM_NOTES) { key = (int8_t)(key - NUM_NOTES); oct++; }
            detune = (int16_t)((fine_bend[key + 1] * hi) >> 8);
        }
    } else {
        uint8_t off;
        if ((int)bend - BEND_CENTER < 0) {
            amount = (int16_t)(BEND_CENTER - bend); key = (int8_t)(key - amount / 5);
            if (key < 0) { key = (int8_t)(key + NUM_NOTES); oct--; }
            if (oct < 0) { key = 0; oct = 0; }
            off = (uint8_t)((amount % 5) + (key >= 6 ? 5 : 0)); detune = (int16_t)(-1 * coarse_bend[off]);
        } else {
            amount = (int16_t)(bend - BEND_CENTER); key = (int8_t)(key + amount / 5);
            if (key >= NUM_NOTES) { key = (int8_t)(key - NUM_NOTES); oct++; }
            off = (uint8_t)((amount % 5) + (key >= 6 ? 5 : 0)); detune = coarse_bend[off];
        }
    }
    set_freq(p, t, (uint8_t)oct, (uint16_t)(FNum[key] + detune), state != NOTE_OFF);
}

static void ev_note_on(HeradPlayer *p, int ch, uint8_t note, uint8_t vel) {
    Chn *h = &p->chn[ch]; const HeradSong *s = p->s;
    if (h->keyon) { h->keyon = 0; play_note(p, ch, h->note, NOTE_OFF); }
    if (s->v2 && (int8_t)s->inst[h->program][I_MODE] == INSTMODE_KMAP) {
        int8_t mp = (int8_t)(note - (s->inst[h->program][2] + 24));          /* keymap: offset at byte 2, instrument index from byte 4 */
        if (mp < 0 || mp >= HERAD_INST_SIZE - 4) return;
        h->playprog = s->inst[h->program][4 + mp];
        if (h->playprog >= s->ninst) { h->playprog = h->program; return; }
        change_program(p, ch, h->playprog);
    }
    h->note = note; h->keyon = 1; h->bend = BEND_CENTER;
    if (s->v2 && (int8_t)s->inst[h->playprog][I_MODE] == INSTMODE_KMAP) return;
    play_note(p, ch, note, NOTE_ON);
    int8_t m = (int8_t)PI(p, ch, I_MC_MOD_OUT_VEL); if (m) macro_mod_output(p, ch, h->playprog, m, vel);
    m = (int8_t)PI(p, ch, I_MC_CAR_OUT_VEL); if (m) macro_car_output(p, ch, h->playprog, m, vel);
    m = (int8_t)PI(p, ch, I_MC_FB_VEL); if (m) macro_feedback(p, ch, h->playprog, m, vel);
    int i2 = second_inst(p, ch);                                              /* the second pair of a 4-operator voice has its own instrument and macros */
    if (i2 >= 0) {
        m = (int8_t)s->inst[i2][I_MC_MOD_OUT_VEL]; if (m) out_mod(p, vc(p, ch) + 3, i2, m, vel);
        m = (int8_t)s->inst[i2][I_MC_CAR_OUT_VEL]; if (m) out_car(p, vc(p, ch) + 3, i2, m, vel);
    }
}

/* after a note off the AGD driver gives the channels back when the track goes on with a program change or ends after a pause of at least 0x30 ticks */
static void maybe_release(HeradPlayer *p, int t) {
    if (!p->s->agd) return;
    const HeradTrack *d = &p->s->track[t]; uint16_t pos = p->trk[t].pos; uint32_t delay = 0;
    if (pos >= d->size) { if (t != 0) alloc_release(p, t); return; }
    do { delay = (delay << 7) | (d->data[pos] & 0x7F); } while ((d->data[pos++] & 0x80) && pos < d->size);
    if (delay < 0x30 || pos >= d->size) return;
    uint8_t st = d->data[pos];
    if (st == 0xFF || (st & 0xF0) == 0xC0) alloc_release(p, t);
}

static void ev_note_off(HeradPlayer *p, int ch, uint8_t note) {
    Chn *h = &p->chn[ch];
    if (note != h->note || !h->keyon) return;
    h->keyon = 0; play_note(p, ch, note, NOTE_OFF);
}

static void ev_aftertouch(HeradPlayer *p, int ch, uint8_t vel) {
    if (p->s->v2) return;
    Chn *h = &p->chn[ch]; int agd = p->s->agd;
    int8_t m = (int8_t)PI(p, ch, I_MC_MOD_OUT_AT); if (m) macro_mod_output(p, ch, h->playprog, m, vel);
    m = (int8_t)PI(p, ch, I_MC_CAR_OUT_AT); if (m && (agd || PI(p, ch, I_MC_CAR_OUT_VEL))) macro_car_output(p, ch, h->playprog, m, vel);
    m = (int8_t)PI(p, ch, I_MC_FB_AT); if (m) macro_feedback(p, ch, h->playprog, m, vel);
    int i2 = second_inst(p, ch);
    if (i2 >= 0) {
        const HeradSong *s = p->s;
        m = (int8_t)s->inst[i2][I_MC_MOD_OUT_AT]; if (m) out_mod(p, vc(p, ch) + 3, i2, m, vel);
        m = (int8_t)s->inst[i2][I_MC_CAR_OUT_AT]; if (m) out_car(p, vc(p, ch) + 3, i2, m, vel);
    }
}

static void execute_command(HeradPlayer *p, int t) {
    const HeradSong *s = p->s; Trk *k = &p->trk[t]; const HeradTrack *d = &s->track[t];
    if (t >= (s->agd ? NUM_VOICES * 2 : NUM_VOICES)) { k->pos = d->size; return; }
#define NEXT() (k->pos < d->size ? d->data[k->pos++] : (k->pos++, 0))
    uint8_t status = NEXT(), note, par;
    if (status == 0xFF) { k->pos = d->size; return; }
    switch (status & 0xF0) {
    case 0x80: { note = NEXT(); if (!s->v2) (void)NEXT(); int was = p->chn[t].keyon && note == p->chn[t].note; ev_note_off(p, t, note); if (was) maybe_release(p, t); } break;
    case 0x90: note = NEXT(); par = NEXT(); ev_note_on(p, t, note, par); break;
    case 0xA0: case 0xB0: k->pos += 2; break;
    case 0xC0: par = NEXT(); if (par < s->ninst) { p->chn[t].program = par; p->chn[t].playprog = par; change_program(p, t, par); } break;
    case 0xD0: par = NEXT(); ev_aftertouch(p, t, par); break;
    case 0xE0: par = NEXT(); p->chn[t].bend = par; if (p->chn[t].keyon) play_note(p, t, p->chn[t].note, NOTE_UPDATE); break;
    default: k->pos = d->size; break;
    }
#undef NEXT
}

static void process_events(HeradPlayer *p) {
    const HeradSong *s = p->s; p->songend = 1;
    for (int i = 0; i < s->ntracks; i++) {
        Trk *k = &p->trk[i]; const HeradTrack *d = &s->track[i]; Chn *h = &p->chn[i];
        if (h->slide_dur > 0 && h->keyon) {                       /* pitch slide macro */
            h->slide_dur--; h->bend = (uint8_t)(h->bend + (int8_t)PI(p, i, I_MC_SLIDE_RANGE));
            if (h->note & 0x7F) play_note(p, i, h->note, NOTE_UPDATE);
        }
        if (k->pos >= d->size) continue;
        p->songend = 0;
        if (!k->counter) {
            int first = k->pos == 0;
            k->ticks = (uint16_t)get_ticks(d, &k->pos);
            if (first && k->ticks) k->ticks++;                    /* there is always one excess tick at the start */
        }
        if (++k->counter >= k->ticks) {
            k->counter = 0;
            while (k->pos < d->size) {
                execute_command(p, i);
                if (k->pos >= d->size) { if (s->agd && i != 0) alloc_release(p, i); break; }
                else if (!d->data[k->pos]) k->pos++;             /* the next delay is zero */
                else break;
            }
        } else if (k->ticks >= 0x8000) { k->pos = d->size; k->counter = k->ticks; }
    }
    if (!p->songend) p->ticks_pos++;
}

int herad_update(HeradPlayer *p) {
    p->wTime = (int16_t)(p->wTime - 256);
    if (p->wTime < 0) { p->wTime = (int16_t)(p->wTime + p->s->speed); process_events(p); }
    return !p->songend;
}

int herad_position(const HeradPlayer *p, int *measure, int *tick) {
    uint32_t t = p->ticks_pos == ~0u ? 0 : p->ticks_pos;
    if (measure) *measure = (int)(t / HERAD_MEASURE_TICKS);
    if (tick) *tick = (int)(t % HERAD_MEASURE_TICKS);
    return 1;
}
