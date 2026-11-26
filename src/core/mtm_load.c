/* MTM loader (MultiTracker). The effects are ProTracker-style, so the song is handed to the MOD replayer (fmt 1).
 * Layout: 66-byte header (incl. 32 channel pan values), samples x 37 bytes, 128 orders, 64-row tracks of 3 bytes (shared by
 * the patterns), 32 track numbers per pattern, comment, sample data (unsigned 8 bit or signed 16 bit). GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int mtm_probe(const uint8_t *b, size_t n){
    if (n < 66 + 128 || memcmp(b, "MTM", 3) || b[3] != 0x10) return 0;
    unsigned nch = b[33], rows = b[32];
    return nch >= 1 && nch <= 32 && rows >= 1 && rows <= 64;
}

int xmtm_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->pt_tuning = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125;
    if (!mtm_probe(b, n)) return -1;
    memcpy(s->title, b + 4, 20); s->title[20] = 0;
    unsigned ntr = rd16(b + 24), npat = b[26] + 1u, nord = b[27] + 1u, clen = rd16(b + 28), nsm = b[30], rows = b[32], nch = b[33];
    if (nsm > 99) return -2;
    s->nch = (int)nch; s->nsmp = (int)nsm;
    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    for (unsigned i = 0; i < nch; i++) if (b[34 + i] <= 15) s->chpan[i] = ngp_pan_tab[b[34 + i]];

    size_t so = 66, oo = so + (size_t)nsm * 37, to = oo + 128, qo = to + (size_t)ntr * rows * 3, co = qo + (size_t)npat * 64, d = co + clen;
    if (co > n) return -3;
    for (unsigned i = 0; i < nord && i < 128; i++) s->orders[i] = b[oo + i] < npat ? b[oo + i] : 0xFE;
    s->nord = (int)nord;
    while (s->nord > 0 && s->orders[s->nord - 1] > 0xFD) s->nord--;
    if (s->nord < 1) return -4;
    s->npat = (int)npat;

    for (unsigned i = 1; i <= nsm; i++) {
        const uint8_t *h = b + so + (size_t)(i - 1) * 37; XSmp *m = &s->smp[i];
        uint32_t len = rd32(h + 22), ls = rd32(h + 26), le = rd32(h + 30);
        int is16 = h[36] & 1;
        m->vol = h[35] > 64 ? 64 : h[35]; m->c2spd = ((int8_t)h[34] >= -8 && (int8_t)h[34] <= 7) ? ngp_finetune_c2spd[h[34] & 15] : (uint32_t)(8363.0 * pow(2.0, (int8_t)h[34] / 96.0) + 0.5);   /* MultiTracker allows the whole signed byte (1/8 semitone per step): the MOD table inside -8..7 (fits libopenmpt best there), the formula beyond */
        if (!len) continue;
        size_t avail = d < n ? n - d : 0, take = len < avail ? len : avail;
        if (is16) { len >>= 1; ls >>= 1; le >>= 1; take >>= 1; }
        if (len > (64u << 20)) { d += len; continue; }
        m->len = len;
        m->data = calloc((size_t)len * (is16 ? 2 : 1) + 4, 1);
        if (!m->data) return -5;
        m->is16 = is16;
        if (is16) { int16_t *w = (int16_t *)m->data; for (uint32_t k = 0; k < take; k++) w[k] = (int16_t)(rd16(b + d + 2 * k) ^ 0x8000); }      /* 16 bit samples are unsigned too (libopenmpt) */
        else for (uint32_t k = 0; k < take; k++) m->data[k] = (int8_t)(b[d + k] ^ 0x80);      /* unsigned 8 bit */
        { uint32_t e = (le > 1 ? le : 1) - 1; if (e > len) e = len; uint32_t st = ls; if (st + 4 >= e) st = e = 0;      /* libopenmpt: the loop end is one less, and a loop of 4 samples or less is none */
          if (e > 2) { m->loop = 1; m->ls = st; m->le = e; } }
        d += (size_t)len * (is16 ? 2 : 1);
    }

    /* patterns: every pattern lists 32 track numbers (1-based, 0 = empty); note = byte0 >> 2, inst = (byte0 & 3) << 4 | byte1 >> 4 */
    s->pat = calloc((size_t)npat * 64 * nch + 1, sizeof(XCell));
    if (!s->pat) return -5;
    for (unsigned p = 0; p < npat; p++)
        for (unsigned c = 0; c < nch; c++) {
            unsigned t = rd16(b + qo + (size_t)p * 64 + c * 2);
            if (!t || t > ntr) continue;
            const uint8_t *q = b + to + (size_t)(t - 1) * rows * 3;
            for (unsigned r = 0; r < rows; r++, q += 3) {
                XCell *x = &s->pat[((size_t)p * 64 + r) * nch + c];
                if (q[0] >> 2) x->note = (uint8_t)((q[0] >> 2) + 24);       /* same note numbering as the MOD loader's period index */
                x->inst = (uint8_t)(((q[0] & 3) << 4) | (q[1] >> 4));
                x->eff = q[1] & 15; x->par = q[2];
                /* what MultiTracker does not support (libopenmpt drops it): 8xx panning (only E8x), E0x/E3x/E4x/E6x/E7x/EFx; Axy slides only one way (the upper nibble wins) */
                if (x->eff == 0x0A) { if (x->par & 0xF0) x->par &= 0xF0; else x->par &= 0x0F; }
                else if (x->eff == 0x08) x->eff = x->par = 0;
                else if (x->eff == 0x0E) switch (x->par & 0xF0) { case 0x00: case 0x30: case 0x40: case 0x60: case 0x70: case 0xF0: x->eff = x->par = 0; break; }
            }
            if (rows < 64) { XCell *x = &s->pat[((size_t)p * 64 + rows - 1) * nch + c]; if (!x->eff && c == 0) { x->eff = 0x0D; x->par = 0; } }   /* short pattern: break after the last row */
        }
    /* MultiTracker: a speed command resets the tempo to 125 and a tempo command the speed to 6. Players of the time did not do that and many songs rely on theirs, so libopenmpt
     * keeps the ProTracker way when a speed and a tempo command ever share a row, and the MultiTracker way when they never do: then every row with such a command gets the
     * opposite one added in the first channel that has no effect there. */
    {
        int hasS = 0, hasT = 0, same = 0;
        for (unsigned p = 0; p < npat; p++) for (unsigned r = 0; r < 64; r++) {
            int rs = 0, rt = 0;
            for (unsigned c = 0; c < nch; c++) { const XCell *x = &s->pat[((size_t)p * 64 + r) * nch + c]; if (x->eff == 0x0F) { if (x->par < 0x20) rs = 1; else rt = 1; } }
            hasS |= rs; hasT |= rt; if (rs && rt) same = 1;
        }
        if (hasS && hasT && !same)
            for (unsigned p = 0; p < npat; p++) for (unsigned r = 0; r < 64; r++) {
                int kind = 0;
                for (unsigned c = 0; c < nch && !kind; c++) { const XCell *x = &s->pat[((size_t)p * 64 + r) * nch + c]; if (x->eff == 0x0F) kind = x->par < 0x20 ? 1 : 2; }
                if (!kind) continue;
                for (unsigned c = 0; c < nch; c++) { XCell *x = &s->pat[((size_t)p * 64 + r) * nch + c]; if (!x->eff && !x->par) { x->eff = 0x0F; x->par = kind == 1 ? 125 : 6; break; } }
            }
    }
    return 0;
}
