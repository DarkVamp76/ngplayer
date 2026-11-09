/* S3M-Loader nach dem Referenz-Player (Loader 0x4C83, Pattern 0x4AEF/0x45FA). GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static unsigned rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

/* Effektbuchstabe (1=A) -> interner Code des Originals (Tabelle 0x4A3D); -1 = kein Effekt */
static const int s3m_eff[26] = {
    0x10, 0x0B, 0x0D, 0x11, 0x12, 0x13, 0x03, 0x04, 0x14, 0x00, 0x17, 0x18, -1, -1, 0x09, -1,
    0x15, 0x07, 0x0E, 0x16, -1, 0x1A, -1, 0x0E, -1, 0x24 };
/* S-Unterbefehle (Tabelle 0x4935): hoechstes Nibble -> Praefix des E-Parameters, -1 = nichts, -2 = nur x */
static const int s3m_sub[15] = { -2, 0x30, 0x50, 0x40, 0x70, -1, -1, -1, 0x80, -1, -1, 0x60, 0xC0, 0xD0, 0xE0 };

int xs3m_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 2; s->maxvol = 0x40; s->gvol = 255;
    if (n < 0x60 || memcmp(b + 0x2C, "SCRM", 4)) return -1;
    memcpy(s->title, b, 20); s->title[20] = 0;
    unsigned ordnum = rd16(b + 0x20), insnum = rd16(b + 0x22), patnum = rd16(b + 0x24);
    unsigned flags = rd16(b + 0x26), ffi = rd16(b + 0x2A);
    if (ordnum > 256 || insnum > 99 || patnum > 256) return -2;
    s->fast = flags;
    int gv = (b[0x30] & 0x7f) << 2; s->gvol = (gv > 255 || gv < 1) ? 255 : gv;
    s->speed = b[0x31]; s->tempo = b[0x32];
    if (!s->speed) s->speed = 6;
    if (s->tempo < 32) s->tempo = 125;               /* Schutz; Original nimmt den Rohwert */
    int stereo = (b[0x33] & 0x80) != 0, dp = b[0x35];
    const uint8_t *chset = b + 0x40;
    for (int i = 0; i < 32; i++) s->chen[i] = !(chset[i] == 0x80 || chset[i] == 0xFF);
    memcpy(s->orders, b + 0x60, ordnum);
    s->nord = ordnum;
    while (s->nord > 0 && s->orders[s->nord - 1] > 0xFD) s->nord--;
    size_t pp = 0x60 + ordnum;
    if (pp + 2 * (insnum + patnum) > n) return -3;
    const uint8_t *insp = b + pp, *patp = b + pp + 2 * insnum;
    const uint8_t *pant = (dp == 0xFC && patp + 2 * patnum + 32 <= b + n) ? patp + 2 * patnum : NULL;
    /* Kanal-Pan wie im Original (0xcd00-Tabelle: Index*17) */
    for (int i = 0; i < 32; i++) {
        s->chpan[i] = -1;
        if (pant && (pant[i] & 0x20)) { s->chpan[i] = ngp_pan_tab[pant[i] & 15]; continue; }
        if (stereo) {
            int c = chset[i] & 0x1f;
            if (c < 8) s->chpan[i] = ngp_pan_tab[3];
            else if (c < 16) s->chpan[i] = ngp_pan_tab[12];
        } else s->chpan[i] = ngp_pan_tab[7];
    }
    /* Instrumente */
    s->nsmp = insnum;
    for (unsigned i = 1; i <= insnum; i++) {
        size_t o = (size_t)rd16(insp + 2 * (i - 1)) << 4;
        if (!o || o + 0x50 > n) continue;
        const uint8_t *h = b + o; XSmp *m = &s->smp[i];
        m->vol = h[0x1C]; m->c2spd = rd32(h + 0x20);
        m->len = rd32(h + 0x10); m->ls = rd32(h + 0x14); m->le = rd32(h + 0x18);
        m->loop = (h[0x1F] & 1) != 0; m->is16 = (h[0x1F] & 4) != 0;
        if (h[0] != 1 || memcmp(h + 0x4C, "SCRS", 4) && 0) { m->len = 0; }
        if (!m->len) continue;
        size_t d = ((size_t)h[0x0D] << 20) | ((size_t)rd16(h + 0x0E) << 4);
        size_t bytes = (size_t)m->len * (m->is16 ? 2 : 1);
        if (m->len > 16000000 || d >= n) { m->len = 0; continue; }
        if (d + bytes > n) { bytes = n - d; m->len = bytes / (m->is16 ? 2 : 1); }
        m->data = calloc(m->len * (m->is16 ? 2 : 1) + 4, 1);
        if (m->is16) {
            int16_t *q = (int16_t *)m->data;
            for (uint32_t k = 0; k < m->len; k++) { int v = rd16(b + d + 2 * k); q[k] = (int16_t)(ffi == 1 ? v : v - 0x8000); }
        } else {
            for (uint32_t k = 0; k < m->len; k++) m->data[k] = (int8_t)(ffi == 1 ? b[d + k] : b[d + k] - 128);
        }
        if (m->le > m->len) m->le = m->len;
    }
    /* Pattern: erst alle entpacken, Kanalzahl bestimmen */
    size_t cells = (size_t)patnum * 64 * 32;
    XCell *tmp = calloc(cells ? cells : 1, sizeof(XCell));
    int maxch = 1;
    for (unsigned p = 0; p < patnum; p++) {
        size_t o = (size_t)rd16(patp + 2 * p) << 4;
        if (!o || o + 2 > n) continue;
        const uint8_t *q = b + o + 2, *e = b + n;
        int row = 0;
        while (row < 64 && q < e) {
            uint8_t w = *q++;
            if (!w) { row++; continue; }
            int ch = w & 31; uint8_t note = 0xFF, ins = 0xFF, vol = 0xFF, cmd = 0xFF, inf = 0xFF;
            if (w & 0x20) { if (q + 2 > e) break; note = q[0]; ins = q[1]; q += 2; }
            if (w & 0x40) { if (q >= e) break; vol = *q++; }
            if (w & 0x80) { if (q + 2 > e) break; cmd = q[0]; inf = q[1]; q += 2; }
            if (!s->chen[ch]) continue;
            if (ch + 1 > maxch) maxch = ch + 1;
            XCell *c = &tmp[((size_t)p * 64 + row) * 32 + ch];
            if (note != 0xFF) c->note = (note == 0xFE) ? 0x7F : (uint8_t)((note >> 4) * 12 + (note & 15));
            if (ins != 0xFF) c->inst = ins;
            if (vol < 0x41) c->vol1 = vol + 1;
            if (cmd != 0xFF && cmd >= 1) {
                int a = cmd - 1, par = inf, code = (a < 26) ? s3m_eff[a] : -1;
                if ((cmd >= 1 && cmd <= 12) || cmd == 15 || (cmd >= 17 && cmd <= 20) || cmd == 22) c->par = inf;
                if (code >= 0) {
                    c->eff = code;
                    if (cmd == 19) {                                   /* S */
                        int sub = inf >> 4, x = inf & 15;
                        c->par = 0;
                        if (sub <= 14 && s3m_sub[sub] != -1) c->par = (s3m_sub[sub] == -2) ? x : (s3m_sub[sub] | x);
                        else c->eff = 0;
                        if (sub <= 14 && s3m_sub[sub] == -1) c->par = 0;
                    } else if (cmd == 22) {                           /* V */
                        c->par = (inf < 0x40) ? (uint8_t)(inf << 2) : 0xFF;
                    } else if (cmd == 24) {                           /* X */
                        if (par <= 0x80) { if (par == 0x80) par = 0x7F; c->par = 0x80 | (par >> 3); }
                        else { c->eff = 0; c->par = 0; }
                    } else if (cmd == 26) {                           /* Z */
                        c->par = (uint8_t)ngp_pan_tab[par & 15];
                    }
                }
            }
        }
    }
    s->nch = maxch; s->npat = patnum;
    s->pat = calloc((size_t)patnum * 64 * maxch + 1, sizeof(XCell));
    for (unsigned p = 0; p < patnum; p++)
        for (int r = 0; r < 64; r++)
            for (int c = 0; c < maxch; c++) s->pat[((size_t)p * 64 + r) * maxch + c] = tmp[((size_t)p * 64 + r) * 32 + c];
    free(tmp);
    return 0;
}
