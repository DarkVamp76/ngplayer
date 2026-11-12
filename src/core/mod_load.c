/* MOD-Loader nach dem Verhalten von dem Referenz-Player (Loader 0x4181). GPL-3.0-or-later */
#include "core.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static int period_to_note(int per){
    if (!per) return 0;
    for (int i = 0; i < 84; i++)
        if (per >= ngp_pt_periods[i]) return i + 1 + 0x17;   /* Original: erstes i mit per >= T[i] */
    return 84 + 0x17;
}

int xmod_load(XSong *s, const uint8_t *b, size_t len){
    memset(s, 0, sizeof *s); s->fmt = 1; s->gvol = 255; for (int i = 0; i < 32; i++) { s->chpan[i] = -1; s->chen[i] = 1; }
    s->maxvol = 0x40; s->speed = 6; s->tempo = 0x7d;
    int nsm = 31, nch = 4, xchn = 0, ami = 0;
    if (len < 1084) nsm = 15;
    if (nsm == 31) {
        const uint8_t *g = b + 1080;
        if (!memcmp(g,"M.K.",4)||!memcmp(g,"M!K!",4)||!memcmp(g,"FLT4",4)) { nch = 4; ami = 1; }
        else if (g[0]>='1'&&g[0]<='9'&&!memcmp(g+1,"CHN",3)) { nch = g[0]-'0'; xchn = 1; }
        else if (g[0]>='0'&&g[0]<='9'&&g[1]>='0'&&g[1]<='9'&&!memcmp(g+2,"CH",2)) { nch = (g[0]-'0')*10+(g[1]-'0'); xchn = 1; }
        else if (!memcmp(g,"FLT8",4)||!memcmp(g,"OCTA",4)||!memcmp(g,"CD81",4)) nch = 4;  /* Original: 4 Kanaele */
        else nsm = 15;   /* kein Kennwort -> altes 15-Sample-MOD */
    }
    if (nsm == 15) ami = 1;                  /* alte 15-Sample-MODs stammen vom Amiga */
    s->amiga = ami;
    if (nch < 1 || nch > 32) return -1;
    s->nch = nch; s->xchn = xchn; s->nsmp = nsm;
    memcpy(s->title, b, 20); s->title[20] = 0;
    const uint8_t *p = b + 20;
    for (int i = 1; i <= nsm; i++, p += 30) {
        XSmp *m = &s->smp[i];
        uint32_t l = ((p[22]<<8)|p[23]) * 2u;
        int ft = p[24] & 0xf;
        m->vol = p[25]; if (m->vol > 64) m->vol = 64;
        uint32_t ls = ((p[26]<<8)|p[27]) * 2u, ll = ((p[28]<<8)|p[29]);
        m->len = l; m->ls = ls; m->le = ls + ll*2u;
        if (ll > 1) m->loop = 1;
        if (m->le > m->len) m->le = m->len;
        m->c2spd = ngp_finetune_c2spd[ft];
    }
    s->nord = p[0]; if (s->nord < 1 || s->nord > 128) s->nord = 128;
    memcpy(s->orders, p + 2, 128);
    int maxp = 0;
    for (int i = 0; i < 128; i++) if (s->orders[i] > maxp) maxp = s->orders[i];
    s->npat = maxp + 1;
    size_t patoff = (nsm == 31) ? 1084 : 600;
    size_t patsz = (size_t)s->npat * 64 * nch * 4;
    if (nsm == 31) {                         /* Amiga nur bei sicherem Hinweis: "st-"-Samplenamen (Soundtracker-Bibliothek); "M.K." allein sagt nichts (schreiben auch PC-Tracker) */
        s->amiga = 0;
        for (int i = 0; i < 31; i++) if ((b[20 + i * 30] | 0x20) == 's' && (b[21 + i * 30] | 0x20) == 't' && b[22 + i * 30] == '-') s->amiga = 1;
    }
    s->pat = calloc((size_t)s->npat * 64 * nch, sizeof(XCell));
    if (!s->pat) return -1;
    for (int pt = 0; pt < s->npat; pt++)
        for (int r = 0; r < 64; r++)
            for (int c = 0; c < nch; c++) {
                size_t o = patoff + (((size_t)pt*64 + r)*nch + c)*4;
                if (o + 4 > len) continue;
                const uint8_t *q = b + o;
                XCell *x = &s->pat[((size_t)pt*64 + r)*nch + c];
                int per = ((q[0]&0xf)<<8)|q[1];
                x->note = period_to_note(per);
                x->inst = (q[0]&0xf0) | (q[2]>>4);
                x->eff = q[2] & 0xf; x->par = q[3];
                if (xchn && x->eff == 8) { x->eff = 0x0e; x->par = 0x80 | (q[3] >> 4); }
            }
    size_t off = patoff + patsz;
    for (int i = 1; i <= nsm; i++) {
        XSmp *m = &s->smp[i];
        if (!m->len) continue;
        m->data = calloc(m->len + 2, 1);
        if (!m->data) return -1;
        size_t n = m->len;
        if (off >= len) n = 0; else if (off + n > len) n = len - off;
        memcpy(m->data, b + off, n);
        off += m->len;
    }
    return 0;
}
void xsong_free(XSong *s){
    for (int i = 0; i < 100; i++) { free(s->smp[i].data); if (s->smp[i].syn) { for (int k = 0; k < s->smp[i].syn->nwf; k++) free(s->smp[i].syn->wf[k]); free(s->smp[i].syn); } }
    for (int i = 0; i < s->nxsmp; i++) free(s->xsmp[i].data);
    free(s->xsmp); free(s->inst); free(s->prows);
    free(s->pat); memset(s, 0, sizeof *s);
}
