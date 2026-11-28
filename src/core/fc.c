/* Future Composer (Amiga): loader + replayer, a port of the NostalgicPlayer FutureComposerWorker (FC 1.4) and of the
   FutureComposer13Format module converter (FC 1.0-1.3 "SMOD" -> FC 1.4, done in memory before loading), MIT.
   Paula-like mixer in the style of sa.c / aon.c. C# integer semantics are kept (ushort/short/sbyte wrap, truncating casts).
   Timing: the worker never sets PlayingFrequency, so Play() runs at the default 50 Hz (PAL vblank). */
#include <stdlib.h>
#include <string.h>
#include "fc.h"

#define PAULA_PAL 3546895.0
#define FC_TICK_HZ 50.0
#define FC_LOOP_GUARD 4096                                          /* C# would hang in a sequence that loops on itself */

/* ---------------------------------------------------------------- tables (Tables.cs) */
static const uint16_t fc_periods[132] = {
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  906,
     856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453,
     428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,
     214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
     113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,
    3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1812,
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  906,
     856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453,
     428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,
     214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
     113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113
};
static const uint8_t fc_silent[8] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe1 };
static const uint8_t pan4[4] = { 0, 1, 1, 0 };                    /* 0 = left, 1 = right */

/* FutureComposer13Format tables */
static const uint8_t smod_wave_length[80] = {
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x10, 0x08, 0x10, 0x10, 0x08, 0x08, 0x18, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t smod_wave_tables[1344] = {
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0x3f, 0x37, 0x2f, 0x27, 0x1f, 0x17, 0x0f, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0x37, 0x2f, 0x27, 0x1f, 0x17, 0x0f, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0x2f, 0x27, 0x1f, 0x17, 0x0f, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0x27, 0x1f, 0x17, 0x0f, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0x1f, 0x17, 0x0f, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x17, 0x0f, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x0f, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x07, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0xff, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0x80, 0x07, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x0f, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x17, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0x1f, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0xa0, 0x27, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0xa0, 0xa8, 0x2f, 0x37,
    0xc0, 0xc0, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x00, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8, 0xd0, 0xc8,
    0xc0, 0xb8, 0xb0, 0xa8, 0xa0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0x37,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f, 0x7f,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f, 0x7f,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x80, 0x80, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8, 0xc0, 0xc8, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8,
    0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x7f,
    0x80, 0x80, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
    0x45, 0x45, 0x79, 0x7d, 0x7a, 0x77, 0x70, 0x66, 0x61, 0x58, 0x53, 0x4d, 0x2c, 0x20, 0x18, 0x12,
    0x04, 0xdb, 0xd3, 0xcd, 0xc6, 0xbc, 0xb5, 0xae, 0xa8, 0xa3, 0x9d, 0x99, 0x93, 0x8e, 0x8b, 0x8a,
    0x45, 0x45, 0x79, 0x7d, 0x7a, 0x77, 0x70, 0x66, 0x5b, 0x4b, 0x43, 0x37, 0x2c, 0x20, 0x18, 0x12,
    0x04, 0xf8, 0xe8, 0xdb, 0xcf, 0xc6, 0xbe, 0xb0, 0xa8, 0xa4, 0x9e, 0x9a, 0x95, 0x94, 0x8d, 0x83,
    0x00, 0x00, 0x40, 0x60, 0x7f, 0x60, 0x40, 0x20, 0x00, 0xe0, 0xc0, 0xa0, 0x80, 0xa0, 0xc0, 0xe0,
    0x00, 0x00, 0x40, 0x60, 0x7f, 0x60, 0x40, 0x20, 0x00, 0xe0, 0xc0, 0xa0, 0x80, 0xa0, 0xc0, 0xe0,
    0x80, 0x80, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8, 0xc0, 0xc8, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8,
    0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x7f,
    0x80, 0x80, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
};
/* ---------------------------------------------------------------- song */
typedef struct FcSmp {
    int8_t *addr; uint16_t len, loop_start, loop_len;
    struct FcSmp *multi;                                            /* [20] or NULL */
} FcSmp;
typedef struct { uint8_t pat[4]; int8_t tr[4], str[4]; uint8_t speed; } FcSeq;
typedef struct { uint8_t speed, frq_number; int8_t vib_speed, vib_depth; uint8_t vib_delay; uint8_t values[59]; } FcVol;

struct FcSong {
    int version;
    FcSmp smp[90];
    int16_t seq_num, pat_num, vol_num;
    int nseq;  FcSeq *seq;                                          /* nseq = max(seq_num, 1) */
    uint8_t *pat;                                                   /* [pat_num][32][2] (note, info) */
    int frq_len; uint8_t *frq;
    FcVol *vol;                                                     /* [1 + vol_num] */
    int nsub; int *sub_start;
};

/* ---------------------------------------------------------------- player */
typedef struct {
    int8_t pitch_bend_speed; uint8_t pitch_bend_time;
    uint16_t song_pos; int8_t cur_note;
    int volume_seq;                                                 /* VolumeSeq = vol[volume_seq].values */
    uint8_t volume_bend_speed, volume_bend_time;
    uint16_t volume_seq_pos;
    int8_t sound_transpose;
    uint8_t volume_counter, volume_speed, vol_sus_counter, sus_counter;
    int8_t vib_speed, vib_depth, vib_value, vib_delay;
    int cur_pattern;                                                /* CurPattern (index) */
    int vol_bend_flag, port_flag;
    uint16_t pattern_pos;
    int pitch_bend_flag;
    int8_t patt_transpose, transpose, volume;
    uint8_t vib_flag, portamento;
    uint16_t frequency_seq_start_offset, frequency_seq_pos, pitch;
} FcVoice;

typedef struct {
    const int8_t *d; uint32_t len; double pos; int on;
    const int8_t *ld; uint32_t llen;                                /* SetLoop: plays when the current pass ends */
    const int8_t *pd; uint32_t plen; int pend;                      /* SetSample: plays when the current pass ends */
    uint32_t period; int volume;
} FcMix;

struct FcPlayer {
    const FcSong *s;
    int rate, sub;
    uint16_t re_sp_cnt, rep_spd, spd_temp; int aud_temp[4];         /* GlobalPlayingInfo */
    FcVoice v[4];
    int end_reached, ended;
    uint8_t *visited;
    int shown_pos, shown_row;
    FcMix m[4];
    double tick_left;
    unsigned mute;
    float *sc; int sc_len; unsigned sc_pos;
};

/* ---------------------------------------------------------------- stream (ModuleStream; EndOfStream = a read came up short) */
typedef struct { const uint8_t *b; size_t n; long long pos; int eof; } Rd;

static size_t rd_read(Rd *r, uint8_t *dst, size_t cnt) {
    size_t got = 0;
    if (r->pos >= 0 && (unsigned long long)r->pos < r->n) { got = r->n - (size_t)r->pos; if (got > cnt) got = cnt; if (dst) memcpy(dst, r->b + r->pos, got); }
    if (dst && got < cnt) memset(dst + got, 0, cnt - got);
    r->pos += (long long)cnt;
    if (r->pos > (long long)r->n) r->pos = (long long)r->n;
    if (got < cnt) r->eof = 1;
    return got;
}
static uint8_t rd_u8(Rd *r) { uint8_t x; rd_read(r, &x, 1); return x; }
static uint16_t rd_b16(Rd *r) { uint8_t x[2]; rd_read(r, x, 2); return (uint16_t)(x[0] << 8 | x[1]); }
static uint32_t rd_b32(Rd *r) { uint8_t x[4]; rd_read(r, x, 4); return (uint32_t)x[0] << 24 | (uint32_t)x[1] << 16 | (uint32_t)x[2] << 8 | x[3]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* growable output (ConverterStream) */
typedef struct { uint8_t *b; size_t n, cap, pos; int err; } Wr;
static void wr_reserve(Wr *w, size_t need) {
    if (w->err || need <= w->cap) return;
    size_t c = w->cap ? w->cap : 4096;
    while (c < need) c *= 2;
    uint8_t *nb = (uint8_t *)realloc(w->b, c);
    if (!nb) { w->err = 1; return; }
    memset(nb + w->cap, 0, c - w->cap);
    w->b = nb; w->cap = c;
}
static void wr_bytes(Wr *w, const uint8_t *src, size_t cnt) {
    wr_reserve(w, w->pos + cnt);
    if (w->err) return;
    if (src) memcpy(w->b + w->pos, src, cnt); else memset(w->b + w->pos, 0, cnt);
    w->pos += cnt;
    if (w->pos > w->n) w->n = w->pos;
}
static void wr_u8(Wr *w, uint8_t x) { wr_bytes(w, &x, 1); }
static void wr_b16(Wr *w, uint16_t x) { uint8_t t[2] = { (uint8_t)(x >> 8), (uint8_t)x }; wr_bytes(w, t, 2); }
static void wr_b32(Wr *w, uint32_t x) { uint8_t t[4] = { (uint8_t)(x >> 24), (uint8_t)(x >> 16), (uint8_t)(x >> 8), (uint8_t)x }; wr_bytes(w, t, 4); }

/* StreamHelper.CopyDataForceLength: copies len bytes, zeros where the source ends (the short read sets EndOfStream) */
static void copy_force(Rd *r, Wr *w, size_t len) {
    wr_reserve(w, w->pos + len);
    if (w->err) return;
    rd_read(r, w->b + w->pos, len);
    w->pos += len;
    if (w->pos > w->n) w->n = w->pos;
}

/* ---------------------------------------------------------------- identify */
static int ident_offsets(const uint8_t *b, size_t n, const char *mark, size_t min) {
    if (!b || n < min || memcmp(b, mark, 4) != 0) return 0;
    for (int i = 0; i < 8; i++) if (be32(b + 8 + i * 4) > n) return 0;
    return 1;
}
static int is_fc14(const uint8_t *b, size_t n) { return ident_offsets(b, n, "FC14", 180); }
static int is_smod(const uint8_t *b, size_t n) { return ident_offsets(b, n, "SMOD", 100); }

int fc_probe(const uint8_t *b, size_t n) { return is_fc14(b, n) || is_smod(b, n); }

/* ---------------------------------------------------------------- FutureComposer13Format.Convert */
static uint8_t *smod_convert(const uint8_t *b, size_t n, size_t *out_n) {
    Rd r = { b, n, 4, 0 };
    Wr w = { NULL, 0, 0, 0, 0 };
    uint32_t offs[8], nofs[8] = { 0 };
    uint16_t sample_lengths[10];

    wr_bytes(&w, (const uint8_t *)"FC14", 4);
    uint32_t seq_length = rd_b32(&r);
    for (int i = 0; i < 8; i++) offs[i] = rd_b32(&r);
    if (seq_length == 0) seq_length = offs[0] - 0x64;                /* cult.fc fix */
    wr_b32(&w, (seq_length % 2) != 0 ? seq_length + 1 : seq_length);
    wr_bytes(&w, NULL, 8 * 4);                                         /* Seek(8 * 4, Current) */
    for (int i = 0; i < 10; i++) {
        sample_lengths[i] = rd_b16(&r);
        wr_b16(&w, sample_lengths[i]);
        wr_b16(&w, rd_b16(&r));
        wr_b16(&w, rd_b16(&r));
    }
    if (r.eof) goto fail;
    wr_bytes(&w, smod_wave_length, 80);

    /* (int)seqLength: a negative count is not defined here (C# helper unknown); huge ones are refused (memory) */
    if ((int32_t)seq_length < 0 || seq_length > 0x4000000u) goto fail;
    copy_force(&r, &w, seq_length);

    nofs[0] = (uint32_t)w.pos;
    if ((nofs[0] % 2) != 0) { nofs[0]++; wr_u8(&w, 0); }
    nofs[1] = offs[1];
    {
        /* C#: for (int i = 1; i < offsetsAndLength[1] - 2; ...) is uint arithmetic; a length below 2 wraps and the
           first pattBuf[i] access throws (IndexOutOfRange) -> fail */
        if (offs[1] < 2) goto fail;
        uint8_t *patt_buf = (uint8_t *)calloc(offs[1], 1);
        if (!patt_buf) goto fail;
        r.pos = offs[0];
        rd_read(&r, patt_buf, offs[1]);
        for (long long i = 1; i < (long long)(offs[1] - 2); i += 2) {
            if ((patt_buf[i] & 0x80) != 0)
                patt_buf[i + 2] = (uint8_t)((((patt_buf[i + 2] & 0x1f) * 2) & 0x1f) | (patt_buf[i + 2] & 0x20));
        }
        wr_bytes(&w, patt_buf, offs[1]);
        free(patt_buf);
    }

    nofs[2] = (uint32_t)w.pos; nofs[3] = offs[3];
    r.pos = offs[2]; copy_force(&r, &w, offs[3]);
    nofs[4] = (uint32_t)w.pos; nofs[5] = offs[5];
    r.pos = offs[4]; copy_force(&r, &w, offs[5]);
    if (r.eof || w.err) goto fail;

    nofs[6] = (uint32_t)w.pos;
    r.pos = offs[6];
    for (int i = 0; i < 10; i++) {
        int length = sample_lengths[i] * 2;
        if (length != 0) {
            if ((long long)n - r.pos < (long long)(length - 256)) goto fail;   /* too much of the last sample missing */
            /* SetSampleDataMarker: the loader later reads these bytes from the original file (short = zeros, no
               EndOfStream); here they are copied into the converted image */
            wr_reserve(&w, w.pos + (size_t)length);
            if (w.err) goto fail;
            memset(w.b + w.pos, 0, (size_t)length);
            if (r.pos < (long long)n) {
                size_t got = n - (size_t)r.pos; if (got > (size_t)length) got = (size_t)length;
                memcpy(w.b + w.pos, b + r.pos, got);
            }
            w.pos += (size_t)length; if (w.pos > w.n) w.n = w.pos;
            r.pos += length;                                           /* the module stream moves past the sample */
        }
        wr_b16(&w, 0);                                                 /* pad bytes */
    }
    nofs[7] = (uint32_t)w.pos;
    wr_bytes(&w, smod_wave_tables, sizeof smod_wave_tables);
    if (w.err) goto fail;
    w.pos = 8;
    for (int i = 0; i < 8; i++) wr_b32(&w, nofs[i]);
    *out_n = w.n;
    return w.b;
fail:
    free(w.b);
    return NULL;
}

/* ---------------------------------------------------------------- Load (FC14) */
static void free_smp(FcSmp *sm) {
    free(sm->addr);
    if (sm->multi) { for (int j = 0; j < 20; j++) free(sm->multi[j].addr); free(sm->multi); }
}

void fc_free(FcSong *s) {
    if (!s) return;
    for (int i = 0; i < 90; i++) free_smp(&s->smp[i]);
    free(s->seq); free(s->pat); free(s->frq); free(s->vol); free(s->sub_start); free(s);
}

/* ReadSampleData(length): a short read zero fills and sets EndOfStream */
static int8_t *read_sample(Rd *r, uint16_t len) {
    int8_t *d = (int8_t *)malloc(len ? len : 1);
    if (d) rd_read(r, (uint8_t *)d, len);
    return d;
}

static int load_fc14(FcSong *s, const uint8_t *b, size_t n) {
    Rd r = { b, n, 4, 0 };
    int32_t seq_length = (int32_t)rd_b32(&r);
    int32_t pat_offset = (int32_t)rd_b32(&r), pat_length = (int32_t)rd_b32(&r);
    int32_t frq_offset = (int32_t)rd_b32(&r), frq_length = (int32_t)rd_b32(&r);
    int32_t vol_offset = (int32_t)rd_b32(&r), vol_length = (int32_t)rd_b32(&r);
    int32_t smp_offset = (int32_t)rd_b32(&r), wav_offset = (int32_t)rd_b32(&r);
    if (r.eof) return 0;
    int i;
    for (i = 0; i < 10; i++) {
        FcSmp *sm = &s->smp[i];
        sm->len = (uint16_t)(rd_b16(&r) * 2);
        sm->loop_start = rd_b16(&r);
        sm->loop_len = (uint16_t)(rd_b16(&r) * 2);
        if (sm->loop_start >= sm->len) { sm->loop_start = 0; sm->loop_len = 2; }
    }
    for (i = 10; i < 90; i++) {
        FcSmp *sm = &s->smp[i];
        sm->len = (uint16_t)(rd_u8(&r) * 2);
        sm->loop_start = 0; sm->loop_len = sm->len;
    }
    if (r.eof) return 0;

    /* sequences (directly after the header) */
    s->seq_num = (int16_t)(seq_length / 13);
    if (s->seq_num < 0) return 0;                                       /* new Sequence[negative] -> OverflowException */
    s->nseq = s->seq_num == 0 ? 1 : s->seq_num;
    s->seq = (FcSeq *)calloc((size_t)s->nseq, sizeof(FcSeq));
    if (!s->seq) return 0;
    for (i = 0; i < s->seq_num; i++) {
        FcSeq *q = &s->seq[i];
        for (int c = 0; c < 4; c++) { q->pat[c] = rd_u8(&r); q->tr[c] = (int8_t)rd_u8(&r); q->str[c] = (int8_t)rd_u8(&r); }
        q->speed = rd_u8(&r);
    }

    /* patterns */
    s->pat_num = (int16_t)(pat_length / 64);
    if (s->pat_num < 0) return 0;
    s->pat = (uint8_t *)calloc((size_t)(s->pat_num ? s->pat_num : 1) * 64, 1);
    if (!s->pat) return 0;
    r.pos = pat_offset;
    rd_read(&r, s->pat, (size_t)s->pat_num * 64);
    if (r.eof) return 0;

    /* frequency sequences: silent sequence + data + end mark */
    if (frq_length < 0 || frq_length > 0x7ffffff0) return 0;
    s->frq_len = 8 + frq_length + 1;
    s->frq = (uint8_t *)calloc((size_t)s->frq_len, 1);
    if (!s->frq) return 0;
    memcpy(s->frq, fc_silent, 8);
    r.pos = frq_offset;
    rd_read(&r, s->frq + 8, (size_t)frq_length);
    s->frq[s->frq_len - 1] = 0xe1;

    /* volume sequences */
    s->vol_num = (int16_t)(vol_length / 64);
    if (s->vol_num < 0) return 0;
    s->vol = (FcVol *)calloc((size_t)(1 + s->vol_num), sizeof(FcVol));
    if (!s->vol) return 0;
    s->vol[0].speed = fc_silent[0]; s->vol[0].frq_number = fc_silent[1];
    s->vol[0].vib_speed = (int8_t)fc_silent[2]; s->vol[0].vib_depth = (int8_t)fc_silent[3]; s->vol[0].vib_delay = fc_silent[4];
    memcpy(s->vol[0].values, fc_silent + 4, 4);
    r.pos = vol_offset;
    for (i = 1; i <= s->vol_num; i++) {
        FcVol *vs = &s->vol[i];
        vs->speed = rd_u8(&r); vs->frq_number = rd_u8(&r);
        vs->vib_speed = (int8_t)rd_u8(&r); vs->vib_depth = (int8_t)rd_u8(&r); vs->vib_delay = rd_u8(&r);
        rd_read(&r, vs->values, 59);
    }
    if (r.eof) return 0;

    /* samples */
    r.pos = smp_offset;
    for (i = 0; i < 10; i++) {
        FcSmp *sm = &s->smp[i];
        if (sm->len != 0) {
            uint8_t mk[4];
            rd_read(&r, mk, 4);
            if (memcmp(mk, "SSMP", 4) == 0) {
                FcSmp *ms = (FcSmp *)calloc(20, sizeof(FcSmp));
                uint32_t moff[20];
                if (!ms) return 0;
                sm->multi = ms;
                for (int j = 0; j < 20; j++) {
                    moff[j] = rd_b32(&r);
                    ms[j].len = (uint16_t)(rd_b16(&r) * 2);
                    ms[j].loop_start = rd_b16(&r);
                    ms[j].loop_len = (uint16_t)(rd_b16(&r) * 2);
                    r.pos += 6;                                            /* Seek(6, Current): no EndOfStream */
                }
                long long start = r.pos;
                for (int j = 0; j < 20; j++) {
                    if (ms[j].len != 0) {
                        r.pos = start + moff[j];
                        ms[j].addr = read_sample(&r, ms[j].len);
                        if (!ms[j].addr) return 0;
                        rd_b16(&r);
                        if (r.eof) return 0;
                    }
                }
            } else {
                r.pos -= 4;
                if (r.pos < 0) r.pos = 0;
                sm->addr = read_sample(&r, sm->len);
                if (!sm->addr) return 0;
            }
        }
        rd_b16(&r);                                                        /* pad bytes */
        if (r.eof) return 0;
    }

    /* wave tables */
    r.pos = wav_offset;
    for (i = 10; i < 90; i++) {
        FcSmp *sm = &s->smp[i];
        if (sm->len != 0) {
            sm->addr = read_sample(&r, sm->len);
            if (!sm->addr) return 0;
            if (r.eof) return 0;
        }
    }
    return 1;
}

/* InitializeSound reads patterns[sequences[pos].VoiceSeq[i].Pattern] without a range check (throws in C#) */
static int init_ok(const FcSong *s, int pos) {
    if (pos < 0 || pos >= s->nseq) return 0;
    for (int c = 0; c < 4; c++) if (s->seq[pos].pat[c] >= s->pat_num) return 0;
    return 1;
}

static const uint8_t *pat_row(const FcSong *s, int pat, int row) { return s->pat + ((size_t)pat * 32 + (size_t)row) * 2; }

/* Sub-songs as ModulePlayerWithPositionDurationAgentBase finds them: play from a start position until voice 0 comes
   to a visited position (endReached), then start again at the first position not visited yet. Only the position
   sequencing of voice 0 decides this (NewNote), so it is simulated row by row. */
static int find_subsongs(FcSong *s) {
    int total = s->seq_num;                                             /* GetTotalNumberOfPositions */
    uint8_t *vis = (uint8_t *)calloc((size_t)(s->nseq + 1), 1);
    s->sub_start = (int *)malloc(sizeof(int) * (size_t)(s->nseq + 1));
    s->nsub = 0;
    if (!vis || !s->sub_start) { free(vis); return 0; }
    int start = 0;
    for (;;) {
        if (init_ok(s, start)) {
            s->sub_start[s->nsub++] = start;
            vis[start] = 1;                                             /* InitDuration: MarkPositionAsVisited */
            uint16_t song_pos = (uint16_t)start, pp = 0;
            int pat = s->seq[start].pat[0];
            for (;;) {
                if (pp == 32 || pat_row(s, pat, pp)[0] == 0x49) {
                    song_pos++; pp = 0;
                    if (song_pos >= s->seq_num) song_pos = 0;
                    int pn = s->seq[song_pos].pat[0];
                    if (pn >= s->pat_num) pn = 0;
                    pat = pn;
                    int was = vis[song_pos];
                    vis[song_pos] = 1;
                    if (was) break;
                }
                pp++;
            }
        } else vis[start] = 1;                                          /* C# would throw: no song there */
        while (start < total && vis[start]) start++;
        if (start >= total) break;
    }
    free(vis);
    return s->nsub > 0;
}

FcSong *fc_load(const uint8_t *b, size_t n, int *err) {
    if (err) *err = 0;
    if (!fc_probe(b, n)) { if (err) *err = 1; return NULL; }
    FcSong *s = (FcSong *)calloc(1, sizeof *s);
    if (!s) { if (err) *err = 2; return NULL; }
    int ok;
    if (is_fc14(b, n)) { s->version = 14; ok = load_fc14(s, b, n); }
    else {
        size_t cn = 0;
        uint8_t *c = smod_convert(b, n, &cn);
        s->version = 13;
        ok = c != NULL && is_fc14(c, cn) && load_fc14(s, c, cn);
        free(c);
    }
    /* patterns[] empty or the first position names a missing pattern: InitSound would throw */
    if (!ok || s->pat_num == 0 || !init_ok(s, 0) || !find_subsongs(s)) { fc_free(s); if (err) *err = 3; return NULL; }
    return s;
}

/* ---------------------------------------------------------------- mixer channel (IChannel) */
static void ch_play_sample(FcMix *m, const int8_t *data, uint32_t len) {
    m->d = data; m->len = len; m->pos = 0; m->on = len > 0;
    m->ld = NULL; m->llen = 0; m->pd = NULL; m->plen = 0; m->pend = 0;
}
static void ch_set_loop(FcMix *m, const int8_t *base, uint32_t start, uint32_t len) { m->ld = base + start; m->llen = len; }
static void ch_set_sample(FcMix *m, const int8_t *base, uint32_t start, uint32_t len) {
    if (len) { m->pd = base + start; m->plen = len; m->pend = 1; }
}
static void ch_mute(FcMix *m) { m->on = 0; }

/* PlaySample + SetLoop of the 0xe2 / 0xe9 commands */
static void play_smp(FcMix *c, const FcSmp *sm) {
    ch_play_sample(c, sm->addr, sm->len);
    if (sm->loop_len > 2) {
        if (sm->loop_start >= sm->len) return;                          /* multi sample: (uint)(negative) length, C# would throw */
        if (sm->loop_start + sm->loop_len > sm->len) ch_set_loop(c, sm->addr, sm->loop_start, (uint32_t)(sm->len - sm->loop_start));
        else ch_set_loop(c, sm->addr, sm->loop_start, sm->loop_len);
    }
}

/* ---------------------------------------------------------------- InitializeSound */
static void initialize_sound(FcPlayer *p, int start) {
    const FcSong *s = p->s;
    uint16_t spd = s->seq[start].speed;
    if (spd == 0) spd = 3;
    p->re_sp_cnt = spd; p->rep_spd = spd; p->spd_temp = 1;
    memset(p->aud_temp, 0, sizeof p->aud_temp);
    p->end_reached = 0;
    memset(p->v, 0, sizeof p->v);
    for (int i = 0; i < 4; i++) {
        FcVoice *v = &p->v[i];
        v->song_pos = (uint16_t)start;
        v->volume_seq = 0;
        v->volume_counter = 1; v->volume_speed = 1;
        v->cur_pattern = s->seq[start].pat[i];
        v->transpose = s->seq[start].tr[i];
        v->sound_transpose = s->seq[start].str[i];
    }
    p->shown_pos = start; p->shown_row = 0;
}

/* ---------------------------------------------------------------- NewNote */
static void new_note(FcPlayer *p, int chan) {
    const FcSong *s = p->s;
    FcVoice *v = &p->v[chan];
    FcMix *c = &p->m[chan];

    if (v->pattern_pos == 32 || pat_row(s, v->cur_pattern, v->pattern_pos)[0] == 0x49) {
        v->song_pos++;
        v->pattern_pos = 0;
        if (v->song_pos >= s->seq_num) v->song_pos = 0;
        p->spd_temp++;
        if (p->spd_temp == 5) {
            p->spd_temp = 1;
            if (s->seq[v->song_pos].speed != 0) { p->re_sp_cnt = s->seq[v->song_pos].speed; p->rep_spd = p->re_sp_cnt; }
        }
        v->transpose = s->seq[v->song_pos].tr[chan];
        v->sound_transpose = s->seq[v->song_pos].str[chan];
        uint8_t patt_num = s->seq[v->song_pos].pat[chan];
        if (patt_num >= s->pat_num) patt_num = 0;
        v->cur_pattern = patt_num;
        if (chan == 0) {
            if (p->visited[v->song_pos]) p->end_reached = 1;
            p->visited[v->song_pos] = 1;
        }
    }

    const uint8_t *row = pat_row(s, v->cur_pattern, v->pattern_pos);
    uint8_t note = row[0], info = row[1];
    if (chan == 0) { p->shown_pos = v->song_pos; p->shown_row = v->pattern_pos; }

    if (note != 0 || (info & 0xc0) != 0) {
        if (note != 0) v->pitch = 0;
        if ((info & 0x80) != 0) v->portamento = v->pattern_pos < 31 ? pat_row(s, v->cur_pattern, v->pattern_pos + 1)[1] : 0;
        else v->portamento = 0;
    }

    note &= 0x7f;
    if (note != 0) {
        v->cur_note = (int8_t)note;
        p->aud_temp[chan] = 0;
        ch_mute(c);
        uint8_t inst = (uint8_t)((info & 0x3f) + v->sound_transpose);
        if (inst >= s->vol_num) inst = 0; else inst++;
        const FcVol *vs = &s->vol[inst];
        v->volume_seq_pos = 0;
        v->volume_counter = vs->speed; v->volume_speed = vs->speed;
        v->vol_sus_counter = 0;
        v->vib_speed = vs->vib_speed;
        v->vib_flag = 0x40;
        v->vib_depth = vs->vib_depth; v->vib_value = vs->vib_depth;
        v->vib_delay = (int8_t)vs->vib_delay;
        v->volume_seq = inst;
        v->frequency_seq_start_offset = (uint16_t)(8 + vs->frq_number * 64);
        v->frequency_seq_pos = 0;
        v->sus_counter = 0;
    }
    v->pattern_pos++;
}

/* ---------------------------------------------------------------- Effect */
/* frqSequences[] / VolumeSeq[] reads outside the array throw in C#; they read 0 here */
static uint8_t frq_at(const FcSong *s, unsigned i) { return i < (unsigned)s->frq_len ? s->frq[i] : 0; }
static uint8_t vol_at(const uint8_t *vs, unsigned i) { return i < 59 ? vs[i] : 0; }

static void do_vol_bend(FcVoice *v) {
    v->vol_bend_flag = !v->vol_bend_flag;
    if (v->vol_bend_flag) {
        v->volume_bend_time--;
        v->volume = (int8_t)(v->volume + (int8_t)v->volume_bend_speed);
        if (v->volume > 64) { v->volume = 64; v->volume_bend_time = 0; }
        else if (v->volume < 0) { v->volume = 0; v->volume_bend_time = 0; }
    }
}

static void effect(FcPlayer *p, int chan) {
    const FcSong *s = p->s;
    FcVoice *v = &p->v[chan];
    FcMix *c = &p->m[chan];
    int guard = 0;

    /* frequency sequence */
    int one_more;
    do {
        one_more = 0;
        if (v->sus_counter != 0) { v->sus_counter--; break; }
        uint16_t seq_poi = (uint16_t)(v->frequency_seq_start_offset + v->frequency_seq_pos);
        if (seq_poi >= s->frq_len) break;
        int parse_effect;
        do {
            uint8_t dat;
            parse_effect = 0;
            uint8_t cmd = frq_at(s, seq_poi++);
            if (cmd == 0xe1) break;
            if (cmd == 0xe0) {
                dat = (uint8_t)(frq_at(s, seq_poi) & 0x3f);
                v->frequency_seq_pos = dat;
                seq_poi = (uint16_t)(v->frequency_seq_start_offset + dat);
                cmd = frq_at(s, seq_poi++);
            }
            switch (cmd) {
                case 0xe2:                                              /* set wave form */
                    dat = frq_at(s, seq_poi++);
                    if (dat < 90 && s->smp[dat].addr != NULL) play_smp(c, &s->smp[dat]);
                    v->volume_seq_pos = 0;
                    v->volume_counter = 1;
                    v->frequency_seq_pos += 2;
                    p->aud_temp[chan] = 1;
                    break;
                case 0xe4:                                              /* set loop */
                    if (p->aud_temp[chan]) {
                        dat = frq_at(s, seq_poi++);
                        if (dat < 90) {
                            const FcSmp *sm = &s->smp[dat];
                            /* SetSample(null) (a multi sample slot or an empty one) is not defined here: skipped;
                               a loop past the sample end would make the mixer read outside: clipped */
                            if (sm->addr && sm->loop_start < sm->len) {
                                uint32_t ll = sm->loop_len;
                                if ((uint32_t)sm->loop_start + ll > sm->len) ll = (uint32_t)(sm->len - sm->loop_start);
                                ch_set_sample(c, sm->addr, sm->loop_start, ll);
                                ch_set_loop(c, sm->addr, sm->loop_start, ll);
                            }
                        }
                        v->frequency_seq_pos += 2;
                    }
                    break;
                case 0xe9:                                              /* set sample (multi sample) */
                    p->aud_temp[chan] = 1;
                    dat = frq_at(s, seq_poi++);
                    if (dat < 90 && s->smp[dat].multi != NULL) {
                        const FcSmp *ms = s->smp[dat].multi;
                        dat = frq_at(s, seq_poi++);
                        if (dat < 20 && ms[dat].addr != NULL) play_smp(c, &ms[dat]);
                        v->volume_seq_pos = 0;
                        v->volume_counter = 1;
                    }
                    v->frequency_seq_pos += 3;
                    break;
                case 0xe7:                                              /* pattern jump */
                    parse_effect = 1;
                    dat = frq_at(s, seq_poi);
                    seq_poi = (uint16_t)(8 + dat * 64);
                    if (seq_poi >= s->frq_len) seq_poi = 0;
                    v->frequency_seq_start_offset = seq_poi;
                    v->frequency_seq_pos = 0;
                    break;
                case 0xea:                                              /* pitch bend */
                    v->pitch_bend_speed = (int8_t)frq_at(s, seq_poi++);
                    v->pitch_bend_time = frq_at(s, seq_poi++);
                    v->frequency_seq_pos += 3;
                    break;
                case 0xe8:                                              /* new sustain */
                    v->sus_counter = frq_at(s, seq_poi++);
                    v->frequency_seq_pos += 2;
                    one_more = 1;
                    break;
                case 0xe3:                                              /* new vibrato */
                    v->vib_speed = (int8_t)frq_at(s, seq_poi++);
                    v->vib_depth = (int8_t)frq_at(s, seq_poi++);
                    v->frequency_seq_pos += 3;
                    break;
            }
            if (!parse_effect && !one_more) {
                seq_poi = (uint16_t)(v->frequency_seq_start_offset + v->frequency_seq_pos);
                v->patt_transpose = (int8_t)frq_at(s, seq_poi);
                v->frequency_seq_pos++;
            }
            if (++guard > FC_LOOP_GUARD) { parse_effect = 0; one_more = 0; }
        } while (parse_effect);
    } while (one_more);

    /* volume sequence */
    if (v->vol_sus_counter != 0) v->vol_sus_counter--;
    else if (v->volume_bend_time != 0) do_vol_bend(v);
    else {
        v->volume_counter--;
        if (v->volume_counter == 0) {
            v->volume_counter = v->volume_speed;
            const uint8_t *vs = s->vol[v->volume_seq].values;
            int parse_effect;
            guard = 0;
            do {
                parse_effect = 0;
                if (v->volume_seq_pos >= 59 || vs[v->volume_seq_pos] == 0xe1) break;
                switch (vs[v->volume_seq_pos]) {
                    case 0xea:
                        v->volume_bend_speed = vol_at(vs, v->volume_seq_pos + 1u);
                        v->volume_bend_time = vol_at(vs, v->volume_seq_pos + 2u);
                        v->volume_seq_pos += 3;
                        do_vol_bend(v);
                        break;
                    case 0xe8:
                        v->vol_sus_counter = vol_at(vs, v->volume_seq_pos + 1u);
                        v->volume_seq_pos += 2;
                        break;
                    case 0xe0:
                        v->volume_seq_pos = (uint16_t)((vol_at(vs, v->volume_seq_pos + 1u) & 0x3f) - 5);
                        parse_effect = 1;
                        break;
                    default:
                        v->volume = (int8_t)(vs[v->volume_seq_pos] & 0x7f);
                        v->volume_seq_pos++;
                        break;
                }
                if (++guard > FC_LOOP_GUARD) parse_effect = 0;
            } while (parse_effect);
        }
    }

    /* period */
    int8_t note = v->patt_transpose;
    if (note >= 0) note = (int8_t)(note + (int8_t)(v->cur_note + v->transpose));
    note = (int8_t)(note & 0x7f);
    uint16_t period = fc_periods[(unsigned)note];                      /* note < 128 < Periods.Length */
    uint8_t vib_flag = v->vib_flag;

    if (v->vib_delay != 0) v->vib_delay--;
    else {
        uint16_t vib_base = (uint16_t)(note * 2);
        int8_t vib_dep = (int8_t)(v->vib_depth * 2);
        int8_t vib_val = v->vib_value;
        if ((vib_flag & 0x80) == 0 || (vib_flag & 0x01) == 0) {
            if ((vib_flag & 0x20) != 0) {
                vib_val = (int8_t)(vib_val + v->vib_speed);
                if (vib_val >= vib_dep) { vib_flag &= 0xdf; vib_val = vib_dep; }
            } else {
                vib_val = (int8_t)(vib_val - v->vib_speed);
                if (vib_val < 0) { vib_flag |= 0x20; vib_val = 0; }
            }
            v->vib_value = vib_val;
        }
        vib_dep = (int8_t)(vib_dep / 2);
        vib_val = (int8_t)(vib_val - vib_dep);
        vib_base = (uint16_t)(vib_base + 160);
        while (vib_base < 256) { vib_val = (int8_t)(vib_val * 2); vib_base = (uint16_t)(vib_base + 24); }
        period = (uint16_t)(period + (uint16_t)vib_val);
    }
    v->vib_flag = (uint8_t)(vib_flag ^ 0x01);

    v->port_flag = !v->port_flag;
    if (v->port_flag && v->portamento != 0) {
        if (v->portamento <= 31) v->pitch = (uint16_t)(v->pitch - v->portamento);
        else v->pitch = (uint16_t)(v->pitch + (uint16_t)(v->portamento & 0x1f));
    }
    v->pitch_bend_flag = !v->pitch_bend_flag;
    if (v->pitch_bend_flag && v->pitch_bend_time != 0) {
        v->pitch_bend_time--;
        v->pitch = (uint16_t)(v->pitch - (uint16_t)v->pitch_bend_speed);
    }
    period = (uint16_t)(period + v->pitch);
    if (period < 113) period = 113;
    else if (period > 3424) period = 3424;

    if (v->volume < 0) v->volume = 0;
    else if (v->volume > 64) v->volume = 64;

    c->period = period;                                                /* SetAmigaPeriod */
    c->volume = v->volume;                                             /* SetAmigaVolume */
}

/* ---------------------------------------------------------------- Play() */
static void play_tick(FcPlayer *p) {
    p->re_sp_cnt--;
    if (p->re_sp_cnt == 0) {
        p->re_sp_cnt = p->rep_spd;
        for (int i = 0; i < 4; i++) new_note(p, i);
    }
    for (int i = 0; i < 4; i++) effect(p, i);
    if (p->end_reached) { p->ended = 1; p->end_reached = 0; }        /* OnEndReached: the song is over (no restart) */
}

/* ---------------------------------------------------------------- public */
FcPlayer *fc_new(const FcSong *s, int sub, int rate) {
    if (!s || rate <= 0 || sub < 0 || sub >= s->nsub) return NULL;
    FcPlayer *p = (FcPlayer *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->visited = (uint8_t *)calloc((size_t)s->nseq + 1, 1);
    if (!p->visited) { free(p); return NULL; }
    p->s = s; p->rate = rate; p->sub = sub;
    int start = s->sub_start[sub];
    initialize_sound(p, start);
    p->visited[start] = 1;                                             /* the start position counts as visited */
    return p;
}

void fc_player_free(FcPlayer *p) { if (p) { free(p->visited); free(p); } }

static inline void mix_advance(FcMix *m) {                         /* end of a pass: pending SetSample, then the loop, else stop */
    while (m->on && m->pos >= m->len) {
        if (m->pend) { m->pos -= m->len; m->d = m->pd; m->len = m->plen; m->pend = 0; }
        else if (m->llen > 0 && m->ld) { m->pos -= m->len; m->d = m->ld; m->len = m->llen; }
        else { m->on = 0; break; }
        if (m->pos < 0) m->pos = 0;
    }
}

int fc_render(FcPlayer *p, int16_t *out, int n) {
    if (!p || p->ended) return 0;
    for (int f = 0; f < n; f++) {
        if (p->tick_left <= 0) {
            play_tick(p);
            if (p->ended) { memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(n - f)); return f; }
            p->tick_left += p->rate / FC_TICK_HZ;
        }
        p->tick_left -= 1.0;
        float l = 0, rr = 0;
        for (int i = 0; i < 4; i++) {
            FcMix *m = &p->m[i];
            size_t si = p->sc ? (size_t)i * (size_t)p->sc_len + (p->sc_pos & (unsigned)(p->sc_len - 1)) : 0;
            if (p->sc) p->sc[si] = 0;
            mix_advance(m);
            if (!m->on || !m->d || m->len == 0) continue;
            uint32_t ip = (uint32_t)m->pos;
            if (ip >= m->len) ip = m->len - 1;
            int s0 = m->d[ip], s1;
            if (ip + 1 < m->len) s1 = m->d[ip + 1];
            else if (m->pend && m->plen) s1 = m->pd[0];
            else if (m->llen > 0 && m->ld) s1 = m->ld[0];
            else s1 = 0;
            float fr = (float)(m->pos - (double)ip);
            float smp = (float)s0 + (float)(s1 - s0) * fr;
            double per = m->period ? m->period : 428;
            m->pos += PAULA_PAL / (per * p->rate);
            if (p->mute >> i & 1) continue;
            float v = smp * (float)m->volume * (10.0f / 7.0f);
            if (p->sc) p->sc[si] = smp * (float)m->volume / (127.0f * 64.0f);
            if (pan4[i]) rr += v; else l += v;
        }
        /* no LED filter: the FC replayer never switches it */
        { float a = l, b = rr; l = 0.866f * a + 0.5f * b; rr = 0.5f * a + 0.866f * b; }   /* uade's default stereo mix */
        p->sc_pos++;
        out[2 * f] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2 * f + 1] = (int16_t)(rr > 32767 ? 32767 : rr < -32768 ? -32768 : rr);
    }
    return n;
}

int fc_version(const FcSong *s) { return s ? s->version : 0; }
int fc_subsongs(const FcSong *s) { return s ? s->nsub : 0; }
int fc_channels(const FcSong *s) { (void)s; return 4; }
int fc_rows(const FcSong *s) { (void)s; return 32; }
int fc_positions(const FcSong *s, int sub) {
    if (!s || sub < 0 || sub >= s->nsub) return 0;
    return s->nseq;
}

void fc_pos(const FcPlayer *p, int *position, int *row) {
    if (position) *position = p ? p->shown_pos : 0;
    if (row) *row = p ? p->shown_row : 0;
}

int fc_cell(const FcSong *s, int sub, int position, int row, int ch, int *note, int *inst, int *fx, int *arg) {
    if (!s || sub < 0 || sub >= s->nsub || position < 0 || position >= s->nseq || row < 0 || row >= 32 || ch < 0 || ch >= 4) return 0;
    const FcSeq *q = &s->seq[position];
    int pat = q->pat[ch];
    if (pat >= s->pat_num) pat = 0;                                   /* as NewNote */
    for (int r = 0; r <= row; r++) if (pat_row(s, pat, r)[0] == 0x49) return 0;   /* END mark: the rest is not played */
    const uint8_t *c = pat_row(s, pat, row);
    int nt = c[0] & 0x7f, in = 0;
    if (nt != 0) {                                                     /* as NewNote + Effect (frequency sequence transpose 0) */
        nt = (uint8_t)((int8_t)(nt + q->tr[ch]) & 0x7f);
        uint8_t i8 = (uint8_t)((c[1] & 0x3f) + q->str[ch]);
        in = i8 >= s->vol_num ? 0 : i8 + 1;
    }
    if (note) *note = nt;
    if (inst) *inst = in;
    if (fx) *fx = (c[1] & 0xc0) >> 6;                                 /* 2 = portamento on, 1 = portamento off */
    if (arg) *arg = (c[1] & 0x80) && row < 31 ? pat_row(s, pat, row + 1)[1] : 0;   /* portamento value (next row's info) */
    return 1;
}

void fc_set_mute(FcPlayer *p, unsigned mask) { if (p) p->mute = mask; }
void fc_set_scope(FcPlayer *p, float *buf, int len) {
    if (!p) return;
    if (!buf || len <= 0 || (len & (len - 1))) { p->sc = NULL; p->sc_len = 0; return; }
    p->sc = buf; p->sc_len = len;
}
unsigned fc_scope_pos(const FcPlayer *p) { return p ? p->sc_pos : 0; }
