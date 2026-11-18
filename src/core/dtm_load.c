/* DTM loader: Digital Tracker (Atari Falcon030), file signature "D.T.". Builds an ITSong that is played by the IT engine, like libopenmpt does
 * (old IT effects, Gxx compatibility, Amiga periods). Pattern formats "2.04" and ProTracker cells are read; "2.06" (Digital Home Studio, tick based
 * patterns) is rejected. GPL-3.0-or-later */
#include "it.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned be32(const uint8_t *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

int dtm_probe(const uint8_t *b, size_t n) {
    if (n < 22 || memcmp(b, "D.T.", 4)) return 0;
    unsigned hs = be32(b + 4);
    return hs >= 14 && hs <= 256 && be16(b + 8) == 0;
}

typedef struct { const uint8_t *p; size_t len; } Chunk;

/* ProTracker period -> note (0 = C-0 of the IT note range; period 856, the lowest ProTracker note, is 48: measured against libopenmpt) */
static int period_note(unsigned period) {
    if (!period || period == 0xFFF) return -1;
    int n = 48 + (int)lround(12.0 * log2(856.0 / (double)period));
    return n < 0 ? 0 : n > 119 ? 119 : n;
}

/* MOD effect -> IT effect (cmd 1 = A ... 26 = Z) or the volume column; the slide nibble rules follow ProTracker */
static void convert_effect(unsigned cmd, unsigned par, ITCell *c) {
    unsigned ic = 0, ip = par;
    switch (cmd) {
    case 0x0: if (par) ic = 10; break;                                  /* arpeggio J */
    case 0x1: if (par) ic = 6; break;                                   /* porta up F */
    case 0x2: if (par) ic = 5; break;                                   /* porta down E */
    case 0x3: ic = 7; break;                                            /* tone portamento G */
    case 0x4: ic = 8; break;                                            /* vibrato H */
    case 0x5: ic = 12; if (par & 0xF0) ip &= 0xF0; else if (!par) ic = 0; break;   /* tone porta + volume slide L */
    case 0x6: ic = 11; if (par & 0xF0) ip &= 0xF0; else if (!par) ic = 0; break;   /* vibrato + volume slide K */
    case 0x7: ic = 18; break;                                           /* tremolo R */
    case 0x8: ic = 24; break;                                           /* panning X */
    case 0x9: ic = 15; break;                                           /* sample offset O */
    case 0xA: ic = 4; if (par & 0xF0) ip &= 0xF0; else if (!par) ic = 0; break;    /* volume slide D */
    case 0xB: ic = 2; break;                                            /* position jump B */
    case 0xC: c->vol = (uint8_t)(par > 64 ? 64 : par); c->has |= IT_HAS_VOL; return;   /* set volume */
    case 0xD: ic = 3; ip = (par >> 4) * 10 + (par & 15); break;        /* pattern break C (decimal) */
    case 0xE: {
        unsigned x = par & 15;
        switch (par >> 4) {
        case 0x1: ic = 6; ip = 0xF0 | x; break;                         /* fine porta up */
        case 0x2: ic = 5; ip = 0xF0 | x; break;                         /* fine porta down */
        case 0x3: ic = 19; ip = 0x10 | x; break;                        /* glissando control */
        case 0x4: ic = 19; ip = 0x30 | x; break;                        /* vibrato waveform */
        case 0x5: ic = 19; ip = 0x20 | x; break;                        /* finetune */
        case 0x6: ic = 19; ip = 0xB0 | x; break;                        /* pattern loop */
        case 0x7: ic = 19; ip = 0x40 | x; break;                        /* tremolo waveform */
        case 0x8: ic = 19; ip = 0x80 | x; break;                        /* coarse panning */
        case 0x9: ic = 17; ip = x; break;                               /* retrigger */
        case 0xA: ic = 4; ip = (x << 4) | 0x0F; break;                  /* fine volume slide up */
        case 0xB: ic = 4; ip = 0xF0 | x; break;                         /* fine volume slide down */
        case 0xC: ic = 19; ip = 0xC0 | x; break;                        /* note cut */
        case 0xD: ic = 19; ip = 0xD0 | x; break;                        /* note delay */
        case 0xE: ic = 19; ip = 0xE0 | x; break;                        /* pattern delay */
        default: break;
        }
        break; }
    case 0xF: if (par) { if (par < 0x20) ic = 1; else ic = 20; } break; /* speed A / tempo T */
    default: break;
    }
    if (ic) { c->cmd = (uint8_t)ic; c->par = (uint8_t)ip; c->has |= IT_HAS_CMD; }
}

ITSong *dtm_load(const uint8_t *b, size_t n, int *err) {
    int e = 0; if (!err) err = &e; *err = 0;
    if (!dtm_probe(b, n)) { *err = -1; return NULL; }
    unsigned hs = be32(b + 4), stereo_mode = b[10], speed = be16(b + 14), tempo = be16(b + 16), forced = be32(b + 18);

    Chunk patt = {0}, sq = {0}, inst = {0};
    size_t nd = 0, ndait = 0, capd = 0, capi = 0; Chunk *dapt = NULL, *dait = NULL;
    for (size_t o = 8 + (size_t)hs; o + 8 <= n;) {
        size_t len = be32(b + o + 4), avail = n - (o + 8); if (len > avail) len = avail;
        Chunk c = { b + o + 8, len };
        if (!memcmp(b + o, "PATT", 4) && !patt.p) patt = c;
        else if (!memcmp(b + o, "S.Q.", 4) && !sq.p) sq = c;
        else if (!memcmp(b + o, "INST", 4) && !inst.p) inst = c;
        else if (!memcmp(b + o, "DAPT", 4)) { if (nd == capd) { capd = capd ? capd * 2 : 64; dapt = realloc(dapt, capd * sizeof *dapt); } dapt[nd++] = c; }
        else if (!memcmp(b + o, "DAIT", 4)) { if (ndait == capi) { capi = capi ? capi * 2 : 64; dait = realloc(dait, capi * sizeof *dait); } dait[ndait++] = c; }
        o += 8 + len;
    }
    ITSong *s = NULL;
#define FAIL(code) do { *err = (code); free(dapt); free(dait); if (s) it_free(s); return NULL; } while (0)
    if (!patt.p || patt.len < 8 || !sq.p || sq.len < 8 || !inst.p || inst.len < 2) FAIL(-2);
    unsigned nch = be16(patt.p), pfmt = be32(patt.p + 4);
    if (nch < 1 || nch > 32) FAIL(-3);
    if (pfmt != 0 && pfmt != 0x322E3034u) FAIL(-4);                     /* 0 = ProTracker cells, "2.04"; "2.06" is not supported */

    s = calloc(1, sizeof *s); if (!s) FAIL(-5);
    { size_t tl = hs - 14; if (tl > 26) tl = 26; if (22 + tl > n) tl = 0; memcpy(s->title, b + 22, tl); s->title[tl] = 0; }
    s->flags = 1 | 16 | 32;                                             /* stereo, old effects, Gxx link; sample mode, Amiga periods */
    s->dtm = 1; s->schism_days = -1; s->cwt = s->cmwt = 0x214; s->gv = 128; s->mv = 48; s->sep = 128;
    s->speed = speed ? (int)speed : 6; s->tempo = tempo ? (int)tempo : 125;
    s->nch = (int)nch;
    for (int i = 0; i < 64; i++) {
        s->chvol[i] = 64;
        s->chpan[i] = stereo_mode == 0 ? ((i & 3) == 0 || (i & 3) == 3 ? 16 : 48) : 32;   /* old stereo: L R R L at 25 % / 75 % (measured against libopenmpt), panoramic: centre */
    }
    memset(s->orders, 255, sizeof s->orders);
    { unsigned ol = be16(sq.p); if (ol > 256) ol = 256; if (ol > sq.len - 8) ol = (unsigned)(sq.len - 8); memcpy(s->orders, sq.p + 8, ol); s->nord = (int)ol; }

    /* samples */
    unsigned ns = be16(inst.p); int newfmt = ns >= 0x8000; ns &= 0x7FFF;
    size_t q = 2, per = 50 + (newfmt ? 2 : 0);
    if (q + (size_t)ns * per > inst.len) FAIL(-6);
    s->smp = calloc(512, sizeof(ITSmp)); if (!s->smp) FAIL(-5);
    s->inst = calloc(1, sizeof(ITInst));
    typedef struct { uint32_t bytes; int st, bits16; } Meta; Meta *meta = calloc(512, sizeof *meta);
    for (unsigned k = 1; k <= ns; k++) {
        unsigned idx = newfmt ? be16(inst.p + q) + 1u : k; const uint8_t *h = inst.p + q + (newfmt ? 2 : 0); q += per;
        if (idx < 1 || idx >= 256) continue;
        ITSmp *sm = &s->smp[idx]; Meta *m = &meta[idx];
        uint32_t bytes = be32(h + 4), ls = be32(h + 10), ll = be32(h + 14);
        int st = h[40] & 1, b16 = h[41] > 8; unsigned fine = h[8], vol = h[9], rate = be32(h + 46);
        if (idx > (unsigned)s->nsmp) s->nsmp = (int)idx;
        { size_t nl = 22; memcpy(sm->name, h + 18, nl); sm->name[nl] = 0; }
        unsigned div = (unsigned)((st ? 2 : 1) * (b16 ? 2 : 1));
        m->bytes = bytes; m->st = st; m->bits16 = b16;
        sm->len = bytes / div; sm->lb = ls / div; sm->le = (ls + ll) / div;
        sm->is16 = b16; sm->flags = b16 ? 2 : 0;
        sm->loop = sm->le > sm->lb + 1; if (!sm->loop) sm->lb = sm->le = 0; else if (sm->le > sm->len) sm->le = sm->len;
        sm->flags |= sm->loop ? 16 : 0;
        double c5 = (pfmt == 0 && forced > 0) ? (double)forced : (double)rate;
        sm->ptft = fine & 15 ? (int)(fine & 15) + 1 : 0;                  /* the ProTracker finetune is applied per note by the engine */
        sm->c5spd = (uint32_t)lround(c5);
        sm->vol = (int)(vol > 64 ? 64 : vol); sm->gvl = 64; sm->dfp = 0; sm->cvt = 1;
    }
    for (size_t i = 0; i < ndait; i++) {
        const Chunk *c = &dait[i]; if (c->len < 2) continue;
        unsigned idx = be16(c->p) + 1u; if (idx < 1 || idx > (unsigned)s->nsmp) continue;
        ITSmp *sm = &s->smp[idx]; Meta *m = &meta[idx];
        size_t avail = c->len - 2; const uint8_t *d = c->p + 2;
        size_t frames = sm->len, bpf = (size_t)((m->st ? 2 : 1) * (m->bits16 ? 2 : 1));
        if (frames * bpf > avail) frames = avail / bpf;
        size_t sz = (size_t)sm->len * (m->bits16 ? 2 : 1) + 8;
        sm->data = calloc(1, sz); if (m->st) sm->data2 = calloc(1, sz);
        if (!sm->data || (m->st && !sm->data2)) { free(meta); FAIL(-5); }
        for (size_t f = 0; f < frames; f++) {
            for (int ch = 0; ch < (m->st ? 2 : 1); ch++) {
                void *dst = ch ? sm->data2 : sm->data;
                const uint8_t *src = d + (f * (m->st ? 2 : 1) + ch) * (m->bits16 ? 2 : 1);
                if (m->bits16) ((int16_t *)dst)[f] = (int16_t)(src[0] << 8 | src[1]);
                else ((int8_t *)dst)[f] = (int8_t)src[0];
            }
        }
    }
    for (int i = 1; i <= s->nsmp; i++) if (!s->smp[i].data) s->smp[i].len = 0;   /* header without data */
    free(meta);

    /* patterns */
    int maxpat = -1;
    for (size_t i = 0; i < nd; i++) { if (dapt[i].len < 8) continue; int pn = (int)be16(dapt[i].p + 4); if (pn > maxpat && pn < 256) maxpat = pn; }
    s->npat = maxpat + 1;
    s->pat = calloc((size_t)s->npat + 1, sizeof(ITCell *)); s->rows = calloc((size_t)s->npat + 1, sizeof(int));
    if (!s->pat || !s->rows) FAIL(-5);
    for (int i = 0; i < s->npat; i++) s->rows[i] = 64;
    for (size_t i = 0; i < nd; i++) {
        const Chunk *c = &dapt[i]; if (c->len < 8) continue;
        unsigned pn = be16(c->p + 4), rows = be16(c->p + 6);
        if (pn >= 256 || rows < 1 || rows > 1024) continue;
        ITCell *cells = calloc((size_t)rows * 64, sizeof(ITCell)); if (!cells) FAIL(-5);
        free(s->pat[pn]); s->pat[pn] = cells; s->rows[pn] = (int)rows;
        const uint8_t *d = c->p + 8; size_t avail = c->len - 8;
        for (unsigned r = 0; r < rows; r++) for (unsigned ch = 0; ch < nch; ch++) {
            size_t k = ((size_t)r * nch + ch) * 4; if (k + 4 > avail) goto next_pattern;
            ITCell *x = &cells[r * 64 + ch]; const uint8_t *v = d + k; unsigned cmd, par = v[3], inst_no;
            if (pfmt == 0) {                                            /* ProTracker cell with a 6 bit instrument number */
                int nt = period_note(((unsigned)(v[0] & 0x0F) << 8) | v[1]);
                if (nt >= 0) { x->note = (uint8_t)nt; x->has |= IT_HAS_NOTE; }
                inst_no = (unsigned)(v[2] >> 4) | (v[0] & 0x30u); cmd = v[2] & 0x0F;
            } else {                                                    /* "2.04": note, 6 bit volume + 2 instrument bits, instrument + effect, parameter */
                if (v[0] > 0 && v[0] < 0x80) { int nt = (v[0] >> 4) * 12 + (v[0] & 15) + 11; x->note = (uint8_t)(nt > 119 ? 119 : nt); x->has |= IT_HAS_NOTE; }
                unsigned vol = v[1] >> 2; if (vol) { x->vol = (uint8_t)(vol - 1); x->has |= IT_HAS_VOL; }
                inst_no = ((unsigned)(v[1] & 3) << 4) | (v[2] >> 4); cmd = v[2] & 0x0F;
            }
            if (inst_no) { x->inst = (uint8_t)inst_no; x->has |= IT_HAS_INST; }
            convert_effect(cmd, par, x);
        }
    next_pattern:;
    }
    free(dapt); free(dait);
    return s;
#undef FAIL
}
