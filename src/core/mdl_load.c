/* Digitrakker (MDL) loader, signature "DMDL" (versions 0.0 and 1.x). Up to 32 channels, instruments with key ranges, volume and
 * panning envelopes, fadeout, 8/16-bit samples, optionally Huffman-style packed. The song is handed to the XM replayer (fmt 4).
 * Blocks: IN info, ME message, PN pattern names, PA patterns (track numbers per channel), TR tracks (packed rows),
 * II instruments, VE/PE/FE envelopes, IS sample info, SA sample data.
 * Not implemented: frequency envelopes, auto vibrato, instrument panning, most E-commands. GPL-3.0-or-later */
#include "core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MDL_NOTE_OFF 12         /* MDL note n (1 = C-0)  ->  XM note n - 12 (XM C-4 = 49 is MDL C-4 = 49 + 12 ... see relnote below) */

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int mdl_probe(const uint8_t *b, size_t n){
    return n >= 12 && !memcmp(b, "DMDL", 4) && (b[4] >> 4) <= 1;
}

typedef struct { const uint8_t *d; size_t n; } Blk;

static Blk find_blk(const uint8_t *b, size_t n, const char *id){
    Blk r = { NULL, 0 }; size_t p = 5;
    while (p + 6 <= n) {
        uint32_t l = rd32(b + p + 2);
        if (l > n - p - 6) l = (uint32_t)(n - p - 6);
        if (b[p] == id[0] && b[p + 1] == id[1]) { r.d = b + p + 6; r.n = l; return r; }
        p += 6 + (size_t)l;
    }
    return r;
}

/* LSB-first bit reader */
typedef struct { const uint8_t *d; size_t nbits, pos; } BR;
static unsigned br_bits(BR *r, int n){
    unsigned v = 0;
    for (int i = 0; i < n; i++) { unsigned b = 0; if (r->pos < r->nbits) b = (r->d[r->pos >> 3] >> (r->pos & 7)) & 1; r->pos++; v |= b << i; }
    return v;
}
static int br_delta(BR *r){
    int sign = (int)br_bits(r, 1), v;
    if (br_bits(r, 1)) v = (int)br_bits(r, 3);
    else { v = 8; while (!br_bits(r, 1) && r->pos < r->nbits) v += 16; v += (int)br_bits(r, 4); }
    return sign ? (v ^ 255) : v;
}

/* decode one track into rows[256][6]; returns number of rows */
static int mdl_track(const uint8_t *q, size_t len, uint8_t rows[256][6]){
    int n = 0; size_t i = 0;
    while (i < len && n < 256) {
        unsigned c = q[i++], x = c >> 2, y = c & 3;
        if (y == 0) { for (unsigned k = 0; k <= x && n < 256; k++) memset(rows[n++], 0, 6); }
        else if (y == 1) { for (unsigned k = 0; k <= x && n < 256; k++) { if (n) memcpy(rows[n], rows[n - 1], 6); else memset(rows[n], 0, 6); n++; } }
        else if (y == 2) { if (x < (unsigned)n) memcpy(rows[n], rows[x], 6); else memset(rows[n], 0, 6); n++; }
        else {
            uint8_t r[6] = {0};
            for (int k = 0; k < 4; k++) if (x & (1 << k)) { if (i < len) r[k] = q[i]; i++; }
            if (x & 16) { if (i < len) r[4] = q[i]; i++; }
            if (x & 32) { if (i < len) r[5] = q[i]; i++; }
            memcpy(rows[n++], r, 6);
        }
    }
    return n;
}


/* effect (e, p): column 1 uses e1 = low nibble, column 2 = high nibble; col2 1-6 are the volume/global slides */
static void mdl_fx(XCell *x, int e, int p, int col2){
    uint8_t eff = 0, par = 0;
    if (col2 && e >= 1 && e <= 5) {
        /* second column only: 1 volume slide up, 2 down (params >= 0xE0 = fine slides, in 1/256 units), 3 retrigger, 4 tremolo, 5 tremor */
        int fine = p >= 0xE0, v = fine ? (p & 15) : (p + 3) / 4;
        if (v > 15) v = 15;
        switch (e) {
        case 1: if (!v) return; if (fine) { if (!x->vol1) { x->vol1 = (uint8_t)(0x90 | v); return; } eff = 0x0E; par = (uint8_t)(0xA0 | v); }
                else { if (!x->vol1) { x->vol1 = (uint8_t)(0x70 | v); return; } eff = 0x0A; par = (uint8_t)(v << 4); } break;
        case 2: if (!v) return; if (fine) { if (!x->vol1) { x->vol1 = (uint8_t)(0x80 | v); return; } eff = 0x0E; par = (uint8_t)(0xB0 | v); }
                else { if (!x->vol1) { x->vol1 = (uint8_t)(0x60 | v); return; } eff = 0x0A; par = (uint8_t)v; } break;
        case 3: if (p & 15) { eff = 0x1B; par = (uint8_t)(p & 15); } break;
        default: break;
        }
    } else switch (e) {
        case 1: eff = 1; par = (uint8_t)p; break;
        case 2: eff = 2; par = (uint8_t)p; break;
        case 3: eff = 3; par = (uint8_t)p; break;
        case 4: eff = 4; par = (uint8_t)p; break;
        case 5: eff = 0; par = (uint8_t)p; if (!p) return; break;       /* arpeggio */
        case 7: eff = 0x0F; par = (uint8_t)(p < 0x20 ? 0x20 : p); break; /* tempo (BPM) */
        case 8: eff = 8; par = (uint8_t)(((p & 0x7F) * 2) > 255 ? 255 : (p & 0x7F) * 2); break;
        case 0x0B: eff = 0x0B; par = (uint8_t)p; break;
        case 0x0C: eff = 0x10; par = (uint8_t)(((p + 2) / 4) > 64 ? 64 : (p + 2) / 4); break;
        case 0x0D: eff = 0x0D; par = (uint8_t)p; break;
        case 0x0E:
            switch (p >> 4) {
            case 1: case 2: case 4: case 5: case 6: case 7: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E:
                eff = 0x0E; par = (uint8_t)p; break;      /* ProTracker-style E commands the XM engine knows */
            case 9: if (p & 15) { eff = 0x1B; par = (uint8_t)(p & 15); } break;
            default: break;
            }
            break;
        case 0x0F: if (p >= 1) { eff = p < 32 ? 0x0F : 0x22; par = (uint8_t)p; } break;   /* speed (0x22: any speed, MDL allows up to 255) */
        default: break;
    }
    if (!eff && !par && !(e == 5)) return;
    if (!x->eff && !x->par) { x->eff = eff; x->par = par; }
}

int xmdl_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 4; s->maxvol = 0x40; s->gvol = 255; s->linear = 1;
    if (!mdl_probe(b, n)) return -1;
    int v1 = (b[4] >> 4) != 0;
    Blk IN = find_blk(b, n, "IN"), PA = find_blk(b, n, "PA"), TR = find_blk(b, n, "TR"), IS = find_blk(b, n, "IS"), SA = find_blk(b, n, "SA");
    Blk II = find_blk(b, n, "II"), VE = find_blk(b, n, "VE"), PE = find_blk(b, n, "PE");
    if (!IN.d || IN.n < 91 || !PA.d || !TR.d || !IS.d || !SA.d) return -2;
    memcpy(s->title, IN.d, 20); s->title[20] = 0;
    for (int i = 19; i >= 0 && (s->title[i] == ' ' || (uint8_t)s->title[i] < 32); i--) s->title[i] = 0;
    for (int i = 0; i < 20; i++) if ((uint8_t)s->title[i] < 32) s->title[i] = 0;
    unsigned nord = rd16(IN.d + 52), rep = rd16(IN.d + 54);
    s->speed = IN.d[57] ? IN.d[57] : 6; s->tempo = IN.d[58] >= 4 ? IN.d[58] : 125;
    const uint8_t *chpan = IN.d + 59;
    if (nord > 255) nord = 255;
    if (IN.n < 91 + nord) return -2;
    memcpy(s->orders, IN.d + 91, nord); s->nord = (int)nord; s->restart = (int)rep < (int)nord ? (int)rep : 0;
    if (!nord) return -2;

    /* tracks */
    if (TR.n < 2) return -3;
    unsigned ntr = rd16(TR.d);
    const uint8_t **tp = calloc(ntr + 1, sizeof *tp); size_t *tl = calloc(ntr + 1, sizeof *tl);
    if (!tp || !tl) return -5;
    { size_t o = 2; for (unsigned t = 1; t <= ntr && o + 2 <= TR.n; t++) { size_t l = rd16(TR.d + o); o += 2; if (o + l > TR.n) l = TR.n - o; tp[t] = TR.d + o; tl[t] = l; o += l; } }

    /* patterns: pass 1 (sizes) */
    unsigned npat = PA.d[0]; int nch = 0;
    size_t pp[256]; int prow[256], pch[256]; size_t o = 1;
    if (npat > 255) npat = 255;
    for (unsigned p = 0; p < npat; p++) {
        int c = 32, r = 64;
        if (v1) { if (o + 18 > PA.n) { npat = p; break; } c = PA.d[o]; r = PA.d[o + 1] + 1; o += 18; }
        if (c > 32) c = 32;
        if (o + (size_t)(v1 ? PA.d[o - 18] : 32) * 2 > PA.n) { npat = p; break; }
        pp[p] = o; prow[p] = r; pch[p] = c;
        o += (size_t)(v1 ? PA.d[o - 18] : 32) * 2;
    }
    if (!npat) { free(tp); free(tl); return -3; }
    int maxused = 0;
    for (unsigned p = 0; p < npat; p++)
        for (int c = 0; c < pch[p]; c++) if (rd16(PA.d + pp[p] + 2 * c) && c + 1 > maxused) maxused = c + 1;
    nch = maxused < 1 ? 1 : maxused;
    s->nch = nch; s->npat = (int)npat;
    for (int i = 0; i < 32; i++) { s->chpan[i] = -1; s->chen[i] = !(chpan[i] & 0x80); }
    s->prows = calloc(npat, sizeof(int));
    size_t total = 0;
    for (unsigned p = 0; p < npat; p++) { s->prows[p] = prow[p]; total += (size_t)prow[p] * nch; }
    s->pat = calloc(total ? total : 1, sizeof(XCell));
    if (!s->prows || !s->pat) { free(tp); free(tl); return -5; }
    for (int i = 0; i < s->nord; i++) if (s->orders[i] >= npat) s->orders[i] = 0;

    /* samples (IS + SA) */
    unsigned nis = IS.d[0]; size_t esz = v1 ? 59 : 57;
    if (IS.n < 1 + (size_t)nis * esz) nis = (unsigned)((IS.n - 1) / esz);
    int8_t **sd = calloc(nis + 1, sizeof *sd); uint32_t *slen = calloc(nis + 1, 4), *sls = calloc(nis + 1, 4), *sle = calloc(nis + 1, 4);
    int *s16 = calloc(nis + 1, sizeof(int)), *spp = calloc(nis + 1, sizeof(int)), *snum = calloc(nis + 1, sizeof(int)), *svol = calloc(nis + 1, sizeof(int));
    double *sc4 = calloc(nis + 1, sizeof(double));
    { size_t q = 0;
      for (unsigned i = 0; i < nis; i++) {
        const uint8_t *e = IS.d + 1 + i * esz;
        uint32_t c4, ln, ls, ll; int fl, vol = 255;
        snum[i] = e[0];
        if (v1) { c4 = rd32(e + 41); ln = rd32(e + 45); ls = rd32(e + 49); ll = rd32(e + 53); fl = e[58]; }
        else { c4 = rd16(e + 41); ln = rd32(e + 43); ls = rd32(e + 47); ll = rd32(e + 51); vol = e[55]; fl = e[56]; }
        int is16 = fl & 1, pk = (fl >> 2) & 3;
        uint32_t cnt = is16 ? ln >> 1 : ln;
        sc4[i] = c4 ? (double)c4 : 8363.0; svol[i] = vol; s16[i] = is16; spp[i] = (fl & 2) != 0;
        if (is16) { ls >>= 1; ll >>= 1; }
        slen[i] = cnt; sls[i] = ls; sle[i] = ll ? ls + ll : 0;
        size_t avail = q <= SA.n ? SA.n - q : 0;
        if (pk) {
            if (avail < 4) continue;
            size_t pl = rd32(SA.d + q); q += 4;
            if (pl > SA.n - q) pl = SA.n - q;
            if (cnt) {
                sd[i] = calloc((size_t)cnt * (is16 ? 2 : 1) + 4, 1);
                if (!sd[i]) continue;
                BR r = { SA.d + q, pl * 8, 0 };
                if (is16) { int16_t *w = (int16_t *)sd[i]; int hi = 0; for (uint32_t k = 0; k < cnt; k++) { unsigned lo = br_bits(&r, 8); hi = (hi + br_delta(&r)) & 255; w[k] = (int16_t)((hi << 8) | lo); } }
                else { int cur = 0; for (uint32_t k = 0; k < cnt; k++) { cur = (cur + br_delta(&r)) & 255; sd[i][k] = (int8_t)cur; } }
            }
            q += pl;
        } else {
            size_t bytes = (size_t)cnt * (is16 ? 2 : 1);
            if (bytes > avail) { bytes = avail; cnt = (uint32_t)(bytes / (is16 ? 2 : 1)); slen[i] = cnt; }
            if (cnt) { sd[i] = calloc(bytes + 4, 1); if (sd[i]) memcpy(sd[i], SA.d + q, bytes); }
            q += bytes;
        }
      }
    }

    /* envelopes: number -> point set */
    typedef struct { int n; uint16_t x[12]; uint8_t y[12]; int sus, ls, le, susen, loopen; int ok; } Env;
    static Env venv[64], penv[64];
    memset(venv, 0, sizeof venv); memset(penv, 0, sizeof penv);
    for (int k = 0; k < 2; k++) {
        Blk B = k ? PE : VE; Env *E = k ? penv : venv;
        if (!B.d || !B.n) continue;
        unsigned cnt = B.d[0];
        for (unsigned i = 0; i < cnt && 1 + (size_t)(i + 1) * 33 <= B.n; i++) {
            const uint8_t *e = B.d + 1 + (size_t)i * 33; int num = e[0];
            if (num > 63) continue;
            Env *v = &E[num]; int np = 0, tick = 0;
            for (int j = 0; j < 15 && np < 12; j++) {
                int dx = e[1 + 2 * j], y = e[2 + 2 * j];
                if (j > 0) { if (!dx) break; tick += dx; }
                v->x[np] = (uint16_t)tick; v->y[np] = (uint8_t)(y > 64 ? 64 : y); np++;
            }
            if (np < 2) { if (np == 1) { v->x[1] = (uint16_t)(v->x[0] + 1); v->y[1] = v->y[0]; } np = 2; }
            v->n = np; v->sus = e[31] & 15; v->susen = (e[31] & 0x10) != 0; v->loopen = (e[31] & 0x20) != 0;
            v->ls = e[32] & 15; v->le = e[32] >> 4;
            if (v->sus >= np) v->sus = np - 1; if (v->ls >= np) v->ls = np - 1; if (v->le >= np) v->le = np - 1;
            v->ok = 1;
        }
    }

    /* instruments */
    int ninst = 0, nxs = 0;
    if (II.d && II.n >= 1) {
        unsigned ni = II.d[0]; size_t q = 1;
        for (unsigned i = 0; i < ni && q + 2 <= II.n; i++) {
            int num = II.d[q], ns = II.d[q + 1]; q += 34 + (size_t)ns * 14;
            if (num > ninst) ninst = num; nxs += ns > 16 ? 16 : ns;
        }
    } else { ninst = (int)nis; nxs = (int)nis; }
    if (ninst > 255) ninst = 255;
    s->ninst = ninst;
    s->inst = calloc((size_t)ninst + 1, sizeof(XInst));
    s->xsmp = calloc((size_t)nxs + 1, sizeof(XSmp));
    if (!s->inst || !s->xsmp) return -5;
    #define FINDSMP(num, idx) do { idx = -1; for (unsigned _k = 0; _k < nis; _k++) if (snum[_k] == (num)) { idx = (int)_k; break; } } while (0)
    #define FILLSMP(m, i, vol64) do { \
        (m)->len = slen[i]; (m)->is16 = s16[i]; (m)->vol = (vol64); (m)->pan = 128; \
        if (sle[i] > sls[i] && sls[i] < slen[i]) { (m)->loop = 1; (m)->ls = sls[i]; (m)->le = sle[i] > slen[i] ? slen[i] : sle[i]; (m)->pingpong = spp[i]; } \
        double _st = 12.0 * log(sc4[i] / 8363.0) / log(2.0) + MDL_NOTE_OFF; int _rn = (int)floor(_st + 0.5); \
        (m)->relnote = _rn; (m)->finetune = (int)floor((_st - _rn) * 128.0 + 0.5); \
        if ((m)->finetune > 127) (m)->finetune = 127; if ((m)->finetune < -128) (m)->finetune = -128; \
        size_t _b = (size_t)slen[i] * (s16[i] ? 2 : 1); \
        if (sd[i] && _b) { (m)->data = calloc(_b + 4, 1); if ((m)->data) memcpy((m)->data, sd[i], _b); else (m)->len = 0; } else (m)->len = 0; \
    } while (0)
    if (II.d && II.n >= 1) {
        unsigned ni = II.d[0]; size_t q = 1;
        for (unsigned i = 0; i < ni && q + 2 <= II.n; i++) {
            int num = II.d[q], ns = II.d[q + 1]; const uint8_t *ent = II.d + q + 34;
            q += 34 + (size_t)ns * 14;
            if (q > II.n || num < 1 || num > 255) continue;
            XInst *I = &s->inst[num]; int cnt = ns > 16 ? 16 : ns, prev = 0;
            I->nsmp = cnt;
            for (int k = 0; k < cnt; k++) {
                const uint8_t *e = ent + k * 14; int si; FINDSMP(e[0], si);
                XSmp *m = &s->xsmp[s->nxsmp]; I->smp[k] = s->nxsmp++;
                if (si >= 0) { int vol = (e[2] + 2) / 4; FILLSMP(m, si, vol > 64 ? 64 : vol); }
                int last = e[1] > 119 ? 119 : e[1];
                for (int nn = prev + 1; nn <= last; nn++) { int xn = nn - MDL_NOTE_OFF; if (xn >= 1 && xn <= 96) I->keymap[xn - 1] = (uint8_t)k; }
                if (last > prev) prev = last;
                if (k == 0) {                                   /* envelopes / fadeout of the first sample entry */
                    int ve = e[3], pe = e[5];
                    if ((ve & 0x80) && venv[ve & 63].ok) { Env *v = &venv[ve & 63]; I->venv.n = v->n; for (int j = 0; j < v->n; j++) { I->venv.x[j] = v->x[j]; I->venv.y[j] = v->y[j]; } I->venv.sus = v->sus; I->venv.ls = v->ls; I->venv.le = v->le; I->venv.on = 1; I->venv.susen = v->susen; I->venv.loopen = v->loopen; }
                    if ((pe & 0x80) && penv[pe & 63].ok) { Env *v = &penv[pe & 63]; I->penv.n = v->n; for (int j = 0; j < v->n; j++) { I->penv.x[j] = v->x[j]; I->penv.y[j] = v->y[j]; } I->penv.sus = v->sus; I->penv.ls = v->ls; I->penv.le = v->le; I->penv.on = 1; I->penv.susen = v->susen; I->penv.loopen = v->loopen; }
                    unsigned fo = rd16(e + 6); I->fadeout = fo > 32767 ? 32767 : (int)fo; if (!fo && !I->venv.on) I->fadeout = 8192;   /* same scale as XM (verified against the IT conversion of Calliope) */
                }
            }
            for (int xn = 1; xn <= 96; xn++) if (I->keymap[xn - 1] >= cnt) I->keymap[xn - 1] = (uint8_t)(cnt ? cnt - 1 : 0);
        }
    } else {
        for (unsigned i = 0; i < nis; i++) {
            int num = snum[i]; if (num < 1 || num > 255) continue;
            XInst *I = &s->inst[num]; XSmp *m = &s->xsmp[s->nxsmp];
            I->nsmp = 1; I->smp[0] = s->nxsmp++;
            int vol = (svol[i] + 2) / 4; FILLSMP(m, i, vol > 64 ? 64 : vol);
            I->fadeout = 32767;
        }
    }

    /* pattern cells */
    for (unsigned p = 0; p < npat; p++) {
        size_t base = 0; for (unsigned k = 0; k < p; k++) base += (size_t)prow[k] * nch;
        for (int c = 0; c < pch[p] && c < nch; c++) {
            unsigned t = rd16(PA.d + pp[p] + 2 * c);
            if (!t || t > ntr || !tp[t]) continue;
            uint8_t rows[256][6]; int nr = mdl_track(tp[t], tl[t], rows);
            for (int r = 0; r < nr && r < prow[p]; r++) {
                const uint8_t *q = rows[r]; XCell *x = &s->pat[base + (size_t)r * nch + c];
                int note = q[0];
                if (note == 255) x->note = 97;
                else if (note >= 1 + MDL_NOTE_OFF) { int xn = note - MDL_NOTE_OFF; x->note = (uint8_t)(xn > 96 ? 96 : xn); }
                if (q[1] && q[1] <= ninst) x->inst = q[1];
                if (q[2]) { int v = (q[2] * 64 + 127) / 255; x->vol1 = (uint8_t)(0x10 + (v > 64 ? 64 : v)); }
                { int e1 = q[3] & 15, e2 = q[3] >> 4;
                  if (!(e1 == 14 && (q[4] >> 4) == 6 && e2 >= 1 && e2 <= 5))   /* like libopenmpt: E6x is dropped when column 2 holds a slide/retrigger/tremolo/tremor */
                      mdl_fx(x, e1, q[4], 0); }
                mdl_fx(x, q[3] >> 4, q[5], 1);
            }
        }
    }
    /* channel panning: XM resets pan on every instrument trigger, so set it with 8xx next to each note */
    for (unsigned p = 0; p < npat; p++) {
        size_t base = 0; for (unsigned k = 0; k < p; k++) base += (size_t)prow[k] * nch;
        for (int r = 0; r < prow[p]; r++) for (int c = 0; c < nch; c++) {
            XCell *x = &s->pat[base + (size_t)r * nch + c]; int pn = chpan[c] & 0x7F;
            if (pn != 64 && x->note && x->note < 97 && !x->eff && !x->par) { x->eff = 8; x->par = (uint8_t)(pn * 2 > 255 ? 255 : pn * 2); }
        }
    }
    for (unsigned i = 0; i < nis; i++) free(sd[i]);
    free(sd); free(slen); free(sls); free(sle); free(s16); free(spp); free(snum); free(svol); free(sc4); free(tp); free(tl);
    return 0;
}
