#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extracts the General MIDI music of Sierra SCI1.1 games (Leisure Suit Larry 6, Freddy Pharkas, ...) as standard MIDI files.

usage: python3 tools/sci2mid.py GAME_DIR OUT_DIR [--prefix NAME] [--min-seconds 15] [--min-notes 60] [--shared] [--device 7] [--all]

  GAME_DIR     the folder with RESOURCE.MAP and RESOURCE.000 (the DOS game, e.g. from GOG)
  OUT_DIR      where the .mid files go (created)
  --prefix     file name prefix (default: the letters of the game folder name: LSL6-120.mid)
  --min-seconds / --min-notes   skip sound effects and jingles (default 15 s and 60 notes; 0 and 0 = everything)
  --shared     also write the sounds whose General MIDI track is the same data as the MT-32 track (the game plays its MT-32
               arrangement on a GM device; these are often wrong for GM, so they are left out by default)
  --device N   which device track to write: 7 = General MIDI (default), 12 = MT-32 (without its timbre set-up, which comes from patch.001
               and is not decoded: only useful to look at the notes)

What it does: reads the SCI1.1 resource map and the sound resources (LZS or DCL packed), takes the track of one device of each sound,
drops the control channel and the Sierra-specific controllers, and writes one MIDI track per channel (60 ticks per second, as SCI counts).
For General MIDI it starts every file with the set-up of the game's GM patch (patch.004): a GS reset and a pitch bend range of +-12
semitones on every channel. Without it the songs that bend notes sound wrong.

Two layouts of the resource files are known (found by the size of the entries of RESOURCE.MAP):
  6-byte entries (number16, offset32), 13-byte resource header (type, number16, packed32, unpacked32, method16), method 32 = LZS   (Leisure Suit Larry 6)
  5-byte entries (number16, offset24 * 2), 9-byte resource header (type, number16, packed16, unpacked16, method16), methods 18-20 = DCL  (Freddy Pharkas)
Needs only Python 3."""
import os, struct, sys

# ------------------------------------------------------------------ DCL (PKWARE "implode"), the structure of zlib's blast
_LITLEN = [11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8, 9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5, 7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12, 8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27, 44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45, 44, 173]
_LENLEN = [2, 35, 36, 53, 38, 23]
_DISTLEN = [2, 20, 53, 230, 247, 151, 248]
_BASE = [3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264]
_EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8]

def _construct(rep):
    lengths = []
    for r in rep: lengths += [r & 15] * ((r >> 4) + 1)
    count = [0] * 16
    for l in lengths: count[l] += 1
    offs = [0] * 17
    for l in range(1, 15): offs[l + 1] = offs[l] + count[l]
    sym = [0] * len(lengths)
    for s, l in enumerate(lengths):
        if l: sym[offs[l]] = s; offs[l] += 1
    return count, sym
_LIT, _LEN, _DIST = _construct(_LITLEN), _construct(_LENLEN), _construct(_DISTLEN)

def dcl_explode(data, usize):
    pos = [0, 0, 0]                                    # byte index, bit buffer, bits in it (least significant bit first)
    def bits(n):
        v, c = pos[1], pos[2]
        while c < n:
            if pos[0] >= len(data): raise EOFError('DCL: data ends')
            v |= data[pos[0]] << c; pos[0] += 1; c += 8
        pos[1] = v >> n; pos[2] = c - n
        return v & ((1 << n) - 1)
    def decode(h):
        count, sym = h; code = first = index = 0
        for l in range(1, 16):
            code |= bits(1) ^ 1
            c = count[l]
            if code < first + c: return sym[index + (code - first)]
            index += c; first += c; first <<= 1; code <<= 1
        raise ValueError('DCL: bad code')
    lit = bits(8); dic = bits(8)
    if lit > 1 or dic < 4 or dic > 6: raise ValueError('DCL: bad header')
    out = bytearray()
    while True:
        if bits(1):
            s = decode(_LEN); ln = _BASE[s] + bits(_EXTRA[s])
            if ln == 519: break
            sh = 2 if ln == 2 else dic
            dist = (decode(_DIST) << sh) + bits(sh) + 1
            if dist > len(out): raise ValueError('DCL: distance too far')
            for _ in range(ln): out.append(out[-dist])
        else:
            out.append(decode(_LIT) if lit else bits(8))
        if len(out) >= usize: break
    return bytes(out)

# ------------------------------------------------------------------ LZS (STACpack), bits most significant first
def lzs_unpack(data, usize):
    pos = 0; out = bytearray()
    def bits(n):
        nonlocal pos
        v = 0
        for _ in range(n):
            byte = data[pos >> 3] if (pos >> 3) < len(data) else 0
            v = (v << 1) | ((byte >> (7 - (pos & 7))) & 1); pos += 1
        return v
    while len(out) < usize and (pos >> 3) < len(data):
        if bits(1) == 0: out.append(bits(8)); continue
        off = bits(7) if bits(1) else bits(11)
        if off == 0: break
        l = bits(2)
        if l < 3: ln = l + 2
        else:
            l = bits(2)
            if l < 3: ln = l + 5
            else:
                ln = 8
                while True:
                    n = bits(4); ln += n
                    if n < 15: break
        for _ in range(ln): out.append(out[-off])
    return bytes(out)

# ------------------------------------------------------------------ the game's resource files
class Game:
    def __init__(self, folder):
        self.dir = folder
        d = open(os.path.join(folder, 'RESOURCE.MAP'), 'rb').read()
        toc = []; i = 0
        while True:
            t = d[i]; o = struct.unpack('<H', d[i + 1:i + 3])[0]; toc.append((t, o)); i += 3
            if t == 0xFF: break
        sizes = {5: 0, 6: 0}
        for k in range(len(toc) - 1):
            span = toc[k + 1][1] - toc[k][1]
            for s in sizes:
                if span % s == 0: sizes[s] += 1
        self.esize = 6 if sizes[6] == len(toc) - 1 else 5 if sizes[5] == len(toc) - 1 else None
        if self.esize is None: raise SystemExit('unknown RESOURCE.MAP layout (not SCI1.1?)')
        self.ents = {}
        for k in range(len(toc) - 1):
            t, o = toc[k]; n = (toc[k + 1][1] - o) // self.esize; lst = []
            for j in range(n):
                e = d[o + j * self.esize:o + (j + 1) * self.esize]
                if self.esize == 6: num, off = struct.unpack('<HI', e); off &= 0x0FFFFFFF
                else: num = struct.unpack('<H', e[:2])[0]; off = struct.unpack('<I', e[2:] + b'\0')[0] * 2
                lst.append((num, off))
            self.ents[t & 0x7F] = lst                        # the type numbers: 4 = sound, 9 = patch (older maps write them with bit 7 set)
        self.f = open(os.path.join(folder, 'RESOURCE.000'), 'rb')
    def read(self, off):
        self.f.seek(off)
        if self.esize == 6:
            h = self.f.read(13); cs, ds, m = struct.unpack('<IIH', h[3:13])
        else:
            h = self.f.read(9); cs, ds, m = struct.unpack('<HHH', h[3:9])
        raw = self.f.read(cs)
        if m == 0: return raw
        if m in (18, 19, 20): return dcl_explode(raw, ds)
        if m == 32: return lzs_unpack(raw, ds)
        raise ValueError('compression method %d is not known' % m)

# ------------------------------------------------------------------ a sound resource -> standard MIDI file
KEEP_CC = {0, 1, 6, 7, 10, 11, 32, 38, 64, 91, 93, 98, 99, 100, 101, 121, 123}       # the Sierra-specific controllers (loops, cues, voice mapping) are dropped

def parse_sound(b):
    """-> {device: [(offset, size)]}"""
    p = 0
    if b[0] == 0xF0:
        while b[p] != 0xFF: p += 1
        p += 1
    devs = {}
    while b[p] != 0xFF:
        dev = b[p]; p += 1; ch = []
        while b[p] != 0xFF:
            a, o, s = struct.unpack('<HHH', b[p:p + 6]); ch.append((o, s)); p += 6
        p += 1; devs[dev] = ch
    return devs

def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n: b.append(0x80 | (n & 0x7F)); n >>= 7
    return bytes(reversed(b))

def channel_events(b):
    ch = b[0] & 0x0F; p = 2; t = 0; ev = []; st = None
    while p < len(b):
        d = 0
        while p < len(b) and b[p] == 0xF8: d += 240; p += 1
        if p >= len(b): break
        d += b[p]; p += 1; t += d
        if p >= len(b) or b[p] == 0xFC: break
        if b[p] & 0x80: st = b[p]; p += 1
        if st is None: break
        k = st & 0xF0
        n = 1 if k in (0xC0, 0xD0) else 2 if k in (0x80, 0x90, 0xA0, 0xB0, 0xE0) else 0
        if not n: break
        ev.append((t, st, b[p:p + n])); p += n
    return ch, ev

def gm_init(patch4):
    """the set-up of the General MIDI patch (patch.004): (GS reset SysEx, [(status, controller, value)]) or None"""
    i = patch4.find(b'\xf0\x41\x10\x42\x12\x40\x00\x7f\x00\x41\xf7')
    if i < 2: return None
    n = patch4[i - 2] | (patch4[i - 1] << 8); d = patch4[i:i + n]
    j = d.index(0xF7) + 1; ev = []; st = None; k = j
    while k + 1 < len(d):
        if d[k] & 0x80: st = d[k]; k += 1
        ev.append((st, d[k], d[k + 1])); k += 2
    return d[:j], ev

def to_smf(b, device, name, init=None):
    """-> (file bytes, seconds, notes) or None when the sound has no such device"""
    devs = parse_sound(b)
    if device not in devs: return None
    tracks = []; maxt = 0; notes = 0
    for off, size in devs[device]:
        data = b[off:off + size]
        if size < 4: continue
        ch, ev = channel_events(data)
        if ch == 15: continue                                           # the control channel
        out = bytearray(); last = 0
        for tk, st, dt in ev:
            k = st & 0xF0
            if k == 0xB0 and dt[0] not in KEEP_CC: continue
            if k == 0x90 and dt[1] > 0: notes += 1
            out += vlq(tk - last) + bytes([k | ch]) + bytes(dt); last = tk; maxt = max(maxt, tk)
        if out: tracks.append(bytes(out))
    if not tracks or notes == 0: return None
    trk = lambda body: b'MTrk' + struct.pack('>I', len(body)) + body
    tn = name.encode('latin1', 'replace')[:60]
    t0 = b'\x00\xff\x03' + bytes([len(tn)]) + tn + b'\x00\xff\x51\x03' + struct.pack('>I', 1000000)[1:]        # 60 ticks per quarter at 1 s per quarter: one tick = 1/60 s
    if init:
        t0 += b'\x00\xf0' + vlq(len(init[0]) - 1) + init[0][1:]
        for st, cc, v in init[1]: t0 += b'\x00' + bytes([st, cc, v])
    elif device == 7: t0 += b'\x00\xf0\x05\x7e\x7f\x09\x01\xf7'          # a plain General MIDI reset
    t0 += b'\x00\xff\x2f\x00'
    body = b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks) + 1, 60) + trk(t0)
    for tr in tracks: body += trk(tr + b'\x00\xff\x2f\x00')
    return body, maxt / 60.0, notes

def main(argv):
    opt = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
    pos = [a for i, a in enumerate(argv) if not a.startswith('--') and not (i and argv[i - 1] in ('--prefix', '--min-seconds', '--min-notes', '--device'))]
    if len(pos) < 2: print(__doc__); return 1
    game_dir, out_dir = pos[0], pos[1]
    device = int(opt('--device', 7)); every = '--all' in argv
    min_s = 0 if every else float(opt('--min-seconds', 15)); min_n = 0 if every else int(opt('--min-notes', 60)); shared = '--shared' in argv or every
    prefix = opt('--prefix', ''.join(c for c in os.path.basename(os.path.normpath(game_dir)).title() if c.isupper() or c.isdigit()) or 'SCI')
    title = os.path.basename(os.path.normpath(game_dir)).replace(' - Frontier Pharmacist', '') or prefix            # the title inside the files: the game's folder name
    g = Game(game_dir)
    init = None
    if device == 7 and 4 in dict(g.ents.get(9, [])): init = gm_init(g.read(dict(g.ents[9])[4]))
    os.makedirs(out_dir, exist_ok=True); n = skipped = same = 0
    for num, off in g.ents.get(4, []):
        b = g.read(off)
        try: devs = parse_sound(b)
        except Exception: continue
        if device == 7 and 12 in devs and not shared:
            gm = b''.join(b[o:o + s] for o, s in devs.get(7, [])); mt = b''.join(b[o:o + s] for o, s in devs[12])
            if gm == mt: same += 1; continue                            # the game plays its MT-32 arrangement on a GM device
        r = to_smf(b, device, '%s - sound %d' % (title, num), init)
        if not r or r[1] < min_s or r[2] < min_n: skipped += 1; continue
        open(os.path.join(out_dir, '%s-%03d.mid' % (prefix, num)), 'wb').write(r[0]); n += 1
    print('%d files in %s (%d sounds too short, %d with the same data as the MT-32 track left out%s)' % (n, out_dir, skipped, same, '' if init is None else '; GM set-up from patch.004'))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
