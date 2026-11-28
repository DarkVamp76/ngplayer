/* Musicline Editor replay: sound initialisation (MusiclineEditorWorker.InitializeSound), tick clock and a Paula-like mixer. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ml_replay.h"
#include "ml_play.h"

#define PAULA_PAL 3546895.0

void ml_on_end_reached(MlPlayer *p, int ch) { if (ch >= 0 && ch < 32) p->end_mask |= 1u << ch; }
void ml_set_bpm_tempo(MlPlayer *p, uint16_t tempo) { p->tempo = tempo; }
void ml_set_amiga_filter(MlPlayer *p, uint8_t on) { p->amiga_filter = on; }
/* IChannel.SetSample: like writing the Paula pointer / length registers without a DMA restart: the new data plays when the current pass ends */
void ml_channel_set_sample(MlChannel *c, const int8_t *data, uint32_t offset, uint32_t length) { c->loop_data = data + offset; c->loop_len = length; c->loop_set = 1; }

static void init_sound(MlPlayer *p, int sub) {
    MlSong *s = p->song;
    p->cur = &s->tune[sub];
    p->number_of_channels = s->mixer_mode ? 8 : 4;
    if (p->number_of_channels > p->cur->channels) p->number_of_channels = p->cur->channels;
    p->end_mask = 0; p->restart = 0;
    for (int i = p->number_of_channels; i < (s->mixer_mode ? 8 : 4); i++) ml_on_end_reached(p, i);       /* channels the tune does not use count as ended */
    memset(&p->playing_info, 0, sizeof p->playing_info);
    p->playing_info.tune_tempo = p->cur->tempo; p->playing_info.tune_speed = p->cur->speed; p->playing_info.tune_groove = p->cur->groove;
    p->playing_info.master_volume = 64 * 16;
    memset(p->voices, 0, sizeof p->voices); memset(p->ch, 0, sizeof p->ch);
    for (int i = 0; i < p->number_of_channels; i++) {
        MlVoice *v = &p->voices[i];
        v->channel_number = i;
        v->speed = p->playing_info.tune_speed; v->groove = p->playing_info.tune_groove;
        v->speed_counter = 1;
        v->part_groove = p->playing_info.tune_groove != 0;
        v->tune_jump_position = -1;
        v->channel_volume = 64 * 16;
        v->pitch_slide_to_note = -1;
    }
    p->playing_enabled = 1;
    ml_set_bpm_tempo(p, p->cur->tempo);
}

MlRender *ml_render_new(MlSong *s, int sub, int rate) {
    if (!s || sub < 0 || sub >= s->ntunes || s->tune[sub].channels == 0) return NULL;
    MlRender *r = (MlRender *)calloc(1, sizeof *r); if (!r) return NULL;
    r->rate = rate; r->sub = sub; r->pl.song = s;
    { double w0 = 6.283185307179586 * 3275.0 / rate, al = sin(w0) / (2.0 * 0.7071067811865476), co = cos(w0), a0 = 1.0 + al;
      r->lb0 = (1.0 - co) / 2.0 / a0; r->lb1 = (1.0 - co) / a0; r->lb2 = r->lb0; r->la1 = -2.0 * co / a0; r->la2 = (1.0 - al) / a0; }
    init_sound(&r->pl, sub);
    return r;
}

void ml_render_free(MlRender *r) { free(r); }

int ml_seq_length(const MlTune *t, int ch) {
    if (!t || ch < 0 || ch >= t->channels || !t->seq[ch]) return 1;
    for (int i = 0; i < 256; i++) { uint16_t d = t->seq[ch][i]; int cmd = (d & 0xC0) >> 6; if ((d & 0x20) && (cmd == 1 || cmd == 2)) return i + 1; }
    return 256;
}

static void tick(MlRender *r) {
    MlPlayer *p = &r->pl;
    ml_play_module(p);
    for (int i = 0; i < p->number_of_channels; i++) {                 /* the mixer takes over what DmaPlay / the effects wrote */
        MlChannel *c = &p->ch[i];
        if (c->trigger) { r->m[i].d = c->data; r->m[i].len = c->len; r->m[i].pos = 0; r->m[i].on = c->len > 0; r->m[i].ld = NULL; r->m[i].llen = 0; c->trigger = 0; }   /* a new sample starts without a loop; SetLoop of the same tick (below) sets the new one */
        if (c->loop_set) { r->m[i].ld = c->loop_data; r->m[i].llen = c->loop_len; c->loop_set = 0; }
        if (!c->trigger && r->m[i].d == NULL && c->data) { r->m[i].d = c->data; r->m[i].len = c->len; }
    }
    /* all voices off: the song starts again (Play() -> RestartSong) */
    int all = 1; for (int i = 0; i < p->number_of_channels; i++) if (!p->voices[i].voice_off) all = 0;
    if (all) { r->ended = 1; }
}

int ml_render(MlRender *r, int16_t *out, int n) {
    MlPlayer *p = &r->pl;
    if (r->ended) return 0;
    for (int f = 0; f < n; f++) {
        if (r->tick_left <= 0) {
            tick(r);
            if (r->ended) { memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(n - f)); return f; }
            double hz = (p->tempo ? p->tempo : 125) * 709378.9 / 1789773.0;      /* the CIA timer value is 1789773 / tempo (not the 1773447 of ProTracker): 0.9 % slower than tempo * 0.4 Hz */
            if (p->song->mixer_mode) {                                /* 8 voices: one tick = one mix buffer of ((3579545 / 126) * 125) / (tempo * 50) bytes (made even) played at period 126 */
                unsigned t = p->tempo ? p->tempo : 125, q1 = 3579545u / 126u, q2 = (q1 * 125u) / (t * 50u);
                q2 &= 0xFFFFu; q2 &= ~1u;
                if (q2) hz = (3546895.0 / 126.0) / (double)q2;
            }
            r->tick_left += r->rate / hz;
        }
        r->tick_left -= 1.0;
        if (r->fast) {                                            /* seeking: only the time runs on */
            int k = (int)r->tick_left; if (k > n - f - 1) k = n - f - 1; if (k < 0) k = 0;
            memset(out + 2 * f, 0, sizeof(int16_t) * 2 * (size_t)(k + 1)); r->tick_left -= k; f += k; continue;
        }
        float l = 0, rr = 0;
        for (int i = 0; i < p->number_of_channels; i++) {
            MlChannel *c = &p->ch[i];
            if (r->sc) r->sc[(size_t)i * r->sc_len + (r->sc_pos & (unsigned)(r->sc_len - 1))] = 0;
            if (!r->m[i].on || !r->m[i].d || (r->mute >> i & 1)) continue;
            double pos = r->m[i].pos;
            uint32_t ip = (uint32_t)pos;
            if (ip >= r->m[i].len) ip = r->m[i].len - 1;
            int s0 = r->m[i].d[ip], s1;                                   /* linear interpolation: the next value is in the sample, at the loop start, or 0 */
            if (ip + 1 < r->m[i].len) s1 = r->m[i].d[ip + 1];
            else s1 = (r->m[i].llen > 0 && r->m[i].ld) ? r->m[i].ld[0] : 0;
            float fr = (float)(pos - (double)(uint32_t)pos);
            float smp = (float)s0 + (float)(s1 - s0) * fr;
            float v = smp * (float)c->volume * (10.0f / 7.0f);            /* the level of uade (measured: 0.70 over all files) */
            if (p->song->mixer_mode) v *= 0.5f;                            /* 8 voices: two voices share a Paula channel, each through the volume table at value * vol / 128 */
            if (r->sc) r->sc[(size_t)i * r->sc_len + (r->sc_pos & (unsigned)(r->sc_len - 1))] = smp * (float)c->volume / (127.0f * 64.0f);
            int right = (i & 3) == 1 || (i & 3) == 2;                     /* Amiga: 0 3 left, 1 2 right */
            if (right) rr += v; else l += v;
            double per = c->period ? c->period : 428;
            pos += PAULA_PAL / (per * r->rate);
            while (pos >= r->m[i].len && r->m[i].on) {
                if (r->m[i].llen > 0) { pos -= r->m[i].len; r->m[i].d = r->m[i].ld; r->m[i].len = r->m[i].llen; }
                else { r->m[i].on = 0; break; }
            }
            r->m[i].pos = pos;
        }
        {   /* the LED filter when the module switches it on (part effect Filter); uade has no fixed RC low-pass here (measured: the spectrum matches best without) */
            double x[2] = { l, rr };
            for (int sd = 0; sd < 2; sd++) {
                if (p->amiga_filter) {
                    double *z = r->lz[sd], y = r->lb0 * x[sd] + r->lb1 * z[0] + r->lb2 * z[1] - r->la1 * z[2] - r->la2 * z[3];
                    z[1] = z[0]; z[0] = x[sd]; z[3] = z[2]; z[2] = y; x[sd] = y;
                } else { r->lz[sd][1] = r->lz[sd][0]; r->lz[sd][0] = x[sd]; r->lz[sd][3] = r->lz[sd][2]; r->lz[sd][2] = x[sd]; }
            }
            l = (float)x[0]; rr = (float)x[1];
        }
        { float a = l, b = rr; l = 0.866f * a + 0.5f * b; rr = 0.5f * a + 0.866f * b; }        /* uade's default stereo: the channels bleed 30 degrees into each other */
        r->sc_pos++;
        out[2 * f] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2 * f + 1] = (int16_t)(rr > 32767 ? 32767 : rr < -32768 ? -32768 : rr);
    }
    r->frames += n;
    return n;
}
