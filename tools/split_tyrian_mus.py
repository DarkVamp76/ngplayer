#!/usr/bin/env python3
"""Splits Tyrian's MUSIC.MUS into single LOUDNESS Sound System songs (.lds), which ngPlayer plays.
The file starts with a 16 bit count and one 32 bit start offset per song; a song ends where the next one starts (the last one at the end of the file).
Usage: split_tyrian_mus.py MUSIC.MUS output-folder"""
import os, struct, sys
d = open(sys.argv[1], 'rb').read(); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
n = struct.unpack_from('<H', d, 0)[0]
offs = list(struct.unpack_from('<%dI' % n, d, 2)) + [len(d)]
for i in range(n):
    open(os.path.join(out, 'TYRIAN-%02d.LDS' % (i + 1)), 'wb').write(d[offs[i]:offs[i + 1]])
print('%d songs written to %s' % (n, out))
