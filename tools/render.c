/* ngp_render: MOD/S3M -> WAV (Prototyp). GPL-3.0-or-later */
#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void ngp_it_tables_init(void);
extern int xplay_solo;
int main(int argc, char **argv){
    if (argc < 3) { fprintf(stderr, "usage: %s in out.wav [max_seconds] [fixe8]\n", argv[0]); return 2; }
    int maxsec = argc > 3 ? atoi(argv[3]) : 600, rate = 44100;
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 1; fclose(f);
    XSong s; int r = s669_probe(b, n) ? xs669_load(&s, b, n) : ult_probe(b, n) ? xult_load(&s, b, n) : ptm_probe(b, n) ? xptm_load(&s, b, n) : stm_probe(b, n) ? xstm_load(&s, b, n) : (n > 0x30 && !memcmp(b + 0x2C, "SCRM", 4)) ? xs3m_load(&s, b, n) : (n > 80 && !memcmp(b, "Extended Module: ", 17)) ? xxm_load(&s, b, n) : xmod_load(&s, b, n);
    if (r) { fprintf(stderr, "load failed (%d)\n", r); return 1; }
    ngp_it_tables_init();
    if (getenv("XSOLO")) xplay_solo = atoi(getenv("XSOLO"));
    XPlayer *p = xplay_new(&s, rate);
    if (argc > 4) xplay_set_fixe8(p, 1);
    size_t cap = (size_t)rate * maxsec, tot = 0; int16_t *pcm = malloc(cap * 4);
    while (tot < cap) { int k = xplay_render(p, pcm + tot*2, 4096 < (int)(cap-tot) ? 4096 : (int)(cap-tot)); if (k <= 0) break; tot += k; }
    int pk = 0; for (size_t i = 0; i < tot*2; i++) { int a = abs(pcm[i]); if (a > pk) pk = a; }
    FILE *o = fopen(argv[2], "wb"); uint32_t dl = tot*4, v32; uint16_t v16;
    fwrite("RIFF",1,4,o); v32 = 36+dl; fwrite(&v32,4,1,o); fwrite("WAVEfmt ",1,8,o);
    v32 = 16; fwrite(&v32,4,1,o); v16 = 1; fwrite(&v16,2,1,o); v16 = 2; fwrite(&v16,2,1,o);
    v32 = rate; fwrite(&v32,4,1,o); v32 = rate*4; fwrite(&v32,4,1,o); v16 = 4; fwrite(&v16,2,1,o); v16 = 16; fwrite(&v16,2,1,o);
    fwrite("data",1,4,o); fwrite(&dl,4,1,o); fwrite(pcm,4,tot,o); fclose(o);
    printf("%s | \"%s\" %dch | %.1f s | peak %d (%.0f%%)\n", argv[1], s.title, s.nch, (double)tot/rate, pk, pk*100.0/32767);
    return 0;
}
