/* "The Player" (Jarno Paananen / Guru of Sahara Surfers; versions 2.2A to 6.1A): packed ProTracker modules. Each version is turned back into a
   ProTracker module in memory (like ProWizard does; ported from the NostalgicPlayer ProWizard converters, MIT) and played by the MOD engine.
   GPL-3.0-or-later */
#ifndef NG_TP_H
#define NG_TP_H
#include <stdint.h>
#include <stddef.h>

/* the module stream of the C# converters: a reader with a position and an end flag (reads past the end give 0 and set eof) */
typedef struct { const uint8_t *b; size_t n, pos; int eof; } TpIn;
uint8_t  tp_u8(TpIn *s);
int8_t   tp_s8(TpIn *s);
uint16_t tp_b16(TpIn *s);                /* big endian */
uint32_t tp_b32(TpIn *s);
void     tp_seek(TpIn *s, long pos);     /* SeekOrigin.Begin */
void     tp_skip(TpIn *s, long d);       /* SeekOrigin.Current */
void     tp_read(TpIn *s, uint8_t *dst, size_t k);

/* what a converter collects: written by tp_mod_build as an "M.K." ProTracker module */
typedef struct { uint8_t name[22]; uint16_t len, ls, ll; uint8_t ft, vol; } TpSample;      /* lengths in words */
typedef struct {
    uint8_t name[20];
    TpSample smp[31]; int nsmp;
    uint8_t pos[128]; int npos; uint8_t restart;          /* restart: 0x7F unless the converter sets one */
    uint8_t *pat; int npat;                               /* npat * 1024 bytes (64 rows * 4 channels * 4 bytes, ProTracker cells) */
    uint8_t *sdata; size_t sdata_len, sdata_cap;          /* the sample data of all samples, in order (each smp[i].len * 2 bytes) */
} TpMod;
void     tp_mod_init(TpMod *m);
void     tp_mod_free(TpMod *m);
uint8_t *tp_pat_add(TpMod *m);                            /* appends a cleared 1024 byte pattern, returns it (NULL on error) */
int      tp_sdata_add(TpMod *m, const uint8_t *d, size_t k);   /* appends sample bytes (0 on error) */
int      tp_sdata_zero(TpMod *m, size_t k);               /* appends k zero bytes (a sample cut short by the end of the file) */
uint8_t *tp_mod_build(const TpMod *m, size_t *outn);      /* the ProTracker module (malloc), NULL on error */

/* tables of ProWizardConverterWorkerBase / ProWizardConverterWorker31SamplesBase */
extern const uint8_t tp_periods[45][2];                   /* ProTracker periods (big endian bytes) of the 36 notes + 9 repeats of the last */
extern const int16_t tp_tuning_periods[16][36];           /* periods per finetune 0..15 */
#define TP_MAX_MISSING 256                                /* MaxNumberOfMissingBytes */

/* the converters: each returns 1 when the module is of its kind and was converted (m filled), 0 when not */
int tp_conv_22a(const uint8_t *b, size_t n, TpMod *m);
int tp_conv_30a(const uint8_t *b, size_t n, TpMod *m);
int tp_conv_40a(const uint8_t *b, size_t n, TpMod *m);
int tp_conv_40b(const uint8_t *b, size_t n, TpMod *m);
int tp_conv_41a(const uint8_t *b, size_t n, TpMod *m);
int tp_conv_50a(const uint8_t *b, size_t n, TpMod *m);
int tp_conv_60a(const uint8_t *b, size_t n, TpMod *m);
int tp_conv_61a(const uint8_t *b, size_t n, TpMod *m);

/* tries all versions; returns the ProTracker module (malloc) and sets *ver to "6.1A" etc., NULL when none fits */
uint8_t *tp_convert(const uint8_t *b, size_t n, size_t *outn, const char **ver);
#endif
