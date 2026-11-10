/* TFMX-Loader und Dateinamen-Logik. GPL-3.0-or-later */
#include "tfmx.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <stdio.h>

static uint32_t be32(const uint8_t *d, size_t n, size_t o) { return o + 4 <= n ? (uint32_t)d[o] << 24 | d[o+1] << 16 | d[o+2] << 8 | d[o+3] : 0; }
static uint32_t be16(const uint8_t *d, size_t n, size_t o) { return o + 2 <= n ? (uint32_t)d[o] << 8 | d[o+1] : 0; }

/* Fruehe TFMX-Songs (Replayer v1) lassen sich nicht am Header erkennen; hier eine Liste per CRC32 der Songdatei
 * (selbst ermittelt: diese Songs klingen nur mit den v1-Effekten richtig). Der alte Header "TFMX " zaehlt ebenfalls. */
static uint32_t crc32_of(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static int tfmx_is_v1(const uint8_t *m, size_t nm) {         /* Bit 0: v1, Bit 1: Turrican-Variante, Bit 2: Noten ohne Feinstimmung (Danger Freak) */
    static const struct { uint32_t crc; int fl; } V1[] = {
        { 0xF352D151, 3 }, { 0x20A44A0F, 3 }, { 0x591C32EA, 3 }, { 0xBEC86482, 3 },                       /* Turrican */
        { 0xBB4AB77F, 3 }, { 0xF15B7260, 3 }, { 0xA72B075C, 3 }, { 0x2E5E2457, 3 },
        { 0xABDA96DF, 1 }, { 0x80167F23, 1 },                                                              /* Hard'n'Heavy */
        { 0x674380D2, 1 }, { 0xAAE2DB6E, 1 },                                                              /* Grand Monster Slam */
        { 0xA706D505, 5 }, { 0x00740C49, 5 }, { 0xD339888B, 5 },                                           /* Danger Freak */
        { 0x746E6FAE, 1 }, { 0x3056F774, 1 } };                                                            /* X-Out */
    if (nm >= 5 && !memcmp(m, "TFMX", 4) && m[4] == 0x20) return 1;
    uint32_t c = crc32_of(m, nm);
    for (size_t i = 0; i < sizeof V1 / sizeof V1[0]; i++) if (V1[i].crc == c) return V1[i].fl;
    return 0;
}

TfmxSong *tfmx_load(const uint8_t *m, size_t nm, const uint8_t *sm, size_t ns, int *err) {
    int e = 0; if (!err) err = &e;
    *err = 0;
    if (nm < 0x200 || (memcmp(m, "TFMX-SONG", 9) && memcmp(m, "TFMX_SONG", 9) && memcmp(m, "TFMX-MOD", 8))) { *err = -1; return NULL; }
    TfmxSong *s = calloc(1, sizeof *s);
    if (!s) { *err = -3; return NULL; }
    s->mdat = malloc(nm); s->smpl = malloc(ns ? ns : 1);
    if (!s->mdat || !s->smpl) { tfmx_free(s); *err = -3; return NULL; }
    memcpy(s->mdat, m, nm); s->nmdat = nm;
    if (ns) memcpy(s->smpl, sm, ns);
    s->nsmpl = ns;
    for (int l = 0; l < 6; l++) {
        memcpy(s->text[l], m + 0x10 + 40 * l, 40); s->text[l][40] = 0;
        for (int i = 39; i >= 0 && (s->text[l][i] == ' ' || s->text[l][i] == 0); i--) s->text[l][i] = 0;
        for (int i = 0; s->text[l][i]; i++) if ((unsigned char)s->text[l][i] < 32) s->text[l][i] = ' ';
    }
    for (int i = 0; i < 32; i++) {
        s->sub[i].start = (uint16_t)be16(m, nm, 0x100 + 2 * i);
        s->sub[i].end   = (uint16_t)be16(m, nm, 0x140 + 2 * i);
        s->sub[i].tempo = (uint16_t)be16(m, nm, 0x180 + 2 * i);
    }
    s->ts = be32(m, nm, 0x1D0); s->pt = be32(m, nm, 0x1D4); s->mt = be32(m, nm, 0x1D8);
    int explicit_tab = (s->ts || s->pt || s->mt);
    if (!explicit_tab) { s->ts = 0x800; s->pt = 0x400; s->mt = 0x600; s->npat = 128; s->nmac = 128; }
    else {
        if (!s->ts) s->ts = 0x800;
        if (!s->pt) s->pt = 0x400;
        if (!s->mt) s->mt = 0x600;
        s->npat = s->mt > s->pt ? (int)((s->mt - s->pt) / 4) : 128;
        if (s->npat > 128) s->npat = 128;
        s->nmac = 128;
    }
    if (s->ts >= nm || s->pt >= nm || s->mt >= nm) { tfmx_free(s); *err = -2; return NULL; }
    if (explicit_tab) {                      /* Makroanzahl: bis Dateiende bzw. max. 128 */
        size_t room = (nm - s->mt) / 4; s->nmac = room > 128 ? 128 : (int)room;
    }
    s->ntrk = (int)(((s->pt < s->ts ? nm : s->pt) - s->ts) / 16);
    if (s->ntrk > 0x2000) s->ntrk = 0x2000;
    s->pal = (m[10] << 8 | m[11]) & 2 ? 1 : 0;
    s->v1 = tfmx_is_v1(m, nm);
    return s;
}

void tfmx_free(TfmxSong *s) { if (!s) return; free(s->mdat); free(s->smpl); free(s); }

int tfmx_subsongs(const TfmxSong *s, int *idx, int max) {
    int n = 0;
    for (int i = 0; i < 32; i++) {
        const TfmxSub *b = &s->sub[i];
        if (b->end < b->start || b->start >= s->ntrk || b->end >= s->ntrk) continue;
        if (b->start == b->end && i >= 8) continue;      /* Padding-Eintraege am Tabellenende (z.B. 511-511) */
        int music = 0;                                   /* mindestens eine Zeile mit Stimmen (weder EFFE-Kommando noch nur FF) */
        for (int r = b->start; r <= b->end && !music; r++) {
            size_t o = s->ts + 16u * (size_t)r;
            if (o + 16 > s->nmdat || (s->mdat[o] == 0xEF && s->mdat[o + 1] == 0xFE)) continue;
            for (int v = 0; v < 8; v++) if (s->mdat[o + 2 * v] != 0xFF) music = 1;
        }
        if (!music) continue;
        int dup = 0;                                     /* Duplikat oder Ueberlappung mit einem schon akzeptierten Subsong */
        for (int k = 0; k < n; k++) {
            const TfmxSub *a = &s->sub[idx[k]];
            if (b->start <= a->end && a->start <= b->end) dup = 1;
        }
        if (!dup && n < max) idx[n++] = i;
    }
    if (n == 0 && max > 0) idx[n++] = 0;
    return n;
}
int tfmx_subsong_count(const TfmxSong *s) { int idx[32]; return tfmx_subsongs(s, idx, 32); }

/* ---- Dateinamen ---- */
static const char *base_of(const char *p) { const char *b = strrchr(p, '/'); return b ? b + 1 : p; }
static int starts_ci(const char *s, const char *pre) { return strncasecmp(s, pre, strlen(pre)) == 0; }
static int ends_ci(const char *s, const char *suf) { size_t a = strlen(s), b = strlen(suf); return a > b && strcasecmp(s + a - b, suf) == 0; }

int tfmx_is_song_name(const char *path) { const char *n = base_of(path); return starts_ci(n, "mdat.") || ends_ci(n, ".tfx"); }
int tfmx_is_sample_name(const char *path) { const char *n = base_of(path); return starts_ci(n, "smpl.") || ends_ci(n, ".sam"); }

int tfmx_partner(const char *path, char *out, size_t cap) {
    const char *n = base_of(path);
    char want[512]; size_t dl = (size_t)(n - path);
    if (starts_ci(n, "mdat.")) snprintf(want, sizeof want, "smpl.%s", n + 5);
    else if (ends_ci(n, ".tfx")) snprintf(want, sizeof want, "%.*s.sam", (int)(strlen(n) - 4), n);
    else return -1;
    char dir[1024]; if (dl >= sizeof dir) return -1;
    memcpy(dir, path, dl); dir[dl] = 0;
    DIR *d = opendir(dl ? dir : ".");
    int found = 1, nsam = 0;
    char only[512] = "";
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strcasecmp(de->d_name, want) == 0) { snprintf(out, cap, "%s%s", dir, de->d_name); found = 0; break; }
            if (tfmx_is_sample_name(de->d_name) && nsam++ == 0) snprintf(only, sizeof only, "%s", de->d_name);
        }
        closedir(d);
    }
    /* Kein exakter Partner: liegt genau eine Sampledatei im Ordner (z. B. ein gemeinsames Set.sam
     * fuer alle Songs eines Spiels), gehoert sie zu diesem Song. */
    if (found && nsam == 1) { snprintf(out, cap, "%s%s", dir, only); found = 0; }
    return found;
}
