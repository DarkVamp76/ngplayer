/* songrender: any module that song.c opens (MOD, S3M, XM, IT, ... all formats, by content) -> 16 bit stereo WAV at 44100 Hz, for tests against libopenmpt.
 * usage: songrender in out.wav [max_seconds]      prints one line: path | title | format | channels | seconds (ended = the song said it was over) | peak | rms */
#include "song.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
extern int xplay_solo;
int main(int argc, char **argv) {
    if (getenv("XSOLO")) xplay_solo = atoi(getenv("XSOLO"));          /* test hook: render only this channel (also for ITSOLO-less formats such as MED, MTM) */
    if (argc < 3) { fprintf(stderr, "usage: %s in out.wav [max_seconds]\n", argv[0]); return 2; }
    const int rate = 44100; int maxsec = argc > 3 ? atoi(argv[3]) : 900;
    char err[200] = "";
    Song *s = song_open(argv[1], rate, err, sizeof err);
    if (!s) { printf("%s | LOAD FAILED | %s\n", argv[1], err); return 1; }
    size_t cap = (size_t)rate * maxsec, tot = 0; int16_t *pcm = malloc(cap * 4); int ended = 0;
    if (!pcm) return 1;
    while (tot < cap) {
        int want = 4096 < (int)(cap - tot) ? 4096 : (int)(cap - tot);
        int k = song_render(s, pcm + tot * 2, want);
        if (k <= 0) { ended = 1; break; }
        tot += (size_t)k;
    }
    int pk = 0; double sq = 0;
    for (size_t i = 0; i < tot * 2; i++) { int a = abs(pcm[i]); if (a > pk) pk = a; sq += (double)pcm[i] * pcm[i]; }
    FILE *o = fopen(argv[2], "wb"); if (!o) return 1;
    uint32_t dl = (uint32_t)(tot * 4), v32; uint16_t v16;
    fwrite("RIFF", 1, 4, o); v32 = 36 + dl; fwrite(&v32, 4, 1, o); fwrite("WAVEfmt ", 1, 8, o);
    v32 = 16; fwrite(&v32, 4, 1, o); v16 = 1; fwrite(&v16, 2, 1, o); v16 = 2; fwrite(&v16, 2, 1, o);
    v32 = rate; fwrite(&v32, 4, 1, o); v32 = rate * 4; fwrite(&v32, 4, 1, o); v16 = 4; fwrite(&v16, 2, 1, o); v16 = 16; fwrite(&v16, 2, 1, o);
    fwrite("data", 1, 4, o); fwrite(&dl, 4, 1, o); fwrite(pcm, 4, tot, o); fclose(o);
    printf("%s | %s | %s | %dch | %.2f s%s | peak %d | rms %.1f\n", argv[1], song_title(s), song_format(s), song_channels(s), (double)tot / rate, ended ? " (ended)" : " (max)", pk, tot ? sqrt(sq / (double)(tot * 2)) : 0.0);
    song_close(s); free(pcm);
    return 0;
}
