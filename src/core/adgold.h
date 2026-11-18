/* AdLib Gold: the optional processors behind the OPL3 - the Yamaha YM7128B surround / reverb processor and the Philips TDA8425 stereo processor
 * (volume, bass, treble, stereo mode). Modelled after the DOSBox Staging emulation (GPL-2.0-or-later); the YM7128B itself is Andrea Zoppi's emulation (BSD-2-Clause). GPL-3.0-or-later */
#pragma once
typedef struct AdGold AdGold;
AdGold *adgold_new(int rate);
void adgold_free(AdGold *g);
void adgold_ctrl(AdGold *g, int index, int val);                /* control register of the card: 4 / 5 volume left / right, 6 bass, 7 treble, 8 switch functions */
void adgold_surround(AdGold *g, int addr, int val);             /* register 0..30 of the YM7128B */
void adgold_process(AdGold *g, float l, float r, float *ol, float *or_);   /* one stereo frame of the FM output, sample values in the 16 bit range */
