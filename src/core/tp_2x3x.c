/* The Player 2.2A and 3.0A -> ProTracker. Ported from the NostalgicPlayer ProWizard converters ThePlayer2x_3xFormatBase,
   ThePlayer22AFormat and ThePlayer30AFormat (MIT). GPL-3.0-or-later */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "tp.h"

/* the fields of the C# converter object (they live from CheckModule to the end of Convert) */
typedef struct {
    int ver;                        /* 22 or 30 */
    uint16_t numberOfPatterns;
    uint8_t numberOfSamples, numberOfPositions;
    uint16_t trackOffsetTable[256];
    uint8_t positionList[256];
    uint32_t trackDataOffset, trackTableOffset, sampleDataOffset;
    uint32_t sampleStartOffsets[256];
    uint32_t sampleLengths[31];
    int fail;                       /* set where the C# would throw (index out of range) */
} Tp2x;

/* CheckForThePlayerFormat */
static int check_format(Tp2x *c, TpIn *s, const char *mark) {
    if (s->n < 20) return 0;
    tp_seek(s, 0);
    uint8_t mk[4]; tp_read(s, mk, 4);
    if (memcmp(mk, mark, 4) != 0) return 0;
    c->numberOfPatterns = tp_u8(s);
    if (c->numberOfPatterns > 0x7f) return 0;
    tp_skip(s, 1);
    uint8_t sampleCount = tp_u8(s);
    if (sampleCount > 0x1f || sampleCount == 0) return 0;
    tp_seek(s, 20);
    if (tp_b32(s) != 0) return 0;
    tp_skip(s, -4);
    uint32_t samplesSize = 0;
    for (int i = 0; i < sampleCount; i++) {
        tp_skip(s, 4);
        uint32_t length = tp_b16(s) * 2U;
        tp_skip(s, 4);
        uint32_t loopLength = tp_b16(s) * 2U;
        if (length > 0xffff || loopLength > 0xffff) return 0;
        if (loopLength > length + 2) return 0;
        samplesSize += length;
        tp_skip(s, 3);
        if (tp_u8(s) > 0x40) return 0;
    }
    if (samplesSize <= 4) return 0;
    return 1;
}

/* BuildPositionList with getNumber = trackOffsetTable[pos] */
static void build_position_list(Tp2x *c) {
    uint16_t used[256]; int nused = 0;
    c->numberOfPatterns = 0;
    for (int i = 0; i < c->numberOfPositions; i++) {
        uint16_t next = c->trackOffsetTable[i];
        int found = 0;
        for (int j = 0; j < nused; j++) {
            if (used[j] == next) { c->positionList[i] = (uint8_t)j; found = 1; break; }
        }
        if (!found) { c->positionList[i] = (uint8_t)c->numberOfPatterns++; used[nused++] = next; }
    }
}

static int prepare(Tp2x *c, TpIn *s) {
    tp_seek(s, 5);
    c->numberOfPositions = (uint8_t)(tp_u8(s) / 2);
    c->numberOfSamples = tp_u8(s);
    tp_skip(s, 1);
    c->trackDataOffset = tp_b32(s);
    c->trackTableOffset = tp_b32(s);
    c->sampleDataOffset = tp_b32(s);
    /* CreateOffsetTable */
    tp_seek(s, (long)(uint32_t)(c->trackTableOffset + 4));
    for (int i = 0; i < c->numberOfPositions; i++) c->trackOffsetTable[i] = tp_b16(s);
    build_position_list(c);
    return 1;
}

static uint8_t effect56a(const Tp2x *c, uint8_t c3) {
    if (c->ver == 22) return (uint8_t)(c3 > 0x7f ? (0x100 - c3) << 4 : c3);
    return (uint8_t)(c3 > 0x7f ? (c3 << 4) & 0xf0 : c3);
}

/* ConvertData */
static void convert_data(Tp2x *c, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t out[4]) {
    uint8_t sample = (uint8_t)(((c1 << 4) & 0x10) | ((c2 >> 4) & 0x0f));
    uint8_t note = (uint8_t)(c1 & 0x7f);
    uint8_t period1, period2;
    if (note != 0) {
        note /= 2;
        note--;
        if (note >= 45) { c->fail = 1; period1 = period2 = 0; }   /* periods[note, 0] would throw */
        else { period1 = tp_periods[note][0]; period2 = tp_periods[note][1]; }
    } else period1 = period2 = 0;
    switch (c2 & 0x0f) {
        case 0x08: c2 = (uint8_t)(c2 - 0x08); break;
        case 0x05: case 0x06: case 0x0a: c3 = effect56a(c, c3); break;
    }
    out[0] = (uint8_t)((sample & 0xf0) | (period1 & 0x0f)); out[1] = period2; out[2] = c2; out[3] = c3;
}

static void store_data(Tp2x *c, const uint8_t d[4], int channel, int row, uint8_t *pattern) {
    int o = row * 16 + channel * 4;
    if (o < 0 || o + 3 >= 1024) { c->fail = 1; return; }   /* the C# would throw */
    memcpy(pattern + o, d, 4);
}

static void convert_pattern_data(Tp2x *c, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, int channel, uint8_t *pattern, int *vri) {
    uint8_t d[4];
    if (c->ver == 22) {
        convert_data(c, c1, c2, c3, d);
        store_data(c, d, channel, vri[channel], pattern);
    } else if (c1 != 0x80) {
        convert_data(c, c1, c2, c3, d);
        store_data(c, d, channel, vri[channel], pattern);
    } else {
        convert_data(c, (uint8_t)(c1 & 0x7f), c2, c3, d);
        for (int k = 0, row = vri[channel]; k <= c4 && !c->fail; k++, row++) store_data(c, d, channel, row, pattern);
    }
    if (c4 > 0x00 && c4 < 0x80) vri[channel] += c4;
    vri[channel]++;
}

/* SetSampleDataMarker: the next len bytes of the module stream are the sample data */
static int sample_marker(TpIn *s, TpMod *m, size_t len) {
    size_t have = 0;
    if (s->pos < s->n) { have = s->n - s->pos; if (have > len) have = len; }
    if (have && !tp_sdata_add(m, s->b + s->pos, have)) return 0;
    if (have < len && !tp_sdata_zero(m, len - have)) return 0;
    tp_skip(s, (long)len);
    return 1;
}

static int convert(const uint8_t *b, size_t n, TpMod *m, int ver) {
    Tp2x *c = (Tp2x *)calloc(1, sizeof *c);
    if (!c) return 0;
    int ok = 0;
    c->ver = ver;
    TpIn in = { b, n, 0, 0 }, *s = &in;
    if (!check_format(c, s, ver == 22 ? "P22A" : "P30A")) goto out;

    /* Convert() */
    memset(c->sampleLengths, 0, sizeof c->sampleLengths);
    if (!prepare(c, s)) goto out;

    memset(m->name, 0, 20);                                  /* GetModuleName returns null */

    /* GetSamples */
    int sampleCount = 0;
    tp_seek(s, 20);
    for (int i = 0; i < c->numberOfSamples; i++) {
        c->sampleStartOffsets[i] = tp_b32(s);
        uint16_t length = tp_b16(s);
        uint32_t loopStart = tp_b32(s);
        uint16_t loopLength = tp_b16(s);
        uint16_t fineTune = tp_b16(s);
        uint16_t volume = tp_b16(s);
        if (s->eof) goto out;
        if (sampleCount >= 31) goto out;                     /* sampleLengths[31] would throw */
        TpSample *sm = &m->smp[sampleCount];
        memset(sm->name, 0, 22);
        sm->len = length;
        sm->ls = (uint16_t)((uint32_t)(loopStart - c->sampleStartOffsets[i]) / 2);
        sm->ll = loopLength;
        sm->vol = (uint8_t)volume;
        sm->ft = (uint8_t)(fineTune / 74);
        c->sampleLengths[sampleCount++] = (uint32_t)length * 2;
    }
    if (sampleCount == 0) goto out;
    m->nsmp = sampleCount;

    /* GetPositionList */
    if (c->numberOfPositions == 0 || s->eof) goto out;
    memcpy(m->pos, c->positionList, c->numberOfPositions);
    m->npos = c->numberOfPositions;
    m->restart = 0x7f;

    /* GetPatterns */
    {
        int vri[4];
        int lastPatternNumber = -1;
        for (int i = 0; i < c->numberOfPositions; i++) {
            if (c->positionList[i] > lastPatternNumber) {
                lastPatternNumber++;
                uint8_t *pattern = tp_pat_add(m);
                if (!pattern) goto out;
                vri[0] = vri[1] = vri[2] = vri[3] = 0;
                uint16_t trackOffset = c->trackOffsetTable[i];
                tp_seek(s, (long)(uint32_t)(c->trackDataOffset + 4 + trackOffset));
                for (int k = 0; k < 64; k++) {
                    for (int j = 0; j < 4; j++) {
                        if (vri[j] > k) continue;
                        uint8_t c1 = tp_u8(s), c2 = tp_u8(s), c3 = tp_u8(s), c4 = tp_u8(s);
                        convert_pattern_data(c, c1, c2, c3, c4, j, pattern, vri);
                        if (c->fail) goto out;
                    }
                }
                if (s->eof) goto out;
            }
        }
    }

    /* WriteSampleData */
    {
        uint32_t sampleOffset = c->sampleDataOffset + 4;
        for (int i = 0; i < c->numberOfSamples; i++) {
            int length = (int)c->sampleLengths[i];
            if (length != 0) {
                tp_seek(s, (long)(uint32_t)(sampleOffset + c->sampleStartOffsets[i]));
                if ((long long)s->n - (long long)s->pos < (long long)(length - TP_MAX_MISSING)) goto out;
                if (!sample_marker(s, m, (size_t)length)) goto out;
            }
        }
    }
    ok = 1;
out:
    free(c);
    return ok;
}

int tp_conv_22a(const uint8_t *b, size_t n, TpMod *m) { return convert(b, n, m, 22); }
int tp_conv_30a(const uint8_t *b, size_t n, TpMod *m) { return convert(b, n, m, 30); }
