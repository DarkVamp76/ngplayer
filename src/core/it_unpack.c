#include "it_unpack.h"
#include <string.h>

typedef struct { const uint8_t *p; size_t nbits, pos; int err; } itbits;

static uint32_t get(itbits *b, unsigned n)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) {
        if (b->pos >= b->nbits) { b->err = 1; return v; }
        v |= (uint32_t)((b->p[b->pos >> 3] >> (b->pos & 7)) & 1u) << i;
        b->pos++;
    }
    return v;
}

int it_unpack(const uint8_t *src, size_t srclen, void *dst, size_t nsamples,
              int is16, int it215, size_t *consumed)
{
    const unsigned maxw   = is16 ? 17 : 9;      /* Startbreite = Obergrenze */
    const unsigned chgbit = is16 ? 4 : 3;       /* Bits fuer Breite bei Methode 1 */
    const size_t   blkmax = is16 ? 0x4000 : 0x8000;
    size_t in = 0, out = 0;
    int8_t  *d8  = dst;
    int16_t *d16 = dst;

    memset(dst, 0, nsamples * (is16 ? 2 : 1));
    while (out < nsamples) {
        if (in + 2 > srclen) return -1;
        size_t bsz = src[in] | (src[in + 1] << 8);
        in += 2;
        if (!bsz || in + bsz > srclen) return -1;
        itbits b = { src + in, bsz * 8, 0, 0 };
        in += bsz;

        size_t blen = nsamples - out; if (blen > blkmax) blen = blkmax;
        unsigned width = maxw;
        int32_t d1 = 0, d2 = 0;
        for (size_t k = 0; k < blen; ) {
            uint32_t val = get(&b, width);
            if (b.err) return -2;
            if (width < 7) {                               /* Methode 1 */
                if (val == (1u << (width - 1))) {
                    uint32_t n = get(&b, chgbit) + 1;
                    width = n < width ? n : n + 1;
                    continue;
                }
            } else if (width < maxw) {                     /* Methode 2 */
                uint32_t border;
                if (is16) border = (0xFFFFu >> (17 - width)) - 8;
                else      border = (0xFFu   >> (9  - width)) - 4;
                if (val > border && val <= border + (is16 ? 16u : 8u)) {
                    uint32_t n = val - border;
                    width = n < width ? n : n + 1;
                    continue;
                }
            } else if (width == maxw) {                    /* Methode 3 */
                if (val & (1u << (maxw - 1))) {
                    width = (val + 1) & 0xFF;
                    if (width == 0 || width > maxw) return -3;
                    continue;
                }
            } else return -3;
            if (width == 0 || width > maxw) return -3;

            int32_t v;
            unsigned bits = is16 ? 16 : 8;
            if (width < bits) { v = (int32_t)(val << (32 - width)) >> (32 - width); }
            else v = is16 ? (int16_t)val : (int8_t)val;
            if (is16) { d1 = (int16_t)(d1 + v); d2 = (int16_t)(d2 + d1); }
            else      { d1 = (int8_t)(d1 + v);  d2 = (int8_t)(d2 + d1); }
            if (is16) d16[out + k] = (int16_t)(it215 ? d2 : d1);
            else      d8 [out + k] = (int8_t) (it215 ? d2 : d1);
            k++;
        }
        out += blen;
    }
    if (consumed) *consumed = in;
    return 0;
}
