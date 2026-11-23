/* SPDX-License-Identifier: GPL-3.0-or-later */
/* MIDI output through the ALSA sequencer: a port of ngPlayer is connected to a hardware port (or any writable port) and the messages are put on a real-time
 * queue with their time stamps, so that the notes keep their time however the audio blocks of the player come (the queue is the clock). */
#include "midi_hw.h"
#if NG_HWMIDI
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MidiHw {
    snd_seq_t *seq;
    snd_midi_event_t *enc;
    int port, queue, dcl, dpt;
    char name[64];
};

static int find_dest(snd_seq_t *seq, const char *want, int *cl, int *pt, char *name, size_t cap) {
    snd_seq_client_info_t *ci; snd_seq_port_info_t *pi;
    snd_seq_client_info_alloca(&ci); snd_seq_port_info_alloca(&pi);
    int self = snd_seq_client_id(seq), wc = -1, wp = -1;
    if (want && sscanf(want, "%d:%d", &wc, &wp) != 2) wc = wp = -1;
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        int c = snd_seq_client_info_get_client(ci);
        const char *cname = snd_seq_client_info_get_name(ci);
        if (c == 0 || c == self || strcasestr(cname, "Midi Through")) continue;      /* System, ourselves and the loop-back */
        int kernel = snd_seq_client_info_get_type(ci) == SND_SEQ_KERNEL_CLIENT;
        snd_seq_port_info_set_client(pi, c); snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pi);
            if ((caps & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) != (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) continue;
            if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) continue;
            int p = snd_seq_port_info_get_port(pi); const char *pname = snd_seq_port_info_get_name(pi);
            if (wc >= 0) { if (c != wc || p != wp) continue; }
            else if (want && *want) { if (!strcasestr(cname, want) && !strcasestr(pname, want)) continue; }
            else if (!kernel) continue;                                              /* without a wish: a hardware port, not a software synth */
            *cl = c; *pt = p; snprintf(name, cap, "%s", cname);
            return 1;
        }
    }
    return 0;
}

MidiHw *midi_hw_open(const char *want) {
    snd_seq_t *seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) < 0) return NULL;
    snd_seq_set_client_name(seq, "ngPlayer");
    snd_seq_set_output_buffer_size(seq, 1 << 16);
    snd_seq_set_client_pool_output(seq, 8192);                                        /* a SysEx message takes a cell for every 12 bytes: the default 500 cells are gone after about 20 long messages */
    MidiHw *h = (MidiHw *)calloc(1, sizeof *h); h->seq = seq;
    if (!find_dest(seq, want, &h->dcl, &h->dpt, h->name, sizeof h->name)) { snd_seq_close(seq); free(h); return NULL; }
    h->port = snd_seq_create_simple_port(seq, "MIDI out", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    h->queue = snd_seq_alloc_queue(seq);
    if (h->port < 0 || h->queue < 0 || snd_seq_connect_to(seq, h->port, h->dcl, h->dpt) < 0 || snd_midi_event_new(8192, &h->enc) < 0) { snd_seq_close(seq); free(h); return NULL; }
    snd_midi_event_no_status(h->enc, 1);
    snd_seq_start_queue(seq, h->queue, NULL); snd_seq_drain_output(seq);
    return h;
}
void midi_hw_close(MidiHw *h) {
    if (!h) return;
    midi_hw_drop(h); midi_hw_panic(h);
    snd_midi_event_free(h->enc); snd_seq_close(h->seq); free(h);
}
const char *midi_hw_name(const MidiHw *h) { return h ? h->name : ""; }
double midi_hw_time(const MidiHw *h) {
    snd_seq_queue_status_t *st; snd_seq_queue_status_alloca(&st);
    if (snd_seq_get_queue_status(h->seq, h->queue, st) < 0) return 0;
    const snd_seq_real_time_t *t = snd_seq_queue_status_get_real_time(st);
    return (double)t->tv_sec + (double)t->tv_nsec * 1e-9;
}
void midi_hw_send(MidiHw *h, const unsigned char *msg, int len, double at) {
    snd_seq_event_t ev;
    static int logging = -1; if (logging < 0) logging = getenv("NGPLAYER_MIDI_LOG") != NULL;       /* NGPLAYER_MIDI_LOG=1: every message that goes out, on stderr (queue time, bytes) */
    if (logging) { fprintf(stderr, "%9.3f %s%3d:", at < 0 ? midi_hw_time(h) : at, at < 0 ? "now " : "    ", len); for (int i = 0; i < len && i < 24; i++) fprintf(stderr, " %02X", msg[i]); if (len > 24) fprintf(stderr, " ..."); fputc('\n', stderr); }
    snd_midi_event_reset_encode(h->enc);
    for (int i = 0; i < len; ) {
        snd_seq_ev_clear(&ev);
        long n = snd_midi_event_encode(h->enc, msg + i, len - i, &ev);
        if (n <= 0) break;
        i += (int)n;
        if (ev.type == SND_SEQ_EVENT_NONE) continue;
        snd_seq_ev_set_source(&ev, h->port); snd_seq_ev_set_subs(&ev);
        if (at < 0) snd_seq_ev_set_direct(&ev);
        else { snd_seq_real_time_t rt; rt.tv_sec = (unsigned)at; rt.tv_nsec = (unsigned)((at - (double)rt.tv_sec) * 1e9); snd_seq_ev_schedule_real(&ev, h->queue, 0, &rt); }
        int r = snd_seq_event_output(h->seq, &ev);
        if (r < 0 && logging) fprintf(stderr, "  event_output failed: %s\n", snd_strerror(r));
    }
    int r = snd_seq_drain_output(h->seq);
    if (r < 0 && logging) fprintf(stderr, "  drain failed: %s\n", snd_strerror(r));
}
void midi_hw_drop(MidiHw *h) {
    if (!h) return;
    snd_seq_drop_output(h->seq);
    snd_seq_remove_events_t *re; snd_seq_remove_events_alloca(&re);
    snd_seq_remove_events_set_queue(re, h->queue); snd_seq_remove_events_set_condition(re, SND_SEQ_REMOVE_OUTPUT);
    snd_seq_remove_events(h->seq, re);
}
void midi_hw_panic(MidiHw *h) {
    if (!h) return;
    for (int c = 0; c < 16; c++) {
        static const unsigned char ctl[3] = { 120, 123, 121 };                       /* all sound off, all notes off, reset all controllers */
        for (int k = 0; k < 3; k++) { unsigned char m[3] = { (unsigned char)(0xB0 | c), ctl[k], 0 }; midi_hw_send(h, m, 3, -1.0); }
    }
}
#else
const char *midi_hw_unused_;
#endif
