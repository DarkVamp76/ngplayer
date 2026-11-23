/* SPDX-License-Identifier: GPL-3.0-or-later */
/* MIDI output to real hardware (a Roland MT-32, a Sound Canvas, any synth on a MIDI interface) through the ALSA sequencer. Linux only (NG_HWMIDI). */
#pragma once
#include <stddef.h>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(NG_NO_HWMIDI)
#define NG_HWMIDI 1
#else
#define NG_HWMIDI 0
#endif

typedef struct MidiHw MidiHw;

MidiHw *midi_hw_open(const char *want);                 /* want: NULL / "" = the first hardware port, else a part of the client or port name ("UM-ONE") or "client:port" (24:0); NULL if none */
void    midi_hw_close(MidiHw *h);
const char *midi_hw_name(const MidiHw *h);              /* the client name of the port that is in use */
double  midi_hw_time(const MidiHw *h);                  /* the time of the output queue in seconds (a real-time clock) */
void    midi_hw_send(MidiHw *h, const unsigned char *msg, int len, double at);      /* one MIDI message (or a whole SysEx F0 .. F7) at the queue time `at`; at < 0: at once */
void    midi_hw_drop(MidiHw *h);                        /* forget everything that is scheduled but not sent yet */
void    midi_hw_panic(MidiHw *h);                       /* all sound off + all notes off + reset controllers on the 16 channels, at once */
