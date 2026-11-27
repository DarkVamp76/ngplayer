/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Standard MIDI files (.mid, .midi, .kar, .rmi) played through a SoundFont (TinySoundFont).
 * Linux, macOS and the web player; the iOS / tvOS apps have no MIDI (NG_MIDI = 0). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "midi_hw.h"

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
typedef struct { char title[96], copyright[96], standard[12], lcd[24], syx[48]; int syx_msgs, format, ntracks, division, notes, tempo_bpm, standard_known; int mt_reverb[3]; } MidiInfo;       /* standard: "MT-32", "GS", "XG", "GM2" or "GM" (from the SysEx messages of the file; standard_known = 0: none named, played as GM); lcd: the display text a MT-32 / SC-55 file sends */
void  midi_info(const Midi *m, MidiInfo *out);
void  midi_hint_mt32(Midi *m);
void  midi_add_syx(Midi *m, const char *name, const uint8_t *data, size_t len);       /* the .syx file next to the MIDI file (read first; the standard and the display text come from it) */                                       /* the file name says MT-32 (.m32) */

/* live state for the display: the 16 channels */
typedef struct { int expr, mod, reverb, chorus, sustain; float bend; } MidiChanFx;       /* CC 11, CC 1, CC 91, CC 93 (0..127), the sustain pedal, the pitch wheel (-1..1) */
void  midi_chan_fx(const Midi *m, int ch, MidiChanFx *o);
int   midi_chan_program(const Midi *m, int ch);                      /* 0..127 (channel 9: the drum kit number) */
int   midi_chan_bank(const Midi *m, int ch);
int   midi_chan_is_drum(int ch);
float midi_chan_out(const Midi *m, int ch);
float midi_chan_level(const Midi *m, int ch);                        /* 0..1, rises with the velocity of a note, falls off */
int   midi_chan_volume(const Midi *m, int ch);                       /* CC 7 */
int   midi_chan_pan(const Midi *m, int ch);                          /* CC 10: 0..127 */
int   midi_chan_notes(const Midi *m, int ch);                        /* notes in the file on this channel (0 = unused channel) */
void  midi_set_mute(Midi *m, unsigned mask);
/* the output: the SoundFont (default) or real MIDI hardware through ALSA (Linux; NG_HWMIDI). Switch with the audio device locked. */
int   midi_set_hardware(int on, const char *want);                   /* on: connect to the hardware port `want` (NULL: the first one); returns 1 when the hardware is the output now */
int   midi_hardware_on(void);
int midi_activity(void);                                            /* 1 while the MIDI hardware gets a message (the lamp of the module) */
const char *midi_output_name(void);                                  /* "SoundFont" or the name of the MIDI device */
void  midi_idle(Midi *m);                                            /* the player does not render (pause, end): the hardware gets a panic and the next render starts again from here */
void  midi_set_scope(Midi *m, float *buf, int len);                  /* scope rings: buf[channel * len + position], len a power of two (the voices of each channel, dry) */
unsigned midi_scope_pos(const Midi *m);                              /* frames written so far (write position = pos & (len - 1)) */
/* time grid (4/4 measures of division * 4 ticks, sixteen rows each: the same grid the tracker views use) and the notes for the roll */
const char *midi_gm_name(int program, int drum);                      /* "Acoustic Grand Piano", ... ("Drum Kit" for the drum channel) */
int    midi_measures(const Midi *m);
int    midi_division(const Midi *m);                                 /* ticks per quarter note (SMPTE files: 480) */
double midi_tick(const Midi *m);                                     /* the playing position in ticks */
double midi_sec_at_tick(const Midi *m, double tick);
int    midi_notes_in(const Midi *m, double t0, double t1, MidiNote *out, int max);   /* notes that sound at some time in [t0, t1], in the order of their start */                         /* bit n = channel n */
