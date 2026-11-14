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
