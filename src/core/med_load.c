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

static int g_8ch, g_soft;       /* the song: 5-8 channel mode (flags 0x40), mixing mode (flags2 0x80) */
/* the BPM of a MED tempo value, as libopenmpt reads it (MMDTempoToBPM) */
static double mmd_bpm(int v, int bpm, int rpb){
    static const int t8[10] = { 179, 164, 152, 141, 131, 123, 116, 110, 104, 99 };
    if (bpm && !g_8ch) return v < 7 ? 111.5 : v * rpb / 4.0;
    if (g_8ch && v > 0) return t8[(v > 10 ? 10 : v) - 1];
    if (!g_soft && v > 0 && v <= 10) return (6.0 * 1773447.0 / 14500.0) / v;
    if (g_soft && v < 8) return 157.86;
    return v / 0.264;
}
static int conv_tempo(int v, int bpm, int rpb){       /* the BPM (up to 510: above 255 the effect 0x1B takes half of it) */
    int t = v < 3 ? 0x70 : (int)(mmd_bpm(v, bpm, rpb) + 0.5);       /* F01 and F02: a bug of OctaMED that libopenmpt keeps (112 BPM) */
    return t < 0x20 ? 0x20 : t > 0x1FE ? 0x1FE : t;
}

static void conv_fx(XCell *x, int e, int p, int hexvol, int bpm, int rpb, const BlkMap *map, int nmap){
    x->eff = 0; x->par = 0;
    switch (e) {
    case 0x00: if (p) { x->eff = 0; x->par = (uint8_t)p; } break;
    case 0x01: case 0x02: if (p) { x->eff = (uint8_t)e; x->par = (uint8_t)p; } break;                    /* (no effect memory: a zero does nothing) */
    case 0x03: case 0x07: case 0x0A: x->eff = (uint8_t)e; x->par = (uint8_t)p; break;
    case 0x04: x->eff = 0x04; x->par = (uint8_t)((p & 0xF0) | ((p & 15) * 2 > 15 ? 15 : (p & 15) * 2)); break;     /* the vibrato of MED is twice as deep as in ProTracker */
    case 0x05: if (p) { x->eff = 0x05; x->par = (uint8_t)p; } else { x->eff = 0x03; x->par = 0; } break;        /* (a zero: the tone portamento goes on) */
    case 0x06: if (p) { x->eff = 0x06; x->par = (uint8_t)p; } else { x->eff = 0x04; x->par = 0; } break;
    case 0x09: if (p >= 1 && p <= 0x20) { x->eff = 0x0F; x->par = (uint8_t)p; } break;
    case 0x0D: x->eff = 0x0A; x->par = (uint8_t)p; break;                              /* MED volume slide (same as 0A) */
    case 0x0B: x->eff = 0x0B; x->par = (uint8_t)(p < nmap ? map[p].first : p); break;
    case 0x0C: if (!hexvol && p >= 0x99) break; { int v = hexvol ? (p & 0x7F) : (p >> 4) * 10 + (p & 15); if (v > 64) v = 64; x->eff = 0x0C; x->par = (uint8_t)v; } break;       /* (decimal volumes above 99, and the bit 7 of hex volumes: no volume) */
    case 0x0F:
        if (p == 0) { x->eff = 0x0D; x->par = 0; }
        else if (p <= 0xF0) { int t = conv_tempo(p, bpm, rpb); if (t > 0xFF) { x->eff = 0x1B; x->par = (uint8_t)((t + 1) / 2); } else if (t > 0x20) { x->eff = 0x0F; x->par = (uint8_t)t; } else { x->eff = 0x16; x->par = (uint8_t)t; } }   /* (0x0F with 0x20 or less would be a speed) */
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
    case 0x1E: x->eff = 0x0E; x->par = (uint8_t)(0xE0 | (p > 15 ? 15 : p)); break;                     /* repeat row = the pattern delay */
    case 0x2E: if (p <= 0x10 || p >= 0xF0) { int v = (((p ^ 0x80) & 0xFF) - 0x70) * 8; if (v < 0) v = 0; if (v > 255) v = 255; x->eff = 0x0E; x->par = (uint8_t)(0x80 | (v >> 4)); } break;     /* set panning */
    case 0x1F: if (p >> 4) { x->eff = 0x0E; x->par = (uint8_t)(0xD0 | (p >> 4)); } else if (p & 15) { x->eff = 0x0E; x->par = (uint8_t)(0x90 | (p & 15)); } break;
    default: break;
    }
}

int xmed_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->med = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125;
    s->mixgain = 0.677;      /* libopenmpt mixes MED 3.4 dB quieter than the MOD gain of the replayer (median of 1100 songs with the same length: 3.39 dB, 90 % between 3.24 and 3.53) */
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
    g_8ch = (flags & 0x40) != 0; g_soft = (flags2 & 0x80) != 0;
    { double t = mmd_bpm((int)deftempo, bpm, rpb); s->tempo_exact = t; s->tempo = (int)(t + 0.5); if (s->tempo < 0x20) s->tempo = 0x20; if (s->tempo > 0xFF) s->tempo = 0xFF; if (t < 32.0) s->tempo_exact = 0; }      /* (a start tempo above 255, e.g. 290 in BPM mode, stays as it is) */
    if (s->speed < 1) s->speed = 1; if (s->speed > 32) s->speed = 32;

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
    s->plen = calloc((size_t)s->npat + 1, 1); s->prem = calloc((size_t)s->npat + 1, 1);
    if (!s->plen || !s->prem) return -5;
    for (unsigned i = 0; i < nblk; i++) for (int k = 0; k < map[i].cnt; k++) s->prem[map[i].first + k] = (uint8_t)(map[i].cnt - 1 - k);
    if (!s->pat) return -5;

    int norder = 0; int seqmap[4096];
    for (unsigned i = 0; i < nseq; i++) {
        seqmap[i] = norder;
        unsigned bk = seq[i];
        if (bk >= nblk) continue;
        for (int k = 0; k < map[bk].cnt && norder < 1024; k++) s->orders[norder++] = (uint8_t)(map[bk].first + k);
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
        if (lines % 64) s->plen[map[bi].first + (lines - 1) / 64] = (uint8_t)(lines % 64);      /* a short (last) pattern ends after its last row (a break in a free effect column did not work when all columns were in use) */
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
        m->c2spd = ngp_finetune_c2spd_mod[ft & 15];                    /* the PAL tuning of the finetune (the note table of the replayer picks it by this value); */
        m->octbase = strans + ptrans;                                  /* the transposition shifts the note */
        uint32_t po = smo + 4u * i;
        if (!smo || po + 4 > n) continue;
        uint32_t io = be32(b + po);
        if (!io || io + 6 > n) continue;
        uint32_t len = be32(b + io); int type = (int16_t)be16(b + io + 4);
        if (type < 0) {                                                /* hybrid (-2) / synthetic (-1): the first waveform of the table is the starting sample */
            const uint8_t *sy = b + io + 6;
            if (io + 6 + 276 > n) continue;
            unsigned nwf = be16(sy + 14), vtl = be16(sy + 8), wtl = be16(sy + 10);
            if (!nwf || nwf > 64 || io + 6 + 272 + 4 * nwf > n) continue;
            unsigned wi = 0, wv = 0x40;
            for (unsigned k = 0; k < wtl && k < 128; k++) { int c = sy[144 + k]; if (c < 0x80) { wi = (unsigned)c; break; } if (c == 0xFF) break; if (c >= 0xF0) k++; }   /* skip command parameters */
            if (wi >= nwf) wi = 0;
            for (unsigned k = 0; k < vtl && k < 128; k++) { int c = sy[16 + k]; if (c <= 0x40) { wv = (unsigned)c; break; } if (c == 0xFF) break; }
            int hybrid = type == -2;                                         /* -2 hybrid (sample + waveforms), -1 synthetic */
            m->syn = calloc(1, sizeof(XSyn));
            if (!m->syn) return -5;
            m->syn->vn = (int)(vtl > 128 ? 128 : vtl); m->syn->vspd = sy[12];
            memcpy(m->syn->vtab, sy + 16, 128);
            m->syn->wn = (int)(wtl > 128 ? 128 : wtl); m->syn->wspd = sy[13]; memcpy(m->syn->wtab, sy + 144, 128);
            if (ext && i < extn) { m->syn->hold = ext[i * extsz] & 127; m->syn->decay = ext[i * extsz + 1] & 127; }
            for (unsigned k = 0; k < nwf; k++) {
                uint32_t o2 = io + be32(sy + 272 + 4 * k);
                uint32_t l2, dofs; uint32_t lps = 0, lpe = 0; int lp = 1;
                if (o2 + 2 > n) break;
                if (hybrid && k == 0) {                                     /* a hybrid's first waveform is a plain sample with the loop of the sample header */
                    if (o2 + 6 > n) break;
                    l2 = be32(b + o2); dofs = o2 + 6;
                    if ((int16_t)be16(b + o2 + 4) != 0) l2 = 0;
                    lp = rlen > 1; lps = rep * 2; lpe = lps + rlen * 2;
                } else { l2 = be16(b + o2) * 2u; dofs = o2 + 2; lps = 0; lpe = l2; }
                if (!l2 || dofs + l2 > n) { if (hybrid && k == 0) { m->syn->wf[k] = calloc(2, 1); m->syn->wl[k] = 0; m->syn->nwf = (int)k + 1; continue; } break; }
                if (lpe > l2) lpe = l2;
                m->syn->wf[k] = malloc(l2);
                if (!m->syn->wf[k]) return -5;
                memcpy(m->syn->wf[k], b + dofs, l2); m->syn->wl[k] = (int)l2; m->syn->wls[k] = (int)lps; m->syn->wle[k] = (int)lpe; m->syn->wloop[k] = lp; m->syn->nwf = (int)k + 1;
            }
            if (wi >= (unsigned)m->syn->nwf || !m->syn->wl[wi]) wi = 0;
            if (!m->syn->nwf || !m->syn->wl[wi]) { free(m->syn); m->syn = NULL; continue; }
            { uint32_t wl = (uint32_t)m->syn->wl[wi];
              m->len = wl; m->is16 = 0; m->data = calloc((size_t)wl + 4, 1);
              if (!m->data) return -5;
              memcpy(m->data, m->syn->wf[wi], wl);
              m->ls = (uint32_t)m->syn->wls[wi]; m->le = (uint32_t)m->syn->wle[wi]; m->loop = m->syn->wloop[wi]; }
            m->vol = (int)(vol > 64 ? 64 : vol);
            (void)wv;
            if (type == -1 && ver <= 2) m->octbase -= 24;   /* OctaMED up to 2: synthetic instruments sound two octaves lower than the sample scale */
            continue;
        }
        { int mt = type & 0x0F; if ((type & 0x80) || mt > 6) continue; }   /* packed samples (and unknown kinds): unsupported */
        int mt = type & 0x0F, delta = (type & 0x40) != 0;
        static const uint8_t nparts[7] = { 1, 5, 3, 2, 4, 6, 7 };       /* sample, IFF 5 / 3 / 2 / 4 / 6 / 7 octaves */
        int np = nparts[mt];
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
        uint32_t ls = 0, ll = 0;
        if (rlen > 1) { ls = is16 ? rep : rep * 2; ll = is16 ? rlen : rlen * 2; }
        if (np == 1) {
            if (delta) { if (is16) { int16_t *w = (int16_t *)m->data; for (uint32_t k = 1; k < cnt; k++) w[k] = (int16_t)(w[k] + w[k - 1]); } else for (uint32_t k = 1; k < cnt; k++) m->data[k] = (int8_t)(m->data[k] + m->data[k - 1]); }
            if (rlen > 1) { m->ls = ls; m->le = ls + ll; if (m->le > m->len) m->le = m->len; m->loop = m->ls < m->le; }
        } else {                                                        /* the parts are one octave apart: the shortest first, each twice as long as the one before */
            static const uint8_t omap[6][8] = { {1,1,0,0,0,0,0,0}, {2,2,1,1,0,0,0,0}, {3,3,2,2,1,0,0,0}, {4,3,2,1,1,0,0,0}, {5,4,3,2,1,0,0,0}, {6,5,4,3,2,1,0,0} };
            static const int8_t otr[6][8] = { {0,0,-12,-12,-24,-36,-48,-60}, {0,0,-12,-12,-24,-36,-48,-60}, {0,0,-12,-12,-24,-36,-48,-60}, {12,0,-12,-24,-24,-36,-48,-60}, {12,0,-12,-24,-36,-48,-48,-60}, {12,0,-12,-24,-36,-48,-60,-72} };
            uint32_t base = cnt / ((1u << np) - 1);
            if (!base) { free(m->data); m->data = NULL; m->len = 0; continue; }
            m->oct = calloc((size_t)np, sizeof(XSmp));
            if (!m->oct) return -5;
            m->noct = np; memcpy(m->octmap, omap[np - 2], 8); memcpy(m->octtr, otr[np - 2], 8);
            uint32_t off = 0, pl = base, pls = ls, pll = ll;
            for (int k = 0; k < np; k++) {
                XSmp *q = &m->oct[k];
                if (off + pl > cnt) pl = cnt - off;
                q->len = pl; q->is16 = is16; q->data = (int8_t *)((uint8_t *)m->data + (size_t)off * (is16 ? 2 : 1)); q->vol = m->vol; q->c2spd = 0;
                if (delta) { if (is16) { int16_t *w = (int16_t *)q->data; for (uint32_t j = 1; j < pl; j++) w[j] = (int16_t)(w[j] + w[j - 1]); } else for (uint32_t j = 1; j < pl; j++) q->data[j] = (int8_t)(q->data[j] + q->data[j - 1]); }
                if (rlen > 1) { q->ls = pls; q->le = pls + pll; if (q->le > q->len) q->le = q->len; q->loop = q->ls < q->le; }
                off += pl; pl *= 2; pls *= 2; pll *= 2;
            }
            m->len = m->oct[0].len; m->data = m->data;                 /* (the whole block stays owned by this sample) */
            m->octbase = strans + ptrans;
            
            for (int k = 0; k < np; k++) m->oct[k].c2spd = m->c2spd;
        }
    }
    return 0;
}
