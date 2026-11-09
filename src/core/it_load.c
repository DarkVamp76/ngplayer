/* IT-Loader nach Impulse-Tracker-Spezifikation (ITTECH). GPL-3.0-or-later */
#include "it.h"
#include "it_unpack.h"
#include <stdlib.h>
#include <string.h>

static unsigned r16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static unsigned r32(const uint8_t *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static void cpstr(char *d, const uint8_t *s, int n){ memcpy(d, s, n); d[n] = 0; for (int i = n - 1; i >= 0 && (d[i] == ' ' || d[i] == 0); i--) d[i] = 0; }

static void load_env(ITEnv *e, const uint8_t *p){
    e->flg = p[0]; e->num = p[1] > 25 ? 25 : p[1]; e->lpb = p[2]; e->lpe = p[3]; e->slb = p[4]; e->sle = p[5];
    for (int i = 0; i < 25; i++) { e->y[i] = (int8_t)p[6 + 3 * i]; e->tick[i] = r16(p + 7 + 3 * i); }
}

static int load_inst(ITInst *in, const uint8_t *b, size_t n, size_t o){
    if (o + 554 > n || memcmp(b + o, "IMPI", 4)) return -1;
    const uint8_t *p = b + o;
    in->nna = p[17]; in->dct = p[18]; in->dca = p[19]; in->fadeout = r16(p + 20); in->pps = (int8_t)p[22]; in->ppc = p[23];
    in->gbv = p[24]; in->dfp = p[25]; in->rv = p[26]; in->rp = p[27]; in->ifc = p[58]; in->ifr = p[59];
    cpstr(in->name, p + 32, 26);
    for (int i = 0; i < 120; i++) { in->note_map[i] = p[64 + 2 * i]; in->smp_map[i] = p[65 + 2 * i]; }
    load_env(&in->venv, p + 304); load_env(&in->penv, p + 386); load_env(&in->pitchenv, p + 468);
    return 0;
}

static int load_smp(ITSmp *s, const uint8_t *b, size_t n, size_t o){
    if (o + 80 > n || memcmp(b + o, "IMPS", 4)) return -1;
    const uint8_t *p = b + o;
    s->gvl = p[17]; s->flags = p[18]; s->vol = p[19]; cpstr(s->name, p + 20, 26); s->cvt = p[46]; s->dfp = p[47];
    s->len = r32(p + 48); s->lb = r32(p + 52); s->le = r32(p + 56); s->c5spd = r32(p + 60); s->slb = r32(p + 64); s->sle = r32(p + 68);
    unsigned ptr = r32(p + 72); s->vis = p[76]; s->vid = p[77]; s->vir = p[78]; s->vit = p[79];
    s->is16 = (s->flags & 2) != 0; s->loop = (s->flags & 16) != 0; s->sloop = (s->flags & 32) != 0;
    s->pingpong = (s->flags & 64) != 0; s->spingpong = (s->flags & 128) != 0;
    s->data = s->data2 = NULL;
    if (!(s->flags & 1) || s->len == 0) { s->len = 0; return 0; }
    if (s->len > 0x10000000u) return -4;
    int nchn = (s->flags & 4) ? 2 : 1;
    size_t bytes = (size_t)s->len * (s->is16 ? 2 : 1);
    void *buf[2] = {0, 0};
    size_t pos = ptr;
    for (int k = 0; k < nchn; k++) {
        buf[k] = calloc(1, bytes + 8);
        if (!buf[k] || pos >= n) { free(buf[0]); free(buf[1]); s->len = 0; return pos >= n ? -6 : -5; }
        if (s->flags & 8) {
            size_t used = 0;
            if (it_unpack(b + pos, n - pos, buf[k], s->len, s->is16, (s->cvt & 4) != 0, &used) < 0) { free(buf[0]); free(buf[1]); s->len = 0; return -7; }
            pos += used;
        } else {
            size_t avail = n - pos, cp = bytes < avail ? bytes : avail;
            memcpy(buf[k], b + pos, cp); pos += bytes;
            uint8_t *d8 = buf[k];
            if (s->is16 && (s->cvt & 2)) for (size_t i = 0; i + 1 < cp; i += 2) { uint8_t t = d8[i]; d8[i] = d8[i + 1]; d8[i + 1] = t; }
            if (!(s->cvt & 1)) {                          /* vorzeichenlos -> vorzeichenbehaftet */
                if (s->is16) { int16_t *d = buf[k]; for (uint32_t i = 0; i < s->len; i++) d[i] = (int16_t)((uint16_t)d[i] ^ 0x8000); }
                else { int8_t *d = buf[k]; for (uint32_t i = 0; i < s->len; i++) d[i] = (int8_t)((uint8_t)d[i] ^ 0x80); }
            }
            if (s->cvt & 4) {                             /* unkomprimierte Delta-Daten */
                if (s->is16) { int16_t *d = buf[k]; int16_t a = 0; for (uint32_t i = 0; i < s->len; i++) { a = (int16_t)(a + d[i]); d[i] = a; } }
                else { int8_t *d = buf[k]; int8_t a = 0; for (uint32_t i = 0; i < s->len; i++) { a = (int8_t)(a + d[i]); d[i] = a; } }
            }
        }
    }
    s->data = buf[0]; s->data2 = buf[1];
    return 0;
}

static int load_pat(ITSong *s, int pi, const uint8_t *b, size_t n, size_t o){
    if (o == 0) { s->rows[pi] = 64; s->pat[pi] = NULL; return 0; }
    if (o + 8 > n) return -1;
    unsigned plen = r16(b + o), rows = r16(b + o + 2);
    if (rows < 1 || rows > 200 || o + 8 + plen > n) return -2;
    ITCell *c = calloc((size_t)rows * 64, sizeof(ITCell)); if (!c) return -3;
    uint8_t mask[64] = {0}, ln[64] = {0}, li[64] = {0}, lv[64] = {0}, lc[64] = {0}, lp[64] = {0};
    const uint8_t *p = b + o + 8, *end = p + plen; unsigned r = 0;
    while (r < rows && p < end) {
        uint8_t v = *p++;
        if (!v) { r++; continue; }
        int ch = (v - 1) & 63;
        if (v & 0x80) { if (p >= end) break; mask[ch] = *p++; }
        int m = mask[ch]; ITCell *x = &c[(size_t)r * 64 + ch];
        if (m & 1) { if (p >= end) break; ln[ch] = *p++; x->note = ln[ch]; x->has |= IT_HAS_NOTE; }
        if (m & 2) { if (p >= end) break; li[ch] = *p++; x->inst = li[ch]; x->has |= IT_HAS_INST; }
        if (m & 4) { if (p >= end) break; lv[ch] = *p++; x->vol = lv[ch]; x->has |= IT_HAS_VOL; }
        if (m & 8) { if (p + 1 >= end) break; lc[ch] = p[0]; lp[ch] = p[1]; p += 2; x->cmd = lc[ch]; x->par = lp[ch]; x->has |= IT_HAS_CMD; }
        if (m & 0x10) { x->note = ln[ch]; x->has |= IT_HAS_NOTE; }
        if (m & 0x20) { x->inst = li[ch]; x->has |= IT_HAS_INST; }
        if (m & 0x40) { x->vol = lv[ch]; x->has |= IT_HAS_VOL; }
        if (m & 0x80) { x->cmd = lc[ch]; x->par = lp[ch]; x->has |= IT_HAS_CMD; }
        if (ch + 1 > s->nch) s->nch = ch + 1;
    }
    s->rows[pi] = rows; s->pat[pi] = c; return 0;
}

ITSong *it_load(const uint8_t *b, size_t n, int *err){
    int e = 0; if (!err) err = &e; *err = 0;
    if (n < 0xC0 || memcmp(b, "IMPM", 4)) { *err = -1; return NULL; }
    ITSong *s = calloc(1, sizeof *s); if (!s) { *err = -2; return NULL; }
    cpstr(s->title, b + 4, 26);
    s->nord = r16(b + 0x20); s->ninst = r16(b + 0x22); s->nsmp = r16(b + 0x24); s->npat = r16(b + 0x26);
    s->cwt = r16(b + 0x28); s->cmwt = r16(b + 0x2A); s->flags = r16(b + 0x2C);
    s->gv = b[0x30]; s->mv = b[0x31]; s->speed = b[0x32] ? b[0x32] : 6; s->tempo = b[0x33] >= 32 ? b[0x33] : 125; s->sep = b[0x34]; s->pwd = b[0x35];
    memcpy(s->chpan, b + 0x40, 64); memcpy(s->chvol, b + 0x80, 64);
    if (s->nord > 256 || s->ninst > 255 || s->nsmp > 255 || s->npat > 256) { *err = -3; free(s); return NULL; }
    size_t tab = 0xC0 + (size_t)s->nord;
    if (tab + 4 * ((size_t)s->ninst + s->nsmp + s->npat) > n) { *err = -4; free(s); return NULL; }
    memcpy(s->orders, b + 0xC0, s->nord);
    if (s->ninst && s->cmwt < 0x200) { *err = -5; free(s); return NULL; }     /* alte Instrumente (IT 1.x) nicht unterstuetzt */
    s->inst = calloc((size_t)s->ninst + 1, sizeof(ITInst)); s->smp = calloc((size_t)s->nsmp + 1, sizeof(ITSmp));
    s->pat = calloc((size_t)s->npat + 1, sizeof(ITCell *)); s->rows = calloc((size_t)s->npat + 1, sizeof(int));
    for (int i = 0; i < s->ninst; i++) if (load_inst(&s->inst[i + 1], b, n, r32(b + tab + 4 * i))) { *err = -6; it_free(s); return NULL; }
    for (int i = 0; i < s->nsmp; i++) { int r = load_smp(&s->smp[i + 1], b, n, r32(b + tab + 4 * s->ninst + 4 * i)); if (r) { *err = r; it_free(s); return NULL; } }
    for (int i = 0; i < s->npat; i++) { int r = load_pat(s, i, b, n, r32(b + tab + 4 * ((size_t)s->ninst + s->nsmp) + 4 * i)); if (r) { *err = -10 + r; it_free(s); return NULL; } }
    return s;
}

void it_free(ITSong *s){
    if (!s) return;
    if (s->smp) for (int i = 1; i <= s->nsmp; i++) { free(s->smp[i].data); free(s->smp[i].data2); }
    if (s->pat) for (int i = 0; i < s->npat; i++) free(s->pat[i]);
    free(s->inst); free(s->smp); free(s->pat); free(s->rows); free(s);
}
