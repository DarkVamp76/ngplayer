/* STM loader (Scream Tracker 2). Fills the same XSong the S3M loader produces, so the S3M replayer plays it.
 * Layout: 0x30-byte header, 31 instruments x 32 bytes, 128 orders, patterns of 64 rows x 4 channels x 4 bytes,
 * then the sample data (parapointers in the instrument headers). GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }

/* STM effect nibble 1..12 = A..L, the same order as S3M: internal codes of the S3M table */
static const int stm_eff[13] = { -1, 0x10, 0x0B, 0x0D, 0x11, 0x12, 0x13, 0x03, 0x04, 0x14, 0x00, 0x17, 0x18 };

int stm_probe(const uint8_t *b, size_t n){
    if (n < 0x490 || b[0x1C] != 0x1A || (b[0x1D] != 1 && b[0x1D] != 2)) return 0;
    return !memcmp(b + 0x14, "!Scream!", 8) || !memcmp(b + 0x14, "BMOD2STM", 8) || !memcmp(b + 0x14, "WUZAMOD!", 8);
}

int xstm_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 2; s->maxvol = 0x40; s->gvol = 255;
    if (!stm_probe(b, n)) return -1;
    memcpy(s->title, b, 20); s->title[20] = 0;
    unsigned verminor = b[0x1F], tempo = b[0x20], npat = b[0x21], gv = b[0x22];
    if (npat > 99) return -2;
    if (verminor < 21) tempo = ((tempo / 10) << 4) + tempo % 10;      /* older versions store the speed as decimal digits */
    s->speed = (int)(tempo >> 4);
    if (s->speed < 1) s->speed = 6;
    s->tempo = 125;                                                  /* ST2 ticks run at a fixed 50 Hz */
    s->fast = 0;
    if (gv > 64) gv = 64;
    s->gvol = gv ? (int)(gv << 2) : 255;
    if (s->gvol > 255) s->gvol = 255;
    for (int i = 0; i < 32; i++) { s->chen[i] = i < 4; s->chpan[i] = -1; }
    for (int i = 0; i < 4; i++) s->chpan[i] = ngp_pan_tab[7];        /* ST2 is mono */

    const uint8_t *ord = b + 0x30 + 31 * 32;
    int nord = 128;
    for (int i = 0; i < 128; i++) if (ord[i] >= 99) { nord = i; break; }   /* 99 = end of song */
    for (int i = 0; i < nord; i++) s->orders[i] = ord[i] < npat ? ord[i] : 0xFE;
    while (nord > 0 && s->orders[nord - 1] > 0xFD) nord--;
    s->nord = nord;

    size_t pbase = 0x30 + 31 * 32 + 128;
    if (pbase + (size_t)npat * 1024 > n) return -3;

    /* samples */
    s->nsmp = 31;
    for (int i = 1; i <= 31; i++) {
        const uint8_t *h = b + 0x30 + 32 * (i - 1); XSmp *m = &s->smp[i];
        unsigned seg = rd16(h + 0x0E), len = rd16(h + 0x10), ls = rd16(h + 0x12), le = rd16(h + 0x14);
        m->vol = h[0x16] > 64 ? 64 : h[0x16]; m->c2spd = rd16(h + 0x18);
        if (len < 2 || !seg) continue;
        size_t d = (size_t)seg << 4;
        if (d >= n) continue;
        if (d + len > n) len = (unsigned)(n - d);
        m->len = len;
        m->data = calloc((size_t)len + 4, 1);
        for (unsigned k = 0; k < len; k++) m->data[k] = (int8_t)b[d + k];     /* signed 8 bit */
        if (le != 0xFFFF && le > ls && le > 1) {
            m->loop = 1; m->ls = ls; m->le = le > len ? len : le;
            if (m->ls >= m->le) m->loop = 0;
        }
    }

    /* patterns: note, instrument<<3 | volume low bits, volume high bits<<4 | effect, effect parameter */
    s->nch = 4; s->npat = (int)npat;
    s->pat = calloc((size_t)npat * 64 * 4 + 1, sizeof(XCell));
    for (unsigned p = 0; p < npat; p++)
        for (int r = 0; r < 64; r++)
            for (int c = 0; c < 4; c++) {
                const uint8_t *q = b + pbase + (size_t)p * 1024 + r * 16 + c * 4;
                XCell *x = &s->pat[((size_t)p * 64 + r) * 4 + c];
                int note = q[0], iv = q[1], vc = q[2], inf = q[3];
                if (note == 0xFE || note == 0xFC) x->note = 0x7F;                      /* note cut */
                else if (note < 0xFC && (note & 15) < 12 && (note >> 4) <= 12) x->note = (uint8_t)(((note >> 4) + 2) * 12 + (note & 15));
                if (iv >> 3) x->inst = (uint8_t)(iv >> 3);
                int vol = (iv & 7) | ((vc & 0xF0) >> 1);
                if (vol <= 64) x->vol1 = (uint8_t)(vol + 1);
                int cmd = vc & 15;
                if (cmd >= 1 && cmd <= 12) {
                    x->eff = (uint8_t)stm_eff[cmd]; x->par = (uint8_t)inf;
                    if (cmd == 1) {                                                     /* A: speed = high nibble */
                        int sp = (verminor < 21 ? (((inf / 10) << 4) + inf % 10) : inf) >> 4;
                        if (sp) x->par = (uint8_t)sp; else { x->eff = 0; x->par = 0; }
                    } else if ((cmd == 5 || cmd == 6) && inf >= 0xE0) x->par = 0xDF;     /* no fine slides in ST2 */
                }
            }
    return 0;
}
