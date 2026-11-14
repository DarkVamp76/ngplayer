/* SoundFX loader (Linel's SoundFX 1.3 with 15 samples and SoundFX 2.0 with 31 samples, Amiga; e.g. Future Wars, Twinworld, Operation Stealth).
 * Layout: sample lengths (u32 big endian, 15 or 31), "SONG" ("SO31" in 2.0), delay (u16, tempo = 14565 * 122 / delay BPM), 14 unknown bytes, sample headers
 * (30 bytes: name 22, length in words, finetune, volume, loop start in bytes, loop length in words), song length, restart, 128 orders,
 * patterns of 64 rows x 4 channels x 4 bytes (period + instrument like a MOD, but with its own effect set and the special periods 0xFFFE = stop, 0xFFFC = pattern break), then signed 8-bit samples.
 * The song is converted into MOD cells and played by the MOD replayer. The layout follows the reverse-engineered description used by
 * libxmp (MIT) as a format reference. GPL-3.0-or-later. */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | (p[2] << 8) | p[3]; }
static unsigned be16(const uint8_t *p){ return (p[0] << 8) | p[1]; }

/* returns 15 or 31 (number of samples), 0 if this is not a SoundFX file */
int sfx_probe(const uint8_t *b, size_t n){
    if (n >= 4 * 15 + 4 && !memcmp(b + 60, "SONG", 4)) return 15;
    if (n >= 4 * 31 + 4 && (!memcmp(b + 124, "SO31", 4) || !memcmp(b + 124, "SONG", 4))) return 31;   /* SoundFX 2.0 uses "SO31" */
    return 0;
}

static int period_to_note(int per){
    if (!per) return 0;
    for (int i = 0; i < 84; i++)
        if (per >= ngp_pt_periods[i]) return i + 1 + 0x17;
    return 84 + 0x17;
}

int xsfx_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    int nins = sfx_probe(b, n);
    if (!nins) return -1;
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->amiga = 1;
    size_t o = (size_t)nins * 4 + 4;                                            /* behind "SONG" */
    unsigned delay = be16(b + o);
    if (delay < 178) return -2;
    s->tempo_exact = 14565.0 * 122.0 / delay;                                   /* BPM as a fraction: 122.55 at the usual delay of 14500 */
    s->tempo = (int)(s->tempo_exact + 0.5); if (s->tempo < 32) s->tempo = 32; if (s->tempo > 255) s->tempo = 255;
    o += 2 + 14;
    size_t hdr = o, ord = hdr + (size_t)nins * 30;
    if (ord + 2 + 128 > n) return -3;
    unsigned len = b[ord]; if (len < 1 || len > 0x7f) return -4;
    s->nch = 4; s->nsmp = nins; s->nord = (int)len; s->title[0] = 0;
    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    unsigned maxp = 0;
    for (unsigned i = 0; i < len; i++) { s->orders[i] = b[ord + 2 + i]; if (s->orders[i] > maxp) maxp = s->orders[i]; }
    s->npat = (int)maxp + 1;
    size_t po = ord + 2 + 128, d = po + (size_t)s->npat * 64 * 4 * 4;
    if (d > n) return -5;
    for (int i = 1; i <= nins; i++) {
        const uint8_t *h = b + hdr + (size_t)(i - 1) * 30; XSmp *m = &s->smp[i];
        uint32_t slen = be32(b + (size_t)(i - 1) * 4), ls = be16(h + 26), ll = be16(h + 28);
        m->vol = h[25] > 64 ? 64 : h[25]; m->c2spd = ngp_finetune_c2spd[h[24] & 15];
        if (slen <= 2) continue;
        size_t avail = d < n ? n - d : 0, take = slen < avail ? slen : avail;
        if (slen > (16u << 20)) { d += slen; continue; }
        m->len = slen; m->data = calloc((size_t)slen + 4, 1);
        if (!m->data) return -6;
        memcpy(m->data, b + d, take);
        if (ll > 1) { m->loop = 1; m->ls = ls; m->le = ls + 2 * ll; if (m->le > slen) m->le = slen; if (m->ls >= m->le) m->loop = 0; }
        d += slen;
    }
    s->pat = calloc((size_t)s->npat * 64 * 4 + 1, sizeof(XCell));
    if (!s->pat) return -6;
    for (int p = 0; p < s->npat; p++)
        for (int r = 0; r < 64; r++)
            for (int c = 0; c < 4; c++) {
                const uint8_t *q = b + po + (((size_t)p * 64 + r) * 4 + c) * 4;
                XCell *x = &s->pat[((size_t)p * 64 + r) * 4 + c];
                if (q[0] == 0xFF && q[1] >= 0xFC) {                    /* special "periods": FFFE stop the note, FFFC end the pattern (break), FFFD unused */
                    if (q[1] == 0xFE) { x->eff = 0x0E; x->par = 0xC0; }
                    else if (q[1] == 0xFC) { x->eff = 0x0D; x->par = 0x00; }
                    continue;
                }
                x->note = (uint8_t)period_to_note(((q[0] & 15) << 8) | q[1]);
                x->inst = (uint8_t)((q[0] & 0xf0) | (q[2] >> 4));
                int fx = q[2] & 15, fp = q[3];
                switch (fx) {
                case 0x1: x->eff = 0x00; x->par = (uint8_t)fp; break;                                         /* arpeggio */
                case 0x2: if (fp >> 4) { x->eff = 0x02; x->par = (uint8_t)(fp >> 4); } else if (fp & 15) { x->eff = 0x01; x->par = (uint8_t)(fp & 15); } break;   /* pitch bend */
                case 0x5: x->eff = 0x0E; x->par = (uint8_t)(0xA0 | (fp > 15 ? 15 : fp)); break;               /* add to volume (once per row) */
                case 0x6: x->eff = 0x0E; x->par = (uint8_t)(0xB0 | (fp > 15 ? 15 : fp)); break;               /* subtract from volume */
                default: break;                                                                               /* 3/4 LED, 7/8 semitone add/sub: not handled */
                }
            }
    return 0;
}
