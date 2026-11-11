/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "song.h"
#include "core.h"
#include "it.h"
#include "dmf.h"
#include "tfmx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern void ngp_it_tables_init(void);
int xplay_pos(const XPlayer *p, int *ord, int *row);
int xxm_pos(const void *p, int *ord, int *row);
void xplay_set_scope(XPlayer *p, float *buf, int len);
unsigned xplay_scope_pos(const XPlayer *p);
void xxm_set_scope(void *p, float *buf, int len);
unsigned xxm_scope_pos(const void *p);
void itp_set_scope(ITPlayer *p, float *buf, int len);
unsigned itp_scope_pos(const ITPlayer *p);
int xplay_chan_pan(const XPlayer *p, int ch);
int xxm_chan_pan(const void *p, int ch);
int itp_chan_pan(const ITPlayer *p, int ch);
void itp_pos(const ITPlayer *p, int *ord, int *pat, int *row);

enum { K_MOD, K_S3M, K_XM, K_IT, K_DMF, K_TFMX };

struct Song {
    int kind;
    XSong xs;               /* MOD/S3M/XM */
    ITSong *it;
    XPlayer *xp;
    XXPlayer *xxp;
    ITPlayer *ip;
    DMFSong *dm;
    DMFPlayer *dp;
    TfmxSong *tf;
    TfmxPlayer *tp;
    int tstart;             /* TFMX: erste Trackstep-Zeile des Subsongs */
    int nsub, cursub;       /* TFMX: Anzahl echter Subsongs, aktueller (0-basiert) */
    char title[40];
    const char *fmt;
    int nch, nord;
};

/* TFMX braucht zwei Dateien (mdat.X + smpl.X oder X.tfx + X.sam); liefert 0 bei Erfolg */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int open_tfmx(Song *s, const char *path, const uint8_t *b, long n, int rate, int sub, char *err, size_t errcap) {
    int e = 0;
    if (n > 0x12 && !memcmp(b, "TFHD", 4)) {
        /* Einzeldatei: TFHD-Kopf (Big-Endian), dann mdat, dann Samples. Byte 8: 0 offen, 1 TFMX 1.5, 2 Pro, 3 7V (Bit 7 = erzwungen) */
        uint32_t hdr = be32(b + 4), msz = be32(b + 10), ssz = be32(b + 14);
        if (b[4] || b[5] || hdr < 0x12 || !msz || !ssz || (uint64_t)hdr + msz + ssz > (uint64_t)n) { snprintf(err, errcap, "TFMX: damaged TFHD header"); return -1; }
        s->tf = tfmx_load(b + hdr, msz, b + hdr + msz, ssz, &e);
        if (s->tf && (b[8] & 0x7F) == 1) s->tf->v1 |= 1;            /* Versionshinweis: TFMX 1.5 = frueher Replayer */
    } else {
        char sm[1100];
        int pr = tfmx_partner(path, sm, sizeof sm);
        if (pr) { snprintf(err, errcap, pr > 0 ? "TFMX: sample file missing (smpl.* or .sam next to the song file)" : "TFMX: file must be named mdat.NAME or NAME.tfx"); return -1; }
        FILE *f = fopen(sm, "rb");
        if (!f) { snprintf(err, errcap, "TFMX: cannot open sample file"); return -1; }
        fseek(f, 0, SEEK_END); long ns = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *sb = (ns > 0 && ns < (64L << 20)) ? (uint8_t *)malloc((size_t)ns) : NULL;
        if (!sb || fread(sb, 1, (size_t)ns, f) != (size_t)ns) { fclose(f); free(sb); snprintf(err, errcap, "TFMX: sample file read error"); return -1; }
        fclose(f);
        s->tf = tfmx_load(b, (size_t)n, sb, (size_t)ns, &e);
        free(sb);
    }
    if (!s->tf) { snprintf(err, errcap, "TFMX: unsupported or damaged file (%d)", e); return -1; }
    s->kind = K_TFMX; s->fmt = "TFMX";
    s->title[0] = 0;
    for (int i = 0; i < 6 && !s->title[0]; i++)
        if (s->tf->text[i][0] && strncasecmp(s->tf->text[i], "date", 4) && strncasecmp(s->tf->text[i], "time", 4) && strncasecmp(s->tf->text[i], "(empty", 6)) snprintf(s->title, sizeof s->title, "%s", s->tf->text[i]);
    int idx[32];
    s->nsub = tfmx_subsongs(s->tf, idx, 32);
    if (sub < 0) sub = 0;
    if (sub >= s->nsub) sub = s->nsub - 1;
    s->cursub = sub;
    int real = idx[sub];
    s->tstart = s->tf->sub[real].start;
    s->nch = 4; s->nord = s->tf->sub[real].end >= s->tf->sub[real].start ? s->tf->sub[real].end - s->tf->sub[real].start + 1 : 1;
    s->tp = tfp_new(s->tf, real, rate);
    if (!s->tp) { snprintf(err, errcap, "TFMX: cannot start player"); return -1; }
    return 0;
}

Song *song_open_sub(const char *path, int rate, int sub, char *err, size_t errcap) {
    static int inited;
    if (!inited) { ngp_it_tables_init(); inited = 1; }
    if (tfmx_is_sample_name(path)) { snprintf(err, errcap, "TFMX sample data (open the mdat.* / .tfx file instead)"); return NULL; }
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errcap, "cannot open file"); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 32 || n > (256L << 20)) { fclose(f); snprintf(err, errcap, "bad file size"); return NULL; }
    uint8_t *b = (uint8_t *)malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); snprintf(err, errcap, "read error"); return NULL; }
    fclose(f);

    Song *s = (Song *)calloc(1, sizeof *s);
    int r = 0;
    if (n > 4 && !memcmp(b, "IMPM", 4)) {
        int e = 0;
        s->it = it_load(b, (size_t)n, &e);
        if (!s->it) { r = e ? e : -1; }
        else {
            s->kind = K_IT; s->fmt = "IT";
            snprintf(s->title, sizeof s->title, "%s", s->it->title);
            s->nch = s->it->nch; s->nord = s->it->nord;
            s->ip = itp_new(s->it, rate);
        }
    } else if (n > 66 && !memcmp(b, "DDMF", 4)) {
        int e = 0;
        s->dm = dmf_load(b, (size_t)n, &e);
        if (!s->dm) { r = e ? e : -1; }
        else {
            s->kind = K_DMF; s->fmt = "DMF";
            snprintf(s->title, sizeof s->title, "%s", s->dm->title);
            s->nch = s->dm->nch; s->nord = s->dm->nord;
            s->dp = dmp_new(s->dm, rate);
        }
    } else if (n > 0x200 && (!memcmp(b, "TFMX-SONG", 9) || !memcmp(b, "TFMX_SONG", 9) || !memcmp(b, "TFMX-MOD", 8) || !memcmp(b, "TFHD", 4))) {
        if (open_tfmx(s, path, b, n, rate, sub, err, errcap)) { free(b); song_close(s); return NULL; }
    } else if (n > 80 && !memcmp(b, "Extended Module: ", 17)) {
        r = xxm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_XM; s->fmt = "XM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xxp = xxm_new(&s->xs, rate);
        }
    } else if (med_probe(b, (size_t)n)) {
        r = xmed_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "MED";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (mtm_probe(b, (size_t)n)) {
        r = xmtm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "MTM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (s669_probe(b, (size_t)n)) {
        r = xs669_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_S3M; s->fmt = "669";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (ult_probe(b, (size_t)n)) {
        r = xult_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_S3M; s->fmt = "ULT";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (ptm_probe(b, (size_t)n)) {
        r = xptm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_S3M; s->fmt = "PTM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (stm_probe(b, (size_t)n)) {
        r = xstm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_S3M; s->fmt = "STM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (n > 0x30 && !memcmp(b + 0x2C, "SCRM", 4)) {
        r = xs3m_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_S3M; s->fmt = "S3M";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else {
        r = xmod_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "MOD";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    }
    free(b);
    if (r || (!s->xp && !s->xxp && !s->ip && !s->dp && !s->tp)) {
        snprintf(err, errcap, "unsupported or damaged module (%d)", r);
        song_close(s);
        return NULL;
    }
    return s;
}

void song_close(Song *s) {
    if (!s) return;
    if (s->xp) xplay_free(s->xp);
    if (s->xxp) xxm_free(s->xxp);
    if (s->ip) itp_free(s->ip);
    if (s->dp) dmp_free(s->dp);
    if (s->dm) dmf_free(s->dm);
    if (s->tp) tfp_free(s->tp);
    if (s->tf) tfmx_free(s->tf);
    if (s->it) it_free(s->it);
    if (s->kind != K_IT && s->kind != K_DMF && s->kind != K_TFMX) xsong_free(&s->xs);
    free(s);
}

int song_render(Song *s, int16_t *out, int n) {
    if (s->ip) return itp_render(s->ip, out, n);
    if (s->dp) return dmp_render(s->dp, out, n);
    if (s->tp) return tfp_render(s->tp, out, n);
    if (s->xxp) return xxm_render(s->xxp, out, n);
    return xplay_render(s->xp, out, n);
}

const char *song_title(const Song *s) { return s->title; }
const char *song_format(const Song *s) { return s->fmt; }
int song_channels(const Song *s) { return s->nch; }
int song_norders(const Song *s) { return s->nord; }
int song_subsongs(const Song *s) { return s->kind == K_TFMX && s->nsub > 0 ? s->nsub : 1; }
int song_subsong(const Song *s) { return s->kind == K_TFMX ? s->cursub : 0; }
Song *song_open(const char *path, int rate, char *err, size_t errcap) { return song_open_sub(path, rate, 0, err, errcap); }
int song_order(const Song *s) {
    if (s->dp) { int o = 0; dmp_pos(s->dp, &o, NULL, NULL); return o; }
    if (s->tp) { int o = tfp_pos(s->tp) - s->tstart; return o < 0 ? 0 : o; }
    if (s->xp) return xplay_order(s->xp);
    if (s->xxp) return xxm_order(s->xxp);
    return -1;
}

/* ================================================================== Pattern-Zugriff */
static int pat_of(const Song *s, int order) {
    if (order < 0 || order >= s->nord) return -1;
    if (s->kind == K_TFMX) return order;          /* keine Patterns im Tracker-Sinn: Trackstep-Zeile */
    if (s->kind == K_DMF) { int p = s->dm->order[order]; return p < s->dm->npat ? p : -1; }
    if (s->kind == K_IT) { int p = s->it->orders[order]; return p >= 254 ? -1 : (p < s->it->npat ? p : -1); }
    int p = s->xs.orders[order];
    return p >= 0xFE || p >= s->xs.npat ? -1 : p;
}
int song_order_pattern(const Song *s, int order) { return pat_of(s, order); }

int song_pattern_rows(const Song *s, int pattern) {
    if (s->kind == K_DMF) return pattern >= 0 && pattern < s->dm->npat ? s->dm->pat[pattern].nrows : 64;
    if (s->kind == K_IT) return pattern >= 0 && pattern < s->it->npat ? s->it->rows[pattern] : 64;
    if (s->kind == K_XM && s->xs.prows && pattern >= 0 && pattern < s->xs.npat) return s->xs.prows[pattern];
    if (s->kind == K_TFMX) { int r = tfp_view_rows(s->tp, s->tstart + pattern); return r < 1 ? 1 : r; }
    return 64;
}

void song_pos(const Song *s, SongPos *o) {
    int ord = 0, row = 0, pat = 0;
    if (s->ip) itp_pos(s->ip, &ord, &pat, &row);
    else if (s->dp) dmp_pos(s->dp, &ord, &pat, &row);
    else if (s->tp) { ord = tfp_pos(s->tp) - s->tstart; if (ord < 0) ord = 0; pat = ord; row = tfp_row(s->tp); }
    else { if (s->xxp) xxm_pos(s->xxp, &ord, &row); else xplay_pos(s->xp, &ord, &row); pat = pat_of(s, ord); }
    o->order = ord; o->pattern = pat; o->row = row; o->nrows = song_pattern_rows(s, pat);
    if (s->tp && o->row >= o->nrows) o->row = o->nrows - 1;
}

static const char NOTES[12][3] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
static void put_note(char *out, int idx) {          /* idx = 0 (C-0) .. */
    if (idx < 0 || idx > 119) { strcpy(out, "???"); return; }
    out[0] = NOTES[idx % 12][0]; out[1] = NOTES[idx % 12][1]; out[2] = (char)('0' + idx / 12); out[3] = 0;
}
static char hexc(int v) { return "0123456789ABCDEF"[v & 15]; }
static char fxc(int e) { return (char)(e < 10 ? '0' + e : 'A' + e - 10); }
static void fx3(char *out, char letter, int par) { out[0] = letter; out[1] = hexc(par >> 4); out[2] = hexc(par); out[3] = 0; }

/* Effektklasse nach Buchstabe (Tracker-Konvention: MOD/XM Ziffern, S3M/IT Buchstaben) */
static int cls_mod(int e, int par) {
    switch (e) {
    case 0: return par ? 2 : 0;
    case 1: case 2: case 3: case 4: case 5: case 6: return 2;
    case 7: case 0xA: case 0xC: return 1;
    case 8: return 3;
    case 9: return 5;
    case 0xB: case 0xD: case 0xF: return 4;
    case 0xE: { int sub = par >> 4; if (sub == 1 || sub == 2 || sub == 3 || sub == 4 || sub == 5) return 2; if (sub == 8) return 3; if (sub == 6 || sub == 0xE) return 4; if (sub == 0xA || sub == 0xB || sub == 7) return 1; return 5; }
    }
    return 5;
}

static void fmt_xcell_mod(const XCell *x, SongCell *o, int xm) {
    if (x->note) {
        o->has_note = 1;
        if (xm) { if (x->note == 97) strcpy(o->note, "==="); else put_note(o->note, x->note - 1); }
        else if (x->note == 0x7F) strcpy(o->note, "^^^");
        else put_note(o->note, x->note - 24 + 12);   /* PT-Oktave 1 = Anzeige 1: C-1 */
    }
    if (x->inst) { o->inst[0] = hexc(x->inst >> 4); o->inst[1] = hexc(x->inst); }
    if (x->eff || x->par) { fx3(o->fx, fxc(x->eff), x->par); o->fxclass = (uint8_t)cls_mod(x->eff, x->par); }
}

/* XM-Volumenspalte */
static void fmt_xm_vol(int b, SongCell *o) {
    if (!b) return;
    int hi = b >> 4, lo = b & 15;
    static const char L[16] = { 0, 0, 0, 0, 0, 0, '-', '+', 'd', 'u', 's', 'v', 'p', 'l', 'r', 'm' };
    if (hi >= 1 && hi <= 5) { int v = b - 0x10; if (v > 64) v = 64; o->vol[0] = 'v'; o->vol[1] = (char)('0' + v / 10); o->vol[2] = (char)('0' + v % 10); o->volclass = 1; }
    else if (hi >= 6) { o->vol[0] = L[hi]; o->vol[1] = hexc(lo); o->vol[2] = ' '; o->volclass = hi >= 0xA && hi <= 0xB ? 2 : (hi == 0xC || hi == 0xD || hi == 0xE) ? 3 : hi == 0xF ? 2 : 1; }
}

/* S3M: interne Codes des Originals -> Tracker-Buchstabe */
static void fmt_s3m_fx(const XCell *x, SongCell *o) {
    int e = x->eff, p = x->par;
    if (!e && !p) return;
    char L = 0; int cl = 5, par = p;
    switch (e) {
    case 0x10: L = 'A'; cl = 4; break;  case 0x0B: L = 'B'; cl = 4; break;  case 0x0D: L = 'C'; cl = 4; break;
    case 0x11: L = 'D'; cl = 1; break;  case 0x12: L = 'E'; cl = 2; break;  case 0x13: L = 'F'; cl = 2; break;
    case 0x03: L = 'G'; cl = 2; break;  case 0x04: L = 'H'; cl = 2; break;  case 0x14: L = 'I'; cl = 1; break;
    case 0x00: L = 'J'; cl = 2; break;  case 0x17: L = 'K'; cl = 1; break;  case 0x18: L = 'L'; cl = 1; break;
    case 0x09: L = 'O'; cl = 5; break;  case 0x15: L = 'Q'; cl = 5; break;  case 0x07: L = 'R'; cl = 1; break;
    case 0x16: L = 'T'; cl = 4; break;  case 0x1A: L = 'V'; cl = 1; par = p >> 2; break;
    case 0x24: L = 'Z'; cl = 5; break;
    case 0x0E:
        if (p >= 0x80) { L = 'X'; cl = 3; par = (p & 0x7F) << 3; }
        else {
            /* Rueckabbildung der S-Unterbefehle */
            int hi = p >> 4;
            static const struct { int pre; int sub; } M[] = { { 0x30, 1 }, { 0x50, 2 }, { 0x40, 3 }, { 0x70, 4 }, { 0x80, 8 }, { 0x60, 0xB }, { 0xC0, 0xC }, { 0xD0, 0xD }, { 0xE0, 0xE } };
            int sub = 0; for (int i = 0; i < 9; i++) if (M[i].pre == (p & 0xF0)) sub = M[i].sub;
            if (!hi) sub = 0;
            L = 'S'; par = (sub << 4) | (p & 15);
            cl = (sub == 8) ? 3 : (sub == 0xC || sub == 0xD) ? 5 : (sub == 0xB || sub == 0xE) ? 4 : 2;
        }
        break;
    default: L = fxc(e); cl = 5; break;
    }
    fx3(o->fx, L, par & 0xFF); o->fxclass = (uint8_t)cl;
}

static void fmt_it(const ITCell *x, SongCell *o) {
    if (x->has & IT_HAS_NOTE) {
        o->has_note = 1;
        if (x->note < 120) put_note(o->note, x->note);
        else if (x->note == 255) strcpy(o->note, "===");
        else if (x->note == 254) strcpy(o->note, "^^^");
        else strcpy(o->note, "~~~");
    }
    if (x->has & IT_HAS_INST) { o->inst[0] = (char)('0' + (x->inst / 10) % 10); o->inst[1] = (char)('0' + x->inst % 10); }
    if (x->has & IT_HAS_VOL) {
        int v = x->vol, k = 0, a = 0;
        if (v <= 64) { o->vol[0] = 'v'; o->vol[1] = (char)('0' + v / 10); o->vol[2] = (char)('0' + v % 10); o->volclass = 1; }
        else {
            char L = 0;
            if (v <= 74) { L = 'a'; a = v - 65; k = 1; } else if (v <= 84) { L = 'b'; a = v - 75; k = 1; }
            else if (v <= 94) { L = 'c'; a = v - 85; k = 1; } else if (v <= 104) { L = 'd'; a = v - 95; k = 1; }
            else if (v <= 114) { L = 'e'; a = v - 105; k = 2; } else if (v <= 124) { L = 'f'; a = v - 115; k = 2; }
            else if (v >= 128 && v <= 192) { L = 'p'; a = v - 128; k = 3; }
            else if (v >= 193 && v <= 202) { L = 'g'; a = v - 193; k = 2; } else if (v >= 203 && v <= 212) { L = 'h'; a = v - 203; k = 2; }
            if (L) {
                o->vol[0] = L;
                if (L == 'p') { o->vol[1] = (char)('0' + a / 10); o->vol[2] = (char)('0' + a % 10); }
                else { o->vol[1] = '0'; o->vol[2] = (char)('0' + a % 10); }
                o->volclass = (uint8_t)k;
            }
        }
    }
    if (x->has & IT_HAS_CMD) {
        int c = x->cmd; char L = (char)('A' + c - 1);
        int cl = 5;
        switch (L) {
        case 'A': case 'B': case 'C': case 'T': cl = 4; break;
        case 'D': case 'K': case 'L': case 'M': case 'N':  case 'R': case 'V': case 'W': case 'I': cl = 1; break;
        case 'E': case 'F': case 'G': case 'H': case 'J': case 'U': cl = 2; break;
        case 'P': case 'X': case 'Y': cl = 3; break;
        case 'S': cl = ((x->par >> 4) == 8 || (x->par >> 4) == 9) ? 3 : ((x->par >> 4) == 0xB || (x->par >> 4) == 0xE) ? 4 : 5; break;
        default: cl = 5; break;
        }
        if (c >= 1 && c <= 26) { fx3(o->fx, L, x->par); o->fxclass = (uint8_t)cl; }
    }
}

void song_cell(const Song *s, int pattern, int row, int ch, SongCell *o) {
    memset(o, 0, sizeof *o);
    strcpy(o->note, "..."); strcpy(o->inst, ".."); strcpy(o->vol, "..."); strcpy(o->fx, "...");
    if (pattern < 0) return;
    SongCell t; memset(&t, 0, sizeof t);
    if (s->kind == K_TFMX) {
        TfmxCell c = tfp_view_cell(s->tp, s->tstart + pattern, row, ch);
        int m = c.note < 48 ? c.note : 36 + (c.note - 48) % 12;       /* Noten 48..63 wiederholen die Periodentabelle ab 36 */
        if (c.kind == 1 || c.kind == 2) {
            put_note(t.note, m); t.has_note = 1;
            if (c.kind == 1) { t.inst[0] = hexc(c.macro >> 4); t.inst[1] = hexc(c.macro); t.inst[2] = 0; }
            t.vol[0] = 'v'; t.vol[1] = (char)('0' + c.vol / 10); t.vol[2] = (char)('0' + c.vol % 10); t.vol[3] = 0; t.volclass = 1;
            if (c.kind == 2) { fx3(t.fx, 'P', (uint8_t)c.fine); t.fxclass = 2; }
            else if (c.fine) { fx3(t.fx, 'D', (uint8_t)c.fine); t.fxclass = 2; }
        } else if (c.kind == 3) strcpy(t.note, "===");
        else if (c.kind == 4) { fx3(t.fx, 'V', c.macro); t.fxclass = 2; }
        else if (c.kind == 5) { fx3(t.fx, 'E', c.macro); t.fxclass = 1; }
    } else if (s->kind == K_DMF) {
        if (pattern >= s->dm->npat || row >= s->dm->pat[pattern].nrows || ch >= 32) return;
        const DMFCell *x = &s->dm->pat[pattern].cell[(size_t)row * 32 + ch];
        if (x->has & 2) {
            if (x->note == 255) strcpy(t.note, "^^^");
            else { int nn = x->note >= 129 ? x->note - 128 : x->note; put_note(t.note, nn - 1); t.has_note = 1; }
        }
        if (x->has & 1) { t.inst[0] = hexc(x->inst >> 4); t.inst[1] = hexc(x->inst); }
        if (x->has & 4) { int v = (x->vol + 2) / 4; t.vol[0] = 'v'; t.vol[1] = (char)('0' + v / 10 % 10); t.vol[2] = (char)('0' + v % 10); t.volclass = 1; }
        else if ((x->has & 32) && (x->ve_cmd || x->ve_par)) { t.vol[0] = (char)('a' + (x->ve_cmd + 25) % 26); t.vol[1] = hexc(x->ve_par >> 4); t.vol[2] = hexc(x->ve_par); t.volclass = (x->ve_cmd == 7 || x->ve_cmd == 8 || x->ve_cmd == 9) ? 3 : 1; }
        if ((x->has & 16) && (x->ne_cmd || x->ne_par)) { fx3(t.fx, fxc(x->ne_cmd), x->ne_par); t.fxclass = (x->ne_cmd >= 3 && x->ne_cmd <= 7) ? 2 : 5; }
        else if ((x->has & 8) && (x->ie_cmd || x->ie_par)) { fx3(t.fx, fxc(x->ie_cmd), x->ie_par); t.fxclass = 5; }
    } else if (s->kind == K_IT) {
        if (pattern >= s->it->npat || !s->it->pat[pattern] || row >= s->it->rows[pattern] || ch >= 64) return;
        fmt_it(&s->it->pat[pattern][(size_t)row * 64 + ch], &t);
    } else {
        int nr = song_pattern_rows(s, pattern);
        if (pattern >= s->xs.npat || row >= nr || ch >= s->xs.nch) return;
        const XCell *x = &s->xs.pat[((size_t)pattern * 64 + row) * s->xs.nch + ch];
        if (s->kind == K_XM) {
            /* XM: Pattern liegen dicht, Zeilen je Pattern = prows */
            size_t base = 0; for (int p = 0; p < pattern; p++) base += (size_t)song_pattern_rows(s, p) * s->xs.nch;
            x = &s->xs.pat[base + (size_t)row * s->xs.nch + ch];
            fmt_xcell_mod(x, &t, 1); fmt_xm_vol(x->vol1, &t);
        } else if (s->kind == K_S3M) {
            fmt_xcell_mod(x, &t, 0);
            t.fx[0] = 0; t.fxclass = 0;
            if (x->vol1) { int v = x->vol1 - 1; t.vol[0] = 'v'; t.vol[1] = (char)('0' + v / 10); t.vol[2] = (char)('0' + v % 10); t.volclass = 1; }
            fmt_s3m_fx(x, &t);
            if (x->inst) { t.inst[0] = (char)('0' + (x->inst / 10) % 10); t.inst[1] = (char)('0' + x->inst % 10); }
            if (x->note) { t.has_note = 1; if (x->note == 0x7F) strcpy(t.note, "^^^"); else put_note(t.note, x->note); }
        } else fmt_xcell_mod(x, &t, 0);
    }
    if (t.note[0]) memcpy(o->note, t.note, 4);
    if (t.inst[0]) memcpy(o->inst, t.inst, 3);
    if (t.vol[0]) memcpy(o->vol, t.vol, 4);
    if (t.fx[0]) memcpy(o->fx, t.fx, 4);
    o->has_note = t.has_note; o->fxclass = t.fxclass; o->volclass = t.volclass;
}

void song_scope_enable(Song *s, float *buf, int len) {
    if (s->ip) itp_set_scope(s->ip, buf, len);
    else if (s->dp) dmp_set_scope(s->dp, buf, len);
    else if (s->tp) tfp_set_scope(s->tp, buf, len);
    else if (s->xxp) xxm_set_scope(s->xxp, buf, len);
    else xplay_set_scope(s->xp, buf, len);
}
unsigned song_scope_pos(const Song *s) {
    if (s->ip) return itp_scope_pos(s->ip);
    if (s->dp) return dmp_scope_pos(s->dp);
    if (s->tp) return tfp_scope_pos(s->tp);
    if (s->xxp) return xxm_scope_pos(s->xxp);
    return xplay_scope_pos(s->xp);
}

int song_paula_filter(const Song *s) {
    if (s->tp) return tfp_get_lowpass(s->tp);
    if (s->xp && (s->kind == K_MOD || s->kind == K_S3M)) return s->kind == K_MOD ? xplay_get_lowpass(s->xp) : -1;
    return -1;
}
void song_toggle_paula_filter(Song *s) {
    int v = song_paula_filter(s);
    if (v < 0) return;
    if (s->tp) tfp_set_lowpass(s->tp, !v); else xplay_set_lowpass(s->xp, !v);
}

extern unsigned long long xplay_mute;
void song_set_mute(unsigned long long mask) { xplay_mute = mask; }
unsigned long long song_get_mute(void) { return xplay_mute; }

int song_chan_pan(const Song *s, int ch) {
    if (ch < 0 || ch >= s->nch) return 128;
    if (s->ip) return itp_chan_pan(s->ip, ch);
    if (s->dp) return dmp_chan_pan(s->dp, ch);
    if (s->tp) return tfp_chan_pan(s->tp, ch) ? 255 : 0;
    if (s->xxp) return xxm_chan_pan(s->xxp, ch);
    return xplay_chan_pan(s->xp, ch);
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }

const char *song_fx_name(const Song *s, const SongCell *c, int *is_vol) {
    if (is_vol) *is_vol = 0;
    char L = c->fx[0];
    if (L != '.') {
        int par = (hexv(c->fx[1]) << 4) | hexv(c->fx[2]);
        if (s->kind == K_DMF) {
            switch (L) {
            case '3': return "Arpeggio";   case '4': return "Porta Up";   case '5': return "Porta Down";
            case '6': return "Tone Porta"; case '7': return "Tone Porta (Note)";
            }
        } else if (s->kind == K_MOD || s->kind == K_XM) {
            switch (L) {
            case '0': return "Arpeggio";       case '1': return "Porta Up";      case '2': return "Porta Down";
            case '3': return "Tone Porta";     case '4': return "Vibrato";       case '5': return "TPorta+VolSlide";
            case '6': return "Vib+VolSlide";   case '7': return "Tremolo";       case '8': return "Set Pan";
            case '9': return "Sample Offset";  case 'A': return "Vol Slide";     case 'B': return "Pos Jump";
            case 'C': return "Set Volume";     case 'D': return "Pattern Break"; case 'F': return par < 32 ? "Set Speed" : "Set Tempo";
            case 'G': return "Global Vol";     case 'H': return "GVol Slide";    case 'K': return "Key Off";
            case 'L': return "Env Position";   case 'P': return "Pan Slide";     case 'R': return "Multi Retrig";
            case 'T': return "Tremor";         case 'X': return (par >> 4) == 1 ? "XFine Porta Up" : "XFine Porta Dn";
            case 'E': {
                static const char *E[16] = { "Filter", "Fine Porta Up", "Fine Porta Dn", "Glissando", "Vib Waveform", "Finetune", "Pattern Loop", "Trem Waveform",
                                             "Set Pan", "Retrigger", "Fine Vol Up", "Fine Vol Dn", "Note Cut", "Note Delay", "Pattern Delay", "Invert Loop" };
                return E[par >> 4];
            }
            }
        } else {
            switch (L) {
            case 'A': return "Set Speed";      case 'B': return "Pos Jump";      case 'C': return "Pattern Break";
            case 'D': return "Vol Slide";      case 'E': return "Porta Down";    case 'F': return "Porta Up";
            case 'G': return "Tone Porta";     case 'H': return "Vibrato";       case 'I': return "Tremor";
            case 'J': return "Arpeggio";       case 'K': return "Vib+VolSlide";  case 'L': return "TPorta+VolSlide";
            case 'M': return "Chan Volume";    case 'N': return "ChanVol Slide"; case 'O': return "Sample Offset";
            case 'P': return "Pan Slide";      case 'Q': return "Retrigger";     case 'R': return "Tremolo";
            case 'T': return "Set Tempo";      case 'U': return "Fine Vibrato";  case 'V': return "Global Vol";
            case 'W': return "GVol Slide";     case 'X': return "Set Pan";       case 'Y': return "Panbrello";
            case 'Z': return "Filter";
            case 'S': {
                static const char *S[16] = { "Filter", "Glissando", "Finetune", "Vib Waveform", "Trem Waveform", "Panb Waveform", "Fine Pat Delay", "Inst Control",
                                             "Set Pan", "Sound Control", "High Offset", "Pattern Loop", "Note Cut", "Note Delay", "Pattern Delay", "Funk Repeat" };
                return S[par >> 4];
            }
            }
        }
        return "Effect";
    }
    char v = c->vol[0];
    if (v == '.' || v == 'v') return NULL;      /* leer oder einfaches "Set Volume" */
    if (is_vol) *is_vol = 1;
    if (s->kind == K_XM) {
        switch (v) { case '-': case '+': return "VolCol Slide"; case 'd': case 'u': return "VolCol Fine Slide"; case 's': return "VolCol Vib Speed";
                     case 'p': return "VolCol Set Pan"; case 'l': case 'r': return "VolCol Pan Slide"; case 'm': return "VolCol Tone Porta"; }
        return "VolCol";
    }
    switch (v) { case 'a': case 'b': return "VolCol Fine Slide"; case 'c': case 'd': return "VolCol Slide"; case 'e': case 'f': return "VolCol Porta";
                 case 'g': return "VolCol Tone Porta"; case 'h': return "VolCol Vibrato"; case 'p': return "VolCol Set Pan"; }
    return "VolCol";
}
