/* OctaMED / MED loader (MMD0, MMD1, MMD2, MMD3). The effects are ProTracker-like, so the song is handed to the MOD replayer
 * (fmt 1). Blocks longer than 64 lines are split into several 64-row patterns, shorter ones get a pattern break.
 * Synth/hybrid instruments are baked into a looping single-cycle sample (first waveform of the waveform table, first volume of the
 * volume table, two octaves below the sample pitch scale); the volume table is run by the replayer (set, wait, slide, envelope), the waveform table is run as well (wave morphing, vibrato). Multi-octave sample sets are not supported (silent). GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned be16(const uint8_t *p){ return (p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int med_probe(const uint8_t *b, size_t n){
    if (n < 52 || b[0] != 'M' || b[1] != 'M' || b[2] != 'D') return 0;
    if (b[3] < '0' || b[3] > '3') return 0;
    uint32_t so = be32(b + 8);
    return so >= 52 && so + 788 <= n;
}

#define MAXPAT 256
#define NOTE_BASE 36            /* engine note of the PT period 856 (C-1); MED note 1 = C-1 */

typedef struct { int first, cnt; } BlkMap;

static int conv_tempo(int v, int bpm, int rpb){
    int t = bpm ? (v * rpb + 2) / 4 : (v * 125 + 16) / 33;
    return t < 0x21 ? 0x21 : t > 0xFF ? 0xFF : t;
}

static void conv_fx(XCell *x, int e, int p, int hexvol, int bpm, int rpb, const BlkMap *map, int nmap){
    x->eff = 0; x->par = 0;
    switch (e) {
    case 0x00: if (p) { x->eff = 0; x->par = (uint8_t)p; } break;
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x0A:
        x->eff = (uint8_t)e; x->par = (uint8_t)p; break;
    case 0x09: if (p >= 1 && p <= 0x20) { x->eff = 0x0F; x->par = (uint8_t)p; } break;
    case 0x0D: x->eff = 0x0A; x->par = (uint8_t)p; break;                              /* MED volume slide (same as 0A) */
    case 0x0B: x->eff = 0x0B; x->par = (uint8_t)(p < nmap ? map[p].first : p); break;
    case 0x0C: { int v = hexvol ? p : (p >> 4) * 10 + (p & 15); if (v > 64) v = 64; x->eff = 0x0C; x->par = (uint8_t)v; } break;
    case 0x0F:
        if (p == 0) { x->eff = 0x0D; x->par = 0; }
        else if (!bpm && p <= 10) { x->eff = 0x0F; x->par = (uint8_t)p; }                /* non-BPM mode: 1..10 = ticks per row */
        else if (p <= 0xF0) { x->eff = 0x0F; x->par = (uint8_t)conv_tempo(p, bpm, rpb); }
        else if (p == 0xFF) { x->eff = 0x0C; x->par = 0; }
        else if (p == 0xF1) { x->eff = 0x0E; x->par = 0x93; }          /* play note twice (approx.: retrigger every 3 ticks) */
        else if (p == 0xF2) { x->eff = 0x0E; x->par = 0xD3; }
        else if (p == 0xF3) { x->eff = 0x0E; x->par = 0x92; }
        break;
    case 0x11: x->eff = 0x0E; x->par = (uint8_t)(0x10 | (p > 15 ? 15 : p)); break;
    case 0x12: x->eff = 0x0E; x->par = (uint8_t)(0x20 | (p > 15 ? 15 : p)); break;
    case 0x14: x->eff = 0x04; x->par = (uint8_t)p; break;
    case 0x15: x->eff = 0x0E; x->par = (uint8_t)(0x50 | (p & 15)); break;
    case 0x16: x->eff = 0x0E; x->par = (uint8_t)(0x60 | (p & 15)); break;
    case 0x18: x->eff = 0x0E; x->par = (uint8_t)(0xC0 | (p & 15)); break;
    case 0x19: x->eff = 0x09; x->par = (uint8_t)p; break;
    case 0x1A: x->eff = 0x0E; x->par = (uint8_t)(0xA0 | (p > 15 ? 15 : p)); break;
    case 0x1B: x->eff = 0x0E; x->par = (uint8_t)(0xB0 | (p > 15 ? 15 : p)); break;
    case 0x1D: x->eff = 0x0D; x->par = (uint8_t)(((p % 64) / 10 << 4) | ((p % 64) % 10)); break;
    case 0x1F: if (p >> 4) { x->eff = 0x0E; x->par = (uint8_t)(0xD0 | (p >> 4)); } else if (p & 15) { x->eff = 0x0E; x->par = (uint8_t)(0x90 | (p & 15)); } break;
    default: break;
    }
}

int xmed_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125;
    if (!med_probe(b, n)) return -1;
    int ver = b[3] - '0';
    uint32_t so = be32(b + 8), bao = be32(b + 16), smo = be32(b + 24), exo = be32(b + 32);
    const uint8_t *sg = b + so;
    unsigned nblk = be16(sg + 504), slen = be16(sg + 506);
    unsigned deftempo = be16(sg + 764); int ptrans = (int8_t)sg[766];
    unsigned flags = sg[767], flags2 = sg[768], tempo2 = sg[769], nsmp = sg[787];
    int bpm = (flags2 & 0x20) != 0, rpb = (flags2 & 0x1F) + 1, hexvol = (flags & 0x10) != 0;
    if (nsmp > 63) nsmp = 63;
    if (!nblk || nblk > 1024 || (size_t)bao + (size_t)nblk * 4 > n) return -2;

    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    s->speed = tempo2 ? (int)tempo2 : 6;
    s->tempo = bpm ? (int)(deftempo * rpb / 4) : (int)(deftempo * 125 / 33);
    if (s->tempo < 0x21) s->tempo = 0x21;
    if (s->tempo > 0xFF) s->tempo = 0xFF;
    if (!bpm && deftempo <= 10) { s->speed = (int)deftempo ? (int)deftempo : 6; s->tempo = 125; }  /* tiny old-style values = PT speeds */

    /* block table + channel count */
    uint32_t bo[1024]; unsigned bl[1024], bt[1024]; int nch = 0;
    for (unsigned i = 0; i < nblk; i++) {
        bo[i] = be32(b + bao + i * 4); bl[i] = bt[i] = 0;
        if (!bo[i] || bo[i] + 8 > n) continue;
        if (ver == 0) { bt[i] = b[bo[i]]; bl[i] = b[bo[i] + 1] + 1u; }
        else { bt[i] = be16(b + bo[i]); bl[i] = be16(b + bo[i] + 2) + 1u; }
        size_t need = (size_t)bt[i] * bl[i] * (ver == 0 ? 3 : 4) + (ver == 0 ? 2 : 8);
        if (bo[i] + need > n) { bt[i] = 0; continue; }
        if ((int)bt[i] > nch) nch = (int)bt[i];
    }
    if (nch < 1) return -3;
    if (nch > 32) nch = 32;
    s->nch = nch;

    /* play sequence (block numbers) */
    uint16_t seq[4096]; unsigned nseq = 0;
    if (ver <= 1) {
        for (unsigned i = 0; i < slen && i < 256; i++) seq[nseq++] = sg[508 + i];
    } else {
        uint32_t pto = be32(sg + 508), sto = be32(sg + 512);
        unsigned npseq = be16(sg + 522);
        for (unsigned i = 0; i < slen && sto + 2 * i + 2 <= n; i++) {
            unsigned ps = be16(b + sto + 2 * i);
            if (ps >= npseq) continue;
            uint32_t po = be32(b + pto + 4 * ps);
            if (!po || po + 42 > n) continue;
            unsigned len = be16(b + po + 40);
            for (unsigned k = 0; k < len && po + 42 + 2 * k + 2 <= n && nseq < 4096; k++) {
                unsigned v = be16(b + po + 42 + 2 * k);
                if (v < 0x8000) seq[nseq++] = (uint16_t)v;
            }
        }
    }
    (void)smo;

    /* expand blocks to 64-row patterns */
    BlkMap map[1024]; int npat = 0;
    static uint8_t used[1024]; memset(used, 0, sizeof used);
    for (unsigned i = 0; i < nseq; i++) if (seq[i] < nblk) used[seq[i]] = 1;
    for (unsigned i = 0; i < nblk; i++) {
        map[i].first = 0; map[i].cnt = 0;
        if (!used[i]) continue;                           /* blocks outside the play sequence are never played */
        map[i].cnt = bl[i] ? (int)((bl[i] + 63) / 64) : 1;
        map[i].first = npat; npat += map[i].cnt;
    }
    if (npat > MAXPAT - 1) return -4;
    s->npat = npat + 1;                                   /* last one stays empty (target for invalid sequence entries) */
    s->pat = calloc((size_t)s->npat * 64 * nch + 1, sizeof(XCell));
    if (!s->pat) return -5;

    int norder = 0; int seqmap[4096];
    for (unsigned i = 0; i < nseq; i++) {
        seqmap[i] = norder;
        unsigned bk = seq[i];
        if (bk >= nblk) continue;
        for (int k = 0; k < map[bk].cnt && norder < 256; k++) s->orders[norder++] = (uint8_t)(map[bk].first + k);
    }
    s->nord = norder;
    if (s->nord < 1) return -6;
    /* jump targets in MED are play sequence positions: remap through seqmap */
    BlkMap jm[256]; for (unsigned i = 0; i < 256; i++) { jm[i].first = i < nseq ? seqmap[i] : (int)i; jm[i].cnt = 1; }

    for (unsigned bi = 0; bi < nblk; bi++) {
        if (!bt[bi] || !used[bi]) continue;
        const uint8_t *q = b + bo[bi] + (ver == 0 ? 2 : 8);
        unsigned tracks = bt[bi], lines = bl[bi];
        for (unsigned r = 0; r < lines; r++) {
            XCell *row = &s->pat[((size_t)(map[bi].first + r / 64) * 64 + (r % 64)) * nch];
            for (unsigned c = 0; c < tracks; c++) {
                const uint8_t *e = q + ((size_t)r * tracks + c) * (ver == 0 ? 3 : 4);
                if ((int)c >= nch) continue;
                XCell *x = &row[c];
                int note, inst, fx, par;
                if (ver == 0) { note = e[0] & 0x3F; inst = ((e[0] & 0x80) >> 3) | ((e[0] & 0x40) >> 1) | (e[1] >> 4); fx = e[1] & 15; par = e[2]; }
                else { note = e[0] & 0x7F; inst = e[1] & 0x3F; fx = e[2]; par = e[3]; }
                if (note) { int en = note - 1 + NOTE_BASE; x->note = (uint8_t)(en > 107 ? 107 : en); }
                x->inst = (uint8_t)inst;
                conv_fx(x, fx, par, hexvol, bpm, rpb, jm, (int)(nseq < 256 ? nseq : 256));
            }
        }
        if (lines % 64) {                                 /* short (last) pattern: break after the last row */
            XCell *row = &s->pat[((size_t)(map[bi].first + (lines - 1) / 64) * 64 + ((lines - 1) % 64)) * nch];
            int done = 0;
            for (int c = 0; c < nch && !done; c++) if (!row[c].eff) { row[c].eff = 0x0D; row[c].par = 0; done = 1; }
        }
    }

    /* instruments */
    const uint8_t *ext = NULL; unsigned extn = 0, extsz = 0;
    if (exo && exo + 52 <= n) {
        uint32_t eo = be32(b + exo + 4);
        extn = be16(b + exo + 8); extsz = be16(b + exo + 10);
        if (eo && extsz >= 4 && (size_t)eo + (size_t)extn * extsz <= n) ext = b + eo;
        uint32_t no = be32(b + exo + 44), nl = be32(b + exo + 48);
        if (no && nl && no + nl <= n) { size_t l = nl > 20 ? 20 : nl; memcpy(s->title, b + no, l); s->title[l] = 0; }
        if (!strcmp(s->title, "<unnamed>")) s->title[0] = 0;   /* OctaMED's default name: no real title */
    }
    s->nsmp = (int)nsmp;
    for (unsigned i = 0; i < nsmp; i++) {
        XSmp *m = &s->smp[i + 1];
        const uint8_t *h = sg + i * 8;
        unsigned rep = be16(h), rlen = be16(h + 2); int vol = h[6]; int strans = (int8_t)h[7];
        int ft = (ext && i < extn) ? (int8_t)ext[i * extsz + 3] : 0;
        m->vol = vol > 64 ? 64 : vol;
        double sp = ngp_finetune_c2spd[ft & 15] * pow(2.0, (strans + ptrans) / 12.0);
        m->c2spd = (uint32_t)(sp + 0.5);
        uint32_t po = smo + 4u * i;
        if (!smo || po + 4 > n) continue;
        uint32_t io = be32(b + po);
        if (!io || io + 6 > n) continue;
        uint32_t len = be32(b + io); int type = (int16_t)be16(b + io + 4);
        if (type < 0) {                                                /* synth (-2) / hybrid (-1): bake first waveform */
            const uint8_t *sy = b + io + 6;
            if (io + 6 + 276 > n) continue;
            unsigned nwf = be16(sy + 14), vtl = be16(sy + 8), wtl = be16(sy + 10);
            if (!nwf || nwf > 64 || io + 6 + 272 + 4 * nwf > n) continue;
            unsigned wi = 0, wv = 0x40;
            for (unsigned k = 0; k < wtl && k < 128; k++) { int c = sy[144 + k]; if (c < 0x80) { wi = (unsigned)c; break; } if (c == 0xFF) break; if (c >= 0xF0) k++; }   /* skip command parameters */
            if (wi >= nwf) wi = 0;
            for (unsigned k = 0; k < vtl && k < 128; k++) { int c = sy[16 + k]; if (c <= 0x40) { wv = (unsigned)c; break; } if (c == 0xFF) break; }
            uint32_t wo = io + be32(sy + 272 + 4 * wi);
            if (wo + 2 > n) continue;
            uint32_t wl = be16(b + wo) * 2u;
            if (!wl || wo + 2 + wl > n) continue;
            m->len = wl; m->is16 = 0;
            m->data = calloc((size_t)wl + 4, 1);
            if (!m->data) return -5;
            memcpy(m->data, b + wo + 2, wl);
            m->ls = 0; m->le = wl; m->loop = 1;
            m->vol = (int)(vol > 64 ? 64 : vol);
            m->syn = calloc(1, sizeof(XSyn));
            if (!m->syn) return -5;
            m->syn->vn = (int)(vtl > 128 ? 128 : vtl); m->syn->vspd = sy[12];
            memcpy(m->syn->vtab, sy + 16, 128);
            m->syn->wn = (int)(wtl > 128 ? 128 : wtl); m->syn->wspd = sy[13]; memcpy(m->syn->wtab, sy + 144, 128);
            for (unsigned k = 0; k < nwf; k++) {
                uint32_t o2 = io + be32(sy + 272 + 4 * k);
                if (o2 + 2 > n) break;
                uint32_t l2 = be16(b + o2) * 2u;
                if (!l2 || o2 + 2 + l2 > n) break;
                m->syn->wf[k] = malloc(l2);
                if (!m->syn->wf[k]) return -5;
                memcpy(m->syn->wf[k], b + o2 + 2, l2); m->syn->wl[k] = (int)l2; m->syn->nwf = (int)k + 1;
            }
            (void)wv;
            m->c2spd = (uint32_t)(m->c2spd * 0.25 + 0.5);                 /* synth notes sound two octaves lower than the sample scale */
            continue;
        }
        if ((type & 0x0F) || (type & 0xC0)) continue;                  /* multi-octave, delta, packed: unsupported */
        int is16 = (type & 0x10) != 0, stereo = (type & 0x20) != 0;
        size_t avail = (size_t)io + 6 < n ? n - io - 6 : 0;
        if (len > avail) len = (uint32_t)avail;
        if (stereo) len >>= 1;
        uint32_t cnt = is16 ? len >> 1 : len;
        if (!cnt || cnt > (64u << 20)) continue;
        m->len = cnt; m->is16 = is16;
        m->data = calloc((size_t)cnt * (is16 ? 2 : 1) + 4, 1);
        if (!m->data) return -5;
        const uint8_t *d = b + io + 6;
        if (is16) { int16_t *w = (int16_t *)m->data; for (uint32_t k = 0; k < cnt; k++) w[k] = (int16_t)be16(d + 2 * k); }
        else memcpy(m->data, d, cnt);
        if (rlen > 1) {
            uint32_t ls = is16 ? rep : rep * 2, ll = is16 ? rlen : rlen * 2;
            m->ls = ls; m->le = ls + ll; if (m->le > m->len) m->le = m->len;
            m->loop = m->ls < m->le;
        }
    }
    return 0;
}
