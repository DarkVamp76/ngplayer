/* The Player: the shared parts (reader, ProTracker writer, tables). GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include "tp.h"

uint8_t tp_u8(TpIn *s) { if (s->pos >= s->n) { s->eof = 1; s->pos++; return 0; } return s->b[s->pos++]; }
int8_t tp_s8(TpIn *s) { return (int8_t)tp_u8(s); }
uint16_t tp_b16(TpIn *s) { uint16_t v = (uint16_t)(tp_u8(s) << 8); return (uint16_t)(v | tp_u8(s)); }
uint32_t tp_b32(TpIn *s) { uint32_t v = (uint32_t)tp_b16(s) << 16; return v | tp_b16(s); }
void tp_seek(TpIn *s, long pos) { if (pos < 0) pos = 0; s->pos = (size_t)pos; s->eof = s->pos > s->n; }
void tp_skip(TpIn *s, long d) { long p = (long)s->pos + d; tp_seek(s, p); }
void tp_read(TpIn *s, uint8_t *dst, size_t k) { for (size_t i = 0; i < k; i++) dst[i] = tp_u8(s); }

void tp_mod_init(TpMod *m) { memset(m, 0, sizeof *m); m->restart = 0x7F; }
void tp_mod_free(TpMod *m) { free(m->pat); free(m->sdata); memset(m, 0, sizeof *m); }
uint8_t *tp_pat_add(TpMod *m) {
    if (m->npat >= 128) return NULL;
    uint8_t *np = (uint8_t *)realloc(m->pat, (size_t)(m->npat + 1) * 1024); if (!np) return NULL;
    m->pat = np; memset(np + (size_t)m->npat * 1024, 0, 1024); return np + (size_t)m->npat++ * 1024;
}
static int sgrow(TpMod *m, size_t k) {
    if (m->sdata_len + k <= m->sdata_cap) return 1;
    size_t c = m->sdata_cap ? m->sdata_cap : 65536; while (c < m->sdata_len + k) c *= 2;
    uint8_t *d = (uint8_t *)realloc(m->sdata, c); if (!d) return 0; m->sdata = d; m->sdata_cap = c; return 1;
}
int tp_sdata_add(TpMod *m, const uint8_t *d, size_t k) { if (!sgrow(m, k)) return 0; memcpy(m->sdata + m->sdata_len, d, k); m->sdata_len += k; return 1; }
int tp_sdata_zero(TpMod *m, size_t k) { if (!sgrow(m, k)) return 0; memset(m->sdata + m->sdata_len, 0, k); m->sdata_len += k; return 1; }

uint8_t *tp_mod_build(const TpMod *m, size_t *outn) {
    if (m->nsmp <= 0 || m->npos <= 0 || m->npat <= 0) return NULL;
    size_t n = 1084 + (size_t)m->npat * 1024 + m->sdata_len;
    uint8_t *o = (uint8_t *)calloc(1, n); if (!o) return NULL;
    memcpy(o, m->name, 20);
    for (int i = 0; i < 31; i++) {
        uint8_t *h = o + 20 + 30 * i;
        if (i < m->nsmp) {
            const TpSample *s = &m->smp[i];
            memcpy(h, s->name, 22); h[22] = (uint8_t)(s->len >> 8); h[23] = (uint8_t)s->len; h[24] = s->ft; h[25] = s->vol;
            h[26] = (uint8_t)(s->ls >> 8); h[27] = (uint8_t)s->ls; h[28] = (uint8_t)(s->ll >> 8); h[29] = (uint8_t)s->ll;
        } else h[29] = 1;
    }
    int np = m->npos > 128 ? 128 : m->npos;                 /* ProTracker holds 128 positions */
    o[950] = (uint8_t)np; o[951] = m->restart;
    memcpy(o + 952, m->pos, (size_t)np);
    memcpy(o + 1080, "M.K.", 4);
    memcpy(o + 1084, m->pat, (size_t)m->npat * 1024);
    if (m->sdata_len) memcpy(o + 1084 + (size_t)m->npat * 1024, m->sdata, m->sdata_len);
    *outn = n; return o;
}

uint8_t *tp_convert(const uint8_t *b, size_t n, size_t *outn, const char **ver) {
    static const struct { int (*f)(const uint8_t *, size_t, TpMod *); const char *v; } conv[] = {
        { tp_conv_61a, "6.1A" }, { tp_conv_60a, "6.0A" }, { tp_conv_50a, "5.0A" }, { tp_conv_41a, "4.1A" }, { tp_conv_40b, "4.0B" },
        { tp_conv_40a, "4.0A" }, { tp_conv_30a, "3.0A" }, { tp_conv_22a, "2.2A" } };
    for (size_t i = 0; i < sizeof conv / sizeof conv[0]; i++) {
        TpMod m; tp_mod_init(&m);
        if (conv[i].f(b, n, &m)) { uint8_t *o = tp_mod_build(&m, outn); tp_mod_free(&m); if (o) { if (ver) *ver = conv[i].v; return o; } }
        else tp_mod_free(&m);
    }
    return NULL;
}

const int16_t tp_tuning_periods[16][36] = {
    { 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113 },
    { 850, 802, 757, 715, 674, 637, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357, 337, 318, 300, 284, 268, 253, 239, 225, 213, 201, 189, 179, 169, 159, 150, 142, 134, 126, 119, 113 },
    { 844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355, 335, 316, 298, 282, 266, 251, 237, 224, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118, 112 },
    { 838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352, 332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118, 111 },
    { 832, 785, 741, 699, 660, 623, 588, 555, 524, 495, 467, 441, 416, 392, 370, 350, 330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 124, 117, 110 },
    { 826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347, 328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116, 109 },
    { 820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345, 325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115, 109 },
    { 814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342, 323, 305, 288, 272, 256, 242, 228, 216, 204, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114, 108 },
    { 907, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120 },
    { 900, 850, 802, 757, 715, 675, 636, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357, 337, 318, 300, 284, 268, 253, 238, 225, 212, 200, 189, 179, 169, 159, 150, 142, 134, 126, 119 },
    { 894, 844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355, 335, 316, 298, 282, 266, 251, 237, 223, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118 },
    { 887, 838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352, 332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118 },
    { 881, 832, 785, 741, 699, 660, 623, 588, 555, 524, 494, 467, 441, 416, 392, 370, 350, 330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 123, 117 },
    { 875, 826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347, 328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116 },
    { 868, 820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345, 325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115 },
    { 862, 814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342, 323, 305, 288, 272, 256, 242, 228, 216, 203, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114 },
};
const uint8_t tp_periods[45][2] = {
    { 0x3, 0x58 }, { 0x3, 0x28 }, { 0x2, 0xfa }, { 0x2, 0xd0 }, { 0x2, 0xa6 }, { 0x2, 0x80 }, { 0x2, 0x5c }, { 0x2, 0x3a }, { 0x2, 0x1a }, { 0x1, 0xfc }, { 0x1, 0xe0 }, { 0x1, 0xc5 }, { 0x1, 0xac }, { 0x1, 0x94 }, { 0x1, 0x7d }, { 0x1, 0x68 }, { 0x1, 0x53 }, { 0x1, 0x40 }, { 0x1, 0x2e }, { 0x1, 0x1d }, { 0x1, 0x0d }, { 0x0, 0xfe }, { 0x0, 0xf0 }, { 0x0, 0xe2 }, { 0x0, 0xd6 }, { 0x0, 0xca }, { 0x0, 0xbe }, { 0x0, 0xb4 }, { 0x0, 0xaa }, { 0x0, 0xa0 }, { 0x0, 0x97 }, { 0x0, 0x8f }, { 0x0, 0x87 }, { 0x0, 0x7f }, { 0x0, 0x78 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }, { 0x0, 0x71 }
};
