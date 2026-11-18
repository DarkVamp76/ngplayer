#!/usr/bin/env python3
"""Generates the file type icons (assets/mime/ngplayer-<format>.svg) and the shared-mime-info file (packaging/ngplayer-mime.xml) for all formats of ngPlayer.
The colour of each icon is the colour of the format badge in src/main.c (FMT_COL), so the icons and the playlist badges match.
Usage: python3 tools/gen_mime.py   (from the repository root)"""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, 'src/main.c'), encoding='utf-8').read()
names = re.findall(r'"([^"]*)"', re.search(r'FMT_NAME\[FMT_COUNT\] = \{(.*?)\};', src, re.S).group(1))
cols = re.findall(r'\{\s*([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f,\s*1\s*\}', re.search(r'FMT_COL\[FMT_COUNT\] = \{(.*?)\};', src, re.S).group(1))
colour = {n: '#%02x%02x%02x' % tuple(round(float(c) * 255) for c in cols[i]) for i, n in enumerate(names) if n}

# format -> (description, German description, extensions, additional globs, parent type)
FM = {'AMD', 'MAD', 'A2M', 'HERAD', 'RAD', 'LDS'}
F = {
    'MOD': ('Amiga module', 'Amiga-Modul', ['mod'], [], 'audio/x-mod'),
    'S3M': ('Scream Tracker 3 module', 'Scream-Tracker-3-Modul', ['s3m'], [], 'audio/x-s3m'),
    'XM': ('FastTracker 2 extended module', 'FastTracker-2-Modul', ['xm'], [], 'audio/x-xm'),
    'IT': ('Impulse Tracker module', 'Impulse-Tracker-Modul', ['it'], [], 'audio/x-it'),
    'STM': ('Scream Tracker 2 module', 'Scream-Tracker-2-Modul', ['stm'], [], None),
    'PTM': ('PolyTracker module', 'PolyTracker-Modul', ['ptm'], [], None),
    'ULT': ('UltraTracker module', 'UltraTracker-Modul', ['ult'], [], None),
    '669': ('Composer 669 module', 'Composer-669-Modul', ['669'], [], None),
    'MTM': ('MultiTracker module', 'MultiTracker-Modul', ['mtm'], [], None),
    'MED': ('OctaMED module', 'OctaMED-Modul', ['med', 'mmd0', 'mmd1', 'mmd2', 'mmd3'], [], None),
    'PSM': ('Epic MegaGames PSM module', 'Epic-MegaGames-PSM-Modul', ['psm'], [], None),
    'FAR': ('Farandole Composer module', 'Farandole-Composer-Modul', ['far'], [], None),
    'MDL': ('DigiTrakker module', 'DigiTrakker-Modul', ['mdl'], [], None),
    'DMF': ('X-Tracker module', 'X-Tracker-Modul', ['dmf'], [], None),
    'TFMX': ('TFMX song', 'TFMX-Song', ['tfx'], ['mdat.*'], None),
    'AMF': ('DSMI / Asylum AMF module', 'DSMI-/Asylum-AMF-Modul', ['amf'], [], None),
    'SFX': ('SoundFX module', 'SoundFX-Modul', ['sfx'], [], None),
    'AHX': ('AHX module', 'AHX-Modul', ['ahx'], [], None),
    'DBM': ('DigiBooster Pro module', 'DigiBooster-Pro-Modul', ['dbm'], [], None),
    'DSM': ('DSIK module', 'DSIK-Modul', ['dsm'], [], None),
    'DTM': ('Digital Tracker module', 'Digital-Tracker-Modul', ['dtm'], [], None),
    'AMD': ('AMUSIC Adlib Tracker module', 'AMUSIC-Adlib-Tracker-Modul', ['amd'], [], None),
    'MAD': ('Mlat Adlib Tracker module', 'Mlat-Adlib-Tracker-Modul', ['mad'], [], None),
    'A2M': ('Adlib Tracker 2 module', 'Adlib-Tracker-2-Modul', ['a2m', 'a2t'], [], None),
    'HERAD': ('Herbulot AdLib System music', 'Herbulot-AdLib-System-Musik', ['hsq', 'sqx', 'sdb', 'agd', 'ha2'], [], None),
    'RAD': ('Reality ADlib Tracker tune', 'Reality-ADlib-Tracker-Stück', ['rad'], [], None),
    'LDS': ('LOUDNESS Sound System song', 'LOUDNESS-Sound-System-Stück', ['lds', 'ld0'], [], None),
}

def icon_svg(fmt):
    c = colour[fmt]; n = len(fmt)
    size = 40 if n <= 3 else 33 if n == 4 else 27
    if fmt in FM:                                     # a chip with pins: the FM formats
        pins = ''.join('<rect x="%d" y="%d" width="3" height="6" rx="1" fill="%s"/><rect x="%d" y="%d" width="3" height="6" rx="1" fill="%s"/>' % (44 + 9 * i, 83, c, 44 + 9 * i, 105, c) for i in range(4))
        glyph = '%s<rect x="40" y="88" width="48" height="16" rx="3" fill="none" stroke="%s" stroke-width="3"/><circle cx="50" cy="96" r="2.5" fill="%s"/>' % (pins, c, c)
    else:                                             # a waveform: the sampled and synthesised formats
        glyph = '<polyline points="30,96 40,96 46,84 54,108 62,86 70,106 77,92 84,96 98,96" fill="none" stroke="%s" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>' % c
    return '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">
  <defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1b2440"/><stop offset="1" stop-color="#0f1526"/></linearGradient></defs>
  <path d="M26 6h54l26 26v84a8 8 0 0 1-8 8H26a8 8 0 0 1-8-8V14a8 8 0 0 1 8-8z" fill="url(#g)" stroke="%(c)s" stroke-opacity="0.55" stroke-width="2.5"/>
  <path d="M80 6v18a8 8 0 0 0 8 8h18z" fill="%(c)s" fill-opacity="0.28"/>
  <rect x="18" y="46" width="88" height="3" fill="%(c)s" fill-opacity="0.35"/>
  <text x="62" y="%(ty)d" text-anchor="middle" font-family="DejaVu Sans, Verdana, sans-serif" font-weight="bold" font-size="%(size)d" fill="%(c)s">%(fmt)s</text>
  %(glyph)s
</svg>
''' % dict(c=c, size=size, fmt=fmt, glyph=glyph, ty=76 - (0 if n <= 4 else 2) - 8)

def main():
    os.makedirs(os.path.join(ROOT, 'assets/mime'), exist_ok=True)
    xml = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<!-- File types of ngPlayer: one type and one icon per format. Generated by tools/gen_mime.py -->',
           '<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">']
    types = []
    for fmt in names:
        if not fmt: continue
        desc, de, exts, extra, parent = F[fmt]
        open(os.path.join(ROOT, 'assets/mime/ngplayer-%s.svg' % fmt.lower()), 'w', encoding='utf-8').write(icon_svg(fmt))
        t = 'audio/x-ngplayer-%s' % fmt.lower(); types.append(t)
        xml.append('  <mime-type type="%s">' % t)
        xml.append('    <comment>%s</comment>' % desc)
        xml.append('    <comment xml:lang="de">%s</comment>' % de)
        xml.append('    <icon name="ngplayer-%s"/>' % fmt.lower())
        xml.append('    <generic-icon name="audio-x-generic"/>')
        if parent: xml.append('    <sub-class-of type="%s"/>' % parent)
        for e in exts: xml.append('    <glob pattern="*.%s" weight="60"/>' % e)
        for g in extra: xml.append('    <glob pattern="%s" weight="60"/>' % g)
        xml.append('  </mime-type>')
    xml.append('</mime-info>')
    open(os.path.join(ROOT, 'packaging/ngplayer-mime.xml'), 'w', encoding='utf-8').write('\n'.join(xml) + '\n')
    # the desktop file lists every type
    p = os.path.join(ROOT, 'packaging/ngplayer.desktop'); d = open(p, encoding='utf-8').read()
    base = ['audio/x-mod', 'audio/x-s3m', 'audio/x-xm', 'audio/x-it']
    d = re.sub(r'MimeType=.*', 'MimeType=' + ';'.join(base + types) + ';', d)
    open(p, 'w', encoding='utf-8').write(d)
    print('%d icons, %d types' % (len(types), len(types)))

main()
