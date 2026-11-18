/* Adlib Tracker 2 replayer (A2M versions 9-14); the OPL chip is reached through a write callback. LGPL-2.1-or-later (derived from AdPlug's a2m-v2.cpp) */
#pragma once
#include "a2m_load.h"
typedef struct A2P A2P;
typedef void (*A2pWrite)(void *user, int chip, int reg, int val);      /* chip 0 = first register set (0x000-0x0FF), 1 = second (0x100-0x1FF) */
A2P *a2p_new(const A2mSong *s, A2pWrite write, void *user);
void a2p_free(A2P *p);
void a2p_rewind(A2P *p);
int a2p_update(A2P *p);                       /* one timer tick; 0 when the song has ended */
float a2p_refresh(const A2P *p);              /* ticks per second at the moment */
int a2p_opl_channel(const A2P *p, int chan);   /* the OPL3 channel (0-17) an AT2 channel plays on, -1 if none */
int a2p_position(const A2P *p, int *order, int *pattern, int *row, int *speed);
