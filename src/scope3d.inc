/* SCOPES on the GPU (gfx.h): the CPU still finds the trigger and the gain of every voice (cheap), but only writes the 96 curve values per
 * voice into a small float texture; one instanced draw call then builds all line segments of all voices (a soft wide glow pass and a thin core pass)
 * in the vertex shader. Before, every voice's two polylines were generated on the CPU as about 1100 vertices. GPL-3.0-or-later */
#define SC_NP 96
#define SC_MAXT 64
static float sc_vals[SC_MAXT][SC_NP], sc_info[SC_MAXT][2][4];
static int sc_nt;

static void sc_begin(void) { sc_nt = 0; }
/* queue one voice: area bx..bx+bw, centre line mid, amplitude amp, np points (values already scaled to -1..1), level, colour index, visible */
static void sc_add(const float *v, int np, float bx, float mid, float bw, float amp, float lvl, int ci, int vis) {
    if (sc_nt >= SC_MAXT) return;
    for (int j = 0; j < SC_NP; j++) sc_vals[sc_nt][j] = j < np ? v[j] : 0;
    sc_info[sc_nt][0][0] = bx + GFX_OX; sc_info[sc_nt][0][1] = mid + GFX_OY; sc_info[sc_nt][0][2] = bw; sc_info[sc_nt][0][3] = amp;
    sc_info[sc_nt][1][0] = lvl; sc_info[sc_nt][1][1] = (float)ci; sc_info[sc_nt][1][2] = (float)np; sc_info[sc_nt][1][3] = (float)vis;
    sc_nt++;
}
static void sc_draw(float lw) {
    if (!sc_nt) return;
    flush();
    float c[9] = { VC[0].r, VC[0].g, VC[0].b, VC[1].r, VC[1].g, VC[1].b, VC[2].r, VC[2].g, VC[2].b };
    gfx_scope_draw(&sc_vals[0][0], &sc_info[0][0][0], sc_nt, lw, c);      /* glow pass and core pass, in the backend */
    add_blend = 0;
}
