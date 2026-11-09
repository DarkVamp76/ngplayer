/* Konsolen-Test fuer den TFMX-Replayer: tfmxtest <songdatei> <sekunden> <out.wav> [subsong] */
#include "../src/core/tfmx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned long long xplay_mute = 0;
static uint8_t *slurp(const char *f, size_t *n) {
    FILE *fp = fopen(f, "rb"); if (!fp) return NULL;
    fseek(fp, 0, SEEK_END); long l = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)l + 1); *n = fread(b, 1, (size_t)l, fp); fclose(fp); return b;
}
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "tfmxtest <song> <sek> <out.wav> [sub]\n"); return 1; }
    char sm[1100]; int pr = tfmx_partner(argv[1], sm, sizeof sm);
    if (pr) { fprintf(stderr, "Partnerdatei: %s\n", pr > 0 ? "fehlt" : "kein TFMX-Songname"); return 2; }
    size_t nm, ns; uint8_t *m = slurp(argv[1], &nm), *s = slurp(sm, &ns);
    if (!m || !s) { fprintf(stderr, "Lesefehler\n"); return 2; }
    int err; TfmxSong *t = tfmx_load(m, nm, s, ns, &err);
    if (!t) { fprintf(stderr, "Ladefehler %d\n", err); return 3; }
    int sub = argc > 4 ? atoi(argv[4]) : 0;
    printf("Sample: %s | Subsongs: %d | Zeile0: %s\n", sm, tfmx_subsong_count(t), t->text[0]);
    TfmxPlayer *p = tfp_new(t, sub, 44100);
    int n = (int)(atof(argv[2]) * 44100); int16_t *buf = malloc((size_t)n * 4);
    int got = tfp_render(p, buf, n);
    FILE *o = fopen(argv[3], "wb");
    uint32_t dl = (uint32_t)got * 4, v;
    fwrite("RIFF", 1, 4, o); v = 36 + dl; fwrite(&v, 4, 1, o); fwrite("WAVEfmt ", 1, 8, o);
    v = 16; fwrite(&v, 4, 1, o); uint16_t u = 1; fwrite(&u, 2, 1, o); u = 2; fwrite(&u, 2, 1, o);
    v = 44100; fwrite(&v, 4, 1, o); v = 44100 * 4; fwrite(&v, 4, 1, o); u = 4; fwrite(&u, 2, 1, o); u = 16; fwrite(&u, 2, 1, o);
    fwrite("data", 1, 4, o); fwrite(&dl, 4, 1, o); fwrite(buf, 4, (size_t)got, o); fclose(o);
    printf("%d Frames (%.1f s) geschrieben, Trackstep-Zeile %d\n", got, got / 44100.0, tfp_pos(p));
    return 0;
}
