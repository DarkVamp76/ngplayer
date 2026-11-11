/* ULT loader (UltraTracker 1.0 - 1.6). Fills the same XSong the S3M loader produces, so the S3M replayer plays it.
 * Layout: "MAS_UTrack_V00" + version char, 32-byte title, song text (n * 32 bytes), sample headers (64 bytes,
 * 66 from V004 on), 256 orders, channel/pattern count (value - 1), pan table (V003+), RLE-packed pattern events
 * stored channel by channel (all patterns of channel 0, then channel 1, ...), then signed 8/16-bit sample data.
 * Every event carries two effects; this core has one effect plus the volume column, so volume (C) goes to the
 * volume column and the more important of the other effects is kept. GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static unsigned rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

int ult_probe(const uint8_t *b, size_t n){
    return n >= 0x100 && !memcmp(b, "MAS_UTrack_V00", 14) && b[14] >= '1' && b[14] <= '4';
}

typedef struct { int eff, par, vol, w; } UCmd;       /* w = importance (0 = nothing); vol = volume column value or -1 */

/* Translate one ULT effect nibble + parameter into the internal S3M-table code */
static UCmd ult_tr(int e, int p, int ver){
    UCmd c = { 0, 0, -1, 0 };
    int lo = p & 15;
    switch (e) {
    case 0: if (p && ver >= '3') { c.eff = 0x00; c.par = p; c.w = 4; } break;                     /* arpeggio */
    case 1: c.eff = 0x13; c.par = p > 0xDF ? 0xDF : p; c.w = 4; break;                             /* slide up */
    case 2: c.eff = 0x12; c.par = p > 0xDF ? 0xDF : p; c.w = 4; break;                             /* slide down */
    case 3: c.eff = 0x03; c.par = p; c.w = 5; break;                                               /* tone portamento */
    case 4: c.eff = 0x04; c.par = p; c.w = 5; break;                                               /* vibrato */
    case 5: if (ver >= '3' && (lo == 0x0C || (p & 0xF0) == 0xC0)) { c.eff = 0x0E; c.par = 0xC0; c.w = 7; } break;  /* stop sample */
    case 7: if (ver >= '4') { c.eff = 0x07; c.par = p; c.w = 4; } break;                          /* tremolo */
    case 9: c.eff = 0x09; c.par = p * 4 > 255 ? 255 : p * 4; c.w = 6; break;                       /* sample offset (1024 units) */
    case 10: c.eff = 0x11; c.par = (p & 0xF0) ? (p & 0xF0) : lo; c.w = 3; break;                  /* volume slide */
    case 11: c.eff = 0x0E; c.par = 0x80 | lo; c.w = 2; break;                                      /* pan 0..15 */
    case 12: c.vol = p / 4 > 64 ? 64 : p / 4; break;                                               /* volume 0..255 */
    case 13: c.eff = 0x0D; c.par = p; c.w = 9; break;                                              /* pattern break (BCD) */
    case 14:
        switch (p >> 4) {
        case 1: c.eff = 0x13; c.par = 0xF0 | lo; c.w = 4; break;                                   /* fine slide up */
        case 2: c.eff = 0x12; c.par = 0xF0 | lo; c.w = 4; break;                                   /* fine slide down */
        case 8: if (ver >= '4') { c.eff = 0x0E; c.par = 0x60 | lo; c.w = 8; } break;               /* pattern loop */
        case 9: c.eff = 0x15; c.par = lo; c.w = 5; break;                                          /* retrigger */
        case 10: c.eff = 0x11; c.par = (lo << 4) | 0x0F; c.w = 3; break;                           /* fine volume up */
        case 11: c.eff = 0x11; c.par = 0xF0 | lo; c.w = 3; break;                                  /* fine volume down */
        case 12: case 13: c.eff = 0x0E; c.par = p; c.w = 8; break;                                 /* note cut / delay */
        default: break;
        }
        break;
    case 15:
        if (p > 0x2F) { c.eff = 0x16; c.par = p; c.w = 9; }                                        /* tempo */
        else { c.eff = 0x10; c.par = p ? p : 6; c.w = 9; }                                         /* speed (F00 = 6) */
        break;
    default: break;
    }
    return c;
}

int xult_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 2; s->maxvol = 0x40; s->gvol = 255;
    if (!ult_probe(b, n)) return -1;
    int ver = b[14];
    memcpy(s->title, b + 15, 20); s->title[20] = 0;
    size_t pos = 48 + (size_t)b[47] * 32;                        /* song text lines */
    if (pos >= n) return -2;
    unsigned nos = b[pos++];
    size_t ssz = ver >= '4' ? 66 : 64;
    if (pos + nos * ssz + 258 > n) return -3;
    const uint8_t *sh = b + pos;
    pos += nos * ssz;
    const uint8_t *ord = b + pos;
    unsigned noc = b[pos + 256] + 1u, nop = b[pos + 257] + 1u;
    pos += 258;
    if (noc > 32 || nop > 256) return -4;
    s->speed = 6; s->tempo = 125; s->fast = 0;
    for (int i = 0; i < 32; i++) { s->chen[i] = (unsigned)i < noc; s->chpan[i] = -1; }
    for (unsigned i = 0; i < noc; i++) {
        if (ver >= '3') { if (pos + i >= n) return -3; s->chpan[i] = ngp_pan_tab[b[pos + i] & 15]; }
        else s->chpan[i] = ngp_pan_tab[(i & 1) ? 11 : 4];        /* older versions alternate left / right */
    }
    if (ver >= '3') pos += noc;
    int nord = 256;
    for (int i = 0; i < 256; i++) if (ord[i] == 0xFF) { nord = i; break; }
    for (int i = 0; i < nord; i++) s->orders[i] = ord[i] < nop ? ord[i] : 0xFE;
    while (nord > 0 && s->orders[nord - 1] > 0xFD) nord--;
    s->nord = nord;

    /* patterns: stored per channel; every track is 64 rows, repeat blocks do not cross tracks */
    s->nch = (int)noc; s->npat = (int)nop;
    s->pat = calloc((size_t)nop * 64 * noc + 1, sizeof(XCell));
    size_t q = pos;
    for (unsigned ch = 0; ch < noc; ch++)
        for (unsigned p = 0; p < nop; p++) {
            int row = 0;
            while (row < 64) {
                if (q + 5 > n) goto patterns_done;
                int rep = 1;
                if (b[q] == 0xFC) { if (q + 7 > n) goto patterns_done; rep = b[q + 1]; q += 2; if (!rep) rep = 1; }
                int note = b[q], ins = b[q + 1], cmd = b[q + 2], d1 = b[q + 3], d2 = b[q + 4];
                q += 5;
                int c1 = cmd & 15, c2 = cmd >> 4;
                UCmd a, c;
                if (c1 == 9 && c2 == 9) {                         /* two offsets form a fine 16-bit offset */
                    int off = ((d2 << 8) | d1) >> 6;
                    a = (UCmd){ 0x09, off > 255 ? 255 : off, -1, 6 }; c = (UCmd){ 0, 0, -1, 0 };
                } else {
                    a = ult_tr(c1, d1, ver);
                    c = c2 == c1 ? (UCmd){ 0, 0, -1, 0 } : ult_tr(c2, d2, ver);
                }
                for (int r = 0; r < rep && row < 64; r++, row++) {
                    XCell *x = &s->pat[((size_t)p * 64 + row) * noc + ch];
                    if (note >= 1 && note <= 60) x->note = (uint8_t)(note + 23);
                    if (ins >= 1 && ins <= 99) x->inst = (uint8_t)ins;
                    int vol = a.vol >= 0 ? a.vol : c.vol;
                    if (vol >= 0) x->vol1 = (uint8_t)(vol + 1);
                    const UCmd *k = (c.w > a.w) ? &c : &a;
                    if (k->w) { x->eff = (uint8_t)k->eff; x->par = (uint8_t)k->par; }
                    if (k->w && k->eff == 0x0E && k->par == 0xC0 && !x->note) x->note = 0x7F;   /* stop sample = cut */
                }
            }
        }
patterns_done:

    /* samples (signed, 8 or 16 bit); data follows the patterns at the end of the file */
    unsigned lim = nos > 99 ? 99 : nos;
    s->nsmp = (int)lim;
    size_t total = 0;
    for (unsigned i = 0; i < nos; i++) {
        const uint8_t *h = sh + i * ssz;
        unsigned ss = rd32(h + 52), se = rd32(h + 56);
        if (se > ss) total += (size_t)(se - ss) * ((h[61] & 4) ? 2 : 1);
    }
    size_t dpos = (n >= total && n - total >= q) ? n - total : q;
    for (unsigned i = 0; i < nos; i++) {
        const uint8_t *h = sh + i * ssz;
        unsigned ss = rd32(h + 52), se = rd32(h + 56);
        int is16 = (h[61] & 4) != 0;
        size_t len = se > ss ? se - ss : 0, bytes = len * (is16 ? 2 : 1);
        size_t d = dpos; dpos += bytes;
        if (i >= lim) continue;
        XSmp *m = &s->smp[i + 1];
        unsigned speed = ver >= '4' ? rd16(h + 62) : 8363;
        int fine = (int)(int16_t)rd16(h + (ver >= '4' ? 64 : 62));
        if (!speed) speed = 8363;
        m->c2spd = fine ? (uint32_t)(speed * __builtin_exp2(fine / (12.0 * 32768.0)) + 0.5) : speed;
        m->vol = h[60] >> 2;
        if (!len || d >= n) continue;
        if (d + bytes > n) { bytes = n - d; len = bytes / (is16 ? 2 : 1); }
        if (len < 2) continue;
        m->len = (uint32_t)len; m->is16 = is16;
        m->data = calloc(len * (is16 ? 2 : 1) + 4, 1);
        if (is16) { int16_t *o = (int16_t *)m->data; for (size_t k = 0; k < len; k++) o[k] = (int16_t)rd16(b + d + 2 * k); }
        else for (size_t k = 0; k < len; k++) m->data[k] = (int8_t)b[d + k];
        if (h[61] & 8) {
            unsigned ls = rd32(h + 44), le = rd32(h + 48);
            if (is16) { ls >>= 1; le >>= 1; }
            m->loop = 1; m->ls = ls; m->le = le > m->len ? m->len : le;
            if (m->ls >= m->le) m->loop = 0;
            m->pingpong = (h[61] & 16) != 0;
        }
    }

    /* UltraTracker slides keep running on rows without a new note: repeat them on empty cells until the next note */
    for (unsigned ch = 0; ch < noc; ch++) {
        int se = -1, sp = 0;
        for (unsigned p = 0; p < nop; p++)
            for (int r = 0; r < 64; r++) {
                XCell *x = &s->pat[((size_t)p * 64 + r) * noc + ch];
                int slide = (x->eff == 0x12 || x->eff == 0x13 || x->eff == 0x03) && x->par && x->par < 0xE0;
                if (slide) { se = x->eff; sp = x->par; }
                else if (x->note) se = -1;
                else if (!x->eff && !x->par && se >= 0) { x->eff = (uint8_t)se; x->par = (uint8_t)sp; }
                else if (x->eff == 0x03 && !x->par) se = -1;                    /* 300 stops the slide */
            }
    }
    return 0;
}
