/* Sonic Arranger (Amiga, .sa): loader + replayer, a port of the NostalgicPlayer SonicArrangerWorker (MIT),
   with a Paula-like mixer in the style of aon.c. C# integer semantics are kept (ushort/short/sbyte wrap, truncating casts).
   Formats: SOARV1.0 (editor), @OARV1.0 (editor, parts packed with lh.library = LZHUF, decoded here) and the "final"
   format (replay code + data block, or the bare data block); the final format is not in the worker (NostalgicPlayer
   converts it in a separate converter), its loader here fills the same structures. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sa.h"

#define PAULA_PAL 3546895.0

/* ---------------------------------------------------------------- tables (Tables.cs) */
static const uint16_t sa_periods[109] = {
        0,
    13696, 12928, 12192, 11520, 10848, 10240,  9664,  9120,  8608,  8128,  7680,  7248,
     6848,  6464,  6096,  5760,  5424,  5120,  4832,  4560,  4304,  4064,  3840,  3624,
     3424,  3232,  3048,  2880,  2712,  2560,  2416,  2280,  2152,  2032,  1920,  1812,
     1712,  1616,  1524,  1440,  1356,  1280,  1208,  1140,  1076,  1016,   960,   906,
      856,   808,   762,   720,   678,   640,   604,   570,   538,   508,   480,   453,
      428,   404,   381,   360,   339,   320,   302,   285,   269,   254,   240,   226,
      214,   202,   190,   180,   170,   160,   151,   143,   135,   127,   120,   113,
      107,   101,    95,    90,    85,    80,    75,    71,    67,    63,    60,    56,
       53,    50,    47,    45,    42,    40,    37,    35,    33,    31,    30,    28
};
static const int8_t sa_vibrato[256] = {
       0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
      49,   52,   54,   57,   60,   63,   66,   68,   71,   73,   76,   78,   81,   83,   86,   88,
      90,   92,   94,   96,   98,  100,  102,  104,  106,  108,  109,  111,  112,  114,  115,  116,
     118,  119,  120,  121,  122,  123,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
     127,  127,  127,  127,  126,  126,  125,  125,  124,  124,  123,  122,  121,  120,  119,  118,
     117,  116,  114,  113,  112,  110,  108,  107,  105,  103,  101,   99,   97,   95,   93,   91,
      89,   87,   84,   82,   80,   77,   75,   72,   69,   67,   64,   61,   59,   56,   53,   50,
      47,   44,   41,   39,   36,   32,   29,   26,   23,   20,   17,   14,   11,    8,    5,    2,
      -1,   -4,   -7,  -10,  -14,  -17,  -20,  -23,  -26,  -29,  -32,  -35,  -38,  -41,  -44,  -47,
     -50,  -53,  -55,  -58,  -61,  -64,  -66,  -69,  -72,  -74,  -77,  -79,  -82,  -84,  -86,  -88,
     -91,  -93,  -95,  -97,  -99, -101, -103, -104, -106, -108, -109, -111, -112, -114, -115, -116,
    -118, -119, -120, -121, -122, -122, -123, -124, -124, -125, -125, -126, -126, -126, -126, -126,
    -126, -126, -126, -126, -126, -125, -125, -124, -124, -123, -122, -121, -120, -119, -118, -117,
    -116, -115, -113, -112, -110, -109, -107, -105, -104, -102, -100,  -98,  -96,  -94,  -92,  -90,
     -87,  -85,  -83,  -80,  -78,  -75,  -73,  -70,  -68,  -65,  -62,  -60,  -57,  -54,  -51,  -48,
     -45,  -42,  -39,  -37,  -34,  -30,  -27,  -24,  -21,  -18,  -15,  -12,   -9,   -6,   -3,    0
};
static const uint8_t pan4[4] = { 0, 1, 1, 0 };                    /* 0 = left, 1 = right */

/* ---------------------------------------------------------------- effects (Effect.cs, SynthesisEffect.cs) */
enum { FX_ARPEGGIO = 0, FX_SET_SLIDE_SPEED, FX_RESTART_ADSR, FX_SET_VIBRATO = 4, FX_SYNC, FX_SET_MASTER_VOLUME, FX_SET_PORTAMENTO,
       FX_SKIP_PORTAMENTO, FX_SET_TRACK_LEN, FX_VOLUME_SLIDE, FX_POSITION_JUMP, FX_SET_VOLUME, FX_TRACK_BREAK, FX_SET_FILTER, FX_SET_SPEED };
enum { SE_NONE, SE_WAVE_NEGATOR, SE_FREE_NEGATOR, SE_ROTATE_VERTICAL, SE_ROTATE_HORIZONTAL, SE_ALIEN_VOICE, SE_POLY_NEGATOR,
       SE_SHACK_WAVE1, SE_SHACK_WAVE2, SE_METAMORPH, SE_LASER, SE_WAVE_ALIAS, SE_NOISE_GENERATOR1, SE_LOW_PASS_FILTER1,
       SE_LOW_PASS_FILTER2, SE_OSZILATOR, SE_NOISE_GENERATOR2, SE_FM_DRUM };

/* ---------------------------------------------------------------- song */
typedef struct { uint16_t start_speed, rows_per_track, first_position, last_position, restart_position, tempo; } SaSongInfo;
typedef struct { uint16_t start_track_row; int8_t sound_transpose, note_transpose; } SaPosInfo;
typedef struct { uint8_t note, instrument, dis_sound_tr, dis_note_tr, arpeggio, effect, arg; } SaLine;
typedef struct { uint8_t length, repeat; int8_t values[14]; } SaArp;
typedef struct {
    uint16_t type, waveform_number, waveform_length, repeat_length, volume;
    int16_t fine_tuning;
    uint16_t portamento_speed, vibrato_delay, vibrato_speed, vibrato_level;
    uint16_t amf_number, amf_delay, amf_length, amf_repeat;
    uint16_t adsr_number, adsr_delay, adsr_length, adsr_repeat, sustain_point, sustain_delay;
    uint16_t effect_arg1, effect, effect_arg2, effect_arg3, effect_delay;
    SaArp arps[3];
} SaInst;

struct SaSong {
    int nsub; SaSongInfo *sub;
    int npos; SaPosInfo *pos;                                      /* [position][4] */
    int nlines; SaLine *lines;
    int ninst; SaInst *inst;
    int nsmp; int8_t **smp; uint32_t *smp_len;                     /* NULL = no sample data */
    int nwave; int8_t *wave;                                       /* [n][128] */
    int nadsr; uint8_t *adsr;                                      /* [n][128] */
    int namf; int8_t *amf;                                         /* [n][128] */
};

/* ---------------------------------------------------------------- player */
typedef struct {
    uint16_t start_track_row; int8_t sound_transpose, note_transpose;
    uint8_t note, instrument, dis_sound_tr, dis_note_tr, arpeggio, effect, arg;
    uint16_t transposed_note, prev_transposed_note;
    uint16_t instrument_type;
    int instr;                                                     /* InstrumentInfo (index, -1 = null) */
    uint16_t transposed_instrument, current_volume;
    int16_t volume_slide_speed;
    uint16_t vibrato_position, vibrato_delay, vibrato_speed, vibrato_level;
    uint16_t portamento_speed, portamento_period, arpeggio_position;
    int16_t slide_speed, slide_value;
    uint16_t adsr_position, adsr_delay_counter, sustain_delay_counter, amf_position, amf_delay_counter;
    uint16_t synth_effect_position, synth_effect_wave_position, effect_delay_counter;
    uint8_t flag;
    int8_t wbuf[128];                                              /* WaveformBuffer (played live by the mixer) */
} SaVoice;

typedef struct {
    const int8_t *d; uint32_t len; double pos; int on;
    const int8_t *ld; uint32_t llen;                               /* SetLoop: plays when the current pass ends */
    uint32_t period; int volume;
} SaMix;

struct SaPlayer {
    const SaSong *s;
    const SaSongInfo *si;
    int rate, sub;
    /* GlobalPlayingInfo */
    uint16_t master_volume, speed_counter, current_speed;
    int16_t song_position;
    uint16_t row_position, rows_per_track;
    SaVoice v[4];
    int end_reached, amiga_filter, playing_frequency;
    uint8_t *visited;
    int shown_pos, shown_row;
    uint32_t rnd;
    /* mixer */
    SaMix m[4];
    double tick_left;
    double lb0, lb1, lb2, la1, la2, lz[2][4];
    unsigned mute;
    float *sc; int sc_len; unsigned sc_pos;
    int ended;
};

/* ---------------------------------------------------------------- loader stream (ModuleStream) */
typedef struct { const uint8_t *b; size_t n; long long pos; int eof; } Rd;   /* eof = a read came up short (EndOfStream) */

static size_t rd_read(Rd *r, uint8_t *dst, size_t cnt) {
    size_t got = 0;
    if (r->pos >= 0 && (size_t)r->pos < r->n) { got = r->n - (size_t)r->pos; if (got > cnt) got = cnt; if (dst) memcpy(dst, r->b + r->pos, got); }
    if (dst && got < cnt) memset(dst + got, 0, cnt - got);
    r->pos += (long long)cnt;
    if (r->pos > (long long)r->n) r->pos = (long long)r->n;
    if (got < cnt) r->eof = 1;
    return got;
}
static uint8_t rd_u8(Rd *r) { uint8_t x; rd_read(r, &x, 1); return x; }
static uint16_t rd_b16(Rd *r) { uint8_t x[2]; rd_read(r, x, 2); return (uint16_t)(x[0] << 8 | x[1]); }
static uint32_t rd_b32(Rd *r) { uint8_t x[4]; rd_read(r, x, 4); return (uint32_t)x[0] << 24 | (uint32_t)x[1] << 16 | (uint32_t)x[2] << 8 | x[3]; }
static void rd_skip(Rd *r, long long off) { r->pos += off; if (r->pos < 0) r->pos = 0; }   /* Seek(Current): no EndOfStream */
static int rd_mark(Rd *r, const char *m) { uint8_t x[4]; rd_read(r, x, 4); return memcmp(x, m, 4) == 0; }
static long long rd_left(const Rd *r) { return r->pos < (long long)r->n ? (long long)r->n - r->pos : 0; }

/* ---------------------------------------------------------------- lh.library decompression
   LZHUF (Okumura) with 317 symbols (256 literals + lengths 1..61), distance = the decoded position, 4 KiB window
   (zero filled). NostalgicPlayer uses LibAncient (not in the given sources); the parameters were found by decoding the
   packed Axis against the unpacked Axis (identical position, track, instrument and sample data). */
#define LH_N 4096
#define LH_F 63                                                     /* 317 symbols; found against the packed/unpacked Axis pair */
#define LH_THRESHOLD 2
#define LH_NCHAR (256 - LH_THRESHOLD + LH_F)
#define LH_T (LH_NCHAR * 2 - 1)
#define LH_R (LH_T - 1)
#define LH_MAXFREQ 0x8000

typedef struct {
    const uint8_t *in; size_t n, ip; uint32_t bitbuf; int bitcnt; int err;
    unsigned freq[LH_T + 1]; int prnt[LH_T + LH_NCHAR]; int son[LH_T];
} Lh;

static const uint8_t lh_d_code[256] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
    0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,
    0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
    0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,
    0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0C,0x0C,0x0C,0x0C,0x0D,0x0D,0x0D,0x0D,0x0E,0x0E,0x0E,0x0E,0x0F,0x0F,0x0F,0x0F,
    0x10,0x10,0x10,0x10,0x11,0x11,0x11,0x11,0x12,0x12,0x12,0x12,0x13,0x13,0x13,0x13,
    0x14,0x14,0x14,0x14,0x15,0x15,0x15,0x15,0x16,0x16,0x16,0x16,0x17,0x17,0x17,0x17,
    0x18,0x18,0x19,0x19,0x1A,0x1A,0x1B,0x1B,0x1C,0x1C,0x1D,0x1D,0x1E,0x1E,0x1F,0x1F,
    0x20,0x20,0x21,0x21,0x22,0x22,0x23,0x23,0x24,0x24,0x25,0x25,0x26,0x26,0x27,0x27,
    0x28,0x28,0x29,0x29,0x2A,0x2A,0x2B,0x2B,0x2C,0x2C,0x2D,0x2D,0x2E,0x2E,0x2F,0x2F,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F
};
static uint8_t lh_d_len(int i) { return i < 32 ? 3 : i < 80 ? 4 : i < 144 ? 5 : i < 192 ? 6 : i < 240 ? 7 : 8; }

static int lh_bit(Lh *h) {
    if (h->bitcnt == 0) {
        if (h->ip >= h->n) { h->err = 1; return 0; }
        h->bitbuf = h->in[h->ip++]; h->bitcnt = 8;
    }
    h->bitcnt--;
    return (int)(h->bitbuf >> h->bitcnt) & 1;
}
static int lh_byte(Lh *h) { int c = 0; for (int i = 0; i < 8; i++) c = c << 1 | lh_bit(h); return c; }

static void lh_start(Lh *h) {
    int i, j;
    for (i = 0; i < LH_NCHAR; i++) { h->freq[i] = 1; h->son[i] = i + LH_T; h->prnt[i + LH_T] = i; }
    i = 0; j = LH_NCHAR;
    while (j <= LH_R) { h->freq[j] = h->freq[i] + h->freq[i + 1]; h->son[j] = i; h->prnt[i] = h->prnt[i + 1] = j; i += 2; j++; }
    h->freq[LH_T] = 0xffff; h->prnt[LH_R] = 0;
}
static void lh_reconst(Lh *h) {
    int i, j, k; unsigned f;
    for (i = j = 0; i < LH_T; i++) if (h->son[i] >= LH_T) { h->freq[j] = (h->freq[i] + 1) / 2; h->son[j] = h->son[i]; j++; }
    for (i = 0, j = LH_NCHAR; j < LH_T; i += 2, j++) {
        k = i + 1; f = h->freq[j] = h->freq[i] + h->freq[k];
        for (k = j - 1; f < h->freq[k]; k--) ;
        k++;
        memmove(&h->freq[k + 1], &h->freq[k], (size_t)(j - k) * sizeof h->freq[0]); h->freq[k] = f;
        memmove(&h->son[k + 1], &h->son[k], (size_t)(j - k) * sizeof h->son[0]); h->son[k] = i;
    }
    for (i = 0; i < LH_T; i++) { if ((k = h->son[i]) >= LH_T) h->prnt[k] = i; else h->prnt[k] = h->prnt[k + 1] = i; }
}
static void lh_update(Lh *h, int c) {
    int i, j, l; unsigned k;
    if (h->freq[LH_R] == LH_MAXFREQ) lh_reconst(h);
    c = h->prnt[c + LH_T];
    do {
        k = ++h->freq[c];
        if (k > h->freq[l = c + 1]) {
            while (k > h->freq[++l]) ;
            l--;
            h->freq[c] = h->freq[l]; h->freq[l] = k;
            i = h->son[c]; h->prnt[i] = l; if (i < LH_T) h->prnt[i + 1] = l;
            j = h->son[l]; h->son[l] = i;
            h->prnt[j] = c; if (j < LH_T) h->prnt[j + 1] = c;
            h->son[c] = j;
            c = l;
        }
    } while ((c = h->prnt[c]) != 0);
}

/* returns a malloc'd buffer of out_len bytes, NULL on error (DecompressionException) */
static uint8_t *lh_decompress(const uint8_t *in, size_t in_len, uint32_t out_len) {
    if (out_len > 0x10000000u) return NULL;
    Lh *h = (Lh *)calloc(1, sizeof *h);
    uint8_t *out = (uint8_t *)malloc(out_len ? out_len : 1);
    uint8_t text[LH_N];
    if (!h || !out) { free(h); free(out); return NULL; }
    h->in = in; h->n = in_len;
    lh_start(h);
    memset(text, 0, LH_N - LH_F);
    memset(text + LH_N - LH_F, 0, LH_F);
    int r = LH_N - LH_F;
    uint32_t count = 0;
    while (count < out_len && !h->err) {
        int c = h->son[LH_R];
        while (c < LH_T && !h->err) { c += lh_bit(h); c = h->son[c]; }
        if (h->err) break;
        c -= LH_T;
        lh_update(h, c);
        if (c < 256) { out[count++] = (uint8_t)c; text[r++] = (uint8_t)c; r &= LH_N - 1; }
        else {
            int i = lh_byte(h);
            int cc = lh_d_code[i] << 6, j = lh_d_len(i) - 2;
            while (j--) i = (i << 1) + lh_bit(h);
            int dpos = cc | (i & 0x3f);
            int st = (r - dpos) & (LH_N - 1), len = c - 255;                /* copy length c - 255 from distance dpos */
            for (int k = 0; k < len && count < out_len; k++) {
                uint8_t b = text[(st + k) & (LH_N - 1)];
                out[count++] = b; text[r++] = b; r &= LH_N - 1;
            }
        }
    }
    int err = h->err;
    free(h);
    if (err) { free(out); return NULL; }
    return out;
}

/* ---------------------------------------------------------------- loader parts (C# Read* methods; count < 0 = read it from the stream) */
static int read_sub_songs(SaSong *s, Rd *r, long long count) {
    uint32_t n = count < 0 ? rd_b32(r) : (uint32_t)count;
    free(s->sub); s->sub = NULL; s->nsub = 0;
    for (uint32_t i = 0; i < n; i++) {
        SaSongInfo si;
        si.start_speed = rd_b16(r); si.rows_per_track = rd_b16(r); si.first_position = rd_b16(r);
        si.last_position = rd_b16(r); si.restart_position = rd_b16(r); si.tempo = rd_b16(r);
        if (r->eof) return 0;
        if (si.last_position == 0xffff || si.restart_position == 0xffff) continue;
        int dup = 0;
        for (int k = 0; k < s->nsub; k++) if (s->sub[k].first_position == si.first_position && s->sub[k].last_position == si.last_position) { dup = 1; break; }
        if (dup) continue;
        SaSongInfo *ns = (SaSongInfo *)realloc(s->sub, (size_t)(s->nsub + 1) * sizeof *ns);
        if (!ns) return 0;
        s->sub = ns; s->sub[s->nsub++] = si;
    }
    return 1;
}

static int read_positions(SaSong *s, Rd *r, long long count) {
    uint32_t n = count < 0 ? rd_b32(r) : (uint32_t)count;
    if ((long long)n * 16 > rd_left(r) + 16) return 0;            /* would end in EndOfStream anyway (or OutOfMemory) */
    free(s->pos); s->pos = (SaPosInfo *)calloc((size_t)(n ? n : 1) * 4, sizeof(SaPosInfo)); s->npos = 0;
    if (!s->pos) return 0;
    for (uint32_t i = 0; i < n; i++) {
        for (int j = 0; j < 4; j++) {
            SaPosInfo *p = &s->pos[(size_t)i * 4 + (size_t)j];
            p->start_track_row = rd_b16(r); p->sound_transpose = (int8_t)rd_u8(r); p->note_transpose = (int8_t)rd_u8(r);
        }
        if (r->eof) return 0;
    }
    s->npos = (int)n;
    return 1;
}

static int read_track_rows(SaSong *s, Rd *r, long long count) {
    uint32_t n = count < 0 ? rd_b32(r) : (uint32_t)count;
    if ((long long)n * 4 > rd_left(r) + 4) return 0;
    free(s->lines); s->lines = (SaLine *)calloc(n ? n : 1, sizeof(SaLine)); s->nlines = 0;
    if (!s->lines) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b1 = rd_u8(r), b2 = rd_u8(r), b3 = rd_u8(r), b4 = rd_u8(r);
        SaLine *l = &s->lines[i];
        l->note = b1; l->instrument = b2;
        l->dis_sound_tr = (b3 & 0x80) != 0; l->dis_note_tr = (b3 & 0x40) != 0;
        l->arpeggio = (uint8_t)((b3 & 0x30) >> 4);
        l->effect = (uint8_t)(b3 & 0x0f);
        l->arg = b4;
        if (r->eof) return 0;
    }
    s->nlines = (int)n;
    return 1;
}

static int read_instruments(SaSong *s, Rd *r, long long count) {
    uint32_t n = count < 0 ? rd_b32(r) : (uint32_t)count;
    if ((long long)n * 152 > rd_left(r) + 152) return 0;
    free(s->inst); s->inst = (SaInst *)calloc(n ? n : 1, sizeof(SaInst)); s->ninst = 0;
    if (!s->inst) return 0;
    for (uint32_t i = 0; i < n; i++) {
        SaInst *in = &s->inst[i];
        in->type = rd_b16(r); in->waveform_number = rd_b16(r); in->waveform_length = rd_b16(r); in->repeat_length = rd_b16(r);
        if (r->eof) return 0;
        rd_skip(r, 8);
        in->volume = (uint16_t)(rd_b16(r) & 0xff);
        in->fine_tuning = (int8_t)rd_b16(r);                        /* (sbyte)Read_B_UINT16() */
        in->portamento_speed = rd_b16(r);
        in->vibrato_delay = rd_b16(r); in->vibrato_speed = rd_b16(r); in->vibrato_level = rd_b16(r);
        in->amf_number = rd_b16(r); in->amf_delay = rd_b16(r); in->amf_length = rd_b16(r); in->amf_repeat = rd_b16(r);
        in->adsr_number = rd_b16(r); in->adsr_delay = rd_b16(r); in->adsr_length = rd_b16(r); in->adsr_repeat = rd_b16(r);
        in->sustain_point = rd_b16(r); in->sustain_delay = rd_b16(r);
        if (r->eof) return 0;
        rd_skip(r, 16);
        in->effect_arg1 = rd_b16(r); in->effect = rd_b16(r); in->effect_arg2 = rd_b16(r); in->effect_arg3 = rd_b16(r); in->effect_delay = rd_b16(r);
        for (int j = 0; j < 3; j++) {
            in->arps[j].length = rd_u8(r); in->arps[j].repeat = rd_u8(r);
            if (rd_read(r, (uint8_t *)in->arps[j].values, 14) < 14) return 0;
        }
        rd_read(r, NULL, 30);                                          /* name (not kept) */
    }
    s->ninst = (int)n;
    return 1;
}

static int read_tables(Rd *r, long long count, uint8_t **dst, int *ndst) {   /* SYWT / SYAR / SYAF: n x 128 bytes */
    uint32_t n = count < 0 ? rd_b32(r) : (uint32_t)count;
    if ((long long)n * 128 > rd_left(r) + 128) return 0;
    free(*dst); *dst = (uint8_t *)calloc((size_t)(n ? n : 1) * 128, 1); *ndst = 0;
    if (!*dst) return 0;
    for (uint32_t i = 0; i < n; i++) if (rd_read(r, *dst + (size_t)i * 128, 128) < 128) return 0;
    *ndst = (int)n;
    return 1;
}
static int read_waveforms(SaSong *s, Rd *r, long long c) { return read_tables(r, c, (uint8_t **)&s->wave, &s->nwave); }
static int read_adsr(SaSong *s, Rd *r, long long c) { return read_tables(r, c, &s->adsr, &s->nadsr); }
static int read_amf(SaSong *s, Rd *r, long long c) { return read_tables(r, c, (uint8_t **)&s->amf, &s->namf); }

static int alloc_samples(SaSong *s, int32_t n) {
    s->smp = (int8_t **)calloc((size_t)(n ? n : 1), sizeof(int8_t *));
    s->smp_len = (uint32_t *)calloc((size_t)(n ? n : 1), sizeof(uint32_t));
    s->nsmp = n;
    return s->smp && s->smp_len;
}

/* ReadSampleData (after the count): n lengths, then the data */
static int read_sample_data(SaSong *s, Rd *r, int32_t n) {
    if (n < 0 || (long long)n * 4 > rd_left(r) + 4) return 0;   /* C#: new sbyte[negative][] would throw */
    if (!alloc_samples(s, n)) return 0;
    if (n > 0) {
        uint32_t *lens = (uint32_t *)calloc((size_t)n, sizeof(uint32_t));
        if (!lens) return 0;
        for (int32_t i = 0; i < n; i++) lens[i] = rd_b32(r);
        if (r->eof) { free(lens); return 0; }
        for (int32_t i = 0; i < n; i++) {
            uint32_t len = lens[i];
            if (len > 0) {
                if ((long long)len > rd_left(r)) { free(lens); return 0; }   /* readBytes < sampleLen */
                s->smp[i] = (int8_t *)malloc(len);
                if (!s->smp[i]) { free(lens); return 0; }
                rd_read(r, (uint8_t *)s->smp[i], len);
                s->smp_len[i] = len;
            }
        }
        free(lens);
    }
    return 1;
}

/* ReadSampleInformation: count + skip of lengths/loop lengths/names */
static int read_sample_info(Rd *r, int32_t *n) {
    *n = (int32_t)rd_b32(r);
    if (*n < 0 || *n > 0x1000000) return 0;                          /* C# would overflow / throw */
    if (*n != 0) rd_skip(r, (long long)*n * 4 * 2 + (long long)*n * 30);
    return 1;
}

typedef int (*ReadFn)(SaSong *, Rd *, long long);

/* ReadCompressedData */
static int read_compressed(SaSong *s, Rd *r, uint32_t elem, ReadFn fn) {
    uint32_t count = rd_b32(r);
    if (count == 0) return fn(s, r, 0);
    uint32_t clen = rd_b32(r);
    if (r->eof) return 0;
    uint64_t dlen = (uint64_t)count * elem;
    if (dlen > 0x10000000u) return 0;
    long long start = r->pos;
    size_t avail = (size_t)rd_left(r); if (avail > clen) avail = clen;
    uint8_t *d = lh_decompress(r->b + start, avail, (uint32_t)dlen);
    if (!d) return 0;
    rd_skip(r, clen);
    Rd ms = { d, (size_t)dlen, 0, 0 };
    int ok = fn(s, &ms, count);
    free(d);
    return ok;
}

static int read_compressed_sample_data(SaSong *s, Rd *r, int32_t n) {
    if (n < 0 || (long long)n * 4 > rd_left(r) + 4) return 0;
    if (!alloc_samples(s, n)) return 0;
    if (n > 0) {
        uint32_t *lens = (uint32_t *)calloc((size_t)n, sizeof(uint32_t));
        if (!lens) return 0;
        for (int32_t i = 0; i < n; i++) lens[i] = rd_b32(r);
        if (r->eof) { free(lens); return 0; }
        for (int32_t i = 0; i < n; i++) {
            uint32_t len = lens[i];
            if (len > 0) {
                uint32_t clen = rd_b32(r);
                size_t avail = (size_t)rd_left(r); if (avail > clen) avail = clen;
                uint8_t *d = lh_decompress(r->b + r->pos, avail, len);
                if (!d) { free(lens); return 0; }
                rd_skip(r, clen);
                s->smp[i] = (int8_t *)d; s->smp_len[i] = len;
            }
        }
        free(lens);
    }
    return 1;
}

/* ---------------------------------------------------------------- final format (not in the worker) */
/* the data block starts with 8 offsets (from the block start): sub-songs (12 bytes each), positions (16), track rows (4),
   instruments (152), waveforms (128), ADSR (128), AMF (128), samples (count, count lengths, data); then 8 more bytes. */
static long long final_data_start(const uint8_t *b, size_t n) {
    if (n < 0x40) return -1;
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0x28) return 0;
    if (b[0] != 0x4e || b[1] != 0xfa) return -1;                      /* jmp xx(pc) to the init routine */
    long long t = 2 + (int16_t)(b[2] << 8 | b[3]);
    if (t < 0 || t + 8 > (long long)n) return -1;
    if (b[t] == 0x48 && b[t + 1] == 0xe7) t += 4;                     /* movem.l ...,-(sp) */
    if (t + 4 > (long long)n || b[t] != 0x41 || b[t + 1] != 0xfa) return -1;   /* lea xx(pc),a0 = the data block */
    long long d = t + 2 + (int16_t)(b[t + 2] << 8 | b[t + 3]);
    if (d < 0 || d + 0x28 > (long long)n) return -1;
    return d;
}
static int final_offsets(const uint8_t *x, size_t n, uint32_t off[8]) {
    for (int i = 0; i < 8; i++) off[i] = (uint32_t)x[i * 4] << 24 | (uint32_t)x[i * 4 + 1] << 16 | (uint32_t)x[i * 4 + 2] << 8 | x[i * 4 + 3];
    if (off[0] != 0x28) return 0;
    for (int i = 1; i < 8; i++) if (off[i] < off[i - 1]) return 0;
    if ((size_t)off[7] + 4 > n) return 0;
    if ((off[1] - off[0]) % 12 || (off[2] - off[1]) % 16 || (off[3] - off[2]) % 4 || (off[4] - off[3]) % 152 ||
        (off[5] - off[4]) % 128 || (off[6] - off[5]) % 128 || (off[7] - off[6]) % 128) return 0;
    return 1;
}

static int load_final(SaSong *s, const uint8_t *b, size_t n) {
    long long d = final_data_start(b, n);
    if (d < 0) return 0;
    const uint8_t *x = b + d; size_t xn = n - (size_t)d;
    uint32_t off[8];
    if (!final_offsets(x, xn, off)) return 0;
    Rd r = { x, xn, 0, 0 };
    r.pos = off[0]; if (!read_sub_songs(s, &r, (off[1] - off[0]) / 12)) return 0;
    r.pos = off[1]; if (!read_positions(s, &r, (off[2] - off[1]) / 16)) return 0;
    r.pos = off[2]; if (!read_track_rows(s, &r, (off[3] - off[2]) / 4)) return 0;
    r.pos = off[3]; if (!read_instruments(s, &r, (off[4] - off[3]) / 152)) return 0;
    r.pos = off[4]; if (!read_waveforms(s, &r, (off[5] - off[4]) / 128)) return 0;
    r.pos = off[5]; if (!read_adsr(s, &r, (off[6] - off[5]) / 128)) return 0;
    r.pos = off[6]; if (!read_amf(s, &r, (off[7] - off[6]) / 128)) return 0;
    r.pos = off[7];
    int32_t ns = (int32_t)rd_b32(&r);
    if (ns < 0 || ns > 0x10000) return 0;
    return read_sample_data(s, &r, ns);
}

/* ---------------------------------------------------------------- probe / load */
int sa_probe(const uint8_t *b, size_t n) {
    if (!b || n < 16) return 0;
    if (!memcmp(b, "SOARV1.0", 8) || !memcmp(b, "@OARV1.0", 8)) return 1;
    long long d = final_data_start(b, n);
    if (d < 0) return 0;
    uint32_t off[8];
    return final_offsets(b + d, n - (size_t)d, off);
}

void sa_free(SaSong *s) {
    if (!s) return;
    if (s->smp) for (int i = 0; i < s->nsmp; i++) free(s->smp[i]);
    free(s->smp); free(s->smp_len); free(s->sub); free(s->pos); free(s->lines); free(s->inst);
    free(s->wave); free(s->adsr); free(s->amf); free(s);
}

static int load_normal(SaSong *s, Rd *r) {
    int32_t ns;
    if (!rd_mark(r, "STBL") || !read_sub_songs(s, r, -1)) return 0;
    if (!rd_mark(r, "OVTB") || !read_positions(s, r, -1)) return 0;
    if (!rd_mark(r, "NTBL") || !read_track_rows(s, r, -1)) return 0;
    if (!rd_mark(r, "INST") || !read_instruments(s, r, -1)) return 0;
    if (!rd_mark(r, "SD8B") || !read_sample_info(r, &ns)) return 0;
    if (!read_sample_data(s, r, ns)) return 0;
    if (!rd_mark(r, "SYWT") || !read_waveforms(s, r, -1)) return 0;
    if (!rd_mark(r, "SYAR") || !read_adsr(s, r, -1)) return 0;
    if (!rd_mark(r, "SYAF") || !read_amf(s, r, -1)) return 0;
    return 1;
}

static int load_compressed(SaSong *s, Rd *r) {
    int32_t ns;
    if (!rd_mark(r, "@TBL") || !read_sub_songs(s, r, -1)) return 0;
    if (!rd_mark(r, "@VTB") || !read_compressed(s, r, 4 * 4, read_positions)) return 0;
    if (!rd_mark(r, "@TBL") || !read_compressed(s, r, 4, read_track_rows)) return 0;
    if (!rd_mark(r, "@NST") || !read_compressed(s, r, 152, read_instruments)) return 0;
    if (!rd_mark(r, "@D8B") || !read_sample_info(r, &ns)) return 0;
    if (!read_compressed_sample_data(s, r, ns)) return 0;
    if (!rd_mark(r, "@YWT") || !read_compressed(s, r, 128, read_waveforms)) return 0;
    if (!rd_mark(r, "@YAR") || !read_compressed(s, r, 128, read_adsr)) return 0;
    if (!rd_mark(r, "@YAF") || !read_compressed(s, r, 128, read_amf)) return 0;
    return 1;
}

SaSong *sa_load(const uint8_t *b, size_t n, int *err) {
    if (err) *err = 0;
    if (!sa_probe(b, n)) { if (err) *err = 1; return NULL; }
    SaSong *s = (SaSong *)calloc(1, sizeof *s);
    if (!s) { if (err) *err = 2; return NULL; }
    int ok;
    if (!memcmp(b, "SOARV1.0", 8) || !memcmp(b, "@OARV1.0", 8)) {
        Rd r = { b, n, 8, 0 };
        ok = b[0] == '@' ? load_compressed(s, &r) : load_normal(s, &r);
    } else ok = load_final(s, b, n);
    /* no sub-song left: InitSound would index an empty array */
    if (!ok || s->nsub == 0 || !s->pos || !s->lines || !s->inst || !s->smp || !s->wave || !s->adsr || !s->amf) {
        sa_free(s); if (err) *err = 3; return NULL;
    }
    return s;
}

/* ---------------------------------------------------------------- mixer channel (IChannel) */
static void ch_play_sample(SaMix *m, const int8_t *data, uint32_t off, uint32_t len) {
    m->d = data + off; m->len = len; m->pos = 0; m->on = len > 0;
    m->ld = NULL; m->llen = 0;
}
static void ch_set_loop(SaMix *m, uint32_t start, uint32_t len) {    /* relative to the sample given to PlaySample */
    if (!m->d) return;
    m->ld = m->d + start; m->llen = len;
}
static void ch_mute(SaMix *m) { m->on = 0; }

/* ---------------------------------------------------------------- helpers */
static const SaInst *vinst(const SaPlayer *p, const SaVoice *v) { return v->instr >= 0 ? &p->s->inst[v->instr] : NULL; }
static const int8_t *wave_of(const SaSong *s, unsigned n) { return n < (unsigned)s->nwave ? s->wave + (size_t)n * 128 : NULL; }
/* WaveformBuffer / waveformData accesses: C# would throw outside 0..127; reads give 0, writes are dropped */
static int8_t wget(const int8_t *w, long i) { return (w && i >= 0 && i < 128) ? w[i] : 0; }
static void wset(int8_t *w, long i, int8_t x) { if (i >= 0 && i < 128) w[i] = x; }
static int sa_rand(SaPlayer *p) {                                     /* RandomGenerator.GetRandomNumber(-128, 127) */
    p->rnd ^= p->rnd << 13; p->rnd ^= p->rnd >> 17; p->rnd ^= p->rnd << 5;
    return (int)(p->rnd % 256u) - 128;
}

/* ---------------------------------------------------------------- InitializeSound */
static void initialize_sound(SaPlayer *p, int sub) {
    const SaSongInfo *si = &p->s->sub[sub];
    p->si = si;
    p->master_volume = 64;
    p->speed_counter = si->start_speed; p->current_speed = si->start_speed;
    p->song_position = (int16_t)(si->first_position - 1);
    p->row_position = si->rows_per_track; p->rows_per_track = si->rows_per_track;
    memset(p->v, 0, sizeof p->v);
    for (int i = 0; i < 4; i++) { p->v[i].instr = -1; p->v[i].flag = 0x01; p->v[i].effect = FX_ARPEGGIO; }
    p->playing_frequency = si->tempo;
    p->end_reached = 0;
}

/* ---------------------------------------------------------------- voice setup */
static void force_quiet(SaVoice *v, SaMix *c) {
    ch_mute(c);
    v->current_volume = 0; v->transposed_instrument = 0; v->instr = -1; v->instrument_type = 0;
}

static void set_synth_instrument(SaVoice *v, const SaInst *in) {
    v->slide_value = in->fine_tuning;
    v->adsr_delay_counter = in->adsr_delay; v->adsr_position = 0;
    v->amf_delay_counter = in->amf_delay; v->amf_position = 0;
    v->synth_effect_position = in->effect_arg2; v->synth_effect_wave_position = 0; v->effect_delay_counter = in->effect_delay;
    v->arpeggio_position = 0;
    v->flag = 0x00;
}

static const SaInst *set_instrument(SaPlayer *p, SaVoice *v, uint16_t instr_num) {
    v->transposed_instrument = (uint16_t)(instr_num & 0xff);
    int idx = v->transposed_instrument - 1;
    if (idx < 0 || idx >= p->s->ninst) return NULL;                   /* C#: instruments[...] out of range -> exception */
    const SaInst *in = &p->s->inst[idx];
    v->instr = idx;
    v->current_volume = in->volume;
    v->portamento_speed = in->portamento_speed; v->portamento_period = 0;
    v->vibrato_position = 0; v->vibrato_delay = in->vibrato_delay; v->vibrato_speed = in->vibrato_speed; v->vibrato_level = in->vibrato_level;
    set_synth_instrument(v, in);
    return in;
}

static void play_sample_instrument(SaPlayer *p, SaVoice *v, SaMix *c, const SaInst *in) {
    const SaSong *s = p->s;
    const int8_t *data = in->waveform_number < s->nsmp ? s->smp[in->waveform_number] : NULL;   /* C#: out of range -> exception */
    if (!data) { v->flag |= 0x01; force_quiet(v, c); return; }
    uint32_t dlen = s->smp_len[in->waveform_number];
    uint32_t play_length = in->waveform_length, loop_start = 0, loop_length = 0;
    if (in->repeat_length == 0) loop_length = in->waveform_length;      /* C#: words used as bytes here (kept) */
    else if (in->repeat_length != 1) {
        play_length += in->repeat_length;
        loop_start = in->waveform_length * 2u;
        loop_length = in->repeat_length * 2u;
    }
    play_length = play_length * 2 < dlen ? play_length * 2 : dlen;
    if (loop_start + loop_length > play_length) loop_length = play_length - loop_start;
    if (play_length > 0) {
        ch_play_sample(c, data, 0, play_length);
        if (loop_length != 0 && loop_start < dlen && (uint64_t)loop_start + loop_length <= dlen)   /* a wrapped length would throw */
            ch_set_loop(c, loop_start, loop_length);
    } else { v->flag |= 0x01; force_quiet(v, c); }
}

static void play_synth_instrument(SaPlayer *p, SaVoice *v, SaMix *c, const SaInst *in) {
    const int8_t *data = wave_of(p->s, in->waveform_number);
    if (!data) { v->flag |= 0x01; force_quiet(v, c); return; }        /* C#: out of range -> exception */
    uint32_t length = in->waveform_length * 2u;
    if (length > 128) length = 128;                                     /* C#: Array.Copy past the 128 byte buffer -> exception */
    ch_play_sample(c, v->wbuf, 0, length);
    ch_set_loop(c, 0, length);
    memcpy(v->wbuf, data, length);
}

static void set_effects(SaPlayer *p, SaVoice *v) {
    v->slide_speed = 0; v->volume_slide_speed = 0;
    switch (v->effect) {
        case FX_SET_SLIDE_SPEED: v->slide_speed = (int8_t)v->arg; break;
        case FX_RESTART_ADSR: v->adsr_position = v->arg; break;
        case FX_SET_VIBRATO:
            v->vibrato_delay = 0;
            v->vibrato_speed = (uint16_t)((v->arg & 0xf0) >> 3);
            v->vibrato_level = (uint16_t)((-((v->arg & 0x0f) << 4)) + 160);
            break;
        case FX_SET_MASTER_VOLUME: p->master_volume = (uint16_t)(v->arg == 64 ? 64 : (v->arg & 0x3f)); break;
        case FX_SET_PORTAMENTO: v->portamento_speed = v->arg; break;
        case FX_SKIP_PORTAMENTO: v->portamento_speed = 0; break;
        case FX_SET_TRACK_LEN: if (v->arg <= 64) p->rows_per_track = v->arg; break;
        case FX_VOLUME_SLIDE: v->volume_slide_speed = (int8_t)v->arg; break;
        case FX_POSITION_JUMP: p->song_position = (int16_t)(v->arg - 1); p->row_position = p->rows_per_track; break;
        case FX_SET_VOLUME: { uint8_t nv = v->arg; if (nv > 64) nv = 64; v->current_volume = nv; break; }
        case FX_TRACK_BREAK: p->row_position = p->rows_per_track; break;
        case FX_SET_FILTER: p->amiga_filter = v->arg == 0; break;
        case FX_SET_SPEED: if (v->arg > 0 && v->arg <= 16) p->current_speed = v->arg; break;
    }
}

static void play_voice(SaPlayer *p, SaVoice *v, SaMix *c) {
    uint16_t note = v->note;
    int16_t instr_num = v->instrument;
    if (note == 0) {
        if (instr_num != 0) set_instrument(p, v, (uint16_t)instr_num);
    } else if (note != 0x80) {
        if (note == 0x7f) force_quiet(v, c);
        else {
            if (!v->dis_note_tr) note = (uint16_t)(note + v->note_transpose);
            if (instr_num != 0 && !v->dis_sound_tr) instr_num = (int16_t)(instr_num + v->sound_transpose);
            v->prev_transposed_note = v->transposed_note;
            v->transposed_note = note;
            if (v->prev_transposed_note == 0) v->prev_transposed_note = note;
            const SaInst *in;
            if (instr_num < 0) { v->flag |= 0x01; force_quiet(v, c); return; }
            if (instr_num == 0) {
                in = vinst(p, v);
                if (!in) { force_quiet(v, c); return; }
                set_synth_instrument(v, in);
            } else {
                in = set_instrument(p, v, (uint16_t)instr_num);
                if (!in) { v->flag |= 0x01; force_quiet(v, c); return; }   /* C# would throw */
            }
            v->instrument_type = in->type;
            if (v->instrument_type == 0) play_sample_instrument(p, v, c, in);
            else play_synth_instrument(p, v, c, in);
        }
    }
    set_effects(p, v);
}

/* ---------------------------------------------------------------- rows / positions */
static void get_next_position(SaPlayer *p) {
    const SaSong *s = p->s;
    p->song_position++;
    if (p->song_position > p->si->last_position || p->song_position >= s->npos) p->song_position = (int16_t)p->si->restart_position;
    if (p->song_position < 0 || p->song_position >= s->npos) { p->end_reached = 1; p->ended = 1; return; }   /* C#: positions[] out of range -> exception */
    if (p->visited[p->song_position]) p->end_reached = 1;
    p->visited[p->song_position] = 1;
    const SaPosInfo *row = &s->pos[(size_t)p->song_position * 4];
    for (int i = 0; i < 4; i++) {
        p->v[i].start_track_row = row[i].start_track_row;
        p->v[i].sound_transpose = row[i].sound_transpose;
        p->v[i].note_transpose = row[i].note_transpose;
    }
}

static void get_new_notes(SaPlayer *p) {
    static const SaLine empty;
    p->shown_pos = p->song_position - p->si->first_position; p->shown_row = p->row_position;
    for (int i = 0; i < 4; i++) {
        SaVoice *v = &p->v[i];
        int position = v->start_track_row + p->row_position;
        const SaLine *l = position < p->s->nlines ? &p->s->lines[position] : &empty;
        v->note = l->note; v->instrument = l->instrument;
        v->dis_sound_tr = l->dis_sound_tr; v->dis_note_tr = l->dis_note_tr;
        v->arpeggio = l->arpeggio; v->effect = l->effect; v->arg = l->arg;
        play_voice(p, v, &p->m[i]);
    }
}

static void get_next_row(SaPlayer *p) {
    p->row_position++;
    if (p->row_position >= p->rows_per_track) {
        p->row_position = 0;
        get_next_position(p);
        if (p->ended) return;
    }
    get_new_notes(p);
}

/* ---------------------------------------------------------------- effects per tick */
typedef struct { uint16_t period, previous_period; } PeriodInfo;

static PeriodInfo do_arpeggio(SaPlayer *p, SaVoice *v, const SaInst *in) {
    uint16_t note = v->transposed_note, prev = v->prev_transposed_note;
    if (v->arpeggio != 0) {
        const SaArp *arp = &in->arps[v->arpeggio - 1];
        int8_t av = v->arpeggio_position < 14 ? arp->values[v->arpeggio_position] : 0;   /* C#: Values[>= 14] -> exception */
        note = (uint16_t)(note + av);
        prev = (uint16_t)(prev + av);
        v->arpeggio_position++;
        int max_length = arp->length + arp->repeat < 13 ? arp->length + arp->repeat : 13;
        if (v->arpeggio_position > max_length) v->arpeggio_position = arp->length;
    } else if (v->effect == FX_ARPEGGIO && v->arg != 0) {
        uint16_t av;
        switch (p->speed_counter % 3) {
            default: case 0: av = 0; break;
            case 1: av = (uint16_t)(v->arg >> 4); break;
            case 2: av = (uint16_t)(v->arg & 0x0f); break;
        }
        note = (uint16_t)(note + av);
        prev = (uint16_t)(prev + av);
    }
    if (note >= 109) note = 0;
    if (prev >= 109) prev = 0;
    PeriodInfo pi = { sa_periods[note], sa_periods[prev] };
    return pi;
}

static void do_portamento(PeriodInfo *pi, SaVoice *v) {
    if (v->portamento_speed != 0) {
        if (v->portamento_period == 0) v->portamento_period = pi->previous_period;
        int diff = pi->period - v->portamento_period;
        if (diff < 0) diff = -diff;
        diff -= v->portamento_speed;
        if (diff < 0) v->portamento_speed = 0;
        else {
            uint16_t np = v->portamento_period;
            if (np >= pi->period) np = (uint16_t)(np - v->portamento_speed);
            else np = (uint16_t)(np + v->portamento_speed);
            v->portamento_period = np;
            pi->period = np;
        }
    }
}

static void do_vibrato(PeriodInfo *pi, SaVoice *v, const SaInst *in) {
    if (v->vibrato_delay != 255) {
        if (v->vibrato_delay == 0) {
            int8_t vv = sa_vibrato[v->vibrato_position & 0xff];
            uint16_t level = in->vibrato_level;                         /* the instrument's values, not the voice's (as in the C#) */
            if (vv != 0 && level != 0)                                  /* C#: level 0 -> DivideByZeroException */
                pi->period = (uint16_t)(pi->period + (uint16_t)((vv * 4) / level));
            v->vibrato_position = (uint16_t)((v->vibrato_position + in->vibrato_speed) & 0xff);
        } else v->vibrato_delay--;
    }
}

static void do_amf(SaPlayer *p, PeriodInfo *pi, SaVoice *v, const SaInst *in) {
    if (in->amf_length + in->amf_repeat != 0) {
        int8_t av = 0;
        if (in->amf_number < p->s->namf && v->amf_position < 128) av = p->s->amf[(size_t)in->amf_number * 128 + v->amf_position];   /* else C# throws */
        pi->period = (uint16_t)(pi->period - av);
        v->amf_delay_counter--;
        if (v->amf_delay_counter == 0) {
            v->amf_delay_counter = in->amf_delay;
            v->amf_position++;
            if (v->amf_position >= in->amf_length + in->amf_repeat) {
                v->amf_position = in->amf_length;
                if (in->amf_repeat == 0) v->amf_position--;
            }
        }
    }
}

static void do_slide(SaPlayer *p, PeriodInfo *pi, SaVoice *v) {
    pi->period = (uint16_t)(pi->period - v->slide_value);
    if (pi->period < 113) pi->period = 113;
    if (p->speed_counter != 0) v->slide_value = (int16_t)(v->slide_value + v->slide_speed);
}

static void set_volume(SaMix *c, unsigned vol) { c->volume = (int)(vol > 64 ? 64 : vol); }   /* SetAmigaVolume: 0..64 */

static void do_adsr(SaPlayer *p, SaVoice *v, SaMix *c, const SaInst *in) {
    if (in->adsr_length + in->adsr_repeat == 0) set_volume(c, (uint16_t)((v->current_volume * p->master_volume) / 64));
    else {
        uint8_t av = 0;
        if (in->adsr_number < p->s->nadsr && v->adsr_position < 128) av = p->s->adsr[(size_t)in->adsr_number * 128 + v->adsr_position];   /* else C# throws */
        uint16_t vol = (uint16_t)((p->master_volume * v->current_volume * av) / 4096);
        if (vol > 64) vol = 64;
        set_volume(c, vol);
        if (v->note == 0x80 && v->adsr_position >= in->sustain_point) {
            if (in->sustain_delay == 0) return;
            if (v->sustain_delay_counter != 0) { v->sustain_delay_counter--; return; }
            v->sustain_delay_counter = in->sustain_delay;
        }
        v->adsr_delay_counter--;
        if (v->adsr_delay_counter == 0) {
            v->adsr_delay_counter = in->adsr_delay;
            v->adsr_position++;
            if (v->adsr_position >= in->adsr_length + in->adsr_repeat) {
                v->adsr_position = in->adsr_length;
                if (in->adsr_repeat == 0) v->adsr_position--;
                if (in->adsr_repeat == 0 && vol == 0) v->flag |= 0x01;
            }
        }
    }
}

static void do_volume_slide(SaVoice *v) {
    int16_t vol = (int16_t)(v->current_volume + v->volume_slide_speed);
    if (vol < 0) vol = 0; else if (vol > 64) vol = 64;
    v->current_volume = (uint16_t)vol;
}

/* ---------------------------------------------------------------- synth effects */
static void inc_se_pos(SaVoice *v, const SaInst *in) {
    v->synth_effect_position++;
    if (v->synth_effect_position >= in->effect_arg3) v->synth_effect_position = in->effect_arg2;
}

static void shack_wave_helper(SaPlayer *p, SaVoice *v, const SaInst *in) {
    const int8_t *src = wave_of(p->s, in->effect_arg1);
    int8_t delta = wget(src, (long)in->effect_arg2 + v->synth_effect_position);
    long bo = in->effect_arg2;
    int16_t count = (int16_t)(in->effect_arg3 - in->effect_arg2);
    while (count >= 0) { wset(v->wbuf, bo, (int8_t)(wget(v->wbuf, bo) + delta)); bo++; count--; }
}

static void metamorph_helper(SaVoice *v, const SaInst *in, const int8_t *src) {
    long bo = in->effect_arg2;
    int16_t count = (int16_t)(in->effect_arg3 - in->effect_arg2);
    int set_flag = 0;
    while (count >= 0) {
        int8_t val = wget(v->wbuf, bo), sv = wget(src, bo);
        if (val != sv) {
            set_flag = 1;
            if (val < sv) val++; else val--;
            wset(v->wbuf, bo, val);
        }
        bo++; count--;
    }
    if (!set_flag) v->flag |= 0x02;
}

static void lpf_step(SaVoice *v, const SaInst *in, int i, int delta) {   /* body shared by both low pass filters */
    int flag = 0;
    int8_t val1 = wget(v->wbuf, i);
    int8_t val2 = i == in->effect_arg3 ? wget(v->wbuf, in->effect_arg2) : wget(v->wbuf, i + 1);
    if (val1 <= val2) flag = 1;
    val1 = (int8_t)(val1 - val2);
    if (val1 < 0) val1 = (int8_t)-val1;
    if (val1 > delta) wset(v->wbuf, i, (int8_t)(wget(v->wbuf, i) + (flag ? 2 : -2)));
}

static void do_synth_effects(SaPlayer *p, SaVoice *v, const SaInst *in) {
    const SaSong *s = p->s;
    v->effect_delay_counter--;
    if (v->effect_delay_counter != 0) return;
    v->effect_delay_counter = in->effect_delay;
    int8_t *dw = v->wbuf;
    uint16_t st = in->effect_arg2, sp = in->effect_arg3;
    switch (in->effect) {
        case SE_WAVE_NEGATOR:
            wset(dw, v->synth_effect_position, (int8_t)-wget(dw, v->synth_effect_position));
            inc_se_pos(v, in);
            break;
        case SE_FREE_NEGATOR:
            if ((v->flag & 0x04) == 0) {
                uint16_t wave_length = in->effect_arg2, wave_repeat = in->effect_arg3;
                const int8_t *wf = wave_of(s, in->effect_arg1);
                int16_t wave_val = (int16_t)(wget(wf, v->synth_effect_wave_position) & 0x7f);
                const int8_t *src = wave_of(s, in->waveform_number);
                int16_t count = (int16_t)(in->waveform_length * 2);
                long bo = count;
                while (count > 0 && count >= wave_val) { bo--; wset(dw, bo, wget(src, bo)); count--; }
                int16_t left = (int16_t)(wave_val - count);
                count = (int16_t)(count + left);
                bo += left;
                while (count > 0) { bo--; wset(dw, bo, (int8_t)-wget(src, bo)); count--; }
                v->synth_effect_wave_position++;
                if (v->synth_effect_wave_position > wave_length + wave_repeat) {
                    v->synth_effect_wave_position = wave_length;
                    if (wave_repeat == 0 && wave_val == 0) v->flag |= 0x04;
                }
            }
            break;
        case SE_ROTATE_VERTICAL: {
            int8_t delta = (int8_t)in->effect_arg1;
            int16_t count = (int16_t)(sp - st);
            long bo = st;
            while (count >= 0) { wset(dw, bo, (int8_t)(wget(dw, bo) + delta)); bo++; count--; }
            inc_se_pos(v, in);
            break;
        }
        case SE_ROTATE_HORIZONTAL: {
            int16_t count = (int16_t)(sp - st);
            long bo = st;
            int8_t first = wget(dw, bo);
            do { wset(dw, bo, wget(dw, bo + 1)); bo++; count--; } while (count >= 0);
            wset(dw, bo, first);
            inc_se_pos(v, in);
            break;
        }
        case SE_ALIEN_VOICE: {
            const int8_t *src = wave_of(s, in->effect_arg1);
            long bo = st;
            int16_t count = (int16_t)(sp - st);
            while (count >= 0) { wset(dw, bo, (int8_t)(wget(dw, bo) + wget(src, bo))); bo++; count--; }
            break;
        }
        case SE_POLY_NEGATOR: {
            const int8_t *src = wave_of(s, in->waveform_number);
            uint16_t pos = v->synth_effect_position;
            wset(dw, pos, wget(src, pos));
            if (pos >= sp) pos = (uint16_t)(st - 1);
            wset(dw, (long)pos + 1, (int8_t)-wget(dw, (long)pos + 1));
            inc_se_pos(v, in);
            break;
        }
        case SE_SHACK_WAVE1:
            shack_wave_helper(p, v, in);
            inc_se_pos(v, in);
            break;
        case SE_SHACK_WAVE2: {
            shack_wave_helper(p, v, in);
            long i = (long)st + v->synth_effect_wave_position;
            wset(dw, i, (int8_t)-wget(dw, i));
            v->synth_effect_wave_position++;
            if (v->synth_effect_wave_position > sp - st) v->synth_effect_wave_position = 0;
            inc_se_pos(v, in);
            break;
        }
        case SE_METAMORPH:
            if ((v->flag & 0x01) == 0) metamorph_helper(v, in, wave_of(s, in->effect_arg1));
            break;
        case SE_LASER: {
            int8_t detune = (int8_t)in->effect_arg2;
            uint16_t repeats = in->effect_arg3;
            if (v->synth_effect_wave_position < repeats) {
                v->slide_value = (int16_t)(v->slide_value + detune);
                v->synth_effect_wave_position++;
            }
            inc_se_pos(v, in);
            break;
        }
        case SE_WAVE_ALIAS: {
            int8_t delta = (int8_t)in->effect_arg1;
            int16_t count = (int16_t)(sp - st);
            long bo = st;
            while (count >= 0) {
                int8_t val = wget(dw, bo);
                if (bo + 1 >= 128 || val > wget(dw, bo + 1)) val = (int8_t)(val - delta);
                else val = (int8_t)(val + delta);
                wset(dw, bo++, val);
                count--;
            }
            inc_se_pos(v, in);
            break;
        }
        case SE_NOISE_GENERATOR1:
            wset(dw, v->synth_effect_position, (int8_t)(wget(dw, v->synth_effect_position) ^ (int8_t)sa_rand(p)));
            inc_se_pos(v, in);
            break;
        case SE_LOW_PASS_FILTER1:
            for (int i = st; i <= sp; i++) lpf_step(v, in, i, in->effect_arg1);
            inc_se_pos(v, in);
            break;
        case SE_LOW_PASS_FILTER2: {
            const int8_t *wf = wave_of(s, in->effect_arg1);
            int bi = sp;
            for (int i = st; i <= sp; i++) {
                uint8_t delta = (uint8_t)(wget(wf, bi++) & 0x7f);
                bi &= 0x7f;
                lpf_step(v, in, i, delta);
            }
            inc_se_pos(v, in);
            break;
        }
        case SE_OSZILATOR: {
            if ((v->flag & 0x02) != 0) { v->flag ^= 0x08; v->flag &= 0xfd; }
            const int8_t *src = (v->flag & 0x08) != 0 ? wave_of(s, in->waveform_number) : wave_of(s, in->effect_arg1);
            metamorph_helper(v, in, src);
            break;
        }
        case SE_NOISE_GENERATOR2: {
            int16_t count = (int16_t)(sp - st);
            long bo = st;
            do {
                int val = wget(dw, bo + count);
                val = (int8_t)(val ^ 0x05);
                int hi = val * 4, lo = val >= 0 ? val >> 6 : ~((~val) >> 6);    /* sign-extended int shifts as in C# */
                val = (int8_t)(hi | lo);
                val = (int8_t)(val + (int8_t)sa_rand(p));
                wset(dw, bo + count, (int8_t)val);
                count--;
            } while (count >= 0);
            break;
        }
        case SE_FM_DRUM: {
            uint8_t level = (uint8_t)in->effect_arg1;
            uint16_t factor = in->effect_arg2, repeats = in->effect_arg3;
            if (v->synth_effect_wave_position >= repeats) { v->slide_value = in->fine_tuning; v->synth_effect_wave_position = 0; }
            uint16_t dec = (uint16_t)((factor << 8) | level);
            v->slide_value = (int16_t)(v->slide_value - dec);
            v->synth_effect_wave_position++;
            inc_se_pos(v, in);
            break;
        }
    }
}

static void update_voice_effect(SaPlayer *p, SaVoice *v, SaMix *c) {
    const SaInst *in = vinst(p, v);
    if ((v->flag & 0x01) != 0 || !in) { ch_mute(c); return; }
    PeriodInfo pi = do_arpeggio(p, v, in);
    do_portamento(&pi, v);
    do_vibrato(&pi, v, in);
    do_amf(p, &pi, v, in);
    do_slide(p, &pi, v);
    c->period = pi.period;                                              /* SetAmigaPeriod */
    if (in->type == 1) do_synth_effects(p, v, in);
    do_adsr(p, v, c, in);
    do_volume_slide(v);
}

/* ---------------------------------------------------------------- Play() */
static void play_tick(SaPlayer *p) {
    p->speed_counter++;
    if (p->speed_counter >= p->current_speed) {
        p->speed_counter = 0;
        get_next_row(p);
        if (p->ended) return;
    }
    for (int i = 0; i < 4; i++) update_voice_effect(p, &p->v[i], &p->m[i]);
    if (p->end_reached) { p->ended = 1; p->end_reached = 0; }      /* OnEndReached: the song is over (no restart) */
}

/* ---------------------------------------------------------------- public */
SaPlayer *sa_new(const SaSong *s, int sub, int rate) {
    if (!s || rate <= 0 || sub < 0 || sub >= s->nsub) return NULL;
    SaPlayer *p = (SaPlayer *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->visited = (uint8_t *)calloc((size_t)(s->npos > 0 ? s->npos : 1), 1);
    if (!p->visited) { free(p); return NULL; }
    p->s = s; p->rate = rate; p->sub = sub; p->rnd = 0x2545f491u;
    { double w0 = 6.283185307179586 * 3275.0 / rate, al = sin(w0) / (2.0 * 0.7071067811865476), co = cos(w0), a0 = 1.0 + al;
      p->lb0 = (1.0 - co) / 2.0 / a0; p->lb1 = (1.0 - co) / a0; p->lb2 = p->lb0; p->la1 = -2.0 * co / a0; p->la2 = (1.0 - al) / a0; }
    initialize_sound(p, sub);
    return p;
}

void sa_player_free(SaPlayer *p) { if (p) { free(p->visited); free(p); } }

static inline void mix_advance(SaMix *m) {                           /* end of a pass: the loop, else stop */
    while (m->on && m->pos >= m->len) {
        if (m->llen > 0 && m->ld) { m->pos -= m->len; m->d = m->ld; m->len = m->llen; }
        else { m->on = 0; break; }
        if (m->pos < 0) m->pos = 0;
    }
}

int sa_render(SaPlayer *p, int16_t *out, int n) {
    if (!p || p->ended) return 0;
    for (int f = 0; f < n; f++) {
        if (p->tick_left <= 0) {
            play_tick(p);
            if (p->ended) { memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(n - f)); return f; }
            /* PlayingFrequency = Tempo (Hz); 0 would divide by zero in the mixer, 50 Hz then */
            double hz = p->playing_frequency > 0 ? (double)p->playing_frequency : 50.0;
            p->tick_left += p->rate / hz;
        }
        p->tick_left -= 1.0;
        float l = 0, rr = 0;
        for (int i = 0; i < 4; i++) {
            SaMix *m = &p->m[i];
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
        {   /* LED filter (effect E: 0 = on) */
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

int sa_subsongs(const SaSong *s) { return s ? s->nsub : 0; }
int sa_channels(const SaSong *s) { (void)s; return 4; }
const char *sa_title(const SaSong *s) { (void)s; return ""; }          /* the format has no song name */
int sa_rows(const SaSong *s) { return s && s->nsub > 0 ? s->sub[0].rows_per_track : 0; }

int sa_positions(const SaSong *s, int sub) {
    if (!s || sub < 0 || sub >= s->nsub) return 0;
    const SaSongInfo *si = &s->sub[sub];
    int last = si->last_position < s->npos ? si->last_position : s->npos - 1;
    int n = last - si->first_position + 1;
    return n > 0 ? n : 0;
}

void sa_pos(const SaPlayer *p, int *position, int *row) {
    if (position) *position = p ? (p->shown_pos > 0 ? p->shown_pos : 0) : 0;
    if (row) *row = p ? p->shown_row : 0;
}

int sa_cell(const SaSong *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg) {
    if (!s || sub < 0 || sub >= s->nsub || position < 0 || position >= sa_positions(s, sub) || row < 0 || row >= 256 || ch < 0 || ch >= 4) return 0;
    int ap = s->sub[sub].first_position + position;
    const SaPosInfo *pi = &s->pos[(size_t)ap * 4 + (size_t)ch];
    int line = pi->start_track_row + row;
    if (line >= s->nlines) return 0;
    const SaLine *l = &s->lines[line];
    int nt = l->note, in = l->instrument;
    if (nt != 0 && nt != 0x80 && nt != 0x7f) {                       /* as PlayVoice */
        if (!l->dis_note_tr) nt = (uint16_t)(nt + pi->note_transpose);
        if (in != 0 && !l->dis_sound_tr) in = (int16_t)(in + pi->sound_transpose);
    }
    if (note) *note = nt;
    if (inst) *inst = in;
    if (fx) *fx = l->effect;
    if (arg) *arg = l->arg;
    return 1;
}

void sa_set_mute(SaPlayer *p, unsigned mask) { if (p) p->mute = mask; }
void sa_set_scope(SaPlayer *p, float *buf, int len) {
    if (!p) return;
    if (!buf || len <= 0 || (len & (len - 1))) { p->sc = NULL; p->sc_len = 0; return; }
    p->sc = buf; p->sc_len = len;
}
unsigned sa_scope_pos(const SaPlayer *p) { return p ? p->sc_pos : 0; }
