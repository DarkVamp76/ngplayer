#!/usr/bin/env python3
"""Correction to the baked Amiga 500 model (src/a500_data.h), applied in place and safe to run twice.
usage: python3 tools/fix_a500_model.py src/a500_data.h

The SketchUp model has a floppy drive on BOTH ends: the end-face picture (a textured part: the photo of the drive) is used on the left end as well. The real Amiga 500 has its
drive on the right end only, the left end is plain. The textured triangles of the left end face (x < -0.985) are dropped; the plain grey end panel of the housing part lies right
behind them. Only the index array c3_A500_ti and its count C3_A500_NTT change; the vertices stay.
"""
import re, sys, numpy as np
p = sys.argv[1]; s = open(p).read()
Q = 1 / 20000.0
def arr(name):
    m = re.search(r'\b' + name + r'\s*\[[^\]]*\]\s*=\s*\{(.*?)\};', s, re.S)
    return np.array([int(x, 0) for x in re.findall(r'-?\d+|0x[0-9a-fA-F]+', m.group(1))])
def fmt(name, ctype, vals, per=16):
    out = 'static const %s %s[%d] = {\n' % (ctype, name, len(vals)); line = []
    for x in vals:
        line.append(str(int(x)))
        if len(line) == per: out += ','.join(line) + ',\n'; line = []
    if line: out += ','.join(line) + ',\n'
    return out + '};\n'
tv = arr('c3_A500_tv').reshape(-1, 3) * Q; ti = arr('c3_A500_ti').reshape(-1, 3)
left = tv[ti].mean(1)[:, 0] < -0.985
print("textured triangles of the left end face dropped:", int(left.sum()))
m = re.search(r'static const [a-z0-9_]+ c3_A500_ti\[\d*\]\s*=\s*\{.*?\};\n', s, re.S); assert m
s = s[:m.start()] + fmt('c3_A500_ti', 'uint16_t', ti[~left].reshape(-1)) + s[m.end():]
s = re.sub(r'(#define C3_A500_NTT )\d+', r'\g<1>%d' % int((~left).sum()), s)
open(p, 'w').write(s)
