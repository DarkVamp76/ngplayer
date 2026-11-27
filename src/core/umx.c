/* Unreal packages (.umx): the music object of the package holds an ordinary IT / S3M / XM / MOD module.
   umx_extract() moves that module to the start of the buffer and returns its length (0 = not an Unreal package
   or no tracker music inside, e.g. the MP3 music of Harry Potter). */
#include <stdint.h>
#include <string.h>
#include "it.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Unreal compact index: 6 bits + sign in the first byte, 7 bits per following byte */
static int cidx(const uint8_t *b, size_t n, size_t *p, int *out) {
    if (*p >= n) return 0;
    uint8_t v = b[(*p)++]; int neg = v & 0x80; uint32_t r = v & 0x3F;
    if (v & 0x40) {
        int sh = 6;
        for (;;) {
            if (*p >= n || sh > 30) return 0;
            uint8_t c = b[(*p)++]; r |= (uint32_t)(c & 0x7F) << sh; sh += 7;
            if (!(c & 0x80)) break;
        }
    }
    *out = neg ? -(int)r : (int)r; return 1;
}

static int mod_sig(const uint8_t *d, size_t n) {
    if (n >= 4 && !memcmp(d, "IMPM", 4)) return 1;
    if (n >= 17 && !memcmp(d, "Extended Module: ", 17)) return 1;
    if (n >= 48 && !memcmp(d + 44, "SCRM", 4)) return 1;
    if (n >= 1084) {
        const uint8_t *m = d + 1080;
        if (!memcmp(m, "M.K.", 4) || !memcmp(m, "M!K!", 4) || !memcmp(m, "FLT4", 4) || !memcmp(m, "FLT8", 4) ||
            !memcmp(m + 1, "CHN", 3) || !memcmp(m + 2, "CH", 2)) return 1;
    }
    return 0;
}

static size_t module_at(const uint8_t *b, size_t n, size_t off, size_t end) {
    for (size_t i = 0; i < 48 && off + i < end; i++)
        if (mod_sig(b + off + i, end - off - i)) return off + i;
    return (size_t)-1;
}

size_t umx_extract(uint8_t *b, size_t n) {
    if (n < 64 || rd32(b) != 0x9E2A83C1u) return 0;
    uint32_t ver = b[4] | (b[5] << 8);
    uint32_t nc = rd32(b + 12), no = rd32(b + 16), ec = rd32(b + 20), eo = rd32(b + 24), ic = rd32(b + 28), io = rd32(b + 32);
    if (nc > 65536 || ec > 65536 || ic > 65536 || no >= n || eo >= n || io >= n) return 0;
    /* names: the one whose text is "Music" is the class we look for (its import index gives the class) */
    int music_name = -1; size_t p = no;
    for (uint32_t i = 0; i < nc; i++) {
        size_t len;
        if (ver >= 64) { if (p >= n) return 0; len = b[p++]; if (p + len > n) return 0; if (len == 6 && !memcmp(b + p, "Music", 5)) music_name = (int)i; p += len; }
        else { size_t q = p; while (q < n && b[q]) q++; if (q >= n) return 0; if (q - p == 5 && !memcmp(b + p, "Music", 5)) music_name = (int)i; p = q + 1; }
        p += 4;
    }
    /* imports: classPackage, className, package(4), objectName -> remember which import is the class "Music" */
    int music_imp = 0; p = io;
    for (uint32_t i = 0; i < ic; i++) {
        int a, c, nm; if (!cidx(b, n, &p, &a) || !cidx(b, n, &p, &c)) break; p += 4;
        if (!cidx(b, n, &p, &nm)) break;
        if (nm == music_name && music_name >= 0) { music_imp = -(int)(i + 1); break; }
    }
    p = eo;
    for (uint32_t i = 0; i < ec; i++) {
        int cl, sp, nm, sz, off = 0;
        if (!cidx(b, n, &p, &cl) || !cidx(b, n, &p, &sp)) break; p += 4;
        if (!cidx(b, n, &p, &nm)) break; p += 4;
        if (!cidx(b, n, &p, &sz)) break;
        if (sz > 0 && !cidx(b, n, &p, &off)) break;
        if (sz <= 0 || off <= 0 || (size_t)off + (size_t)sz > n) continue;
        if (music_imp && cl != music_imp) continue;
        size_t m = module_at(b, n, (size_t)off, (size_t)off + (size_t)sz);
        if (m != (size_t)-1) { size_t len = (size_t)off + (size_t)sz - m; memmove(b, b + m, len); return len; }
    }
    /* packages of the beta versions: a different table layout, so look for the module itself */
    for (size_t i = 36; i + 4 < n; i++)
        if (!memcmp(b + i, "IMPM", 4) || (i + 17 <= n && !memcmp(b + i, "Extended Module: ", 17))) {
            size_t len = n - i; memmove(b, b + i, len); return len;
        }
    for (size_t i = 44 + 36; i + 4 < n; i++)
        if (!memcmp(b + i, "SCRM", 4)) { size_t len = n - (i - 44); memmove(b, b + i - 44, len); return len; }
    return 0;
}
