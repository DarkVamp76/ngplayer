# Bakes the MIDI sound module (a rack module) for the player: the model assets/models/midi-module.obj (+ .mtl) and the front panel picture
# assets/models/module-front.png (drawn by tools/make_module_panel.py). Same data layout as tools/bake_a500.py / bake_card3d.py (no back side):
#  - the parts with a plain material get a palette colour (the Kd of the .mtl);
#  - the part with the picture (material with map_Kd: the front) is the textured mesh, the picture is scaled to TW x TH (RGB565).
# usage: python3 tools/bake_module.py > src/module_data.h        (needs numpy and Pillow)
import os, sys, numpy as np
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__))
MD = os.path.join(here, '..', 'assets', 'models')
TAG = 'MODULE'
Q = 20000.0
TW, TH = 1280, 256
def arr(name, ctype, vals, per=16):
    s = 'static const %s %s[%d] = {\n' % (ctype, name, len(vals)); line = []
    for v in vals:
        line.append(str(int(v)))
        if len(line) == per: s += ','.join(line) + ',\n'; line = []
    if line: s += ','.join(line) + ',\n'
    return s + '};\n'
# materials
mats = {}; cur = None
for ln in open(os.path.join(MD, 'midi-module.mtl')):
    t = ln.split()
    if not t: continue
    if t[0] == 'newmtl': cur = t[1]; mats[cur] = {'kd': (0.7, 0.7, 0.7), 'tex': False}
    elif t[0] == 'Kd': mats[cur]['kd'] = tuple(float(x) for x in t[1:4])
    elif t[0] == 'map_Kd': mats[cur]['tex'] = True
# the model: v / vt / f with v/vt/vn, grouped by usemtl
P = []; T = []; faces = []; mat = None
for ln in open(os.path.join(MD, 'midi-module.obj')):
    t = ln.split()
    if not t: continue
    if t[0] == 'v': P.append([float(x) for x in t[1:4]])
    elif t[0] == 'vt': T.append([float(x) for x in t[1:3]])
    elif t[0] == 'usemtl': mat = t[1]
    elif t[0] == 'f':
        idx = []
        for tok in t[1:]:
            a = tok.split('/'); idx.append((int(a[0]) - 1, int(a[1]) - 1 if len(a) > 1 and a[1] else -1))
        for k in range(1, len(idx) - 1): faces.append((mat, idx[0], idx[k], idx[k + 1]))
P = np.array(P); T = np.array(T)
V = []; VC = []; I = []; TV = []; TUV = []; TI = []; cols = []; vmap = {}; tmap = {}
for m, *tri in faces:
    if mats[m]['tex']:
        for (vi, ti) in tri:
            key = (vi, ti)
            if key not in tmap:
                tmap[key] = len(TV); TV.append(P[vi]); u, v = T[ti]; TUV.append(((u * (TW - 1) + 0.5) / TW, ((1.0 - v) * (TH - 1) + 0.5) / TH))
            TI.append(tmap[key])
    else:
        c = tuple(int(round(x * 255)) for x in mats[m]['kd'])
        if c not in cols: cols.append(c)
        ci = cols.index(c)
        for (vi, ti) in tri:
            key = (vi, ci)
            if key not in vmap: vmap[key] = len(V); V.append(P[vi]); VC.append(ci)
            I.append(vmap[key])
V = np.array(V); TV = np.array(TV); TUV = np.array(TUV)
allp = np.vstack([V, TV]); lo = allp.min(0); hi = allp.max(0); c0 = (lo + hi) / 2; half = (hi[0] - lo[0]) / 2
def quant(Pp): return np.round((Pp - c0) / half * Q).astype(int).flatten()
im = Image.open(os.path.join(MD, 'module-front.png')).convert('RGB').resize((TW, TH), Image.LANCZOS)
a = np.asarray(im).astype(np.uint16); t565 = ((a[:, :, 0] >> 3) << 11) | ((a[:, :, 1] >> 2) << 5) | (a[:, :, 2] >> 3)
w, h, d = hi - lo
print('MODULE: verts', len(V), 'tris', len(I) // 3, 'colours', len(cols), 'texverts', len(TV), 'textris', len(TI) // 3, 'size', w.round(1), h.round(1), d.round(1), file=sys.stderr)
assert len(V) < 65536 and len(TV) < 65536 and len(cols) <= 255
s = '/* The MIDI sound module (a rack module), baked from assets/models/midi-module.obj and module-front.png by tools/bake_module.py (same layout as the cards of card3d_data.h, no back side) */\n#include <stdint.h>\n'
s += '#define C3_%s_NV %d\n#define C3_%s_NT %d\n#define C3_%s_NTV %d\n#define C3_%s_NTT %d\n#define C3_%s_TW %d\n#define C3_%s_TH %d\n' % (TAG, len(V), TAG, len(I) // 3, TAG, len(TV), TAG, len(TI) // 3, TAG, TW, TAG, TH)
s += '#define C3_%s_ASPECT %.4ff            /* width / depth */\n' % (TAG, w / d)
s += '#define C3_%s_HEIGHT %.4ff            /* height of the model in units of half the width */\n#define C3_%s_DEPTH %.4ff             /* depth of the model in units of half the width */\n' % (TAG, h / half, TAG, d / half)
s += arr('c3_%s_v' % TAG, 'int16_t', quant(V)) + arr('c3_%s_vc' % TAG, 'uint8_t', VC, 32) + arr('c3_%s_i' % TAG, 'uint16_t', I)
s += arr('c3_%s_pal' % TAG, 'uint8_t', [x for col in cols for x in col])
s += arr('c3_%s_tv' % TAG, 'int16_t', quant(TV)) + arr('c3_%s_tuv' % TAG, 'uint16_t', np.round(TUV.flatten() * 65535).astype(int)) + arr('c3_%s_ti' % TAG, 'uint16_t', TI)
s += arr('c3_%s_tex' % TAG, 'uint16_t', t565.flatten(), 12)
s += '/* no back side: an empty mesh and a 1 x 1 picture */\n#define C3_%s_BW 1\n#define C3_%s_BH 1\n#define C3_%s_NBV 0\n#define C3_%s_NBT 0\n' % (TAG, TAG, TAG, TAG)
s += arr('c3_%s_bv' % TAG, 'int16_t', [0, 0, 0]) + arr('c3_%s_buv' % TAG, 'uint16_t', [0, 0]) + arr('c3_%s_bi' % TAG, 'uint16_t', [0, 0, 0]) + arr('c3_%s_bpal' % TAG, 'uint8_t', [0, 0, 0]) + arr('c3_%s_bpix' % TAG, 'uint8_t', [0])
sys.stdout.write(s)
