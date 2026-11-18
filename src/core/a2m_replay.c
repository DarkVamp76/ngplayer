/* Adlib Tracker 2 replayer for the A2M/A2T formats, versions 9-14. Transliterated to C from the player core of AdPlug's a2m-v2.cpp (Adlib Tracker II Player by Dmitry Smagin,
 * originally by Stanislav Baranec; Copyright (C) 1999-2008 Simon Peter et al.; LGPL-2.1-or-later), which is a port of the FreePascal sources of Adlib Tracker II.
 * Changes: C instead of C++, the OPL chip is reached through two callbacks, the loader lives in a2m_load.c. This file stays LGPL-2.1-or-later. */
#include "a2m_replay.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define keyoff_flag         0x80
#define fixed_note_flag     0x90
#define pattern_loop_flag   0xe0
#define pattern_break_flag  0xf0

typedef enum {
    isPlaying = 0, isPaused, isStopped
} tPLAY_STATUS;

#define BYTE_NULL (uint8_t)(0xFFFFFFFF)

#define MIN_IRQ_FREQ        50
#define MAX_IRQ_FREQ        1000

#define ef_Arpeggio            0x80 // ef_fix1 replacement to distinguish from "no effect" (was 0)
#define ef_FSlideUp            1
#define ef_FSlideDown          2
#define ef_TonePortamento      3
#define ef_Vibrato             4
#define ef_TPortamVolSlide     5
#define ef_VibratoVolSlide     6
#define ef_FSlideUpFine        7
#define ef_FSlideDownFine      8
#define ef_SetModulatorVol     9
#define ef_VolSlide            10 // A
#define ef_PositionJump        11 // B
#define ef_SetInsVolume        12 // C
#define ef_PatternBreak        13 // D
#define ef_SetTempo            14 // E
#define ef_SetSpeed            15 // F
#define ef_TPortamVSlideFine   16 // G
#define ef_VibratoVSlideFine   17 // H
#define ef_SetCarrierVol       18 // I
#define ef_SetWaveform         19 // J
#define ef_VolSlideFine        20 // K
#define ef_RetrigNote          21 // L
#define ef_Tremolo             22 // M
#define ef_Tremor              23 // N
#define ef_ArpggVSlide         24 // O
#define ef_ArpggVSlideFine     25 // P
#define ef_MultiRetrigNote     26 // Q
#define ef_FSlideUpVSlide      27 // R
#define ef_FSlideDownVSlide    28 // S
#define ef_FSlUpFineVSlide     29 // T
#define ef_FSlDownFineVSlide   30 // U
#define ef_FSlUpVSlF           31 // V
#define ef_FSlDownVSlF         32 // W
#define ef_FSlUpFineVSlF       33 // X
#define ef_FSlDownFineVSlF     34 // Y
#define ef_Extended            35 // Z
#define ef_Extended2           36 // &
#define ef_SetGlobalVolume     37 // %
#define ef_SwapArpeggio        38 // !
#define ef_SwapVibrato         39 // @
#define ef_ForceInsVolume      40 // =
#define ef_Extended3           41 // #
#define ef_ExtraFineArpeggio   42 // $
#define ef_ExtraFineVibrato    43 // ~
#define ef_ExtraFineTremolo    44 // ^
#define ef_SetCustomSpeedTab   45 // `
#define ef_GlobalFSlideUp      46 // >
#define ef_GlobalFSlideDown    47 // <
#define ef_GlobalFreqSlideUpXF 48 // ef_fix2 replacement for >xx + ZFE
#define ef_GlobalFreqSlideDnXF 49 // ef_fix2 replacement for <xx + ZFE

#define ef_ex_SetTremDepth     0
#define ef_ex_SetVibDepth      1
#define ef_ex_SetAttckRateM    2
#define ef_ex_SetDecayRateM    3
#define ef_ex_SetSustnLevelM   4
#define ef_ex_SetRelRateM      5
#define ef_ex_SetAttckRateC    6
#define ef_ex_SetDecayRateC    7
#define ef_ex_SetSustnLevelC   8
#define ef_ex_SetRelRateC      9
#define ef_ex_SetFeedback      10
#define ef_ex_SetPanningPos    11
#define ef_ex_PatternLoop      12
#define ef_ex_PatternLoopRec   13
#define ef_ex_ExtendedCmd      14
#define ef_ex_cmd_MKOffLoopDi  0
#define ef_ex_cmd_MKOffLoopEn  1
#define ef_ex_cmd_TPortaFKdis  2
#define ef_ex_cmd_TPortaFKenb  3
#define ef_ex_cmd_RestartEnv   4
#define ef_ex_cmd_4opVlockOff  5
#define ef_ex_cmd_4opVlockOn   6
#define ef_ex_cmd_ForceBpmSld  7
#define ef_ex_ExtendedCmd2     15
#define ef_ex_cmd2_RSS         0
#define ef_ex_cmd2_ResetVol    1
#define ef_ex_cmd2_LockVol     2
#define ef_ex_cmd2_UnlockVol   3
#define ef_ex_cmd2_LockVP      4
#define ef_ex_cmd2_UnlockVP    5
#define ef_ex_cmd2_VSlide_mod  6
#define ef_ex_cmd2_VSlide_car  7
#define ef_ex_cmd2_VSlide_def  8
#define ef_ex_cmd2_LockPan     9
#define ef_ex_cmd2_UnlockPan   10
#define ef_ex_cmd2_VibrOff     11
#define ef_ex_cmd2_TremOff     12
#define ef_ex_cmd2_FVib_FGFS   13
#define ef_ex_cmd2_FTrm_XFGFS  14
#define ef_ex_cmd2_NoRestart   15
#define ef_ex2_PatDelayFrame   0
#define ef_ex2_PatDelayRow     1
#define ef_ex2_NoteDelay       2
#define ef_ex2_NoteCut         3
#define ef_ex2_FineTuneUp      4
#define ef_ex2_FineTuneDown    5
#define ef_ex2_GlVolSlideUp    6
#define ef_ex2_GlVolSlideDn    7
#define ef_ex2_GlVolSlideUpF   8
#define ef_ex2_GlVolSlideDnF   9
#define ef_ex2_GlVolSldUpXF    10
#define ef_ex2_GlVolSldDnXF    11
#define ef_ex2_VolSlideUpXF    12
#define ef_ex2_VolSlideDnXF    13
#define ef_ex2_FreqSlideUpXF   14
#define ef_ex2_FreqSlideDnXF   15
#define ef_ex3_SetConnection   0
#define ef_ex3_SetMultipM      1
#define ef_ex3_SetKslM         2
#define ef_ex3_SetTremoloM     3
#define ef_ex3_SetVibratoM     4
#define ef_ex3_SetKsrM         5
#define ef_ex3_SetSustainM     6
#define ef_ex3_SetMultipC      7
#define ef_ex3_SetKslC         8
#define ef_ex3_SetTremoloC     9
#define ef_ex3_SetVibratoC     10
#define ef_ex3_SetKsrC         11
#define ef_ex3_SetSustainC     12

#define EFGR_ARPVOLSLIDE 1
#define EFGR_FSLIDEVOLSLIDE 2
#define EFGR_TONEPORTAMENTO 3
#define EFGR_VIBRATO 4
#define EFGR_TREMOLO 5
#define EFGR_VIBRATOVOLSLIDE 6
#define EFGR_PORTAVOLSLIDE 7
#define EFGR_RETRIGNOTE 8

// Macros for extracting little-endian integers from raw buffer
#define INT8LE(A)                           (int8_t)((A)[0])
#define UINT8LE(A)                          (uint8_t)((A)[0])
#define INT16LE(A)                          (int16_t)(((A)[0]) | ((A)[1] << 8))
#define UINT16LE(A)                         (uint16_t)(((A)[0]) | ((A)[1] << 8))
#define INT32LE(A)                          (int32_t)(((A)[0]) | ((A)[1] << 8) | ((A)[2] << 16) | ((A)[3] << 24))
#define UINT32LE(A)                         (uint32_t)(((A)[0]) | ((A)[1] << 8) | ((A)[2] << 16) | ((A)[3] << 24))


typedef struct {
    uint16_t freq;
    uint8_t speed;
} tPORTA_TABLE;

typedef struct {
    uint8_t state, note, add1, add2;
} tARPGG_TABLE;

typedef struct {
    int8_t pos;
    uint8_t volM, volC;
} tTREMOR_TABLE;

typedef struct {
    uint8_t pos, dir, speed, depth;
    bool fine;
} tVIBRTREM_TABLE;

typedef struct {
    uint8_t def, val;
} tEFFECT_TABLE;

typedef struct {
    uint16_t fmreg_pos,
         arpg_pos,
         vib_pos;
    uint8_t
         fmreg_duration,
         arpg_count,
         vib_count,
         vib_delay,
         fmreg_ins, // fmreg_table
         arpg_table,
         vib_table,
         arpg_note;
    bool vib_paused;
    uint16_t vib_freq;
} tCH_MACRO_TABLE;





typedef struct {
    tFM_INST_DATA fmpar_table[20]; // TODO: rename to 'fm'
    bool volume_lock[20];
    bool vol4op_lock[20];
    bool peak_lock[20];
    bool pan_lock[20];
    uint8_t modulator_vol[20];
    uint8_t carrier_vol[20];
    // note/instr_def - memorized across rows
    // effects - change each row
    tADTRACK2_EVENT event_table[20];
    uint8_t voice_table[20];
    uint16_t freq_table[20];
    uint16_t zero_fq_table[20];
    tEFFECT_TABLE effect_table[2][20];
    uint8_t fslide_table[2][20];
    tEFFECT_TABLE glfsld_table[2][20];
    tPORTA_TABLE porta_table[2][20];
    bool portaFK_table[20];
    tARPGG_TABLE arpgg_table[2][20];
    tVIBRTREM_TABLE vibr_table[2][20];
    tVIBRTREM_TABLE trem_table[2][20];
    uint8_t retrig_table[2][20];
    tTREMOR_TABLE tremor_table[2][20];
    uint8_t panning_table[20];
    tEFFECT_TABLE last_effect[2][20];
    uint8_t volslide_type[20];
    uint8_t notedel_table[20];
    uint8_t notecut_table[20];
    int8_t ftune_table[20];
    bool keyoff_loop[20];
    uint8_t loopbck_table[20];
    uint8_t loop_table[20][256];
    bool reset_chan[20];
    tCH_MACRO_TABLE macro_table[20];
} tCHDATA;
typedef struct _4op_data {
    uint32_t mode: 1, conn: 3, ch1: 4, ch2: 4, ins1: 8, ins2: 8;
} t4OP_DATA;
#define def_vibtrem_speed_factor 1
#define def_vibtrem_table_size 32

struct A2P {
    const A2mSong *song; A2pWrite write; void *user; int curchip;
    uint8_t current_order, current_pattern, current_line, tempo, speed;
    uint16_t macro_speedup; bool irq_mode; int16_t IRQ_freq; int IRQ_freq_shift; bool irq_initialized, timer_fix, pattern_break, pattern_delay; uint8_t next_line;
    int playback_speed_shift; tPLAY_STATUS play_status; uint8_t overall_volume, global_volume, fade_out_volume;
    uint8_t vibtrem_speed_factor, vibtrem_table_size, vibtrem_table[256], misc_register, current_tremolo_depth, current_vibrato_depth;
    bool speed_update, lockvol, panlock, lockVP; uint8_t tremolo_depth, vibrato_depth; bool volume_scaling, percussion_mode, editor_mode;
    tSONGINFO *songinfo; tINSTR_INFO *instrinfo; unsigned arpvib_count; tVIBRATO_TABLE **vibrato_table; tARPEGGIO_TABLE **arpeggio_table; tEVENTS_INFO *eventsinfo; tCHDATA *ch;
    int ticks, tick0, tickD, tickXF, ticklooper, macro_ticklooper; int type, ffver; unsigned len[21]; bool adsr_carrier[9]; bool songend; int chip;
};
static void opl_setchip(A2P *p, int c) { p->curchip = c; }
static void opl_write(A2P *p, int reg, int val) { p->write(p->user, p->curchip, reg, val); }

static tINSTR_DATA_EXT * get_instr(A2P *p, uint8_t ins);
static int8_t get_instr_fine_tune(A2P *p, uint8_t ins);
static tINSTR_DATA * get_instr_data_by_ch(A2P *p, int chan);
static char get_fm_connect_by_chan(A2P *p, int chan);
static tINSTR_DATA * get_instr_data(A2P *p, uint8_t ins);
static void raw_fill_from_fmdata(A2P *p, uint8_t *dst, tFM_INST_DATA *fm);
static tARPEGGIO_TABLE * get_arpeggio_table(A2P *p, uint8_t arp_table);
static tVIBRATO_TABLE * get_vibrato_table(A2P *p, uint8_t vib_table);
static tFMREG_TABLE * get_fmreg_table(A2P *p, uint8_t fmreg_ins);
static tADTRACK2_EVENT * get_event_p(A2P *p, int pattern, int channel, int row);
static uint16_t regoffs_n(A2P *p, int chan);
static uint16_t regoffs_m(A2P *p, int chan);
static uint16_t regoffs_c(A2P *p, int chan);
static void opl2out(A2P *p, uint16_t reg, uint16_t data);
static void opl3out(A2P *p, uint16_t reg, uint8_t data);
static void opl3exp(A2P *p, uint16_t data);
static uint16_t calc_vibrato_shift(A2P *p, uint8_t depth, uint8_t position);
static void change_freq(A2P *p, int chan, uint16_t freq);
static bool is_chan_adsr_data_empty(A2P *p, int chan);
static bool is_ins_adsr_data_empty(A2P *p, int ins);
static void change_frequency(A2P *p, int chan, uint16_t freq);
static uint16_t _macro_speedup(A2P *p);
static void set_clock_rate(A2P *p, uint8_t clock_rate);
static void update_timer(A2P *p, int Hz);
static void update_playback_speed(A2P *p, int speed_shift);
static void key_on(A2P *p, int chan);
static void key_off(A2P *p, int chan);
static void release_sustaining_sound(A2P *p, int chan);
static t4OP_DATA get_4op_data(A2P *p, uint8_t chan);
static bool _4op_vol_valid_chan(A2P *p, int chan);
static void set_ins_volume(A2P *p, uint8_t modulator, uint8_t carrier, uint8_t chan);
static void set_volume(A2P *p, uint8_t modulator, uint8_t carrier, uint8_t chan);
static void set_ins_volume_4op(A2P *p, uint8_t volume, uint8_t chan);
static void reset_ins_volume(A2P *p, int chan);
static void set_global_volume(A2P *p);
static void set_overall_volume(A2P *p, unsigned char level);
static void init_macro_table(A2P *p, int chan, uint8_t note, uint8_t ins, uint16_t freq);
static void set_ins_data(A2P *p, uint8_t ins, int chan);
static void update_modulator_adsrw(A2P *p, int chan);
static void update_carrier_adsrw(A2P *p, int chan);
static void update_fmpar(A2P *p, int chan);
static bool is_4op_chan(A2P *p, int chan);
static bool is_4op_chan_hi(A2P *p, int chan);
static bool is_4op_chan_lo(A2P *p, int chan);
static void output_note(A2P *p, uint8_t note, uint8_t ins, int chan, bool restart_macro, bool restart_adsr);
static bool no_loop(A2P *p, uint8_t current_chan, uint8_t current_line);
static void update_effect_table(A2P *p, int slot, int chan, int eff_group, uint8_t def, uint8_t val);
static void play_line_arpgg_cleanup(A2P *p, const tADTRACK2_EVENT *event, int chan);
static void play_line_apply_global_fslide_row(A2P *p, tADTRACK2_EVENT *event, int chan);
static void play_line_tremor_row_reset(A2P *p, const tADTRACK2_EVENT *event, int chan);
static void process_effects_prepare(A2P *p, tADTRACK2_EVENT *event, int slot, int chan);
static void process_effects(A2P *p, tADTRACK2_EVENT *event, int slot, int chan);
static void new_process_note(A2P *p, tADTRACK2_EVENT *event, int chan);
static void play_line(A2P *p);
static void generate_custom_vibrato(A2P *p, uint8_t value);
static void check_swap_arp_vibr(A2P *p, tADTRACK2_EVENT *event, int slot, int chan);
static void portamento_up(A2P *p, int chan, uint16_t slide, uint16_t limit);
static void portamento_down(A2P *p, int chan, uint16_t slide, uint16_t limit);
static void macro_vibrato__porta_up(A2P *p, int chan, uint8_t depth);
static void macro_vibrato__porta_down(A2P *p, int chan, uint8_t depth);
static void tone_portamento(A2P *p, int slot, int chan);
static void slide_carrier_volume_up(A2P *p, uint8_t chan, uint8_t slide, uint8_t limit);
static void slide_modulator_volume_up(A2P *p, uint8_t chan, uint8_t slide, uint8_t limit);
static void slide_volume_up(A2P *p, int chan, uint8_t slide);
static void slide_carrier_volume_down(A2P *p, uint8_t chan, uint8_t slide);
static void slide_modulator_volume_down(A2P *p, uint8_t chan, uint8_t slide);
static void slide_volume_down(A2P *p, int chan, uint8_t slide);
static void volume_slide(A2P *p, int chan, uint8_t up_speed, uint8_t down_speed);
static void global_volume_slide(A2P *p, uint8_t up_speed, uint8_t down_speed);
static void arpeggio(A2P *p, int slot, int chan);
static void vibrato(A2P *p, int slot, int chan);
static void tremolo(A2P *p, int slot, int chan);
static int chanvol(A2P *p, int chan);
static void update_effects_slot(A2P *p, int slot, int chan);
static void update_effects(A2P *p);
static void update_fine_effects(A2P *p, int slot, int chan);
static void update_extra_fine_effects_slot(A2P *p, int slot, int chan);
static void update_extra_fine_effects(A2P *p);
static void set_current_order(A2P *p, uint8_t new_order);
static int calc_following_order(A2P *p, uint8_t order);
static void update_song_position(A2P *p);
static void poll_proc(A2P *p);
static void macro_poll_proc(A2P *p);
static void newtimer(A2P *p);
static void init_buffers(A2P *p);
static void init_player(A2P *p);
static void a2t_stop(A2P *p);

/*
 * Adplug - Replayer for many OPL2/OPL3 audio file formats.
 * Copyright (C) 1999 - 2008 Simon Peter, <dn.tlp@gmx.net>, et al.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * a2m-v2.cpp - Adlib Tracker II Player by Dmitry Smagin <dmitry.s.smagin@gmail.com>
 *              Originally by Stanislav Baranec <subz3ro.altair@gmail.com>
 *
 * NOTES:
 * This player loads a2m and a2t modules versions 1 - 14.
 * The code is adapted directly from FreePascal sources of the Adlib Tracker II
 *
 * REFERENCES:
 * https://github.com/ijsf/at2
 * http://www.adlibtracker.net/
 *
 */


static const uint8_t _panning[3] = { 0x30, 0x10, 0x20 };
static const uint8_t def_vibtrem_table[256] = {
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24,
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24,
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24,
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24,
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24,
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24,
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24,
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,
    253,250,244,235,224,212,197,180,161,141,120,97,74,49,24
};

/*** public methods *************************************/









// Helpers for instruments ========================================================================



static tINSTR_DATA_EXT * get_instr(A2P *p, uint8_t ins)
{
    if (ins == 0 || ins > p->instrinfo->count ) {
        return NULL;
    }

    return &p->instrinfo->instruments[ins - 1];
}

static int8_t get_instr_fine_tune(A2P *p, uint8_t ins)
{
    tINSTR_DATA_EXT *instrument = get_instr(p, ins);

    return instrument ? instrument->instr_data.fine_tune : 0;
}

static tINSTR_DATA * get_instr_data_by_ch(A2P *p, int chan)
{
    tINSTR_DATA_EXT *instrument = get_instr(p, p->ch->voice_table[chan]);

    return instrument ? &instrument->instr_data : NULL;
}

static char get_fm_connect_by_chan(A2P *p, int chan)
{
    tINSTR_DATA *instr = get_instr_data_by_ch(p, chan);
    if (!instr) {
        
    }

    return instr ? instr->fm.connect : 0;
}

static tINSTR_DATA * get_instr_data(A2P *p, uint8_t ins)
{
    tINSTR_DATA_EXT *instrument = get_instr(p, ins);

    return instrument ? &instrument->instr_data : NULL;
}

// Fill tFM_INST_DATA from the raw byte array

// Fill uint8_t data[11] array from tFM_INST_DATA
static void raw_fill_from_fmdata(A2P *p, uint8_t *dst, tFM_INST_DATA *fm)
{
    dst[0] = (fm->multipM & 0xf) |
            ((fm->ksrM  & 1) << 4) |
            ((fm->sustM & 1) << 5) |
            ((fm->vibrM & 1) << 6) |
            ((fm->tremM & 1) << 7);
    dst[1] = (fm->multipC & 0xf) |
            ((fm->ksrC  & 1) << 4) |
            ((fm->sustC & 1) << 5) |
            ((fm->vibrC & 1) << 6) |
            ((fm->tremC & 1) << 7);
    dst[2] = (fm->volM & 0x3f) | ((fm->kslM & 3) << 6);
    dst[3] = (fm->volC & 0x3f) | ((fm->kslC & 3) << 6);
    dst[4] = (fm->decM & 0xf) | ((fm->attckM & 0xf) << 4);
    dst[5] = (fm->decC & 0xf) | ((fm->attckC & 0xf) << 4);
    dst[6] = (fm->relM & 0xf) | ((fm->sustnM & 0xf) << 4);
    dst[7] = (fm->relC & 0xf) | ((fm->sustnC & 0xf) << 4);
    dst[8] = fm->wformM & 7;
    dst[9] = fm->wformC & 7;
    dst[10] = (fm->connect & 1) | ((fm->feedb & 7) << 1);
}

// Helpers for macro tables =======================================================================

/* If FMREG length byte is 0, infer span from macro cells when any cell has duration, freq_slide,
 * or macro_flags. Also allocate fmreg when header bytes [1..5] are non-zero even if length stays 0
 * — Pascal still binds arpeggio/vibrato indices from the FMREG header (top-2act instr 19: vibrato
 * table 1 with empty macro cells). */





static tARPEGGIO_TABLE * get_arpeggio_table(A2P *p, uint8_t arp_table)
{
    return arp_table && p->arpeggio_table && p->arpeggio_table[arp_table - 1] ? p->arpeggio_table[arp_table - 1] : NULL;
}

static tVIBRATO_TABLE * get_vibrato_table(A2P *p, uint8_t vib_table)
{
    return vib_table && p->vibrato_table && p->vibrato_table[vib_table - 1] ? p->vibrato_table[vib_table - 1] : NULL;
}

static tFMREG_TABLE * get_fmreg_table(A2P *p, uint8_t fmreg_ins)
{
    tINSTR_DATA_EXT *instrument = get_instr(p, fmreg_ins);

    return instrument && instrument->fmreg ? instrument->fmreg : NULL;
}

// Helpers for patterns ===========================================================================

// event = pattern * (channels * rows) + ch * rows + row
static tADTRACK2_EVENT * get_event_p(A2P *p, int pattern, int channel, int row)
{
    static tADTRACK2_EVENT null_event = { 0 };

    return (
        pattern < p->eventsinfo->patterns
            ? &p->eventsinfo->events[
                pattern * p->eventsinfo->channels * p->eventsinfo->rows +
                channel * p->eventsinfo->rows + row]
            : &null_event
    );
}



// End of patterns helpers ========================================================================

inline bool note_in_range(uint8_t note)
{
    uint8_t n = note & (uint8_t)~keyoff_flag;
    /* Pascal output_note: NOT (note in [1..12*8+1]) uses the inclusive upper bound 97 (a2player.pas). */
    return n >= 1 && n <= (uint8_t)(12 * 8 + 1);
}

static uint16_t regoffs_n(A2P *p, int chan)
{
    static const uint16_t _ch_n[2][20] = {
        {  // mm
            0x003,0x000,0x004,0x001,0x005,0x002,0x006,0x007,0x008,0x103,
            0x100,0x104,0x101,0x105,0x102,0x106,0x107,0x108,BYTE_NULL,BYTE_NULL
        }, { // pm
            0x003,0x000,0x004,0x001,0x005,0x002,0x106,0x107,0x108,0x103,
            0x100,0x104,0x101,0x105,0x102,0x006,0x007,0x008,0x008,0x007
        }
    };

    return _ch_n[!!p->percussion_mode][chan];
}

static uint16_t regoffs_m(A2P *p, int chan)
{
    static const uint16_t _ch_m[2][20] = {
        {  // mm
            0x008,0x000,0x009,0x001,0x00a,0x002,0x010,0x011,0x012,0x108,
            0x100,0x109,0x101,0x10a,0x102,0x110,0x111,0x112,BYTE_NULL,BYTE_NULL
        }, { // pm
            0x008,0x000,0x009,0x001,0x00a,0x002,0x110,0x111,0x112,0x108,
            0x100,0x109,0x101,0x10a,0x102,0x010,0x014,0x012,0x015,0x011
        }
    };

    return _ch_m[!!p->percussion_mode][chan];
}

static uint16_t regoffs_c(A2P *p, int chan)
{
    static const uint16_t _ch_c[2][20] = {
        {
            0x00b,0x003,0x00c,0x004,0x00d,0x005,0x013,0x014,0x015,0x10b,
            0x103,0x10c,0x104,0x10d,0x105,0x113,0x114,0x115,BYTE_NULL,BYTE_NULL
        }, {
            0x00b,0x003,0x00c,0x004,0x00d,0x005,0x113,0x114,0x115,0x10b,
            0x103,0x10c,0x104,0x10d,0x105,0x013,BYTE_NULL,BYTE_NULL,BYTE_NULL,BYTE_NULL
        }
    };

    return _ch_c[!!p->percussion_mode][chan];
}

#define FreqStart   0x156
#define FreqEnd     0x2ae
#define FreqRange   (FreqEnd - FreqStart)

/* Round a/b to nearest integer (matching Pascal's Round(a/b) for positive ints) */
#define ROUND_DIV(a, b) (((a) + (b) / 2) / (b))

/* PLAYER */
static void opl2out(A2P *p, uint16_t reg, uint16_t data)
{
    if (p->chip != 0) {
        p->chip = 0;
        opl_setchip(p, p->chip);
    }
    opl_write(p, reg, data);
}

static void opl3out(A2P *p, uint16_t reg, uint8_t data)
{
    int _chip = reg < 0x100 ? 0 : 1;
    if (p->chip != _chip) {
        p->chip = _chip;
        opl_setchip(p, p->chip);
    }
    opl_write(p, reg & 0xff, data);
}

static void opl3exp(A2P *p, uint16_t data)
{
    if (p->chip != 1) {
        p->chip = 1;
        opl_setchip(p, p->chip);
    }
    opl_write(p, data & 0xff, (data >> 8) & 0xff);
}

static uint16_t nFreq(uint8_t note)
{
    static uint16_t Fnum[12] = {0x157,0x16b,0x181,0x198,0x1b0,0x1ca,0x1e5,
                0x202,0x220,0x241,0x263,0x287};

    if (note >= 12 * 8)
        return (7 << 10) | FreqEnd;

    return (note / 12 << 10) | Fnum[note % 12];
}

static uint16_t calc_freq_shift_up(uint16_t freq, uint16_t shift)
{
    uint16_t oc = (freq >> 10) & 7;
    uint16_t fr = (freq & 0x3ff) + shift;

    if (fr >= FreqEnd) {
        if (oc == 7) {
            fr = FreqEnd;
        } else {
            oc++;
            fr -= FreqRange;
        }
    }

    return (uint16_t)((oc << 10) + fr);
}

static uint16_t calc_freq_shift_down(uint16_t freq, uint16_t shift)
{
    uint16_t oc = (freq >> 10) & 7;
    uint16_t fr = (freq & 0x3ff) - shift;

    if (fr <= FreqStart) {
        if (oc == 0) {
            fr = FreqStart;
        } else {
            oc--;
            fr += FreqRange;
        }
    }

    return (uint16_t)((oc << 10) + fr);
}

/* == calc_vibtrem_shift() in AT2 */
static uint16_t calc_vibrato_shift(A2P *p, uint8_t depth, uint8_t position)
{
    /* depth: 0..F, max result: FF*F=EF1 */
    uint16_t X = (uint16_t)depth * p->vibtrem_table[position & (p->vibtrem_table_size - 1)];
    uint16_t rotated = (X << 1) | (X >> 15);
    uint16_t result  = ((rotated >> 8) & 0xFF) | ((rotated & 1) << 8);

    return result;
}

static void change_freq(A2P *p, int chan, uint16_t freq)
{
    if (is_4op_chan(p, chan) && is_4op_chan_hi(p, chan)) {
        p->ch->freq_table[chan + 1] = p->ch->freq_table[chan];
        chan++;
    }

    p->ch->freq_table[chan] &= ~0x1fff;
    p->ch->freq_table[chan] |= (freq & 0x1fff);

    uint16_t n = regoffs_n(p, chan);

    opl3out(p, 0xa0 + n, p->ch->freq_table[chan] & 0xFF);
    opl3out(p, 0xb0 + n, (p->ch->freq_table[chan] >> 8) & 0xFF);

    if (is_4op_chan(p, chan) && is_4op_chan_lo(p, chan)) {
        p->ch->freq_table[chan - 1] = p->ch->freq_table[chan];
    }
}

static bool is_chan_adsr_data_empty(A2P *p, int chan)
{
    tFM_INST_DATA *fmpar = &p->ch->fmpar_table[chan];
    uint8_t data[11];
    raw_fill_from_fmdata(p, data, fmpar);

    return (
        !data[4] &&
        !data[5] &&
        !data[6] &&
        !data[7]
    );
}

static bool is_ins_adsr_data_empty(A2P *p, int ins)
{
    tINSTR_DATA *i = get_instr_data(p, ins);
    if (!i)
        return true;

    uint8_t data[11];
    raw_fill_from_fmdata(p, data, &i->fm);

    return (
        !data[4] &&
        !data[5] &&
        !data[6] &&
        !data[7]
    );
}

static bool is_data_empty(char *data, unsigned int size)
{
    while (size--) {
        if (*(char *)data++)
            return false;
    }

    return true;
}

static inline uint16_t max(uint16_t value, uint16_t maximum)
{
    return (value > maximum ? maximum : value);
}

static inline uint16_t min(uint16_t value, uint16_t minimum)
{
    return (value < minimum ? minimum : value);
}

static void change_frequency(A2P *p, int chan, uint16_t freq)
{
    p->ch->macro_table[chan].vib_paused = true;
    change_freq(p, chan, freq);

    if (is_4op_chan(p, chan)) {
        int i = is_4op_chan_hi(p, chan) ? 1 : -1;

        p->ch->macro_table[chan + i].vib_count = 1;
        p->ch->macro_table[chan + i].vib_pos = 0;
        p->ch->macro_table[chan + i].vib_freq = freq;
        p->ch->macro_table[chan + i].vib_paused = false;
    }

    p->ch->macro_table[chan].vib_count = 1;
    p->ch->macro_table[chan].vib_pos = 0;
    p->ch->macro_table[chan].vib_freq = freq;
    p->ch->macro_table[chan].vib_paused = false;
}

static uint16_t _macro_speedup(A2P *p)
{
    return p->macro_speedup ? p->macro_speedup : 1;
}

static void set_clock_rate(A2P *p, uint8_t clock_rate)
{

}

static void update_timer(A2P *p, int Hz)
{
    if (Hz == 0) {
        set_clock_rate(p, 0);
        return;
    } else {
        p->tempo = Hz;
    }

    if (p->tempo == 18 && p->timer_fix) {
        p->IRQ_freq = ((float)p->tempo + 0.2) * 20.0;
    } else {
        p->IRQ_freq = 250;
    }

    while (p->IRQ_freq % (p->tempo * _macro_speedup(p)) != 0) {
        p->IRQ_freq++;
    }

    if (p->IRQ_freq > MAX_IRQ_FREQ)
        p->IRQ_freq = MAX_IRQ_FREQ;

    while ((p->IRQ_freq + p->IRQ_freq_shift + p->playback_speed_shift > MAX_IRQ_FREQ) && (p->playback_speed_shift > 0))
        p->playback_speed_shift--;

    while ((p->IRQ_freq + p->IRQ_freq_shift + p->playback_speed_shift > MAX_IRQ_FREQ) && (p->IRQ_freq_shift > 0))
        p->IRQ_freq_shift--;

    set_clock_rate(p, 1193180 / max(p->IRQ_freq + p->IRQ_freq_shift + p->playback_speed_shift, MAX_IRQ_FREQ));
}

static void update_playback_speed(A2P *p, int speed_shift)
{
    if (!speed_shift)
        return;

    if ((speed_shift > 0) && (p->IRQ_freq + p->playback_speed_shift + speed_shift > MAX_IRQ_FREQ)) {
        while (p->IRQ_freq + p->IRQ_freq_shift + p->playback_speed_shift + speed_shift > MAX_IRQ_FREQ)
            speed_shift--;
    } else if ((speed_shift < 0) && (p->IRQ_freq + p->IRQ_freq_shift + p->playback_speed_shift + speed_shift < MIN_IRQ_FREQ)) {
        while (p->IRQ_freq + p->IRQ_freq_shift + p->playback_speed_shift + speed_shift < MIN_IRQ_FREQ)
            speed_shift++;
    }

    p->playback_speed_shift += speed_shift;
    update_timer(p, p->tempo);
}

static void key_on(A2P *p, int chan)
{
    int i = is_4op_chan(p, chan) && is_4op_chan_hi(p, chan) ? 1 : 0;

    opl3out(p, 0xb0 + regoffs_n(p, chan + i), 0);
}

static void key_off(A2P *p, int chan)
{
    p->ch->freq_table[chan] &= ~0x2000;
    change_frequency(p, chan, p->ch->freq_table[chan]);
    p->ch->event_table[chan].note |= keyoff_flag;
}

static void release_sustaining_sound(A2P *p, int chan)
{
    uint16_t m = regoffs_m(p, chan);
    uint16_t c = regoffs_c(p, chan);

    opl3out(p, 0x40 + m, 63);
    opl3out(p, 0x40 + c, 63);

    // clear adsrw_mod and adsrw_car (match Pascal's FillChar zeroing all fields)
    p->ch->fmpar_table[chan].decM = 0;
    p->ch->fmpar_table[chan].attckM = 0;
    p->ch->fmpar_table[chan].relM = 0;
    p->ch->fmpar_table[chan].sustnM = 0;
    p->ch->fmpar_table[chan].wformM = 0;
    p->ch->fmpar_table[chan].decC = 0;
    p->ch->fmpar_table[chan].attckC = 0;
    p->ch->fmpar_table[chan].relC = 0;
    p->ch->fmpar_table[chan].sustnC = 0;
    p->ch->fmpar_table[chan].wformC = 0;

    key_on(p, chan);
    opl3out(p, 0x60 + m, BYTE_NULL);
    opl3out(p, 0x60 + c, BYTE_NULL);
    opl3out(p, 0x80 + m, BYTE_NULL);
    opl3out(p, 0x80 + c, BYTE_NULL);

    key_off(p, chan);
    p->ch->event_table[chan].instr_def = 0;
    p->ch->reset_chan[chan] = true;
}

// inverted volume here
static uint8_t scale_volume(uint8_t volume, uint8_t scale_factor)
{
    return 63 - (((63 - volume) * (63 - scale_factor) + 31) / 63);
}

// former _4op_data_flag()
static t4OP_DATA get_4op_data(A2P *p, uint8_t chan)
{
    t4OP_DATA d = { false, 0, 0, 0, 0, 0 };

    if (!is_4op_chan(p, chan))
        return d;

    d.mode = true;

    if (is_4op_chan_hi(p, chan)) {
        d.ch1 = chan;
        d.ch2 = chan + 1;
    } else {
        d.ch1 = chan - 1;
        d.ch2 = chan;
    }

    d.ins1 = p->ch->event_table[d.ch1].instr_def;
    if (d.ins1 == 0) d.ins1 = p->ch->voice_table[d.ch1];

    d.ins2 = p->ch->event_table[d.ch2].instr_def;
    if (d.ins2 == 0) d.ins2 = p->ch->voice_table[d.ch2];

    if (d.ins1 && d.ins2) {
        tINSTR_DATA *id1 = get_instr_data(p, d.ins1);
        tINSTR_DATA *id2 = get_instr_data(p, d.ins2);
        if (id1 && id2)
            d.conn = (id1->fm.connect << 1) | id2->fm.connect;
    }

    return d;
}

static bool _4op_vol_valid_chan(A2P *p, int chan)
{
    t4OP_DATA d = get_4op_data(p, chan);

    return d.mode && p->ch->vol4op_lock[chan] && d.ins1 && d.ins2;
}

// inverted volume here
static void set_ins_volume(A2P *p, uint8_t modulator, uint8_t carrier, uint8_t chan)
{
    tINSTR_DATA *instr = get_instr_data_by_ch(p, chan);

    uint8_t volM = instr ? instr->fm.volM : 0;
    uint8_t volC = instr ? instr->fm.volC : 0;
    uint8_t conn = instr ? instr->fm.connect : 0;

    uint16_t m = regoffs_m(p, chan);
    uint16_t c = regoffs_c(p, chan);

    // Note: fmpar[].volM/volC has pure unscaled volume,
    // modulator_vol/carrier_vol have scaled but without p->overall_volume
    if (modulator != BYTE_NULL) {
        uint8_t regm;
        bool is_perc_chan = conn ||
                            (p->percussion_mode && chan >= 16); // in [17..20]

        p->ch->fmpar_table[chan].volM = modulator;

        if (is_perc_chan) { // in [17..20]
            if (p->volume_scaling)
                modulator = scale_volume(volM, modulator);

            modulator = scale_volume(modulator, scale_volume(63 - p->global_volume, 63 - p->fade_out_volume));
            regm = scale_volume(modulator, 63 - p->overall_volume) + (p->ch->fmpar_table[chan].kslM << 6);
        } else {
            regm = modulator + (p->ch->fmpar_table[chan].kslM << 6);
        }

        opl3out(p, 0x40 + m, regm);
        p->ch->modulator_vol[chan] = 63 - modulator;
    }

    if (carrier != BYTE_NULL) {
        uint8_t regc;

        p->ch->fmpar_table[chan].volC = carrier;

        if (p->volume_scaling)
            carrier = scale_volume(volC, carrier);

        carrier = scale_volume(carrier, scale_volume(63 - p->global_volume, 63 - p->fade_out_volume));
        regc = scale_volume(carrier, 63 - p->overall_volume) + (p->ch->fmpar_table[chan].kslC << 6);

        opl3out(p, 0x40 + c, regc);
        p->ch->carrier_vol[chan] = 63 - carrier;
    }
}

// Used by set_ins_volume_4op() only
static void set_volume(A2P *p, uint8_t modulator, uint8_t carrier, uint8_t chan)
{
    tINSTR_DATA *instr = get_instr_data_by_ch(p, chan);

    uint8_t volM = instr ? instr->fm.volM : 0;
    uint8_t volC = instr ? instr->fm.volC : 0;

    uint16_t m = regoffs_m(p, chan);
    uint16_t c = regoffs_c(p, chan);

    if (modulator != BYTE_NULL) {
        uint8_t regm;
        p->ch->fmpar_table[chan].volM = modulator;

        modulator = scale_volume(volM, modulator);
        modulator = scale_volume(modulator, scale_volume(63 - p->global_volume, 63 - p->fade_out_volume));

        regm = scale_volume(modulator, 63 - p->overall_volume) + (p->ch->fmpar_table[chan].kslM << 6);

        opl3out(p, 0x40 + m, regm);
        p->ch->modulator_vol[chan] = 63 - modulator;
    }

    if (carrier != BYTE_NULL) {
        uint8_t regc;
        p->ch->fmpar_table[chan].volC = carrier;

        carrier = scale_volume(volC, carrier);
        carrier = scale_volume(carrier, scale_volume(63 - p->global_volume, 63 - p->fade_out_volume));

        regc = scale_volume(carrier, 63 - p->overall_volume) + (p->ch->fmpar_table[chan].kslC << 6);

        opl3out(p, 0x40 + c, regc);
        p->ch->carrier_vol[chan] = 63 - carrier;
    }
}

static void set_ins_volume_4op(A2P *p, uint8_t volume, uint8_t chan)
{
    t4OP_DATA d = get_4op_data(p, chan);

    if (!_4op_vol_valid_chan(p, chan))
        return;

    uint8_t volM1 = BYTE_NULL;
    uint8_t volC1 = BYTE_NULL;
    uint8_t volM2 = BYTE_NULL;
    uint8_t volC2 = BYTE_NULL;

    volC1 = volume == BYTE_NULL ? p->ch->fmpar_table[d.ch1].volC : volume;

    switch (d.conn) {
    case 0: // FM/FM ins1=FM, ins2=FM
        break;
    case 1: // FM/AM ins1=FM, ins2=AM
        volM2 = volume == BYTE_NULL ? p->ch->fmpar_table[d.ch2].volM : volume;
        break;
    case 2: // AM/FM ins1=AM, ins2=FM
        volC2 = volume == BYTE_NULL ? p->ch->fmpar_table[d.ch2].volC : volume;
        break;
    case 3:// AM/AM ins1=AM, ins2=AM
        volM1 = volume == BYTE_NULL ? p->ch->fmpar_table[d.ch1].volM : volume;
        volM2 = volume == BYTE_NULL ? p->ch->fmpar_table[d.ch2].volM : volume;
        break;
    }

    set_volume(p, volM1, volC1, d.ch1);
    set_volume(p, volM2, volC2, d.ch2);
}

static void reset_ins_volume(A2P *p, int chan)
{
    tINSTR_DATA *instr = get_instr_data_by_ch(p, chan);
    if (!instr) {
        set_ins_volume(p, 0, 0, chan);
        return;
    }

    uint8_t vol_mod = instr->fm.volM;
    uint8_t vol_car = instr->fm.volC;
    uint8_t conn = instr->fm.connect;

    if (p->volume_scaling) {
        vol_mod = (!conn ? vol_mod : 0);
        vol_car = 0;
    }

    set_ins_volume(p, vol_mod, vol_car, chan);
}

static void set_global_volume(A2P *p)
{
    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        if (_4op_vol_valid_chan(p, chan)) {
            set_ins_volume_4op(p, BYTE_NULL, chan);
        } else if (p->ch->carrier_vol[chan] || p->ch->modulator_vol[chan]) {
            char instr_fm_connect = get_fm_connect_by_chan(p, chan);

            set_ins_volume(p, instr_fm_connect ? p->ch->fmpar_table[chan].volM : BYTE_NULL, p->ch->fmpar_table[chan].volC, chan);
        }
    }
}

static void set_overall_volume(A2P *p, unsigned char level)
{
    p->overall_volume = min(level, 63);
    set_global_volume(p);
}

static void init_macro_table(A2P *p, int chan, uint8_t note, uint8_t ins, uint16_t freq)
{
    tINSTR_DATA_EXT *instrument = get_instr(p, ins);

    uint8_t arp_table = instrument ? instrument->arpeggio : 0;
    p->ch->macro_table[chan].fmreg_pos = 0;
    p->ch->macro_table[chan].fmreg_duration = 0;
    p->ch->macro_table[chan].fmreg_ins = ins; // todo: check against instruments->fmreg.length
    p->ch->macro_table[chan].arpg_count = 1;
    p->ch->macro_table[chan].arpg_pos = 0;
    p->ch->macro_table[chan].arpg_table = arp_table;
    p->ch->macro_table[chan].arpg_note = note;

    uint8_t vib_table = instrument ? instrument->vibrato : 0;
    tVIBRATO_TABLE *vib = get_vibrato_table(p, vib_table);
    uint8_t vib_delay = vib ? vib->delay : 0;

    p->ch->macro_table[chan].vib_count = 1;
    p->ch->macro_table[chan].vib_paused = false;
    p->ch->macro_table[chan].vib_pos = 0;
    p->ch->macro_table[chan].vib_table = vib_table;
    p->ch->macro_table[chan].vib_freq = freq;
    p->ch->macro_table[chan].vib_delay = vib_delay;

    p->ch->zero_fq_table[chan] = 0;
}

static void set_ins_data(A2P *p, uint8_t ins, int chan)
{
    static tINSTR_DATA zeroins = { 0 };

    if (ins == 0) return;

    tINSTR_DATA *i = get_instr_data(p, ins);
    i = i ? i : &zeroins;
    uint8_t data[11];
    raw_fill_from_fmdata(p, data, &i->fm);

    if (is_data_empty((char *)i, sizeof(tINSTR_DATA))) {
        release_sustaining_sound(p, chan);
    }

    if ((ins != p->ch->event_table[chan].instr_def) || p->ch->reset_chan[chan]) {
        p->ch->panning_table[chan] = !p->ch->pan_lock[chan]
                                  ? i->panning
                                  : p->songinfo->lock_flags[chan] & 3;

        if (p->ch->panning_table[chan] >= sizeof (_panning))
            p->ch->panning_table[chan] = 0; /* various code paths can lead to this value going out of the 0-2 range */

        uint16_t m = regoffs_m(p, chan);
        uint16_t c = regoffs_c(p, chan);
        uint16_t n = regoffs_n(p, chan);

        opl3out(p, 0x20 + m, data[0]);
        opl3out(p, 0x20 + c, data[1]);
        opl3out(p, 0x40 + m, (data[2] & 0xc0) + 63);
        opl3out(p, 0x40 + c, (data[3] & 0xc0) + 63);
        opl3out(p, 0x60 + m, data[4]);
        opl3out(p, 0x60 + c, data[5]);
        opl3out(p, 0x80 + m, data[6]);
        opl3out(p, 0x80 + c, data[7]);
        opl3out(p, 0xe0 + m, data[8]);
        opl3out(p, 0xe0 + c, data[9]);
        opl3out(p, 0xc0 + n, data[10] | _panning[p->ch->panning_table[chan]]);

        p->ch->fmpar_table[chan] = i->fm; // Copy struct

        // Stop instr macro if resetting voice
        if (!p->ch->reset_chan[chan])
            p->ch->keyoff_loop[chan] = false;

        if (p->ch->reset_chan[chan]) {
            p->ch->voice_table[chan] = ins;
            reset_ins_volume(p, chan);
            p->ch->reset_chan[chan] = false;
        }

        uint8_t note = p->ch->event_table[chan].note & ~keyoff_flag;
        note = note_in_range(note) ? note : 0;

        init_macro_table(p, chan, note, ins, p->ch->freq_table[chan]);
    }

    p->ch->voice_table[chan] = ins;
    uint8_t old_ins = p->ch->event_table[chan].instr_def;
    p->ch->event_table[chan].instr_def = ins;

    if (!p->ch->volume_lock[chan] || (ins != old_ins))
        reset_ins_volume(p, chan);
}

static void update_modulator_adsrw(A2P *p, int chan)
{
    tFM_INST_DATA *fmpar = &p->ch->fmpar_table[chan];
    uint16_t m = regoffs_m(p, chan);
    uint8_t data[11];
    raw_fill_from_fmdata(p, data, fmpar);

    opl3out(p, 0x60 + m, data[4]);
    opl3out(p, 0x80 + m, data[6]);
    opl3out(p, 0xe0 + m, data[8]);
}

static void update_carrier_adsrw(A2P *p, int chan)
{
    tFM_INST_DATA *fmpar = &p->ch->fmpar_table[chan];
    uint16_t c = regoffs_c(p, chan);
    uint8_t data[11];
    raw_fill_from_fmdata(p, data, fmpar);

    opl3out(p, 0x60 + c, data[5]);
    opl3out(p, 0x80 + c, data[7]);
    opl3out(p, 0xe0 + c, data[9]);
}

static void update_fmpar(A2P *p, int chan)
{
    tFM_INST_DATA *fmpar = &p->ch->fmpar_table[chan];
    uint8_t data[11];
    raw_fill_from_fmdata(p, data, fmpar);

    opl3out(p, 0x20 + regoffs_m(p, chan), data[0]);
    opl3out(p, 0x20 + regoffs_c(p, chan), data[1]);
    opl3out(p, 0xc0 + regoffs_n(p, chan), data[10] | _panning[p->ch->panning_table[chan]]);

    set_ins_volume(p, fmpar->volM, fmpar->volC, chan);
}

static bool is_4op_chan(A2P *p, int chan)
{
    static char mask[20] = {
        (1<<0), (1<<0), (1<<1), (1<<1), (1<<2), (1<<2), 0, 0, 0,
        (1<<3), (1<<3), (1<<4), (1<<4), (1<<5), (1<<5), 0, 0, 0, 0, 0
    };
/*
    4-op track extension flags byte, channels 1-18
    0  - tracks 1,2
    1  - tracks 3,4
    2  - tracks 5,6
    3  - tracks 10,11
    4  - tracks 12,13
    5  - tracks 14,15
    6  - %unused%
    7  - %unused%
*/
    return (chan > 14 ? false : !!(p->songinfo->flag_4op & mask[chan]));
}

static bool is_4op_chan_hi(A2P *p, int chan)
{
    static bool _4op_hi[20] = {
        true, false, true, false, true, false, false, false, false,                // 0, 2, 4
        true, false, true, false, true, false, false, false, false, false, false   // 9, 10, 13
    };

    return _4op_hi[chan];
}

static bool is_4op_chan_lo(A2P *p, int chan)
{
    static bool _4op_lo[20] = {
        false, true, false, true, false, true, false, false, false,               // 1, 3, 5
        false, true, false, true, false, true, false, false, false, false, false  // 10, 12, 14
    };

    return _4op_lo[chan];
}

static void output_note(A2P *p, uint8_t note, uint8_t ins, int chan, bool restart_macro, bool restart_adsr)
{
    uint16_t freq;

    if ((note == 0) && (p->ch->ftune_table[chan] == 0)) return;

    if ((note & keyoff_flag) || !note_in_range(note)) {
        freq = p->ch->freq_table[chan];
    } else {
        freq = nFreq(note - 1) + get_instr_fine_tune(p, ins);

        if (restart_adsr) {
            key_on(p, chan);
        } else {
            
        }

        p->ch->freq_table[chan] |= 0x2000;
    }

    if (p->ch->ftune_table[chan] == -127)
        p->ch->ftune_table[chan] = 0;

    freq = (uint16_t)((int32_t)freq + (int32_t)p->ch->ftune_table[chan]);
    change_frequency(p, chan, freq);

    if (note) {
        p->ch->event_table[chan].note = note;

        /* Pascal (a2player.pas ~1163–1165): If is_4op_chan(p, chan) then
         * event_table[PRED(chan)].note := note — applies to hi and lo tracks. */
        if (is_4op_chan(p, chan) && chan >= 1)
            p->ch->event_table[chan - 1].note = note;

        if (restart_macro) {
            // Check if no ZFF - force no restart
            bool force_no_restart = (
                    ((p->ch->event_table[chan].eff[0].def == ef_Extended) &&
                     (p->ch->event_table[chan].eff[0].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_NoRestart)) ||
                    ((p->ch->event_table[chan].eff[1].def == ef_Extended) &&
                     (p->ch->event_table[chan].eff[1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_NoRestart))
                );
            if (!force_no_restart) {
                init_macro_table(p, chan, note, ins, freq);
            } else {
                p->ch->macro_table[chan].arpg_note = note;
            }
        }
    }
}

static bool no_loop(A2P *p, uint8_t current_chan, uint8_t current_line)
{
    for (int chan = 0; chan < current_chan; chan++) {
        if ((p->ch->loop_table[chan][current_line] != 0) &&
            (p->ch->loop_table[chan][current_line] != BYTE_NULL))
            return false;
    }

    return true;
}

static int get_effect_group(uint8_t def)
{
    switch (def) {
    case ef_ArpggVSlide:
    case ef_ArpggVSlideFine:    return EFGR_ARPVOLSLIDE;
    case ef_FSlideUpVSlide:
    case ef_FSlUpVSlF:
    case ef_FSlideDownVSlide:
    case ef_FSlDownVSlF:
    case ef_FSlUpFineVSlide:
    case ef_FSlUpFineVSlF:
    case ef_FSlDownFineVSlide:
    case ef_FSlDownFineVSlF:    return EFGR_FSLIDEVOLSLIDE;
    case ef_TonePortamento:     return EFGR_TONEPORTAMENTO;
    case ef_Vibrato:
    case ef_ExtraFineVibrato:   return EFGR_VIBRATO;
    case ef_Tremolo:
    case ef_ExtraFineTremolo:   return EFGR_TREMOLO;
    case ef_VibratoVolSlide:
    case ef_VibratoVSlideFine:  return EFGR_VIBRATOVOLSLIDE;
    case ef_TPortamVolSlide:
    case ef_TPortamVSlideFine:  return EFGR_PORTAVOLSLIDE;
    case ef_RetrigNote:
    case ef_MultiRetrigNote:    return EFGR_RETRIGNOTE;
    default: return -1;
    }

    return -1;
}

// In case of x00 set value of the previous compatible effect command
static void update_effect_table(A2P *p, int slot, int chan, int eff_group, uint8_t def, uint8_t val)
{
    uint8_t lval = p->ch->last_effect[slot][chan].val;

    p->ch->effect_table[slot][chan].def = def;

    if (val) {
        p->ch->effect_table[slot][chan].val = val;
    } else if (get_effect_group(p->ch->last_effect[slot][chan].def) == eff_group && lval) {
        p->ch->effect_table[slot][chan].val = lval;
    } else {
        // x00 without any previous compatible command — Pascal: effect_table := effect_def (val=0, def kept)
        p->ch->effect_table[slot][chan].val = 0;
    }
}

/* a2player.pas play_line first loop: single else-if between arpgg_table and arpgg_table2 — not two
 * independent cleanups, and cleanup runs before Case effect_def (FineTune etc.) on either column. */
static void play_line_arpgg_cleanup(A2P *p, const tADTRACK2_EVENT *event, int chan)
{
    for (int slot = 0; slot < 2; slot++) {
        uint8_t def = event->eff[slot].def;
        uint8_t val = event->eff[slot].val;

        bool arp = (((def == ef_Arpeggio) && (val != 0)) || (def == ef_ExtraFineArpeggio));

        if (!arp &&
            p->ch->arpgg_table[slot][chan].note != 0 &&
            p->ch->arpgg_table[slot][chan].state != 1) {
            p->ch->arpgg_table[slot][chan].state = 1;
            change_frequency(p, chan, nFreq(p->ch->arpgg_table[slot][chan].note - 1) +
                get_instr_fine_tune(p, p->ch->event_table[chan].instr_def));
        }
    }
}

/* a2player.pas play_line first loop (~1364–1442): GlobalFSlide for column 1 then 2,
 * per channel, before the cross-channel effect_def / effect_def2 Case passes. */
static void play_line_apply_global_fslide_row(A2P *p, tADTRACK2_EVENT *event, int chan)
{
    for (int slot = 0; slot < 2; slot++) {
        uint8_t def = event->eff[slot].def;
        uint8_t val = event->eff[slot].val;

        if ((def != ef_GlobalFSlideUp) && (def != ef_GlobalFSlideDown))
            continue;

        if ((event->eff[slot ^ 1].def == ef_Extended) &&
            (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd * 16 + ef_ex_cmd_ForceBpmSld)) {

            

            if (def == ef_GlobalFSlideUp) {
                update_playback_speed(p, val);
            } else {
                update_playback_speed(p, -val);
            }
        } else {
            uint8_t eff;

            switch (def) {
            case ef_GlobalFSlideUp:
                eff = ef_FSlideUp;

                // >xx + ZFE
                if ((event->eff[slot ^ 1].def == ef_Extended) &&
                    (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_FTrm_XFGFS)) {
                    eff = ef_GlobalFreqSlideUpXF;
                }

                // >xx + ZFD
                if ((event->eff[slot ^ 1].def == ef_Extended) &&
                    (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_FVib_FGFS)) {
                    eff = ef_FSlideUpFine;
                }

                p->ch->effect_table[slot][chan].def = eff;
                p->ch->effect_table[slot][chan].val = val;
                break;
            case ef_GlobalFSlideDown:
                eff = ef_FSlideDown;

                // <xx + ZFE
                if ((event->eff[slot ^ 1].def == ef_Extended) &&
                    (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_FTrm_XFGFS)) {
                    eff = ef_GlobalFreqSlideDnXF;
                }

                // <xx + ZFD
                if ((event->eff[slot ^ 1].def == ef_Extended) &&
                    (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_FVib_FGFS)) {
                    eff = ef_FSlideDownFine;
                }

                p->ch->effect_table[slot][chan].def = eff;
                p->ch->effect_table[slot][chan].val = val;
                break;
            }

            for (int c = chan; c < p->songinfo->nm_tracks; c++) {
                p->ch->fslide_table[slot][c] = val;
                p->ch->glfsld_table[slot][c].def = p->ch->effect_table[slot][chan].def;
                p->ch->glfsld_table[slot][c].val = p->ch->effect_table[slot][chan].val;
            }
        }
    }
}

/* a2player.pas ~1452–1466: once per row after init loop, from pattern columns. */
static void play_line_tremor_row_reset(A2P *p, const tADTRACK2_EVENT *event, int chan)
{
    for (int slot = 0; slot < 2; slot++) {
        if (p->ch->tremor_table[slot][chan].pos && (event->eff[slot].def != ef_Tremor)) {
            p->ch->tremor_table[slot][chan].pos = 0;
            set_ins_volume(p, p->ch->tremor_table[slot][chan].volM, p->ch->tremor_table[slot][chan].volC, chan);
        }
    }
}

static void process_effects_prepare(A2P *p, tADTRACK2_EVENT *event, int slot, int chan)
{
    uint8_t def = event->eff[slot].def;

    if ((def != ef_Vibrato) &&
        (def != ef_ExtraFineVibrato) &&
        (def != ef_VibratoVolSlide) &&
        (def != ef_VibratoVSlideFine))
        memset(&p->ch->vibr_table[slot][chan], 0, sizeof(p->ch->vibr_table[slot][chan]));

    if ((def != ef_RetrigNote) &&
        (def != ef_MultiRetrigNote))
        memset(&p->ch->retrig_table[slot][chan], 0, sizeof(p->ch->retrig_table[slot][chan]));

    if ((def != ef_Tremolo) &&
        (def != ef_ExtraFineTremolo))
        memset(&p->ch->trem_table[slot][chan], 0, sizeof(p->ch->trem_table[slot][chan]));
}

static void process_effects(A2P *p, tADTRACK2_EVENT *event, int slot, int chan)
{
    uint8_t def = event->eff[slot].def;
    uint8_t val = event->eff[slot].val;

    switch (def) {
    case ef_Arpeggio:
    case ef_ExtraFineArpeggio:
    case ef_ArpggVSlide:
    case ef_ArpggVSlideFine:
        /* Pascal (a2player.pas ~1498-1499): skip entire arpeggio block when
         * effect_def == ef_Arpeggio AND effect == 0. This preserves the
         * previous arpeggio state/add1/add2 instead of resetting them. */
        if ((def == ef_Arpeggio) && (val == 0))
            break;

        switch (def) {
        case ef_Arpeggio:
            p->ch->effect_table[slot][chan].def = ef_Arpeggio;
            p->ch->effect_table[slot][chan].val = val;
            break;
        case ef_ExtraFineArpeggio:
            p->ch->effect_table[slot][chan].def = ef_ExtraFineArpeggio;
            p->ch->effect_table[slot][chan].val = val;
            break;
        case ef_ArpggVSlide:
        case ef_ArpggVSlideFine:
            update_effect_table(p, slot, chan, EFGR_ARPVOLSLIDE, def, val);
            break;
        }

        {
            uint8_t raw_note = event->note & ~keyoff_flag;
            bool has_new_note = note_in_range(raw_note);
            uint8_t resolved_note = has_new_note
                ? raw_note
                : (note_in_range(p->ch->event_table[chan].note & ~keyoff_flag)
                    ? p->ch->event_table[chan].note & ~keyoff_flag
                    : 0);

            /* Pascal (a2player.pas ~1527-1529): check last_effect (saved at
             * start of play_line before effect_table was cleared) to decide
             * whether to reset arpeggio state on carry-over rows. */
            uint8_t prev_def = p->ch->last_effect[slot][chan].def;
            bool prev_was_arp = (prev_def == ef_Arpeggio) ||
                                (prev_def == ef_ExtraFineArpeggio) ||
                                (prev_def == ef_ArpggVSlide) ||
                                (prev_def == ef_ArpggVSlideFine);

            if (resolved_note) {
                if (has_new_note) {
                    /* Pascal: event[chan].note is in range — always reset */
                    p->ch->arpgg_table[slot][chan].state = 0;
                } else if (!prev_was_arp) {
                    /* Pascal: carry-over from event_table — check last_effect */
                    p->ch->arpgg_table[slot][chan].state = 0;
                }

                p->ch->arpgg_table[slot][chan].note = resolved_note;
                if ((def == ef_Arpeggio) || (def == ef_ExtraFineArpeggio)) {
                    p->ch->arpgg_table[slot][chan].add1 = (val >> 4) & 0x0f;
                    p->ch->arpgg_table[slot][chan].add2 = val & 0x0f;
                }
            } else {
                p->ch->effect_table[slot][chan].def = 0;
                p->ch->effect_table[slot][chan].val = 0;
            }
        }
        break;

    case ef_FSlideUp:
    case ef_FSlideDown:
    case ef_FSlideUpFine:
    case ef_FSlideDownFine:
        p->ch->effect_table[slot][chan].def = def;
        p->ch->effect_table[slot][chan].val = val;
        p->ch->fslide_table[slot][chan] = val;
        break;

    case ef_FSlideUpVSlide:
    case ef_FSlUpVSlF:
    case ef_FSlideDownVSlide:
    case ef_FSlDownVSlF:
    case ef_FSlUpFineVSlide:
    case ef_FSlUpFineVSlF:
    case ef_FSlDownFineVSlide:
    case ef_FSlDownFineVSlF:
        update_effect_table(p, slot, chan, EFGR_FSLIDEVOLSLIDE, def, val);
        break;

    case ef_TonePortamento:
        /* Pascal (a2player.pas ~1577-1600, ~2155-2176): TonePortamento is activated
         * only when the row carries a plain note (1..12*8+1, no keyoff bit) OR when
         * last_effect's p->type already matches ef_TonePortamento (carry-over).
         * A key-off note (note | 0x80) is NOT in [1..97], so Pascal falls through
         * to the carry-over branch — effect_table/p->speed only set if prior porta existed. */
        {
            uint8_t has_note = event->note >= 1 && event->note  <= 12*8+1;
            bool has_carry = p->ch->last_effect[slot][chan].def == ef_TonePortamento;

            if (has_note || has_carry) {
                if (event->eff[slot].val != 0) {
                    p->ch->effect_table[slot][chan].def = ef_TonePortamento;
                    p->ch->effect_table[slot][chan].val = val;
                } else if (has_carry && p->ch->last_effect[slot][chan].val != 0) {
                    p->ch->effect_table[slot][chan].def = ef_TonePortamento;
                    p->ch->effect_table[slot][chan].val = p->ch->last_effect[slot][chan].val;
                } else {
                    p->ch->effect_table[slot][chan].def = ef_TonePortamento;
                    p->ch->effect_table[slot][chan].val = 0;
                }

                p->ch->porta_table[slot][chan].speed = p->ch->effect_table[slot][chan].val;
                if (has_note) {
                    p->ch->porta_table[slot][chan].freq =
                        nFreq((event->note - 1)) +
                        get_instr_fine_tune(p, p->ch->event_table[chan].instr_def);
                }
            }
        }
        break;

    case ef_TPortamVolSlide:
    case ef_TPortamVSlideFine:
        update_effect_table(p, slot, chan, EFGR_PORTAVOLSLIDE, def, val);

        break;

    case ef_Vibrato:
    case ef_ExtraFineVibrato:
        update_effect_table(p, slot, chan, EFGR_VIBRATO, def, val);

        if ((event->eff[slot ^ 1].def == ef_Extended) &&
            (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_FVib_FGFS)) {
            p->ch->vibr_table[slot][chan].fine = true;
        }

        /* Pascal: vibr_table.speed := HI(effect_table) DIV 16 (after merge / x00 carry). */
        {
            uint8_t m = p->ch->effect_table[slot][chan].val;
            p->ch->vibr_table[slot][chan].speed = m / 16;
            p->ch->vibr_table[slot][chan].depth = m % 16;
        }
        break;

    case ef_Tremolo:
    case ef_ExtraFineTremolo:
        update_effect_table(p, slot, chan, EFGR_TREMOLO, def, val);

        if ((event->eff[slot ^ 1].def == ef_Extended) &&
            (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_FTrm_XFGFS)) {
            p->ch->trem_table[slot][chan].fine = true;
        }

        {
            uint8_t m = p->ch->effect_table[slot][chan].val;
            p->ch->trem_table[slot][chan].speed = m / 16;
            p->ch->trem_table[slot][chan].depth = m % 16;
        }
        break;

    case ef_VibratoVolSlide:
    case ef_VibratoVSlideFine:
        update_effect_table(p, slot, chan, EFGR_VIBRATOVOLSLIDE, def, val);

        if ((event->eff[slot ^ 1].def == ef_Extended) &&
            (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_FVib_FGFS))
            p->ch->vibr_table[slot][chan].fine = true;
        break;

    case ef_SetCarrierVol:
        set_ins_volume(p, BYTE_NULL, 63 - val, chan);
        /* Pascal (a2player.pas ~1661): one-shot — does NOT set effect_table.
         * Clear def so last_effect captures {def=0, val=preserved}. */
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_SetModulatorVol:
        set_ins_volume(p, 63 - val, BYTE_NULL, chan);
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_SetInsVolume:
        {
            tINSTR_DATA *instr = get_instr_data_by_ch(p, chan);
            if (!instr || is_data_empty((char *)instr, sizeof(tINSTR_DATA))) {
                p->ch->effect_table[slot][chan].def = 0;
                break;
            }

            if (_4op_vol_valid_chan(p, chan)) {
                set_ins_volume_4op(p, 63 - val, chan);
            } else if (p->percussion_mode && ((chan >= 16) && (chan <= 19))) {
                set_ins_volume(p, 63 - val, BYTE_NULL, chan);
            } else if (instr->fm.connect == 0) {
                set_ins_volume(p, BYTE_NULL, 63 - val, chan);
            } else {
                set_ins_volume(p, 63 - val, 63 - val, chan);
            }
            p->ch->effect_table[slot][chan].def = 0;
            break;
    }

    case ef_ForceInsVolume:
        {
            tINSTR_DATA *instr = get_instr_data_by_ch(p, chan);
            if (!instr || is_data_empty((char *)instr, sizeof(tINSTR_DATA))) {
                p->ch->effect_table[slot][chan].def = 0;
                break;
            }

            if (p->percussion_mode && ((chan >= 16) && (chan <= 19))) {
                set_ins_volume(p, 63 - val, BYTE_NULL, chan);
            } else if (instr->fm.connect == 0) {
                set_ins_volume(p, scale_volume(instr->fm.volM, 63 - val), 63 - val, chan);
            } else {
                set_ins_volume(p, 63 - val, 63 - val, chan);
            }
            p->ch->effect_table[slot][chan].def = 0;
            break;
        }

    case ef_PositionJump:
        if (no_loop(p, chan, p->current_line)) {
            p->pattern_break = true;
            p->next_line = pattern_break_flag + chan;
        }
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_PatternBreak:
        if (no_loop(p, chan, p->current_line)) {
            p->pattern_break = true;
            p->next_line = max(val, p->songinfo->patt_len - 1);
        }
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_SetSpeed:
        p->speed = val;
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_SetTempo:
        update_timer(p, val);
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_SetWaveform:
        if (val / 16 <= 7) {
            p->ch->fmpar_table[chan].wformC = val / 16;
            update_carrier_adsrw(p, chan);
        }

        if (val % 16 <= 7) {
            p->ch->fmpar_table[chan].wformM = val % 16;
            update_modulator_adsrw(p, chan);
        }
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_VolSlide:
        p->ch->effect_table[slot][chan].def = def;
        p->ch->effect_table[slot][chan].val = val;
        break;

    case ef_VolSlideFine:
        p->ch->effect_table[slot][chan].def = def;
        p->ch->effect_table[slot][chan].val = val;
        break;

    case ef_RetrigNote:
        if (val) {
            if (get_effect_group(p->ch->last_effect[slot][chan].def) != EFGR_RETRIGNOTE) {
                p->ch->retrig_table[slot][chan] = 1;
            }

            p->ch->effect_table[slot][chan].def = def;
            p->ch->effect_table[slot][chan].val = val;
        }
        break;

    case ef_MultiRetrigNote:
        if (val / 16) {
            if (get_effect_group(p->ch->last_effect[slot][chan].def) != EFGR_RETRIGNOTE) {
                p->ch->retrig_table[slot][chan] = 1;
            }

            p->ch->effect_table[slot][chan].def = def;
            p->ch->effect_table[slot][chan].val = val;
        }
        break;

    case ef_SetGlobalVolume:
        p->global_volume = val;
        set_global_volume(p);
        p->ch->effect_table[slot][chan].def = 0;
        break;

    case ef_Tremor:
        if (val) {
            if (p->ch->last_effect[slot][chan].def != ef_Tremor) {
                p->ch->tremor_table[slot][chan].pos = 0;
                p->ch->tremor_table[slot][chan].volM = p->ch->fmpar_table[chan].volM;
                p->ch->tremor_table[slot][chan].volC = p->ch->fmpar_table[chan].volC;
            }

            p->ch->effect_table[slot][chan].def = def;
            p->ch->effect_table[slot][chan].val = val;
        }
        break;

    case ef_Extended:
        switch (val / 16) {
        case ef_ex_SetTremDepth:
            switch (val % 16) {
            case 0:
                opl3out(p, 0xbd, p->misc_register & 0x7f);
                p->current_tremolo_depth = 0;
                break;

            case 1:
                opl3out(p, 0xbd, p->misc_register | 0x80);
                p->current_tremolo_depth = 1;
                break;
            }
            break;

        case ef_ex_SetVibDepth:
            switch (val % 16) {
            case 0:
                opl3out(p, 0xbd, p->misc_register & 0xbf);
                p->current_vibrato_depth = 0;
                break;

            case 1:
                opl3out(p, 0xbd, p->misc_register | 0x40);
                p->current_vibrato_depth = 1;
                break;
            }
            break;

        case ef_ex_SetAttckRateM:
            p->ch->fmpar_table[chan].attckM = val % 16;
            update_modulator_adsrw(p, chan);
            break;

        case ef_ex_SetDecayRateM:
            p->ch->fmpar_table[chan].decM = val % 16;
            update_modulator_adsrw(p, chan);
            break;

        case ef_ex_SetSustnLevelM:
            p->ch->fmpar_table[chan].sustnM = val % 16;
            update_modulator_adsrw(p, chan);
            break;

        case ef_ex_SetRelRateM:
            p->ch->fmpar_table[chan].relM = val % 16;
            update_modulator_adsrw(p, chan);
            break;

        case ef_ex_SetAttckRateC:
            p->ch->fmpar_table[chan].attckC = val % 16;
            update_carrier_adsrw(p, chan);
            break;

        case ef_ex_SetDecayRateC:
            p->ch->fmpar_table[chan].decC = val % 16;
            update_carrier_adsrw(p, chan);
            break;

        case ef_ex_SetSustnLevelC:
            p->ch->fmpar_table[chan].sustnC = val % 16;
            update_carrier_adsrw(p, chan);
            break;

        case ef_ex_SetRelRateC:
            p->ch->fmpar_table[chan].relC = val % 16;
            update_carrier_adsrw(p, chan);
            break;

        case ef_ex_SetFeedback:
            p->ch->fmpar_table[chan].feedb = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex_SetPanningPos:
            p->ch->panning_table[chan] = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex_PatternLoop:
        case ef_ex_PatternLoopRec:
            if (val % 16 == 0) {
                p->ch->loopbck_table[chan] = p->current_line;
            } else {
                if (p->ch->loopbck_table[chan] != BYTE_NULL) {
                    if (p->ch->loop_table[chan][p->current_line] == BYTE_NULL)
                        p->ch->loop_table[chan][p->current_line] = val % 16;

                    if (p->ch->loop_table[chan][p->current_line] != 0) {
                        p->pattern_break = true;
                        p->next_line = pattern_loop_flag + chan;
                    } else {
                        if (val / 16 == ef_ex_PatternLoopRec)
                            p->ch->loop_table[chan][p->current_line] = BYTE_NULL;
                    }
                }
            }
            break;
        case ef_ex_ExtendedCmd:
            switch (val & 0x0f) {
            case ef_ex_cmd_MKOffLoopDi: p->ch->keyoff_loop[chan] = false;   break;
            case ef_ex_cmd_MKOffLoopEn: p->ch->keyoff_loop[chan] = true;    break;
            case ef_ex_cmd_TPortaFKdis: p->ch->portaFK_table[chan] = false; break;
            case ef_ex_cmd_TPortaFKenb: p->ch->portaFK_table[chan] = true;  break;
            case ef_ex_cmd_RestartEnv:
                key_on(p, chan);
                change_freq(p, chan, p->ch->freq_table[chan]);
                break;
            case ef_ex_cmd_4opVlockOff:
                if (is_4op_chan(p, chan)) {
                    p->ch->vol4op_lock[chan] = false;
                    int i = is_4op_chan_hi(p, chan) ? 1 : -1;

                    p->ch->vol4op_lock[chan + i] = false;
                }
                break;
            case ef_ex_cmd_4opVlockOn:
                if (is_4op_chan(p, chan)) {
                    p->ch->vol4op_lock[chan] = true;
                    int i = is_4op_chan_hi(p, chan) ? 1 : -1;

                    p->ch->vol4op_lock[chan + i] = true;
                }
                break;
            }
            break;
        case ef_ex_ExtendedCmd2:
            switch (val % 16) {
            case ef_ex_cmd2_RSS:        release_sustaining_sound(p, chan); break;
            case ef_ex_cmd2_ResetVol:   reset_ins_volume(p, chan); break;
            case ef_ex_cmd2_LockVol:    p->ch->volume_lock  [chan] = true; break;
            case ef_ex_cmd2_UnlockVol:  p->ch->volume_lock  [chan] = false; break;
            case ef_ex_cmd2_LockVP:     p->ch->peak_lock    [chan] = true; break;
            case ef_ex_cmd2_UnlockVP:   p->ch->peak_lock    [chan] = false; break;
            case ef_ex_cmd2_VSlide_def: p->ch->volslide_type[chan] = 0; break;
            case ef_ex_cmd2_LockPan:    p->ch->pan_lock     [chan] = true; break;
            case ef_ex_cmd2_UnlockPan:  p->ch->pan_lock     [chan] = false; break;
            case ef_ex_cmd2_VibrOff:    change_frequency(p, chan, p->ch->freq_table[chan]); break;
            case ef_ex_cmd2_TremOff:
                if (is_4op_chan(p, chan)) {
                    set_ins_volume_4op(p, BYTE_NULL, chan);
                } else {
                    set_ins_volume(p, p->ch->fmpar_table[chan].volM, p->ch->fmpar_table[chan].volC, chan);
                }
                break;
            case ef_ex_cmd2_VSlide_car:
                if ((event->eff[slot ^ 1].def == ef_Extended) &&
                    (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 +
                              ef_ex_cmd2_VSlide_mod)) {
                    p->ch->volslide_type[chan] = 3;
                } else {
                    p->ch->volslide_type[chan] = 1;
                }
                break;

            case ef_ex_cmd2_VSlide_mod:
                if ((event->eff[slot ^ 1].def == ef_Extended) &&
                    (event->eff[slot ^ 1].val == ef_ex_ExtendedCmd2 * 16 +
                              ef_ex_cmd2_VSlide_car)) {
                    p->ch->volslide_type[chan] = 3;
                } else {
                    p->ch->volslide_type[chan] = 2;
                }
                break;
            }
            break;
        }
        break;

    case ef_Extended2:
        switch (val / 16) {
        case ef_ex2_PatDelayFrame:
            p->pattern_delay = true;
            p->tickD = val % 16;
            break;

        case ef_ex2_PatDelayRow:
            p->pattern_delay = true;
            p->tickD = p->speed * (val % 16);
            break;

        case ef_ex2_NoteDelay:
            p->ch->effect_table[slot][chan].def = ef_Extended2;
            p->ch->effect_table[slot][chan].val = val;
            p->ch->notedel_table[chan] = val % 16;
            break;

        case ef_ex2_NoteCut:
            p->ch->effect_table[slot][chan].def = ef_Extended2;
            p->ch->effect_table[slot][chan].val = val;
            p->ch->notecut_table[chan] = val % 16;
            break;

        case ef_ex2_FineTuneUp:
            p->ch->ftune_table[chan] += val % 16;
            break;

        case ef_ex2_FineTuneDown:
            p->ch->ftune_table[chan] -= val % 16;
            break;

        case ef_ex2_GlVolSlideUp:
        case ef_ex2_GlVolSlideDn:
        case ef_ex2_GlVolSlideUpF:
        case ef_ex2_GlVolSlideDnF:
        case ef_ex2_GlVolSldUpXF:
        case ef_ex2_GlVolSldDnXF:
        case ef_ex2_VolSlideUpXF:
        case ef_ex2_VolSlideDnXF:
        case ef_ex2_FreqSlideUpXF:
        case ef_ex2_FreqSlideDnXF:
            p->ch->effect_table[slot][chan].def = ef_Extended2;
            p->ch->effect_table[slot][chan].val = val;
            break;
        }
        break;

    case ef_Extended3:
        switch (val / 16) {
        case ef_ex3_SetConnection:
            p->ch->fmpar_table[chan].connect = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetMultipM:
            p->ch->fmpar_table[chan].multipM = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetKslM:
            p->ch->fmpar_table[chan].kslM = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetTremoloM:
            p->ch->fmpar_table[chan].tremM = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetVibratoM:
            p->ch->fmpar_table[chan].vibrM = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetKsrM:
            p->ch->fmpar_table[chan].ksrM = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetSustainM:
            p->ch->fmpar_table[chan].sustM = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetMultipC:
            p->ch->fmpar_table[chan].multipC = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetKslC:
            p->ch->fmpar_table[chan].kslC = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetTremoloC:
            p->ch->fmpar_table[chan].tremC = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetVibratoC:
            p->ch->fmpar_table[chan].vibrC = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetKsrC:
            p->ch->fmpar_table[chan].ksrC = val % 16;
            update_fmpar(p, chan);
            break;

        case ef_ex3_SetSustainC:
            p->ch->fmpar_table[chan].sustC = val % 16;
            update_fmpar(p, chan);
            break;
        }
        break;
    }
}

static bool no_swap_and_restart(tADTRACK2_EVENT *event)
{
    // [!xx/@xx] swap arp/swap vib + [zff] no force restart
    return
        !(((event->eff[1].def == ef_SwapArpeggio) ||
            (event->eff[1].def == ef_SwapVibrato)) &&
            (event->eff[0].def == ef_Extended) &&
            (event->eff[0].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_NoRestart)) &&
        !(((event->eff[0].def == ef_SwapArpeggio) ||
            (event->eff[0].def == ef_SwapVibrato)) &&
            (event->eff[1].def == ef_Extended) &&
            (event->eff[1].val == ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_NoRestart));
}

static bool is_eff_porta(tADTRACK2_EVENT *event)
{
    int eff0 = event->eff[0].def;
    bool is_p0 = (eff0 == ef_TonePortamento) ||
                (eff0 == ef_TPortamVolSlide) ||
                (eff0 == ef_TPortamVSlideFine);
    int eff1 = event->eff[1].def;
    bool is_p1 = (eff1 == ef_TonePortamento) ||
                (eff1 == ef_TPortamVolSlide) ||
                (eff1 == ef_TPortamVSlideFine);
    return is_p0 || is_p1;
}

static bool is_eff_notedelay(tADTRACK2_EVENT *event)
{
    return (
        (event->eff[0].def == ef_Extended2 && (event->eff[0].val / 16 == ef_ex2_NoteDelay)) ||
        (event->eff[1].def == ef_Extended2 && (event->eff[1].val / 16 == ef_ex2_NoteDelay))
    );
}

static void new_process_note(A2P *p, tADTRACK2_EVENT *event, int chan)
{
    bool tporta_flag = is_eff_porta(event);
    bool notedelay_flag = is_eff_notedelay(event);
    bool defer_note_row = tporta_flag || notedelay_flag;

    if (event->note == 0) {
        if (p->ch->ftune_table[chan])
            output_note(p, 0, p->ch->voice_table[chan], chan, true, true);
        return;
    }

    /*
     * Pascal play_line(p, a2player.pas): key_off runs when the row carries key-off
     * BEFORE the branches that defer notes (tone porta / note delay). Otherwise a
     * cell like Extended2+NoteDelay + key-off never calls key_off and OPL KEY-ON
     * stays set (fank5 ~IRQ 47232, secondary shadow_regs[1][0xb0]).
     */
    if (event->note & keyoff_flag) {
        key_off(p, chan);
        return;
    }

    /* Same-row immediate output (a2player.pas ~2656–2667): both slots must lack porta & notedelay in LO. */
    if (!defer_note_row) {
        output_note(p, event->note, p->ch->voice_table[chan], chan, true, no_swap_and_restart(event));
        return;
    }

    /* a2player.pas: old note had keyoff — retrigger from stored pitch */
    if ((event->note != 0) && tporta_flag && (p->ch->event_table[chan].note & keyoff_flag)) {
        output_note(p, p->ch->event_table[chan].note & ~keyoff_flag,
                    p->ch->voice_table[chan], chan, false, true);
        return;
    }

    if (event->note != 0) {
        if (p->ch->portaFK_table[chan] && tporta_flag) {
            output_note(p, event->note, event->instr_def, chan, false, true);
        } else {
            p->ch->event_table[chan].note = event->note;
        }
    }
}

static void play_line(A2P *p)
{
    /* Align with a2player.pas play_line:
     * - Per channel: pattern + set_ins + prepare both slots + arpeggio cleanup + GlobalFSlide
     *   (Pascal's first loop), then tremor row reset (second loop).
     * - Then process column-1 (slot 0) row effects for *all* channels, then column-2
     *   for all (Pascal's Case effect_def then Case effect_def2).
     *   Running both columns per channel before advancing let column-2 effects (e.g.
     *   Z21 pattern-delay-rows: p->tickD = p->speed * n) see a stale p->speed when a higher
     *   channel's column-1 had Dxx SetSpeed (tunes/MLF/PINK.A2T).
     * - Final pass: new_process_note + swap macros + update_fine_effects per channel. */
    tADTRACK2_EVENT events[20];
    tADTRACK2_EVENT *event;

    assert((int)p->songinfo->nm_tracks <= 20);

    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        event = &events[chan];
        // save effect_table into last_effect
        for (int slot = 0; slot < 2; slot++) {
            if (p->ch->effect_table[slot][chan].def | p->ch->effect_table[slot][chan].val) {
                p->ch->last_effect[slot][chan].def = p->ch->effect_table[slot][chan].def;
                p->ch->last_effect[slot][chan].val = p->ch->effect_table[slot][chan].val;
            }
            if (p->ch->glfsld_table[slot][chan].def | p->ch->glfsld_table[slot][chan].val) {
                p->ch->effect_table[slot][chan].def = p->ch->glfsld_table[slot][chan].def;
                p->ch->effect_table[slot][chan].val = p->ch->glfsld_table[slot][chan].val;
            } else {
                /* a2player.pas ~1288: effect_table := effect_table AND $0ff00 — clear LO (def), keep HI (val) */
                p->ch->effect_table[slot][chan].def = 0;
            }
        }

        p->ch->ftune_table[chan] = 0;

        // Do a full copy of the event, because we modify event->note
        *event = *get_event_p(p, p->current_pattern, chan, p->current_line); // Copy struct

        // Fixup event->note
        if (event->note == 0xff) { // Key off
            event->note = p->ch->event_table[chan].note | keyoff_flag;
        } else if ((event->note >= fixed_note_flag + 1) && (event->note <= fixed_note_flag + 12*8+1)) {
            event->note -= fixed_note_flag;
        }

        /* Normalize arpeggio: raw file uses def=0 for arpeggio. Convert to
         * ef_Arpeggio (0x80) so it's distinguishable from "no effect". */
        for (int slot = 0; slot < 2; slot++) {
            if (event->eff[slot].def == 0 && event->eff[slot].val != 0)
                event->eff[slot].def = ef_Arpeggio;
        }

        /* Pascal play_line(p, ~1313-1322): unconditional eff copy — when note, instr,
         * or any eff field is non-zero, write ALL eff fields (including zeros) to
         * event_table.  This clears previous eff when a new note appears without eff. */
        if (event->note != 0 || event->instr_def != 0 ||
            (event->eff[0].def | event->eff[0].val) ||
            (event->eff[1].def | event->eff[1].val))
        {
            p->ch->event_table[chan].eff[0].def = event->eff[0].def;
            p->ch->event_table[chan].eff[0].val = event->eff[0].val;
            p->ch->event_table[chan].eff[1].def = event->eff[1].def;
            p->ch->event_table[chan].eff[1].val = event->eff[1].val;
        }

        // alters p->ch->event_table[].instr_def
        set_ins_data(p, event->instr_def, chan);

        // set effect_table here
        process_effects_prepare(p, event, 0, chan);
        process_effects_prepare(p, event, 1, chan);
        play_line_arpgg_cleanup(p, event, chan);
        play_line_apply_global_fslide_row(p, event, chan);
        play_line_tremor_row_reset(p, event, chan);
    }

    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        event = &events[chan];
        process_effects(p, event, 0, chan);
    }

    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        event = &events[chan];
        process_effects(p, event, 1, chan);
    }

    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        for (int slot = 0; slot < 2; slot++) {
            if ((events[chan].eff[slot].def == 0) && (events[chan].eff[slot].val == 0)) {
                if ((p->ch->glfsld_table[slot][chan].def == 0) && (p->ch->glfsld_table[slot][chan].val == 0)) {
                    p->ch->effect_table[slot][chan].def = 0;
                    p->ch->effect_table[slot][chan].val = 0;
                }
            } else {
                p->ch->event_table[chan].eff[slot].def = events[chan].eff[slot].def;
                p->ch->event_table[chan].eff[slot].val = events[chan].eff[slot].val;
            }
        }
    }

    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        event = &events[chan];
        new_process_note(p, event, chan);

        check_swap_arp_vibr(p, event, 0, chan);
        check_swap_arp_vibr(p, event, 1, chan);

        update_fine_effects(p, 0, chan);
        update_fine_effects(p, 1, chan);
    }
}

static void generate_custom_vibrato(A2P *p, uint8_t value)
{
    const uint8_t vibtab_size[16] = { 16,16,16,16,32,32,32,32,64,64,64,64,128,128,128,128 };
    int idx, idx2;

    #define min0(VALUE) ((int)VALUE >= 0 ? (int)VALUE : 0)

    if (value == 0) {
        // 0: set default p->speed table
        p->vibtrem_table_size = def_vibtrem_table_size;
        memcpy(&p->vibtrem_table, &def_vibtrem_table, sizeof(p->vibtrem_table));
    } else if (value <= 239) {
        // 1-239: set custom p->speed table (fixed size = 32)
        p->vibtrem_table_size = def_vibtrem_table_size;
        double mul_r = (double)value / 16.0;

        for (idx2 = 0; idx2 <= 7; idx2++) {
            p->vibtrem_table[idx2 * 32] = 0;

            for (idx = 1; idx <= 16; idx++) {
                p->vibtrem_table[idx2 * 32 + idx] = (uint8_t)round(idx * mul_r);
            }

            for (idx = 17; idx <= 31; idx++) {
                p->vibtrem_table[idx2 * 32 + idx] = (uint8_t)round((32 - idx) * mul_r);
            }
        }
    } else {
        // 240-255: set custom p->speed table (p->speed factor = 1-4)
        p->vibtrem_speed_factor = (value - 240) % 4 + 1;
        p->vibtrem_table_size = 2 * vibtab_size[value - 240];
        int mul_b = 256 / (vibtab_size[value - 240]);

        for (idx2 = 0; idx2 <= 128 / vibtab_size[value - 240] - 1; idx2++) {
            p->vibtrem_table[2 * vibtab_size[value - 240] * idx2] = 0;

            for (idx = 1; idx <= vibtab_size[value - 240]; idx++) {
                p->vibtrem_table[2 * vibtab_size[value - 240] * idx2 + idx] =
                    min0(idx * mul_b - 1);
            }

            for (idx = vibtab_size[value - 240] + 1; idx <= 2 * vibtab_size[value - 240] - 1; idx++) {
                p->vibtrem_table[2 * vibtab_size[value - 240] * idx2 + idx] =
                    min0((2 * vibtab_size[value - 240] - idx) * mul_b - 1);
            }
        }
    }
}

static void check_swap_arp_vibr(A2P *p, tADTRACK2_EVENT *event, int slot, int chan)
{
    // Check if second effect is ZFF - force no restart
    bool is_norestart = (event->eff[slot ^ 1].def == ef_Extended) &&
                        (event->eff[slot ^ 1].val == (ef_ex_ExtendedCmd2 * 16 + ef_ex_cmd2_NoRestart));

    switch (event->eff[slot].def) {
    case ef_SwapArpeggio:
        if (is_norestart) {
            tARPEGGIO_TABLE *arp = get_arpeggio_table(p, event->eff[slot].val);
            uint8_t length = arp ? arp->length : 0;

            if (p->ch->macro_table[chan].arpg_pos > length)
                p->ch->macro_table[chan].arpg_pos = length;
            p->ch->macro_table[chan].arpg_table = event->eff[slot].val;
        } else {
            p->ch->macro_table[chan].arpg_count = 1;
            p->ch->macro_table[chan].arpg_pos = 0;
            p->ch->macro_table[chan].arpg_table = event->eff[slot].val;
            p->ch->macro_table[chan].arpg_note = p->ch->event_table[chan].note;
        }
        break;

    case ef_SwapVibrato:
        if (is_norestart) {
            tVIBRATO_TABLE *vib = get_vibrato_table(p, event->eff[slot].val);
            uint8_t length = vib ? vib->length : 0;

            if (p->ch->macro_table[chan].vib_pos > length)
                p->ch->macro_table[chan].vib_pos = length;
            p->ch->macro_table[chan].vib_table = event->eff[slot].val;
        } else {
            tVIBRATO_TABLE *vib = get_vibrato_table(p, p->ch->macro_table[chan].vib_table);
            uint8_t vib_delay = vib ? vib->delay : 0;

            p->ch->macro_table[chan].vib_count = 1;
            p->ch->macro_table[chan].vib_pos = 0;
            p->ch->macro_table[chan].vib_table = event->eff[slot].val;
            p->ch->macro_table[chan].vib_delay = vib_delay;
        }
        break;
    case ef_SetCustomSpeedTab:
        generate_custom_vibrato(p, event->eff[slot].val);
        break;
    }
}

static void portamento_up(A2P *p, int chan, uint16_t slide, uint16_t limit)
{
    uint16_t freq;

    if ((p->ch->freq_table[chan] & 0x1fff) == 0) return;

    freq = calc_freq_shift_up(p->ch->freq_table[chan] & 0x1fff, slide);

    change_frequency(p, chan, freq <= limit ? freq : limit);
}

static void portamento_down(A2P *p, int chan, uint16_t slide, uint16_t limit)
{
    uint16_t freq;

    if ((p->ch->freq_table[chan] & 0x1fff) == 0) return;

    freq = calc_freq_shift_down(p->ch->freq_table[chan] & 0x1fff, slide);

    change_frequency(p, chan, freq >= limit ? freq : limit);
}

static void macro_vibrato__porta_up(A2P *p, int chan, uint8_t depth)
{
    uint16_t freq = calc_freq_shift_up(p->ch->macro_table[chan].vib_freq & 0x1fff, depth);
    uint16_t newfreq = freq <= nFreq(12*8+1) ? freq : nFreq(12*8+1);

    change_freq(p, chan, newfreq);
}

static void macro_vibrato__porta_down(A2P *p, int chan, uint8_t depth)
{
    uint16_t freq = calc_freq_shift_down(p->ch->macro_table[chan].vib_freq & 0x1fff, depth);
    uint16_t newfreq = freq >= nFreq(0) ? freq : nFreq(0);

    change_freq(p, chan, newfreq);
}

static void tone_portamento(A2P *p, int slot, int chan)
{
    uint16_t freq = p->ch->freq_table[chan] & 0x1fff;
    /* Pascal tone_portamento: compare to porta_table.freq without masking (cf. a2player.pas). */
    uint16_t portafreq = p->ch->porta_table[slot][chan].freq;

    if (freq > portafreq) {
        portamento_down(p, chan, p->ch->porta_table[slot][chan].speed, portafreq);
    } else if (freq < portafreq) {
        portamento_up(p, chan, p->ch->porta_table[slot][chan].speed, portafreq);
    }
}

static void slide_carrier_volume_up(A2P *p, uint8_t chan, uint8_t slide, uint8_t limit)
{
    uint8_t volC = p->ch->fmpar_table[chan].volC;
    uint8_t newvolC = (volC - slide >= limit) ? volC - slide : limit;

    set_ins_volume(p, BYTE_NULL, newvolC, chan);
}

static void slide_modulator_volume_up(A2P *p, uint8_t chan, uint8_t slide, uint8_t limit)
{
    uint8_t volM = p->ch->fmpar_table[chan].volM;
    uint8_t newvolM = (volM - slide >= limit) ? volM - slide : limit;

    set_ins_volume(p, newvolM, BYTE_NULL, chan);
}

static void slide_volume_up(A2P *p, int chan, uint8_t slide)
{
    uint8_t limit1 = 0, limit2 = 0;
    t4OP_DATA d = get_4op_data(p, chan);

    if (!_4op_vol_valid_chan(p, chan)) {
        tINSTR_DATA *ins = get_instr_data(p, p->ch->event_table[chan].instr_def);

        limit1 = p->ch->peak_lock[chan] && ins ? ins->fm.volC : 0;
        limit2 = p->ch->peak_lock[chan] && ins ? ins->fm.volM : 0;
    }

    switch (p->ch->volslide_type[chan]) {
    case 0:
        if (!_4op_vol_valid_chan(p, chan)) {
            slide_carrier_volume_up(p, chan, slide, limit1);

            tINSTR_DATA *i = get_instr_data_by_ch(p, chan);
            if (i && (i->fm.connect || (p->percussion_mode && (chan >= 16))))  // in [17..20]
                slide_modulator_volume_up(p, chan, slide, limit2);
        } else {
            // Can use get_instr_data_by_ch(p)
            tINSTR_DATA *ins1 = get_instr_data(p, d.ins1);
            tINSTR_DATA *ins2 = get_instr_data(p, d.ins2);

            uint8_t limit1_volC = p->ch->peak_lock[d.ch1] && ins1 ? ins1->fm.volC : 0;
            uint8_t limit1_volM = p->ch->peak_lock[d.ch1] && ins1 ? ins1->fm.volM : 0;
            uint8_t limit2_volC = p->ch->peak_lock[d.ch2] && ins2 ? ins2->fm.volC : 0;
            uint8_t limit2_volM = p->ch->peak_lock[d.ch2] && ins2 ? ins2->fm.volM : 0;

            switch (d.conn) {
            // FM/FM
            case 0:
                slide_carrier_volume_up(p, d.ch1, slide, limit1_volC);
                break;
            // FM/AM
            case 1:
                slide_carrier_volume_up(p, d.ch1, slide, limit1_volC);
                slide_modulator_volume_up(p, d.ch2, slide, limit2_volM);
                break;
            // AM/FM
            case 2:
                slide_carrier_volume_up(p, d.ch1, slide, limit1_volC);
                slide_carrier_volume_up(p, d.ch2, slide, limit2_volC);
                break;
            // AM/AM
            case 3:
                slide_carrier_volume_up(p, d.ch1, slide, limit1_volC);
                slide_modulator_volume_up(p, d.ch1, slide, limit1_volM);
                slide_modulator_volume_up(p, d.ch2, slide, limit2_volM);
                break;
           }
        }
        break;

    case 1:
        slide_carrier_volume_up(p, chan, slide, limit1);
        break;

    case 2:
        slide_modulator_volume_up(p, chan, slide, limit2);
        break;

    case 3:
        slide_carrier_volume_up(p, chan, slide, limit1);
        slide_modulator_volume_up(p, chan, slide, limit2);
        break;
    }
}

static void slide_carrier_volume_down(A2P *p, uint8_t chan, uint8_t slide)
{
    uint8_t volC = p->ch->fmpar_table[chan].volC;
    uint8_t newvolC = volC + slide <= 63 ? volC + slide : 63;

    set_ins_volume(p, BYTE_NULL, newvolC, chan);
}

static void slide_modulator_volume_down(A2P *p, uint8_t chan, uint8_t slide)
{
    uint8_t volM = p->ch->fmpar_table[chan].volM;
    uint8_t newvolM = volM + slide <= 63 ? volM + slide : 63;

    set_ins_volume(p, newvolM, BYTE_NULL, chan);
}

static void slide_volume_down(A2P *p, int chan, uint8_t slide)
{
    t4OP_DATA d = get_4op_data(p, chan);

    switch (p->ch->volslide_type[chan]) {
    case 0:
        if (!_4op_vol_valid_chan(p, chan)) {
            slide_carrier_volume_down(p, chan, slide);

            tINSTR_DATA *i = get_instr_data_by_ch(p, chan);
            if (i && (i->fm.connect || (p->percussion_mode && (chan >= 16)))) { //in [17..20]
                slide_modulator_volume_down(p, chan, slide);
            }
        } else {
            switch (d.conn) {
            // FM/FM
            case 0:
                slide_carrier_volume_down(p, d.ch1, slide);
                break;
            // FM/AM
            case 1:
                slide_carrier_volume_down(p, d.ch1, slide);
                slide_modulator_volume_down(p, d.ch2, slide);
                break;
            // AM/FM
            case 2:
                slide_carrier_volume_down(p, d.ch1, slide);
                slide_carrier_volume_down(p, d.ch2, slide);
                break;
            // AM/AM
            case 3:
                slide_carrier_volume_down(p, d.ch1, slide);
                slide_modulator_volume_down(p, d.ch1, slide);
                slide_modulator_volume_down(p, d.ch2, slide);
                break;
            }
        }
        break;

    case 1:
        slide_carrier_volume_down(p, chan, slide);
        break;

    case 2:
        slide_modulator_volume_down(p, chan, slide);
        break;

    case 3:
        slide_carrier_volume_down(p, chan, slide);
        slide_modulator_volume_down(p, chan, slide);
        break;
    }
}

static void volume_slide(A2P *p, int chan, uint8_t up_speed, uint8_t down_speed)
{
    if (up_speed)
        slide_volume_up(p, chan, up_speed);
    else if (down_speed) {
        slide_volume_down(p, chan, down_speed);
    }
}

static void global_volume_slide(A2P *p, uint8_t up_speed, uint8_t down_speed)
{
    if (up_speed != BYTE_NULL)
        p->global_volume = max(p->global_volume + up_speed, 63);

    if (down_speed != BYTE_NULL) {
        if (p->global_volume >= down_speed)
            p->global_volume -= down_speed;
        else
            p->global_volume = 0;
    }

    set_global_volume(p);
}

static void arpeggio(A2P *p, int slot, int chan)
{
    static uint8_t arpgg_state[3] = {1, 2, 0};

    uint16_t freq;

    switch (p->ch->arpgg_table[slot][chan].state) {
    case 0: freq = nFreq(p->ch->arpgg_table[slot][chan].note - 1); break;
    case 1: freq = nFreq(p->ch->arpgg_table[slot][chan].note - 1 + p->ch->arpgg_table[slot][chan].add1); break;
    case 2: freq = nFreq(p->ch->arpgg_table[slot][chan].note - 1 + p->ch->arpgg_table[slot][chan].add2); break;
    default: freq = 0;
    }

    p->ch->arpgg_table[slot][chan].state = arpgg_state[p->ch->arpgg_table[slot][chan].state];
    change_frequency(p, chan, freq +
            get_instr_fine_tune(p, p->ch->event_table[chan].instr_def));
}

static void vibrato(A2P *p, int slot, int chan)
{
    uint16_t freq, slide;
    uint8_t direction;

    freq = p->ch->freq_table[chan];

    p->ch->vibr_table[slot][chan].pos += p->ch->vibr_table[slot][chan].speed * p->vibtrem_speed_factor;
    slide = calc_vibrato_shift(p, p->ch->vibr_table[slot][chan].depth, p->ch->vibr_table[slot][chan].pos);
    direction = p->ch->vibr_table[slot][chan].pos & p->vibtrem_table_size;
    p->ch->vibr_table[slot][chan].dir = (direction != 0) ? 1 : 0;

    if (direction == 0)
        portamento_down(p, chan, slide, nFreq(0));
    else
        portamento_up(p, chan, slide, nFreq(12*8+1));

    p->ch->freq_table[chan] = freq;
}

static void tremolo(A2P *p, int slot, int chan)
{
    uint16_t slide;
    uint8_t direction;

    uint8_t volM = p->ch->fmpar_table[chan].volM;
    uint8_t volC = p->ch->fmpar_table[chan].volC;

    p->ch->trem_table[slot][chan].pos += p->ch->trem_table[slot][chan].speed * p->vibtrem_speed_factor;
    slide = calc_vibrato_shift(p, p->ch->trem_table[slot][chan].depth, p->ch->trem_table[slot][chan].pos);
    direction = p->ch->trem_table[slot][chan].pos & p->vibtrem_table_size;
    p->ch->trem_table[slot][chan].dir = (direction != 0) ? 1 : 0;

    /* a2player.pas: tremolo uses calc_vibtrem_shift dir; tremolo2 uses (pos = 0) after Inc. */
    if (slot == 1) {
        if (p->ch->trem_table[slot][chan].pos == 0)
            slide_volume_down(p, chan, slide);
        else
            slide_volume_up(p, chan, slide);
    } else {
        direction = p->ch->trem_table[slot][chan].pos & p->vibtrem_table_size;
        if (direction == 0)
            slide_volume_down(p, chan, slide);
        else
            slide_volume_up(p, chan, slide);
    }

    // is this needed?
    p->ch->fmpar_table[chan].volM = volM;
    p->ch->fmpar_table[chan].volC = volC;
}

static int chanvol(A2P *p, int chan)
{
    char instr_fm_connect = get_fm_connect_by_chan(p, chan);

    if (instr_fm_connect == 0)
        return 63 - p->ch->fmpar_table[chan].volC;
    else
        return 63 - ROUND_DIV(p->ch->fmpar_table[chan].volM + p->ch->fmpar_table[chan].volC, 2);
}

static void update_effects_slot(A2P *p, int slot, int chan)
{
    uint8_t def = p->ch->effect_table[slot][chan].def;
    uint8_t val = p->ch->effect_table[slot][chan].val;

    switch (def) {
    case ef_Arpeggio:
        if (!val)
            break;

        arpeggio(p, slot, chan);
        break;

    case ef_ArpggVSlide:
        volume_slide(p, chan, val / 16, val % 16);
        arpeggio(p, slot, chan);
        break;

    case ef_ArpggVSlideFine:
        arpeggio(p, slot, chan);
        break;

    case ef_FSlideUp:
        portamento_up(p, chan, val, nFreq(12*8+1));
        break;

    case ef_FSlideDown:
        portamento_down(p, chan, val, nFreq(0));
        break;

    case ef_FSlideUpVSlide:
        portamento_up(p, chan, p->ch->fslide_table[slot][chan], nFreq(12*8+1));
        volume_slide(p, chan, val / 16, val % 16);
        break;

    case ef_FSlUpVSlF:
        portamento_up(p, chan, p->ch->fslide_table[slot][chan], nFreq(12*8+1));
        break;

    case ef_FSlideDownVSlide:
        portamento_down(p, chan, p->ch->fslide_table[slot][chan], nFreq(0));
        volume_slide(p, chan, val / 16, val % 16);
        break;

    case ef_FSlDownVSlF:
        portamento_down(p, chan, p->ch->fslide_table[slot][chan], nFreq(0));
        break;

    case ef_FSlUpFineVSlide:
        volume_slide(p, chan, val / 16, val % 16);
        break;

    case ef_FSlDownFineVSlide:
        volume_slide(p, chan, val / 16, val % 16);
        break;

    case ef_TonePortamento:
        tone_portamento(p, slot, chan);
        break;

    case ef_TPortamVolSlide:
        volume_slide(p, chan, val / 16, val % 16);
        tone_portamento(p, slot, chan);
        break;

    case ef_TPortamVSlideFine:
        tone_portamento(p, slot, chan);
        break;

    case ef_Vibrato:
        if (!p->ch->vibr_table[slot][chan].fine)
            vibrato(p, slot, chan);
        break;

    case ef_Tremolo:
        if (!p->ch->trem_table[slot][chan].fine)
            tremolo(p, slot, chan);
        break;

    case ef_VibratoVolSlide:
        volume_slide(p, chan, val / 16, val % 16);
        if (!p->ch->vibr_table[slot][chan].fine)
            vibrato(p, slot, chan);
        break;

    case ef_VibratoVSlideFine:
        if (!p->ch->vibr_table[slot][chan].fine)
            vibrato(p, slot, chan);
        break;

    case ef_VolSlide:
        volume_slide(p, chan, val / 16, val % 16);
        break;

    case ef_RetrigNote:
        if (p->ch->retrig_table[slot][chan] >= val) {
            p->ch->retrig_table[slot][chan] = 0;
            output_note(p, p->ch->event_table[chan].note, p->ch->event_table[chan].instr_def, chan, true, true);
        } else {
            p->ch->retrig_table[slot][chan]++;
        }
        break;

    case ef_MultiRetrigNote:
        if (p->ch->retrig_table[slot][chan] >= val / 16) {
            switch (val % 16) {
            case 0: break;
            case 8: break;

            case 1: slide_volume_down(p, chan, 1); break;
            case 2: slide_volume_down(p, chan, 2); break;
            case 3: slide_volume_down(p, chan, 4); break;
            case 4: slide_volume_down(p, chan, 8); break;
            case 5: slide_volume_down(p, chan, 16); break;

            case 9: slide_volume_up(p, chan, 1); break;
            case 10: slide_volume_up(p, chan, 2); break;
            case 11: slide_volume_up(p, chan, 4); break;
            case 12: slide_volume_up(p, chan, 8); break;
            case 13: slide_volume_up(p, chan, 16); break;

            case 6: slide_volume_down(p, chan, chanvol(p, chan) - chanvol(p, chan) * 2 / 3);
                break;

            case 7: slide_volume_down(p, chan, chanvol(p, chan) - chanvol(p, chan) * 1 / 2);
                break;

            case 14: slide_volume_up(p, chan, max(chanvol(p, chan) * 3 / 2 - chanvol(p, chan), 63));
                break;

            case 15: slide_volume_up(p, chan,max(chanvol(p, chan) * 2 - chanvol(p, chan), 63));
                break;
            }

            p->ch->retrig_table[slot][chan] = 0;
            output_note(p, p->ch->event_table[chan].note, p->ch->event_table[chan].instr_def, chan, true, true);
        } else {
            p->ch->retrig_table[slot][chan]++;
        }
        break;

    case ef_Tremor:
        if (p->ch->tremor_table[slot][chan].pos >= 0) {
            if ((p->ch->tremor_table[slot][chan].pos + 1) <= val / 16) {
                p->ch->tremor_table[slot][chan].pos++;
            } else {
                slide_volume_down(p, chan, 63);
                p->ch->tremor_table[slot][chan].pos = -1;
            }
        } else {
            if ((p->ch->tremor_table[slot][chan].pos - 1) >= -(val % 16)) {
                p->ch->tremor_table[slot][chan].pos--;
            } else {
                set_ins_volume(p, p->ch->tremor_table[slot][chan].volM, p->ch->tremor_table[slot][chan].volC, chan);
                p->ch->tremor_table[slot][chan].pos = 1;
            }
        }
        break;

    case ef_Extended2:
        switch (val / 16) {
        case ef_ex2_NoteDelay:
            if (p->ch->notedel_table[chan] == 0) {
                p->ch->notedel_table[chan] = BYTE_NULL;
                output_note(p, p->ch->event_table[chan].note, p->ch->event_table[chan].instr_def, chan, true, true);
            } else if (p->ch->notedel_table[chan] != BYTE_NULL) {
                p->ch->notedel_table[chan]--;
            }
            break;
        case ef_ex2_NoteCut:
            if (p->ch->notecut_table[chan] == 0) {
                p->ch->notecut_table[chan] = BYTE_NULL;
                key_off(p, chan);
            } else if (p->ch->notecut_table[chan] != BYTE_NULL) {
                p->ch->notecut_table[chan]--;
            }
            break;
        case ef_ex2_GlVolSlideUp: global_volume_slide(p, val & 0xf, BYTE_NULL); break;
        case ef_ex2_GlVolSlideDn: global_volume_slide(p, BYTE_NULL, val & 0xf); break;
        }
        break;
    }
}

static void update_effects(A2P *p)
{
    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        update_effects_slot(p, 0, chan);
        update_effects_slot(p, 1, chan);
    }
}

static void update_fine_effects(A2P *p, int slot, int chan)
{
    uint8_t def = p->ch->effect_table[slot][chan].def;
    uint8_t val = p->ch->effect_table[slot][chan].val;

    switch (def) {
    case ef_ArpggVSlideFine:    volume_slide(p, chan, val / 16, val % 16); break;
    case ef_FSlideUpFine:       portamento_up(p, chan, val, nFreq(12*8+1)); break;
    case ef_FSlideDownFine:     portamento_down(p, chan, val, nFreq(0)); break;
    case ef_FSlUpVSlF:          volume_slide(p, chan, val / 16, val % 16); break;
    case ef_FSlDownVSlF:        volume_slide(p, chan, val / 16, val % 16); break;
    case ef_FSlUpFineVSlide:    portamento_up(p, chan, p->ch->fslide_table[slot][chan], nFreq(12*8+1)); break;
    case ef_FSlDownFineVSlide:  portamento_down(p, chan, p->ch->fslide_table[slot][chan], nFreq(0)); break;

    case ef_FSlUpFineVSlF:
        portamento_up(p, chan, p->ch->fslide_table[slot][chan], nFreq(12*8+1));
        volume_slide(p, chan, val / 16, val % 16);
        break;

    case ef_FSlDownFineVSlF:
        portamento_down(p, chan, p->ch->fslide_table[slot][chan], nFreq(0));
        volume_slide(p, chan, val / 16, val % 16);
        break;

    case ef_TPortamVSlideFine:  volume_slide(p, chan, val / 16, val % 16); break;
    case ef_Vibrato:            if (p->ch->vibr_table[slot][chan].fine) vibrato(p, slot, chan); break;
    case ef_Tremolo:            if (p->ch->trem_table[slot][chan].fine) tremolo(p, slot, chan); break;
    case ef_VibratoVolSlide:    if (p->ch->vibr_table[slot][chan].fine) vibrato(p, slot, chan); break;

    case ef_VibratoVSlideFine:
        volume_slide(p, chan, val / 16, val % 16);
        if (p->ch->vibr_table[slot][chan].fine)
            vibrato(p, slot, chan);
        break;

    case ef_VolSlideFine:       volume_slide(p, chan, val / 16, val % 16); break;

    case ef_Extended2:
        switch(val / 16) {
        case ef_ex2_GlVolSlideUpF: global_volume_slide(p, val & 0xf, BYTE_NULL); break;
        case ef_ex2_GlVolSlideDnF: global_volume_slide(p, BYTE_NULL, val & 0xf); break;
        }
        break;
    }
}

static void update_extra_fine_effects_slot(A2P *p, int slot, int chan)
{
    uint8_t def = p->ch->effect_table[slot][chan].def;
    uint8_t val = p->ch->effect_table[slot][chan].val;

    switch (def) {
    case ef_Extended2:
        switch(val / 16) {
        case ef_ex2_GlVolSldUpXF:  global_volume_slide(p, val & 0xf, BYTE_NULL); break;
        case ef_ex2_GlVolSldDnXF:  global_volume_slide(p, BYTE_NULL, val & 0xf); break;
        case ef_ex2_VolSlideUpXF:  volume_slide(p, chan, val & 0xf, 0); break;
        case ef_ex2_VolSlideDnXF:  volume_slide(p, chan, 0, val & 0xf); break;
        case ef_ex2_FreqSlideUpXF: portamento_up(p, chan, val & 0xf, nFreq(12*8+1)); break;
        case ef_ex2_FreqSlideDnXF: portamento_down(p, chan, val & 0xf, nFreq(0)); break;
        }
        break;

    case ef_GlobalFreqSlideUpXF:    portamento_up(p, chan, val, nFreq(12*8+1)); break;
    case ef_GlobalFreqSlideDnXF:    portamento_down(p, chan, val, nFreq(0)); break;
    case ef_ExtraFineArpeggio:      arpeggio(p, slot, chan); break;
    case ef_ExtraFineVibrato:       if (!p->ch->vibr_table[slot][chan].fine) vibrato(p, slot, chan); break;
    case ef_ExtraFineTremolo:       if (!p->ch->trem_table[slot][chan].fine) tremolo(p, slot, chan); break;
    }
}

static void update_extra_fine_effects(A2P *p)
{
    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        update_extra_fine_effects_slot(p, 0, chan);
        update_extra_fine_effects_slot(p, 1, chan);
    }
}

static void set_current_order(A2P *p, uint8_t new_order)
{
    if (new_order >= 0x80) {
        
    }
    p->current_order = new_order < 0x80 ? new_order : 0;

    if (p->songinfo->pattern_order[p->current_order] < 0x80)
        return;

    // protect from circular jump to jump order command
    // if after 128 attempts of order jump we still land on jump command
    // then quit
    int i = 0;
    do {
        if (p->songinfo->pattern_order[p->current_order] > 0x7f) {
            uint8_t old_order = p->current_order;
            p->current_order = p->songinfo->pattern_order[p->current_order] - 0x80;
            if (p->current_order <= old_order)
                p->songend = true;
        }
        i++;
    } while (i < 128 && p->songinfo->pattern_order[p->current_order] > 0x7f);

    if (i >= 128) {
        
        p->songend = true;
        a2t_stop(p);
    }
}

static int calc_following_order(A2P *p, uint8_t order)
{
    int result;
    uint8_t index, jump_count;

    result = -1;
    index = order;
    jump_count = 0;

    do {
        if (p->songinfo->pattern_order[index] < 0x80) {
            result = index;
        } else {
            index = p->songinfo->pattern_order[index] - 0x80;
            jump_count++;
        }
    } while (!((jump_count > 0x7f) || (result != -1)));

    return result;
}

static void update_song_position(A2P *p)
{
    if ((p->current_line < p->songinfo->patt_len - 1) && !p->pattern_break) {
        p->current_line++;
    } else {
        bool do_pattern_loop =  p->pattern_break && ((p->next_line & 0xf0) == pattern_loop_flag);
        bool do_position_jump = p->pattern_break && ((p->next_line & 0xf0) == pattern_break_flag);

        if (do_pattern_loop) {
            // ZCx, ZDx - loop back
            uint8_t chan = p->next_line - pattern_loop_flag;
            p->next_line = p->ch->loopbck_table[chan];

            if (p->ch->loop_table[chan][p->current_line] != 0)
                p->ch->loop_table[chan][p->current_line]--;
        } else {
            // A bit overkill to clean arrays here, better do it in the end of pattern loop
            memset(p->ch->loopbck_table, BYTE_NULL, sizeof(p->ch->loopbck_table));
            memset(p->ch->loop_table, BYTE_NULL, sizeof(p->ch->loop_table));

            if (do_position_jump) {
                // Bxx - order position jump
                uint8_t old_order = p->current_order;

                uint8_t chan = p->next_line - pattern_break_flag;
                int slot = p->ch->event_table[chan].eff[0].def == ef_PositionJump ? 0 : 1;
                uint8_t val = p->ch->event_table[chan].eff[slot].val;

                set_current_order(p, val);

                if (p->current_order <= old_order)
                    p->songend = true;
                /* Reset global volume when jumping back to the start order (order 0)
                    adt2play doesn't do it, but mmori.a2m has global volume fade at the end */
                if (val == 0 && old_order != 0) {
                    p->global_volume = 63;
                    set_global_volume(p);
                }
                p->pattern_break = false;
            } else {
                int new_order = p->current_order < 0x7f ? p->current_order + 1 : 0;
                uint8_t old_order = p->current_order;
                set_current_order(p, new_order);
                /* Reset global volume when jumping back to the start order (order 0)
                    adt2play doesn't do it, but mmori.a2m has global volume fade at the end */
                if (p->current_order == 0 && old_order != 0) {
                    p->global_volume = 63;
                    set_global_volume(p);
                }
            }
        }

        if ((p->songinfo->pattern_order[p->current_order] > 0x7f))
            return;

        p->current_pattern = p->songinfo->pattern_order[p->current_order];
        if (!p->pattern_break) {
            p->current_line = 0;
        } else {
            p->pattern_break = false;
            p->current_line = p->next_line;
        }
    }

    for (int chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        p->ch->glfsld_table[0][chan].def = 0;
        p->ch->glfsld_table[0][chan].val = 0;
        p->ch->glfsld_table[1][chan].def = 0;
        p->ch->glfsld_table[1][chan].val = 0;
    }

    if (p->speed_update && p->current_line == 0 && p->current_order == calc_following_order(p, 0)) {
        p->tempo = p->songinfo->tempo;
        p->speed = p->songinfo->speed;
        update_timer(p, p->tempo);
    }
}

static void poll_proc(A2P *p)
{
    /* Pascal match: don't call a2t_stop(p) here. a2t_stop(p) is only called from
     * timer_poll_proc, but a2t_update_dump skips that path. If we a2t_stop(p) here,
     * subsequent IRQ p->ticks reprocess from reset state (order=0) and produce zeroed
     * frames that Pascal doesn't produce. The main loop exits on !p->songend instead. */
    if (p->pattern_delay) {
        update_effects(p);
        p->ticks++;
        if (p->tickD > 1) {
            p->tickD--;
        } else {
            p->tick0 = p->ticks;
            update_song_position(p);
            p->pattern_delay = false;
        }
    } else {
        if (p->ticks - p->tick0 + 1 >= p->speed) {
            play_line(p);
            update_effects(p);
            /* Pascal poll_proc: update_song_position only if NOT p->pattern_delay
             * (or fast_forward — not used in this port). Otherwise the row /
             * order advances immediately and p->tickD-based pattern delay never runs
             * (divergence vs adt2_dump, e.g. tunes/MLF/PINK.A2T). */
            if (!p->pattern_delay)
                update_song_position(p);
            p->tick0 = p->ticks;
        } else {
            update_effects(p);
            p->ticks++;
        }
    }

    p->tickXF++;
    if (p->tickXF % 4 == 0) {
        update_extra_fine_effects(p);
        p->tickXF -= 4;
    }
}

static void macro_poll_proc(A2P *p)
{
#define  IDLE       0xfff
#define  FINISHED   0xffff
    uint16_t chan;
    uint16_t finished_flag;

    for (chan = 0; chan < p->songinfo->nm_tracks; chan++) {
        finished_flag = p->ch->keyoff_loop[chan] ? IDLE : FINISHED;

        tCH_MACRO_TABLE *mt = &p->ch->macro_table[chan];
        tFMREG_TABLE *rt = get_fmreg_table(p, mt->fmreg_ins);

        bool force_macro_keyon = false;

        /* Pascal: when an instrument has no fmreg allocated (src[0]==0 and no header
         * links) or length==0, macro_poll_proc immediately terminates with
         * fmreg_pos→finished_flag (0xffff), never processing cell data.
         * C must match so inactive/idle channels show FINISHED instead of 0. */
        if (mt->fmreg_ins != 0 && (!rt || rt->length == 0)) {
            mt->fmreg_pos = finished_flag;
        }

        if (rt && rt->length && p->speed != 0) {
            if (mt->fmreg_duration > 1) {
                mt->fmreg_duration--;
            } else {
                if (mt->fmreg_pos <= rt->length) {
                    if (rt->loop_begin && rt->loop_length) {
                        if (mt->fmreg_pos == rt->loop_begin + rt->loop_length - 1) {
                            mt->fmreg_pos = rt->loop_begin;
                        } else {
                            if (mt->fmreg_pos < rt->length) {
                                mt->fmreg_pos++;
                            } else {
                                mt->fmreg_pos = finished_flag;
                            }
                        }
                    } else {
                        if (mt->fmreg_pos < rt->length) {
                            mt->fmreg_pos++;
                        } else {
                            mt->fmreg_pos = finished_flag;
                        }
                    }
                } else {
                    mt->fmreg_pos = finished_flag;
                }

                if (((p->ch->freq_table[chan] | 0x2000) == p->ch->freq_table[chan]) &&
                     (rt->keyoff_pos != 0) &&
                     (mt->fmreg_pos >= rt->keyoff_pos)) {
                    mt->fmreg_pos = IDLE;
                } else {
                    if (((p->ch->freq_table[chan] | 0x2000) != p->ch->freq_table[chan]) &&
                         (mt->fmreg_pos != 0) && (rt->keyoff_pos != 0) &&
                        ((mt->fmreg_pos < rt->keyoff_pos) || (mt->fmreg_pos == IDLE)))
                        mt->fmreg_pos = rt->keyoff_pos;
                }

                if (mt->fmreg_pos && mt->fmreg_pos != IDLE && mt->fmreg_pos != finished_flag) {
                    mt->fmreg_duration = rt->data[mt->fmreg_pos - 1].duration;

                    if (mt->fmreg_duration) {
                        tREGISTER_TABLE_DEF *d = &rt->data[mt->fmreg_pos - 1];
                        // NOTE: if we are already here, no need to call get_instr(p)
                        uint32_t disabled = p->instrinfo->instruments[mt->fmreg_ins - 1].dis_fmreg_cols;

                        // force KEY-ON with missing ADSR instrument data
                        force_macro_keyon = false;
                        if (mt->fmreg_pos == 1) {
                            uint32_t adsr_disabled = disabled & ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) |
                                (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15));
                            if (is_ins_adsr_data_empty(p, p->ch->voice_table[chan]) && !adsr_disabled) {
                                force_macro_keyon = true;
                            }
                        }

                        for (unsigned bit = 0; bit < 28; bit++) {
                            if (disabled & (1 << bit))
                                continue;

                            switch (bit) {
                            case 0:  p->ch->fmpar_table[chan].attckM = d->fm.attckM; break;
                            case 1:  p->ch->fmpar_table[chan].decM = d->fm.decM; break;
                            case 2:  p->ch->fmpar_table[chan].sustnM = d->fm.sustnM; break;
                            case 3:  p->ch->fmpar_table[chan].relM = d->fm.relM; break;
                            case 4:  p->ch->fmpar_table[chan].wformM = d->fm.wformM; break;
                            case 5:  set_ins_volume(p, 63 - d->fm.volM, BYTE_NULL, chan); break;
                            case 6:  p->ch->fmpar_table[chan].kslM = d->fm.kslM; break;
                            case 7:  p->ch->fmpar_table[chan].multipM = d->fm.multipM; break;
                            case 8:  p->ch->fmpar_table[chan].tremM = d->fm.tremM; break;
                            case 9:  p->ch->fmpar_table[chan].vibrM = d->fm.vibrM; break;
                            case 10: p->ch->fmpar_table[chan].ksrM = d->fm.ksrM; break;
                            case 11: p->ch->fmpar_table[chan].sustM = d->fm.sustM; break;
                            case 12: p->ch->fmpar_table[chan].attckC = d->fm.attckC; break;
                            case 13: p->ch->fmpar_table[chan].decC = d->fm.decC; break;
                            case 14: p->ch->fmpar_table[chan].sustnC = d->fm.sustnC; break;
                            case 15: p->ch->fmpar_table[chan].relC = d->fm.relC; break;
                            case 16: p->ch->fmpar_table[chan].wformC = d->fm.wformC; break;
                            case 17: set_ins_volume(p, BYTE_NULL, 63 - d->fm.volC, chan); break;
                            case 18: p->ch->fmpar_table[chan].kslC = d->fm.kslC; break;
                            case 19: p->ch->fmpar_table[chan].multipC = d->fm.multipC; break;
                            case 20: p->ch->fmpar_table[chan].tremC = d->fm.tremC; break;
                            case 21: p->ch->fmpar_table[chan].vibrC = d->fm.vibrC; break;
                            case 22: p->ch->fmpar_table[chan].ksrC = d->fm.ksrC; break;
                            case 23: p->ch->fmpar_table[chan].sustC = d->fm.sustC; break;
                            case 24: p->ch->fmpar_table[chan].connect = d->fm.connect; break;
                            case 25: p->ch->fmpar_table[chan].feedb = d->fm.feedb; break;
                            case 27: if (!p->ch->pan_lock[chan]) p->ch->panning_table[chan] = d->panning; break;
                            }
                        }

                        update_modulator_adsrw(p, chan);
                        update_carrier_adsrw(p, chan);
                        update_fmpar(p, chan);

                        // TODO: check if those flags are really set by the editor
                        uint8_t macro_flags = d->macro_flags;

                        if (force_macro_keyon || (macro_flags & 0x80)) { // MACRO_NOTE_RETRIG_FLAG
                            if (!((is_4op_chan(p, chan) && is_4op_chan_hi(p, chan)))) {
                                output_note(p, p->ch->event_table[chan].note,
                                            p->ch->event_table[chan].instr_def, chan, false, true);
                                if (is_4op_chan(p, chan) && is_4op_chan_lo(p, chan))
                                    init_macro_table(p, chan - 1, 0, p->ch->voice_table[chan - 1], 0);
                            }
                        } else if (macro_flags & 0x40) { // MACRO_ENVELOPE_RESTART_FLAG
                            key_on(p, chan);
                            change_freq(p, chan, p->ch->freq_table[chan]);
                        } else if (macro_flags & 0x20) { // MACRO_ZERO_FREQ_FLAG
                            if (p->ch->freq_table[chan]) {
                                p->ch->zero_fq_table[chan] = p->ch->freq_table[chan];
                                p->ch->freq_table[chan] &= ~0x1fff;
                                change_freq(p, chan, p->ch->freq_table[chan]);
                            }
                        } else if (p->ch->zero_fq_table[chan]) {
                            p->ch->freq_table[chan] = p->ch->zero_fq_table[chan];
                            p->ch->zero_fq_table[chan] = 0;
                            change_freq(p, chan, p->ch->freq_table[chan]);
                        }

                        int freq_slide = d->freq_slide;

                        if (!(disabled & (1 << 26))) {
                            if (freq_slide > 0) {
                                portamento_up(p, chan, freq_slide, nFreq(12*8+1));
                            } else if (freq_slide < 0) {
                                portamento_down(p, chan, abs(freq_slide), nFreq(0));
                            }
                        }
                    }
                }
            }
        }

        tARPEGGIO_TABLE *at = get_arpeggio_table(p, mt->arpg_table);

        if (at && at->length && at->speed) {
            if (mt->arpg_count == at->speed) {
                mt->arpg_count = 1;

                if (mt->arpg_pos <= at->length) {
                    if ((at->loop_begin != 0) && (at->loop_length != 0)) {
                        if (mt->arpg_pos == at->loop_begin + (at->loop_length - 1)) {
                            mt->arpg_pos = at->loop_begin;
                        } else {
                            if (mt->arpg_pos < at->length)
                                mt->arpg_pos++;
                            else
                                mt->arpg_pos = finished_flag;
                        }
                    } else {
                        if (mt->arpg_pos < at->length)
                            mt->arpg_pos++;
                        else
                            mt->arpg_pos = finished_flag;
                    }
                } else {
                    mt->arpg_pos = finished_flag;
                }

                if (((p->ch->freq_table[chan] | 0x2000) == p->ch->freq_table[chan]) &&
                     (at->keyoff_pos != 0) &&
                     (mt->arpg_pos >= at->keyoff_pos)) {
                    mt->arpg_pos = IDLE;
                } else {
                    if (((p->ch->freq_table[chan] | 0x2000) != p->ch->freq_table[chan]) &&
                         (at->keyoff_pos != 0) && (at->keyoff_pos != 0) &&
                        ((mt->arpg_pos < at->keyoff_pos) || (mt->arpg_pos == IDLE)))
                        mt->arpg_pos = at->keyoff_pos;
                }

                if ((mt->arpg_pos != 0) &&
                    (mt->arpg_pos != IDLE) && (mt->arpg_pos != finished_flag)) {
                    int8_t fine_tune = get_instr_fine_tune(p, p->ch->event_table[chan].instr_def);
                    uint8_t d = at->data[mt->arpg_pos - 1];

                    if (d == 0) {
                        change_frequency(p, chan, nFreq(mt->arpg_note - 1) + fine_tune);
                    } else if (d <= 96) {
                        // 1 - 96:
                        change_frequency(p, chan, nFreq(max(mt->arpg_note + at->data[mt->arpg_pos], 97) - 1) +
                            fine_tune);
                    } else if (d >= 0x80 && d <= 0x80+12*8+1) {
                        // 0x80 - 0x80+12*8+1:
                        change_frequency(p, chan, nFreq(at->data[mt->arpg_pos - 1] - 0x80 - 1) +
                            fine_tune);
                    }
                }
            } else {
                mt->arpg_count++;
            }
        }

        tVIBRATO_TABLE *vt = get_vibrato_table(p, mt->vib_table);

        if (vt && vt->length && vt->speed && !mt->vib_paused) {
            if (mt->vib_count == vt->speed) {
                if (mt->vib_delay != 0) {
                    mt->vib_delay--;
                } else {
                    mt->vib_count = 1;

                    if (mt->vib_pos <= vt->length) {
                        if ((vt->loop_begin != 0) && (vt->loop_length != 0)) {
                            if (mt->vib_pos == vt->loop_begin + (vt->loop_length-1)) {
                                mt->vib_pos = vt->loop_begin;
                            } else {
                                if (mt->vib_pos < vt->length)
                                    mt->vib_pos++;
                                else
                                    mt->vib_pos = finished_flag;
                            }
                        } else {
                            if (mt->vib_pos < vt->length)
                                mt->vib_pos++;
                            else
                                mt->vib_pos = finished_flag;
                        }
                    } else {
                        mt->vib_pos = finished_flag;
                    }

                    if (((p->ch->freq_table[chan] | 0x2000) == p->ch->freq_table[chan]) &&
                         (vt->keyoff_pos != 0) &&
                         (mt->vib_pos >= vt->keyoff_pos)) {
                        mt->vib_pos = IDLE;
                    } else {
                        if (((p->ch->freq_table[chan] | 0x2000) != p->ch->freq_table[chan]) &&
                             (mt->vib_pos != 0) && (vt->keyoff_pos != 0) &&
                            ((mt->vib_pos < vt->keyoff_pos) || (mt->vib_pos == IDLE)))
                            mt->vib_pos = vt->keyoff_pos;
                    }

                    if ((mt->vib_pos != 0) &&
                        (mt->vib_pos != IDLE) && (mt->vib_pos != finished_flag)) {
                        if (vt->data[mt->vib_pos - 1] > 0)
                            macro_vibrato__porta_up(p, chan, vt->data[mt->vib_pos - 1]);
                        else if (vt->data[mt->vib_pos - 1] < 0)
                            macro_vibrato__porta_down(p, chan, abs(vt->data[mt->vib_pos - 1]));
                        else
                            change_freq(p, chan, mt->vib_freq);
                    }
                }
            } else {
                mt->vib_count++;
            }
        }
    }
}

static void newtimer(A2P *p)
{
    if ((p->ticklooper == 0) && (p->irq_mode)) {
        poll_proc(p);
        if (p->IRQ_freq != p->tempo * _macro_speedup(p)) {
            p->IRQ_freq = (p->tempo < 18 ? 18 : p->tempo) * _macro_speedup(p);
        }
    }

    if ((p->macro_ticklooper == 0) && (p->irq_mode))
        macro_poll_proc(p);

    p->ticklooper++;
    if (p->ticklooper >= p->IRQ_freq / p->tempo)
        p->ticklooper = 0;

    p->macro_ticklooper++;
    if (p->macro_ticklooper >= p->IRQ_freq / (p->tempo * _macro_speedup(p)))
        p->macro_ticklooper = 0;
}



static void init_buffers(A2P *p)
{
    memset(p->ch, 0, sizeof(*p->ch));

    if (!p->lockvol) {
        memset(p->ch->volume_lock, 0, sizeof(p->ch->volume_lock));
    } else {
        for (int i = 0; i < 20; i++)
            p->ch->volume_lock[i] = (bool)((p->songinfo->lock_flags[i] >> 4) & 1);
    }

    if (!p->panlock) {
        memset(p->ch->panning_table, 0, sizeof(p->ch->panning_table));
        memset(p->ch->pan_lock, 0, sizeof(p->ch->pan_lock));
    } else {
        for (int i = 0; i < 20; i++) {
              p->ch->panning_table[i] = p->songinfo->lock_flags[i] & 3;
              p->ch->pan_lock[i] = true;
        }
    }

    if (!p->lockVP) {
        memset(p->ch->peak_lock, 0, sizeof(p->ch->peak_lock));
    } else {
        for (int i = 0; i < 20; i++)
              p->ch->peak_lock[i] = (bool)((p->songinfo->lock_flags[i] >> 5) & 1);
    }

    static uint8_t _4op_main_chan[6] = { 1, 3, 5, 10, 12, 14 }; // 0-based

    memset(p->ch->vol4op_lock, false, sizeof(p->ch->vol4op_lock));
    for (int i = 0; i < 6; i++) {
        p->ch->vol4op_lock[_4op_main_chan[i]] =
            ((p->songinfo->lock_flags[_4op_main_chan[i]] | 0x40) == p->songinfo->lock_flags[_4op_main_chan[i]]);
        p->ch->vol4op_lock[_4op_main_chan[i] - 1] =
            ((p->songinfo->lock_flags[_4op_main_chan[i] - 1] | 0x40) == p->songinfo->lock_flags[_4op_main_chan[i] - 1]);
    }

    for (int i = 0; i < 20; i++)
        p->ch->volslide_type[i] = (p->songinfo->lock_flags[i] >> 2) & 3;

    memset(p->ch->notedel_table, BYTE_NULL, sizeof(p->ch->notedel_table));
    memset(p->ch->notecut_table, BYTE_NULL, sizeof(p->ch->notecut_table));
    memset(p->ch->loopbck_table, BYTE_NULL, sizeof(p->ch->loopbck_table));
    memset(p->ch->loop_table, BYTE_NULL, sizeof(p->ch->loop_table));
}

static void init_player(A2P *p)
{
    opl2out(p, 0x01, 0);

    for (int i = 0; i < 18; i++)
        opl2out(p, 0xb0 + regoffs_n(p, i), 0);

    for (int i = 0x80; i <= 0x8d; i++)
        opl2out(p, i, BYTE_NULL);

    for (int i = 0x90; i <= 0x95; i++)
        opl2out(p, i, BYTE_NULL);

    p->misc_register = (p->tremolo_depth << 7) +
            (p->vibrato_depth << 6) +
            (p->percussion_mode << 5);

    opl2out(p, 0x01, 0x20);
    opl2out(p, 0x08, 0x40);
    opl3exp(p, 0x0105);
    opl3exp(p, 0x04 + (p->songinfo->flag_4op << 8));

    init_buffers(p);

    key_off(p, 16);
    key_off(p, 17);
    opl2out(p, 0xbd, p->misc_register);

    p->current_tremolo_depth = p->tremolo_depth;
    p->current_vibrato_depth = p->vibrato_depth;
    p->global_volume = 63;

    p->vibtrem_speed_factor = def_vibtrem_speed_factor;
    p->vibtrem_table_size = def_vibtrem_table_size;
    memcpy(&p->vibtrem_table, &def_vibtrem_table, sizeof(p->vibtrem_table));

    for (int i = 0; i < 20; i++) {
        p->ch->arpgg_table[0][i].state = 1;
        p->ch->arpgg_table[1][i].state = 1;
        p->ch->voice_table[i] = i + 1;
    }
}

static void a2t_stop(A2P *p)
{
    p->irq_mode = false;
    p->play_status = isStopped;
    p->global_volume = 63;
    p->current_tremolo_depth = p->tremolo_depth;
    p->current_vibrato_depth = p->vibrato_depth;
    p->pattern_break = false;
    p->current_order = 0;
    p->current_pattern = 0;
    p->current_line = 0;
    p->playback_speed_shift = 0;

    for (int i = 0; i < 20; i++)
        release_sustaining_sound(p, i);

    opl2out(p, 0xbd, 0);
    opl3exp(p, 0x0004);
    opl3exp(p, 0x0005);
    p->lockvol = false;
    p->panlock = false;
    p->lockVP = false;
    init_buffers(p);

    p->speed = 4;
    update_timer(p, 50);
}

/* Clean songinfo before importing a2t tune */


/* LOADER FOR A2M/A2T */


// read the variable part of the header








// common for both a2t/a2m









/* ---------------- public interface ---------------- */
A2P *a2p_new(const A2mSong *s, A2pWrite write, void *user)
{
    A2P *p = calloc(1, sizeof *p); if (!p) return NULL;
    p->ch = calloc(1, sizeof(tCHDATA)); if (!p->ch) { free(p); return NULL; }
    p->song = s; p->write = write; p->user = user;
    p->songinfo = (tSONGINFO *)&s->songinfo; p->instrinfo = (tINSTR_INFO *)&s->instrinfo; p->eventsinfo = (tEVENTS_INFO *)&s->eventsinfo;
    p->vibrato_table = s->vibrato_table; p->arpeggio_table = s->arpeggio_table; p->arpvib_count = s->arpvib_count;
    p->ffver = s->ffver; p->type = s->is_tiny;
    p->tempo = 50; p->speed = 6; p->macro_speedup = 1; p->IRQ_freq = 50; p->timer_fix = true;
    p->overall_volume = p->global_volume = p->fade_out_volume = 63;
    p->vibtrem_speed_factor = def_vibtrem_speed_factor; p->vibtrem_table_size = def_vibtrem_table_size; p->play_status = isStopped;
    p->speed_update = (p->songinfo->common_flag >> 0) & 1; p->lockvol = (p->songinfo->common_flag >> 1) & 1; p->lockVP = (p->songinfo->common_flag >> 2) & 1;
    p->tremolo_depth = (p->songinfo->common_flag >> 3) & 1; p->vibrato_depth = (p->songinfo->common_flag >> 4) & 1; p->panlock = (p->songinfo->common_flag >> 5) & 1;
    p->percussion_mode = (p->songinfo->common_flag >> 6) & 1; p->volume_scaling = (p->songinfo->common_flag >> 7) & 1;
    return p;
}

void a2p_free(A2P *p) { if (!p) return; free(p->ch); free(p); }

void a2p_rewind(A2P *p)
{
    p->chip = 0; p->curchip = 0;
    init_player(p);
    p->songend = false;
    set_current_order(p, 0);
    if (p->songinfo->pattern_order[p->current_order] > 0x7f) return;
    p->current_pattern = p->songinfo->pattern_order[p->current_order];
    p->current_line = 0; p->pattern_break = false; p->pattern_delay = false;
    p->tickXF = 0; p->ticks = 0; p->tick0 = 0; p->tickD = 0; p->next_line = 0;
    p->irq_mode = true; p->play_status = isPlaying;
    p->ticklooper = 0; p->macro_ticklooper = 0;
    p->speed = p->songinfo->speed; p->macro_speedup = p->songinfo->macro_speedup;
    update_timer(p, p->songinfo->tempo);
}

int a2p_update(A2P *p) { newtimer(p); return !p->songend; }
float a2p_refresh(const A2P *p) { return (float)p->tempo * _macro_speedup((A2P *)p); }
int a2p_position(const A2P *p, int *order, int *pattern, int *row, int *speed)
{
    if (order) *order = p->current_order; if (pattern) *pattern = p->current_pattern; if (row) *row = p->current_line; if (speed) *speed = p->speed;
    return 0;
}
