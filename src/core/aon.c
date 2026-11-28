/* Art Of Noise (Amiga, .aon / .aon8): loader + replayer, a port of the NostalgicPlayer ArtOfNoiseWorker (MIT),
   with a Paula-like mixer in the style of ml_replay.c. C# integer semantics are kept (byte/ushort wrap, truncating casts). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "aon.h"

#define PAULA_PAL 3546895.0

/* ---------------------------------------------------------------- tables (Tables.cs) */
static const uint16_t aon_periods[16][60] = {
    { 3434, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1812,
      1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  906,
      856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453,
      428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,
      214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113 },
    { 3400, 3208, 3028, 2860, 2696, 2548, 2404, 2268, 2140, 2020, 1908, 1800,
      1700, 1604, 1514, 1430, 1348, 1274, 1202, 1134, 1070, 1010,  954,  900,
      850,  802,  757,  715,  674,  637,  601,  567,  535,  505,  477,  450,
      425,  401,  379,  357,  337,  318,  300,  284,  268,  253,  239,  225,
      213,  201,  189,  179,  169,  159,  150,  142,  134,  126,  119,  113 },
    { 3376, 3184, 3008, 2836, 2680, 2528, 2388, 2252, 2128, 2008, 1896, 1788,
      1688, 1592, 1504, 1418, 1340, 1264, 1194, 1126, 1064, 1004,  948,  894,
      844,  796,  752,  709,  670,  632,  597,  563,  532,  502,  474,  447,
      422,  398,  376,  355,  335,  316,  298,  282,  266,  251,  237,  224,
      211,  199,  188,  177,  167,  158,  149,  141,  133,  125,  118,  112 },
    { 3352, 3164, 2984, 2816, 2660, 2512, 2368, 2236, 2112, 1992, 1880, 1776,
      1676, 1582, 1492, 1408, 1330, 1256, 1184, 1118, 1056,  996,  940,  888,
      838,  791,  746,  704,  665,  628,  592,  559,  528,  498,  470,  444,
      419,  395,  373,  352,  332,  314,  296,  280,  264,  249,  235,  222,
      209,  198,  187,  176,  166,  157,  148,  140,  132,  125,  118,  111 },
    { 3328, 3140, 2964, 2796, 2640, 2492, 2352, 2220, 2096, 1980, 1868, 1764,
      1664, 1570, 1482, 1398, 1320, 1246, 1176, 1110, 1048,  990,  934,  882,
      832,  785,  741,  699,  660,  623,  588,  555,  524,  495,  467,  441,
      416,  392,  370,  350,  330,  312,  294,  278,  262,  247,  233,  220,
      208,  196,  185,  175,  165,  156,  147,  139,  131,  124,  117,  110 },
    { 3304, 3116, 2944, 2776, 2620, 2476, 2336, 2204, 2080, 1964, 1852, 1748,
      1652, 1558, 1472, 1388, 1310, 1238, 1168, 1102, 1040,  982,  926,  874,
      826,  779,  736,  694,  655,  619,  584,  551,  520,  491,  463,  437,
      413,  390,  368,  347,  328,  309,  292,  276,  260,  245,  232,  219,
      206,  195,  184,  174,  164,  155,  146,  138,  130,  123,  116,  109 },
    { 3280, 3096, 2920, 2756, 2604, 2456, 2320, 2188, 2064, 1948, 1840, 1736,
      1640, 1548, 1460, 1378, 1302, 1228, 1160, 1094, 1032,  974,  920,  868,
      820,  774,  730,  689,  651,  614,  580,  547,  516,  487,  460,  434,
      410,  387,  365,  345,  325,  307,  290,  274,  258,  244,  230,  217,
      205,  193,  183,  172,  163,  154,  145,  137,  129,  122,  115,  109 },
    { 3256, 3072, 2900, 2736, 2584, 2440, 2300, 2172, 2052, 1936, 1828, 1724,
      1628, 1536, 1450, 1368, 1292, 1220, 1150, 1086, 1026,  968,  914,  862,
      814,  768,  725,  684,  646,  610,  575,  543,  513,  484,  457,  431,
      407,  384,  363,  342,  323,  305,  288,  272,  256,  242,  228,  216,
      204,  192,  181,  171,  161,  152,  144,  136,  128,  121,  114,  108 },
    { 3628, 3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920,
      1814, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,
      907,  856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,
      453,  428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,
      226,  214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120 },
    { 3600, 3400, 3208, 3028, 2860, 2700, 2544, 2404, 2268, 2140, 2020, 1908,
      1800, 1700, 1604, 1514, 1430, 1350, 1272, 1202, 1134, 1070, 1010,  954,
      900,  850,  802,  757,  715,  675,  636,  601,  567,  535,  505,  477,
      450,  425,  401,  379,  357,  337,  318,  300,  284,  268,  253,  238,
      225,  212,  200,  189,  179,  169,  159,  150,  142,  134,  126,  119 },
    { 3576, 3376, 3184, 3008, 2836, 2680, 2528, 2388, 2252, 2128, 2008, 1896,
      1788, 1688, 1592, 1504, 1418, 1340, 1264, 1194, 1126, 1064, 1004,  948,
      894,  844,  796,  752,  709,  670,  632,  597,  563,  532,  502,  474,
      447,  422,  398,  376,  355,  335,  316,  298,  282,  266,  251,  237,
      223,  211,  199,  188,  177,  167,  158,  149,  141,  133,  125,  118 },
    { 3548, 3352, 3164, 2984, 2816, 2660, 2512, 2368, 2236, 2112, 1992, 1880,
      1774, 1676, 1582, 1492, 1408, 1330, 1256, 1184, 1118, 1056,  996,  940,
      887,  838,  791,  746,  704,  665,  628,  592,  559,  528,  498,  470,
      444,  419,  395,  373,  352,  332,  314,  296,  280,  264,  249,  235,
      222,  209,  198,  187,  176,  166,  157,  148,  140,  132,  125,  118 },
    { 3524, 3328, 3140, 2964, 2796, 2640, 2492, 2352, 2220, 2096, 1976, 1868,
      1762, 1664, 1570, 1482, 1398, 1320, 1246, 1176, 1110, 1048,  988,  934,
      881,  832,  785,  741,  699,  660,  623,  588,  555,  524,  494,  467,
      441,  416,  392,  370,  350,  330,  312,  294,  278,  262,  247,  233,
      220,  208,  196,  185,  175,  165,  156,  147,  139,  131,  123,  117 },
    { 3500, 3304, 3116, 2944, 2776, 2620, 2476, 2336, 2204, 2080, 1964, 1852,
      1750, 1652, 1558, 1472, 1388, 1310, 1238, 1168, 1102, 1040,  982,  926,
      875,  826,  779,  736,  694,  655,  619,  584,  551,  520,  491,  463,
      437,  413,  390,  368,  347,  328,  309,  292,  276,  260,  245,  232,
      219,  206,  195,  184,  174,  164,  155,  146,  138,  130,  123,  116 },
    { 3472, 3280, 3096, 2920, 2756, 2604, 2456, 2320, 2188, 2064, 1948, 1840,
      1736, 1640, 1548, 1460, 1378, 1302, 1228, 1160, 1094, 1032,  974,  920,
      868,  820,  774,  730,  689,  651,  614,  580,  547,  516,  487,  460,
      434,  410,  387,  365,  345,  325,  307,  290,  274,  258,  244,  230,
      217,  205,  193,  183,  172,  163,  154,  145,  137,  129,  122,  115 },
    { 3448, 3256, 3072, 2900, 2736, 2584, 2440, 2300, 2172, 2052, 1936, 1828,
      1724, 1628, 1536, 1450, 1368, 1292, 1220, 1150, 1086, 1026,  968,  914,
      862,  814,  768,  725,  684,  646,  610,  575,  543,  513,  484,  457,
      431,  407,  384,  363,  342,  323,  305,  288,  272,  256,  242,  228,
      216,  203,  192,  181,  171,  161,  152,  144,  136,  128,  121,  114 }
};
static const uint8_t vib_sine[32] = { 0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24 };
static const uint8_t vib_ramp_down[32] = { 255, 248, 240, 232, 224, 216, 208, 200, 192, 184, 176, 168, 160, 152, 144,
    136, 128, 120, 112, 104, 96, 88, 80, 72, 64, 56, 48, 40, 32, 24, 16, 8 };
static const uint8_t vib_square[32] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 };
static const int8_t nibble_tab[16] = { 0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1 };
static const uint8_t pan4[4] = { 0, 1, 1, 0 };                          /* 0 = left, 1 = right */
static const uint8_t pan8[8] = { 0, 0, 1, 1, 1, 1, 0, 0 };

/* ---------------------------------------------------------------- effects (Effect.cs, ExtraEffect.cs) */
enum {
    FX_ARPEGGIO = 0, FX_SLIDE_UP, FX_SLIDE_DOWN, FX_TONE_PORTA, FX_VIBRATO, FX_TONE_PORTA_VOLSLIDE, FX_VIBRATO_VOLSLIDE,
    FX_SAMPLE_OFFSET = 9, FX_VOLUME_SLIDE, FX_POSITION_JUMP, FX_SET_VOLUME, FX_PATTERN_BREAK, FX_EXTRA, FX_SET_SPEED,
    FX_NEW_VOLUME, FX_SYNTH_CONTROL, FX_WAVE_SPEED, FX_SET_ARP_SPEED, FX_SET_VOLUME_VIBRATO, FX_FINE_SLIDE_PORTA_UP,
    FX_FINE_SLIDE_PORTA_DOWN, FX_AVOID_NOISE, FX_OVERSIZE, FX_FINE_VOLSLIDE_VIBRATO, FX_VOLUME_PORT_DOWN_SLIDE,
    FX_SET_VOLUME_TONE_PORTA, FX_FINE_VOLSLIDE_TONE_PORTA, FX_SET_TRACK_VOLUME, FX_SET_WAVE_MODE, FX_EXTERNAL_EVENT = 33
};
enum { EX_SET_FILTER = 0x00, EX_FINE_SLIDE_UP = 0x10, EX_FINE_SLIDE_DOWN = 0x20, EX_SET_VIB_WAVE = 0x40, EX_SET_LOOP = 0x50,
       EX_PATTERN_LOOP = 0x60, EX_RETRIG = 0x90, EX_FINE_VOL_UP = 0xa0, EX_FINE_VOL_DOWN = 0xb0, EX_NOTE_CUT = 0xc0,
       EX_NOTE_DELAY = 0xd0, EX_PATTERN_DELAY = 0xe0 };
enum { ENV_DONE, ENV_ADD, ENV_SUB };

#define IS_TONE_PORTA(fx) ((fx) == FX_TONE_PORTA || (fx) == FX_TONE_PORTA_VOLSLIDE || (fx) == FX_SET_VOLUME_TONE_PORTA || (fx) == FX_FINE_VOLSLIDE_TONE_PORTA)

/* ---------------------------------------------------------------- song */
typedef struct { uint8_t instrument, note, arpeggio, effect, arg; } AonCell;

typedef struct {
    uint8_t type;                                        /* 0 = sample, 1 = synth */
    uint8_t volume, fine_tune, wave_form;
    uint8_t env_start, env_add, env_end, env_sub;
    uint32_t start_offset, length, loop_start, loop_length;   /* sample */
    uint8_t s_length, vib_param, vib_delay, vib_wave, wave_speed, wave_length, wave_loop_start, wave_loop_length, wave_loop_control;  /* synth */
} AonInst;

struct AonSong {
    int channels;
    char title[256], author[256];
    uint8_t number_of_positions, restart_position;
    uint8_t position_list[256];                           /* PLST, padded with 0 */
    int plst_len, has_plst;
    int npatterns; AonCell *patterns;                     /* [pattern][row][channel] */
    int ninstruments; AonInst *instruments;
    int has_arpg; uint8_t arpeggios[16][4];
    int nwaves; int8_t **waves; uint32_t *wave_len;       /* NULL = no wave form (length 0) */
    int has_wlen;
};

/* ---------------------------------------------------------------- player */
typedef struct {
    uint8_t ch_flag, last_note;
    int wave; uint32_t wave_off;                          /* WaveForm (index, -1 = null) + offset */
    uint16_t wave_len, old_wave_len;
    int rep; uint32_t rep_off; uint16_t rep_len;          /* RepeatStart */
    int instr; int16_t instr_num;                         /* Instrument (index, -1 = null) */
    uint8_t volume, step_fx_cnt, ch_mode;
    uint16_t period; int16_t per_slide;
    uint16_t arp_off; uint8_t arp_ft; int16_t arp_tab[8]; uint8_t arp_spd, arp_cnt;
    int sw_act; uint32_t sw_act_off, sw_end_off;
    int sw_rep; uint32_t sw_rep_off, sw_rep_end_off;
    int32_t sw_add_bytes;
    uint8_t sw_cnt, sw_spd, sw_rep_ctrl, sw_cont, sw_stop;
    uint8_t s_add, s_sub, s_end; int s_env; uint8_t s_vol;
    uint8_t vib_on; int vib_done; uint8_t vib_cont, vib_spd, vib_ampl, vib_pos; int16_t vib_trig_delay;
    uint8_t fx_com, fx_dat;
    int slide_flag;
    uint32_t old_sample_offset;
    uint8_t gliss_spd, track_volume;
} AonVoice;

typedef struct {
    const int8_t *d; uint32_t len; double pos; int on;
    const int8_t *ld; uint32_t llen;                      /* SetLoop */
    const int8_t *pd; uint32_t plen; int pend;            /* SetSample: plays when the current pass ends */
    uint32_t period; int volume;
} AonMix;

struct AonPlayer {
    const AonSong *s;
    int rate, nch;
    /* GlobalPlayingInfo */
    uint8_t tempo, speed, frame_cnt;
    int pattern_break; uint8_t pat_cnt; int8_t pat_delay_cnt;
    int loop_flag; uint8_t loop_point, loop_cnt;
    uint8_t position, new_position, current_pattern;
    int noise_avoid, oversize;
    uint8_t event[3];
    AonVoice v[8];
    int end_reached, restart_song;
    uint8_t visited[256];
    int bpm, amiga_filter;
    int shown_pos, shown_pat, shown_row;
    /* mixer */
    AonMix m[8];
    double tick_left;
    double lb0, lb1, lb2, la1, la2, lz[2][4];
    int hlp; float g8; double hb0, hb1, hb2, ha1, ha2, hz[2][4];       /* 8 voices: low-pass of the software mix + level */
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
static uint32_t rd_b32(Rd *r) { uint8_t x[4]; rd_read(r, x, 4); return (uint32_t)x[0] << 24 | (uint32_t)x[1] << 16 | (uint32_t)x[2] << 8 | x[3]; }
static int rd_seek(Rd *r, long long off) { if (r->pos + off < 0) return -1; r->pos += off; return 0; }

static void rd_string(Rd *r, char *dst, size_t dsz, int len) {   /* ReadString: len bytes, up to the first 0; Amiga (Latin-1) -> UTF-8 */
    size_t o = 0; int stop = 0;
    for (int i = 0; i < len; i++) {
        uint8_t c = rd_u8(r);
        if (c == 0) stop = 1;
        if (stop) continue;
        if (c < 0x80) { if (o + 1 < dsz) dst[o++] = (char)c; }
        else if (o + 2 < dsz) { dst[o++] = (char)(0xC0 | c >> 6); dst[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    dst[o] = 0;
}

int aon_probe(const uint8_t *b, size_t n) {
    if (!b || n < 54) return 0;
    return memcmp(b, "AON4", 4) == 0 || memcmp(b, "AON8", 4) == 0;
}

void aon_free(AonSong *s) {
    if (!s) return;
    if (s->waves) for (int i = 0; i < s->nwaves; i++) free(s->waves[i]);
    free(s->waves); free(s->wave_len); free(s->patterns); free(s->instruments); free(s);
}

AonSong *aon_load(const uint8_t *b, size_t n, int *err) {
    if (err) *err = 0;
    if (!aon_probe(b, n)) { if (err) *err = 1; return NULL; }
    AonSong *s = (AonSong *)calloc(1, sizeof *s);
    if (!s) { if (err) *err = 2; return NULL; }
    s->channels = memcmp(b, "AON8", 4) == 0 ? 8 : 4;
    Rd r = { b, n, 46, 0 };
    int bad = 0;
    for (;;) {
        uint8_t mark[4]; rd_read(&r, mark, 4);
        int32_t chunk = (int32_t)rd_b32(&r);
        if (r.eof) break;
        if ((long long)chunk > (long long)r.n - r.pos) { bad = 1; break; }
        if (chunk < 0) { bad = 1; break; }                              /* C# would throw on a negative size */
        if (!memcmp(mark, "NAME", 4)) { rd_string(&r, s->title, sizeof s->title, chunk); if (r.eof) bad = 1; }
        else if (!memcmp(mark, "AUTH", 4)) { rd_string(&r, s->author, sizeof s->author, chunk); if (r.eof) bad = 1; }
        else if (!memcmp(mark, "RMRK", 4)) { if (chunk > 0) { rd_read(&r, NULL, (size_t)chunk); if (r.eof) bad = 1; } }
        else if (!memcmp(mark, "INFO", 4)) {
            rd_seek(&r, 1);
            s->number_of_positions = rd_u8(&r);
            s->restart_position = rd_u8(&r);
            if (s->restart_position >= s->number_of_positions) s->restart_position = 0;
            if (r.eof) bad = 1;
            else if (rd_seek(&r, (long long)chunk - 3)) bad = 1;
        }
        else if (!memcmp(mark, "ARPG", 4)) {                            /* 16 arpeggios x 4 bytes, the chunk size is not used */
            for (int i = 0; i < 16 && !bad; i++) if (rd_read(&r, s->arpeggios[i], 4) < 4) bad = 1;
            s->has_arpg = 1;
        }
        else if (!memcmp(mark, "PLST", 4)) {
            memset(s->position_list, 0, sizeof s->position_list);
            uint8_t *tmp = (uint8_t *)malloc((size_t)chunk + 1);
            if (!tmp) { bad = 1; break; }
            size_t got = rd_read(&r, tmp, (size_t)chunk);
            memcpy(s->position_list, tmp, chunk < 256 ? (size_t)chunk : 256);
            free(tmp);
            s->plst_len = chunk; s->has_plst = 1;
            if (got < (size_t)chunk) bad = 1;
        }
        else if (!memcmp(mark, "PATT", 4)) {
            int np = chunk / (4 * s->channels * 64);
            free(s->patterns);
            s->patterns = (AonCell *)calloc((size_t)(np ? np : 1) * 64 * (size_t)s->channels, sizeof(AonCell));
            s->npatterns = np;
            if (!s->patterns) { bad = 1; break; }
            for (int i = 0; i < np && !bad; i++) {
                for (int row = 0; row < 64; row++)
                    for (int c = 0; c < s->channels; c++) {
                        uint8_t b1 = rd_u8(&r), b2 = rd_u8(&r), b3 = rd_u8(&r), b4 = rd_u8(&r);
                        AonCell *cl = &s->patterns[((size_t)i * 64 + (size_t)row) * (size_t)s->channels + (size_t)c];
                        cl->instrument = (uint8_t)(b2 & 0x3f);
                        cl->note = (uint8_t)(b1 & 0x3f);
                        cl->arpeggio = (uint8_t)(((b3 & 0xc0) >> 4) | ((b2 & 0xc0) >> 6));
                        cl->effect = (uint8_t)(b3 & 0x3f);
                        cl->arg = b4;
                    }
                if (r.eof) bad = 1;
            }
        }
        else if (!memcmp(mark, "INST", 4)) {
            int ni = chunk / 32;
            free(s->instruments);
            s->instruments = (AonInst *)calloc((size_t)(ni ? ni : 1), sizeof(AonInst));
            s->ninstruments = ni;
            if (!s->instruments) { bad = 1; break; }
            for (int i = 0; i < ni && !bad; i++) {
                AonInst *in = &s->instruments[i];
                uint8_t type = rd_u8(&r), volume = rd_u8(&r), fine = rd_u8(&r), wf = rd_u8(&r);
                if (type == 0) {
                    in->start_offset = rd_b32(&r); in->length = rd_b32(&r); in->loop_start = rd_b32(&r); in->loop_length = rd_b32(&r);
                    rd_seek(&r, 8);
                } else if (type == 1) {
                    in->s_length = rd_u8(&r);
                    rd_seek(&r, 5);
                    in->vib_param = rd_u8(&r); in->vib_delay = rd_u8(&r); in->vib_wave = rd_u8(&r); in->wave_speed = rd_u8(&r);
                    in->wave_length = rd_u8(&r); in->wave_loop_start = rd_u8(&r); in->wave_loop_length = rd_u8(&r); in->wave_loop_control = rd_u8(&r);
                    rd_seek(&r, 10);
                } else { bad = 1; break; }
                in->env_start = rd_u8(&r); in->env_add = rd_u8(&r); in->env_end = rd_u8(&r); in->env_sub = rd_u8(&r);
                if (r.eof) { bad = 1; break; }
                in->type = type; in->volume = volume; in->fine_tune = fine; in->wave_form = wf;
            }
        }
        else if (!memcmp(mark, "INAM", 4)) {                            /* names are not kept; the stream moves like in C# */
            if (!s->instruments) { bad = 1; break; }                    /* C#: foreach over null -> exception */
            long long left = chunk;
            for (int i = 0; i < s->ninstruments && !bad; i++) { rd_read(&r, NULL, 32); if (r.eof) bad = 1; left -= 32; }
            if (!bad && rd_seek(&r, left)) bad = 1;
        }
        else if (!memcmp(mark, "WLEN", 4)) {
            int nw = chunk / 4;
            if (s->waves) for (int i = 0; i < s->nwaves; i++) free(s->waves[i]);
            free(s->waves); free(s->wave_len);
            s->waves = (int8_t **)calloc((size_t)(nw ? nw : 1), sizeof(int8_t *));
            s->wave_len = (uint32_t *)calloc((size_t)(nw ? nw : 1), sizeof(uint32_t));
            s->nwaves = nw; s->has_wlen = 1;
            if (!s->waves || !s->wave_len) { bad = 1; break; }
            for (int i = 0; i < nw; i++) {
                uint32_t len = rd_b32(&r);
                if (len != 0) {
                    if (len > 0x10000000u) { bad = 1; break; }          /* C# new sbyte[length] would fail */
                    s->waves[i] = (int8_t *)calloc(len, 1);
                    if (!s->waves[i]) { bad = 1; break; }
                    s->wave_len[i] = len;
                }
            }
            if (r.eof) bad = 1;
        }
        else if (!memcmp(mark, "WAVE", 4)) {
            if (!s->has_wlen) { bad = 1; break; }                       /* C#: waveForms null -> exception */
            for (int i = 0; i < s->nwaves && !bad; i++)
                if (s->waves[i]) { rd_read(&r, (uint8_t *)s->waves[i], s->wave_len[i]); if (r.eof) bad = 1; }
        }
        else rd_seek(&r, chunk);
        if (bad) break;
    }
    /* a PLST shorter than the INFO position count is padded with 0 (C# would index out of range) */
    if (!bad && (s->number_of_positions == 0 || !s->has_arpg || !s->has_plst || !s->patterns || !s->instruments || !s->has_wlen)) bad = 1;
    if (bad) { aon_free(s); if (err) *err = 3; return NULL; }
    return s;
}

/* ---------------------------------------------------------------- helpers */
static const AonCell zero_cell;
static const AonCell *cell_at(const AonSong *s, int pat, int row, int ch) {
    if (pat < 0 || pat >= s->npatterns || row < 0 || row >= 64 || ch < 0 || ch >= s->channels) return &zero_cell;   /* C# would index out of range */
    return &s->patterns[((size_t)pat * 64 + (size_t)row) * (size_t)s->channels + (size_t)ch];
}
static int wave_idx(const AonSong *s, int w) { return (w >= 0 && w < s->nwaves && s->waves[w]) ? w : -1; }
static uint16_t period_of(int fine, int note) { return aon_periods[fine & 0x0f][note]; }   /* C#: Periods[fineTune, note]; fine tune > 15 would throw */

/* ---------------------------------------------------------------- mixer channel (IChannel) */
static void ch_play_sample(AonMix *m, const int8_t *data, uint32_t off, uint32_t len) {
    m->d = data + off; m->len = len; m->pos = 0; m->on = 1;
    m->ld = NULL; m->llen = 0; m->pd = NULL; m->plen = 0; m->pend = 0;
}
static void ch_set_loop(AonMix *m, const int8_t *data, uint32_t off, uint32_t len) { m->ld = data + off; m->llen = len; }
static void ch_set_sample(AonMix *m, const int8_t *data, uint32_t off, uint32_t len) { if (len) { m->pd = data + off; m->plen = len; m->pend = 1; } }

/* ---------------------------------------------------------------- InitializeSound */
static void initialize_sound(AonPlayer *p, int start) {
    p->tempo = 125; p->speed = 6; p->frame_cnt = 0;
    p->pattern_break = 0; p->pat_cnt = 0; p->pat_delay_cnt = 0;
    p->loop_flag = 0; p->loop_point = 0; p->loop_cnt = 0;
    p->position = (uint8_t)start; p->current_pattern = p->s->position_list[start];
    p->noise_avoid = 0; p->oversize = 0;
    memset(p->event, 0, sizeof p->event);
    memset(p->v, 0, sizeof p->v);
    for (int i = 0; i < p->nch; i++) {
        AonVoice *v = &p->v[i];
        v->wave = -1; v->rep = -1; v->instr = -1; v->sw_act = -1; v->sw_rep = -1;
        v->s_env = ENV_DONE; v->fx_com = FX_ARPEGGIO; v->track_volume = 64;
    }
    p->end_reached = 0; p->restart_song = 0;
}

/* ---------------------------------------------------------------- forward */
static void do_fx_vibrato(AonVoice *v, uint8_t arg);

static void init_adsr(AonVoice *v, const AonInst *in) {
    if (v->fx_com != FX_SYNTH_CONTROL || (v->fx_dat & 0x01) == 0) {
        v->s_vol = in->env_start;
        if (in->env_add != 0) { v->s_add = in->env_add; v->s_sub = in->env_sub; v->s_end = in->env_end; v->s_env = ENV_ADD; }
        else { v->s_vol = 127; v->s_env = ENV_DONE; }
    }
}

static void start_repeat(AonPlayer *p, const AonCell *t, AonVoice *v, const AonInst *si) {
    const AonSong *s = p->s;
    if (t->note != 0) v->per_slide = 0;
    int wf = wave_idx(s, si->wave_form);
    v->old_wave_len = v->wave_len;
    v->wave_len = (uint16_t)si->length;
    if (si->loop_length != 0) {
        if (p->oversize || si->loop_start != 0) {
            v->rep = wf; v->rep_off = si->loop_start * 2; v->rep_len = (uint16_t)si->loop_length;
            if (!p->oversize) v->wave_len = (uint16_t)(si->loop_length + si->loop_start);
        } else { v->rep = wf; v->rep_off = 0; v->rep_len = (uint16_t)si->loop_length; }
    } else { v->rep = -1; v->rep_off = 0; v->rep_len = 0; }
    uint32_t offset = si->start_offset * 2;
    v->wave_len = (uint16_t)(v->wave_len - (uint16_t)(v->old_sample_offset / 2));
    if (v->fx_com == FX_SAMPLE_OFFSET) {
        uint32_t new_offset = v->fx_dat * 256u;
        int16_t new_wave_len = (int16_t)(uint32_t)(v->wave_len - (new_offset / 2));
        if (new_wave_len < 0) {
            v->wave = v->rep; v->wave_off = v->rep_off;
            v->wave_len = (uint16_t)(v->rep_off + v->rep_len);
        } else {
            v->wave_len = (uint16_t)new_wave_len;
            v->old_sample_offset += new_offset;
            v->wave = wf; v->wave_off = offset + v->old_sample_offset;
        }
    } else { v->wave = wf; v->wave_off = offset + v->old_sample_offset; }
}

static void start_sample(AonPlayer *p, const AonCell *t, AonVoice *v, const AonInst *si) {
    v->vib_on = 0x21; v->ch_mode = 0;
    if (v->fx_com == FX_EXTRA) {
        uint8_t ex = (uint8_t)(v->fx_dat & 0xf0), lo = (uint8_t)(v->fx_dat & 0x0f);
        if (ex == EX_NOTE_DELAY) v->step_fx_cnt = lo;
        else { if (ex == EX_RETRIG) v->step_fx_cnt = lo; v->ch_flag = 3; }
    } else v->ch_flag = 3;
    start_repeat(p, t, v, si);
}

static void init_synth(AonPlayer *p, const AonCell *t, AonVoice *v, const AonInst *si) {
    const AonSong *s = p->s;
    v->ch_mode = 1;
    uint8_t effect = v->fx_com, ex = (uint8_t)(v->fx_dat & 0xf0);
    if (effect == FX_EXTRA && ex == EX_RETRIG) v->step_fx_cnt = (uint8_t)(v->fx_dat & 0x0f);
    if (t->note != 0) {
        v->per_slide = 0;
        uint8_t arg = effect == FX_SYNTH_CONTROL ? v->fx_dat : 0;
        if ((arg & 0x10) == 0) {
            if (effect == FX_EXTRA && ex == EX_NOTE_DELAY) v->step_fx_cnt = (uint8_t)(v->fx_dat & 0x0f);
            else v->ch_flag = 3;
            int wf = wave_idx(s, si->wave_form);
            int reset = 1;
            if (v->wave == wf && v->wave_off == 0) { v->ch_flag = 0; reset = v->sw_cont == 0; }
            if (reset) {
                v->wave = wf; v->wave_off = 0;
                uint16_t wl = si->s_length;
                uint32_t offset = 0;
                if (effect == FX_SAMPLE_OFFSET) {
                    offset = (uint32_t)v->fx_dat * wl;
                    if (v->sw_stop != 0) { v->sw_act = wf; v->sw_act_off = offset; v->rep = wf; v->rep_off = offset; }
                }
                if (v->sw_stop == 0) {
                    v->sw_act = wf; v->sw_act_off = offset;
                    v->old_wave_len = v->wave_len;
                    v->wave_len = wl; v->rep_len = wl;
                    wl = (uint16_t)(wl * 2);
                    v->sw_add_bytes = wl;
                    v->rep = wf; v->rep_off = (uint32_t)si->wave_loop_start * wl + offset;
                    v->sw_end_off = (uint32_t)si->wave_length * wl + offset;
                    v->sw_rep = wf; v->sw_rep_off = (uint32_t)si->wave_loop_start * wl + offset;
                    v->sw_rep_end_off = (uint32_t)(si->wave_loop_length + si->wave_loop_start) * wl + offset;
                    v->sw_cnt = si->wave_speed; v->sw_spd = si->wave_speed; v->sw_rep_ctrl = si->wave_loop_control;
                }
            }
        }
        v->vib_on = 0;
        if (si->vib_wave != 3) {
            v->vib_trig_delay = si->vib_delay;
            if (si->vib_param != 0) {
                do_fx_vibrato(v, si->vib_param);
                v->vib_ampl &= 0x9f;
                v->vib_ampl |= (uint8_t)((uint8_t)(si->vib_wave >> 3) | (uint8_t)(si->vib_wave << (8 - 3)));
                v->vib_cont = 1;
            } else v->vib_trig_delay = -2;
        } else v->vib_on = 0x21;
    }
}

static void use_old_instrument(AonPlayer *p, const AonCell *t, AonVoice *v, const AonInst *in) {
    v->vib_cont = 0;
    init_adsr(v, in);
    if (in->type == 0) start_sample(p, t, v, in);
    else init_synth(p, t, v, in);
}

/* ---------------------------------------------------------------- GetDaChannel */
static void get_da_channel(AonPlayer *p, const AonCell *t, AonVoice *v) {
    const AonSong *s = p->s;
    v->fx_com = t->effect; v->fx_dat = t->arg;
    if (v->fx_com == FX_PATTERN_BREAK) { if (p->pat_cnt == 63) p->pat_cnt = 62; }
    uint8_t arg_hi = (uint8_t)(v->fx_dat & 0xf0), arg_lo = (uint8_t)(v->fx_dat & 0x0f);
    if (v->fx_com == FX_NEW_VOLUME) v->step_fx_cnt = arg_lo;
    else if (v->fx_com == FX_EXTRA) {
        if (arg_hi == EX_NOTE_CUT) v->step_fx_cnt = arg_lo;
        else if (arg_hi == EX_PATTERN_DELAY) { if (p->pat_delay_cnt < 0) p->pat_delay_cnt = (int8_t)arg_lo; }
        else if (arg_hi == EX_PATTERN_LOOP) {
            if (p->loop_cnt != 0xf0 && arg_lo != 0) {
                if (p->loop_cnt == 0) p->loop_cnt = arg_lo;
                p->loop_cnt--;
                if (p->loop_cnt == 0) p->loop_cnt = 0xf0;
                p->loop_flag = 1;
            }
        }
    }
    const AonInst *in;
    int16_t instr_num = (int16_t)(t->instrument - 1);
    if (instr_num < 0) {
        if (v->instr < 0) return;
        in = &s->instruments[v->instr];
        if (t->note != 0) { if (!IS_TONE_PORTA(v->fx_com)) use_old_instrument(p, t, v, in); }
    } else {
        if (instr_num >= s->ninstruments) return;
        in = &s->instruments[instr_num];
        if (t->note == 0) {
            if (in->type == 0) {
                if (v->instr != instr_num) { v->instr = instr_num; v->instr_num = instr_num; v->ch_flag = 1; start_repeat(p, t, v, in); }
            }
            v->volume = in->volume;
        } else {
            v->old_sample_offset = 0;
            if (v->instr != instr_num || !IS_TONE_PORTA(v->fx_com)) { v->instr = instr_num; v->instr_num = instr_num; use_old_instrument(p, t, v, in); }
            v->volume = in->volume;
        }
    }
    uint8_t note = t->note;
    if (note == 0) { note = v->last_note; if (note == 0 || note > 60) return; }
    else { v->slide_flag = 0; v->last_note = note; if (note > 60) return; }
    v->arp_ft = in->fine_tune;
    note--;
    if (IS_TONE_PORTA(v->fx_com)) {
        if (t->note != 0) {
            v->slide_flag = 1;
            uint16_t period = period_of(v->arp_ft, note);
            v->per_slide = (int16_t)(v->period + v->per_slide - period);
        }
    }
    if (v->arp_tab[1] == -1) { v->arp_off = 0; v->arp_cnt = 0; }
    if (v->fx_com == FX_ARPEGGIO && v->fx_dat != 0) {
        uint8_t a1 = (uint8_t)((v->fx_dat & 0xf0) >> 4), a2 = (uint8_t)(v->fx_dat & 0x0f);
        v->arp_tab[0] = note; v->arp_tab[1] = (int16_t)(note + a1); v->arp_tab[2] = (int16_t)(note + a2); v->arp_tab[3] = -1;
    } else {
        const uint8_t *arp = s->arpeggios[t->arpeggio & 15];
        int ro = 0, wo = 0;
        uint8_t ab = arp[ro++];
        uint8_t count = (uint8_t)(ab >> 4);
        if (count != 0) {
            v->arp_tab[wo++] = (int16_t)((ab & 0x0f) + note);
            count--;
            while (count > 0 && wo < 7) {                                  /* C# would write past the 8 entries for counts > 7 (exception) */
                ab = arp[ro];                                              /* readOffset is not advanced in the C# (kept) */
                v->arp_tab[wo++] = (int16_t)(((ab & 0xf0) >> 4) + note);
                count--;
                if (count == 0 || wo >= 7) break;
                v->arp_tab[wo++] = (int16_t)((ab & 0x0f) + note);
                count--;
            }
        } else {
            v->arp_off = 0;
            v->arp_cnt = (uint8_t)(v->arp_spd - 1);
            v->arp_tab[wo++] = note;
        }
        v->arp_tab[wo] = -1;
    }
}

/* ---------------------------------------------------------------- PlayNewStep */
static void play_new_step(AonPlayer *p) {
    const AonSong *s = p->s;
    int one_more;
    do {
        one_more = 0;
        if (!p->pattern_break) {
            if (p->pat_delay_cnt > 0) { p->pat_delay_cnt--; return; }
            p->pat_delay_cnt = -1;
            int row = p->pat_cnt & 63;                                     /* C#: PatCnt can be 255 after E50 on row 63 + E6x (exception) */
            p->shown_pos = p->position; p->shown_pat = p->current_pattern; p->shown_row = row;
            for (int i = 0; i < p->nch; i++) get_da_channel(p, cell_at(s, p->current_pattern, row, i), &p->v[i]);
            if (p->loop_flag) { p->loop_flag = 0; p->pat_cnt = p->loop_point; return; }
            p->pat_cnt++;
            if (p->pat_cnt < 64) return;
        } else p->position = p->new_position;
        p->pat_cnt = 0; p->pat_delay_cnt = 0; p->loop_point = 0; p->loop_cnt = 0;
        p->position++;
        if (p->position >= s->number_of_positions) { p->position = s->restart_position; p->end_reached = 1; }
        else if (p->visited[p->position]) p->end_reached = 1;
        p->visited[p->position] = 1;
        if (p->pattern_break) {
            p->pattern_break = 0;
            p->current_pattern = s->position_list[p->position];
            one_more = 1;
        }
    } while (one_more);
}

/* ---------------------------------------------------------------- effects */
static void do_fx_portamento_up(AonVoice *v, uint8_t arg) { v->per_slide = (int16_t)(v->per_slide - arg); }
static void do_fx_portamento_down(AonVoice *v, uint8_t arg) { v->per_slide = (int16_t)(v->per_slide + arg); }
static void do_fx_tone_slide(AonVoice *v) {
    if (v->slide_flag && v->per_slide != 0) {
        if (v->per_slide < 0) { v->per_slide = (int16_t)(v->per_slide + v->gliss_spd); if (v->per_slide >= 0) v->per_slide = 0; }
        else { v->per_slide = (int16_t)(v->per_slide - v->gliss_spd); if (v->per_slide < 0) v->per_slide = 0; }
    }
}
static void do_fx_vibrato(AonVoice *v, uint8_t arg) {
    if (arg != 0) {
        uint8_t spd = (uint8_t)((arg & 0xf0) >> 4);
        if (spd != 0) v->vib_spd = spd;
        uint8_t ampl = (uint8_t)(arg & 0x0f);
        if (ampl != 0) { v->vib_ampl &= 0xf0; v->vib_ampl |= ampl; }
    }
}
static void do_fx_vib_old_ampl(AonVoice *v) {
    if (!v->vib_done) {
        v->vib_done = 1;
        const uint8_t *table;
        uint8_t va = (uint8_t)(v->vib_ampl & 0x60);
        if (va == 0) table = vib_sine; else if (va == 32) table = vib_ramp_down; else table = vib_square;
        uint8_t vv = table[v->vib_pos & 31];
        uint16_t add = (uint16_t)(((v->vib_ampl & 0x0f) * vv) >> 7);
        if (v->vib_ampl & 0x80) v->period = (uint16_t)(v->period - add); else v->period = (uint16_t)(v->period + add);
        v->vib_pos = (uint8_t)(v->vib_pos + v->vib_spd);
        if (v->vib_pos >= 32) { v->vib_pos &= 0x1f; v->vib_ampl ^= 0x80; }
    }
}
static void do_fx_volume_slide(AonVoice *v, uint8_t arg) {
    uint8_t lo = (uint8_t)(arg & 0x0f), hi = (uint8_t)((arg & 0xf0) >> 4);
    int8_t vol = (int8_t)v->volume;
    if (hi == 0) { vol = (int8_t)(vol - lo); if (vol < 0) vol = 0; }
    else { vol = (int8_t)(vol + hi); if (vol > 64) vol = 64; }
    v->volume = (uint8_t)vol;
}
static void do_fx_set_volume(AonVoice *v, uint8_t arg) { if (arg > 64) arg = 64; v->volume = arg; }
static void do_fx_fine_vol_up(AonPlayer *p, AonVoice *v, uint8_t arg) {
    if (p->frame_cnt == 0) { v->volume = (uint8_t)(v->volume + arg); if (v->volume > 64) v->volume = 64; }
}
static void do_fx_fine_vol_down(AonPlayer *p, AonVoice *v, uint8_t arg) {
    if (p->frame_cnt == 0) { int8_t nv = (int8_t)(v->volume - arg); if (nv < 0) nv = 0; v->volume = (uint8_t)nv; }
}
static void do_fx_fine_vol_up_down(AonPlayer *p, AonVoice *v, uint8_t arg) {
    uint8_t hi = (uint8_t)((arg & 0xf0) >> 4);
    if (hi == 0) do_fx_fine_vol_down(p, v, (uint8_t)(arg & 0x0f)); else do_fx_fine_vol_up(p, v, hi);
}
static void do_fx_port_vol_slide(AonPlayer *p, AonVoice *v, uint8_t arg, int down) {
    uint8_t lo = (uint8_t)(arg & 0x0f), hi = (uint8_t)((arg & 0xf0) >> 4);
    int8_t nib = nibble_tab[hi];
    if (nib < 0) do_fx_fine_vol_down(p, v, (uint8_t)-nib); else do_fx_fine_vol_up(p, v, (uint8_t)nib);
    if (p->frame_cnt != 0) { if (down) do_fx_portamento_down(v, lo); else do_fx_portamento_up(v, lo); }
}
static void do_fx_set_loop_point(AonPlayer *p) {
    uint8_t lp = (uint8_t)(p->pat_cnt - 1);
    if (lp != p->loop_point) { p->loop_point = lp; p->loop_cnt = 0; }
}
static void do_fx_retrig_note(AonVoice *v) {
    if (v->step_fx_cnt == 0) { v->ch_flag = 3; v->fx_com = 0xef; }
    else v->step_fx_cnt--;
}
static void do_fx_e_commands(AonPlayer *p, AonVoice *v, uint8_t arg) {
    uint8_t lo = (uint8_t)(arg & 0x0f);
    switch (arg & 0xf0) {
        case EX_SET_FILTER: p->amiga_filter = lo == 0; break;
        case EX_FINE_SLIDE_UP: if (p->frame_cnt == 0) v->per_slide = (int16_t)(v->per_slide - lo); break;
        case EX_FINE_SLIDE_DOWN: if (p->frame_cnt == 0) v->per_slide = (int16_t)(v->per_slide + lo); break;
        case EX_SET_VIB_WAVE: {
            uint8_t a = (uint8_t)(lo & 0x03);
            a = (uint8_t)((uint8_t)(a >> 3) | (uint8_t)(a << (8 - 3)));
            v->vib_ampl &= 0x9f; v->vib_ampl |= a;
            break;
        }
        case EX_SET_LOOP: do_fx_set_loop_point(p); break;
        case EX_PATTERN_LOOP: if (lo == 0) do_fx_set_loop_point(p); break;
        case EX_RETRIG: do_fx_retrig_note(v); break;
        case EX_FINE_VOL_UP: do_fx_fine_vol_up(p, v, lo); break;
        case EX_FINE_VOL_DOWN: do_fx_fine_vol_down(p, v, lo); break;
        case EX_NOTE_CUT: if (v->step_fx_cnt == 0) v->volume = 0; else v->step_fx_cnt--; break;
        case EX_NOTE_DELAY: do_fx_retrig_note(v); break;
    }
}

/* ---------------------------------------------------------------- DoSynth */
static void reset_loop(AonVoice *v) {
    switch (v->sw_rep_ctrl) {
        case 0: v->sw_act = v->sw_rep; v->sw_act_off = v->sw_rep_off; v->sw_end_off = v->sw_rep_end_off; break;
        case 1: {
            v->sw_act = v->sw_rep; v->sw_act_off = v->sw_end_off;
            int32_t off = v->sw_add_bytes;
            if (off >= 0) off = -off;
            if (v->sw_stop != 0) return;
            v->sw_act_off = (uint32_t)((int64_t)v->sw_act_off + off);
            v->sw_add_bytes = off;
            break;
        }
        default:
            v->sw_act = v->sw_rep; v->sw_end_off = v->sw_rep_end_off;
            v->sw_act_off = (uint32_t)((int64_t)v->sw_act_off - v->sw_add_bytes);
            v->sw_add_bytes = -v->sw_add_bytes;
            break;
    }
}

static void do_synth(AonVoice *v) {
    v->vib_done = 0;
    if (v->ch_flag == 0) {
        if (v->ch_mode != 0 && v->wave >= 0) {
            if (v->sw_stop == 0) {
                v->sw_cnt++;
                if (v->sw_cnt >= v->sw_spd) {
                    v->sw_cnt = 0;
                    int32_t add = v->sw_add_bytes;
                    v->sw_act_off = (uint32_t)((int64_t)v->sw_act_off + add);
                    if (add < 0) { if ((int64_t)(int32_t)v->sw_act_off < (int64_t)v->sw_rep_off) reset_loop(v); }   /* C#: int vs uint compare as long */
                    else { if ((int64_t)(int32_t)v->sw_act_off >= (int64_t)v->sw_end_off) reset_loop(v); }
                    v->ch_flag = 1;
                    v->rep = v->sw_act; v->rep_off = v->sw_act_off;
                }
            }
        }
        if (v->wave >= 0) {
            int vol = v->s_vol;
            switch (v->s_env) {
                case ENV_ADD: vol += v->s_add; if (vol > 127) { vol = 127; v->s_env = ENV_SUB; } break;
                case ENV_SUB: vol -= v->s_sub; if (vol <= v->s_end) { vol = v->s_end; v->s_env = ENV_DONE; } break;
            }
            v->s_vol = (uint8_t)vol;
            if (v->vib_on != 0x21) {
                if (v->vib_trig_delay == -1) v->vib_on = 1;
                else v->vib_trig_delay--;
            }
        }
    }
    if (v->vib_on == 1) do_fx_vib_old_ampl(v);
}

/* ---------------------------------------------------------------- DoFx / PlayFx */
static void do_fx(AonPlayer *p, AonVoice *v) {
    if (v->vib_cont == 0) v->vib_on = 0x21;
    v->arp_cnt++;
    if (v->arp_cnt >= v->arp_spd) v->arp_cnt = 0;
    int16_t po = v->arp_tab[v->arp_off & 7];
    if (po >= 0) {
        if (po >= 60) po = 59;
        v->period = period_of(v->arp_ft, po);
        v->arp_off++; v->arp_off &= 0x07;
    } else v->arp_off = 0;
    if (v->fx_com != FX_ARPEGGIO) {
        uint8_t arg = v->fx_dat;
        if (p->frame_cnt != 0) {
            switch (v->fx_com) {
                case FX_SLIDE_UP: do_fx_portamento_up(v, arg); break;
                case FX_SLIDE_DOWN: do_fx_portamento_down(v, arg); break;
                case FX_TONE_PORTA: if (arg != 0) v->gliss_spd = arg; do_fx_tone_slide(v); break;
                case FX_VIBRATO: v->vib_on = 1; do_fx_vibrato(v, arg); break;
                case FX_TONE_PORTA_VOLSLIDE: do_fx_tone_slide(v); do_fx_volume_slide(v, arg); break;
                case FX_VIBRATO_VOLSLIDE: do_fx_vib_old_ampl(v); do_fx_volume_slide(v, arg); break;
                case FX_VOLUME_SLIDE: do_fx_volume_slide(v, arg); break;
            }
        }
        switch (v->fx_com) {
            case FX_POSITION_JUMP: p->new_position = (uint8_t)(arg - 1); p->pat_cnt = 0; p->pattern_break = 1; break;
            case FX_SET_VOLUME: do_fx_set_volume(v, arg); break;
            case FX_PATTERN_BREAK: {
                uint8_t lo = (uint8_t)(arg & 0x0f), hi = (uint8_t)(arg & 0xf0);
                uint8_t temp = (uint8_t)(hi >> 1);
                uint8_t pos = (uint8_t)(temp + (temp >> 2));
                pos = (uint8_t)(pos + lo);
                if (pos >= 64) pos = 0;
                p->pat_cnt = pos; p->new_position = p->position; p->pattern_break = 1;
                break;
            }
            case FX_EXTRA: do_fx_e_commands(p, v, arg); break;
            case FX_SET_SPEED:
                if (arg != 0) {
                    if (arg <= 32) p->speed = arg;
                    else if (arg <= 200) p->bpm = arg;
                } else { p->end_reached = 1; p->restart_song = 1; }
                break;
            case FX_NEW_VOLUME:
                if (v->step_fx_cnt == 0) v->volume = (uint8_t)(((arg & 0xf0) >> 4) * 4 + 4);
                else v->step_fx_cnt--;
                break;
            case FX_WAVE_SPEED: v->sw_spd = (uint8_t)((arg & 0xf0) >> 4); break;
            case FX_SET_ARP_SPEED: if ((arg & 0x0f) != 0) v->arp_spd = (uint8_t)(arg & 0x0f); break;
            case FX_SET_VOLUME_VIBRATO: do_fx_vib_old_ampl(v); do_fx_set_volume(v, arg); break;
            case FX_FINE_SLIDE_PORTA_UP: do_fx_port_vol_slide(p, v, arg, 0); break;
            case FX_FINE_SLIDE_PORTA_DOWN: do_fx_port_vol_slide(p, v, arg, 1); break;
            case FX_AVOID_NOISE: p->noise_avoid = arg != 0; break;
            case FX_OVERSIZE: p->oversize = arg != 0; break;
            case FX_FINE_VOLSLIDE_VIBRATO: do_fx_vib_old_ampl(v); do_fx_fine_vol_up_down(p, v, arg); break;
            case FX_VOLUME_PORT_DOWN_SLIDE: do_fx_portamento_down(v, (uint8_t)((arg >> 4) * 8)); do_fx_volume_slide(v, (uint8_t)(arg & 0x0f)); break;
            case FX_SET_VOLUME_TONE_PORTA: do_fx_tone_slide(v); do_fx_set_volume(v, arg); break;
            case FX_FINE_VOLSLIDE_TONE_PORTA: do_fx_tone_slide(v); do_fx_fine_vol_up_down(p, v, arg); break;
            case FX_SET_TRACK_VOLUME: if (arg > 64) arg = 64; v->track_volume = arg; break;
            case FX_SET_WAVE_MODE: v->sw_cont = (uint8_t)(arg & 0x0f); v->sw_stop = (uint8_t)((arg & 0xf0) >> 4); break;
            case FX_EXTERNAL_EVENT: if (p->frame_cnt == 0) p->event[0] = arg; break;
        }
    }
    do_synth(v);
}

/* ---------------------------------------------------------------- SetupChannel */
static void setup_channel(AonPlayer *p, AonVoice *v, AonMix *c) {
    const AonSong *s = p->s;
    if (v->fx_com != FX_EXTRA || (v->fx_dat & 0xf0) != EX_NOTE_DELAY) {
        if ((v->ch_flag & 0x02) != 0 && v->wave >= 0) {
            const int8_t *wd = s->waves[v->wave];
            int64_t avail = (int64_t)s->wave_len[v->wave] - (int64_t)v->wave_off;   /* C#: int - uint = long */
            int64_t want = (int64_t)v->wave_len * 2;
            int64_t l = avail < want ? avail : want;
            uint32_t off = v->wave_off, length;
            if (l < 0) { off = 0; length = 0; } else length = (uint32_t)l;          /* C# would pass a huge length (offset past the end) */
            if (p->noise_avoid) {
                if (v->old_wave_len > 255 || v->old_wave_len == 0 || v->wave_len > 255) ch_play_sample(c, wd, off, length);
                else ch_set_sample(c, wd, off, length);
            } else ch_play_sample(c, wd, off, length);
            if (v->rep >= 0 && v->rep_len > 1) {
                uint32_t rl = s->wave_len[v->rep];
                length = v->rep_len * 2u;
                if ((int64_t)v->rep_off + length > (int64_t)rl) length = (uint32_t)((int64_t)rl - v->rep_off);
                if (length > 0 && v->rep_off < rl) ch_set_loop(c, s->waves[v->rep], v->rep_off, length);
            }
        } else {
            if (v->rep >= 0 && v->rep_len > 1) {
                uint32_t rl = s->wave_len[v->rep];
                uint32_t length = v->rep_len * 2u;
                if ((int64_t)v->rep_off + length > (int64_t)rl) length = (uint32_t)((int64_t)rl - v->rep_off);
                if (length > 0 && v->rep_off < rl) { ch_set_sample(c, s->waves[v->rep], v->rep_off, length); ch_set_loop(c, s->waves[v->rep], v->rep_off, length); }
            }
        }
        uint32_t period = (uint32_t)(v->period + v->per_slide);
        if (period < 103) period = 103;
        c->period = period;
        c->volume = (uint16_t)((((v->volume * v->s_vol) / 128) * v->track_volume) / 64);
        v->ch_flag = 0;
    }
}

/* ---------------------------------------------------------------- Play() */
static void play_tick(AonPlayer *p) {
    p->frame_cnt++;
    if (p->speed != 0) { if (p->frame_cnt >= p->speed) { p->frame_cnt = 0; play_new_step(p); } }
    uint8_t pat = p->s->position_list[p->position];
    if (pat != p->current_pattern) p->current_pattern = pat;
    for (int i = 0; i < p->nch; i++) do_fx(p, &p->v[i]);
    for (int i = 0; i < p->nch; i++) setup_channel(p, &p->v[i], &p->m[i]);
    if (p->end_reached) { p->ended = 1; p->end_reached = 0; }    /* OnEndReached: the song is over (no restart) */
}

/* ---------------------------------------------------------------- public */
AonPlayer *aon_new(const AonSong *s, int rate) {
    if (!s || rate <= 0) return NULL;
    AonPlayer *p = (AonPlayer *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->s = s; p->rate = rate; p->nch = s->channels;
    { double w0 = 6.283185307179586 * 3275.0 / rate, al = sin(w0) / (2.0 * 0.7071067811865476), co = cos(w0), a0 = 1.0 + al;
      p->lb0 = (1.0 - co) / 2.0 / a0; p->lb1 = (1.0 - co) / a0; p->lb2 = p->lb0; p->la1 = -2.0 * co / a0; p->la2 = (1.0 - al) / a0; }
    { /* 8 voices: the original mixes two voices into each Paula channel in software; against uade (62 files) a 2-pole low-pass at 6 kHz and 0.6 per voice
         match best (spectrogram 0.825 -> 0.856, 5-12 kHz within about 2 dB instead of up to +17 dB, level 1.0) */
      double fc = 6000.0; p->g8 = 0.6f; p->hlp = 1;
      if (p->hlp) { double w0 = 6.283185307179586 * fc / rate, al = sin(w0) / (2.0 * 0.7071067811865476), co = cos(w0), a0 = 1.0 + al;
        p->hb0 = (1.0 - co) / 2.0 / a0; p->hb1 = (1.0 - co) / a0; p->hb2 = p->hb0; p->ha1 = -2.0 * co / a0; p->ha2 = (1.0 - al) / a0; } }
    p->bpm = 125;                                                  /* NostalgicPlayer default: 50 Hz */
    initialize_sound(p, 0);
    p->visited[0] = 1;
    return p;
}

void aon_player_free(AonPlayer *p) { free(p); }

static inline void mix_advance(AonMix *m) {                        /* end of a pass: pending SetSample, then the loop, else stop */
    while (m->on && m->pos >= m->len) {
        if (m->pend) { m->pos -= m->len; m->d = m->pd; m->len = m->plen; m->pend = 0; }
        else if (m->llen > 0) { m->pos -= m->len; m->d = m->ld; m->len = m->llen; }
        else { m->on = 0; break; }
        if (m->pos < 0) m->pos = 0;
    }
}

int aon_render(AonPlayer *p, int16_t *out, int n) {
    if (!p || p->ended) return 0;
    for (int f = 0; f < n; f++) {
        if (p->tick_left <= 0) {
            play_tick(p);
            if (p->ended) { memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(n - f)); return f; }
            double hz = (p->bpm ? p->bpm : 125) * 2.0 / 5.0;          /* SetBpmTempo(bpm): bpm * 2 / 5 ticks per second */
            p->tick_left += p->rate / hz;
        }
        p->tick_left -= 1.0;
        float l = 0, rr = 0;
        for (int i = 0; i < p->nch; i++) {
            AonMix *m = &p->m[i];
            size_t si = p->sc ? (size_t)i * (size_t)p->sc_len + (p->sc_pos & (unsigned)(p->sc_len - 1)) : 0;
            if (p->sc) p->sc[si] = 0;
            mix_advance(m);
            if (!m->on || !m->d || m->len == 0) continue;
            uint32_t ip = (uint32_t)m->pos;
            if (ip >= m->len) ip = m->len - 1;
            int s0 = m->d[ip], s1;
            if (ip + 1 < m->len) s1 = m->d[ip + 1];
            else if (m->pend && m->plen) s1 = m->pd[0];
            else if (m->llen > 0 && m->ld) s1 = m->ld[0];
            else s1 = 0;
            float fr = (float)(m->pos - (double)ip);
            float smp = (float)s0 + (float)(s1 - s0) * fr;
            double per = m->period ? m->period : 428;
            m->pos += PAULA_PAL / (per * p->rate);
            if (p->mute >> i & 1) continue;
            float v = smp * (float)m->volume * (10.0f / 7.0f);
            if (p->nch == 8) v *= p->g8;                                /* 8 voices: two voices share one Paula channel */
            if (p->sc) p->sc[si] = smp * (float)m->volume / (127.0f * 64.0f);
            int right = p->nch == 8 ? pan8[i] : pan4[i & 3];
            if (right) rr += v; else l += v;
        }
        if (p->nch == 8 && p->hlp) {                               /* 8 voices: low-pass of the software mix (see aon_new) */
            double x[2] = { l, rr };
            for (int sd = 0; sd < 2; sd++) { double *z = p->hz[sd], y = p->hb0 * x[sd] + p->hb1 * z[0] + p->hb2 * z[1] - p->ha1 * z[2] - p->ha2 * z[3]; z[1] = z[0]; z[0] = x[sd]; z[3] = z[2]; z[2] = y; x[sd] = y; }
            l = (float)x[0]; rr = (float)x[1];
        }
        {   /* LED filter (E00 on / E01 off) */
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
        { float a = l, b = rr; l = 0.866f * a + 0.5f * b; rr = 0.5f * a + 0.866f * b; }   /* uade's default stereo mix, as ml_replay.c */
        p->sc_pos++;
        out[2 * f] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2 * f + 1] = (int16_t)(rr > 32767 ? 32767 : rr < -32768 ? -32768 : rr);
    }
    return n;
}

int aon_channels(const AonSong *s) { return s ? s->channels : 0; }
const char *aon_title(const AonSong *s) { return s ? s->title : ""; }
const char *aon_author(const AonSong *s) { return s ? s->author : ""; }
int aon_positions(const AonSong *s) { return s ? s->number_of_positions : 0; }

void aon_pos(const AonPlayer *p, int *position, int *pattern, int *row) {
    if (position) *position = p ? p->shown_pos : 0;
    if (pattern) *pattern = p ? p->shown_pat : 0;
    if (row) *row = p ? p->shown_row : 0;
}

int aon_cell(const AonSong *s, int pattern, int row, int ch, int *note, int *inst, int *fx, int *arg) {
    if (!s || pattern < 0 || pattern >= s->npatterns || row < 0 || row >= 64 || ch < 0 || ch >= s->channels) return 0;
    const AonCell *c = cell_at(s, pattern, row, ch);
    if (note) *note = c->note;
    if (inst) *inst = c->instrument;
    if (fx) *fx = c->effect;
    if (arg) *arg = c->arg;
    return 1;
}

void aon_set_mute(AonPlayer *p, unsigned mask) { if (p) p->mute = mask; }
void aon_set_scope(AonPlayer *p, float *buf, int len) {
    if (!p) return;
    if (!buf || len <= 0 || (len & (len - 1))) { p->sc = NULL; p->sc_len = 0; return; }
    p->sc = buf; p->sc_len = len;
}
unsigned aon_scope_pos(const AonPlayer *p) { return p ? p->sc_pos : 0; }

int aon_position_pattern(const AonSong *s, int position) {
    if (!s || position < 0 || position >= s->number_of_positions) return -1;
    int pt = s->position_list[position & 255];
    return pt < s->npatterns ? pt : -1;
}
