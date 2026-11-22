#!/usr/bin/env python3
"""Corrections to the baked Amiga 500 model (src/a500_data.h), applied in place and safe to run twice.
usage: python3 tools/fix_a500_model.py src/a500_data.h

The SketchUp model has a floppy drive on BOTH ends: the end-face picture (a textured part: the photo of the drive) is used on the left end as well. The real Amiga 500 has its
drive on the right end only, the left end is plain. The textured triangles of the left end face (x < -0.985) are dropped; the plain grey end panel of the housing part lies right
behind them.

Also dropped: the housing triangles that lie completely under textured triangles (same plane, within 4e-4, all four sample points inside the textured ones; 5791 of 17348).
The SketchUp model has the housing and the textured picture of the lid, the keys and the sides on top of each other; where two surfaces lie in the same plane the depth buffer
picks the winner per pixel and the winner changes with every step of the turn: bright and dark streaks on the lid strip in front of the vents, flickering when the card turns
(measured on a screen recording: 6.75 % of the flat pixels jumped from frame to frame, 0.51 % after the change).
Only the index arrays c3_A500_ti and c3_A500_i and their counts C3_A500_NTT and C3_A500_NT change; the vertices stay.
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
# housing triangles completely under textured triangles (the textured part as it is after the step above)
v = arr('c3_A500_v').reshape(-1, 3) * Q; idx = arr('c3_A500_i').reshape(-1, 3); ti = ti[~left]
A = v[idx]; B = tv[ti]
def planes(T):
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); ln = np.linalg.norm(n, axis=1); ok = ln > 1e-12; n[ok] /= ln[ok][:, None]
    return n, (n * T[:, 0]).sum(1), ok
nA, dA, okA = planes(A); nB, dB, okB = planes(B)
def inside(P, Tm):
    a = Tm[:, 0]; b = Tm[:, 1]; c = Tm[:, 2]; v0 = c - a; v1 = b - a; v2 = P - a
    d00 = (v0 * v0).sum(1); d01 = (v0 * v1).sum(1); d02 = (v0 * v2).sum(1); d11 = (v1 * v1).sum(1); d12 = (v1 * v2).sum(1)
    den = d00 * d11 - d01 * d01; den[den == 0] = 1e-30
    u = (d11 * d02 - d01 * d12) / den; w = (d00 * d12 - d01 * d02) / den; e = -1e-3
    return (u >= e) & (w >= e) & (u + w <= 1 - e)
covered = np.zeros(len(A), bool)
for t in range(len(A)):
    if not okA[t]: continue
    c = A[t].mean(0)
    cand = np.where(okB & (np.abs((nB * nA[t]).sum(1)) > 0.9998) & (np.abs((nB * c).sum(1) - dB) < 4e-4))[0]
    if not len(cand): continue
    covered[t] = all(inside(np.repeat(P[None], len(cand), 0), B[cand]).any() for P in [c] + [c + (A[t][k] - c) * 0.97 for k in range(3)])
print("housing triangles completely under textured triangles dropped:", int(covered.sum()))
m = re.search(r'static const [a-z0-9_]+ c3_A500_i\[\d*\]\s*=\s*\{.*?\};\n', s, re.S); assert m
s = s[:m.start()] + fmt('c3_A500_i', 'uint16_t', idx[~covered].reshape(-1)) + s[m.end():]
s = re.sub(r'(#define C3_A500_NT )\d+', r'\g<1>%d' % int((~covered).sum()), s)
open(p, 'w').write(s)
