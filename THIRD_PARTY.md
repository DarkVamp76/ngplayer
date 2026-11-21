# Third-party components

- **SDL2** (zlib license), linked dynamically / bundled in the AppImage. https://libsdl.org
- **DejaVu Sans** glyph data is baked into `src/font_data.h`. DejaVu fonts are derived from Bitstream Vera and
  are distributed under a permissive licence (free to use, modify and redistribute; the font name may not be
  used for modified fonts). https://dejavu-fonts.github.io/License.html
- The OpenGL renderer layer (`src/gfx.inc`) originates from the author's own project ngSid.
- **ScummVM TFMX decoder** (`audio/mods/tfmx.cpp`, GPL-3.0-or-later) served as the reference for the semantics of the TFMX
  macro, pattern and trackstep commands and for the note period table. `src/core/tfmx_*.c` is an independent
  implementation with its own structure. https://www.scummvm.org
- **HivelyTracker replayer** (`replay.c` / `replay.h`, BSD-3-Clause, Copyright (c) 2006-2018 Pete Gordon) is the basis of the AHX
  player in `src/core/ahx.c` and `src/core/ahx_hvl.h`, adapted for ngPlayer (portable types, loading from memory, per-voice scope
  output and muting, bounds checks on values from the file). The licence text is kept at the top of both files.
  https://github.com/pete-gordon/hivelytracker
- **Nuked OPL3** (`src/core/opl/opl3.c`, `opl3.h`, LGPL-2.1-or-later, Copyright (C) 2013-2020 Nuke.YKT) is the FM synthesis (OPL2/OPL3)
  behind the AdLib formats. The sources keep their licence header; the licence text is in
  `src/core/opl/LICENSE.LGPL-2.1`. https://github.com/nukeykt/Nuked-OPL3
- **YM7128B emulation** (`src/core/ym7128b/`, BSD-2-Clause, Copyright (c) 2020-2023 Andrea Zoppi) emulates the reverb / surround chip of
  the AdLib Gold (`.agd`). The licence text is in `src/core/ym7128b/LICENSE.BSD-2-Clause`.
- **AdPlug** (LGPL-2.1-or-later, https://adplug.github.io) is the reference for the AdLib module players. `src/core/a2m_replay.c` is a
  transliteration of its Adlib Tracker II player core (Dmitry Smagin, Stanislav Baranec, Simon Peter et al.); `src/core/lds.*`, `rad.*` and
  `herad.*` are C ports that follow its LOUDNESS, RAD (public domain RAD2 replayer by Shayde / Reality) and HERAD players. The AdLib Gold
  processor (`src/core/adgold.*`) is modelled after the DOSBox Staging emulation (GPL-2.0-or-later).
- **aPlib / LZH unpackers** (`src/core/a2m_unpack.c`): the aPlib LZ77 format by Joergen Ibsen and the static Huffman scheme of Haruhiko
  Okumura's public domain `ar002`, written from the published algorithms.
