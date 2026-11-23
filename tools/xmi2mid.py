#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# xmi2mid.py - the music of Origin / Miles Sound System games (XMIDI) as standard MIDI files.
#
# Strike Commander keeps its music in a TRE archive (LILTRE.TRE): a table of 74-byte records (flag, a 65-byte name, ..., offset and size as two little-endian
# 32-bit numbers at the end), then the files. The music comes once per sound device, told apart by the extension: .MID = General MIDI (MIDI.DRV), .ROL = Roland MT-32
# (ROLAND.DRV), .ADL = AdLib; the .DAT / .MT / .AD files are timbre data. Such a file holds the songs one after the other as XMIDI: FORM XDIR, then CAT XMID with one
# FORM XMID per sequence (TIMB = the timbres, EVNT = the events). The converter finds the XMIDI blocks by their IFF tags, so any file or archive with raw XMIDI works.
#
# XMIDI differs from a standard MIDI file in two ways: a note is ONE event with its length (the note-off is made from it), and the time between events is a run of
# bytes below 0x80 that are added up. The time base is fixed at 120 ticks per second (the tempo events inside are not used). The Miles controllers 110..120
# (channel lock, bank select, FOR / NEXT loops, branch) are dropped; a General MIDI reset is put in front.
#
# usage: python3 tools/xmi2mid.py LILTRE.TRE OUT_DIR [--ext MID] [--prefix SC] [--min-seconds 15] [--min-notes 60] [--all] [--list]
#   --ext MID     which device's files of a TRE archive (MID General MIDI, ROL MT-32, ADL AdLib); several: --ext MID,ROL. Default MID.
#   --all         also the short ones (jingles, signals); default: only songs of at least --min-seconds and --min-notes
#   --list        only list what is in there (archive entries, sequences)
# A file that is not a TRE archive (a .PAK, a .XMI, raw data) is searched for XMIDI blocks as a whole.
import os, re, struct, sys

DROP_CC = set(range(110, 121))                         # the Miles controllers (see above)

def vlq_w(v):
    b = [v & 0x7f]; v >>= 7
    while v: b.append((v & 0x7f) | 0x80); v >>= 7
    return bytes(reversed(b))

def vlq_r(b, i):
    v = 0
    while True:
        c = b[i]; i += 1; v = (v << 7) | (c & 0x7f)
        if not c & 0x80: return v, i

def events(ev):
    """EVNT chunk -> [(tick, status, a, b)], note-offs made from the note lengths; meta / SysEx: (tick, 0xFF / 0xF0, type, data)"""
    i = 0; t = 0; out = []; n = len(ev)
    while i < n:
        c = ev[i]
        if c < 0x80: t += c; i += 1; continue
        i += 1
        if c == 0xFF:
            ty = ev[i]; i += 1; l, i = vlq_r(ev, i); out.append((t, 0xFF, ty, ev[i:i + l])); i += l
            if ty == 0x2F: break
        elif c == 0xF0:
            l, i = vlq_r(ev, i); out.append((t, 0xF0, 0, ev[i:i + l])); i += l
        else:
            hi = c & 0xF0; ch = c & 15
            if hi == 0x90:
                note, vel = ev[i], ev[i + 1]; i += 2; d, i = vlq_r(ev, i)
                out.append((t, 0x90 | ch, note, vel)); out.append((t + d, 0x80 | ch, note, 0))
            elif hi in (0xC0, 0xD0): out.append((t, c, ev[i], 0)); i += 1
            else: out.append((t, c, ev[i], ev[i + 1])); i += 2
    return out

def to_smf(ev):
    es = [e for e in events(ev) if not (0xB0 <= e[1] < 0xC0 and e[2] in DROP_CC) and not (e[1] == 0xFF and e[2] == 0x51)]
    es.sort(key=lambda e: (e[0], 0 if 0x80 <= e[1] < 0x90 else 1))        # the note-off before a note-on at the same time
    tr = bytearray(b'\x00\xFF\x51\x03\x07\xA1\x20' b'\x00\xF0\x05\x7E\x7F\x09\x01\xF7'); t = 0   # 500000 us per quarter, division 60 = 120 ticks / s; General MIDI on
    for e in es:
        tr += vlq_w(max(0, e[0] - t)); t = max(t, e[0])
        if e[1] == 0xFF: tr += bytes([0xFF, e[2]]) + vlq_w(len(e[3])) + e[3]
        elif e[1] == 0xF0: tr += b'\xF0' + vlq_w(len(e[3])) + e[3]
        elif 0xC0 <= e[1] < 0xE0: tr += bytes([e[1], e[2]])
        else: tr += bytes([e[1], e[2], e[3] & 0x7f])
    tr += b'\x00\xFF\x2F\x00'
    return b'MThd' + struct.pack('>IHHH', 6, 0, 1, 60) + b'MTrk' + struct.pack('>I', len(tr)) + bytes(tr)

def stats(ev):
    es = events(ev); notes = [e for e in es if 0x90 <= e[1] < 0xA0]
    return len(notes), max([e[0] for e in es] or [0]) / 120.0

def xmidi_blocks(d):
    """the EVNT chunks of all XMIDI blocks (FORM XDIR + CAT XMID) of a byte string, in order"""
    for m in re.finditer(rb'FORM(....)XDIR', d, re.S):
        p = m.start(); r = p + 8 + struct.unpack('>I', m.group(1))[0]; r += r & 1
        if d[r:r + 4] != b'CAT ' or d[r + 8:r + 12] != b'XMID': continue
        cl = struct.unpack('>I', d[r + 4:r + 8])[0]; blk = d[r + 12:r + 8 + cl]; q = 0
        while q + 12 <= len(blk) and blk[q:q + 4] == b'FORM':
            l = struct.unpack('>I', blk[q + 4:q + 8])[0]; body = blk[q + 12:q + 8 + l]; k = 0
            while k + 8 <= len(body):
                tag = body[k:k + 4]; cl2 = struct.unpack('>I', body[k + 4:k + 8])[0]
                if tag == b'EVNT': yield body[k + 8:k + 8 + cl2]
                k += 8 + cl2 + (cl2 & 1)
            q += 8 + l + (l & 1)

def tre_entries(d):
    """[(name, offset, size)] of a TRE archive, or None if the file is not one"""
    if len(d) < 8: return None
    n, toc = struct.unpack('<II', d[:8])
    if not (0 < n < 5000) or 8 + n * 74 > len(d) or d[8] != 1 or d[9:15] != b'..\\..\\': return None
    out = []
    for i in range(n):
        r = d[8 + i * 74:8 + (i + 1) * 74]
        if r[0] != 1 or b'\0' not in r[1:]: break
        off, sz = struct.unpack('<II', r[66:74]); out.append((r[1:r.index(0, 1)].decode('latin1'), off, sz))
    return out

def main():
    a = sys.argv[1:]
    if len(a) < 2 and '--list' not in a or not a: print(__doc__); return 1
    opt = lambda k, dv: a[a.index(k) + 1] if k in a else dv
    src = a[0]; dst = a[1] if len(a) > 1 and not a[1].startswith('--') else '.'
    exts = [e.upper().lstrip('.') for e in opt('--ext', 'MID').split(',')]; prefix = opt('--prefix', 'SC')
    minsec = float(opt('--min-seconds', 15)); minnotes = int(opt('--min-notes', 60)); allm = '--all' in a; lst = '--list' in a
    d = open(src, 'rb').read(); ents = tre_entries(d)
    parts = []                                           # (label, data)
    if ents:
        for nm, off, sz in ents:
            base = nm.split('\\')[-1]; ext = base.rsplit('.', 1)[-1].upper() if '.' in base else ''
            if lst: print('%-44s offset %9d size %8d' % (nm, off, sz))
            if ext in exts and d[off:off + sz].find(b'XDIR') >= 0: parts.append((base.rsplit('.', 1)[0].upper(), d[off:off + sz]))
    else: parts.append((os.path.splitext(os.path.basename(src))[0].upper(), d))
    if not lst: os.makedirs(dst, exist_ok=True)
    w = 0
    for label, data in parts:
        for k, ev in enumerate(xmidi_blocks(data), 1):
            try: n, sec = stats(ev); smf = to_smf(ev)
            except IndexError: continue                      # cut off
            keep = allm or (n >= minnotes and sec >= minsec)
            if lst: print('  %s %3d: %5d notes %6.1f s%s' % (label, k, n, sec, '' if keep else '  (short)'))
            elif keep:
                name = '%s-%s-%03d.mid' % (prefix, label, k); open(os.path.join(dst, name), 'wb').write(smf); w += 1
    if not lst: print('%d files in %s' % (w, dst))
    return 0

if __name__ == '__main__': sys.exit(main())
