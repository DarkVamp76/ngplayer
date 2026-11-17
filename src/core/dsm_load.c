/* DSM loader (DSIK / Digisound Interface Kit "DSMF" RIFF modules, e.g. from Dynamic Studio and Epic Megagames' games).
 * Layout: "RIFF" size "DSMF", then chunks: SONG (192 bytes: title[28], version, flags, order pos, restart, orders, samples, patterns,
 * channels, global volume, master volume, speed, tempo, 16 pan values, 128 orders), INST (64 byte header + 8 bit sample data), PATT
 * (u16 length incl. itself, then per row: flag bytes with channel in bits 0-3 and 0x80 note, 0x40 instrument, 0x20 volume, 0x10
 * effect + parameter; flag 0 ends the row). The effects are the ProTracker ones. The song is converted into MOD cells and played by
 * the MOD replayer (fmt 1). GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static unsigned rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

#ifndef DSM_GAIN
#define DSM_GAIN 2.026
#endif
#ifndef DSM_NOTE_OFS
#define DSM_NOTE_OFS (-1)   /* DSM note 1 = C-0 of a period table starting at note index 0: measured against libopenmpt (pitch within 0.2 cents) */
#endif

int dsm_probe(const uint8_t *b, size_t n){
    return n > 12 + 8 + 192 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "DSMFSONG", 8) && rd32(b + 16) >= 192;
}

int xdsm_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125;
    if (!dsm_probe(b, n)) return -1;
    const uint8_t *sg = b + 20; int ofs = DSM_NOTE_OFS;
    memcpy(s->title, sg, 20); s->title[20] = 0;
    unsigned nord = rd16(sg + 36), nsmp = rd16(sg + 38), npat = rd16(sg + 40), nch = rd16(sg + 42);
    if (nord > 128 || nsmp > 99 || npat > 256 || !nch || nch > 16) return -2;
    int gv = sg[44] > 64 ? 64 : sg[44];
    s->gvol = gv * 4 > 255 ? 255 : gv * 4;
    s->speed = sg[46] ? sg[46] : 6; s->tempo = sg[47] >= 32 ? sg[47] : 125;
    s->linpan = 1; s->pt_tuning = 3;
    { double mv = sg[45] & 0x7F; if (sg[45] == 0x80) mv = 256.0 / (double)nch; if (sg[45] == 0) mv = 1e-6;   /* 0x80: libopenmpt divides by the channel count */
      s->mixgain = DSM_GAIN * mv / 96.0; }   /* master volume (bit 7 = stereo flag), measured against libopenmpt */
    s->restart = rd16(sg + 34) < nord ? (int)rd16(sg + 34) : 0;
    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    for (unsigned i = 0; i < nch; i++) { int p = sg[48 + i]; if (p <= 0x80) s->chpan[i] = p * 2 > 255 ? 255 : p * 2; }
    for (unsigned i = 0; i < nord; i++) s->orders[i] = sg[64 + i] < npat ? sg[64 + i] : 0xFE;
    while (nord > 0 && s->orders[nord - 1] > 0xFD) nord--;
    s->nord = (int)nord; s->nch = (int)nch; s->nsmp = (int)nsmp; s->npat = (int)npat;
    s->pat = calloc((size_t)npat * 64 * nch + 1, sizeof(XCell));

    size_t o = 12 + 8 + rd32(b + 16);
    unsigned si = 0, pi = 0;
    while (o + 8 <= n) {
        size_t l = rd32(b + o + 4), d = o + 8;
        if (l > n - d) l = n - d;
        if (!memcmp(b + o, "INST", 4) && si < nsmp && l >= 64) {
            const uint8_t *h = b + d; XSmp *m = &s->smp[++si];
            unsigned flags = rd16(h + 13), len = rd32(h + 16), ls = rd32(h + 20), le = rd32(h + 24), rate = rd32(h + 32);
            m->vol = h[15] > 64 ? 64 : h[15]; m->c2spd = rate ? rate : 8363;
            if (len > l - 64) len = (unsigned)(l - 64);
            if (len >= 2) {
                m->len = len; m->data = calloc((size_t)len + 4, 1);
                for (unsigned k = 0; k < len; k++) m->data[k] = (int8_t)(((flags & 2) != 0) ? h[64 + k] : h[64 + k] - 128);
                if (flags & 0x40) { int acc = 0; for (unsigned k = 0; k < len; k++) { acc = (int8_t)(acc + m->data[k]); m->data[k] = (int8_t)acc; } }   /* delta coded */
                if ((flags & 1) && le > ls + 1 && ls < len) { m->loop = 1; m->ls = ls; m->le = le > len ? len : le; }
            }
        } else if (!memcmp(b + o, "PATT", 4) && pi < npat && l >= 2) {
            const uint8_t *q = b + d, *e = b + d + l; q += 2;
            int row = 0;
            while (row < 64 && q < e) {
                uint8_t w = *q++;
                if (!w) { row++; continue; }
                XCell *x = &s->pat[((size_t)pi * 64 + row) * nch + (w & 15) % nch];
                int ok = (w & 15) < (int)nch;
                if (w & 0x80) { if (q >= e) break; int nt = *q++; if (ok && nt) x->note = (uint8_t)(nt + ofs > 127 ? 127 : nt + ofs); }
                if (w & 0x40) { if (q >= e) break; if (ok) x->inst = *q; q++; }
                if (w & 0x20) { if (q >= e) break; if (ok && *q <= 64) x->vol1 = (uint8_t)(*q + 1); q++; }
                if (w & 0x10) { if (q + 2 > e) break; if (ok) { x->eff = q[0]; x->par = q[1];
                        if (x->eff == 8) { if (x->par <= 0x80) x->par = (uint8_t)(x->par * 2 > 255 ? 255 : x->par * 2); else { x->eff = 0; x->par = 0; } }   /* panning 0..0x80 */
                    } q += 2; }
            }
            pi++;
        }
        o = d + l + (l & 1 ? 0 : 0);
    }
    return 0;
}
