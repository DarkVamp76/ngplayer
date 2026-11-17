/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ngPlayer - OpenGL-3.3-Oberflaeche. Renderer-Schicht (gfx.inc) stammt aus ngSid.
 *
 *   ngplayer [--screenshot out.bmp [--frames N]] [datei.mod|s3m|stm|ptm|ult|669|mtm|med|psm|far|xm|it | ordner ...]
 *
 * Tasten: Leertaste Pause, Links/Rechts Subsong (TFMX) bzw. Order spulen, Strg+Links/Rechts Order spulen, Hoch/Runter Titel, +/- Lautstaerke, R Neustart,
 *         F/F11 Vollbild, Esc Ende. Maus: Playlist anklicken, Lautstaerkebalken ziehen.
 *         Dateien/Ordner per Drag & Drop auf das Fenster.
 */
#define _DEFAULT_SOURCE
#include "font_data.h"
#include "song.h"
#include "tfmx.h"
#include "net.h"
#include "version.h"

#include <SDL2/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "gfx.inc"
#include "card_data.h"
static GLuint card_tex[2];

/* ================================================================== Palette */
static const col BG0 = { 0.035f, 0.043f, 0.078f, 1 };
static const col BG1 = { 0.070f, 0.090f, 0.150f, 1 };
static const col CARD = { 0.082f, 0.102f, 0.168f, 1 };
static const col CARDHI = { 0.105f, 0.130f, 0.208f, 1 };
static const col EDGE = { 0.22f, 0.27f, 0.42f, 1 };
static const col TXT = { 0.92f, 0.94f, 1.0f, 1 };
static const col DIM = { 0.52f, 0.58f, 0.74f, 1 };
static const col FAINT = { 0.30f, 0.35f, 0.50f, 1 };
static const col VC[3] = { { 0.24f, 0.88f, 1.0f, 1 }, { 1.0f, 0.36f, 0.80f, 1 }, { 1.0f, 0.76f, 0.28f, 1 } };
static const col OKC = { 0.35f, 0.95f, 0.55f, 1 };
static const col WARN = { 1.0f, 0.45f, 0.35f, 1 };

/* ================================================================== Audio / Zustand */
#define RING 8192
static int g_rate = 44100;
static SDL_AudioDeviceID g_dev;
static Song *g_song;
static int g_paused, g_ended;
static float g_vol = 0.8f;
static volatile unsigned long long g_frames;
static float ringL[RING], ringR[RING];
static volatile unsigned ringpos;
static float peakL, peakR;
static char g_error[160];

/* Zeilenwechsel-Ereignisse: der Audio-Thread meldet jede neue Zeile mit der Sample-Position, an der sie beginnt */
typedef struct { int order, pattern, row, nrows; unsigned long long frame; } RowEv;
#define EVN 1024
static RowEv evq[EVN];
static unsigned ev_w;                         /* Schreibindex (Audio-Thread) */
static unsigned long long g_rendered;         /* insgesamt gerenderte Frames */
static int g_lastord = -1, g_lastrow = -1;
static unsigned g_cb_seq;                     /* zaehlt Callbacks */
static unsigned long long g_cb_rendered;      /* g_rendered am Ende des letzten Callbacks */
static Uint64 g_cb_time;                      /* Zeitstempel des letzten Callbacks */
static int g_resync = 1;

static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    int16_t *o = (int16_t *)stream;
    int n = len / 4;
    if (!g_song || g_paused || g_ended) { memset(stream, 0, (size_t)len); g_resync = 1; return; }
    int done = 0;
    while (done < n) {
        int k = n - done < 32 ? n - done : 32;
        int got = song_render(g_song, o + done * 2, k);
        if (got < 0) got = 0;
        for (int i = 0; i < got; i++) {
            int16_t *p = o + (done + i) * 2;
            float l = p[0] * g_vol, r = p[1] * g_vol;
            p[0] = (int16_t)l; p[1] = (int16_t)r;
            float fl = l / 32768.0f, fr = r / 32768.0f;
            ringL[ringpos % RING] = fl; ringR[ringpos % RING] = fr; ringpos++;
            if (fabsf(fl) > peakL) peakL = fabsf(fl);
            if (fabsf(fr) > peakR) peakR = fabsf(fr);
        }
        SongPos sp; song_pos(g_song, &sp);
        if (sp.order != g_lastord || sp.row != g_lastrow) {
            g_lastord = sp.order; g_lastrow = sp.row;
            RowEv *e = &evq[ev_w % EVN];
            e->order = sp.order; e->pattern = sp.pattern; e->row = sp.row; e->nrows = sp.nrows; e->frame = g_rendered;
            __sync_synchronize();
            ev_w++;
        }
        g_rendered += (unsigned long long)got;
        g_frames += (unsigned)got;
        done += got;
        if (got < k) { memset(o + done * 2, 0, (size_t)(n - done) * 4); g_ended = 1; break; }
    }
    g_cb_rendered = g_rendered;
    g_cb_time = SDL_GetPerformanceCounter();
    g_cb_seq++;
}

/* ================================================================== Playlist */
typedef struct { char *path; char *key; char label[128]; int ma_id; int fmt; } entry;   /* fmt: -1 not yet determined, 0 unknown, else FMT_* */   /* key: Sortierschluessel (Ordner + Anzeigename) */
#ifdef __EMSCRIPTEN__
static void ma_need_download(int idx);        /* Mod Archive list of the web build, see below */
static void ma_sync_web(int force);
#endif
static entry *pl;
static int npl, cur = -1;
static float list_scroll;
static int g_mobile;                  /* Touch-Geraet (Web): schlanker Modus mit weniger Ueberblend-Effekten */
static int g_vmode;                   /* 0 = Pattern, 1 = Orb, 2 = Voxel, 3 = Scopes (key V / buttons); the tab order is VIEW_ORDER */
static const int VIEW_ORDER[4] = { 0, 3, 1, 2 };   /* PATTERN, SCOPES, ORB, VOXEL */
static int g_portrait, g_plopen;      /* Hochformat (Handy): gestapeltes Layout, Playlist als Overlay */
static int list_follow;
static int g_pltab;                      /* playlist tab: 0 = FILES (local), 1 = MOD ARCHIVE (online) */
static int *pl_vis, nvis, cvis;        /* visible entries of the active tab, position of the playing song in it */

static int is_mod_name(const char *n) {
    if (tfmx_is_song_name(n)) return 1;          /* mdat.NAME / NAME.tfx; smpl.* und .sam sind nur Sampledaten */
    const char *d = strrchr(n, '.');
    if (!d) return 0;
    static const char *ext[] = { ".mod", ".s3m", ".stm", ".ptm", ".ult", ".669", ".mtm", ".med", ".psm", ".far", ".mdl", ".mmd0", ".mmd1", ".mmd2", ".mmd3", ".xm", ".it", ".dmf", ".amf", ".dsm", ".sfx", ".ahx", ".dbm", NULL };
    for (int i = 0; ext[i]; i++) if (!strcasecmp(d, ext[i])) return 1;
    return 0;
}

/* ---- File format badge in front of each list entry (colour per format) ---- */
enum { FMT_NONE, FMT_MOD, FMT_S3M, FMT_XM, FMT_IT, FMT_STM, FMT_PTM, FMT_ULT, FMT_669, FMT_MTM, FMT_MED, FMT_PSM, FMT_FAR, FMT_MDL, FMT_DMF, FMT_TFMX, FMT_AMF, FMT_SFX, FMT_AHX, FMT_DBM, FMT_DSM, FMT_COUNT };
static const char *const FMT_NAME[FMT_COUNT] = { "", "MOD", "S3M", "XM", "IT", "STM", "PTM", "ULT", "669", "MTM", "MED", "PSM", "FAR", "MDL", "DMF", "TFMX", "AMF", "SFX", "AHX", "DBM", "DSM" };
static const col FMT_COL[FMT_COUNT] = {
    { 0.5f, 0.5f, 0.5f, 1 }, { 1.00f, 0.76f, 0.28f, 1 }, { 0.35f, 0.95f, 0.55f, 1 }, { 0.24f, 0.88f, 1.00f, 1 }, { 1.00f, 0.36f, 0.80f, 1 },
    { 0.30f, 0.85f, 0.75f, 1 }, { 0.65f, 0.50f, 1.00f, 1 }, { 1.00f, 0.55f, 0.25f, 1 }, { 1.00f, 0.42f, 0.38f, 1 }, { 0.75f, 0.95f, 0.30f, 1 },
    { 0.40f, 0.60f, 1.00f, 1 }, { 0.90f, 0.45f, 1.00f, 1 }, { 0.85f, 0.75f, 0.55f, 1 }, { 0.55f, 1.00f, 0.85f, 1 }, { 1.00f, 0.60f, 0.65f, 1 }, { 0.85f, 0.88f, 1.00f, 1 }, { 0.95f, 0.62f, 0.50f, 1 }, { 0.80f, 0.78f, 0.45f, 1 }, { 0.55f, 0.90f, 0.30f, 1 }, { 0.40f, 0.78f, 0.95f, 1 }, { 0.95f, 0.85f, 0.35f, 1 } };
static int fmt_from_name(const char *fn) {
    if (tfmx_is_song_name(fn)) return FMT_TFMX;
    const char *d = strrchr(fn, '.');
    if (!d) return FMT_NONE;
    static const struct { const char *ext; int f; } map[] = { {".mod", FMT_MOD}, {".s3m", FMT_S3M}, {".xm", FMT_XM}, {".it", FMT_IT}, {".stm", FMT_STM}, {".ptm", FMT_PTM},
        {".ult", FMT_ULT}, {".669", FMT_669}, {".mtm", FMT_MTM}, {".med", FMT_MED}, {".mmd0", FMT_MED}, {".mmd1", FMT_MED}, {".mmd2", FMT_MED}, {".mmd3", FMT_MED},
        {".psm", FMT_PSM}, {".far", FMT_FAR}, {".mdl", FMT_MDL}, {".dmf", FMT_DMF}, {".amf", FMT_AMF}, {".sfx", FMT_SFX}, {".ahx", FMT_AHX}, {".dbm", FMT_DBM}, {".dsm", FMT_DSM}, { NULL, 0 } };
    for (int i = 0; map[i].ext; i++) if (!strcasecmp(d, map[i].ext)) return map[i].f;
    return FMT_NONE;
}
/* Mod Archive downloads are all cached as <id>.mod, so look at the file contents */
static int fmt_sniff(const char *path) {
    unsigned char b[1100]; size_t n = 0; FILE *f = fopen(path, "rb");
    if (f) { n = fread(b, 1, sizeof b, f); fclose(f); }
    if (n < 4) return FMT_NONE;
    if (n >= 128 && (!memcmp(b + 60, "SONG", 4) || !memcmp(b + 124, "SO31", 4))) return FMT_SFX;   /* SoundFX */
    if (n >= 16 && !memcmp(b, "DBM0", 4)) return FMT_DBM;
    if (n >= 16 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "DSMF", 4)) return FMT_DSM;   /* DSIK DSM */
    if (n >= 14 && !memcmp(b, "THX", 3) && b[3] <= 1) return FMT_AHX;   /* AHX */
    if (!memcmp(b, "IMPM", 4)) return FMT_IT;
    if (n >= 17 && !memcmp(b, "Extended Module: ", 17)) return FMT_XM;
    if (n > 0x30 && !memcmp(b + 0x2C, "SCRM", 4)) return FMT_S3M;
    if (n > 0x30 && !memcmp(b + 0x2C, "PTMF", 4)) return FMT_PTM;
    if (!memcmp(b, "MMD", 3) && b[3] >= '0' && b[3] <= '3') return FMT_MED;
    if (!memcmp(b, "MTM", 3)) return FMT_MTM;
    if (!memcmp(b, "DDMF", 4)) return FMT_DMF;
    if (!memcmp(b, "DMDL", 4)) return FMT_MDL;
    if (!memcmp(b, "FAR\xFE", 4)) return FMT_FAR;
    if (n >= 24 && !memcmp(b, "ASYLUM Music Format V1.0", 24)) return FMT_AMF;      /* Crusader */
    if (!memcmp(b, "AMF", 3) && b[3] >= 8 && b[3] <= 14) return FMT_AMF;           /* DSMI */
    if (!memcmp(b, "PSM\xFE", 4) || !memcmp(b, "PSM ", 4)) return FMT_PSM;
    if (!memcmp(b, "MAS_UTrack_V00", 14)) return FMT_ULT;
    if ((b[0] == 'i' && b[1] == 'f') || (b[0] == 'J' && b[1] == 'N')) return FMT_669;
    if (n > 0x30 && !memcmp(b + 0x14, "!Scream!", 8)) return FMT_STM;
    if (n > 0x30 && !memcmp(b + 0x14, "BMOD2STM", 8)) return FMT_STM;
    if (n >= 1084) { const unsigned char *m = b + 1080; if (!memcmp(m, "M.K.", 4) || !memcmp(m, "M!K!", 4) || !memcmp(m, "FLT", 3) || !memcmp(m, "CD81", 4) || !memcmp(m, "OKTA", 4) || (m[1] == 'C' && m[2] == 'H' && m[3] == 'N') || (m[2] == 'C' && m[3] == 'H')) return FMT_MOD; }
    return FMT_NONE;
}
static int entry_fmt(entry *e) {
    if (e->fmt < 0) { const char *b = strrchr(e->path, '/'); e->fmt = e->ma_id > 0 ? fmt_sniff(e->path) : fmt_from_name(b ? b + 1 : e->path); if (e->ma_id > 0 && e->fmt == FMT_NONE) e->fmt = fmt_from_name(b ? b + 1 : e->path); }
    return e->fmt;
}
/* Draws the badge at (x, y = top) and returns the width to advance (the same for every format so the names line up) */
#define FMT_BADGE_W 36.0f
static float draw_fmt_badge(float x, float y, entry *e, int isc) {
    int f = entry_fmt(e);
    if (f > 0 && f < FMT_COUNT) {
        float tw = text_width(FMT_NAME[f], 10.5f);
        rrect(x, y, FMT_BADGE_W, 17, 4, alpha(FMT_COL[f], isc ? 0.32f : 0.20f));
        text(x + (FMT_BADGE_W - tw) / 2, y + 2.5f, 10.5f, FMT_COL[f], FMT_NAME[f]);
    }
    return FMT_BADGE_W + 7.0f;
}

static void add_file(const char *path) {
    pl = (entry *)realloc(pl, sizeof(entry) * (size_t)(npl + 1));
    pl[npl].path = strdup(path);
    pl[npl].ma_id = 0;                           /* >0: stammt vom The Mod Archive (Modul-ID) */
    pl[npl].fmt = -1;
    const char *b = strrchr(path, '/');
    const char *fn = b ? b + 1 : path;
    if (tfmx_is_song_name(fn)) {                 /* TFMX: Namensschema mdat.X / X.tfx ist kein sinnvoller Anzeigename */
        char nm[128];
        size_t n = strlen(fn);
        if (!strncasecmp(fn, "mdat.", 5)) snprintf(nm, sizeof nm, "%s", fn + 5);
        else { snprintf(nm, sizeof nm, "%s", fn); if (n > 4) nm[n - 4] = 0; }
        snprintf(pl[npl].label, sizeof pl[npl].label, "%s TFMX", nm);
    } else snprintf(pl[npl].label, sizeof pl[npl].label, "%s", fn);
    {   /* Sortierschluessel: Ordner + Anzeigename, damit TFMX-Songs nach ihrem Namen (nicht nach 'mdat.') einsortiert werden */
        size_t dl = b ? (size_t)(b + 1 - path) : 0;
        size_t kl = dl + strlen(pl[npl].label) + 1;
        pl[npl].key = (char *)malloc(kl);
        if (pl[npl].key) { memcpy(pl[npl].key, path, dl); strcpy(pl[npl].key + dl, pl[npl].label); }
    }
    npl++;
}

static int cmp_entry(const void *a, const void *b) {
    const entry *x = (const entry *)a, *y = (const entry *)b;
    return strcasecmp(x->key ? x->key : x->path, y->key ? y->key : y->path);
}

static void add_path(const char *path, int depth) {
    struct stat st;
    if (stat(path, &st)) return;
    if (S_ISDIR(st.st_mode)) {
        if (depth > 4) return;
        DIR *d = opendir(path);
        if (!d) return;
        int first = npl;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            char sub[1024];
            snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
            add_path(sub, depth + 1);
        }
        closedir(d);
        qsort(pl + first, (size_t)(npl - first), sizeof(entry), cmp_entry);
    } else if (is_mod_name(path)) {
        add_file(path);
    }
}

static void unload(void) {
    if (g_dev) SDL_LockAudioDevice(g_dev);
    Song *old = g_song;
    g_song = NULL;
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
    song_close(old);
}

static int ov_reset, view_reset = 1;
static float pat_disp;
static int pat_last_order = -1, pat_last_nrows = 64, pat_ch0;

#define SCOPE_LEN 2048
static float g_scope[64 * SCOPE_LEN];

static int g_open_sub;   /* gewaehlter Subsong (TFMX), bleibt bei R/Spulen erhalten */

/* List label: Mod Archive titles are cut to 20 characters + "..." and always get a separate "#ID" tag (drawn after the title,
 * so fit_text can never cut it off), so the module can always be found again. tag stays empty for other entries. */
static void list_label(char *dst, size_t cap, char *tag, size_t tcap, const entry *e) {
    snprintf(dst, cap, "%s", e->label); tag[0] = 0;
    if (e->ma_id <= 0) return;
    char id[24]; snprintf(id, sizeof id, "#%d", e->ma_id);
    { size_t b = 0; while (dst[b] == ' ' || dst[b] == '\t') b++; if (!dst[b]) snprintf(dst, cap, "ModArchive"); }   /* empty title */
    size_t n = strlen(dst), il = strlen(id);
    snprintf(tag, tcap, " %s", id);
    if (n >= il && !strcmp(dst + n - il, id)) {              /* no title found: label is "ModArchive #172325" -> ID flush right as well */
        n -= il; while (n > 0 && dst[n - 1] == ' ') n--;
        dst[n] = 0; return;
    }
    int chars = 0; size_t cut = 0;
    for (size_t i = 0; i < n; i++) { if (((unsigned char)dst[i] & 0xC0) != 0x80) { if (chars == 20) { cut = i; break; } chars++; } }
    if (!cut) return;
    while (cut > 0 && dst[cut - 1] == ' ') cut--;
    snprintf(dst + cut, cap - cut, "...");
}
#ifdef __EMSCRIPTEN__
static void probe_start(int idx);
#endif
static void play_index_sub(int idx, int sub) {
    if (idx < 0 || idx >= npl) return;
#ifdef __EMSCRIPTEN__
    /* Web: Mod Archive entries restored from the saved list have no file yet (the virtual file system is empty after a reload): fetch it first */
    if (pl[idx].ma_id > 0 && access(pl[idx].path, R_OK)) { ma_need_download(idx); return; }
#endif
    char err[128] = "";
    Song *s = song_open_sub(pl[idx].path, g_rate, sub, err, sizeof err);
    unload();
    cur = idx;
#ifdef __EMSCRIPTEN__
    /* Adresszeile = teilbarer Link: #<ID> bei Mod-Archive-Titeln, sonst ohne Hash (replaceState loest kein hashchange aus) */
    EM_ASM({ try { history.replaceState(null, '', $0 ? '#' + $0 : location.pathname + location.search); } catch (e) {} }, pl[idx].ma_id);
#endif
    song_set_mute(0);
    list_follow = 1;
    g_pltab = pl[idx].ma_id > 0;
    g_frames = 0; g_ended = 0; g_paused = 0;
    pat_last_order = -1; pat_disp = 0; pat_ch0 = 0;
    ov_reset = 1;
    ringpos = 0; memset(ringL, 0, sizeof ringL); memset(ringR, 0, sizeof ringR);
    if (!s) { snprintf(g_error, sizeof g_error, "%s: %s", pl[idx].label, err); return; }
    g_error[0] = 0;
    memset(g_scope, 0, sizeof g_scope);
    song_scope_enable(s, g_scope, SCOPE_LEN);
    if (g_dev) SDL_LockAudioDevice(g_dev);
    g_song = s;
    g_rendered = 0; g_lastord = g_lastrow = -1; g_resync = 1;
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
    if (g_portrait && g_mobile && song_channels(s) > 8) g_vmode = 1;      /* phone, portrait, more than 8 voices: Orb view (scopes are too heavy) */
    view_reset = 1;
    g_open_sub = s ? song_subsong(s) : 0;
#ifdef __EMSCRIPTEN__
    probe_start(s ? idx : -1);        /* length of the song for the lock screen */
#endif
}
static void play_index(int idx) { play_index_sub(idx, 0); }

/* ================================================================== Analyse (FFT, Spektrum, Pegel) */
#define NBANDS 64
#define FFTN 2048
static float band[NBANDS], bandpk[NBANDS], bandhold[NBANDS];
static float vu[2], vupk[2], vuhold[2];
static float g_beat;    /* 1 bei erkanntem Beat, klingt schnell ab: treibt den Puls der Soundchip-Grafik */
static float scopeL[RING], scopeR[RING];   /* Momentaufnahme fuer die Anzeige */

static void fft(double *re, double *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * M_PI / len, wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}

static void analyse(float dt) {
    static double re[FFTN], im[FFTN], win[FFTN];
    static int init;
    if (!init) { for (int i = 0; i < FFTN; i++) win[i] = 0.5 - 0.5 * cos(2 * M_PI * i / (FFTN - 1.0)); init = 1; }
    float pl_, pr_;
    unsigned pos;
    if (g_dev) SDL_LockAudioDevice(g_dev);
    memcpy(scopeL, ringL, sizeof ringL); memcpy(scopeR, ringR, sizeof ringR);
    pos = ringpos; pl_ = peakL; pr_ = peakR; peakL = peakR = 0;
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
    /* Ringpuffer linearisieren: aelteste Probe zuerst */
    static float lin[RING][2];
    for (int i = 0; i < RING; i++) { unsigned k = (pos + (unsigned)i) % RING; lin[i][0] = scopeL[k]; lin[i][1] = scopeR[k]; }
    for (int i = 0; i < RING; i++) { scopeL[i] = lin[i][0]; scopeR[i] = lin[i][1]; }
    for (int i = 0; i < FFTN; i++) { int k = RING - FFTN + i; re[i] = 0.5 * (scopeL[k] + scopeR[k]) * win[i]; im[i] = 0; }
    fft(re, im, FFTN);
    float mag[FFTN / 2 + 1];
    for (int i = 0; i <= FFTN / 2; i++) mag[i] = (float)(sqrt(re[i % FFTN] * re[i % FFTN] + im[i % FFTN] * im[i % FFTN]) / (FFTN / 4.0));
    double fmin = 40.0, fmax = 17000.0, binhz = (double)g_rate / FFTN;
    for (int b = 0; b < NBANDS; b++) {
        double f0 = fmin * pow(fmax / fmin, (double)b / NBANDS), f1 = fmin * pow(fmax / fmin, (double)(b + 1) / NBANDS);
        double b0 = f0 / binhz, b1 = f1 / binhz;
        float m = 0;
        if (b1 - b0 < 1.0) {
            double c = (b0 + b1) / 2; int i0 = (int)c; double fr = c - i0;
            m = (float)(mag[i0] * (1 - fr) + mag[i0 + 1] * fr);
        } else {
            for (int i = (int)ceil(b0); i <= (int)b1 && i <= FFTN / 2; i++) if (mag[i] > m) m = mag[i];
        }
        float db = 20.0f * log10f(m + 1e-7f);
        float val = (db + 66.0f) / 66.0f + (float)b / NBANDS * 0.10f;
        if (val < 0) val = 0;
        if (val > 1) val = 1;
        if (val > band[b]) band[b] += (val - band[b]) * 0.65f;
        else { band[b] -= dt * 1.3f; if (band[b] < val) band[b] = val; }
        if (band[b] >= bandpk[b]) { bandpk[b] = band[b]; bandhold[b] = 0.45f; }
        else if (bandhold[b] > 0) bandhold[b] -= dt;
        else { bandpk[b] -= dt * 0.55f; if (bandpk[b] < band[b]) bandpk[b] = band[b]; }
    }
    {   /* Beat: Bass oder Hi-Hat/Rauschen springt ueber den eigenen gleitenden Mittelwert (Schlaege) */
        static float slow[2], last[2], since = 1;
        float e[2] = { 0, 0 };
        int lo1 = (int)(220.0 / binhz) + 1, h0 = (int)(3500.0 / binhz), h1 = (int)(12000.0 / binhz);
        for (int i = 1; i <= lo1; i++) e[0] += mag[i] * mag[i];
        for (int i = h0; i <= h1 && i <= FFTN / 2; i++) e[1] += mag[i] * mag[i];
        e[0] = sqrtf(e[0]); e[1] = sqrtf(e[1]);
        since += dt;
        int hitb = 0;
        for (int k = 0; k < 2; k++) {
            if (e[k] > 1.5f * slow[k] + 0.006f && e[k] > 1.10f * last[k]) hitb = 1;
            slow[k] += (e[k] - slow[k]) * (1.0f - expf(-dt / 0.40f));
            last[k] = e[k];
        }
        if (hitb && since > 0.11f) { g_beat = 1; since = 0; }
        g_beat *= expf(-dt * 7.0f);
    }
    float pk[2] = { pl_, pr_ };
    for (int c = 0; c < 2; c++) {
        float db = 20.0f * log10f(pk[c] + 1e-6f), val = (db + 48.0f) / 48.0f;
        if (val < 0) val = 0;
        if (val > 1) val = 1;
        if (val > vu[c]) vu[c] = val; else { vu[c] -= dt * 1.6f; if (vu[c] < val) vu[c] = val; }
        if (vu[c] >= vupk[c]) { vupk[c] = vu[c]; vuhold[c] = 0.8f; }
        else if (vuhold[c] > 0) vuhold[c] -= dt;
        else { vupk[c] -= dt * 0.5f; if (vupk[c] < vu[c]) vupk[c] = vu[c]; }
    }
}

/* ================================================================== UI-Helfer */
#define MOB_BTN_W 34.0f                                           /* width of the per-row menu button (touch devices) */
static float mx, my;
static int click, clickx, clicky, mouse_down, click_ctrl;
static int rclick, rclickx, rclicky;                       /* right mouse button */
static int lp_on, lp_fired, lp_swallow, lp_x, lp_y;        /* long press on a touch screen = right click */
static Uint32 lp_t;
static int ctx_idx = -1; static float ctx_x, ctx_y;        /* context menu ("Share") of a Mod Archive entry in the list */
static float toast_t; static char toast_txt[48];
#define CTX_W 120.0f
#define CTX_ROW 30.0f
static int ctx_n(int idx) { return idx >= 0 && idx < npl && pl[idx].ma_id ? 3 : 1; }      /* MA entries: Share, Info, Remove; local files: Remove */
#define CTX_H(idx) (ctx_n(idx) * CTX_ROW + 2.0f)
static int hit(float x, float y, float w, float h) { return mx >= x && mx < x + w && my >= y && my < y + h; }
static int clicked(float x, float y, float w, float h) { return click && clickx >= x && clickx < x + w && clicky >= y && clicky < y + h; }

static float chip(float x, float y, const char *label, col c, int lit) {
    float w = text_width(label, 12) + 14;
    if (lit) { rrect(x, y, w, 20, 6, alpha(c, 0.22f)); text(x + 7, y + 3, 12, c, label); }
    else { rrect(x, y, w, 20, 6, alpha(FAINT, 0.18f)); text(x + 7, y + 3, 12, alpha(FAINT, 0.9f), label); }
    return w + 5;
}
static void card(float x, float y, float w, float h, int hover) {
    shadow(x, y + 6, w, h, 14, 22, C(0, 0, 0, 0.5f));
    rrect(x, y, w, h, 14, alpha(EDGE, hover ? 0.75f : 0.45f));
    rrect(x + 1, y + 1, w - 2, h - 2, 13, hover ? CARDHI : CARD);
}
static void mmss(char *buf, size_t cap, double sec) { int s = (int)sec; snprintf(buf, cap, "%d:%02d", s / 60, s % 60); }

static float glow_l, glow_r;
static double g_dt;
static void draw_pattern(float x, float y, float w, float h);
static void draw_voxel(float x, float y, float w, float h);
/* Umschalter Pattern | Orb oben in der Karte (auch per Touch/Klick) */
static void view_switch(float x, float y, float w) {
    float cw = 62, bx = g_portrait ? x + w - 14 - 4 * cw + 4 : x + w / 2 - 2.0f * cw, by = y + 8;
    static const char *const NAME[4] = { "PATTERN", "ORB", "VOXEL", "SCOPES" };
    for (int t = 0; t < 4; t++) {
        int i = VIEW_ORDER[t];
        float xx = bx + t * cw;
        int on = (i == g_vmode), hv = hit(xx, by, cw - 4, 20);
        rrect(xx, by, cw - 4, 20, 6, on ? alpha(VC[2], 0.25f) : alpha(FAINT, hv ? 0.30f : 0.16f));
        text(xx + (cw - 4 - text_width(NAME[i], 11)) / 2, by + 4, 11, on ? VC[2] : alpha(DIM, 0.9f), NAME[i]);
        if (clicked(xx, by, cw - 4, 20)) { g_vmode = i; click = 0; }
    }
}

/* ---------------------------------------------------------------- Abspiel-Position fuer die Anzeige
 * Der Audio-Thread meldet jeden Zeilenwechsel mit der Sample-Position (evq). Hier wird daraus eine zeit-
 * kontinuierliche Scroll-Position (Zeile + Bruchteil) gebildet, synchron zum Gehoerten:
 *   gehoert(t) = t * rate + clk_off - Latenz,  clk_off wird langsam an die Callback-Zeitstempel gekoppelt. */
static SongPos g_view;           /* aktuelle Zeile (zum Gehoerten synchron) */
static int g_view_valid;
static float g_pos;              /* Zeile + Bruchteil, relativ zur Order von g_view */
static RowEv pq[EVN]; static unsigned pq_r, pq_w;
static unsigned ev_r;
static unsigned long long view_cur_frame;
static double view_interval;
static double clk_off; static int clk_init, was_running;
static unsigned last_cb_seq;
static float g_lag_ms = 46.0f;
static int g_view_snap;          /* 1 = letzter Ereigniswechsel war ein Sprung */

static void view_update(void) {
    double freq = (double)SDL_GetPerformanceFrequency();
    if (view_reset) {
        if (g_dev) SDL_LockAudioDevice(g_dev);
        ev_r = ev_w; last_cb_seq = g_cb_seq;
        if (g_dev) SDL_UnlockAudioDevice(g_dev);
        pq_r = pq_w = 0; g_view_valid = 0; clk_init = 0; was_running = 0; g_pos = 0;
        view_interval = g_rate * 0.12; view_cur_frame = 0; view_reset = 0;
    }
    if (!g_song) return;
    int running = !(g_paused || g_ended);
    if (running && !was_running) clk_init = 0;
    was_running = running;
    unsigned seq; unsigned long long cbr; Uint64 cbt; unsigned w;
    if (g_dev) SDL_LockAudioDevice(g_dev);
    w = ev_w; seq = g_cb_seq; cbr = g_cb_rendered; cbt = g_cb_time;
    if (w - ev_r > EVN) ev_r = w - 1;
    for (; ev_r != w; ev_r++) pq[pq_w++ % EVN] = evq[ev_r % EVN];
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
    if (pq_w - pq_r > EVN) pq_r = pq_w - 1;

    double lagf = g_lag_ms * g_rate / 1000.0;
    if (seq != last_cb_seq && running) {
        last_cb_seq = seq;
        double sample = (double)cbr - lagf - ((double)cbt / freq) * g_rate;
        if (!clk_init) { clk_off = sample; clk_init = 1; } else clk_off += (sample - clk_off) * 0.03;
    }
    if (!clk_init) return;
    double heard = ((double)SDL_GetPerformanceCounter() / freq) * g_rate + clk_off;
    if (!running) { /* Pause/Ende: Position einfrieren */ return; }
    while (pq_r != pq_w && (double)pq[pq_r % EVN].frame <= heard) {
        RowEv e = pq[pq_r % EVN]; pq_r++;
        if (g_view_valid) {
            int normal = (e.order == g_view.order && e.row == g_view.row + 1) ||
                         (e.order == g_view.order + 1 && e.row == 0 && g_view.row == g_view.nrows - 1);
            if (normal) { double iv = (double)(e.frame - view_cur_frame); if (iv > g_rate * 0.004 && iv < g_rate * 4.0) view_interval = iv; }
            g_view_snap = !normal;
        } else g_view_snap = 1;
        g_view.order = e.order; g_view.pattern = e.pattern; g_view.row = e.row; g_view.nrows = e.nrows;
        view_cur_frame = e.frame; g_view_valid = 1;
    }
    if (!g_view_valid) return;
    double frac = (heard - (double)view_cur_frame) / view_interval;
    if (frac < 0) frac = 0;
    if (frac > 0.999) frac = 0.999;
    float target = (float)g_view.row + (float)frac;
    static int last_ord = -1; static float last_pos;
    /* nie rueckwaerts laufen, solange wir in derselben Order und innerhalb einer Zeile bleiben */
    if (g_view.order == last_ord && !g_view_snap && last_pos - target > 0 && last_pos - target < 0.6f) target = last_pos;
    g_view_snap = 0;
    last_ord = g_view.order; last_pos = target;
    g_pos = target;
}

/* ---------------------------------------------------------------- Pattern-Ansicht */
static const col FXC[6] = { { 0.30f, 0.35f, 0.50f, 1 }, { 1.0f, 0.76f, 0.28f, 1 }, { 0.24f, 0.88f, 1.0f, 1 },
                            { 1.0f, 0.36f, 0.80f, 1 }, { 1.0f, 0.45f, 0.35f, 1 }, { 0.35f, 0.95f, 0.55f, 1 } };
static int pat_visch = 8;         /* zuletzt berechnete Zahl sichtbarer Kanaele */
static int g_decimal;             /* Zeilenzaehler dezimal (S3M/IT) statt hex */

/* virtuelle Zeile relativ zur aktuellen Order -> Pattern/Zeile (mit Vor-/Nachbar-Pattern) */
static int resolve_row(const SongPos *sp, int vr, int *pat, int *row, int *other) {
    *other = 0;
    if (vr >= 0 && vr < sp->nrows) { *pat = sp->pattern; *row = vr; return 1; }
    if (vr >= sp->nrows) {
        int p = song_order_pattern(g_song, sp->order + 1);
        if (p < 0) return 0;
        int n = song_pattern_rows(g_song, p), r = vr - sp->nrows;
        if (r >= n) return 0;
        *pat = p; *row = r; *other = 1; return 1;
    }
    int p = song_order_pattern(g_song, sp->order - 1);
    if (p < 0) return 0;
    int n = song_pattern_rows(g_song, p), r = n + vr;
    if (r < 0) return 0;
    *pat = p; *row = r; *other = 1; return 1;
}

/* Scope einer Stimme: aufsteigender Nulldurchgang als Trigger; Zeichenflaeche bx..bx+bw, Mittellinie mid, Hoehe sh */
static void chan_scope(int c, float bx2, float mid2, float bw2, float sh2, float lw) {
    unsigned wp = song_scope_pos(g_song);
    const float *sb = g_scope + (size_t)c * SCOPE_LEN;
    enum { SPAN = 360, NP = 96 };
    int start = SPAN;
    float peak = 0;
    for (int k = 1; k <= SPAN + 700; k++) { float v = sb[(wp - (unsigned)k) & (SCOPE_LEN - 1)]; if (fabsf(v) > peak) peak = fabsf(v); }
    for (int k = SPAN + 1; k < SPAN + 700; k++) {
        float a = sb[(wp - (unsigned)k) & (SCOPE_LEN - 1)], b = sb[(wp - (unsigned)k + 1) & (SCOPE_LEN - 1)];
        if (a < 0 && b >= 0) { start = k; break; }
    }
    static float sgain[64];
    float want = peak > 0.004f ? 0.85f / peak : 1.0f;
    if (want > 10) want = 10;
    if (want < 1) want = 1;
    if (sgain[c] < 1) sgain[c] = 1;
    sgain[c] += (want - sgain[c]) * (want < sgain[c] ? 0.5f : 0.08f);
    float amp = sh2 * 0.46f;
    int np = bw2 < 40 ? 32 : NP;
    static float pts[NP * 2];
    for (int j = 0; j < np; j++) {
        float v = sb[(wp - (unsigned)start + (unsigned)((long)j * SPAN / np)) & (SCOPE_LEN - 1)] * sgain[c];
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        pts[2 * j] = bx2 + bw2 * j / (np - 1.0f);
        pts[2 * j + 1] = mid2 - v * amp;
    }
    col lc = VC[c % 3];
    float lvl = peak > 0.004f ? 1.0f : 0.35f;
    rect(bx2, mid2, bw2, 1, alpha(FAINT, 0.25f));
    set_additive(1);
    polyline(pts, np, 3.4f * lw, 3.5f, alpha(lc, 0.20f * lvl));
    set_additive(0);
    polyline(pts, np, 1.0f * lw, 3.0f, alpha(mix(lc, C(1, 1, 1, 1), 0.3f), 0.35f + 0.65f * lvl));
}

/* One voice tile: scope, live panning, number, mute (click = mute, Ctrl+click = solo) */
static void chan_tile(int c, float bx0, float by0, float bwd, float bht) {
    unsigned long long mm = song_get_mute(), all = song_channels(g_song) >= 64 ? ~0ULL : ((1ULL << song_channels(g_song)) - 1);
    rrect(bx0, by0, bwd, bht, 7, C(0.02f, 0.03f, 0.055f, 1));
    float sh2 = bht - 24, mid2 = by0 + 5 + sh2 / 2;
    chan_scope(c, bx0 + 6, mid2, bwd - 12, sh2, 1.0f);
    /* Panning in Echtzeit, links -> rechts */
    {
        static float pan_disp[64];
        float pw = bwd - 12, px0 = bx0 + 6, py = by0 + bht - 13;
        float pt = (float)song_chan_pan(g_song, c);
        pan_disp[c] += (pt - pan_disp[c]) * 0.35f;
        float pn = pan_disp[c] / 255.0f, mxp = px0 + pw * pn, midx = px0 + pw * 0.5f;
        col lc2 = VC[c % 3];
        rrect(px0, py + 3, pw, 4, 2, alpha(FAINT, 0.30f));
        rect(midx - 0.5f, py, 1, 10, alpha(FAINT, 0.7f));
        float fx0 = mxp < midx ? mxp : midx, fw = fabsf(mxp - midx);
        if (fw > 1) rrect(fx0, py + 3, fw, 4, 2, alpha(lc2, 0.55f));
        set_additive(1);
        blob(mxp, py + 5, 9, alpha(lc2, 0.30f));
        set_additive(0);
        rrect(mxp - 4, py + 1, 8, 8, 4, mix(lc2, C(1, 1, 1, 1), 0.35f));
    }
    char nb[16]; snprintf(nb, sizeof nb, "%02d", c + 1);
    text(bx0 + 6, by0 + 2, 10, alpha(FAINT, 1.0f), nb);
    if (clicked(bx0, by0, bwd, bht)) {
        if (click_ctrl) mm = (mm == (all & ~(1ULL << c))) ? 0 : (all & ~(1ULL << c));
        else mm ^= 1ULL << c;
        song_set_mute(mm);
    }
    if ((mm >> c) & 1) {
        rrect(bx0, by0, bwd, bht, 7, C(0.02f, 0.03f, 0.055f, 0.72f));
        text(bx0 + bwd - text_width("MUTE", 10) - 6, by0 + 2, 10, WARN, "MUTE");
    }
}

/* SCOPES view: one tile per voice in a grid that fills the card */
static void draw_scopes(float x, float y, float w, float h) {
    card(x, y, w, h, 0);
    if (!g_portrait) text(x + 18, y + 12, 12, DIM, "SCOPES");
    view_switch(x, y, w);
    if (!g_song) { text(x + 18, y + 44, 14, FAINT, "No song loaded."); return; }
    int nch = song_channels(g_song);
    if (nch > 64) nch = 64;
    float ax = x + 14, ay = y + 40, aw = w - 28, ah = h - 40 - 12;
    if (aw < 60 || ah < 40 || nch < 1) return;
    int bestc = 1; float bestd = 1e9f;
    for (int cols = 1; cols <= nch; cols++) {            /* the column count that gives tiles closest to 2.6 : 1 */
        int rows = (nch + cols - 1) / cols;
        float tw = aw / cols, th = ah / rows, d = fabsf(logf((tw / th) / 2.6f));
        if (th < 34) d += 5;
        if (d < bestd) { bestd = d; bestc = cols; }
    }
    int cols = bestc, rows = (nch + cols - 1) / cols;
    float gap = 6, tw = (aw - gap * (cols - 1)) / cols, th = (ah - gap * (rows - 1)) / rows;
    for (int c = 0; c < nch; c++) chan_tile(c, ax + (c % cols) * (tw + gap), ay + (c / cols) * (th + gap), tw, th);
}


static void draw_pattern(float x, float y, float w, float h) {
    card(x, y, w, h, 0);
    if (!g_portrait) text(x + 18, y + 12, 12, DIM, "PATTERN");
    view_switch(x, y, w);
    if (!g_song) { text(x + 18, y + 44, 14, FAINT, "No song loaded."); return; }
    if (!g_view_valid) return;
    SongPos sp = g_view;
    int nch = song_channels(g_song);
    char hb[64];
    snprintf(hb, sizeof hb, "ORD %02d  PAT %02d  ROW %02d/%02d", sp.order + 1, sp.pattern, sp.row, sp.nrows);
    if (g_portrait) text(x + 18, y + 12, 12, DIM, hb);   /* Platz: Info links, Umschalter rechts */
    else text_right(x + w - 18, y + 12, 12, DIM, hb);

    pat_disp = g_pos;

    int wide = nch <= 6;
    float fs = wide ? 14.0f : (nch <= 8 ? 12.0f : 11.0f), rowh = wide ? 19.0f : (nch <= 8 ? 17.0f : 16.0f);
    float wn = text_width("C#4", fs), wi = text_width("00", fs), wv = text_width("v00", fs), wf = text_width("000", fs) + text_width("A", fs);
    float gap = fs * (nch > 8 ? 0.38f : 0.55f);
    float rn_w = 34;
    float ax0 = x + 14 + rn_w, avail = w - 28 - rn_w;
    /* Detailstufe: 0 = Note Inst Vol Fx, 1 = ohne Vol, 2 = nur Note Inst. Wird eng, faellt erst Vol, dann Fx weg,
     * damit moeglichst alle Stimmen in die Spalten passen. */
    float cw = 0;
    int vis = 1, lvl = 0, tryl;
    for (tryl = 0; tryl < 3; tryl++) {
        cw = wn + wi + (tryl < 1 ? wv : 0) + (tryl < 2 ? wf : 0) + gap * (4 - tryl) + 6;
        if ((int)(avail / cw) >= nch) break;
    }
    lvl = tryl < 3 ? tryl : 2;      /* passt nichts komplett: kompakteste Stufe, damit moeglichst viele Stimmen sichtbar sind */
    cw = wn + wi + (lvl < 1 ? wv : 0) + (lvl < 2 ? wf : 0) + gap * (4 - lvl) + 6;
    vis = (int)(avail / cw);
    if (vis < 1) vis = 1;
    if (vis > nch) vis = nch;
    pat_visch = vis;
    if (pat_ch0 > nch - vis) pat_ch0 = nch - vis;
    if (pat_ch0 < 0) pat_ch0 = 0;
    float used = cw * vis, ox = ax0 + (avail - used) / 2;

    /* Kanalkoepfe (Scope + Panning je Stimme). Passen nicht alle Stimmen in die Pattern-Spalten, bekommen ALLE Stimmen
     * ein Kopf-Raster mit je max. 16 pro Zeile (oben), das Pattern darunter scrollt mit Nummernleiste. */
    float hy = y + 34, hh = 0;                 /* the PATTERN view shows the notes only; the scopes have their own tab (SCOPES) */
    int strip = nch > vis;
    float labh = 16;
    {   /* number bar of the visible columns: click = mute, Ctrl+click = solo */
        for (int i = 0; i < vis; i++) {
            char nb[16]; snprintf(nb, sizeof nb, "%02d", pat_ch0 + i + 1);
            float lx = ox + i * cw + 2;
            int c2 = pat_ch0 + i, muted = (int)((song_get_mute() >> c2) & 1);
            rrect(lx, hy + hh + 6, cw - 4, 14, 5, alpha(muted ? WARN : VC[0], muted ? 0.22f : 0.10f));
            text(lx + 6, hy + hh + 8, 10, muted ? WARN : alpha(DIM, 1.0f), nb);
            if (clicked(lx, hy + hh + 6, cw - 4, 14)) {
                unsigned long long mm = song_get_mute(), all = nch >= 64 ? ~0ULL : ((1ULL << nch) - 1);
                if (click_ctrl) mm = (mm == (all & ~(1ULL << c2))) ? 0 : (all & ~(1ULL << c2));
                else mm ^= 1ULL << c2;
                song_set_mute(mm);
            }
        }
    }
    (void)strip;
    /* Zeilenbereich */
    float ry0 = hy + hh + 8 + labh, ry1 = y + h - 10;
    int nrows_vis = (int)((ry1 - ry0) / rowh);
    float mid = (ry0 + ry1) / 2 - rowh / 2;
    /* Playhead */
    rrect(x + 10, mid - 1, w - 20, rowh + 2, 7, alpha(VC[0], 0.14f));
    rrect(x + 10, mid + 3, 3, rowh - 6, 1.5f, VC[0]);
    set_additive(1);
    blob(x + w / 2, mid + rowh / 2, 150, alpha(VC[0], 0.05f));
    set_additive(0);
    int half = nrows_vis / 2 + 2;
    int base = (int)floorf(pat_disp);
    float frac = pat_disp - (float)base;
    for (int k = -half; k <= half; k++) {
        int vr = base + k;
        float yy = mid + ((float)k - frac) * rowh;
        if (yy < ry0 - 2 || yy + rowh > ry1 + 2) continue;
        int pat, row, other;
        if (!resolve_row(&sp, vr, &pat, &row, &other)) continue;
        float dist = fabsf((float)k - frac);
        float fade = 1.0f - dist / (float)(half + 1) * 0.40f;
        if (other) fade *= 0.65f;
        int cur_row = (vr == sp.row);
        char rb[16];
        if (g_decimal) snprintf(rb, sizeof rb, "%02d", row); else snprintf(rb, sizeof rb, "%02X", row);
        col rc = cur_row ? TXT : (row % 4 == 0 ? DIM : FAINT);
        text(x + 18, yy + 2, fs, alpha(rc, fade), rb);
        if (row % 4 == 0) rect(ox, yy + rowh - 1, used, 1, alpha(FAINT, 0.10f * fade));
        for (int i = 0; i < vis; i++) {
            int c = pat_ch0 + i;
            SongCell cell; song_cell(g_song, pat, row, c, &cell);
            float cx = ox + i * cw + 4;
            col nc = cell.has_note ? (cell.note[0] == '=' || cell.note[0] == '^' || cell.note[0] == '~' ? WARN : TXT) : FAINT;
            if (!cell.has_note) nc = alpha(FAINT, 0.75f);
            text(cx, yy + 2, fs, alpha(cell.has_note ? mix(nc, C(1, 1, 1, 1), cur_row ? 0.3f : 0) : nc, fade), cell.note);
            cx += wn + gap;
            int hi = cell.inst[0] != '.';
            text(cx, yy + 2, fs, alpha(hi ? mix(DIM, VC[0], 0.5f) : alpha(FAINT, 0.75f), fade), cell.inst);
            cx += wi + gap;
            if (lvl < 1) {
                int hv = cell.vol[0] != '.';
                text(cx, yy + 2, fs, alpha(hv ? FXC[cell.volclass < 6 ? cell.volclass : 5] : alpha(FAINT, 0.75f), fade), cell.vol);
                cx += wv + gap;
            }
            if (lvl < 2) {
                int hf = cell.fx[0] != '.';
                text(cx, yy + 2, fs, alpha(hf ? FXC[cell.fxclass < 6 ? cell.fxclass : 5] : alpha(FAINT, 0.75f), fade), cell.fx);
            }
        }
    }
    /* Kanal-Scrollleiste, wenn nicht alle Kanaele passen */
    if (vis < nch) {
        float sx = x + 14 + rn_w, sw = w - 28 - rn_w, sy2 = y + h - 7;
        rrect(sx, sy2, sw, 3, 1.5f, alpha(FAINT, 0.25f));
        rrect(sx + sw * pat_ch0 / nch, sy2, sw * vis / nch, 3, 1.5f, alpha(VC[0], 0.7f));
    }
}

static void draw_spectrum(float x, float y, float w, float h) {
    card(x, y, w, h, 0);
    text(x + 18, y + 10, 12, DIM, "SPECTRUM");
    float bx = x + 18, bw = w - 36, by0 = y + 30, avail = h - 30 - 20, bh = avail * 0.80f, base = by0 + bh;
    for (int g = 0; g <= 3; g++) rect(bx, by0 + bh * g / 3.0f, bw, 1, alpha(FAINT, 0.20f));
    float step = bw / NBANDS, barw = step * 0.68f, refl = avail * 0.20f;
    for (int b = 0; b < NBANDS; b++) {
        float xx = bx + b * step + (step - barw) / 2, hh2 = band[b] * bh, u = (float)b / (NBANDS - 1);
        col lo = u < 0.5f ? mix(VC[0], VC[1], u * 2) : mix(VC[1], VC[2], (u - 0.5f) * 2);
        col top = mix(lo, C(1, 1, 1, 1), 0.35f);
        if (hh2 > 1) {
            rect_grad(xx, base - hh2, barw, hh2, top, alpha(lo, 0.35f));
            float rh2 = hh2 * 0.35f < refl ? hh2 * 0.35f : refl;
            rect_grad(xx, base + 2, barw, rh2, alpha(lo, 0.22f), alpha(lo, 0.0f));
        }
        if (bandpk[b] > 0.02f) rect(xx, base - bandpk[b] * bh - 3, barw, 2, alpha(top, 0.95f));
    }
    if (!g_mobile) set_additive(1);
    for (int b = 0; !g_mobile && b < NBANDS; b += 2) {
        float u = (float)b / (NBANDS - 1);
        col lo = u < 0.5f ? mix(VC[0], VC[1], u * 2) : mix(VC[1], VC[2], (u - 0.5f) * 2);
        blob(bx + (b + 0.5f) * step, base - band[b] * bh, 12 + band[b] * 8, alpha(lo, 0.10f * band[b]));
    }
    set_additive(0);
    static const struct { double f; const char *l; } AX[] = { { 100, "100" }, { 1000, "1k" }, { 10000, "10k" } };
    for (int i = 0; i < 3; i++) {
        float xx = bx + bw * (float)(log(AX[i].f / 40.0) / log(17000.0 / 40.0));
        text(xx - text_width(AX[i].l, 11) / 2, y + h - 18, 11, alpha(DIM, 0.8f), AX[i].l);
    }
}

/* ================================================================== Orb: Geodaetennetz, vom Spektrum verformt
 * Ikosaeder, zweimal unterteilt (162 Knoten, 480 Kanten, 320 Dreiecke), leicht verrauscht. Knoten und Dreiecke werden vom
 * Spektrum nach aussen gedrueckt und leuchten auf; Aequator = Bass, Pole = Hoehen. */
#define OB_V 162
#define OB_F 320
#define OB_E 480
static float ob_u[OB_V][3];
static int ob_f[OB_F][3], ob_e[OB_E][2], ob_nv, ob_nf, ob_ne;
static float orb_bs[NBANDS];            /* geglaettetes Spektrum */

static int ob_mid(int a, int b) {       /* Mittelpunkt zweier Knoten (mit Nachschlagen, damit Knoten nicht doppelt entstehen) */
    float m[3];
    for (int k = 0; k < 3; k++) m[k] = 0.5f * (ob_u[a][k] + ob_u[b][k]);
    float l = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    for (int k = 0; k < 3; k++) m[k] /= l;
    for (int i = 0; i < ob_nv; i++)
        if (fabsf(ob_u[i][0] - m[0]) + fabsf(ob_u[i][1] - m[1]) + fabsf(ob_u[i][2] - m[2]) < 1e-4f) return i;
    for (int k = 0; k < 3; k++) ob_u[ob_nv][k] = m[k];
    return ob_nv++;
}
static void ob_init(void) {
    const float g = 1.61803399f;
    static const float iv[12][3] = { {-1, 1, 0}, {1, 1, 0}, {-1, -1, 0}, {1, -1, 0}, {0, -1, 1}, {0, 1, 1}, {0, -1, -1}, {0, 1, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, -1}, {-1, 0, 1} };
    static const int itri[20][3] = { {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
                                     {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1} };
    (void)g;
    ob_nv = 12; ob_nf = 20;
    for (int i = 0; i < 12; i++) {
        float l = sqrtf(iv[i][0] * iv[i][0] + iv[i][1] * iv[i][1] + iv[i][2] * iv[i][2]);
        for (int k = 0; k < 3; k++) ob_u[i][k] = iv[i][k] / l;
    }
    for (int i = 0; i < 20; i++) for (int k = 0; k < 3; k++) ob_f[i][k] = itri[i][k];
    for (int pass = 0; pass < 2; pass++) {
        static int nf2[OB_F][3];
        int n2 = 0;
        for (int i = 0; i < ob_nf; i++) {
            int a = ob_f[i][0], b = ob_f[i][1], c = ob_f[i][2];
            int ab = ob_mid(a, b), bc = ob_mid(b, c), ca = ob_mid(c, a);
            int t[4][3] = { {a, ab, ca}, {b, bc, ab}, {c, ca, bc}, {ab, bc, ca} };
            for (int k = 0; k < 4; k++) { nf2[n2][0] = t[k][0]; nf2[n2][1] = t[k][1]; nf2[n2][2] = t[k][2]; n2++; }
        }
        for (int i = 0; i < n2; i++) for (int k = 0; k < 3; k++) ob_f[i][k] = nf2[i][k];
        ob_nf = n2;
    }
    unsigned seed = 12345u;                              /* leichtes Rauschen: wirkt organischer als das glatte Raster */
    for (int i = 0; i < ob_nv; i++) {
        for (int k = 0; k < 3; k++) { seed = seed * 1664525u + 1013904223u; ob_u[i][k] += ((float)(seed >> 8) / 16777216.0f - 0.5f) * 0.10f; }
        float l = sqrtf(ob_u[i][0] * ob_u[i][0] + ob_u[i][1] * ob_u[i][1] + ob_u[i][2] * ob_u[i][2]);
        for (int k = 0; k < 3; k++) ob_u[i][k] /= l;
    }
    ob_ne = 0;                                           /* Kanten: jede Seite eines Dreiecks einmal */
    for (int i = 0; i < ob_nf; i++)
        for (int k = 0; k < 3; k++) {
            int a = ob_f[i][k], b = ob_f[i][(k + 1) % 3], dup = 0;
            if (a > b) { int tmp = a; a = b; b = tmp; }
            for (int j = 0; j < ob_ne; j++) if (ob_e[j][0] == a && ob_e[j][1] == b) { dup = 1; break; }
            if (!dup && ob_ne < OB_E) { ob_e[ob_ne][0] = a; ob_e[ob_ne][1] = b; ob_ne++; }
        }
}

static void draw_orb(float x, float y, float w, float h) {
    static int init;
    static float t, spin;
    if (!init) { ob_init(); init = 1; }
    float dt = (float)g_dt;
    if (dt > 0.1f) dt = 0.1f;
    float beat = g_paused ? 0.0f : g_beat;
    if (!g_paused) { t += dt; spin += dt * (0.16f + 0.35f * beat); }
    card(x, y, w, h, 0);
    text(x + 18, y + 10, 12, DIM, "ORB");
    for (int b = 0; b < NBANDS; b++) {      /* Nachbarbaender mischen: weichere Wellen */
        float l = band[b > 0 ? b - 1 : b], r = band[b < NBANDS - 1 ? b + 1 : b];
        float v = 0.5f * band[b] + 0.25f * (l + r);
        orb_bs[b] += (v - orb_bs[b]) * (v > orb_bs[b] ? 0.7f : 0.25f);
    }
    float cx = x + w / 2, cy = y + h / 2 + 6, R = 0.34f * (w < h ? w : h);
    float ca = cosf(spin), sa = sinf(spin), ct = cosf(0.42f), st = sinf(0.42f), fz = 8.0f * R;
    static float sx[OB_V], sy[OB_V], dn[OB_V], ev[OB_V];
    for (int i = 0; i < ob_nv; i++) {
        float ux = ob_u[i][0], uy = ob_u[i][1], uz = ob_u[i][2];
        float lat = fabsf(uy), bf = lat * (NBANDS - 1);      /* Aequator = Bass, Pole = Hoehen */
        int b0 = (int)bf;
        if (b0 > NBANDS - 2) b0 = NBANDS - 2;
        float fr = bf - b0, lon = atan2f(uz, ux);
        float e = (orb_bs[b0] * (1 - fr) + orb_bs[b0 + 1] * fr) * (0.65f + 0.35f * sinf(lon * 3.0f + t * 1.3f + lat * 5.0f));
        if (e > 1) e = 1;
        ev[i] = e;
        float rad = R * (1.0f + 0.06f * beat + 0.36f * e);
        float px = ux * rad, py = uy * rad, pz = uz * rad;
        float rx = px * ca + pz * sa, rz = -px * sa + pz * ca;
        float ry = py * ct - rz * st, rz2 = py * st + rz * ct;
        float sc = fz / (fz + rz2);
        sx[i] = cx + rx * sc; sy[i] = cy + ry * sc;
        float d = 0.5f - 0.5f * rz2 / (1.5f * R);             /* 1 = vorne, 0 = hinten */
        dn[i] = d < 0 ? 0 : (d > 1 ? 1 : d);
    }
    set_additive(1);
    blob(cx, cy, R * (0.60f + 0.12f * beat), alpha(VC[0], 0.05f + 0.08f * beat));
    for (int f = 0; f < ob_nf; f++) {                        /* halbtransparente Flaechen, je nach Energie der drei Knoten */
        int a = ob_f[f][0], b = ob_f[f][1], c = ob_f[f][2];
        float e = (ev[a] + ev[b] + ev[c]) * (1.0f / 3.0f), d = (dn[a] + dn[b] + dn[c]) * (1.0f / 3.0f);
        float al = (0.012f + 0.30f * e * e) * (0.25f + 0.75f * d);
        if (al < 0.01f) continue;
        col col0 = e < 0.5f ? mix(VC[0], VC[1], e * 2.0f) : mix(VC[1], VC[2], (e - 0.5f) * 2.0f);
        tri(sx[a], sy[a], sx[b], sy[b], sx[c], sy[c], alpha(mix(col0, C(1, 1, 1, 1), 0.25f), al));
    }
    for (int k = 0; k < ob_ne; k++) {                        /* Kanten */
        int a = ob_e[k][0], b = ob_e[k][1];
        float e = 0.5f * (ev[a] + ev[b]), d = 0.5f * (dn[a] + dn[b]);
        col c = e < 0.5f ? mix(VC[0], VC[1], e * 2.0f) : mix(VC[1], VC[2], (e - 0.5f) * 2.0f);
        c = mix(c, C(1, 1, 1, 1), 0.45f);
        float xy[4] = { sx[a], sy[a], sx[b], sy[b] };
        polyline(xy, 2, 0.55f + 0.5f * d, 3.0f, alpha(c, (0.10f + 0.50f * d) * (0.55f + 0.9f * e)));
    }
    for (int i = 0; i < ob_nv; i++) {                        /* Knoten: heller Kern plus Leuchten */
        float e = ev[i], d = dn[i];
        col c = e < 0.5f ? mix(VC[0], VC[1], e * 2.0f) : mix(VC[1], VC[2], (e - 0.5f) * 2.0f);
        c = mix(c, C(1, 1, 1, 1), 0.55f);
        float rr = 0.9f + 1.5f * d + 1.6f * e;
        rrect(sx[i] - rr, sy[i] - rr, rr * 2, rr * 2, rr, alpha(c, 0.25f + 0.70f * d));
        if (e > 0.12f) blob(sx[i], sy[i], 3.0f + 5.0f * e, alpha(c, 0.30f * e * (0.3f + 0.7f * d)));
    }
    set_additive(0);
    view_switch(x, y, w);
}

/* ================================================================== Voxel spectrum
 * The spectrum as 3D voxel columns in perspective: one long column per frequency band running from the front into the depth (to the horizon); the height
 * is the current level of the band and the same along the whole depth. Same colours as the spectrum bars (VC[0] -> VC[1] -> VC[2]), light tops, fog in the
 * distance. Drawn back to front (depth blocks), outer columns first, with front, top and inner side faces. */
static float vx_lv[NBANDS];
static void draw_voxel(float x, float y, float w, float h) {
    float dt = (float)g_dt; if (dt > 0.1f) dt = 0.1f;
    for (int b = 0; b < NBANDS; b++) {           /* mix neighbouring bands a little and smooth in time: calm columns */
        float l = band[b > 0 ? b - 1 : b], r = band[b < NBANDS - 1 ? b + 1 : b];
        float v = 0.6f * band[b] + 0.2f * (l + r);
        vx_lv[b] += (v - vx_lv[b]) * (v > vx_lv[b] ? 0.75f : 0.22f);
    }
    card(x, y, w, h, 0);
    text(x + 18, y + 10, 12, DIM, "VOXEL");
    float ax = x + 12, ay = y + 34, aw = w - 24, ah = h - 46;
    if (aw < 60 || ah < 60) { view_switch(x, y, w); return; }
    const int NSEG = g_mobile ? 10 : 16;
    const float Z0 = 12.0f, DZ = g_mobile ? 5.0f : 3.2f, CAMY = 17.0f, HMAX = 15.0f, CW = 1.0f;
    float hor = ay + ah * 0.20f, cx = ax + aw * 0.5f, fx = aw * 0.178f, fy = ah * 0.55f;
    col fogc = mix(BG1, VC[1], 0.20f);
    rect_grad(ax, ay, aw, ah, mix(BG0, VC[0], 0.03f), mix(BG1, VC[1], 0.14f));       /* backdrop */
    {   /* floor lines towards the vanishing point */
        float zf = Z0 + NSEG * DZ;
        for (int b = 0; b <= NBANDS; b += 8) {
            float wx = (b - NBANDS / 2) * CW;
            float xy[4] = { cx + wx / Z0 * fx, hor + CAMY / Z0 * fy, cx + wx / zf * fx, hor + CAMY / zf * fy };
            polyline(xy, 2, 0.5f, 3.0f, alpha(FAINT, 0.18f));
        }
    }
    for (int sg = NSEG - 1; sg >= 0; sg--) {
        float zn = Z0 + sg * DZ, zf = zn + DZ * 0.88f;
        float fgt = (float)sg / NSEG; fgt = fgt * fgt;
        float sn = 1.0f / zn, sf = 1.0f / zf;
        for (int m = 0; m < NBANDS; m++) {
            int b = m < NBANDS / 2 ? m : NBANDS - 1 - (m - NBANDS / 2);               /* from the left edge inwards, then from the right edge inwards */
            float hh = (vx_lv[b] < 0.02f ? 0.02f : vx_lv[b]) * HMAX, wx0 = (b - NBANDS / 2) * CW + 0.08f, wx1 = wx0 + CW * 0.84f;
            float xn0 = cx + wx0 * sn * fx, xn1 = cx + wx1 * sn * fx, xf0 = cx + wx0 * sf * fx, xf1 = cx + wx1 * sf * fx;
            float ybn = hor + CAMY * sn * fy, ytn = hor + (CAMY - hh) * sn * fy;
            float ybf = hor + CAMY * sf * fy, ytf = hor + (CAMY - hh) * sf * fy;
            float u = (float)b / (NBANDS - 1);
            col lo = u < 0.5f ? mix(VC[0], VC[1], u * 2) : mix(VC[1], VC[2], (u - 0.5f) * 2);
            float fg = fgt * 0.85f;
            col topc = mix(mix(lo, C(1, 1, 1, 1), 0.35f), fogc, fg), frontc = mix(lo, fogc, fg), sidec = mix(mix(lo, BG0, 0.45f), fogc, fg);
            if (b >= NBANDS / 2) { tri(xn0, ybn, xn0, ytn, xf0, ytf, sidec); tri(xn0, ybn, xf0, ytf, xf0, ybf, sidec); }
            else                 { tri(xn1, ybn, xn1, ytn, xf1, ytf, sidec); tri(xn1, ybn, xf1, ytf, xf1, ybf, sidec); }
            if (sg == 0) rect_grad(xn0, ytn, xn1 - xn0, ybn - ytn, mix(frontc, C(1, 1, 1, 1), 0.10f), alpha(frontc, 0.55f));   /* front face of the nearest block only */
            tri(xn0, ytn, xn1, ytn, xf0, ytf, topc); tri(xn1, ytn, xf1, ytf, xf0, ytf, topc);
        }
    }
    view_switch(x, y, w);
}

/* Pegel: zwei waagerechte Balken */
static void draw_level(float x, float y, float w, float h) {
    card(x, y, w, h, 0);
    text(x + 18, y + 10, 12, DIM, "LEVEL");
    enum { SEG = 22 };
    float bx = x + 40, bw = w - 40 - 18, sg = bw / SEG, bh = 22;
    for (int c = 0; c < 2; c++) {
        float yy = y + 34 + c * (bh + 12);
        text(x + 18, yy + 3, 13, DIM, c ? "R" : "L");
        for (int i = 0; i < SEG; i++) {
            float lvl = (float)(i + 1) / SEG;
            col base = lvl > 0.9f ? WARN : (lvl > 0.72f ? VC[2] : OKC);
            int on = vu[c] >= lvl - 0.5f / SEG;
            rrect(bx + i * sg + 1, yy, sg - 2, bh, 2, alpha(base, on ? 0.95f : 0.12f));
        }
        int pi = (int)(vupk[c] * SEG);
        if (pi > 0 && pi <= SEG) {
            float lvl = (float)pi / SEG;
            col base = lvl > 0.9f ? WARN : (lvl > 0.72f ? VC[2] : OKC);
            rrect(bx + (pi - 1) * sg + 1, yy, sg - 2, bh, 2, mix(base, C(1, 1, 1, 1), 0.4f));
        }
    }
}

/* ---------------------------------------------------------------- Kanaluebersicht */
static float ch_lvl[64], ch_pk[64], ch_pkhold[64];
static char ch_note[64][4];
static char ch_fx[64][24];
static int ch_fxcls[64];
static float ch_fxage[64], ch_noteage[64];
static int ov_last_order = -1, ov_last_row = -1;

static int overview_cols(int nch) { if (g_portrait) return nch <= 8 ? 1 : (nch <= 16 ? 2 : 3); return nch <= 10 ? 1 : (nch <= 20 ? 2 : 3); }
static float overview_height(int nch) {
    int cols = overview_cols(nch), rows = (nch + cols - 1) / cols;
    float rh = g_portrait ? 16.0f : (cols == 1 ? 17.0f : 15.0f);
    return 38.0f + rows * rh + 10.0f;
}

static void overview_update(float dt) {
    if (!g_song) return;
    if (ov_reset) { memset(ch_lvl, 0, sizeof ch_lvl); memset(ch_pk, 0, sizeof ch_pk); memset(ch_note, 0, sizeof ch_note); memset(ch_fx, 0, sizeof ch_fx); ov_last_order = -1; ov_reset = 0; }
    if (!g_view_valid) return;
    SongPos sp = g_view;
    int nch = song_channels(g_song);
    if (nch > 64) nch = 64;
    /* Pegel je Kanal aus dem Scope-Puffer: Spitzenwert der letzten ~600 Frames */
    unsigned wp = song_scope_pos(g_song);
    for (int c = 0; c < nch; c++) {
        const float *sb = g_scope + (size_t)c * SCOPE_LEN;
        float pk = 0;
        for (int k = 1; k <= 600; k++) { float v = fabsf(sb[(wp - (unsigned)k) & (SCOPE_LEN - 1)]); if (v > pk) pk = v; }
        float val = powf(fminf(pk / 0.7f, 1.0f), 0.6f);
        if (val > ch_lvl[c]) ch_lvl[c] += (val - ch_lvl[c]) * 0.7f; else { ch_lvl[c] -= dt * 1.8f; if (ch_lvl[c] < val) ch_lvl[c] = val; }
        if (ch_lvl[c] >= ch_pk[c]) { ch_pk[c] = ch_lvl[c]; ch_pkhold[c] = 0.5f; }
        else if (ch_pkhold[c] > 0) ch_pkhold[c] -= dt;
        else { ch_pk[c] -= dt * 0.6f; if (ch_pk[c] < ch_lvl[c]) ch_pk[c] = ch_lvl[c]; }
        ch_fxage[c] += dt; ch_noteage[c] += dt;
    }
    /* neue Zeile -> Note/Effekt der Zeile uebernehmen */
    if (sp.order != ov_last_order || sp.row != ov_last_row) {
        ov_last_order = sp.order; ov_last_row = sp.row;
        for (int c = 0; c < nch; c++) {
            SongCell cell; song_cell(g_song, sp.pattern, sp.row, c, &cell);
            if (cell.has_note) { memcpy(ch_note[c], cell.note, 4); ch_noteage[c] = 0; }
            int isv = 0;
            const char *nm = song_fx_name(g_song, &cell, &isv);
            if (nm) {
                if (isv) snprintf(ch_fx[c], sizeof ch_fx[c], "%s %s", nm, cell.vol + 1);
                else snprintf(ch_fx[c], sizeof ch_fx[c], "%s %s", nm, cell.fx + 1);
                ch_fxcls[c] = isv ? cell.volclass : cell.fxclass;
                ch_fxage[c] = 0;
            }
        }
    }
}

static void draw_overview(float x, float y, float w, float h) {
    card(x, y, w, h, 0);
    if (!g_song) return;
    int nch = song_channels(g_song);
    if (nch > 64) nch = 64;
    int cols = overview_cols(nch), rows = (nch + cols - 1) / cols;
    float rh = g_portrait ? 16.0f : (cols == 1 ? 17.0f : 15.0f), fs = cols == 1 ? 12.0f : 11.0f;
    text(x + 18, y + 10, 12, DIM, "CHANNELS");
    float cwid = (w - 36) / cols;
    float panw = cwid * 0.17f; if (panw < 54) panw = 54; if (panw > 150) panw = 150;
    float barw = cwid * 0.26f; if (barw < 64) barw = 64; if (barw > 300) barw = 300;
    if (g_portrait) barw = cwid * (cols == 1 ? 0.55f : 0.48f);
    for (int c = 0; c < nch; c++) {
        int col_i = c / rows, row_i = c % rows;
        float cx = x + 18 + col_i * cwid, cy = y + 32 + row_i * rh;
        col lc = VC[c % 3];
        /* Nummer */
        char nb[8]; snprintf(nb, sizeof nb, "%02d", c + 1);
        text(cx, cy + 1, fs, alpha(FAINT, 1.0f), nb);
        /* Volumebar */
        float bx = cx + 22, bh = rh - 6;
        rrect(bx, cy + 2, barw, bh, 3, alpha(FAINT, 0.16f));
        float lw = barw * ch_lvl[c];
        if (lw > 1.5f) {
            col l2 = ch_lvl[c] > 0.9f ? WARN : (ch_lvl[c] > 0.72f ? VC[2] : lc);
            rrect(bx, cy + 2, lw, bh, 3, alpha(l2, 0.85f));
            rect_grad(bx + 1, cy + 3, lw - 2, bh * 0.5f, alpha(C(1, 1, 1, 1), 0.28f), alpha(C(1, 1, 1, 1), 0.0f));
        }
        if (ch_pk[c] > 0.03f) rect(bx + barw * ch_pk[c] - 1, cy + 2, 2, bh, alpha(mix(lc, C(1, 1, 1, 1), 0.5f), 0.9f));
        /* Note */
        float nx = bx + barw + 10;
        float na = ch_noteage[c] < 0.25f ? 1.0f : 0.55f;
        text(nx, cy + 1, fs, ch_note[c][0] ? alpha(TXT, na) : alpha(FAINT, 0.4f), ch_note[c][0] ? ch_note[c] : "...");
        if (g_portrait) continue;           /* Hochformat: ohne Effekttext und Panning */
        /* Effekt (blendet nach kurzer Zeit aus) */
        float ex = nx + text_width("C#4", fs) + 12, ew = cx + cwid - panw - 14 - ex;
        if (ch_fx[c][0]) {
            float fa = ch_fxage[c] < 0.5f ? 1.0f : (1.0f - (ch_fxage[c] - 0.5f) / 0.8f);
            if (fa > 0.02f) {
                char fb[40]; snprintf(fb, sizeof fb, "%s", ch_fx[c]);
                fit_text(fb, ew > 20 ? ew : 20, fs);
                text(ex, cy + 1, fs, alpha(FXC[ch_fxcls[c] < 6 ? ch_fxcls[c] : 5], fa), fb);
            }
        }
        /* Panning */
        float px = cx + cwid - panw - 4;
        int pan = song_chan_pan(g_song, c);
        for (int d = 0; d < 9; d++) rect(px + (panw - 2) * d / 8.0f, cy + rh / 2, 2, 2, alpha(FAINT, d == 4 ? 0.9f : 0.5f));
        float mxp = px + (panw - 6) * (float)pan / 255.0f;
        float act = 0.35f + 0.65f * ch_lvl[c];
        set_additive(1);
        blob(mxp + 3, cy + rh / 2 + 1, 7, alpha(lc, 0.25f * act));
        set_additive(0);
        rrect(mxp, cy + rh / 2 - 3, 6, 7, 3, alpha(mix(lc, C(1, 1, 1, 1), 0.3f), act));
    }
}


/* ---------------------------------------------------------------- Logo (ngPlayer) */
static const int LG_BAR[6][3] = { {26,70,60}, {48,44,86}, {70,24,106}, {92,54,76}, {114,36,94}, {136,78,52} }; /* x, y, h im 160er Raster */
static void draw_logo(float x, float y, float s) {
    float k = s / 160.0f;
    col cy = C(0.133f, 0.827f, 0.933f, 1), pk = C(0.957f, 0.447f, 0.714f, 1);
    rrect(x - 1, y - 1, s + 2, s + 2, 36 * k + 1, C(1, 1, 1, 0.10f));
    rrect(x, y, s, s, 36 * k, C(0.055f, 0.086f, 0.149f, 1));
    for (int i = 0; i < 6; i++)
        rrect(x + LG_BAR[i][0] * k, y + LG_BAR[i][1] * k, 14 * k, LG_BAR[i][2] * k, 5 * k, (i & 1) ? pk : cy);
    rrect(x + 26 * k, y + 140 * k, 124 * k, 3 * k, 1.5f * k, alpha(cy, 0.35f));
}
#ifdef __EMSCRIPTEN__
#define TAGLINE "Module player for the Web"
#else
#define TAGLINE "Module player for Linux"
#endif
/* ---------------------------------------------------------------- Wachssiegel (Meilenstein, oben rechts in der Logo-Box) */
#define SEAL_TEXT "1.9"          /* Text im Siegel: hier aendern */
#define SEAL_ROT 0.7854f         /* Drehung im Uhrzeigersinn (45 Grad) */
static void seal_lobes(float cx, float cy, float r, col c) {
    static const float lr[12] = { 1.00f, 0.93f, 1.05f, 0.96f, 1.02f, 0.92f, 1.04f, 0.97f, 1.01f, 0.94f, 1.06f, 0.95f };
    rrect(cx - r * 0.84f, cy - r * 0.84f, r * 1.68f, r * 1.68f, r * 0.84f, c);
    for (int i = 0; i < 12; i++) {
        float a = i * 0.5236f + 0.3f + SEAL_ROT, d = r * 0.78f * lr[i], lrad = r * 0.24f * lr[(i + 5) % 12];
        rrect(cx + cosf(a) * d - lrad, cy + sinf(a) * d - lrad, lrad * 2, lrad * 2, lrad, c);
    }
}
/* Stateless fire: every particle lives in cycles (phase = time * speed), its randomness comes from a hash of (particle, cycle),
 * so no per-particle state is needed. Additive blobs: white-yellow core -> orange -> red -> gone. */
static float fhash(unsigned n) { n = n * 747796405u + 2891336453u; n = ((n >> ((n >> 28) + 4)) ^ n) * 277803737u; n = (n >> 22) ^ n; return (float)(n & 0xFFFF) / 65535.0f; }
static col fire_col(float u) {
    if (u < 0.16f) return mix(C(1.00f, 0.90f, 0.50f, 1), C(1.00f, 0.60f, 0.12f, 1), u / 0.16f);
    if (u < 0.55f) return mix(C(1.00f, 0.60f, 0.12f, 1), C(0.90f, 0.20f, 0.04f, 1), (u - 0.16f) / 0.39f);
    return mix(C(0.90f, 0.20f, 0.04f, 1), C(0.35f, 0.05f, 0.04f, 1), (u - 0.55f) / 0.45f);
}
/* front = 0: flames licking out behind the seal; front = 1: shorter tongues rising from the wax surface (over the lettering) */
static void seal_flames(float cx, float cy, float r, float t, float beat, float flick, int front) {
    int n = g_mobile ? 10 : 22;
    float pow_ = 1.0f + 0.55f * beat;
    for (int i = 0; i < n; i++) {
        float sp = 0.85f + 0.9f * fhash((unsigned)i * 7 + 1);
        float ph = t * sp + fhash((unsigned)i * 3 + 2) * 8.0f, cyc = floorf(ph), u = ph - cyc;
        unsigned sd = (unsigned)i * 131u + (unsigned)cyc * 17u + (front ? 9001u : 0u);
        float a = -2.3f + (fhash(sd) + fhash(sd + 1) - 1.0f) * (front ? 1.6f : 2.6f);             /* around the rim, biased to the upper left */
        float ro = front ? r * (0.45f + 0.5f * fhash(sd + 4)) : r * 0.85f;
        float ox = cx + cosf(a) * ro, oy = cy + sinf(a) * ro;
        float dx = cosf(a) * 0.55f - 0.55f, dy = sinf(a) * 0.45f - 0.55f, dl = sqrtf(dx * dx + dy * dy);   /* outwards + wind to the upper left */
        dx /= dl; dy /= dl;
        float L = r * (front ? 0.8f : 1.3f + 1.5f * fhash(sd + 2)) * pow_, ue = powf(u, 0.8f);
        float wob = sinf(u * 9.0f + fhash(sd + 5) * 6.0f) * r * 0.16f * u;
        float px = ox + dx * L * ue - dy * wob, py = oy + dy * L * ue + dx * wob;
        float sz = r * (front ? 0.30f : 0.42f) * (0.6f + 0.7f * fhash(sd + 3)) * (1.0f - 0.85f * u);
        float al = (u < 0.12f ? u / 0.12f : 1.0f) * powf(1.0f - u, 1.3f) * (front ? 0.16f : 0.30f);
        col c = fire_col(u);
        blob(px, py, sz * 1.9f, alpha(c, 0.06f * al));                             /* soft halo */
        blob(px, py, sz, alpha(c, 0.5f * al));
        float u2 = u > 0.06f ? u - 0.06f : 0.0f, ue2 = powf(u2, 0.8f);            /* trailing blob: makes the tongue longer */
        blob(ox + dx * L * ue2 - dy * wob * 0.8f, oy + dy * L * ue2 + dx * wob * 0.8f, sz * 0.8f, alpha(fire_col(u2), 0.45f * al));
    }
    if (front) return;
    int nt = g_mobile ? 6 : 11, np = g_mobile ? 6 : 9;                             /* flame tongues: S-shaped, thinning towards the tip, the tips curl upwards (heat rises) */
    for (int k = 0; k < nt; k++) {
        float fk = (float)k;
        float ak = -3.0f + (fk - (nt - 1) * 0.5f) * (3.0f / (float)nt) + 0.12f * sinf(t * 1.7f + fk * 2.3f);
        float len = r * (1.0f + 0.9f * fhash((unsigned)k * 19 + 3)) * (0.78f + 0.26f * sinf(t * (5.0f + fk * 0.7f) + fk) + 0.10f * sinf(t * 17.0f + fk * 1.9f)) * (1.0f + 0.5f * beat);
        float dx = cosf(ak) * 0.8f - 0.5f, dy = sinf(ak) * 0.8f - 0.2f, dl = sqrtf(dx * dx + dy * dy);
        dx /= dl; dy /= dl;
        float bx = cx + cosf(ak) * r * 0.86f, by = cy + sinf(ak) * r * 0.86f, ph = t * 3.4f + fk * 1.7f;
        float pts[2 * 12];
        for (int m = 0; m < np; m++) {
            float sf = (float)m / (float)(np - 1);
            float sw = r * 0.30f * sinf(sf * 4.2f + ph) * sf;                       /* S-curve, flickering along the flame */
            pts[2 * m] = bx + dx * len * sf - dy * sw;
            pts[2 * m + 1] = by + dy * len * sf + dx * sw - r * 0.85f * sf * sf;    /* rising curl */
        }
        float g = 0.55f + 0.45f * flick;
        blob(bx, by, r * 0.5f, alpha(C(1.0f, 0.35f, 0.06f, 1), 0.14f * g));
        static const float LW[3] = { 0.26f, 0.16f, 0.075f }, LF[3] = { 1.0f, 0.72f, 0.42f }, LA[3] = { 0.26f, 0.34f, 0.46f };
        static const col LC[3] = { { 0.92f, 0.16f, 0.04f, 1 }, { 1.00f, 0.52f, 0.08f, 1 }, { 1.00f, 0.92f, 0.55f, 1 } };
        for (int ly = 0; ly < 3; ly++) {
            int ns = (int)((np - 1) * LF[ly] + 0.5f);
            for (int m = 0; m < ns; m++) {                                          /* one ribbon piece per segment: width tapers to the tip */
                float tp = (float)m / (float)ns, w = r * LW[ly] * powf(1.0f - tp, 0.85f) + 0.4f;
                polyline(pts + 2 * m, 2, w, 1.5f, alpha(LC[ly], LA[ly] * g * (1.0f - 0.4f * tp)));
            }
        }
    }
    int ne = g_mobile ? 6 : 14;                                                    /* embers: small sparks flying away, drifting left with the wind */
    for (int i = 0; i < ne; i++) {
        float sp = 0.35f + 0.5f * fhash((unsigned)i * 11 + 5);
        float ph = t * sp + fhash((unsigned)i * 5 + 6) * 9.0f, cyc = floorf(ph), u = ph - cyc;
        unsigned sd = (unsigned)i * 977u + (unsigned)cyc * 31u;
        float a = -1.5708f + (fhash(sd) - 0.5f) * 2.6f;
        float L = r * (1.8f + 2.4f * fhash(sd + 1)), ue = 1.0f - (1.0f - u) * (1.0f - u);
        float px = cx + cosf(a) * r * 0.8f + (cosf(a) * 0.6f - 0.7f * u) * L * ue + sinf(u * 14.0f + fhash(sd + 2) * 6.0f) * r * 0.10f;
        float py = cy + sinf(a) * r * 0.8f + (sinf(a) * 0.6f - 1.0f) * L * ue * 0.9f;
        float fl = 0.6f + 0.4f * sinf(t * 23.0f + (float)i * 4.1f), al = (u < 0.08f ? u / 0.08f : 1.0f) * (1.0f - u) * fl;
        blob(px, py, 1.1f + 0.9f * fhash(sd + 3), alpha(mix(C(1.0f, 0.85f, 0.4f, 1), C(1.0f, 0.4f, 0.1f, 1), u), 0.95f * al));
    }
}
/* Molten wax drops: now and then a drop swells at the lower rim of the seal, lets go, falls over the card (cooling from glowing orange to dark red) and splashes. */
static void seal_drop(float cx, float cy, float r, float t, float off, float per) {
    float tt = t + off, ph = fmodf(tt, per);
    unsigned cyc = (unsigned)(tt / per) * 7u + (unsigned)(off * 10.0f);
    const float TF = 1.3f, TD = 0.95f, TS = 0.4f;                                  /* swelling, falling, splash (seconds) */
    float a = 1.5708f + (fhash(cyc + 1) - 0.5f) * 1.5f;                            /* somewhere along the lower rim */
    float bx = cx + cosf(a) * r * 0.9f, by = cy + sinf(a) * r * 0.9f;
    col hot = C(1.00f, 0.52f, 0.12f, 1), wax = C(0.68f, 0.07f, 0.09f, 1), hi = C(1.00f, 0.80f, 0.55f, 1);
    if (ph < TF) {                                                                 /* hanging: the drop swells and sags */
        float u = ph / TF, w = r * (0.06f + 0.18f * u * u), sag = r * 0.30f * u * u * u;
        col c = mix(hot, wax, 0.45f);
        rrect(bx - w, by - w * 0.5f, w * 2, w * 1.5f + sag, w, c);
        rrect(bx - w * 0.35f, by - w * 0.1f, w * 0.5f, w * 0.7f, w * 0.25f, alpha(hi, 0.6f));
    } else if (ph < TF + TD) {                                                     /* falling */
        float v = (ph - TF) / TD, w = r * 0.24f, y = by + r * 0.25f + r * 3.6f * v * v, st = w * (0.4f + 1.4f * v);
        float fa = v > 0.85f ? (1.0f - v) / 0.15f : 1.0f;
        col c = mix(hot, wax, v);
        set_additive(1); blob(bx, y, w * 2.6f, alpha(hot, 0.35f * (1.0f - v) * fa)); set_additive(0);
        rrect(bx - w * 0.8f, y - st, w * 1.6f, w + st, w * 0.8f, alpha(c, fa));                                   /* stretched tear */
        rrect(bx - w * 0.3f, y - st * 0.2f, w * 0.5f, w * 0.7f, w * 0.25f, alpha(hi, 0.55f * (1.0f - v) * fa));
    } else if (ph < TF + TD + TS) {                                                /* splash on the card */
        float u = (ph - TF - TD) / TS, y = by + r * 0.25f + r * 3.6f, rr = r * (0.10f + 0.35f * u), pts[2 * 13];
        for (int i = 0; i < 13; i++) { float an = i * 6.2832f / 12; pts[2 * i] = bx + cosf(an) * rr; pts[2 * i + 1] = y + sinf(an) * rr * 0.35f; }
        polyline(pts, 13, 1.6f, 1.0f, alpha(mix(hot, wax, 0.6f), 0.7f * (1.0f - u)));
    }
}
static void draw_seal(float cx, float cy, float r, const char *label) {
    static float age;                                                              /* Sekunden seit Programmstart */
    age += (float)g_dt;
    const float T1 = 0.30f;                                                        /* Dauer des Falls bis zum Aufschlag */
    float since = age - T1, sc, al, heat;
    if (age < T1) { float u = age / T1; sc = 1.0f + 1.0f * (1 - u) * (1 - u); al = u < 0.4f ? u / 0.4f : 1.0f; heat = 1.0f; }
    else { sc = 1.0f - 0.10f * expf(-since * 9.0f) * sinf(since * 18.0f); al = 1.0f; heat = expf(-since * 1.1f); }
    float beat = g_paused ? 0.0f : g_beat;
    float flick = 0.5f + 0.25f * sinf(age * 13.0f) + 0.15f * sinf(age * 7.3f + 1.0f) + 0.10f * sinf(age * 31.0f);   /* fire flicker, 0..1 */
    float burn = 0.34f + 0.16f * flick + 0.12f * beat;                             /* the seal burns all the time: wax stays glowing */
    if (heat < burn) heat = burn;
    float gl = heat + 0.35f + 0.25f * flick + 0.25f * beat;                        /* glow + flicker + pulse with the beat */
    col dk = mix(C(0.34f, 0.03f, 0.05f, 1), C(0.80f, 0.20f, 0.05f, 1), heat * 0.6f);
    col md = mix(C(0.68f, 0.07f, 0.09f, 1), C(1.00f, 0.45f, 0.10f, 1), heat * 0.85f);
    col lt = mix(C(0.90f, 0.26f, 0.22f, 1), C(1.00f, 0.88f, 0.45f, 1), heat);
    r *= sc;
    set_additive(1);                                                               /* Glut hinter dem Siegel */
    blob(cx, cy, r * (1.25f + 0.35f * heat), alpha(C(1.0f, 0.32f, 0.06f, 1), 0.12f * gl * al));
    blob(cx, cy, r * (0.85f + 0.35f * heat), alpha(C(1.0f, 0.32f, 0.06f, 1), 0.24f * gl * al));
    seal_flames(cx, cy, r, age, beat, flick, 0);                                          /* flames licking out behind the seal */
    set_additive(0);
    blob(cx + 1.5f, cy + 3.0f, r * 0.55f, C(0, 0, 0, 0.55f * al));                 /* Schatten */
    seal_lobes(cx + 0.8f, cy + 1.0f, r, alpha(dk, al));                            /* dunkler Rand unten rechts */
    seal_lobes(cx - 0.4f, cy - 0.5f, r, alpha(md, al));                            /* Wachskoerper */
    float ri = r * 0.70f;                                                          /* eingedrueckte Mulde */
    rrect(cx - ri + 0.9f, cy - ri + 0.9f, ri * 2, ri * 2, ri, alpha(lt, 0.75f * al));
    rrect(cx - ri - 0.9f, cy - ri - 0.9f, ri * 2, ri * 2, ri, alpha(dk, al));
    rrect(cx - ri + 0.9f, cy - ri + 0.9f, (ri - 0.9f) * 2, (ri - 0.9f) * 2, ri - 0.9f, alpha(mix(md, dk, 0.12f), al));
    float ts = r * 0.80f, tx = cx - text_width(label, ts) / 2, ty = cy - ts * 0.44f;
    text_rot(cx + 0.8f, cy + 0.8f, SEAL_ROT, tx + 0.8f, ty + 0.8f, ts, alpha(dk, al), label);              /* erhabene Schrift, gedreht */
    text_rot(cx - 0.5f, cy - 0.5f, SEAL_ROT, tx - 0.5f, ty - 0.5f, ts, alpha(lt, 0.85f * al), label);
    text_rot(cx, cy, SEAL_ROT, tx, ty, ts, alpha(mix(md, lt, 0.35f), al), label);
    set_additive(1);
    blob(cx - r * 0.35f, cy - r * 0.42f, r * 0.30f, alpha(C(1, 0.9f, 0.8f, 1), 0.30f * al));   /* Glanz */
    blob(cx, cy, r * 0.5f, alpha(C(1.0f, 0.55f, 0.15f, 1), 0.40f * heat * al));                /* heisses Wachs */
    float gs = sinf(age * 1.3f);                                                   /* Glanzpunkt wandert am Rand entlang */
    if (gs > 0) { float g6 = gs * gs * gs * gs * gs * gs, ga = age * 0.9f; blob(cx + cosf(ga) * r * 0.8f, cy + sinf(ga) * r * 0.8f, r * 0.16f, alpha(C(1, 0.85f, 0.7f, 1), 0.55f * g6)); }
    if (since > 0 && since < 0.55f) {                                              /* Druckwelle beim Aufschlag */
        float u = since / 0.55f, rr = r * (1.0f + 1.3f * u), pts[2 * 25];
        for (int i = 0; i < 25; i++) { float an = i * 6.2832f / 24; pts[2 * i] = cx + cosf(an) * rr; pts[2 * i + 1] = cy + sinf(an) * rr; }
        polyline(pts, 25, 2.0f, 3.0f, alpha(C(1.0f, 0.55f, 0.2f, 1), 0.55f * (1 - u) * (1 - u)));
    }
    if (since > 0 && since < 1.0f) {                                               /* Funken */
        for (int i = 0; i < 12; i++) {
            float an = i * 0.5236f + 0.4f + 0.3f * sinf(i * 2.7f), sp = r * (1.8f + (i % 4) * 0.55f);
            float dd = sp * 0.35f * (1.0f - expf(-5.0f * since)), fa = 1.0f - since;
            blob(cx + cosf(an) * dd, cy + sinf(an) * dd + 14.0f * since * since, 1.3f + 0.4f * (i % 3), alpha(C(1.0f, 0.7f, 0.25f, 1), 0.9f * fa * fa));
        }
    }
    seal_flames(cx, cy, r, age, beat, flick, 1);                                          /* tongues over the wax */
    set_additive(0);
    seal_drop(cx, cy, r, age, 0.0f, 5.3f);                                         /* drips: two with different rhythms */
    seal_drop(cx, cy, r, age, 2.9f, 8.1f);
}
/* ---------------------------------------------------------------- Soundchips (Kopfzeile)
   Gezeichnet mit den vorhandenen Primitiven (Koordinaten im 320x200-Raster, Chip um 160,100). */
#define BEATC C(0.133f, 0.827f, 0.933f, 1)      /* Akzentfarbe des Beat-Pulses (Cyan wie das Logo) */
static float chip_tsz(float fs, float k) { float z = fs * k; return z < 7.0f ? 7.0f : z; }
static void chip_dip(float x, float y, float h, float beat) {            /* Paula, DIP-48 (MOS 8364R7); h = Hoehe, Breite = 5 * h */
    float k = h / 100.0f;
    col pin = mix(C(0.62f, 0.65f, 0.70f, 1), BEATC, 0.55f * beat), body = C(0.23f, 0.24f, 0.255f, 1), ink = mix(C(0.79f, 0.80f, 0.83f, 1), C(1, 1, 1, 1), 0.5f * beat), dimple = C(0.09f, 0.095f, 0.10f, 1);
    float x0 = 10, x1 = 490, pw = (x1 - x0) / 24.0f;
    for (int i = 0; i < 24; i++) {
        float px = x0 + i * pw + pw * 0.22f;
        rect(x + px * k, y, pw * 0.56f * k, 12 * k, pin);
        rect(x + px * k, y + 88 * k, pw * 0.56f * k, 12 * k, pin);
    }
    rrect(x + (x0 - 4) * k - 1, y + 11 * k - 1, (x1 - x0 + 8) * k + 2, 78 * k + 2, 3 * k + 1, C(0.07f, 0.07f, 0.08f, 1));
    rrect(x + (x0 - 4) * k, y + 11 * k, (x1 - x0 + 8) * k, 78 * k, 3 * k, body);
    rrect(x + (95 - 12) * k, y + (50 - 12) * k, 24 * k, 24 * k, 12 * k, dimple);
    rrect(x + (415 - 12) * k, y + (50 - 12) * k, 24 * k, 24 * k, 12 * k, dimple);
    float cx = x + 250 * k, z;
    z = chip_tsz(15, k); text(cx - text_width("MOS", z) / 2, y + 36 * k - z * 0.8f, z, ink, "MOS");
    z = chip_tsz(17, k); text(cx - text_width("8364R7", z) / 2, y + 57 * k - z * 0.8f, z, ink, "8364R7");
    z = chip_tsz(14, k); text(cx - text_width("1589  27", z) / 2, y + 77 * k - z * 0.8f, z, ink, "1589  27");
}
/* Welcher Chip: 0 = Paula (TFMX, MOD bis 4 Kanaele), 1 = GF1 (bis 14 Kanaele), 2 = InterWave (ab 15 Kanaelen) */
static int chip_kind(void) {
    const char *f = song_format(g_song);
    int n = song_channels(g_song);
    if (!strcmp(f, "TFMX") || !strcmp(f, "AHX") || (!strcmp(f, "MOD") && n <= 4)) return 0;
    return n >= 15 ? 2 : 1;
}
static float chips_width(float h) {                       /* Breite des Chipbereichs (0 = keiner) */
    if (!g_song) return 0;
    int k = chip_kind();
    if (k == 0) return 5.0f * h * 0.5f;                            /* Paula: flacher Streifen (5:1), in halber Hoehe */
    return k == 1 ? h * ((float)CARD_GUS_W / CARD_GUS_H) : h * ((float)CARD_IW_W / CARD_IW_H);   /* photo of the sound card */
}
static void draw_chips(float x, float y, float h) {
    if (!g_song) return;
    int k = chip_kind();
    float beat = g_paused ? 0.0f : g_beat;
    float cw = chips_width(h), sc = 1.0f + 0.07f * beat;
    float gx = x + cw / 2, gy = k == 0 ? y + h * 0.08f + h * 0.25f : y + h / 2;
    if (beat > 0.01f) {                                   /* Glow hinter dem Chip */
        set_additive(1);
        blob(gx, gy, (k == 0 ? cw * 0.30f : h * 0.55f) * (1.0f + 0.4f * beat), alpha(BEATC, 0.55f * beat));
        set_additive(0);
    }
    float hh = h * sc;                                    /* leicht wachsen, um die Mitte */
    float xx = x - (cw * sc - cw) / 2, yy = y - (hh - h) / 2;
    if (k == 0) chip_dip(xx, yy + hh * 0.08f, hh * 0.5f, beat);
    else { float b = 1.0f + 0.30f * beat, f = 0.88f; tex_quad(card_tex[k - 1], xx + cw * sc * (1 - f) / 2, yy + hh * 0.15f, cw * sc * f, hh * f, C(b, b, b, 1)); }   /* a bit smaller and lower: clear of the top edge */
}
/* XTC-style water ripples behind the logo: one source per channel, amplitude follows the channel level */
static void logo_waves(float x, float y, float w, float h) {
    static float wt;
    wt += (float)g_dt;
    float src[16 * 4];
    int n = g_song ? song_channels(g_song) : 8;
    if (n > 16) n = 16;
    if (n < 1) n = 1;
    for (int i = 0; i < 16; i++) {
        float *p = src + 4 * i;
        p[0] = p[1] = p[2] = p[3] = 0;
        if (i >= n) continue;
        float lv;
        if (g_song) lv = g_paused ? 0.0f : ch_lvl[i];
        else lv = 0.35f + 0.35f * sinf(wt * 1.1f + i * 1.7f);          /* idle: gentle pulsing */
        if (lv < 0) lv = 0;
        p[0] = (n > 1 ? (i - (n - 1) * 0.5f) * (w - 40) / 15.0f : 0.0f);
        p[1] = h * 0.14f;
        p[2] = powf(lv, 0.7f);
        p[3] = i * 1.3f;
    }
    ripples(x + 1, y + 1, w - 2, h - 2, 13, wt, src);
}
static int lg_inside(float px, float py, float x, float y, float w, float h, float r) {
    float cx = px < x + r ? x + r : (px > x + w - r ? x + w - r : px);
    float cy = py < y + r ? y + r : (py > y + h - r ? y + h - r : py);
    if (px < x || px > x + w || py < y || py > y + h) return 0;
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}
static SDL_Surface *make_icon(int n) {
    SDL_Surface *su = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_RGBA32);
    if (!su) return NULL;
    Uint8 *px = su->pixels;
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
        float fx = (i + 0.5f) * 160.0f / n, fy = (j + 0.5f) * 160.0f / n;
        Uint8 *p = px + j * su->pitch + i * 4; p[0] = p[1] = p[2] = p[3] = 0;
        if (!lg_inside(fx, fy, 0, 0, 160, 160, 36)) continue;
        p[0] = 14; p[1] = 22; p[2] = 38; p[3] = 255;
        for (int b = 0; b < 6; b++)
            if (lg_inside(fx, fy, LG_BAR[b][0], LG_BAR[b][1], 14, LG_BAR[b][2], 5)) {
                if (b & 1) { p[0] = 244; p[1] = 114; p[2] = 182; } else { p[0] = 34; p[1] = 211; p[2] = 238; }
            }
    }
    return su;
}

static void next_prev(int d) {      /* stays inside the group (local files / Mod Archive) of the current song */
    if (npl <= 0) return;
    int grp = cur >= 0 ? pl[cur].ma_id > 0 : g_pltab, i = cur < 0 ? -1 : cur;
    for (int n = 0; n < npl; n++) {
        i += d;
        if (i < 0) i = npl - 1;
        if (i >= npl) i = 0;
        if ((pl[i].ma_id > 0) == grp) { play_index(i); return; }
    }
}

/* playlist tabs: indices of the entries of the active tab */
static int pl_count_tab(int tab) { int n = 0; for (int i = 0; i < npl; i++) if ((pl[i].ma_id > 0) == (tab == 1)) n++; return n; }
/* Remove an entry from the list (the file itself stays untouched). The playing song moves on to the next one of its group, or stops. */
static void pl_remove(int i) {
    if (i < 0 || i >= npl) return;
    if (i == cur) { if (pl_count_tab(pl[i].ma_id > 0) > 1) next_prev(1); else { unload(); cur = -1; } }
    free(pl[i].path); free(pl[i].key);
    memmove(pl + i, pl + i + 1, sizeof(entry) * (size_t)(npl - i - 1));
    npl--;
    if (cur > i) cur--;
    ctx_idx = -1;
#ifdef __EMSCRIPTEN__
    ma_sync_web(1);
#endif
}
static void pl_build_vis(void) {
    pl_vis = (int *)realloc(pl_vis, sizeof(int) * (size_t)(npl > 0 ? npl : 1));
    nvis = 0; cvis = -1;
    for (int i = 0; i < npl; i++) if ((pl[i].ma_id > 0) == (g_pltab == 1)) { if (i == cur) cvis = nvis; pl_vis[nvis++] = i; }
}
/* FILES | MOD ARCHIVE tabs; tap_mode: use the tap flag instead of clicked() (touch overlay) */
static void pl_tabs(float x, float y, float w, int tap_mode, int tap) {
    const char *nm[2] = { "FILES", "MOD ARCHIVE" };
    int ntab = pl_count_tab(1) > 0 ? 2 : 1;                 /* the MOD ARCHIVE tab appears once something was loaded from there */
    if (ntab == 1) g_pltab = 0;
    float bw = ntab == 2 ? (w - 36 - 8) / 2 : 120;
    for (int t = 0; t < ntab; t++) {
        float bx = x + 18 + t * (bw + 8), by = y;
        char lb[48]; snprintf(lb, sizeof lb, "%s  %d", nm[t], pl_count_tab(t));
        int on = g_pltab == t, hv = hit(bx, by, bw, 24);
        rrect(bx, by, bw, 24, 7, on ? alpha(VC[2], 0.25f) : alpha(FAINT, hv ? 0.30f : 0.16f));
        text(bx + (bw - text_width(lb, 11)) / 2, by + 6, 11, on ? VC[2] : alpha(DIM, 0.9f), lb);
        int hitnow = tap_mode ? (tap && mx >= bx && mx < bx + bw && my >= by && my < by + 24) : clicked(bx, by, bw, 24);
        if (hitnow) { g_pltab = t; list_scroll = 0; list_follow = 1; if (!tap_mode) click = 0; }
    }
}


/* Spulen innerhalb des Songs: neu oeffnen und lautlos bis zur Ziel-Order vorrechnen (Kern bleibt unberuehrt). */
static void seek_order(int target) {
    if (cur < 0 || !g_song) return;
    if (target < 0) target = 0;
    if (target >= song_norders(g_song)) { next_prev(1); return; }
    char err[128] = "";
    Song *s = song_open_sub(pl[cur].path, g_rate, g_open_sub, err, sizeof err);
    if (!s) return;
    static int16_t tmp[128 * 2];
    long long cap = (long long)g_rate * 3600; unsigned long long adv = 0;
    for (SongPos p; ; ) {
        song_pos(s, &p);
        if (p.order >= target) break;
        int got = song_render(s, tmp, 128);
        cap -= 128; adv += (unsigned)got;
        if (!got || cap < 0) { song_close(s); return; }   /* Songende/Schleife: Abbruch, alter Song laeuft weiter */
    }
    memset(g_scope, 0, sizeof g_scope);
    song_scope_enable(s, g_scope, SCOPE_LEN);
    if (g_dev) SDL_LockAudioDevice(g_dev);
    Song *old = g_song;
    g_song = s;
    g_rendered = 0; g_lastord = g_lastrow = -1; g_resync = 1;
    g_frames = adv;                                   /* elapsed time = what was skipped over */
    g_ended = 0; g_paused = 0;
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
    song_close(old);
    ov_reset = 1; view_reset = 1; pat_ch0 = pat_ch0;
}


/* ---------------------------------------------------------------- Dice: random module from The Mod Archive
 * A little 3D die at the bottom of the playlist. A click rolls it (it tumbles while the module is fetched) and it settles on a face. */
static void ma_random(void);
static int ma_busy(void);
static int ma_loading(void);

/* ---- Random mode (RND): at the end of the Mod Archive list "next" (button, key, lock screen) and the end of a song draw a new random
 * module instead of wrapping around to the first entry. Remembered between runs. ---- */
static int g_rand;
static Uint32 rand_fail_t;                 /* a draw that failed: wait a few seconds before the next try (the song end would retry every frame) */
static int last_of_group(int i) {
    for (int j = i + 1; j < npl; j++) if ((pl[j].ma_id > 0) == (pl[i].ma_id > 0)) return 0;
    return 1;
}
static void next_user(int d) {              /* next / previous as the user asks for it */
    if (d > 0 && g_rand && cur >= 0 && cur < npl && pl[cur].ma_id > 0 && last_of_group(cur)) {
        if (ma_busy()) { if (ma_loading()) { snprintf(toast_txt, sizeof toast_txt, "Still loading ..."); toast_t = 1.5f; } }
        else if (!rand_fail_t || SDL_GetTicks() - rand_fail_t > 5000) { snprintf(toast_txt, sizeof toast_txt, "Drawing a random module ..."); toast_t = 2.5f; ma_random(); }
        return;
    }
    next_prev(d);
}
static void song_end_advance(void) {        /* song ended -> next entry (a new random one at the end of the list in random mode) */
    if (g_ended && cur >= 0 && (npl > 1 || (g_rand && npl == 1 && pl[cur].ma_id > 0))) next_user(1);
}
static int dice_roll, dice_stop_req;         /* rolling / the module has arrived, stop as soon as the roll was long enough */
static float dice_t, dice_since, dice_hop, dice_w[3], dice_Rm[3][3] = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };   /* orientation + angular velocity */

static void dice_start(void) {
    dice_roll = 1; dice_stop_req = 0; dice_since = 0;
    dice_w[0] = 7.5f; dice_w[1] = -9.5f; dice_w[2] = 5.5f;
}
static void m3_mul(float r[3][3], const float a[3][3], const float b[3][3]) {
    float t[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    memcpy(r, t, sizeof t);
}
static void m3_rot(float m[3][3], int axis, float a) {
    float c = cosf(a), s = sinf(a);
    memset(m, 0, 9 * sizeof(float));
    int i = (axis + 1) % 3, j = (axis + 2) % 3;
    m[axis][axis] = 1; m[i][i] = c; m[i][j] = -s; m[j][i] = s; m[j][j] = c;
}
static void m3_axis(float m[3][3], const float ax[3], float a) {   /* rotation by a around the unit vector ax (Rodrigues) */
    float c = cosf(a), s = sinf(a), t = 1 - c, x = ax[0], y = ax[1], z = ax[2];
    float r[3][3] = { { t * x * x + c, t * x * y - s * z, t * x * z + s * y }, { t * x * y + s * z, t * y * y + c, t * y * z - s * x },
                      { t * x * z - s * y, t * y * z + s * x, t * z * z + c } };
    memcpy(m, r, sizeof r);
}
static void dice_view(float V[3][3]) {                     /* the slightly wobbling view tilt */
    float V1[3][3], V2[3][3];
    m3_rot(V1, 0, 0.45f + 0.05f * sinf(dice_t * 0.9f)); m3_rot(V2, 1, -0.55f + 0.10f * sinf(dice_t * 1.1f));
    m3_mul(V, V1, V2);
}

/* ---- the die: an icosahedron (D20), flat shaded facets with a number on each face ---- */
typedef struct { float v[12][3]; int tri[20][3]; float n[20][3], u[20][3], w[20][3]; int num[20]; } d20_t;
static d20_t D20; static int d20_ready;
static void d20_init(void) {
    const float ph = 1.6180340f, nl = 1.0f / sqrtf(1 + ph * ph);
    static const signed char B[12][3] = { {0, 1, 1}, {0, 1, -1}, {0, -1, 1}, {0, -1, -1}, {1, 1, 0}, {1, -1, 0}, {-1, 1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1} };
    for (int i = 0; i < 12; i++) {           /* (0,±1,±φ) (±1,±φ,0) (±φ,0,±1) : the table cycles which coordinate carries φ */
        float a = (float)B[i][0], b = (float)B[i][1], c = (float)B[i][2], x, y, z;
        if (i < 4) { x = 0; y = b; z = c * ph; } else if (i < 8) { x = a; y = b * ph; z = 0; } else { x = a * ph; y = 0; z = c; }
        D20.v[i][0] = x * nl; D20.v[i][1] = y * nl; D20.v[i][2] = z * nl;
    }
    int nf = 0;
    float edge2 = 4.0f * nl * nl;
    for (int a = 0; a < 12; a++) for (int b = a + 1; b < 12; b++) for (int c = b + 1; c < 12; c++) {
        int p[3] = { a, b, c }; int ok = 1;
        for (int e = 0; e < 3; e++) {
            const float *P = D20.v[p[e]], *Q = D20.v[p[(e + 1) % 3]];
            float dd = (P[0] - Q[0]) * (P[0] - Q[0]) + (P[1] - Q[1]) * (P[1] - Q[1]) + (P[2] - Q[2]) * (P[2] - Q[2]);
            if (fabsf(dd - edge2) > 0.02f) ok = 0;
        }
        if (!ok || nf >= 20) continue;
        D20.tri[nf][0] = a; D20.tri[nf][1] = b; D20.tri[nf][2] = c; nf++;
    }
    for (int f = 0; f < 20; f++) {
        float g[3] = { 0, 0, 0 };
        for (int k = 0; k < 3; k++) for (int e = 0; e < 3; e++) g[e] += D20.v[D20.tri[f][k]][e] / 3.0f;
        float l = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
        for (int e = 0; e < 3; e++) D20.n[f][e] = g[e] / l;
        const float *P = D20.v[D20.tri[f][0]];                    /* u: from the centre of the face to its first corner, v = n x u (so u x v = n) */
        float u[3] = { P[0] - g[0], P[1] - g[1], P[2] - g[2] }, ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        for (int e = 0; e < 3; e++) D20.u[f][e] = u[e] / ul;
        const float *n = D20.n[f], *uu = D20.u[f];
        D20.w[f][0] = n[1] * uu[2] - n[2] * uu[1]; D20.w[f][1] = n[2] * uu[0] - n[0] * uu[2]; D20.w[f][2] = n[0] * uu[1] - n[1] * uu[0];
    }
    for (int f = 0; f < 20; f++) D20.num[f] = 0;                  /* numbers 1..20, opposite faces add up to 21 */
    int k = 1;
    for (int f = 0; f < 20; f++) {
        if (D20.num[f]) continue;
        int o = -1; float best = 2;
        for (int g = 0; g < 20; g++) { float d = D20.n[f][0] * D20.n[g][0] + D20.n[f][1] * D20.n[g][1] + D20.n[f][2] * D20.n[g][2]; if (d < best) { best = d; o = g; } }
        D20.num[f] = k; D20.num[o] = 21 - k; k++;
    }
    d20_ready = 1;
}
/* digits as strokes on a 2 x 4 grid (y up): x y pairs, -1 starts the next stroke, -2 ends the glyph */
static const signed char DG[10][30] = {
    { 0, 0, 2, 0, 2, 4, 0, 4, 0, 0, -2 },
    { 0, 3, 1, 4, 1, 0, -2 },
    { 0, 4, 2, 4, 2, 2, 0, 2, 0, 0, 2, 0, -2 },
    { 0, 4, 2, 4, 2, 0, 0, 0, -1, 0, 2, 2, 2, -2 },
    { 0, 4, 0, 2, 2, 2, -1, 2, 4, 2, 0, -2 },
    { 2, 4, 0, 4, 0, 2, 2, 2, 2, 0, 0, 0, -2 },
    { 2, 4, 0, 4, 0, 0, 2, 0, 2, 2, 0, 2, -2 },
    { 0, 4, 2, 4, 1, 0, -2 },
    { 0, 0, 2, 0, 2, 4, 0, 4, 0, 0, -1, 0, 2, 2, 2, -2 },
    { 2, 2, 0, 2, 0, 4, 2, 4, 2, 0, 0, 0, -2 } };
/* a number written in the plane of a face: centre c, axes u (right) and v (up), height h (in model units) */
static void d20_number(int num, const float P[3], const float U[3], const float W[3], float h, float sx, float cx, float py, float lw, col c, float depth) {
    int dg[2], nd = 0;
    if (num >= 10) dg[nd++] = num / 10;
    dg[nd++] = num % 10;
    float gw = h * 0.5f, gap = h * 0.22f, tot = nd * gw + (nd - 1) * gap, x0 = -tot * 0.5f;
    for (int d = 0; d < nd; d++, x0 += gw + gap) {
        const signed char *g = DG[dg[d]];
        float xy[16]; int np = 0;
        for (int i = 0;; i += 2) {
            if (g[i] < 0) {
                if (np > 1) polyline(xy, np, lw, 2.2f, c);
                np = 0;
                if (g[i] == -2) break;
                i -= 1; continue;
            }
            float a = x0 + g[i] * 0.5f * gw, b = (g[i + 1] * 0.25f - 0.5f) * h;
            float X = P[0] + a * U[0] + b * W[0], Y = P[1] + a * U[1] + b * W[1], Z = P[2] + a * U[2] + b * W[2];
            float pz = 1.0f / (1.0f - 0.10f * Z);
            xy[np * 2] = cx + X * sx * pz; xy[np * 2 + 1] = py - Y * sx * pz; np++;
        }
    }
    (void)depth;
}
static unsigned g_fid, dice_fid = ~0u;      /* frame counter: the dice state advances once per frame, however often it is drawn */
static void dice_step(void) {
    if (dice_fid == g_fid) return;
    dice_fid = g_fid;
    if (!d20_ready) d20_init();
    float dt = (float)g_dt > 0.1f ? 0.1f : (float)g_dt;
    dice_t += dt;
    float wl = sqrtf(dice_w[0] * dice_w[0] + dice_w[1] * dice_w[1] + dice_w[2] * dice_w[2]);
    if (dice_roll) {
        dice_since += dt;
        if (dice_stop_req && dice_since > 1.1f) dice_roll = 0;
    } else {
        float damp = expf(-dt * 3.5f);
        for (int k = 0; k < 3; k++) dice_w[k] *= damp;
        wl *= damp;
    }
    if (wl > 1e-4f) {                                       /* tumble */
        float ax[3] = { dice_w[0] / wl, dice_w[1] / wl, dice_w[2] / wl }, dR[3][3];
        m3_axis(dR, ax, wl * dt); m3_mul(dice_Rm, dR, dice_Rm);
    }
    if (!dice_roll && wl < 3.0f) {                          /* come to rest with the best facing face turned to the viewer */
        float V[3][3], tg[3], bn[3] = { 0, 0, 1 }, best = -2;
        dice_view(V);
        for (int k = 0; k < 3; k++) tg[k] = V[2][k];                      /* V^T * (0,0,1) = third row of V */
        for (int f = 0; f < 20; f++) {
            float c[3], d;
            for (int k = 0; k < 3; k++) c[k] = dice_Rm[k][0] * D20.n[f][0] + dice_Rm[k][1] * D20.n[f][1] + dice_Rm[k][2] * D20.n[f][2];
            d = c[0] * tg[0] + c[1] * tg[1] + c[2] * tg[2];
            if (d > best) { best = d; bn[0] = c[0]; bn[1] = c[1]; bn[2] = c[2]; }
        }
        float ax[3] = { bn[1] * tg[2] - bn[2] * tg[1], bn[2] * tg[0] - bn[0] * tg[2], bn[0] * tg[1] - bn[1] * tg[0] };
        float al = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (al > 1e-5f) {
            float ang = atan2f(al, best), k = dt * 9 > 1 ? 1 : dt * 9, dR[3][3];
            for (int e = 0; e < 3; e++) ax[e] /= al;
            m3_axis(dR, ax, ang * k); m3_mul(dice_Rm, dR, dice_Rm);
        }
    }
    {   /* keep the matrix orthonormal */
        float *r0 = dice_Rm[0], *r1 = dice_Rm[1], *r2 = dice_Rm[2];
        float l = sqrtf(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]); for (int k = 0; k < 3; k++) r0[k] /= l;
        float d = r0[0] * r1[0] + r0[1] * r1[1] + r0[2] * r1[2]; for (int k = 0; k < 3; k++) r1[k] -= d * r0[k];
        l = sqrtf(r1[0] * r1[0] + r1[1] * r1[1] + r1[2] * r1[2]); for (int k = 0; k < 3; k++) r1[k] /= l;
        r2[0] = r0[1] * r1[2] - r0[2] * r1[1]; r2[1] = r0[2] * r1[0] - r0[0] * r1[2]; r2[2] = r0[0] * r1[1] - r0[1] * r1[0];
    }
    dice_hop += ((dice_roll ? 1.0f : 0.0f) - dice_hop) * (dt * 8 > 1 ? 1 : dt * 8);
}
static void dice_render(float cx, float cy, float s, int interactive) {
    dice_step();
    int hv = interactive ? hit(cx - s * 1.6f, cy - s * 1.8f, s * 3.2f, s * 3.6f) : 0;
    float sc0 = s * 1.32f * (hv && !dice_roll ? 1.14f : 1.0f);
    float hop = dice_roll ? fabsf(sinf(dice_since * 9.0f)) * s * 0.9f * dice_hop : 0;

    float V[3][3], M[3][3];
    dice_view(V); m3_mul(M, V, dice_Rm);

    float py = cy - hop;
    rrect(cx - sc0 * 0.85f, cy + s * 1.55f, sc0 * 1.7f, s * 0.36f, s * 0.18f, C(0, 0, 0, 0.30f * (1.0f - 0.45f * dice_hop)));   /* shadow */
    float PX[12][3];
    for (int i = 0; i < 12; i++) for (int k = 0; k < 3; k++) PX[i][k] = M[k][0] * D20.v[i][0] + M[k][1] * D20.v[i][1] + M[k][2] * D20.v[i][2];
    const float lx = -0.40f, ly = 0.70f, lz = 0.59f;
    float hx = lx, hy = ly, hz = lz + 1.0f, hl = sqrtf(hx * hx + hy * hy + hz * hz); hx /= hl; hy /= hl; hz /= hl;   /* highlight half vector */
    struct { float x[3], y[3]; float sh; int v; } fc[20];
    col gold = mix(VC[0], C(1.0f, 0.86f, 0.55f, 1), 0.70f);
    for (int f = 0; f < 20; f++) {
        float n[3], u[3], w[3];
        for (int k = 0; k < 3; k++) {
            n[k] = M[k][0] * D20.n[f][0] + M[k][1] * D20.n[f][1] + M[k][2] * D20.n[f][2];
            u[k] = M[k][0] * D20.u[f][0] + M[k][1] * D20.u[f][1] + M[k][2] * D20.u[f][2];
            w[k] = M[k][0] * D20.w[f][0] + M[k][1] * D20.w[f][1] + M[k][2] * D20.w[f][2];
        }
        fc[f].v = n[2] > 0.02f;
        if (!fc[f].v) continue;
        float dl = n[0] * lx + n[1] * ly + n[2] * lz, nh = n[0] * hx + n[1] * hy + n[2] * hz;
        float sh = 0.28f + 0.72f * (dl > 0 ? dl : 0), spec = nh > 0 ? powf(nh, 26.0f) : 0;
        fc[f].sh = sh;
        for (int c = 0; c < 3; c++) {
            float pz = 1.0f / (1.0f - 0.10f * PX[D20.tri[f][c]][2]);
            fc[f].x[c] = cx + PX[D20.tri[f][c]][0] * sc0 * pz; fc[f].y[c] = py - PX[D20.tri[f][c]][1] * sc0 * pz;
        }
        col body = mix(C(0.05f, 0.06f, 0.15f, 0.98f), mix(C(0.22f, 0.20f, 0.52f, 0.98f), VC[0], 0.25f), sh);
        body = mix(body, C(1, 1, 1, 0.98f), spec * 0.55f);
        tri(fc[f].x[0], fc[f].y[0], fc[f].x[1], fc[f].y[1], fc[f].x[2], fc[f].y[2], body);
        float gx = (fc[f].x[0] + fc[f].x[1] + fc[f].x[2]) / 3, gy = (fc[f].y[0] + fc[f].y[1] + fc[f].y[2]) / 3, q = 0.80f, ix[3], iy[3];
        for (int c = 0; c < 3; c++) { ix[c] = gx + (fc[f].x[c] - gx) * q; iy[c] = gy + (fc[f].y[c] - gy) * q; }   /* inset facet: a gem-like bevel */
        col inner = mix(body, mix(C(0.45f, 0.50f, 0.95f, 0.98f), VC[0], 0.3f), 0.20f + 0.20f * sh);
        tri(ix[0], iy[0], ix[1], iy[1], ix[2], iy[2], inner);
    }
    set_additive(1);                                          /* glow, number engraving and edges */
    blob(cx, py, s * 1.7f, alpha(VC[0], 0.10f + 0.16f * dice_hop + (hv ? 0.10f : 0.0f)));
    for (int f = 0; f < 20; f++) {
        if (!fc[f].v) continue;
        float n[3], u[3], w[3], P[3];
        for (int k = 0; k < 3; k++) {
            n[k] = M[k][0] * D20.n[f][0] + M[k][1] * D20.n[f][1] + M[k][2] * D20.n[f][2];
            u[k] = M[k][0] * D20.u[f][0] + M[k][1] * D20.u[f][1] + M[k][2] * D20.u[f][2];
            w[k] = M[k][0] * D20.w[f][0] + M[k][1] * D20.w[f][1] + M[k][2] * D20.w[f][2];
            P[k] = n[k] * 0.795f;                                       /* centre of the face (inradius of the unit icosahedron) */
        }
        float xy[8] = { fc[f].x[0], fc[f].y[0], fc[f].x[1], fc[f].y[1], fc[f].x[2], fc[f].y[2], fc[f].x[0], fc[f].y[0] };
        polyline(xy, 4, 0.9f, 3.0f, alpha(mix(VC[0], C(1, 1, 1, 1), 0.35f), 0.25f + 0.55f * fc[f].sh));
        float vis = n[2] > 0.15f ? 1.0f : (n[2] - 0.02f) / 0.13f;
        if (sc0 > 30) d20_number(D20.num[f], P, u, w, 0.30f, sc0, cx, py, 0.5f + 0.012f * sc0, alpha(gold, (0.45f + 0.55f * fc[f].sh) * vis), 0);
    }
    set_additive(0);
    if (interactive && !ma_busy() && clicked(cx - s * 1.6f, cy - s * 1.8f, s * 3.2f, s * 3.6f)) { ma_random(); click = 0; }
}
static void draw_dice(float cx, float cy, float s) { dice_render(cx, cy, s, 1); }

/* While a random module is being fetched the dice dances over the whole screen, big: it flies a loop that covers the screen, tumbling and
 * hopping, on a dimmed screen. It grows in when the roll starts and shrinks away when the module has arrived (it settles on a face). */
static float dice_big;
static void draw_dice_big(float W, float H) {
    float dt = (float)g_dt > 0.1f ? 0.1f : (float)g_dt;
    dice_big += ((dice_roll ? 1.0f : 0.0f) - dice_big) * (dt * 5 > 1 ? 1 : dt * 5);
    if (dice_big < 0.02f) return;
    dice_step();
    float m = W < H ? W : H, s = 0.15f * m * dice_big, t = dice_t;
    rect(0, 0, W, H, C(0.02f, 0.03f, 0.07f, 0.55f * dice_big));
    float ax = W * 0.5f - s * 1.9f, ay = H * 0.5f - s * 2.2f;
    if (ax < 0) ax = 0;
    if (ay < 0) ay = 0;
    float cx = W * 0.5f + ax * sinf(0.95f * t + 0.5f * sinf(0.37f * t)), cy = H * 0.5f + ay * sinf(1.35f * t + 1.0f);
    dice_render(cx, cy, s, 0);
}

#ifdef __EMSCRIPTEN__
EM_JS(int, web_rand_get, (void), { try { return localStorage.getItem('ngp.random') === '1' ? 1 : 0; } catch (e) { return 0; } });
EM_JS(void, web_rand_set, (int v), { try { localStorage.setItem('ngp.random', v ? '1' : '0'); } catch (e) {} });
#endif
/* RND chip next to the dice: switches the random mode (see next_user) */
static void draw_rand_chip(float x, float y, float w) {
    const float h = 22;
    int hv = hit(x, y, w, h);
    rrect(x, y, w, h, 7, alpha(g_rand ? VC[0] : (hv ? VC[2] : EDGE), g_rand ? 0.32f : 0.30f));
    text(x + (w - text_width("RND", 11)) / 2, y + 5.5f, 11, g_rand ? TXT : DIM, "RND");
    if (clicked(x, y, w, h)) {
        g_rand = !g_rand;
#ifdef __EMSCRIPTEN__
        web_rand_set(g_rand);
#endif
        snprintf(toast_txt, sizeof toast_txt, g_rand ? "Random mode on" : "Random mode off"); toast_t = 1.8f;
        click = 0;
    }
}

/* ---------------------------------------------------------------- The-Mod-Archive-Dialog (Download per ID oder Link) */
static int ma_open, ma_eat;
static char ma_buf[256]; static int ma_len;
static char ma_msg[160]; static int ma_msg_err;
static volatile int dl_state;              /* 0 leer, 1 laedt, 2 fertig, 3 Fehler */
static int dl_id, dl_rc, dl_random; static char dl_path[1100]; static SDL_Thread *dl_thr;   /* dl_random: the dice asked for it, dl_id 0 = ID not known yet */
static int ma_loading(void) { return dl_state == 1; }
static int ma_busy(void) { return dl_state != 0; }   /* running, or finished and not yet taken over by ma_poll (a new draw must not discard that result) */
static int dl_want = -1;                 /* entry that asked for its file (restored list), played when the download is done */
#ifdef __EMSCRIPTEN__
static void ma_need_download(int idx) {
    if (dl_state == 1) { snprintf(toast_txt, sizeof toast_txt, "Another download is running"); toast_t = 2.0f; return; }
    dl_id = pl[idx].ma_id; dl_want = idx; dl_random = 0; dl_state = 1;
    snprintf(toast_txt, sizeof toast_txt, "Loading #%d ...", dl_id); toast_t = 4.0f;
    EM_ASM({ if (window.ngMaFetch) window.ngMaFetch($0); }, dl_id);
}
#endif

static int dl_thread(void *p) {
    if (!dl_id) {                                          /* random module: ask The Mod Archive for an ID first; a draw that fails is repeated with a new ID (up to 3 times, not after 40 s) */
        Uint32 t0 = SDL_GetTicks();
        for (int tries = 0; tries < 3 && SDL_GetTicks() - t0 < 40000 && !net_cancel; tries++) {
            int id = ma_random_id();
            if (!id) { fprintf(stderr, "ngplayer: random draw %d: no ID (%u ms)\n", tries + 1, SDL_GetTicks() - t0); net_log("random draw try %d: no ID (%u ms)", tries + 1, SDL_GetTicks() - t0); continue; }
            int rc = ma_download(id, dl_path, sizeof dl_path);
            if (!rc) { net_log("random draw try %d: #%d ok (%u ms)", tries + 1, id, SDL_GetTicks() - t0); dl_id = id; dl_rc = 0; dl_state = 2; return 0; }
            fprintf(stderr, "ngplayer: random draw %d: #%d failed (code %d, %u ms)\n", tries + 1, id, rc, SDL_GetTicks() - t0);
            net_log("random draw try %d: #%d failed, code %d (%u ms)", tries + 1, id, rc, SDL_GetTicks() - t0);
        }
        net_log("random draw: given up after %u ms%s", SDL_GetTicks() - t0, net_cancel ? " (cancelled)" : "");
        dl_rc = 28; dl_state = 3; return 0;
    }
    dl_rc = ma_download(dl_id, dl_path, sizeof dl_path);
    dl_state = dl_rc ? 3 : 2;
    return 0;
}
static void ma_show(int on) {
    ma_open = on; ma_eat = on; ma_msg[0] = 0; ma_msg_err = 0;
    if (on) SDL_StartTextInput(); else SDL_StopTextInput();
}
static void ma_start(void) {
    if (dl_state != 0) return;
    int id = ma_parse_id(ma_buf);
    if (!id) { snprintf(ma_msg, sizeof ma_msg, "No module ID found. Expected: a number or a modarchive link."); ma_msg_err = 1; return; }
#ifdef __EMSCRIPTEN__
    /* Web: der Download laeuft ueber die Webseite (fetch auf ma.php), Ergebnis kommt per ng_web_ma_done() zurueck */
    dl_id = id; dl_state = 1;
    snprintf(ma_msg, sizeof ma_msg, "Downloading module #%d ...", id); ma_msg_err = 0;
    EM_ASM({ if (window.ngMaFetch) window.ngMaFetch($0); }, id);
    return;
#endif
    if (!net_have_curl()) { snprintf(ma_msg, sizeof ma_msg, "curl not found - please install it."); ma_msg_err = 1; return; }
    dl_id = id; dl_state = 1;
    snprintf(ma_msg, sizeof ma_msg, "Downloading module #%d ...", id); ma_msg_err = 0;
    net_cancel = 0; dl_thr = SDL_CreateThread(dl_thread, "download", NULL);
}
static void ma_random(void) {
    if (dl_state != 0) return;
#ifdef __EMSCRIPTEN__
    dl_id = 0; dl_random = 1; dl_state = 1; dice_start();
    EM_ASM({ if (window.ngMaRandom) window.ngMaRandom(); });          /* the page asks ma.php for an ID, then fetches the module */
    return;
#endif
    if (!net_have_curl()) { snprintf(toast_txt, sizeof toast_txt, "curl not found - please install it"); toast_t = 3.0f; return; }
    dl_id = 0; dl_random = 1; dl_state = 1; dice_start();
    net_log("random draw started (playlist %d, current %d, song ended %d)", npl, cur, g_ended);
    net_cancel = 0; dl_thr = SDL_CreateThread(dl_thread, "download", NULL);
}
static void ma_poll(void) {
#ifdef __EMSCRIPTEN__
    {   /* safety net: the page should always answer (it has its own time limits); if it never does, do not stay "loading" for ever */
        static Uint32 busy_since;
        if (dl_state != 1) busy_since = 0;
        else if (!busy_since) busy_since = SDL_GetTicks() | 1;
        else if (SDL_GetTicks() - busy_since > 75000) { dl_rc = 28; dl_state = 3; busy_since = 0; }
    }
#else
    {   /* Linux: the same safety net; a download that has not finished after 60 s is cancelled (kills the curl process, the thread then returns) */
        static Uint32 busy_since;
        if (dl_state != 1) busy_since = 0;
        else if (!busy_since) busy_since = SDL_GetTicks() | 1;
        else if (SDL_GetTicks() - busy_since > 60000 && !net_cancel) { fprintf(stderr, "ngplayer: download took more than 60 s, cancelled\n"); net_log("download took more than 60 s, cancelled"); net_cancel = 1; }
        else if (SDL_GetTicks() - busy_since > 75000) { fprintf(stderr, "ngplayer: download thread does not answer, given up\n"); net_log("download thread does not answer after 75 s, given up"); if (dl_thr) SDL_DetachThread(dl_thr); dl_thr = NULL; dl_rc = 28; dl_state = 3; busy_since = 0; }
    }
#endif
    if (dl_state == 2 || dl_state == 3) {
        if (dl_thr) SDL_WaitThread(dl_thr, NULL);
        dl_thr = NULL;
        if (dl_random) {
            dice_stop_req = 1;                                     /* the dice settles */
            if (dl_state == 3) { snprintf(toast_txt, sizeof toast_txt, "Mod Archive did not answer"); toast_t = 3.0f; rand_fail_t = SDL_GetTicks(); }
            dl_random = 0;
        }
        if (dl_state == 2) {
            int j = -1;                                            /* the module is already in the list (restored entry, or loaded before): reuse it */
            if (dl_want >= 0 && dl_want < npl && pl[dl_want].ma_id == dl_id) j = dl_want;
            else for (int i = 0; i < npl; i++) if (pl[i].ma_id == dl_id) { j = i; break; }
            if (j >= 0) { free(pl[j].path); pl[j].path = strdup(dl_path); pl[j].fmt = -1; }
            else {
                add_file(dl_path);
                pl[npl - 1].ma_id = dl_id;
                snprintf(pl[npl - 1].label, sizeof pl[npl - 1].label, "ModArchive #%d", dl_id);
                j = npl - 1;
            }
            play_index(j); list_follow = 1;
            net_log("module #%d (%s) %s", dl_id, pl[j].path, g_song ? "playing" : g_error);
            if (g_song) { const char *tt = song_title(g_song); if (tt && tt[0]) snprintf(pl[j].label, sizeof pl[j].label, "%s", tt); }   /* Titel des Lieds statt der ID anzeigen */
            ma_buf[0] = 0; ma_len = 0; ma_show(0);
#ifdef __EMSCRIPTEN__
            ma_sync_web(1);
#endif
        } else {
            if (dl_want >= 0) { snprintf(toast_txt, sizeof toast_txt, "Mod Archive did not answer"); toast_t = 3.0f; }
            net_log("download #%d failed, code %d", dl_id, dl_rc);
            const char *m = dl_rc == 100 ? "Server returned no module file (does the ID exist?)." : dl_rc == 22 ? "Module not found (server error)." : (dl_rc == 6 || dl_rc == 7 || dl_rc == 28) ? "Cannot reach the server." : "Download failed.";
            snprintf(ma_msg, sizeof ma_msg, "%s (Code %d)", m, dl_rc); ma_msg_err = 1;
        }
        dl_want = -1;
        dl_state = 0;
    }
}
static void ma_key(const SDL_KeyboardEvent *k) {
    switch (k->keysym.sym) {
    case SDLK_ESCAPE: ma_show(0); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: ma_start(); break;
    case SDLK_BACKSPACE: if (ma_len > 0) ma_buf[--ma_len] = 0; break;
    case SDLK_r: if (k->keysym.mod & KMOD_CTRL) { ma_show(0); ma_random(); } break;
    case SDLK_v:
#ifndef __EMSCRIPTEN__   /* Web: Einfuegen laeuft ueber das paste-Ereignis der Seite (ng_web_paste) */
        if (k->keysym.mod & KMOD_CTRL) {
            char *c = SDL_GetClipboardText();
            if (c) { for (char *p = c; *p && ma_len < (int)sizeof ma_buf - 1; p++) if (*p >= ' ') ma_buf[ma_len++] = *p; ma_buf[ma_len] = 0; SDL_free(c); }
        }
#endif
        break;
    default: break;
    }
}
static void ma_text(const char *t) {
    if (ma_eat) { ma_eat = 0; if (t[0] == 'o' || t[0] == 'O') return; }
    for (; *t && ma_len < (int)sizeof ma_buf - 1; t++) if ((unsigned char)*t >= ' ') ma_buf[ma_len++] = *t;
    ma_buf[ma_len] = 0;
}
static void draw_ma_dialog(double t) {
    if (!ma_open) return;
    rect(0, 0, view_w, view_h, C(0, 0, 0, 0.60f));
    float w = 640, h = 168, x = (view_w - w) / 2, y = (view_h - h) / 2 - 20;
    card(x, y, w, h, 0);
    text(x + 24, y + 18, 12, DIM, "THE MOD ARCHIVE");
    text(x + 24, y + 38, 14, TXT, "Paste a module ID or link");
    float bx = x + 24, by = y + 70, bw = w - 48, bh = 36;
    rrect(bx, by, bw, bh, 8, alpha(EDGE, 0.5f));
    rrect(bx + 1, by + 1, bw - 2, bh - 2, 7, BG1);
    const char *s = ma_buf;
    while (*s && text_width(s, 15) > bw - 36) s++;
    text(bx + 12, by + 9, 15, TXT, s);
    if (dl_state != 1 && ((int)(t * 2) & 1)) rect(bx + 12 + text_width(s, 15) + 2, by + 8, 2, 20, VC[0]);
    if (ma_msg[0]) text(x + 24, y + 120, 13, ma_msg_err ? WARN : VC[0], ma_msg);
    else text(x + 24, y + 120, 12, FAINT, "Enter downloads  \xb7  Ctrl+V paste  \xb7  Ctrl+R random  \xb7  Esc close");
}

static void draw_ui_portrait(float W, float H);
static void draw_ui(double t) {
    (void)t;
    float W = view_w, H = view_h;
    rect_grad(0, 0, W, H, BG1, BG0);
    float act = (g_song && !g_paused && !g_ended) ? 1.0f : 0.0f;
    glow_l += (act * (0.3f + vu[0]) - glow_l) * 0.12f;
    glow_r += (act * (0.3f + vu[1]) - glow_r) * 0.12f;
    if (!g_mobile) {                       /* grosse Leuchtflecken kosten viel Fuellrate: auf dem Handy weglassen */
        set_additive(1);
        blob(W * 0.25f, -30, 380, alpha(VC[0], 0.06f + 0.05f * glow_l));
        blob(W * 0.60f, -60, 420, alpha(VC[1], 0.05f + 0.05f * glow_r));
        blob(W * 0.92f, H * 0.9f, 380, alpha(VC[2], 0.04f + 0.03f * (glow_l + glow_r)));
        set_additive(0);
    }

    if (g_portrait) { draw_ui_portrait(W, H); return; }
    const float M = 16, SB = 288, BOT = 64;
    /* ---------------- Playlist */
    {
        float x = M, y = M, w = SB, h = H - 2 * M;
        card(x, y, w, h, 0);
        text(x + 18, y + 14, 13, TXT, "PLAYLIST");
        char cnt[32]; snprintf(cnt, sizeof cnt, "%d", npl);
        text_right(x + w - 18, y + 14, 13, DIM, cnt);
        pl_build_vis();
        if (npl) pl_tabs(x, y + 40, w, 0, 0);
        float ry = y + (npl ? 76 : 44), rh = 28, area = h - (npl ? 76 : 44) - 66;
        int vis_rows = (int)(area / rh), maxscroll = nvis > vis_rows ? nvis - vis_rows : 0;
        if (list_follow && cvis >= 0) {
            if (cvis < list_scroll) list_scroll = (float)cvis;
            if (cvis >= list_scroll + vis_rows) list_scroll = (float)(cvis - vis_rows + 1);
            list_follow = 0;
        } else if (list_follow) list_follow = 0;
        if (list_scroll > maxscroll) list_scroll = (float)maxscroll;
        if (list_scroll < 0) list_scroll = 0;
        int first = (int)list_scroll;
        for (int r = 0; r < vis_rows + 1 && first + r < nvis; r++) {
            int i = pl_vis[first + r];
            float yy = ry + r * rh;
            if (yy + rh > y + h - 62) break;
            int isc = i == cur, hv = hit(x + 8, yy, w - 16, rh);
            if (isc) {
                rrect(x + 8, yy, w - 16, rh - 2, 8, alpha(VC[0], 0.16f));
                rrect(x + 8, yy + 5, 3, rh - 12, 1.5f, VC[0]);
            } else if (hv) rrect(x + 8, yy, w - 16, rh - 2, 8, alpha(EDGE, 0.25f));
            char lab[160], tg[24]; list_label(lab, sizeof lab, tg, sizeof tg, &pl[i]);
            float tx = x + 20;
            tx += draw_fmt_badge(tx, yy + 6, &pl[i], isc);      /* file format in its own colour */
            float tgw = tg[0] ? text_width(tg, 14) : 0;
            float mob = g_mobile ? MOB_BTN_W - 18 : 0, bx = x + w - 8 - MOB_BTN_W;       /* touch devices: the menu button of the phone list instead of the hover x */
            fit_text(lab, w - 74 - mob - (tx - (x + 20)) - tgw, 14);
            text(tx, yy + 5, 14, isc ? TXT : DIM, lab);
            if (tg[0]) text_right(x + w - 44 - mob, yy + 5, 14, VC[1], tg);      /* #ID, left of the remove / menu button */
            int xhv = hit(x + w - 38, yy + 2, 26, rh - 6);
            if (g_mobile) {
                rrect(bx, yy + 2, MOB_BTN_W, rh - 8, 8, alpha(EDGE, 0.40f));
                for (int d = -1; d <= 1; d++) rrect(bx + MOB_BTN_W * 0.5f - 2 + d * 8, yy + 2 + (rh - 8) * 0.5f - 2, 4, 4, 2, isc ? TXT : DIM);   /* three dots */
                if (click && clickx >= bx - 4 && clickx < x + w - 4 && clicky >= yy && clicky < yy + rh) {      /* tap on the button: open the menu of this entry, do not play it */
                    ctx_idx = i; click = 0;
                    ctx_x = bx + MOB_BTN_W - CTX_W; ctx_y = yy + (rh - 8) * 0.5f;
                    if (ctx_x < 4) ctx_x = 4;
                    if (ctx_x + CTX_W > view_w - 4) ctx_x = view_w - 4 - CTX_W;
                    if (ctx_y + CTX_H(i) > view_h - 4) ctx_y = view_h - 4 - CTX_H(i);
                }
            } else {
                if (hv) {               /* red x: remove from list */
                    if (xhv) rrect(x + w - 38, yy + 3, 26, rh - 8, 6, alpha(WARN, 0.22f));
                    text(x + w - 31, yy + 5, 15, xhv ? WARN : alpha(WARN, 0.55f), "\xd7");
                }
                if (hv && xhv && click && clickx >= x + w - 38 && clickx < x + w - 12 && clicky >= yy && clicky < yy + rh) { pl_remove(i); click = 0; break; }
            }
            if (rclick && rclickx >= x + 8 && rclickx < x + w - 8 && rclicky >= yy && rclicky < yy + rh) {
                ctx_idx = i; rclick = 0;
                ctx_x = (float)rclickx; ctx_y = (float)rclicky;
                if (ctx_x + CTX_W > view_w - 4) ctx_x = view_w - 4 - CTX_W;
                if (ctx_y + CTX_H(i) > view_h - 4) ctx_y = view_h - 4 - CTX_H(i);
            }
            if (clicked(x + 8, yy, w - 16, rh)) { play_index(i); click = 0; }
        }
        { char vb[96]; snprintf(vb, sizeof vb, "ngPlayer %s", NGPLAYER_VERSION); text(x + 18, y + h - 28, 11, FAINT, vb); }
        draw_dice(x + w - 44, y + h - 33, 12.5f);
        draw_rand_chip(x + w - 104, y + h - 44, 38);
        if (npl && !nvis) text(x + 18, ry + 6, 13, FAINT, g_pltab ? "Nothing loaded from The Mod Archive yet." : "No local files.");
        if (!npl) {
            text(x + 18, y + 60, 14, DIM, "No files.");
            {   /* word-wrapped format list: the sidebar can be narrow */
                const char *fl = "MOD, S3M, STM, PTM, ULT, 669, MTM, MED, PSM, FAR, MDL, XM, IT, DMF or TFMX";
                float ly = y + 84, maxw = w - 36;
                char ln[160]; int ll = 0; const char *p = fl;
                while (*p) {
                    const char *e = p; while (*e && *e != ' ') e++;
                    int wl = (int)(e - p);
                    char t[160]; int tl = ll;
                    memcpy(t, ln, (size_t)tl); if (tl && tl + 1 + wl < 158) t[tl++] = ' ';
                    if (tl + wl < 159) memcpy(t + tl, p, (size_t)wl);
                    t[tl + wl < 159 ? tl + wl : 159] = 0;
                    if (ll && text_width(t, 13) > maxw) { ln[ll] = 0; text(x + 18, ly, 13, FAINT, ln); ly += 18; ll = 0; if (wl < 158) { memcpy(ln, p, (size_t)wl); ll = wl; } }
                    else { memcpy(ln, t, (size_t)strlen(t)); ll = (int)strlen(t); }
                    p = *e ? e + 1 : e;
                }
                if (ll) { ln[ll] = 0; text(x + 18, ly, 13, FAINT, ln); ly += 18; }
                text(x + 18, ly + 2, 13, FAINT, "drop them on the window.");
            }
        }
    }

    float ax = M + SB + M, aw = W - ax - M;

    /* ---------------- Kopf */
    float hy = M, hh = 94, bw = 270, haw = aw - bw - M;
    card(ax, hy, haw, hh, 0);
    card(ax + haw + M, hy, bw, hh, 0);
    logo_waves(ax + haw + M, hy, bw, hh);
    draw_logo(ax + haw + M + 18, hy + 19, 56);
    text(ax + haw + M + 88, hy + 22, 28, TXT, "ngPlayer");
    text(ax + haw + M + 89, hy + 56, 12, DIM, TAGLINE);
    draw_seal(ax + haw + M + bw - 12, hy + 14, 24, SEAL_TEXT);
    {
        char title[160], sub[200];
        if (g_song) {
            const char *tt = song_title(g_song);
            snprintf(title, sizeof title, "%s", tt[0] ? tt : (cur >= 0 ? pl[cur].label : ""));
            snprintf(sub, sizeof sub, "%s", cur >= 0 ? pl[cur].label : "");
        } else {
            snprintf(title, sizeof title, "ngPlayer");
            snprintf(sub, sizeof sub, TAGLINE);
        }
        float chh = hh - 14, chw = chips_width(chh);      /* Soundchip-Grafik zwischen Titel und Zeit */
        float tmax = haw - 200 - (chw > 0 ? chw + 20 : 0);
        if (chw > 0) draw_chips(ax + haw - 22 - 130 - chw, hy + 7, chh);
        float tsize = 26;
        while (tsize > 18 && text_width(title, tsize) > tmax) tsize -= 2;
        fit_text(title, tmax, tsize);
        text(ax + 22, hy + 10, tsize, TXT, title);
        fit_text(sub, tmax, 14);
        if (!g_error[0]) text(ax + 22, hy + 14 + tsize + 2, 14, DIM, sub);
        if (g_song) {
            float cx = ax + 22, cy = hy + hh - 30;
            char b[48];
            cx += chip(cx, cy, song_format(g_song), VC[0], 1);
            snprintf(b, sizeof b, "%d CHANNELS", song_channels(g_song));
            cx += chip(cx, cy, b, VC[2], 1);
            snprintf(b, sizeof b, "%d ORDERS", song_norders(g_song));
            cx += chip(cx, cy, b, DIM, 0);
            int pf = song_paula_filter(g_song);
            if (pf >= 0) cx += chip(cx, cy, "PAULA-FILTER", pf ? VC[1] : DIM, pf);
            if (song_subsongs(g_song) > 1) { char sb[24]; snprintf(sb, sizeof sb, "SONG %d/%d", song_subsong(g_song) + 1, song_subsongs(g_song)); cx += chip(cx, cy, sb, VC[2], 1); }
        }
        if (g_error[0]) text(ax + 22, hy + 14 + tsize + 2, 13, WARN, g_error);
        char tb[32];
        mmss(tb, sizeof tb, (double)g_frames / g_rate);
        text_right(ax + haw - 22, hy + 10, 34, TXT, tb);
        if (g_song) {
            int o = g_view_valid ? g_view.order : -1;
            if (o >= 0) {
                char ob[48]; snprintf(ob, sizeof ob, "ORDER %d / %d", o + 1, song_norders(g_song));
                text_right(ax + haw - 22, hy + 48, 14, VC[0], ob);
            }
            if (g_paused) text_right(ax + haw - 22, hy + 66, 12, VC[2], "PAUSE");
            else if (g_ended) text_right(ax + haw - 22, hy + 66, 12, DIM, "ENDE");
        }
    }

    /* ---------------- Mitte (Platzhalter: Oszilloskop) und Spektrum/Pegel */
    float by = H - M - BOT, sh = 124, sy = by - M - sh;
    float cy0 = hy + hh + M;
    float oh = g_song ? overview_height(song_channels(g_song) > 64 ? 64 : song_channels(g_song)) : 60.0f;
    float avail = sy - M - cy0;
    if (avail - oh - M < 190) oh = avail - M - 190;
    view_update();
    overview_update((float)g_dt);
    draw_overview(ax, cy0, aw, oh);
    if (g_vmode == 1) draw_orb(ax, cy0 + oh + M, aw, avail - oh - M);
    else if (g_vmode == 3) draw_scopes(ax, cy0 + oh + M, aw, avail - oh - M);
    else if (g_vmode == 2) draw_voxel(ax, cy0 + oh + M, aw, avail - oh - M);
    else draw_pattern(ax, cy0 + oh + M, aw, avail - oh - M);
    if (g_song) g_decimal = (!strcmp(song_format(g_song), "S3M") || !strcmp(song_format(g_song), "STM") || !strcmp(song_format(g_song), "PTM") || !strcmp(song_format(g_song), "ULT") || !strcmp(song_format(g_song), "669") || !strcmp(song_format(g_song), "MED") || !strcmp(song_format(g_song), "PSM") || !strcmp(song_format(g_song), "IT"));
    float vuw = 300, spw = aw - vuw - M;
    draw_spectrum(ax, sy, spw, sh);
    draw_level(ax + spw + M, sy, vuw, sh);

    /* ---------------- Transport */
    {
        float bx = ax, bw = aw;
        card(bx, by, bw, BOT, 0);
        float cy = by + BOT / 2, r = 20;
        float bxs[3] = { bx + 22, bx + 74, bx + 126 };
        for (int i = 0; i < 3; i++) {
            float x = bxs[i];
            int hv = hit(x, cy - r, 2 * r, 2 * r);
            rrect(x, cy - r, 2 * r, 2 * r, r, alpha(hv ? VC[0] : EDGE, hv ? 0.30f : 0.35f));
            col ic = hv ? TXT : DIM;
            float ccx = x + r, ccy = cy;
            if (i == 0) { tri(ccx + 6, ccy - 8, ccx + 6, ccy + 8, ccx - 6, ccy, ic); rect(ccx - 9, ccy - 8, 3, 16, ic); }
            else if (i == 1) {
                if (g_paused || g_ended) tri(ccx - 6, ccy - 9, ccx - 6, ccy + 9, ccx + 9, ccy, ic);
                else { rect(ccx - 7, ccy - 8, 5, 16, ic); rect(ccx + 2, ccy - 8, 5, 16, ic); }
            } else { tri(ccx - 6, ccy - 8, ccx - 6, ccy + 8, ccx + 6, ccy, ic); rect(ccx + 6, ccy - 8, 3, 16, ic); }
            if (clicked(x, cy - r, 2 * r, 2 * r)) {
                if (i == 0) next_user(-1);
                else if (i == 2) next_user(1);
                else if (g_ended) { if (cur >= 0) play_index_sub(cur, g_open_sub); }
                else g_paused = !g_paused;
                click = 0;
            }
        }
        float vx = bx + bw - 250, vw = 200;
        float hmax = (vx - 56) - (bx + 190);        /* free room for the hints, left of the VOL label */
        {
            char l1[160] = "Space pause  \xb7  Left/Right seek (subsong)  \xb7  Up/Down track  \xb7  Shift+arrows channels", l2[160];
#ifdef __EMSCRIPTEN__
            snprintf(l2, sizeof l2, "O Mod Archive  \xb7  V view  \xb7  R restart  \xb7  F fullscreen  \xb7  F5/F6 A/V sync %.0f ms", g_lag_ms);
#else
            snprintf(l2, sizeof l2, "O Mod Archive  \xb7  V view  \xb7  R restart  \xb7  F fullscreen  \xb7  F5/F6 A/V sync %.0f ms  \xb7  Esc quit", g_lag_ms);
#endif
            fit_text(l1, hmax, 12); fit_text(l2, hmax, 12);
            text(bx + 190, cy - 20, 12, FAINT, l1);
            text(bx + 190, cy + 2, 12, FAINT, l2);
        }
        text(vx - 44, cy - 8, 12, DIM, "VOL");
        rrect(vx, cy - 3, vw, 6, 3, alpha(FAINT, 0.35f));
        rrect(vx, cy - 3, 6 + (vw - 6) * g_vol, 6, 3, VC[0]);
        float kx = vx + 3 + (vw - 6) * g_vol;
        rrect(kx - 8, cy - 8, 16, 16, 8, TXT);
        if (mouse_down && (hit(vx - 10, cy - 14, vw + 20, 28) || (click && clicked(vx - 10, cy - 14, vw + 20, 28)))) {
            g_vol = (mx - vx - 3) / (vw - 6);
            if (g_vol < 0) g_vol = 0;
            if (g_vol > 1) g_vol = 1;
        }
    }
}


/* ================================================================== Hochformat (Handy)
 * Virtuelle Breite 440: Kopf, Kanaluebersicht, Pattern/Orb, Spektrum + Pegel, Transport mit LIST-Knopf.
 * Die Playlist erscheint als Overlay (Tippen waehlt, Ziehen scrollt). */
static void draw_playlist_overlay(float x, float y, float w, float h) {
    static int pd, moved, was_open;
    static float py0, ls0;
    const float rh = 34;
    if (!was_open) { list_follow = 1; }
    was_open = 1;
    card(x, y, w, h, 0);
    text(x + 16, y + 14, 13, TXT, "PLAYLIST");
    char cnt[32]; snprintf(cnt, sizeof cnt, "%d", npl);
    text(x + 16 + text_width("PLAYLIST", 13) + 12, y + 14, 13, DIM, cnt);
    int tap = 0;
    if (mouse_down) {
        if (!pd) { pd = 1; py0 = my; ls0 = list_scroll; moved = 0; }
        if (fabsf(my - py0) > 8) moved = 1;
        if (moved) list_scroll = ls0 + (py0 - my) / rh;
    } else if (pd) { pd = 0; if (!moved && !lp_swallow) tap = 1; lp_swallow = 0; }
    float cbx = x + w - 92, cby = y + 8;
    int hvc = hit(cbx, cby, 78, 28);
    rrect(cbx, cby, 78, 28, 8, alpha(hvc ? VC[0] : EDGE, 0.35f));
    text(cbx + (78 - text_width("CLOSE", 12)) / 2, cby + 7, 12, TXT, "CLOSE");
    if (tap && mx >= cbx && mx < cbx + 78 && my >= cby && my < cby + 28) { g_plopen = 0; was_open = 0; return; }
    pl_build_vis();
    if (npl) pl_tabs(x, y + 46, w, 1, tap);
    float ry = y + (npl ? 80 : 48), area = h - (npl ? 80 : 48) - 8;
    int vis_rows = (int)(area / rh), maxscroll = nvis > vis_rows ? nvis - vis_rows : 0;
    if (list_follow && cvis >= 0) {
        if (cvis < list_scroll) list_scroll = (float)cvis;
        if (cvis >= list_scroll + vis_rows) list_scroll = (float)(cvis - vis_rows + 1);
        list_follow = 0;
    } else if (list_follow) list_follow = 0;
    if (list_scroll > maxscroll) list_scroll = (float)maxscroll;
    if (list_scroll < 0) list_scroll = 0;
    int first = (int)list_scroll;
    for (int r = 0; r < vis_rows && first + r < nvis; r++) {
        int i = pl_vis[first + r];
        float yy = ry + r * rh;
        int isc = i == cur;
        if (isc) {
            rrect(x + 8, yy, w - 16, rh - 3, 8, alpha(VC[0], 0.16f));
            rrect(x + 8, yy + 6, 3, rh - 15, 1.5f, VC[0]);
        }
        char lab[160], tg[24]; list_label(lab, sizeof lab, tg, sizeof tg, &pl[i]);
        float tx = x + 20;
        tx += draw_fmt_badge(tx, yy + 8, &pl[i], isc);          /* file format in its own colour */
        float tgw = tg[0] ? text_width(tg, 14) : 0;
        float mob = g_mobile ? MOB_BTN_W + 6 : 0, bx = x + w - 8 - MOB_BTN_W;       /* touch devices: a menu button at the right of every row */
        fit_text(lab, w - 40 - mob - (tx - (x + 20)) - tgw, 14);
        text(tx, yy + 8, 14, isc ? TXT : DIM, lab);
        if (tg[0]) text_right(x + w - 20 - mob, yy + 8, 14, VC[1], tg);      /* #ID flush right (left of the button) */
        if (g_mobile) {
            rrect(bx, yy + 3, MOB_BTN_W, rh - 9, 8, alpha(EDGE, 0.40f));
            for (int d = -1; d <= 1; d++) rrect(bx + MOB_BTN_W * 0.5f - 2 + d * 8, yy + 3 + (rh - 9) * 0.5f - 2, 4, 4, 2, isc ? TXT : DIM);   /* three dots */
            if (tap && !moved && mx >= bx - 4 && mx < x + w && my >= yy && my < yy + rh) {      /* tap on the button: open the menu of this entry, do not play it */
                ctx_idx = i; tap = 0;
                ctx_x = bx + MOB_BTN_W - CTX_W; ctx_y = yy + (rh - 9) * 0.5f;
                if (ctx_x < 4) ctx_x = 4;
                if (ctx_x + CTX_W > view_w - 4) ctx_x = view_w - 4 - CTX_W;
                if (ctx_y + CTX_H(i) > view_h - 4) ctx_y = view_h - 4 - CTX_H(i);
            }
        }
        if (rclick && !moved && rclickx >= x + 8 && rclickx < x + w - 8 && rclicky >= yy && rclicky < yy + rh) {       /* long press: Share/Info (Mod Archive) and Remove (every entry); not while dragging */
            ctx_idx = i; rclick = 0;
            ctx_x = (float)rclickx; ctx_y = (float)rclicky;
            if (ctx_x + CTX_W > view_w - 4) ctx_x = view_w - 4 - CTX_W;
            if (ctx_y + CTX_H(i) > view_h - 4) ctx_y = view_h - 4 - CTX_H(i);
        }
        if (tap && !moved && mx >= x + 8 && mx < x + w - 8 && my >= yy && my < yy + rh) { play_index(i); g_plopen = 0; was_open = 0; return; }
    }
    if (!npl) text(x + 16, y + 64, 14, DIM, "No files.");
    else if (!nvis) text(x + 16, ry + 6, 13, FAINT, g_pltab ? "Nothing loaded from The Mod Archive yet." : "No local files.");
}

/* ---- Order slider (phone layout): drag the knob to choose an order, the jump happens when the finger lifts (seek_order re-renders the
 * song up to the target, too slow to do on every move); the small buttons at both ends step one order back / forward. ---- */
#define SEEK_ROW 36.0f
static int seek_drag; static int seek_target;
static void draw_seek_row(float bx, float ty, float bw) {
    int n = song_norders(g_song), curo = g_view_valid ? g_view.order : song_order(g_song);
    if (curo < 0) curo = 0;
    if (curo > n - 1) curo = n - 1;
    float cy = ty + SEEK_ROW / 2 - 3, bwid = 30, x0 = bx + 12 + bwid + 10, x1 = bx + bw - 12 - bwid - 10, tw = x1 - x0;
    for (int i = 0; i < 2; i++) {               /* step buttons: ‹ and › */
        float x = i ? bx + bw - 12 - bwid : bx + 12;
        int hv = hit(x - 4, cy - 17, bwid + 8, 34);
        rrect(x, cy - 13, bwid, 26, 8, alpha(hv ? VC[0] : EDGE, hv ? 0.30f : 0.35f));
        if (i == 0) { tri(x + 18, cy - 6, x + 18, cy + 6, x + 10, cy, hv ? TXT : DIM); }
        else { tri(x + 12, cy - 6, x + 12, cy + 6, x + 20, cy, hv ? TXT : DIM); }
        if (clicked(x - 4, cy - 17, bwid + 8, 34)) { seek_order(curo + (i ? 1 : -1)); click = 0; }
    }
    int over = hit(x0 - 14, cy - 20, tw + 28, 40);
    if (!seek_drag && click && clicked(x0 - 14, cy - 20, tw + 28, 40)) { seek_drag = 1; click = 0; }
    if (seek_drag) {
        if (mouse_down) {
            float f = (mx - x0) / tw; if (f < 0) f = 0; if (f > 1) f = 1;
            seek_target = (int)(f * (float)(n - 1) + 0.5f);
        } else { seek_drag = 0; if (seek_target != curo) seek_order(seek_target); }
    }
    int shown = seek_drag ? seek_target : curo;
    float f = n > 1 ? (float)shown / (float)(n - 1) : 0, kx = x0 + tw * f;
    rrect(x0, cy - 3, tw, 6, 3, alpha(FAINT, 0.35f));
    rrect(x0, cy - 3, 6 + (tw - 6) * f, 6, 3, VC[1]);
    rrect(kx - 10, cy - 10, 20, 20, 10, (seek_drag || over) ? TXT : DIM);
    if (seek_drag) {                            /* the order under the finger, above the knob */
        char lb[24]; snprintf(lb, sizeof lb, "ORD %d", shown + 1);
        float lw = text_width(lb, 12) + 16, lx = kx - lw / 2;
        if (lx < bx + 6) lx = bx + 6;
        if (lx + lw > bx + bw - 6) lx = bx + bw - 6 - lw;
        rrect(lx, cy - 40, lw, 22, 7, alpha(CARDHI, 0.96f));
        text(lx + 8, cy - 36, 12, VC[1], lb);
    }
}

static void draw_ui_portrait(float W, float H) {
    const float M = 10, BOT = 64;
    float aw = W - 2 * M;
    /* ---------------- Kopf */
    float hy = M, hh = 92;
    card(M, hy, aw, hh, 0);
    {
        char title[160], sub[200];
        if (g_song) {
            const char *tt = song_title(g_song);
            snprintf(title, sizeof title, "%s", tt[0] ? tt : (cur >= 0 ? pl[cur].label : ""));
            snprintf(sub, sizeof sub, "%s", cur >= 0 ? pl[cur].label : "");
        } else {
            snprintf(title, sizeof title, "ngPlayer");
            snprintf(sub, sizeof sub, TAGLINE);
        }
        float tmax = aw - 32 - 96;
        float tsize = 22;
        while (tsize > 15 && text_width(title, tsize) > tmax) tsize -= 1;
        fit_text(title, tmax, tsize);
        text(M + 16, hy + 10, tsize, TXT, title);
        fit_text(sub, tmax, 13);
        if (g_error[0]) text(M + 16, hy + 14 + tsize, 12, WARN, g_error);
        else text(M + 16, hy + 14 + tsize, 13, DIM, sub);
        char tb[32];
        mmss(tb, sizeof tb, (double)g_frames / g_rate);
        text_right(M + aw - 16, hy + 10, 28, TXT, tb);
        if (g_song) {
            int o = g_view_valid ? g_view.order : -1;
            if (o >= 0) { char ob[48]; snprintf(ob, sizeof ob, "%d / %d", o + 1, song_norders(g_song)); text_right(M + aw - 16, hy + 44, 13, VC[0], ob); }
            if (g_paused) text_right(M + aw - 16, hy + 62, 12, VC[2], "PAUSE");
            else if (g_ended) text_right(M + aw - 16, hy + 62, 12, DIM, "ENDE");
            float cx = M + 16, cy = hy + hh - 28;
            char b[48];
            cx += chip(cx, cy, song_format(g_song), VC[0], 1);
            snprintf(b, sizeof b, "%d CH", song_channels(g_song));
            cx += chip(cx, cy, b, VC[2], 1);
            if (song_subsongs(g_song) > 1) { snprintf(b, sizeof b, "SONG %d/%d", song_subsong(g_song) + 1, song_subsongs(g_song)); cx += chip(cx, cy, b, VC[2], 1); }
        }
    }
    /* ---------------- Mitte */
    int nosp = g_song && song_channels(g_song) > 8;      /* many voices: drop spectrum/level, the room goes to channels + pattern */
    int has_seek = g_song && song_norders(g_song) > 1;   /* order slider row above the buttons */
    float botH = BOT + (has_seek ? SEEK_ROW : 0);
    float by = H - M - botH, sh = nosp ? 0 : 96, sy = nosp ? by : by - M - sh, cy0 = hy + hh + M;
    float oh = g_song ? overview_height(song_channels(g_song) > 64 ? 64 : song_channels(g_song)) : 60.0f;
    float avail = sy - M - cy0;
    if (avail - oh - M < 150) oh = avail - M - 150;   /* channel list keeps priority, the pattern may shrink */
    if (oh < 40) oh = 40;
    view_update();
    overview_update((float)g_dt);
    if (g_song) g_decimal = (!strcmp(song_format(g_song), "S3M") || !strcmp(song_format(g_song), "STM") || !strcmp(song_format(g_song), "PTM") || !strcmp(song_format(g_song), "ULT") || !strcmp(song_format(g_song), "669") || !strcmp(song_format(g_song), "MED") || !strcmp(song_format(g_song), "PSM") || !strcmp(song_format(g_song), "IT"));
    if (g_plopen) {
        draw_playlist_overlay(M, cy0, aw, sy + sh - cy0);   /* the transport bar below (LIST) still gets its click */
    } else {
        draw_overview(M, cy0, aw, oh);
        draw_seal(M + aw - 12, cy0 + 6, 21, SEAL_TEXT);              /* portrait: on the top-right corner of the channels card */
        if (g_vmode == 1) draw_orb(M, cy0 + oh + M, aw, avail - oh - M);
        else if (g_vmode == 3) draw_scopes(M, cy0 + oh + M, aw, avail - oh - M);
        else if (g_vmode == 2) draw_voxel(M, cy0 + oh + M, aw, avail - oh - M);
        else draw_pattern(M, cy0 + oh + M, aw, avail - oh - M);
        draw_rand_chip(M + aw - 56, cy0 + oh + M + (avail - oh - M) - 34, 44);       /* RND in the lower right corner of the pattern / orb card */
        float vuw = 130, spw = aw - vuw - M;
        if (!nosp) { draw_spectrum(M, sy, spw, sh); draw_level(M + spw + M, sy, vuw, sh); }
    }
    /* ---------------- Transport */
    {
        float bx = M, bw = aw;
        card(bx, by, bw, botH, 0);
        float cy = by + botH - BOT / 2, r = 20;
        if (has_seek) draw_seek_row(bx, by + 6, bw);
        float bxs[3] = { bx + 12, bx + 62, bx + 112 };
        for (int i = 0; i < 3; i++) {
            float x = bxs[i];
            int hv = hit(x, cy - r, 2 * r, 2 * r);
            rrect(x, cy - r, 2 * r, 2 * r, r, alpha(hv ? VC[0] : EDGE, hv ? 0.30f : 0.35f));
            col ic = hv ? TXT : DIM;
            float ccx = x + r, ccy = cy;
            if (i == 0) { tri(ccx + 6, ccy - 8, ccx + 6, ccy + 8, ccx - 6, ccy, ic); rect(ccx - 9, ccy - 8, 3, 16, ic); }
            else if (i == 1) {
                if (g_paused || g_ended) tri(ccx - 6, ccy - 9, ccx - 6, ccy + 9, ccx + 9, ccy, ic);
                else { rect(ccx - 7, ccy - 8, 5, 16, ic); rect(ccx + 2, ccy - 8, 5, 16, ic); }
            } else { tri(ccx - 6, ccy - 8, ccx - 6, ccy + 8, ccx + 6, ccy, ic); rect(ccx + 6, ccy - 8, 3, 16, ic); }
            if (clicked(x, cy - r, 2 * r, 2 * r)) {
                if (i == 0) next_user(-1);
                else if (i == 2) next_user(1);
                else if (g_ended) { if (cur >= 0) play_index_sub(cur, g_open_sub); }
                else g_paused = !g_paused;
                click = 0;
            }
        }
        {   /* Playlist-Overlay ein/aus */
            float lx = bx + 166, lw = 58;
            int hv = hit(lx, cy - 16, lw, 32);
            rrect(lx, cy - 16, lw, 32, 9, alpha(g_plopen ? VC[2] : (hv ? VC[0] : EDGE), g_plopen ? 0.28f : 0.35f));
            text(lx + (lw - text_width("LIST", 12)) / 2, cy - 8, 12, g_plopen ? VC[2] : TXT, "LIST");
            if (clicked(lx, cy - 16, lw, 32)) { g_plopen = !g_plopen; click = 0; }
        }
        draw_dice(bx + 266, cy, 13.0f);           /* random dice (portrait: between LIST and the volume slider), big enough for a finger */
        float vx = bx + 306, vw = bw - 306 - 22;
        rrect(vx, cy - 3, vw, 6, 3, alpha(FAINT, 0.35f));
        rrect(vx, cy - 3, 6 + (vw - 6) * g_vol, 6, 3, VC[0]);
        float kx = vx + 3 + (vw - 6) * g_vol;
        rrect(kx - 9, cy - 9, 18, 18, 9, TXT);
        if (mouse_down && (hit(vx - 12, cy - 18, vw + 24, 36) || (click && clicked(vx - 12, cy - 18, vw + 24, 36)))) {
            g_vol = (mx - vx - 3) / (vw - 6);
            if (g_vol < 0) g_vol = 0;
            if (g_vol > 1) g_vol = 1;
        }
    }
}

/* ================================================================== Start */
static void save_screenshot(const char *file, int w, int h) {
    uint8_t *px = (uint8_t *)malloc((size_t)w * (size_t)h * 4), *fl = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (int y = 0; y < h; y++) memcpy(fl + (size_t)y * (size_t)w * 4, px + (size_t)(h - 1 - y) * (size_t)w * 4, (size_t)w * 4);
    SDL_Surface *sf = SDL_CreateRGBSurfaceFrom(fl, w, h, 32, w * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (sf) { SDL_SaveBMP(sf, file); SDL_FreeSurface(sf); }
    free(px); free(fl);
}

static int gl_compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, sizeof log, NULL, log); fprintf(stderr, "Shader error: %s\n", log); return 0; }
    return (int)sh;
}

/* ---------------------------------------------------------------- Hauptschleife (als Funktion, damit sie auch im Browser laeuft) */
#ifdef __EMSCRIPTEN__
#define AUDIO_SAMPLES 2048
#else
#define AUDIO_SAMPLES 1024
#endif
static SDL_Window *win;
static SDL_GLContext ctx;
static const char *shot;
static int shot_frames, vsync_ok, running, frame, fullscreen;
static Uint64 t0, tlast;
static double freq;

#ifndef __EMSCRIPTEN__
/* Einstellungen: $XDG_CONFIG_HOME/ngplayer/settings.ini (sonst ~/.config/ngplayer/settings.ini), Zeilen "key=value" */
static int g_win_w = 1280, g_win_h = 860, g_win_x = SDL_WINDOWPOS_CENTERED, g_win_y = SDL_WINDOWPOS_CENTERED;
static int settings_path(char *out, size_t n, int mkdir_p) {
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    char dir[900];
    if (x && *x) snprintf(dir, sizeof dir, "%s/ngplayer", x);
    else if (h && *h) snprintf(dir, sizeof dir, "%s/.config/ngplayer", h);
    else return 0;
    if (mkdir_p) {
        char par[900]; snprintf(par, sizeof par, "%s", dir);
        char *sl = strrchr(par, '/'); if (sl) { *sl = 0; mkdir(par, 0755); }
        mkdir(dir, 0755);
    }
    snprintf(out, n, "%s/settings.ini", dir);
    return 1;
}
static void settings_load(void) {
    char path[1000]; FILE *f;
    if (!settings_path(path, sizeof path, 0) || !(f = fopen(path, "r"))) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "window_w=%d", &v) == 1 && v >= 360 && v <= 16384) g_win_w = v;
        else if (sscanf(line, "window_h=%d", &v) == 1 && v >= 480 && v <= 16384) g_win_h = v;
        else if (sscanf(line, "orb=%d", &v) == 1) g_vmode = v != 0;                  /* older settings files */
        else if (sscanf(line, "view=%d", &v) == 1 && v >= 0 && v <= 3) g_vmode = v;
        else if (sscanf(line, "random=%d", &v) == 1) g_rand = v != 0;
        else if (sscanf(line, "window_x=%d", &v) == 1) g_win_x = v;
        else if (sscanf(line, "window_y=%d", &v) == 1) g_win_y = v;
    }
    fclose(f);
    if (g_win_x != SDL_WINDOWPOS_CENTERED || g_win_y != SDL_WINDOWPOS_CENTERED) {   /* only use the position if it is still on a screen (monitor unplugged?) */
        int ok = 0;
        for (int d = 0; d < SDL_GetNumVideoDisplays() && !ok; d++) {
            SDL_Rect r;
            if (!SDL_GetDisplayBounds(d, &r) && g_win_x + 100 >= r.x && g_win_x + 100 < r.x + r.w && g_win_y + 20 >= r.y && g_win_y + 20 < r.y + r.h) ok = 1;
        }
        if (!ok) g_win_x = g_win_y = SDL_WINDOWPOS_CENTERED;
    }
}
/* Mod Archive list: one "id<TAB>title" per line in modarchive.list next to settings.ini. Entries are restored from the download cache; ones whose file is gone are dropped. */
static int ma_list_path(char *out, size_t n, int mk) {
    char p[1000];
    if (!settings_path(p, sizeof p, mk)) return 0;
    char *sl = strrchr(p, '/'); if (!sl) return 0;
    snprintf(sl + 1, sizeof p - (size_t)(sl + 1 - p), "modarchive.list");
    snprintf(out, n, "%s", p);
    return 1;
}
static void ma_list_load(void) {
    char path[1000], dir[900], line[256]; FILE *f;
    if (!ma_list_path(path, sizeof path, 0) || !(f = fopen(path, "r")) || net_cache_dir(dir, sizeof dir)) { if (f) fclose(f); return; }
    while (fgets(line, sizeof line, f)) {
        char *tab = strchr(line, '\t'), *nl = strpbrk(line, "\r\n");
        int id = atoi(line);
        if (nl) *nl = 0;
        if (id <= 0) continue;
        char fp[1100]; struct stat sb;
        snprintf(fp, sizeof fp, "%s/modarchive/%d.mod", dir, id);
        if (stat(fp, &sb) || sb.st_size <= 0) continue;
        add_file(fp);
        pl[npl - 1].ma_id = id;
        snprintf(pl[npl - 1].label, sizeof pl[npl - 1].label, "%s", tab && tab[1] ? tab + 1 : "ModArchive");
    }
    fclose(f);
}
static void ma_list_save(void) {
    char path[1000]; FILE *f;
    if (!ma_list_path(path, sizeof path, 1) || !(f = fopen(path, "w"))) return;
    for (int i = 0; i < npl; i++) {
        if (pl[i].ma_id <= 0) continue;
        char t[128]; snprintf(t, sizeof t, "%s", pl[i].label);
        for (char *c = t; *c; c++) if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
        fprintf(f, "%d\t%s\n", pl[i].ma_id, t);
    }
    fclose(f);
}
static void settings_save(void) {
    char path[1000]; FILE *f;
    if (!settings_path(path, sizeof path, 1) || !(f = fopen(path, "w"))) return;
    /* Groesse nur merken, wenn das Fenster normal ist (nicht Vollbild/maximiert) */
    if (!(SDL_GetWindowFlags(win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MAXIMIZED)))
        {
        int px, py;
        SDL_GetWindowSize(win, &g_win_w, &g_win_h);
        SDL_GetWindowPosition(win, &px, &py);
        if ((px || py) && (px & 0xFFFF0000) != 0x2FFF0000 && (px & 0xFFFF0000) != 0x1FFF0000) { g_win_x = px; g_win_y = py; }      /* Wayland has no window position (SDL returns 0,0 or the "centered" flag): then keep the old value */
    }
    fprintf(f, "window_w=%d\nwindow_h=%d\nwindow_x=%d\nwindow_y=%d\nview=%d\nrandom=%d\n", g_win_w, g_win_h, g_win_x, g_win_y, g_vmode, g_rand ? 1 : 0);
    fclose(f);
}
#endif

#ifdef __EMSCRIPTEN__
/* Wird von der Webseite (Drag & Drop / Dateiauswahl) aufgerufen, nachdem die Datei ins virtuelle Dateisystem geschrieben wurde. */
/* Canvas-Groesse in Pixeln (Hochformat auf dem Handy, siehe ngweb/shell.html) */
EMSCRIPTEN_KEEPALIVE void ng_web_size(int w, int h) { if (win && w > 0 && h > 0) SDL_SetWindowSize(win, w, h); }

EMSCRIPTEN_KEEPALIVE void ng_web_add(const char *path) {
    int was = npl;
    add_path(path, 0);
    if (npl > was && (cur < 0 || !g_song)) play_index(was);
}

/* Ergebnis des Mod-Archive-Downloads (rc 0 = ok, Datei liegt unter path im virtuellen Dateisystem) */
EMSCRIPTEN_KEEPALIVE void ng_web_ma_done(int id, int rc, const char *path) {
    dl_id = id; dl_rc = rc;
    if (path) snprintf(dl_path, sizeof dl_path, "%s", path);
    dl_state = rc ? 3 : 2;
}

/* Modul-ID aus Eingabe (Zahl oder Link) fuer das HTML-Eingabefeld der Webseite; 0 = nicht erkannt */
EMSCRIPTEN_KEEPALIVE int ng_web_parse_id(const char *t) { return ma_parse_id(t); }

/* Text aus der Zwischenablage (paste-Ereignis) */
EMSCRIPTEN_KEEPALIVE void ng_web_paste(const char *t) {
    if (!ma_open || !t) return;
    ma_eat = 0;
    ma_text(t);
}

/* Mod Archive list in the browser: one "id<TAB>FORMAT<TAB>title" per line in localStorage (the files themselves are not kept, they are fetched
 * again when an entry is played). Saved whenever the list or a title changes: a browser tab has no reliable "quit", especially on iOS. */
EM_JS_DEPS(ngp_ma_deps, "$stringToUTF8");
EM_JS(void, web_ma_store, (const char *txt), {
    try { var s = UTF8ToString(txt); if (s) localStorage.setItem('ngp.malist', s); else localStorage.removeItem('ngp.malist'); } catch (e) {}
});
EM_JS(void, web_ma_fetch_saved, (char *buf, int cap), {
    var s = '';
    try { s = localStorage.getItem('ngp.malist') || ''; } catch (e) {}
    stringToUTF8(s, buf, cap);
});
#define MA_LIST_CAP 65536
static char ma_last[MA_LIST_CAP];
static void ma_list_text(char *out, size_t cap) {
    size_t n = 0; out[0] = 0;
    for (int i = 0; i < npl; i++) {
        if (pl[i].ma_id <= 0) continue;
        char t[128]; snprintf(t, sizeof t, "%s", pl[i].label);
        for (char *c = t; *c; c++) if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
        int f = entry_fmt(&pl[i]); if (f < 0 || f >= FMT_COUNT) f = 0;
        int w = snprintf(out + n, cap - n, "%d\t%s\t%s\n", pl[i].ma_id, FMT_NAME[f], t);
        if (w < 0 || (size_t)w >= cap - n) break;
        n += (size_t)w;
    }
}
static void ma_sync_web(int force) {
    static int tick;
    if (!force && ++tick < 45) return;       /* the main loop checks every 45 frames as a safety net, changes are saved at once (force) */
    tick = 0;
    static char txt[MA_LIST_CAP];
    ma_list_text(txt, sizeof txt);
    if (strcmp(txt, ma_last)) { memcpy(ma_last, txt, sizeof ma_last); web_ma_store(txt); }
}
static void ma_list_load_web(void) {
    static char buf[MA_LIST_CAP];
    web_ma_fetch_saved(buf, sizeof buf);
    for (char *line = buf; *line; ) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        int id = atoi(line), dup = 0;
        if (id > 0 && t2) {
            for (int i = 0; i < npl; i++) if (pl[i].ma_id == id) dup = 1;
            if (!dup) {
                *t1 = 0; *t2 = 0;
                char p[64]; snprintf(p, sizeof p, "/tmp/ma/%d.mod", id);     /* where the page stores the download */
                add_file(p);
                entry *e = &pl[npl - 1]; e->ma_id = id;
                snprintf(e->label, sizeof e->label, "%s", t2[1] ? t2 + 1 : "ModArchive");
                e->fmt = 0;
                for (int f = 1; f < FMT_COUNT; f++) if (!strcmp(FMT_NAME[f], t1 + 1)) e->fmt = f;      /* badge without the file; set to -1 again when it arrives */
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    ma_list_text(ma_last, sizeof ma_last);
    if (pl_count_tab(1) > 0 && pl_count_tab(0) == 0) g_pltab = 1;      /* only Mod Archive entries: show that tab */
}

/* Lock screen / control centre (iOS, Android, desktop media keys): Media Session with title, play/pause and next/previous.
 * iOS only lists a page as "now playing" while an HTMLMediaElement is playing, and SDL plays through Web Audio, so a silent looping
 * <audio> element runs alongside (started at the first touch, as the browser requires a gesture). */
EM_JS(void, web_media_setup, (void), {
    if (!('mediaSession' in navigator)) return;
    var ms = navigator.mediaSession;
    var cmd = function (c) { return function () { Module._ng_web_media(c); }; };
    /* iOS shows +-10 s skip buttons instead of next/previous when it has dropped the handlers (it seems to reset them when the media
     * element starts playing), so they are registered again at every start and state change, and the seek actions are cleared. */
    Module.ngMediaHandlers = function () {
        try {
            ms.setActionHandler('play', cmd(0));
            ms.setActionHandler('pause', cmd(1));
            ms.setActionHandler('nexttrack', cmd(2));
            ms.setActionHandler('previoustrack', cmd(3));
            ms.setActionHandler('seekbackward', null);
            ms.setActionHandler('seekforward', null);
            ms.setActionHandler('seekto', null);
        } catch (e) {}
    };
    Module.ngMediaHandlers();
    var n = 8000, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
    var str = function (o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); };
    str(0, 'RIFF'); v.setUint32(4, 36 + n * 2, true); str(8, 'WAVE'); str(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
    v.setUint16(22, 1, true); v.setUint32(24, 8000, true); v.setUint32(28, 16000, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
    str(36, 'data'); v.setUint32(40, n * 2, true);
    var a = new Audio(URL.createObjectURL(new Blob([b], { type: 'audio/wav' })));
    a.loop = true;
    Module.ngMediaEl = a;
    /* Heartbeat for the locked screen: the browser stops requestAnimationFrame and throttles timers, but the media element keeps playing and
     * keeps firing timeupdate (a few times per second). Each one lets the app finish a download, move on at the end of a song and refresh the
     * lock screen (ng_web_media 4). */
    a.addEventListener('timeupdate', function () { Module._ng_web_media(4); });
    Module.ngLoopUrl = a.src;
    /* when the page goes to the background (screen lock) put the element where the song is now */
    var park = function () {
        if (!Module.ngTl || Module.ngPosT === undefined) return;
        var now = Module.ngPosBase + (Module.ngPosRate > 0 ? (performance.now() - Module.ngPosT) / 1000 : 0);
        if (Math.abs(a.currentTime - now) > 0.5) a.currentTime = now;
    };
    document.addEventListener('visibilitychange', function () { if (document.visibilityState === 'hidden') park(); });
    window.addEventListener('pagehide', park);
    a.addEventListener('play', Module.ngMediaHandlers);
    a.addEventListener('playing', Module.ngMediaHandlers);
    Module.ngMediaWant = false;
    /* The first tap primes the element (browsers allow later programmatic play() on an element a gesture has played); it is paused again
     * at once if nothing is playing yet, so the lock screen does not show an idle player. */
    var kick = function () {
        if (!Module.ngPrimed) {
            Module.ngPrimed = true;
            var p = a.play();
            if (p && p.then) p.then(function () { if (!Module.ngMediaWant) a.pause(); }).catch(function () { Module.ngPrimed = false; });
        } else if (Module.ngMediaWant && a.paused) { var q = a.play(); if (q && q.catch) q.catch(function () {}); }
    };
    window.addEventListener('touchend', kick, true);
    window.addEventListener('click', kick, true);
});
EM_JS(void, web_media_update, (const char *title, int playing), {
    if (!('mediaSession' in navigator)) return;
    var ms = navigator.mediaSession;
    /* artwork: the site icon as an opaque full-bleed square (/icon-512.png). iOS rounds the artwork itself and paints transparent areas
     * white, so rounded corners in the image itself show up as white slivers. Without any artwork it takes the 180 px apple-touch-icon. */
    try { ms.metadata = new MediaMetadata({ title: UTF8ToString(title), artist: 'ngPlayer', album: 'ngplayer.org', artwork: [{ src: '/icon-512.png', sizes: '512x512', type: 'image/png' }] }); } catch (e) {}
    ms.playbackState = playing ? 'playing' : 'paused';
    Module.ngMediaWant = !!playing;
    var a = Module.ngMediaEl;
    if (a) { if (playing) { var p = a.play(); if (p && p.then) p.then(Module.ngMediaHandlers).catch(function () {}); } else a.pause(); }
    if (Module.ngMediaHandlers) Module.ngMediaHandlers();
});
/* Lock screen progress: iOS only knows the 1 s of the silent loop and lets the bar swing back and forth unless it is told the real length
 * and position. The length of a module is not known up front, so a second instance of the song is rendered silently up to its end in
 * small slices (timer driven, so it also runs while the screen is locked and the main loop stands still). */
EM_JS(void, web_media_position, (double dur, double pos, double rate), {
    var ms = ('mediaSession' in navigator) ? navigator.mediaSession : null;
    if (ms && ms.setPositionState) {
        try {
            var d = dur > 0 ? dur : Infinity;
            ms.setPositionState({ duration: d, position: Math.max(0, Math.min(pos, d)), playbackRate: rate });
        } catch (e) {}
    }
    /* iOS reads the lock screen position from the silent media element when the screen locks, not from setPositionState. So the element
     * carries the real timeline: a silent track as long as the song (8 kHz, 8 bit), its currentTime kept at the song position. Songs of
     * unknown length (or over 15 min) keep the 1 s loop. */
    var a = Module.ngMediaEl;
    if (!a) return;
    Module.ngPosBase = pos; Module.ngPosT = performance.now(); Module.ngPosRate = rate;
    if (dur > 0 && dur < 900) {
        if (!Module.ngTl || Math.abs(Module.ngTl - dur) > 0.5) {
            var secs = Math.ceil(dur) + 3, n = 8000 * secs;
            var buf = new ArrayBuffer(44 + n), v = new DataView(buf), u = new Uint8Array(buf);
            var str = function (o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); };
            str(0, 'RIFF'); v.setUint32(4, 36 + n, true); str(8, 'WAVE'); str(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
            v.setUint16(22, 1, true); v.setUint32(24, 8000, true); v.setUint32(28, 8000, true); v.setUint16(32, 1, true); v.setUint16(34, 8, true);
            str(36, 'data'); v.setUint32(40, n, true);
            u.fill(128, 44);
            var url = URL.createObjectURL(new Blob([buf], { type: 'audio/wav' }));
            if (Module.ngTlUrl) URL.revokeObjectURL(Module.ngTlUrl);
            Module.ngTlUrl = url; Module.ngTl = dur;
            a.loop = false;
            var onmeta = function () {
                a.removeEventListener('loadedmetadata', onmeta);
                a.currentTime = Module.ngPosBase + (Module.ngPosRate > 0 ? (performance.now() - Module.ngPosT) / 1000 : 0);
                if (Module.ngMediaWant) { var p = a.play(); if (p && p.catch) p.catch(function () {}); }
            };
            a.addEventListener('loadedmetadata', onmeta);
            a.src = url;
        } else if (Math.abs(a.currentTime - pos) > 1.0) a.currentTime = pos;
    } else if (Module.ngTl) {                       /* length unknown again (next song): back to the 1 s loop */
        Module.ngTl = 0; a.loop = true; a.src = Module.ngLoopUrl;
        if (Module.ngMediaWant) { var q = a.play(); if (q && q.catch) q.catch(function () {}); }
    }
});
static Song *g_probe; static unsigned long long g_probe_frames; static int g_probe_gen;
static double g_len_s;                      /* > 0: length in seconds, 0: still being measured, -1: not available (reported as endless) */
static double ps_pos, ps_rate = -1, ps_len = -2; static Uint32 ps_t;
static void media_push_position(void) {
    double pos = (double)g_frames / g_rate, rate = (g_song && !g_paused && !g_ended) ? 1.0 : 0.0;
    web_media_position(g_len_s > 0 ? g_len_s : -1, pos, rate);
    ps_pos = pos; ps_rate = rate; ps_len = g_len_s; ps_t = SDL_GetTicks();
}
static void probe_tick(void *arg) {
    if ((int)(intptr_t)arg != g_probe_gen || !g_probe) return;        /* another song started meanwhile */
    static int16_t buf[2048 * 2];
    Uint64 t0 = SDL_GetPerformanceCounter(), fq = SDL_GetPerformanceFrequency();
    for (;;) {
        int got = song_render(g_probe, buf, 2048);
        if (got > 0) g_probe_frames += (unsigned)got;
        if (got <= 0 || g_probe_frames > (unsigned long long)g_rate * 3600) {
            g_len_s = got <= 0 ? (double)g_probe_frames / g_rate : -1;
            song_close(g_probe); g_probe = NULL;
            media_push_position();
            return;
        }
        if ((SDL_GetPerformanceCounter() - t0) * 1000 / fq >= 2) break;      /* 2 ms per slice: the audio callback shares this thread */
    }
    emscripten_async_call(probe_tick, arg, 8);
}
static void probe_start(int idx) {
    g_probe_gen++;
    if (g_probe) { song_close(g_probe); g_probe = NULL; }
    g_len_s = 0; g_probe_frames = 0;
    if (idx < 0 || idx >= npl || !g_song) { g_len_s = -1; return; }
    const char *f = song_format(g_song);
    /* only the replayers that keep all state in their own instance; the others report an endless song */
    if (!strcmp(f, "TFMX") || !strcmp(f, "DMF") || !strcmp(f, "AHX")) { g_len_s = -1; return; }
    char err[64] = "";
    g_probe = song_open_sub(pl[idx].path, g_rate, g_open_sub, err, sizeof err);
    if (!g_probe) { g_len_s = -1; return; }
    emscripten_async_call(probe_tick, (void *)(intptr_t)g_probe_gen, 30);
}
/* Pushes the title and the playing state to the Media Session when one of them changed. Also called right after a lock screen command:
 * with the screen locked the browser stops requestAnimationFrame (the main loop), only the audio keeps running, so the loop alone would
 * leave the old title on the lock screen. */
static void media_sync(void) {
    static char mt[160]; static int mp = -1;
    const char *t = (cur >= 0 && cur < npl) ? pl[cur].label : "ngPlayer";
    int now = g_song && !g_paused && !g_ended;
    int changed = now != mp || strcmp(t, mt);
    if (changed) { mp = now; snprintf(mt, sizeof mt, "%s", t); web_media_update(mt, now); }
    {   /* progress: push on every change, when the length became known, after a jump (seek) and every 15 s */
        double pos = (double)g_frames / g_rate, rate = now ? 1.0 : 0.0;
        Uint32 tn = SDL_GetTicks();
        double expect = ps_pos + (ps_rate > 0 ? (double)(tn - ps_t) / 1000.0 : 0.0);
        int jump = fabs(pos - expect) > 1.5;
        if (changed || g_len_s != ps_len || rate != ps_rate || jump || tn - ps_t > 15000) media_push_position();
    }
}
/* Commands from the lock screen: 0 play, 1 pause, 2 next, 3 previous; 4 = heartbeat from the silent audio element */
EMSCRIPTEN_KEEPALIVE void ng_web_media(int cmd) {
    if (cmd == 0) { if (g_ended) { if (cur >= 0) play_index_sub(cur, g_open_sub); } else g_paused = 0; }
    else if (cmd == 1) g_paused = 1;
    else if (cmd == 2) next_user(1);
    else if (cmd == 3) next_user(-1);
    else if (cmd == 4) { ma_poll(); if (!shot) song_end_advance(); }      /* heartbeat, see web_media_setup */
    media_sync();
}
/* Timer that runs even while the screen is locked (iOS stops requestAnimationFrame, so iter() does not run): finishes a download (a new
 * random module), moves on when a song has ended and keeps the lock screen in sync. */
static void bg_tick(void *arg) {
    (void)arg;
    ma_poll();
    if (!shot) song_end_advance();
    media_sync();
    emscripten_async_call(bg_tick, NULL, 400);
}
#endif

#ifdef __EMSCRIPTEN__
/* Browsers (iOS Safari above all) only allow window.open and clipboard writes inside the handler of a user gesture. The menu click is
 * processed here in the main loop, after the event, so such a call is blocked. Instead the action is queued in the page and runs at the
 * next touchend/click of the same tap (kind 0: open the URL in a new tab, 1: copy the text); if that gesture was already over, it is
 * tried after 450 ms anyway (works on desktop browsers). */
static void web_on_gesture(int kind, const char *arg) {
    EM_ASM({
        var kind = $0;
        var arg = UTF8ToString($1);
        if (!Module.ngGq) {
            Module.ngGq = [];
            var run = function () { var q = Module.ngGq; var n = performance.now(); Module.ngGq = []; q.forEach(function (it) { if (n - it.t < 2500) it.f(); }); };
            window.addEventListener('touchend', run, true);
            window.addEventListener('click', run, true);
        }
        var copy = function (t) {
            var fb = function () {
                var a = document.createElement('textarea');
                a.value = t; a.style.position = 'fixed'; a.style.opacity = '0';
                document.body.appendChild(a); a.focus(); a.select();
                try { document.execCommand('copy'); } catch (e) {}
                document.body.removeChild(a);
            };
            if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(t).catch(fb); else fb();
        };
        var f = kind == 0 ? function () { window.open(arg, '_blank'); } : function () { copy(arg); };
        var item = {}; item.t = performance.now(); item.f = f;
        Module.ngGq.push(item);
        setTimeout(function () { var i = Module.ngGq.indexOf(item); if (i >= 0) { Module.ngGq.splice(i, 1); f(); } }, 450);
    }, kind, arg);
}
#endif

/* ---- Share: right click on a Mod Archive entry -> menu "Share" copies https://ngplayer.org/player/#<ID> to the clipboard ---- */
static void share_link(int idx) {
    char url[96]; snprintf(url, sizeof url, "https://ngplayer.org/player/#%d", pl[idx].ma_id);
#ifdef __EMSCRIPTEN__
    web_on_gesture(1, url);
#else
    SDL_SetClipboardText(url);
#endif
    snprintf(toast_txt, sizeof toast_txt, "Link copied"); toast_t = 2.0f;
}
/* Info: open the module page on The Mod Archive in the browser */
static void info_open(int idx) {
    char url[128]; snprintf(url, sizeof url, "https://modarchive.org/index.php?request=view_by_moduleid&query=%d", pl[idx].ma_id);
#ifdef __EMSCRIPTEN__
    web_on_gesture(0, url);
#else
    SDL_OpenURL(url);
#endif
}
static void draw_ctx_menu(void) {
    if (ctx_idx >= 0 && ctx_idx < npl) {
        static const char *const item[3] = { "Share", "Info", "Remove" };
        int n = ctx_n(ctx_idx), off = 3 - n;           /* local files only show the last item (Remove) */
        rrect(ctx_x - 1, ctx_y - 1, CTX_W + 2, CTX_H(ctx_idx) + 2, 9, alpha(EDGE, 0.9f));
        rrect(ctx_x, ctx_y, CTX_W, CTX_H(ctx_idx), 8, CARDHI);
        for (int j = 0; j < n; j++) {
            float iy = ctx_y + 1 + j * CTX_ROW;
            int hv = mx >= ctx_x && mx < ctx_x + CTX_W && my >= iy && my < iy + CTX_ROW;
            if (hv) rrect(ctx_x + 3, iy + 2, CTX_W - 6, CTX_ROW - 4, 6, alpha(VC[0], 0.22f));
            text(ctx_x + 14, iy + 7, 14, item[j + off][0] == 'R' ? (hv ? WARN : alpha(WARN, 0.75f)) : (hv ? TXT : DIM), item[j + off]);
        }
    }
    if (toast_t > 0) {
        toast_t -= (float)g_dt;
        float w = text_width(toast_txt, 14) + 32, x = (view_w - w) / 2, y = view_h - 70;
        rrect(x, y, w, 32, 10, alpha(CARDHI, 0.96f));
        text(x + 16, y + 8, 14, OKC, toast_txt);
    }
}
static void iter(void) {
        SDL_Event e;
        click = 0; rclick = 0;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_RIGHT) { rclick = 1; rclickx = (int)e.button.x; rclicky = (int)e.button.y; }
                if (e.button.button == SDL_BUTTON_LEFT) {
                    click = 1; clickx = (int)e.button.x; clicky = (int)e.button.y; click_ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
                    lp_on = g_portrait; lp_fired = 0; lp_swallow = 0; lp_x = clickx; lp_y = clicky; lp_t = SDL_GetTicks();
                }
                break;
            case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) lp_on = 0; break;
            case SDL_MOUSEWHEEL:
                if (mx < 16 + 288 + 16) list_scroll -= e.wheel.y * 3;
                else if (e.wheel.x) pat_ch0 -= e.wheel.x;
                else if (SDL_GetModState() & KMOD_SHIFT) pat_ch0 -= e.wheel.y;
                break;
            case SDL_DROPFILE: {
                int was = npl;
                add_path(e.drop.file, 0);
                SDL_free(e.drop.file);
                if (npl > was) { if (cur < 0 || !g_song) play_index(was); }
                break;
            }
            case SDL_TEXTINPUT: if (ma_open) ma_text(e.text.text); break;
            case SDL_KEYDOWN:
                if (ma_open) { ma_key(&e.key); break; }
                switch (e.key.keysym.sym) {
                case SDLK_o: ma_show(1); break;
                case SDLK_v: { int t = 0; for (int k = 0; k < 4; k++) if (VIEW_ORDER[k] == g_vmode) t = k; g_vmode = VIEW_ORDER[(t + 1) % 4]; } break;
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_SPACE: g_paused = !g_paused; break;
                case SDLK_LEFT: if (e.key.keysym.mod & KMOD_SHIFT) pat_ch0--; else if (!(e.key.keysym.mod & KMOD_CTRL) && g_song && song_subsongs(g_song) > 1) { if (song_subsong(g_song) > 0) play_index_sub(cur, song_subsong(g_song) - 1); } else seek_order((g_view_valid ? g_view.order : song_order(g_song)) - 1); break;
                case SDLK_RIGHT: if (e.key.keysym.mod & KMOD_SHIFT) pat_ch0++; else if (!(e.key.keysym.mod & KMOD_CTRL) && g_song && song_subsongs(g_song) > 1) { if (song_subsong(g_song) + 1 < song_subsongs(g_song)) play_index_sub(cur, song_subsong(g_song) + 1); } else seek_order((g_view_valid ? g_view.order : song_order(g_song)) + 1); break;
                case SDLK_UP: next_user(-1); break;
                case SDLK_DOWN: next_user(1); break;
                case SDLK_r: if (cur >= 0) play_index_sub(cur, g_open_sub); break;
                case SDLK_F5: g_lag_ms -= 5; if (g_lag_ms < 0) g_lag_ms = 0; break;
                case SDLK_F6: g_lag_ms += 5; if (g_lag_ms > 300) g_lag_ms = 300; break;
                case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS: g_vol += 0.05f; if (g_vol > 1) g_vol = 1; break;
                case SDLK_MINUS: case SDLK_KP_MINUS: g_vol -= 0.05f; if (g_vol < 0) g_vol = 0; break;
                case SDLK_p: if (g_song) song_toggle_paula_filter(g_song); break;
                case SDLK_f: case SDLK_F11:
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    break;
                default: break;
                }
                break;
            default: break;
            }
        }
#ifdef __EMSCRIPTEN__
        media_sync();       /* keep the lock screen in sync: title of the current entry and playing / paused */
        ma_sync_web(0);     /* save the Mod Archive list when it changed */
#endif
        int ww, wh, dw, dh;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        /* Hochformat (Seitenverhaeltnis < 0,8) oder zu klein fuer das Querformat: schmales, gestapeltes Layout (virtuell 440 breit) */
        g_portrait = dw * 10 < dh * 12 || dw < 640 || dh < 430;      /* aspect < 1.2: tablets upright, phones */
        if (getenv("NGPLAYER_TEST_MOBILE")) g_portrait = 1;          /* test hook: force the phone layout */
        if (g_portrait) {                                              /* virtual width 440, but at least 640 high (tablets are wider than phones) */
            view_scale = (float)dw / 440.0f;
            if ((float)dh / 640.0f < view_scale) view_scale = (float)dh / 640.0f;
        }
        else {
            view_scale = (float)dw / 1280.0f;
            if ((float)dh / 860.0f < view_scale) view_scale = (float)dh / 860.0f;
            if (view_scale < 0.5f) view_scale = 0.5f;
        }
        view_w = (float)dw / view_scale;
        view_h = (float)dh / view_scale;
        int mxi, myi;
        Uint32 mb = SDL_GetMouseState(&mxi, &myi);
        mouse_down = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        float k = ww ? (float)dw / (float)ww / view_scale : 1;
        mx = (float)mxi * k; my = (float)myi * k;
        if (lp_on && !mouse_down) lp_on = 0;
        if (lp_on && !lp_fired && SDL_GetTicks() - lp_t >= 500) {      /* held still for half a second: open the menu like a right click */
            lp_fired = 1; lp_on = 0; lp_swallow = 1;
            /* Use the current pointer position (mx, my), like the taps in the list do: the coordinates of the touch-down event are not reliable
             * on iOS (the menu then belonged to the wrong entry, always the first one). Converted back to event units because the scaling below
             * turns rclickx/rclicky into view units again. */
            rclick = 1; rclickx = k > 0 ? (int)(mx / k) : lp_x; rclicky = k > 0 ? (int)(my / k) : lp_y;
        }
        if (click) { clickx = (int)(clickx * k); clicky = (int)(clicky * k); }
        if (rclick) { rclickx = (int)(rclickx * k); rclicky = (int)(rclicky * k); }
        if (ctx_idx >= 0 && click) {                         /* click on the open menu: copy the link; any click closes it */
            float cmx = g_mobile ? mx : (float)clickx, cmy = g_mobile ? my : (float)clicky;     /* touch: current pointer (the event coordinates are not reliable on iOS) */
            if (cmx >= ctx_x && cmx < ctx_x + CTX_W && cmy >= ctx_y && cmy < ctx_y + CTX_H(ctx_idx) && ctx_idx < npl) {
                int j = (int)((cmy - ctx_y - 1) / CTX_ROW) + 3 - ctx_n(ctx_idx);          /* 0 Share, 1 Info, 2 Remove */
                if (j == 0) share_link(ctx_idx); else if (j == 1) info_open(ctx_idx); else if (j == 2) pl_remove(ctx_idx);
            }
            ctx_idx = -1; click = 0; lp_swallow = 1;
        }
        if (rclick && ctx_idx >= 0) ctx_idx = -1;

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - tlast) / freq;
        tlast = now;
        if (dt > 0.1) dt = 0.1;
        g_dt = dt;
        g_fid++;
        analyse((float)dt);

        /* Songende -> naechster Titel */
        if (!shot) song_end_advance();

        glViewport(0, 0, dw, dh);
        glClearColor(BG0.r, BG0.g, BG0.b, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);
        glUniform2f(u_res, view_w, view_h);
        glUniform1f(u_pxs, view_scale);
        glUniform1i(u_tex, 0);
        glBindVertexArray(vao);
        glBindTexture(GL_TEXTURE_2D, fonttex);
        ma_poll();
        draw_ui((double)(now - t0) / freq);
        draw_ma_dialog((double)(now - t0) / freq);
        draw_dice_big(view_w, view_h);
        draw_ctx_menu();
        set_additive(0);
        flush();
        if (shot && ++frame >= shot_frames) { save_screenshot(shot, dw, dh); running = 0; }
        SDL_GL_SwapWindow(win);
#ifndef __EMSCRIPTEN__
        double spent = (double)(SDL_GetPerformanceCounter() - now) / freq;
        if (!vsync_ok || shot) { if (spent < 0.012) SDL_Delay((Uint32)((0.0167 - spent) * 1000)); }
        else if (spent < 0.002) SDL_Delay(1);       /* Schutz, falls der Treiber VSync nicht wirklich abwartet */
#endif
    }

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--version")) { printf("ngPlayer %s\n", NGPLAYER_VERSION); return 0; }
    shot = NULL;
    shot_frames = 90;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) shot_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) g_rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: %s [--rate N] [--screenshot out.bmp [--frames N]] [datei|ordner ...]\n", argv[0]);
            return 0;
        } else add_path(argv[i], 0);
    }
    if (!npl) {   /* ohne Argumente: Musikordner des Benutzers */
        const char *home = getenv("HOME");
        if (home) {
            static const char *const dirs[] = { "Musik", "Music" };
            for (int k = 0; k < 2 && !npl; k++) {
                char dp[1024];
                snprintf(dp, sizeof dp, "%s/%s", home, dirs[k]);
                add_path(dp, 0);
            }
        }
    }
#ifdef __EMSCRIPTEN__
    /* Tastatur nur vom Canvas, damit HTML-Eingabefelder der Seite (Mod-Archive-Dialog) normal funktionieren */
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
#ifndef __EMSCRIPTEN__
    if (!shot) { settings_load(); ma_list_load(); }
#define WIN_X0 g_win_x
#define WIN_Y0 g_win_y
#define WIN_W0 g_win_w
#define WIN_H0 g_win_h
#else
#define WIN_X0 SDL_WINDOWPOS_CENTERED
#define WIN_Y0 SDL_WINDOWPOS_CENTERED
#define WIN_W0 1280
#define WIN_H0 860
#endif
    win = SDL_CreateWindow("ngPlayer", WIN_X0, WIN_Y0, WIN_W0, WIN_H0,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "Fenster: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, 360, 480);
    { const char *ts = getenv("NGPLAYER_TEST_SIZE"); int tw, th; if (ts && sscanf(ts, "%dx%d", &tw, &th) == 2) SDL_SetWindowSize(win, tw, th); }   /* test hook: fixed window size for screenshots */      /* schmal erlaubt: dann greift das Hochformat-Layout */
    { SDL_Surface *ic = make_icon(64); if (ic) { SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic); } }
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (window.ngFit) window.ngFit(); });
#endif
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "OpenGL-3.3-Kontext: %s\n", SDL_GetError()); return 1; }
    if (gl_load()) return 1;
    { const char *tm = getenv("NGPLAYER_TEST_MA"); if (tm) { snprintf(ma_buf, sizeof ma_buf, "%s", tm); ma_len = (int)strlen(ma_buf); ma_show(1); ma_start(); } }
    vsync_ok = SDL_GL_SetSwapInterval(-1) == 0 || SDL_GL_SetSwapInterval(1) == 0;

    int vsh = gl_compile(GL_VERTEX_SHADER, VS), fsh = gl_compile(GL_FRAGMENT_SHADER, FS);
    if (!vsh || !fsh) return 1;
    prog = glCreateProgram();
    glAttachShader(prog, (GLuint)vsh); glAttachShader(prog, (GLuint)fsh);
    glLinkProgram(prog);
    GLint lk = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &lk);
    if (!lk) { char log[1024]; glGetProgramInfoLog(prog, sizeof log, NULL, log); fprintf(stderr, "Link: %s\n", log); return 1; }
    u_res = glGetUniformLocation(prog, "res");
    u_pxs = glGetUniformLocation(prog, "pxs");
    u_tex = glGetUniformLocation(prog, "tex");
    u_rip = glGetUniformLocation(prog, "rip");
    u_rt = glGetUniformLocation(prog, "rt");

    vb = (vtx *)malloc(sizeof(vtx) * MAXV);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(vtx) * MAXV), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(vtx), (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(vtx), (void *)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(vtx), (void *)16);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, 0, sizeof(vtx), (void *)32);

    for (int i = 0; i < 256; i++) glyph_idx[i] = -1;
    for (int i = 0; i < FONT_GLYPH_COUNT; i++) if (font_glyphs[i].code < 256) glyph_idx[font_glyphs[i].code] = i;
    glGenTextures(1, &fonttex);
    glBindTexture(GL_TEXTURE_2D, fonttex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_ATLAS_W, FONT_ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, font_atlas);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(2, card_tex);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, card_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, i ? CARD_IW_W : CARD_GUS_W, i ? CARD_IW_H : CARD_GUS_H, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, i ? (const void *)card_iw : (const void *)card_gus);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, fonttex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#ifdef __EMSCRIPTEN__
    g_mobile = EM_ASM_INT({ return (window.matchMedia && window.matchMedia('(pointer: coarse)').matches) ? 1 : 0; });
    web_media_setup();
    g_rand = web_rand_get();
    emscripten_async_call(bg_tick, NULL, 400);
#endif
    if (getenv("NGPLAYER_TEST_MOBILE")) g_mobile = 1;        /* test hook: touch layout on the desktop */
    if (getenv("NGPLAYER_TEST_PLOPEN")) g_plopen = 1;        /* test hook: playlist overlay open (phone layout) */
    if (getenv("NGPLAYER_TEST_SCOPES")) g_vmode = 3;               /* test hook: scopes view */
    if (getenv("NGPLAYER_TEST_VOXEL")) g_vmode = 2;               /* test hook: voxel flight view */
    if (getenv("NGPLAYER_TEST_DICE")) { dice_start(); if (getenv("NGPLAYER_TEST_DICE")[0] == '2') dice_stop_req = 1; }        /* test hook: the big dancing dice, as while a random module is fetched */
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = g_rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = AUDIO_SAMPLES; want.callback = audio_cb;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) fprintf(stderr, "Audio: %s (weiter ohne Ton)\n", SDL_GetError());
    else { g_rate = have.freq; SDL_PauseAudioDevice(g_dev, 0); }

#ifdef __EMSCRIPTEN__
    ma_list_load_web();                 /* Mod Archive list of the last visit (entries without file: fetched when played) */
#endif
    if (npl && !(pl[0].ma_id > 0 && access(pl[0].path, R_OK))) play_index(0);

    running = 1;
    t0 = SDL_GetPerformanceCounter(); tlast = t0;
    freq = (double)SDL_GetPerformanceFrequency();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(iter, 0, 1);
#else
    while (running) iter();
#endif
#ifndef __EMSCRIPTEN__
    if (!shot) { settings_save(); ma_list_save(); }
#endif
    if (g_dev) SDL_CloseAudioDevice(g_dev);
    unload();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
