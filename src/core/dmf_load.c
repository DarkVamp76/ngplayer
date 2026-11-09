/* DMF-Loader. GPL-3.0-or-later */
#include "dmf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t rd32(const uint8_t *p){ return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p){ return p[0] | p[1] << 8; }

typedef struct { const uint8_t *p; size_t n, pos; int bit; } BR;          /* LSB zuerst */
static int br_get(BR *r, int nb, int *eof){
    int v = 0;
    for (int i = 0; i < nb; i++) {
        if (r->pos >= r->n) { *eof = 1; return v; }
        v |= ((r->p[r->pos] >> r->bit) & 1) << i;
        if (++r->bit == 8) { r->bit = 0; r->pos++; }
    }
    return v;
}
typedef struct { int16_t l, r; uint8_t v; } Node;
static int tree_read(BR *r, Node *t, int *cnt, int *eof){
    if (*cnt >= 256 || *eof) return -1;
    int id = (*cnt)++;
    t[id].v = (uint8_t)br_get(r, 7, eof);
    int hl = br_get(r, 1, eof), hr = br_get(r, 1, eof);
    t[id].l = t[id].r = -1;
    if (hl) t[id].l = (int16_t)tree_read(r, t, cnt, eof);
    if (hr) t[id].r = (int16_t)tree_read(r, t, cnt, eof);
    return id;
}
/* liefert Anzahl erzeugter Bytes, *used = verbrauchte Bytes des Bitstroms */
static size_t unpack(const uint8_t *src, size_t n, uint8_t *dst, size_t len, size_t *used){
    BR r = { src, n, 0, 0 }; Node t[256]; int cnt = 0, eof = 0;
    tree_read(&r, t, &cnt, &eof);
    size_t o = 0;
    if (cnt && t[0].l >= 0 && t[0].r >= 0 && !eof) {
        uint8_t acc = 0;
        while (o < len) {
            int sign = br_get(&r, 1, &eof); if (eof) break;
            int nd = 0;
            while (t[nd].l >= 0 && t[nd].r >= 0) { int b = br_get(&r, 1, &eof); if (eof) break; nd = b ? t[nd].r : t[nd].l; }
            if (eof) break;
            uint8_t d = t[nd].v; if (sign) d ^= 0xFF;
            acc = (uint8_t)(acc + d); dst[o++] = acc;
        }
    }
    *used = r.pos + (r.bit ? 1 : 0);
    return o;
}

void dmf_free(DMFSong *s){
    if (!s) return;
    for (int i = 0; i < s->npat; i++) { free(s->pat[i].cell); free(s->pat[i].gcmd); free(s->pat[i].gpar); }
    if (s->smp) for (int i = 1; i <= s->nsmp; i++) free(s->smp[i].data);
    free(s->pat); free(s->smp); free(s->order); free(s->msg); free(s);
}

static int load_patt(DMFSong *s, const uint8_t *p, size_t n){
    if (n < 3) return -1;
    s->npat = (int)rd16(p); s->nch = p[2]; if (s->nch < 1) s->nch = 1; if (s->nch > 32) s->nch = 32;
    size_t pos = 3;
    s->pat = calloc((size_t)s->npat, sizeof(DMFPat));
    for (int pi = 0; pi < s->npat; pi++) {
        size_t hl = s->version < 3 ? 9 : 8;
        if (pos + hl > n) return -2;
        const uint8_t *h = p + pos;
        int rows, beat = 0, ntr = h[0]; uint32_t plen;
        if (ntr < 1) ntr = 1; if (ntr > 32) ntr = 32;
        if (ntr > s->nch) s->nch = ntr;
        if (s->version < 3) { rows = (int)rd16(h + 3); plen = rd32(h + 5); }
        else { beat = h[1] >> 4; rows = (int)rd16(h + 2); plen = rd32(h + 4); }
        if (s->version < 6) beat = 0;
        pos += hl;
        if (rows < 1 || pos + plen > n) return -3;
        DMFPat *pt = &s->pat[pi]; pt->nrows = rows; pt->beat = beat;
        pt->ntr = ntr; pt->cell = calloc((size_t)rows * 32, sizeof(DMFCell)); pt->gcmd = calloc((size_t)rows, 1); pt->gpar = calloc((size_t)rows, 1);
        const uint8_t *d = p + pos, *e = d + plen;
        int gcnt = 0; int cnt[33] = {0};
        uint8_t gc = 0, gp = 0; DMFCell cur[33]; memset(cur, 0, sizeof cur);
        for (int row = 0; row < rows; row++) {
            if (gcnt == 0) {
                if (d >= e) return -4;
                uint8_t g = *d++; if (g & 0x80) { if (d >= e) return -4; gcnt = *d++; }
                gc = g & 0x3F; gp = 0; if (gc) { if (d >= e) return -4; gp = *d++; }
            } else gcnt--;
            pt->gcmd[row] = gc; pt->gpar[row] = gp;
            for (int c = 0; c < ntr; c++) {
                DMFCell *cc = &pt->cell[(size_t)row * 32 + c];
                if (cnt[c] == 0) {
                    if (d >= e) return -5;
                    uint8_t f = *d++; if (f & 0x80) { if (d >= e) return -5; cnt[c] = *d++; }
                    DMFCell x; memset(&x, 0, sizeof x); x.has = f & 0x7F;
                    int need = ((f & 0x40) ? 1 : 0) + ((f & 0x20) ? 1 : 0) + ((f & 0x10) ? 1 : 0) + ((f & 8) ? 2 : 0) + ((f & 4) ? 2 : 0) + ((f & 2) ? 2 : 0);
                    if (d + need > e) return -6;
                    if (f & 0x40) x.inst = *d++;
                    if (f & 0x20) x.note = *d++;
                    if (f & 0x10) x.vol = *d++;
                    if (f & 8) { x.ie_cmd = d[0]; x.ie_par = d[1]; d += 2; }
                    if (f & 4) { x.ne_cmd = d[0]; x.ne_par = d[1]; d += 2; }
                    if (f & 2) { x.ve_cmd = d[0]; x.ve_par = d[1]; d += 2; }
                    cur[c] = x; *cc = x;
                } else { cnt[c]--; /* Wiederholung: Zelle bleibt leer (Werte gelten nur in der Startzeile) */ }
            }
        }
        if (d != e) { fprintf(stderr, "DMF: Pattern %d: %ld Bytes Rest\n", pi, (long)(e - d)); }
        pos += plen;
    }
    return 0;
}

DMFSong *dmf_load(const uint8_t *b, size_t n, int *err){
    int dummy; if (!err) err = &dummy; *err = 0;
    if (n < 66 || memcmp(b, "DDMF", 4)) { *err = -1; return NULL; }
    DMFSong *s = calloc(1, sizeof *s);
    s->version = b[4]; if (!s->version || s->version > 10) { *err = -2; free(s); return NULL; }
    memcpy(s->title, b + 13, 30); memcpy(s->composer, b + 43, 20);
    size_t pos = 66; int have_smpi = 0;
    const uint8_t *smpd = NULL; size_t smpd_n = 0;
    while (pos + 8 <= n) {
        const uint8_t *id = b + pos; uint32_t len = rd32(b + pos + 4); const uint8_t *d = b + pos + 8;
        if (pos + 8 + len > n) len = (uint32_t)(n - pos - 8);
        pos += 8 + len;
        if (!memcmp(id, "CMSG", 4) && len > 1) { s->msg = malloc(len); memcpy(s->msg, d + 1, len - 1); s->msg[len - 1] = 0; }
        else if (!memcmp(id, "SEQU", 4)) {
            size_t q = 0;
            if (s->version >= 3) { s->loop_start = (int)rd16(d); q += 2; }
            if (s->version >= 4) { s->loop_end = (int)rd16(d + 2); q += 2; }
            s->nord = (int)((len - q) / 2); s->order = malloc((size_t)(s->nord + 1) * 2);
            for (int i = 0; i < s->nord; i++) s->order[i] = (uint16_t)rd16(d + q + 2 * i);
            if (s->version == 3) pos += 2; else if (s->version == 4) pos += 4;
        }
        else if (!memcmp(id, "PATT", 4)) { int r = load_patt(s, d, len); if (r) { *err = r; dmf_free(s); return NULL; } }
        else if (!memcmp(id, "SMPI", 4)) {
            have_smpi = 1; s->nsmp = d[0]; size_t q = 1;
            s->smp = calloc((size_t)s->nsmp + 1, sizeof(DMFSmp));
            for (int i = 1; i <= s->nsmp; i++) {
                DMFSmp *m = &s->smp[i]; int nl;
                if (s->version >= 2) { nl = d[q++]; } else nl = 30;
                if (q + nl + 16 > len) { *err = -10; dmf_free(s); return NULL; }
                memcpy(m->name, d + q, (size_t)nl); m->name[nl] = 0; q += (size_t)nl;
                m->len = rd32(d + q); m->lb = rd32(d + q + 4); m->le = rd32(d + q + 8); m->c3spd = rd16(d + q + 12);
                m->vol = d[q + 14]; m->flags = d[q + 15]; q += 16;
                if (s->version >= 8) q += 8;
                q += s->version > 1 ? 6 : 2;
                m->is16 = (m->flags & 2) != 0; m->loop = (m->flags & 1) != 0; m->compressed = (m->flags & 0x0C) == 4;
            }
        }
        else if (!memcmp(id, "SMPD", 4)) { smpd = d; smpd_n = len; if (s->version < 8) smpd_n = n - (size_t)(d - b); }
        else if (!memcmp(id, "ENDE", 4)) break;
    }
    if (!s->order || !s->pat) { *err = -11; dmf_free(s); return NULL; }
    if (have_smpi && smpd) {
        size_t q = 0;
        for (int i = 1; i <= s->nsmp; i++) {
            DMFSmp *m = &s->smp[i];
            if (q + 4 > smpd_n) break;
            uint32_t clen = rd32(smpd + q); q += 4;
            if (q + clen > smpd_n) clen = (uint32_t)(smpd_n - q);
            size_t bytes = m->len; if (!bytes) { q += clen; continue; }
            uint8_t *raw = calloc(bytes + 2, 1);
            if (m->compressed) {
                size_t used; size_t got = unpack(smpd + q, clen, raw, bytes, &used);
                if (got != bytes || used != clen) fprintf(stderr, "DMF: Sample %d: dekomprimiert %zu/%zu, verbraucht %zu/%u\n", i, got, bytes, used, clen);
            } else memcpy(raw, smpd + q, clen < bytes ? clen : bytes);
            q += clen;
            m->data = raw;
            if (m->is16) { m->len /= 2; m->lb /= 2; m->le /= 2; }
            if (m->le > m->len) m->le = m->len;
            if (!(m->loop && m->le > m->lb)) m->loop = 0;
        }
    }
    return s;
}
