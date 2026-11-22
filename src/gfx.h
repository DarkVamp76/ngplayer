/* SPDX-License-Identifier: GPL-3.0-or-later */
/* gfx.h - the interface between ngPlayer (main.c and the *.inc files: batched 2D renderer and the CPU side of the four 3D views) and the graphics API.
 *
 * The GPU side lives behind this header: one 2D shader (SDF shapes, font atlas, pictures, XTC ripples), the orb, the scope lines, the voxel field and the 3D
 * sound card, plus textures, render targets and meshes. The backends: gfx_gl.c (OpenGL 3.3 core on the desktop, OpenGL ES 3 in the browser and on iOS) and
 * gfx_metal.m (Metal on Apple). One backend is compiled in; there is no run time switch. */
#ifndef GFX_H
#define GFX_H

#include <SDL2/SDL.h>
#include <stdint.h>

/* one 2D vertex: position (view units), uv, colour, and 4 parameters (the mode m selects the look in the shader, p1..p3 are its parameters) */
typedef struct { float x, y, u, v, r, g, b, a, m, p1, p2, p3; } gfx_vtx;

typedef struct gfx_tex gfx_tex;                       /* a texture, also a render target (opaque) */
typedef struct gfx_mesh gfx_mesh;                     /* an indexed triangle mesh of the 3D card (opaque) */

/* ---- window and frame ---------------------------------------------------------------------------------------------------------------------------------- */
Uint32 gfx_window_flags(void);                        /* the SDL window flag of the backend (SDL_WINDOW_OPENGL or SDL_WINDOW_METAL) */
void   gfx_window_attrs(void);                        /* before the window is created: attributes of the context (OpenGL); nothing for Metal */
int    gfx_init(SDL_Window *win, int max_vtx);        /* after the window: context / device, shaders, vertex buffer. 0 = ok, else -1 (a message is printed) */
void   gfx_shutdown(void);
int    gfx_vsync(void);                               /* 1 = the swap waits for the display, 0 = the caller has to pace the frames */
void   gfx_drawable_size(SDL_Window *win, int *w, int *h);   /* size of the frame buffer in pixels */

/* a frame: clear the whole frame buffer (fw x fh) to r,g,b, then draw only in the part vx,vy,vw,vh (pixels, y counted from the BOTTOM like OpenGL) */
void   gfx_frame_begin(int fw, int fh, int vx, int vy, int vw, int vh, float r, float g, float b);
void   gfx_frame_end(SDL_Window *win);                /* show the frame */
void   gfx_read_pixels(int w, int h, unsigned char *rgba);   /* the current target (frame buffer or render target) as RGBA, bottom row first (like glReadPixels) */

/* ---- Apple TV (elsewhere: harmless defaults) ---------------------------------------------------------------------------------------------------------- */
void   gfx_native_size(SDL_Window *win, int *w, int *h);   /* the size of the screen in pixels, whatever the picture is rendered at */
int    gfx_refresh_hz(SDL_Window *win);                 /* refresh rate of the screen (Apple TV: 50, 60 ...); 60 where it is not known */
void   gfx_set_render_scale(float s);                  /* Apple TV: render at this fraction of the native size (1 = native); elsewhere without effect */
float  gfx_last_gpu_ms(void);                          /* how long the GPU needed for the last finished frame (Metal; 0 where it is not known) */

/* ---- 2D ------------------------------------------------------------------------------------------------------------------------------------------------ */
void   gfx_set_params(float res_w, float res_h, float pxs);  /* size of the view in view units, and pixels per view unit */
void   gfx_set_texture(gfx_tex *t);                   /* the texture of the following 2D draws (the caller flushes its batch first) */
void   gfx_set_additive(int on);                      /* blend mode of the following draws: 0 = alpha, 1 = additive (the caller flushes first) */
void   gfx_set_ripples(const float *src16x4, float t);  /* the 16 water sources (x, y, amplitude, phase) and the time of the ripple mode */
void   gfx_draw(const gfx_vtx *v, int n);             /* n vertices = n/3 triangles */

/* ---- textures and render targets ----------------------------------------------------------------------------------------------------------------------- */
enum { GFX_R8, GFX_RGBA8, GFX_RGB565 };               /* pixel formats (GFX_RGB565: 16 bit, as unsigned short) */
enum { GFX_MIPS = 1, GFX_TARGET = 2, GFX_DEPTH = 4, GFX_MSAA = 8 };  /* flags: full mip chain (made from px, or after every pass into a target), render target, depth buffer, multisampled (target + depth only: gfx_card_render draws with GFX_MSAA_SAMPLES samples and resolves into the texture; a device without that many samples draws as before) */
#define GFX_MSAA_SAMPLES 4                                    /* the sample count of a GFX_MSAA target (both backends) */
gfx_tex *gfx_tex_create(int w, int h, int fmt, const void *px, int flags);  /* linear filtering, clamped edges; px may be NULL for a target */
void   gfx_tex_free(gfx_tex *t);
/* 2D drawing into a target (the cover picture): clear to r,g,b,a, view = the whole target; gfx_target_end goes back to the window */
int    gfx_target_begin(gfx_tex *t, float r, float g, float b, float a);
void   gfx_target_end(gfx_tex *t);

/* ---- the four 3D views (each draws with its own shader; the caller flushes its 2D batch first) --------------------------------------------------------- */
/* orb: three vertex sets (faces, edges, nodes) of 11 floats each (own node xyz, other node xyz, third node xyz, small value x y); made once */
void   gfx_orb_init(const float *v[3], const int n[3]);
/* geo = cx, cy, R in view units, anim = spin, time, beat, bs = the 64 smoothed band levels, col = the three voice colours (9 floats) */
void   gfx_orb_draw(float cx, float cy, float R, float spin, float t, float beat, const float *bs, const float *col);
/* scopes: nt voices of 96 values in -1..1 (vals) and 8 floats of info each (x, mid, width, amp, level, colour index, points, visible); lw = line width */
void   gfx_scope_draw(const float *vals, const float *info, int nt, float lw, const float *col);
/* voxel field into an off-screen picture (w x h, with depth) that is returned; cam, geo (z0, segments, cube, seam), the 64 levels, five colours (15 floats: c0 c1 c2 fog bg0) */
gfx_tex *gfx_voxel_render(int w, int h, const float *cam, const float *geo, const float *lv, const float *col);
/* the 3D card: vertices xyz, uv, rgba8 (24 bytes each) with 16 bit indices */
gfx_mesh *gfx_mesh_create(const void *verts, int nverts, const uint16_t *idx, int nidx);
/* renders into the target (cleared to transparent, depth tested): the board (colours), the front picture and the back of the board */
void   gfx_card_render(gfx_tex *target, const float *mvp, const float *mv, gfx_mesh *board, gfx_mesh *front, gfx_tex *front_tex, gfx_mesh *back, gfx_tex *back_tex);

#endif
