/* The Player 5.0A / 6.0A / 6.1A -> ProTracker. Ported from the NostalgicPlayer ProWizard converters (MIT):
   ThePlayer5x_6xFormatBase.cs, ThePlayer50AFormat.cs, ThePlayer60AFormat.cs, ThePlayer61AFormat.cs.
   A C# exception (array index out of range, negative seek) ends the conversion with 0 here (ctx->fail).
   GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include "tp.h"

static const int8_t pack_table[16] = { 0, 1, 2, 4, 8, 16, 32, 64, -128, -64, -32, -16, -8, -4, -2, -1 };

typedef struct {
    TpIn s;
    int ver;                                  /* 50 (also used by 6.0A, which inherits the 5.0A track decoder) or 61 */
    uint32_t trackDataOffset, sampleDataOffset;
    uint8_t numberOfSamples;
    int packedSamples, deltaSamples;
    uint32_t sampleStartOffsets[32];
    int samplePacked[32];
    uint16_t trackOffsetTable[4 * 256];
    uint16_t numberOfPatterns;
    uint32_t sampleLengths[31];
    uint8_t pattern[1024];
    int fail;                                 /* set where the C# code would throw */
} Ctx;

static long tpos(Ctx *c) { return (long)c->s.pos; }

/* ModuleStream.Seek(pos, Begin): a negative position throws in C# */
static void cseek(Ctx *c, long pos) { if (pos < 0) { c->fail = 1; return; } tp_seek(&c->s, pos); }

static void pset(Ctx *c, int off, uint8_t v) { if (off < 0 || off >= 1024) { c->fail = 1; return; } c->pattern[off] = v; }

/* GotId */
static int got_id(Ctx *c) {
    tp_seek(&c->s, 0);
    uint8_t mk[4]; tp_read(&c->s, mk, 4);
    if (memcmp(mk, "P50A", 4) && memcmp(mk, "P60A", 4) && memcmp(mk, "P61A", 4)) { tp_seek(&c->s, 0); return 0; }
    return 1;
}

/* CheckForThePlayerFormat; -1 for unknown (also where C# would throw) */
static int check_format(Ctx *c) {
    TpIn *s = &c->s;
    if (s->n < 4) return -1;
    uint32_t sampleInfoOffset = 0;
    int type = 50;
    if (got_id(c)) sampleInfoOffset = 4;

    uint16_t sampleOffset = tp_b16(s);
    if ((sampleOffset & 0x1) != 0 || sampleOffset < 0x24) return -1;

    c->numberOfPatterns = tp_u8(s);
    if (c->numberOfPatterns == 0 || c->numberOfPatterns > 64) return -1;

    uint8_t sampleCount = tp_u8(s);
    if (sampleCount > 0xdf) return -1;
    if ((sampleCount & 0x40) != 0) sampleInfoOffset += 8; else sampleInfoOffset += 4;
    sampleCount &= 0x1f;
    if (sampleCount == 0) return -1;

    if ((int)sampleOffset <= (sampleCount * 6 + c->numberOfPatterns * 8)) return -1;

    tp_seek(s, (long)sampleInfoOffset);
    uint32_t samplesSize = 0;
    uint16_t lengths[32]; memset(lengths, 0, sizeof lengths);

    for (int i = 0; i < sampleCount; i++) {
        uint16_t temp1 = tp_b16(s);
        uint8_t fineTune = tp_u8(s);
        if (temp1 >= 0x8000) {
            temp1 = (uint16_t)(-(int16_t)temp1);
            if (temp1 > 31) return -1;
            if ((int)temp1 - 1 >= sampleCount) return -1;     /* IndexOutOfRange in C# */
            temp1 = lengths[temp1 - 1];
        } else {
            samplesSize += temp1;
            if ((fineTune & 0x80) == 0) samplesSize += temp1;
        }
        lengths[i] = temp1;
        if ((fineTune & 0x70) != 0) return -1;
        if (tp_u8(s) > 64) return -1;
        temp1 = tp_b16(s);
        if (temp1 < 0x8000) {
            if (((int)lengths[i] - (int)temp1) >= 0x8000) return -1;
        }
    }

    if (tp_b16(s) != 0) return -1;
    tp_skip(s, -2);

    for (int i = 0; i < c->numberOfPatterns; i++) {
        for (int j = 0; j < 3; j++) {
            uint16_t temp2 = tp_b16(s);
            uint16_t temp3 = (uint16_t)(tp_b16(s) - 3);
            if (temp3 < temp2) return -1;
            if (temp2 > sampleOffset) return -1;
            if (temp3 > sampleOffset) return -1;
            tp_skip(s, -2);
        }
        tp_skip(s, 2);
    }

    int taken = 0;
    uint8_t temp4;
    while ((temp4 = tp_u8(s)) != 0xff) {
        if ((temp4 % 2) == 0) {
            if (temp4 > 0x7e) return -1;
        } else {
            type = 60;
            if (temp4 > 0x3f) return -1;
        }
        taken++;
        if (taken > 0x7f) return -1;
    }

    if ((int64_t)((uint32_t)(sampleOffset + samplesSize)) > (int64_t)s->n + TP_MAX_MISSING) return -1;

    uint32_t endOffset = (uint32_t)sampleOffset - 4U;
    uint32_t offset = (uint32_t)s->pos;
    uint32_t temp = 0;

    while ((int64_t)s->pos < (int64_t)endOffset) {
        uint8_t byt1 = tp_u8(s);
        uint8_t byt2 = tp_u8(s);
        if (byt1 == 0x00 && byt2 == 0x00) temp++;
        tp_skip(s, -1);
    }

    if (temp == 0) type = 61;
    else {
        endOffset -= 4;
        temp = 0;
        tp_seek(s, (long)offset);
        while ((int64_t)s->pos < (int64_t)endOffset) {
            if (tp_u8(s) == 0x80 && tp_u8(s) <= 0x0f) {
                uint16_t temp1 = tp_b16(s);
                if (((int64_t)s->pos - temp1) > (int64_t)offset) temp++;
            }
        }
        if (temp == 0) type = 61;
    }
    return type;
}

/* PrepareConversion */
static int prepare(Ctx *c) {
    got_id(c);
    c->sampleDataOffset = tp_b16(&c->s);
    tp_skip(&c->s, 1);
    c->numberOfSamples = tp_u8(&c->s);
    c->packedSamples = (c->numberOfSamples & 0x40) != 0;
    c->deltaSamples = (c->numberOfSamples & 0x80) != 0;
    c->numberOfSamples &= 0x1f;
    if (c->packedSamples) tp_skip(&c->s, 4);
    return 1;
}

/* GetNote */
static void get_note(Ctx *c, uint8_t trackByte, uint8_t *byt1, uint8_t *byt2) {
    if ((trackByte & 0x01) != 0) { *byt1 = 0x10; trackByte &= 0xfe; }
    else *byt1 = 0x00;
    if (trackByte != 0) {
        trackByte = (uint8_t)(trackByte - 2);
        trackByte /= 2;
        if (trackByte >= 45) { c->fail = 1; *byt2 = 0; return; }
        *byt1 |= tp_periods[trackByte][0];
        *byt2 = tp_periods[trackByte][1];
    } else *byt2 = 0x00;
}

/* CheckEffect */
static void check_effect(uint8_t *byt3, uint8_t *byt4) {
    switch (*byt3 & 0x0f) {
        case 0xd: *byt4 = 0x00; break;
        case 0xe: if (*byt4 == 0x02) *byt4 = 0x01; break;
        case 0x5: case 0x6: case 0xa:
            if (*byt4 >= 0x80) *byt4 = (uint8_t)((-(int)(int8_t)*byt4) << 4);
            break;
        case 0x8: *byt3 &= 0xf0; break;
    }
}

/* SetMultipleRows */
static void set_rows(Ctx *c, uint8_t count, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, int *wo) {
    while (count != 0 && !c->fail) {
        pset(c, *wo, b1); pset(c, *wo + 1, b2); pset(c, *wo + 2, b3); pset(c, *wo + 3, b4);
        *wo += 16;
        count--;
    }
}

static void put4(Ctx *c, int wo, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4) {
    pset(c, wo, b1); pset(c, wo + 1, b2); pset(c, wo + 2, b3); pset(c, wo + 3, b4);
}

/* ThePlayer50AFormat: private ConvertData */
static int conv50_line(Ctx *c, uint8_t temp2, int *wo) {
    TpIn *s = &c->s;
    int k = 0;
    if (temp2 > 0x80) {
        temp2 = (uint8_t)((-(int)(int8_t)temp2) - 1);
        uint8_t byt1, byt2; get_note(c, temp2, &byt1, &byt2);
        if (c->fail) return k;
        uint8_t byt3 = tp_u8(s), byt4 = tp_u8(s);
        check_effect(&byt3, &byt4);
        put4(c, *wo, byt1, byt2, byt3, byt4);
        *wo += 16; k++;
        temp2 = tp_u8(s);
        if (temp2 <= 64) { set_rows(c, temp2, 0, 0, 0, 0, wo); k += temp2; }
        else { temp2 = (uint8_t)(-(int)(int8_t)temp2); set_rows(c, temp2, byt1, byt2, byt3, byt4, wo); k += temp2; }
    } else {
        uint8_t byt1, byt2; get_note(c, temp2, &byt1, &byt2);
        if (c->fail) return k;
        uint8_t byt3 = tp_u8(s), byt4 = tp_u8(s);
        check_effect(&byt3, &byt4);
        put4(c, *wo, byt1, byt2, byt3, byt4);
        *wo += 16; k++;
    }
    return k;
}

/* ThePlayer50AFormat: ConvertData (also 6.0A) */
static int conv50(Ctx *c, int writeOffset) {
    TpIn *s = &c->s;
    int k;
    uint8_t temp2 = tp_u8(s);
    if (temp2 == 0x80) {
        uint16_t temp = (uint16_t)(tp_u8(s) + 1);
        uint16_t temp1 = tp_b16(s);
        long oldPosition = tpos(c);
        cseek(c, oldPosition - temp1);
        if (c->fail) return 0;
        k = 0;
        for (; temp != 0 && !c->fail; temp--) {
            temp2 = tp_u8(s);
            k += conv50_line(c, temp2, &writeOffset);
        }
        cseek(c, oldPosition);
    } else k = conv50_line(c, temp2, &writeOffset);
    return k;
}

/* ThePlayer61AFormat: private ConvertData */
static int conv61_line(Ctx *c, uint8_t temp2, int *wo) {
    TpIn *s = &c->s;
    int k = 0;
    uint8_t byt1, byt2, byt3, byt4;
    if (temp2 == 0x7f) {
        put4(c, *wo, 0, 0, 0, 0);
        *wo += 16; k++;
    } else if (temp2 < 0x60) {
        get_note(c, temp2, &byt1, &byt2);
        if (c->fail) return k;
        byt3 = tp_u8(s); byt4 = tp_u8(s);
        check_effect(&byt3, &byt4);
        put4(c, *wo, byt1, byt2, byt3, byt4);
        *wo += 16; k++;
    } else if (temp2 < 0x80) {
        if ((temp2 & 0xf0) == 0x60) {
            byt3 = (uint8_t)(temp2 & 0x0f);
            byt4 = tp_u8(s);
            check_effect(&byt3, &byt4);
            put4(c, *wo, 0, 0, byt3, byt4);
        } else {
            uint8_t temp1 = tp_u8(s);
            temp2 = (uint8_t)((temp2 << 4) | (temp1 >> 4));
            get_note(c, temp2, &byt1, &byt2);
            if (c->fail) return k;
            put4(c, *wo, byt1, byt2, (uint8_t)(temp1 << 4), 0);
        }
        *wo += 16; k++;
    } else if (temp2 < 0xd0) {
        temp2 = (uint8_t)(temp2 - 0x80);
        get_note(c, temp2, &byt1, &byt2);
        if (c->fail) return k;
        byt3 = tp_u8(s); byt4 = tp_u8(s);
        check_effect(&byt3, &byt4);
        put4(c, *wo, byt1, byt2, byt3, byt4);
        *wo += 16; k++;
        temp2 = tp_u8(s);
        if (temp2 < 0x80) { set_rows(c, temp2, 0, 0, 0, 0, wo); k += temp2; }
        else { temp2 = (uint8_t)(temp2 - 0x80); set_rows(c, temp2, byt1, byt2, byt3, byt4, wo); k += temp2; }
    } else if (temp2 < 0xf0) {
        byt3 = (uint8_t)(temp2 & 0x0f);
        byt4 = tp_u8(s);
        check_effect(&byt3, &byt4);
        put4(c, *wo, 0, 0, byt3, byt4);
        *wo += 16; k++;
        temp2 = tp_u8(s);
        if (temp2 < 0x80) { set_rows(c, temp2, 0, 0, 0, 0, wo); k += temp2; }
        else { temp2 = (uint8_t)(temp2 - 0x80); set_rows(c, temp2, 0, 0, byt3, byt4, wo); k += temp2; }
    } else if (temp2 < 0xff) {
        uint8_t temp1 = tp_u8(s);
        temp2 = (uint8_t)((temp2 << 4) | (temp1 >> 4));
        get_note(c, temp2, &byt1, &byt2);
        if (c->fail) return k;
        byt3 = (uint8_t)(temp1 << 4);
        put4(c, *wo, byt1, byt2, byt3, 0);
        *wo += 16; k++;
        temp2 = tp_u8(s);
        if (temp2 < 0x80) { set_rows(c, temp2, 0, 0, 0, 0, wo); k += temp2; }
        else { temp2 = (uint8_t)(temp2 - 0x80); set_rows(c, temp2, byt1, byt2, byt3, 0, wo); k += temp2; }
    }
    return k;
}

/* ThePlayer61AFormat: ConvertData */
static int conv61(Ctx *c, int writeOffset) {
    TpIn *s = &c->s;
    uint8_t temp2 = tp_u8(s);
    int k = conv61_line(c, temp2, &writeOffset);
    if (c->fail) return k;
    if (k == 0) {
        temp2 = tp_u8(s);
        if (temp2 < 0x40) {
            temp2++;
            set_rows(c, temp2, 0, 0, 0, 0, &writeOffset);
            k += temp2;
        } else {
            uint16_t temp;
            uint32_t readOffset;
            if (temp2 < 0x80) {
                temp = (uint16_t)(temp2 - 0x40);
                uint32_t p = (uint32_t)s->pos;
                readOffset = p - tp_u8(s) + 1u;
            } else {
                temp = (uint16_t)(temp2 - 0xc0);
                uint16_t temp1 = tp_b16(s);
                readOffset = (uint32_t)s->pos - temp1;
            }
            long oldPosition = tpos(c);
            cseek(c, (long)readOffset);
            temp++;
            for (; temp != 0 && !c->fail; temp--) {
                temp2 = tp_u8(s);
                int bytesRead = conv61_line(c, temp2, &writeOffset);
                if (c->fail) break;
                if (bytesRead == 0) {
                    temp2 = (uint8_t)(tp_u8(s) + 1);
                    set_rows(c, temp2, 0, 0, 0, 0, &writeOffset);
                    k += temp2;
                } else k += bytesRead;
            }
            cseek(c, oldPosition);
        }
    }
    return k;
}

static int cmp_u16(const void *a, const void *b) { int x = *(const uint16_t *)a, y = *(const uint16_t *)b; return (x > y) - (x < y); }

/* GetPatterns: builds pattern i into c->pattern (the C# reuses the same buffer for all patterns) */
static int build_pattern(Ctx *c, int i, const uint16_t *ordered, int count) {
    TpIn *s = &c->s;
    for (int j = 0; j < 4; j++) {
        uint16_t trackOffset = c->trackOffsetTable[i * 4 + j];
        int index = -1;
        for (int q = 0; q < count; q++) if (ordered[q] == trackOffset) { index = q; break; }
        uint32_t endOffset = index < count - 1 ? c->trackDataOffset + ordered[index + 1] : c->sampleDataOffset;
        tp_seek(s, (long)(uint32_t)(c->trackDataOffset + trackOffset));
        for (int k = 0; k < 64;) {
            if ((int64_t)s->pos < (int64_t)endOffset) {
                k += c->ver == 61 ? conv61(c, k * 16 + j * 4) : conv50(c, k * 16 + j * 4);
                if (c->fail) return 0;
            } else {
                put4(c, k * 16 + j * 4, 0, 0, 0, 0);
                if (c->fail) return 0;
                k++;
            }
        }
    }
    return 1;
}

/* WriteSampleData */
static int write_samples(Ctx *c, TpMod *m) {
    TpIn *s = &c->s;
    if (c->deltaSamples || c->packedSamples) {
        for (int i = 0; i < c->numberOfSamples; i++) {
            int length = (int)c->sampleLengths[i];
            if (length != 0) {
                tp_seek(s, (long)(uint32_t)(c->sampleDataOffset + c->sampleStartOffsets[i]));
                int8_t *dst = (int8_t *)calloc((size_t)length, 1);
                if (!dst) return 0;
                if (c->samplePacked[i]) {
                    int8_t sample = 0;
                    for (int j = 0; j < length;) {
                        uint8_t nib1 = tp_u8(s);
                        uint8_t nib2 = (uint8_t)(nib1 & 0x0f);
                        nib1 >>= 4;
                        sample = (int8_t)(sample - pack_table[nib1]);
                        dst[j++] = sample;
                        sample = (int8_t)(sample - pack_table[nib2]);
                        if (j >= length) { free(dst); return 0; }   /* IndexOutOfRange in C# */
                        dst[j++] = sample;
                    }
                } else if (c->deltaSamples) {
                    int8_t sample = tp_s8(s);
                    dst[0] = sample;
                    for (int j = 1; j < length; j++) {
                        sample = (int8_t)(sample - tp_s8(s));
                        dst[j] = sample;
                    }
                } else {
                    tp_read(s, (uint8_t *)dst, (size_t)length);
                }
                int ok = tp_sdata_add(m, (const uint8_t *)dst, (size_t)length);
                free(dst);
                if (!ok) return 0;
            }
        }
    } else {
        for (int i = 0; i < c->numberOfSamples; i++) {
            int length = (int)c->sampleLengths[i];
            if (length != 0) {
                tp_seek(s, (long)(uint32_t)(c->sampleDataOffset + c->sampleStartOffsets[i]));
                if ((int64_t)s->n - (int64_t)s->pos < (int64_t)(length - TP_MAX_MISSING)) return 0;
                /* SetSampleDataMarker */
                size_t avail = s->pos < s->n ? s->n - s->pos : 0;
                size_t take = avail < (size_t)length ? avail : (size_t)length;
                if (take && !tp_sdata_add(m, s->b + s->pos, take)) return 0;
                if ((size_t)length > take && !tp_sdata_zero(m, (size_t)length - take)) return 0;
                tp_skip(s, length);
            }
        }
    }
    return 1;
}

static int convert(const uint8_t *b, size_t n, TpMod *m, int type, int ver, uint8_t divisor) {
    Ctx *c = (Ctx *)calloc(1, sizeof *c);
    if (!c) return 0;
    int ok = 0;
    c->s.b = b; c->s.n = n; c->s.pos = 0; c->s.eof = 0;
    c->ver = ver;

    /* Identify: CheckModule */
    if (check_format(c) != type) goto done;

    /* Convert */
    memset(c->sampleLengths, 0, sizeof c->sampleLengths);
    if (!prepare(c)) goto done;

    memset(m->name, 0, 20);                                   /* GetModuleName returns null */

    /* GetSamples */
    {
        uint32_t startOffset = 0;
        int sampleCount = 0;
        for (int i = 0; i < c->numberOfSamples; i++) {
            uint16_t length = tp_b16(&c->s);
            uint8_t fineTune = tp_u8(&c->s);
            uint8_t volume = tp_u8(&c->s);
            uint16_t loopStart = tp_b16(&c->s);
            if (length >= 0x8000) {
                length = (uint16_t)(-(int16_t)length);
                int idx = (int)length - 1;
                if (idx < 0 || idx >= c->numberOfSamples || idx >= 31) goto done;   /* IndexOutOfRange in C# */
                c->sampleStartOffsets[i] = c->sampleStartOffsets[idx];
                length = (uint16_t)(c->sampleLengths[idx] / 2);
            } else {
                c->sampleStartOffsets[i] = startOffset;
                startOffset += length;
                if ((fineTune & 0x80) == 0) startOffset += length;
            }
            uint16_t loopLength;
            if (loopStart == 0xffff) { loopStart = 0x0000; loopLength = 0x0001; }
            else loopLength = (uint16_t)(length - loopStart);
            c->samplePacked[i] = (fineTune & 0x80) != 0;

            /* Convert(): after each yielded sample */
            if (c->s.eof) goto done;
            TpSample *sm = &m->smp[sampleCount];
            memset(sm->name, 0, 22);
            sm->len = length; sm->ls = loopStart; sm->ll = loopLength;
            sm->vol = volume; sm->ft = (uint8_t)(fineTune & 0x0f);
            c->sampleLengths[sampleCount++] = (uint32_t)length * 2;
        }
        if (sampleCount == 0) goto done;
        m->nsmp = sampleCount;
    }

    /* GetPositionList -> LoadPositionList (CreateOffsetTable first) */
    {
        int cnt = 4 * c->numberOfPatterns;
        for (int i = 0; i < cnt; i++) c->trackOffsetTable[i] = tp_b16(&c->s);
        int numberOfPositions = 0;
        uint8_t pos;
        while ((pos = tp_u8(&c->s)) != 0xff) {
            pos /= divisor;
            if (numberOfPositions >= 128) goto done;                         /* IndexOutOfRange in C# */
            m->pos[numberOfPositions++] = pos;
        }
        c->trackDataOffset = (uint32_t)c->s.pos;
        if (numberOfPositions == 0 || c->s.eof) goto done;
        m->npos = numberOfPositions;
    }
    m->restart = 0x7f;                                        /* GetRestartPosition of the base */

    /* GetPatterns */
    {
        int count = 4 * c->numberOfPatterns;
        uint16_t ordered[4 * 256];
        memcpy(ordered, c->trackOffsetTable, (size_t)count * sizeof(uint16_t));
        qsort(ordered, (size_t)count, sizeof(uint16_t), cmp_u16);
        for (int i = 0; i < c->numberOfPatterns; i++) {
            if (!build_pattern(c, i, ordered, count)) goto done;
            if (c->s.eof) goto done;
            uint8_t *p = tp_pat_add(m);
            if (!p) goto done;
            memcpy(p, c->pattern, 1024);
        }
    }

    /* WriteSampleData */
    if (!write_samples(c, m)) goto done;
    ok = 1;
done:
    free(c);
    return ok;
}

int tp_conv_50a(const uint8_t *b, size_t n, TpMod *m) { return convert(b, n, m, 50, 50, 2); }
int tp_conv_60a(const uint8_t *b, size_t n, TpMod *m) { return convert(b, n, m, 60, 50, 1); }
int tp_conv_61a(const uint8_t *b, size_t n, TpMod *m) { return convert(b, n, m, 61, 61, 1); }
