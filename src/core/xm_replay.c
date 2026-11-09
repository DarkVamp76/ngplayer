/* XM-Replayer (FT2-Semantik, Prototyp; wird gegen Referenz-Renders kalibriert). GPL-3.0-or-later */
#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define MAXCH 32
static const uint8_t vib_tab[32] = {0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,253,250,244,235,224,212,197,180,161,141,120,97,74,49,24};

typedef struct XXPlayer XXPlayer;
typedef struct {
    int inst, smpi;            /* aktuelles Instrument (1..), Sample-Index in xsmp, -1 = keins */
    int note;                  /* letzte Note 1..96 (ohne Relnote) */
    double per, tper;          /* Periode (linear: 7680-..., amiga: FT2-Einheiten), kontinuierlich */
    int vol, pan, ft;
    int keyon, fade, kopend;
    int venvt, penvt;
    int avpos, avcnt;
    int m_porta, m_tp, m_vs, m_vd, m_vpos, m_ts, m_td, m_tpos, m_vsl, m_psl, m_fvs, m_off, m_arp, m_ret, m_rv, m_gvs, m_tr, m_fine1;
    int vwave, twave;
    int cutdelay;
    /* Ausgabe */
    int on, dir; double pos; double step; int ovol, opan; /* ovol 0..64*... */
    double fvol;
    int trig, trigoff;
    int dnote, dinst, dvol;    /* verzoegerte Note (EDx) */
    int tr_on, tr_cnt;
    /* Crossfade der alten Stimme beim Retrigger */
    int xf; int osmpi, odir; double opos, ostep, ofvol; int oopan;
    double pprev; int pinst;   /* Pan-Rampe wie Lautstaerke */
    double vprev; int vinst;   /* Lautstaerke-Rampe ueber den Tick: Start (vorheriger Tick) / sofort */
    int m_eau, m_ead;          /* EAx/EBx Gedaechtnis */
} XCh;

struct XXPlayer {
    const XSong *s;  /* muss erstes Feld sein (Dispatch in replay.c) */ int rate;
    int speed, tempo, gvol;
    int order, row, tick, nrows, done;
    int d_ord, d_row;          /* Anzeigeposition */
    float *scb; int scl; unsigned scn;   /* Scope-Ringpuffer */
    int loop_row[MAXCH], loop_cnt[MAXCH];
    int jump_ord, jump_row, pdelay;
    double acc;
    int first, eloop; double tsec;
    int tkpos, tklen;          /* Position im aktuellen Tick / Ticklaenge in Samples */
    uint8_t seen[256 * 256 / 8];
    XCh ch[MAXCH];
};

static int xf_len(void){ static int v = -1; if (v < 0) { const char *e = getenv("XMXF"); v = e ? atoi(e) : 40; } return v; }
static void start_xf(XCh *c){ if (c->on && c->smpi >= 0 && xf_len() > 0) { c->xf = xf_len(); c->osmpi = c->smpi; c->odir = c->dir; c->opos = c->pos; c->ostep = c->step; c->ofvol = c->fvol; c->oopan = c->opan; } }
static int rd_inst_smp(const XSong *s, int inst, int note, int *smpi){
    if (inst < 1 || inst > s->ninst) { *smpi = -1; return 0; }
    const XInst *I = &s->inst[inst];
    if (!I->nsmp || note < 1 || note > 96) { *smpi = -1; return 0; }
    int k = I->keymap[note - 1]; if (k >= I->nsmp) k = 0;
    *smpi = I->smp[k]; return 1;
}
static double note_period(const XSong *s, int note, int relnote, int ft){
    int n = note + relnote;           /* 1-basiert */
    if (s->linear) return 7680.0 - (n - 1) * 64 - ft / 2.0;
    return 27392.0 * pow(2.0, -((n - 1) + ft / 128.0) / 12.0);
}
static double period_hz(const XSong *s, double per){
    if (per < 1) per = 1;
    if (s->linear) return 8363.0 * pow(2.0, (4608.0 - per) / 768.0);
    return 14317456.0 / per;
}
static int clampi(int v, int lo, int hi){ return v < lo ? lo : v > hi ? hi : v; }

static int env_val(const XEnv *e, int pos){
    int n = e->n; if (n <= 0) return 64;
    if (pos >= e->x[n - 1]) return e->y[n - 1];
    for (int i = 0; i < n - 1; i++) if (pos >= e->x[i] && pos < e->x[i + 1]) {
        int dx = e->x[i + 1] - e->x[i]; if (dx <= 0) return e->y[i];
        return e->y[i] + ((int)e->y[i + 1] - (int)e->y[i]) * (pos - e->x[i]) / dx;
    }
    return e->y[0];
}
static void env_adv(const XEnv *e, int *pos, int keyon){
    if (!e->on || e->n <= 0) return;
    if (!(e->susen && keyon && *pos == e->x[e->sus < e->n ? e->sus : 0])) (*pos)++;
    if (e->loopen && e->le < e->n && *pos >= e->x[e->le]) *pos = e->x[e->ls < e->n ? e->ls : 0];
    if (*pos > e->x[e->n - 1] && !e->loopen) *pos = e->x[e->n - 1];
}

XXPlayer *xxm_new(const XSong *s, int rate){
    XXPlayer *p = calloc(1, sizeof *p);
    p->s = s; p->rate = rate; p->speed = s->speed; p->tempo = s->tempo; p->gvol = 64;
    p->nrows = s->npat ? s->prows[s->orders[0]] : 64; p->first = 1;
    p->jump_ord = p->jump_row = -1;
    for (int i = 0; i < MAXCH; i++) { p->ch[i].smpi = -1; p->ch[i].pan = 128; p->ch[i].fade = 32768; }
    return p;
}
void xxm_free(XXPlayer *p){ free(p); }
int xxm_order(const XXPlayer *p){ return p->order; }
void xxm_set_scope(void *pp, float *buf, int len){ XXPlayer *p = pp; p->scb = buf; p->scl = len; p->scn = 0; }
int xxm_chan_pan(const void *pp, int ch){ return ((const XXPlayer *)pp)->ch[ch].opan; }
unsigned xxm_scope_pos(const void *pp){ return ((const XXPlayer *)pp)->scn; }
int xxm_pos(const void *pp, int *ord, int *row){ const XXPlayer *p = pp; *ord = p->d_ord; *row = p->d_row; return 0; }
extern int xplay_solo;

static const XCell *cell(const XXPlayer *p, int ch){
    const XSong *s = p->s; int pat = s->orders[p->order]; size_t base = 0;
    for (int i = 0; i < pat; i++) base += (size_t)s->prows[i] * s->nch;
    return &s->pat[base + (size_t)p->row * s->nch + ch];
}

static void trigger(XXPlayer *p, XCh *c, int note, int inst, int hasinst, int tp){
    c->vinst = 1; c->pinst = 1;
    const XSong *s = p->s;
    if (note == 97) {                       /* Key-Off */
        c->keyon = 0;
        if (c->inst >= 1 && !s->inst[c->inst].venv.on) c->kopend = 1;
        return;
    }
    if (hasinst) {
        if (inst >= 1 && inst <= s->ninst) c->inst = inst;
    }
    int ni = c->inst;
    if (note >= 1 && note <= 96) {
        int si; rd_inst_smp(s, ni, note, &si);
        if (si < 0 || !s->xsmp[si].len) { if (!tp) { c->smpi = si; } if (si < 0) return; }
        XSmp *m = &s->xsmp[si];
        double per = note_period(s, note, m->relnote, tp && c->smpi == si ? c->ft : m->finetune);
        if (tp) { c->tper = per; if (hasinst && c->smpi >= 0) { XSmp *cm = &s->xsmp[c->smpi]; c->vol = cm->vol; c->pan = cm->pan; } }
        else {
            start_xf(c);
            c->smpi = si; c->ft = m->finetune; c->note = note;
            c->per = c->tper = per;
            c->on = m->len > 0; c->pos = 0; c->dir = 1; c->avpos = 0; c->avcnt = 0;
            c->m_vpos = (c->vwave & 4) ? c->m_vpos : 0; c->m_tpos = (c->twave & 4) ? c->m_tpos : 0;
            if (hasinst) { c->vol = m->vol; c->pan = m->pan; }
        }
        if (!tp || hasinst) { c->keyon = 1; c->fade = 32768; c->venvt = c->penvt = 0; }
    } else if (hasinst && ni >= 1 && c->smpi >= 0) {   /* nur Instrument: Lautstaerke/Pan aus dem NEUEN Instrument (Sample der letzten Note) */
        int si2; rd_inst_smp(s, ni, c->note, &si2); if (si2 < 0) si2 = c->smpi;
        XSmp *m = &s->xsmp[si2];
        c->vol = m->vol; c->pan = m->pan; c->keyon = 1; c->fade = 32768; c->venvt = c->penvt = 0;
    }
}

static void volslide(int *v, int par){ int u = par >> 4, d = par & 15; if (u) *v += u; else *v -= d; *v = clampi(*v, 0, 64); }

static void do_row_cell(XXPlayer *p, int chn, XCh *c, const XCell *x){
    const XSong *s = p->s;
    int eff = x->eff, par = x->par, vc = x->vol1;
    int tpvol = (vc >> 4) == 0xF || eff == 3 || eff == 5;
    c->trig = 0;
    if (eff == 0x0E && (par >> 4) == 0x0D && (par & 15)) { c->dnote = x->note ? x->note : -1; c->dinst = x->inst; c->dvol = vc; }
    else {
        if (x->note || x->inst) trigger(p, c, x->note, x->inst, x->inst != 0, tpvol && x->note != 97);
    }
    if (eff == 9 && par) c->m_off = par;
    if (eff == 9 && !(eff == 0x0E)) { XSmp *m = c->smpi >= 0 ? &s->xsmp[c->smpi] : NULL; if (m && x->note && c->m_off) { c->pos = c->m_off * 256.0; if (c->pos >= m->len) c->on = 0; } }
    /* Volumenspalte Tick 0 */
    if (vc >= 0x10 && vc <= 0x50) { c->vol = vc - 0x10; c->vinst = 1; }
    switch (vc >> 4) {
    case 0x6: if ((vc & 15) == 15) c->vol = clampi(c->vol - 15, 0, 64); break;   /* gemessen: 6F/7F wirken auch in Tick 0 */
    case 0x7: if ((vc & 15) == 15) c->vol = clampi(c->vol + 15, 0, 64); break;
    case 0x8: c->vol = clampi(c->vol - (vc & 15), 0, 64); break;
    case 0x9: c->vol = clampi(c->vol + (vc & 15), 0, 64); break;
    case 0xA: if (vc & 15) c->m_vs = vc & 15; break;
    case 0xB: if (vc & 15) c->m_vd = vc & 15; break;
    case 0xC: c->pan = (vc & 15) * 16 + 8; c->pinst = 1; break;   /* gemessen: x*16+8 */
    case 0xF: if (vc & 15) c->m_tp = (vc & 15) * 16; break;
    }
    int hi = par >> 4, lo = par & 15;
    switch (eff) {
    case 1: case 2: { if (par) c->m_porta = par; int sp = c->m_porta;
            if (sp >= 0xF0) c->per += (eff == 1 ? -4 : 4) * (sp & 15); } break;   /* gemessen: 1xx/2xx FF = Feinslide 4*x in Tick 0 (S3M-Semantik) */
    case 3: if (par) c->m_tp = par; break;
    case 4: if (hi) c->m_vs = hi; if (lo) c->m_vd = lo; break;
    case 5: case 6: case 0xA: if (par) c->m_vsl = par; break;
    case 7: if (hi) c->m_ts = hi; if (lo) c->m_td = lo; break;
    case 8: c->pan = par; c->pinst = 1; break;
    case 0xB: p->jump_ord = par; p->jump_row = 0; break;
    case 0xC: c->vol = clampi(par, 0, 64); c->vinst = 1; break;
    case 0xD: p->jump_ord = p->order + 1; p->jump_row = hi * 10 + lo; break;
    case 0x0F: if (par) { if (par < 32) p->speed = par; else p->tempo = par; } break;
    case 0x10: p->gvol = clampi(par, 0, 64); break;
    case 0x11: if (par) c->m_gvs = par; break;
    case 0x14: if (par == 0) { c->keyon = 0; if (c->inst >= 1 && !s->inst[c->inst].venv.on) c->kopend = 1; } break;
    case 0x15: c->venvt = par; break;
    case 0x19: if (par) c->m_psl = par; break;
    case 0x1B: if (par) c->m_rv = par; break;
    case 0x1D: if (par) c->m_tr = par; break;
    case 0x21: if (hi == 1) { int d = lo ? lo : 0; (void)d; c->per -= lo; } else if (hi == 2) c->per += lo; break;
    case 0x0E:
        switch (hi) {
        case 1: if (lo) c->m_fine1 = lo; c->per -= 4 * lo; break;
        case 2: c->per += 4 * lo; break;
        case 4: c->vwave = lo; break;
        case 5: c->ft = (lo - 8) * 16; break;
        case 6: if (!lo) p->loop_row[chn] = p->row; else { if (p->loop_cnt[chn] == 0) p->loop_cnt[chn] = lo; else p->loop_cnt[chn]--; if (p->loop_cnt[chn]) { p->jump_ord = p->order; p->jump_row = p->loop_row[chn]; p->eloop = 1; } } break;
        case 7: c->twave = lo; break;
        case 8: c->pan = lo * 17; c->pinst = 1; break;
        case 0xA: if (lo) c->m_eau = lo; c->vol = clampi(c->vol + c->m_eau, 0, 64); break;
        case 0xB: if (lo) c->m_ead = lo; c->vol = clampi(c->vol - c->m_ead, 0, 64); break;
        case 0xE: p->pdelay = lo; break;
        }
        break;
    }
    c->cutdelay = (eff == 0x0E && hi == 0xC) ? lo : -1;
    if (c->cutdelay == 0) c->vol = 0;
}

static double sine64(int pos, int wave){
    pos &= 63; wave &= 3;
    if (wave == 0) { static int m = -1; if (m < 0) { const char *e = getenv("XMSIN"); m = e ? atoi(e) : 0; } if (m == 1) return 256.0 * sin(pos * M_PI / 32.0); if (m == 2) return floor(256.0 * sin(pos * M_PI / 32.0) + 0.5); return (pos < 32 ? vib_tab[pos] : -vib_tab[pos - 32]); }
    if (wave == 1) { int v = (32 - pos) * 8; return v > 255 ? 255 : v < -255 ? -255 : v; }
    return pos < 32 ? 255 : 0;       /* gemessen: Rechteck = 255/0 */
}

static void do_tick_effects(XXPlayer *p, int chn, XCh *c, const XCell *x){
    const XSong *s = p->s; int eff = x->eff, par = x->par, hi = par >> 4, lo = par & 15, vc = x->vol1, t = p->tick;
    if (t == 0) return;
    /* Volumenspalte */
    switch (vc >> 4) {
    case 0x6: c->vol = clampi(c->vol - (vc & 15), 0, 64); break;
    case 0x7: c->vol = clampi(c->vol + (vc & 15), 0, 64); break;
    case 0xD: c->pan = clampi(c->pan - 4 * (vc & 15), 0, 255); break;
    case 0xE: c->pan = clampi(c->pan + 4 * (vc & 15), 0, 255); break;
    }
    int tporta = (eff == 3 || eff == 5 || (vc >> 4) == 0xF);
    if (tporta) {
        int sp = 4 * c->m_tp;
        if (c->per < c->tper) { c->per += sp; if (c->per > c->tper) c->per = c->tper; }
        else if (c->per > c->tper) { c->per -= sp; if (c->per < c->tper) c->per = c->tper; }
    }
    switch (eff) {
    case 0x14: if (t == par) { c->keyon = 0; if (c->inst >= 1 && !p->s->inst[c->inst].venv.on) c->kopend = 1; } break;
    case 1: if (c->m_porta < 0xF0) c->per -= 4 * c->m_porta; break;
    case 2: if (c->m_porta < 0xF0) c->per += 4 * c->m_porta; break;
    case 5: volslide(&c->vol, c->m_vsl); break;
    case 6: volslide(&c->vol, c->m_vsl); break;
    case 0xA: volslide(&c->vol, c->m_vsl); break;
    case 0x11: volslide(&p->gvol, c->m_gvs); break;
    case 0x19: { int u = c->m_psl >> 4, d = c->m_psl & 15; if (u) c->pan = clampi(c->pan + 4 * u, 0, 255); else c->pan = clampi(c->pan - 4 * d, 0, 255); } break;
    case 0x0E:
        if (hi == 0xC && t == lo) c->vol = 0;
        if (hi == 0xD && t == lo) { trigger(p, c, c->dnote > 0 ? c->dnote : 0, c->dinst, c->dinst != 0, 0); if (c->dvol >= 0x10 && c->dvol <= 0x50) c->vol = c->dvol - 0x10; }
        if (hi == 9 && lo && t % lo == 0) { start_xf(c); c->pos = 0; c->on = 1; }
        break;
    case 0x1B: { int rl = c->m_rv & 15, rh = c->m_rv >> 4;
        if (rl && t % rl == 0) { start_xf(c); c->pos = 0; c->on = 1; static const int add[16]={0,-1,-2,-4,-8,-16,0,0,0,1,2,4,8,16,0,0};
            if (rh == 6) c->vol = c->vol * 2 / 3; else if (rh == 7) c->vol >>= 1; else if (rh == 0xE) c->vol = c->vol * 3 / 2; else if (rh == 0xF) c->vol *= 2; else c->vol += add[rh];
            c->vol = clampi(c->vol, 0, 64); } } break;
    }
    (void)s; (void)chn; (void)hi; (void)lo;
}

static void do_tick(XXPlayer *p){
    const XSong *s = p->s; p->tkpos = 0;
    if (p->tick == 0) { p->d_ord = p->order; p->d_row = p->row; }
    if (getenv("XPOS") && p->tick == 0) fprintf(stderr, "POS %.2f %d %d sp%d tp%d\n", p->tsec, p->order, p->row, p->speed, p->tempo);
    if (getenv("XMDBG") && p->tick == 0) fprintf(stderr, "ROW o%d(p%d) r%d sp%d tp%d\n", p->order, s->orders[p->order], p->row, p->speed, p->tempo);
    for (int i = 0; i < s->nch; i++) {
        XCh *c = &p->ch[i]; const XCell *x = cell(p, i);
        if (c->kopend == 2) { start_xf(c); c->vol = 0; c->kopend = 0; }
        if (p->tick == 0) {
            if (x->eff == 0x0E && (x->par >> 4) == 0xD && (x->par & 15)) { c->dnote = x->note; c->dinst = x->inst; c->dvol = x->vol1; }
            do_row_cell(p, i, c, x);
        } else if (x->eff == 0x0E && (x->par >> 4) == 0xD && p->tick == (x->par & 15)) {
            do_tick_effects(p, i, c, x);
        } else do_tick_effects(p, i, c, x);
        if (c->kopend == 1) c->kopend = 2;
        /* Ausgabe-Parameter */
        XSmp *m = c->smpi >= 0 ? &s->xsmp[c->smpi] : NULL;
        if (!m) { c->step = 0; continue; }
        int eff = x->eff, par = x->par, vc = x->vol1;
        double per = c->per;
        if (eff == 0 && par) { int a = p->tick % 3; int semi = a == 1 ? par >> 4 : a == 2 ? par & 15 : 0; per = s->linear ? per - 64 * semi : per * pow(2.0, -semi / 12.0); }
        if (eff == 4 || eff == 6 || (vc >> 4) == 0xB) {
            double v = sine64(c->m_vpos, c->vwave); per += v * c->m_vd / 32.0; if (p->tick > 0) { c->m_vpos += c->m_vs; c->m_vpos &= 63; }
        }
        { const XInst *I = &s->inst[c->inst];
          if (I->vibdepth) {
              int amp = I->vibdepth;
              if (I->vibsweep) { amp = I->vibdepth * (c->avcnt < I->vibsweep ? c->avcnt : I->vibsweep) / I->vibsweep; if (c->keyon) c->avcnt++; } 
              double wv = I->vibtype == 0 ? sin(c->avpos * 2 * M_PI / 256.0) * 64 : I->vibtype == 1 ? ((c->avpos & 255) < 128 ? 64 : -64) : I->vibtype == 2 ? (64 - (c->avpos & 255) * 128.0 / 256) : ((c->avpos & 255) * 128.0 / 256 - 64);
              per += wv * amp / 16.0; c->avpos += I->vibrate;
          } }
        if (eff == 0x21 && 0) {}
        /* Huellkurven */
        int ev = 64, pv = 32; const XInst *I = &s->inst[c->inst];
        if (I->venv.on) { ev = env_val(&I->venv, c->venvt); env_adv(&I->venv, &c->venvt, c->keyon); } 
        if (I->penv.on) { pv = env_val(&I->penv, c->penvt); env_adv(&I->penv, &c->penvt, c->keyon); }
        if (!c->keyon) { c->fade -= I->fadeout; if (c->fade < 0) c->fade = 0; }
        double fv = (double)c->vol / 64.0 * ev / 64.0 * c->fade / 32768.0 * p->gvol / 64.0;
        if (eff == 7) { double v = sine64(c->m_tpos, c->twave); (void)v; }
        if (getenv("XMDBG2") && i == 0) fprintf(stderr, "T%d vol%d keyon%d fade%d ev%d fv%.3f inst%d smpi%d on%d\n", p->order * 64 * 6 + p->row * 6 + p->tick, c->vol, c->keyon, c->fade, ev, fv, c->inst, c->smpi, c->on);
        c->vprev = c->fvol; c->fvol = fv < 0 ? 0 : fv; if (c->vinst || c->vprev <= 0 || c->fvol <= 0) { c->vprev = c->fvol; c->vinst = 0; }   /* aus/zu Stille: sofort */
        c->pprev = c->opan; c->opan = clampi(c->pan + (pv - 32) * (128 - abs(c->pan - 128)) / 32, 0, 255); if (c->pinst) { c->pprev = c->opan; c->pinst = 0; }
        c->step = period_hz(s, per) / p->rate;
    }
}

static void advance(XXPlayer *p){
    const XSong *s = p->s;
    if (++p->tick < p->speed + (p->tick >= p->speed ? 0 : 0)) return;
    p->tick = 0;
    if (p->pdelay) { p->pdelay--; p->tick = 0; if (p->pdelay) { } }
    int no = p->order, nr = p->row + 1;
    int jumped = 0;
    if (p->jump_ord >= 0) { jumped = (p->jump_ord != p->order || p->jump_row <= p->row) && !p->eloop; no = p->jump_ord; nr = p->jump_row; p->jump_ord = -1; if (no != p->order && p->jump_row >= 0) { } }
    else if (nr >= p->nrows) { no++; nr = 0; }
    if (p->jump_row >= 0 && p->jump_ord < 0) { }
    p->jump_row = -1;
    if (no >= s->nord) { p->done = 1; return; }
    if (jumped) { if (p->seen[(no * 256 + nr) >> 3] & (1 << ((no * 256 + nr) & 7))) { p->done = 1; return; } }
    p->order = no; p->row = nr; p->eloop = 0;
    p->seen[(no * 256 + nr) >> 3] |= 1 << ((no * 256 + nr) & 7);
    p->nrows = s->prows[s->orders[p->order]];
    if (p->row >= p->nrows) p->row = 0;
}

static double read_smp(const XSmp *m, double pos, int *ended){
    uint32_t i0 = (uint32_t)pos; if (i0 >= m->len) { *ended = 1; return 0; }
    double fr = pos - i0; uint32_t i1 = i0 + 1;
    if (m->loop && !m->pingpong && i1 >= m->le) i1 = m->ls + (i1 - m->le);
    if (i1 >= m->len) i1 = i0;
    double s0, s1;
    if (m->is16) { const int16_t *d = (const int16_t *)m->data; s0 = d[i0] / 32768.0; s1 = d[i1] / 32768.0; }
    else { s0 = m->data[i0] / 128.0; s1 = m->data[i1] / 128.0; }
    return s0 + (s1 - s0) * fr;
}
static void adv_pos(const XSmp *m, double *pos, int *dir, double step, int *on){
    *pos += *dir * step;
    if (m->loop) {
        if (m->pingpong) {
            if (*dir > 0 && *pos >= m->le) { *pos = 2.0 * m->le - *pos; *dir = -1; }
            if (*dir < 0 && *pos < m->ls) { *pos = 2.0 * m->ls - *pos; *dir = 1; }
        } else while (*pos >= m->le) *pos -= (m->le - m->ls);
    } else if (*pos >= m->len) *on = 0;
}
static void pan_add(double sm, int pan, double *l, double *r){
    double pr = pan / 255.0; pr = pr < 0 ? 0 : pr > 1 ? 1 : pr;   /* XM gemessen: linear, R = pan/255 */
    *l += sm * (1.0 - pr); *r += sm * pr;
}
int xxm_render(XXPlayer *p, int16_t *out, int n){
    const XSong *s = p->s; int done = 0;
    double master = 96.0 / 256.0;
    { const char *e = getenv("XMGAIN"); if (e) master = atof(e); }
    while (done < n) {
        if (p->done) break;
        if (p->acc <= 0) {
            do_tick(p);
            p->tklen = (int)(p->rate * 2.5 / p->tempo); p->acc += (double)p->tklen;
            advance(p);
            if (p->done && p->acc <= 0) break;
        }
        int chunk = (int)p->acc; if (chunk < 1) chunk = 1; if (chunk > n - done) chunk = n - done;
        for (int f = 0; f < chunk; f++) {
            double l = 0, r = 0, cs[MAXCH];
            for (int i = 0; i < s->nch; i++) cs[i] = 0;
            for (int i = 0; i < s->nch; i++) {
                XCh *c = &p->ch[i];
                int solo_skip = xplay_solo >= 0 && i != xplay_solo;
                double gnew = 1.0;
                if (c->xf > 0) {                          /* alte Stimme ausblenden */
                    int L = xf_len(); double w = (double)(c->xf - 1) / L; gnew = 1.0;
                    { static int md = -1; if (md < 0) { const char *e = getenv("XMMODE"); md = e ? atoi(e) : 0; } if (md == 1) { w = 1; gnew = 0; } else if (md == 2) { w = 0; gnew = 1; } }
                    const XSmp *om = &s->xsmp[c->osmpi]; int oon = 1, en = 0;
                    if (!solo_skip) { double v = read_smp(om, c->opos, &en); if (!en) { pan_add(v * c->ofvol * w, c->oopan, &l, &r); cs[i] += v * c->ofvol * w; } }
                    adv_pos(om, &c->opos, &c->odir, c->ostep, &oon); c->xf--;
                }
                if (!c->on || c->smpi < 0) continue;
                const XSmp *m = &s->xsmp[c->smpi];
                if (!solo_skip) { int en = 0; double v = read_smp(m, c->pos, &en); if (en) { c->on = 0; continue; }
                    double rf = p->tklen > 0 ? (double)(p->tkpos + 1) / p->tklen : 1.0; if (rf > 1) rf = 1;
                    double vol = c->vprev + (c->fvol - c->vprev) * rf;
                    pan_add(v * vol * gnew, (int)(c->pprev + (c->opan - c->pprev) * rf + 0.5), &l, &r); cs[i] += v * vol * gnew; }
                adv_pos(m, &c->pos, &c->dir, c->step, &c->on);
            }
            p->tkpos++;
            if (p->scb) { unsigned w = p->scn & (unsigned)(p->scl - 1); for (int i = 0; i < s->nch; i++) p->scb[(size_t)i * p->scl + w] = (float)(cs[i] * master * 2.0); p->scn++; }
            l *= master * 32767; r *= master * 32767;
            out[(done + f) * 2]     = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            out[(done + f) * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        done += chunk; p->acc -= chunk; p->tsec += (double)chunk / p->rate;
    }
    return done;
}
