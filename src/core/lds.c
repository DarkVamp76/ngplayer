/* LOUDNESS Sound System: loader and replayer, C port of the AdPlug replayer. GPL-3.0-or-later */
#include "lds.h"
#include <stdlib.h>
#include <string.h>

#define MAXSOUND 0x3f
#define MAXPOS 0xff

static const uint8_t op_table[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
static const uint16_t frequency[192] = {
    343, 344, 345, 347, 348, 349, 350, 352, 353, 354, 356, 357, 358, 359, 361, 362, 363, 365, 366, 367, 369, 370, 371, 373, 374, 375,
    377, 378, 379, 381, 382, 384, 385, 386, 388, 389, 391, 392, 393, 395, 396, 398, 399, 401, 402, 403, 405, 406, 408, 409, 411, 412,
    414, 415, 417, 418, 420, 421, 423, 424, 426, 427, 429, 430, 432, 434, 435, 437, 438, 440, 442, 443, 445, 446, 448, 450, 451, 453,
    454, 456, 458, 459, 461, 463, 464, 466, 468, 469, 471, 473, 475, 476, 478, 480, 481, 483, 485, 487, 488, 490, 492, 494, 496, 497,
    499, 501, 503, 505, 506, 508, 510, 512, 514, 516, 518, 519, 521, 523, 525, 527, 529, 531, 533, 535, 537, 538, 540, 542, 544, 546,
    548, 550, 552, 554, 556, 558, 560, 562, 564, 566, 568, 571, 573, 575, 577, 579, 581, 583, 585, 587, 589, 591, 594, 596, 598, 600,
    602, 604, 607, 609, 611, 613, 615, 618, 620, 622, 624, 627, 629, 631, 633, 636, 638, 640, 643, 645, 647, 650, 652, 654, 657, 659,
    662, 664, 666, 669, 671, 674, 676, 678, 681, 683 };
static const uint8_t vibtab[64] = {
    0, 13, 25, 37, 50, 62, 74, 86, 98, 109, 120, 131, 142, 152, 162, 171, 180, 189, 197, 205, 212, 219, 225, 231, 236, 240, 244, 247, 250, 252, 254, 255,
    255, 255, 254, 252, 250, 247, 244, 240, 236, 231, 225, 219, 212, 205, 197, 189, 180, 171, 162, 152, 142, 131, 120, 109, 98, 86, 74, 62, 50, 37, 25, 13 };
static const uint8_t tremtab[128] = {
    0, 0, 1, 1, 2, 4, 5, 7, 10, 12, 15, 18, 21, 25, 29, 33, 37, 42, 47, 52, 57, 62, 67, 73, 79, 85, 90, 97, 103, 109, 115, 121, 128, 134,
    140, 146, 152, 158, 165, 170, 176, 182, 188, 193, 198, 203, 208, 213, 218, 222, 226, 230, 234, 237, 240, 243, 245, 248, 250, 251,
    253, 254, 254, 255, 255, 255, 254, 254, 253, 251, 250, 248, 245, 243, 240, 237, 234, 230, 226, 222, 218, 213, 208, 203, 198, 193,
    188, 182, 176, 170, 165, 158, 152, 146, 140, 134, 127, 121, 115, 109, 103, 97, 90, 85, 79, 73, 67, 62, 57, 52, 47, 42, 37, 33, 29,
    25, 21, 18, 15, 12, 10, 7, 5, 4, 2, 1, 1, 0 };

/* ---------------- loader ---------------- */
typedef struct { const uint8_t *b; size_t n, p; int bad; } Rd;
static unsigned r8(Rd *r) { if (r->p >= r->n) { r->bad = 1; return 0; } return r->b[r->p++]; }
static unsigned r16(Rd *r) { unsigned a = r8(r); return a | (r8(r) << 8); }

void lds_free(LdsSong *s) { if (!s) return; free(s->patch); free(s->pos); free(s->pat); free(s); }

LdsSong *lds_load(const uint8_t *buf, size_t len, int no_midi, int *err) {
    int e = 0; if (!err) err = &e; *err = 0;
    Rd r = { buf, len, 0, 0 };
    LdsSong *s = calloc(1, sizeof *s); if (!s) { *err = -5; return NULL; }
    s->no_midi = no_midi;
    s->mode = (int)r8(&r); if (s->mode > 2) { free(s); *err = -1; return NULL; }
    s->speed = (uint16_t)r16(&r); s->tempo = (int)r8(&r); s->pattlen = (int)r8(&r);
    for (int i = 0; i < 9; i++) s->chandelay[i] = (uint8_t)r8(&r);
    s->regbd = (uint8_t)r8(&r);
    s->npatch = r16(&r);
    size_t psz = no_midi ? 40 : 46;
    if (r.bad || s->speed == 0 || s->pattlen == 0 || s->npatch == 0 || s->npatch > 0xFFF || r.p + s->npatch * psz > len) { lds_free(s); *err = -2; return NULL; }
    s->npatch_alloc = s->npatch > MAXSOUND + 1 ? s->npatch : MAXSOUND + 1;          /* the original keeps 64 instrument slots, the unused ones are empty */
    s->patch = calloc(s->npatch_alloc, sizeof(LdsPatch)); if (!s->patch) { lds_free(s); *err = -5; return NULL; }
    for (unsigned i = 0; i < s->npatch; i++) {
        LdsPatch *p = &s->patch[i];
        p->mod_misc = (uint8_t)r8(&r); p->mod_vol = (uint8_t)r8(&r); p->mod_ad = (uint8_t)r8(&r); p->mod_sr = (uint8_t)r8(&r); p->mod_wave = (uint8_t)r8(&r);
        p->car_misc = (uint8_t)r8(&r); p->car_vol = (uint8_t)r8(&r); p->car_ad = (uint8_t)r8(&r); p->car_sr = (uint8_t)r8(&r); p->car_wave = (uint8_t)r8(&r);
        p->feedback = (uint8_t)r8(&r); p->keyoff = (uint8_t)r8(&r); p->portamento = (uint8_t)r8(&r); p->glide = (uint8_t)r8(&r); p->finetune = (uint8_t)r8(&r);
        p->vibrato = (uint8_t)r8(&r); p->vibdelay = (uint8_t)r8(&r); p->mod_trem = (uint8_t)r8(&r); p->car_trem = (uint8_t)r8(&r); p->tremwait = (uint8_t)r8(&r);
        p->arpeggio = (uint8_t)r8(&r);
        for (int j = 0; j < 12; j++) p->arp_tab[j] = (uint8_t)r8(&r);
        p->start = (uint16_t)r16(&r); p->size = (uint16_t)r16(&r); p->fms = (uint8_t)r8(&r); p->transp = (uint16_t)r16(&r);
        if (!no_midi) { p->midinst = (uint8_t)r8(&r); p->midvelo = (uint8_t)r8(&r); p->midkey = (uint8_t)r8(&r); p->midtrans = (uint8_t)r8(&r); p->middum1 = (uint8_t)r8(&r); p->middum2 = (uint8_t)r8(&r); }
    }
    s->nposi = r16(&r);
    if (r.bad || s->nposi == 0 || s->nposi > 0x100 || r.p + (size_t)s->nposi * 27 > len) { lds_free(s); *err = -3; return NULL; }
    s->pos = calloc((size_t)s->nposi * 9, sizeof(LdsPos)); if (!s->pos) { lds_free(s); *err = -5; return NULL; }
    for (unsigned i = 0; i < s->nposi; i++) for (int j = 0; j < 9; j++) { s->pos[i * 9 + j].patnum = (uint16_t)(r16(&r) / 2); s->pos[i * 9 + j].transpose = (uint8_t)r8(&r); }
    (void)r16(&r);                                        /* the number of digital sounds, not played */
    if (r.bad) { lds_free(s); *err = -3; return NULL; }
    s->npat = (unsigned)((len - r.p) / 2);
    s->pat = calloc(s->npat + 1, sizeof(uint16_t)); if (!s->pat) { lds_free(s); *err = -5; return NULL; }
    for (unsigned i = 0; i < s->npat; i++) s->pat[i] = (uint16_t)r16(&r);
    return s;
}

double lds_refresh(const LdsSong *s) { return (double)(1193182.0f / (float)s->speed); }

/* ---------------- pattern view ---------------- */
int lds_cell(const LdsSong *s, int order, int row, int chan, LdsCell *c) {
    memset(c, 0, sizeof *c);
    if (order < 0 || (unsigned)order >= s->nposi || chan < 0 || chan >= 9 || row < 0 || row >= s->pattlen) return 0;
    unsigned patnum = s->pos[order * 9 + chan].patnum; unsigned char transpose = s->pos[order * 9 + chan].transpose;
    unsigned packpos = 0; int wait = 0;
    for (int r = 0; r <= row; r++) {
        if (wait) { wait--; continue; }
        unsigned short w = (patnum + packpos) < s->npat ? s->pat[patnum + packpos] : 0x8001;
        unsigned char hi = w >> 8, lo = w & 0xFF;
        packpos++;
        if (!w) continue;
        if (hi == 0x80) { wait = lo; continue; }
        if (r != row) continue;
        if (hi >= 0x80) { c->kind = 2; c->cmd = hi; c->param = lo; return 1; }
        signed char tr = (signed char)(transpose & 127); if (transpose & 64) tr |= (signed char)0x80;
        int n, snd;
        if (transpose & 128) { snd = (lo + tr) & MAXSOUND; n = hi; } else { snd = lo & MAXSOUND; n = hi + tr; }
        c->kind = 1; c->note = n; c->inst = snd; return 1;
    }
    return 0;
}

/* ---------------- player ---------------- */
typedef struct {
    uint16_t gototune, lasttune, packpos;
    uint8_t finetune, glideto, portspeed, nextvol, volmod, volcar, vibwait, vibspeed, vibrate, trmstay, trmwait, trmspeed, trmrate, trmcount, trcwait, trcspeed, trcrate,
            trccount, arp_size, arp_speed, keycount, vibcount, arp_pos, arp_count, packwait, arp_tab[12];
    struct { uint8_t chandelay, sound; uint16_t high; } chancheat;
} Chan;

struct LdsPlayer {
    const LdsSong *S; LdsWrite wr; void *user;
    Chan channel[9];
    uint8_t fmchip[0x100], jumping, fadeonoff, allvolume, hardfade, tempo_now, pattplay, tempo;
    uint16_t posplay, jumppos;
    int playing, songlooped; unsigned mainvolume;
};

static void setregs(LdsPlayer *P, unsigned reg, unsigned val) {
    reg &= 0xFF; val &= 0xFF;
    if (P->fmchip[reg] == val) return;
    P->fmchip[reg] = (uint8_t)val; P->wr(P->user, (int)reg, (int)val);
}
static void setregs_adv(LdsPlayer *P, unsigned reg, unsigned mask, unsigned val) { setregs(P, reg, (P->fmchip[reg & 0xFF] & mask) | val); }

static void playsound(LdsPlayer *P, int inst_number, int channel_number, int tunehigh) {
    Chan *c = &P->channel[channel_number];
    if ((unsigned)inst_number >= P->S->npatch_alloc) return;                       /* (the sound number is masked to 0..63 already) */
    const LdsPatch *i = &P->S->patch[inst_number];
    unsigned regnum = op_table[channel_number];
    uint8_t volcalc, octave; uint16_t freq;

    tunehigh += ((i->finetune + c->finetune + 0x80) & 0xff) - 0x80;
    if (!i->arpeggio) {
        unsigned short arpcalc = (unsigned short)(i->arp_tab[0] << 4);
        if (arpcalc > 0x800) tunehigh = tunehigh - (arpcalc ^ 0xff0) - 16; else tunehigh += arpcalc;
    }
    if (c->glideto != 0) { c->gototune = (uint16_t)tunehigh; c->portspeed = c->glideto; c->glideto = c->finetune = 0; return; }

    setregs(P, 0x20 + regnum, i->mod_misc);
    volcalc = i->mod_vol;
    if (!c->nextvol || !(i->feedback & 1)) c->volmod = volcalc;
    else c->volmod = (uint8_t)((volcalc & 0xc0) | ((((volcalc & 0x3f) * c->nextvol) >> 6)));
    if ((i->feedback & 1) == 1 && P->allvolume != 0) setregs(P, 0x40 + regnum, ((c->volmod & 0xc0) | (((c->volmod & 0x3f) * P->allvolume) >> 8)) ^ 0x3f);
    else setregs(P, 0x40 + regnum, c->volmod ^ 0x3f);
    setregs(P, 0x60 + regnum, i->mod_ad); setregs(P, 0x80 + regnum, i->mod_sr); setregs(P, 0xe0 + regnum, i->mod_wave);

    setregs(P, 0x23 + regnum, i->car_misc);
    volcalc = i->car_vol;
    if (!c->nextvol) c->volcar = volcalc;
    else c->volcar = (uint8_t)((volcalc & 0xc0) | ((((volcalc & 0x3f) * c->nextvol) >> 6)));
    if (P->allvolume) setregs(P, 0x43 + regnum, ((c->volcar & 0xc0) | (((c->volcar & 0x3f) * P->allvolume) >> 8)) ^ 0x3f);
    else setregs(P, 0x43 + regnum, c->volcar ^ 0x3f);
    setregs(P, 0x63 + regnum, i->car_ad); setregs(P, 0x83 + regnum, i->car_sr); setregs(P, 0xe3 + regnum, i->car_wave);
    setregs(P, 0xc0 + channel_number, i->feedback);
    setregs_adv(P, 0xb0 + channel_number, 0xdf, 0);                  /* key off */

    freq = frequency[(unsigned)tunehigh % (12 * 16)];
    octave = (uint8_t)((unsigned)tunehigh / (12 * 16) - 1);
    if (!i->glide) {
        if (!i->portamento || !c->lasttune) {
            setregs(P, 0xa0 + channel_number, freq & 0xff);
            setregs(P, 0xb0 + channel_number, (octave << 2) + 0x20 + (freq >> 8));
            c->lasttune = c->gototune = (uint16_t)tunehigh;
        } else {
            c->gototune = (uint16_t)tunehigh; c->portspeed = i->portamento;
            setregs_adv(P, 0xb0 + channel_number, 0xdf, 0x20);       /* key on */
        }
    } else {
        setregs(P, 0xa0 + channel_number, freq & 0xff);
        setregs(P, 0xb0 + channel_number, (octave << 2) + 0x20 + (freq >> 8));
        c->lasttune = (uint16_t)tunehigh;
        c->gototune = (uint16_t)(tunehigh + ((i->glide + 0x80) & 0xff) - 0x80);
        c->portspeed = i->portamento;
    }

    if (!i->vibrato) c->vibwait = c->vibspeed = c->vibrate = 0;
    else { c->vibwait = i->vibdelay; c->vibspeed = (uint8_t)((i->vibrato >> 4) + 2); c->vibrate = (i->vibrato & 15) + 1; }

    if (!(c->trmstay & 0xf0)) { c->trmwait = (i->tremwait & 0xf0) >> 3; c->trmspeed = i->mod_trem >> 4; c->trmrate = i->mod_trem & 15; c->trmcount = 0; }
    if (!(c->trmstay & 0x0f)) { c->trcwait = (i->tremwait & 15) << 1; c->trcspeed = i->car_trem >> 4; c->trcrate = i->car_trem & 15; c->trccount = 0; }

    c->arp_size = i->arpeggio & 15; c->arp_speed = i->arpeggio >> 4;
    memcpy(c->arp_tab, i->arp_tab, 12);
    c->keycount = i->keyoff;
    c->nextvol = c->glideto = c->finetune = c->vibcount = c->arp_pos = c->arp_count = 0;
}

void lds_rewind(LdsPlayer *P) {
    P->tempo_now = 3; P->playing = 1; P->songlooped = 0;
    P->jumping = P->fadeonoff = P->allvolume = P->hardfade = P->pattplay = 0; P->posplay = P->jumppos = 0; P->mainvolume = 0;
    memset(P->channel, 0, sizeof P->channel); memset(P->fmchip, 0, sizeof P->fmchip);
    P->tempo = (uint8_t)P->S->tempo;
    P->wr(P->user, 1, 0x20); P->wr(P->user, 8, 0); P->wr(P->user, 0xbd, P->S->regbd);
    for (int i = 0; i < 9; i++) {
        P->wr(P->user, 0x20 + op_table[i], 0); P->wr(P->user, 0x23 + op_table[i], 0);
        P->wr(P->user, 0x40 + op_table[i], 0x3f); P->wr(P->user, 0x43 + op_table[i], 0x3f);
        P->wr(P->user, 0x60 + op_table[i], 0xff); P->wr(P->user, 0x63 + op_table[i], 0xff);
        P->wr(P->user, 0x80 + op_table[i], 0xff); P->wr(P->user, 0x83 + op_table[i], 0xff);
        P->wr(P->user, 0xe0 + op_table[i], 0); P->wr(P->user, 0xe3 + op_table[i], 0);
        P->wr(P->user, 0xa0 + i, 0); P->wr(P->user, 0xb0 + i, 0); P->wr(P->user, 0xc0 + i, 0);
    }
}

LdsPlayer *lds_new(const LdsSong *s, LdsWrite wr, void *user) {
    LdsPlayer *P = calloc(1, sizeof *P); if (!P) return NULL;
    P->S = s; P->wr = wr; P->user = user; lds_rewind(P); return P;
}
void lds_free_player(LdsPlayer *P) { free(P); }
double lds_player_refresh(const LdsPlayer *P) { return lds_refresh(P->S); }
int lds_position(const LdsPlayer *P, int *order, int *row, int *speed) {
    if (order) *order = P->posplay; if (row) *row = P->pattplay; if (speed) *speed = P->tempo; return 1;
}

int lds_update(LdsPlayer *P) {
    const LdsSong *S = P->S;
    unsigned short comword, freq, octave, tune, wibc, tremc, arpreg; int vbreak;
    uint8_t level, regnum, comhi, comlo; Chan *c;

    if (!P->playing) return 0;

    if (P->fadeonoff) {                                           /* fading */
        if (P->fadeonoff <= 128) {
            if (P->allvolume > P->fadeonoff || P->allvolume == 0) P->allvolume = (uint8_t)(P->allvolume - P->fadeonoff);
            else {
                P->allvolume = 1; P->fadeonoff = 0;
                if (P->hardfade != 0) { P->playing = 0; P->hardfade = 0; for (int i = 0; i < 9; i++) P->channel[i].keycount = 1; }
            }
        } else {
            if (((P->allvolume + (0x100 - P->fadeonoff)) & 0xff) <= (int)P->mainvolume) P->allvolume = (uint8_t)(P->allvolume + 0x100 - P->fadeonoff);
            else { P->allvolume = (uint8_t)P->mainvolume; P->fadeonoff = 0; }
        }
    }

    for (int chan = 0; chan < 9; chan++) {                       /* channel delay */
        c = &P->channel[chan];
        if (c->chancheat.chandelay) if (!(--c->chancheat.chandelay)) playsound(P, c->chancheat.sound, chan, c->chancheat.high);
    }

    if (!P->tempo_now) {                                          /* notes */
        vbreak = 0;
        for (int chan = 0; chan < 9; chan++) {
            c = &P->channel[chan];
            if (!c->packwait) {
                unsigned patnum = P->posplay < S->nposi ? S->pos[P->posplay * 9 + chan].patnum : 0;
                uint8_t transpose = P->posplay < S->nposi ? S->pos[P->posplay * 9 + chan].transpose : 0;
                if (P->posplay >= S->nposi) comword = 0x8001;
                else if ((patnum + c->packpos) < S->npat) comword = S->pat[patnum + c->packpos]; else comword = 0x8001;
                comhi = comword >> 8; comlo = comword & 0xff;
                if (comword) {
                    if (comhi == 0x80) c->packwait = comlo;
                    else if (comhi >= 0x80) {
                        switch (comhi) {
                        case 0xff:
                            c->volcar = (uint8_t)((((c->volcar & 0x3f) * comlo) >> 6) & 0x3f);
                            if (P->fmchip[0xc0 + chan] & 1) c->volmod = (uint8_t)((((c->volmod & 0x3f) * comlo) >> 6) & 0x3f);
                            break;
                        case 0xfe: P->tempo = comword & 0x3f; break;
                        case 0xfd: c->nextvol = comlo; break;
                        case 0xfc: P->playing = 0; break;
                        case 0xfb: c->keycount = 1; break;
                        case 0xfa: vbreak = 1; P->jumppos = (P->posplay + 1) & MAXPOS; break;
                        case 0xf9: vbreak = 1; P->jumppos = comlo & MAXPOS; P->jumping = 1; if (P->jumppos < P->posplay) P->songlooped = 1; break;
                        case 0xf8: c->lasttune = 0; break;
                        case 0xf7: c->vibwait = 0; c->vibspeed = (uint8_t)((comlo >> 4) + 2); c->vibrate = (comlo & 15) + 1; break;
                        case 0xf6: c->glideto = comlo; break;
                        case 0xf5: c->finetune = comlo; break;
                        case 0xf4: if (!P->hardfade) { P->allvolume = P->mainvolume = comlo; P->fadeonoff = 0; } break;
                        case 0xf3: if (!P->hardfade) P->fadeonoff = comlo; break;
                        case 0xf2: c->trmstay = comlo; break;
                        case 0xf1: case 0xf0: break;                          /* MIDI commands (unhandled) */
                        default: if (comhi < 0xa0) c->glideto = comhi & 0x1f; break;
                        }
                    } else {
                        uint8_t sound; unsigned short high; signed char transp = (signed char)(transpose & 127);
                        if (transpose & 64) transp |= (signed char)0x80;
                        if (transpose & 128) { sound = (uint8_t)((comlo + transp) & MAXSOUND); high = (unsigned short)(comhi << 4); }
                        else { sound = comlo & MAXSOUND; high = (unsigned short)((comhi + transp) << 4); }
                        if (!S->chandelay[chan]) playsound(P, sound, chan, high);
                        else { c->chancheat.chandelay = S->chandelay[chan]; c->chancheat.sound = sound; c->chancheat.high = high; }
                    }
                }
                c->packpos++;
            } else c->packwait--;
        }
        P->tempo_now = P->tempo;
        P->pattplay++;
        if (vbreak) { P->pattplay = 0; for (int i = 0; i < 9; i++) P->channel[i].packpos = P->channel[i].packwait = 0; P->posplay = P->jumppos; }
        else if (P->pattplay >= S->pattlen) { P->pattplay = 0; for (int i = 0; i < 9; i++) P->channel[i].packpos = P->channel[i].packwait = 0; P->posplay = (P->posplay + 1) & MAXPOS; }
    } else P->tempo_now--;

    for (int chan = 0; chan < 9; chan++) {                       /* effects */
        c = &P->channel[chan]; regnum = op_table[chan];
        if (c->keycount > 0) { if (c->keycount == 1) setregs_adv(P, 0xb0 + chan, 0xdf, 0); c->keycount--; }

        if (c->arp_size == 0) arpreg = 0;
        else {
            arpreg = (unsigned short)(c->arp_tab[c->arp_pos] << 4);
            if (arpreg == 0x800) {
                if (c->arp_pos > 0) c->arp_tab[0] = c->arp_tab[c->arp_pos - 1];
                c->arp_size = 1; c->arp_pos = 0; arpreg = (unsigned short)(c->arp_tab[0] << 4);
            }
            if (c->arp_count == c->arp_speed) { c->arp_pos++; if (c->arp_pos >= c->arp_size) c->arp_pos = 0; c->arp_count = 0; } else c->arp_count++;
        }

        if (c->lasttune && (c->lasttune != c->gototune)) {         /* glide and portamento */
            if (c->lasttune > c->gototune) { if (c->lasttune - c->gototune < c->portspeed) c->lasttune = c->gototune; else c->lasttune = (uint16_t)(c->lasttune - c->portspeed); }
            else { if (c->gototune - c->lasttune < c->portspeed) c->lasttune = c->gototune; else c->lasttune = (uint16_t)(c->lasttune + c->portspeed); }
            if (arpreg >= 0x800) arpreg = (unsigned short)(c->lasttune - (arpreg ^ 0xff0) - 16); else arpreg = (unsigned short)(arpreg + c->lasttune);
            freq = frequency[arpreg % (12 * 16)]; octave = (unsigned short)(arpreg / (12 * 16) - 1);
            setregs(P, 0xa0 + chan, freq & 0xff); setregs_adv(P, 0xb0 + chan, 0x20, ((octave << 2) + (freq >> 8)) & 0xdf);
        } else {
            if (!c->vibwait) {
                if (c->vibrate) {
                    wibc = (unsigned short)(vibtab[c->vibcount & 0x3f] * c->vibrate);
                    if ((c->vibcount & 0x40) == 0) tune = (unsigned short)(c->lasttune + (wibc >> 8)); else tune = (unsigned short)(c->lasttune - (wibc >> 8));
                    if (arpreg >= 0x800) tune = (unsigned short)(tune - (arpreg ^ 0xff0) - 16); else tune = (unsigned short)(tune + arpreg);
                    freq = frequency[tune % (12 * 16)]; octave = (unsigned short)(tune / (12 * 16) - 1);
                    setregs(P, 0xa0 + chan, freq & 0xff); setregs_adv(P, 0xb0 + chan, 0x20, ((octave << 2) + (freq >> 8)) & 0xdf);
                    c->vibcount = (uint8_t)(c->vibcount + c->vibspeed);
                } else if (c->arp_size != 0) {
                    if (arpreg >= 0x800) tune = (unsigned short)(c->lasttune - (arpreg ^ 0xff0) - 16); else tune = (unsigned short)(c->lasttune + arpreg);
                    freq = frequency[tune % (12 * 16)]; octave = (unsigned short)(tune / (12 * 16) - 1);
                    setregs(P, 0xa0 + chan, freq & 0xff); setregs_adv(P, 0xb0 + chan, 0x20, ((octave << 2) + (freq >> 8)) & 0xdf);
                }
            } else {
                c->vibwait--;
                if (c->arp_size != 0) {
                    if (arpreg >= 0x800) tune = (unsigned short)(c->lasttune - (arpreg ^ 0xff0) - 16); else tune = (unsigned short)(c->lasttune + arpreg);
                    freq = frequency[tune % (12 * 16)]; octave = (unsigned short)(tune / (12 * 16) - 1);
                    setregs(P, 0xa0 + chan, freq & 0xff); setregs_adv(P, 0xb0 + chan, 0x20, ((octave << 2) + (freq >> 8)) & 0xdf);
                }
            }
        }

        if (!c->trmwait) {                                         /* tremolo, modulator */
            if (c->trmrate) {
                tremc = (unsigned short)(tremtab[c->trmcount & 0x7f] * c->trmrate);
                if ((tremc >> 8) <= (c->volmod & 0x3f)) level = (uint8_t)((c->volmod & 0x3f) - (tremc >> 8)); else level = 0;
                if (P->allvolume != 0 && (P->fmchip[0xc0 + chan] & 1)) setregs_adv(P, 0x40 + regnum, 0xc0, ((level * P->allvolume) >> 8) ^ 0x3f);
                else setregs_adv(P, 0x40 + regnum, 0xc0, level ^ 0x3f);
                c->trmcount = (uint8_t)(c->trmcount + c->trmspeed);
            } else if (P->allvolume != 0 && (P->fmchip[0xc0 + chan] & 1)) setregs_adv(P, 0x40 + regnum, 0xc0, ((((c->volmod & 0x3f) * P->allvolume) >> 8) ^ 0x3f) & 0x3f);
            else setregs_adv(P, 0x40 + regnum, 0xc0, (c->volmod ^ 0x3f) & 0x3f);
        } else {
            c->trmwait--;
            if (P->allvolume != 0 && (P->fmchip[0xc0 + chan] & 1)) setregs_adv(P, 0x40 + regnum, 0xc0, ((((c->volmod & 0x3f) * P->allvolume) >> 8) ^ 0x3f) & 0x3f);
        }

        if (!c->trcwait) {                                         /* tremolo, carrier */
            if (c->trcrate) {
                tremc = (unsigned short)(tremtab[c->trccount & 0x7f] * c->trcrate);
                if ((tremc >> 8) <= (c->volcar & 0x3f)) level = (uint8_t)((c->volcar & 0x3f) - (tremc >> 8)); else level = 0;
                if (P->allvolume != 0) setregs_adv(P, 0x43 + regnum, 0xc0, ((level * P->allvolume) >> 8) ^ 0x3f);
                else setregs_adv(P, 0x43 + regnum, 0xc0, level ^ 0x3f);
                c->trccount = (uint8_t)(c->trccount + c->trcspeed);
            } else if (P->allvolume != 0) setregs_adv(P, 0x43 + regnum, 0xc0, ((((c->volcar & 0x3f) * P->allvolume) >> 8) ^ 0x3f) & 0x3f);
            else setregs_adv(P, 0x43 + regnum, 0xc0, (c->volcar ^ 0x3f) & 0x3f);
        } else {
            c->trcwait--;
            if (P->allvolume != 0) setregs_adv(P, 0x43 + regnum, 0xc0, ((((c->volcar & 0x3f) * P->allvolume) >> 8) ^ 0x3f) & 0x3f);
        }
    }
    return (!P->playing || P->songlooped) ? 0 : 1;
}
