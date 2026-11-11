/* 669 loader (Composer 669 "if" and UNIS 669 "JN"). Fills the same XSong the S3M loader produces, so the S3M
 * replayer plays it. Layout: 108-byte song message, sample/pattern counts, loop order, 128 orders, 128 per-pattern
 * speeds, 128 per-pattern break rows, 25-byte sample headers, patterns of 64 rows x 8 channels x 3 bytes, then
 * unsigned 8-bit samples. Effects are remembered per channel until the next note or a zero parameter; slides are
 * linear in Hz (replayer codes 0x30..0x34). Timing: 78 BPM, speed (ticks per row) from the pattern's speed list.
 * GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

int s669_probe(const uint8_t *b, size_t n){
    if (n < 0x1f1 || !((b[0] == 'i' && b[1] == 'f') || (b[0] == 'J' && b[1] == 'N'))) return 0;
    unsigned nos = b[0x6e], nop = b[0x6f];
    if (nos > 64 || nop > 128 || !nop) return 0;
    for (int i = 0; i < 128; i++) {
        unsigned o = b[0x71 + i];
        if (o >= 128 && o < 0xFE) return 0;
        if (o < 128 && (b[0xf1 + i] == 0 || b[0xf1 + i] > 15)) return 0;
        if (b[0x171 + i] >= 64) return 0;
    }
    return n >= 0x1f1 + (size_t)nos * 25 + (size_t)nop * 0x600;
}

int xs669_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 2; s->maxvol = 0x40; s->gvol = 255;
    if (!s669_probe(b, n)) return -1;
    memcpy(s->title, b + 2, 20); s->title[20] = 0;
    unsigned nos = b[0x6e], nop = b[0x6f];
    s->speed = 4; s->tempo = 78; s->fast = 0; s->restart = b[0x70];
    for (int i = 0; i < 32; i++) { s->chen[i] = i < 8; s->chpan[i] = ngp_pan_tab[(i & 1) ? 11 : 4]; }

    int nord = 128;
    for (int i = 0; i < 128; i++) if (b[0x71 + i] == 0xFF) { nord = i; break; }
    for (int i = 0; i < nord; i++) s->orders[i] = b[0x71 + i] < nop ? b[0x71 + i] : 0xFE;
    while (nord > 0 && s->orders[nord - 1] > 0xFD) nord--;
    s->nord = nord;

    /* samples: unsigned 8 bit after the patterns */
    size_t dpos = 0x1f1 + (size_t)nos * 25 + (size_t)nop * 0x600;
    s->nsmp = (int)nos;
    for (unsigned i = 0; i < nos; i++) {
        const uint8_t *h = b + 0x1f1 + (size_t)i * 25;
        size_t len = rd32(h + 13); unsigned ls = rd32(h + 17), le = rd32(h + 21);
        XSmp *m = &s->smp[i + 1];
        m->vol = 64; m->c2spd = 8363;
        size_t d = dpos; dpos += len;
        if (len < 2 || d >= n) continue;
        if (d + len > n) len = n - d;
        m->len = (uint32_t)len;
        m->data = calloc(len + 4, 1);
        for (size_t k = 0; k < len; k++) m->data[k] = (int8_t)(b[d + k] - 128);
        if (le < 0xFFFFF && le > ls && ls < len) { m->loop = 1; m->ls = ls; m->le = le > len ? (uint32_t)len : le; }
    }

    /* patterns */
    s->nch = 8; s->npat = (int)nop;
    s->pat = calloc((size_t)nop * 64 * 8 + 1, sizeof(XCell));
    const uint8_t *pd = b + 0x1f1 + (size_t)nos * 25;
    for (unsigned p = 0; p < nop; p++) {
        uint8_t mem[8]; memset(mem, 0xFF, sizeof mem);
        uint8_t expl[64][8]; memset(expl, 0, sizeof expl);
        for (int r = 0; r < 64; r++)
            for (int c = 0; c < 8; c++) {
                const uint8_t *q = pd + (size_t)p * 0x600 + r * 24 + c * 3;
                XCell *x = &s->pat[((size_t)p * 64 + r) * 8 + c];
                int a = q[0], v = q[1], e = q[2], hasnote = 0;
                if (a < 0xFE) {
                    x->note = (uint8_t)((a >> 2) + 24);                                  /* 0 = C-3 of OpenMPT's numbering */
                    x->inst = (uint8_t)((((a & 3) << 4) | (v >> 4)) + 1);
                    mem[c] = 0xFF; hasnote = 1;
                }
                if (a <= 0xFE) x->vol1 = (uint8_t)(((v & 15) * 64 + 8) / 15 + 1);
                if (e != 0xFF) { mem[c] = (uint8_t)e; expl[r][c] = 1; if ((e & 15) == 0 && e != 0x30) mem[c] = 0xFF; }
                if (mem[c] == 0xFF) continue;
                int cmd = mem[c] >> 4, par = mem[c] & 15;
                switch (cmd) {
                case 0: x->eff = 0x30; x->par = (uint8_t)par; break;
                case 1: x->eff = 0x31; x->par = (uint8_t)par; break;
                case 2: x->eff = 0x32; x->par = (uint8_t)par; if (hasnote) x->inst = 0; break;   /* the note's instrument is ignored */
                case 3: x->eff = 0x33; x->par = (uint8_t)par; break;
                case 4: x->eff = 0x34; x->par = (uint8_t)par; break;
                case 5: if (par) { x->eff = 0x10; x->par = (uint8_t)par; } break;
                default: break;
                }
            }
        /* pattern speed at row 0 and the break row come from the header lists: put them into a free effect slot */
        int spd = b[0xf1 + p];
        for (int pass = 0; pass < 2; pass++) {
            int row = pass == 0 ? 0 : b[0x171 + p];
            if (pass == 1 && row >= 63) break;
            int pick = -1;
            for (int c = 0; c < 8 && pick < 0; c++) { XCell *x = &s->pat[((size_t)p * 64 + row) * 8 + c]; if (!x->eff && !x->par) pick = c; }
            for (int c = 0; c < 8 && pick < 0; c++) if (!expl[row][c]) pick = c;
            if (pick < 0) pick = 0;
            XCell *x = &s->pat[((size_t)p * 64 + row) * 8 + pick];
            if (pass == 0) { x->eff = 0x10; x->par = (uint8_t)spd; } else { x->eff = 0x0D; x->par = 0; }
        }
    }
    return 0;
}
