/* Adlib Tracker 2 (AT2) modules, .a2m / .a2t: the song data as loaded from the file. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {            /* one FM instrument as stored: modulator / carrier registers and the connection */
    uint8_t multipM, ksrM, sustM, vibrM, tremM, multipC, ksrC, sustC, vibrC, tremC;
    uint8_t volM, kslM, volC, kslC, decM, attckM, decC, attckC, relM, sustnM, relC, sustnC;
    uint8_t wformM, wformC, connect, feedb;
} A2mFm;

typedef struct { A2mFm fm; uint8_t panning; int8_t fine_tune; uint8_t perc_voice; } A2mInst;

typedef struct { A2mFm fm; int16_t freq_slide; uint8_t panning, duration, macro_flags; } A2mRegDef;     /* one step of an instrument macro */
typedef struct { uint8_t length, loop_begin, loop_length, keyoff_pos, arpeggio_table, vibrato_table; A2mRegDef data[255]; } A2mFmRegTable;
typedef struct { uint8_t length, speed, loop_begin, loop_length, keyoff_pos, data[255]; } A2mArpTable;
typedef struct { uint8_t length, speed, delay, loop_begin, loop_length, keyoff_pos; int8_t data[255]; } A2mVibTable;

typedef struct { uint8_t note, instr; struct { uint8_t def, val; } eff[2]; } A2mEvent;

typedef struct A2mSong {
    int ffver, is_tiny;                      /* file format version 1..14, A2T */
    char title[43], composer[43];
    int ninst; char instr_names[255][43];
    A2mInst inst[255];
    A2mFmRegTable *fmreg;                    /* [ninst] macro tables (NULL if none) */
    A2mArpTable *arp; A2mVibTable *vib;      /* [255] arpeggio / vibrato tables */
    uint8_t order[128];
    int tempo, speed, common_flag, patt_len, nm_tracks, macro_speedup, flag_4op;
    uint8_t lock_flags[20];
    uint32_t dis_fmreg_cols[255];
    int bpm_rows_per_beat, bpm_tempo_finetune;
    int npatt;                               /* stored patterns */
    A2mEvent *ev;                            /* npatt * nm_tracks * patt_len, track-major: [(pat * nm_tracks + track) * patt_len + row] */
} A2mSong;

int a2m_probe(const uint8_t *buf, size_t len);                 /* 1 = Adlib Tracker 2 module (A2M or A2T) */
A2mSong *a2m_load(const uint8_t *buf, size_t len, int *err);   /* NULL on error, err < 0; versions 9-14 for now */
void a2m_free(A2mSong *s);
