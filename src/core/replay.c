#include <stdlib.h>
#include <stdio.h>
/* Replayer + einfacher Mixer, Verhalten nach dem Referenz-Player (siehe STATUS.md). GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include "period.h"
#define PK 256
#include <math.h>
/* S3M (fmt 2): gleichstufige Frequenz, gemessen am Original (60 Noten): Hz = floor(c2spd * 2^((n-48)/12)); Periode = 14318184/Hz in PK-Aufloesung */
/* Test: Periode ueber die ganzzahlige Hz-Frequenz zurueckfuehren (Rundungsrueckkopplung des Originals) */
static double hzq(double x){ static int m=-1; if(m<0){const char*e=getenv("XHZ"); m=e?atoi(e):0;} return m==1?floor(x+0.5):m==2?ceil(x):floor(x); }
static int qef(void){ static int m=-1; if(m<0){const char*e=getenv("XQ"); m=e?atoi(e):1;} return m; }
static int qmod(void){ static int q=-1; if(q<0) q=getenv("XNOQM")==NULL; return q; }
static int g_qc = 0;
static int qper(int per){ int qc = g_qc;
    if (per <= 0) return per; double hz = hzq(14318184.0 * PK / per); if (hz <= 0) return per;
    double v = 14318184.0 * PK / hz; return qc==1 ? (int)ceil(v) : qc==2 ? (int)(v+0.5) : (int)v; }
/* S3M: Rate = (c2spd * ST[halbton]) >> (16 - Oktave), ST = floor(2^(s/12) * 65536) -- gemessen an 58/60 Noten (ngp_calib_notes) */
static const uint32_t ST16[12] = {65536, 69432, 73561, 77935, 82570, 87480, 92681, 98193, 104031, 110217, 116771, 123715};
static int period_fine(const XSong *s, int note, uint32_t c2spd){
    if ((s->fmt == 2 || (s->fmt == 1 && getenv("XMODHZ") == NULL)) && c2spd && note >= 0) {
        int d = note / 12 - 4;
        uint64_t m = (uint64_t)c2spd * ST16[note % 12];
        uint64_t hz = (16 - d >= 0) ? (m >> (16 - d)) : (m << (d - 16));
        if (hz < 1) hz = 1;
        return (int)(14318184.0 * PK / (double)hz);
    }
    return (int)ngp_period_generic_k(note, c2spd, PK);
}   /* Periodenaufloesung: Original rechnet offenbar mit Nachkommaanteil (gemessen an ICEFRONT) */
#include <stdlib.h>
#include <string.h>

#define NCH 32

typedef struct {
    int inst, smp, lastnote, d0, c2spd;
    int p74, p80, pD4, v44, vD8, vE8, pan5c;
    int m78, m7c, m84, m90, mb0, mcc;
    int vpos, vspd, vdep, tpos, tspd, tdep;
    int cnt164, arp16c, dcf, e0f, trig, start38;
    int eff, par;                    /* aktuelle Zelle */
    int rt;                          /* Instrumentwechsel bei dieser Note */
    int swap;                        /* Sample live tauschen (Note+Inst+3xx) */
    uint32_t len, ls, le; int loop;  /* Kopie der Sampledaten */
    const int8_t *data;
    int loop_row, loop_cnt;
    int is16, m168, mA4, mA8;
    int sd_on, sd_tick; XCell sd_cell;   /* S3M SDx: verzoegerte Zeile */
} Ch;

typedef struct { int on; int is16; double pos, step; const int8_t *data; uint32_t len, ls, le; int loop; int vol, pan; double volf; int hold; } Voice;

int xplay_solo = -1;
unsigned long long xplay_mute = 0;   /* Bit n = Kanal n stumm (alle Formate) */
#define XFMT4(p) ((*(const XSong *const *)(p))->fmt == 4)
struct XPlayer {
    const XSong *s; int rate;
    Ch ch[NCH]; Voice v[NCH];
    int order, row, tick, speed, tempo;
    int d_ord, d_row;                /* Anzeigeposition: Zeile, die gerade gespielt wird */
    float *scb; int scl; unsigned scn;   /* Scope-Ringpuffer je Kanal (scb[ch*scl + pos]) */
    int delay_cnt, in_delay;
    int jump_ord, jump_row, have_jump, loop_jump;
    uint8_t vis[256][64];
    int gvol, fix_e8;
    int done; double acc;
    int lp; double lpa, lpl, lpr;    /* Paula-Tiefpass (A500: RC ca. 4,9 kHz): an/aus, Koeffizient, Zustand L/R */
};

static int clampi(int v, int lo, int hi){ return v < lo ? lo : v > hi ? hi : v; }

static void trigger_note(XPlayer *p, int chn, Ch *c, const XCell *x){
    const XSong *s = p->s; int note;
    if (x->note == 0x7f && c->inst) { p->v[chn].on = 0; c->vD8 = 0; c->vE8 = 0; c->trig = 0; return; }
    int inst_ok = x->inst && x->inst <= s->nsmp;
    c->rt = inst_ok;
    { static int xl=-1; if(xl<0) xl=getenv("XLIVE")!=NULL; c->swap = xl && s->fmt == 1 && inst_ok && x->note && (x->eff == 3 || x->eff == 5); }
    int keepold = getenv("XRT2") && s->fmt == 1 && (x->eff == 3 || x->eff == 5) && inst_ok && c->inst;
    if (inst_ok) { note = x->note ? x->note : c->lastnote; if (!keepold) c->inst = x->inst; }
    else note = x->note;
    if (!c->inst || !note || note >= 0x7f) return;
    { static int ni=-1; if(ni<0) ni=getenv("XNOINS")==NULL;
      if (ni && s->fmt == 1 && inst_ok && !x->note && c->smp) {
          const XSmp *m0 = &s->smp[c->inst];
          if (m0->vol <= s->maxvol) c->vD8 = c->v44 = m0->vol;
          c->vE8 = c->v44; return; } }
    const XSmp *m = &s->smp[c->inst];
    c->smp = c->inst;
    if (inst_ok) {
        { const XSmp *mv = keepold ? &s->smp[x->inst] : m; if (mv->vol <= s->maxvol) c->vD8 = c->v44 = mv->vol; }
        c->c2spd = m->c2spd;
        c->len = m->len; c->ls = m->ls; c->le = m->le; c->loop = m->loop; c->data = m->data; c->is16 = m->is16;
        c->cnt164 = 0; c->m168 = 0;
    }
    c->d0 = 0;                                    /* Periodenmaßstab des Originals: 4x Amiga */
    c->lastnote = note;
    c->vE8 = c->v44;
    int per = period_fine(p->s, note + c->d0, c->c2spd);
    { static int tn=-1; if(tn<0) tn=getenv("XNOTPN")==NULL;
      if (tn && s->fmt == 1 && (x->eff == 3 || x->eff == 5) && c->pD4 && p->v[chn].on) { c->p80 = per; c->p74 = c->pD4; }
      else c->p80 = c->pD4 = per; }
    c->trig = 1; c->start38 = 0;
    if (!(c->mb0 & 0x80)) c->tpos = 0;
    { static int xv=-1; if(xv<0){const char*e=getenv("XV"); xv=e?atoi(e):0;} static int nv=-1; if(nv<0) nv=getenv("XNOVR")==NULL;
      int keep = nv && s->fmt == 1 && (x->eff == 3 || x->eff == 5);
      if (!(c->mb0 & 0x08) && !((xv&1) && p->s->fmt==2) && !keep) c->vpos = 0; }
}

/* 0x38e83 */
static void volslide(XPlayer *p, Ch *c, int par, int tick){
    if (!par || !tick) return;
    int hi = par >> 4, lo = par & 15;
    if (hi == 0 || lo == 0) c->vD8 = c->v44 + hi - lo;
    c->vD8 = clampi(c->vD8, 0, p->s->maxvol);
}
/* 0x38a79 */
static void vib_step(Ch *c, int tick){
    int d = ngp_vibrato_delta((int8_t)c->vpos, c->mb0 & 3, c->vdep);
    { static int nv=-1; if(nv<0) nv=getenv("XNOVIB")!=NULL; if(nv) d=0; }
    c->p74 = c->pD4 + d * PK;
    if (tick) c->vpos = (int8_t)(c->vpos + c->vspd);
}
/* 0x38b9c */
static void trem_step(XPlayer *p, Ch *c, int tick){
    int idx = (c->tpos >> 2) & 0x1f, wf = (c->mb0 >> 4) & 3, v;
    if (wf == 0) v = ngp_vib_sine[idx];
    else if (wf == 1) { v = idx << 3; if (c->tpos < 0) v = 255 - v; }
    else v = 255;
    int d = (v * c->tdep) >> 6;
    if (c->tpos >= 0) c->v44 = clampi(c->vD8 + d, -999999, p->s->maxvol);
    else c->v44 = c->vD8 - d < 0 ? 0 : c->vD8 - d;
    if (tick) c->tpos += c->tspd;
}

static void e_sub(XPlayer *p, int chn, Ch *c, int par, int tick){
    int h = par >> 4, x = par & 15, mx = p->s->maxvol;
    switch (h) {
    case 1: if (!tick) c->pD4 -= (x << 2) * PK; break;
    case 2: if (!tick) c->pD4 += (x << 2) * PK; break;
    case 4: c->mb0 = (c->mb0 & 0xf0) | x; break;
    case 5: c->c2spd = ngp_finetune_c2spd[x];
            c->pD4 = period_fine(p->s, c->lastnote + c->d0, c->c2spd); break;
    case 7: c->mb0 = (c->mb0 & 0x0f) | (x << 4); break;
    case 8: c->pan5c = ngp_pan_tab[x]; break;   /* gemessen: S8x setzt echten Pan */
    case 9: if (x) { if (c->cnt164 == 0) { c->trig = 1; c->cnt164 = x; } c->cnt164--; } break;
    case 10: if (!tick) { if (!x) x = c->m90 >> 4; else c->m90 = (c->m90 & 15) + (x << 4);
                          c->vD8 = clampi(c->vD8 + x, -99999, mx); } break;
    case 11: if (!tick) { if (!x) x = c->m90 & 15; else c->m90 = (c->m90 & 0xf0) + x;
                          c->vD8 -= x; if (c->vD8 < 0) c->vD8 = 0; } break;
    case 12: if (tick >= x) p->v[chn].on = 0; break;
    case 13: if (p->s->fmt == 2) break;   /* S3M SDx: in do_tick behandelt (gemessen) */
             if (tick == x) { c->trig = 1; c->vE8 = c->vD8; } else c->trig = 0; break;
    case 14: if (!tick && !p->in_delay) p->delay_cnt = x + 1; break;
    default: break;
    }
}


/* S3M D (0x38F7D): Speicher +0x84, schnelle Slides per Flag */
static void dslide(XPlayer *p, Ch *c, int par, int tick){
    if (par) c->m84 = par; else par = c->m84;
    int lo = par & 15, hi = par >> 4, fast = (p->s->fast & 0x40) != 0;
    if (lo == 0) { if (tick || fast) c->vD8 += hi; }
    else if (hi == 0) { if (tick || fast) c->vD8 -= lo; }
    else if (lo == 15 && !tick) { c->vD8 += hi ? hi : 15; }
    else if (hi == 15 && !tick) { c->vD8 -= lo ? lo : 15; }
    c->vD8 = clampi(c->vD8, 0, p->s->maxvol);
}
/* S3M I (0x395C9) */
static void tremor(Ch *c, int par){
    int on = (par >> 4) + 1, off = (par & 15) + 1;
    c->m168 %= (on + off);
    c->v44 = (c->m168 < on) ? c->vD8 : 0;
    c->m168++;
}
/* S3M Q (0x3966E) */
static void retrig_q(XPlayer *p, Ch *c, int par, int tick){
    int hi = par >> 4, lo = par & 15;
    if (lo) { c->mA8 = hi; c->mA4 = lo; }
    if (hi) c->mA8 = hi;
    if (c->mA4 > 0) {
        if (c->cnt164 == 0) {
            c->trig = 1; c->cnt164 = c->mA4;
            if (tick) {
                switch (c->mA8) {
                case 1: case 2: case 3: case 4: case 5: c->vD8 -= 1 << (c->mA8 - 1); break;
                case 6: c->vD8 = (c->vD8 * 2) / 3; break;
                case 7: c->vD8 >>= 1; break;
                case 9: case 10: case 11: case 12: case 13: c->vD8 += 1 << (c->mA8 - 9); break;
                case 14: c->vD8 = (c->vD8 * 3) / 2; break;
                case 15: c->vD8 <<= 1; break;
                default: break;
                }
                c->vD8 = clampi(c->vD8, 0, 0x40);
            }
        }
        c->cnt164--;
    }
}

/* 669 (Composer / UNIS): slides run in Hz per tick, not in period steps. Period and frequency are linked by
 * Hz = 14318184 * PK / period (see v->step). */
static int hz_shift(int per, double dhz){
    if (per <= 0) return per;
    double hz = 14318184.0 * PK / per + dhz;
    if (hz < 50.0) hz = 50.0;
    if (hz > 64000.0) hz = 64000.0;
    return (int)(14318184.0 * PK / hz);
}

static void do_effect(XPlayer *p, int chn, Ch *c, int tick){
    int e = c->eff, par = c->par, mx = p->s->maxvol;
    c->dcf = c->e0f = 0;
    c->arp16c = c->lastnote;
    /* Phase 1 */
    switch (e) {
    case 0x0A: if (par) c->m84 = par; else par = c->m84;
               volslide(p, c, par, tick); c->vE8 = c->vD8; par = c->par; break;
    case 0x0C: { int v = par > mx ? mx : par; c->vD8 = c->vE8 = v; } break;
    case 0x0E: e_sub(p, chn, c, par, tick); break;
    case 0x0F: if (!tick && !p->in_delay && par) { if (par <= 0x20) p->speed = par; else p->tempo = par; } break;
    case 0x10: if (!tick && !p->in_delay && (par & 0x7f)) p->speed = par & 0x7f; break;
    case 0x11: case 0x17: case 0x18: dslide(p, c, par, tick); c->vE8 = c->vD8; break;
    case 0x16: if (!tick && !p->in_delay && par >= 0x20) p->tempo = par; break;
    case 0x1A: if (!tick) p->gvol = par > 255 ? 255 : par; break;
    case 0x24: break;   /* gemessen: Zxx hat im Original keine Wirkung */
    default: break;
    }
    /* Phase 2: nur mit Instrument und Sample */
    if (!c->inst || !c->smp) goto fin;
    switch (e) {
    case 0:
        if (par) {
            int k = tick % 3;
            if (k == 1) c->arp16c += par >> 4; else if (k == 2) c->arp16c += par & 15;
            c->p74 = period_fine(p->s, c->arp16c + c->d0, c->c2spd); c->dcf = 1;
        } break;
    case 1: if (par) c->m78 = par << 2; if (tick) { if (qmod() && p->s->fmt == 1) c->pD4 = qper(c->pD4); c->pD4 -= c->m78 * PK; } break;
    case 2: if (par) c->m78 = par << 2; if (tick) { if (qmod() && p->s->fmt == 1) c->pD4 = qper(c->pD4); c->pD4 += c->m78 * PK; } break;
    case 3: if (!(p->s->fmt == 1 && getenv("XRT") && c->rt && !p->v[chn].on)) c->trig = 0; if (par) c->m7c = par << 2;
            if (!tick && p->s->fmt == 1 && getenv("XTPR")) c->p74 = c->pD4;
            if (tick || (p->s->fmt == 1 && getenv("XTP0"))) { c->p74 = c->pD4 = ngp_tporta_normal((p->s->fmt == 2 || (qmod() && p->s->fmt == 1)) ? qper(c->p74) : c->p74, c->p80, c->m7c * PK); }
            c->dcf = 1; break;
    case 4: if (par & 15) c->vdep = par & 15; if (par & 0xf0) c->vspd = (par & 0xf0) >> 2;
            vib_step(c, tick); c->dcf = 1; break;
    case 5: if (!(p->s->fmt == 1 && getenv("XRT") && c->rt && !p->v[chn].on)) c->trig = 0;
            if (!tick && p->s->fmt == 1 && getenv("XTPR")) c->p74 = c->pD4;
            if (tick || (p->s->fmt == 1 && getenv("XTP0"))) { c->p74 = c->pD4 = ngp_tporta_normal((p->s->fmt == 2 || (qmod() && p->s->fmt == 1)) ? qper(c->p74) : c->p74, c->p80, c->m7c * PK); }
            volslide(p, c, par, tick); c->dcf = 1; c->vE8 = c->vD8; break;
    case 6: vib_step(c, tick); volslide(p, c, par, tick); c->dcf = 1; c->vE8 = c->vD8; break;
    case 7: if (par & 15) c->tdep = par & 15; if (par & 0xf0) c->tspd = (par & 0xf0) >> 2;
            trem_step(p, c, tick); c->e0f = 1; break;
    case 0x12: if (par) c->m78 = par; else par = c->m78;
               { int hi = par >> 4, lo = par & 15; if (qef() && p->s->fmt == 2 && (tick || hi >= 14)) c->pD4 = qper(c->pD4);
                 if (hi == 15) { if (!tick) c->pD4 += (lo << 2) * PK; }
                 else if (hi == 14) { if (!tick) c->pD4 += lo * PK; }
                 else if (tick) c->pD4 += (par << 2) * PK; } break;
    case 0x13: if (par) c->m78 = par; else par = c->m78;
               { int hi = par >> 4, lo = par & 15; if (qef() && p->s->fmt == 2 && (tick || hi >= 14)) c->pD4 = qper(c->pD4);
                 if (hi == 15) { if (!tick) c->pD4 -= (lo << 2) * PK; }
                 else if (hi == 14) { if (!tick) c->pD4 -= lo * PK; }
                 else if (tick) c->pD4 -= (par << 2) * PK; } break;
    case 0x14: tremor(c, par); c->e0f = 1; break;
    case 0x15: retrig_q(p, c, par, tick); break;
    case 0x17: vib_step(c, tick); c->dcf = 1; break;
    case 0x18: c->trig = 0;
               if (tick || (p->s->fmt == 1 && getenv("XTP0"))) { c->p74 = c->pD4 = ngp_tporta_normal((p->s->fmt == 2 || (qmod() && p->s->fmt == 1)) ? qper(c->p74) : c->p74, c->p80, c->m7c * PK); }
               c->dcf = 1; break;
    case 0x30: c->pD4 = hz_shift(c->pD4, par * 80.0); break;                          /* 669 slide up, every tick */
    case 0x31: c->pD4 = hz_shift(c->pD4, -par * 80.0); break;                         /* 669 slide down */
    case 0x32: c->trig = 0;                                                            /* 669 slide to the note on the line */
               { int cur = c->p74 > 0 ? c->p74 : c->pD4;
                 if (c->p80 > 0 && cur > 0) {
                     double K = 14318184.0 * PK, ch = K / cur, th = K / c->p80, st = par * 40.0;
                     if (ch < th) { ch += st; if (ch > th) ch = th; } else if (ch > th) { ch -= st; if (ch < th) ch = th; }
                     c->p74 = c->pD4 = (int)(K / ch);
                 } }
               c->dcf = 1; break;
    case 0x33: c->p74 = hz_shift(c->pD4, par * 80.0); c->dcf = 1; break;              /* 669 frequency adjust (constant offset) */
    case 0x34: c->p74 = (tick & 1) ? hz_shift(c->pD4, par * 24.0) : c->pD4; c->dcf = 1; break;   /* 669 vibrato: two-state wobble */
    case 9: if (par) c->mcc = par << 8;
            c->start38 = (int)((uint32_t)c->mcc > c->len ? c->len : (uint32_t)c->mcc); break;
    default: break;
    }
fin:
    if (!c->e0f) c->v44 = c->vD8;
    if (!c->dcf) c->p74 = c->pD4;
}

static int final_vol(int v44, int maxv, int gvol){
    int t = (v44 * 0x40) >> 3; t = (t * 0x10000) >> 16; t *= gvol; int u = t >> 7; u *= 128;
    return (u / maxv) >> 3;
}

static int xlive_on(void){ static int x=-1; if(x<0) x=getenv("XLIVE")!=NULL; return x; }
static void voice_update(XPlayer *p, int chn){
    Ch *c = &p->ch[chn]; Voice *v = &p->v[chn];
    if (c->trig && c->len) {
        c->trig = 0;
        v->on = 1; v->is16 = c->is16; v->data = c->data; v->len = c->len; v->ls = c->ls;
        v->le = c->le > c->len ? c->len : c->le; v->loop = c->loop;
        v->pos = c->start38;
        if (v->pos >= v->len) v->on = 0;
        v->hold = 0; c->swap = 0;
    } else if (c->swap) {
        c->swap = 0;
        v->is16 = c->is16; v->data = c->data; v->len = c->len; v->ls = c->ls;
        v->le = c->le > c->len ? c->len : c->le; v->loop = c->loop;
        if (v->hold && v->pos < v->len) { v->on = 1; v->hold = 0; }
    }
    { static int fv=-1; if(fv<0){const char*e=getenv("XFV"); fv=e?atoi(e):2;}
      double ex = (double)c->v44 * p->gvol / p->s->maxvol; if (ex > 255) ex = 255; if (ex < 0) ex = 0;
      int old = clampi(final_vol(c->v44, p->s->maxvol, p->gvol), 0, 255);
      if (fv == 1) { v->vol = (int)(ex + 0.5); v->volf = v->vol; }
      else if (fv == 2) { v->volf = ex; v->vol = (int)(ex + 0.5); }
      else if (fv == 3) { v->vol = (int)ex; v->volf = v->vol; }
      else { v->vol = old; v->volf = old; } }
    v->pan = clampi(c->pan5c, 0, 255);
    v->step = c->p74 > 0 ? hzq(14318184.0 * PK / c->p74) / p->rate : 0.0;
}

XPlayer *xplay_new(const XSong *s, int rate){
    if (s->fmt == 4) return (XPlayer *)xxm_new(s, rate);
    XPlayer *p = calloc(1, sizeof *p);
    { const char*e=getenv("XQC"); g_qc = e ? atoi(e) : (s->fmt == 1 ? 2 : 0); }
    p->s = s; p->rate = rate; p->speed = s->speed; p->tempo = s->tempo; p->gvol = s->gvol ? s->gvol : 255;
    for (int i = 0; i < NCH; i++) p->ch[i].pan5c = (s->fmt == 1) ? ((i % 4 == 0 || i % 4 == 3) ? 60 : 195) : (s->chpan[i] >= 0 ? s->chpan[i] : 128);
    p->tick = 0; p->acc = 0;
    p->lp = (s->fmt == 1 && s->amiga) ? 1 : 0;
    if (getenv("XLP") && !atoi(getenv("XLP"))) p->lp = 0;
    p->lpa = 1.0 - exp(-6.283185307179586 * 4900.0 / rate);
    return p;
}
void xplay_free(XPlayer *p){ if (XFMT4(p)) { xxm_free((XXPlayer *)p); return; } free(p); }
int xxm_pos(const void *p, int *ord, int *row);
void xxm_set_scope(void *p, float *buf, int len);
unsigned xxm_scope_pos(const void *p);
void xplay_set_scope(XPlayer *p, float *buf, int len){ if (XFMT4(p)) { xxm_set_scope(p, buf, len); return; } p->scb = buf; p->scl = len; p->scn = 0; }
int xxm_chan_pan(const void *p, int ch);
int xplay_chan_pan(const XPlayer *p, int ch){ if (XFMT4(p)) return xxm_chan_pan(p, ch); return p->v[ch].pan; }
unsigned xplay_scope_pos(const XPlayer *p){ if (XFMT4(p)) return xxm_scope_pos(p); return p->scn; }
int xplay_pos(const XPlayer *p, int *ord, int *row){ if (XFMT4(p)) return xxm_pos(p, ord, row); *ord = p->d_ord; *row = p->d_row; return 0; }
int xplay_order(const XPlayer *p){ if (XFMT4(p)) return xxm_order((const XXPlayer *)p); return p->order; }

static void skip_markers(XPlayer *p){
    const XSong *s = p->s;
    while (p->order < s->nord && s->orders[p->order] >= 0xFE) {
        if (s->orders[p->order] == 0xFF) { p->done = 1; return; }
        p->order++; p->row = 0;
    }
    if (p->order >= s->nord) p->done = 1;
}

static void do_tick(XPlayer *p){
    const XSong *s = p->s;
    if (p->tick == 0 && !p->in_delay) {
        skip_markers(p);
        if (p->done) return;
        int pat = s->orders[p->order];
        if (p->vis[p->order][p->row] && !p->loop_jump) { p->done = 1; return; }
        p->vis[p->order][p->row] = 1; p->loop_jump = 0;
        p->d_ord = p->order; p->d_row = p->row;
        for (int i = 0; i < s->nch; i++) {
            static const XCell empty = {0};
            const XCell *x = pat < s->npat ? &s->pat[((size_t)pat*64 + p->row)*s->nch + i] : &empty;
            Ch *c = &p->ch[i];
            c->eff = x->eff; c->par = x->par;
            c->sd_on = 0;
            if (s->fmt == 2 && x->eff == 0x0E && (x->par >> 4) == 13) {
                /* gemessen (ngp_calib_sd): Note, Instrument und Volumenspalte erst bei Tick x (1..speed-1); sonst verloren */
                c->sd_on = 1; c->sd_tick = x->par & 15; c->sd_cell = *x;
                continue;
            }
            trigger_note(p, i, c, x);
            if (x->vol1 && x->vol1 - 1 <= s->maxvol) c->vD8 = c->v44 = c->vE8 = x->vol1 - 1;   /* Volumenspalte, Tick 0 (0x3A9F1) */
        }
    }
    for (int i = 0; i < s->nch; i++) {
        Ch *c = &p->ch[i];
        if (c->sd_on && p->tick > 0 && p->tick == c->sd_tick && !p->in_delay) {
            c->sd_on = 0;
            trigger_note(p, i, c, &c->sd_cell);
            if (c->sd_cell.vol1 && c->sd_cell.vol1 - 1 <= s->maxvol) c->vD8 = c->v44 = c->vE8 = c->sd_cell.vol1 - 1;
        }
        do_effect(p, i, c, p->tick); voice_update(p, i);
    }
}

static void advance(XPlayer *p){
    const XSong *s = p->s; p->tick++;
    if (p->tick < p->speed) return;
    p->tick = 0;
    if (p->delay_cnt > 0) { p->delay_cnt--; if (p->delay_cnt > 0) { p->in_delay = 1; return; } }
    p->in_delay = 0;
    int pat = s->orders[p->order], jo = -1, jr = -1, loopto = -1;
    for (int i = 0; pat < s->npat && i < s->nch; i++) {
        const XCell *x = &s->pat[((size_t)pat*64 + p->row)*s->nch + i];
        Ch *c = &p->ch[i];
        if (x->eff == 0x0B) { jo = x->par; if (jr < 0) jr = 0; }
        else if (x->eff == 0x0D) { jr = (x->par >> 4) * 10 + (x->par & 15); if (jo < 0) jo = p->order + 1; }
        else if (x->eff == 0x0E && (x->par >> 4) == 6) {
            int n = x->par & 15;
            if (!n) c->loop_row = p->row;
            else { if (c->loop_cnt == 0) c->loop_cnt = n; else c->loop_cnt--;
                   if (c->loop_cnt > 0) loopto = c->loop_row; }
        }
    }
    if (loopto >= 0 && jo < 0) { p->row = loopto; p->loop_jump = 1; return; }
    if (jo >= 0) { p->order = jo; p->row = jr < 0 ? 0 : jr; }
    else if (++p->row >= 64) { p->row = 0; p->order++; }
    if (p->row >= 64) p->row = 0;
    if (p->order >= s->nord) { p->done = 1; }
    else skip_markers(p);
}

static double xpos_t = 0;
int xplay_render(XPlayer *p, int16_t *out, int n){
    if (XFMT4(p)) return xxm_render((XXPlayer *)p, out, n);
    int done_frames = 0;
    const XSong *s = p->s;
    double master = 96.0 / 256.0;      /* gemessen am Original (Kalibrier-MOD, 4 Kanaele): fester Gain je Kanal */
    while (done_frames < n) {
        if (p->done) break;
        if (p->acc <= 0) {
            { static int xp=-1; if(xp<0) xp=getenv("XPOS")!=NULL; if(xp && p->tick==0) fprintf(stderr,"POS %.2f %d %d sp%d tp%d\n", xpos_t, p->order, p->row, p->speed, p->tempo); }
            do_tick(p);
            if (p->done) break;
            { static int tk=-1; if(tk<0){const char*e=getenv("XTK"); tk=e?atoi(e):1;}
              double t = p->rate * 2.5 / p->tempo;
              p->acc += tk==1 ? (double)(long)t : tk==2 ? (double)(long)(t+0.5) : t; }
            advance(p);
        }
        int chunk = (int)p->acc; if (chunk < 1) chunk = 1;
        if (chunk > n - done_frames) chunk = n - done_frames;
        xpos_t += (double)chunk / p->rate;
        for (int f = 0; f < chunk; f++) {
            double l = 0, r = 0, cs[NCH];
            for (int i = 0; i < s->nch; i++) cs[i] = 0;
            for (int i = 0; i < s->nch; i++) {
                Voice *v = &p->v[i];
                if ((xplay_solo >= 0 && i != xplay_solo) || ((xplay_mute >> i) & 1)) { if (v->on) v->pos += v->step; if (v->loop && v->on) { while (v->pos >= v->le) v->pos -= (v->le - v->ls); } else if (v->on && v->pos >= v->len) { v->on = 0; if (xlive_on()) { v->hold = 1; v->pos = v->len; } } continue; }
                if (!v->on || !v->vol) { if (v->on) v->pos += v->step; continue; }
                if (v->pos < 0 || v->pos >= v->len) {   /* a sample swap can leave pos behind a shorter sample: wrap into the loop or stop */
                    if (v->pos >= 0 && v->loop && v->le > v->ls) { while (v->pos >= v->le) v->pos -= (v->le - v->ls); }
                    else { v->on = 0; continue; }
                }
                uint32_t i0 = (uint32_t)v->pos;
                double fr = v->pos - i0;
                uint32_t i1 = i0 + 1;
                if (v->loop && i1 >= v->le) i1 = v->ls + (i1 - v->le);
                double s0, s1;
                if (v->is16) { const int16_t *d16 = (const int16_t *)v->data; s0 = d16[i0] / 32768.0; s1 = (i1 < v->len) ? d16[i1] / 32768.0 : s0; }
                else { s0 = v->data[i0] / 128.0; s1 = (i1 < v->len) ? v->data[i1] / 128.0 : s0; }
                double sm = (s0 + (s1 - s0) * fr) * (v->volf / 255.0);
                cs[i] = sm;
                { double pr = (1.0 + 30.0 * v->pan / 255.0) / 32.0;   /* S3M gemessen: R = (2n+1)/32, n=pan>>4; MOD vorlaeufig */
                  pr = pr < 0 ? 0 : pr > 1 ? 1 : pr;
                  l += sm * (1.0 - pr); r += sm * pr; }
                v->pos += v->step;
                if (v->loop) { while (v->pos >= v->le) v->pos -= (v->le - v->ls); }
                else if (v->pos >= v->len) { v->on = 0; if (xlive_on()) { v->hold = 1; v->pos = v->len; } }
            }
            if (p->scb) { unsigned w = p->scn & (unsigned)(p->scl - 1); for (int i = 0; i < s->nch; i++) p->scb[(size_t)i * p->scl + w] = (float)(cs[i] * master * 2.0); p->scn++; }
            l *= master * 32767; r *= master * 32767;
            if (p->lp) { p->lpl += p->lpa * (l - p->lpl); p->lpr += p->lpa * (r - p->lpr); l = p->lpl; r = p->lpr; }
            out[(done_frames + f)*2]     = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            out[(done_frames + f)*2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        done_frames += chunk; p->acc -= chunk;
    }
    return done_frames;
}

/* Diagnose: Kanalzustand pro Tick (Format wie ocpref/ocp_trace) */
#include <stdio.h>
int xplay_trace(XPlayer *p, int nticks, FILE *o){
    const XSong *s = p->s;
    for (int t = 0; t < nticks && !p->done; t++) {
        int tr[NCH];
        for (int i = 0; i < s->nch; i++) p->ch[i].trig = 0;
        do_tick(p);
        if (p->done) break;
        fprintf(o, "T%d", t); if (getenv("XROW")) fprintf(stderr, "T%d ord%d row%d spd%d\n", t, p->order, p->row, p->speed);
        for (int i = 0; i < s->nch; i++) {
            Ch *c = &p->ch[i]; tr[i] = c->trig;
            voice_update(p, i);
            Voice *v = &p->v[i];
            fprintf(o, " | %d s%d i%d f%d v%d p%d", tr[i], v->on, c->smp, (int)(v->step * p->rate), v->vol, v->pan);
        }
        fprintf(o, "\n");
        advance(p);
    }
    return 0;
}

void xplay_set_fixe8(XPlayer *p, int on){ p->fix_e8 = on; }
void xplay_set_lowpass(XPlayer *p, int on){ if (!XFMT4(p)) p->lp = on ? 1 : 0; }
int  xplay_get_lowpass(const XPlayer *p){ return XFMT4(p) ? -1 : p->lp; }
