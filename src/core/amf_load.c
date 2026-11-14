/* DSMI AMF loader (Advanced Module Format of Otto Chrons' DSMI, signature "AMF" + version byte 0x08..0x0E). Not to be confused with the
 * Asylum Music Format (asylum_load.c), which shares the extension. Layout (checked against files; the size adds up exactly to the file):
 *   "AMF" + version, title[32], instrument count, pattern count, track count (u16), [v>=9: channel count, pan table 16 (v>=0x0C: 32) bytes],
 *   [v>=0x0D: tempo, speed], patterns (v>=0x0E: rows u16; then one u16 track slot per channel), instruments (65 bytes, v<0x0A: 59 bytes),
 *   track slot table (u16 per slot, maps to a block), track blocks (u16 event count, type byte, events of 3 bytes: row, command, value),
 *   then the sample data of every distinct sample in turn.
 * Events: command < 0x80 = note (value = volume), >= 0x80 = effect (0x80 instrument, 0x81 speed, 0x82 volume slide, 0x83 volume, 0x84 bender, ...).
 * The song is converted into MOD cells and played by the MOD replayer (fmt 1); the patterns are the order list. GPL-3.0-or-later. */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p){ return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int amf_probe(const uint8_t *b, size_t n){
    return n >= 64 && !memcmp(b, "AMF", 3) && b[3] >= 8 && b[3] <= 14;
}

#ifndef AMF_NOTE_OFS
#define AMF_NOTE_OFS (-12)
#endif
#ifndef AMF_INST_BASE
#define AMF_INST_BASE 1
#endif
#ifndef AMF_STOP_ON_NOTE
#define AMF_STOP_ON_NOTE 0
#endif
#ifndef AMF_BEND_FLIP
#define AMF_BEND_FLIP 0
#endif
#ifndef AMF_BEND_SCALE
#define AMF_BEND_SCALE 1
#endif
#ifndef AMF_SIGNED
#define AMF_SIGNED 0
#endif

/* AMF effect -> MOD effect + parameter; returns 0 if there is none */
static int amf_effect(int cmd, int v, uint8_t *eff, uint8_t *par){
    switch (cmd) {
    case 0x81: *eff = 0x0F; *par = (uint8_t)(v < 1 ? 1 : v > 31 ? 31 : v); return 1;                       /* speed (ticks per row) */
    case 0x95: *eff = 0x0F; *par = (uint8_t)(v < 32 ? 32 : v); return 1;                                   /* tempo (BPM) */
    case 0x82: { int sv = (int8_t)v; *eff = 0x0A; *par = sv >= 0 ? (uint8_t)((sv > 15 ? 15 : sv) << 4) : (uint8_t)(-sv > 15 ? 15 : -sv); return 1; }
    case 0x83: *eff = 0x0C; *par = (uint8_t)(v > 64 ? 64 : v); return 1;                                    /* volume */
    case 0x84: { int sv = (int8_t)v * AMF_BEND_SCALE; if (AMF_BEND_FLIP) sv = -sv; if (sv >= 0) { *eff = 0x02; *par = (uint8_t)(sv > 255 ? 255 : sv); } else { *eff = 0x01; *par = (uint8_t)(-sv > 255 ? 255 : -sv); } return 1; }
    case 0x86: *eff = 0x03; *par = (uint8_t)v; return 1;                                                    /* tone portamento */
    case 0x87: *eff = 0x07; *par = (uint8_t)v; return 1;                                                    /* tremolo */
    case 0x88: *eff = 0x00; *par = (uint8_t)v; return 1;                                                    /* arpeggio */
    case 0x89: *eff = 0x04; *par = (uint8_t)v; return 1;                                                    /* vibrato */
    case 0x8A: *eff = 0x05; *par = (uint8_t)v; return 1;                                                    /* tone portamento + volume slide */
    case 0x8B: *eff = 0x06; *par = (uint8_t)v; return 1;                                                    /* vibrato + volume slide */
    case 0x8C: *eff = 0x0D; *par = (uint8_t)(((v / 10) << 4) | (v % 10)); return 1;                         /* pattern break */
    case 0x8D: *eff = 0x0B; *par = (uint8_t)v; return 1;                                                    /* position jump */
    case 0x8F: *eff = 0x0E; *par = (uint8_t)(0x90 | (v & 15)); return 1;                                    /* retrigger */
    case 0x90: *eff = 0x09; *par = (uint8_t)v; return 1;                                                    /* sample offset */
    case 0x93: *eff = 0x0E; *par = (uint8_t)(0xD0 | (v & 15)); return 1;                                    /* note delay */
    case 0x94: *eff = 0x0E; *par = (uint8_t)(0xC0 | (v & 15)); return 1;                                    /* note cut */
    case 0x97: *eff = 0x08; *par = (uint8_t)(((int8_t)v + 64) * 2 > 255 ? 255 : ((int8_t)v + 64) * 2); return 1;   /* panning */
    default: return 0;
    }
}

int xamf_load(XSong *s, const uint8_t *b, size_t n){
    memset(s, 0, sizeof *s);
    s->fmt = 1; s->maxvol = 0x40; s->gvol = 255; s->speed = 6; s->tempo = 125;
    if (!amf_probe(b, n)) return -1;
    int ver = b[3];
    memcpy(s->title, b + 4, 20); s->title[20] = 0;
    size_t o = 36;
    unsigned nins = b[o], npat = b[o + 1], ntr = rd16(b + o + 2), nch = 4; o += 4;
    uint8_t pan[32]; for (int i = 0; i < 32; i++) pan[i] = 0;
    if (ver >= 9) {
        nch = b[o++]; unsigned pl = ver >= 0x0C ? 32 : 16;
        if (o + pl > n) return -2;
        memcpy(pan, b + o, pl); o += pl;
    }
    if (ver < 0x0B) { static const int8_t o16[4] = { -63, 63, 63, -63 }; for (int i = 0; i < 32; i++) pan[i] = (uint8_t)o16[i & 3]; }
    if (ver >= 0x0D) { if (o + 2 > n) return -2; if (b[o] >= 32) s->tempo = b[o]; if (b[o + 1]) s->speed = b[o + 1]; o += 2; }
    if (nch < 1 || nch > 32 || !npat || !nins) return -3;
    s->nch = (int)nch;
    for (int i = 0; i < 32; i++) { s->chen[i] = 1; s->chpan[i] = -1; }
    for (unsigned i = 0; i < nch; i++) { int p = (int8_t)pan[i]; if (p >= -64 && p <= 64) s->chpan[i] = (p + 64) * 2 > 255 ? 255 : (p + 64) * 2; }
    s->npat = (int)npat; s->nord = (int)npat;
    if (npat > 255) return -3;

    /* pattern table: track slot per channel */
    uint16_t (*pt)[32] = calloc(npat, sizeof *pt); uint16_t *plen = calloc(npat, 2);
    if (!pt || !plen) { free(pt); free(plen); return -5; }
    for (unsigned p = 0; p < npat; p++) {
        plen[p] = 64;
        if (ver >= 0x0E) { if (o + 2 > n) goto bad; plen[p] = (uint16_t)rd16(b + o); o += 2; }
        if (o + nch * 2 > n) goto bad;
        for (unsigned c = 0; c < nch; c++, o += 2) pt[p][c] = (uint16_t)rd16(b + o);
        s->orders[p] = (uint8_t)p;
    }

    /* instruments */
    struct { uint32_t smp, size, ls, le; int vol; uint32_t rate; } ins[100];
    memset(ins, 0, sizeof ins);
    size_t isz = ver >= 0x0A ? 65 : 59;
    if (nins > 99 || o + nins * isz > n) goto bad;
    for (unsigned i = 0; i < nins; i++, o += isz) {
        const uint8_t *h = b + o;
        if (ver >= 0x0A) { ins[i].smp = rd32(h + 46); ins[i].size = rd32(h + 50); ins[i].rate = rd16(h + 54); ins[i].vol = h[56]; ins[i].ls = rd32(h + 57); ins[i].le = rd32(h + 61); }
        else { ins[i].smp = rd32(h + 46); ins[i].size = rd16(h + 50); ins[i].rate = rd16(h + 52); ins[i].vol = h[54]; ins[i].ls = rd16(h + 55); ins[i].le = rd16(h + 57); if (ins[i].le == 65535) ins[i].ls = ins[i].le = 0; }
    }

    /* track slots -> blocks, then the blocks */
    if (o + (size_t)ntr * 2 > n) goto bad;
    uint16_t *slot = malloc((ntr + 1) * 2); unsigned nblk = 0;
    if (!slot) goto bad;
    for (unsigned t = 0; t < ntr; t++, o += 2) { slot[t] = (uint16_t)rd16(b + o); if (slot[t] > nblk) nblk = slot[t]; }
    const uint8_t **blk = calloc(nblk + 1, sizeof *blk); unsigned *bcnt = calloc(nblk + 1, sizeof *bcnt);
    if (!blk || !bcnt) { free(slot); free(blk); free(bcnt); goto bad; }
    for (unsigned t = 0; t < nblk; t++) {
        if (o + 3 > n) break;
        unsigned cnt = rd16(b + o); o += 3;
        if (o + (size_t)cnt * 3 > n) cnt = (unsigned)((n - o) / 3);
        blk[t] = b + o; bcnt[t] = cnt; o += (size_t)cnt * 3;
    }

    /* samples: sample number k (1-based) takes the size of the first instrument that uses it; the data follows in sample order */
    unsigned lastsmp = 0; uint32_t ssz[256]; memset(ssz, 0, sizeof ssz);
    for (unsigned i = 0; i < nins; i++) if (ins[i].smp > 0 && ins[i].smp < 256) { if (ins[i].smp > lastsmp) lastsmp = ins[i].smp; if (!ssz[ins[i].smp]) ssz[ins[i].smp] = ins[i].size; }
    int8_t *sdata[256]; memset(sdata, 0, sizeof sdata);
    for (unsigned k = 1; k <= lastsmp; k++) {
        if (!ssz[k]) continue;
        size_t avail = o < n ? n - o : 0, take = ssz[k] < avail ? ssz[k] : avail;
        sdata[k] = calloc((size_t)ssz[k] + 4, 1);
        if (!sdata[k]) { free(slot); free(blk); free(bcnt); goto bad; }
        for (size_t j = 0; j < take; j++) sdata[k][j] = (int8_t)(AMF_SIGNED ? b[o + j] : (b[o + j] ^ 0x80));
        o += ssz[k];
    }
    s->nsmp = (int)(nins > 31 ? 31 : nins);
    for (unsigned i = 0; i < nins && i < 99; i++) {
        XSmp *m = &s->smp[i + 1];
        unsigned k = ins[i].smp; if (k == 0 || k > lastsmp || !sdata[k]) continue;
        m->len = ssz[k]; m->vol = ins[i].vol > 64 ? 64 : ins[i].vol; m->c2spd = ins[i].rate ? ins[i].rate : 8363;
        m->data = calloc((size_t)m->len + 4, 1);
        if (!m->data) { free(slot); free(blk); free(bcnt); goto bad; }
        memcpy(m->data, sdata[k], m->len);
        if (ins[i].le > ins[i].ls && ins[i].le > 2) { m->loop = 1; m->ls = ins[i].ls; m->le = ins[i].le > m->len ? m->len : ins[i].le; if (m->ls >= m->le) m->loop = 0; }
    }
    for (unsigned k = 1; k <= lastsmp; k++) free(sdata[k]);

    /* patterns: merge the events of a row into one MOD cell (note, instrument, one effect; the note volume becomes Cxx if the effect is free) */
    s->pat = calloc((size_t)npat * 64 * nch + 1, sizeof(XCell));
    if (!s->pat) { free(slot); free(blk); free(bcnt); goto bad; }
    for (unsigned p = 0; p < npat; p++)
        for (unsigned c = 0; c < nch; c++) {
            unsigned sl = pt[p][c], bi = sl >= 1 && sl <= ntr ? slot[sl - 1] : 0;
            if (!bi || bi > nblk || !blk[bi - 1]) continue;
            int notevol[64]; for (int r = 0; r < 64; r++) notevol[r] = -1;
            for (unsigned e = 0; e < bcnt[bi - 1]; e++) {
                const uint8_t *q = blk[bi - 1] + e * 3; unsigned r = q[0]; int cmd = q[1], v = q[2];
                if (r >= 64) continue;
                XCell *x = &s->pat[((size_t)p * 64 + r) * nch + c];
                if (cmd < 0x80) { int note = cmd + AMF_NOTE_OFS; x->note = (uint8_t)(note < 1 ? 1 : note > 127 ? 127 : note); notevol[r] = v; }
                else if (cmd == 0x80) x->inst = (uint8_t)(v + AMF_INST_BASE);
                else { uint8_t ef, pa; if (amf_effect(cmd, v, &ef, &pa) && !(x->eff | x->par) ) { x->eff = ef; x->par = pa; } }
            }
            /* AMF effects stay active until the next one: repeat the continuous ones (slides, porta, vibrato, tremolo, arpeggio) on the
             * rows that follow, until another effect or (AMF_STOP_ON_NOTE) a new note replaces them */
            { int ae = -1, ap = 0;
              for (int r = 0; r < 64; r++) {
                XCell *x = &s->pat[((size_t)p * 64 + r) * nch + c];
                int cont = (x->eff == 0x01 || x->eff == 0x02 || x->eff == 0x03 || x->eff == 0x04 || x->eff == 0x05 || x->eff == 0x06 || x->eff == 0x07 || x->eff == 0x0A);
                if (x->eff || x->par) { if (cont) { ae = x->eff; ap = x->par; } else if (x->eff == 0x0C || x->eff == 0x00) ae = ae; else ae = -1; }
                else if (ae >= 0 && !(AMF_STOP_ON_NOTE && x->note)) { x->eff = (uint8_t)ae; x->par = (uint8_t)ap; }
                else if (ae >= 0) ae = -1;
              } }
            for (int r = 0; r < 64; r++) {
                XCell *x = &s->pat[((size_t)p * 64 + r) * nch + c];
                if (notevol[r] >= 0 && notevol[r] <= 64 && !x->eff && !x->par && notevol[r] != 64) { x->eff = 0x0C; x->par = (uint8_t)notevol[r]; }
            }
            if (plen[p] < 64 && plen[p] > 0 && c == 0) { XCell *x = &s->pat[((size_t)p * 64 + plen[p] - 1) * nch]; if (!x->eff) { x->eff = 0x0D; x->par = 0; } }   /* short pattern: break after the last row */
        }
    free(slot); free(blk); free(bcnt); free(pt); free(plen);
    return 0;
bad:
    free(pt); free(plen);
    return -4;
}
