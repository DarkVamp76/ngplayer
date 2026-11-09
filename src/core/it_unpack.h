#ifndef IT_UNPACK_H
#define IT_UNPACK_H
#include <stddef.h>
#include <stdint.h>

/* Entpackt ein IT214/IT215-komprimiertes Sample.
 *  src, srclen : Rohdaten ab Sample-Pointer (Bloecke: u16 Groesse + Daten)
 *  dst         : Ausgabe, nsamples Werte (int8_t bzw. int16_t, host-endian)
 *  is16        : 0 = 8 Bit, 1 = 16 Bit
 *  it215       : 0 = IT214 (d1), 1 = IT215 (d2); in IT: Cvt-Bit 2 gesetzt
 *  consumed    : (optional) verbrauchte Eingabebytes
 * Rueckgabe 0 = ok, <0 = Datenfehler. */
int it_unpack(const uint8_t *src, size_t srclen, void *dst, size_t nsamples,
              int is16, int it215, size_t *consumed);
#endif
