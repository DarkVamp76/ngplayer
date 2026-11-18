/* Adlib Tracker 2 module loader (.a2m and .a2t, file versions 1-14). The layout of the unpacked song data follows the AT2 file format as used by Adlib Tracker II and
 * documented by the AdPlug project; the conversion of the old (versions 1-4) effects after the AdPlug loader. GPL-3.0-or-later */
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

/* unpacked data of file versions 9-14 as one block (A2M): offsets in the song data */
#define SD_SIZE       1138338u
#define SD_NAMES      86
#define SD_INST       11051
#define SD_FMREG      14621
#define SD_ARPVIB     991526
#define SD_ORDER      1124381
#define SD_TEMPO      1124509
#define SD_DISCOLS    1130042
/* file versions 1-8 */
#define OLD_SD_SIZE   11717u
#define OLD_NAMES     86
#define OLD_INST      8336
#define OLD_ORDER     11586
#define OLD_TEMPO     11714
#define FMREG_SIZE    3831u
#define ARPVIB_SIZE   521u

void a2m_free(A2mSong *s) {
    if (!s) return;
    if (s->instrinfo.instruments) { for (unsigned i = 0; i < s->instrinfo.count; i++) free(s->instrinfo.instruments[i].fmreg); free(s->instrinfo.instruments); }
    if (s->vibrato_table) { for (unsigned i = 0; i < s->arpvib_count; i++) free(s->vibrato_table[i]); free(s->vibrato_table); }
    if (s->arpeggio_table) { for (unsigned i = 0; i < s->arpvib_count; i++) free(s->arpeggio_table[i]); free(s->arpeggio_table); }
    free(s->eventsinfo.events); free(s);
}

/* the packer of a file version */
static long depack(int ver, const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    switch (ver) {
    case 1: case 5: return a2m_sixpack_depack(src, n, dst, cap);
    case 2: case 6: return a2m_lzw_depack(src, n, dst, cap);
    case 3: case 7: return a2m_lzss_depack(src, n, dst, cap);
    case 4: case 8: { size_t k = n < cap ? n : cap; memcpy(dst, src, k); return (long)k; }      /* stored */
    case 9: case 10: case 11: return a2m_aplib_depack(src, n, dst, cap);
    default: return a2m_lzh_depack(src, n, dst, cap);
    }
}

static A2mSong *new_song(int ver, int tiny) {
    A2mSong *s = calloc(1, sizeof *s); if (!s) return NULL;
    s->ffver = ver; s->is_tiny = tiny;
    s->songinfo.patt_len = 64; s->songinfo.nm_tracks = 18; s->songinfo.macro_speedup = 1;
    memset(s->songinfo.pattern_order, 0x80, sizeof s->songinfo.pattern_order);
    s->arpvib_count = 255;                                         /* all 255 table slots, whatever the number of instruments; entries without a table are NULL */
    s->arpeggio_table = calloc(255, sizeof(tARPEGGIO_TABLE *)); s->vibrato_table = calloc(255, sizeof(tVIBRATO_TABLE *));
    if (!s->arpeggio_table || !s->vibrato_table) { a2m_free(s); return NULL; }
    return s;
}

/* the instrument records: slots * size bytes (size 13 up to version 8, 14 from version 9) */
static int build_instruments(A2mSong *s, const uint8_t *base, int slots, int size) {
    int count = slots; while (count && all_zero(base + (size_t)(count - 1) * size, (size_t)size)) count--;
    s->instrinfo.count = (unsigned)count; s->instrinfo.size = (size_t)count * sizeof(tINSTR_DATA_EXT);
    s->instrinfo.instruments = calloc((size_t)(count ? count : 1), sizeof(tINSTR_DATA_EXT));
    if (!s->instrinfo.instruments) return -1;
    for (int i = 0; i < count; i++) {
        const uint8_t *q = base + (size_t)i * size; tINSTR_DATA *d = &s->instrinfo.instruments[i].instr_data;
        fm_from_raw(&d->fm, q); d->panning = q[11] & 3; if (d->panning >= 3) d->panning = 0;
        d->fine_tune = (int8_t)q[12]; d->perc_voice = size >= 14 ? q[13] : 0;
    }
    return 0;
}

/* instrument macros (version 9 and later): 255 tables of 3831 bytes; one without length and without links is left out */
static int build_fmreg(A2mSong *s, const uint8_t *base) {
    for (unsigned i = 0; i < s->instrinfo.count; i++) {
        const uint8_t *q = base + (size_t)i * FMREG_SIZE; tINSTR_DATA_EXT *ie = &s->instrinfo.instruments[i];
        int links = (q[1] | q[2] | q[3] | q[4] | q[5]) != 0;
        if (!(q[0] || links)) continue;
        tFMREG_TABLE *t = calloc(1, sizeof *t); if (!t) return -1; ie->fmreg = t;
        t->length = q[0]; t->loop_begin = q[1]; t->loop_length = q[2]; t->keyoff_pos = q[3]; t->arpeggio_table = q[4]; t->vibrato_table = q[5];
        for (int k = 0; k < 255; k++) {
            const uint8_t *r = q + 6 + (size_t)k * 15; tREGISTER_TABLE_DEF *d = &t->data[k];
            fm_from_raw(&d->fm, r); d->freq_slide = (int16_t)(r[11] | (r[12] << 8)); d->panning = r[13]; d->duration = r[14]; d->macro_flags = r[10] & 0xF0;
        }
        ie->arpeggio = t->arpeggio_table; ie->vibrato = t->vibrato_table;
    }
    return 0;
}
static int build_arpvib(A2mSong *s, const uint8_t *base) {
    for (int i = 0; i < 255; i++) {
        const uint8_t *q = base + (size_t)i * ARPVIB_SIZE;
        if (q[0]) { tARPEGGIO_TABLE *a = calloc(1, sizeof *a); if (!a) return -1; s->arpeggio_table[i] = a; a->length = q[0]; a->speed = q[1]; a->loop_begin = q[2]; a->loop_length = q[3]; a->keyoff_pos = q[4]; memcpy(a->data, q + 5, 255); }
        if (q[260]) { tVIBRATO_TABLE *v = calloc(1, sizeof *v); if (!v) return -1; s->vibrato_table[i] = v; v->length = q[260]; v->speed = q[261]; v->delay = q[262]; v->loop_begin = q[263]; v->loop_length = q[264]; v->keyoff_pos = q[265]; memcpy(v->data, q + 266, 255); }
    }
    return 0;
}
static void build_disabled(A2mSong *s, const uint8_t *base) {                /* disabled macro columns: 28 flags per instrument */
    for (unsigned i = 0; i < s->instrinfo.count; i++) {
        const uint8_t *q = base + (size_t)i * 28; uint32_t m = 0; for (int k = 0; k < 28; k++) m |= (uint32_t)(q[k] & 1) << k; s->instrinfo.instruments[i].dis_fmreg_cols = m;
    }
}

/* ---------------- patterns ---------------- */
enum { ef_FSlideUp = 1, ef_FSlideDown = 2, ef_TonePortamento = 3, ef_Vibrato = 4, ef_TPortamVolSlide = 5, ef_VibratoVolSlide = 6, ef_FSlideUpFine = 7, ef_FSlideDownFine = 8,
       ef_SetModulatorVol = 9, ef_VolSlide = 10, ef_PositionJump = 11, ef_SetInsVolume = 12, ef_PatternBreak = 13, ef_SetTempo = 14, ef_SetSpeed = 15, ef_SetCarrierVol = 18,
       ef_SetWaveform = 19, ef_VolSlideFine = 20, ef_RetrigNote = 21, ef_Extended = 35, ef_Extended2 = 36 };
enum { ex_SetTremDepth = 0, ex_SetVibDepth = 1, ex_AttckM = 2, ex_DecayM = 3, ex_SustnM = 4, ex_RelM = 5, ex_AttckC = 6, ex_DecayC = 7, ex_SustnC = 8, ex_RelC = 9, ex_SetFeedback = 10, ex_ExtendedCmd2 = 15 };
enum { ex2_FineTuneUp = 4, ex2_FineTuneDown = 5 };
enum { cmd2_RSS = 0, cmd2_LockVol = 2, cmd2_UnlockVol = 3, cmd2_LockVP = 4, cmd2_UnlockVP = 5, cmd2_VSlide_mod = 6, cmd2_VSlide_car = 7, cmd2_VSlide_def = 8 };

/* the effects of file versions 1-4 (one effect per cell) as the later numbering; adsr[] remembers per channel whether the ADSR effects address the carrier */
static void convert_v1234_effects(tADTRACK2_EVENT *ev, int chan, uint8_t *adsr) {
    uint8_t *def = &ev->eff[0].def, *val = &ev->eff[0].val; uint8_t v = *val;
    switch (*def) {
    case 0x00: *def = 0; break;                                    /* arpeggio is 0x80 in the new numbering, 0 here means "none"; the value stays */
    case 0x01: *def = ef_FSlideUp; break;
    case 0x02: *def = ef_FSlideDown; break;
    case 0x03: *def = ef_FSlideUpFine; break;
    case 0x04: *def = ef_FSlideDownFine; break;
    case 0x05: *def = ef_TonePortamento; break;
    case 0x06: *def = ef_TPortamVolSlide; break;
    case 0x07: *def = ef_Vibrato; break;
    case 0x08: *def = ef_VibratoVolSlide; break;
    case 0x09:
        if (v & 0xf0) { *def = ef_SetCarrierVol; *val = (uint8_t)((v >> 4) * 4 + 3); }
        else if (v & 0x0f) { *def = ef_SetModulatorVol; *val = (uint8_t)((v & 0x0f) * 4 + 3); }
        else *def = 0;
        break;
    case 0x0a: *def = ef_SetInsVolume; break;
    case 0x0b: *def = ef_PatternBreak; break;
    case 0x0c: *def = ef_PositionJump; break;
    case 0x0d: *def = ef_SetSpeed; break;
    case 0x0e: *def = ef_SetTempo; break;
    case 0x0f:
        switch (v >> 4) {
        case 0x00: *def = ef_Extended; *val = (uint8_t)((ex_SetTremDepth << 4) | (v & 0x0f)); break;
        case 0x01: *def = ef_Extended; *val = (uint8_t)((ex_SetVibDepth << 4) | (v & 0x0f)); break;
        case 0x02: *def = ef_SetWaveform; *val = (v & 0x0f) < 4 ? (uint8_t)(((v & 0x0f) << 4) | 0x0f) : (uint8_t)(((v & 0x0f) - 4) | 0xf0); break;
        case 0x03: *def = ef_Extended2; *val = (uint8_t)((ex2_FineTuneUp << 4) | (v & 0x0f)); break;
        case 0x04: *def = ef_Extended2; *val = (uint8_t)((ex2_FineTuneDown << 4) | (v & 0x0f)); break;
        case 0x05: *def = ef_VolSlide; *val = (uint8_t)((v & 0x0f) << 4); break;
        case 0x06: *def = ef_VolSlide; *val = (uint8_t)(v & 0x0f); break;
        case 0x07: *def = ef_VolSlideFine; *val = (uint8_t)((v & 0x0f) << 4); break;
        case 0x08: *def = ef_VolSlideFine; *val = (uint8_t)(v & 0x0f); break;
        case 0x09: *def = ef_RetrigNote; *val = (uint8_t)((v & 0x0f) + 1); break;
        case 0x0a: *def = ef_Extended; *val = (uint8_t)((v & 0x0f) | ((adsr[chan] ? ex_AttckC : ex_AttckM) << 4)); break;
        case 0x0b: *def = ef_Extended; *val = (uint8_t)((v & 0x0f) | ((adsr[chan] ? ex_DecayC : ex_DecayM) << 4)); break;
        case 0x0c: *def = ef_Extended; *val = (uint8_t)((v & 0x0f) | ((adsr[chan] ? ex_SustnC : ex_SustnM) << 4)); break;
        case 0x0d: *def = ef_Extended; *val = (uint8_t)((v & 0x0f) | ((adsr[chan] ? ex_RelC : ex_RelM) << 4)); break;
        case 0x0e: *def = ef_Extended; *val = (uint8_t)((ex_SetFeedback << 4) | (v & 0x0f)); break;
        case 0x0f:
            *def = ef_Extended; *val = (uint8_t)(ex_ExtendedCmd2 << 4);
            switch (v & 0x0f) {
            case 0: *val |= cmd2_RSS; break;
            case 1: *val |= cmd2_LockVol; break;
            case 2: *val |= cmd2_UnlockVol; break;
            case 3: *val |= cmd2_LockVP; break;
            case 4: *val |= cmd2_UnlockVP; break;
            case 5: *def = 0; *val = 0; adsr[chan] = 1; break;
            case 6: *def = 0; *val = 0; adsr[chan] = 0; break;
            case 7: *val |= cmd2_VSlide_car; break;
            case 8: *val |= cmd2_VSlide_mod; break;
            case 9: *val |= cmd2_VSlide_def; break;
            default: *def = 0; *val = 0; break;
            }
            break;
        }
        break;
    default: *def = 0; *val = 0; break;
    }
}

/* Reads the pattern blocks that start at len[sidx]: A2M files have the song data in len[0] and the patterns from len[1], A2T files later. */
static int read_patterns(A2mSong *s, const uint32_t *len, int sidx, const uint8_t *src, size_t size, int npatt) {
    int ver = s->ffver, rows = s->songinfo.patt_len, chans = s->songinfo.nm_tracks;
    tEVENTS_INFO *ei = &s->eventsinfo; ei->patterns = npatt; ei->rows = rows; ei->channels = chans;
    ei->size = (size_t)(npatt ? npatt : 1) * chans * rows * sizeof(tADTRACK2_EVENT); ei->events = calloc(1, ei->size);
    if (!ei->events) return -5;
    int groups = ver < 5 ? 4 : ver < 9 ? 8 : 16;
    size_t per = ver < 5 ? 16u * 64 * 9 * 4 : ver < 9 ? 8u * 18 * 64 * 4 : 8u * 20 * 256 * 6;      /* bytes of one unpacked block */
    uint8_t *blk = calloc(1, per), adsr[32] = { 0 };
    if (!blk) return -5;
    for (int g = 0; g < groups; g++) {
        unsigned L = len[sidx + g]; if (!L) continue;
        if (L > size) { free(blk); return -8; }
        memset(blk, 0, per);
        depack(ver, src, L, blk, per);
        src += L; size -= L;
        if (ver < 5) {                                                /* [4][16][64 rows][9 channels][4 bytes] */
            for (int p = 0; p < 16; p++) {
                int pat = g * 16 + p; if (pat >= npatt) break;
                for (int r = 0; r < 64; r++) for (int c = 0; c < 9; c++) {
                    const uint8_t *q = blk + (size_t)p * (64 * 9 * 4) + ((size_t)r * 9 + c) * 4;
                    tADTRACK2_EVENT *d = &ei->events[((size_t)pat * chans + c) * rows + r];
                    d->note = q[0]; d->instr_def = q[1]; d->eff[0].def = q[2]; d->eff[0].val = q[3];
                    convert_v1234_effects(d, c, adsr);
                }
            }
        } else if (ver < 9) {                                         /* [8][8][18 channels][64 rows][4 bytes] */
            for (int p = 0; p < 8; p++) {
                int pat = g * 8 + p; if (pat >= npatt) break;
                for (int c = 0; c < 18; c++) for (int r = 0; r < 64; r++) {
                    const uint8_t *q = blk + (size_t)p * (18 * 64 * 4) + ((size_t)c * 64 + r) * 4;
                    tADTRACK2_EVENT *d = &ei->events[((size_t)pat * chans + c) * rows + r];
                    d->note = q[0]; d->instr_def = q[1];
                    if (q[2] == 22) {                                 /* manual fine slide: fine tune up / down */
                        d->eff[0].def = ef_Extended2;
                        d->eff[0].val = q[3] / 16 ? (uint8_t)((ex2_FineTuneUp << 4) | (q[3] / 16)) : (uint8_t)((ex2_FineTuneDown << 4) | (q[3] % 16));
                    } else { d->eff[0].def = q[2]; d->eff[0].val = q[3]; }
                }
            }
        } else {                                                      /* [16][8][20 channels][256 rows][6 bytes] */
            for (int p = 0; p < 8; p++) {
                int pat = g * 8 + p; if (pat >= npatt) break;
                for (int c = 0; c < chans; c++) for (int r = 0; r < rows; r++) {
                    const uint8_t *q = blk + (size_t)p * 20 * 256 * 6 + ((size_t)c * 256 + r) * 6;
                    tADTRACK2_EVENT *d = &ei->events[((size_t)pat * chans + c) * rows + r];
                    d->note = q[0]; d->instr_def = q[1]; d->eff[0].def = q[2]; d->eff[0].val = q[3]; d->eff[1].def = q[4]; d->eff[1].val = q[5];
                }
            }
        }
    }
    free(blk);
    return 0;
}

/* ---------------- A2M ---------------- */
static A2mSong *load_a2m(const uint8_t *b, size_t n, int *err) {
    int ver = b[14], npatt = b[15];
    size_t pos = 16; uint32_t len[21] = { 0 };
    int lensize = ver < 5 ? 5 : ver < 9 ? 9 : 17;
    size_t need = (size_t)lensize * (ver < 9 ? 2 : 4);
    if (pos + need > n) { *err = -4; return NULL; }
    if (ver < 9) {
        unsigned maxblock = (unsigned)((ver < 5 ? npatt / 16 : npatt / 8) + 1);
        for (int i = 0; i < lensize && (unsigned)i <= maxblock; i++) len[i] = u16(b + pos + 2 * i);          /* the rest may be rubbish (MARIO.A2M) */
    } else for (int i = 0; i < lensize; i++) len[i] = u32(b + pos + 4 * i);
    pos += need;
    if (len[0] > n - pos) { *err = -4; return NULL; }
    size_t cap = ver < 9 ? OLD_SD_SIZE : SD_SIZE;
    uint8_t *sd = calloc(1, cap); if (!sd) { *err = -5; return NULL; }
    long got = depack(ver, b + pos, len[0], sd, cap);
    if (got < (ver < 9 ? 1 : (long)SD_TEMPO)) { free(sd); *err = -6; return NULL; }
    pos += len[0];
    A2mSong *s = new_song(ver, 0); if (!s) { free(sd); *err = -5; return NULL; }
    tSONGINFO *si = &s->songinfo;
    pascal_str(si->songname, sd, 42); pascal_str(si->composer, sd + 43, 42);
    if (ver < 9) {
        for (int i = 0; i < 250; i++) pascal_str(si->instr_names[i], sd + OLD_NAMES + (size_t)i * 33, 32);
        if (build_instruments(s, sd + OLD_INST, 250, 13)) { free(sd); a2m_free(s); *err = -5; return NULL; }
        memcpy(si->pattern_order, sd + OLD_ORDER, 128);
        si->tempo = sd[OLD_TEMPO]; si->speed = sd[OLD_TEMPO + 1];
        if (ver > 4) si->common_flag = sd[OLD_TEMPO + 2];
    } else {
        for (int i = 0; i < 255; i++) pascal_str(si->instr_names[i], sd + SD_NAMES + (size_t)i * 43, 42);
        if (build_instruments(s, sd + SD_INST, 255, 14) || build_fmreg(s, sd + SD_FMREG) || build_arpvib(s, sd + SD_ARPVIB)) { free(sd); a2m_free(s); *err = -5; return NULL; }
        build_disabled(s, sd + SD_DISCOLS);
        memcpy(si->pattern_order, sd + SD_ORDER, 128);
        si->tempo = sd[SD_TEMPO]; si->speed = sd[SD_TEMPO + 1]; si->common_flag = sd[SD_TEMPO + 2];
        si->patt_len = (uint16_t)u16(sd + SD_TEMPO + 3); si->nm_tracks = sd[SD_TEMPO + 5]; si->macro_speedup = (uint16_t)u16(sd + SD_TEMPO + 6); si->flag_4op = sd[SD_TEMPO + 8];
        memcpy(si->lock_flags, sd + SD_TEMPO + 9, 20);
        si->bpm_rows_per_beat = sd[1138335]; si->bpm_tempo_finetune = (int16_t)u16(sd + 1138336);
    }
    free(sd);
    if (si->patt_len < 1 || si->patt_len > 256 || si->nm_tracks < 1 || si->nm_tracks > 20) { a2m_free(s); *err = -7; return NULL; }
    int r = read_patterns(s, len, 1, b + pos, n - pos, npatt);
    if (r) { a2m_free(s); *err = r; return NULL; }
    return s;
}

/* ---------------- A2T (the "tiny" module: a header, then the blocks one after the other) ---------------- */
static A2mSong *load_a2t(const uint8_t *b, size_t n, int *err) {
    int ver = b[19], npatt = b[20];
    size_t pos = 23; uint32_t len[21] = { 0 };
    A2mSong *s = new_song(ver, 1); if (!s) { *err = -5; return NULL; }
    tSONGINFO *si = &s->songinfo; si->tempo = b[21]; si->speed = b[22];
    const uint8_t *v = b + pos; size_t avail = n - pos, vsz = 0;
    if (ver < 5) {
        vsz = 12; if (vsz > avail) goto bad;
        for (int i = 0; i < 6; i++) len[i] = u16(v + 2 * i);
    } else if (ver < 9) {
        vsz = 21; if (vsz > avail) goto bad;
        si->common_flag = v[0]; for (int i = 0; i < 10; i++) len[i] = u16(v + 1 + 2 * i);
    } else if (ver == 9) {
        vsz = 86; if (vsz > avail) goto bad;
        si->common_flag = v[0]; si->patt_len = (uint16_t)u16(v + 1); si->nm_tracks = v[3]; si->macro_speedup = (uint16_t)u16(v + 4);
        for (int i = 0; i < 20; i++) len[i] = u32(v + 6 + 4 * i);
    } else {
        vsz = ver == 10 ? 107 : 111; if (vsz > avail) goto bad;
        si->common_flag = v[0]; si->patt_len = (uint16_t)u16(v + 1); si->nm_tracks = v[3]; si->macro_speedup = (uint16_t)u16(v + 4);
        si->flag_4op = v[6]; memcpy(si->lock_flags, v + 7, 20);
        for (int i = 0; i < (ver == 10 ? 20 : 21); i++) len[i] = u32(v + 27 + 4 * i);
    }
    pos += vsz;
    if (si->patt_len < 1 || si->patt_len > 256 || si->nm_tracks < 1 || si->nm_tracks > 20) { *err = -7; a2m_free(s); return NULL; }
    {   /* the instruments (and, from version 12, the 4-operator table and reserved data before them) */
        int slots = ver < 9 ? 250 : 255, isz = ver < 9 ? 13 : 14;
        size_t extra = ver >= 12 ? 3 + 129 + 1024 : 0, cap = (size_t)slots * isz + extra;
        if (len[0] > n - pos) goto bad;
        uint8_t *u = calloc(1, cap); if (!u) { *err = -5; a2m_free(s); return NULL; }
        depack(ver, b + pos, len[0], u, cap);
        const uint8_t *p = u + (ver == 14 ? 3 : 0) + (ver >= 12 ? 129 + 1024 : 0);
        int rc = build_instruments(s, p, slots, isz);
        free(u); if (rc) { *err = -5; a2m_free(s); return NULL; }
        pos += len[0];
    }
    if (ver >= 9) {                                                  /* macros, arpeggio / vibrato tables, disabled columns */
        if (len[1] > n - pos) goto bad;
        uint8_t *fm = calloc(255, FMREG_SIZE); if (!fm) { *err = -5; a2m_free(s); return NULL; }
        depack(ver, b + pos, len[1], fm, 255u * FMREG_SIZE); pos += len[1];
        int rc = build_fmreg(s, fm); free(fm); if (rc) { *err = -5; a2m_free(s); return NULL; }
        if (len[2] > n - pos) goto bad;
        uint8_t *av = calloc(255, ARPVIB_SIZE); if (!av) { *err = -5; a2m_free(s); return NULL; }
        depack(ver, b + pos, len[2], av, 255u * ARPVIB_SIZE); pos += len[2];
        rc = build_arpvib(s, av); free(av); if (rc) { *err = -5; a2m_free(s); return NULL; }
        if (ver >= 11) {
            if (len[3] > n - pos) goto bad;
            uint8_t *dc = calloc(255, 28); if (!dc) { *err = -5; a2m_free(s); return NULL; }
            depack(ver, b + pos, len[3], dc, 255u * 28); pos += len[3];
            build_disabled(s, dc); free(dc);
        }
    }
    {   /* the order list, then the pattern blocks */
        static const int blocknum[14] = { 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 4, 4, 4, 4 }, blockstart[14] = { 2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 5, 5, 5, 5 };
        unsigned L = len[blocknum[ver - 1]]; if (L > n - pos) goto bad;
        depack(ver, b + pos, L, si->pattern_order, 128); pos += L;
        int r = read_patterns(s, len, blockstart[ver - 1], b + pos, n - pos, npatt);
        if (r) { *err = r; a2m_free(s); return NULL; }
    }
    return s;
bad:
    *err = -4; a2m_free(s); return NULL;
}

A2mSong *a2m_load(const uint8_t *b, size_t n, int *err) {
    int e = 0; if (!err) err = &e; *err = 0;
    if (!a2m_probe(b, n)) { *err = -1; return NULL; }
    return !memcmp(b, "_A2module_", 10) ? load_a2m(b, n, err) : load_a2t(b, n, err);
}
