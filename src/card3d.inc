/* Rotating 3D sound card (Gravis Ultrasound GF1 / Ultrasound PnP with AMD InterWave, Terratec EWS64, AdLib Gold) in the header, rendered on the GPU (gfx.h):
 * the baked model (card3d_data.h, made from the real 3D models by tools/bake_card3d.py) lives in vertex/index buffers, is drawn with a small
 * shader (flat shading from screen space derivatives, the photo of the board as texture) and a depth buffer into an off-screen texture, which
 * the 2D renderer then shows like any picture. GPL-3.0-or-later */
#include <math.h>
#include "card3d_data.h"
#include "a500_data.h"      /* the Amiga 500 replaces the drawn Paula chip */
#define C3_N 5

typedef struct { const int16_t *v; const uint8_t *vc, *pal; const uint16_t *idx; int nv, nt; const int16_t *tv; const uint16_t *tuv, *tidx; int ntt, ntv; const uint16_t *tex; int tw, th; float aspect; const int16_t *bv; const uint16_t *buv, *bidx; int nbv, nbt; const uint8_t *bpal, *bpix; int bw, bh; float tilt, fit; } C3Model;   /* tilt and fit: 0 = the defaults of the sound cards (0.17 rad, 0.95) */
static const C3Model C3M[C3_N] = {
    { c3_GF1_v, c3_GF1_vc, c3_GF1_pal, c3_GF1_i, C3_GF1_NV, C3_GF1_NT, c3_GF1_tv, c3_GF1_tuv, c3_GF1_ti, C3_GF1_NTT, C3_GF1_NTV, c3_GF1_tex, C3_GF1_TW, C3_GF1_TH, C3_GF1_ASPECT, c3_GF1_bv, c3_GF1_buv, c3_GF1_bi, C3_GF1_NBV, C3_GF1_NBT, c3_GF1_bpal, c3_GF1_bpix, C3_GF1_BW, C3_GF1_BH },
    { c3_IW_v, c3_IW_vc, c3_IW_pal, c3_IW_i, C3_IW_NV, C3_IW_NT, c3_IW_tv, c3_IW_tuv, c3_IW_ti, C3_IW_NTT, C3_IW_NTV, c3_IW_tex, C3_IW_TW, C3_IW_TH, C3_IW_ASPECT, c3_IW_bv, c3_IW_buv, c3_IW_bi, C3_IW_NBV, C3_IW_NBT, c3_IW_bpal, c3_IW_bpix, C3_IW_BW, C3_IW_BH },
    { c3_EWS_v, c3_EWS_vc, c3_EWS_pal, c3_EWS_i, C3_EWS_NV, C3_EWS_NT, c3_EWS_tv, c3_EWS_tuv, c3_EWS_ti, C3_EWS_NTT, C3_EWS_NTV, c3_EWS_tex, C3_EWS_TW, C3_EWS_TH, C3_EWS_ASPECT, c3_EWS_bv, c3_EWS_buv, c3_EWS_bi, C3_EWS_NBV, C3_EWS_NBT, c3_EWS_bpal, c3_EWS_bpix, C3_EWS_BW, C3_EWS_BH },
    { c3_GOLD_v, c3_GOLD_vc, c3_GOLD_pal, c3_GOLD_i, C3_GOLD_NV, C3_GOLD_NT, c3_GOLD_tv, c3_GOLD_tuv, c3_GOLD_ti, C3_GOLD_NTT, C3_GOLD_NTV, c3_GOLD_tex, C3_GOLD_TW, C3_GOLD_TH, C3_GOLD_ASPECT, c3_GOLD_bv, c3_GOLD_buv, c3_GOLD_bi, C3_GOLD_NBV, C3_GOLD_NBT, c3_GOLD_bpal, c3_GOLD_bpix, C3_GOLD_BW, C3_GOLD_BH },
    /* Amiga 500: lies flat, so it is seen from above at 0.50 rad and turns about its vertical axis; the box aspect (1.75 after C3_BOX) holds the diagonal of the turning case, fit leaves room for the perspective */
    { c3_A500_v, c3_A500_vc, c3_A500_pal, c3_A500_i, C3_A500_NV, C3_A500_NT, c3_A500_tv, c3_A500_tuv, c3_A500_ti, C3_A500_NTT, C3_A500_NTV, c3_A500_tex, C3_A500_TW, C3_A500_TH, 2.19f, c3_A500_bv, c3_A500_buv, c3_A500_bi, C3_A500_NBV, C3_A500_NBT, c3_A500_bpal, c3_A500_bpix, C3_A500_BW, C3_A500_BH, 0.50f, 0.72f },
};
#define C3_BOX 0.80f                                        /* box aspect = model aspect * this: head room for the edge-on view */
static float c3_box_aspect(int mi) { return C3M[mi].aspect * C3_BOX; }

typedef struct { gfx_mesh *mesh[3]; gfx_tex *tex, *btex, *color, *color2; int ready, w, h; } C3Gl;   /* the second target (…2) is the picture of the Specs window: the card swings there instead of turning */
static C3Gl c3gl[C3_N];
static float c3_ang;
static int c3_first = 1;

static void c3_matrices(float ang, float tilt, float fit, float aspect, float *mvp, float *mv) {
    const float D = 7.0f, zn = 2.5f, zf = 12.0f;
    float ca = cosf(ang), sa = sinf(ang), ct = cosf(tilt), st = sinf(tilt);
    /* model -> view (column major): rotate about the vertical axis, tilt about the horizontal axis, move away from the camera */
    float M[16] = { ca, st * sa, -ct * sa, 0,   0, ct, st, 0,   sa, -st * ca, ct * ca, 0,   0, 0, -D, 1 };
    for (int i = 0; i < 16; i++) mv[i] = M[i];
    float fx = fit * D, fy = -fit * D * aspect;           /* fy negative: the picture is upside down in the texture */
    float P[16] = { fx, 0, 0, 0,  0, fy, 0, 0,  0, 0, -(zf + zn) / (zf - zn), -1,  0, 0, -2 * zf * zn / (zf - zn), 0 };
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) { float s = 0; for (int k = 0; k < 4; k++) s += P[k * 4 + r] * M[c * 4 + k]; mvp[c * 4 + r] = s; }
}

static void c3_setup(int mi) {
    C3Gl *g = &c3gl[mi]; const C3Model *m = &C3M[mi];
    if (g->ready) return;
    const float Q = 1.0f / 20000.0f;
    typedef struct { float x, y, z, u, v; unsigned char r, g, b, a; } V3;
    int counts[3] = { m->nv, m->ntv, m->nbv }, tcnt[3] = { m->nt, m->ntt, m->nbt };
    for (int p = 0; p < 3; p++) {
        int n = counts[p];
        V3 *vb3 = (V3 *)malloc(sizeof(V3) * (size_t)n);
        for (int i = 0; i < n; i++) {
            const int16_t *s = p == 2 ? m->bv : (p ? m->tv : m->v);
            V3 *o = &vb3[i];
            o->x = s[3 * i] * Q; o->y = s[3 * i + 1] * Q; o->z = s[3 * i + 2] * Q + (p == 1 ? 0.0002f : 0.0f);   /* the front texture sits a hair above the board (the back quad is baked a hair below) */
            { const uint16_t *uv = p == 2 ? m->buv : m->tuv; o->u = p ? uv[2 * i] / 65535.0f : 0; o->v = p ? uv[2 * i + 1] / 65535.0f : 0; }
            if (p) { o->r = o->g = o->b = 255; } else { const uint8_t *pl = m->pal + 3 * m->vc[i]; o->r = pl[0]; o->g = pl[1]; o->b = pl[2]; }
            o->a = 255;
        }
        g->mesh[p] = gfx_mesh_create(vb3, n, p == 2 ? m->bidx : (p ? m->tidx : m->idx), 3 * tcnt[p]);
        free(vb3);
    }
    g->tex = gfx_tex_create(m->tw, m->th, GFX_RGB565, m->tex, GFX_MIPS);                 /* the photo of the board */
    {                                                                /* the back of the board: 4 bit indexed picture, made from the model's parts by the bake tool */
        uint16_t *px = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)m->bw * (size_t)m->bh);
        for (int i = 0; i < m->bw * m->bh; i++) {
            int idx = (i & 1) ? (m->bpix[i >> 1] & 15) : (m->bpix[i >> 1] >> 4);
            const uint8_t *pl = m->bpal + 3 * idx;
            px[i] = (uint16_t)(((pl[0] >> 3) << 11) | ((pl[1] >> 2) << 5) | (pl[2] >> 3));
        }
        g->btex = gfx_tex_create(m->bw, m->bh, GFX_RGB565, px, GFX_MIPS);
        free(px);
    }
    g->w = getenv("NGPLAYER_CARD_W") ? atoi(getenv("NGPLAYER_CARD_W")) : (g_mobile && !NG_TV ? 512 : 1024); g->h = (int)(g->w / c3_box_aspect(mi) + 0.5f);       /* off-screen target: the thin ridges of the cases (vents, floppy slot) only look calm when the picture is made large and shrunk through the mip maps (512 / 320 flickered when turning on a big screen or in the Specs window) */
    g->color = gfx_tex_create(g->w, g->h, GFX_RGBA8, NULL, GFX_MIPS | GFX_TARGET | GFX_DEPTH);     /* target 0: the turning card of the header */
    g->color2 = gfx_tex_create(g->w, g->h, GFX_RGBA8, NULL, GFX_MIPS | GFX_TARGET | GFX_DEPTH);    /* target 1: the swinging card of the Specs window */
    g->ready = 1;
}
/* draws the card at the angle ang into the given target */
static void c3_render(int mi, float ang, gfx_tex *target, int fw, int fh) {
    C3Gl *g = &c3gl[mi]; const C3Model *m = &C3M[mi];
    float mvp[16], mv[16];
    c3_matrices(ang, m->tilt > 0 ? m->tilt : 0.17f, m->fit > 0 ? m->fit : 0.95f, (float)g->w / (float)g->h, mvp, mv);
    gfx_card_render(target, mvp, mv, g->mesh[0], g->mesh[1], g->tex, g->mesh[2], g->btex);
}
/* once per frame, before the 2D pass: advance the turn and draw the card into its texture (only while a card is shown) */
static void card3d_frame(int mi, int fw, int fh) {
    c3_setup(mi);
    static int last_mi = -1;
    const char *fix = getenv("NGPLAYER_TEST_CARDANG");
    if (fix) c3_ang = (float)atof(fix) * 0.017453293f;
    else if (!g_paused) c3_ang += (float)g_dt * 0.55f;
    else if (!c3_first && last_mi == mi) return;                                 /* paused: keep the last picture */
    c3_first = 0; last_mi = mi;
    c3_render(mi, c3_ang, c3gl[mi].color, fw, fh);
}
/* the card of the Specs window: it does not turn but swings to and fro and shows only its front */
static float c3_swing_t;
#define C3_SWING_AMP 0.50f          /* radians to each side (about 29 degrees) */
static void card3d_frame_swing(int mi, int fw, int fh) {
    c3_setup(mi);
    const char *fix = getenv("NGPLAYER_TEST_SWING");                             /* test: a fixed phase in degrees of the swing */
    if (fix) c3_swing_t = (float)atof(fix) * 0.017453293f / 1.1f; else c3_swing_t += (float)g_dt;
    c3_render(mi, C3_SWING_AMP * sinf(c3_swing_t * 1.1f), c3gl[mi].color2, fw, fh);
}
static gfx_tex *c3_texture_swing(int mi) { return c3gl[mi].color2; }
static gfx_tex *c3_texture(int mi) { return c3gl[mi].color; }
