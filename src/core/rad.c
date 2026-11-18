/* Reality ADlib Tracker (RAD) 1.0 / 2.1: tune validation, loader and replayer. C port of the public domain RAD2 replayer by Shayde / Reality (as shipped
 * with the AdPlug project, which also reads the original RAD v1 format). GPL-3.0-or-later */
#include "rad.h"
#include <stdlib.h>
#include <string.h>

enum { cmPortamentoUp = 0x1, cmPortamentoDwn = 0x2, cmToneSlide = 0x3, cmToneVolSlide = 0x5, cmVolSlide = 0xA, cmSetVol = 0xC, cmJumpToLine = 0xD, cmSetSpeed = 0xF,
       cmIgnore = 'I' - 55, cmMultiplier = 'M' - 55, cmRiff = 'R' - 55, cmTranspose = 'T' - 55, cmFeedback = 'U' - 55, cmVolume = 'V' - 55 };
enum { SNone, SRiff, SIRiff };
enum { fKeyOn = 1, fKeyOff = 2, fKeyedOn = 4 };

static const int8_t NoteSize[8] = { 0, 2, 1, 3, 1, 3, 2, 4 };
static const uint16_t ChanOffsets3[9] = { 0, 1, 2, 0x100, 0x101, 0x102, 6, 7, 8 };
static const uint16_t Chn2Offsets3[9] = { 3, 4, 5, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108 };
static const uint16_t NoteFreq[12] = { 0x16b, 0x181, 0x198, 0x1b0, 0x1ca, 0x1e5, 0x202, 0x220, 0x241, 0x263, 0x287, 0x2ae };
static const uint16_t OpOffsets2[9][2] = { { 0x003, 0x000 }, { 0x004, 0x001 }, { 0x005, 0x002 }, { 0x00B, 0x008 }, { 0x00C, 0x009 }, { 0x00D, 0x00A }, { 0x013, 0x010 }, { 0x014, 0x011 }, { 0x015, 0x012 } };
static const uint16_t OpOffsets3[9][4] = {
    { 0x00B, 0x008, 0x003, 0x000 }, { 0x00C, 0x009, 0x004, 0x001 }, { 0x00D, 0x00A, 0x005, 0x002 },
    { 0x10B, 0x108, 0x103, 0x100 }, { 0x10C, 0x109, 0x104, 0x101 }, { 0x10D, 0x10A, 0x105, 0x102 },
    { 0x113, 0x110, 0x013, 0x010 }, { 0x114, 0x111, 0x014, 0x011 }, { 0x115, 0x112, 0x015, 0x012 } };
static const uint8_t AlgCarriers[7][4] = { { 1, 0, 0, 0 }, { 1, 1, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 0, 1 }, { 1, 0, 1, 0 }, { 1, 0, 1, 1 }, { 1, 1, 1, 1 } };

/* ---------------- validation (the tune must not be able to crash the player) ---------------- */
static int check_pattern(const uint8_t **ps, const uint8_t *e, int riff) {
    const uint8_t *s = *ps;
    if (s + 2 > e) return 1;
    unsigned size = s[0] | (s[1] << 8); s += 2;
    const uint8_t *pe = s + size; if (pe > e) return 1;
    uint8_t linedef, chandef;
    do {
        if (s >= pe) return 1;
        linedef = *s++; if ((linedef & 0x7F) >= 64) return 1;
        do {
            if (s >= pe) return 1;
            chandef = *s++;
            if (!riff && (chandef & 0x0F) >= 9) return 1;
            if (chandef & 0x40) { if (s >= pe) return 1; uint8_t n = *s++ & 15; if (n == 0 || n == 13 || n == 14) return 1; }
            if (chandef & 0x20) { if (s >= pe) return 1; uint8_t in = *s++; if (in == 0 || in >= 128) return 1; }
            if (chandef & 0x10) { if (s + 2 > pe) return 1; uint8_t ef = *s++, pa = *s++; if (ef > 31 || pa > 99) return 1; }
        } while (!(chandef & 0x80));
    } while (!(linedef & 0x80));
    if (s != pe) return 1;
    *ps = s; return 0;
}
static int check_pattern_old(const uint8_t *s, const uint8_t *e) {
    if (s > e) return 1;
    uint8_t linedef, chandef;
    do {
        if (s >= e) return 1;
        linedef = *s++; if ((linedef & 0x7F) >= 64) return 1;
        do {
            if (s >= e) return 1;
            chandef = *s++; if ((chandef & 0x0F) >= 9) return 1;
            if (s >= e) return 1; s++;
            if (s >= e) return 1; uint8_t in = *s++;
            if (in & 0xf) { if (s > e) return 1; s++; }
        } while (!(chandef & 0x80));
    } while (!(linedef & 0x80));
    return 0;
}
static int validate(const uint8_t *data, size_t n) {
    const uint8_t *s = data, *e = s + n;
    if (n < 16) return 1;
    s += 16;
    if (s >= e || (*s != 0x10 && *s != 0x21)) return 1;
    int ver = *s >> 4; s++;
    if (s >= e) return 1;
    uint8_t flags = *s++;
    if (ver >= 2 && (flags & 0x80)) return 1;
    if (ver == 1 && (flags & 0x20)) return 1;
    if (ver >= 2 && (flags & 0x20)) { if (s + 2 > e) return 1; unsigned bpm = s[0] | (s[1] << 8); s += 2; if (bpm < 46 || bpm > 300) return 1; }
    if (ver >= 2 || (flags & 0x80)) { do { if (s >= e) return 1; } while (*s++); }
    uint8_t last = 0;
    for (;;) {
        if (s >= e) return 1;
        uint8_t in = *s++; if (!in) break;
        if (in > 127 || in <= last) return 1;
        last = in;
        if (ver >= 2) {
            if (s >= e) return 1;
            uint8_t nl = *s++; s += nl;
            if (s > e) return 1;
            uint8_t alg = *s;
            if ((alg & 7) == 7) { if (s + 7 > e) return 1; if (s[2] >> 4) return 1; s += 7; }
            else { s += 24; if (s > e) return 1; }
            if (alg & 0x80) { if (check_pattern(&s, e, 0)) return 1; }
        } else s += 11;
    }
    if (s >= e) return 1;
    uint8_t osz = *s++; const uint8_t *ol = s;
    if (osz > 128) return 1;
    s += osz;
    for (int i = 0; i < osz; i++) {
        uint8_t o = ol[i];
        if (o & 0x80) { if ((o & 0x7F) >= osz) return 1; }
        else { if (ver >= 2 && o >= 100) return 1; if (ver == 1 && o >= 32) return 1; }
    }
    if (ver >= 2) {
        for (;;) {
            if (s >= e) return 1;
            uint8_t pn = *s++; if (pn == 0xFF) break;
            if (pn >= 100) return 1;
            if (check_pattern(&s, e, 0)) return 1;
        }
    } else for (int i = 0; i < 32; i++) {
        if (s + 2 > e) return 1;
        int pos = s[0] | (s[1] << 8); s += 2;
        if (pos) { if (check_pattern_old(data + pos, e)) return 1; }
    }
    if (ver >= 2) for (;;) {
        if (s >= e) return 1;
        uint8_t rn = *s++; if (rn == 0xFF) break;
        if ((rn >> 4) > 9 || (rn & 15) == 0 || (rn & 15) > 9) return 1;
        if (check_pattern(&s, e, 1)) return 1;
    }
    return 0;
}

int rad_probe(const uint8_t *b, size_t n) { return n >= 18 && !memcmp(b, "RAD by REALiTY!!", 16) && (b[16] == 0x10 || b[16] == 0x21); }

void rad_free(RadSong *s) { if (!s) return; free(s->data); free(s); }

/* ---------------- loader (the player's Init) ---------------- */
RadSong *rad_load(const uint8_t *buf, size_t len, int *err) {
    int e = 0; if (!err) err = &e; *err = 0;
    if (!rad_probe(buf, len)) { *err = -1; return NULL; }
    RadSong *t = calloc(1, sizeof *t); if (!t) { *err = -5; return NULL; }
    t->data = calloc(1, len + 1); if (!t->data) { free(t); *err = -5; return NULL; }
    memcpy(t->data, buf, len); t->size = len + 1;
    if (validate(t->data, t->size)) { rad_free(t); *err = -2; return NULL; }
    const uint8_t *tune = t->data, *s = tune + 0x11;
    t->version = tune[0x10] >> 4; t->use_opl3 = t->version >= 2;
    uint8_t flags = *s++;
    t->speed = flags & 0x1F;
    t->hertz = 50.0f;
    if (t->version >= 2 && (flags & 0x20)) { t->hertz = (float)(s[0] | ((int)s[1] << 8)) * 2 / 5; s += 2; }
    if (flags & 0x40) t->hertz = 18.2f;
    t->desc = NULL;
    if (t->version >= 2 || (flags & 0x80)) { t->desc = s; while (*s) s++; s++; }
    t->ninst = 0;
    for (;;) {
        uint8_t in = *s++; if (in == 0) break;
        if (in > t->ninst) t->ninst = in;
        RadInst *inst = &t->inst[in - 1];
        if (t->version >= 2) {
            uint8_t nl = *s++; for (int i = 0; i < nl; i++) inst->name[i] = (char)*s++; inst->name[nl] = 0;
            uint8_t alg = *s++;
            inst->algorithm = alg & 7; inst->panning[0] = (alg >> 3) & 3; inst->panning[1] = (alg >> 5) & 3;
            if (inst->algorithm < 7) {
                uint8_t b = *s++; inst->feedback[0] = b & 15; inst->feedback[1] = b >> 4;
                b = *s++; inst->detune = b >> 4; inst->riffspeed = b & 15;
                inst->volume = *s++;
                for (int i = 0; i < 4; i++) for (int j = 0; j < 5; j++) inst->operators[i][j] = *s++;
            } else s += 6;
            if (alg & 0x80) { int size = s[0] | ((int)s[1] << 8); s += 2; inst->riff = s; s += size; } else inst->riff = NULL;
        } else {
            inst->name[0] = 0; inst->algorithm = s[8] & 1; inst->panning[0] = inst->panning[1] = 0;
            inst->feedback[0] = (s[8] >> 1) & 7; inst->feedback[1] = 0; inst->detune = 0; inst->riffspeed = 0; inst->volume = 64;
            inst->operators[0][0] = s[0]; inst->operators[1][0] = s[1]; inst->operators[0][1] = s[2]; inst->operators[1][1] = s[3];
            inst->operators[0][2] = s[4]; inst->operators[1][2] = s[5]; inst->operators[0][3] = s[6]; inst->operators[1][3] = s[7];
            inst->operators[0][4] = s[9]; inst->operators[1][4] = s[10];
            inst->riff = NULL; s += 11;
        }
    }
    t->norder = *s++; t->order = s; s += t->norder;
    t->ntracks = 0;
    if (t->version >= 2) {
        for (;;) {
            uint8_t tn = *s++; if (tn >= RAD_TRACKS) break;
            if (tn + 1 > t->ntracks) t->ntracks = tn + 1;
            int size = s[0] | ((int)s[1] << 8); s += 2;
            t->tracks[tn] = s; s += size;
        }
    } else for (int i = 0; i < 32; i++) {
        int pos = s[0] | ((int)s[1] << 8); s += 2;
        if (pos) { t->ntracks = i + 1; t->tracks[i] = tune + pos; }
    }
    if (t->version >= 2) for (;;) {
        uint8_t id = *s++; uint8_t rn = id >> 4, cn = id & 15;
        if (rn >= RAD_RIFFTRACKS || cn > RAD_CHANNELS) break;
        int size = s[0] | ((int)s[1] << 8); s += 2;
        t->riffs[rn][cn - 1] = s; s += size;
    }
    return t;
}

int rad_order_track(const RadSong *s, int order) {
    if (order < 0 || order >= s->norder) return -1;
    int e = s->order[order]; return (e & 0x80) ? -1 : e;
}

/* ---------------- one note of a track ---------------- */
typedef struct { int8_t note, octave; uint8_t inst, effect, param; } Un;

static int unpack_note(int version, const uint8_t **ps, uint8_t *last_instrument, Un *u) {
    const uint8_t *s = *ps; uint8_t chanid = *s++;
    u->inst = 0; u->effect = 0; u->param = 0;
    uint8_t note = 0;
    if (version >= 2) {
        if (chanid & 0x40) { uint8_t n = *s++; note = n & 0x7F; if (n & 0x80) u->inst = *last_instrument; }
        if (chanid & 0x20) { u->inst = *s++; *last_instrument = u->inst; }
        if (chanid & 0x10) { u->effect = *s++; u->param = *s++; }
    } else {
        uint8_t n = *s++; note = n & 0x7f; if (n & 0x80) u->inst = 16;
        n = *s++; u->inst |= n >> 4; if (u->inst) *last_instrument = u->inst;
        u->effect = n & 0xf; if (u->effect) u->param = *s++;
    }
    u->note = note & 15; u->octave = note >> 4;
    *ps = s; return (chanid & 0x80) != 0;
}

static const uint8_t *skip_to_line(int version, const uint8_t *trk, uint8_t linenum, int chan_riff) {
    for (;;) {
        uint8_t lineid = *trk;
        if ((lineid & 0x7F) >= linenum) return trk;
        if (lineid & 0x80) break;
        trk++;
        uint8_t chanid;
        do {
            chanid = *trk++;
            if (version >= 2) trk += NoteSize[(chanid >> 4) & 7];
            else if (trk[1] & 0xf) trk += 3; else trk += 2;
        } while (!(chanid & 0x80) && !chan_riff);
    }
    return NULL;
}

int rad_cell(const RadSong *s, int track, int line, int chan, RadCell *c) {
    memset(c, 0, sizeof *c);
    if (track < 0 || track >= RAD_TRACKS || !s->tracks[track]) return 0;
    const uint8_t *trk = s->tracks[track];
    for (;;) {
        uint8_t lineid = *trk, ln = lineid & 0x7F;
        if (ln > line) return 0;
        if (ln == line) {
            trk++; uint8_t last_inst = 0; int last;
            do {
                int cn = *trk & 15; Un u; last = unpack_note(s->version, &trk, &last_inst, &u);
                if (cn == chan) { c->note = u.note; c->octave = u.octave; c->inst = u.inst; c->effect = u.effect; c->param = u.param; return 1; }
            } while (!last);
            return 0;
        }
        if (lineid & 0x80) return 0;
        trk = skip_to_line(s->version, trk, (uint8_t)(ln + 1), 0);
        if (!trk) return 0;
    }
}

/* ---------------- player ---------------- */
typedef struct { int8_t PortSlide, VolSlide; uint16_t ToneSlideFreq; uint8_t ToneSlideOct, ToneSlideSpeed; int8_t ToneSlideDir; } Fx;
typedef struct { Fx FX; const uint8_t *Track, *TrackStart; uint8_t Line, Speed, SpeedCnt; int8_t TransposeOctave, TransposeNote; uint8_t LastInstrument; int Updated; } Riff;
typedef struct { uint8_t LastInstrument; const RadInst *Instrument; uint8_t Volume, DetuneA, DetuneB, KeyFlags; uint16_t CurrFreq; int8_t CurrOctave; Fx FX; Riff Riff, IRiff; } Chan;

struct RadPlayer {
    const RadSong *S; RadWrite wr; void *user;
    Chan Channels[RAD_CHANNELS];
    uint32_t PlayTime, OrderMap[4]; int Repeating;
    const uint8_t *Track;
    uint8_t Speed, SpeedCnt, Order, Line, MasterVol; int8_t Entrances, LineJump;
    uint8_t OPL3Regs[512];
    Un U;                                        /* values of the last unpacked note */
};

static void SetOPL3(RadPlayer *P, uint16_t reg, uint8_t val) { P->OPL3Regs[reg] = val; P->wr(P->user, reg, val); }
static uint8_t GetOPL3(const RadPlayer *P, uint16_t reg) { return P->OPL3Regs[reg]; }

static void PlayNote(RadPlayer *P, int channum, int8_t notenum, int8_t octave, uint16_t instnum, uint8_t cmd, uint8_t param, int src, int op);

static void ResetFX(Fx *fx) { fx->PortSlide = 0; fx->VolSlide = 0; fx->ToneSlideDir = 0; }

static const uint8_t *GetTrack(RadPlayer *P) {
    const RadSong *S = P->S;
    if (P->Order >= S->norder) P->Order = 0;
    uint8_t track_num = S->norder ? S->order[P->Order] : 0;
    if (track_num & 0x80) { P->Order = track_num & 0x7F; track_num = S->order[P->Order] & 0x7F; }
    if (P->Order < 128) {
        int byte = P->Order >> 5; uint32_t bit = (uint32_t)1 << (P->Order & 31);
        if (P->OrderMap[byte] & bit) P->Repeating = 1; else P->OrderMap[byte] |= bit;
    }
    return track_num < RAD_TRACKS ? S->tracks[track_num] : NULL;
}

void rad_rewind(RadPlayer *P) {
    for (uint16_t reg = 0x20; reg < 0xF6; reg++) {
        uint8_t val = (reg >= 0x60 && reg < 0xA0) ? 0xFF : 0;
        SetOPL3(P, reg, val); SetOPL3(P, reg + 0x100, val);
    }
    SetOPL3(P, 1, 0x20); SetOPL3(P, 8, 0); SetOPL3(P, 0xbd, 0); SetOPL3(P, 0x104, 0); SetOPL3(P, 0x105, 1);
    P->PlayTime = 0; P->Repeating = 0; for (int i = 0; i < 4; i++) P->OrderMap[i] = 0;
    P->Speed = (uint8_t)P->S->speed;
    P->SpeedCnt = 1; P->Order = 0; P->Track = GetTrack(P); P->Line = 0; P->Entrances = 0; P->MasterVol = 64;
    for (int i = 0; i < RAD_CHANNELS; i++) {
        Chan *c = &P->Channels[i];
        c->LastInstrument = 0; c->Instrument = NULL; c->Volume = 0; c->DetuneA = 0; c->DetuneB = 0; c->KeyFlags = 0; c->Riff.SpeedCnt = 0; c->IRiff.SpeedCnt = 0;
    }
}

RadPlayer *rad_new(const RadSong *s, RadWrite wr, void *user) {
    RadPlayer *P = calloc(1, sizeof *P); if (!P) return NULL;
    P->S = s; P->wr = wr; P->user = user;
    for (int i = 0; i < 512; i++) P->OPL3Regs[i] = 255;
    rad_rewind(P);
    return P;
}
void rad_free_player(RadPlayer *P) { free(P); }

static void LoadInstrumentOPL3(RadPlayer *P, int channum) {
    Chan *chan = &P->Channels[channum]; const RadInst *inst = chan->Instrument; int use3 = P->S->use_opl3;
    if (!inst) return;
    uint8_t alg = inst->algorithm;
    chan->Volume = inst->volume; chan->DetuneA = (uint8_t)((inst->detune + 1) >> 1); chan->DetuneB = inst->detune >> 1;
    if (use3 && channum < 6) {                           /* 4-op mode for algorithms 2 and 3 (4 to 6 are simulated with 2-op mode) */
        uint8_t mask = (uint8_t)(1 << channum);
        SetOPL3(P, 0x104, (uint8_t)((GetOPL3(P, 0x104) & ~mask) | (alg == 2 || alg == 3 ? mask : 0)));
    }
    if (use3) {
        SetOPL3(P, 0xC0 + ChanOffsets3[channum], (uint8_t)(((inst->panning[1] ^ 3) << 4) | inst->feedback[1] << 1 | (alg == 3 || alg == 5 || alg == 6 ? 1 : 0)));
        SetOPL3(P, 0xC0 + Chn2Offsets3[channum], (uint8_t)(((inst->panning[0] ^ 3) << 4) | inst->feedback[0] << 1 | (alg == 1 || alg == 6 ? 1 : 0)));
    } else SetOPL3(P, 0xC0 + channum, (uint8_t)(((inst->panning[0] ^ 3) << 4) | inst->feedback[0] << 1 | (alg == 1 ? 1 : 0)));
    for (int i = 0; i < (use3 ? 4 : 2); i++) {
        static const uint8_t blank[5] = { 0, 0x3F, 0, 0xF0, 0 };
        const uint8_t *op = (alg < 2 && i >= 2) ? blank : inst->operators[i];
        uint16_t reg = use3 ? OpOffsets3[channum][i] : OpOffsets2[channum][i];
        uint16_t vol = ~op[1] & 0x3F;
        if (AlgCarriers[alg][i]) { vol = (uint16_t)(vol * inst->volume / 64); vol = (uint16_t)(vol * P->MasterVol / 64); }
        SetOPL3(P, reg + 0x20, op[0]);
        SetOPL3(P, reg + 0x40, (uint8_t)((op[1] & 0xC0) | ((vol ^ 0x3F) & 0x3F)));
        SetOPL3(P, reg + 0x60, op[2]); SetOPL3(P, reg + 0x80, op[3]); SetOPL3(P, reg + 0xE0, op[4]);
    }
}

static void PlayNoteOPL3(RadPlayer *P, int channum, int8_t octave, int8_t note) {
    Chan *chan = &P->Channels[channum]; int use3 = P->S->use_opl3; uint16_t o1, o2;
    if (use3) { o1 = ChanOffsets3[channum]; o2 = Chn2Offsets3[channum]; } else { o1 = 0; o2 = (uint16_t)channum; }
    if (chan->KeyFlags & fKeyOff) {
        chan->KeyFlags &= ~(fKeyOff | fKeyedOn);
        if (use3) SetOPL3(P, 0xB0 + o1, GetOPL3(P, 0xB0 + o1) & ~0x20);
        SetOPL3(P, 0xB0 + o2, GetOPL3(P, 0xB0 + o2) & ~0x20);
    }
    if (note > 12) return;
    int op4 = use3 && chan->Instrument && chan->Instrument->algorithm >= 2;
    uint16_t freq = NoteFreq[note - 1], frq2 = freq;
    chan->CurrFreq = freq; chan->CurrOctave = octave;
    freq = (uint16_t)(freq + chan->DetuneA); frq2 = (uint16_t)(frq2 - chan->DetuneB);
    if (op4) SetOPL3(P, 0xA0 + o1, frq2 & 0xFF);
    SetOPL3(P, 0xA0 + o2, freq & 0xFF);
    if (chan->KeyFlags & fKeyOn) chan->KeyFlags = (uint8_t)((chan->KeyFlags & ~fKeyOn) | fKeyedOn);
    if (op4) SetOPL3(P, 0xB0 + o1, (uint8_t)((frq2 >> 8) | (octave << 2) | ((chan->KeyFlags & fKeyedOn) ? 0x20 : 0)));
    else if (use3) SetOPL3(P, 0xB0 + o1, 0);
    SetOPL3(P, 0xB0 + o2, (uint8_t)((freq >> 8) | (octave << 2) | ((chan->KeyFlags & fKeyedOn) ? 0x20 : 0)));
}

static void Transpose(RadPlayer *P, int8_t note, int8_t octave) {
    Un *u = &P->U;
    if (u->note >= 1 && u->note <= 12) {
        int8_t toct = (int8_t)(octave - 3);
        if (toct != 0) { u->octave = (int8_t)(u->octave + toct); if (u->octave < 0) u->octave = 0; else if (u->octave > 7) u->octave = 7; }
        int8_t tnot = (int8_t)(note - 12);
        if (tnot != 0) {
            u->note = (int8_t)(u->note + tnot);
            if (u->note < 1) { u->note = (int8_t)(u->note + 12); if (u->octave > 0) u->octave--; else u->note = 1; }
        }
    }
}

static void TickRiff(RadPlayer *P, int channum, Riff *riff, int chan_riff) {
    const RadSong *S = P->S; uint8_t lineid;
    riff->Updated = 1;
    if (riff->SpeedCnt == 0) { ResetFX(&riff->FX); return; }
    riff->SpeedCnt--;
    if (riff->SpeedCnt > 0) return;
    riff->SpeedCnt = riff->Speed;
    uint8_t line = riff->Line++;
    if (riff->Line >= RAD_LINES) riff->SpeedCnt = 0;
    ResetFX(&riff->FX);
    const uint8_t *trk = riff->Track;
    if (trk && (*trk & 0x7F) == line) {
        lineid = *trk++;
        riff->Updated = 0;                               /* the riff may be clobbered by recursive riffs */
        if (chan_riff) {
            unpack_note(S->version, &trk, &riff->LastInstrument, &P->U);
            Transpose(P, riff->TransposeNote, riff->TransposeOctave);
            PlayNote(P, channum, P->U.note, P->U.octave, P->U.inst, P->U.effect, P->U.param, SRiff, 0);
        } else {
            int last;
            do {
                int col = *trk & 15;
                last = unpack_note(S->version, &trk, &riff->LastInstrument, &P->U);
                if (P->U.effect != cmIgnore) Transpose(P, riff->TransposeNote, riff->TransposeOctave);
                PlayNote(P, channum, P->U.note, P->U.octave, P->U.inst, P->U.effect, P->U.param, SIRiff, col > 0 ? (col - 1) & 3 : 0);
            } while (!last);
        }
        if (riff->Updated) return;
        riff->Updated = 1;
        if (lineid & 0x80) trk = NULL;
        riff->Track = trk;
    }
    if (!trk || (*trk++ & 0x7F) != riff->Line) return;      /* if the next line has a jump command, run it now */
    unpack_note(S->version, &trk, &lineid, &P->U);
    if (P->U.effect == cmJumpToLine && P->U.param < RAD_LINES) {
        riff->Line = P->U.param;
        riff->Track = skip_to_line(S->version, riff->TrackStart, P->U.param, chan_riff);
    }
}

static void SetVolume(RadPlayer *P, int channum, uint8_t vol) {
    Chan *chan = &P->Channels[channum];
    if (vol > 64) vol = 64;
    chan->Volume = vol;
    vol = (uint8_t)(vol * P->MasterVol / 64);
    const RadInst *inst = chan->Instrument; if (!inst) return;
    uint8_t alg = inst->algorithm;
    for (int i = 0; i < 4; i++) {
        const uint8_t *op = inst->operators[i];
        if (!AlgCarriers[alg][i]) continue;
        uint8_t opvol = (uint8_t)((uint16_t)((op[1] & 63) ^ 63) * vol / 64);
        uint16_t reg = (uint16_t)(0x40 + (P->S->use_opl3 ? OpOffsets3[channum][i] : OpOffsets2[channum][i]));
        SetOPL3(P, reg, (uint8_t)((GetOPL3(P, reg) & 0xC0) | (opvol ^ 0x3F)));
    }
}

static void GetSlideDir(RadPlayer *P, int channum, Fx *fx) {
    Chan *chan = &P->Channels[channum];
    int8_t speed = (int8_t)fx->ToneSlideSpeed;
    if (speed > 0) {
        uint8_t oct = fx->ToneSlideOct; uint16_t freq = fx->ToneSlideFreq, oldfreq = chan->CurrFreq; uint8_t oldoct = (uint8_t)chan->CurrOctave;
        if (oldoct > oct) speed = (int8_t)-speed;
        else if (oldoct == oct) { if (oldfreq > freq) speed = (int8_t)-speed; else if (oldfreq == freq) speed = 0; }
    }
    fx->ToneSlideDir = speed;
}

static void Portamento(RadPlayer *P, uint16_t channum, Fx *fx, int8_t amount, int toneslide) {
    Chan *chan = &P->Channels[channum]; int use3 = P->S->use_opl3;
    uint16_t freq = chan->CurrFreq; uint8_t oct = (uint8_t)chan->CurrOctave;
    freq = (uint16_t)(freq + amount);
    if (freq < 0x156) { if (oct > 0) { oct--; freq = (uint16_t)(freq + 0x2AE - 0x156); } else freq = 0x156; }
    else if (freq > 0x2AE) { if (oct < 7) { oct++; freq = (uint16_t)(freq - (0x2AE - 0x156)); } else freq = 0x2AE; }
    if (toneslide) {
        if (amount >= 0) { if (oct > fx->ToneSlideOct || (oct == fx->ToneSlideOct && freq >= fx->ToneSlideFreq)) { freq = fx->ToneSlideFreq; oct = fx->ToneSlideOct; } }
        else { if (oct < fx->ToneSlideOct || (oct == fx->ToneSlideOct && freq <= fx->ToneSlideFreq)) { freq = fx->ToneSlideFreq; oct = fx->ToneSlideOct; } }
    }
    chan->CurrFreq = freq; chan->CurrOctave = (int8_t)oct;
    uint16_t frq2 = (uint16_t)(freq - chan->DetuneB); freq = (uint16_t)(freq + chan->DetuneA);
    uint16_t co = use3 ? Chn2Offsets3[channum] : channum;
    SetOPL3(P, 0xA0 + co, freq & 0xFF);
    SetOPL3(P, 0xB0 + co, (uint8_t)(((freq >> 8) & 3) | oct << 2 | (GetOPL3(P, 0xB0 + co) & 0xE0)));
    if (use3) {
        co = ChanOffsets3[channum];
        SetOPL3(P, 0xA0 + co, frq2 & 0xFF);
        SetOPL3(P, 0xB0 + co, (uint8_t)(((frq2 >> 8) & 3) | oct << 2 | (GetOPL3(P, 0xB0 + co) & 0xE0)));
    }
}

static void ContinueFX(RadPlayer *P, int channum, Fx *fx) {
    Chan *chan = &P->Channels[channum];
    if (fx->PortSlide) Portamento(P, (uint16_t)channum, fx, fx->PortSlide, 0);
    if (fx->VolSlide) { int8_t vol = (int8_t)chan->Volume; vol = (int8_t)(vol - fx->VolSlide); if (vol < 0) vol = 0; SetVolume(P, channum, (uint8_t)vol); }
    if (fx->ToneSlideDir) Portamento(P, (uint16_t)channum, fx, fx->ToneSlideDir, 1);
}

static void tone_slide(RadPlayer *P, int channum, Fx *fx, uint8_t param) {
    if (param) fx->ToneSlideSpeed = param;
    GetSlideDir(P, channum, fx);
}

static void PlayNote(RadPlayer *P, int channum, int8_t notenum, int8_t octave, uint16_t instnum, uint8_t cmd, uint8_t param, int src, int op) {
    const RadSong *S = P->S; Chan *chan = &P->Channels[channum];
    if (P->Entrances >= 8) return;                       /* riffs can trigger other riffs: a loop detector */
    P->Entrances++;
    Fx *fx = &chan->FX;
    if (src == SRiff) fx = &chan->Riff.FX; else if (src == SIRiff) fx = &chan->IRiff.FX;
    int transposing = 0;

    if (cmd == cmToneSlide) {                            /* for tone slides the note is the target */
        if (notenum > 0 && notenum <= 12) { fx->ToneSlideOct = (uint8_t)octave; fx->ToneSlideFreq = NoteFreq[notenum - 1]; }
        tone_slide(P, channum, fx, param);
        P->Entrances--; return;
    }

    if (instnum > 0) {
        const RadInst *oldinst = chan->Instrument, *inst = &S->inst[instnum - 1];
        chan->Instrument = inst;
        if (inst->algorithm < 7) {
            LoadInstrumentOPL3(P, channum);
            chan->KeyFlags |= fKeyOff | fKeyOn;          /* bounce the channel */
            ResetFX(&chan->IRiff.FX);
            if (src != SIRiff || inst != oldinst) {
                if (inst->riff && inst->riffspeed > 0) {
                    chan->IRiff.Track = chan->IRiff.TrackStart = inst->riff;
                    chan->IRiff.Line = 0; chan->IRiff.Speed = inst->riffspeed; chan->IRiff.LastInstrument = 0;
                    if (notenum >= 1 && notenum <= 12) { chan->IRiff.TransposeOctave = octave; chan->IRiff.TransposeNote = notenum; transposing = 1; }
                    else { chan->IRiff.TransposeOctave = 3; chan->IRiff.TransposeNote = 12; }
                    chan->IRiff.SpeedCnt = 1;
                    TickRiff(P, channum, &chan->IRiff, 0);
                } else chan->IRiff.SpeedCnt = 0;
            }
        } else chan->Instrument = NULL;                  /* ignore MIDI instruments */
    }

    if (cmd == cmRiff || cmd == cmTranspose) {
        ResetFX(&chan->Riff.FX);
        uint8_t p0 = param / 10, p1 = param % 10;
        chan->Riff.Track = (p1 > 0 && p0 < RAD_RIFFTRACKS) ? S->riffs[p0][p1 - 1] : NULL;
        if (chan->Riff.Track) {
            chan->Riff.TrackStart = chan->Riff.Track;
            chan->Riff.Line = 0; chan->Riff.Speed = P->Speed; chan->Riff.LastInstrument = 0;
            if (cmd == cmTranspose && notenum >= 1 && notenum <= 12) { chan->Riff.TransposeOctave = octave; chan->Riff.TransposeNote = notenum; transposing = 1; }
            else { chan->Riff.TransposeOctave = 3; chan->Riff.TransposeNote = 12; }
            chan->Riff.SpeedCnt = 1;
            TickRiff(P, channum, &chan->Riff, 1);
        } else chan->Riff.SpeedCnt = 0;
    }

    if (!transposing && notenum > 0) {
        if (notenum == 15) chan->KeyFlags |= fKeyOff;
        if (!chan->Instrument || chan->Instrument->algorithm < 7) PlayNoteOPL3(P, channum, octave, notenum);
    }

    switch (cmd) {
    case cmSetVol: SetVolume(P, channum, param); break;
    case cmSetSpeed:
        if (src == SNone) { P->Speed = param; P->SpeedCnt = param; }
        else if (src == SRiff) { chan->Riff.Speed = param; chan->Riff.SpeedCnt = param; }
        else if (src == SIRiff) { chan->IRiff.Speed = param; chan->IRiff.SpeedCnt = param; }
        break;
    case cmPortamentoUp: fx->PortSlide = (int8_t)param; break;
    case cmPortamentoDwn: fx->PortSlide = (int8_t)-(int8_t)param; break;
    case cmToneVolSlide: case cmVolSlide: {
        int8_t val = (int8_t)param;
        if (val >= 50) val = (int8_t)-(val - 50);
        fx->VolSlide = val;
        if (cmd != cmToneVolSlide) break;
        tone_slide(P, channum, fx, param);
        break;
    }
    case cmJumpToLine:
        if (param >= RAD_LINES) break;
        if (src == SNone) P->LineJump = (int8_t)param;    /* jump commands in riffs are checked in TickRiff() */
        break;
    case cmMultiplier:
        if (src == SIRiff) { uint16_t reg = (uint16_t)(0x20 + OpOffsets3[channum][op & 3]); SetOPL3(P, reg, (uint8_t)((GetOPL3(P, reg) & 0xF0) | (param & 15))); }
        break;
    case cmVolume:
        if (src == SIRiff) { uint16_t reg = (uint16_t)(0x40 + OpOffsets3[channum][op & 3]); SetOPL3(P, reg, (uint8_t)((GetOPL3(P, reg) & 0xC0) | ((param & 0x3F) ^ 0x3F))); }
        break;
    case cmFeedback:
        if (src == SIRiff) {
            uint8_t which = param / 10, fb = param % 10;
            if (which == 0) { uint16_t reg = (uint16_t)(0xC0 + Chn2Offsets3[channum]); SetOPL3(P, reg, (uint8_t)((GetOPL3(P, reg) & 0x31) | ((fb & 7) << 1))); }
            else if (which == 1) { uint16_t reg = (uint16_t)(0xC0 + ChanOffsets3[channum]); SetOPL3(P, reg, (uint8_t)((GetOPL3(P, reg) & 0x31) | ((fb & 7) << 1))); }
        }
        break;
    }
    P->Entrances--;
}

static void PlayLine(RadPlayer *P) {
    const RadSong *S = P->S;
    P->SpeedCnt--;
    if (P->SpeedCnt > 0) return;
    P->SpeedCnt = P->Speed;
    for (int i = 0; i < RAD_CHANNELS; i++) ResetFX(&P->Channels[i].FX);
    P->LineJump = -1;
    const uint8_t *trk = P->Track;
    if (trk && (*trk & 0x7F) <= P->Line) {
        uint8_t lineid = *trk++; int last;
        do {
            int channum = *trk & 15;
            Chan *chan = &P->Channels[channum];
            last = unpack_note(S->version, &trk, &chan->LastInstrument, &P->U);
            PlayNote(P, channum, P->U.note, P->U.octave, P->U.inst, P->U.effect, P->U.param, SNone, 0);
        } while (!last);
        if (lineid & 0x80) trk = NULL;
        P->Track = trk;
    }
    P->Line++;
    if (P->Line >= RAD_LINES || P->LineJump >= 0) {
        if (P->LineJump >= 0) P->Line = (uint8_t)P->LineJump; else P->Line = 0;
        P->Order++;
        P->Track = GetTrack(P);
        if (P->Line > 0 && P->Track) P->Track = skip_to_line(S->version, P->Track, P->Line, 0);
    }
}

int rad_update(RadPlayer *P) {
    for (int i = 0; i < RAD_CHANNELS; i++) { Chan *c = &P->Channels[i]; TickRiff(P, i, &c->IRiff, 0); TickRiff(P, i, &c->Riff, 1); }
    PlayLine(P);
    for (int i = 0; i < RAD_CHANNELS; i++) { Chan *c = &P->Channels[i]; ContinueFX(P, i, &c->IRiff.FX); ContinueFX(P, i, &c->Riff.FX); ContinueFX(P, i, &c->FX); }
    P->PlayTime++;
    return !P->Repeating;
}

int rad_position(const RadPlayer *P, int *order, int *line, int *speed) {
    if (order) *order = P->Order;
    if (line) *line = P->Line;
    if (speed) *speed = P->Speed;
    return 1;
}
