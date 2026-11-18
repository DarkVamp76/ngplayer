/* AdLib (OPL2) FM music: AMD (AMUSIC Adlib Tracker) and MAD (Mlat Adlib Tracker) loaders and a ProTracker-style replayer driving Nuked-OPL3.
 * The file layouts and the replay behaviour follow the formats as documented by the AdPlug project (LGPL), written here from scratch. GPL-3.0-or-later */
#include "opl_replay.h"
#include "opl/opl3.h"
#include "a2m_replay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t OP[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0a, 0x10, 0x11, 0x12 };    /* operator register offset of each of the nine channels */
static const uint16_t NOTETAB[12] = { 340, 363, 385, 408, 432, 458, 485, 514, 544, 577, 611, 647 };   /* F-numbers of one octave (SA2 table) */
static const uint8_t VIBTAB[32] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1 };

/* ---------------- loaders ---------------- */
static unsigned le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

int opl_probe(const uint8_t *b, size_t n) {
    if (n >= 4 + 9 * 20 + 4 && !memcmp(b, "MAD+", 4)) return 1;
    if (n >= 1072 && (!memcmp(b + 1062, "<o\xefQU\xeeRoR", 9) || !memcmp(b + 1062, "MaDoKaN96", 9))) return 1;
    return 0;
}

static void cell_decode_amd(OplCell *c, unsigned b0, unsigned b1, unsigned b2) {
    c->p1 = (uint8_t)((b0 & 0x7F) / 10); c->p2 = (uint8_t)((b0 & 0x7F) % 10);
    c->command = (uint8_t)(b1 & 15); c->inst = (uint8_t)(b1 >> 4);
    c->inst = (uint8_t)(c->inst + ((b2 & 1) << 4));
    c->note = (b2 >> 4) ? (uint8_t)(((b2 & 0x0E) >> 1) * 12 + (b2 >> 4)) : 0;       /* a note nibble of 0 means no note (the AMD writer is buggy there) */
}

static OplSong *load_amd(const uint8_t *b, size_t n, int *err) {
    OplSong *s = calloc(1, sizeof *s); if (!s) { *err = -2; return NULL; }
    memcpy(s->title, b, 24); memcpy(s->author, b + 24, 24);
    snprintf(s->kind, sizeof s->kind, "AMUSIC Adlib Tracker");
    static const uint8_t reorder[11] = { 1, 9, 3, 5, 7, 2, 10, 4, 6, 8, 0 };         /* file order -> OPL instrument data order */
    s->ninst = 26;
    for (int i = 0; i < 26; i++) {
        const uint8_t *q = b + 48 + (size_t)i * 34;
        memcpy(s->instname[i], q, 23); for (int j = 0; j < 23; j++) if ((uint8_t)s->instname[i][j] == 0xFF) s->instname[i][j] = ' ';
        for (int j = 0; j < 11; j++) s->inst[i][reorder[j]] = q[23 + j];
    }
    s->length = b[932]; s->nop = b[933] + 1;
    if (s->length == 0 || s->length > 128 || s->nop > 64) { *err = -3; free(s); return NULL; }
    memcpy(s->order, b + 934, 128);
    for (int i = 0; i < s->length; i++) if ((s->order[i] & 0x7F) >= 64) { *err = -4; free(s); return NULL; }
    s->nrows = 64; s->nchan = 9; s->decimal = 1; s->initspeed = 6; s->bpm = 50;
    s->ntracks = 64 * 9;
    s->tracks = calloc((size_t)s->ntracks * s->nrows, sizeof(OplCell)); s->trackord = calloc((size_t)s->nop * 9 + 9, 2);
    if (!s->tracks || !s->trackord) { *err = -2; opl_free(s); return NULL; }
    size_t pos = 1072, version = b[1071];
    if (version == 0x10) {                                           /* unpacked: the tracks follow in pattern order */
        for (int p = 0; p < s->nop; p++) for (int c = 0; c < 9; c++) s->trackord[p * 9 + c] = (uint16_t)(p * 9 + c + 1);
        for (int t = 0; t < s->nop * 9 && pos < n; t += 9)
            for (int r = 0; r < 64; r++) for (int c = t; c < t + 9; c++) {
                unsigned b0 = pos < n ? b[pos] : 0, b1 = pos + 1 < n ? b[pos + 1] : 0, b2 = pos + 2 < n ? b[pos + 2] : 0; pos += 3;
                cell_decode_amd(&s->tracks[(size_t)c * 64 + r], b0, b1, b2);
            }
    } else {                                                          /* packed: a track table, then the stored tracks with runs of empty rows */
        for (int p = 0; p < s->nop; p++) for (int c = 0; c < 9; c++) {
            unsigned v = pos + 1 < n ? le16(b + pos) + 1 : 0; pos += 2;
            s->trackord[p * 9 + c] = (uint16_t)(v > 64 * 9 ? 0 : v);
        }
        unsigned numtrax = pos + 1 < n ? le16(b + pos) : 0; pos += 2;
        for (unsigned k = 0; k < numtrax && pos < n; k++) {
            unsigned ti = pos + 1 < n ? le16(b + pos) : 0; pos += 2; if (ti > 64 * 9 - 1) ti = 64 * 9 - 1;
            for (int r = 0; r < 64 && pos < n; r++) {
                unsigned b0 = b[pos++];
                if (b0 & 0x80) { int len = b0 & 0x7F; if (len > 64 - r) len = 64 - r; memset(&s->tracks[(size_t)ti * 64 + r], 0, (size_t)len * sizeof(OplCell)); r += len - 1; continue; }
                unsigned b1 = pos < n ? b[pos] : 0, b2 = pos + 1 < n ? b[pos + 1] : 0; pos += 2;
                cell_decode_amd(&s->tracks[(size_t)ti * 64 + r], b0, b1, b2);
            }
        }
    }
    /* convert the effects to the ProTracker-based replay set */
    static const uint8_t convfx[10] = { 0, 1, 2, 9, 17, 11, 13, 18, 3, 14 };
    static const uint8_t convvol[64] = { 0x00,0x00,0x00,0x01,0x01,0x02,0x02,0x03, 0x03,0x04,0x04,0x05,0x05,0x06,0x06,0x07, 0x07,0x08,0x09,0x09,0x0a,0x0a,0x0b,0x0c,
        0x0c,0x0d,0x0e,0x0e,0x0f,0x10,0x10,0x11, 0x12,0x13,0x14,0x14,0x15,0x16,0x17,0x18, 0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x21,
        0x22,0x23,0x25,0x26,0x28,0x29,0x2b,0x2d, 0x2e,0x30,0x32,0x35,0x37,0x3a,0x3c,0x3f };
    for (size_t i = 0; i < (size_t)s->ntracks * 64; i++) {
        OplCell *c = &s->tracks[i];
        c->command = c->command < 10 ? convfx[c->command] : 0;
        if (c->command == 14) {
            if (c->p1 == 2) { c->command = 10; c->p1 = c->p2; c->p2 = 0; }
            if (c->p1 == 3) { c->command = 10; c->p1 = 0; }
        }
        if (c->command == 17) { unsigned v = (unsigned)c->p1 * 10 + c->p2; v = v < 64 ? convvol[v] : 63; c->p1 = (uint8_t)(v / 10); c->p2 = (uint8_t)(v % 10); }
    }
    return s;
}

static OplSong *load_mad(const uint8_t *b, size_t n, int *err) {
    OplSong *s = calloc(1, sizeof *s); if (!s) { *err = -2; return NULL; }
    snprintf(s->kind, sizeof s->kind, "Mlat Adlib Tracker"); s->is_mad = 1;
    static const uint8_t conv[10] = { 2, 1, 10, 9, 4, 3, 6, 5, 8, 7 };
    s->ninst = 9;
    for (int i = 0; i < 9; i++) {
        const uint8_t *q = b + 4 + (size_t)i * 20;
        memcpy(s->instname[i], q, 8);
        for (int j = 0; j < 10; j++) s->inst[i][conv[j]] = q[8 + j];
    }
    size_t pos = 4 + 9 * 20 + 1;
    s->length = b[pos]; s->nop = b[pos + 1]; int timer = b[pos + 2]; pos += 3;
    s->nrows = 32; s->nchan = 9; s->initspeed = 1; s->fixed_refresh = (double)timer;
    if (s->length == 0 || s->nop == 0 || pos + (size_t)s->nop * 32 * 9 + s->length > n) { *err = -3; free(s); return NULL; }
    s->ntracks = s->nop * 9;
    s->tracks = calloc((size_t)s->ntracks * s->nrows, sizeof(OplCell)); s->trackord = calloc((size_t)s->nop * 9, 2);
    if (!s->tracks || !s->trackord) { *err = -2; opl_free(s); return NULL; }
    for (int p = 0; p < s->nop; p++) for (int c = 0; c < 9; c++) s->trackord[p * 9 + c] = (uint16_t)(p * 9 + c + 1);
    for (int p = 0; p < s->nop; p++) for (int r = 0; r < 32; r++) for (int c = 0; c < 9; c++) {
        unsigned ev = b[pos++]; OplCell *cell = &s->tracks[(size_t)(p * 9 + c) * 32 + r];
        if (ev < 0x61) cell->note = (uint8_t)ev;
        if (ev == 0xFF) cell->command = 8;                           /* release */
        if (ev == 0xFE) cell->command = 13;                          /* pattern break */
    }
    for (int i = 0; i < s->length && i < 256; i++) s->order[i] = (uint8_t)(b[pos++] - 1);
    return s;
}

OplSong *opl_load(const uint8_t *b, size_t n, int *err) {
    int e = 0; if (!err) err = &e; *err = 0;
    if (!opl_probe(b, n)) { *err = -1; return NULL; }
    return !memcmp(b, "MAD+", 4) ? load_mad(b, n, err) : load_amd(b, n, err);
}

void opl_free(OplSong *s) { if (!s) return; free(s->trackord); free(s->tracks); free(s); }

/* ---------------- replayer ---------------- */
typedef struct {
    uint16_t freq, nextfreq;
    uint8_t oct, vol1, vol2, inst, fx, info1, info2, key, nextoct, note, portainfo, vibinfo1, vibinfo2;
    int8_t trigger;
} Chn;

struct OplPlayer {
    const OplSong *s; const A2mSong *a2s; A2P *a2; int a2map[20], a2n; opl3_chip chip; int rate;
    Chn ch[9];
    int speed, del, songend, regbd, ord, rw; unsigned tempo, restartpos;
    int rem; double acc;                 /* output samples left in the current tick, and the fractional remainder carried to the next one */
    int ended;
    unsigned mute; float *scope; int scope_len; unsigned scope_pos;
};

static void wr(OplPlayer *p, unsigned reg, unsigned v) { OPL3_WriteRegBuffered(&p->chip, (uint16_t)reg, (uint8_t)v);   /* buffered: key off and key on in one tick need a chip cycle in between */ }

static void set_volume(OplPlayer *p, int c) {
    const uint8_t *d = p->s->inst[p->ch[c].inst];
    wr(p, 0x40 + OP[c], 63 - p->ch[c].vol2 + (d[9] & 192));
    wr(p, 0x43 + OP[c], 63 - p->ch[c].vol1 + (d[10] & 192));
}
static void set_freq(OplPlayer *p, int c) {
    Chn *h = &p->ch[c];
    wr(p, 0xA0 + c, h->freq & 255);
    wr(p, 0xB0 + c, ((h->freq & 768) >> 8) + (h->oct << 2) + (h->key ? 32 : 0));
}
static void play_note(OplPlayer *p, int c) {
    Chn *h = &p->ch[c]; const uint8_t *d = p->s->inst[h->inst]; unsigned op = OP[c];
    wr(p, 0xB0 + c, 0);                                                /* stop the old note */
    wr(p, 0x20 + op, d[1]); wr(p, 0x23 + op, d[2]); wr(p, 0x60 + op, d[3]); wr(p, 0x63 + op, d[4]);
    wr(p, 0x80 + op, d[5]); wr(p, 0x83 + op, d[6]); wr(p, 0xE0 + op, d[7]); wr(p, 0xE3 + op, d[8]);
    wr(p, 0xC0 + c, d[0]); wr(p, 0xBD, 0);
    h->key = 1; set_freq(p, c); set_volume(p, c);
}
static void set_note(OplPlayer *p, int c, int note) {
    Chn *h = &p->ch[c];
    if (note == 127) { h->key = 0; set_freq(p, c); return; }
    if (note > 96) note = 96; else if (note < 1) note = 1;
    h->freq = NOTETAB[(note - 1) % 12]; h->oct = (uint8_t)((note - 1) / 12);
}
static void slide_down(Chn *h, int amount) {
    h->freq = (uint16_t)(h->freq - amount);
    if (h->freq <= 342) { if (h->oct) { h->oct--; h->freq = (uint16_t)(h->freq << 1); } else h->freq = 342; }
}
static void slide_up(Chn *h, int amount) {
    h->freq = (uint16_t)(h->freq + amount);
    if (h->freq >= 686) { if (h->oct < 7) { h->oct++; h->freq >>= 1; } else h->freq = 686; }
}
static void tone_porta(OplPlayer *p, int c, int info) {
    Chn *h = &p->ch[c];
    if (h->freq + (h->oct << 10) < h->nextfreq + (h->nextoct << 10)) {
        slide_up(h, info);
        if (h->freq + (h->oct << 10) > h->nextfreq + (h->nextoct << 10)) { h->freq = h->nextfreq; h->oct = h->nextoct; }
    }
    if (h->freq + (h->oct << 10) > h->nextfreq + (h->nextoct << 10)) {
        slide_down(h, info);
        if (h->freq + (h->oct << 10) < h->nextfreq + (h->nextoct << 10)) { h->freq = h->nextfreq; h->oct = h->nextoct; }
    }
    set_freq(p, c);
}
static void vibrato(OplPlayer *p, int c, int speed, int depth) {
    Chn *h = &p->ch[c];
    if (!speed || !depth) return;
    if (depth > 14) depth = 14;
    for (int i = 0; i < speed; i++) {
        h->trigger++; while (h->trigger >= 64) h->trigger -= 64;
        if (h->trigger >= 16 && h->trigger < 48) slide_down(h, VIBTAB[h->trigger - 16] / (16 - depth));
        if (h->trigger < 16) slide_up(h, VIBTAB[h->trigger + 16] / (16 - depth));
        if (h->trigger >= 48) slide_up(h, VIBTAB[h->trigger - 48] / (16 - depth));
    }
    set_freq(p, c);
}
static void vol_up(Chn *h, int a) { h->vol1 = h->vol1 + a < 63 ? (uint8_t)(h->vol1 + a) : 63; h->vol2 = h->vol2 + a < 63 ? (uint8_t)(h->vol2 + a) : 63; }
static void vol_down(Chn *h, int a) { h->vol1 = h->vol1 - a > 0 ? (uint8_t)(h->vol1 - a) : 0; h->vol2 = h->vol2 - a > 0 ? (uint8_t)(h->vol2 - a) : 0; }
static void vol_up_alt(OplPlayer *p, Chn *h, int a) { h->vol1 = h->vol1 + a < 63 ? (uint8_t)(h->vol1 + a) : 63; if (p->s->inst[h->inst][0] & 1) h->vol2 = h->vol2 + a < 63 ? (uint8_t)(h->vol2 + a) : 63; }
static void vol_down_alt(OplPlayer *p, Chn *h, int a) { h->vol1 = h->vol1 - a > 0 ? (uint8_t)(h->vol1 - a) : 0; if (p->s->inst[h->inst][0] & 1) h->vol2 = h->vol2 - a > 0 ? (uint8_t)(h->vol2 - a) : 0; }

static int resolve_order(OplPlayer *p) {
    if (p->ord < p->s->length) {
        while (p->s->order[p->ord] >= 0x80) {
            int neword = p->s->order[p->ord] - 0x80;
            if (neword <= p->ord) p->songend = 1;
            if (neword == p->ord) return 0;
            p->ord = neword;
        }
    } else { p->songend = 1; p->ord = (int)p->restartpos; }
    return 1;
}

static double refresh(const OplPlayer *p) {
    if (p->a2) return (double)a2p_refresh(p->a2);
    if (p->s->is_mad) return p->s->fixed_refresh;
    return p->tempo ? (double)p->tempo : (double)18.2f;      /* the PC timer default, as a float like the reference player */
}

static void reset_player(OplPlayer *p) {
    p->songend = p->del = p->ord = p->rw = p->regbd = 0; p->tempo = (unsigned)p->s->bpm; p->speed = p->s->initspeed;
    memset(p->ch, 0, sizeof p->ch);
    OPL3_Reset(&p->chip, (uint32_t)p->rate); wr(p, 1, 32);              /* OPL2 mode */
    if (p->s->is_mad) for (int i = 0; i < 9; i++) {                   /* default instruments */
        p->ch[i].inst = (uint8_t)i; p->ch[i].vol1 = (uint8_t)(63 - (p->s->inst[i][10] & 63)); p->ch[i].vol2 = (uint8_t)(63 - (p->s->inst[i][9] & 63));
    }
}

/* one timer tick: the effects of the running row, then a new row when the row delay has run out; returns 0 when the song has ended */
static int update(OplPlayer *p) {
    if (p->a2) return a2p_update(p->a2);
    const OplSong *s = p->s; int pattbreak = 0, pattern_delay = 0;
    if (!p->speed) return 0;
    for (int c = 0; c < 9; c++) {
        Chn *h = &p->ch[c]; int i1 = h->info1, i2 = h->info2, info = s->decimal ? i1 * 10 + i2 : (i1 << 4) + i2;
        switch (h->fx) {
        case 0: if (info) { if (h->trigger < 2) h->trigger++; else h->trigger = 0;
                switch (h->trigger) { case 0: set_note(p, c, h->note); break; case 1: set_note(p, c, h->note + i1); break; case 2: set_note(p, c, h->note + i2); }
                set_freq(p, c); } break;
        case 1: slide_up(h, info); set_freq(p, c); break;
        case 2: slide_down(h, info); set_freq(p, c); break;
        case 3: tone_porta(p, c, h->portainfo); break;
        case 4: vibrato(p, c, h->vibinfo1, h->vibinfo2); break;
        case 5: case 6:
            if (h->fx == 5) tone_porta(p, c, h->portainfo); else vibrato(p, c, h->vibinfo1, h->vibinfo2);
            /* fall through: the volume slide runs every fourth tick */
        case 10:
            if (p->del % 4) break;
            if (i1) vol_up(h, i1); else vol_down(h, i2);
            set_volume(p, c); break;
        case 14: if (i1 == 3 && !(p->del % (i2 + 1))) play_note(p, c); break;
        case 16: if (p->del % 4) break; if (i1) vol_up_alt(p, h, i1); else vol_down_alt(p, h, i2); set_volume(p, c); break;
        case 26: if (i1) vol_up(h, i1); else vol_down(h, i2); set_volume(p, c); break;
        }
    }
    if (p->del) { p->del--; return !p->songend; }
    if (!resolve_order(p)) return !p->songend;
    int pat = s->order[p->ord]; if (pat >= s->nop) { p->songend = 1; return 0; }
    int row = p->rw;
    for (int c = 0; c < 9; c++) {
        Chn *h = &p->ch[c]; unsigned tk = s->trackord[pat * 9 + c];
        if (!tk) continue;
        const OplCell *cell = &s->tracks[(size_t)(tk - 1) * s->nrows + row]; int donote = 0;
        if (cell->inst) {
            h->inst = (uint8_t)(cell->inst - 1); if (h->inst >= s->ninst) h->inst = 0;
            h->vol1 = (uint8_t)(63 - (s->inst[h->inst][10] & 63)); h->vol2 = (uint8_t)(63 - (s->inst[h->inst][9] & 63)); set_volume(p, c);
        }
        if (cell->note && cell->command != 3) {
            h->note = cell->note; set_note(p, c, cell->note); h->nextfreq = h->freq; h->nextoct = h->oct;
            if (cell->note != 127) donote = 1;
        }
        h->fx = cell->command; h->info1 = cell->p1; h->info2 = cell->p2;
        if (donote) play_note(p, c);
        int i1 = h->info1, i2 = h->info2, info = s->decimal ? i1 * 10 + i2 : (i1 << 4) + i2;
        switch (h->fx) {
        case 3:
            if (cell->note) {
                if (cell->note < 13) h->nextfreq = NOTETAB[cell->note - 1];
                else h->nextfreq = cell->note % 12 > 0 ? NOTETAB[(cell->note % 12) - 1] : NOTETAB[11];
                h->nextoct = (uint8_t)((cell->note - 1) / 12);
                if (cell->note == 127) { h->nextfreq = h->freq; h->nextoct = h->oct; }
            }
            if (info) h->portainfo = (uint8_t)info;
            break;
        case 4: if (info) { h->vibinfo1 = (uint8_t)i1; h->vibinfo2 = (uint8_t)i2; } break;
        case 7: p->tempo = (unsigned)info; break;
        case 8: h->key = 0; set_freq(p, c); break;
        case 9: if (i1) h->vol1 = (uint8_t)(i1 * 7); else h->vol2 = (uint8_t)(i2 * 7); set_volume(p, c); break;
        case 11: pattbreak = 1; p->rw = 0; if (info <= p->ord) p->songend = 1; p->ord = info; break;
        case 12: h->vol1 = h->vol2 = (uint8_t)(info > 63 ? 63 : info); set_volume(p, c); break;
        case 13: if (!pattbreak) { pattbreak = 1; p->rw = info < s->nrows ? info : 0; p->ord++; } break;
        case 14:
            switch (i1) {
            case 0: if (i2) p->regbd |= 128; else p->regbd &= 127; wr(p, 0xBD, (unsigned)p->regbd); break;
            case 1: if (i2) p->regbd |= 64; else p->regbd &= 191; wr(p, 0xBD, (unsigned)p->regbd); break;
            case 4: vol_up_alt(p, h, i2); set_volume(p, c); break;
            case 5: vol_down_alt(p, h, i2); set_volume(p, c); break;
            case 6: slide_up(h, i2); set_freq(p, c); break;
            case 7: slide_down(h, i2); set_freq(p, c); break;
            case 8: pattern_delay = i2 * p->speed; break;
            }
            break;
        case 15: if (info <= 0x1F) p->speed = info; if (info >= 0x32) p->tempo = (unsigned)info; if (!info) p->songend = 1; break;
        case 17: h->vol1 = (uint8_t)(info > 63 ? 63 : info); if (s->inst[h->inst][0] & 1) h->vol2 = (uint8_t)(info > 63 ? 63 : info); set_volume(p, c); break;
        case 18: if (info <= 31 && info > 0) p->speed = info; if (info > 31 || !info) p->tempo = (unsigned)info; break;
        }
    }
    p->del = p->speed - 1 + pattern_delay;
    if (!pattbreak) { p->rw++; if (p->rw >= s->nrows) { p->rw = 0; p->ord++; } }
    resolve_order(p);
    return !p->songend;
}

OplPlayer *opl_new(const OplSong *s, int rate) {
    OplPlayer *p = calloc(1, sizeof *p); if (!p) return NULL;
    p->s = s; p->rate = rate; p->restartpos = 0; reset_player(p);
    return p;
}
static void a2_write(void *u, int chip, int reg, int val) { OplPlayer *p = u; OPL3_WriteRegBuffered(&p->chip, (uint16_t)((chip << 8) | reg), (uint8_t)val); }
OplPlayer *opl_new_a2m(const A2mSong *s, int rate) {
    OplPlayer *p = calloc(1, sizeof *p); if (!p) return NULL;
    p->a2s = s; p->rate = rate; OPL3_Reset(&p->chip, (uint32_t)rate);
    p->a2 = a2p_new(s, a2_write, p); if (!p->a2) { free(p); return NULL; }
    p->a2n = s->songinfo.nm_tracks > 20 ? 20 : s->songinfo.nm_tracks;
    a2p_rewind(p->a2);                                           /* the chip is reset above, the replayer programs it */
    for (int c = 0; c < p->a2n; c++) p->a2map[c] = a2p_opl_channel(p->a2, c);
    return p;
}
void opl_pfree(OplPlayer *p) { if (p && p->a2) a2p_free(p->a2); free(p); }

int opl_position(const OplPlayer *p, int *order, int *row, int *speed) {
    if (p->a2) { int pat; return a2p_position(p->a2, order, &pat, row, speed); }
    if (order) *order = p->ord;
    if (row) *row = p->rw;
    if (speed) *speed = p->speed;
    return 0;
}

void opl_set_mute(OplPlayer *p, unsigned mask) {
    if (!p->a2) { p->mute = mask & 0x1FF; OPL3_SetMute(&p->chip, p->mute); return; }
    unsigned m = 0; for (int c = 0; c < p->a2n; c++) if (((mask >> c) & 1) && p->a2map[c] >= 0) m |= 1u << p->a2map[c];     /* AT2 channel -> OPL3 channel */
    p->mute = mask; OPL3_SetMute(&p->chip, m);
}
void opl_set_scope(OplPlayer *p, float *buf, int len) { p->scope = buf; p->scope_len = len; p->scope_pos = 0; }
unsigned opl_scope_pos(const OplPlayer *p) { return p->scope_pos; }

int opl_render(OplPlayer *p, int16_t *out, int n) {
    int done = 0;
    while (done < n && !p->ended) {
        if (p->rem <= 0) {                                             /* a new timer tick: the whole samples of its length, the fraction is carried over */
            if (!update(p)) p->ended = 1;
            p->acc += (double)((float)p->rate / (float)refresh(p)); p->rem = (int)p->acc; p->acc -= p->rem;    /* single precision like the reference player */
            if (p->rem <= 0) continue;
        }
        int chunk = p->rem > n - done ? n - done : p->rem;
        for (int i = 0; i < chunk; i++) {
            int16_t f[2]; OPL3_GenerateResampled(&p->chip, f); out[2 * (done + i)] = f[0]; out[2 * (done + i) + 1] = f[1];
            if (p->scope) {                                            /* the output of each FM channel, before the (mono) panning */
                const int16_t *co = OPL3_ChannelOutput(&p->chip); unsigned w = p->scope_pos & (unsigned)(p->scope_len - 1);
                if (p->a2) { for (int c = 0; c < p->a2n; c++) p->scope[(size_t)c * p->scope_len + w] = p->a2map[c] >= 0 ? (float)co[p->a2map[c]] * (1.0f / 16384.0f) : 0.0f; }
                else for (int c = 0; c < 9; c++) p->scope[(size_t)c * p->scope_len + w] = (float)co[c] * (1.0f / 16384.0f);
                p->scope_pos++;
            }
        }
        p->rem -= chunk; done += chunk;
    }
    return done;
}
