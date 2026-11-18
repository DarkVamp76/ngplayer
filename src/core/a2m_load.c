/* Adlib Tracker 2 module loader (.a2m, file versions 9-14). The layout of the unpacked song data follows the AT2 file format as used by Adlib Tracker II and documented by
 * the AdPlug project. GPL-3.0-or-later */
#include "a2m_load.h"
#include "a2m_unpack.h"
#include <stdlib.h>
#include <string.h>

static unsigned u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static unsigned u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

int a2m_probe(const uint8_t *b, size_t n) {
    if (n > 16 && !memcmp(b, "_A2module_", 10)) return b[14] >= 1 && b[14] <= 14;
    if (n > 23 && !memcmp(b, "_A2tiny_module_", 15)) return b[19] >= 1 && b[19] <= 14;
    return 0;
}

static void fm_from_raw(tFM_INST_DATA *f, const uint8_t *s) {
    f->multipM = s[0] & 15; f->ksrM = (s[0] >> 4) & 1; f->sustM = (s[0] >> 5) & 1; f->vibrM = (s[0] >> 6) & 1; f->tremM = (s[0] >> 7) & 1;
    f->multipC = s[1] & 15; f->ksrC = (s[1] >> 4) & 1; f->sustC = (s[1] >> 5) & 1; f->vibrC = (s[1] >> 6) & 1; f->tremC = (s[1] >> 7) & 1;
    f->volM = s[2] & 0x3F; f->kslM = (s[2] >> 6) & 3; f->volC = s[3] & 0x3F; f->kslC = (s[3] >> 6) & 3;
    f->decM = s[4] & 15; f->attckM = (s[4] >> 4) & 15; f->decC = s[5] & 15; f->attckC = (s[5] >> 4) & 15;
    f->relM = s[6] & 15; f->sustnM = (s[6] >> 4) & 15; f->relC = s[7] & 15; f->sustnC = (s[7] >> 4) & 15;
    f->wformM = s[8] & 7; f->wformC = s[9] & 7; f->connect = s[10] & 1; f->feedb = (s[10] >> 1) & 7;
}

static int all_zero(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) if (p[i]) return 0; return 1; }
static void pascal_str(char *dst, const uint8_t *src, size_t max) { size_t l = src[0]; if (l > max) l = max; memcpy(dst, src + 1, l); dst[l] = 0; }

#define SD_SIZE       1138338u
#define SD_NAMES      86
#define SD_INST       11051
#define SD_FMREG      14621
#define SD_ARPVIB     991526
#define SD_ORDER      1124381
#define SD_TEMPO      1124509
#define SD_DISCOLS    1130042

void a2m_free(A2mSong *s) {
    if (!s) return;
    if (s->instrinfo.instruments) { for (unsigned i = 0; i < s->instrinfo.count; i++) free(s->instrinfo.instruments[i].fmreg); free(s->instrinfo.instruments); }
    if (s->vibrato_table) { for (unsigned i = 0; i < s->arpvib_count; i++) free(s->vibrato_table[i]); free(s->vibrato_table); }
    if (s->arpeggio_table) { for (unsigned i = 0; i < s->arpvib_count; i++) free(s->arpeggio_table[i]); free(s->arpeggio_table); }
    free(s->eventsinfo.events); free(s);
}

A2mSong *a2m_load(const uint8_t *b, size_t n, int *err) {
    int e = 0; if (!err) err = &e; *err = 0;
    if (!a2m_probe(b, n)) { *err = -1; return NULL; }
    if (memcmp(b, "_A2module_", 10)) { *err = -2; return NULL; }                 /* the tiny (.a2t) variant is not loaded yet */
    int ver = b[14], npatt = b[15];
    if (ver < 9) { *err = -3; return NULL; }                                      /* versions 1 to 8 use other packers and layouts: not loaded yet */
    size_t pos = 16; unsigned len[17];
    if (pos + 68 > n) { *err = -4; return NULL; }
    for (int i = 0; i < 17; i++) len[i] = u32(b + pos + 4 * i);
    pos += 68;
    int unpk = ver <= 11;                                                          /* 9-11 aPlib, 12-14 LZH */
    if (len[0] > n - pos) { *err = -4; return NULL; }
    uint8_t *sd = calloc(1, SD_SIZE); if (!sd) { *err = -5; return NULL; }
    long got = unpk ? a2m_aplib_depack(b + pos, len[0], sd, SD_SIZE) : a2m_lzh_depack(b + pos, len[0], sd, SD_SIZE);
    if (got < (long)SD_TEMPO) { free(sd); *err = -6; return NULL; }
    pos += len[0];

    A2mSong *s = calloc(1, sizeof *s); if (!s) { free(sd); *err = -5; return NULL; }
    tSONGINFO *si = &s->songinfo;
    s->ffver = ver; pascal_str(si->songname, sd, 42); pascal_str(si->composer, sd + 43, 42);
    int count = 255; while (count && all_zero(sd + SD_INST + (size_t)(count - 1) * 14, 14)) count--;
    for (int i = 0; i < 255; i++) pascal_str(si->instr_names[i], sd + SD_NAMES + (size_t)i * 43, 42);
    s->instrinfo.count = (unsigned)count; s->instrinfo.size = (size_t)count * sizeof(tINSTR_DATA_EXT);
    s->instrinfo.instruments = calloc((size_t)(count ? count : 1), sizeof(tINSTR_DATA_EXT));
    if (!s->instrinfo.instruments) { free(sd); a2m_free(s); *err = -5; return NULL; }
    for (int i = 0; i < count; i++) {
        const uint8_t *q = sd + SD_INST + (size_t)i * 14; tINSTR_DATA *d = &s->instrinfo.instruments[i].instr_data;
        fm_from_raw(&d->fm, q); d->panning = q[11] & 3; if (d->panning >= 3) d->panning = 0;
        d->fine_tune = (int8_t)q[12]; d->perc_voice = q[13];
    }
    for (int i = 0; i < count; i++) {                                              /* instrument macros: a table without length and without links is left out */
        const uint8_t *q = sd + SD_FMREG + (size_t)i * 3831; tINSTR_DATA_EXT *ie = &s->instrinfo.instruments[i];
        int links = (q[1] | q[2] | q[3] | q[4] | q[5]) != 0;
        if (!(q[0] || links)) continue;
        tFMREG_TABLE *t = calloc(1, sizeof *t); if (!t) { free(sd); a2m_free(s); *err = -5; return NULL; } ie->fmreg = t;
        t->length = q[0]; t->loop_begin = q[1]; t->loop_length = q[2]; t->keyoff_pos = q[3]; t->arpeggio_table = q[4]; t->vibrato_table = q[5];
        for (int k = 0; k < 255; k++) {
            const uint8_t *r = q + 6 + (size_t)k * 15; tREGISTER_TABLE_DEF *d = &t->data[k];
            fm_from_raw(&d->fm, r); d->freq_slide = (int16_t)(r[11] | (r[12] << 8)); d->panning = r[13]; d->duration = r[14]; d->macro_flags = r[10] & 0xF0;
        }
        ie->arpeggio = t->arpeggio_table; ie->vibrato = t->vibrato_table;
    }
    s->arpvib_count = 255;                                                          /* all 255 table slots, whatever the number of instruments */
    s->arpeggio_table = calloc(255, sizeof(tARPEGGIO_TABLE *)); s->vibrato_table = calloc(255, sizeof(tVIBRATO_TABLE *));
    if (!s->arpeggio_table || !s->vibrato_table) { free(sd); a2m_free(s); *err = -5; return NULL; }
    for (int i = 0; i < 255; i++) {
        const uint8_t *q = sd + SD_ARPVIB + (size_t)i * 521;
        if (q[0]) { tARPEGGIO_TABLE *a = calloc(1, sizeof *a); s->arpeggio_table[i] = a; a->length = q[0]; a->speed = q[1]; a->loop_begin = q[2]; a->loop_length = q[3]; a->keyoff_pos = q[4]; memcpy(a->data, q + 5, 255); }
        if (q[260]) { tVIBRATO_TABLE *v = calloc(1, sizeof *v); s->vibrato_table[i] = v; v->length = q[260]; v->speed = q[261]; v->delay = q[262]; v->loop_begin = q[263]; v->loop_length = q[264]; v->keyoff_pos = q[265]; memcpy(v->data, q + 266, 255); }
    }
    for (int i = 0; i < count; i++) {                                              /* disabled macro columns */
        const uint8_t *q = sd + SD_DISCOLS + (size_t)i * 28; uint32_t m = 0; for (int k = 0; k < 28; k++) m |= (uint32_t)(q[k] & 1) << k; s->instrinfo.instruments[i].dis_fmreg_cols = m;
    }
    memcpy(si->pattern_order, sd + SD_ORDER, 128);
    si->tempo = sd[SD_TEMPO]; si->speed = sd[SD_TEMPO + 1]; si->common_flag = sd[SD_TEMPO + 2];
    si->patt_len = (uint16_t)u16(sd + SD_TEMPO + 3); si->nm_tracks = sd[SD_TEMPO + 5]; si->macro_speedup = (uint16_t)u16(sd + SD_TEMPO + 6); si->flag_4op = sd[SD_TEMPO + 8];
    memcpy(si->lock_flags, sd + SD_TEMPO + 9, 20);
    si->bpm_rows_per_beat = sd[1138335]; si->bpm_tempo_finetune = (int16_t)u16(sd + 1138336);
    free(sd);
    if (si->patt_len < 1 || si->patt_len > 256 || si->nm_tracks < 1 || si->nm_tracks > 20) { a2m_free(s); *err = -7; return NULL; }

    /* patterns: 16 blocks of 8 patterns, each 20 tracks * 256 rows * 6 bytes in the file */
    int rows = si->patt_len, chans = si->nm_tracks;
    tEVENTS_INFO *ei = &s->eventsinfo; ei->patterns = npatt; ei->rows = rows; ei->channels = chans;
    ei->size = (size_t)(npatt ? npatt : 1) * chans * rows * sizeof(tADTRACK2_EVENT); ei->events = calloc(1, ei->size);
    uint8_t *grp = calloc(8, 20 * 256 * 6); if (!ei->events || !grp) { free(grp); a2m_free(s); *err = -5; return NULL; }
    for (int g = 0; g < 16; g++) {
        unsigned L = len[1 + g]; if (!L) continue;
        if (L > n - pos) { free(grp); a2m_free(s); *err = -8; return NULL; }
        memset(grp, 0, 8 * 20 * 256 * 6);
        if (unpk) a2m_aplib_depack(b + pos, L, grp, 8 * 20 * 256 * 6); else a2m_lzh_depack(b + pos, L, grp, 8 * 20 * 256 * 6);
        pos += L;
        for (int p = 0; p < 8; p++) {
            if (g * 8 + p >= npatt) break;
            for (int c = 0; c < chans; c++) for (int r = 0; r < rows; r++) {
                const uint8_t *q = grp + (size_t)p * 20 * 256 * 6 + ((size_t)c * 256 + r) * 6;
                tADTRACK2_EVENT *d = &ei->events[(size_t)(g * 8 + p) * chans * rows + (size_t)c * rows + r];
                d->note = q[0]; d->instr_def = q[1]; d->eff[0].def = q[2]; d->eff[0].val = q[3]; d->eff[1].def = q[4]; d->eff[1].val = q[5];
            }
        }
    }
    free(grp);
    return s;
}
