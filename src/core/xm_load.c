/* XM-Loader (Format-ID 4 wie im Original, Detect 0x7a2a / Loader 0x8810). GPL-3.0-or-later */
#include "core.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static unsigned rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

static void load_env(XEnv *e, const uint8_t *pts, int n, int sus, int ls, int le, int flags){
    if (n > 12) n = 12;
    e->n = n;
    for (int i = 0; i < n; i++) { e->x[i] = rd16(pts + 4 * i); e->y[i] = (uint8_t)(rd16(pts + 4 * i + 2) > 64 ? 64 : rd16(pts + 4 * i + 2)); }
    e->sus = sus; e->ls = ls; e->le = le;
    e->on = (flags & 1) != 0 && n > 0; e->susen = (flags & 2) != 0; e->loopen = (flags & 4) != 0;
}

int xxm_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 4; s->maxvol = 0x40; s->gvol = 255;
    if (n < 80 || memcmp(b, "Extended Module: ", 17)) return -1;
    memcpy(s->title, b + 17, 20); s->title[20] = 0;
    unsigned ver = rd16(b + 58), hs = rd32(b + 60);
    unsigned nord = rd16(b + 64), rst = rd16(b + 66), nch = rd16(b + 68), npat = rd16(b + 70), nins = rd16(b + 72);
    unsigned flags = rd16(b + 74), speed = rd16(b + 76), tempo = rd16(b + 78);
    if (ver < 0x0104 || nch < 1 || nch > 32 || nord > 256 || npat > 256 || nins > 128) return -2;
    if (60 + hs > n || hs < 20) return -3;
    s->nch = nch; s->nord = nord; s->npat = npat; s->ninst = nins; s->linear = flags & 1; s->restart = rst;
    s->speed = speed ? (int)speed : 6; s->tempo = tempo ? (int)tempo : 125;
    memcpy(s->orders, b + 80, nord > 256 ? 256 : nord);
    for (int i = 0; i < 32; i++) { s->chpan[i] = -1; s->chen[i] = 1; }
    size_t off = 60 + hs;
    s->pat = calloc((size_t)(npat ? npat : 1) * 64 * nch, sizeof(XCell));
    /* Pattern: Zeilenzahl je Pattern variabel (1..256). Wir speichern fest 64 Zeilen/Pattern nur fuer MOD/S3M;
       fuer XM liegen die Zeilen in prows[]/pat_off[] (siehe unten). */
    free(s->pat);
    /* Zeilenzahlen zuerst bestimmen */
    size_t o = off; int *rows = calloc(npat ? npat : 1, sizeof(int)); size_t total = 0;
    for (unsigned p = 0; p < npat; p++) {
        if (o + 9 > n) { free(rows); return -4; }
        unsigned phl = rd32(b + o), nr = rd16(b + o + 5), ps = rd16(b + o + 7);
        rows[p] = nr; total += (size_t)nr * nch;
        o += phl + ps;
    }
    s->pat = calloc(total ? total : 1, sizeof(XCell));
    s->prows = rows;
    o = off; size_t cell0 = 0;
    for (unsigned p = 0; p < npat; p++) {
        unsigned phl = rd32(b + o), nr = rd16(b + o + 5), ps = rd16(b + o + 7);
        const uint8_t *d = b + o + phl; o += phl + ps;
        if (o > n) { ps = 0; }
        XCell *c = s->pat + cell0; cell0 += (size_t)nr * nch;
        if (!ps) continue;
        size_t q = 0;
        for (size_t k = 0; k < (size_t)nr * nch && q < ps; k++) {
            uint8_t f = d[q++], v[5] = {0, 0, 0, 0, 0};
            if (f & 0x80) { for (int j = 0; j < 5; j++) if ((f & (1 << j)) && q < ps) v[j] = d[q++]; }
            else { v[0] = f; for (int j = 1; j < 5 && q < ps; j++) v[j] = d[q++]; }
            c[k].note = v[0]; c[k].inst = v[1]; c[k].vol1 = v[2]; c[k].eff = v[3]; c[k].par = v[4];
        }
    }
    /* Instrumente */
    s->inst = calloc(nins + 1, sizeof(XInst));
    s->xsmp = calloc(nins * 16 + 1, sizeof(XSmp));
    for (unsigned i = 1; i <= nins; i++) {
        if (o + 29 > n) break;
        unsigned ihs = rd32(b + o), ns = rd16(b + o + 27);
        XInst *I = &s->inst[i];
        if (ihs < 29) ihs = 29;
        if (ns > 16) ns = 16;
        if (ns && ihs >= 243 && o + 243 <= n) {
            unsigned shs = rd32(b + o + 29);
            memcpy(I->keymap, b + o + 33, 96);
            load_env(&I->venv, b + o + 129, b[o + 225], b[o + 227], b[o + 228], b[o + 229], b[o + 233]);
            load_env(&I->penv, b + o + 177, b[o + 226], b[o + 230], b[o + 231], b[o + 232], b[o + 234]);
            I->vibtype = b[o + 235]; I->vibsweep = b[o + 236]; I->vibdepth = b[o + 237]; I->vibrate = b[o + 238];
            I->fadeout = rd16(b + o + 239);
            if (shs < 18) shs = 40;
            size_t sh = o + ihs, dat = sh + (size_t)ns * shs;
            I->nsmp = ns;
            for (unsigned k = 0; k < ns; k++) {
                XSmp *m = &s->xsmp[s->nxsmp]; I->smp[k] = s->nxsmp++;
                const uint8_t *h = b + sh + (size_t)k * shs;
                if (sh + (size_t)(k + 1) * shs > n) break;
                uint32_t len = rd32(h), ls = rd32(h + 4), ll = rd32(h + 8);
                int type = h[14];
                m->vol = h[12]; m->finetune = (int8_t)h[13]; m->pan = h[15]; m->relnote = (int8_t)h[16];
                m->is16 = (type & 16) != 0;
                m->loop = (type & 3) != 0; m->pingpong = (type & 3) == 2;
                if (m->is16) { len >>= 1; ls >>= 1; ll >>= 1; }
                m->len = len; m->ls = ls; m->le = ls + ll;
                if (m->le > m->len) m->le = m->len;
                if (!ll || ls >= m->len) m->loop = 0;
                size_t bytes = (size_t)len * (m->is16 ? 2 : 1);
                if (len && dat < n) {
                    if (dat + bytes > n) { bytes = n - dat; m->len = len = bytes / (m->is16 ? 2 : 1); if (m->le > m->len) m->le = m->len; }
                    m->data = calloc((size_t)len * (m->is16 ? 2 : 1) + 4, 1);
                    if (m->is16) { int16_t *q = (int16_t *)m->data; int acc = 0;
                        for (uint32_t j = 0; j < len; j++) { acc = (int16_t)(acc + (int16_t)rd16(b + dat + 2 * j)); q[j] = (int16_t)acc; } }
                    else { int acc = 0;
                        for (uint32_t j = 0; j < len; j++) { acc = (int8_t)(acc + (int8_t)b[dat + j]); m->data[j] = (int8_t)acc; } }
                } else m->len = 0;
                dat += rd32(h);
            }
            o = dat;
        } else o += ihs;
    }
    return 0;
}
