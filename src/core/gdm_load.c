/* GDM (General Digital Music, BWSB / 2GDM): the converted modules of other trackers. libopenmpt plays them like the
   original format; the cells, the order list and the samples map one to one onto Scream Tracker 3, so the file is
   rewritten as an S3M image in memory and the S3M engine plays it. gdm_convert() returns that image (malloc), or NULL. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "it.h"

static uint32_t r32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void w16(uint8_t *p, unsigned v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }
static void w32(uint8_t *p, uint32_t v) { w16(p, v & 0xFFFF); w16(p + 2, v >> 16); }

int gdm_probe(const uint8_t *b, size_t n) {
    return n >= 157 && !memcmp(b, "GDM\xFE", 4) && b[68] == 13 && b[69] == 10 && b[70] == 26 && !memcmp(b + 71, "GMFS", 4);
}

/* GDM effect number -> S3M command letter (A = 1); 0 = none */
static const uint8_t eff_tab[32] = {
    0, 6, 5, 7, 8, 12, 11, 18, 9, 15, 4, 2, 0 /* volume: volume column */, 3, 0 /* MOD extended */, 1,
    10, 0, 17, 22, 21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 19, 20 };

typedef struct { int cmd, par, vol; } Cell;

/* a MOD extended effect (Exy) as the S3M command */
static void modex(Cell *c, int par) {
    int x = par & 15;
    switch (par >> 4) {
    case 0x1: c->cmd = 6; c->par = 0xF0 | x; break;                  /* fine porta up (Fxx: F0|x is fine) */
    case 0x2: c->cmd = 5; c->par = 0xF0 | x; break;
    case 0x3: c->cmd = 19; c->par = 0x10 | x; break;
    case 0x4: c->cmd = 19; c->par = 0x30 | x; break;
    case 0x5: c->cmd = 19; c->par = 0x20 | x; break;
    case 0x6: c->cmd = 19; c->par = 0xB0 | x; break;
    case 0x7: c->cmd = 19; c->par = 0x40 | x; break;
    case 0x8: c->cmd = 6; c->par = 0xE0 | x; break;                  /* GDM: 8x / 9x are extra fine slides */
    case 0x9: c->cmd = 5; c->par = 0xE0 | x; break;
    case 0xA: c->cmd = 4; c->par = (x << 4) | 0x0F; break;           /* fine volume up */
    case 0xB: c->cmd = 4; c->par = 0xF0 | x; break;                  /* fine volume down */
    case 0xC: c->cmd = 19; c->par = 0xC0 | x; break;
    case 0xD: c->cmd = 19; c->par = 0xD0 | x; break;
    case 0xE: c->cmd = 19; c->par = 0xE0 | x; break;
    case 0xF: c->cmd = 19; c->par = 0xF0 | x; break;
    default: c->cmd = 0; c->par = 0; break;
    }
}

uint8_t *gdm_convert(const uint8_t *b, size_t n, size_t *outn) {
    if (!gdm_probe(b, n)) return NULL;
    int nch = 0; while (nch < 32 && b[81 + nch] != 255) nch++;
    if (!nch || nch > 32) return NULL;
    uint32_t oo = r32(b + 118); int nord = b[122] + 1; uint32_t po = r32(b + 123); int npat = b[127] + 1;
    uint32_t sho = r32(b + 128), sdo = r32(b + 132); int nsmp = b[136] + 1;
    if (oo + (uint32_t)nord > n || sho + 62u * nsmp > n || po > n) return NULL;
    int ordn = (nord + 1) & ~1;
    size_t hdr = 0x60 + ordn + 2 * nsmp + 2 * npat + 32;
    size_t insoff = (hdr + 15) & ~(size_t)15;
    size_t patoff = insoff + 80u * nsmp;
    /* patterns first (packed lengths are known after the conversion), then the sample data */
    size_t cap = patoff + (size_t)npat * (64 * (1 + 6 * (size_t)nch) + 32) + 64;
    uint32_t datasz = 0; for (int i = 0; i < nsmp; i++) datasz += r32(b + sho + 62 * i + 45) + 16;
    uint8_t *o = (uint8_t *)calloc(1, cap + datasz + 64);
    if (!o) return NULL;
    memcpy(o, b + 4, 28); o[0x1C] = 0x1A; o[0x1D] = 16;
    w16(o + 0x20, ordn); w16(o + 0x22, nsmp); w16(o + 0x24, npat);
    w16(o + 0x28, 0x1320); w16(o + 0x2A, 2); memcpy(o + 0x2C, "SCRM", 4);
    o[0x30] = b[113] > 64 ? 64 : b[113]; o[0x31] = b[114] ? b[114] : 6; o[0x32] = b[115] ? b[115] : 125;
    o[0x33] = 0xB0; o[0x35] = 0xFC;
    for (int i = 0; i < 32; i++) o[0x40 + i] = i < nch ? (uint8_t)(i & 15) : 255;
    for (int i = 0; i < nord; i++) o[0x60 + i] = b[oo + i];
    for (int i = nord; i < ordn; i++) o[0x60 + i] = 255;
    uint8_t *pp = o + 0x60 + ordn;                                   /* instrument, then pattern parapointers */
    uint8_t *pan = pp + 2 * nsmp + 2 * npat;
    for (int i = 0; i < 32; i++) pan[i] = (i < nch && b[81 + i] < 16) ? (uint8_t)(0x20 | b[81 + i]) : (i < nch ? 0x28 : 0);
    /* patterns */
    size_t cur = patoff; const uint8_t *gp = b + po;
    for (int p = 0; p < npat; p++) {
        cur = (cur + 15) & ~(size_t)15; w16(pp + 2 * nsmp + 2 * p, (unsigned)(cur >> 4));
        if ((size_t)(gp - b) + 2 > n) { w16(o + cur, 2); cur += 2; continue; }
        unsigned plen = gp[0] | (gp[1] << 8); const uint8_t *q = gp + 2, *qe = gp + plen > b + n ? b + n : gp + plen; gp += plen;
        if (plen <= 2) { w16(o + cur, 2); cur += 2; continue; }
        uint8_t *w = o + cur + 2;
        for (int row = 0; row < 64; row++) {
            for (;;) {
                if (q >= qe) break;
                int cb = *q++; if (!cb) break;
                int ch = cb & 31;
                int note = 255, ins = 0; Cell c = { 0, 0, -1 };
                if (cb & 0x20) {
                    if (q + 2 > qe) break;
                    note = q[0]; ins = q[1]; q += 2;
                    if (note) { note = (note & 0x7F) - 1; note = ((note >> 4) << 4) | (note & 15); if ((note & 15) > 11) note = 255; } else note = 255;
                }
                if (cb & 0x40) {
                    while (q + 2 <= qe) {
                        int e = q[0], par = q[1]; q += 2; int more = e & 0x20, cm = e & 31;
                        if (cm == 12) c.vol = par > 64 ? 64 : par;
                        else if (cm == 14) { if (!c.cmd) { c.cmd = 0; } modex(&c, par); }
                        else {
                            c.cmd = eff_tab[cm]; c.par = par;
                            if ((c.cmd == 5 || c.cmd == 6) && par >= 0xE0) c.par = 0xDF;
                            if ((c.cmd == 11 || c.cmd == 12) && (par & 0xF0)) c.par = par & 0xF0;
                            if (cm == 30) { if (par == 1) c.par = 0x91; else if ((par & 0xF0) != 0x80) c.cmd = 0; }
                            if (!c.cmd) c.par = 0;
                        }
                        if (!more) break;
                    }
                }
                int fl = ch;
                if (note != 255 || ins) fl |= 0x20;
                if (c.vol >= 0) fl |= 0x40;
                if (c.cmd) fl |= 0x80;
                if (ch >= nch || fl == ch) continue;                       /* a channel the header does not list: read, not played */
                *w++ = (uint8_t)fl;
                if (fl & 0x20) { *w++ = (uint8_t)note; *w++ = (uint8_t)ins; }
                if (fl & 0x40) *w++ = (uint8_t)c.vol;
                if (fl & 0x80) { *w++ = (uint8_t)c.cmd; *w++ = (uint8_t)c.par; }
            }
            *w++ = 0;
        }
        w16(o + cur, (unsigned)(w - (o + cur))); cur = (size_t)(w - o);
    }
    /* samples: headers first, data after the patterns */
    size_t dcur = (cur + 15) & ~(size_t)15; const uint8_t *sd = b + sdo;
    for (int i = 0; i < nsmp; i++) {
        const uint8_t *h = b + sho + 62 * i; uint8_t *ih = o + insoff + 80 * i;
        uint32_t len = r32(h + 45), ls = r32(h + 49), le = r32(h + 53); int fl = h[57], vol = h[60];
        w16(pp + 2 * i, (unsigned)((insoff + 80 * i) >> 4));
        ih[0] = len ? 1 : 0; memcpy(ih + 1, h + 32, 12);
        if (fl & 0x12) len = 0;                                      /* 16-bit and LZW samples: not found in any module */
        if ((size_t)(sd - b) + len > n) len = (uint32_t)((b + n) - sd > 0 ? (b + n) - sd : 0);
        if (len) {
            dcur = (dcur + 15) & ~(size_t)15;
            ih[13] = (uint8_t)(dcur >> 20); w16(ih + 14, (unsigned)((dcur >> 4) & 0xFFFF));
            memcpy(o + dcur, sd, len); dcur += len;
        }
        sd += r32(h + 45);
        w32(ih + 16, len);
        int loop = (fl & 1) && le > 1;
        if (loop) { w32(ih + 20, ls); w32(ih + 24, le - 1 > len ? len : le - 1); }
        ih[28] = (fl & 4) && vol != 0xFF ? (vol > 64 ? 64 : vol) : 64;
        ih[31] = loop ? 1 : 0;
        w16(ih + 32, h[58] | (h[59] << 8));                           /* C4 speed */
        memcpy(ih + 48, h, 28); memcpy(ih + 76, "SCRS", 4);
    }
    *outn = dcur + 16;
    return o;
}
