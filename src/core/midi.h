/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Standard MIDI files (.mid, .midi, .kar, .rmi) played through a SoundFont (TinySoundFont).
 * Linux, macOS and the web player; the iOS / tvOS apps have no MIDI (NG_MIDI = 0). */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define NG_MIDI 0
#else
#define NG_MIDI 1
#endif

typedef struct Midi Midi;

int   midi_probe(const uint8_t *b, size_t n);                       /* "MThd", or a RIFF "RMID" wrapper around one */
void  midi_set_soundfont(const char *path);                          /* use this SoundFont (else: see midi_soundfont_path) */
const char *midi_soundfont_path(void);                               /* the SoundFont that is (or would be) used, NULL when none is found */
Midi *midi_open(const uint8_t *b, size_t n, int rate, char *err, size_t errcap);
void  midi_close(Midi *m);
int   midi_render(Midi *m, int16_t *out, int frames);                /* stereo frames; 0 at the end (after the last event and the release tail) */
void  midi_seek(Midi *m, double sec);                                /* replays program / controller / pitch state up to sec, no notes */
double midi_length(const Midi *m);                                   /* seconds up to the last event */
double midi_position(const Midi *m);

typedef struct { double t0, t1; uint8_t ch, key, vel; } MidiNote;     /* a note of the file: start, end (seconds), channel, key, velocity */
typedef struct { char title[96], copyright[96]; int format, ntracks, division, notes, tempo_bpm; } MidiInfo;
void  midi_info(const Midi *m, MidiInfo *out);

/* live state for the display: the 16 channels */
int   midi_chan_program(const Midi *m, int ch);                      /* 0..127 (channel 9: the drum kit number) */
int   midi_chan_bank(const Midi *m, int ch);
int   midi_chan_is_drum(int ch);
float midi_chan_level(const Midi *m, int ch);                        /* 0..1, rises with the velocity of a note, falls off */
int   midi_chan_volume(const Midi *m, int ch);                       /* CC 7 */
int   midi_chan_pan(const Midi *m, int ch);                          /* CC 10: 0..127 */
int   midi_chan_notes(const Midi *m, int ch);                        /* notes in the file on this channel (0 = unused channel) */
void  midi_set_mute(Midi *m, unsigned mask);
/* time grid (4/4 measures of division * 4 ticks, sixteen rows each: the same grid the tracker views use) and the notes for the roll */
const char *midi_gm_name(int program, int drum);                      /* "Acoustic Grand Piano", ... ("Drum Kit" for the drum channel) */
int    midi_measures(const Midi *m);
int    midi_division(const Midi *m);                                 /* ticks per quarter note (SMPTE files: 480) */
double midi_tick(const Midi *m);                                     /* the playing position in ticks */
double midi_sec_at_tick(const Midi *m, double tick);
int    midi_notes_in(const Midi *m, double t0, double t1, MidiNote *out, int max);   /* notes that sound at some time in [t0, t1], in the order of their start */                         /* bit n = channel n */
