#include <stdlib.h>
#include "period.h"
#include <math.h>
uint32_t ngp_it_pitch[120], ngp_it_slide_up[257], ngp_it_slide_dn[257],
         ngp_it_fine_up[16], ngp_it_fine_dn[16];
static const uint32_t T12[12] = {27392,25856,24384,23040,21696,20480,
                                 19328,18240,17216,16256,15360,14512};
static uint32_t rnd(double x){ return (uint32_t)(x + 0.5); }  /* Original: trunc(x+0.5) */
void ngp_it_tables_init(void){
    for (int i=-60;i<60;i++)  ngp_it_pitch[i+60]=rnd(pow(2.0,i/12.0)*65536.0);
    for (int i=0;i<257;i++){ ngp_it_slide_up[i]=rnd(pow(2.0, i/192.0)*65536.0);
                             ngp_it_slide_dn[i]=rnd(pow(2.0,-i/192.0)*65536.0);}
    for (int i=0;i<16;i++){  ngp_it_fine_up[i]=rnd(pow(2.0, i/768.0)*65536.0);
                             ngp_it_fine_dn[i]=rnd(pow(2.0,-i/768.0)*65536.0);}
}
/* c2spd==0 -> 0x32f (Original) */
uint32_t ngp_period_generic(int note, uint32_t c2spd){
    if(!c2spd) return 0x32f;
    uint64_t v=((uint64_t)T12[note%12]*0x20ab)>>(note/12);
    return (uint32_t)(v/c2spd);
}
uint32_t ngp_period_generic_k(int note, uint32_t c2spd, uint32_t k){
    if(!c2spd) return 0x32f*k;
    uint64_t v=((uint64_t)T12[note%12]*0x20ab)>>(note/12);
    return (uint32_t)((v*k)/c2spd);
}
uint32_t ngp_period_it(int note, uint32_t c2spd, int linear){
    if(!c2spd) return 0x32f;
    if(linear) return (uint32_t)(((uint64_t)ngp_it_pitch[note]*c2spd)>>16);
    uint64_t v=((uint64_t)T12[note%12]*0x4156)>>(note/12);
    return (uint32_t)(v/c2spd);
}

int ngp_tporta_normal(int cur, int target, int speed){
    int d = cur - target;
    if (d == 0 || (d<0?-d:d) < speed) return target;
    return d > 0 ? cur - speed : cur + speed;
}
int ngp_tporta_linear(int cur, int target, int speed){
    int d = cur - target;
    if (d == 0) return cur;
    if (d > 0){ cur = (int)(((uint64_t)(uint32_t)cur * ngp_it_slide_dn[speed]) >> 16);
                if (cur < target) cur = target; }
    else      { cur = (int)(((uint64_t)(uint32_t)cur * ngp_it_slide_up[speed]) >> 16);
                if (cur > target) cur = target; }
    return cur;
}
static const uint8_t VSIN[32]={0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,
                               255,253,250,244,235,224,212,197,180,161,141,120,97,74,49,24};
int ngp_vibrato_delta(int pos, int wf, int depth){
    /* gemessen am Original (ngp_calib_hef): Rampe fallend, Rechteck negativ = 0, Tiefe ohne Zwischenkuerzung */
    int idx = (pos >> 2) & 0x1f, v;           /* pos als int8 vorzeichenbehaftet */
    switch (wf & 3){
      case 0: v = VSIN[idx]; break;
      case 1: v = idx << 3; if (pos >= 0) v = 255 - v; break;
      default: v = (pos >= 0) ? 255 : 0; break;
    }
    /* gemessen (ngp_calib_hef + ICEFRONT Kanal 6/7): Betrag (v*depth + C)>>5, C=24 positiv, C=8 negativ.
       Offen: bei Tiefe 15 passt C_neg=16 besser (H8F), siehe STATUS.md */
    int d = (v * depth + (pos < 0 ? 8 : 24)) >> 5;
    return pos < 0 ? -d : d;
}
