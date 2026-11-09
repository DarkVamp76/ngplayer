/* TFMX-Replayer: Tick-Player mit Paula-Modell. GPL-3.0-or-later
 * Befehlssemantik nach ScummVM audio/mods/tfmx.cpp (GPL-3.0-or-later); eigene Umsetzung. */
#include "tfmx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern unsigned long long xplay_mute;   /* Bit n = Kanal n stumm (alle Formate) */

#define PAULA_CLK 3546895.0
#define CIA_CLK   709379.0

static const uint16_t NOTE[64] = {
    1710,1614,1524,1438,1357,1281,1209,1141,1077,1017, 960, 908,
     856, 810, 764, 720, 680, 642, 606, 571, 539, 509, 480, 454,
     428, 404, 381, 360, 340, 320, 303, 286, 270, 254, 240, 227,
     214, 202, 191, 180, 170, 160, 151, 143, 135, 127, 120, 113,
     214, 202, 191, 180, 170, 160, 151, 143, 135, 127, 120, 113,
     214, 202, 191, 180 };

typedef struct {
    int note, prev, rel, fine;               /* Note, vorige Note, relative Lautstaerke, Feinstimmung */
    int mo, step, wait, run, keyup, sfx, defer, dmaint, ret_mo, ret_step;
    uint8_t loop;
    int vol;                                 /* Kanal-Lautstaerke 0..64 */
    int per, ref;                            /* Paula-Periode / Referenzperiode */
    int env_d, env_s, env_c, env_e;          /* Huellkurve */
    int vl, vc, vd, vv;                      /* Vibrato */
    int pd, pc, pskip, pv;                   /* Portamento */
    int abl, abc, abd;                       /* AddBegin */
    int rstart, rlen;                        /* Paula-Register (Start in Bytes, Laenge in Worten) */
    int cs, cl; double pos; int dma;         /* laufende Wiedergabe: Start, Laenge (Bytes), Position */
} Chan;

typedef struct { int cmd, off, expose, step, wait; uint8_t loop; } Pat;

struct TfmxPlayer {
    const TfmxSong *s; int rate;
    Chan c[4]; Pat p[8];
    int tpos, tloop, start, stop;            /* Trackstep-Position/-Schleife */
    int pskip, pcount;
    double tick_hz, spt, acc;
    int done, started;
    float *tl, *tr, *tsc; int tlen, toff, tcap;
    float *scb; int scl; unsigned scn;
};

static inline unsigned B(const TfmxSong *s, size_t o) { return o < s->nmdat ? s->mdat[o] : 0; }
static inline uint32_t R32(const TfmxSong *s, size_t o) { return B(s,o) << 24 | B(s,o+1) << 16 | B(s,o+2) << 8 | B(s,o+3); }
static inline int S8(int v) { return v > 127 ? v - 256 : v; }
static inline int S16(int v) { return v > 32767 ? v - 65536 : v; }
static inline size_t MACRO(const TfmxSong *s, int i) { return R32(s, s->mt + 4u * (i & 127)); }
static inline size_t PATT(const TfmxSong *s, int i) { return R32(s, s->pt + 4u * (i & 127)); }

static void clear_fx(Chan *c) { c->abl = 0; c->env_s = 0; c->vl = 0; c->pd = 0; }

static void set_note(Chan *c, int n, int ft) {
    c->ref = (int)(((uint32_t)NOTE[n & 0x3F] * (uint16_t)(ft + c->fine + 256)) >> 8) & 0xFFFF;
    if (!c->pd) c->per = c->ref;
}

static void note_cmd(TfmxPlayer *p, int n, int p1, int p2, int p3) {
    Chan *c = &p->c[p2 & 3];
    if (n < 0xC0) {
        c->prev = c->note; c->note = n; c->mo = p1 & 127; c->rel = p2 >> 4; c->fine = S8(p3);
        c->step = 0; c->wait = 0; c->run = 1; c->sfx = 0; c->loop = 0xFF; c->dmaint = 0; c->defer = 0;
        c->ret_mo = c->ret_step = 0; c->keyup = 0;
    } else if (n < 0xF0) {
        c->pskip = p1; c->pc = 1;
        if (!c->pd) c->pv = c->ref;
        c->pd = p3; c->note = n & 0x3F; c->ref = NOTE[c->note];
    } else if (n == 0xF5) c->keyup = 1;
    else if (n == 0xF6) { c->vl = p1 & 0xFE; c->vc = p1 / 2; c->vd = p3; c->vv = 0; }
    else if (n == 0xF7) { c->env_d = p1; c->env_c = c->env_s = (p2 >> 4) + 1; c->env_e = S8(p3); }
}

static void macro_run(TfmxPlayer *p, Chan *c) {
    const TfmxSong *s = p->s;
    int defer = c->defer;
    for (;;) {
        size_t o = MACRO(s, c->mo) + 4u * (unsigned)c->step;
        if (o + 4 > s->nmdat) { c->run = 0; return; }
        int op = (int)B(s,o), a = (int)B(s,o+1), b = (int)B(s,o+2), e = (int)B(s,o+3), w = b << 8 | e, brk = 0;
        c->step++;
        switch (op) {
        case 0: case 0x13:
            if (op == 0) clear_fx(c);
            c->dma = 0; c->defer = defer = (a != 0);
            if (defer) { c->per = 4; c->rlen = 1; }
            if (b || e) c->vol = (b ? 0 : c->rel * 3) + e;
            break;
        case 1:
            c->dmaint = 0; c->cs = c->rstart; c->cl = (c->rlen < 1 ? 1 : c->rlen) * 2; c->pos = 0.0; c->dma = 1; c->defer = defer = 0;
            break;
        case 2: c->abl = 0; c->rstart = a << 16 | w; break;
        case 3: c->rlen = w; break;
        case 4: c->wait = w; brk = 1; break;
        case 0x10: if (c->keyup) break; /* fall through */
        case 5:
            if (c->loop != 0) { if (c->loop == 0xFF) c->loop = (uint8_t)a; c->step = w; }
            c->loop--;
            break;
        case 6: c->mo = a & 127; c->step = w; c->loop = 0xFF; break;
        case 7: c->run = 0; c->step--; return;
        case 8: set_note(c, c->note + a, w); brk = 1; break;
        case 9: set_note(c, a, w); brk = 1; break;
        case 0xA: clear_fx(c); break;
        case 0xB:
            c->pskip = a; c->pc = 1;
            if (!c->pd) c->pv = c->ref;
            c->pd = w; break;
        case 0xC:
            c->vl = a; c->vc = a / 2; c->vd = e;
            if (!c->pd) { c->per = c->ref; c->vv = 0; }
            break;
        case 0xD: case 0xE:
            if (b == 0xFE) set_note(c, c->note + a, 0);
            c->vol = op == 0xD ? c->rel * 3 + e : e;
            break;
        case 0xF: c->env_d = a; c->env_c = c->env_s = b; c->env_e = S8(e); break;
        case 0x11: c->abl = c->abc = a; c->abd = S16(w); c->rstart += c->abd; break;
        case 0x12: c->rlen = (c->rlen + S16(w)) & 0xFFFF; break;
        case 0x14:
            if (c->keyup || c->loop == 0) { c->loop = 0xFF; break; }
            if (c->loop == 0xFF) c->loop = (uint8_t)e;
            c->loop--; c->step--; return;
        case 0x15: c->ret_mo = c->mo; c->ret_step = c->step; c->mo = a & 127; c->step = w; break;
        case 0x16: c->mo = c->ret_mo; c->step = c->ret_step; break;
        case 0x17: c->ref = w; if (!c->pd) c->per = c->ref; break;
        case 0x18: c->rstart += w & 0xFFFE; c->rlen = (c->rlen - w / 2) & 0xFFFF; break;
        case 0x19: c->abl = 0; c->rstart = 0; c->rlen = 1; break;
        case 0x1A: c->dmaint = w + 1; c->run = 0; brk = 1; break;
        case 0x1C: if (c->note > a) c->step = w; break;
        case 0x1D: if (c->vol > a) c->step = w; break;
        case 0x1F: set_note(c, c->prev + a, w); brk = 1; break;
        case 0x21: note_cmd(p, c->note, a, c->rel << 4 | b, e); break;
        default: brk = 1; break;
        }
        if (brk && !defer) return;
    }
}

static void effects(Chan *c) {
    if (c->abl) { c->rstart += c->abd; if (--c->abc == 0) { c->abc = c->abl; c->abd = -c->abd; } }
    if (c->vl) {
        c->vv += c->vd;
        if (--c->vc == 0) { c->vc = c->vl; c->vd = -c->vd; }
        if (!c->pd) c->per = (int)(((uint32_t)c->ref * (uint16_t)(2048 + c->vv)) >> 11) & 0xFFFF;
    }
    if (c->pd && --c->pc == 0) {
        c->pc = c->pskip;
        int reset = 1, per = c->ref, v = c->pv;
        if (per > v) { v = (int)(((uint32_t)v * (uint16_t)(256 + c->pd)) >> 8); reset = per <= v; }
        else if (per < v) { v = (int)(((uint32_t)v * (uint16_t)(256 - c->pd)) >> 8); reset = per >= v; }
        if (reset) { c->pd = 0; c->pv = per & 0x7FF; }
        else c->per = c->pv = v & 0x7FF;
    }
    if (c->env_s) {
        int cnt = c->env_c--;
        if (cnt == 0) {
            c->env_c = c->env_s;
            int v = c->vol, reset;
            if (c->env_e > v) { v += c->env_d; reset = c->env_e <= v; }
            else { v -= c->env_d; reset = v <= 0 || c->env_e >= v; }
            if (reset) { c->env_s = 0; v = c->env_e; }
            c->vol = v;
        }
    }
}

static int track_run(TfmxPlayer *p, int inc) {
    const TfmxSong *s = p->s;
    if (inc) {
        if (p->tpos == p->stop) { p->tpos = p->start; p->done = 1; }
        else p->tpos++;
    }
    for (int guard = 0; guard < 4096; guard++) {
        size_t o = s->ts + 16u * (unsigned)p->tpos;
        if (o + 16 > s->nmdat) return 0;
        unsigned t[8]; for (int i = 0; i < 8; i++) t[i] = B(s,o+2*i) << 8 | B(s,o+2*i+1);
        if (t[0] != 0xEFFE) {
            for (int i = 0; i < 8; i++) {
                int pn = t[i] >> 8, ex = S8(t[i] & 0xFF);
                Pat *pt = &p->p[i];
                if (pn < 0x80) { pt->cmd = pn; pt->off = (int)PATT(s, pn); pt->expose = ex; pt->step = 0; pt->wait = 0; pt->loop = 0xFF; }
                else { pt->cmd = pn; pt->expose = ex; }
            }
            return 1;
        }
        switch (t[1]) {
        case 0: p->done = 1; return 0;
        case 1:
            if (p->tloop != 0) {
                if (p->tloop < 0) p->tloop = (int)t[3];
                if ((int)t[2] <= p->tpos) p->done = 1;          /* Rueckwaertssprung = Songende (einmal gespielt) */
                p->tpos = (int)t[2]; continue;
            }
            p->tloop--; break;
        case 2: p->pskip = p->pcount = (int)t[2]; break;
        default: break;
        }
        if (p->tpos == p->stop) { p->done = 1; return 0; }
        p->tpos++;
    }
    return 0;
}

static int pattern_run(TfmxPlayer *p, Pat *pt) {
    const TfmxSong *s = p->s;
    for (int guard = 0; guard < 65536; guard++) {
        size_t o = (size_t)pt->off + 4u * (unsigned)pt->step;
        if (o + 4 > s->nmdat) { pt->cmd = 0xFF; return 0; }
        int b0 = (int)B(s,o), b1 = (int)B(s,o+1), b2 = (int)B(s,o+2), b3 = (int)B(s,o+3);
        pt->step++;
        if (b0 < 0xF0) {
            int dw = 0, n = (b0 + pt->expose) & 0xFF;
            if (b0 < 0xC0) { if (b0 >= 0x80) { pt->wait = b3; b3 = 0; dw = 1; } n &= 0x3F; }
            note_cmd(p, n, b1, b2, b3);
            if (dw) return 0;
        } else {
            int w = b2 << 8 | b3;
            switch (b0 & 0xF) {
            case 0: pt->cmd = 0xFF; pt->step--; return 1;
            case 1:
                if (pt->loop != 0) { if (pt->loop == 0xFF) pt->loop = (uint8_t)b1; pt->step = w; }
                pt->loop--; break;
            case 2: pt->off = (int)PATT(s, b1); pt->step = w; break;
            case 3: pt->wait = b1; return 0;
            case 4: case 14: pt->cmd = 0xFF; pt->step--; return 0;
            case 5: p->c[b2 & 3].keyup = 1; break;
            case 6: case 7: note_cmd(p, b0, b1, b2, b3); break;
            default: break;                  /* Subroutine, Fade, Lock, Cue: noch nicht umgesetzt */
            }
        }
    }
    return 0;
}

static void advance_patterns(TfmxPlayer *p) {
    for (int again = 1; again;) {
        again = 0;
        for (int i = 0; i < 8; i++) {
            Pat *pt = &p->p[i];
            if (pt->cmd < 0x90) {
                if (!pt->wait) {
                    if (pattern_run(p, pt)) { if (track_run(p, 1)) { again = 1; break; } else return; }
                } else pt->wait--;
            } else if (pt->cmd == 0xFE) {
                pt->cmd = 0xFF;
                Chan *c = &p->c[pt->expose & 3]; c->run = 0; c->dma = 0;
            }
        }
    }
}

static void gen_ch(TfmxPlayer *p, Chan *c, float *o, int n, int mute) {
    const TfmxSong *s = p->s;
    for (int i = 0; i < n; i++) o[i] = 0.0f;
    if (!c->dma || c->per < 1 || !s->nsmpl) return;
    double step = PAULA_CLK / c->per / p->rate;
    int vol = c->vol < 0 ? 0 : c->vol > 64 ? 64 : c->vol;
    int filled = 0;
    while (filled < n) {
        int need = (int)ceil((c->cl - c->pos) / step); if (need < 1) need = 1;
        int k = n - filled < need ? n - filled : need;
        for (int j = 0; j < k; j++) {
            long idx = c->cs + (long)(c->pos + j * step);
            if (idx < 0) idx = 0;
            if ((size_t)idx >= s->nsmpl) idx = (long)s->nsmpl - 1;
            o[filled + j] = mute ? 0.0f : (float)s->smpl[idx] / 128.0f * (float)vol / 64.0f;
        }
        c->pos += k * step; filled += k;
        if (c->pos >= c->cl) {
            c->pos -= c->cl; c->cs = c->rstart; c->cl = (c->rlen < 1 ? 1 : c->rlen) * 2;
            if (c->dmaint > 0 && --c->dmaint == 0) c->run = 1;
            if (c->pos >= c->cl) c->pos = 0.0;
        }
    }
}

static void do_tick(TfmxPlayer *p) {
    for (int i = 0; i < 4; i++) {
        Chan *c = &p->c[i];
        if (c->sfx > 0) effects(c);
        if (c->run) { if (!c->wait) macro_run(p, c); else c->wait--; }
        if (c->sfx >= 0) c->sfx = 1;
    }
    if (p->pcount == 0) { p->pcount = p->pskip; advance_patterns(p); }
    else p->pcount--;
    p->acc += p->spt;
    int n = (int)p->acc; p->acc -= n;
    if (n < 1) n = 1;
    if (n > p->tcap) n = p->tcap;
    for (int i = 0; i < 4; i++) gen_ch(p, &p->c[i], p->tsc + (size_t)i * p->tcap, n, (int)((xplay_mute >> i) & 1));
    for (int i = 0; i < n; i++) {
        p->tl[i] = (p->tsc[i] + p->tsc[3 * p->tcap + i]) * 0.5f;
        p->tr[i] = (p->tsc[p->tcap + i] + p->tsc[2 * p->tcap + i]) * 0.5f;
    }
    p->tlen = n; p->toff = 0;
}

TfmxPlayer *tfp_new(const TfmxSong *s, int sub, int rate) {
    if (!s || sub < 0 || sub >= 32 || rate < 8000) return NULL;
    TfmxPlayer *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->s = s; p->rate = rate;
    const TfmxSub *b = &s->sub[sub];
    p->start = b->start; p->stop = b->end; p->tpos = b->start; p->tloop = -1;
    if (b->tempo >= 0x10) { p->tick_hz = CIA_CLK / (double)(0x1B51F8 / b->tempo); p->pskip = 0; }
    else { p->tick_hz = CIA_CLK / (s->pal ? 11822.0 : 14320.0); p->pskip = b->tempo; }
    p->spt = rate / p->tick_hz;
    p->tcap = (int)p->spt + 4;
    p->tl = calloc((size_t)p->tcap, sizeof(float)); p->tr = calloc((size_t)p->tcap, sizeof(float));
    p->tsc = calloc((size_t)p->tcap * 4, sizeof(float));
    if (!p->tl || !p->tr || !p->tsc) { tfp_free(p); return NULL; }
    for (int i = 0; i < 4; i++) { p->c[i].sfx = -1; p->c[i].per = p->c[i].ref = 4; p->c[i].rlen = 1; p->c[i].cl = 2; p->c[i].loop = 0xFF; }
    for (int i = 0; i < 8; i++) p->p[i].cmd = 0xFF;
    p->pcount = 0;
    if (!track_run(p, 0)) p->done = 1;
    p->done = p->done && 0;                  /* ein Fehlschlag beim ersten Laden beendet nur, wenn keine Zeile ladbar war */
    return p;
}

void tfp_free(TfmxPlayer *p) { if (!p) return; free(p->tl); free(p->tr); free(p->tsc); free(p); }

int tfp_render(TfmxPlayer *p, int16_t *out, int nframes) {
    int done = 0;
    while (done < nframes) {
        if (p->toff >= p->tlen) {
            if (p->done) break;
            do_tick(p);
        }
        int k = p->tlen - p->toff; if (k > nframes - done) k = nframes - done;
        for (int i = 0; i < k; i++) {
            float l = p->tl[p->toff + i] * 32767.0f, r = p->tr[p->toff + i] * 32767.0f;
            out[2 * (done + i)] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            out[2 * (done + i) + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            if (p->scb) {
                unsigned w = p->scn & (unsigned)(p->scl - 1);
                for (int c = 0; c < 4; c++) p->scb[(size_t)c * p->scl + w] = p->tsc[(size_t)c * p->tcap + p->toff + i];
                p->scn++;
            }
        }
        p->toff += k; done += k;
    }
    return done;
}

void tfp_set_scope(TfmxPlayer *p, float *buf, int len) { p->scb = buf; p->scl = len; p->scn = 0; }
unsigned tfp_scope_pos(const TfmxPlayer *p) { return p->scn; }
int tfp_chan_pan(const TfmxPlayer *p, int ch) { (void)p; return (ch == 1 || ch == 2) ? 1 : 0; }
int tfp_pos(const TfmxPlayer *p) { return p->tpos; }
