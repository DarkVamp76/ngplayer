/* DigiBooster Pro (DBM0) loader. The song is converted into the XM data model and played by the XM replayer (fmt 4): DBM effects use the
 * XM effect numbers, the instruments have volume and panning envelopes (up to 32 points), and a cell has two effect slots.
 * Layout (IFF-like, big endian): "DBM0", version u16, reserved u16, then chunks (tag, size u32):
 *   NAME (44 bytes title), INFO (instruments, samples, songs, patterns, channels: 5 x u16),
 *   SONG (name[44], length u16, orders u16 each), INST (50 bytes each: name[30], sample u16, volume u16, c2spd u32, loop start u32,
 *   loop length u32, pan i16 (centre 0), flags u16: 1 loop, 2 ping-pong), PATT (per pattern: rows u16, size u32, packed rows: a byte 0 ends
 *   the row, otherwise channel + 1, a flag byte and the fields note, instrument, effect, parameter, effect 2, parameter 2),
 *   SMPL (flags u32: 1 = 8 bit, 2 = 16 bit, 4 = 32 bit; length u32; signed big endian data), VENV / PENV (volume and pan envelopes),
 *   DSPE (DSP effects, ignored).
 * The layout follows the format description as used by libxmp (MIT) as a reference. GPL-3.0-or-later. */
#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned be16(const uint8_t *p){ return (p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | (p[2] << 8) | p[3]; }

int dbm_probe(const uint8_t *b, size_t n){ return n >= 16 && !memcmp(b, "DBM0", 4); }

#ifndef DBM_NOTE_OFS
#define DBM_NOTE_OFS -12
#endif
#ifndef DBM_LINEAR
#define DBM_LINEAR 0
#endif

typedef struct { const uint8_t *d; size_t len; } Chunk;

/* DBM effect -> XM effect. The table follows the one OpenMPT uses (effects it does not know are ignored; 0x1C "Real BPM" is one of them).
 * DBM volume slides use fine slides like S3M: AFx = fine down x, AxF = fine up x (XM: EBx / EAx). 0x1F toggles the echo DSP (internal 0x30), 0x20..0x23
 * set the echo delay / feedback / mix / cross (internal 0x31..0x34). *echo is set when the toggle effect occurs. */
static void dbm_effect(int *eff, int *par, int *echo){
    switch (*eff) {
    case 0x00: if (!*par) *eff = 0; break;
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0B: case 0x0C: case 0x0D: case 0x0F:
    case 0x10: case 0x11: case 0x14: case 0x15: case 0x19:
        break;
    case 0x0A: {
        int hi = *par >> 4, lo = *par & 15;
        if (hi == 0xF && lo != 0 && lo != 0xF) { *eff = 0x0E; *par = 0xB0 | lo; }        /* fine slide down */
        else if (lo == 0xF && hi != 0 && hi != 0xF) { *eff = 0x0E; *par = 0xA0 | hi; }   /* fine slide up */
        break;
    }
    case 0x0E:
        switch (*par >> 4) {
        case 0x3: *eff = 0x35; *par = 1; break;                 /* play backwards (internal effect 0x35) */
        case 0x4: *par = 0xC0; break;                           /* turn off the sound: note cut at tick 0 */
        case 0x5: *eff = *par = 0; break;                       /* turn the channel on/off: not supported */
        case 0x7: *eff = *par = 0; break;                       /* coarse sample offset: not supported */
        }
        break;
    case 0x1F: *eff = 0x30; *echo = 1; break;
    case 0x20: case 0x21: case 0x22: case 0x23: *eff = 0x31 + (*eff - 0x20); break;
    default: *eff = *par = 0;
    }
}

static void load_env(XEnv *e, const uint8_t *h){
    /* h: flags u8, points u8 (sections), sustain, loop start, loop end, sustain 2, then 32 x (position u16, value i16) */
    int fl = h[0] & 7, npt = h[1] + 1, sus = h[2], lps = h[3], lpe = h[4];
    if (npt > 32 || sus >= 32 || lps >= 32 || lpe >= 32) return;
    for (int i = 0; i < npt; i++) {
        int v = (int16_t)be16(h + 6 + 4 * i + 2);
        e->x[i] = (uint16_t)be16(h + 6 + 4 * i);
        e->y[i] = (uint8_t)(v < 0 ? 0 : v > 64 ? 64 : v);
    }
    e->n = npt; e->sus = sus; e->ls = lps; e->le = lpe;
    e->on = (fl & 1) != 0; e->susen = (fl & 2) != 0; e->loopen = (fl & 4) != 0;
}

int xdbm_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 4; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125; s->linear = DBM_LINEAR;
    if (!dbm_probe(b, n)) return -1;
    Chunk name = {0}, info = {0}, song = {0}, inst = {0}, patt = {0}, smpl = {0}, venv = {0}, penv = {0}, dspe = {0};
    int maj = b[4];
    for (size_t o = 8; o + 8 <= n;) {
        uint32_t ln = be32(b + o + 4); size_t avail = n - (o + 8);
        Chunk c = { b + o + 8, ln > avail ? avail : ln };
        if (!memcmp(b + o, "NAME", 4) && !name.d) name = c;
        else if (!memcmp(b + o, "INFO", 4) && !info.d) info = c;
        else if (!memcmp(b + o, "SONG", 4) && !song.d) song = c;
        else if (!memcmp(b + o, "INST", 4) && !inst.d) inst = c;
        else if (!memcmp(b + o, "PATT", 4) && !patt.d) patt = c;
        else if (!memcmp(b + o, "SMPL", 4) && !smpl.d) smpl = c;
        else if (!memcmp(b + o, "VENV", 4) && !venv.d) venv = c;
        else if (!memcmp(b + o, "PENV", 4) && !penv.d) penv = c;
        else if (!memcmp(b + o, "DSPE", 4) && !dspe.d) dspe = c;
        if (ln > avail) break;
        o += 8 + (size_t)ln;
    }
    if (!info.d || info.len < 10 || !song.d || !patt.d) return -2;
    unsigned nins = be16(info.d), nsmp = be16(info.d + 2), npat = be16(info.d + 6), nch = be16(info.d + 8);
    if (nins > 255 || nsmp > 255 || npat > 256 || nch < 1 || nch > 64) return -3;
    if (name.d) { size_t k = name.len < 20 ? name.len : 20; memcpy(s->title, name.d, k); s->title[k] = 0; }
    s->nch = (int)nch; s->npat = (int)npat; s->ninst = (int)nins;
    for (int i = 0; i < 64; i++) { s->chpan[i] = -1; s->chen[i] = 1; }

    /* order list (first song) */
    if (song.len < 46) return -4;
    unsigned nord = be16(song.d + 44);
    if (nord > 256 || 46 + (size_t)nord * 2 > song.len) return -4;
    unsigned nvalid = 0;       /* the XM engine has no order markers: entries that point at a missing pattern are left out */
    for (unsigned i = 0; i < nord; i++) { unsigned p = be16(song.d + 46 + 2 * i); if (p < npat && p < 0xFE) s->orders[nvalid++] = (uint8_t)p; }
    s->nord = (int)nvalid;
    if (!nvalid) return -4;

    /* samples (the data is shared by the instruments that use it) */
    typedef struct { int8_t *data; uint32_t len; int is16; } RawSmp;
    RawSmp *raw = calloc(nsmp + 1, sizeof *raw);
    if (!raw) return -5;
    if (smpl.d) {
        size_t o = 0;
        for (unsigned i = 0; i < nsmp; i++) {
            if (o + 8 > smpl.len) break;
            uint32_t fl = be32(smpl.d + o), len = be32(smpl.d + o + 4); o += 8;
            int bytes = (fl & 1) ? 1 : (fl & 2) ? 2 : (fl & 4) ? 4 : 0;
            if (!bytes) break;
            size_t need = (size_t)len * bytes;
            if (len > (256u << 20) || o + need > smpl.len) { /* truncated file: keep what is there */ if (o >= smpl.len) break; need = smpl.len - o; len = (uint32_t)(need / bytes); }
            if (bytes != 4 && len) {
                raw[i + 1].data = calloc((size_t)len * bytes + 4, 1);
                if (!raw[i + 1].data) { for (unsigned k = 1; k <= nsmp; k++) free(raw[k].data); free(raw); return -5; }
                raw[i + 1].len = len; raw[i + 1].is16 = bytes == 2;
                if (bytes == 2) { int16_t *q = (int16_t *)raw[i + 1].data; for (uint32_t j = 0; j < len; j++) q[j] = (int16_t)be16(smpl.d + o + 2 * j); }
                else for (uint32_t j = 0; j < len; j++) raw[i + 1].data[j] = (int8_t)smpl.d[o + j];
            }
            o += need;
        }
    }

    /* instruments: every instrument gets its own XSmp (volume, pan, loop, pitch) pointing at the shared sample data */
    s->inst = calloc(nins + 1, sizeof(XInst));
    s->xsmp = calloc(nins + 1, sizeof(XSmp));
    if (!s->inst || !s->xsmp) { for (unsigned k = 1; k <= nsmp; k++) free(raw[k].data); free(raw); return -5; }
    unsigned char *owned = calloc(nsmp + 1, 1);
    for (unsigned i = 1; i <= nins; i++) {
        XInst *I = &s->inst[i];
        if (!inst.d || (size_t)i * 50 > inst.len) continue;
        const uint8_t *h = inst.d + (size_t)(i - 1) * 50;
        unsigned sn = be16(h + 30);
        if (sn == 0 || sn > nsmp || !raw[sn].len) continue;
        XSmp *m = &s->xsmp[s->nxsmp]; I->nsmp = 1; I->smp[0] = s->nxsmp++;
        int vol = (int)be16(h + 32); m->vol = vol > 64 ? 64 : vol;
        uint32_t c2spd = be32(h + 34), ls = be32(h + 38), ll = be32(h + 42); int flags = (int)be16(h + 48);
        int pan = 128 + (int16_t)be16(h + 46); m->pan = pan < 0 ? 0 : pan > 255 ? 255 : pan;
        m->is16 = raw[sn].is16; m->len = raw[sn].len; m->data = raw[sn].data;
        if (owned[sn]) m->nofree = 1; else owned[sn] = 1;
        if (flags & 3) { m->loop = 1; m->pingpong = (flags & 2) != 0; m->ls = ls; m->le = ls + ll; if (m->le > m->len) m->le = m->len; if (ls >= m->len || !ll) m->loop = 0; }
        /* c2spd -> relative note + finetune (1/128 semitone) around note 49 = 8363 Hz */
        if (c2spd < 1) c2spd = 8363;
        double t = 12.0 * log2((double)c2spd / 8363.0) * 128.0;
        int rel = (int)floor(t / 128.0 + 0.5); double ft = t - rel * 128.0;
        if (rel < -96) { rel = -96; ft = 0; } if (rel > 95) { rel = 95; ft = 0; }
        m->relnote = rel; m->finetune = (int)(ft < -128 ? -128 : ft > 127 ? 127 : ft);
    }
    for (unsigned k = 1; k <= nsmp; k++) if (!owned[k]) free(raw[k].data);       /* samples no instrument uses */
    free(owned); free(raw);

    /* envelopes */
    for (int pass = 0; pass < 2; pass++) {
        const Chunk *e = pass ? &penv : &venv;
        if (!e->d || e->len < 2) continue;
        unsigned ne = be16(e->d); size_t o = 2;
        for (unsigned i = 0; i < ne && o + 8 + 32 * 4 <= e->len; i++, o += 2 + 6 + 32 * 4) {
            int ins = (int)be16(e->d + o);
            if (ins < 1 || ins > (int)nins) continue;
            XEnv tmp = {0}; load_env(&tmp, e->d + o + 2);
            if (pass && maj >= 3) for (int k = 0; k < tmp.n; k++) { int v = (int16_t)be16(e->d + o + 2 + 6 + 4 * k + 2); v = v / 4 + 32; tmp.y[k] = (uint8_t)(v < 0 ? 0 : v > 64 ? 64 : v); }   /* DigiBooster 3 stores the pan as -128..128 */
            if (pass) s->inst[ins].penv = tmp; else s->inst[ins].venv = tmp;
        }
    }

    /* patterns */
    s->prows = calloc(npat ? npat : 1, sizeof(int));
    if (!s->prows) return -5;
    int has_echo_toggle = 0;
    size_t o = 0, total = 0;
    size_t *poff = calloc(npat ? npat : 1, sizeof *poff);
    if (!poff) return -5;
    for (unsigned p = 0; p < npat; p++) {                  /* first pass: rows per pattern */
        if (o + 6 > patt.len) { poff[p] = (size_t)-1; s->prows[p] = 1; total += nch; continue; }
        unsigned rows = be16(patt.d + o); uint32_t sz = be32(patt.d + o + 2);
        if (rows < 1) rows = 1; if (rows > 256) rows = 256;
        s->prows[p] = (int)rows; poff[p] = o; total += (size_t)rows * nch;
        o += 6 + (size_t)sz; if (o > patt.len) o = patt.len;
    }
    s->pat = calloc(total ? total : 1, sizeof(XCell));
    if (!s->pat) { free(poff); return -5; }
    size_t cell0 = 0;
    for (unsigned p = 0; p < npat; p++) {
        int rows = s->prows[p]; XCell *cells = s->pat + cell0; cell0 += (size_t)rows * nch;
        if (poff[p] == (size_t)-1) continue;
        size_t q = poff[p] + 6, end = q + be32(patt.d + poff[p] + 2); if (end > patt.len) end = patt.len;
        int r = 0;
        while (q < end && r < rows + 1) {
            int c = patt.d[q++];
            if (c == 0) { r++; continue; }
            c--;
            if (q >= end) break;
            int fl = patt.d[q++];
            XCell tmp = {0}; int e1 = 0, p1 = 0, e2 = 0, p2 = 0;
            if ((fl & 1) && q < end) { int x = patt.d[q++]; if (x == 0x1F) tmp.note = 97; else { int nn = 13 + (x >> 4) * 12 + (x & 15) + DBM_NOTE_OFS; if ((x & 15) < 12 && nn >= 1 && nn <= 96) tmp.note = (uint8_t)nn; } }
            if ((fl & 2) && q < end) tmp.inst = patt.d[q++];
            if ((fl & 4) && q < end) e1 = patt.d[q++];
            if ((fl & 8) && q < end) p1 = patt.d[q++];
            if ((fl & 16) && q < end) e2 = patt.d[q++];
            if ((fl & 32) && q < end) p2 = patt.d[q++];
            dbm_effect(&e1, &p1, &has_echo_toggle); dbm_effect(&e2, &p2, &has_echo_toggle);
            tmp.eff = (uint8_t)e1; tmp.par = (uint8_t)p1; tmp.eff2 = (uint8_t)e2; tmp.par2 = (uint8_t)p2;
            if (c < (int)nch && r < rows) cells[(size_t)r * nch + c] = tmp;
        }
    }
    free(poff);

    /* echo DSP (chunk DSPE: u16 channel count, one byte per channel (0 = echo on), then delay, feedback, mix, cross as 4 x u16) */
    {
        int any = has_echo_toggle;
        uint8_t st[8] = { 0, 80, 0, 150, 0, 80, 0, 255 };       /* defaults of DigiBooster Pro 2.21 */
        if (dspe.d && dspe.len >= 2) {
            unsigned mask = be16(dspe.d);
            for (unsigned i = 0; i < mask && 2 + i < dspe.len; i++) if (i < nch && dspe.d[2 + i] == 0) { s->echo_on[i] = 1; any = 1; }
            if (2 + (size_t)mask + 8 <= dspe.len) memcpy(st, dspe.d + 2 + mask, 8);
        }
        if (any) { s->echo = 1; s->echo_par[0] = st[1]; s->echo_par[1] = st[3]; s->echo_par[2] = st[5]; s->echo_par[3] = st[7]; }
    }
    return 0;
}
