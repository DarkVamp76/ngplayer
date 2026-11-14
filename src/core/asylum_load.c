/* Asylum Music Format loader ("ASYLUM Music Format V1.0", Crusader: No Remorse / No Regret). The effects are ProTracker-style, so the
 * song is handed to the MOD replayer (fmt 1). Layout: 32-byte signature, speed, tempo, number of samples, patterns and orders, restart
 * position, 256 orders, 64 sample headers x 37 bytes (name 22, finetune, volume, transpose, length, loop start, loop length),
 * patterns of 64 rows x 8 channels x 4 bytes (note, instrument, effect, parameter), then the signed 8-bit sample data. GPL-3.0-or-later.
 * This is a different format from the DSMI "AMF" (signature "AMF" + version byte), which shares the file extension. */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

#define ASY_HDR 294            /* 32 + 6 + 256 */
#define ASY_SMP 37
#define ASY_PAT (ASY_HDR + 64 * ASY_SMP)

int asylum_probe(const uint8_t *b, size_t n){
    return n >= ASY_PAT && !memcmp(b, "ASYLUM Music Format V1.0", 24);
}

#ifndef ASY_NOTE_OFS
#define ASY_NOTE_OFS 0
#endif

int xasylum_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255;
    if (!asylum_probe(b, n)) return -1;
    s->speed = b[32] ? b[32] : 6; s->tempo = b[33] >= 32 ? b[33] : 125;
    unsigned nsm = b[34], npat = b[35], nord = b[36];
    if (nsm > 64 || !nord || !npat) return -2;
    s->nch = 8; s->nsmp = (int)(nsm > 31 ? 31 : nsm);
    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    for (unsigned i = 0; i < nord; i++) s->orders[i] = b[38 + i] < npat ? b[38 + i] : 0xFE;
    s->nord = (int)nord; s->npat = (int)npat;
    size_t so = ASY_HDR, po = ASY_PAT, d = po + (size_t)npat * 64 * 8 * 4;
    if (d > n) return -3;
    s->title[0] = 0;
    for (unsigned i = 1; i <= nsm && i < 100; i++) {
        const uint8_t *h = b + so + (size_t)(i - 1) * ASY_SMP; XSmp *m = &s->smp[i];
        uint32_t len = rd32(h + 25), ls = rd32(h + 29), ll = rd32(h + 33);
        m->vol = h[23] > 64 ? 64 : h[23]; m->c2spd = ngp_finetune_c2spd[h[22] & 15];
        if (!len) continue;
        size_t avail = d < n ? n - d : 0, take = len < avail ? len : avail;
        if (len > (64u << 20)) { d += len; continue; }
        m->len = len; m->data = calloc((size_t)len + 4, 1);
        if (!m->data) return -5;
        memcpy(m->data, b + d, take);
        if (ll > 2) { m->loop = 1; m->ls = ls; m->le = ls + ll; if (m->le > len) m->le = len; if (m->ls >= m->le) m->loop = 0; }
        d += len;
    }
    s->pat = calloc((size_t)npat * 64 * 8 + 1, sizeof(XCell));
    if (!s->pat) return -5;
    for (unsigned p = 0; p < npat; p++)
        for (unsigned r = 0; r < 64; r++)
            for (unsigned c = 0; c < 8; c++) {
                const uint8_t *q = b + po + (((size_t)p * 64 + r) * 8 + c) * 4;
                XCell *x = &s->pat[((size_t)p * 64 + r) * 8 + c];
                if (q[0]) x->note = (uint8_t)(q[0] + ASY_NOTE_OFS);
                x->inst = q[1]; x->eff = q[2]; x->par = q[3];
            }
    return 0;
}
