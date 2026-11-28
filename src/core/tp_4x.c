/* The Player 4.0A, 4.0B and 4.1A: turned back into a ProTracker module.
   Ported from NostalgicPlayer (MIT): ThePlayer4xFormatBase.cs, ThePlayer40AFormat.cs, ThePlayer40BFormat.cs, ThePlayer41AFormat.cs
   and the Convert() order of ProWizardConverterWorker31SamplesBase.cs. GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include "tp.h"

/* the converter object of the C# (its fields live between CheckModule and Convert) */
typedef struct {
    int vol_first;                        /* 4.1A: volume before fine tune in the sample info */
    uint8_t numberOfSamples, numberOfPositions;
    uint16_t trackOffsetTable[4][256];
    uint8_t positionList[256];
    uint32_t trackDataOffset, trackTableOffset, sampleDataOffset, firstSampleAddress;
    int skipLastSample;
    uint32_t sampleStartOffsets[31];
    uint32_t sampleLengths[31];
    uint16_t numberOfPatterns;
} P4x;

typedef struct {
    uint8_t p4xPatternData[3];
    int8_t p4xInfo;
    uint16_t repeatLines;
    long oldPosition;
    uint8_t proPatternData[4];
} P4xChannel;

typedef struct { uint16_t len, ls, ll; uint8_t vol, ft; } P4xSampleInfo;

/* ThePlayer40AFormat / ThePlayer41AFormat.CheckSampleInfo */
static uint32_t check_sample_info(P4x *c, TpIn *s, uint8_t sampleCount, uint32_t *firstAddress, uint16_t *lastLength) {
    int32_t lastSampleStart = -1;
    uint32_t samplesSize = 0;
    *firstAddress = 0; *lastLength = 0;
    tp_seek(s, 20);
    for (int i = 0; i < sampleCount; i++) {
        int32_t sampleStart = (int32_t)tp_b32(s);
        uint16_t sampleLength = tp_b16(s);
        uint16_t fineTune, volume;
        tp_skip(s, 6);
        if (c->vol_first) { volume = tp_b16(s); fineTune = tp_b16(s); }
        else { fineTune = tp_b16(s); volume = tp_b16(s); }
        if (i == 0) *firstAddress = (uint32_t)sampleStart;
        *lastLength = sampleLength;
        if ((fineTune % 74) != 0) return 0;
        if (fineTune > 0x456) continue;
        if (volume > 0x40) return 0;
        if (sampleLength >= 0x8000) return 0;
        if (sampleStart > lastSampleStart) {
            lastSampleStart = sampleStart;
            samplesSize += sampleLength * 2U;
        }
    }
    return samplesSize;
}

/* ThePlayer40AFormat / ThePlayer41AFormat.ReadSampleInfo */
static P4xSampleInfo read_sample_info(P4x *c, TpIn *s, uint32_t *startOffset) {
    P4xSampleInfo si;
    uint32_t loopStart;
    uint16_t length, loopLength, fineTune, volume;
    *startOffset = tp_b32(s);
    length = tp_b16(s);
    loopStart = tp_b32(s);
    loopLength = tp_b16(s);
    if (c->vol_first) { volume = tp_b16(s); fineTune = tp_b16(s); }
    else { fineTune = tp_b16(s); volume = tp_b16(s); }
    if (fineTune > 0x456) {
        si.len = 0; si.ls = 0; si.ll = 1; si.vol = 0; si.ft = 0;
        return si;
    }
    si.len = length;
    si.ls = (uint16_t)((uint32_t)(loopStart - *startOffset) / 2);
    si.ll = loopLength;
    si.vol = (uint8_t)volume;
    si.ft = (uint8_t)(fineTune / 74);
    return si;
}

static void fix_addresses(uint32_t firstAddress, uint8_t sampleCount, uint32_t *tdo, uint32_t *tto, uint32_t *sdo) {
    if (firstAddress != 0) {
        uint32_t temp = (sampleCount + 1U) * 16;
        uint32_t temp1 = *tto - temp;
        *tto = temp;
        *tdo -= temp1;
        *sdo -= temp1;
    }
}

/* ThePlayer4xFormatBase.CheckForThePlayerFormat */
static int check_for_the_player_format(P4x *c, TpIn *s, const char *mark) {
    uint8_t id[4], positionListLength, sampleCount;
    uint32_t tdo, tto, sdo, samplesSize, firstAddress, endOffset;
    uint16_t lastLength;
    if (s->n < 20) return 0;
    tp_seek(s, 0);
    tp_read(s, id, 4);
    if (memcmp(id, mark, 4) != 0) return 0;
    tp_skip(s, 1);
    positionListLength = tp_u8(s);
    sampleCount = tp_u8(s);
    tp_skip(s, 1);
    tdo = tp_b32(s); tto = tp_b32(s); sdo = tp_b32(s);
    samplesSize = check_sample_info(c, s, sampleCount, &firstAddress, &lastLength);
    if (samplesSize == 0) return 0;
    tp_seek(s, positionListLength * 8 + (sampleCount + 1) * 16 + 4);
    if (tp_b16(s) != 0xffff) return 0;
    fix_addresses(firstAddress, sampleCount, &tdo, &tto, &sdo);
    endOffset = sdo + 4 + samplesSize;
    if ((long long)endOffset > (long long)s->n + TP_MAX_MISSING) {
        uint32_t temp;
        lastLength = (uint16_t)(lastLength * 2);
        temp = endOffset - lastLength;
        if ((long long)temp > (long long)s->n + TP_MAX_MISSING) return 0;
        c->skipLastSample = 1;
    } else
        c->skipLastSample = 0;
    return 1;
}

/* ProWizardConverterWorkerBase.BuildPositionList with the 4 track offsets as key */
static void create_position_list(P4x *c) {
    uint64_t used[256];
    int nused = 0;
    c->numberOfPatterns = 0;
    for (int i = 0; i < c->numberOfPositions; i++) {
        uint64_t next = ((uint64_t)c->trackOffsetTable[0][i] << 48) | ((uint64_t)c->trackOffsetTable[1][i] << 32) |
                        ((uint64_t)c->trackOffsetTable[2][i] << 16) | c->trackOffsetTable[3][i];
        int found = 0;
        for (int j = 0; j < nused; j++) {
            if (used[j] == next) { c->positionList[i] = (uint8_t)j; found = 1; break; }
        }
        if (!found) {
            c->positionList[i] = (uint8_t)c->numberOfPatterns++;
            used[nused++] = next;
        }
    }
}

/* ThePlayer4xFormatBase.PrepareConversion */
static int prepare_conversion(P4x *c, TpIn *s) {
    tp_seek(s, 5);
    c->numberOfPositions = tp_u8(s);
    c->numberOfSamples = tp_u8(s);
    tp_skip(s, 1);
    c->trackDataOffset = tp_b32(s);
    c->trackTableOffset = tp_b32(s);
    c->sampleDataOffset = tp_b32(s);
    c->firstSampleAddress = tp_b32(s);
    fix_addresses(c->firstSampleAddress, c->numberOfSamples, &c->trackDataOffset, &c->trackTableOffset, &c->sampleDataOffset);
    /* CreateOffsetTable */
    tp_seek(s, 20 + c->numberOfSamples * 16);
    for (int i = 0; i < c->numberOfPositions; i++)
        for (int j = 0; j < 4; j++) c->trackOffsetTable[j][i] = (uint16_t)(tp_b16(s) & 0xfffe);
    create_position_list(c);
    if (c->skipLastSample) c->numberOfSamples--;
    return 1;
}

/* ThePlayer4xFormatBase.ConvertData */
static int convert_data(P4x *c, TpIn *s, P4xChannel *ch, uint8_t *pattern, int writeOffset) {
    uint8_t temp;
    int result = 0;
    if (ch->p4xInfo < 0) {
        ch->p4xInfo++;
        memcpy(pattern + writeOffset, ch->proPatternData, 4);
        return 0;
    }
    if (ch->p4xInfo > 0) {
        ch->p4xInfo--;
        memset(pattern + writeOffset, 0, 4);
        return 0;
    }
    if (ch->repeatLines != 0) {
        ch->repeatLines--;
        ch->p4xPatternData[0] = tp_u8(s);
        ch->p4xPatternData[1] = tp_u8(s);
        ch->p4xPatternData[2] = tp_u8(s);
        ch->p4xInfo = tp_s8(s);
    } else {
        if (ch->oldPosition != -1) {
            tp_seek(s, ch->oldPosition);
            ch->oldPosition = -1;
        }
        temp = tp_u8(s);
        if ((temp & 0x80) != 0) {
            uint16_t offset;
            ch->repeatLines = tp_u8(s);
            offset = tp_b16(s);
            ch->oldPosition = (long)s->pos;
            tp_seek(s, (long)(uint32_t)(c->trackDataOffset + offset + 4U));
            ch->p4xPatternData[0] = tp_u8(s);
            ch->p4xPatternData[1] = tp_u8(s);
            ch->p4xPatternData[2] = tp_u8(s);
            ch->p4xInfo = tp_s8(s);
        } else {
            ch->p4xPatternData[0] = temp;
            ch->p4xPatternData[1] = tp_u8(s);
            ch->p4xPatternData[2] = tp_u8(s);
            ch->p4xInfo = tp_s8(s);
        }
    }

    if ((ch->p4xPatternData[0] & 0x1) != 0) ch->proPatternData[0] = 0x10;
    else ch->proPatternData[0] = 0x00;

    temp = (uint8_t)(ch->p4xPatternData[0] & 0xfe);
    if (temp != 0) {
        temp /= 2;
        temp--;
        if (temp < 45) {
            ch->proPatternData[0] |= tp_periods[temp][0];
            ch->proPatternData[1] = tp_periods[temp][1];
        } else
            ch->proPatternData[1] = 0x00;
    } else
        ch->proPatternData[1] = 0x00;

    ch->proPatternData[2] = ch->p4xPatternData[1];
    ch->proPatternData[3] = ch->p4xPatternData[2];

    switch (ch->proPatternData[2] & 0x0f) {
    case 0xb:
        ch->proPatternData[3] /= 2;
        result = 1;
        break;
    case 0xd:
        ch->proPatternData[3] = (uint8_t)((ch->proPatternData[3] / 0x10) * 10 + (ch->proPatternData[3] % 0x10));
        result = 1;
        break;
    case 0xe:
        if ((ch->proPatternData[3] & 0xf0) == 0x00) {
            if (ch->proPatternData[3] == 0x02) ch->proPatternData[3] = 0x01;
        } else {
            if ((ch->proPatternData[2] & 0xf0) == 0xc0) ch->proPatternData[3]++;   /* C# line 474: tests [2], as in the original */
        }
        break;
    case 0xa:
    case 0x5:
    case 0x6:
        if (ch->proPatternData[3] >= 0x80) {
            ch->proPatternData[3] = (uint8_t)((uint8_t)(-(int)(int8_t)ch->proPatternData[3]) << 4);
            if (ch->proPatternData[3] >= 0x80) ch->proPatternData[3] = (uint8_t)(-(int)(int8_t)ch->proPatternData[3]);
        }
        break;
    case 0x8:
        ch->proPatternData[2] &= 0xf0;
        break;
    }

    memcpy(pattern + writeOffset, ch->proPatternData, 4);
    return result;
}

static int convert(P4x *c, TpIn *s, TpMod *m) {
    int sampleCount = 0, cnt;

    memset(c->sampleLengths, 0, sizeof c->sampleLengths);
    if (!prepare_conversion(c, s)) return 0;

    /* GetModuleName: not overridden (null) */
    memset(m->name, 0, sizeof m->name);

    /* GetSamples */
    memset(c->sampleStartOffsets, 0, sizeof c->sampleStartOffsets);
    tp_seek(s, 20);
    cnt = c->numberOfSamples < 31 ? c->numberOfSamples : 31;
    for (int i = 0; i < cnt; i++) {
        uint32_t startOffset;
        P4xSampleInfo si = read_sample_info(c, s, &startOffset);
        TpSample *d;
        if (s->eof) return 0;
        d = &m->smp[sampleCount];
        memset(d->name, 0, sizeof d->name);
        d->len = si.len; d->ft = si.ft; d->vol = si.vol; d->ls = si.ls; d->ll = si.ll;
        c->sampleLengths[sampleCount++] = (uint32_t)si.len * 2;
        c->sampleStartOffsets[i] = startOffset - c->firstSampleAddress;
    }
    if (sampleCount == 0) return 0;
    m->nsmp = sampleCount;

    /* GetPositionList */
    if (c->numberOfPositions == 0 || s->eof) return 0;
    m->npos = c->numberOfPositions;
    memcpy(m->pos, c->positionList, c->numberOfPositions > 128 ? 128 : c->numberOfPositions);
    m->restart = 0x7f;

    /* GetPatterns */
    {
        int lastPatternNumber = -1;
        for (int i = 0; i < c->numberOfPositions; i++) {
            if (c->positionList[i] > lastPatternNumber) {
                P4xChannel channels[4];
                int maxRowNumber = 64;
                uint8_t *pattern;
                lastPatternNumber++;
                pattern = tp_pat_add(m);
                if (!pattern) return 0;
                memset(channels, 0, sizeof channels);
                for (int j = 0; j < 4; j++) channels[j].oldPosition = -1;
                for (int j = 0; j < 4; j++) {
                    uint16_t trackOffset = c->trackOffsetTable[j][i];
                    tp_seek(s, (long)(uint32_t)(c->trackDataOffset + 4U + trackOffset));
                    for (int k = 0; k < maxRowNumber; k++) {
                        if (convert_data(c, s, &channels[j], pattern, k * 16 + j * 4)) {
                            maxRowNumber = k + 1;
                            break;
                        }
                    }
                }
                if (s->eof) return 0;
            }
        }
    }

    /* WriteSampleData */
    {
        uint32_t sampleOffset = c->sampleDataOffset + 4U;
        for (int i = 0; i < cnt; i++) {
            int length = (int)c->sampleLengths[i];
            if (length != 0) {
                size_t avail, k;
                tp_seek(s, (long)(uint32_t)(sampleOffset + c->sampleStartOffsets[i]));
                if ((long long)s->n - (long long)s->pos < (long long)length - TP_MAX_MISSING) return 0;
                /* SetSampleDataMarker: the next `length` bytes of the module stream */
                avail = s->pos < s->n ? s->n - s->pos : 0;
                k = avail < (size_t)length ? avail : (size_t)length;
                if (k && !tp_sdata_add(m, s->b + s->pos, k)) return 0;
                if (k < (size_t)length && !tp_sdata_zero(m, (size_t)length - k)) return 0;
                tp_skip(s, length);
            }
        }
    }
    return 1;
}

static int conv(const uint8_t *b, size_t n, TpMod *m, const char *mark, int vol_first) {
    P4x *c = (P4x *)calloc(1, sizeof *c);
    TpIn s;
    int r = 0;
    if (!c) return 0;
    c->vol_first = vol_first;
    s.b = b; s.n = n; s.pos = 0; s.eof = 0;
    if (check_for_the_player_format(c, &s, mark)) r = convert(c, &s, m);
    free(c);
    return r;
}

int tp_conv_40a(const uint8_t *b, size_t n, TpMod *m) { return conv(b, n, m, "P40A", 0); }
int tp_conv_40b(const uint8_t *b, size_t n, TpMod *m) { return conv(b, n, m, "P40B", 0); }
int tp_conv_41a(const uint8_t *b, size_t n, TpMod *m) { return conv(b, n, m, "P41A", 1); }
