/* AdLib Gold surround (YM7128B) and stereo (TDA8425) processing, see adgold.h. GPL-3.0-or-later */
#include "adgold.h"
#include "ym7128b/YM7128B_emu.h"
#include <math.h>
#include <stdlib.h>

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Bq;          /* biquad, transposed direct form II */
static float bq_run(Bq *f, float x) {
    double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2; f->z2 = f->b2 * x - f->a2 * y;
    return (float)y;
}
static void bq_set(Bq *f, double b0, double b1, double b2, double a0, double a1, double a2) {
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0; f->a1 = a1 / a0; f->a2 = a2 / a0;      /* the state is kept, like a filter whose parameters change */
}
static void low_shelf(Bq *f, int rate, double fc, double gain_db, double slope) {              /* RBJ cookbook */
    double A = pow(10, gain_db / 40), w0 = 2 * M_PI * fc / rate, cs = cos(w0), sn = sin(w0);
    double al = sn / 2 * sqrt((A + 1 / A) * (1 / slope - 1) + 2), sq = 2 * sqrt(A) * al;
    bq_set(f, A * ((A + 1) - (A - 1) * cs + sq), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sq),
              (A + 1) + (A - 1) * cs + sq, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sq);
}
static void high_shelf(Bq *f, int rate, double fc, double gain_db, double slope) {
    double A = pow(10, gain_db / 40), w0 = 2 * M_PI * fc / rate, cs = cos(w0), sn = sin(w0);
    double al = sn / 2 * sqrt((A + 1 / A) * (1 / slope - 1) + 2), sq = 2 * sqrt(A) * al;
    bq_set(f, A * ((A + 1) + (A - 1) * cs + sq), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sq),
              (A + 1) - (A - 1) * cs + sq, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sq);
}
static void all_pass(Bq *f, int rate, double fc, double q) {
    double w0 = 2 * M_PI * fc / rate, cs = cos(w0), al = sin(w0) / (2 * q);
    bq_set(f, 1 - al, -2 * cs, 1 + al, 1 + al, -2 * cs, 1 - al);
}

enum { MODE_MONO = 0, MODE_LINEAR = 1, MODE_PSEUDO = 2, MODE_SPATIAL = 3 };
enum { SRC_A1 = 2, SRC_A2 = 3, SRC_B1 = 4, SRC_B2 = 5 };            /* 6 and 7 stereo; the invalid 0 and 1 also mean stereo, like the Dune driver needs */

struct AdGold {
    int rate;
    YM7128B_ChipIdeal chip;
    Bq low[2], high[2], allpass;
    float gl, gr;
    int source, mode;
};

static float db_gain(double db) { return (float)pow(10.0, db / 20.0); }
static float vol_gain(int v) { double db = (v - 60) * 2.0; if (db < -128) db = -128; if (db > 6) db = 6; return db_gain(db); }
static double shelf_db(int v) { double db = (v - 6) * 3.0; if (db < -12) db = -12; if (db > 15) db = 15; return db; }

void adgold_ctrl(AdGold *g, int index, int data) {
    switch (index) {
    case 4: g->gl = vol_gain(data & 63); break;
    case 5: g->gr = vol_gain(data & 63); break;
    case 6: { double db = shelf_db(data & 15); for (int i = 0; i < 2; i++) low_shelf(&g->low[i], g->rate, 400.0, db, 0.5); } break;
    case 7: { double db = shelf_db((data & 15) + 1); for (int i = 0; i < 2; i++) high_shelf(&g->high[i], g->rate, 2500.0, db, 0.5); } break;   /* one step of extra treble, as the DOSBox emulation has it */
    case 8: g->source = data & 7; g->mode = (data >> 3) & 3; break;
    }
}

void adgold_surround(AdGold *g, int addr, int val) { YM7128B_ChipIdeal_Write(&g->chip, (YM7128B_Address)addr, (YM7128B_Register)val); }

AdGold *adgold_new(int rate) {
    AdGold *g = calloc(1, sizeof *g); if (!g) return NULL;
    g->rate = rate;
    YM7128B_ChipIdeal_Ctor(&g->chip); YM7128B_ChipIdeal_Setup(&g->chip, rate); YM7128B_ChipIdeal_Reset(&g->chip); YM7128B_ChipIdeal_Start(&g->chip);
    all_pass(&g->allpass, rate, 400.0, 1.7);
    adgold_ctrl(g, 4, 60); adgold_ctrl(g, 5, 60); adgold_ctrl(g, 6, 6); adgold_ctrl(g, 7, 6); adgold_ctrl(g, 8, (MODE_LINEAR << 3) | 6);
    return g;
}
void adgold_free(AdGold *g) { if (!g) return; YM7128B_ChipIdeal_Stop(&g->chip); YM7128B_ChipIdeal_Dtor(&g->chip); free(g); }

void adgold_process(AdGold *g, float l, float r, float *ol, float *or_) {
    YM7128B_ChipIdeal_Process_Data d = { { 0 }, { 0 } };
    d.inputs[0] = l + r;
    YM7128B_ChipIdeal_Process(&g->chip, &d);
    l += d.outputs[0] * 1.8f; r += d.outputs[1] * 1.8f;                 /* the wet signal, boosted like the DOSBox emulation does to match recordings of the hardware */
    switch (g->source) {                                                /* source selection */
    case SRC_A1: case SRC_A2: r = l; break;
    case SRC_B1: case SRC_B2: l = r; break;
    }
    l = bq_run(&g->high[0], bq_run(&g->low[0], l)); r = bq_run(&g->high[1], bq_run(&g->low[1], r));
    switch (g->mode) {
    case MODE_MONO: { float m = l + r; l = r = m; } break;
    case MODE_PSEUDO: l = bq_run(&g->allpass, l); break;
    case MODE_SPATIAL: { float k = 0.52f, a = l, b = r; l = a + (a - b) * k; r = b + (b - a) * k; } break;
    }
    *ol = l * g->gl; *or_ = r * g->gr;
}
