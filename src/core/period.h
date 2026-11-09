/* Periodenberechnung wie in dem Referenz-Player (Funktion 0x37c4c), clean-room.
 * GPL-3.0-or-later */
#pragma once
#include <stdint.h>
void ngp_it_tables_init(void);          /* einmal aufrufen (IT-Tabellen) */
uint32_t ngp_period_generic_k(int note, uint32_t c2spd, uint32_t k);
uint32_t ngp_period_generic(int note, uint32_t c2spd);   /* MOD/S3M/... */
uint32_t ngp_period_it(int note, uint32_t c2spd, int linear);
extern uint32_t ngp_it_pitch[120];      /* 2^((n-60)/12)*65536        */
extern uint32_t ngp_it_slide_up[257], ngp_it_slide_dn[257]; /* 2^(+-i/192) */
extern uint32_t ngp_it_fine_up[16], ngp_it_fine_dn[16];     /* 2^(+-i/768) */

/* Tone-Portamento und Vibrato (Original 0x38ce0 / 0x38d88 / 0x38a79).
 * Alle nur bei tick != 0 aufrufen (Original prueft [0x56d84]). */
int  ngp_tporta_normal(int cur, int target, int speed);  /* liefert neues cur */
int  ngp_tporta_linear(int cur, int target, int speed);  /* speed = Index 0..256 */
int  ngp_vibrato_delta(int pos /*int8*/, int waveform, int depth); /* signiert, addiert auf Periode */
