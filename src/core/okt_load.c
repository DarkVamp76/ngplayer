/* OKT loader: Oktalyzer (Amiga, Armin Sander), IFF "OKTASONG". Builds an ITSong that is played by the IT engine, following libopenmpt's
 * Load_okt (Storlek / Schism): 4 Amiga channels, each one can be split into two mixed channels (CMOD), 36 samples with sustain loops (the
 * R effect releases them), the volume of split channels through the channel volume, note slides and the three arpeggio forms.
 * GPL-3.0-or-later */
#include "it.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned be32(const uint8_t *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

int okt_probe(const uint8_t *b, size_t n) { return n >= 16 && !memcmp(b, "OKTASONG", 8); }

/* the IT effect weight order of OpenMPT (ModCommand::GetEffectWeight) for the commands used here: a higher weight wins a cell */
static int weight(int cmd) {
    switch (cmd) { case 0: return 0; case 10: return 30; case 5: case 6: return 60; case 27: case 28: return 55; case 4: return 70; case 14: return 75;
                   case 13: return 80; case 1: return 95; case 2: return 100; default: return 50; }
}

static uint8_t inv_arp(uint8_t p) { p &= 15; return !p ? 0 : p <= 12 ? (uint8_t)(12 - p) : (uint8_t)(24 - p); }

ITSong *okt_load(const uint8_t *b, size_t n, int *err) {
    if (!okt_probe(b, n)) { *err = -1; return NULL; }
    ITSong *s = (ITSong *)calloc(1, sizeof *s); if (!s) { *err = -2; return NULL; }
    int8_t paired[8] = { 0 }; int nch = 0, nsmp0 = 0, norders = 0, speed = 6;
    const uint8_t *samp = NULL; size_t sampn = 0;
    const uint8_t *pb[256]; size_t pbn[256]; int npb = 0;
    const uint8_t *sb[72]; size_t sbn[72]; int nsb = 0;
    const uint8_t *patt = NULL; size_t pattn = 0;
    size_t p = 8;
    while (p + 8 <= n) {
        unsigned id = be32(b + p); size_t len = be32(b + p + 4); p += 8;
        if (len > n - p) len = n - p;
        const uint8_t *c = b + p;
        switch (id) {
        case 0x434D4F44: /* CMOD */
            if (!nch && len >= 8) {
                for (int ch = 0; ch < 4; ch++) {
                    int pan = ((ch & 3) == 1 || (ch & 3) == 2) ? 48 : 16;           /* libopenmpt 0xC0 / 0x40 of 256 */
                    if (be16(c + ch * 2)) { paired[nch] = 1; s->chpan[nch++] = (uint8_t)pan; paired[nch] = -1; }
                    s->chpan[nch++] = (uint8_t)pan;
                }
            }
            break;
        case 0x53414D50: /* SAMP */ if (!samp) { samp = c; sampn = len; } break;
        case 0x53504545: /* SPEE */ if (len >= 2) { speed = (int)be16(c); if (speed < 1) speed = 1; if (speed > 255) speed = 255; } break;
        case 0x504C454E: /* PLEN */ if (len >= 2) norders = (int)be16(c); break;
        case 0x50415454: /* PATT */ patt = c; pattn = len; break;
        case 0x50424F44: /* PBOD */ if (npb < 256) { pb[npb] = c; pbn[npb++] = len; } break;
        case 0x53424F44: /* SBOD */ if (nsb < 72 && len > 0) { sb[nsb] = c; sbn[nsb++] = len; } break;
        }
        p += len;
    }
    if (!nch) { it_free(s); *err = -3; return NULL; }
    s->nch = nch; s->speed = speed; s->tempo = 125; s->gv = 128; s->mv = 48; s->sep = 128;
    s->flags = 1 | 16;                         /* stereo, old effects (Amiga slides, no linear table) */
    s->okt = 1; s->cwt = 0x214; s->cmwt = 0x214;
    for (int i = 0; i < 64; i++) s->chvol[i] = 64;
    for (int i = nch; i < 64; i++) s->chpan[i] = 32;
    snprintf(s->title, sizeof s->title, "%s", "");
    /* samples: 36 in the file, plus copies (37..72) without the loop for "B" samples on split channels, plus one empty sample for the
       notes that libopenmpt silences (a sample made for the other kind of channel) */
    nsmp0 = samp ? (int)(sampn / 32) : 0; if (nsmp0 > 36) nsmp0 = 36;
    int empty = 73;
    s->nsmp = empty; s->smp = (ITSmp *)calloc((size_t)s->nsmp + 1, sizeof(ITSmp));
    int type[37] = { 0 }; uint8_t need_copy[37] = { 0 };
    if (!s->smp) { it_free(s); *err = -2; return NULL; }
    for (int i = 1; i <= nsmp0; i++) {
        const uint8_t *h = samp + (i - 1) * 32; ITSmp *m = &s->smp[i];
        memcpy(m->name, h, 20); m->name[20] = 0;
        m->c5spd = 8287; m->gvl = 64; unsigned v = be16(h + 28); m->vol = v > 64 ? 64 : (int)v;
        m->len = be32(h + 20) & ~1u; type[i] = (int)be16(h + 30);
        uint32_t ls = be16(h + 24) * 2u, ll = be16(h + 26) * 2u;
        if (ll > 2 && ls + ll <= m->len) { m->sloop = 1; m->slb = ls; m->sle = ls + ll; m->flags |= 32; }
    }
    /* patterns */
    s->npat = npb; s->pat = (ITCell **)calloc((size_t)(npb ? npb : 1), sizeof(ITCell *)); s->rows = (int *)calloc((size_t)(npb ? npb : 1), sizeof(int));
    if (!s->pat || !s->rows) { it_free(s); *err = -2; return NULL; }
    for (int pt = 0; pt < npb; pt++) {
        const uint8_t *c = pb[pt]; size_t cl = pbn[pt];
        int rows = cl >= 2 ? (int)be16(c) : 64; if (rows < 1) rows = 1; if (rows > 1024) rows = 1024;
        s->rows[pt] = rows; ITCell *cells = (ITCell *)calloc((size_t)rows * 64, sizeof(ITCell)); s->pat[pt] = cells;
        if (!cells) { it_free(s); *err = -2; return NULL; }
        size_t q = 2;
        for (int r = 0; r < rows; r++) for (int ch = 0; ch < nch; ch++, q += 4) {
            if (q + 4 > cl) continue;
            ITCell *m = &cells[(size_t)r * 64 + ch];
            int note = c[q], ins = c[q + 1], eff = c[q + 2], par = c[q + 3];
            int oldcmd = m->cmd, oldpar = m->par; int oldhas = m->has & IT_HAS_CMD;
            if (note > 0 && note <= 36) {
                int nn = note + 60 - 13;                                      /* NOTE_MIDDLEC (61, 1-based) - 13 -> IT 0-based C-5 = 60 */
                if (paired[ch] && nn >= 60 + 21) nn = 60 + 20;               /* split channels cannot play above A#5 */
                m->note = (uint8_t)nn; m->has |= IT_HAS_NOTE;
                int in = ins + 1;
                if (in >= 1 && in <= nsmp0) {
                    if (paired[ch] && s->smp[in].vol < 64) { m->vol = 64; m->has |= IT_HAS_VOL; }
                    if ((type[in] == 1 && paired[ch]) || (type[in] == 0 && !paired[ch])) in = empty;   /* a sample made for the other kind of channel: silent */
                    else if (type[in] == 2 && paired[ch] && s->smp[in].sloop) { need_copy[in] = 1; in += 36; }
                } else in = empty;
                m->inst = (uint8_t)in; m->has |= IT_HAS_INST;
            }
            int cmd = 0, cp = 0;
            switch (eff) {
            case 1: if (par) { cmd = 6; cp = par; } break;                     /* 1: period down = pitch up (F) */
            case 2: if (par) { cmd = 5; cp = par; } break;                     /* 2: E */
            case 10: if (par) { cmd = 10; cp = (par & 15) | (inv_arp((uint8_t)(par >> 4)) << 4); } break;
            case 11: if (par) { cmd = 10; cp = (par & 0xF0) | inv_arp((uint8_t)(par & 15)); } break;
            case 12: if (par) { cmd = 10; cp = par; } break;
            case 13: if (par) { cmd = 27; cp = 0x10 | (par > 15 ? 15 : par); } break;   /* D: note slide down, every tick */
            case 30: if (par) { cmd = 28; cp = 0x10 | (par > 15 ? 15 : par); } break;   /* U: up */
            case 21: if (par) { cmd = 27; cp = 0x50 | (par > 15 ? 15 : par); } break;   /* L: down once */
            case 17: if (par) { cmd = 28; cp = 0x50 | (par > 15 ? 15 : par); } break;   /* H: up once */
            case 25: cmd = 2; cp = par; break;                                 /* P: position jump */
            case 27: m->note = 255; m->has |= IT_HAS_NOTE; m->inst = 0; m->has &= ~IT_HAS_INST; break;   /* R: release (note off) */
            case 28: if (par < 0x20) { cmd = 1; cp = par; } break;            /* S: speed */
            case 31: {                                                         /* V: volume */
                int vc = 0, vpar = par;
                if (par & 15) { cmd = paired[ch] ? 14 : 4; cp = par & 15; }
                switch (par >> 4) {
                case 4: if (par != 0x40) break; /* fall through */
                case 0: case 1: case 2: case 3:
                    if (paired[ch]) { cmd = 13; cp = par; }
                    else { m->vol = (uint8_t)(par > 64 ? 64 : par); m->has |= IT_HAS_VOL; cmd = oldhas ? oldcmd : 0; cp = oldpar; vc = 1; }
                    break;
                case 5: cp = (cp << 4) & 0xFF; break;
                case 6: cp = 0xF0 | ((par & 15) > 14 ? 14 : (par & 15)); break;
                case 7: cp = (((par & 15) > 14 ? 14 : (par & 15)) << 4) | 0x0F; break;
                default: cmd = oldhas ? oldcmd : 0; cp = oldpar; break;
                }
                (void)vc; (void)vpar;
                if (paired[ch] && (cmd == 13 || cmd == 14)) {                /* the channel volume of a split channel goes to both halves */
                    int other = ch + paired[ch];
                    ITCell *o = &cells[(size_t)r * 64 + other];
                    if (other < ch || !(o->has & IT_HAS_CMD)) {
                        if (weight(o->has & IT_HAS_CMD ? o->cmd : 0) < weight(cmd)) { o->cmd = (uint8_t)cmd; o->par = (uint8_t)cp; o->has |= IT_HAS_CMD; }
                        else if (r < rows - 1) { ITCell *nx = &cells[(size_t)(r + 1) * 64 + other]; nx->cmd = (uint8_t)cmd; nx->par = (uint8_t)cp; nx->has |= IT_HAS_CMD; }
                    } else if (r < rows - 1) { ITCell *nx = &cells[(size_t)(r + 1) * 64 + other]; nx->cmd = (uint8_t)cmd; nx->par = (uint8_t)cp; nx->has |= IT_HAS_CMD; }
                }
                break;
            }
            default: break;
            }
            if (cmd) {
                if (oldhas && oldcmd && oldcmd != cmd && weight(oldcmd) > weight(cmd)) { /* keep the stronger command already in the cell (FillInTwoCommands) */ }
                else { m->cmd = (uint8_t)cmd; m->par = (uint8_t)cp; m->has |= IT_HAS_CMD; }
            }
        }
    }
    /* orders */
    s->nord = norders; if (s->nord > 256) s->nord = 256; if (patt && (size_t)s->nord > pattn) s->nord = (int)pattn;
    for (int i = 0; i < s->nord; i++) s->orders[i] = patt[i] < npb ? patt[i] : 254;
    for (int i = s->nord; i < 256; i++) s->orders[i] = 255;
    if (s->nord < 1) { s->nord = 1; s->orders[0] = 0; }
    /* sample data: one SBOD chunk per sample that has a length, in order */
    int fs = 0;
    for (int i = 1; i <= nsmp0; i++) {
        ITSmp *m = &s->smp[i];
        if (!m->len) continue;
        if (fs >= nsb) { m->len = 0; continue; }
        if (m->len > sbn[fs]) m->len = (uint32_t)sbn[fs];
        int8_t *d = (int8_t *)malloc(m->len ? m->len : 1); if (!d) { it_free(s); *err = -2; return NULL; }
        memcpy(d, sb[fs], m->len); m->data = d;
        if (m->sloop && m->sle > m->len) { m->sloop = 0; m->flags &= ~32; }
        if (need_copy[i]) {
            ITSmp *cpy = &s->smp[i + 36]; *cpy = *m; cpy->sloop = 0; cpy->flags &= ~32; cpy->slb = cpy->sle = 0;
            int8_t *d2 = (int8_t *)malloc(m->len); if (!d2) { it_free(s); *err = -2; return NULL; } memcpy(d2, d, m->len); cpy->data = d2;
        }
        fs++;
    }
    return s;
}
