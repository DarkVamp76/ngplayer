/* Farandole Composer (FAR) loader. 16 channels, 8/16-bit samples, signature "FAR\xFE" (Digital Infinity / Daniel Potter, 1993).
 * The song is handed to the MOD replayer (fmt 1). Layout: 98-byte header + song text, order list, 256 pattern sizes, patterns
 * (rows x 16 channels x 4 bytes: note, sample, volume, effect/parameter), 64-bit sample map, then sample records
 * (48-byte header + data) for each map bit that is set.
 * Tempo: coarse tempo (F?) picks 128/n BPM, D?/E? add a fine offset; the "new tempo mode" (default) turns that into
 * speed + tempo the way the original player does, so the loader tracks the tempo state and emits explicit 0F cells.
 * GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

#define FAR_NOTE_OFF 23        /* FAR note n (C-0 = 1)  ->  engine note (48 = c2spd) */
#define FAR_C2SPD    16726
#define FAR_FINE_TEMPO 0       /* D?/E? fine tempo: off, libopenmpt ignores it too and Cold Recognition then runs at the right speed */

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int far_probe(const uint8_t *b, size_t n){
    return n >= 98 + 771 && !memcmp(b, "FAR\xFE", 4) && b[44] == 13 && b[45] == 10 && b[46] == 26;
}

typedef struct { int coarse, fine, newmode, depth, spd, tmp; } FarSt;

static int far_tbl(int c){ return c ? 128 / c : 256; }

/* tempo state -> speed / MOD tempo */
static void far_timing(const FarSt *st, int *spd, int *tmp){
    int bpm = far_tbl(st->coarse) + st->fine, sp;
    if (bpm < 1) bpm = 1;
    if (!st->newmode) sp = 4;
    else {
        long div = 1197255 / bpm; sp = 0;
        while (div > 0xFFFF) { div >>= 1; bpm *= 2; sp++; }
        if (sp >= 2) sp++;
        sp += 4;
    }
    int t = (bpm * 5 + 1) / 2;                       /* bpm * 2.5 */
    while (t > 255 && sp > 1 && !(sp & 1)) { sp >>= 1; t >>= 1; }
    if (t > 255) t = 255;
    if (t < 0x21) t = 0x21;
    *spd = sp; *tmp = t;
}

static XCell *far_free_slot(XCell *row, int nch){
    for (int c = 0; c < nch; c++) if (!row[c].eff && !row[c].par) return &row[c];
    return NULL;
}

static void far_fx(XCell *x, int e, int p, FarSt *st, int *pv){
    x->eff = 0; x->par = 0;
    switch (e) {
    case 0:
        if (p == 3) { x->eff = 0x0E; x->par = 0xC0; }                      /* note cut */
        else if (p == 4) st->newmode = 0;
        else if (p == 5) st->newmode = 1;
        break;
    case 1: x->eff = 0x0E; x->par = (uint8_t)(0x10 | p); break;           /* pitch up: once per row (fine slide) */
    case 2: x->eff = 0x0E; x->par = (uint8_t)(0x20 | p); break;           /* pitch down: once per row */
    case 3: x->eff = 0x03; x->par = (uint8_t)(p < 2 ? 1 : (p + 1) / 2); break;         /* tone portamento: slow glide (tuned by ear) */
    case 4: if (x->note) { int iv = st->spd / (p ? p : 1); if (iv < 1) iv = 1; x->eff = 0x0E; x->par = (uint8_t)(0x90 | (iv > 15 ? 15 : iv)); } break;
    case 5: st->depth = p; break;
    case 6: case 9: {
        int rate = p * 3 > 15 ? 15 : p * 3;
        if (!p) { *pv = 0; break; }
        x->eff = 0x04; x->par = (uint8_t)((rate << 4) | (st->depth > 15 ? 15 : st->depth));
        if (e == 9) *pv = x->par; else *pv = 0;
        break; }
    case 7: x->eff = 0x0E; x->par = (uint8_t)(0xA0 | (p * 4 > 15 ? 15 : p * 4)); break;   /* fine volume up */
    case 8: x->eff = 0x0E; x->par = (uint8_t)(0xB0 | (p * 4 > 15 ? 15 : p * 4)); break;   /* fine volume down */
    case 0x0B: x->eff = 0x0E; x->par = (uint8_t)(0x80 | p); break;       /* balance */
    case 0x0D: if (!FAR_FINE_TEMPO) break; st->fine -= p; if (far_tbl(st->coarse) + st->fine <= 0) st->fine = 1 - far_tbl(st->coarse); break;
    case 0x0E: if (!FAR_FINE_TEMPO) break; st->fine += p; if (far_tbl(st->coarse) + st->fine >= 100) st->fine = 100 - far_tbl(st->coarse); break;
    case 0x0F: st->coarse = p; st->fine = 0; break;
    default: break;                                                        /* A (slide to volume), C: not supported */
    }
}

/* converts one FAR pattern (rows rows stored, rp rows played) into engine patterns starting at ep0 */
static void far_pattern(XSong *s, int ep0, const uint8_t *d, int rows, int rp, FarSt *st, int *emit_spd, int *emit_tmp){
    int nch = s->nch;
    int pv[16] = {0};
    for (int r = 0; r < rp; r++) {
        XCell *row = &s->pat[((size_t)(ep0 + r / 64) * 64 + (r % 64)) * nch];
        int tempo_touched = 0;
        for (int c = 0; c < 16 && c < nch; c++) {
            const uint8_t *q = d + 2 + ((size_t)r * 16 + c) * 4;
            XCell *x = &row[c];
            if (q[0]) { x->note = (uint8_t)(q[0] + FAR_NOTE_OFF); x->inst = (uint8_t)(q[1] + 1); }
            if (q[2] >= 1 && q[2] <= 16) x->vol1 = (uint8_t)((q[2] - 1) * 64 / 15 + 1);
            int e = q[3] >> 4, p = q[3] & 15;
            if (q[3]) { if (e >= 0x0D || (e == 0 && (p == 4 || p == 5))) tempo_touched = 1; far_fx(x, e, p, st, &pv[c]); }
            else if (pv[c]) { x->eff = 0x04; x->par = (uint8_t)pv[c]; }
        }
        if (tempo_touched) {
            int sp, tm; far_timing(st, &sp, &tm); st->spd = sp; st->tmp = tm;
            if (sp != *emit_spd) { XCell *f = far_free_slot(row, nch); if (f) { f->eff = 0x0F; f->par = (uint8_t)sp; *emit_spd = sp; } }
            if (tm != *emit_tmp) { XCell *f = far_free_slot(row, nch); if (f) { f->eff = 0x0F; f->par = (uint8_t)tm; *emit_tmp = tm; } }
        }
    }
    if (rp < rows || rp % 64) {                       /* pattern ends early (break) */
        int last = rp - 1;
        XCell *row = &s->pat[((size_t)(ep0 + last / 64) * 64 + (last % 64)) * nch];
        if (last % 64 != 63) { XCell *f = far_free_slot(row, nch); if (f) { f->eff = 0x0D; f->par = 0; } }
    }
}

int xfar_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 4; s->tempo = 80;
    if (!far_probe(b, n)) return -1;
    memcpy(s->title, b + 4, 20); s->title[20] = 0;
    for (int i = 19; i >= 0 && (s->title[i] == ' ' || (uint8_t)s->title[i] < 32); i--) s->title[i] = 0;
    for (int i = 0; i < 20; i++) if ((uint8_t)s->title[i] < 32) s->title[i] = 0;
    unsigned hd = rd16(b + 47), stl = rd16(b + 96);
    size_t o = 98 + (size_t)stl, fixed = 869 + (size_t)stl;
    if (o + 771 > n) return -2;
    const uint8_t *ord = b + o;
    unsigned olen = b[o + 257], loop = b[o + 258];
    unsigned ps[256]; for (int i = 0; i < 256; i++) ps[i] = rd16(b + o + 259 + 2 * i);
    size_t pos = hd > fixed ? hd : fixed;
    if (!olen) return -3;

    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    s->nch = 16;
    for (int i = 0; i < 16; i++) { s->chpan[i] = ngp_pan_tab[b[76 + i] & 15]; }

    /* locate patterns, count engine patterns (64 rows each) */
    size_t po[256]; int first[256], rowsA[256], rpA[256], nep = 0; size_t p = pos;
    for (int i = 0; i < 256; i++) {
        first[i] = -1; rowsA[i] = rpA[i] = 0;
        if (ps[i] < 2) continue;
        if (p + ps[i] > n) return -4;
        po[i] = p; p += ps[i];
        int rows = (int)((ps[i] - 2) / 64), rp = b[po[i]] + 2;
        if (rows < 1) continue;
        if (rp > rows) rp = rows;
        rowsA[i] = rows; rpA[i] = rp;
        first[i] = nep; nep += (rp + 63) / 64;
    }
    if (!nep) return -4;
    s->npat = nep;
    s->pat = calloc((size_t)nep * 64 * 16 + 1, sizeof(XCell));
    if (!s->pat) return -5;

    /* order list (zero-size patterns are skipped) */
    int ordmap[256]; int no = 0; int loopo = 0;
    for (unsigned i = 0; i < olen && i < 256; i++) {
        if (i == loop) loopo = no;
        int pi = ord[i];
        if (first[pi] < 0) continue;
        int chunks = (rpA[pi] + 63) / 64;
        for (int k = 0; k < chunks && no < 256; k++) s->orders[no++] = (uint8_t)(first[pi] + k);
        ordmap[i] = no;
    }
    (void)ordmap;
    s->nord = no;
    if (s->nord < 1) return -3;

    /* pattern conversion in play order so the tempo state carries over */
    FarSt st = { b[75] & 15, 0, 1, 4, 0, 0 };
    far_timing(&st, &st.spd, &st.tmp);
    s->speed = st.spd; s->tempo = st.tmp;
    int es = st.spd, et = st.tmp, done[256] = {0};
    for (unsigned i = 0; i < olen && i < 256; i++) {
        int pi = ord[i];
        if (first[pi] < 0 || done[pi]) continue;
        done[pi] = 1; far_pattern(s, first[pi], b + po[pi], rowsA[pi], rpA[pi], &st, &es, &et);
    }
    for (int pi = 0; pi < 256; pi++) if (first[pi] >= 0 && !done[pi]) { int a = es, c = et; FarSt t2 = st; far_pattern(s, first[pi], b + po[pi], rowsA[pi], rpA[pi], &t2, &a, &c); }

    /* song loop: jump back from the last row of the last order */
    if (loopo > 0 && loopo < s->nord) {
        int ep = s->orders[s->nord - 1], rows = 64;
        for (int i = 0; i < 256; i++) if (first[i] >= 0 && ep >= first[i] && ep < first[i] + (rpA[i] + 63) / 64) { int k = ep - first[i]; rows = rpA[i] - k * 64; if (rows > 64) rows = 64; }
        XCell *row = &s->pat[((size_t)ep * 64 + (rows - 1)) * 16];
        XCell *f = NULL;
        for (int c = 0; c < 16; c++) if (row[c].eff == 0x0D) { f = &row[c]; break; }
        if (!f) f = far_free_slot(row, 16);
        if (f) { f->eff = 0x0B; f->par = (uint8_t)loopo; }
    }

    /* samples */
    size_t q = p;
    if (q + 8 > n) return 0;
    const uint8_t *map = b + q; q += 8;
    s->nsmp = 64;
    for (int i = 0; i < 64; i++) {
        if (!(map[i >> 3] & (1 << (i & 7)))) continue;
        if (q + 48 > n) break;
        const uint8_t *h = b + q; q += 48;
        uint32_t len = rd32(h + 32), ls = rd32(h + 38), le = rd32(h + 42);
        int is16 = h[46] & 1, loop_on = (h[47] & 8) != 0;
        if (len > n - q) len = (uint32_t)(n - q);
        XSmp *m = &s->smp[i + 1];
        uint32_t cnt = is16 ? len >> 1 : len;
        if (cnt) {
            m->data = calloc((size_t)cnt * (is16 ? 2 : 1) + 4, 1);
            if (!m->data) return -5;
            memcpy(m->data, b + q, (size_t)cnt * (is16 ? 2 : 1));
            m->len = cnt; m->is16 = is16; m->vol = 64; m->c2spd = FAR_C2SPD;
            if (is16) { ls >>= 1; le >>= 1; }
            if (loop_on && le > ls && ls < cnt) { m->loop = 1; m->ls = ls; m->le = le > cnt ? cnt : le; }
        }
        q += len;
    }
    return 0;
}
