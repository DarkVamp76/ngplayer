/* Tabellen der Tracker-Formate (kleine Standardwerte), GPL-3.0-or-later:
 *  - ngp_pt_periods: ProTracker-Periodentabelle, Finetune 0, drei Oktaven (MOD-Spezifikation)
 *  - ngp_finetune_c2spd: Scream-Tracker-3-C2Spd je Finetune (S3M-Spezifikation)
 *  - ngp_vib_sine: 32-stufige Vibrato-Sinustabelle (ProTracker)
 *  - ngp_t12: Notenfrequenz-Faktoren je Halbton, ngp_pan_tab: Panning 17 * n */
#pragma once
#include <stdint.h>
static const uint16_t ngp_pt_periods[84] = {1712,1616,1524,1440,1356,1280,1208,1140,1076,1016,960,906,856,808,762,720,678,640,604,570,538,508,480,453,428,404,381,360,339,320,302,285,269,254,240,226,214,202,190,180,170,160,151,143,135,127,120,113,107,101,95,90,85,80,75,71,67,63,60,56,53,50,47,45,42,40,37,35,33,31,30,28,27,25,24,22,21,20,19,18,17,16,15,14};
static const uint32_t ngp_finetune_c2spd[16] = {8363,8413,8463,8529,8581,8651,8723,8757,7895,7941,7985,8046,8107,8169,8232,8280};
static const uint16_t ngp_vib_sine[32] = {0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,253,250,244,235,224,212,197,180,161,141,120,97,74,49,24};
static const uint32_t ngp_t12[12] = {27392,25856,24384,23040,21696,20480,19328,18240,17216,16256,15360,14512};
static const uint16_t ngp_pan_tab[16] = {0,17,34,51,68,85,102,119,136,153,170,187,204,221,238,255};
