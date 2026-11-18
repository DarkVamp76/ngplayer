/* Adlib Tracker 2 (AT2) modules, .a2m / .a2t: the song data as loaded from the file. The data types follow the original player's. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {            /* one FM instrument: modulator (M) and carrier (C) registers and the connection */
    uint8_t multipM: 4, ksrM: 1, sustM: 1, vibrM: 1, tremM: 1;
    uint8_t multipC: 4, ksrC: 1, sustC: 1, vibrC: 1, tremC: 1;
    uint8_t volM: 6, kslM: 2;
    uint8_t volC: 6, kslC: 2;
    uint8_t decM: 4, attckM: 4;
    uint8_t decC: 4, attckC: 4;
    uint8_t relM: 4, sustnM: 4;
    uint8_t relC: 4, sustnC: 4;
    uint8_t wformM: 3, : 5;
    uint8_t wformC: 3, : 5;
    uint8_t connect: 1, feedb: 3, : 4;
} tFM_INST_DATA;

typedef struct { tFM_INST_DATA fm; uint8_t panning; int8_t fine_tune; uint8_t perc_voice; } tINSTR_DATA;

typedef struct { uint8_t length, speed, loop_begin, loop_length, keyoff_pos, data[255]; } tARPEGGIO_TABLE;
typedef struct { uint8_t length, speed, delay, loop_begin, loop_length, keyoff_pos; int8_t data[255]; } tVIBRATO_TABLE;
typedef struct { tFM_INST_DATA fm; int16_t freq_slide; uint8_t panning, duration, macro_flags; } tREGISTER_TABLE_DEF;      /* one step of an instrument macro */
typedef struct { uint8_t length, loop_begin, loop_length, keyoff_pos, arpeggio_table, vibrato_table; tREGISTER_TABLE_DEF data[255]; } tFMREG_TABLE;
typedef struct { tINSTR_DATA instr_data; uint8_t vibrato, arpeggio; tFMREG_TABLE *fmreg; uint32_t dis_fmreg_cols; } tINSTR_DATA_EXT;

typedef struct {
    char songname[43], composer[43], instr_names[255][43];
    uint8_t pattern_order[0x80], tempo, speed, common_flag;
    uint16_t patt_len; uint8_t nm_tracks; uint16_t macro_speedup; uint8_t flag_4op, lock_flags[20], bpm_rows_per_beat; int bpm_tempo_finetune;
} tSONGINFO;

typedef struct { uint8_t note, instr_def; struct { uint8_t def, val; } eff[2]; } tADTRACK2_EVENT;
typedef struct { unsigned count; size_t size; tINSTR_DATA_EXT *instruments; } tINSTR_INFO;
typedef struct { int patterns, rows, channels; size_t size; tADTRACK2_EVENT *events; } tEVENTS_INFO;   /* event = pattern * (channels * rows) + channel * rows + row */

typedef struct A2mSong {
    int ffver, is_tiny;                      /* file format version 1..14, tiny module (.a2t) */
    tSONGINFO songinfo; tINSTR_INFO instrinfo; tEVENTS_INFO eventsinfo;
    tVIBRATO_TABLE **vibrato_table; tARPEGGIO_TABLE **arpeggio_table; unsigned arpvib_count;   /* [arpvib_count], entries without a table are NULL */
} A2mSong;

int a2m_probe(const uint8_t *buf, size_t len);                 /* 1 = Adlib Tracker 2 module (A2M or A2T) */
A2mSong *a2m_load(const uint8_t *buf, size_t len, int *err);   /* NULL on error, err < 0; file versions 1-14, A2M and A2T */
void a2m_free(A2mSong *s);
