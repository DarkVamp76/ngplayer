/* PSM16 loader (ProTracker Studio 16 / Epic MegaGames, signature "PSM\xFE"; Epic Pinball, Silverball). The effects are
 * ProTracker-like, so the song is handed to the MOD replayer (fmt 1). Layout: 106-byte header, PORD order list, PPAN pan
 * positions, patterns (rows of flagged channel entries), PSAM sample data (delta-coded), PSAH 64-byte sample headers.
 * GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int psm16_probe(const uint8_t *b, size_t n){
    if (n < 164 || memcmp(b, "PSM\xFE", 4)) return 0;
    uint32_t oo = rd32(b + 82), po = rd32(b + 86), pao = rd32(b + 90), sho = rd32(b + 94);
    if (oo < 4 || po < 4 || pao >= n || sho < 4 || sho >= n) return 0;
    return !memcmp(b + oo - 4, "PORD", 4) && !memcmp(b + sho - 4, "PSAH", 4);
}

static int bcd(int v){ return ((v / 10) << 4) | (v % 10); }

static void conv_fx(XCell *x, int e, int p){
    x->eff = 0; x->par = 0;
    int h = p > 15 ? 15 : p, hv = p > 15 ? 15 : p;
    switch (e) {
    case 1:  x->eff = 0x0E; x->par = (uint8_t)(0xA0 | h); break;           /* fine volume slide up */
    case 2:  x->eff = 0x0A; x->par = (uint8_t)(hv << 4); break;            /* volume slide up */
    case 3:  x->eff = 0x0E; x->par = (uint8_t)(0xB0 | h); break;           /* fine volume slide down */
    case 4:  x->eff = 0x0A; x->par = (uint8_t)hv; break;                   /* volume slide down */
    case 10: x->eff = 0x0E; x->par = (uint8_t)(0x10 | h); break;           /* fine porta up */
    case 11: x->eff = 0x01; x->par = (uint8_t)p; break;
    case 12: x->eff = 0x0E; x->par = (uint8_t)(0x20 | h); break;
    case 13: x->eff = 0x02; x->par = (uint8_t)p; break;
    case 14: x->eff = 0x03; x->par = (uint8_t)p; break;
    case 15: x->eff = 0x0E; x->par = (uint8_t)(0x30 | (p & 1)); break;
    case 16: x->eff = 0x05; x->par = (uint8_t)(hv << 4); break;            /* tone porta + volslide up */
    case 17: x->eff = 0x05; x->par = (uint8_t)hv; break;
    case 20: x->eff = 0x04; x->par = (uint8_t)p; break;
    case 21: x->eff = 0x0E; x->par = (uint8_t)(0x40 | (p & 15)); break;
    case 22: x->eff = 0x06; x->par = (uint8_t)(hv << 4); break;            /* vibrato + volslide up */
    case 23: x->eff = 0x06; x->par = (uint8_t)hv; break;
    case 30: x->eff = 0x07; x->par = (uint8_t)p; break;
    case 31: x->eff = 0x0E; x->par = (uint8_t)(0x70 | (p & 15)); break;
    case 40: x->eff = 0x09; x->par = (uint8_t)p; break;
    case 41: x->eff = 0x0E; x->par = (uint8_t)(0x90 | (p & 15)); break;
    case 42: x->eff = 0x0E; x->par = (uint8_t)(0xC0 | (p & 15)); break;
    case 43: x->eff = 0x0E; x->par = (uint8_t)(0xD0 | (p & 15)); break;
    case 50: x->eff = 0x0B; x->par = (uint8_t)p; break;
    case 51: x->eff = 0x0D; x->par = (uint8_t)bcd(p % 100); break;
    case 52: x->eff = 0x0E; x->par = (uint8_t)(0x60 | (p & 15)); break;
    case 53: x->eff = 0x0E; x->par = (uint8_t)(0xE0 | (p & 15)); break;
    case 60: if (p >= 1 && p <= 0x20) { x->eff = 0x0F; x->par = (uint8_t)p; } break;
    case 61: if (p > 0x20) { x->eff = 0x0F; x->par = (uint8_t)p; } break;
    case 70: x->eff = 0x00; x->par = (uint8_t)p; break;
    case 71: x->eff = 0x0E; x->par = (uint8_t)(0x50 | (p & 15)); break;
    case 72: x->eff = 0x0E; x->par = (uint8_t)(0x80 | (p & 15)); break;
    default: break;
    }
}

static int xpsm16_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125;
    if (!psm16_probe(b, n)) return -1;
    memcpy(s->title, b + 4, 20); s->title[20] = 0;
    for (int i = 0; i < 20; i++) if (s->title[i] && (uint8_t)s->title[i] < 32) s->title[i] = 0;
    unsigned sp = b[67], bpm = b[68], ordlen = rd16(b + 72), npat = rd16(b + 74), nsmp = rd16(b + 76);
    uint32_t oo = rd32(b + 82), po = rd32(b + 86), pao = rd32(b + 90), sho = rd32(b + 94);
    if (sp >= 1 && sp <= 0x20) s->speed = (int)sp;
    s->tempo = bpm > 0x20 ? (int)bpm : 125;
    if (!npat || npat > 255 || !ordlen || oo + ordlen > n) return -2;

    /* patterns: walk the size words, then decode */
    uint32_t pofs[256]; int nch = 0; uint32_t o = pao;
    for (unsigned p = 0; p < npat; p++) {
        if (o + 4 > n) return -3;
        pofs[p] = o; unsigned sz = rd16(b + o), ch = b[o + 3];
        if (sz < 4 || o + sz > n) return -3;
        if ((int)ch > nch) nch = (int)ch;
        o += sz;
    }
    { unsigned cm = rd16(b + 80); if ((int)cm > nch) nch = (int)cm; }              /* channels to process (may exceed the per-pattern count) */
    for (unsigned p = 0; p < npat; p++) {                                         /* channel numbers used in the entries */
        const uint8_t *q = b + pofs[p] + 4, *end = b + pofs[p] + rd16(b + pofs[p]);
        for (unsigned r = 0, rows = b[pofs[p] + 2]; r < rows && q < end; r++)
            while (q < end) {
                unsigned fl = *q++; if (!fl) break;
                if ((int)(fl & 31) + 1 > nch) nch = (int)(fl & 31) + 1;
                if (fl & 0x80) q += 2;
                if (fl & 0x40) q += 1;
                if (fl & 0x20) q += 2;
            }
    }
    if (nch < 1) return -3;
    if (nch > 32) nch = 32;
    s->nch = nch; s->npat = (int)npat;
    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    if (po + 12 <= n) for (int i = 0; i < nch && i < 12; i++) if (b[po + i] <= 15 && (i < 4 || b[po + i])) s->chpan[i] = ngp_pan_tab[b[po + i]];

    s->pat = calloc((size_t)npat * 64 * nch + 1, sizeof(XCell));
    if (!s->pat) return -5;
    for (unsigned p = 0; p < npat; p++) {
        const uint8_t *q = b + pofs[p] + 4, *end = b + pofs[p] + rd16(b + pofs[p]);
        unsigned rows = b[pofs[p] + 2];
        if (rows > 64) rows = 64;
        for (unsigned r = 0; r < rows && q < end; r++) {
            XCell *row = &s->pat[((size_t)p * 64 + r) * nch];
            while (q < end) {
                unsigned fl = *q++;
                if (!fl) break;
                unsigned c = fl & 31;
                XCell tmp = {0}, *x = c < (unsigned)nch ? &row[c] : &tmp;
                if (fl & 0x80) { if (q + 2 > end) break; if (q[0]) x->note = (uint8_t)(q[0] + 24); x->inst = q[1] > 99 ? 0 : q[1]; q += 2; }
                if (fl & 0x40) { if (q >= end) break; if (*q <= 64) x->vol1 = (uint8_t)(*q + 1); q++; }
                if (fl & 0x20) { if (q + 2 > end) break; conv_fx(x, q[0], q[1]); q += 2; }
            }
        }
        if (rows < 64 && rows) {                          /* short pattern: break after the last row if none is there already */
            XCell *row = &s->pat[((size_t)p * 64 + rows - 1) * nch]; int has = 0;
            for (int c = 0; c < nch; c++) if (row[c].eff == 0x0B || row[c].eff == 0x0D) has = 1;
            for (int c = 0; c < nch && !has; c++) if (!row[c].eff) { row[c].eff = 0x0D; row[c].par = 0; has = 1; }
        }
    }

    for (unsigned i = 0; i < ordlen && i < 256; i++) s->orders[i] = b[oo + i] < npat ? b[oo + i] : 0xFE;
    s->nord = (int)(ordlen > 256 ? 256 : ordlen);
    while (s->nord > 0 && s->orders[s->nord - 1] > 0xFD) s->nord--;
    if (s->nord < 1) return -4;

    /* samples */
    if (nsmp > 255 || sho + (size_t)nsmp * 64 > n) return -6;
    s->nsmp = 99;
    for (unsigned i = 0; i < nsmp; i++) {
        const uint8_t *h = b + sho + (size_t)i * 64;
        unsigned num = rd16(h + 45), fl = h[47];
        uint32_t off = rd32(h + 37), len = rd32(h + 48), ls = rd32(h + 52), le = rd32(h + 56);
        if (num < 1 || num > 99 || (fl & 0x41)) continue;               /* synth / Gravis patch: unsupported */
        XSmp *m = &s->smp[num];
        int is16 = (fl & 4) != 0;
        m->vol = h[61] > 64 ? 64 : h[61];
        int ft = h[60] & 15; if (ft > 7) ft -= 16;
        unsigned fr = rd16(h + 62); if (!fr) fr = 8448;
        m->c2spd = (uint32_t)(fr * pow(2.0, ft / 96.0) + 0.5);
        if (len < 2 || off >= n) continue;
        size_t avail = n - off; if (len > avail) len = (uint32_t)avail;
        uint32_t cnt = is16 ? len >> 1 : len;
        if (!cnt) continue;
        m->len = cnt; m->is16 = is16;
        m->data = calloc((size_t)cnt * (is16 ? 2 : 1) + 4, 1);
        if (!m->data) return -5;
        const uint8_t *d = b + off;
        int raw = (fl & 0x10) != 0, uns = (fl & 8) != 0;
        if (is16) {
            int16_t *w = (int16_t *)m->data; int cur = 0;
            for (uint32_t k = 0; k < cnt; k++) { int v = (int16_t)rd16(d + 2 * k); if (!raw) { cur = (int16_t)(cur + v); v = cur; } w[k] = (int16_t)(uns ? v ^ 0x8000 : v); }
        } else {
            int cur = 0;
            for (uint32_t k = 0; k < cnt; k++) { int v = d[k]; if (!raw) { cur = (cur + v) & 255; v = cur; } m->data[k] = (int8_t)(uns ? v ^ 0x80 : v); }
        }
        if ((fl & 0x80) && le > ls) {
            if (is16 == 0 && 0) {}
            m->loop = 1; m->ls = ls; m->le = le > m->len ? m->len : le; if (m->ls >= m->le) m->loop = 0;
            if (fl & 0x20) m->pingpong = 1;
        }
    }
    return 0;
}

/* ---- new PSM ("PSM " + size + "FILE": Epic MegaGames MASI; Jazz Jackrabbit, Extreme Pinball, One Must Fall) ---- */
static int psmn_probe(const uint8_t *b, size_t n){
    return n >= 12 + 8 && !memcmp(b, "PSM ", 4) && !memcmp(b + 8, "FILE", 4);
}

static void conv_fx_n(XCell *x, int e, int p){
    x->eff = 0; x->par = 0;
    int hv = (p >> 1) > 15 ? 15 : (p >> 1);
    int pp = p >> 2; if (p > 0 && !pp) pp = 1;
    switch (e) {
    case 0x01: x->eff = 0x0E; x->par = (uint8_t)(0xA0 | hv); break;
    case 0x02: x->eff = 0x0A; x->par = (uint8_t)(hv << 4); break;
    case 0x03: x->eff = 0x0E; x->par = (uint8_t)(0xB0 | hv); break;
    case 0x04: x->eff = 0x0A; x->par = (uint8_t)hv; break;
    case 0x0B: x->eff = 0x0E; x->par = (uint8_t)(0x10 | (p < 4 ? p : (pp > 15 ? 15 : pp))); break;
    case 0x0C: if (p < 4) { x->eff = 0x0E; x->par = (uint8_t)(0x10 | p); } else { x->eff = 0x01; x->par = (uint8_t)(pp > 255 ? 255 : pp); } break;
    case 0x0D: x->eff = 0x0E; x->par = (uint8_t)(0x20 | (p < 4 ? p : (pp > 15 ? 15 : pp))); break;
    case 0x0E: if (p < 4) { x->eff = 0x0E; x->par = (uint8_t)(0x20 | p); } else { x->eff = 0x02; x->par = (uint8_t)(pp > 255 ? 255 : pp); } break;
    case 0x0F: x->eff = 0x03; x->par = (uint8_t)pp; break;
    case 0x10: x->eff = 0x0E; x->par = (uint8_t)(0x30 | (p & 1)); break;
    case 0x11: x->eff = 0x05; x->par = (uint8_t)(hv << 4); break;
    case 0x12: x->eff = 0x05; x->par = (uint8_t)hv; break;
    case 0x15: x->eff = 0x04; x->par = (uint8_t)p; break;
    case 0x16: x->eff = 0x0E; x->par = (uint8_t)(0x40 | (p & 15)); break;
    case 0x17: x->eff = 0x06; x->par = (uint8_t)(hv << 4); break;
    case 0x18: x->eff = 0x06; x->par = (uint8_t)hv; break;
    case 0x1F: x->eff = 0x07; x->par = (uint8_t)p; break;
    case 0x20: x->eff = 0x0E; x->par = (uint8_t)(0x70 | (p & 15)); break;
    case 0x29: x->eff = 0x09; x->par = (uint8_t)p; break;
    case 0x2A: x->eff = 0x0E; x->par = (uint8_t)(0x90 | (p & 15)); break;
    case 0x2B: x->eff = 0x0E; x->par = (uint8_t)(0xC0 | (p & 15)); break;
    case 0x2C: x->eff = 0x0E; x->par = (uint8_t)(0xD0 | (p & 15)); break;
    case 0x33: x->eff = 0x0B; x->par = (uint8_t)(p >> 1); break;
    case 0x34: x->eff = 0x0D; x->par = 0; break;
    case 0x35: x->eff = 0x0E; x->par = (uint8_t)(0x60 | (p & 15)); break;
    case 0x36: x->eff = 0x0E; x->par = (uint8_t)(0xE0 | (p & 15)); break;
    case 0x3D: if (p >= 1 && p <= 0x20) { x->eff = 0x0F; x->par = (uint8_t)p; } break;
    case 0x3E: if (p > 0x20) { x->eff = 0x0F; x->par = (uint8_t)p; } break;
    case 0x47: x->eff = 0x00; x->par = (uint8_t)p; break;
    case 0x48: x->eff = 0x0E; x->par = (uint8_t)(0x50 | (p & 15)); break;
    case 0x49: x->eff = 0x0E; x->par = (uint8_t)(0x80 | (p & 15)); break;
    default: break;
    }
}

static int xpsmn_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125;
    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }

    /* pass 1: chunk table */
    const uint8_t *pb[256]; size_t pbl[256]; int npb = 0;
    const uint8_t *dsmp[256]; size_t dsl[256]; int ndsmp = 0;
    const uint8_t *song = NULL; size_t songl = 0; const uint8_t *titl = NULL; size_t titll = 0;
    size_t o = 12;
    while (o + 8 <= n) {
        uint32_t sz = rd32(b + o + 4); const uint8_t *c = b + o + 8;
        if (sz > n - o - 8) sz = (uint32_t)(n - o - 8);
        if (!memcmp(b + o, "PBOD", 4) && npb < 256) { pb[npb] = c; pbl[npb++] = sz; }
        else if (!memcmp(b + o, "DSMP", 4) && ndsmp < 256) { dsmp[ndsmp] = c; dsl[ndsmp++] = sz; }
        else if (!memcmp(b + o, "SONG", 4) && !song) { song = c; songl = sz; }
        else if (!memcmp(b + o, "TITL", 4)) { titl = c; titll = sz; }
        o += 8 + (size_t)sz;
    }
    if (!song || songl < 12 || !npb) return -2;
    if (titl) { size_t l = titll > 20 ? 20 : titll; memcpy(s->title, titl, l); s->title[l] = 0; }

    /* SONG: "MAINSONG ", compression, channels, then sub-chunks */
    int nch = song[10], fixed = nch >= 1 && nch <= 32;       /* channel count from the SONG chunk is authoritative */
    size_t q = 11; const uint8_t *oplh = NULL; size_t oplhl = 0;
    while (q + 8 <= songl) {
        uint32_t sz = rd32(song + q + 4);
        if (!memcmp(song + q, "OPLH", 4)) { oplh = song + q + 8; oplhl = sz; if (oplhl > songl - q - 8) oplhl = songl - q - 8; }
        if (!memcmp(song + q, "DATE", 4) && sz == 6) { q += 8 + sz; continue; }
        if (sz > songl - q - 8) break;
        q += 8 + sz;
    }
    if (!oplh || oplhl < 2) return -3;

    /* pattern table: id string -> index */
    uint8_t pid[256][4]; unsigned prow[256];
    for (int i = 0; i < npb; i++) { if (pbl[i] < 10) return -4; memcpy(pid[i], pb[i] + 4, 4); prow[i] = rd16(pb[i] + 8); }

    /* pass over the PBOD chunks to find the channel count */
    for (int i = 0; i < npb; i++) {
        const uint8_t *d = pb[i], *e = pb[i] + pbl[i], *r = d + 10;
        for (unsigned row = 0; row < prow[i] && r + 2 <= e; row++) {
            unsigned rs = rd16(r); const uint8_t *re = r + rs, *x = r + 2;
            if (rs < 2 || re > e) break;
            while (x + 2 <= re) {
                unsigned fl = x[0], len = 2 + ((fl & 0x80) ? 1 : 0) + ((fl & 0x40) ? 1 : 0) + ((fl & 0x20) ? 1 : 0) + ((fl & 0x10) ? 2 : 0);
                if (x + len > re || x[1] > 31) break;                       /* damaged entry: ignore the rest of the row */
                if (!fixed && (int)x[1] + 1 > nch) nch = x[1] + 1;
                x += len;
            }
            r = re;
        }
    }
    if (nch < 1) return -4;
    if (nch > 32) nch = 32;
    s->nch = nch;

    /* OPLH opcodes */
    int cnt = (int)rd16(oplh); const uint8_t *p = oplh + 2, *pe = oplh + oplhl;
    int nord = 0, restart_chunk = -1, restart_ord = 0, chunk = 0;
    while (chunk < cnt && p < pe) {
        int op = *p;
        switch (op) {
        case 0x00: p = pe; continue;
        case 0x01: {
            if (p + 5 > pe) { p = pe; continue; }
            int found = 0xFE;
            for (int i = 0; i < npb; i++) if (!memcmp(pid[i], p + 1, 4)) { found = i; break; }
            if (nord < 256) s->orders[nord++] = (uint8_t)found;
            p += 5; break; }
        case 0x02: p += 7; break;
        case 0x03: p += 4; break;
        case 0x04: if (p + 3 <= pe) { restart_chunk = rd16(p + 1); restart_ord = -1; } p += 3; break;
        case 0x05: p += 3; break;
        case 0x06: p += 2; break;
        case 0x07: if (p[1] >= 1 && p[1] <= 0x20) s->speed = p[1]; p += 2; break;
        case 0x08: s->tempo = p[1] > 0x20 ? p[1] : 125; p += 2; break;
        case 0x0C: p += 7; break;
        case 0x0D: if (p + 4 <= pe && p[1] < 32) { if (p[3] == 0) s->chpan[p[1]] = p[2] ^ 0x80; else s->chpan[p[1]] = 128; } p += 4; break;
        case 0x0E: p += 3; break;
        default: p = pe; continue;
        }
        if (restart_chunk == chunk && restart_ord < 0) restart_ord = nord;
        chunk++;
    }
    (void)restart_chunk;
    s->nord = nord;
    while (s->nord > 0 && s->orders[s->nord - 1] > 0xFD) s->nord--;
    if (s->nord < 1) return -5;
    s->npat = npb;

    /* patterns */
    s->pat = calloc((size_t)npb * 64 * nch + 1, sizeof(XCell));
    if (!s->pat) return -6;
    for (int i = 0; i < npb; i++) {
        const uint8_t *d = pb[i], *e = pb[i] + pbl[i], *r = d + 10;
        unsigned rows = prow[i] > 64 ? 64 : prow[i];
        for (unsigned row = 0; row < prow[i] && r + 2 <= e; row++) {
            unsigned rs = rd16(r); const uint8_t *re = r + rs, *x = r + 2;
            if (rs < 2 || re > e) break;
            XCell *cells = row < 64 ? &s->pat[((size_t)i * 64 + row) * nch] : NULL;
            while (x + 2 <= re) {
                unsigned fl = x[0], c = x[1], len = 2 + ((fl & 0x80) ? 1 : 0) + ((fl & 0x40) ? 1 : 0) + ((fl & 0x20) ? 1 : 0) + ((fl & 0x10) ? 2 : 0);
                if (x + len > re || c > 31) break;
                x += 2;
                XCell tmp = {0}, *cell = (cells && c < (unsigned)nch) ? &cells[c] : &tmp;
                if (fl & 0x80) { unsigned nt = *x++; unsigned k = (nt >> 4) * 12 + (nt & 15); cell->note = (uint8_t)(k > 107 ? 107 : k); }
                if (fl & 0x40) { unsigned in = *x++; cell->inst = (uint8_t)(in + 1); }
                if (fl & 0x20) { unsigned v = *x++; v >>= 1; if (v > 64) v = 64; cell->vol1 = (uint8_t)(v + 1); }
                if (fl & 0x10) { conv_fx_n(cell, x[0], x[1]); x += 2; }
            }
            r = re;
        }
        if (rows < 64 && rows) {
            XCell *row = &s->pat[((size_t)i * 64 + rows - 1) * nch]; int has = 0;
            for (int c = 0; c < nch; c++) if (row[c].eff == 0x0B || row[c].eff == 0x0D) has = 1;
            for (int c = 0; c < nch && !has; c++) if (!row[c].eff) { row[c].eff = 0x0D; row[c].par = 0; has = 1; }
        }
    }

    /* samples: content = flags(1) name(8) id(4) name(33) hdr... ; header starts at 45, data at 96 */
    s->nsmp = 99;
    for (int i = 0; i < ndsmp; i++) {
        const uint8_t *d = dsmp[i]; size_t dl = dsl[i];
        if (dl < 96) continue;
        unsigned num = rd16(d + 45 + 7);
        uint32_t len = rd32(d + 45 + 9), ls = rd32(d + 45 + 13), le = rd32(d + 45 + 17);
        int vol = d[45 + 23] >> 1; unsigned rate = rd16(d + 45 + 28);
        if (num + 1 > 99) continue;
        XSmp *m = &s->smp[num + 1];
        m->vol = vol > 64 ? 64 : vol; m->c2spd = rate ? rate : 8363;
        if (len > dl - 96) len = (uint32_t)(dl - 96);
        if (len < 2) continue;
        m->len = len; m->data = calloc((size_t)len + 4, 1);
        if (!m->data) return -6;
        int cur = 0;
        for (uint32_t k = 0; k < len; k++) { cur = (cur + d[96 + k]) & 255; m->data[k] = (int8_t)cur; }
        if ((d[0] & 0x80) && ls < len) {
            m->loop = 1; m->ls = ls; m->le = (le == 0xFFFFFFFFu || le > len) ? len : le;
            if (m->ls >= m->le) m->loop = 0;
        }
    }
    (void)restart_ord;
    return 0;
}

int psm_probe(const uint8_t *b, size_t n){ return psm16_probe(b, n) || psmn_probe(b, n); }
int xpsm_load(XSong *s, const uint8_t *b, size_t n){ return psm16_probe(b, n) ? xpsm16_load(s, b, n) : xpsmn_load(s, b, n); }
