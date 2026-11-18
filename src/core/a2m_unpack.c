/* Unpackers for the Adlib Tracker 2 module formats (A2M / A2T): aPlib (file versions 9-11) and LZH (versions 12-14).
 * aPlib is the common LZ77 format by Joergen Ibsen; the LZH variant is the static-Huffman scheme of Haruhiko Okumura's public domain "ar002"
 * with the larger parameters Adlib Tracker 2 uses (16 kB window, 16 bit code lengths). Written from the published algorithms. GPL-3.0-or-later */
#include "a2m_unpack.h"
#include <stdlib.h>
#include <string.h>

/* ---------------- aPlib ---------------- */
typedef struct { const uint8_t *s; size_t n, pos; unsigned tag; int bits; int err; } ApIn;
static unsigned ap_byte(ApIn *a) { if (a->pos >= a->n) { a->err = 1; return 0; } return a->s[a->pos++]; }
static unsigned ap_bit(ApIn *a) {
    if (!a->bits) { a->tag = ap_byte(a); a->bits = 8; }
    unsigned b = (a->tag >> 7) & 1; a->tag = (a->tag << 1) & 0xFF; a->bits--; return b;
}
static unsigned ap_gamma(ApIn *a) {                                 /* Elias gamma coded number, at least 2 */
    unsigned v = 1;
    do { v = (v << 1) | ap_bit(a); } while (ap_bit(a) && !a->err && v < (1u << 24));
    return v;
}

long a2m_aplib_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen) {
    /* The aPlib dialect of Adlib Tracker 2: no "last was match" state (a gamma 2 always repeats the last offset, offsets always lose 3), and the data ends at the
     * end marker or when the output buffer is full (the song data block has a fixed size and no marker). Returns the number of bytes written. */
    ApIn a = { src, srclen, 0, 0, 0, 0 };
    size_t o = 0; unsigned r0 = (unsigned)-1;
    if (!srclen || !dstlen) return 0;
    dst[o++] = (uint8_t)ap_byte(&a);                                 /* the first byte is always a literal */
    while (o < dstlen && !a.err) {
        if (!ap_bit(&a)) { dst[o++] = (uint8_t)ap_byte(&a); continue; }                          /* 0: literal */
        if (!ap_bit(&a)) {                                           /* 10: a match with a gamma coded offset */
            unsigned offs = ap_gamma(&a), len;
            if (offs == 2) { offs = r0; len = ap_gamma(&a); }       /* repeat the previous offset */
            else {
                offs = ((offs - 3) << 8) + ap_byte(&a); len = ap_gamma(&a);
                if (offs >= 32000) len++;
                if (offs >= 1280) len++;
                if (offs < 128) len += 2;
                r0 = offs;
            }
            if (offs == 0 || offs > o) break;
            for (; len && o < dstlen; len--, o++) dst[o] = dst[o - offs];
            continue;
        }
        if (!ap_bit(&a)) {                                           /* 110: a short match, or the end marker */
            unsigned v = ap_byte(&a), len = 2 + (v & 1), offs = v >> 1;
            if (!offs) break;                                        /* end of data */
            r0 = offs;
            if (offs > o) break;
            for (; len && o < dstlen; len--, o++) dst[o] = dst[o - offs];
            continue;
        }
        {                                                            /* 111: a single byte from a 4 bit offset, or a zero */
            unsigned offs = 0; for (int i = 0; i < 4; i++) offs = (offs << 1) | ap_bit(&a);
            if (offs) { if (offs > o) break; dst[o] = dst[o - offs]; } else dst[o] = 0;
            o++;
        }
    }
    return (long)o;
}

/* ---------------- LZH (Okumura ar002, Adlib Tracker 2 parameters) ---------------- */
#define DICBIT   14
#define DIC_SIZE (1u << DICBIT)
#define MAXMATCH 256
#define THRESHOLD 2
#define NC       (255 + MAXMATCH + 2 - THRESHOLD)     /* 511 */
#define CBIT     16
#define CODE_BIT 16
#define NP       (DICBIT + 1)
#define NT       (CODE_BIT + 3)
#define PBIT     14
#define TBIT     15
#define NPT      (1u << TBIT)

typedef struct {
    const uint8_t *in; size_t n, pos;
    uint16_t bitbuf; unsigned subbitbuf; int bitcount;
    uint16_t left[2 * NC - 1], right[2 * NC - 1];
    uint8_t c_len[NC], pt_len[NPT];
    unsigned blocksize;
    uint16_t pt_table[256], c_table[4096];
    int bad;
} Lzh;

static unsigned lz_byte(Lzh *z) { return z->pos < z->n ? z->in[z->pos++] : 0; }
static void fillbuf(Lzh *z, int n) {
    z->bitbuf = (uint16_t)(z->bitbuf << n);
    while (n > z->bitcount) {
        n -= z->bitcount; z->bitbuf |= (uint16_t)(z->subbitbuf << n);
        z->subbitbuf = lz_byte(z); z->bitcount = 8;
    }
    z->bitcount -= n; z->bitbuf |= (uint16_t)(z->subbitbuf >> z->bitcount);
}
static unsigned getbits(Lzh *z, int n) { unsigned x = (unsigned)z->bitbuf >> (16 - n); fillbuf(z, n); return x; }

static void make_table(Lzh *z, int nchar, const uint8_t *bitlen, int tablebits, uint16_t *table) {
    uint16_t count[17], weight[17], start[18], *p;
    unsigned i, k, len, ch, jutbits, avail, nextcode, mask;
    for (i = 1; i <= 16; i++) count[i] = 0;
    for (i = 0; i < (unsigned)nchar; i++) count[bitlen[i]]++;
    start[1] = 0;
    for (i = 1; i <= 16; i++) start[i + 1] = (uint16_t)(start[i] + (count[i] << (16 - i)));
    if (start[17] != 0) z->bad = 1;
    jutbits = 16 - tablebits;
    for (i = 1; i <= (unsigned)tablebits; i++) { start[i] >>= jutbits; weight[i] = (uint16_t)(1u << (tablebits - i)); }
    while (i <= 16) { weight[i] = (uint16_t)(1u << (16 - i)); i++; }
    i = start[tablebits + 1] >> jutbits;
    if (i != 0) { k = 1u << tablebits; while (i != k) table[i++] = 0; }
    avail = (unsigned)nchar; mask = 1u << (15 - tablebits);
    for (ch = 0; ch < (unsigned)nchar; ch++) {
        if ((len = bitlen[ch]) == 0) continue;
        nextcode = start[len] + weight[len];
        if (len <= (unsigned)tablebits) {
            if (1u << tablebits < nextcode) { z->bad = 1; return; }
            for (i = start[len]; i < nextcode; i++) table[i] = (uint16_t)ch;
        } else {
            k = start[len]; p = &table[k >> jutbits]; i = len - tablebits;
            while (i != 0) {
                if (*p == 0) { if (avail >= 2 * NC - 1) { z->bad = 1; return; } z->right[avail] = z->left[avail] = 0; *p = (uint16_t)avail++; }
                p = (k & mask) ? &z->right[*p] : &z->left[*p];
                k <<= 1; i--;
            }
            *p = (uint16_t)ch;
        }
        start[len] = (uint16_t)nextcode;
    }
}

static void read_pt_len(Lzh *z, int nn, int nbit, int i_special) {
    int i, c, n; unsigned mask;
    n = (int)getbits(z, nbit);
    if (n == 0) {
        c = (int)getbits(z, nbit);
        for (i = 0; i < nn; i++) z->pt_len[i] = 0;
        for (i = 0; i < 256; i++) z->pt_table[i] = (uint16_t)c;
    } else {
        i = 0;
        while (i < n && i < (int)NPT - 4) {
            c = z->bitbuf >> 13;
            if (c == 7) { mask = 1u << 12; while (mask & z->bitbuf) { mask >>= 1; c++; } if (c > 16) { z->bad = 1; return; } }
            fillbuf(z, c < 7 ? 3 : c - 3);
            z->pt_len[i++] = (uint8_t)c;
            if (i == i_special) { c = (int)getbits(z, 2); while (--c >= 0) z->pt_len[i++] = 0; }
        }
        while (i < nn) z->pt_len[i++] = 0;
        make_table(z, nn, z->pt_len, 8, z->pt_table);
    }
}

static void read_c_len(Lzh *z) {
    int i, c, n; unsigned mask;
    n = (int)getbits(z, CBIT);
    if (n == 0) {
        c = (int)getbits(z, CBIT);
        for (i = 0; i < NC; i++) z->c_len[i] = 0;
        for (i = 0; i < 4096; i++) z->c_table[i] = (uint16_t)c;
    } else {
        i = 0;
        while (i < n && i < NC) {
            c = z->pt_table[z->bitbuf >> 8];
            if (c >= NT) { mask = 1u << 7; do { c = (z->bitbuf & mask) ? z->right[c] : z->left[c]; mask >>= 1; } while (c >= NT && mask); }
            fillbuf(z, z->pt_len[c]);
            if (c <= 2) {
                if (c == 0) c = 1; else if (c == 1) c = (int)getbits(z, 4) + 3; else c = (int)getbits(z, CBIT) + 20;
                while (--c >= 0 && i < NC) z->c_len[i++] = 0;
            } else z->c_len[i++] = (uint8_t)(c - 2);
        }
        while (i < NC) z->c_len[i++] = 0;
        make_table(z, NC, z->c_len, 12, z->c_table);
    }
}

static unsigned decode_c(Lzh *z) {
    unsigned j, mask;
    if (z->blocksize == 0) {
        z->blocksize = getbits(z, 16);
        if (z->blocksize == 0) return NC;                            /* end of data */
        read_pt_len(z, NT, TBIT, 3); read_c_len(z); read_pt_len(z, NP, PBIT, -1);
    }
    z->blocksize--;
    j = z->c_table[z->bitbuf >> 4];
    if (j >= NC) { mask = 1u << 3; do { j = (z->bitbuf & mask) ? z->right[j] : z->left[j]; mask >>= 1; } while (j >= NC && mask); }
    fillbuf(z, z->c_len[j]);
    return j;
}

static unsigned decode_p(Lzh *z) {
    unsigned j, mask;
    j = z->pt_table[z->bitbuf >> 8];
    if (j >= NP) { mask = 1u << 7; do { j = (z->bitbuf & mask) ? z->right[j] : z->left[j]; mask >>= 1; } while (j >= NP && mask); }
    fillbuf(z, z->pt_len[j]);
    if (j != 0) j = (1u << (j - 1)) + getbits(z, (int)(j - 1));
    return j;
}

/* the stream: a flag byte, the unpacked size (32 bit, little endian), then the coded data */
long a2m_lzh_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen) {
    if (srclen < 5) return -1;
    Lzh *z = calloc(1, sizeof *z); if (!z) return -1;
    uint8_t *win = calloc(DIC_SIZE, 1); if (!win) { free(z); return -1; }
    z->in = src; z->n = srclen; z->pos = 1;
    unsigned total = src[1] | (src[2] << 8) | (src[3] << 16) | ((unsigned)src[4] << 24); z->pos = 5;
    if (total > dstlen) total = (unsigned)dstlen;
    z->bitbuf = 0; z->subbitbuf = 0; z->bitcount = 0; fillbuf(z, 16); z->blocksize = 0;
    size_t o = 0; long long copy = 0; unsigned from = 0, pos = 0;       /* pos: write position in the window */
    while (o < total && !z->bad) {
        if (copy > 0) { win[pos] = win[from]; from = (from + 1) & (DIC_SIZE - 1); dst[o++] = win[pos]; pos = (pos + 1) & (DIC_SIZE - 1); copy--; continue; }
        unsigned c = decode_c(z);
        if (c == NC) break;
        if (c <= 255) { win[pos] = (uint8_t)c; dst[o++] = (uint8_t)c; pos = (pos + 1) & (DIC_SIZE - 1); }
        else { copy = (long long)c - (255 + 1 - THRESHOLD); from = (pos - decode_p(z) - 1) & (DIC_SIZE - 1); }
    }
    int bad = z->bad; free(win); free(z);
    return bad ? -1 : (long)o;
}

/* ---------------- sixpack (adaptive Huffman, file versions 1 and 5) ----------------
 * After the Sixpack decoder of the AdPlug project (LGPL-2.1+), which follows the original of Adlib Tracker 2. */
enum { SP_COPYRANGES = 6, SP_MINCOPY = 3, SP_MAXCOPY = 255, SP_CODESPERRANGE = SP_MAXCOPY - SP_MINCOPY + 1, SP_ROOT = 1, SP_TERMINATE = 256, SP_FIRSTCODE = 257,
       SP_MAXCHAR = SP_FIRSTCODE + SP_COPYRANGES * SP_CODESPERRANGE - 1, SP_SUCCMAX = SP_MAXCHAR + 1, SP_TWICEMAX = 2 * SP_MAXCHAR + 1, SP_MAXFREQ = 2000, SP_MAXBUF = 42 * 1024 };
typedef struct {
    uint16_t ibitcount, ibitbuffer, leftc[SP_MAXCHAR + 1], rghtc[SP_MAXCHAR + 1], dad[SP_TWICEMAX + 1], freq[SP_TWICEMAX + 1];
    size_t ibufcount, input_size, output_size; const uint8_t *in; uint8_t *out;
} Six;
static unsigned sp_copybits(unsigned range) { return 2 * range + 4; }
static unsigned sp_copymin(unsigned range) { static const uint16_t t[SP_COPYRANGES] = { 0, 16, 80, 336, 1360, 5456 }; return t[range]; }
static void sp_inittree(Six *x) {
    for (unsigned i = 2; i <= SP_TWICEMAX; i++) { x->dad[i] = (uint16_t)(i / 2); x->freq[i] = 1; }
    for (unsigned i = 1; i <= SP_MAXCHAR; i++) { x->leftc[i] = (uint16_t)(2 * i); x->rghtc[i] = (uint16_t)(2 * i + 1); }
}
static void sp_updatefreq(Six *x, unsigned a, unsigned b) {
    for (;;) {
        x->freq[x->dad[a]] = (uint16_t)(x->freq[a] + x->freq[b]);
        a = x->dad[a];
        if (a == SP_ROOT) break;
        b = x->leftc[x->dad[a]] == a ? x->rghtc[x->dad[a]] : x->leftc[x->dad[a]];
    }
    if (x->freq[SP_ROOT] == SP_MAXFREQ) for (a = 1; a <= SP_TWICEMAX; a++) x->freq[a] >>= 1;
}
static void sp_updatemodel(Six *x, unsigned code) {
    unsigned a = code + SP_SUCCMAX, b, c, code1, code2;
    x->freq[a]++;
    if (x->dad[a] != SP_ROOT) {
        code1 = x->dad[a];
        if (x->leftc[code1] == a) sp_updatefreq(x, a, x->rghtc[code1]); else sp_updatefreq(x, a, x->leftc[code1]);
        do {
            code2 = x->dad[code1];
            b = x->leftc[code2] == code1 ? x->rghtc[code2] : x->leftc[code2];
            if (x->freq[a] > x->freq[b]) {
                if (x->leftc[code2] == code1) x->rghtc[code2] = (uint16_t)a; else x->leftc[code2] = (uint16_t)a;
                if (x->leftc[code1] == a) { x->leftc[code1] = (uint16_t)b; c = x->rghtc[code1]; } else { x->rghtc[code1] = (uint16_t)b; c = x->leftc[code1]; }
                x->dad[b] = (uint16_t)code1; x->dad[a] = (uint16_t)code2;
                sp_updatefreq(x, b, c);
                a = b;
            }
            a = x->dad[a]; code1 = x->dad[a];
        } while (code1 != SP_ROOT);
    }
}
static int sp_bit(Six *x) {                                    /* the next bit, 16 bit words from the most significant bit; -1 at the end of the data */
    if (!x->ibitcount) {
        if (x->ibufcount == x->input_size) return -1;
        x->ibitbuffer = (uint16_t)(x->in[2 * x->ibufcount] | (x->in[2 * x->ibufcount + 1] << 8)); x->ibufcount++; x->ibitcount = 15;
    } else x->ibitcount--;
    int b = (x->ibitbuffer & 0x8000) != 0; x->ibitbuffer = (uint16_t)(x->ibitbuffer << 1); return b;
}
static unsigned sp_inputcode(Six *x, unsigned bits) {
    unsigned code = 0;
    for (unsigned i = 1; i <= bits; i++) { int b = sp_bit(x); if (b < 0) return 0; if (b) code |= 1u << (i - 1); }
    return code;
}
static unsigned sp_uncompress(Six *x) {
    unsigned a = 1;
    do { int b = sp_bit(x); if (b < 0) return SP_TERMINATE; a = b ? x->rghtc[a] : x->leftc[a]; } while (a <= SP_MAXCHAR);
    a -= SP_SUCCMAX; sp_updatemodel(x, a); return a;
}
long a2m_sixpack_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen) {
    if (srclen < 2 || srclen > SP_MAXBUF - 4096 || dstlen < 1) return 0;
    if (dstlen > SP_MAXBUF) dstlen = SP_MAXBUF;
    Six *x = calloc(1, sizeof *x); if (!x) return -1;
    x->input_size = srclen / 2; x->output_size = dstlen; x->in = src; x->out = dst;
    size_t o = 0; sp_inittree(x);
    for (;;) {
        unsigned c = sp_uncompress(x);
        if (c == SP_TERMINATE) break;
        if (c < 256) { if (o == dstlen) break; dst[o++] = (uint8_t)c; }
        else {
            unsigned t = c - SP_FIRSTCODE, index = t / SP_CODESPERRANGE, len = t + SP_MINCOPY - index * SP_CODESPERRANGE;
            unsigned dist = sp_inputcode(x, sp_copybits(index)) + sp_copymin(index) + len;
            int done = 0;
            for (unsigned i = 0; i < len; i++) { if (o == dstlen) { done = 1; break; } dst[o] = dist > o ? 0 : dst[o - dist]; o++; }
            if (done) break;
        }
    }
    free(x); return (long)o;
}

/* ---------------- LZW (file versions 2 and 6) ----------------
 * The custom LZW of Adlib Tracker 2 (Stanislav Baranec), as adapted for the AdPlug project (LGPL-2.1+). */
long a2m_lzw_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen) {
    static const uint16_t bitmask[5] = { 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff };
    uint8_t *stack = calloc(1, 65636), *work = calloc(1, 65636);
    if (!stack || !work) { free(stack); free(work); return -1; }
    uint16_t bitshift = 9, le6a = 0, le6c = 0, le6e = 0, le70 = 0x102, le74 = 0x200, stringlength = 0; uint8_t le76 = 0, le77 = 0;
    uint32_t prev = 0; size_t o = 0; int sp = 65536 - 1;
#define LZW_IN(i) ((size_t)(i) < srclen ? src[i] : 0)
#define NEXTCODE(var) do { size_t ix = prev >> 3; uint32_t bs = (uint32_t)LZW_IN(ix) + (LZW_IN(ix + 1) << 8) + (LZW_IN(ix + 2) << 16); bs >>= prev & 7; prev += bitshift; var = (int)(bs & bitmask[bitshift - 9]); } while (0)
    for (;;) {
        int code; NEXTCODE(code);
        if ((size_t)(prev >> 3) > srclen + 3) break;                    /* ran off the data */
        if (code == 0x101) break;
        if (code == 0x100) {
            bitshift = 9; le74 = 0x200; le70 = 0x102;
            NEXTCODE(code);
            le6a = le6c = (uint16_t)code; le77 = le76 = (uint8_t)code;
            if (o >= dstlen) break;
            dst[o++] = (uint8_t)code;
            continue;
        }
        le6a = (uint16_t)code; le6e = (uint16_t)code;
        if (code >= le70) { code = le6c; le6a = (uint16_t)code; code = (code & 0xff00) + le76; stack[--sp] = (uint8_t)code; stringlength++; }
        while (le6a > 0xff) {
            code = (code & 0xff00) + work[(le6a * 3) + 2]; stack[--sp] = (uint8_t)code; stringlength++;
            code = work[le6a * 3] + (work[(le6a * 3) + 1] << 8); le6a = (uint16_t)code;
            if (sp < 8) goto out;
        }
        code = le6a; le76 = le77 = (uint8_t)code; stack[--sp] = (uint8_t)code; stringlength++;
        while (stringlength--) { if (o >= dstlen) goto out; dst[o++] = stack[sp++]; }
        stringlength = 0;
        if (le70 >= 21845) break;
        work[(le70 * 3) + 0] = (uint8_t)le6c; work[(le70 * 3) + 1] = (uint8_t)(le6c >> 8); work[(le70 * 3) + 2] = le77; le70++;
        le6c = le6e;
        if (le70 >= le74 && bitshift < 14) { bitshift++; le74 = (uint16_t)(le74 << 1); }
    }
out:
    free(stack); free(work);
#undef NEXTCODE
#undef LZW_IN
    return (long)o;
}

/* ---------------- LZSS (file versions 3 and 7) ---------------- */
long a2m_lzss_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen) {
    enum { LZSS_N = 1 << 12, LZSS_F = (1 << 4) + 2, LZSS_T = 2 };
    uint8_t *work = calloc(1, 65536); if (!work) return -1;
    size_t in = 0, o = 0; uint16_t dx = 0; uint32_t ebx = 0, edi = LZSS_N - LZSS_F;
    for (;;) {
        dx = (uint16_t)(dx >> 1);
        if (!(dx >> 8)) { if (in >= srclen) break; dx = (uint16_t)(0xff00 | src[in++]); }
        if (dx & 1) {
            if (in >= srclen) break;
            uint8_t code = src[in++]; work[edi] = code; edi = (edi + 1) & (LZSS_N - 1);
            if (o >= dstlen) break;
            dst[o++] = code; continue;
        }
        if (in >= srclen) break;
        uint8_t prevcode = src[in++];
        if (in >= srclen) break;
        uint8_t code = src[in++];
        ebx = ((uint32_t)(code << 4) & 0xff00) | prevcode;
        int length = (code & 0x0f) + LZSS_T + 1;
        int stop = 0;
        do {
            if (o >= dstlen) { stop = 1; break; }
            dst[o++] = work[edi] = work[ebx];
            ebx = (ebx + 1) & (LZSS_N - 1); edi = (edi + 1) & (LZSS_N - 1);
        } while (--length > 0);
        if (stop) break;
    }
    free(work); return (long)o;
}
