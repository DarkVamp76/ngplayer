/* IT-Replayer (Impulse-Tracker-Semantik, nach ITTECH/Schism-Verhalten). GPL-3.0-or-later
 * Stand: Kern ohne Envelopes/NNA/Filter (Sample- und einfacher Instrument-Modus). */
#include "it.h"
extern unsigned long long xplay_mute;
#include "it_tables.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

enum { F_LOOP = 1, F_PP = 2, F_PPREV = 4, F_SUS = 8, F_KEYOFF = 16, F_FADE = 32, F_VIB = 64, F_TREM = 128, F_PORTA = 256,
       F_FAST = 512, F_SURR = 1024, F_GLISS = 2048, F_MUTE = 4096, F_FILT = 8192, F_NEWNOTE = 16384 };
enum { NONE = 0, NOTE_FADE = 253, NOTE_CUT = 254, NOTE_OFF = 255 };
enum { V_NONE, V_VOL, V_FINEUP, V_FINEDOWN, V_SLUP, V_SLDOWN, V_PDOWN, V_PUP, V_PAN, V_TPORTA, V_VIB };
#define NEXT_ORDER 0xFFFE

typedef struct {
    const ITSmp *smp; const ITInst *ins;
    uint32_t len, ls, le;            /* effektive Laenge/Loop */
    double pos; int flags;
    double freq, ptarget; uint32_t c5;
    int note, new_note, new_inst, last_inst;
    int row_note, row_inst, row_vc, row_vp, row_cmd, row_par;
    int vol, chvol, instvol, pan, chpan; int fadeout; int vpos, ppos, tpos; int master; int cutoff, resonance; double fa0, fb0, fb1, fy[2][2];
    int vibpos, vibdepth, vibspeed, vibtype, tremdepth, tremspeed, trempos, tremtype, tremdelta;
    int pbdepth, pbspeed, pbpos, pbtype, pbdelta;
    int avdepth, avpos;
    int m_pitch, m_porta, m_vol, m_vc, m_off, m_arp, m_ret, m_trem, m_spec, m_tempo, m_pan, m_cvol, m_gvol;
    int cd_ret, cd_trem, cd_cut, cd_delay, cd_loop, loop_row;
    int ncmd;
    double gl, gr, pgl, pgr; double step;
    int active;
} V;

typedef struct ITPlayer {
    const ITSong *s; int rate;
    int speed, tempo, gvol, tick_count, row_count, process_row, process_order, cur_order, cur_pat, row, break_row, frame_delay, firsttick;
    V ch[192];
    uint8_t *visited; int ended;
    int tk_left, tk_len; double K; int jumped;
    double *tkbuf; int16_t *tkout; int tkcap; int solo; long long nsamp;
    float *scb; int scl; unsigned scn;   /* Scope-Ringpuffer je Kanal */
    float *tksc; int tkscap;             /* Scope des laufenden Ticks: [64][tk_len] */
} ITPlayer;

static int clampi(int v, int a, int b){ return v < a ? a : v > b ? b : v; }
static int32_t muldivr(int64_t a, int64_t b, int64_t c){ return (int32_t)((a * b + c / 2) / c); }

static int g_it200;   /* cmwt == 0x200: libopenmpt rechnet mit der Semiton-Tabelle (aus Kalibrierung gemessen) */
static int g_linear;
static int g_swapcancel;   /* Gxx+Notiz+Samplewechsel ignoriert Portamento (gemessen: cmwt 0x200 mit cwtv 0x214/0x217, cwtv 0x888 und >= 0x1000) */
static double freq_from_note(int note, uint32_t c5){
    if (note <= 0 || note > 0xF0) return 0;
    if (g_it200 && c5) {
        static const int tab[12] = {1712,1616,1524,1440,1356,1280,1208,1140,1076,1016,960,907};
        int n = note - 1;
        if (g_linear) return c5 * (1712.0 / tab[n % 12]) * pow(2.0, n / 12 - 5);
        int64_t per = (((int64_t)tab[n % 12] << 5) >> (n / 12)) * 8363 / c5;
        if (per < 1) per = 1;
        return 1712.0 * 8363.0 / (double)per;
    }
    return c5 * pow(2.0, (note - 61) / 12.0);
}
static int note_from_freq(double f, uint32_t c5){
    if (f <= 0) return 0;
    for (int n = 0; n <= 120; n++) if (f <= freq_from_note(n + 1, c5) * 1.00000001) return n + 1;
    return 120;
}

/* slide: Einheiten 1/768 Oktave (linear) bzw. Amiga-Periodeneinheiten (Faktor 4 je Parameter) */
static double freq_slide(const ITPlayer *p, double f, int32_t slide, int tp){
    if (f <= 0) return 0;
    if (p->s->flags & 8) {
        if (!getenv("ITNOINT")) {
            int64_t fi = (int64_t)floor(f + 0.5), nf; int n = slide < 0 ? -slide : slide; if (n > 1020) n = 1020;
            if (n == 0) return f;
            double m = n < 16 ? pow(2.0, (slide < 0 ? -n : n) / 768.0) : pow(2.0, (slide < 0 ? -(n / 4) : (n / 4)) / 192.0);
            int64_t mult = (int64_t)floor(m * 65536.0 + 0.5);
            nf = (fi * mult + 32768) >> 16;
            if (nf == fi) nf += slide > 0 ? 1 : -1;
            return (double)nf;
        }
        return f * pow(2.0, slide / 768.0);
    }
    const double K = 1712.0 * 8363.0;
    if (slide < 0) return K * f / (f * -slide + K);
    if (slide > 0) {
        double div = K - f * slide;
        if (div <= 0) { if (tp) div = 1e-3; else return 0; }
        return K * f / div;
    }
    return f;
}

static int wave(int type, int pos){
    switch (type & 3) { case 1: return it_ramp_down_table[pos & 255]; case 2: return it_square_table[pos & 255];
        case 3: return (rand() & 0x7F) - 0x40; default: return it_sine_table[pos & 255]; }
}

/* ---------------- Loop-/Sample-Setup ---------------- */
static void set_loop(V *c){
    const ITSmp *m = c->smp; c->len = m->len; c->ls = 0; c->le = m->len;
    c->flags &= ~(F_LOOP | F_PP | F_SUS);
    if (m->sloop) { c->ls = m->slb; c->le = m->sle; c->flags |= F_LOOP | F_SUS; if (m->spingpong) c->flags |= F_PP; }
    else if (m->loop) { c->ls = m->lb; c->le = m->le; c->flags |= F_LOOP; if (m->pingpong) c->flags |= F_PP; }
    if (c->le > m->len) c->le = m->len;
    if ((c->flags & F_LOOP) && c->le < c->len) c->len = c->le;
    if ((c->flags & F_LOOP) && c->ls >= c->le) c->flags &= ~(F_LOOP | F_PP | F_SUS);
}

static void key_off(ITPlayer *p, V *c){
    c->flags |= F_KEYOFF;
    if ((p->s->flags & 4) && c->ins) { /* ohne Volume-Envelope: Fade */ if (!(c->ins->venv.flg & 1)) c->flags |= F_FADE; }
    if (!c->len || !c->smp) return;
    if ((c->flags & F_SUS) && c->smp->sloop && c->smp->loop) {      /* Sustain-Loop -> normaler Loop */
        c->flags &= ~(F_SUS | F_PP); c->flags |= F_LOOP; if (c->smp->pingpong) c->flags |= F_PP;
        c->ls = c->smp->lb; c->le = c->smp->le; c->len = c->smp->len; if (c->le > c->len) c->le = c->len; if (c->len > c->le) c->len = c->le;
    } else if ((c->flags & F_SUS)) {                                 /* Sustain-Loop ohne normalen Loop: Loop aus */
        c->flags &= ~(F_SUS | F_LOOP | F_PP); c->len = c->smp->len;
    }
}
static void note_cut(ITPlayer *p, V *c){
    (void)p;
    if ((c->row_note) && c->row_vc == V_TPORTA && c->row_note < 120) return;
    c->flags |= F_FADE | F_FAST; c->fadeout = 0; c->len = 0;
}

/* ---------------- Instrument / Note ---------------- */
static const ITSmp *translate(const ITSong *s, const ITInst *in, int note, int *notemap){
    int n = note - 1; if (n < 0 || n > 119) return NULL;
    int sm = in->smp_map[n]; if (notemap) *notemap = in->note_map[n] + 1;
    if (sm == 0 || sm > s->nsmp) return NULL;
    return &s->smp[sm];
}

static void set_inst_pan(V *c, const ITInst *in, const ITSmp *sm){
    if (in && !(in->dfp & 0x80)) { c->pan = (in->dfp & 0x7F) * 4; if (c->pan > 256) c->pan = 256; c->flags &= ~F_SURR; }
    else if (sm && (sm->dfp & 0x80)) { c->pan = (sm->dfp & 0x7F) * 4; if (c->pan > 256) c->pan = 256; c->flags &= ~F_SURR; }
}

static void instrument_change(ITPlayer *p, V *c, int instr, int porta, int inst_column){
    const ITSong *s = p->s; int imode = (s->flags & 4) != 0;
    const ITInst *in = NULL; const ITSmp *sm = NULL; int note = c->new_note;
    if (note == NONE) return;
    if (imode) {
        if (instr < 1 || instr > s->ninst) return;
        in = &s->inst[instr];
        if (note >= 1 && note <= 120) { int nm; sm = translate(s, in, note, &nm); if (!sm) { c->ins = in; return; } if (nm > 120) return; }
        else { if (note >= 253) return; sm = NULL; }
    } else {
        if (instr < 1 || instr > s->nsmp) return;
        sm = &s->smp[instr];
    }
    if (inst_column && sm) c->vol = sm->vol * 4;
    c->new_inst = 0;
    c->ins = in;
    if (sm) { c->instvol = in ? (sm->gvl * in->gbv) >> 7 : sm->gvl; }
    if (((p->s->flags & 32) || getenv("ITCOMPAT")) && c->smp && sm != c->smp && porta && c->pos > 0 && c->len) sm = c->smp;   /* Compat-Gxx: Sample bleibt */
    if (!sm) { c->smp = NULL; c->instvol = 0; return; }
    if (sm == c->smp && c->len) return;
    if (porta && !c->len) c->pos = 0;
    c->flags &= ~(F_KEYOFF | F_FADE);
    { double keep = c->pos; c->smp = sm; c->c5 = sm->c5spd; c->pos = (porta && getenv("ITKEEPPOS")) ? keep : 0; c->avdepth = 0; c->avpos = 0; }
    set_loop(c);
}

static void note_change(ITPlayer *p, V *c, int ch, int note, int porta, int retrig, int have_inst){
    (void)ch;
    const ITSong *s = p->s; const ITInst *in = (s->flags & 4) ? c->ins : NULL; const ITSmp *sm = c->smp;
    if (note == NONE || note < 0) return;
    int truenote = note;
    if (in && note >= 1 && note <= 120) {
        if (!(have_inst && porta && sm)) { int nm; const ITSmp *t = translate(s, in, note, &nm); if (t) sm = t; else if (!sm) return; else return; }
        note = in->note_map[note - 1] + 1;
    }
    if (note >= 253) {
        c->new_note = NOTE_OFF;
        switch (note) {
        case NOTE_OFF: key_off(p, c); if (!porta && (s->flags & 16) && c->row_inst) c->flags &= ~(F_FADE | F_KEYOFF); break;
        case NOTE_CUT: note_cut(p, c); break;
        default: if (s->flags & 4) c->flags |= F_FADE; break;
        }
        return;
    }
    if (!sm) return;
    if (!porta) c->c5 = sm->c5spd;
    if (porta && c->pos == 0 && !c->len) porta = 0;
    note = clampi(note, 1, 120); c->note = clampi(truenote, 1, 120); c->new_inst = 0;
    double f = freq_from_note(note, c->c5);
    c->pbdelta = 0;
    if (f) {
        if (porta && c->freq) c->ptarget = f;
        else { c->ptarget = 0; c->freq = f; }
        if (!porta || !c->len) {
            c->smp = sm; set_loop(c); c->pos = 0; c->flags &= ~F_PPREV;
        }
        if (c->pos >= c->len) c->pos = c->ls;
    } else porta = 0;
    if (in && in->pps && !porta) {
        int d = (c->note - in->ppc - 1) * in->pps / 2; c->pan = clampi(c->pan + d, 0, 256);
    }
    if (!porta) { c->fadeout = 65536; c->flags &= ~F_FADE; c->vpos = c->ppos = c->tpos = 0; c->flags |= F_NEWNOTE;
        if (in) { if (in->ifr & 0x80) c->resonance = in->ifr & 0x7F; if (in->ifc & 0x80) c->cutoff = in->ifc & 0x7F; } }
    if (!(porta && (!(s->flags & 32) || !c->row_inst))) c->flags &= ~F_KEYOFF;
    if (!porta) {
        c->flags |= F_FAST; if (!retrig) { c->avdepth = 0; c->avpos = 0; c->vibpos = 0; }
        c->pgl = c->pgr = 0;
        if (!have_inst || 1) set_inst_pan(c, in, sm);
    }
}

/* ---------------- Effekte ---------------- */
static void fx_vol_up(V *c, int v){ c->vol += v * 4; if (c->vol > 256) c->vol = 256; }
static void fx_vol_down(V *c, int v){ c->vol -= v * 4; if (c->vol < 0) c->vol = 0; }

static void fx_volslide(V *c, int ft, int param){
    if (param) c->m_vol = param; else param = c->m_vol;
    if (param == (param & 0xF0)) { param >>= 4; if (param == 0xF || !ft) fx_vol_up(c, param); }
    else if (param == (param & 0x0F)) { if (param == 0xF || !ft) fx_vol_down(c, param); }
    else if ((param & 0x0F) == 0x0F) { param >>= 4; if (ft) fx_vol_up(c, param); }
    else if ((param & 0xF0) == 0xF0) { param &= 0x0F; if (ft) fx_vol_down(c, param); }
}

static void fx_reg_porta(ITPlayer *p, V *c, int ft, int param, int up){
    if (!ft) c->freq = freq_slide(p, c->freq, up ? param * 4 : -param * 4, 0);
}
static void fx_porta(ITPlayer *p, V *c, int ft, int param, int up){
    if (!param) param = c->m_pitch;
    int hi = param & 0xF0;
    if (hi == 0xE0) { if (ft && c->freq && (param & 15)) c->freq = freq_slide(p, c->freq, up ? (param & 15) : -(param & 15), 0); }
    else if (hi == 0xF0) { if (ft && c->freq && (param & 15)) c->freq = freq_slide(p, c->freq, up ? (param & 15) * 4 : -(param & 15) * 4, 0); }
    else fx_reg_porta(p, c, ft, param, up);
}
static void fx_tporta(ITPlayer *p, V *c, int ft, int param, int is_vol_variant){
    c->flags |= F_PORTA;
    if (c->freq && c->ptarget && !ft) {
        if (!param && is_vol_variant) {
            if (c->freq > 1 && (p->s->flags & 8)) c->freq *= pow(2.0, -1.0 / 768.0);
            if (c->freq < c->ptarget) { c->freq = c->ptarget; c->ptarget = 0; }
        } else if (param && c->freq < c->ptarget) {
            c->freq = freq_slide(p, c->freq, param * 4, 1);
            if (c->freq >= c->ptarget) { c->freq = c->ptarget; c->ptarget = 0; }
        } else if (param && c->freq >= c->ptarget) {
            c->freq = freq_slide(p, c->freq, -param * 4, 1);
            if (c->freq < c->ptarget) { c->freq = c->ptarget; c->ptarget = 0; }
        }
    }
}
static void fx_vibrato(V *c, int param){
    if (param & 0x0F) c->vibdepth = (param & 0x0F) * 4;
    if (param & 0xF0) c->vibspeed = (param >> 4) & 15;
    c->flags |= F_VIB;
}
static void fx_finevib(V *c, int param){
    if (param & 0x0F) c->vibdepth = param & 0x0F;
    if (param & 0xF0) c->vibspeed = (param >> 4) & 15;
    c->flags |= F_VIB;
}
static void fx_tremolo(ITPlayer *p, V *c, int ft, int param){
    if (param & 0x0F) c->tremdepth = (param & 0x0F) << 2;
    if (param & 0xF0) c->tremspeed = (param >> 4) & 15;
    c->flags |= F_TREM;
    if (ft && (p->s->flags & 16)) return;
    int tp = c->trempos & 255, d = wave(c->tremtype, tp);
    c->trempos = (tp + 4 * c->tremspeed) & 255;
    c->tremdelta = (d * c->tremdepth) >> 5;
}
static void fx_panbrello(V *c, int param){
    if (param & 0x0F) c->pbdepth = param & 0x0F;
    if (param & 0xF0) c->pbspeed = (param >> 4) & 15;
    int pp = c->pbpos & 255, d = wave(c->pbtype, pp);
    if (c->pbtype == 3) { if (!c->pbpos || c->pbpos >= c->pbspeed) c->pbpos = 0; c->pbpos++; }
    else c->pbpos += c->pbspeed;
    c->pbdelta = d;
}
static void fx_panslide(V *c, int ft, int param){
    int slide = 0;
    if (param) c->m_pan = param; else param = c->m_pan;
    if ((param & 15) == 15 && (param & 0xF0)) { if (ft) slide = -((param & 0xF0) >> 2); }
    else if ((param & 0xF0) == 0xF0 && (param & 15)) { if (ft) slide = (param & 15) << 2; }
    else if (!ft) { if (param & 15) slide = (param & 15) << 2; else slide = -((param & 0xF0) >> 2); }
    if (slide) c->pan = clampi(c->pan + slide, 0, 256);
    c->flags &= ~F_SURR; c->pbdelta = 0;
}
static void fx_chvolslide(V *c, int ft, int param){
    int slide = 0;
    if (param) c->m_cvol = param; else param = c->m_cvol;
    if ((param & 15) == 15 && (param & 0xF0)) { if (ft) slide = param >> 4; }
    else if ((param & 0xF0) == 0xF0 && (param & 15)) { if (ft) slide = -(param & 15); }
    else if (!ft) { if (param & 15) slide = -(param & 15); else slide = (param & 0xF0) >> 4; }
    if (slide) c->chvol = clampi(c->chvol + slide, 0, 64);
}
static void fx_gvolslide(ITPlayer *p, V *c, int ft, int param){
    int slide = 0;
    if (param) c->m_gvol = param; else param = c->m_gvol;
    if ((param & 15) == 15 && (param & 0xF0)) { if (ft) slide = param >> 4; }
    else if ((param & 0xF0) == 0xF0 && (param & 15)) { if (ft) slide = -(param & 15); }
    else if (!ft) { if (param & 0xF0) slide = (param & 0xF0) >> 4; else slide = -(param & 15); }
    if (slide) p->gvol = clampi(p->gvol + slide, 0, 128);
}
static void fx_retrig(ITPlayer *p, V *c, int ft, int param){
    if (ft && c->row_note != NONE) { c->cd_ret = param & 15; return; }
    if (--c->cd_ret <= 0) {
        if (c->pos == 0 && 0) return;
        c->cd_ret = param & 15;
        int pp = param >> 4;
        if (pp) {
            int v = c->vol;
            if (it_retrig1[pp]) v = (v * it_retrig1[pp]) >> 4; else v += it_retrig2[pp] * 4;
            c->vol = clampi(v, 0, 256); c->flags |= F_FAST;
        }
        int note = c->new_note; double f = c->freq;
        note_change(p, c, 0, note, 1, 1, 0);
        if (f && c->row_note == NONE) c->freq = f;
        c->pos = 0;
    }
}
static void pattern_loop(ITPlayer *p, V *c, int param){
    if (param) {
        if (c->cd_loop) {
            if (!--c->cd_loop) { c->loop_row = p->row + 1; return; }
        } else c->cd_loop = param;
        p->process_row = c->loop_row - 1; p->jumped = 0;
    } else c->loop_row = p->row;
}

static void fx_special(ITPlayer *p, V *c, int ft, int param){
    int cmd = param & 0xF0; param &= 15;
    switch (cmd) {
    case 0x10: if (param) c->flags |= F_GLISS; else c->flags &= ~F_GLISS; break;
    case 0x30: c->vibtype = param; break;
    case 0x40: c->tremtype = param; break;
    case 0x50: c->pbtype = param < 4 ? param : 0; c->pbpos = 0; break;
    case 0x60: if (ft) { p->frame_delay += param; p->tick_count += param; } break;
    case 0x80: if (ft) { c->flags &= ~F_SURR; c->pbdelta = 0; c->pan = param * 17 + (param >= 7); c->flags |= F_FAST; } break;
    case 0x90:
        if (!ft) break;
        if (param == 0) c->flags &= ~F_SURR;
        else if (param == 1) { c->flags |= F_SURR; c->pbdelta = 0; c->pan = 128; }
        else if (param == 0xE) c->flags &= ~F_PPREV;
        else if (param == 0xF) { if (c->pos == 0 && c->len && (c->row_note > 0 && c->row_note < 253 || !(c->flags & F_LOOP))) c->pos = c->len - 0.0001; c->flags |= F_PPREV; }
        break;
    case 0xA0: if (ft) c->m_off = (param << 16) | (c->m_off & ~0xF0000); break;
    case 0xB0: if (ft) pattern_loop(p, c, param); break;
    case 0xC0: if (ft) c->cd_cut = param ? param : 1; else if (--c->cd_cut == 0) note_cut(p, c); break;
    case 0xE0: if (ft && !p->row_count) p->row_count = param + 1; break;
    }
}

static void handle_vol(ITPlayer *p, V *c, int vc, int vp, int ft, int start_note){
    switch (vc) {
    case V_VOL: if (start_note) { c->vol = (vp > 64 ? 64 : vp) << 2; c->flags |= F_FAST; } break;
    case V_PAN: if (start_note) { c->pan = (vp > 64 ? 64 : vp) << 2; c->pbdelta = 0; c->flags |= F_FAST; c->flags &= ~F_SURR; } break;
    case V_PUP: if (!start_note) fx_reg_porta(p, c, ft, c->m_pitch, 1); break;
    case V_PDOWN: if (!start_note) fx_reg_porta(p, c, ft, c->m_pitch, 0); break;
    case V_TPORTA: if (!start_note) fx_tporta(p, c, ft, c->m_porta, 0); break;
    case V_SLUP: if (start_note) { if (vp) c->m_vc = vp; } else fx_vol_up(c, c->m_vc); break;
    case V_SLDOWN: if (start_note) { if (vp) c->m_vc = vp; } else fx_vol_down(c, c->m_vc); break;
    case V_FINEUP: if (start_note) { if (vp) c->m_vc = vp; else vp = c->m_vc; fx_vol_up(c, vp); } break;
    case V_FINEDOWN: if (start_note) { if (vp) c->m_vc = vp; else vp = c->m_vc; fx_vol_down(c, vp); } break;
    case V_VIB: fx_vibrato(c, vp); break;
    }
}

static void handle_effect(ITPlayer *p, V *c, int cmd, int param, int ft){
    switch (cmd) {
    case 0: break;
    case 22: /* V */ if (ft && param <= 128) p->gvol = param; break;
    case 23: /* W */ fx_gvolslide(p, c, ft, param); break;
    case 1: if (ft && param) { p->tick_count = param; p->speed = param; } break;
    case 4: fx_volslide(c, ft, param); break;
    case 5: fx_porta(p, c, ft, c->m_pitch, 0); break;
    case 6: fx_porta(p, c, ft, c->m_pitch, 1); break;
    case 7: fx_tporta(p, c, ft, c->m_porta, 0); break;
    case 12: fx_tporta(p, c, ft, c->m_porta, 1); fx_volslide(c, ft, param); break;
    case 8: fx_vibrato(c, param); break;
    case 11: fx_volslide(c, ft, param); fx_vibrato(c, 0); break;
    case 21: fx_finevib(c, param); break;
    case 20:
        if (ft) { if (param) c->m_tempo = param; else param = c->m_tempo; if (param >= 0x20) p->tempo = param; }
        else { param = c->m_tempo; if ((param >> 4) == 0) { p->tempo -= param & 15; if (p->tempo < 32) p->tempo = 32; }
               else if ((param >> 4) == 1) { p->tempo += param & 15; if (p->tempo > 255) p->tempo = 255; } }
        break;
    case 15: /* O */
        if (!ft) break;
        if (param) c->m_off = (c->m_off & ~0xFF00) | (param << 8);
        if ((c->row_inst ? c->new_note : c->row_note) >= 1 && (c->row_inst ? c->new_note : c->row_note) <= 120) {
            c->pos = c->m_off; if (c->pos > c->len) c->pos = (p->s->flags & 16) ? c->len : 0;
        }
        break;
    case 10: /* J */ c->ncmd = 10; if (ft && param) c->m_arp = param; break;
    case 17: /* Q */ if (param) c->m_ret = param & 0xFF; fx_retrig(p, c, ft, c->m_ret); break;
    case 9: /* I */
        if (ft) { if (!param) param = c->m_trem; else if (!(p->s->flags & 16)) { if (param & 0xF0) param -= 0x10; if (param & 0x0F) param -= 1; }
                  c->m_trem = param; c->cd_trem |= 128; }
        c->ncmd = 9; break;
    case 13: /* M */ if (ft && param <= 64) { c->chvol = param; c->flags |= F_FAST; } break;
    case 14: fx_chvolslide(c, ft, param); break;
    case 16: fx_panslide(c, ft, param); break;
    case 24: /* X */ if (ft) { c->flags &= ~F_SURR; c->pbdelta = 0; c->pan = param; c->flags |= F_FAST; } break;
    case 18: fx_tremolo(p, c, ft, param); break;
    case 25: fx_panbrello(c, param); break;
    case 19: fx_special(p, c, ft, param); break;
    case 2: /* B */
        p->process_order = param - 1; p->process_row = NEXT_ORDER; p->jumped = 1; break;
    case 3: break;
    }
}


/* ---------------- NNA / DNA (nach Schism csf_check_nna, nur Instrumentmodus) ---------------- */
static int get_nna_channel(ITPlayer *p, V *c){
    for (int i = 64; i < 192; i++) if (!p->ch[i].len) return i;
    if (!c->fadeout) return -1;
    int res = -1; int64_t vol = 64LL * 65536; int envpos = 0xFFFFFF;
    for (int j = 64; j < 192; j++) {
        V *pj = &p->ch[j];
        if (!pj->fadeout) return j;
        int64_t v = pj->vol; if (pj->flags & F_FADE) v *= pj->fadeout; else v <<= 16;
        if (pj->flags & F_LOOP) v >>= 1;
        if (v < vol || (v == vol && pj->vpos > envpos)) { envpos = pj->vpos; vol = v; res = j; }
    }
    return res;
}
static void check_nna(ITPlayer *p, int n, int instr, int note){
    const ITSong *s = p->s; V *c = &p->ch[n];
    if (!(s->flags & 4) || note < 1 || note > 120) return;
    const ITInst *ni = (instr > 0 && instr <= s->ninst) ? &s->inst[instr] : c->ins;
    const ITSmp *nsm = c->smp;
    if (ni) { int sm = ni->smp_map[note - 1]; if (sm > 0 && sm <= s->nsmp) nsm = &s->smp[sm]; }
    for (int i = n; i < 192; i = (i == n ? 64 : i + 1)) {
        V *v = &p->ch[i];
        if (i != n && !(v->master == n + 1)) continue;
        if (!v->ins) continue;
        int apply = 0;
        switch (v->ins->dct) {
        case 1: apply = (v->note == note && ni == v->ins); break;
        case 2: apply = (nsm && nsm == v->smp && ni == v->ins); break;
        case 3: apply = (ni == v->ins); break;
        }
        if (!apply) continue;
        switch (v->ins->dca) {
        case 0: key_off(p, v); v->vol = 0; break;
        case 1: key_off(p, v); break;
        case 2: v->flags |= F_FADE; break;
        }
        if (!v->vol) { v->fadeout = 0; v->flags |= F_FADE | F_FAST; }
    }
    if (c->flags & F_MUTE) return;
    if (c->freq && c->len && c->ins) {
        int k = get_nna_channel(p, c);
        if (k >= 0) {
            V *v = &p->ch[k]; *v = *c;
            v->flags &= ~(F_VIB | F_TREM | F_PORTA); v->tremdelta = 0; v->ncmd = 0; v->master = n + 1;
            switch (c->ins->nna) {
            case 2: key_off(p, v); break;
            case 0: v->fadeout = 0; v->flags |= F_FADE; break;
            case 3: v->flags |= F_FADE; break;
            }
            if (!v->vol) { v->fadeout = 0; v->flags |= F_FADE | F_FAST; }
            c->len = 0; c->pos = 0;
        }
    }
}

static void process_effects(ITPlayer *p, int ft){
    const ITSong *s = p->s; int patloop = 0;
    for (int n = 0; n < 64; n++) {
        V *c = &p->ch[n];
        c->ncmd = 0;
        int instr = c->row_inst, vc = c->row_vc, vp = c->row_vp, cmd = c->row_cmd, param = c->row_par;
        int porta = (cmd == 7 || cmd == 12 || vc == V_TPORTA);
        int start_note = ft;
        c->flags &= ~F_FAST;
        if (instr && start_note) c->new_inst = instr;
        if ((s->flags & 4) && instr > 0 && instr <= s->ninst) {          /* IT-Bug: Note ohne Sample-Zuordnung -> Zelle ignorieren */
            int note = c->row_note != NONE ? c->row_note : c->new_note;
            if (note >= 1 && note <= 120 && s->inst[instr].smp_map[note - 1] == 0) { c->new_note = note; c->row_vc = V_NONE; c->row_cmd = 0; continue; }
        }
        if (cmd == 19) {
            if (param) c->m_spec = param; else param = c->m_spec;
            if ((param >> 4) == 0xD) {
                if (ft) { c->cd_delay = (param & 15) ? (param & 15) : 1; continue; }
                if (--c->cd_delay > 0) continue;
                start_note = (c->cd_delay == 0);
            }
        }
        if (start_note) {
            int note = c->row_note;
            if ((s->flags & 4) && (note >= 1 && note <= 120 || note == NONE)) {
                int ic = instr ? instr : c->last_inst;
                if (ic && (ic > s->ninst)) { note = NONE; instr = 0; }
            }
            if (instr && note == NONE) {
                if (s->flags & 4) { if (c->smp) c->vol = c->smp->vol * 4; if (instr <= s->ninst && c->ins != &s->inst[instr]) note = c->new_note; }
                else if (instr <= s->nsmp) { c->vol = s->smp[instr].vol * 4; if (c->smp != &s->smp[instr]) note = c->new_note; }
            }
            if (instr > 255) instr = 0;
            if (note >= 253) {
                if (instr) {
                    const ITSmp *sm = NULL;
                    if (s->flags & 4) { if (instr <= s->ninst && c->note >= 1) { int sm_i = s->inst[instr].smp_map[c->note - 1]; if (sm_i > 0 && sm_i <= s->nsmp) sm = &s->smp[sm_i]; } }
                    else if (instr <= s->nsmp) sm = &s->smp[instr];
                    if (sm) c->vol = sm->vol * 4;
                }
                if (!(s->flags & 16)) instr = 0;
            }
            if (note >= 1 && note <= 120 && !porta) check_nna(p, n, instr, note);
            int prev_new = c->new_note;
            if (note >= 1 && note <= 120) {
                c->new_note = note;
            }
            if (instr) {
                const ITSmp *psmp = c->smp;
                instrument_change(p, c, instr, porta, 1);
                if (note >= 1 && note <= 120) c->new_inst = 0;
                if (psmp != c->smp && note >= 1 && note <= 120 && g_swapcancel) porta = 0;
            }
            if (note != NONE) {
                if (!instr && c->new_inst && note >= 1 && note <= 120) {
                    if (prev_new >= 1 && prev_new <= 120) c->new_note = prev_new;
                    instrument_change(p, c, c->new_inst, porta, 0);
                    c->new_note = note; c->new_inst = 0;
                }
                note_change(p, c, n, note, porta, 0, !instr);
            }
        }
        if (ft) {
            int ecol = (cmd == 7 || cmd == 12);
            if (ecol) {
                int tp = cmd != 12 ? param : 0;
                if (tp) c->m_porta = tp; else if (!tp && !(s->flags & 32)) c->m_porta = c->m_pitch;
                if (!(s->flags & 32)) c->m_pitch = c->m_porta;
            }
            if (vc == V_TPORTA) { if (vp) c->m_porta = it_vc_porta[vp & 15]; if (!(s->flags & 32)) c->m_pitch = c->m_porta; }
            if (vp && (vc == V_PUP || vc == V_PDOWN)) { c->m_pitch = 4 * vp; if (!ecol && !(s->flags & 32)) c->m_porta = c->m_pitch; }
            if (param && (cmd == 5 || cmd == 6)) { c->m_pitch = param; if (!(s->flags & 32)) c->m_porta = c->m_pitch; }
        }
        handle_vol(p, c, vc, vp, ft, start_note);
        handle_effect(p, c, cmd, param, ft);
        if (vc == V_VIB && cmd == 8) fx_vibrato(c, vp);
        if (c->cd_loop) patloop = 1;
    }
    for (int n = 0; n < 64; n++) {
        V *c = &p->ch[n];
        if (c->row_cmd == 3 && !patloop) { p->break_row = c->row_par; p->process_row = NEXT_ORDER; p->jumped = 1; }
    }
}

/* ---------------- Tick-Ablauf ---------------- */
static int rows_of(const ITPlayer *p, int pat){ return p->s->rows[pat]; }
static int increment_order(ITPlayer *p){
    const ITSong *s = p->s;
    p->process_row = p->break_row; p->break_row = 0;
    do { p->process_order++; } while (p->process_order < s->nord && s->orders[p->process_order] == 254);
    if (p->process_order >= s->nord || s->orders[p->process_order] == 255) { p->process_row = NEXT_ORDER; return 0; }
    if (s->orders[p->process_order] >= s->npat) { p->process_row = NEXT_ORDER; return 0; }
    p->cur_order = p->process_order; p->cur_pat = s->orders[p->process_order];
    if (p->process_row >= rows_of(p, p->cur_pat)) p->process_row = 0;
    return 1;
}

static void decode_vol(int v, int *vc, int *vp){
    *vc = V_NONE; *vp = 0;
    if (v <= 64) { *vc = V_VOL; *vp = v; }
    else if (v <= 74) { *vc = V_FINEUP; *vp = v - 65; }
    else if (v <= 84) { *vc = V_FINEDOWN; *vp = v - 75; }
    else if (v <= 94) { *vc = V_SLUP; *vp = v - 85; }
    else if (v <= 104) { *vc = V_SLDOWN; *vp = v - 95; }
    else if (v <= 114) { *vc = V_PDOWN; *vp = v - 105; }
    else if (v <= 124) { *vc = V_PUP; *vp = v - 115; }
    else if (v >= 128 && v <= 192) { *vc = V_PAN; *vp = v - 128; }
    else if (v >= 193 && v <= 202) { *vc = V_TPORTA; *vp = v - 193; }
    else if (v >= 203 && v <= 212) { *vc = V_VIB; *vp = v - 203; }
}

static int process_tick(ITPlayer *p){
    const ITSong *s = p->s;
    p->firsttick = 0;
    if (--p->tick_count == 0) {
        p->tick_count = p->speed + p->frame_delay;
        if (--p->row_count <= 0) {
            p->row_count = 0;
            p->process_row++;
            if (p->process_row >= 0xFFFE || p->process_row >= rows_of(p, p->cur_pat)) {
                if (!increment_order(p)) return 0;
            }
            p->row = p->process_row;
            if (p->jumped) {
                uint8_t *vv = &p->visited[(size_t)p->cur_order * 256 + p->row];
                if (*vv) return 0;
                p->jumped = 0;
            }
            p->visited[(size_t)p->cur_order * 256 + p->row] = 1;
            p->frame_delay = 0; p->tick_count = p->speed; p->firsttick = 1;
        }
        const ITCell *m = s->pat[p->cur_pat] ? &s->pat[p->cur_pat][(size_t)p->row * 64] : NULL;
        for (int n = 0; n < 64; n++) {
            V *c = &p->ch[n]; ITCell z = {0}; const ITCell *x = m ? &m[n] : &z;
            c->row_note = (x->has & IT_HAS_NOTE) ? (x->note < 120 ? x->note + 1 : x->note) : NONE;
            c->row_inst = (x->has & IT_HAS_INST) ? x->inst : 0; if (c->row_inst) c->last_inst = c->row_inst;
            if (x->has & IT_HAS_VOL) decode_vol(x->vol, &c->row_vc, &c->row_vp); else { c->row_vc = V_NONE; c->row_vp = 0; }
            c->row_cmd = (x->has & IT_HAS_CMD) ? x->cmd : 0; c->row_par = (x->has & IT_HAS_CMD) ? x->par : 0;
            c->flags &= ~(F_PORTA | F_VIB | F_TREM); c->ncmd = 0;
        }
        process_effects(p, p->firsttick);
    } else {
        if (!(p->tick_count % (p->speed + p->frame_delay))) p->firsttick = 1;
        process_effects(p, 0);
        p->firsttick = 0;
    }
    return 1;
}

/* ---------------- Kanalparameter je Tick ---------------- */


/* ---------------- IT-Filter (nach Schism filters.c) ---------------- */
static const double reso_tab(int i){ return pow(10.0, -3.0 * i / 320.0); }
static void setup_filter(ITPlayer *p, V *c, int reset, int mod){
    int cutoff = c->cutoff * (mod + 256) / 256, res = c->resonance;
    if (cutoff > 255) cutoff = 255;
    if (res > 255) res = 255;
    if (res == 0 && cutoff >= 254) { if (c->flags & F_NEWNOTE) c->flags &= ~F_FILT; return; }
    c->flags |= F_FILT;
    double freq = 110.0 * pow(2.0, cutoff * (128.0 / (24.0 * 256.0)) + 0.25);
    if (freq > p->rate / 2.0) freq = p->rate / 2.0;
    double r = p->rate / (2.0 * M_PI * freq);
    double d = reso_tab(res) * r + reso_tab(res) - 1.0, e = r * r;
    c->fa0 = 1.0 / (1.0 + d + e); c->fb0 = (d + e + e) / (1.0 + d + e); c->fb1 = -e / (1.0 + d + e);
    if (reset) c->fy[0][0] = c->fy[0][1] = c->fy[1][0] = c->fy[1][1] = 0;
}

/* ---------------- Envelopes (nach Schism) ---------------- */
static void env_step(const ITEnv *e, int *pos, int keyoff, int fade_flag, V *c, int is_vol){
    if (!(e->flg & 1) || e->num == 0) return;
    int start, end;
    if ((e->flg & 4) && !keyoff) { start = e->tick[e->slb]; end = e->tick[e->sle] + 1; fade_flag = 0; }
    else if (e->flg & 2) { start = e->tick[e->lpb]; end = e->tick[e->lpe] + 1; fade_flag = 0; }
    else { start = end = e->tick[e->num - 1]; }
    if (*pos >= end) {
        if (fade_flag && e->y[e->num - 1] == 0) c->fadeout = 0;
        *pos = start;
        if (fade_flag) c->flags |= F_FADE;
    }
    (*pos)++;
    (void)is_vol;
}
/* liefert interpolierten Wert in Roh-Einheiten *scale; off verschiebt (Pan/Pitch: +32 bzw. 0) */
static int env_eval(const ITEnv *e, int position, int offs, int scale, int *ok){
    *ok = 0; if (!(e->flg & 1) || e->num == 0 || position == 0) return 0;
    *ok = 1;
    int envpos = position - 1, pt = e->num - 1;
    for (int i = 0; i < e->num - 1; i++) if (envpos <= e->tick[i]) { pt = i; break; }
    int x2 = e->tick[pt], x1, v;
    int y2 = (e->y[pt] + offs) * scale;
    if (envpos >= x2) { v = y2; x1 = x2; }
    else if (pt) { v = (e->y[pt-1] + offs) * scale; x1 = e->tick[pt-1]; }
    else { v = 0; x1 = 0; }
    if (envpos > x2) envpos = x2;
    if (x2 > x1 && envpos > x1) v += ((envpos - x1) * (y2 - v)) / (x2 - x1);
    return v;
}

static void update_channels(ITPlayer *p){
    const ITSong *s = p->s; double master = (double)(s->mv << 2);
    for (int n = 0; n < 192; n++) {
        V *c = &p->ch[n]; c->pgl = c->gl; c->pgr = c->gr; c->gl = c->gr = 0;
        if (c->flags & F_MUTE) continue;
        int fpan = c->pan; if (c->pbdelta) fpan += (c->pbdelta * c->pbdepth + 2) >> 3;
        if (c->freq && c->len && c->smp) {
            int vol = c->vol;
            if (c->flags & F_TREM) vol += c->tremdelta;
            vol = clampi(vol, 0, 256);
            if (c->ncmd == 9) {
                if ((c->cd_trem & 128) && c->len) {
                    if (c->cd_trem == 128) c->cd_trem = (c->m_trem >> 4) | 192;
                    else if (c->cd_trem == 192) c->cd_trem = (c->m_trem & 15) | 128;
                    else c->cd_trem--;
                }
                if ((c->cd_trem & 192) == 128) vol = 0;
                c->flags |= F_FAST;
            }
            vol <<= 6;
            if ((s->flags & 4) && c->ins) {
                const ITInst *ii = c->ins; int ok;
                env_step(&ii->venv, &c->vpos, !!(c->flags & F_KEYOFF), 1, c, 1);
                env_step(&ii->penv, &c->ppos, !!(c->flags & F_KEYOFF), 0, c, 0);
                env_step(&ii->pitchenv, &c->tpos, !!(c->flags & F_KEYOFF), 0, c, 0);
                int ev = env_eval(&ii->venv, c->vpos, 0, 4, &ok);
                if (ok) { ev = clampi(ev, 0, 256); vol = (vol * ev) >> 8; }
                int ep = env_eval(&ii->penv, c->ppos, 32, 1, &ok);
                if (ok) { ep = clampi(ep, 0, 64); int pn = fpan; if (pn >= 128) pn += ((ep - 32) * (256 - pn)) / 32; else pn += ((ep - 32) * pn) / 32; fpan = pn; }
                if (c->flags & F_FADE) {
                    if (ii->fadeout) { c->fadeout -= ii->fadeout << 6; if (c->fadeout < 0) c->fadeout = 0; vol = (int)(((int64_t)vol * c->fadeout) >> 16); }
                    else if (!c->fadeout) vol = 0;
                }
            } else if (c->flags & F_FADE) { c->fadeout = 0; vol = 0; }
            int64_t fv = vol ? ((int64_t)vol * p->gvol * c->chvol * clampi(c->instvol, 0, 64)) >> 19 : 0;
            double f = c->freq;
            if ((c->flags & (F_GLISS | F_PORTA)) == (F_GLISS | F_PORTA)) f = freq_from_note(note_from_freq(f, c->c5), c->c5);
            if (c->ncmd == 10) {
                int rt = p->speed + p->frame_delay - p->tick_count, tk = rt % (p->speed + p->frame_delay), a = 0;
                if (tk % 3 == 1) a = c->m_arp >> 4; else if (tk % 3 == 2) a = c->m_arp & 15;
                if (a) f *= pow(2.0, a / 12.0);
            }
            if (n < 64 && p->firsttick && c->row_cmd == 26) {
                int zp = c->row_par;
                if (zp < 0x80) { c->cutoff = zp; setup_filter(p, c, !(c->flags & F_FILT), 256); }
                else if (zp < 0x90) { c->resonance = (zp & 15) * 8; setup_filter(p, c, !(c->flags & F_FILT), 256); }
            }
            int envpitch_f = 0;
            if ((s->flags & 4) && c->ins && (c->ins->pitchenv.flg & 0x80)) {
                int ok; int et = env_eval(&c->ins->pitchenv, c->tpos, 0, 8, &ok); if (ok) envpitch_f = clampi(et, -256, 256);
            }
            if (c->flags & F_NEWNOTE) setup_filter(p, c, 1, 256);
            if ((s->flags & 4) && c->ins && (c->ins->pitchenv.flg & 0x80)) setup_filter(p, c, !(c->flags & F_FILT), envpitch_f);
            c->flags &= ~F_NEWNOTE;
            if ((s->flags & 4) && c->ins && !(c->ins->pitchenv.flg & 0x80)) {
                int ok; int et = env_eval(&c->ins->pitchenv, c->tpos, 0, 8, &ok);
                if (ok) { et = clampi(et, -256, 256); f *= pow(2.0, et / 192.0); }
            }
            if (c->flags & F_VIB) {
                int d = wave(c->vibtype, c->vibpos & 255), depth;
                if (s->flags & 16) { depth = 5; d = -d; } else depth = 6;
                d = (d * c->vibdepth) >> depth;
                f = freq_slide(p, f, d, 0);
                if (!p->firsttick || !(s->flags & 16)) c->vibpos = (c->vibpos + 4 * c->vibspeed) & 255;
            }
            if (c->smp->vid && c->smp->vis) {
                int vp = c->avpos & 255, ad = 1;
                ad = c->avdepth; ad += c->smp->vir & 0xFF; if (ad > (c->smp->vid << 8)) ad = c->smp->vid << 8; c->avdepth = ad; ad >>= 8;
                c->avpos += c->smp->vis;
                int d = wave(c->smp->vit, vp); d = (d * ad) >> 6;
                f = f * pow(2.0, d / 768.0);
            }
            c->step = (getenv("ITNOFLOOR") ? f : floor(f)) / p->rate; if (c->step <= 0) c->step = 1e-6;
            fpan = clampi(fpan, 0, 256);
            double pan = (double)(fpan - 128) * s->sep / 128.0 + 128.0; pan = pan < 0 ? 0 : pan > 256 ? 256 : pan;
            double real = (double)fv * master / 128.0;
            if (c->flags & F_SURR) { c->gl = real * 0.5; c->gr = real * 0.5; }
            else { c->gl = real * (256.0 - pan) / 256.0; c->gr = real * pan / 256.0; }
            c->gl *= p->K; c->gr *= p->K;
            c->active = 1;
            if (n >= 64 && (c->flags & F_FADE) && c->fadeout == 0) { c->len = 0; c->active = 0; c->gl = c->gr = 0; }
        } else { c->active = 0; if (n >= 64) c->len = 0; }
    }
}

/* ---------------- Mixer ---------------- */
static inline double samp(const ITSmp *m, uint32_t i){ return m->is16 ? ((const int16_t *)m->data)[i] : ((const int8_t *)m->data)[i] * 256.0; }
static inline double samp2(const ITSmp *m, uint32_t i){ return m->is16 ? ((const int16_t *)m->data2)[i] : ((const int8_t *)m->data2)[i] * 256.0; }

static void mix_voice(ITPlayer *p, V *c, double *mix, int n, int fast_ramp_len, float *sc){
    if (!c->smp || !c->smp->data || !c->len) return;
    if (!c->active && c->pgl == 0 && c->pgr == 0) return;
    double step = c->active ? c->step : 0.0;
    int rl = n;
    if (!(c->flags & F_FAST) && (c->pgl != 0 || c->pgr != 0) && (c->gl != 0 || c->gr != 0)) rl = n; else rl = fast_ramp_len < n ? fast_ramp_len : n;
    if (rl < 1) rl = 1;
    const ITSmp *m = c->smp; double dl = (c->gl - c->pgl) / rl, dr = (c->gr - c->pgr) / rl, gl = c->pgl, gr = c->pgr;
    int dir = (c->flags & F_PPREV) ? -1 : 1;
    for (int i = 0; i < n; i++) {
        if (i < rl) { gl += dl; gr += dr; } else { gl = c->gl; gr = c->gr; }
        if (!c->len) break;
        uint32_t idx = (uint32_t)c->pos; double fr = c->pos - idx;
        double s0 = samp(m, idx), s1;
        uint32_t nx = idx + 1;
        if (nx >= c->len) { if (c->flags & F_LOOP) nx = (c->flags & F_PP) ? idx : c->ls; else nx = idx; }
        s1 = samp(m, nx < m->len ? nx : idx);
        double v = s0 + (s1 - s0) * fr;
        if (c->flags & F_FILT) { double t = c->fa0 * v + c->fb0 * c->fy[0][0] + c->fb1 * c->fy[0][1]; c->fy[0][1] = c->fy[0][0]; c->fy[0][0] = t; v = t; }
        if (m->data2) {
            double r0 = samp2(m, idx), r1 = samp2(m, nx < m->len ? nx : idx), vr = r0 + (r1 - r0) * fr;
            if (c->flags & F_FILT) { double t = c->fa0 * vr + c->fb0 * c->fy[1][0] + c->fb1 * c->fy[1][1]; c->fy[1][1] = c->fy[1][0]; c->fy[1][0] = t; vr = t; }
            mix[2 * i] += v * gl; mix[2 * i + 1] += vr * gr;
            if (sc) sc[i] += (float)((v * gl + vr * gr) * 0.5 / 32768.0);
        } else { mix[2 * i] += v * gl; mix[2 * i + 1] += v * gr; if (sc) sc[i] += (float)((v * gl + v * gr) * 0.5 / 32768.0); }
        c->pos += dir * step;
        if (dir > 0) {
            if (c->pos >= c->len) {
                if (c->flags & F_LOOP) {
                    double ll = (double)c->le - c->ls;
                    if (c->flags & F_PP) { c->pos = 2.0 * c->len - c->pos - (g_it200 ? 0.0 : 1.0) - 1e-9;   /* gemessen: Umkehr oben bei le-0.5 (libopenmpt), unten bei ls */ c->flags ^= F_PPREV; dir = -dir; if (c->pos < c->ls) c->pos = c->ls; }
                    else { c->pos = c->ls + fmod(c->pos - c->ls, ll); }
                } else { c->len = 0; c->pos = 0; }
            }
        } else {
            if (c->pos < (double)c->ls && (c->flags & F_LOOP)) { c->pos = 2.0 * c->ls - c->pos; c->flags ^= F_PPREV; dir = -dir; if (c->pos >= c->len) c->pos = c->len - 1e-9; }
            else if (c->pos < 0) { c->len = 0; c->pos = 0; }
        }
    }
    (void)p;
}

/* ---------------- Oeffentliche API ---------------- */
ITPlayer *itp_new(const ITSong *s, int rate){
    ITPlayer *p = calloc(1, sizeof *p); p->s = s; p->rate = rate;
    g_it200 = (s->cmwt == 0x200 && (s->cwt == 0x214 || s->cwt == 0x217 || s->cwt == 0x888)); g_linear = (s->flags & 8) != 0; g_swapcancel = (s->cmwt == 0x200 && (s->cwt == 0x214 || s->cwt == 0x217)) || s->cwt == 0x888 || s->cwt >= 0x1000; p->speed = s->speed; p->tempo = s->tempo; p->gvol = s->gv;
    p->tick_count = 1; p->row_count = 0; p->process_row = NEXT_ORDER; p->process_order = -1; p->break_row = 0;
    p->visited = calloc((size_t)(s->nord + 1) * 256, 1);
    p->K = 1.0 / 65536.0;     /* gegen libopenmpt kalibriert (Faktor 4 gegenueber Schism-Skalierung) */
    { const char *e = getenv("ITGAIN"); if (e) p->K *= atof(e); }
    for (int i = 0; i < 64; i++) {
        V *c = &p->ch[i];
        int cp = s->chpan[i]; c->chvol = s->chvol[i] > 64 ? 64 : s->chvol[i];
        if (cp & 128) c->flags |= F_MUTE;
        if ((cp & 127) == 100) { c->flags |= F_SURR; c->pan = 128; } else c->pan = (cp & 127) > 64 ? 128 : (cp & 127) * 4;
        c->cutoff = 0x7F; c->resonance = 0; c->vol = 256; c->vibtype = c->tremtype = c->pbtype = 0; c->fadeout = 65536; c->instvol = 64;
    }
    p->jumped = 0; p->solo = getenv("ITSOLO") ? atoi(getenv("ITSOLO")) : -1;
    return p;
}
void itp_pos(const ITPlayer *p, int *ord, int *pat, int *row){ *ord = p->cur_order; *pat = p->cur_pat; *row = p->row; }
void itp_set_scope(ITPlayer *p, float *buf, int len){ p->scb = buf; p->scl = len; p->scn = 0; }
int itp_chan_pan(const ITPlayer *p, int ch){ int v = p->ch[ch].pan; return v > 255 ? 255 : v < 0 ? 0 : v; }
unsigned itp_scope_pos(const ITPlayer *p){ return p->scn; }
void itp_free(ITPlayer *p){ if (p) { free(p->visited); free(p->tkbuf); free(p->tkout); free(p->tksc); free(p); } }

int itp_render(ITPlayer *p, int16_t *out, int n){
    int done = 0;
    while (done < n) {
        if (p->tk_left == 0) {
            if (p->ended) break;
            if (!process_tick(p)) { p->ended = 1; break; }
            if (p->firsttick && getenv("ITPOS")) fprintf(stderr, "POS %.2f %d %d sp%d tp%d\n", (double)p->nsamp / p->rate, p->cur_order, p->row, p->speed, p->tempo);
            update_channels(p);
            p->tk_len = p->tk_left = (int)(p->rate * 5 / (p->tempo * 2)); if (p->tk_left < 1) p->tk_left = p->tk_len = 1;
            if (p->tkcap < p->tk_len * 2) { p->tkcap = p->tk_len * 2; p->tkbuf = realloc(p->tkbuf, sizeof(double) * p->tkcap); p->tkout = realloc(p->tkout, sizeof(int16_t) * p->tkcap); }
            memset(p->tkbuf, 0, sizeof(double) * 2 * p->tk_len);
            if (p->scb) { if (p->tkscap < p->tk_len) { p->tkscap = p->tk_len; p->tksc = realloc(p->tksc, sizeof(float) * 64 * (size_t)p->tkscap); } memset(p->tksc, 0, sizeof(float) * 64 * (size_t)p->tk_len); }
            p->nsamp += p->tk_len;
            for (int i = 0; i < 192; i++) { if ((p->solo < 0 || i == p->solo || (i >= 64 && p->ch[i].master == p->solo + 1)) && !((xplay_mute >> (i < 64 ? i : (p->ch[i].master > 0 && p->ch[i].master <= 64 ? p->ch[i].master - 1 : 63))) & 1)) mix_voice(p, &p->ch[i], p->tkbuf, p->tk_len, 32, p->scb ? p->tksc + (size_t)(i < 64 ? i : (p->ch[i].master > 0 && p->ch[i].master <= 64 ? p->ch[i].master - 1 : 63)) * p->tk_len : NULL); p->ch[i].pgl = p->ch[i].gl; p->ch[i].pgr = p->ch[i].gr; }
            for (int i = 0; i < 2 * p->tk_len; i++) { double v = p->tkbuf[i]; v = v > 32767 ? 32767 : v < -32768 ? -32768 : v; p->tkout[i] = (int16_t)lrint(v); }
        }
        int k = p->tk_left < n - done ? p->tk_left : n - done, off = p->tk_len - p->tk_left;
        memcpy(out + 2 * done, p->tkout + 2 * off, sizeof(int16_t) * 2 * k);
        if (p->scb) for (int j = 0; j < k; j++) { unsigned w = p->scn & (unsigned)(p->scl - 1); for (int c = 0; c < 64; c++) p->scb[(size_t)c * p->scl + w] = p->tksc[(size_t)c * p->tk_len + off + j]; p->scn++; }
        p->tk_left -= k; done += k;
    }
    return done;
}
