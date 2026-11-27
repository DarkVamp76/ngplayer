/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Standard MIDI file reader + player on TinySoundFont (src/core/tsf.h, MIT). Desktop only, see midi.h. */
#include "midi.h"
#if NG_MIDI
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define TSF_IMPLEMENTATION
#include "tsf.h"

#define NCH 16
#define MIDI_BLOCK 512
#define TAIL_SEC 6.0                      /* after the last event the release of the notes may ring on for at most this long */

enum { EV_RESET = 1, EV_SYSEX, EV_TEMPO, EV_NOTEOFF, EV_PROG, EV_CC, EV_BEND, EV_NOTEON };      /* in the order they are handled at one tick (see prio) */
typedef struct { uint32_t tick, seq, val; uint8_t type, ch, a, b; } Ev;

/* ---------------------------------------------------------------- the reverb and the chorus
 * TinySoundFont has no effects, so the patched tsf.h hands every channel's CC 91 / 93 share of the voices to two extra buses.
 * Reverb: Freeverb (Jezar at Dreampoint, public domain): 8 parallel low-pass comb filters + 4 all-pass filters per side.
 * Chorus: three slowly modulated delay lines per side. */
#define FR_NCOMB 8
#define FR_NAP 4
typedef struct { float *buf; int n, i; float store; } FrComb;
typedef struct { float *buf; int n, i; } FrAp;
typedef struct { FrComb comb[2][FR_NCOMB]; FrAp ap[2][FR_NAP]; float feedback, damp; } Reverb;
typedef struct { float *buf[2]; int n, w; float ph[3]; float rate; } Chorus;
static const int FR_COMB_T[FR_NCOMB] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, FR_AP_T[FR_NAP] = { 556, 441, 341, 225 };
static void reverb_init(Reverb *r, int rate, float room, float damp) {
    r->feedback = room * 0.28f + 0.7f; r->damp = damp * 0.4f;
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < FR_NCOMB; i++) { FrComb *k = &r->comb[c][i]; k->n = (int)((FR_COMB_T[i] + c * 23) * (double)rate / 44100.0); k->buf = (float *)calloc((size_t)k->n, sizeof(float)); k->i = 0; k->store = 0; }
        for (int i = 0; i < FR_NAP; i++) { FrAp *k = &r->ap[c][i]; k->n = (int)((FR_AP_T[i] + c * 23) * (double)rate / 44100.0); k->buf = (float *)calloc((size_t)k->n, sizeof(float)); k->i = 0; }
    }
}
static void reverb_free(Reverb *r) {
    for (int c = 0; c < 2; c++) { for (int i = 0; i < FR_NCOMB; i++) free(r->comb[c][i].buf); for (int i = 0; i < FR_NAP; i++) free(r->ap[c][i].buf); }
}
static void reverb_clear(Reverb *r) {
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < FR_NCOMB; i++) { memset(r->comb[c][i].buf, 0, (size_t)r->comb[c][i].n * sizeof(float)); r->comb[c][i].store = 0; }
        for (int i = 0; i < FR_NAP; i++) memset(r->ap[c][i].buf, 0, (size_t)r->ap[c][i].n * sizeof(float));
    }
}
/* in: interleaved stereo send bus, out: interleaved stereo wet signal (the caller scales it) */
static void reverb_process(Reverb *r, const float *in, float *out, int n) {
    const float width = 0.5f, w1 = width * 0.5f + 0.5f, w2 = (1.0f - width) * 0.5f;
    for (int t = 0; t < n; t++) {
        float x = (in[2 * t] + in[2 * t + 1]) * 0.015f * 3.0f, o[2];
        for (int c = 0; c < 2; c++) {
            float acc = 0;
            for (int i = 0; i < FR_NCOMB; i++) {
                FrComb *k = &r->comb[c][i]; float y = k->buf[k->i];
                k->store = y * (1.0f - r->damp) + k->store * r->damp;
                k->buf[k->i] = x + k->store * r->feedback; if (++k->i >= k->n) k->i = 0;
                acc += y;
            }
            for (int i = 0; i < FR_NAP; i++) {
                FrAp *k = &r->ap[c][i]; float b = k->buf[k->i], y = -acc + b;
                k->buf[k->i] = acc + b * 0.5f; if (++k->i >= k->n) k->i = 0;
                acc = y;
            }
            o[c] = acc;
        }
        out[2 * t] = o[0] * w1 + o[1] * w2; out[2 * t + 1] = o[1] * w1 + o[0] * w2;
    }
}
static void chorus_init(Chorus *c, int rate) {
    c->n = (int)(0.06 * rate); c->w = 0; c->rate = 0.8f;
    for (int i = 0; i < 2; i++) c->buf[i] = (float *)calloc((size_t)c->n, sizeof(float));
    c->ph[0] = 0.0f; c->ph[1] = 0.33f; c->ph[2] = 0.66f;
}
static void chorus_free(Chorus *c) { free(c->buf[0]); free(c->buf[1]); }
static void chorus_clear(Chorus *c) { memset(c->buf[0], 0, (size_t)c->n * sizeof(float)); memset(c->buf[1], 0, (size_t)c->n * sizeof(float)); }
static void chorus_process(Chorus *c, const float *in, float *out, int n, int rate) {
    for (int t = 0; t < n; t++) {
        c->buf[0][c->w] = in[2 * t]; c->buf[1][c->w] = in[2 * t + 1];
        float l = 0, r = 0;
        for (int v = 0; v < 3; v++) {
            c->ph[v] += c->rate / (float)rate; if (c->ph[v] >= 1.0f) c->ph[v] -= 1.0f;
            float d = (0.012f + 0.004f * sinf(6.2831853f * c->ph[v])) * (float)rate, rp = (float)c->w - d; if (rp < 0) rp += (float)c->n;
            int i0 = (int)rp, i1 = i0 + 1 >= c->n ? 0 : i0 + 1; float fr = rp - (float)i0;
            float a = c->buf[0][i0] * (1 - fr) + c->buf[0][i1] * fr, b = c->buf[1][i0] * (1 - fr) + c->buf[1][i1] * fr;
            if (v == 1) { l += b; r += a; } else { l += a; r += b; }                          /* the middle line swaps the sides */
        }
        out[2 * t] = l * 0.33f; out[2 * t + 1] = r * 0.33f;
        if (++c->w >= c->n) c->w = 0;
    }
}


struct Midi {
    tsf *f;
    int rate;
    Ev *ev; size_t nev;
    uint64_t *frame;                      /* the time of each event in output frames */
    size_t next;                          /* the next event to play */
    uint64_t pos;                         /* frames played */
    uint64_t tail;                        /* frames played after the last event */
    double length;
    MidiInfo info;
    int notes_ch[NCH];
    int sx_mt32, sx_gs, sx_xg, sx_gm, sx_gm2;                            /* SysEx messages of each kind found in the file (what the file was written for) */
    char lcd[24];
    uint8_t *syx; size_t syx_len; char syx_name[48]; int syx_msgs;      /* the .syx file next to the MIDI file */
    uint8_t *sxp; size_t sxn, sxcap;                                     /* every SysEx message of the file as it is (2 bytes length, then F0 .. F7): for the hardware output */
    int hw_ready, hw_mode, hw_setup_done; double hw_base, hw_pos0, hw_sx_free, hw_setup_end; size_t hw_next;      /* hw_next: the next event for the hardware (the SoundFont synth runs on, silent, for the scopes and has its own m->next) */
    size_t setup_end;                                                    /* the index of the first note: what comes before it is the set-up of the file */                      /* the hardware output: the queue time of the song time hw_pos0 */
    unsigned mute;
    int prog[NCH], bank[NCH], vol[NCH], pan[NCH];
    float lvl[NCH];
    int8_t mtrev[3];                      /* the MT-32 reverb of the file: mode (0 room, 1 hall, 2 plate, 3 tap delay), time 0..7, level 0..7 (-1: not set) */
    uint8_t cc1[NCH];                     /* the modulation wheel (the synth does not keep it) */
    float aout[NCH];
    uint32_t *tm_tick; double *tm_sec, *tm_us; size_t ntm;       /* the tempo map: tick, seconds at that tick, microseconds per quarter note */
    double spt;                           /* SMPTE division: seconds per tick (else 0) */
    int div, measures;
    MidiNote *notes; size_t nnotes; double maxdur;
    Reverb rev; Chorus cho; float rev_level, cho_level;
    float rev_base, cho_base, mvol;       /* the start values of the reverb / chorus level and the GS master volume (a GS reset sets them back) */
    uint8_t drumch[NCH];                  /* the channel plays a drum kit: 10, and the parts a GS file turns into rhythm parts (SysEx 40 1x 15) */
    float dry[2 * MIDI_BLOCK], wrev[2 * MIDI_BLOCK], wcho[2 * MIDI_BLOCK], orev[2 * MIDI_BLOCK], ocho[2 * MIDI_BLOCK];
    float scblk[NCH * MIDI_BLOCK];        /* the dry signal of each channel for one block (tsf scopeBlk) */
    float *scope; int scope_len; unsigned scope_pos;
};

static const char *const GM_NAMES[128] = {
"Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano","Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
"Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
"Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion",
"Nylon Guitar","Steel Guitar","Jazz Guitar","Clean Guitar","Muted Guitar","Overdriven Guitar","Distortion Guitar","Guitar Harmonics",
"Acoustic Bass","Fingered Bass","Picked Bass","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
"Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
"String Ensemble 1","String Ensemble 2","Synth Strings 1","Synth Strings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
"Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
"Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet",
"Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
"Square Lead","Sawtooth Lead","Calliope Lead","Chiff Lead","Charang Lead","Voice Lead","Fifths Lead","Bass + Lead",
"New Age Pad","Warm Pad","Polysynth Pad","Choir Pad","Bowed Pad","Metallic Pad","Halo Pad","Sweep Pad",
"Rain FX","Soundtrack FX","Crystal FX","Atmosphere FX","Brightness FX","Goblins FX","Echoes FX","Sci-Fi FX",
"Sitar","Banjo","Shamisen","Koto","Kalimba","Bagpipe","Fiddle","Shanai",
"Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
"Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot" };
const char *midi_gm_name(int program, int drum) { return drum ? "Drum Kit" : (program >= 0 && program < 128 ? GM_NAMES[program] : ""); }


/* ---------------------------------------------------------------- the SoundFont */
static char g_sf_set[512];
static char g_sf_found[512];
static tsf *g_base;                       /* the loaded SoundFont, shared by all songs (tsf_copy shares the samples) */
#if NG_HWMIDI
static MidiHw *g_hw;                      /* the MIDI hardware port while the hardware is the output */
#endif

void midi_set_soundfont(const char *path) {
    snprintf(g_sf_set, sizeof g_sf_set, "%s", path ? path : "");
    if (g_base) { tsf_close(g_base); g_base = NULL; }
    g_sf_found[0] = 0;
}
static int exists(const char *p) { return p && p[0] && access(p, R_OK) == 0; }
static void exe_dir(char *out, size_t cap) {
    out[0] = 0;
#ifdef __EMSCRIPTEN__
    (void)cap;                                                                  /* the web player has no executable: the page puts the SoundFont into /soundfont/ */
#else
#ifdef __APPLE__
    char b[1024]; uint32_t n = sizeof b;
    if (_NSGetExecutablePath(b, &n) != 0) return;
#else
    char b[1024]; ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    if (n <= 0) return; b[n] = 0;
#endif
    char *s = strrchr(b, '/'); if (s) *s = 0;
    snprintf(out, cap, "%s", b);
#endif
}
const char *midi_soundfont_path(void) {
    if (g_sf_found[0]) return g_sf_found;
    char c[640], d[640]; const char *h = getenv("HOME"), *e = getenv("NGPLAYER_SOUNDFONT");
#ifdef __EMSCRIPTEN__
    if (exists("/soundfont/GeneralUser-GS.sf2")) { snprintf(g_sf_found, sizeof g_sf_found, "%s", "/soundfont/GeneralUser-GS.sf2"); return g_sf_found; }     /* the page fetches it into the in-memory file system */
#endif
    if (g_sf_set[0] && exists(g_sf_set)) { snprintf(g_sf_found, sizeof g_sf_found, "%s", g_sf_set); return g_sf_found; }
    if (exists(e)) { snprintf(g_sf_found, sizeof g_sf_found, "%s", e); return g_sf_found; }
    exe_dir(d, sizeof d);
    const char *name = "GeneralUser-GS.sf2";
    const char *dirs[] = { "%s/../share/ngplayer", "%s/../Resources", "%s", NULL };      /* AppImage / install prefix, the Mac app bundle, next to the program */
    for (int i = 0; dirs[i] && d[0]; i++) {
        char dd[700]; snprintf(dd, sizeof dd, dirs[i], d);
        snprintf(c, sizeof c, "%s/%s", dd, name);
        if (exists(c)) { snprintf(g_sf_found, sizeof g_sf_found, "%s", c); return g_sf_found; }
    }
    static const char *const fixed[] = { "/usr/local/share/ngplayer/GeneralUser-GS.sf2", "/usr/share/ngplayer/GeneralUser-GS.sf2",
        "/usr/share/soundfonts/GeneralUser-GS.sf2", "/usr/share/soundfonts/default.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2", NULL };
    if (h) { snprintf(c, sizeof c, "%s/.local/share/ngplayer/%s", h, name); if (exists(c)) { snprintf(g_sf_found, sizeof g_sf_found, "%s", c); return g_sf_found; } }
    for (int i = 0; fixed[i]; i++) if (exists(fixed[i])) { snprintf(g_sf_found, sizeof g_sf_found, "%s", fixed[i]); return g_sf_found; }
    return NULL;
}
static tsf *soundfont(char *err, size_t cap) {
    if (g_base) return g_base;
    const char *p = midi_soundfont_path();
    if (!p) { snprintf(err, cap, "no SoundFont found (GeneralUser-GS.sf2; or set NGPLAYER_SOUNDFONT)"); return NULL; }
    g_base = tsf_load_filename(p);
    if (!g_base) snprintf(err, cap, "cannot read the SoundFont %s", p);
    return g_base;
}

/* ---------------------------------------------------------------- the file */
static int vlq(const uint8_t *b, size_t n, size_t *p, uint32_t *v) {
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        if (*p >= n) return 0;
        uint8_t c = b[(*p)++]; r = (r << 7) | (c & 0x7F);
        if (!(c & 0x80)) { *v = r; return 1; }
    }
    return 0;
}
static const uint8_t *smf_start(const uint8_t *b, size_t n, size_t *len) {
    if (n >= 14 && !memcmp(b, "MThd", 4)) { *len = n; return b; }
    if (n >= 20 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "RMID", 4)) {             /* RIFF RMID: a "data" chunk holds the file */
        size_t p = 12;
        while (p + 8 <= n) {
            uint32_t sz = (uint32_t)b[p + 4] | (uint32_t)b[p + 5] << 8 | (uint32_t)b[p + 6] << 16 | (uint32_t)b[p + 7] << 24;
            if (!memcmp(b + p, "data", 4) && p + 8 + 14 <= n && !memcmp(b + p + 8, "MThd", 4)) { *len = n - (p + 8); if (sz < *len) *len = sz; return b + p + 8; }
            p += 8 + (size_t)sz + (sz & 1);
        }
    }
    return NULL;
}
int midi_probe(const uint8_t *b, size_t n) { size_t l; return smf_start(b, n, &l) != NULL; }

static void evpush(Midi *m, size_t *cap, uint32_t tick, uint8_t type, uint8_t ch, uint8_t a, uint8_t bb, uint32_t val, uint32_t *seq) {
    if (m->nev == *cap) { *cap = *cap ? *cap * 2 : 4096; m->ev = (Ev *)realloc(m->ev, *cap * sizeof(Ev)); }
    Ev *e = &m->ev[m->nev++]; e->tick = tick; e->type = type; e->ch = ch; e->a = a; e->b = bb; e->val = val; e->seq = (*seq)++;
}
static void textcpy(char *dst, size_t cap, const uint8_t *s, uint32_t n) {
    size_t o = 0; for (uint32_t i = 0; i < n && o + 1 < cap; i++) dst[o++] = (s[i] >= 32 && s[i] < 127) ? (char)s[i] : (s[i] >= 0xA0 ? (char)s[i] : ' ');
    while (o > 0 && dst[o - 1] == ' ') o--; dst[o] = 0;
    size_t k = 0; while (dst[k] == ' ') k++; if (k) memmove(dst, dst + k, strlen(dst + k) + 1);
}
static int prio(const Ev *e) { return e->type; }             /* EV_RESET < tempo < note off < program < controller < bend < note on */
static int evcmp(const void *x, const void *y) {
    const Ev *a = (const Ev *)x, *b = (const Ev *)y;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    if (prio(a) != prio(b)) return prio(a) < prio(b) ? -1 : 1;
    return a->seq < b->seq ? -1 : a->seq > b->seq;
}

/* ---------------------------------------------------------------- the channels */
static void chan_reset(Midi *m, int ch) {
    tsf_channel_midi_control(m->f, ch, 121, 0);
    tsf_channel_drum_reset(m->f, ch);
    tsf_channel_set_pitchwheel(m->f, ch, 8192);
    tsf_channel_sounds_off_all(m->f, ch);
    tsf_channel_set_bank(m->f, ch, 0);
    tsf_channel_set_presetnumber(m->f, ch, 0, m->drumch[ch]);
    tsf_channel_set_keyshift(m->f, ch, 0.0f);
    tsf_channel_midi_control(m->f, ch, 7, 100); tsf_channel_midi_control(m->f, ch, 10, 64);      /* the General MIDI defaults: volume 100, pan centre */
    m->prog[ch] = 0; m->bank[ch] = 0; m->vol[ch] = 100; m->pan[ch] = 64; m->cc1[ch] = 0;
}
static void all_reset(Midi *m) {
    m->mvol = 1.0f; m->rev_level = m->rev_base; m->cho_level = m->cho_base; m->rev.feedback = 0.7f * 0.28f + 0.7f;      /* (the start values of the effects) */
    memset(m->drumch, 0, sizeof m->drumch); m->drumch[9] = 1;
    for (int c = 0; c < NCH; c++) chan_reset(m, c);
}
/* the Roland GS parameters the synth follows (SysEx 41 dev 42 12 aa aa aa data cs F7): master volume 40 00 04, reverb level 40 01 33 and time 40 01 34, chorus level 40 01 3A (the reverb and chorus macros and the other
 * effect parameters are not used: one reverb and one chorus), and per part (blocks 40 1x: 10 is part 10, 11 .. 19 the parts 1 .. 9, 1A .. 1F the parts 11 .. 16): 15 use for rhythm part, 16 pitch key shift */
static void gs_param(Midi *m, unsigned a, int v) {
    if (a == 0x400004) m->mvol = (v / 127.0f) * (v / 127.0f);
    else if (a == 0x400133) m->rev_level = m->rev_base * (v / 64.0f);
    else if (a == 0x400134) { float room = 0.7f + (v - 64) / 64.0f * 0.28f; if (room < 0.05f) room = 0.05f; if (room > 0.99f) room = 0.99f; m->rev.feedback = room * 0.28f + 0.7f; }
    else if (a == 0x40013A) m->cho_level = m->cho_base * (v / 64.0f);
    else if ((a >> 16) == 0x40 && ((a >> 8) & 0xF0) == 0x10) {
        unsigned blk = (a >> 8) & 0xFF, off = a & 0xFF; int ch = blk == 0x10 ? 9 : blk <= 0x19 ? (int)blk - 0x11 : (int)blk - 0x1A + 10;
        if (ch < 0 || ch >= NCH) return;
        if (off == 0x15) { int dr = v != 0; if (dr != m->drumch[ch]) { m->drumch[ch] = (uint8_t)dr; tsf_channel_set_presetnumber(m->f, ch, m->prog[ch], dr); } }
        else if (off == 0x16 && v >= 0x28 && v <= 0x58) tsf_channel_set_keyshift(m->f, ch, (float)(v - 0x40));
    }
}
static void apply_sysex(Midi *m, const Ev *e) {
    const uint8_t *q = m->sxp + e->val; int n = (q[0] | (q[1] << 8)) - 1; const uint8_t *p = q + 3;      /* p: the bytes after the F0, ending with F7 */
    if (n >= 9 && p[0] == 0x41 && p[2] == 0x42 && p[3] == 0x12) {
        unsigned a = (unsigned)p[4] << 16 | (unsigned)p[5] << 8 | p[6];
        for (int k = 0; k < n - 9; k++) gs_param(m, a + (unsigned)k, p[7 + k]);
    }
}

static void apply(Midi *m, const Ev *e, int with_notes) {
    int c = e->ch;
    switch (e->type) {
    case EV_RESET: all_reset(m); break;
    case EV_NOTEON:
        if (!with_notes || ((m->mute >> c) & 1)) break;
        tsf_channel_note_on(m->f, c, e->a, e->b / 127.0f);
        if (e->b / 127.0f > m->lvl[c]) m->lvl[c] = e->b / 127.0f;
        break;
    case EV_NOTEOFF: if (with_notes) tsf_channel_note_off(m->f, c, e->a); break;
    case EV_PROG:
        m->prog[c] = e->a;
        tsf_channel_set_presetnumber(m->f, c, e->a, m->drumch[c]);
        break;
    case EV_SYSEX: apply_sysex(m, e); break;
    case EV_CC:
        if (e->a == 0 || e->a == 32) m->bank[c] = e->a == 0 ? e->b : m->bank[c];
        if (e->a == 7) m->vol[c] = e->b;
        if (e->a == 1) m->cc1[c] = (uint8_t)e->b;
        if (e->a == 10) m->pan[c] = e->b;
        if (m->sx_mt32 && ((e->a >= 71 && e->a <= 75) || e->a == 98 || e->a == 99)) break;      /* the Roland GS sound controllers and NRPN do not exist on the MT-32 (ICEMAN uses CC 75 for something else) */
        tsf_channel_midi_control(m->f, c, e->a, e->b);
        break;
    case EV_BEND: tsf_channel_set_pitchwheel(m->f, c, e->a | (e->b << 7)); break;
    }
}

/* which standard a SysEx message (the bytes after F0, ending with F7) says the file was written for: Roland MT-32 / GS, Yamaha XG, General MIDI (1 / 2); the display text of the MT-32 / Sound Canvas */
static void sysex_note(Midi *m, const uint8_t *d, uint32_t sl) {
    if (sl < 5) return;
    if (d[0] == 0x41 && sl >= 6) {
        if (d[2] == 0x16) m->sx_mt32++;                                 /* the MT-32 has its own model id */
        else if (d[2] == 0x42 || d[2] == 0x45) m->sx_gs++;              /* Sound Canvas (GS) and its display */
        if (d[2] == 0x16 && d[3] == 0x12 && sl >= 10 && d[4] == 0x10 && d[5] == 0x00) {      /* the MT-32 system area (10 00 xx): 00 master tune, 01 reverb mode, 02 reverb time, 03 reverb level; the last message of the file counts */
            for (uint32_t k = 0; k < sl - 9; k++) { unsigned ad = (unsigned)d[6] + k; if (ad >= 1 && ad <= 3) m->mtrev[ad - 1] = (int8_t)d[7 + k]; }
        }
        if (d[2] == 0x16 && d[3] == 0x12 && sl >= 10 && d[4] == 0x20 && d[5] == 0 && d[6] == 0 && !m->lcd[0]) textcpy(m->lcd, sizeof m->lcd, d + 7, sl - 9 > 20 ? 20 : sl - 9);        /* the MT-32 display message */
        else if (d[2] == 0x45 && d[3] == 0x12 && sl >= 10 && d[4] == 0x10 && d[5] == 0 && d[6] == 0 && !m->lcd[0]) textcpy(m->lcd, sizeof m->lcd, d + 7, sl - 9 > 23 ? 23 : sl - 9);   /* the Sound Canvas display message */
    } else if (d[0] == 0x7E && d[2] == 0x09) { if (d[3] == 0x01) m->sx_gm++; else if (d[3] == 0x03) m->sx_gm2++; }
    else if (d[0] == 0x43 && d[2] == 0x4C) m->sx_xg++;
}
/* a .syx file that belongs to the MIDI file (the same name next to it): its messages come first (the MT-32 and GS setups live there) */
void midi_add_syx(Midi *m, const char *name, const uint8_t *b, size_t n) {
    free(m->syx); m->syx = (uint8_t *)malloc(n ? n : 1); memcpy(m->syx, b, n); m->syx_len = n; m->lcd[0] = 0;       /* its display text wins */
    snprintf(m->syx_name, sizeof m->syx_name, "%s", name); m->syx_msgs = 0;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != 0xF0) continue;
        size_t j = i + 1; while (j < n && b[j] != 0xF7) j++;
        if (j < n) { sysex_note(m, b + i + 1, (uint32_t)(j - i)); m->syx_msgs++; i = j; }
    }
}

/* ---------------------------------------------------------------- open */
Midi *midi_open(const uint8_t *buf, size_t total, int rate, char *err, size_t errcap) {
    size_t n; const uint8_t *b = smf_start(buf, total, &n);
    if (!b) { snprintf(err, errcap, "not a MIDI file"); return NULL; }
    int fmt = b[8] << 8 | b[9], ntrk = b[10] << 8 | b[11], div = b[12] << 8 | b[13];
    if (div == 0) { snprintf(err, errcap, "bad MIDI time division"); return NULL; }
    tsf *base = soundfont(err, errcap);
    if (!base) return NULL;
    Midi *m = (Midi *)calloc(1, sizeof *m);
    memset(m->mtrev, -1, sizeof m->mtrev);
    m->rate = rate; m->info.format = fmt; m->info.ntracks = ntrk; m->info.division = div; m->info.tempo_bpm = 120;
    size_t cap = 0, p = 8 + (b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7]); uint32_t seq = 0;
    int got_title = 0, got_tempo = 0;
    for (int t = 0; t < ntrk && p + 8 <= n; t++) {
        uint32_t len = (uint32_t)b[p + 4] << 24 | (uint32_t)b[p + 5] << 16 | (uint32_t)b[p + 6] << 8 | b[p + 7];
        if (memcmp(b + p, "MTrk", 4)) { p += 8 + (size_t)len; t--; continue; }          /* an unknown chunk: skip it */
        size_t q = p + 8, end = q + len; if (end > n) end = n; p = end;
        uint32_t tick = 0; uint8_t run = 0;
        while (q < end) {
            uint32_t dt; if (!vlq(b, end, &q, &dt)) break;
            tick += dt; if (q >= end) break;
            uint8_t st = b[q];
            if (st < 0x80) { if (!run) break; st = run; } else { q++; if (st < 0xF0) run = st; }
            if (st == 0xFF) {
                if (q >= end) break;
                uint8_t mt = b[q++]; uint32_t ml; if (!vlq(b, end, &q, &ml) || q + ml > end) break;
                if (mt == 0x51 && ml == 3) { uint32_t us = (uint32_t)b[q] << 16 | b[q + 1] << 8 | b[q + 2]; if (us) { evpush(m, &cap, tick, EV_TEMPO, 0, 0, 0, us, &seq); if (!got_tempo) { got_tempo = 1; m->info.tempo_bpm = (int)(60000000.0 / us + 0.5); } } }
                else if (mt == 0x03 && !got_title && tick == 0) { textcpy(m->info.title, sizeof m->info.title, b + q, ml); if (m->info.title[0]) got_title = 1; }
                else if (mt == 0x02 && !m->info.copyright[0]) textcpy(m->info.copyright, sizeof m->info.copyright, b + q, ml);
                else if (mt == 0x2F) { q += ml; break; }
                q += ml;
            } else if (st == 0xF0 || st == 0xF7) {
                uint32_t sl; if (!vlq(b, end, &q, &sl) || q + sl > end) break;
                const uint8_t *d = b + q;
                if (st == 0xF0) {
                    sysex_note(m, d, sl);
                    if (sl >= 2 && sl < 65000 && d[sl - 1] == 0xF7) {                       /* the whole message, for the hardware */
                        if (m->sxn + sl + 3 > m->sxcap) { m->sxcap = (m->sxcap + sl + 3) * 2; m->sxp = (uint8_t *)realloc(m->sxp, m->sxcap); }
                        size_t off = m->sxn; m->sxp[off] = (uint8_t)((sl + 1) & 0xFF); m->sxp[off + 1] = (uint8_t)((sl + 1) >> 8); m->sxp[off + 2] = 0xF0;   /* the length counts the F0 too */ memcpy(m->sxp + off + 3, d, sl); m->sxn += sl + 3;
                        evpush(m, &cap, tick, EV_SYSEX, 0, 0, 0, (uint32_t)off, &seq);
                    }
                }
                if (st == 0xF0) {                                                       /* the resets: General MIDI / GM2, Roland GS, Yamaha XG */
                    if ((sl >= 5 && d[0] == 0x7E && d[2] == 0x09 && (d[3] == 0x01 || d[3] == 0x03)) ||
                        (sl >= 10 && d[0] == 0x41 && d[2] == 0x42 && d[3] == 0x12 && d[4] == 0x40 && d[5] == 0x00 && d[6] == 0x7F) ||
                        (sl >= 8 && d[0] == 0x43 && d[2] == 0x4C && d[3] == 0x00 && d[4] == 0x00 && d[5] == 0x7E))
                        evpush(m, &cap, tick, EV_RESET, 0, 0, 0, 0, &seq);
                }
                q += sl;
            } else if (st >= 0x80 && st < 0xF0) {
                int two = !((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0);
                if (q + (two ? 2 : 1) > end) break;
                uint8_t d1 = b[q++] & 0x7F, d2 = two ? b[q++] & 0x7F : 0, ch = st & 15;
                switch (st & 0xF0) {
                case 0x80: evpush(m, &cap, tick, EV_NOTEOFF, ch, d1, 0, 0, &seq); break;
                case 0x90: if (d2) { evpush(m, &cap, tick, EV_NOTEON, ch, d1, d2, 0, &seq); m->notes_ch[ch]++; m->info.notes++; } else evpush(m, &cap, tick, EV_NOTEOFF, ch, d1, 0, 0, &seq); break;
                case 0xB0: evpush(m, &cap, tick, EV_CC, ch, d1, d2, 0, &seq); break;
                case 0xC0: evpush(m, &cap, tick, EV_PROG, ch, d1, 0, 0, &seq); break;
                case 0xE0: evpush(m, &cap, tick, EV_BEND, ch, d1, d2, 0, &seq); break;
                }
            } else break;                                                               /* a system message that has no place in a file */
        }
    }
    if (!m->info.notes) { snprintf(err, errcap, "the MIDI file has no notes"); free(m->ev); free(m); return NULL; }
    qsort(m->ev, m->nev, sizeof(Ev), evcmp);
    m->setup_end = m->nev; for (size_t k = 0; k < m->nev; k++) if (m->ev[k].type == EV_NOTEON) { m->setup_end = k; break; }
    /* the time of every event: walk the tempo map */
    m->frame = (uint64_t *)malloc(m->nev * sizeof(uint64_t));
    double sec = 0, tempo = 500000.0, spt = (div & 0x8000) ? 1.0 / ((double)(256 - (div >> 8)) * (double)(div & 0xFF)) : 0;       /* SMPTE: seconds per tick */
    uint32_t lt = 0;
    m->spt = spt; m->div = (div & 0x8000) ? 480 : div;
    m->tm_tick = (uint32_t *)malloc((m->nev + 1) * sizeof(uint32_t)); m->tm_sec = (double *)malloc((m->nev + 1) * sizeof(double)); m->tm_us = (double *)malloc((m->nev + 1) * sizeof(double));
    m->tm_tick[0] = 0; m->tm_sec[0] = 0; m->tm_us[0] = tempo; m->ntm = 1;
    for (size_t i = 0; i < m->nev; i++) {
        double d = (double)(m->ev[i].tick - lt);
        sec += (div & 0x8000) ? d * spt : d * tempo / 1e6 / (double)div;
        lt = m->ev[i].tick;
        if (m->ev[i].type == EV_TEMPO) {
            tempo = (double)m->ev[i].val;
            if (m->ntm && m->tm_tick[m->ntm - 1] == m->ev[i].tick) m->tm_us[m->ntm - 1] = tempo;            /* a tempo change at the same tick replaces the earlier one */
            else { m->tm_tick[m->ntm] = m->ev[i].tick; m->tm_sec[m->ntm] = sec; m->tm_us[m->ntm] = tempo; m->ntm++; }
        }
        m->frame[i] = (uint64_t)(sec * rate + 0.5);
    }
    m->length = sec;
    uint32_t last_note_tick = 0;
    for (size_t i = 0; i < m->nev; i++) if ((m->ev[i].type == EV_NOTEON || m->ev[i].type == EV_NOTEOFF) && m->ev[i].tick > last_note_tick) last_note_tick = m->ev[i].tick;
    { uint32_t mt = (uint32_t)m->div * 4; m->measures = (int)(last_note_tick / mt) + 1; }
    /* the notes: pair every note on with its note off (the same channel and key; an overlap closes the earlier note) */
    m->notes = (MidiNote *)malloc((size_t)m->info.notes * sizeof(MidiNote));
    { static int open[NCH][128]; memset(open, 0, sizeof open);                               /* index + 1 of the note that is still sounding */
      for (size_t i = 0; i < m->nev; i++) {
        const Ev *e = &m->ev[i]; double t = (double)m->frame[i] / rate;
        if (e->type == EV_NOTEON && m->nnotes < (size_t)m->info.notes) {
            int *o = &open[e->ch][e->a];
            if (*o) m->notes[*o - 1].t1 = t;
            MidiNote *nt = &m->notes[m->nnotes++]; nt->t0 = t; nt->t1 = m->length; nt->ch = e->ch; nt->key = e->a; nt->vel = e->b;
            *o = (int)m->nnotes;
        } else if (e->type == EV_NOTEOFF) {
            int *o = &open[e->ch][e->a];
            if (*o) { m->notes[*o - 1].t1 = t; double d = t - m->notes[*o - 1].t0; if (d > m->maxdur) m->maxdur = d; *o = 0; }
        }
      }
      for (size_t i = 0; i < m->nnotes; i++) { double d = m->notes[i].t1 - m->notes[i].t0; if (d > m->maxdur) m->maxdur = d; } }
    m->f = tsf_copy(base);
    tsf_set_output(m->f, TSF_STEREO_INTERLEAVED, rate, -9.0f);
    tsf_set_max_voices(m->f, 256);
    { const char *e = getenv("NGPLAYER_REVERB"); m->rev_level = e ? (float)atof(e) : 0.22f; e = getenv("NGPLAYER_CHORUS"); m->cho_level = e ? (float)atof(e) : 0.6f; m->rev_base = m->rev_level; m->cho_base = m->cho_level; m->mvol = 1.0f; m->drumch[9] = 1; }
    reverb_init(&m->rev, rate, 0.7f, 0.5f); chorus_init(&m->cho, rate);
    all_reset(m);
    return m;
}
void midi_close(Midi *m) {
    if (!m) return;
    if (m->f) tsf_close(m->f);
    if (m->rev.comb[0][0].buf) reverb_free(&m->rev);
    if (m->cho.buf[0]) chorus_free(&m->cho);
#if NG_HWMIDI
    if (m->hw_ready && g_hw) { midi_hw_drop(g_hw); midi_hw_panic(g_hw); }
#endif
    free(m->sxp); free(m->syx); free(m->ev); free(m->frame); free(m->tm_tick); free(m->tm_sec); free(m->tm_us); free(m->notes); free(m);
}


/* ---------------------------------------------------------------- the output: SoundFont or MIDI hardware */
int midi_set_hardware(int on, const char *want) {
#if NG_HWMIDI
    if (on) { if (!g_hw) g_hw = midi_hw_open(want); return g_hw != NULL; }
    if (g_hw) { midi_hw_close(g_hw); g_hw = NULL; }
    return 0;
#else
    (void)on; (void)want; return 0;
#endif
}
int midi_hardware_on(void) {
#if NG_HWMIDI
    return g_hw != NULL;
#else
    return 0;
#endif
}
int midi_activity(void) {                      /* the MIDI lamp: 1 while the hardware gets a message */
#if NG_HWMIDI
    return g_hw ? midi_hw_activity(g_hw, 0.06) : 0;
#else
    return 0;
#endif
}
const char *midi_output_name(void) {
#if NG_HWMIDI
    if (g_hw) return midi_hw_name(g_hw);
#endif
    return "SoundFont";
}
void midi_idle(Midi *m) {
#if NG_HWMIDI
    if (g_hw && m->hw_ready && m->pos > 0) { midi_hw_drop(g_hw); midi_hw_panic(g_hw); m->hw_ready = 0; }      /* pause / end: no hanging notes; the next render starts again from this position (at the very start the set-up goes on: nothing sounds yet) */
#else
    (void)m;
#endif
}
#if NG_HWMIDI
/* a SysEx message takes its time on the wire (31250 baud: 0.32 ms per byte) and the device needs a moment to store it (the MT-32 drops what comes too fast), so the messages
 * are spaced by their own length plus a pause */
static double sx_gap_base(void) { static double g = -1; if (g < 0) { const char *e = getenv("NGPLAYER_MIDI_SXGAP_MS"); g = (e ? atof(e) : 50.0) / 1000.0; } return g; }      /* the pause after a SysEx message: 50 ms like the maintainer's midiplay, NGPLAYER_MIDI_SXGAP_MS changes it */
static double sx_gap_msg(const uint8_t *p, int len) {               /* p: the message with its F0; len: its length */
    double g = (double)len * 0.00032 + sx_gap_base();
    if (len >= 9 && p[0] == 0xF0 && p[1] == 0x41 && p[3] == 0x16 && p[4] == 0x12 && p[5] == 0x7F && p[6] == 0 && p[7] == 0) g += 0.4;        /* the MT-32 reset (7F 00 00) takes its time */
    return g;
}
/* what the hardware needs: once per song the GM reset (not for the MT-32), the .syx next to the file and the set-up messages before the first note, in order and spaced; and every time
 * (start, seek, resume) the state of the channels up to the song position: the LAST program, controllers and pitch bend of each channel (not everything that came on the way, and not the
 * set-up again and no SysEx at all: they are an INIT, sent once at the start; the device keeps what it has been told). Then the time of the song in the queue. */
static void hw_resync(Midi *m) {
    midi_hw_drop(g_hw); midi_hw_panic(g_hw);
    if (m->hw_setup_done && midi_hw_time(g_hw) < m->hw_setup_end) m->hw_setup_done = 0;        /* the drop above threw away a set-up that was still waiting in the queue: send it again */
    double t = midi_hw_time(g_hw) + 0.06;
    m->hw_pos0 = (double)m->pos / m->rate; m->hw_sx_free = 0;
    if (!m->hw_setup_done) {
        if (m->sx_mt32) { static const unsigned char rs[11] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x01, 0x00, 0xF7 }; midi_hw_send(g_hw, rs, 11, t); t += sx_gap_msg(rs, 11); }      /* an MT-32: reset all parameters first, so that it starts clean (and the set-up that follows is all it has) */
        else { static const unsigned char gm[6] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }; midi_hw_send(g_hw, gm, 6, t); t += sx_gap_base() + 0.08; }
        for (size_t k = 0; k < m->syx_len; k++) {
            if (m->syx[k] != 0xF0) continue;
            size_t j = k + 1; while (j < m->syx_len && m->syx[j] != 0xF7) j++;
            if (j < m->syx_len) { midi_hw_send(g_hw, m->syx + k, (int)(j - k + 1), t); t += sx_gap_msg(m->syx + k, (int)(j - k + 1)); k = j; }
        }
        for (size_t j = 0; j < m->setup_end; j++) {                              /* the SysEx messages before the first note: the set-up of the file */
            const Ev *e = &m->ev[j];
            if (e->type == EV_SYSEX) {
                const uint8_t *p = m->sxp + e->val; int n = p[0] | (p[1] << 8), dup = 0;
                for (size_t k = 0; k < m->syx_len && !dup; k++) {                /* the same message was in the .syx already: not again */
                    if (m->syx[k] != 0xF0) continue;
                    size_t q = k + 1; while (q < m->syx_len && m->syx[q] != 0xF7) q++;
                    if (q < m->syx_len && (int)(q - k + 1) == n && !memcmp(m->syx + k, p + 2, (size_t)n)) dup = 1;
                    k = q;
                }
                if (dup) continue;
                midi_hw_send(g_hw, p + 2, n, t); t += sx_gap_msg(p + 2, n);
            }
        }
        m->hw_setup_done = 1; t += 0.1; m->hw_setup_end = t;
    }
    int prog[NCH], bend[NCH][2], cc[NCH][128];
    for (int c = 0; c < NCH; c++) { prog[c] = -1; bend[c][0] = -1; for (int k = 0; k < 128; k++) cc[c][k] = -1; }
    size_t i = 0;
    for (; i < m->nev; i++) {
        const Ev *e = &m->ev[i];
        if (i >= m->setup_end && m->frame[i] >= m->pos) break;                  /* up to the song position (and always the events before the first note) */
        if (e->type == EV_PROG) prog[e->ch] = e->a;
        else if (e->type == EV_CC) cc[e->ch][e->a] = e->b;
        else if (e->type == EV_BEND) { bend[e->ch][0] = e->a; bend[e->ch][1] = e->b; }
    }
    for (int c = 0; c < NCH; c++) {
        unsigned char b[3];
        static const int first[2] = { 0, 32 };                                  /* bank select first, then the other controllers, then the program (it needs the bank), then the bend */
        for (int k = 0; k < 2; k++) if (cc[c][first[k]] >= 0) { b[0] = (unsigned char)(0xB0 | c); b[1] = (unsigned char)first[k]; b[2] = (unsigned char)cc[c][first[k]]; midi_hw_send(g_hw, b, 3, t); t += 0.002; }
        for (int k = 1; k < 128; k++) if (k != 32 && cc[c][k] >= 0) { b[0] = (unsigned char)(0xB0 | c); b[1] = (unsigned char)k; b[2] = (unsigned char)cc[c][k]; midi_hw_send(g_hw, b, 3, t); t += 0.002; }
        if (prog[c] >= 0) { b[0] = (unsigned char)(0xC0 | c); b[1] = (unsigned char)prog[c]; midi_hw_send(g_hw, b, 2, t); t += 0.002; }
        if (bend[c][0] >= 0) { b[0] = (unsigned char)(0xE0 | c); b[1] = (unsigned char)bend[c][0]; b[2] = (unsigned char)bend[c][1]; midi_hw_send(g_hw, b, 3, t); t += 0.002; }
    }
    t += 0.05;
    m->hw_next = i; m->hw_base = t; m->hw_ready = 1;
}
static void hw_event(Midi *m, size_t i) {
    const Ev *e = &m->ev[i]; unsigned char b[3];
    double at = m->hw_base + ((double)m->frame[i] / m->rate - m->hw_pos0);
    switch (e->type) {
    case EV_NOTEON: if ((m->mute >> e->ch) & 1) break; b[0] = (unsigned char)(0x90 | e->ch); b[1] = e->a; b[2] = e->b; midi_hw_send(g_hw, b, 3, at); if (e->b / 127.0f > m->lvl[e->ch]) m->lvl[e->ch] = e->b / 127.0f; break;
    case EV_NOTEOFF: b[0] = (unsigned char)(0x80 | e->ch); b[1] = e->a; b[2] = 0; midi_hw_send(g_hw, b, 3, at); break;
    case EV_PROG: b[0] = (unsigned char)(0xC0 | e->ch); b[1] = e->a; midi_hw_send(g_hw, b, 2, at); break;
    case EV_CC: b[0] = (unsigned char)(0xB0 | e->ch); b[1] = e->a; b[2] = e->b; midi_hw_send(g_hw, b, 3, at); break;
    case EV_BEND: b[0] = (unsigned char)(0xE0 | e->ch); b[1] = e->a; b[2] = e->b; midi_hw_send(g_hw, b, 3, at); break;
    case EV_SYSEX: { const uint8_t *p = m->sxp + e->val; int n = p[0] | (p[1] << 8); if (at < m->hw_sx_free) at = m->hw_sx_free; midi_hw_send(g_hw, p + 2, n, at); m->hw_sx_free = at + sx_gap_msg(p + 2, n); break; }
    default: break;                                                             /* the resets are in the SysEx messages themselves, the tempo is in the times */
    }
}
/* the hardware part of a render call: the events go to the device a little ahead of their time; returns 0 while the set-up is still being sent (the song does not run yet) */
static int hw_feed(Midi *m, int frames) {
    if (!m->hw_ready) hw_resync(m);
    if (midi_hw_time(g_hw) < m->hw_base - 0.12) return 0;
    double ahead = ((double)m->pos + frames) / m->rate + 0.12;
    while (m->hw_next < m->nev && (double)m->frame[m->hw_next] / m->rate <= ahead) { hw_event(m, m->hw_next); m->hw_next++; }
    return 1;
}
#endif

/* ---------------------------------------------------------------- play */
int midi_render(Midi *m, int16_t *out, int frames) {
#if NG_HWMIDI
    int hw = g_hw != NULL;
    if (hw != m->hw_mode) {                                                     /* the output was switched */
        m->hw_mode = hw;
        if (hw) { for (int c = 0; c < NCH; c++) tsf_channel_sounds_off_all(m->f, c); m->hw_ready = 0; }
        else { uint64_t p = m->pos; midi_seek(m, (double)p / m->rate); }
    }
    if (hw && !hw_feed(m, frames)) { memset(out, 0, (size_t)frames * 4); return frames; }            /* the set-up is still on its way: the song does not run yet */
#endif
    int done = 0;
    while (done < frames) {
        while (m->next < m->nev && m->frame[m->next] <= m->pos) { apply(m, &m->ev[m->next], 1); m->next++; }
        int chunk = frames - done;
        if (m->next < m->nev) { uint64_t d = m->frame[m->next] - m->pos; if (d < (uint64_t)chunk) chunk = (int)d; }
        else {
            if (m->tail >= (uint64_t)(TAIL_SEC * m->rate) || tsf_active_voice_count(m->f) == 0) break;      /* the end: nothing left to ring */
            if ((uint64_t)chunk > (uint64_t)(TAIL_SEC * m->rate) - m->tail) chunk = (int)((uint64_t)(TAIL_SEC * m->rate) - m->tail);
        }
        if (chunk < 1) chunk = 1;
        for (int part = 0; part < chunk; part += MIDI_BLOCK) {
            int k = chunk - part < MIDI_BLOCK ? chunk - part : MIDI_BLOCK;
            memset(m->wrev, 0, (size_t)k * 8); memset(m->wcho, 0, (size_t)k * 8);
            m->f->wetRev = m->wrev; m->f->wetCho = m->wcho;
            memset(m->scblk, 0, sizeof m->scblk); m->f->scopeBlk = m->scblk; m->f->scopeStride = MIDI_BLOCK;
            tsf_render_float(m->f, m->dry, k, 0);
            if (m->scope) {                                                         /* the scope rings: one trace per channel (the view normalises the level) */
                for (int c = 0; c < NCH; c++) for (int i = 0; i < k; i++) m->scope[(size_t)c * (size_t)m->scope_len + ((m->scope_pos + (unsigned)i) & (unsigned)(m->scope_len - 1))] = m->scblk[c * MIDI_BLOCK + i] * 4.0f;
                m->scope_pos += (unsigned)k;
            }
            for (int c = 0; c < NCH; c++) { float pk = 0; const float *sb = m->scblk + c * MIDI_BLOCK; for (int i = 0; i < k; i++) { float a = sb[i] < 0 ? -sb[i] : sb[i]; if (a > pk) pk = a; } if (pk > m->aout[c]) m->aout[c] = pk; }
            reverb_process(&m->rev, m->wrev, m->orev, k);
            chorus_process(&m->cho, m->wcho, m->ocho, k, m->rate);
            int16_t *o = out + 2 * (size_t)(done + part);
            for (int i = 0; i < 2 * k; i++) {
                float y = (m->dry[i] + m->orev[i] * m->rev_level + m->ocho[i] * m->cho_level) * m->mvol * 32767.0f, a = y < 0 ? -y : y;
                if (a > 24000.0f) { a = 24000.0f + 8767.0f * tanhf((a - 24000.0f) / 8767.0f); y = y < 0 ? -a : a; }          /* a soft limiter: hot files bend over instead of clipping */
                o[i] = (int16_t)y;
            }
        }
        m->pos += (uint64_t)chunk; done += chunk;
        if (m->next >= m->nev) m->tail += (uint64_t)chunk;
    }
    if (done) { float k = (float)pow(0.85, (double)done / m->rate * 20.0); for (int c = 0; c < NCH; c++) { m->lvl[c] *= k; m->aout[c] *= k; } }
    if (done < frames) memset(out + 2 * (size_t)done, 0, (size_t)(frames - done) * 4);
    return done;                                                                      /* on hardware the SoundFont synth only runs for the pictures (scopes, spectrum): the caller mutes it, see midi_hardware_on() */
}
void midi_set_scope(Midi *m, float *buf, int len) { m->scope = buf; m->scope_len = len; m->scope_pos = 0; }
unsigned midi_scope_pos(const Midi *m) { return m->scope_pos; }
void midi_set_mute(Midi *m, unsigned mask) {
    unsigned off = mask & ~m->mute; m->mute = mask;
    for (int c = 0; c < NCH; c++) if ((off >> c) & 1) {
        tsf_channel_sounds_off_all(m->f, c);
#if NG_HWMIDI
        if (g_hw && m->hw_ready) { unsigned char b[3] = { (unsigned char)(0xB0 | c), 123, 0 }; midi_hw_send(g_hw, b, 3, -1.0); }
#endif
    }
}
void midi_seek(Midi *m, double sec) {
    if (sec < 0) sec = 0;
    uint64_t target = (uint64_t)(sec * m->rate);
    for (int c = 0; c < NCH; c++) tsf_channel_sounds_off_all(m->f, c);
    all_reset(m);
    size_t i = 0;
    while (i < m->nev && m->frame[i] <= target) { apply(m, &m->ev[i], 0); i++; }
    m->next = i; m->pos = target; m->tail = 0; m->hw_ready = 0;
    for (int c = 0; c < NCH; c++) m->lvl[c] = 0;
    reverb_clear(&m->rev); chorus_clear(&m->cho);
}
double midi_length(const Midi *m) { return m->length; }
int midi_measures(const Midi *m) { return m->measures; }
int midi_division(const Midi *m) { return m->div; }
static double tick_at_sec(const Midi *m, double sec) {
    size_t lo = 0, hi = m->ntm;                                               /* the last tempo entry that starts at or before sec */
    while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (m->tm_sec[mid] <= sec) lo = mid; else hi = mid; }
    return m->spt ? sec / m->spt : (double)m->tm_tick[lo] + (sec - m->tm_sec[lo]) * 1e6 * m->div / m->tm_us[lo];
}
double midi_sec_at_tick(const Midi *m, double tick) {
    size_t lo = 0, hi = m->ntm;
    while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if ((double)m->tm_tick[mid] <= tick) lo = mid; else hi = mid; }
    return m->spt ? tick * m->spt : m->tm_sec[lo] + (tick - (double)m->tm_tick[lo]) * m->tm_us[lo] / 1e6 / m->div;
}
double midi_tick(const Midi *m) { return tick_at_sec(m, (double)m->pos / m->rate); }
int midi_notes_in(const Midi *m, double t0, double t1, MidiNote *out, int max) {
    size_t lo = 0, hi = m->nnotes; double from = t0 - m->maxdur;                  /* a note that started before t0 may still sound: start at t0 - the longest note */
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (m->notes[mid].t0 < from) lo = mid + 1; else hi = mid; }
    int n = 0;
    for (size_t i = lo; i < m->nnotes && m->notes[i].t0 <= t1 && n < max; i++) if (m->notes[i].t1 >= t0) out[n++] = m->notes[i];
    return n;
}
double midi_position(const Midi *m) { return (double)m->pos / m->rate; }
void midi_hint_mt32(Midi *m) { m->sx_mt32++; }
void midi_info(const Midi *m, MidiInfo *out) {
    *out = m->info; for (int k = 0; k < 3; k++) out->mt_reverb[k] = m->mtrev[k];
    const char *s = m->sx_mt32 ? "MT-32" : m->sx_gs ? "GS" : m->sx_xg ? "XG" : m->sx_gm2 ? "GM2" : "GM";
    snprintf(out->standard, sizeof out->standard, "%s", s);
    out->standard_known = m->sx_mt32 || m->sx_gs || m->sx_xg || m->sx_gm2 || m->sx_gm;
    snprintf(out->lcd, sizeof out->lcd, "%s", m->lcd); snprintf(out->syx, sizeof out->syx, "%s", m->syx_name); out->syx_msgs = m->syx_msgs;
}
void midi_chan_fx(const Midi *m, int c, MidiChanFx *o) {
    c &= 15; o->expr = 127; o->reverb = 40; o->chorus = 0; o->sustain = 0; o->bend = 0; o->mod = m->cc1[c];
    if (m->f && m->f->channels && c < m->f->channels->channelNum) {
        const struct tsf_channel *ch = &m->f->channels->channels[c];
        o->expr = ch->midiExpression >> 7; o->reverb = (int)(ch->reverbSend * 127.0f + 0.5f); o->chorus = (int)(ch->chorusSend * 127.0f + 0.5f);
        o->sustain = ch->sustain; o->bend = ((float)ch->pitchWheel - 8192.0f) / 8192.0f;
    }
}
int  midi_chan_program(const Midi *m, int c) { return m->prog[c & 15]; }
int  midi_chan_bank(const Midi *m, int c) { return m->bank[c & 15]; }
int  midi_chan_is_drum(const Midi *m, int c) { return m->drumch[c & 15]; }
float midi_chan_level(const Midi *m, int c) { return m->lvl[c & 15]; }
float midi_chan_out(const Midi *m, int c) { return m->aout[c & 15]; }
int  midi_chan_volume(const Midi *m, int c) { return m->vol[c & 15]; }
int  midi_chan_pan(const Midi *m, int c) { return m->pan[c & 15]; }
int  midi_chan_notes(const Midi *m, int c) { return m->notes_ch[c & 15]; }

#else  /* NG_MIDI == 0: the iOS / tvOS apps (stubs: the calls in song.c are dead code there) */
int midi_probe(const uint8_t *b, size_t n) { (void)b; (void)n; return 0; }
void midi_set_soundfont(const char *p) { (void)p; }
const char *midi_soundfont_path(void) { return 0; }
Midi *midi_open(const uint8_t *b, size_t n, int r, char *e, size_t c) { (void)b; (void)n; (void)r; (void)e; (void)c; return 0; }
void midi_close(Midi *m) { (void)m; }
int midi_render(Midi *m, int16_t *o, int f) { (void)m; (void)o; (void)f; return 0; }
void midi_seek(Midi *m, double s) { (void)m; (void)s; }
double midi_length(const Midi *m) { (void)m; return 0; }
double midi_position(const Midi *m) { (void)m; return 0; }
void midi_info(const Midi *m, MidiInfo *o) { (void)m; (void)o; }
void midi_hint_mt32(Midi *m) { (void)m; }
int midi_set_hardware(int on, const char *w) { (void)on; (void)w; return 0; }
int midi_hardware_on(void) { return 0; }
const char *midi_output_name(void) { return "SoundFont"; }
int midi_activity(void) { return 0; }
void midi_idle(Midi *m) { (void)m; }
void midi_add_syx(Midi *m, const char *n, const uint8_t *b, size_t l) { (void)m; (void)n; (void)b; (void)l; }
int midi_chan_program(const Midi *m, int c) { (void)m; (void)c; return 0; }
int midi_chan_bank(const Midi *m, int c) { (void)m; (void)c; return 0; }
int midi_chan_is_drum(const Midi *m, int c) { (void)m; return c == 9; }
float midi_chan_level(const Midi *m, int c) { (void)m; (void)c; return 0; }
float midi_chan_out(const Midi *m, int c) { (void)m; (void)c; return 0; }
void midi_chan_fx(const Midi *m, int c, MidiChanFx *o) { (void)m; (void)c; o->expr = 127; o->mod = 0; o->reverb = 40; o->chorus = 0; o->sustain = 0; o->bend = 0; }
int midi_chan_volume(const Midi *m, int c) { (void)m; (void)c; return 0; }
int midi_chan_pan(const Midi *m, int c) { (void)m; (void)c; return 64; }
int midi_chan_notes(const Midi *m, int c) { (void)m; (void)c; return 0; }
void midi_set_mute(Midi *m, unsigned x) { (void)m; (void)x; }
void midi_set_scope(Midi *m, float *b, int l) { (void)m; (void)b; (void)l; }
unsigned midi_scope_pos(const Midi *m) { (void)m; return 0; }
const char *midi_gm_name(int p, int d) { (void)p; (void)d; return ""; }
int midi_measures(const Midi *m) { (void)m; return 1; }
int midi_division(const Midi *m) { (void)m; return 480; }
double midi_tick(const Midi *m) { (void)m; return 0; }
double midi_sec_at_tick(const Midi *m, double t) { (void)m; (void)t; return 0; }
int midi_notes_in(const Midi *m, double a, double b, MidiNote *o, int x) { (void)m; (void)a; (void)b; (void)o; (void)x; return 0; }
#endif
