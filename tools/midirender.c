/* midirender: MIDI -> WAV through the SoundFont (test aid, no SDL). GPL-3.0-or-later
 * usage: midirender in.mid out.wav [max_seconds] [soundfont.sf2] */
#include "midi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s in.mid out.wav [max_seconds] [soundfont.sf2]\n", argv[0]); return 2; }
    int maxsec = argc > 3 ? atoi(argv[3]) : 600, rate = 44100;
    if (argc > 4) midi_set_soundfont(argv[4]);
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 1; fclose(f);
    char err[200] = "";
    Midi *m = midi_open(b, (size_t)n, rate, err, sizeof err);
    if (!m) { fprintf(stderr, "%s: %s\n", argv[1], err); return 1; }
    MidiInfo mi; midi_info(m, &mi);
    size_t cap = (size_t)rate * maxsec, tot = 0; int16_t *pcm = malloc(cap * 4);
    while (tot < cap) { int k = midi_render(m, pcm + tot * 2, 4096 < (int)(cap - tot) ? 4096 : (int)(cap - tot)); if (k <= 0) break; tot += k; }
    int pk = 0; for (size_t i = 0; i < tot * 2; i++) { int a = abs(pcm[i]); if (a > pk) pk = a; }
    FILE *o = fopen(argv[2], "wb"); uint32_t dl = tot * 4, v32; uint16_t v16;
    fwrite("RIFF", 1, 4, o); v32 = 36 + dl; fwrite(&v32, 4, 1, o); fwrite("WAVEfmt ", 1, 8, o);
    v32 = 16; fwrite(&v32, 4, 1, o); v16 = 1; fwrite(&v16, 2, 1, o); v16 = 2; fwrite(&v16, 2, 1, o);
    v32 = rate; fwrite(&v32, 4, 1, o); v32 = rate * 4; fwrite(&v32, 4, 1, o); v16 = 4; fwrite(&v16, 2, 1, o); v16 = 16; fwrite(&v16, 2, 1, o);
    fwrite("data", 1, 4, o); fwrite(&dl, 4, 1, o); fwrite(pcm, 4, tot, o); fclose(o);
    printf("%s | \"%s\" fmt %d, %d tracks, div %d, %d notes, %d bpm | file %.1f s, played %.1f s | peak %d (%.0f%%) | %s\n", argv[1], mi.title, mi.format, mi.ntracks, mi.division, mi.notes, mi.tempo_bpm,
           midi_length(m), (double)tot / rate, pk, pk * 100.0 / 32767, midi_soundfont_path());
    midi_close(m); return 0;
}
