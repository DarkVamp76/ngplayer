<p align="center"><img src="assets/ngplayer-logo-dark.png" alt="ngPlayer" width="520"></p>

# ngPlayer

A fast, good-looking module player for Linux with a GPU-rendered interface: per-voice oscilloscopes, realtime
panning and buttery-smooth pattern scrolling.

<img width="974" height="852" alt="Bildschirmfoto_20261109_011108" src="https://github.com/user-attachments/assets/671bf8eb-88cb-423e-a01b-592d369f34a9" />
<img width="1318" height="1116" alt="Bildschirmfoto_20261109_011034" src="https://github.com/user-attachments/assets/73cd3f3a-93f1-46b6-97bf-57baa953fe1e" />

## Why

ngPlayer is inspired by the great **XTC-Play** module player by Sanction. Back in the DOS days, players like it
on a Gravis UltraSound (and UltraSound PnP) set the bar for how tracker music could sound. I wanted that same
level of quality on Linux, together with an interface that is a joy to look at.

## Supported formats

| Format | Status |
| --- | --- |
| MOD (ProTracker and compatibles) | supported |
| S3M (Scream Tracker 3) | supported |
| STM (Scream Tracker 2) | supported (4 channels, played through the S3M engine) |
| PTM (Poly Tracker) | supported (1-32 channels, 16-bit samples, played through the S3M engine) |
| ULT (UltraTracker) | supported (1-32 channels, 8/16-bit samples, played through the S3M engine) |
| 669 (Composer 669 / UNIS 669) | supported (8 channels, linear-frequency slides, played through the S3M engine) |
| MTM (MultiTracker) | supported (1-32 channels, played through the MOD engine) |
| PSM (Epic MegaGames, PSM16 "PSM\xFE") | supported (Epic Pinball, Silverball; played through the MOD engine) |
| FAR (Farandole Composer) | supported (16 channels, 8/16-bit samples, played through the MOD engine; fine tempo D?/E?, slide-to-volume A? and note offset C? are not implemented) |
| MDL (Digitrakker) | supported (up to 32 channels, 8/16-bit and packed samples, volume/panning envelopes and fadeout, played through the XM engine; frequency envelopes, auto vibrato, instrument panning and pattern-loop E6x are not implemented) |
| MED / OctaMED (MMD0-MMD3) | supported (sample instruments, up to 32 tracks, played through the MOD engine; synth/hybrid instruments are silent) |
| DBM (DigiBooster Pro, `DBM0`, versions 2.0-2.33) | supported (up to 64 channels, volume and pan envelopes with up to 32 points, two effects per cell, the echo DSP, ping-pong loops, 8/16-bit samples; played through the XM engine; not implemented: 32-bit samples, channel on/off `E5x`, coarse offset `E7x`, the dynamic part of some DSP effects) |
| AHX (Abyss' Highest eXperience, Amiga synthesizer music, signature `THX`) | supported (4 channels, synthesised in real time: waveforms, filter, PWM, vibrato, performance lists; the engine is adapted from the HivelyTracker replayer; AHX only, no HVL) |
| SFX (SoundFX 1.3 / 2.0, Linel, Amiga: *Future Wars*, *Twinworld*, ...) | supported (4 channels, 15 or 31 samples, played through the MOD engine; the LED filter and the semitone-add/subtract effects are not implemented) |
| AMF (DSMI, Otto Chrons; signature `AMF` + version 0x08-0x0E) | supported (up to 32 channels, event-based tracks converted to MOD cells, played through the MOD engine; slides and portamento stay active until the next effect, as in the format; checked against one file only, some effects are approximated) |
| AMF (Asylum Music Format, *Crusader: No Remorse / No Regret*; signature `ASYLUM Music Format V1.0`; a different format than DSMI AMF) | supported (8 channels, played through the MOD engine) |
| DSM (DSIK / Digisound Interface Kit, `RIFF`/`DSMF`) | supported (up to 16 channels, 8-bit samples signed, unsigned or delta coded, ProTracker effects with panning `8xx`, played through the MOD engine; pitch within 0.01 cents and level of libopenmpt, correlation 0.975-0.999 on 28 files; not implemented: `ED` note delay without a note on the line) |
| XM (FastTracker 2) | supported |
| IT (Impulse Tracker) | supported, still being completed |
| DMF (X-Tracker, v8) | supported, some rare effects still missing |
| DTM (Digital Tracker, Atari Falcon030, `D.T.`) | supported (4-24 channels, 8/16-bit and stereo samples, pattern formats "2.04" and ProTracker cells; played through the IT engine like libopenmpt does; the tick-based "2.06" format of Digital Home Studio, instrument envelopes and the global panning/volume chunk are not implemented) |
| AMD (AMUSIC Adlib Tracker, Elyssis) | supported (AdLib FM, OPL2, 9 channels; packed and unpacked modules; played by an own replayer on the Nuked-OPL3 emulator) |
| MAD (Mlat Adlib Tracker, `MAD+`) | supported (AdLib FM, OPL2, 9 channels, 32 rows per pattern; same FM engine as AMD) |
| A2M / A2T (Adlib Tracker 2, Subz3ro) | supported for all file versions 1 to 14 and the tiny `.a2t` variant (OPL3, 18 channels, two effects per cell, instrument macros, 4-operator instruments; the replayer is a C port of the Adlib Tracker 2 player core; the old versions use sixpack, LZW and LZSS packers and one effect per cell; register-identical to AdPlug's AT2 core on 46 test modules - versions 1, 5, 9, 11, 14 and an A2T; versions 2-4 and 6-8 are untested for lack of files) |
| HERAD (Herbulot AdLib System, Cryo: Dune and others; `.hsq` `.sqx` `.sdb` `.agd` `.ha2`) | supported (HSQ and SQX unpackers, versions 1 and 2, keymaps and instrument macros, pitch slides; OPL2 with 9 voices, `.agd` runs the OPL3 with 18 voices and instrument panning, hands out the channels like the Dune AGD driver does and plays its 4-operator instruments (instrument mode 4); the pattern view shows one row per tick, 96 per measure; register-identical to AdPlug on 23 test files; `.agd` files also drive the AdLib Gold surround processor (YM7128B reverb / surround) and stereo processor (TDA8425 bass, treble, stereo mode) with the settings stored in the file header, as the Dune driver does) |
| RAD (Reality ADlib Tracker, `RAD by REALiTY!!`, `.rad`) | supported for file formats 1.0 (OPL2) and 2.1 (OPL3: 4-operator instruments, instrument and channel riffs, BPM); a C port of the public domain RAD2 replayer including its tune validation; register-identical to AdPlug on 5 test tunes; 9 channels, 64 lines per pattern |
| LDS (LOUDNESS Sound System, `.lds` / `.ld0`; the music of Epic MegaGames' Tyrian) | supported (AdLib OPL2, 9 channels, vibrato, tremolo, arpeggio, glide, fades; a C port of the AdPlug replayer, register-identical on 41 of 42 test songs, the 42nd differs only where the song uses an instrument that does not exist - AdPlug reads random memory there; Tyrian keeps its songs in `MUSIC.MUS`: `tools/split_tyrian_mus.py MUSIC.MUS folder` writes them as single `.lds` files) |
| TFMX (Chris Hülsbeck, `mdat.NAME` + `smpl.NAME` or `NAME.tfx` + `NAME.sam`) | supported (subsong 0, no pattern view yet) |

Looking for DMF files to try? The [Radical Rhythms Archive](https://www.radicalrhythms.org/) collects tracks by a German tracker group from the mid-1990s, including DMF modules.

Planned: 15-sample MOD, WAV/VOC, MP1/MP2/MP3, more FM formats (RAD).

## Features

- Pattern view with **all channels**: notes, instruments, volume column and colour-coded effects
- **Oscilloscope per voice** and **realtime panning** in every channel header
- Channel overview: volume bars with peak hold, last note, effect name and panning per channel
- Time-continuous scrolling locked to the audio clock, drawn at the full refresh rate of your monitor
- Spectrum analyser and level meters
- Four views for the main area (key **V**): **pattern** (the notes only; click a column number to mute, Ctrl+click for solo), **scopes** (one oscilloscope tile per voice with live panning), **orb** (a geodesic sphere shaped by the spectrum) and **voxel** (the spectrum as stacks of real voxel cubes in perspective)
- A **Specs** window (the small `i` right of the title, key `I`, or "Specs" in the list menu): the sound card as the head, the song title, the tracker that wrote the file, the number of voices and the technical details (OPL2 / OPL3, 8 or 16 bit samples, instruments, orders, patterns).
- A turning 3D model of the sound card in the header, chosen by the number of voices: the Paula chip (up to 4 voices), the Gravis UltraSound (GF1, up to 14), the UltraSound PnP (AMD InterWave, 15 to 32) and the Terratec EWS64 (more than 32 voices) The FM formats (AMD, MAD, A2M, HERAD, RAD, LDS) show the AdLib Gold card (Yamaha OPL3).
- Seek inside a song (Left/Right), drag and drop for files and folders, playlist

## Technical details

- Written in C. The replayer and mixer are independent of the interface; one loader and replayer per format
  behind a common song layer.
- Replayer behaviour is checked against reference renders with an automated regression suite.
- Interface: SDL2 for window, input and audio, **OpenGL 3.3** for drawing. A batched renderer draws rounded
  rectangles via signed distance fields, glow, polylines and a baked glyph atlas from a single vertex buffer.
- The audio thread renders in short slices and reports every row change with its sample position, so the
  display scrolls exactly in time with what you hear.

## Install

Download the latest `ngPlayer-*-x86_64.AppImage` from the [Releases](../../releases) page:

```sh
chmod +x ngPlayer-*-x86_64.AppImage
./ngPlayer-*-x86_64.AppImage ~/Music/modules
```

## Build from source

Requirements: `gcc`, `make`, SDL2 development files (Arch/CachyOS: `sudo pacman -S sdl2`,
Debian/Ubuntu: `sudo apt install libsdl2-dev`).

```sh
make
./build/ngplayer path/to/files-or-folders
```

Build an AppImage yourself: `sh packaging/build-appimage.sh`.

## Keys

| Key | Action |
| --- | --- |
| Space | Pause / play |
| Left / Right | Seek to previous / next order in the song |
| Up / Down | Previous / next file |
| Shift + Left / Right, mouse wheel | Scroll channels |
| R | Restart song |
| F / F11 | Fullscreen |
| F5 / F6 | Audio/video sync trim (±5 ms) |
| Esc | Quit |

## Status

ngPlayer is young. Playback is regression-tested against reference renders; known deviations and open items
are tracked in the issue tracker. Planned: more formats, online browser for [The Mod Archive](https://modarchive.org/),
per-channel mute/solo, interpolation and filter options.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Third-party components: [THIRD_PARTY.md](THIRD_PARTY.md).
