/* DMF-Replayer (X-Tracker). Semantik nach oeffentlicher Beschreibung / Referenzvergleich. GPL-3.0-or-later */
#include "dmf.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern unsigned long long xplay_mute;   /* Bit n = Kanal n stumm (alle Formate) */

typedef struct {
    const DMFSmp *smp; double pos, step; int on, key;
    int note, lastnote, lastinst, vol, pan;       /* vol 0..256, pan 0..255 */
    double pitch;                                 /* Oktaven-Offset durch Slides */
    int nec, nep, vec, vep, tgt, buf, porta_p, vsl_p, pan_p, arp_p;
    double gl, gr, pgl, pgr;
} Ch;

struct DMFPlayer {
    const DMFSong *s; int rate;
    int ord, row, tick, speed, tempo, done;
    int mode_bpm, T, B, beat;
    double tick_len, acc;
    Ch ch[32];
    float *scb; int scl; unsigned scn;
    float *tl, *tr, *tsc; int tcap, tlen, toff;
    float gain;
};

static void set_timing(DMFPlayer *p){
    int tickspeed = p->mode_bpm ? p->B * p->beat * 2 : (p->T + 1) * 30;
    if (tickspeed < 1) tickspeed = 1;
    int speed, tempo = 0;
    for (speed = 255; speed >= 1; speed--) { tempo = tickspeed * speed / 48; if (tempo >= 32 && tempo <= 255) break; }
    if (speed < 1) { speed = 1; tempo = tickspeed / 48; if (tempo < 32) tempo = 32; if (tempo > 255) tempo = 255; }
    p->speed = speed; p->tempo = tempo;
    p->tick_len = p->rate * 2.5 / tempo;
}

static double note_step(const DMFSmp *m, int note, int rate){
    return (m->c3spd ? m->c3spd : 8363) * pow(2.0, (note - 37) / 12.0) / rate;
}
static double step_of(const Ch *c, int rate, double extra_semi){
    const DMFSmp *m = c->smp; double f = (m->c3spd ? m->c3spd : 8363) * pow(2.0, (c->note - 37 + extra_semi) / 12.0 + c->pitch);
    return f / rate;
}

static int pat_of(const DMFSong *s, int ord){ if (ord < 0 || ord >= s->nord) return -1; int pn = s->order[ord]; return pn < s->npat ? pn : -1; }

DMFPlayer *dmp_new(const DMFSong *s, int rate){
    DMFPlayer *p = calloc(1, sizeof *p); p->s = s; p->rate = rate;
    p->T = 31; p->mode_bpm = 0; p->B = 0; p->beat = 0;
    p->gain = 0.375f;
    set_timing(p);
    for (int i = 0; i < 32; i++) p->ch[i].pan = 128;
    p->tick = p->speed;      /* erste Zeile sofort */
    p->ord = 0; p->row = -1;
    return p;
}
void dmp_free(DMFPlayer *p){ if (!p) return; free(p->tl); free(p->tr); free(p->tsc); free(p); }
void dmp_set_scope(DMFPlayer *p, float *buf, int len){ p->scb = buf; p->scl = len; p->scn = 0; }
unsigned dmp_scope_pos(const DMFPlayer *p){ return p->scn; }
int dmp_chan_pan(const DMFPlayer *p, int ch){ return ch >= 0 && ch < 32 ? p->ch[ch].pan : 128; }
void dmp_pos(const DMFPlayer *p, int *ord, int *pat, int *row){ if (ord) *ord = p->ord; if (pat) *pat = pat_of(p->s, p->ord); if (row) *row = p->row < 0 ? 0 : p->row; }

static void trigger(DMFPlayer *p, Ch *c, int inst, int note){
    const DMFSong *s = p->s;
    if (inst < 1 || inst > s->nsmp || !s->smp[inst].data || !s->smp[inst].len) { c->on = 0; return; }
    const DMFSmp *m = &s->smp[inst];
    c->smp = m; c->pos = 0; c->on = 1; c->key = 1; c->note = note; c->lastnote = note; c->lastinst = inst;
    c->step = note_step(m, note, p->rate);
    c->vol = m->vol == 0 ? 256 : m->vol + 1; if (c->vol > 256) c->vol = 256;
}

static void do_row(DMFPlayer *p){
    const DMFSong *s = p->s;
    int pn = pat_of(s, p->ord); if (pn < 0) { p->done = 1; return; }
    const DMFPat *pt = &s->pat[pn];
    if (p->row == 0 && pt->beat) { p->beat = pt->beat; if (p->B) p->mode_bpm = 1; set_timing(p); }
    /* globale Spur */
    int gc = pt->gcmd[p->row], gp = pt->gpar[p->row], retime = 0;
    switch (gc) {
    case 1: p->T = gp > 1 ? gp : 1; p->mode_bpm = 0; retime = 1; break;
    case 2: if (gp && p->beat) { p->B = gp; p->mode_bpm = 1; retime = 1; } else if (gp) { p->B = gp; } break;
    case 3: p->beat = gp >> 4; if (!p->beat) p->mode_bpm = 0; else if (p->B) p->mode_bpm = 1; retime = 1; break;
    case 6: if (p->mode_bpm) { p->B += gp; if (p->B > 255) p->B = 255; } else { p->T += gp; if (p->T > 255) p->T = 255; } retime = 1; break;
    case 7: if (p->mode_bpm) { p->B -= gp; if (p->B < 1) p->B = 1; } else { p->T -= gp; if (p->T < 1) p->T = 1; } retime = 1; break;
    }
    if (retime) set_timing(p);
    for (int ci = 0; ci < pt->ntr && ci < 32; ci++) {
        const DMFCell *x = &pt->cell[(size_t)p->row * 32 + ci]; Ch *c = &p->ch[ci];
        int inst = x->inst, note = x->note;
        int hasn = (x->has & 0x20) != 0, hasi = (x->has & 0x40) != 0, hasv = (x->has & 0x10) != 0;
        c->nec = (x->has & 4) ? x->ne_cmd : 0; c->nep = x->ne_par;
        c->vec = (x->has & 2) ? x->ve_cmd : 0; c->vep = x->ve_par;
        int ic = (x->has & 8) ? x->ie_cmd : 0, ip = x->ie_par;
        if (hasn && note >= 129 && note <= 236) { c->buf = note & 0x7F; note = 0; hasn = 0; }
        int trig = 0;
        if (hasn && note == 255) { c->on = 0; }
        else if (hasn && note >= 1 && note <= 108) {
            if (hasi && inst && !(c->nec == 6 && c->on)) { trigger(p, c, inst, note); trig = 1; }   /* Note+Instrument+Tonportamento: Slide ohne Neustart */
            else if (c->nec == 6) c->tgt = note;
            else if (c->smp) { c->note = note; c->lastnote = note; c->pitch = 0; c->step = step_of(c, p->rate, 0); }
        } else if (hasi && inst && c->lastnote) { trigger(p, c, c->lastinst, c->lastnote); trig = 1; }
        if (c->nec == 6 && !c->tgt && c->buf) c->tgt = c->buf;
        if (trig) c->pitch = 0;
        if (hasv) c->vol = ((x->vol + 2) / 4) * 4;
        /* Instrument-Effekte */
        if (ic == 1) c->on = 0;
        else if (ic == 2) c->key = 0;
        else if (ic == 3 && c->lastnote) { trigger(p, c, c->lastinst, c->lastnote); c->pitch = 0; }
        else if (ic >= 6 && ic <= 9) {
            if (!trig && !hasn && c->lastnote) { trigger(p, c, c->lastinst, c->lastnote); c->pitch = 0; }
            if (c->on && c->smp) { double off = (double)((ic - 6) * 65536 + ip * 256); if (off < c->smp->len) c->pos = off; else c->on = 0; }
        }
        if (c->vec == 7) c->pan = x->ve_par;
        if (c->smp) c->step = step_of(c, p->rate, 0);
    }
}

static void tick_fx(DMFPlayer *p, int tick){
    int ticks = p->speed;
    for (int ci = 0; ci < 32; ci++) {
        Ch *c = &p->ch[ci]; if (!c->smp) continue;
        double semi = 0;
        switch (c->nec) {
        case 3: { int pr = c->nep ? c->nep : c->arp_p; c->arp_p = pr; if (tick % 3 == 1) semi = pr >> 4; else if (tick % 3 == 2) semi = pr & 15; break; }
        case 4: case 5: {
            int pr = c->nep ? c->nep : c->porta_p; c->porta_p = pr; double dir = c->nec == 4 ? 1 : -1;
            if (pr <= 15 || ticks < 2) { if (tick == 0) c->pitch += dir * pr / 192.0; }
            else if (tick > 0) { int n = pr / (ticks - 1); if (n < 1) n = 1; { if (n > 255) n = 255; c->pitch += dir * n / 192.0; } }
            break; }
        case 6: if (tick > 0 && c->tgt) {
            int pr = c->nep ? c->nep : c->porta_p; c->porta_p = pr; int n = ticks >= 2 ? pr / (ticks - 1) : pr; if (n < 1) n = 1;
            double cur = (c->note - 37) / 12.0 + c->pitch, tg = (c->tgt - 37) / 12.0, d = n / 192.0;
            if (cur < tg) { cur += d; if (cur > tg) cur = tg; } else { cur -= d; if (cur < tg) cur = tg; }
            c->pitch = cur - (c->note - 37) / 12.0; }
            break;
        case 7: if (tick == 0 && c->tgt) { c->pitch = (c->tgt - c->note) / 12.0; } break;
        }
        if (c->vec == 1 || c->vec == 2 || c->vec == 8 || c->vec == 9) {
            int pr = c->vep ? c->vep : (c->vec <= 2 ? c->vsl_p : c->pan_p);
            if (c->vec <= 2) c->vsl_p = pr; else c->pan_p = pr;
            int v = pr / 4; if (v < 1) v = 1;
            if (v >= 15 && ticks >= 2) { v = (v + ticks - 2) / (ticks - 1); if (v < 1) v = 1; }
            if (v > 15) v = 15;
            int run = ticks < 2 ? tick == 0 : tick > 0;
            if (run) {
                if (c->vec == 1) { c->vol += v * 4; if (c->vol > 256) c->vol = 256; }
                else if (c->vec == 2) { c->vol -= v * 4; if (c->vol < 0) c->vol = 0; }
                else if (c->vec == 8) { c->pan -= v * 4; if (c->pan < 0) c->pan = 0; }
                else { c->pan += v * 4; if (c->pan > 255) c->pan = 255; }
            }
        }
        c->step = step_of(c, p->rate, semi);
    }
}

static void next_pos(DMFPlayer *p){
    const DMFSong *s = p->s;
    int pn = pat_of(s, p->ord);
    p->row++;
    int nrows = pn >= 0 ? s->pat[pn].nrows : 0;
    if (p->row >= nrows) {
        p->row = 0;
        int lastord = s->loop_end > 0 && s->loop_end < s->nord ? s->loop_end : s->nord - 1;
        if (p->ord >= lastord) { p->done = 1; return; }
        p->ord++;
        while (p->ord < s->nord && pat_of(s, p->ord) < 0) { if (p->ord >= lastord) { p->done = 1; return; } p->ord++; }
    }
}

static void gen_tick(DMFPlayer *p, int n){
    if (n > p->tcap) { p->tcap = n; p->tl = realloc(p->tl, sizeof(float) * (size_t)n); p->tr = realloc(p->tr, sizeof(float) * (size_t)n); p->tsc = realloc(p->tsc, sizeof(float) * 32 * (size_t)n); }
    memset(p->tl, 0, sizeof(float) * (size_t)n); memset(p->tr, 0, sizeof(float) * (size_t)n); memset(p->tsc, 0, sizeof(float) * 32 * (size_t)n);
    const float mg = p->gain / 256.0f;
    for (int ci = 0; ci < 32; ci++) {
        Ch *c = &p->ch[ci]; const DMFSmp *m = c->smp;
        double tgl = 0, tgr = 0;
        if (c->on && m) { double v = c->vol / 256.0; tgl = v * (256 - c->pan) / 256.0; tgr = v * c->pan / 256.0; }
        c->pgl = c->gl; c->pgr = c->gr; c->gl = tgl; c->gr = tgr;
        if (!c->on || !m) continue;
        const int16_t *d16 = m->data; const int8_t *d8 = m->data;
        double rl = 1.0 / (n > 64 ? 64 : n);
        const int mu = (int)((xplay_mute >> ci) & 1);
        for (int i = 0; i < n && c->on; i++) {
            uint32_t i0 = (uint32_t)c->pos; double fr = c->pos - i0;
            uint32_t i1 = i0 + 1; if (m->loop && i1 >= m->le) i1 = m->lb + (i1 - m->le);
            double s0, s1;
            if (m->is16) { s0 = d16[i0] / 32768.0; s1 = i1 < m->len ? d16[i1] / 32768.0 : s0; }
            else { s0 = d8[i0] / 128.0; s1 = i1 < m->len ? d8[i1] / 128.0 : s0; }
            double sm = s0 + (s1 - s0) * fr;
            double rf = i * rl; if (rf > 1) rf = 1;
            double gl = c->pgl + (c->gl - c->pgl) * rf, gr = c->pgr + (c->gr - c->pgr) * rf;
            if (!mu) {
                p->tl[i] += (float)(sm * gl * 256.0) * mg; p->tr[i] += (float)(sm * gr * 256.0) * mg;
                p->tsc[(size_t)ci * n + i] = (float)(sm * (gl + gr) * 256.0) * mg;
            }
            c->pos += c->step;
            if (m->loop && c->key) { while (c->pos >= m->le) c->pos -= (m->le - m->lb); }
            else if (c->pos >= m->len) c->on = 0;
        }
    }
    p->tlen = n; p->toff = 0;
}

int dmp_render(DMFPlayer *p, int16_t *out, int nframes){
    int done = 0;
    while (done < nframes) {
        if (p->toff >= p->tlen) {
            if (p->done) break;
            if (p->tick >= p->speed) {
                p->tick = 0; next_pos(p);
                if (p->done) break;
                do_row(p);
                if (p->done) break;
            }
            int n = (int)p->tick_len; if (n < 1) n = 1;   /* wie libopenmpt: ganze Samples je Tick */
            tick_fx(p, p->tick); gen_tick(p, n); p->tick++;
        }
        int k = p->tlen - p->toff; if (k > nframes - done) k = nframes - done;
        for (int i = 0; i < k; i++) {
            float l = p->tl[p->toff + i], r = p->tr[p->toff + i];
            l *= 32768.0f; r *= 32768.0f;
            out[2 * (done + i)] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            out[2 * (done + i) + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            if (p->scb) { unsigned w = p->scn & (unsigned)(p->scl - 1); for (int c = 0; c < 32; c++) p->scb[(size_t)c * p->scl + w] = p->tsc[(size_t)c * p->tlen + p->toff + i]; p->scn++; }
        }
        p->toff += k; done += k;
    }
    return done;
}
