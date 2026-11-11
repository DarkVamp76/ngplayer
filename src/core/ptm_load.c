/* PTM loader (Poly Tracker 2.03). Fills the same XSong the S3M loader produces, so the S3M replayer plays it.
 * Layout: 608-byte header (1..32 channels, 1..128 patterns, up to 256 orders), 80-byte instrument headers,
 * packed patterns (always 64 rows, S3M-like packing), signed 8-bit delta sample data (16-bit samples are the
 * same byte-wise delta stream). Effects follow the S3M set, so most map 1:1. GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static unsigned rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

int ptm_probe(const uint8_t *b, size_t n){
    if (n < 608 + 80 || memcmp(b + 44, "PTMF", 4)) return 0;
    unsigned nord = rd16(b + 32), nins = rd16(b + 34), npat = rd16(b + 36), nch = rd16(b + 38);
    return nord >= 1 && nord <= 256 && nins >= 1 && nins <= 255 && npat >= 1 && npat <= 128 && nch >= 1 && nch <= 32;
}

/* Convert one PTM effect (0..9 = 0..9, 10..22 = A..M) to the internal S3M-table code. Returns 0 if unsupported. */
static void ptm_effect(XCell *x, int cmd, int inf){
    int x4 = inf >> 4, y4 = inf & 15;
    x->eff = 0; x->par = 0;
    switch (cmd) {
    case 0:  if (inf) { x->eff = 0x00; x->par = (uint8_t)inf; } break;     /* arpeggio */
    case 1:  x->eff = 0x12; x->par = (uint8_t)inf; break;                   /* slide down (1fx fine, 1ex extra fine) */
    case 2:  x->eff = 0x13; x->par = (uint8_t)inf; break;                   /* slide up */
    case 3:  x->eff = 0x03; x->par = (uint8_t)inf; break;                   /* tone portamento */
    case 4:  x->eff = 0x04; x->par = (uint8_t)inf; break;                   /* vibrato */
    case 5:  x->eff = 0x18; x->par = (uint8_t)inf; break;                   /* tone porta + volume slide */
    case 6:  x->eff = 0x17; x->par = (uint8_t)inf; break;                   /* vibrato + volume slide */
    case 7:  x->eff = 0x07; x->par = (uint8_t)inf; break;                   /* tremolo */
    case 9:  x->eff = 0x09; x->par = (uint8_t)inf; break;                   /* sample offset */
    case 10: x->eff = 0x11; x->par = (uint8_t)inf; break;                   /* A: volume slide (S3M D semantics) */
    case 11: x->eff = 0x0B; x->par = (uint8_t)inf; break;                   /* B: position jump */
    case 12: x->par = (uint8_t)(inf > 64 ? 64 : inf); x->eff = 0xFF; break;  /* C: set volume, handled by caller */
    case 13: x->eff = 0x0D; x->par = (uint8_t)inf; break;                   /* D: pattern break */
    case 14:                                                                 /* E: extended */
        switch (x4) {
        case 1: x->eff = 0x12; x->par = (uint8_t)(0xF0 | y4); break;         /* fine slide down */
        case 2: x->eff = 0x13; x->par = (uint8_t)(0xF0 | y4); break;         /* fine slide up */
        case 4: x->eff = 0x0E; x->par = (uint8_t)(0x40 | y4); break;         /* vibrato waveform */
        case 5: x->eff = 0x0E; x->par = (uint8_t)(0x50 | y4); break;         /* finetune */
        case 6: x->eff = 0x0E; x->par = (uint8_t)(0x60 | y4); break;         /* pattern loop */
        case 7: x->eff = 0x0E; x->par = (uint8_t)(0x70 | y4); break;         /* tremolo waveform */
        case 8: x->eff = 0x0E; x->par = (uint8_t)(0x80 | y4); break;         /* set pan */
        case 9: x->eff = 0x15; x->par = (uint8_t)(0x80 | y4); break;         /* retrigger */
        case 10: x->eff = 0x11; x->par = (uint8_t)((y4 << 4) | 0x0F); break; /* fine volume up */
        case 11: x->eff = 0x11; x->par = (uint8_t)(0xF0 | y4); break;        /* fine volume down */
        case 12: x->eff = 0x0E; x->par = (uint8_t)(0xC0 | y4); break;        /* note cut */
        case 13: x->eff = 0x0E; x->par = (uint8_t)(0xD0 | y4); break;        /* note delay */
        case 14: x->eff = 0x0E; x->par = (uint8_t)(0xE0 | y4); break;        /* pattern delay */
        default: break;
        }
        break;
    case 15:                                                                 /* F: speed (<0x20) or tempo */
        if (inf >= 0x20) { x->eff = 0x16; x->par = (uint8_t)inf; }
        else if (inf) { x->eff = 0x10; x->par = (uint8_t)inf; }
        break;
    case 16: x->eff = 0x1A; x->par = (uint8_t)(inf < 0x40 ? inf << 2 : 0xFF); break;   /* G: global volume */
    case 17: x->eff = 0x15; x->par = (uint8_t)inf; break;                   /* H: retrigger + volume change */
    case 18: x->eff = 0x04; x->par = (uint8_t)((x4 << 4) | (y4 >> 2)); break;  /* I: fine vibrato = vibrato at 1/4 depth */
    default: break;                                                          /* J..M note slides: not supported */
    }
}

int xptm_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 2; s->maxvol = 0x40; s->gvol = 255;
    if (!ptm_probe(b, n)) return -1;
    memcpy(s->title, b, 20); s->title[20] = 0;
    unsigned nord = rd16(b + 32), nins = rd16(b + 34), npat = rd16(b + 36), nch = rd16(b + 38);
    if (608 + (size_t)nins * 80 > n) return -3;
    s->speed = 6; s->tempo = 125; s->fast = 0;
    for (int i = 0; i < 32; i++) {
        s->chen[i] = (unsigned)i < nch;
        s->chpan[i] = ngp_pan_tab[b[64 + i] & 15];
    }
    for (unsigned i = 0; i < nord; i++) { unsigned o = b[96 + i]; s->orders[i] = o < npat ? (uint8_t)o : 0xFE; }
    s->nord = (int)nord;
    while (s->nord > 0 && s->orders[s->nord - 1] > 0xFD) s->nord--;

    /* instruments / samples */
    s->nsmp = nins > 99 ? 99 : (int)nins;
    for (int i = 1; i <= s->nsmp; i++) {
        const uint8_t *h = b + 608 + 80 * (size_t)(i - 1); XSmp *m = &s->smp[i];
        m->vol = h[13] > 64 ? 64 : h[13]; m->c2spd = rd16(h + 14);
        if ((h[0] & 3) != 1) continue;
        size_t d = rd32(h + 18), bytes = rd32(h + 22);
        unsigned ls = rd32(h + 26), le = rd32(h + 30);
        int is16 = (h[0] & 16) != 0;
        if (!bytes || !d || d >= n || bytes > 32000000) continue;
        if (d + bytes > n) bytes = n - d;
        if (is16) { bytes &= ~(size_t)1; ls >>= 1; le >>= 1; }
        if (bytes < (is16 ? 2u : 1u)) continue;
        m->is16 = is16; m->len = (uint32_t)(is16 ? bytes / 2 : bytes);
        m->data = calloc(bytes + 4, 1);
        uint8_t acc = 0, *o = (uint8_t *)m->data;              /* byte-wise signed delta; 16-bit is little endian */
        for (size_t k = 0; k < bytes; k++) { acc = (uint8_t)(acc + b[d + k]); o[k] = acc; }
        if (h[0] & 4) {
            m->loop = 1; m->ls = ls; m->le = le > m->len ? m->len : le;
            if (m->ls >= m->le) m->loop = 0;
            m->pingpong = (h[0] & 8) != 0;
        }
    }

    /* patterns */
    s->nch = (int)nch; s->npat = (int)npat;
    s->pat = calloc((size_t)npat * 64 * nch + 1, sizeof(XCell));
    for (unsigned p = 0; p < npat; p++) {
        size_t o = (size_t)rd16(b + 352 + 2 * p) << 4;
        if (!o || o >= n) continue;
        const uint8_t *q = b + o, *e = b + n;
        int row = 0;
        while (row < 64 && q < e) {
            uint8_t w = *q++;
            if (!w) { row++; continue; }
            unsigned ch = w & 31; int note = 0, ins = 0, cmd = -1, inf = 0, vol = -1;
            if (w & 0x20) { if (q + 2 > e) break; note = q[0]; ins = q[1]; q += 2; }
            if (w & 0x40) { if (q + 2 > e) break; cmd = q[0]; inf = q[1]; q += 2; }
            if (w & 0x80) { if (q >= e) break; vol = *q++; }
            if (ch >= nch) continue;
            XCell *x = &s->pat[((size_t)p * 64 + row) * nch + ch];
            if (note == 254) x->note = 0x7F;                                       /* note off */
            else if (note >= 1 && note <= 120) x->note = (uint8_t)(note - 1);      /* 1 = C-0 */
            if (ins >= 1 && ins <= 99) x->inst = (uint8_t)ins;
            if (vol >= 0 && vol <= 64) x->vol1 = (uint8_t)(vol + 1);
            if (cmd >= 0) {
                ptm_effect(x, cmd, inf);
                if (x->eff == 0xFF) { if (vol < 0) x->vol1 = (uint8_t)(x->par + 1); x->eff = 0; x->par = 0; }   /* C: set volume */
            }
        }
    }
    return 0;
}
