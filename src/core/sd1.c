/* SidMon 1.0 (Amiga, R. van Vliet): loader + replayer, a port of the NostalgicPlayer SidMon10Worker (MIT).
   SidMon 1.0 modules are ripped executables (player + data): like the C#, the 68k init and play code in the first
   4096 bytes is searched for the offsets of the data blocks, the period table version, the waveform mixing and the
   "filter" (a waveform inversion, not the LED: the ripped players never write $bfe001), and the special samples of the
   later player version. C# integer semantics are kept (byte/sbyte/ushort wrap, truncating casts). Paula-like mixer in
   the style of bp.c. Timing: the worker never sets PlayingFrequency, so Play() runs at the default 50 Hz (PAL vblank).
   Sub-songs: the worker derives from ModulePlayerWithSubSongDurationAgentBase without SubSongs, so there is one song;
   it ends where the C# sets endReached (the position list wraps around). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sd1.h"

#define PAULA_PAL 3546895.0
#define SD1_TICK_HZ 50.0
#define SEARCH_LEN 4096                                             /* Identify reads the first 4096 bytes */
#define MAX_SPECIAL 512

/* ---------------------------------------------------------------- tables (Tables.cs) */
static const uint16_t periods1[470] = {
       0, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232,
    3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616,
    1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  904,  856,  808,
     762,  720,  678,  640,  604,  570,  538,  508,  480,  452,  428,  404,
     381,  360,  339,  320,  302,  285,  269,  254,  240,  226,  214,  202,
     190,  180,  170,  160,  151,  143,  135,  127,    0,    0,    0,    0,
       0,    0,    0, 4028, 3806, 3584, 3394, 3204, 3013, 2855, 2696, 2538,
    2395, 2268, 2141, 2014, 1903, 1792, 1697, 1602, 1507, 1428, 1348, 1269,
    1198, 1134, 1071, 1007,  952,  896,  849,  801,  754,  714,  674,  635,
     599,  567,  536,  504,  476,  448,  425,  401,  377,  357,  337,  310,
     300,  284,  268,  252,  238,  224,  213,  201,  189,  179,  169,  159,
     150,  142,  134,    0,    0,    0,    0,    0,    0,    0, 3993, 3773,
    3552, 3364, 3175, 2987, 2830, 2672, 2515, 2374, 2248, 2122, 1997, 1887,
    1776, 1682, 1588, 1494, 1415, 1336, 1258, 1187, 1124, 1061,  999,  944,
     888,  841,  794,  747,  708,  668,  629,  594,  562,  531,  500,  472,
     444,  421,  397,  374,  354,  334,  315,  297,  281,  266,  250,  236,
     222,  211,  199,  187,  177,  167,  158,  149,  141,  133,    0,    0,
       0,    0,    0,    0,    0, 3957, 3739, 3521, 3334, 3147, 2960, 2804,
    2648, 2493, 2353, 2228, 2103, 1979, 1870, 1761, 1667, 1574, 1480, 1402,
    1324, 1247, 1177, 1114, 1052,  990,  935,  881,  834,  787,  740,  701,
     662,  624,  589,  557,  526,  495,  468,  441,  417,  394,  370,  351,
     331,  312,  295,  279,  263,  248,  234,  221,  209,  197,  185,  176,
     166,  156,  148,  140,  132,    0,    0,    0,    0,    0,    0,    0,
    3921, 3705, 3489, 3304, 3119, 2933, 2779, 2625, 2470, 2331, 2208, 2084,
    1961, 1853, 1745, 1652, 1560, 1467, 1390, 1313, 1235, 1166, 1104, 1042,
     981,  927,  873,  826,  780,  734,  695,  657,  618,  583,  552,  521,
     491,  464,  437,  413,  390,  367,  348,  329,  309,  292,  276,  261,
     246,  232,  219,  207,  195,  184,  174,  165,  155,  146,  138,  131,
       0,    0,    0,    0,    0,    0,    0, 3886, 3671, 3457, 3274, 3090,
    2907, 2754, 2601, 2448, 2310, 2188, 2065, 1943, 1836, 1729, 1637, 1545,
    1454, 1377, 1301, 1224, 1155, 1094, 1033,  972,  918,  865,  819,  773,
     727,  689,  651,  612,  578,  547,  517,  486,  459,  433,  410,  387,
     364,  345,  326,  306,  289,  274,  259,  243,  230,  217,  205,  194,
     182,  173,  163,  153,  145,  137,  130,    0,    0,    0,    0,    0,
       0,    0, 3851, 3638, 3426, 3244, 3062, 2880, 2729, 2577, 2426, 2289,
    2168, 2047, 1926, 1819, 1713, 1622, 1531, 1440, 1365, 1289, 1213, 1145,
    1084, 1024,  963,  910,  857,  811,  766,  720,  683,  645,  607,  573,
     542,  512,  482,  455,  429,  406,  383,  360,  342,  323,  304,  287,
     271,  256,  241,  228,  215,  203,  192,  180,  171,  162,  152,  144,
     136,  128,
};
static const uint16_t periods2[481] = {
       0, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232,
    3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616,
    1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  904,  856,  808,
     762,  720,  678,  640,  604,  570,  538,  508,  480,  452,  428,  404,
     381,  360,  339,  320,  302,  285,  269,  254,  240,  226,  214,  202,
     190,  180,  170,  160,  151,  143,  135,  127,    0,    0,    0,    0,
       0,    0,    0,    0, 4028, 3806, 3584, 3394, 3204, 3013, 2855, 2696,
    2538, 2395, 2268, 2141, 2014, 1903, 1792, 1697, 1602, 1507, 1428, 1348,
    1269, 1198, 1134, 1071, 1007,  952,  896,  849,  801,  754,  714,  674,
     635,  599,  567,  536,  504,  476,  448,  425,  401,  377,  357,  337,
     310,  300,  284,  268,  252,  238,  224,  213,  201,  189,  179,  169,
     159,  150,  142,  134,    0,    0,    0,    0,    0,    0,    0,    0,
    3993, 3773, 3552, 3364, 3175, 2987, 2830, 2672, 2515, 2374, 2248, 2122,
    1997, 1887, 1776, 1682, 1588, 1494, 1415, 1336, 1258, 1187, 1124, 1061,
     999,  944,  888,  841,  794,  747,  708,  668,  629,  594,  562,  531,
     500,  472,  444,  421,  397,  374,  354,  334,  315,  297,  281,  266,
     250,  236,  222,  211,  199,  187,  177,  167,  158,  149,  141,  133,
       0,    0,    0,    0,    0,    0,    0,    0, 3957, 3739, 3521, 3334,
    3147, 2960, 2804, 2648, 2493, 2353, 2228, 2103, 1979, 1870, 1761, 1667,
    1574, 1480, 1402, 1324, 1247, 1177, 1114, 1052,  990,  935,  881,  834,
     787,  740,  701,  662,  624,  589,  557,  526,  495,  468,  441,  417,
     394,  370,  351,  331,  312,  295,  279,  263,  248,  234,  221,  209,
     197,  185,  176,  166,  156,  148,  140,  132,    0,    0,    0,    0,
       0,    0,    0,    0, 3921, 3705, 3489, 3304, 3119, 2933, 2779, 2625,
    2470, 2331, 2208, 2084, 1961, 1853, 1745, 1652, 1560, 1467, 1390, 1313,
    1235, 1166, 1104, 1042,  981,  927,  873,  826,  780,  734,  695,  657,
     618,  583,  552,  521,  491,  464,  437,  413,  390,  367,  348,  329,
     309,  292,  276,  261,  246,  232,  219,  207,  195,  184,  174,  165,
     155,  146,  138,  131,    0,    0,    0,    0,    0,    0,    0,    0,
    3886, 3671, 3457, 3274, 3090, 2907, 2754, 2601, 2448, 2310, 2188, 2065,
    1943, 1836, 1729, 1637, 1545, 1454, 1377, 1301, 1224, 1155, 1094, 1033,
     972,  918,  865,  819,  773,  727,  689,  651,  612,  578,  547,  517,
     486,  459,  433,  410,  387,  364,  345,  326,  306,  289,  274,  259,
     243,  230,  217,  205,  194,  182,  173,  163,  153,  145,  137,  130,
       0,    0,    0,    0,    0,    0,    0,    0, 3851, 3638, 3426, 3244,
    3062, 2880, 2729, 2577, 2426, 2289, 2168, 2047, 1926, 1819, 1713, 1622,
    1531, 1440, 1365, 1289, 1213, 1145, 1084, 1024,  963,  910,  857,  811,
     766,  720,  683,  645,  607,  573,  542,  512,  482,  455,  429,  406,
     383,  360,  342,  323,  304,  287,  271,  256,  241,  228,  215,  203,
     192,  180,  171,  162,  152,  144,  136,  128,    0,    0,    0,    0,
       0,
};
static const uint16_t periods3[83] = {
       0, 6848, 6464, 6096, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840,
    3616, 3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920,
    1808, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,
     904,  856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,
     452,  428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,
     226,  214,  202,  190,  180,  170,  160,  151,  143,  135,  127,    0,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};
static const uint8_t pan4[4] = { 0, 1, 1, 0 };                    /* 0 = left, 1 = right */

enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_DONE };  /* EnvelopeState */

/* ---------------------------------------------------------------- song */
typedef struct { int8_t note; uint8_t inst, fx, arg, dur; } Sd1Row;            /* TrackRow */
typedef struct { Sd1Row *rows; int nrows; } Sd1Track;
typedef struct { uint32_t track; int8_t transpose; } Sd1Seq;                    /* Sequence */
typedef struct {                                                                 /* Instrument */
    uint32_t waveform_number;                                                    /* >= 16 -> sample number */
    uint8_t arpeggio[16];
    uint8_t attack_speed, attack_max, decay_speed, decay_min, sustain_time, release_speed, release_min;
    uint8_t phase_shift, phase_speed, fine_tune;
    int8_t pitch_fall;
    uint16_t volume;
} Sd1Inst;
typedef struct { int8_t *data; uint32_t len; int loop_start; } Sd1Sample;       /* data NULL = SampleData null */
typedef struct { uint32_t source1, source2, destination, speed; } Sd1MixInfo;
typedef struct { uint32_t instrument_number; int sample_offset, length; } Sd1Special;

struct Sd1Song {
    const uint16_t *periods; int nperiods; int fine_tune_multiply, period_version;
    int enable_filter, enable_mixing;
    Sd1Seq *seq[4]; int npos;
    Sd1Track *tracks; int ntracks;
    Sd1Inst *inst; int ninst;
    uint8_t (*wave_info)[16]; int nwave_info;
    int8_t *waves; int nwaves;                                                   /* nwaves * 32 */
    Sd1Sample *samples; int nsamples;
    Sd1MixInfo mix1, mix2;
    uint32_t start_rows, start_speed;
    int nsub;
};

/* ---------------------------------------------------------------- player */
typedef struct {                                                                 /* VoiceInfo */
    int sequence_index;
    const Sd1Seq *current_sequence;
    int row_index;
    const Sd1Track *current_track;                                               /* NULL: track number out of range */
    int current_instrument;                                                      /* index into the instruments, -1 = null */
    int instrument_number;
    uint16_t note_period;
    uint8_t bend_to; int8_t bend_speed; int8_t note_offset; uint8_t arpeggio_index;
    int envelope_in_progress;
    uint8_t row_count, volume, sustain_control;
    uint16_t pitch_control;
    uint8_t phase_index, phase_speed;
    uint16_t pitch_fall_control;
    uint8_t wave_index, waveform_number, wave_speed;
    int loop_control;
} Sd1Voice;

typedef struct { uint32_t counter, position; } Sd1MixPlay;                       /* MixPlayingInfo */

typedef struct {
    const int8_t *d; uint32_t len; double pos; int on;
    const int8_t *ld; uint32_t llen;                                             /* SetLoop: plays when the current pass ends */
    uint32_t period; int volume;                                                 /* volume 0..256 (IChannel.SetVolume) */
} Sd1Mix;

struct Sd1Player {
    const Sd1Song *s;
    int rate, sub;
    Sd1Inst *inst;                                                               /* copy: effect 0 changes AttackMax / AttackSpeed */
    int8_t *waves;                                                               /* copy: changed by the mixing and the filter effect */
    /* GlobalPlayingInfo */
    uint32_t number_of_rows, speed, speed_counter;
    int new_track, loop_song;
    int current_row;
    uint32_t current_position;
    Sd1MixPlay mix1_play, mix2_play;
    Sd1Voice v[4];
    int end_reached, ended, fast;
    int shown_pos, shown_row;
    Sd1Mix m[4];
    double tick_left;
    unsigned mute;
    float *sc; int sc_len; unsigned sc_pos;
};

/* ---------------------------------------------------------------- identify (TestModule): search buffer reader */
typedef struct {
    uint8_t buf[SEARCH_LEN];
    int bad;                                                                     /* an index outside the buffer: C# throws */
    int base_offset;
    int sequence_offsets[4];
    int track_data_offset, track_offset_table_offset, instrument_offset, waveform_list_offset, waveform_offset;
    int sample_offset, song_data_offset;
    uint32_t no_loop_value;
    int period_version, fine_tune_multiply, enable_filter, enable_mixing;
    int has_special; int nspecial; Sd1Special special[MAX_SPECIAL];
} Ident;

static int sb(Ident *id, int i) {                                    /* searchBuffer[i] */
    if (i < 0 || i >= SEARCH_LEN) { id->bad = 1; return 0; }
    return id->buf[i];
}
static int sw(Ident *id, int i) { return (int16_t)(uint16_t)(sb(id, i) << 8 | sb(id, i + 1)); }   /* ((sbyte)b[i] << 8) | b[i + 1] */

static int extract_info_from_init_function(Ident *id) {
    const int search_length = SEARCH_LEN;
    int index, channel;

    for (index = 0; index < search_length - 2; index += 2)
        if (sb(id, index) == 0x41 && sb(id, index + 1) == 0xfa) break;
    if (index >= search_length - 4) return 0;

    id->base_offset = ((sb(id, index + 2) << 8) | sb(id, index + 3)) + index + 2;
    index += 4;

    for (channel = 0; index < search_length - 2 && channel < 4; index += 2)
        if (sb(id, index) == 0xd1 && sb(id, index + 1) == 0xe8)
            id->sequence_offsets[channel++] = sw(id, index + 2) + id->base_offset;
    if (index >= search_length - 4) return 0;

    for (; index < search_length - 2; index += 2)
        if ((sb(id, index) == 0x70 && sb(id, index + 1) == 0x03) ||
            (sb(id, index) == 0x20 && sb(id, index + 1) == 0x3c && sb(id, index + 5) == 0x03)) break;
    if (index >= search_length - 4) return 0;

    if (sb(id, index) == 0x20) index += 4;

    if (sb(id, index + 2) != 0x41 || sb(id, index + 3) != 0xfa || sb(id, index + 6) != 0xd1 || sb(id, index + 7) != 0xe8) return 0;

    id->track_data_offset = sw(id, index + 8) + id->base_offset;
    index += 10;

    for (; index < search_length - 2; index += 2)
        if (sb(id, index) == 0xd9 && sb(id, index + 1) == 0xec) break;
    if (index >= search_length - 4) return 0;

    id->track_offset_table_offset = sw(id, index + 2) + id->base_offset;
    index += 4;

    for (; index < search_length - 2; index += 2)
        if (sb(id, index) == 0xd9 && sb(id, index + 1) == 0xec) break;
    if (index >= search_length - 4) return 0;

    id->instrument_offset = sw(id, index + 2) + id->base_offset;
    return !id->bad;
}

static int extract_info_from_play_function(Ident *id) {
    const int search_length = SEARCH_LEN;
    int index, index1;

    id->enable_filter = 0;
    id->enable_mixing = 0;
    id->has_special = 0; id->nspecial = 0;

    /* start to find the play function in the player */
    for (index = 0; index < search_length - 4; index += 2)
        if (sb(id, index) == 0x48 && sb(id, index + 1) == 0xe7 && sb(id, index + 2) == 0xff && sb(id, index + 3) == 0xfe) break;
    if (index >= search_length - 4) return 0;

    /* find "PlayNote" function */
    for (; index < search_length - 4; index += 2)
        if (sb(id, index) == 0x61 && sb(id, index + 1) == 0x00) break;
    if (index >= search_length - 4) return 0;

    int play_note_offset = sw(id, index + 2) + index + 2;

    /* find song data */
    for (; index < search_length - 4; index += 2)
        if (sb(id, index) == 0xd3 && sb(id, index + 1) == 0xe9) break;
    if (index >= search_length - 4) return 0;

    id->song_data_offset = sw(id, index + 2) + id->base_offset;

    /* find out if filter effect is enabled or not */
    for (; index < search_length - 4; index += 2)
        if (sb(id, index) == 0x4c && sb(id, index + 1) == 0xdf) break;
    if (index >= search_length - 4) return 0;

    index -= 4;
    if ((sb(id, index) == 0x00 && sb(id, index + 1) == 0xdf) || (sb(id, index) == 0x4e && sb(id, index + 1) == 0x71))
        index -= 6;

    for (int i = 0; i < 3; i++) {
        if (sb(id, index) != 0x61 || sb(id, index + 1) != 0x00) break;
        int check_offset = sw(id, index + 2) + index + 2;
        if (sb(id, check_offset + 14) == 0x44 && sb(id, check_offset + 15) == 0x10) id->enable_filter = 1;
        else if ((sb(id, check_offset + 12) == 0x20 && sb(id, check_offset + 13) == 0x10) ||
                 (sb(id, check_offset + 14) == 0x20 && sb(id, check_offset + 15) == 0x10)) id->enable_mixing = 1;
        index -= 4;
    }
    if (id->bad) return 0;

    /* find rest of the offsets */
    for (index = play_note_offset; index < search_length; index += 2)
        if (sb(id, index) == 0xd9 && sb(id, index + 1) == 0xec) break;

    if (index < search_length - 4) id->waveform_list_offset = sw(id, index + 2) + id->base_offset;
    else index = play_note_offset;

    for (; index < search_length; index += 2)
        if (sb(id, index) == 0xeb && (sb(id, index + 1) == 0x81 || sb(id, index + 1) == 0x49)) break;
    if (index >= search_length - 4) return 0;

    for (; index < search_length; index += 2)
        if (sb(id, index) == 0xdd && sb(id, index + 1) == 0xee) break;
    if (index >= search_length - 4) return 0;

    id->waveform_offset = sw(id, index + 2) + id->base_offset;

    /* find period table to use */
    for (index1 = index; index1 < search_length; index1 += 2)
        if (sb(id, index1) == 0x16 && sb(id, index1 + 1) == 0x2c) break;

    if (index1 >= search_length - 4) { id->period_version = 3; id->fine_tune_multiply = 0; }
    else if (sb(id, index1 + 4) == 0xc6 && sb(id, index1 + 5) == 0xfc) { id->period_version = 1; id->fine_tune_multiply = 67; }
    else { id->period_version = 2; id->fine_tune_multiply = 68; }

    /* find "PlaySample" function */
    for (; index >= 0; index -= 2)
        if (sb(id, index) == 0x64 && sb(id, index + 1) == 0x00) break;
    if (index < 4) return 0;

    if (sb(id, index - 2) == 0x00 && sb(id, index - 1) == 0x3c) {
        index = sw(id, index + 2) + index + 2;
        id->has_special = 1;

        for (; index < search_length; index += 2) {
            if (id->bad) return 0;
            if (sb(id, index) == 0x4e && sb(id, index + 1) == 0x75) break;
            if (sb(id, index) == 0x0c && sb(id, index + 1) == 0x00) {
                uint32_t instrument_number = (uint32_t)((sb(id, index + 2) << 8) | sb(id, index + 3));
                index += 8;
                if (sb(id, index) != 0x4d || sb(id, index + 1) != 0xfa) return 0;
                int start_offset = sw(id, index + 2) + index + 2;
                if (sb(id, index + 4) == 0xdd && sb(id, index + 5) == 0xfc) {
                    start_offset += (int)((uint32_t)sb(id, index + 6) << 24 | (uint32_t)sb(id, index + 7) << 16 |
                                          (uint32_t)sb(id, index + 8) << 8 | (uint32_t)sb(id, index + 9));
                    index += 6;
                }
                index += 10;
                if (sb(id, index) != 0x33 || sb(id, index + 1) != 0xfc) return 0;
                int length = ((sb(id, index + 2) << 8) | sb(id, index + 3)) * 2;
                if (id->bad || id->nspecial >= MAX_SPECIAL) return 0;
                id->special[id->nspecial].instrument_number = instrument_number;
                id->special[id->nspecial].sample_offset = start_offset;
                id->special[id->nspecial].length = length;
                id->nspecial++;
            }
        }
        if (index >= search_length - 4) return 0;
    } else {
        index = sw(id, index + 2) + index + 2;

        for (; index < search_length; index += 2)
            if (sb(id, index) == 0xd9 && sb(id, index + 1) == 0xec) break;
        if (index >= search_length - 4) return 0;

        id->sample_offset = sw(id, index + 2) + id->base_offset;

        /* find "no loop" special value */
        for (; index < search_length; index += 2)
            if (sb(id, index) == 0x0c && sb(id, index + 1) == 0xab) break;
        if (index >= search_length - 4) return 0;

        id->no_loop_value = (uint32_t)sb(id, index + 2) << 24 | (uint32_t)sb(id, index + 3) << 16 |
                            (uint32_t)sb(id, index + 4) << 8 | (uint32_t)sb(id, index + 5);
    }
    return !id->bad;
}

/* Identify + TestModule; the PSID / RSID check is not in the C# (the C64 .sid files share the extension) */
static int test_module(Ident *id, const uint8_t *b, size_t n) {
    memset(id, 0, sizeof *id);
    if (!b || n < 1024) return 0;
    if ((!memcmp(b, "PSID", 4) || !memcmp(b, "RSID", 4))) return 0;
    memcpy(id->buf, b, n < SEARCH_LEN ? n : SEARCH_LEN);
    /* SC68 support is handled in a converter, so ignore these modules here */
    if (id->buf[0] == 0x53 && id->buf[1] == 0x43 && id->buf[2] == 0x36 && id->buf[3] == 0x38) return 0;
    if (!extract_info_from_init_function(id)) return 0;
    if (!extract_info_from_play_function(id)) return 0;
    return 1;
}

int sd1_probe(const uint8_t *b, size_t n) {
    Ident *id = (Ident *)malloc(sizeof *id);
    if (!id) return 0;
    int r = test_module(id, b, n);
    free(id);
    return r;
}

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
static int rd_seek(Rd *r, long long pos) { if (pos < 0) return 0; r->pos = pos; return 1; }   /* a negative Seek throws */
static uint8_t rd_u8(Rd *r) { uint8_t x; rd_read(r, &x, 1); return x; }
static uint32_t rd_b32(Rd *r) { uint8_t x[4]; rd_read(r, x, 4); return (uint32_t)x[0] << 24 | (uint32_t)x[1] << 16 | (uint32_t)x[2] << 8 | x[3]; }
static long long rd_left(const Rd *r) { return r->pos >= (long long)r->n ? 0 : (long long)r->n - r->pos; }

/* ---------------------------------------------------------------- Load */
void sd1_free(Sd1Song *s) {
    if (!s) return;
    for (int i = 0; i < 4; i++) free(s->seq[i]);
    if (s->tracks) for (int i = 0; i < s->ntracks; i++) free(s->tracks[i].rows);
    free(s->tracks); free(s->inst); free(s->wave_info); free(s->waves);
    if (s->samples) for (int i = 0; i < s->nsamples; i++) free(s->samples[i].data);
    free(s->samples);
    free(s);
}

static int cmp_ll(const void *a, const void *b) {
    long long x = *(const long long *)a, y = *(const long long *)b;
    return x < y ? -1 : x > y;
}

/* offsets: Read_B_INT32() + baseOffset (C# int arithmetic, kept in 64 bits: a wrapped value fails later anyway) */
static int read_offset(Rd *r, int at, int base, int *out) {
    if (!rd_seek(r, at)) return 0;
    *out = (int)(uint32_t)((uint32_t)rd_b32(r) + (uint32_t)base);
    return 1;
}

typedef struct { long long tracks, instruments, waveform_list, waveforms; } Counts;

static int find_counts(Ident *id, Rd *r, Counts *c) {
    for (int i = 0; i < 4; i++) if (!read_offset(r, id->sequence_offsets[i], id->base_offset, &id->sequence_offsets[i])) return 0;
    if (!read_offset(r, id->track_data_offset, id->base_offset, &id->track_data_offset)) return 0;
    if (!read_offset(r, id->track_offset_table_offset, id->base_offset, &id->track_offset_table_offset)) return 0;
    if (!read_offset(r, id->instrument_offset, id->base_offset, &id->instrument_offset)) return 0;
    if (id->waveform_list_offset != 0 && !read_offset(r, id->waveform_list_offset, id->base_offset, &id->waveform_list_offset)) return 0;
    if (!read_offset(r, id->waveform_offset, id->base_offset, &id->waveform_offset)) return 0;
    if (id->sample_offset != 0 && !read_offset(r, id->sample_offset, id->base_offset, &id->sample_offset)) return 0;
    if (!read_offset(r, id->song_data_offset, id->base_offset, &id->song_data_offset)) return 0;

    long long sorted[11] = {
        id->sequence_offsets[0], id->sequence_offsets[1], id->sequence_offsets[2], id->sequence_offsets[3],
        id->track_data_offset, id->track_offset_table_offset, id->instrument_offset,
        id->waveform_list_offset, id->waveform_offset, id->sample_offset, id->song_data_offset
    };
    qsort(sorted, 11, sizeof sorted[0], cmp_ll);
    int index;
    for (index = 0; index < 11 && sorted[index] != id->track_offset_table_offset; index++) ;
    if (index == 10) {
        /* last block of data, so count the track offsets by looking at them */
        c->tracks = 1;                                                           /* skip the first one, it is always 0 */
        if (!rd_seek(r, (long long)id->track_offset_table_offset + 4)) return 0;
        do {
            uint32_t offset = rd_b32(r);
            if (offset == 0 || offset > r->n) break;
            c->tracks++;
        } while (!r->eof);
    } else c->tracks = (sorted[index + 1] - sorted[index]) / 4;

    for (index = 0; index < 11 && sorted[index] != id->instrument_offset; index++) ;
    if (index + 1 >= 11) return 0;                                               /* sortedOffsets[index + 1] throws */
    c->instruments = (sorted[index + 1] - sorted[index]) / 32;
    if (c->instruments > 63) c->instruments = 63;

    for (index = 0; index < 11 && sorted[index] != id->waveform_list_offset; index++) ;
    if (index + 1 >= 11) return 0;
    c->waveform_list = (sorted[index + 1] - sorted[index]) / 16;

    for (index = 0; index < 11 && sorted[index] != id->waveform_offset; index++) ;
    if (index + 1 >= 11) return 0;
    c->waveforms = (sorted[index + 1] - sorted[index]) / 32;
    return 1;
}

static int load_song_data(Sd1Song *s, Ident *id, Rd *r, uint32_t *number_of_positions) {
    if (!rd_seek(r, id->song_data_offset)) return 0;
    s->mix1.source1 = rd_b32(r);
    s->mix2.source1 = rd_b32(r);
    s->mix1.source2 = rd_b32(r);
    s->mix2.source2 = rd_b32(r);
    s->mix1.destination = rd_b32(r);
    s->mix2.destination = rd_b32(r);
    s->start_rows = rd_b32(r);
    *number_of_positions = rd_b32(r) - 1;
    s->start_speed = rd_b32(r);
    s->mix1.speed = rd_b32(r);
    s->mix2.speed = rd_b32(r);
    return !r->eof;
}

static int load_instruments(Sd1Song *s, Ident *id, Rd *r, int number_of_instruments) {
    if (!rd_seek(r, id->instrument_offset)) return 0;
    int extra = id->has_special ? id->nspecial : 0;
    s->ninst = number_of_instruments + extra;
    s->inst = (Sd1Inst *)calloc((size_t)s->ninst + 1, sizeof(Sd1Inst));
    if (!s->inst) return 0;
    for (int i = 0; i < number_of_instruments; i++) {
        Sd1Inst *in = &s->inst[i];
        in->waveform_number = rd_b32(r);
        rd_read(r, in->arpeggio, 16);
        in->attack_speed = rd_u8(r);
        in->attack_max = rd_u8(r);
        in->decay_speed = rd_u8(r);
        in->decay_min = rd_u8(r);
        in->sustain_time = rd_u8(r);
        r->pos += 1;                                                             /* Seek(1, Current) */
        in->release_speed = rd_u8(r);
        in->release_min = rd_u8(r);
        in->phase_shift = rd_u8(r);
        in->phase_speed = rd_u8(r);
        in->fine_tune = rd_u8(r);
        in->pitch_fall = (int8_t)rd_u8(r);
        if (r->eof) return 0;
        /* with extra instruments the player version is known: some information has to move */
        if (extra != 0) { in->pitch_fall = (int8_t)in->fine_tune; in->fine_tune = 0; }
        else if (in->fine_tune > 6) in->fine_tune = 0;
    }
    for (int i = 0; i < extra; i++) {
        Sd1Inst *in = &s->inst[number_of_instruments + i];
        in->waveform_number = id->special[i].instrument_number - 60 + 16;
        in->volume = 64;
    }
    return 1;
}

static int load_tracks(Sd1Song *s, Ident *id, Rd *r, long long number_of_tracks) {
    if (number_of_tracks <= 0) return 0;                                         /* tracks[] empty: the player throws */
    if (!rd_seek(r, id->track_offset_table_offset)) return 0;
    if (number_of_tracks * 4 > rd_left(r)) return 0;                             /* would end in EndOfStream */
    int nt = (int)number_of_tracks;
    int32_t *table = (int32_t *)malloc(sizeof(int32_t) * (size_t)nt);
    if (!table) return 0;
    for (int i = 0; i < nt; i++) table[i] = (int32_t)rd_b32(r);
    if (r->eof) { free(table); return 0; }
    s->tracks = (Sd1Track *)calloc((size_t)nt, sizeof(Sd1Track));
    if (!s->tracks) { free(table); return 0; }
    s->ntracks = nt;
    int extra = id->has_special ? id->nspecial : 0;
    for (int i = 0; i < nt; i++) {
        /* LoadSingleTrack */
        if (!rd_seek(r, (long long)id->track_data_offset + table[i])) { free(table); return 0; }
        Sd1Row rows[64];
        int count = 0, row_line = 0;
        do {
            Sd1Row *row = &rows[count++];
            row->note = (int8_t)rd_u8(r);
            row->inst = rd_u8(r);
            row->fx = rd_u8(r);
            row->arg = rd_u8(r);
            row->dur = rd_u8(r);
            if (id->has_special && row->inst >= 60) row->inst = (uint8_t)(s->ninst - extra + (row->inst - 60));
            row_line += row->dur + 1;
        } while (row_line < 64);
        s->tracks[i].rows = (Sd1Row *)malloc(sizeof(Sd1Row) * (size_t)count);
        if (!s->tracks[i].rows) { free(table); return 0; }
        memcpy(s->tracks[i].rows, rows, sizeof(Sd1Row) * (size_t)count);
        s->tracks[i].nrows = count;
        if (r->eof) { free(table); return 0; }
    }
    free(table);
    return 1;
}

static int load_sequences(Sd1Song *s, Ident *id, Rd *r, uint32_t number_of_positions) {
    if (number_of_positions == 0) return 0;                                      /* sequences[i][0] throws in InitializeSound */
    if ((long long)number_of_positions * 6 > (long long)r->n) return 0;          /* would end in EndOfStream (or OutOfMemory) */
    s->npos = (int)number_of_positions;
    for (int i = 0; i < 4; i++) {
        if (!rd_seek(r, id->sequence_offsets[i])) return 0;
        s->seq[i] = (Sd1Seq *)calloc((size_t)s->npos, sizeof(Sd1Seq));
        if (!s->seq[i]) return 0;
        for (int j = 0; j < s->npos; j++) {
            s->seq[i][j].track = rd_b32(r);
            r->pos += 1;
            s->seq[i][j].transpose = (int8_t)rd_u8(r);
            if (r->eof) return 0;
        }
    }
    return 1;
}

static int load_waveforms(Sd1Song *s, Ident *id, Rd *r, long long number_of_waveform_list, long long number_of_waveforms) {
    if (id->waveform_list_offset == 0) {
        /* no waveform offset list in the module, so create one since it is needed */
        s->nwave_info = s->ninst;
        s->wave_info = (uint8_t (*)[16])calloc((size_t)s->nwave_info + 1, 16);
        if (!s->wave_info) return 0;
        for (int i = 1; i <= s->nwave_info; i++) {
            s->wave_info[i - 1][0] = (uint8_t)i; s->wave_info[i - 1][1] = 0x01;
            s->wave_info[i - 1][2] = 0xff; s->wave_info[i - 1][3] = 0x10;
        }
    } else {
        if (!rd_seek(r, id->waveform_list_offset)) return 0;
        if (number_of_waveform_list * 16 > rd_left(r)) return 0;                 /* a short read fails */
        s->nwave_info = (int)number_of_waveform_list;
        s->wave_info = (uint8_t (*)[16])calloc((size_t)s->nwave_info + 1, 16);
        if (!s->wave_info) return 0;
        for (int i = 0; i < s->nwave_info; i++) if (rd_read(r, s->wave_info[i], 16) < 16) return 0;
    }
    if (!rd_seek(r, id->waveform_offset)) return 0;
    if (number_of_waveforms * 32 > rd_left(r)) return 0;                         /* would end in EndOfStream */
    s->nwaves = (int)number_of_waveforms;
    s->waves = (int8_t *)calloc((size_t)s->nwaves * 32 + 1, 1);
    if (!s->waves) return 0;
    for (int i = 0; i < s->nwaves; i++) {
        rd_read(r, (uint8_t *)s->waves + (size_t)i * 32, 32);
        if (r->eof) return 0;
    }
    return 1;
}

/* ReadSampleData(length): zero padded when the file ends; the C# fails when more than 32 bytes are missing */
static int read_sample(Rd *r, long long pos, long long length, Sd1Sample *smp) {
    if (length < 0) return 0;                                                    /* new sbyte[negative] throws */
    if (!rd_seek(r, pos)) return 0;
    if (length > rd_left(r) + 32) return 0;
    smp->data = (int8_t *)calloc((size_t)length + 1, 1);
    if (!smp->data) return 0;
    smp->len = (uint32_t)length;
    rd_read(r, (uint8_t *)smp->data, (size_t)length);
    return 1;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static int load_samples(Sd1Song *s, Ident *id, Rd *r) {
    /* orderedWaveforms: the distinct sample numbers used by the instruments, ascending */
    uint32_t *ordered = (uint32_t *)malloc(sizeof(uint32_t) * ((size_t)s->ninst + 1));
    if (!ordered) return 0;
    int nord = 0;
    for (int i = 0; i < s->ninst; i++) if (s->inst[i].waveform_number >= 16) ordered[nord++] = s->inst[i].waveform_number - 16;
    qsort(ordered, (size_t)nord, sizeof(uint32_t), cmp_u32);
    { int k = 0; for (int i = 0; i < nord; i++) if (k == 0 || ordered[k - 1] != ordered[i]) ordered[k++] = ordered[i]; nord = k; }

    int ok = 0;
    if (id->sample_offset == 0) {
        if (!id->has_special) goto done;                                         /* specialSamples null: throws */
        s->nsamples = id->nspecial;
        s->samples = (Sd1Sample *)calloc((size_t)s->nsamples + 1, sizeof(Sd1Sample));
        if (!s->samples) goto done;
        for (int i = 0; i < id->nspecial; i++) {
            s->samples[i].loop_start = -1;
            if (!read_sample(r, id->special[i].sample_offset, id->special[i].length, &s->samples[i])) goto done;
        }
    } else {
        if (!rd_seek(r, id->sample_offset)) goto done;
        uint32_t sample_base_offset = rd_b32(r);
        long long sample_info_offset = r->pos;
        long long first_sample_position = sample_base_offset != 0 ? (long long)sample_base_offset : (long long)UINT32_MAX;
        /* sample numbers whose info lies past the file end fail (or stay empty): keeps the list small */
        if (nord > 0 && (long long)ordered[nord - 1] * 32 > (long long)r->n + 32 * 64) goto done;
        s->nsamples = nord > 0 ? (int)ordered[nord - 1] + 1 : 0;
        s->samples = (Sd1Sample *)calloc((size_t)s->nsamples + 1, sizeof(Sd1Sample));
        if (!s->samples) goto done;
        for (int sample_number = 0, ordered_index = 0; ordered_index < nord; sample_number++) {
            Sd1Sample *smp = &s->samples[sample_number];
            smp->loop_start = 0;
            if ((uint32_t)sample_number == ordered[ordered_index]) {
                long long new_position = (uint32_t)(ordered[ordered_index] * 32u);
                if (new_position < first_sample_position) {
                    r->pos = sample_info_offset + new_position;
                    uint32_t start_offset = rd_b32(r);
                    int32_t loop_start = (int32_t)rd_b32(r);
                    uint32_t end_offset = rd_b32(r);
                    uint8_t name[20];
                    if (rd_read(r, name, 20) < 20) goto done;
                    if ((long long)loop_start == (long long)id->no_loop_value || (long long)loop_start >= (long long)end_offset ||
                        (long long)start_offset > (long long)loop_start)
                        loop_start = -1;
                    else loop_start = (int32_t)((uint32_t)loop_start - start_offset);
                    int32_t length = (int32_t)(end_offset - start_offset);
                    smp->loop_start = loop_start;
                    long long sample_position = sample_info_offset + (long long)sample_base_offset + (long long)start_offset;
                    if (sample_position < first_sample_position) first_sample_position = sample_position;
                    if (!read_sample(r, sample_position, length, smp)) goto done;
                }
                ordered_index++;
            }
        }
    }
    ok = 1;
done:
    free(ordered);
    return ok;
}

static int load(Sd1Song *s, Ident *id, const uint8_t *b, size_t n) {
    Rd r = { b, n, 0, 0 };
    Counts c;
    uint32_t number_of_positions;
    if (!find_counts(id, &r, &c)) return 2;
    r.eof = 0;
    if (!load_song_data(s, id, &r, &number_of_positions)) return 3;
    if (c.instruments < 0) return 4;
    if (!load_instruments(s, id, &r, (int)c.instruments)) return 4;
    if (!load_tracks(s, id, &r, c.tracks)) return 5;
    if (!load_sequences(s, id, &r, number_of_positions)) return 6;
    if (!load_waveforms(s, id, &r, c.waveform_list, c.waveforms)) return 7;
    if (!load_samples(s, id, &r)) return 8;
    s->period_version = id->period_version;
    s->fine_tune_multiply = id->fine_tune_multiply;
    if (id->period_version == 1) { s->periods = periods1; s->nperiods = (int)(sizeof periods1 / sizeof periods1[0]); }
    else if (id->period_version == 2) { s->periods = periods2; s->nperiods = (int)(sizeof periods2 / sizeof periods2[0]); }
    else { s->periods = periods3; s->nperiods = (int)(sizeof periods3 / sizeof periods3[0]); }
    s->enable_filter = id->enable_filter;
    s->enable_mixing = id->enable_mixing;
    s->nsub = 1;
    return 0;
}

Sd1Song *sd1_load(const uint8_t *b, size_t n, int *err) {
    if (err) *err = 0;
    Ident *id = (Ident *)malloc(sizeof *id);
    if (!id) { if (err) *err = 9; return NULL; }
    if (!test_module(id, b, n)) { free(id); if (err) *err = 1; return NULL; }
    Sd1Song *s = (Sd1Song *)calloc(1, sizeof *s);
    if (!s) { free(id); if (err) *err = 9; return NULL; }
    int e = load(s, id, b, n);
    free(id);
    if (e) { sd1_free(s); if (err) *err = e; return NULL; }
    return s;
}

/* ---------------------------------------------------------------- mixer channel (IChannel) */
static void ch_play_sample(Sd1Mix *m, const int8_t *data, uint32_t len) {   /* PlaySample(data, 0, len): starts now, no loop */
    m->ld = NULL; m->llen = 0; m->pos = 0;
    m->d = data; m->len = data ? len : 0; m->on = data && len > 0;
}
static void ch_set_loop(Sd1Mix *m, const int8_t *data, uint32_t data_len, uint32_t start, uint32_t len) {
    if (!data || start >= data_len) { m->ld = NULL; m->llen = 0; return; }
    if (len > data_len - start) len = data_len - start;
    m->ld = data + start; m->llen = len;
}
static void ch_mute(Sd1Mix *m) { m->on = 0; }

/* ---------------------------------------------------------------- helpers */
static Sd1Inst *voice_inst(Sd1Player *p, const Sd1Voice *v) { return v->current_instrument >= 0 ? &p->inst[v->current_instrument] : NULL; }

static uint16_t period_at(const Sd1Song *s, int index) {             /* C#: index >= Length gives 0; a negative one throws */
    return index < 0 || index >= s->nperiods ? 0 : s->periods[index];
}

static const Sd1Track *track_of(const Sd1Song *s, const Sd1Seq *seq) {
    return seq && seq->track < (uint32_t)s->ntracks ? &s->tracks[seq->track] : NULL;   /* tracks[] out of range throws */
}

/* ---------------------------------------------------------------- InitializeSound */
static void initialize_sound(Sd1Player *p) {
    const Sd1Song *s = p->s;
    p->number_of_rows = s->start_rows;
    p->speed = s->start_speed;
    p->speed_counter = s->start_speed;
    p->new_track = 0; p->loop_song = 0;
    p->current_row = -1;
    p->current_position = 1;
    memset(&p->mix1_play, 0, sizeof p->mix1_play);
    memset(&p->mix2_play, 0, sizeof p->mix2_play);
    for (int i = 0; i < 4; i++) {
        Sd1Voice *v = &p->v[i];
        memset(v, 0, sizeof *v);
        v->note_period = 0x9999;
        v->envelope_in_progress = ENV_DONE;
        v->current_instrument = -1;
        v->current_sequence = &s->seq[i][0];
        v->current_track = track_of(s, v->current_sequence);
        int instr_number = v->current_track && v->current_track->nrows > 0 ? v->current_track->rows[0].inst - 1 : -1;
        if (instr_number >= 0 && instr_number < s->ninst) { v->current_instrument = instr_number; v->instrument_number = instr_number; }
    }
    memcpy(p->inst, s->inst, sizeof(Sd1Inst) * (size_t)s->ninst);
    if (s->nwaves > 0) memcpy(p->waves, s->waves, (size_t)s->nwaves * 32);
    p->end_reached = 0;
    p->shown_pos = 0; p->shown_row = 0;
}

/* ---------------------------------------------------------------- envelope, arpeggio, bending, phase shift */
static int process_envelope(Sd1Voice *v, const Sd1Inst *in) {
    if (in->volume != 0) return in->volume * 4;
    int volume = 0;
    switch (v->envelope_in_progress) {
        case ENV_ATTACK:
            volume = v->volume + in->attack_speed;
            if (volume > in->attack_max) { volume = in->attack_max; v->envelope_in_progress = ENV_DECAY; }
            v->volume = (uint8_t)volume;
            break;
        case ENV_DECAY:
            volume = v->volume - in->decay_speed;
            if (volume <= in->decay_min) { volume = in->decay_min; v->sustain_control = in->sustain_time; v->envelope_in_progress = ENV_SUSTAIN; }
            v->volume = (uint8_t)volume;
            break;
        case ENV_SUSTAIN:
            volume = v->volume;
            v->sustain_control--;
            if (v->sustain_control == 0) v->envelope_in_progress = ENV_RELEASE;
            break;
        case ENV_RELEASE:
            volume = v->volume - in->release_speed;
            if (volume <= in->release_min) { volume = in->release_min; v->envelope_in_progress = ENV_DONE; }
            v->volume = (uint8_t)volume;
            break;
    }
    return volume;
}

static uint16_t do_arpeggio(const Sd1Song *s, Sd1Voice *v, const Sd1Inst *in) {
    v->arpeggio_index = (uint8_t)((v->arpeggio_index + 1) & 0x0f);
    uint8_t arp = in->arpeggio[v->arpeggio_index];
    uint16_t period = period_at(s, v->note_offset + arp + in->fine_tune * s->fine_tune_multiply);
    v->note_period = period;
    return period;
}

static void do_bending(const Sd1Song *s, Sd1Voice *v, const Sd1Inst *in) {
    if (v->bend_speed != 0) {
        int16_t bend_speed = (int16_t)-v->bend_speed;
        int index = v->bend_to + in->fine_tune * s->fine_tune_multiply;
        if (index >= s->nperiods) { v->bend_speed = 0; return; }
        uint16_t bend_to = s->periods[index];
        v->pitch_control = (uint16_t)(v->pitch_control + bend_speed);
        v->note_period = (uint16_t)(v->note_period + v->pitch_control);
        if (bend_speed < 0 ? v->note_period < bend_to : v->note_period > bend_to) {
            v->note_offset = (int8_t)v->bend_to;
            v->note_period = bend_to;
            v->pitch_control = 0;
            v->bend_speed = 0;
        }
    }
}

static void do_phase_shift(Sd1Player *p, Sd1Voice *v, const Sd1Inst *in) {
    if (in->phase_shift != 0) {
        if (v->phase_speed == 0) {
            if (in->phase_shift - 1 < p->s->nwaves) {
                const int8_t *waveform = p->waves + (size_t)(in->phase_shift - 1) * 32;
                v->phase_index = (uint8_t)((v->phase_index + 1) & 0x1f);
                v->note_period = (uint16_t)(v->note_period + waveform[v->phase_index] / 4);
            }
        } else v->phase_speed--;
    }
}

/* ---------------------------------------------------------------- PlayNote */
static void play_note(Sd1Player *p, int ch) {
    const Sd1Song *s = p->s;
    Sd1Voice *v = &p->v[ch];
    Sd1Mix *channel = &p->m[ch];
    const int8_t *sample_address = NULL; uint32_t sample_length = 0;
    int loop_start = -1;
    int sample_address_set = 0;
    uint32_t period = 0;
    Sd1Inst *instr;

    if (p->speed_counter == 0) {
        if (p->new_track) {
            v->row_count = 0;
            v->sequence_index++;
            if (p->loop_song) v->sequence_index = 0;
            if (v->sequence_index >= s->npos) v->sequence_index = 0;              /* cannot happen: positions and voices move together */
            v->current_sequence = &s->seq[ch][v->sequence_index];
            v->current_track = track_of(s, v->current_sequence);
            v->row_index = 0;
        }

        if (v->row_count == 0) {
            static const Sd1Row empty_row = { 0, 0, 0, 0, 0 };
            /* Rows[RowIndex] past the track (or a track number out of range) throws in C#: read as an empty row here */
            const Sd1Row *row = v->current_track && v->row_index < v->current_track->nrows ? &v->current_track->rows[v->row_index] : &empty_row;

#define PLAY_SAMPLE(sample_number) do {                                                       \
                if (v->loop_control) ch_mute(channel);                                        \
                uint32_t sn_ = (uint32_t)(sample_number) - 16;                                \
                if (sn_ < (uint32_t)s->nsamples) {                                             \
                    const Sd1Sample *smp_ = &s->samples[sn_];                                  \
                    sample_address = smp_->data; sample_length = smp_->len;                   \
                    loop_start = smp_->loop_start;                                            \
                } else { sample_address = NULL; sample_length = 0; loop_start = -1; }         \
                sample_address_set = 1;                                                       \
                v->loop_control = 1;                                                          \
            } while (0)

            if (row->inst != 0) {
                if (row->inst - 1 >= s->ninst) goto row_done;                   /* instruments[] out of range throws: row ignored */
                instr = &p->inst[row->inst - 1];
                if (v->loop_control) { ch_mute(channel); v->loop_control = 0; }
                uint32_t waveform_number = instr->waveform_number;
                if (waveform_number >= 16) PLAY_SAMPLE(waveform_number);
                v->wave_index = 0;
                v->waveform_number = (uint8_t)waveform_number;
                if (!sample_address_set && waveform_number != 0) {
                    if (v->waveform_number >= 1 && v->waveform_number - 1 < s->nwave_info) {
                        const uint8_t *wave_info = s->wave_info[v->waveform_number - 1];
                        v->wave_speed = wave_info[1];
                        if (wave_info[0] >= 1 && wave_info[0] - 1 < s->nwaves) {
                            sample_address = p->waves + (size_t)(wave_info[0] - 1) * 32; sample_length = 32;
                        }
                        loop_start = 0;
                    }
                }
                v->current_instrument = row->inst - 1;
                v->instrument_number = row->inst - 1;
                v->envelope_in_progress = ENV_ATTACK;
                v->pitch_fall_control = 0;
                v->pitch_control = 0;
                v->row_count = row->dur;
            } else if (row->note != 0) {
                v->row_count = row->dur;
                if (v->loop_control && row->note != -1) PLAY_SAMPLE(v->waveform_number);
            }
#undef PLAY_SAMPLE

            int8_t note = row->note;
            if (note != 0) {
                v->row_count = row->dur;
                instr = voice_inst(p, v);
                if (note != -1 && instr) {                                       /* CurrentInstrument null throws: ignored */
                    int8_t transpose = v->current_sequence->transpose;
                    note = (int8_t)(note + transpose);
                    period = period_at(s, note + instr->fine_tune * s->fine_tune_multiply + 1);
                    v->note_period = (uint16_t)period;
                    v->note_offset = note;
                    v->envelope_in_progress = ENV_ATTACK;
                    v->volume = 0;
                    v->sustain_control = 0;
                    v->pitch_control = 0;
                    v->pitch_fall_control = 0;
                    v->bend_speed = 0;
                    v->phase_speed = instr->phase_speed;
                    switch (row->fx) {
                        case 0:
                            if (row->arg != 0) { instr->attack_max = row->arg; instr->attack_speed = row->arg; v->wave_speed = 0; }
                            break;
                        case 2: p->speed = row->arg; v->wave_speed = 0; break;
                        case 3: p->number_of_rows = row->arg; v->wave_speed = 0; break;
                        default: v->bend_to = (uint8_t)(row->fx + transpose); v->bend_speed = (int8_t)row->arg; break;
                    }
                }
            }
        row_done:
            v->row_index++;
        } else v->row_count--;
    }

    instr = voice_inst(p, v);
    if (instr && v->waveform_number > 0) {
        int volume = process_envelope(v, instr);
        period = do_arpeggio(s, v, instr);
        do_bending(s, v, instr);
        do_phase_shift(p, v, instr);
        v->pitch_fall_control = (uint16_t)(v->pitch_fall_control - instr->pitch_fall);
        v->note_period = (uint16_t)(v->note_period + v->pitch_fall_control);

        if (!v->loop_control) {
            if (v->wave_speed == 0) {
                /* waveformInfo[] / waveInfo[] / waveforms[] out of range throw in C#: nothing happens here */
                if (v->wave_index < 16 && v->waveform_number - 1 < s->nwave_info) {
                    const uint8_t *wave_info = s->wave_info[v->waveform_number - 1];
                    if (wave_info[v->wave_index] == 0xff) v->wave_index = (uint8_t)(wave_info[v->wave_index + 1] & 0xfe);
                    else {
                        if (wave_info[v->wave_index] >= 1 && wave_info[v->wave_index] - 1 < s->nwaves) {
                            sample_address = p->waves + (size_t)(wave_info[v->wave_index] - 1) * 32; sample_length = 32;
                        }
                        loop_start = 0;
                        v->wave_speed = wave_info[v->wave_index + 1];
                        v->wave_index += 2;
                    }
                }
            } else v->wave_speed--;
        }

        if (period != 0 && v->note_period != 0) channel->period = v->note_period;    /* SetAmigaPeriod (0 is ignored) */
        if (volume != 0) channel->volume = volume > 256 ? 256 : volume;             /* SetVolume */
        if (sample_address != NULL) {
            ch_play_sample(channel, sample_address, sample_length);
            if (loop_start != -1) ch_set_loop(channel, sample_address, sample_length, (uint32_t)loop_start, sample_length - (uint32_t)loop_start);
        }
    }
}

/* ---------------------------------------------------------------- waveform mixing and filter effect */
static void do_waveform_mixing(Sd1Player *p, const Sd1MixInfo *mix_info, Sd1MixPlay *mix_play) {
    const Sd1Song *s = p->s;
    if (s->enable_mixing && mix_info->speed != 0) {
        if (mix_play->counter == 0) {
            mix_play->counter = mix_info->speed;
            mix_play->position = (mix_play->position + 1) & 0x1f;
            /* a source of 0 gives waveforms[-1] in C# (throws): skipped */
            if (mix_info->source1 >= 1 && mix_info->source2 >= 1 && mix_info->destination >= 1 &&
                mix_info->source1 <= (uint32_t)s->nwaves && mix_info->source2 <= (uint32_t)s->nwaves && mix_info->destination <= (uint32_t)s->nwaves) {
                const int8_t *source1 = p->waves + (size_t)(mix_info->source1 - 1) * 32;
                const int8_t *source2 = p->waves + (size_t)(mix_info->source2 - 1) * 32;
                int8_t *dest = p->waves + (size_t)(mix_info->destination - 1) * 32;
                uint32_t pos = mix_play->position;
                for (int i = 31; i >= 0; i--) {
                    dest[i] = (int8_t)((source1[i] + source2[pos]) / 2);
                    pos = (pos - 1) & 0x1f;
                }
            }
        }
        mix_play->counter--;
    }
}

static void do_filter_effect(Sd1Player *p) {
    if (p->s->enable_filter && p->s->nwaves > 0) {
        int8_t *w = &p->waves[p->mix1_play.position & 0x1f];
        *w = (int8_t)-*w;
    }
}

/* ---------------------------------------------------------------- Play() */
static void play_tick(Sd1Player *p) {
    const Sd1Song *s = p->s;
    int row_tick = p->speed_counter == 0;
    for (int i = 0; i < 4; i++) play_note(p, i);
    if (row_tick) { p->shown_pos = p->v[0].sequence_index; p->shown_row = p->current_row; }

    p->speed_counter++;
    if (p->speed_counter > p->speed) p->speed_counter = 0;

    p->new_track = 0;
    p->loop_song = 0;

    if (p->speed_counter == 0) {
        p->current_row++;
        if ((long long)p->current_row == (long long)p->number_of_rows) {
            p->current_row = 0;
            p->new_track = 1;
            p->current_position++;
            if ((long long)p->current_position > (long long)s->npos) {
                p->current_position = 1;
                p->loop_song = 1;
                p->end_reached = 1;
            }
        }
    }

    do_waveform_mixing(p, &s->mix1, &p->mix1_play);
    do_waveform_mixing(p, &s->mix2, &p->mix2_play);
    do_filter_effect(p);

    if (p->end_reached) { p->ended = 1; p->end_reached = 0; }            /* OnEndReached: the song is over (no restart) */
}

/* ---------------------------------------------------------------- public */
Sd1Player *sd1_new(const Sd1Song *s, int sub, int rate) {
    if (!s || rate <= 0 || sub < 0 || sub >= s->nsub) return NULL;
    Sd1Player *p = (Sd1Player *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->s = s; p->rate = rate; p->sub = sub;
    p->inst = (Sd1Inst *)calloc((size_t)s->ninst + 1, sizeof(Sd1Inst));
    p->waves = (int8_t *)calloc((size_t)s->nwaves * 32 + 1, 1);
    if (!p->inst || !p->waves) { sd1_player_free(p); return NULL; }
    initialize_sound(p);
    return p;
}

void sd1_player_free(Sd1Player *p) {
    if (!p) return;
    free(p->inst); free(p->waves); free(p);
}

static inline void mix_advance(Sd1Mix *m) {                          /* end of a pass: the loop, else stop */
    while (m->on && m->pos >= m->len) {
        if (m->llen > 0 && m->ld) { m->pos -= m->len; m->d = m->ld; m->len = m->llen; }
        else { m->on = 0; break; }
        if (m->pos < 0) m->pos = 0;
    }
}

static inline double mix_step(const Sd1Player *p, const Sd1Mix *m) {
    double per = m->period ? m->period : 428;
    return PAULA_PAL / (per * p->rate);
}

int sd1_render(Sd1Player *p, int16_t *out, int n) {
    if (!p || p->ended) return 0;
    for (int f = 0; f < n; ) {
        if (p->tick_left <= 0) {
            play_tick(p);
            if (p->ended) { memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(n - f)); return f; }
            p->tick_left += p->rate / SD1_TICK_HZ;
        }
        if (p->fast) {                                                   /* seeking: only move the sample positions on */
            int k = (int)ceil(p->tick_left);
            if (k < 1) k = 1;
            if (k > n - f) k = n - f;
            for (int i = 0; i < 4; i++) {
                Sd1Mix *m = &p->m[i];
                mix_advance(m);
                if (!m->on || !m->d || m->len == 0) continue;
                m->pos += mix_step(p, m) * k;
                if (m->llen > 0 && m->ld && m->pos >= m->len) {              /* skip whole loop passes at once */
                    double over = m->pos - m->len;
                    m->d = m->ld; m->len = m->llen;
                    m->pos = fmod(over, (double)m->llen);
                }
                mix_advance(m);
            }
            memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)k);
            p->tick_left -= k;
            f += k;
            continue;
        }
        p->tick_left -= 1.0;
        float l = 0, rr = 0;
        for (int i = 0; i < 4; i++) {
            Sd1Mix *m = &p->m[i];
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
            float v = smp * (float)m->volume * (10.0f / 28.0f);            /* volume 0..256 = 0..64 * 4, then the 10/7 level */
            if (p->sc) p->sc[si] = smp * (float)m->volume / (127.0f * 256.0f);
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

const char *sd1_title(const Sd1Song *s) { (void)s; return ""; }         /* the format has no title */
int sd1_subsongs(const Sd1Song *s) { return s ? s->nsub : 0; }
int sd1_channels(const Sd1Song *s) { (void)s; return 4; }
int sd1_positions(const Sd1Song *s, int sub) {
    if (!s || sub < 0 || sub >= s->nsub) return 0;
    return s->npos;
}

void sd1_pos(const Sd1Player *p, int *position, int *row) {
    if (position) *position = p ? p->shown_pos : 0;
    if (row) *row = p ? p->shown_row : 0;
}

/* rows of a position: NumberOfRows at the start (each row = Speed + 1 ticks); effect 3 can change it while playing */
int sd1_rows(const Sd1Song *s, int position) {
    (void)position;
    if (!s) return 0;
    if (s->start_rows == 0) return 1;
    return s->start_rows > 256 ? 256 : (int)s->start_rows;
}

int sd1_cell(const Sd1Song *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg) {
    if (!s || sub < 0 || sub >= s->nsub || position < 0 || position >= s->npos || row < 0 || row >= sd1_rows(s, position) || ch < 0 || ch >= 4) return 0;
    const Sd1Seq *seq = &s->seq[ch][position];
    const Sd1Track *t = track_of(s, seq);
    if (!t) return 0;
    /* as PlayNote walks the track: a row lasts Duration + 1 rows, a row without note and instrument only one */
    const Sd1Row *r = NULL;
    for (int k = 0, line = 0; k < t->nrows && line <= row; k++) {
        const Sd1Row *x = &t->rows[k];
        if (line == row) { r = x; break; }
        line += (x->note == 0 && x->inst == 0) ? 1 : x->dur + 1;
    }
    if (!r || (r->note == 0 && r->inst == 0)) return 0;
    int nt = 0, f = 0, a = 0;
    if (r->note != 0 && r->note != -1) {
        int8_t n8 = (int8_t)(r->note + seq->transpose);
        /* period index n8 + 1; 1712 (C-0 for the view, note 1) is index 25 in the version 3 table, 22 in the others */
        nt = n8 + 1 - (s->period_version == 3 ? 25 : 22) + 1;
        if (nt < 1) nt = 1;
        f = r->fx; a = r->arg;                                           /* effects only work together with a note */
    }
    if (note) *note = nt;
    if (inst) *inst = r->inst;
    if (fx) *fx = f;
    if (arg) *arg = a;
    return 1;
}

void sd1_set_mute(Sd1Player *p, unsigned mask) { if (p) p->mute = mask; }
void sd1_set_scope(Sd1Player *p, float *buf, int len) {
    if (!p) return;
    if (!buf || len <= 0 || (len & (len - 1))) { p->sc = NULL; p->sc_len = 0; return; }
    p->sc = buf; p->sc_len = len;
}
unsigned sd1_scope_pos(const Sd1Player *p) { return p ? p->sc_pos : 0; }
void sd1_set_fast(Sd1Player *p, int on) { if (p) p->fast = on != 0; }
