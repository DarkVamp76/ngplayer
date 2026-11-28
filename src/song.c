/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "song.h"
#include "core.h"
#include "it.h"
#include "dmf.h"
#include "tfmx.h"
#include "ahx.h"
#include "opl_replay.h"
#include "midi.h"
#include "herad.h"
#include "rad.h"
#include "ml_replay.h"
#include "lds.h"
#include "a2m_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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

enum { K_MOD, K_S3M, K_XM, K_IT, K_DMF, K_TFMX, K_AHX, K_OPL, K_MIDI, K_ML };

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
    AhxPlayer *ap;          /* AHX */
    OplSong *os; OplPlayer *op;   /* AdLib FM: AMD, MAD */
    A2mSong *a2s;                 /* AdLib FM: Adlib Tracker 2 (the player is op, too) */
    HeradSong *hs;                /* AdLib FM: HERAD (the player is op, too) */
    RadSong *rd;                  /* AdLib FM: Reality ADlib Tracker (the player is op, too) */
    LdsSong *ld;                  /* AdLib FM: LOUDNESS Sound System (the player is op, too) */
    Midi *mi;                     /* MIDI file played through the SoundFont (desktop and web) */
    MlSong *msong; MlRender *mr;  /* Musicline Editor */
    int tstart;             /* TFMX: erste Trackstep-Zeile des Subsongs */
    int nsub, cursub;       /* TFMX: Anzahl echter Subsongs, aktueller (0-basiert) */
    char title[40];
    const char *fmt;
    char tracker[64];             /* the tracker that wrote the file, as far as the header tells (Info window) */
    int nch, nord;
};


/* ---- Info window: which tracker wrote the file, read from the header while the file is still in memory ---- */
static void trim_copy(char *dst, size_t cap, const uint8_t *src, size_t n) {
    size_t k = 0; while (k < n && k + 1 < cap && src[k] >= 32 && src[k] < 127) { dst[k] = (char)src[k]; k++; }
    while (k > 0 && dst[k - 1] == ' ') k--;
    dst[k] = 0;
}
static void detect_tracker(Song *s, const uint8_t *b, long n) {
    char *t = s->tracker; const char *f = s->fmt; t[0] = 0;
    if (!strcmp(f, "ML")) {                                   /* Musicline Editor: the VERS chunk holds the version word (0x0115 = 1.15) */
        for (long i = 8; i + 10 <= n && i < 64; i++) if (!memcmp(b + i, "VERS", 4)) { unsigned v = (b[i + 8] << 8) | b[i + 9]; snprintf(t, 64, "Musicline Editor %u.%u%u", v >> 8, (v >> 4) & 15, v & 15); return; }
        snprintf(t, 64, "Musicline Editor"); return;
    }
    if (!strcmp(f, "XM") && n >= 58 && !memcmp(b, "Extended Module: ", 17)) trim_copy(t, 64, b + 38, 20);
    else if (!strcmp(f, "S3M") && n > 0x2A) {
        unsigned v = b[0x28] | (b[0x29] << 8), mj = (v >> 8) & 15;
        unsigned flg = b[0x26] | (b[0x27] << 8), ordn = b[0x20] | (b[0x21] << 8), special = b[0x3E] | (b[0x3F] << 8);
        switch (v >> 12) {
        case 1:
            /* ModPlug Tracker (and the first OpenMPT versions) wrote "ST3.20" into the header; libopenmpt recognises them by the other fields:
             * no GUS click removal, the panning table present, nothing in the special field, an even order count and only the flags 16 / 64 */
            if (v == 0x1320 && b[0x34] == 0 && b[0x35] == 0xFC && special == 0 && (flg & ~0x50u) == 0 && !(ordn & 1)) snprintf(t, 64, "ModPlug Tracker");
            else snprintf(t, 64, "Scream Tracker %u.%02x", mj, v & 0xFF);
            break;
        case 2: snprintf(t, 64, "Imago Orpheus %u.%02x", mj, v & 0xFF); break;
        case 3: snprintf(t, 64, "Impulse Tracker %u.%02x", mj, v & 0xFF); break;
        case 4: snprintf(t, 64, "Schism Tracker"); break;
        case 5:                                          /* OpenMPT 1.xx (BCD); from 1.29 the file also names the last two parts of the version (bytes 0x37, 0x36), like libopenmpt shows it */
            if (v >= 0x5129) snprintf(t, 64, "OpenMPT %u.%02x.%02x.%02x", mj, v & 0xFF, b[0x37], b[0x36]);
            else snprintf(t, 64, "OpenMPT %u.%02x", mj, v & 0xFF);
            break;
        case 6: snprintf(t, 64, "BeRoTracker"); break;
        case 7: snprintf(t, 64, "CreamTracker"); break;
        default: snprintf(t, 64, "Scream Tracker compatible"); break;
        }
    } else if (!strcmp(f, "IT") && s->it) {
        unsigned v = (unsigned)s->it->cwt, mj = (v >> 8) & 15;
        if (v == 0x0888 || v == 0x0889) snprintf(t, 64, "OpenMPT (1.18 or later)");
        else switch (v >> 12) {
        case 0: snprintf(t, 64, "Impulse Tracker %u.%02x", mj, v & 0xFF); break;
        case 1: snprintf(t, 64, "Schism Tracker"); break;
        case 5: snprintf(t, 64, "OpenMPT"); break;
        case 6: snprintf(t, 64, "BeRoTracker"); break;
        case 7: snprintf(t, 64, "ITMCK"); break;
        default: snprintf(t, 64, "Impulse Tracker compatible"); break;
        }
    } else if (!strcmp(f, "MOD") && n >= 1084) {
        const uint8_t *m = b + 1080;
        if (!memcmp(m, "M.K.", 4) || !memcmp(m, "M!K!", 4)) snprintf(t, 64, "ProTracker compatible (%.4s)", (const char *)m);
        else if (!memcmp(m, "FLT", 3)) snprintf(t, 64, "Startrekker (%.4s)", (const char *)m);
        else if (!memcmp(m, "OKTA", 4) || !memcmp(m, "CD81", 4)) snprintf(t, 64, "Oktalyzer / Atari (%.4s)", (const char *)m);
        else if ((m[1] == 'C' && m[2] == 'H' && m[3] == 'N') || (m[2] == 'C' && m[3] == 'H')) snprintf(t, 64, "FastTracker II / TakeTracker (%.4s)", (const char *)m);
        else snprintf(t, 64, "Soundtracker compatible");
    } else if (!strcmp(f, "MOD")) snprintf(t, 64, "Soundtracker (15 samples)");
    else if (!strcmp(f, "RAD") && n > 16) snprintf(t, 64, "Reality ADlib Tracker %d.%d", b[16] >> 4, b[16] & 15);
    else if (!strcmp(f, "A2M") && s->a2s) snprintf(t, 64, "Adlib Tracker 2 (%sfile version %d)", s->a2s->is_tiny ? "tiny module, " : "", s->a2s->ffver);
    else if (!strcmp(f, "HERAD") && s->hs) snprintf(t, 64, "HERAD System (Cryo)");
    else if (!strcmp(f, "AMD")) snprintf(t, 64, "AMUSIC Adlib Tracker");
    else if (!strcmp(f, "MAD")) snprintf(t, 64, "Mlat Adlib Tracker (Mad Tracker 2)");
}

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

#ifndef __EMSCRIPTEN__
#include <dirent.h>
#endif
/* the .syx file that belongs to a MIDI file: the same name, any case, in the same folder (NAME.mid / NAME.m32 + NAME.syx) */
static int find_syx(const char *path, char *out, size_t cap, char *name, size_t ncap) {
#ifdef __EMSCRIPTEN__
    (void)path; (void)out; (void)cap; (void)name; (void)ncap; return 0;
#else
    char dir[512], stem[256]; const char *sl = strrchr(path, '/');
    if (sl) { size_t n = (size_t)(sl - path); if (n >= sizeof dir) return 0; memcpy(dir, path, n); dir[n] = 0; } else strcpy(dir, ".");
    snprintf(stem, sizeof stem, "%s", sl ? sl + 1 : path);
    char *dt = strrchr(stem, '.'); if (dt) *dt = 0;
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; int found = 0, nsyx = 0; char only[256] = "";
    while ((e = readdir(d))) {
        const char *x = strrchr(e->d_name, '.'); if (!x || strcasecmp(x, ".syx")) continue;
        size_t n = (size_t)(x - e->d_name);
        if (n == strlen(stem) && !strncasecmp(e->d_name, stem, n)) { snprintf(out, cap, "%s/%s", dir, e->d_name); snprintf(name, ncap, "%s", e->d_name); found = 1; break; }
        if (nsyx++ == 0) snprintf(only, sizeof only, "%s", e->d_name);
    }
    closedir(d);
    if (!found && nsyx == 1) { snprintf(out, cap, "%s/%s", dir, only); snprintf(name, ncap, "%s", only); found = 1; }      /* no .syx with the name of the song, but exactly one in the folder (one set-up for a whole game): that one */
    return found;
#endif
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

    { size_t um = umx_extract(b, (size_t)n); if (um) n = (long)um; }          /* Unreal package: play the module inside */
    char gdm_trk[64] = "";
    if (gdm_probe(b, (size_t)n)) {                                    /* GDM: rewritten as an S3M image */
        static const char *const org[] = { "?", "MOD", "MTM", "S3M", "669", "FAR", "ULT", "STM", "MED", "PSM" };
        unsigned of = b[116] | (b[117] << 8);
        snprintf(gdm_trk, sizeof gdm_trk, "BWSB 2GDM %u.%02u (from %s)", b[79], b[80], of < 10 ? org[of] : "?");
        size_t gn = 0; uint8_t *g = gdm_convert(b, (size_t)n, &gn);
        if (!g) { free(b); snprintf(err, errcap, "GDM: damaged file"); return NULL; }
        free(b); b = g; n = (long)gn;
    }
    Song *s = (Song *)calloc(1, sizeof *s);
    int r = 0;
    const char *dot = strrchr(path, '.');
    if (dot && (!strcasecmp(dot, ".lds") || !strcasecmp(dot, ".ld0"))) {          /* LOUDNESS has no signature: the extension decides */
        int e = 0;
        s->ld = lds_load(b, (size_t)n, !strcasecmp(dot, ".ld0"), &e);
        if (!s->ld) { snprintf(err, errcap, "LDS: damaged or unsupported file (%d)", e); free(b); song_close(s); return NULL; }
        s->kind = K_OPL; s->fmt = "LDS"; s->title[0] = 0;
        s->nch = 9; s->nord = (int)s->ld->nposi;
        s->op = opl_new_lds(s->ld, rate);
        if (!s->op) r = -1;
    } else if (dot && (!strcasecmp(dot, ".hsq") || !strcasecmp(dot, ".sqx") || !strcasecmp(dot, ".sdb") || !strcasecmp(dot, ".agd") || !strcasecmp(dot, ".ha2"))) {
        int e = 0;
        s->hs = herad_load(b, (size_t)n, &e);
        if (!s->hs) { snprintf(err, errcap, "HERAD: damaged or unsupported file (%d)", e); free(b); song_close(s); return NULL; }
        s->kind = K_OPL; s->fmt = "HERAD";
        s->title[0] = 0;
        s->nch = s->hs->ntracks > (s->hs->agd ? 18 : 9) ? (s->hs->agd ? 18 : 9) : s->hs->ntracks;
        s->nord = (int)(s->hs->last_tick / HERAD_MEASURE_TICKS) + 1;
        s->op = opl_new_herad(s->hs, rate);
        if (!s->op) r = -1;
    } else if (NG_MIDI && midi_probe(b, (size_t)n)) {
        s->mi = midi_open(b, (size_t)n, rate, err, errcap);
        if (!s->mi) { free(b); song_close(s); return NULL; }
        MidiInfo mi; midi_info(s->mi, &mi);
        if (dot && !strcasecmp(dot, ".m32")) midi_hint_mt32(s->mi);          /* .m32: written for the MT-32 */
        {   /* a .syx file with the same name next to the MIDI file: it comes first */
            char sp[700], nm[96];
            if (find_syx(path, sp, sizeof sp, nm, sizeof nm)) { FILE *sf = fopen(sp, "rb"); if (sf) { fseek(sf, 0, SEEK_END); long sn = ftell(sf); fseek(sf, 0, SEEK_SET); if (sn > 0 && sn < (4L << 20)) { uint8_t *sb = (uint8_t *)malloc((size_t)sn); if (sb && fread(sb, 1, (size_t)sn, sf) == (size_t)sn) midi_add_syx(s->mi, nm, sb, (size_t)sn); free(sb); } fclose(sf); } }
        }
        midi_info(s->mi, &mi);
        s->kind = K_MIDI; s->fmt = "MIDI"; snprintf(s->title, sizeof s->title, "%s", mi.title);
        if (!s->title[0]) {                                           /* the file names no song: the file name without its extension */
            const char *bn = strrchr(path, '/'); const char *bs = strrchr(path, '\\'); if (bs > bn) bn = bs; bn = bn ? bn + 1 : path;
            snprintf(s->title, sizeof s->title, "%s", bn); char *dot = strrchr(s->title, '.'); if (dot && dot > s->title) *dot = 0;
        }
        s->nch = 16; s->nord = midi_measures(s->mi);                 /* no patterns: the orders are 4/4 measures of 16 rows */
    } else if (ml_probe(b, (size_t)n)) {
        int e = 0;
        s->msong = ml_load(b, (size_t)n, &e);
        if (!s->msong) { snprintf(err, errcap, "Musicline Editor: damaged or unsupported file (%d)", e); free(b); song_close(s); return NULL; }
        s->nsub = s->msong->ntunes; s->cursub = sub >= 0 && sub < s->nsub ? sub : 0;
        s->mr = ml_render_new(s->msong, s->cursub, rate);
        if (!s->mr) { snprintf(err, errcap, "Musicline Editor: this tune has no channels"); free(b); song_close(s); return NULL; }
        s->kind = K_ML; s->fmt = "ML";
        snprintf(s->title, sizeof s->title, "%s", s->msong->name[0] ? s->msong->name : s->msong->tune[s->cursub].title);
        s->nch = s->mr->pl.number_of_channels; s->nord = ml_seq_length(&s->msong->tune[s->cursub], 0);
    } else if (rad_probe(b, (size_t)n)) {
        int e = 0;
        s->rd = rad_load(b, (size_t)n, &e);
        if (!s->rd) { snprintf(err, errcap, "RAD: damaged or unsupported tune (%d)", e); free(b); song_close(s); return NULL; }
        s->kind = K_OPL; s->fmt = "RAD"; s->title[0] = 0;
        s->nch = RAD_CHANNELS; s->nord = s->rd->norder;
        s->op = opl_new_rad(s->rd, rate);
        if (!s->op) r = -1;
    } else if (n > 4 && !memcmp(b, "IMPM", 4)) {
        int e = 0;
        s->it = it_load(b, (size_t)n, &e);
        if (!s->it) { r = e ? e : -1; }
        else {
            s->kind = K_IT; s->fmt = "IT";
            snprintf(s->title, sizeof s->title, "%s", s->it->title);
            s->nch = s->it->nch; s->nord = s->it->nord;
            s->ip = itp_new(s->it, rate);
        }
    } else if (dtm_probe(b, (size_t)n)) {
        int e = 0;
        s->it = dtm_load(b, (size_t)n, &e);
        if (!s->it) { r = e ? e : -1; }
        else {
            s->kind = K_IT; s->fmt = "DTM";
            snprintf(s->title, sizeof s->title, "%s", s->it->title);
            s->nch = s->it->nch; s->nord = s->it->nord;
            s->ip = itp_new(s->it, rate);
        }
    } else if (a2m_probe(b, (size_t)n)) {
        int e = 0;
        s->a2s = a2m_load(b, (size_t)n, &e);
        if (!s->a2s) { snprintf(err, errcap, "Adlib Tracker 2: damaged or unsupported module (%d)", e); free(b); song_close(s); return NULL; }
        s->kind = K_OPL; s->fmt = "A2M";
        snprintf(s->title, sizeof s->title, "%s", s->a2s->songinfo.songname);
        s->nch = s->a2s->songinfo.nm_tracks;
        { int last = -1; for (int i = 0; i < 128; i++) if (s->a2s->songinfo.pattern_order[i] < 0x80) last = i; s->nord = last + 1; }
        s->op = opl_new_a2m(s->a2s, rate);
        if (!s->op) r = -1;
    } else if (opl_probe(b, (size_t)n)) {
        int e = 0;
        s->os = opl_load(b, (size_t)n, &e);
        if (!s->os) { r = e ? e : -1; }
        else {
            s->kind = K_OPL; s->fmt = s->os->is_mad ? "MAD" : "AMD";
            snprintf(s->title, sizeof s->title, "%s", s->os->is_mad ? "" : s->os->title);
            s->nch = s->os->nchan; s->nord = s->os->length;
            s->op = opl_new(s->os, rate);
            if (!s->op) r = -1;
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
    } else if (ahx_probe(b, (size_t)n)) {
        s->ap = ahx_new(b, (size_t)n, rate);
        if (!s->ap) r = -1;
        else {
            s->kind = K_AHX; s->fmt = "AHX";
            snprintf(s->title, sizeof s->title, "%s", ahx_title(s->ap));
            s->nch = ahx_channels(s->ap); s->nord = ahx_npos(s->ap);
        }
    } else if (n > 80 && !memcmp(b, "Extended Module: ", 17)) {
        r = xxm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_XM; s->fmt = "XM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xxp = xxm_new(&s->xs, rate);
        }
    } else if (dbm_probe(b, (size_t)n)) {
        r = xdbm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_XM; s->fmt = "DBM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xxp = xxm_new(&s->xs, rate);
        }
    } else if (mdl_probe(b, (size_t)n)) {
        r = xmdl_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_XM; s->fmt = "MDL";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xxp = xxm_new(&s->xs, rate);
        }
    } else if (psm_probe(b, (size_t)n)) {
        r = xpsm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "PSM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (far_probe(b, (size_t)n)) {
        r = xfar_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "FAR";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (med_probe(b, (size_t)n)) {
        r = xmed_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "MED";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (sfx_probe(b, (size_t)n)) {
        r = xsfx_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "SFX";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (amf_probe(b, (size_t)n)) {
        r = xamf_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "AMF";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (dsm_probe(b, (size_t)n)) {
        r = xdsm_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "DSM";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    } else if (asylum_probe(b, (size_t)n)) {
        r = xasylum_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "AMF";
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
    } else if (n > 4 && !memcmp(b, "AMF", 3) && b[3] >= 8 && b[3] <= 14) {      /* DSMI Advanced Module Format: not supported (yet) */
        snprintf(err, errcap, "AMF (DSMI) is not supported yet");
        free(b); song_close(s); return NULL;
    } else {
        r = xmod_load(&s->xs, b, (size_t)n);
        if (!r) {
            s->kind = K_MOD; s->fmt = "MOD";
            snprintf(s->title, sizeof s->title, "%s", s->xs.title);
            s->nch = s->xs.nch; s->nord = s->xs.nord;
            s->xp = xplay_new(&s->xs, rate);
        }
    }
    if (!r) detect_tracker(s, b, n);
    if (!r && gdm_trk[0]) snprintf(s->tracker, sizeof s->tracker, "%s", gdm_trk);
    free(b);
    if (r || (!s->xp && !s->xxp && !s->ip && !s->dp && !s->tp && !s->ap && !s->op && !s->mi && !s->mr)) {
        snprintf(err, errcap, "unsupported or damaged module (%d)", r);
        song_close(s);
        return NULL;
    }
    {   /* module titles are padded with spaces (XM: 20 characters, some 669 and others centre the text): cut the padding at both ends, so the "i" sits next to the text */
        size_t n = strlen(s->title), lead = 0;
        while (n > 0 && ((unsigned char)s->title[n - 1] <= ' ')) s->title[--n] = 0;
        while (lead < n && (unsigned char)s->title[lead] <= ' ') lead++;          /* titles written centred have their padding in front */
        if (lead) memmove(s->title, s->title + lead, n - lead + 1);
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
    if (s->ap) ahx_free(s->ap);
    if (s->op) opl_pfree(s->op);
    if (s->os) opl_free(s->os);
    if (s->a2s) a2m_free(s->a2s);
    if (s->hs) herad_free(s->hs);
    if (s->rd) rad_free(s->rd);
    if (s->ld) lds_free(s->ld);
    if (s->mi) midi_close(s->mi);
    if (s->mr) ml_render_free(s->mr);
    if (s->msong) ml_free(s->msong);
    if (s->tp) tfp_free(s->tp);
    if (s->tf) tfmx_free(s->tf);
    if (s->it) it_free(s->it);
    if (s->kind != K_IT && s->kind != K_DMF && s->kind != K_TFMX && s->kind != K_AHX && s->kind != K_OPL && s->kind != K_MIDI && s->kind != K_ML) xsong_free(&s->xs);
    free(s);
}

int song_render(Song *s, int16_t *out, int n) {
    if (s->mi) { midi_set_mute(s->mi, (unsigned)song_get_mute()); return midi_render(s->mi, out, n); }
    if (s->op) { opl_set_mute(s->op, (unsigned)song_get_mute()); return opl_render(s->op, out, n); }
    if (s->ap) { ahx_set_mute(s->ap, (unsigned)song_get_mute()); return ahx_render(s->ap, out, n); }
    if (s->ip) return itp_render(s->ip, out, n);
    if (s->dp) return dmp_render(s->dp, out, n);
    if (s->mr) { s->mr->mute = song_get_mute(); return ml_render(s->mr, out, n); }
    if (s->tp) return tfp_render(s->tp, out, n);
    if (s->xxp) return xxm_render(s->xxp, out, n);
    return xplay_render(s->xp, out, n);
}

const char *song_title(const Song *s) { return s->title; }
const char *song_format(const Song *s) { return s->fmt; }
int song_channels(const Song *s) { return s->nch; }
int song_norders(const Song *s) { return s->nord; }
int song_subsongs(const Song *s) { return (s->kind == K_TFMX || s->kind == K_ML) && s->nsub > 0 ? s->nsub : 1; }
int song_subsong(const Song *s) { return s->kind == K_TFMX || s->kind == K_ML ? s->cursub : 0; }
void song_set_fast(Song *s, int on) { if (s && s->mr) s->mr->fast = on ? 1 : 0; }
Song *song_open(const char *path, int rate, char *err, size_t errcap) { return song_open_sub(path, rate, 0, err, errcap); }
int song_order(const Song *s) {
    if (s->mi) { double t = midi_tick(s->mi); return (int)(t / (midi_division(s->mi) * 4)); }
    if (s->op) { int o = 0; opl_position(s->op, &o, NULL, NULL); return o; }
    if (s->ap) { int o = 0; ahx_pos(s->ap, &o, NULL); return o; }
    if (s->dp) { int o = 0; dmp_pos(s->dp, &o, NULL, NULL); return o; }
    if (s->mr) return s->mr->pl.voices[0].tune_position;
    if (s->tp) { int o = tfp_pos(s->tp) - s->tstart; return o < 0 ? 0 : o; }
    if (s->xp) return xplay_order(s->xp);
    if (s->xxp) return xxm_order(s->xxp);
    return -1;
}

static int ml_pattern_rows(const Song *s, int order);
/* ================================================================== Pattern-Zugriff */
static int pat_of(const Song *s, int order) {
    if (order < 0 || order >= s->nord) return -1;
    if (s->kind == K_MIDI) return order;
    if (s->kind == K_ML) return order;
    if (s->kind == K_TFMX || s->kind == K_AHX) return order;          /* TFMX: Trackstep-Zeile; AHX: die Position (4 Tracks) ist das Pattern */
    if (s->kind == K_OPL && s->hs) return order;                       /* HERAD: the measure */
    if (s->kind == K_OPL && s->rd) return rad_order_track(s->rd, order);   /* RAD: the track, -1 = jump marker */
    if (s->kind == K_OPL && s->ld) return order < (int)s->ld->nposi ? order : -1;     /* LDS: the position */
    if (s->kind == K_OPL && s->a2s) { int p = s->a2s->songinfo.pattern_order[order & 127]; return p >= 0x80 || p >= s->a2s->eventsinfo.patterns ? -1 : p; }
    if (s->kind == K_OPL) { int p = s->os->order[order]; return p >= 0x80 || p >= s->os->nop ? -1 : p; }
    if (s->kind == K_DMF) { int p = s->dm->order[order]; return p < s->dm->npat ? p : -1; }
    if (s->kind == K_IT) { int p = s->it->orders[order]; return p >= 254 ? -1 : (p < s->it->npat ? p : -1); }
    int p = s->xs.orders[order];
    return p >= 0xFE || p >= s->xs.npat ? -1 : p;
}
int song_order_pattern(const Song *s, int order) { return pat_of(s, order); }

int song_pattern_rows(const Song *s, int pattern) {
    if (s->kind == K_MIDI) return 16;
    if (s->kind == K_ML) return s->mr ? ml_pattern_rows(s, pattern) : 128;
    if (s->kind == K_OPL && s->hs) return HERAD_MEASURE_TICKS;           /* one row per tick */
    if (s->kind == K_OPL && s->rd) return RAD_LINES;
    if (s->kind == K_OPL && s->ld) return s->ld->pattlen;
    if (s->kind == K_OPL && s->a2s) return s->a2s->songinfo.patt_len;
    if (s->kind == K_OPL) return s->os->nrows;
    if (s->kind == K_AHX) return ahx_tracklen(s->ap);
    if (s->kind == K_DMF) return pattern >= 0 && pattern < s->dm->npat ? s->dm->pat[pattern].nrows : 64;
    if (s->kind == K_IT) return pattern >= 0 && pattern < s->it->npat ? s->it->rows[pattern] : 64;
    if (s->kind == K_XM && s->xs.prows && pattern >= 0 && pattern < s->xs.npat) return s->xs.prows[pattern];
    if (s->kind == K_TFMX) { int r = tfp_view_rows(s->tp, s->tstart + pattern); return r < 1 ? 1 : r; }
    return 64;
}

void song_pos(const Song *s, SongPos *o) {
    int ord = 0, row = 0, pat = 0;
    if (s->mi) { double t = midi_tick(s->mi); int mt = midi_division(s->mi) * 4; ord = (int)(t / mt); if (ord >= s->nord) ord = s->nord - 1; pat = ord; row = (int)((t - (double)ord * mt) * 16.0 / mt); if (row > 15) row = 15; if (row < 0) row = 0; }
    else if (s->op) { opl_position(s->op, &ord, &row, NULL); pat = pat_of(s, ord); }
    else if (s->ip) itp_pos(s->ip, &ord, &pat, &row);
    else if (s->ap) { ahx_pos(s->ap, &ord, &row); pat = ord; }
    else if (s->dp) dmp_pos(s->dp, &ord, &pat, &row);
    else if (s->mr) { const MlVoice *v0 = &s->mr->pl.voices[0]; ord = v0->tune_position; pat = ord; row = v0->part_position_work & 127; }
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
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: L = (char)('0' + (e - 0x30)); cl = 2; break;     /* 669: slide up, slide down, slide to note, frequency adjust, vibrato */
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


/* ---- Musicline Editor: the parts in the pattern view ---- */
typedef struct { uint8_t code; char sym; const char *name; } MlFxName;
static const MlFxName ml_fx_names[] = {
    { 0x01, '1', "Slide Up" },
    { 0x02, '2', "Slide Down" },
    { 0x03, '3', "Portamento" },
    { 0x04, '4', "Init Inst Portamento" },
    { 0x05, '5', "Pitch Up" },
    { 0x06, '6', "Pitch Down" },
    { 0x07, '7', "Vibrato Speed" },
    { 0x08, '8', "Vibrato Up" },
    { 0x09, '9', "Vibrato Down" },
    { 0x0A, 'A', "Vibrato Wave" },
    { 0x0B, 'B', "Set Fine Tune" },
    { 0x10, 'C', "Set Inst Vol" },
    { 0x11, 'D', "Inst Vol Slide Up" },
    { 0x12, 'E', "Inst Vol Slide Down" },
    { 0x13, 'F', "Set Slide Inst Vol" },
    { 0x14, 'G', "Slide To Inst Vol" },
    { 0x15, 'H', "Inst Vol Add" },
    { 0x16, 'I', "Inst Vol Sub" },
    { 0x17, 'J', "Tremolo Speed" },
    { 0x18, 'K', "Tremolo Up" },
    { 0x19, 'L', "Tremolo Down" },
    { 0x1A, 'M', "Tremolo Wave" },
    { 0x20, 'N', "Set Chan Vol" },
    { 0x21, 'O', "Chan Vol Slide Up" },
    { 0x22, 'P', "Chan Vol Slide Down" },
    { 0x23, 'Q', "Set Slide Chan Vol" },
    { 0x24, 'R', "Slide To Chan Vol" },
    { 0x25, 'S', "Chan Vol Add" },
    { 0x26, 'T', "Chan Vol Sub" },
    { 0x27, 'U', "Set Vol All Chans" },
    { 0x30, 'V', "Set Master Vol" },
    { 0x31, 'W', "Master Vol Slide Up" },
    { 0x32, 'X', "Master Vol Slide Down" },
    { 0x33, 'Y', "Set Slide Master Vol" },
    { 0x34, 'Z', "Slide To Master Vol" },
    { 0x35, 'a', "Master Vol Add" },
    { 0x36, 'b', "Master Vol Sub" },
    { 0x40, 'c', "Speed One Chan" },
    { 0x41, 'd', "Groove One Chan" },
    { 0x42, 'e', "Speed All Chans" },
    { 0x43, 'f', "Groove All Chans" },
    { 0x44, 'g', "Arpeggio Table" },
    { 0x45, 'h', "Arpeggio Table One Step" },
    { 0x46, 'i', "Hold Sustain" },
    { 0x47, 'j', "Filter" },
    { 0x48, 'k', "Sample Offset" },
    { 0x49, 'l', "Restart No Vol" },
    { 0x4A, 'm', "Wave Sample" },
    { 0x4B, 'n', "Init Inst" },
    { 0xE1, 'o', "PT Slide Up" },
    { 0xE2, 'p', "PT Slide Down" },
    { 0xE3, 'q', "PT Portamento" },
    { 0xE4, 'r', "PT Fine Slide Up" },
    { 0xE5, 's', "PT Fine Slide Down" },
    { 0xE6, 't', "PT Vol Slide Up" },
    { 0xE7, 'u', "PT Vol Slide Down" },
    { 0xE8, 'v', "PT Tremolo" },
    { 0xE9, 'w', "PT Tremolo Wave" },
    { 0xEA, 'x', "PT Vibrato" },
    { 0xEB, 'y', "PT Vibrato Wave" }
};
static const MlFxName *ml_fx_by_code(int code) { for (size_t i = 0; i < sizeof ml_fx_names / sizeof ml_fx_names[0]; i++) if (ml_fx_names[i].code == code) return &ml_fx_names[i]; return NULL; }
static const MlFxName *ml_fx_by_sym(char c) { for (size_t i = 0; i < sizeof ml_fx_names / sizeof ml_fx_names[0]; i++) if (ml_fx_names[i].sym == c) return &ml_fx_names[i]; return NULL; }
static int ml_fx_class(int code) { int g = code >> 4; return g == 0 || code == 0xE1 || code == 0xE2 || code == 0xE3 || code == 0xE4 || code == 0xE5 || code == 0xEA || code == 0xEB ? 2 : (g == 1 || g == 2 || g == 3 || code >= 0xE6) ? 1 : (code >= 0x40 && code <= 0x43) ? 4 : 5; }
/* the part a channel plays at an order: the live one for the order that is playing, else the one the sequence names; -1 = a command step */
static int ml_part_at(const Song *s, int order, int ch, int *transpose) {
    const MlPlayer *p = &s->mr->pl;
    if (ch < 0 || ch >= p->number_of_channels) return -1;
    if (order == p->voices[0].tune_position) { *transpose = (int)(int8_t)p->voices[ch].transpose_number; return p->voices[ch].part_number; }
    if (order < 0 || order > 255 || !p->cur->seq[ch]) return -1;
    uint16_t d = p->cur->seq[ch][order]; int cmd = (d & 0xC0) >> 6;
    if ((d & 0x20) && cmd != 0) return -1;
    *transpose = (int)(d & 0x1F) - 16;
    return ((d << 2) & 0x300) | ((d & 0xFF00) >> 8);
}
static int ml_pattern_rows(const Song *s, int order) {
    int best = 1;
    for (int ch = 0; ch < s->mr->pl.number_of_channels; ch++) {
        int tr = 0, pn = ml_part_at(s, order, ch, &tr); const MlPart *pt = pn >= 0 && pn < 1024 ? s->msong->part[pn] : NULL;
        if (!pt) continue;
        int n = 128; for (int i = 0; i < 128; i++) if (pt->line[i].note == 61) { n = i + 1; break; }
        if (n > best) best = n;
    }
    return best;
}
static void ml_cell(const Song *s, int order, int row, int ch, SongCell *t) {
    int tr = 0, pn = ml_part_at(s, order, ch, &tr);
    const MlPart *pt = pn >= 0 && pn < 1024 ? s->msong->part[pn] : NULL;
    if (!pt || row < 0 || row > 127) return;
    const MlPartLine *l = &pt->line[row];
    if (l->note == 61) strcpy(t->note, "===");                                         /* end of the part */
    else if (l->note & 0x80) { t->note[0] = 'J'; t->note[1] = hexc((l->note & 0x7F) >> 4); t->note[2] = hexc(l->note & 0x7F); t->note[3] = 0; }   /* jump to line */
    else if (l->note) { int n = l->note - 1 + 12 + tr; if (n < 0) n = 0; put_note(t->note, n); t->has_note = 1; }
    if (l->instrument && !(l->note & 0x80)) { t->inst[0] = hexc(l->instrument >> 4); t->inst[1] = hexc(l->instrument); t->inst[2] = 0; }
    int k = 0;
    for (int i = 0; i < 5 && k < 2; i++) {
        const MlEff *e = &l->eff[i]; if (!e->effect) continue;
        const MlFxName *f = ml_fx_by_code(e->effect); if (!f) continue;
        if (k == 0) { fx3(t->fx, f->sym, e->argument); t->fxclass = (uint8_t)ml_fx_class(e->effect); }
        else { fx3(t->vol, f->sym, e->argument); t->volclass = (uint8_t)ml_fx_class(e->effect); }
        k++;
    }
}

void song_cell(const Song *s, int pattern, int row, int ch, SongCell *o) {
    memset(o, 0, sizeof *o);
    strcpy(o->note, "..."); strcpy(o->inst, ".."); strcpy(o->vol, "..."); strcpy(o->fx, "...");
    if (pattern < 0 || s->kind == K_MIDI) return;
    SongCell t; memset(&t, 0, sizeof t);
    if (s->kind == K_ML) { if (s->mr) ml_cell(s, pattern, row, ch, &t); }
    else if (s->kind == K_TFMX) {
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
    } else if (s->kind == K_AHX) {
        int note, inst, fx, par;
        if (!ahx_cell(s->ap, pattern, row, ch, &note, &inst, &fx, &par)) return;
        if (note) { put_note(t.note, note + 11); t.has_note = 1; }              /* AHX note 1 = C-1 */
        if (inst) { t.inst[0] = hexc(inst >> 4); t.inst[1] = hexc(inst); }
        if (fx || par) { fx3(t.fx, fxc(fx), par); t.fxclass = (fx == 1 || fx == 2 || fx == 3 || fx == 5) ? 2 : (fx == 0xA || fx == 0xC) ? 1 : (fx == 0xB || fx == 0xD || fx == 0xF) ? 4 : 5; }
    } else if (s->kind == K_OPL && s->ld) {
        LdsCell lc; if (!lds_cell(s->ld, pattern, row, ch, &lc)) return;
        if (lc.kind == 1) {
            put_note(t.note, lc.note - 12); t.has_note = 1;                              /* the tune value 12 is C-0 (OPL block 0, F-number 343) */
            t.inst[0] = hexc(lc.inst >> 4); t.inst[1] = hexc(lc.inst);
        } else if (lc.kind == 2) {
            char L = lc.cmd == 0xff ? 'V' : lc.cmd == 0xfe ? 'T' : lc.cmd == 0xfd ? 'N' : lc.cmd == 0xfc ? 'E' : lc.cmd == 0xfb ? 'K' : lc.cmd == 0xfa ? 'B' : lc.cmd == 0xf9 ? 'J' :
                     lc.cmd == 0xf8 ? 'R' : lc.cmd == 0xf7 ? 'W' : lc.cmd == 0xf6 ? 'G' : lc.cmd == 0xf5 ? 'F' : lc.cmd == 0xf4 ? 'M' : lc.cmd == 0xf3 ? 'D' : lc.cmd == 0xf2 ? 'S' :
                     lc.cmd >= 0xf0 ? 'X' : 'P';
            int par = L == 'P' ? lc.cmd & 0x1f : lc.param;
            fx3(t.fx, L, par);
            t.fxclass = (L == 'V' || L == 'N' || L == 'M' || L == 'D') ? 1 : (L == 'G' || L == 'F' || L == 'W' || L == 'P' || L == 'R') ? 2 : (L == 'T' || L == 'B' || L == 'J' || L == 'E') ? 4 : 5;
        }
    } else if (s->kind == K_OPL && s->rd) {
        RadCell rc; if (!rad_cell(s->rd, pattern, row, ch, &rc)) return;
        if (rc.note == 15) strcpy(t.note, "===");
        else if (rc.note >= 1 && rc.note <= 12) { put_note(t.note, rc.octave * 12 + rc.note); t.has_note = 1; }      /* note 1 = C#, 12 = the C above, in the octave of the note */
        if (rc.inst) { t.inst[0] = hexc(rc.inst >> 4); t.inst[1] = hexc(rc.inst); }
        if (rc.effect || rc.param) {
            int e = rc.effect; char L = (char)(e < 10 ? '0' + e : 'A' + e - 10);
            fx3(t.fx, L, ((rc.param / 10) << 4) | (rc.param % 10));                        /* the parameter is decimal, 0 to 99 */
            t.fxclass = (e == 1 || e == 2 || e == 3 || e == 5) ? 2 : (e == 10 || e == 12 || e == 31) ? 1 : (e == 13 || e == 15) ? 4 : 5;      /* pitch, volume, flow, other */
        }
    } else if (s->kind == K_OPL && s->hs) {
        HeradCell hc; herad_cell(s->hs, ch, (uint32_t)pattern * HERAD_MEASURE_TICKS + (uint32_t)row, &hc);
        if (hc.note_kind == 1) { put_note(t.note, hc.note - 12); t.has_note = 1; }          /* HERAD note 36 is C-2 on the display like the other FM formats */
        else if (hc.note_kind == 2) strcpy(t.note, "===");
        if (hc.prog >= 0) { t.inst[0] = hexc(hc.prog >> 4); t.inst[1] = hexc(hc.prog); }
        if (hc.bend >= 0) { fx3(t.fx, 'E', (uint8_t)hc.bend); t.fxclass = 2; }
        else if (hc.touch >= 0) { fx3(t.fx, 'D', (uint8_t)hc.touch); t.fxclass = 1; }
        if (hc.note_kind == 1) { t.vol[0] = 'v'; t.vol[1] = hexc(hc.vel >> 4); t.vol[2] = hexc(hc.vel); t.vol[3] = 0; t.volclass = 1; }
    } else if (s->kind == K_OPL && s->a2s) {
        const tEVENTS_INFO *ei = &s->a2s->eventsinfo;
        if (pattern < 0 || pattern >= ei->patterns || row >= ei->rows || ch >= ei->channels) return;
        const tADTRACK2_EVENT *x = &ei->events[((size_t)pattern * ei->channels + ch) * ei->rows + row];
        if (x->note == 0xFF) strcpy(t.note, "===");
        else if (x->note >= 0x91 && x->note <= 0x91 + 96) { put_note(t.note, x->note - 0x91 + 12); t.has_note = 1; }
        else if (x->note >= 1 && x->note <= 97) { put_note(t.note, x->note - 1 + 12); t.has_note = 1; }
        if (x->instr_def) { t.inst[0] = hexc(x->instr_def >> 4); t.inst[1] = hexc(x->instr_def); }
        for (int k = 0; k < 2; k++) {
            int def = x->eff[k].def, val = x->eff[k].val;
            if (!def && !val) continue;
            if (!def) def = 0x80;                                                   /* arpeggio is stored as 0 */
            char L = def == 0x80 ? '0' : def < 48 ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ&%!@=#$~^`><"[def] : '?';
            int cl = (def == 9 || def == 10 || def == 12 || def == 16 || def == 17 || def == 18 || def == 20 || def == 24 || def == 25 || def == 37 || def == 40) ? 1
                   : (def == 11 || def == 13 || def == 14 || def == 15 || def == 45) ? 4
                   : (def == 0x80 || (def >= 1 && def <= 8) || def == 21 || (def >= 27 && def <= 34) || def == 38 || def == 39 || def == 42 || def == 43 || def == 46 || def == 47) ? 2 : 5;
            if (k == 0) { fx3(t.fx, L, val); t.fxclass = (uint8_t)cl; }
            else { t.vol[0] = L; t.vol[1] = hexc(val >> 4); t.vol[2] = hexc(val); t.volclass = (uint8_t)(cl == 1 ? 1 : cl == 2 ? 2 : 3); }
        }
    } else if (s->kind == K_OPL) {
        const OplSong *os = s->os;
        if (pattern < 0 || pattern >= os->nop || row >= os->nrows || ch >= os->nchan) return;
        unsigned tk = os->trackord[pattern * 9 + ch]; if (!tk) return;
        const OplCell *x = &os->tracks[(size_t)(tk - 1) * os->nrows + row];
        if (x->note == 127) strcpy(t.note, "===");
        else if (x->note) { put_note(t.note, x->note - 1); t.has_note = 1; }
        if (x->inst) { t.inst[0] = hexc(x->inst >> 4); t.inst[1] = hexc(x->inst); }
        if (x->command || x->p1 || x->p2) {
            fx3(t.fx, fxc(x->command), (x->p1 << 4) | (x->p2 & 15));
            int e = x->command;
            t.fxclass = (e >= 1 && e <= 6) || (e == 0 && (x->p1 || x->p2)) ? 2 : (e == 9 || e == 10 || e == 12 || e == 17) ? 1 : (e == 7 || e == 11 || e == 13 || e == 15 || e == 18) ? 4 : 5;
        }
    } else if (s->kind == K_DMF) {
        if (pattern >= s->dm->npat || row >= s->dm->pat[pattern].nrows || ch >= 32) return;
        const DMFCell *x = &s->dm->pat[pattern].cell[(size_t)row * 32 + ch];
        /* the flag bits of DMFCell.has as the loader sets them: 0x40 instrument, 0x20 note, 0x10 volume, 0x08 instrument effect, 0x04 note effect, 0x02 volume effect */
        if (x->has & 0x20) {
            if (x->note == 255) strcpy(t.note, "^^^");
            else { int nn = x->note >= 129 ? x->note - 128 : x->note; put_note(t.note, nn - 1); }
            t.has_note = 1;
        }
        if (x->has & 0x40) { t.inst[0] = hexc(x->inst >> 4); t.inst[1] = hexc(x->inst); }
        if (x->has & 0x10) { int v = (x->vol + 2) / 4; t.vol[0] = 'v'; t.vol[1] = (char)('0' + v / 10 % 10); t.vol[2] = (char)('0' + v % 10); t.volclass = 1; }
        else if ((x->has & 2) && (x->ve_cmd || x->ve_par)) { t.vol[0] = (char)('a' + (x->ve_cmd + 25) % 26); t.vol[1] = hexc(x->ve_par >> 4); t.vol[2] = hexc(x->ve_par); t.volclass = (x->ve_cmd == 7 || x->ve_cmd == 8 || x->ve_cmd == 9) ? 3 : 1; }
        if ((x->has & 4) && (x->ne_cmd || x->ne_par)) { fx3(t.fx, fxc(x->ne_cmd), x->ne_par); t.fxclass = (x->ne_cmd >= 3 && x->ne_cmd <= 7) ? 2 : 5; }
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
    if (s->mi) { midi_set_scope(s->mi, buf, len); return; }
    if (s->mr) { s->mr->sc = buf; s->mr->sc_len = len; return; }
    if (s->op) opl_set_scope(s->op, buf, len);
    else if (s->ap) ahx_set_scope(s->ap, buf, len);
    else if (s->ip) itp_set_scope(s->ip, buf, len);
    else if (s->dp) dmp_set_scope(s->dp, buf, len);
    else if (s->tp) tfp_set_scope(s->tp, buf, len);
    else if (s->xxp) xxm_set_scope(s->xxp, buf, len);
    else xplay_set_scope(s->xp, buf, len);
}
unsigned song_scope_pos(const Song *s) {
    if (s->mi) return midi_scope_pos(s->mi);
    if (s->mr) return s->mr->sc_pos;
    if (s->op) return opl_scope_pos(s->op);
    if (s->ap) return ahx_scope_pos(s->ap);
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
    if (s->mi) { int v = midi_chan_pan(s->mi, ch) * 2; return v > 255 ? 255 : v; }
    if (s->op) return 128;                                   /* the OPL2 is mono */
    if (s->ap) return ahx_chan_pan(s->ap, ch);
    if (s->ip) return itp_chan_pan(s->ip, ch);
    if (s->dp) return dmp_chan_pan(s->dp, ch);
    if (s->mr) return ((ch & 3) == 1 || (ch & 3) == 2) ? 255 : 0;
    if (s->tp) return tfp_chan_pan(s->tp, ch) ? 255 : 0;
    if (s->xxp) return xxm_chan_pan(s->xxp, ch);
    return xplay_chan_pan(s->xp, ch);
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }

const char *song_fx_name(const Song *s, const SongCell *c, int *is_vol) {
    if (is_vol) *is_vol = 0;
    if (s->kind == K_ML) {                                    /* Musicline: one symbol per part effect, the second effect sits in the volume column */
        const MlFxName *f = c->fx[0] != '.' ? ml_fx_by_sym(c->fx[0]) : NULL;
        if (f) return f->name;
        f = c->vol[0] != '.' ? ml_fx_by_sym(c->vol[0]) : NULL;
        if (f) { if (is_vol) *is_vol = 1; return f->name; }
        return NULL;
    }
    char L = c->fx[0];
    if (L != '.') {
        int par = (hexv(c->fx[1]) << 4) | hexv(c->fx[2]);
        if (s->kind == K_OPL && s->ld) {
            switch (L) {
            case 'V': return "Scale Volume";   case 'T': return "Set Tempo";       case 'N': return "Next Note Volume"; case 'E': return "End of Song";
            case 'K': return "Key Off";        case 'B': return "Break to Next";   case 'J': return "Jump to Position"; case 'R': return "Reset Glide";
            case 'W': return "Vibrato";        case 'G': return "Glide To";        case 'F': return "Fine Tune";        case 'M': return "Main Volume";
            case 'D': return "Fade";           case 'S': return "Tremolo Stay";    case 'P': return "Portamento";       case 'X': return "MIDI (ignored)";
            }
        } else if (s->kind == K_OPL && s->rd) {
            switch (L) {
            case '1': return "Portamento Up";    case '2': return "Portamento Down";   case '3': return "Tone Slide";
            case '5': return "Tone Slide+Vol Slide"; case 'A': return "Volume Slide";  case 'C': return "Set Volume";
            case 'D': return "Jump to Line";     case 'F': return "Set Speed";         case 'I': return "Ignore (riff)";
            case 'M': return "Multiplier";       case 'R': return "Riff";              case 'T': return "Transpose Riff";
            case 'U': return "Feedback";         case 'V': return "Operator Volume";
            }
        } else if (s->kind == K_OPL && s->hs) {
            switch (L) { case 'E': return "Pitch Bend"; case 'D': return "Aftertouch"; }
        } else if (s->kind == K_OPL && s->a2s) {
            static const char *N[48] = { 0, "Slide Up", "Slide Down", "Tone Porta", "Vibrato", "TPorta+VolSlide", "Vib+VolSlide", "Fine Slide Up", "Fine Slide Down", "Set Mod Volume",
                "Vol Slide", "Pos Jump", "Set Ins Volume", "Pattern Break", "Set Tempo", "Set Speed", "TPorta+FineVolSlide", "Vib+FineVolSlide", "Set Car Volume", "Set Waveform",
                "Fine Vol Slide", "Retrigger", "Tremolo", "Tremor", "Arp+VolSlide", "Arp+FineVolSlide", "Multi Retrig", "Slide Up+VolSlide", "Slide Down+VolSlide",
                "FineSlideUp+VolSlide", "FineSlideDn+VolSlide", "Slide Up+FineVol", "Slide Down+FineVol", "FineSlideUp+FineVol", "FineSlideDn+FineVol", "Extended", "Extended 2",
                "Global Volume", "Swap Arpeggio", "Swap Vibrato", "Force Ins Volume", "Extended 3", "XFine Arpeggio", "XFine Vibrato", "XFine Tremolo", "Custom Speed Table",
                "Global Slide Up", "Global Slide Down" };
            const char *all = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ&%!@=#$~^`><";
            if (L == '0') return "Arpeggio";
            const char *q = strchr(all, L); if (q && q - all > 0 && q - all < 48 && N[q - all]) return N[q - all];
        } else if (s->kind == K_OPL) {
            switch (L) {
            case '0': return "Arpeggio";       case '1': return "Slide Up";        case '2': return "Slide Down";
            case '3': return "Tone Porta";     case '4': return "Vibrato";         case '5': return "TPorta+VolSlide";
            case '6': return "Vib+VolSlide";   case '7': return "Set Tempo";       case '8': return "Key Off";
            case '9': return "Set Op Volume";  case 'A': return "Vol Slide";       case 'B': return "Pos Jump";
            case 'C': return "Set Volume";     case 'D': return "Pattern Break";   case 'E': return "Extended";
            case 'F': return "Set Speed";      case 'H': return "Set Volume";      case 'I': return "Set Speed";
            }
        } else if (s->kind == K_AHX) {
            switch (L) {
            case '0': return "Pos Jump (hi)";   case '1': return "Porta Up";      case '2': return "Porta Down";
            case '3': return "Tone Porta";      case '4': return "Filter";        case '5': return "TPorta+VolSlide";
            case '7': return "Set Pan";         case '9': return "Square Offset"; case 'A': return "Vol Slide";
            case 'B': return "Pos Jump";        case 'C': return "Set Volume";    case 'D': return "Pattern Break";
            case 'F': return "Set Speed";
            case 'E': {
                switch (par >> 4) { case 1: return "Fine Porta Up"; case 2: return "Fine Porta Dn"; case 4: return "Vib Control"; case 0xA: return "Fine Vol Up";
                                    case 0xB: return "Fine Vol Dn"; case 0xC: return "Note Cut"; case 0xF: return "Misc Flags"; }
                return "Extended";
            }
            }
        } else if (s->kind == K_DMF) {
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
        } else if (s->fmt && !strcmp(s->fmt, "669") && L >= '0' && L <= '4') {      /* Composer 669 effects */
            static const char *N669[5] = { "Slide Up", "Slide Down", "Slide to Note", "Freq Adjust", "Vibrato" };
            return N669[L - '0'];
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
    if (s->kind == K_OPL && s->a2s) { SongCell cc = *c; cc.fx[0] = c->vol[0]; cc.fx[1] = c->vol[1]; cc.fx[2] = c->vol[2]; cc.fx[3] = 0; int iv; return song_fx_name(s, &cc, &iv); }
    if (s->kind == K_XM) {
        switch (v) { case '-': case '+': return "VolCol Slide"; case 'd': case 'u': return "VolCol Fine Slide"; case 's': return "VolCol Vib Speed";
                     case 'p': return "VolCol Set Pan"; case 'l': case 'r': return "VolCol Pan Slide"; case 'm': return "VolCol Tone Porta"; }
        return "VolCol";
    }
    switch (v) { case 'a': case 'b': return "VolCol Fine Slide"; case 'c': case 'd': return "VolCol Slide"; case 'e': case 'f': return "VolCol Porta";
                 case 'g': return "VolCol Tone Porta"; case 'h': return "VolCol Vibrato"; case 'p': return "VolCol Set Pan"; }
    return "VolCol";
}

/* ---- Info window ---- */
static const struct { const char *f, *name, *tracker; } FMT_INFO[] = {
    { "MOD", "Amiga module", "" }, { "S3M", "Scream Tracker 3 module", "" }, { "XM", "Extended module", "" }, { "IT", "Impulse Tracker module", "" },
    { "STM", "Scream Tracker 2 module", "Scream Tracker 2" }, { "PTM", "PolyTracker module", "PolyTracker" }, { "ULT", "UltraTracker module", "UltraTracker" },
    { "669", "Composer 669 / UNIS 669", "Composer 669" }, { "MTM", "MultiTracker module", "MultiTracker" }, { "MED", "OctaMED module (MMD0-3)", "OctaMED" },
    { "PSM", "Epic MegaGames PSM", "Protracker Studio (Epic MegaGames)" }, { "FAR", "Farandole Composer module", "Farandole Composer" },
    { "MDL", "DigiTrakker module", "DigiTrakker" }, { "DMF", "X-Tracker module", "X-Tracker" }, { "TFMX", "TFMX song (mdat + smpl)", "TFMX (Chris Huelsbeck)" },
    { "AMF", "AMF module", "DSMI / Asylum Music Format" }, { "SFX", "SoundFX module", "SoundFX" }, { "AHX", "AHX module", "AHX (Abyss' Highest eXperience)" },
    { "DBM", "DigiBooster Pro module", "DigiBooster Pro" }, { "DSM", "DSIK module", "DSIK (Digital Sound Interface Kit)" }, { "DTM", "Digital Tracker module", "Digital Tracker (Atari Falcon030)" },
    { "AMD", "AMUSIC module", "AMUSIC Adlib Tracker" }, { "MAD", "Mlat Adlib Tracker module", "Mlat Adlib Tracker" }, { "A2M", "Adlib Tracker 2 module", "Adlib Tracker 2" },
    { "LDS", "LOUDNESS Sound System song", "LOUDNESS Sound System (Andras Molnar)" }, { "HERAD", "Herbulot AdLib System tune", "HERAD System (Cryo)" }, { "RAD", "Reality ADlib Tracker tune", "Reality ADlib Tracker" }, { NULL, NULL, NULL } };

static void info_add(SongInfo *o, const char *fmt, ...) {
    if (o->nextra >= SONG_INFO_LINES) return;
    va_list ap; va_start(ap, fmt); vsnprintf(o->extra[o->nextra], sizeof o->extra[0], fmt, ap); va_end(ap); o->nextra++;
}
static void info_samples(SongInfo *o, int n8, int n16, int stereo) {
    if (n8 + n16 == 0) return;
    char b[64] = ""; if (n16 && n8) snprintf(b, sizeof b, "%d x 16-bit, %d x 8-bit", n16, n8); else if (n16) snprintf(b, sizeof b, "all 16-bit"); else snprintf(b, sizeof b, "all 8-bit");
    if (stereo) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, ", %d stereo", stereo); }
    info_add(o, "Samples: %d (%s)", n8 + n16, b);
}

void song_info(const Song *s, SongInfo *o) {
    memset(o, 0, sizeof *o);
    snprintf(o->title, sizeof o->title, "%s", s->title);
    if (s->mi) {
        MidiInfo mi; midi_info(s->mi, &mi);
        snprintf(o->format, sizeof o->format, "Standard MIDI file (format %d)", mi.format);
        {   /* what the file is made for, always named in full (the subtitle of the header and the first line of the Info page) */
            const char *nm = !strcmp(mi.standard, "MT-32") ? "MT-32 Multi Timbre" : !strcmp(mi.standard, "GS") ? "Roland Sound Canvas" : !strcmp(mi.standard, "XG") ? "Yamaha XG" : !strcmp(mi.standard, "GM2") ? "General MIDI 2" : "General MIDI";
            snprintf(o->tracker, sizeof o->tracker, "%s", nm);
        }
        o->voices = 16; snprintf(o->voice_unit, sizeof o->voice_unit, "channels");
        if (!strcmp(mi.standard, "MT-32")) info_add(o, "Made for the MT-32: its instruments differ from General MIDI");
        else if (!strcmp(mi.standard, "GS")) info_add(o, "Made for the Sound Canvas (General MIDI with GS extensions)");
        else if (!strcmp(mi.standard, "XG")) info_add(o, "General MIDI with the Yamaha XG extensions");
        else if (!strcmp(mi.standard, "GM2")) info_add(o, "General MIDI 2");
        else info_add(o, mi.standard_known ? "General MIDI" : "No standard named in the file: played as General MIDI");
        if (mi.syx[0]) info_add(o, "Setup file: %s (%d messages), read first", mi.syx, mi.syx_msgs);
        if (mi.lcd[0]) info_add(o, "Display text: \"%s\"", mi.lcd);
        if (mi.copyright[0]) info_add(o, "%s", mi.copyright);
        info_add(o, "%d tracks, %d ticks per quarter note", mi.ntracks, mi.division & 0x8000 ? 0 : mi.division);
        info_add(o, "%d notes, tempo %d bpm at the start", mi.notes, mi.tempo_bpm);
        const char *sf = midi_soundfont_path(); const char *bn = sf ? strrchr(sf, '/') : NULL;
        if (midi_hardware_on()) info_add(o, "Output: %s (MIDI hardware), key H switches to the SoundFont", midi_output_name());
        else info_add(o, "SoundFont: %s%s", sf ? (bn ? bn + 1 : sf) : "none", NG_HWMIDI ? ", key H: MIDI hardware" : "");
        return;
    }
    int k = -1; for (int i = 0; FMT_INFO[i].f; i++) if (!strcmp(FMT_INFO[i].f, s->fmt)) { k = i; break; }
    snprintf(o->format, sizeof o->format, "%s (%s)", k >= 0 ? FMT_INFO[k].name : "Module", s->fmt);
    snprintf(o->tracker, sizeof o->tracker, "%s", s->tracker[0] ? s->tracker : k >= 0 && FMT_INFO[k].tracker[0] ? FMT_INFO[k].tracker : "unknown");
    o->voices = s->nch; snprintf(o->voice_unit, sizeof o->voice_unit, "%s", s->kind == K_OPL ? "FM voices" : "channels");
    if (s->kind == K_OPL) {
        if (s->a2s) {
            info_add(o, "Chip: OPL3 (YMF262), 18 FM channels, 4-operator capable");
            info_add(o, "%d instruments, %d patterns", s->a2s->instrinfo.count, s->a2s->eventsinfo.patterns);
            info_add(o, "Two effects per cell, instrument macros, arpeggio and vibrato tables");
        } else if (s->hs) {
            info_add(o, "File: %s, version %d%s", s->hs->agd ? "AGD (AdLib Gold)" : "SDB / HSQ (AdLib)", s->hs->v2 ? 2 : 1, s->hs->comp == 1 ? ", HSQ packed" : s->hs->comp == 2 ? ", SQX packed" : "");
            if (s->hs->agd) { info_add(o, "Chip: OPL3 (YMF262), 18 voices"); info_add(o, "Instrument panning, 4-operator instruments"); info_add(o, "AdLib Gold: YM7128B surround, TDA8425 stereo"); }
            else info_add(o, "Chip: OPL2 (YM3812), 9 voices");
            int n4 = 0; for (int i = 0; i < s->hs->ninst; i++) if (s->hs->agd && (int8_t)s->hs->inst[i][0] == 4) n4++;
            info_add(o, "%d instruments%s, %d tracks", s->hs->ninst, n4 ? " (some with 4 operators)" : "", s->hs->ntracks);
        } else if (s->ld) {
            info_add(o, "Chip: OPL2 (YM3812), 9 FM voices");
            info_add(o, "%u instruments, %u positions, %d rows per pattern", s->ld->npatch, s->ld->nposi, s->ld->pattlen);
            info_add(o, "Update rate %.1f Hz", lds_refresh(s->ld));
        } else if (s->rd) {
            if (s->rd->use_opl3) info_add(o, "Chip: OPL3 (YMF262), 9 channels, 4-operator instruments");
            else info_add(o, "Chip: OPL2 (YM3812), 9 channels");
            int riffs = 0; for (int i = 0; i < s->rd->ninst; i++) if (s->rd->inst[i].riff) riffs++;
            info_add(o, "%d instruments%s, %d patterns", s->rd->ninst, riffs ? ", with instrument riffs" : "", s->rd->ntracks);
            info_add(o, "Update rate %.1f Hz, initial speed %d", s->rd->hertz, s->rd->speed);
        } else if (s->os) {
            info_add(o, "Chip: OPL2 (YM3812), 9 FM voices");
            info_add(o, "%d instruments, %d patterns, %d orders", s->os->ninst, s->os->nop, s->os->length);
        }
        return;
    }
    if (s->kind == K_IT && s->it) {
        const ITSong *it = s->it; int n8 = 0, n16 = 0, st = 0;
        for (int i = 1; i <= it->nsmp; i++) { if (!it->smp[i].len) continue; if (it->smp[i].is16) n16++; else n8++; if (it->smp[i].data2) st++; }
        info_samples(o, n8, n16, st);
        if (it->ninst > 0) info_add(o, "%d instruments", it->ninst);
        info_add(o, "%d orders, %d patterns", it->nord, it->npat);
        info_add(o, "Speed %d, tempo %d, %s frequency table", it->speed, it->tempo, (it->flags & 8) ? "linear" : "Amiga");
    } else if (s->kind == K_DMF && s->dm) {
        const DMFSong *d = s->dm; int n8 = 0, n16 = 0;
        for (int i = 1; i <= d->nsmp; i++) { if (!d->smp[i].len) continue; if (d->smp[i].is16) n16++; else n8++; }
        info_samples(o, n8, n16, 0);
        info_add(o, "%d orders, %d patterns", d->nord, d->npat);
        if (d->composer[0]) info_add(o, "Composer: %s", d->composer);
    } else if (s->kind == K_TFMX && s->tf) {
        info_add(o, "Paula, 4 voices (Amiga), 8-bit samples");
        info_add(o, "%d patterns, %d macros, %s", s->tf->npat, s->tf->nmac, s->tf->pal ? "PAL" : "NTSC");
        if (s->nsub > 1) info_add(o, "%d subsongs", s->nsub);
    } else if (s->kind == K_ML && s->msong) {
        const MlSong *m = s->msong; int nwave = 0, nsmp = 0;
        for (int i = 1; i <= m->nsmp; i++) { if (m->smp[i].sample_type) nwave++; else nsmp++; }
        info_add(o, "Paula, %d voices%s", m->mixer_mode ? 8 : 4, m->mixer_mode ? " (software mixed)" : " (Amiga)");
        info_add(o, "%d instruments, %d samples, %d waveforms", m->ninst, nsmp, nwave);
        if (m->author[0]) info_add(o, "Author: %s", m->author);
        if (s->nsub > 1) info_add(o, "%d subsongs", s->nsub);
    } else if (s->kind == K_AHX) {
        info_add(o, "Synthesised on the Paula model: 4 voices, no samples");
    } else {                                                  /* MOD, S3M, XM and the formats played through them */
        const XSong *x = &s->xs; int n8 = 0, n16 = 0;
        if (s->kind == K_XM) { for (int i = 0; i < x->nxsmp; i++) { if (!x->xsmp[i].len) continue; if (x->xsmp[i].is16) n16++; else n8++; } }
        else for (int i = 1; i < 100; i++) { if (!x->smp[i].len) continue; if (x->smp[i].is16) n16++; else n8++; }
        info_samples(o, n8, n16, 0);
        if (s->kind == K_XM && x->ninst > 0) info_add(o, "%d instruments, %s frequency table", x->ninst, x->linear ? "linear" : "Amiga");
        info_add(o, "%d orders, %d patterns", x->nord, x->npat);
        info_add(o, "Speed %d, tempo %d", x->speed, x->tempo);
        if (x->amiga) info_add(o, "Amiga module: Paula low-pass filter");
        if (x->echo) info_add(o, "Echo DSP in use");
    }
}

/* ---- MIDI ---- */
int song_is_midi(const Song *s) { return s && s->kind == K_MIDI; }
int song_is_fm_s3m(const Song *s) { return s && s->kind == K_S3M && s->xs.has_adlib; }      /* an S3M with AdLib (OPL2) instruments: made for an FM card */
/* FM voices of a song: how many channels play FM (OPL) and how many play digital samples. Only the tracker formats that can hold both (S3M for now) have FM voices next to samples; the pure FM
 * formats (K_OPL) are not counted here. Returns the FM channel count. */
int song_fm_voices(const Song *s, int *digital_ch) {
    XFmInfo fi; memset(&fi, 0, sizeof fi);
    if (s && s->kind == K_S3M) xsong_fm_info(&s->xs, &fi);
    if (digital_ch) *digital_ch = fi.smp_channels;
    return fi.fm_channels;
}
/* 1 = FM in a format that can also hold samples (S3M with AdLib instruments, whether or not samples are used): the case that gets the AWE32 card */
int song_fm_in_sample_format(const Song *s) { return s && s->kind == K_S3M && s->xs.has_adlib && song_fm_voices(s, NULL) > 0; }
void song_midi_chan(const Song *s, int ch, SongMidiChan *o) {
    memset(o, 0, sizeof *o);
    if (!s || !s->mi || ch < 0 || ch > 15) return;
    o->level = midi_chan_level(s->mi, ch); o->out = midi_chan_out(s->mi, ch); o->program = midi_chan_program(s->mi, ch); o->bank = midi_chan_bank(s->mi, ch);
    o->drum = midi_chan_is_drum(s->mi, ch); o->volume = midi_chan_volume(s->mi, ch); o->pan = midi_chan_pan(s->mi, ch); o->used = midi_chan_notes(s->mi, ch) > 0;
    { MidiChanFx fx; midi_chan_fx(s->mi, ch, &fx); o->expr = fx.expr; o->mod = fx.mod; o->reverb = fx.reverb; o->chorus = fx.chorus; o->sustain = fx.sustain; o->bend = fx.bend; }
}
int song_midi_notes(const Song *s, double t0, double t1, SongMidiNote *out, int max) {
    if (!s || !s->mi) return 0;
    return midi_notes_in(s->mi, t0, t1, (MidiNote *)out, max);
}
double song_midi_grid_sec(const Song *s, double g) {
    if (!s || !s->mi) return 0;
    return midi_sec_at_tick(s->mi, g * midi_division(s->mi) / 4.0);
}
double song_midi_length(const Song *s) { return s && s->mi ? midi_length(s->mi) : 0; }
int song_midi_seek_order(Song *s, int order, double *sec) {
    if (!s || !s->mi) return 0;
    if (order < 0) order = 0;
    if (order >= s->nord) order = s->nord - 1;
    double t = midi_sec_at_tick(s->mi, (double)order * midi_division(s->mi) * 4.0);
    midi_seek(s->mi, t); if (sec) *sec = t;
    return 1;
}
int song_midi_seek_by(Song *s, double delta, double *sec) {
    if (!s || !s->mi) return 0;
    double t = midi_position(s->mi) + delta;
    if (t >= midi_length(s->mi)) return 2;
    if (t < 0) t = 0;
    midi_seek(s->mi, t); if (sec) *sec = t;
    return 1;
}
const char *song_midi_program_name(int program, int drum) { return midi_gm_name(program, drum); }
int song_midi_mt32_reverb(const Song *s, int out[3]) {
    MidiInfo mi; if (!s || !s->mi) return 0;
    midi_info(s->mi, &mi); for (int k = 0; k < 3; k++) out[k] = mi.mt_reverb[k];
    return out[0] >= 0 || out[1] >= 0 || out[2] >= 0;
}
const char *song_midi_standard(const Song *s, int *known) {
    static char b[12]; MidiInfo mi; b[0] = 0;
    if (known) *known = 0;
    if (!s || !s->mi) return b;
    midi_info(s->mi, &mi); snprintf(b, sizeof b, "%s", mi.standard); if (known) *known = mi.standard_known;
    return b;
}
const char *song_midi_lcd(const Song *s) {                              /* the display text of the MT-32 that the file sends in a SysEx message (at most 20 characters), "" if it has none */
    static char b[24]; MidiInfo mi; b[0] = 0;
    if (!s || !s->mi) return b;
    midi_info(s->mi, &mi); snprintf(b, sizeof b, "%s", mi.lcd);
    return b;
}
void song_midi_idle(Song *s) { if (s && s->mi) midi_idle(s->mi); }

