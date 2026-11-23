/* SPDX-License-Identifier: GPL-3.0-or-later */
/* gfx_metal.m - Metal backend of gfx.h (Apple: iOS now, macOS later). The shaders are the ones of gfx_gl.c in the Metal shading language, compiled at run time
 * from the source below (so no Metal toolchain is needed to build the app). The window is created with SDL_WINDOW_METAL; SDL gives the CAMetalLayer.
 *
 * Differences against OpenGL that the code below takes care of:
 *  - a frame is one command buffer; every off-screen pass (3D card, voxel field, cover) is its own render encoder in it, the window pass is ended and
 *    resumed (load action Load) around the passes that happen in the middle of a frame (voxel)
 *  - Metal's clip space depth is 0..1 (OpenGL: -1..1) and its texture rows run from the top: the off-screen passes flip y so that the pictures lie in the
 *    texture exactly as the OpenGL ones do (the 2D renderer samples them with the same uv)
 *  - no scalar-per-call uniforms: small structs are passed with setVertexBytes / setFragmentBytes */
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "gfx.h"
#include <SDL2/SDL_metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <TargetConditionals.h>

static NSString *const MSL =
    @"#include <metal_stdlib>\n"
    "using namespace metal;\n"
    /* ---------------------------------------------------------------- 2D */
    "struct VIn { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; float4 par [[attribute(3)]]; };\n"
    "struct VOut { float4 position [[position]]; float2 uv; float4 col; float4 par; };\n"
    "struct P2 { float2 res; float pxs; float rt; float4 rip[16]; };\n"
    "vertex VOut vs2d(VIn in [[stage_in]], constant P2 &p [[buffer(1)]]) {\n"
    "  VOut o; o.uv = in.uv; o.col = in.col; o.par = in.par;\n"
    "  o.position = float4(in.pos.x / p.res.x * 2.0 - 1.0, 1.0 - in.pos.y / p.res.y * 2.0, 0.0, 1.0);\n"
    "  return o;\n"
    "}\n"
    "float sdbox(float2 p, float2 h, float r) { float2 q = abs(p) - h + float2(r); return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "fragment float4 fs2d(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant P2 &p [[buffer(1)]]) {\n"
    "  int m = int(in.par.x + 0.5);\n"
    "  float4 tx = tex.sample(smp, in.uv);\n"
    "  float4 o;\n"
    "  if (m == 0) { o = in.col; }\n"
    "  else if (m == 1) { o = float4(in.col.rgb, in.col.a * tx.r); }\n"
    "  else if (m == 2) { float d = sdbox(in.uv, in.par.yz, in.par.w); o = float4(in.col.rgb, in.col.a * clamp(0.5 - d * p.pxs, 0.0, 1.0)); }\n"
    "  else if (m == 3) { float t = abs(in.uv.y); o = float4(in.col.rgb, in.col.a * exp(-t * t * in.par.y)); }\n"
    "  else if (m == 7) {\n"
    "    float d0 = sdbox(in.uv, in.par.yz, in.par.w); float mk = clamp(0.5 - d0 * p.pxs, 0.0, 1.0);\n"
    "    float s = 0.0, e = 0.0;\n"
    "    for (int i = 0; i < 16; i++) { float4 r = p.rip[i]; if (r.z > 0.001) { float2 q = (in.uv - r.xy) * float2(1.0, 1.5); float d = length(q);\n"
    "      s += sin(d * 0.62 - p.rt * 4.0 + r.w) * exp(-d * 0.05) * r.z; e += exp(-d * d * 0.02) * r.z; } }\n"
    "    float v = clamp(0.5 + 0.55 * s, 0.0, 1.0);\n"
    "    float3 c = float3(0.015, 0.03, 0.24) + float3(0.08, 0.26, 0.70) * v * v + float3(0.25, 0.45, 0.55) * clamp(e * 0.30, 0.0, 1.0) * v;\n"
    "    float fy = floor(in.uv.y); float sl = 0.84 + 0.16 * step(1.0, fy - 2.0 * floor(fy * 0.5));\n"
    "    o = float4(c * sl * in.col.rgb, in.col.a * mk); }\n"
    "  else if (m == 8) { o = float4(tx.rgb * in.col.rgb, tx.a * in.col.a); }\n"
    "  else if (m == 5) { float d = sdbox(in.uv, in.par.yz, in.par.w); float a = clamp(1.0 - d / in.par.w, 0.0, 1.0); o = float4(in.col.rgb, in.col.a * a * a); }\n"
    "  else { o = float4(in.col.rgb, in.col.a * exp(-dot(in.uv, in.uv) / (2.0 * in.par.y * in.par.y))); }\n"
    "  return o;\n"
    "}\n"
    /* ---------------------------------------------------------------- orb */
    "struct ObIn { float3 ua [[attribute(0)]]; float3 ub [[attribute(1)]]; float3 uc [[attribute(2)]]; float2 mi [[attribute(3)]]; };\n"
    "struct ObOut { float4 position [[position]]; float point_size [[point_size]]; float4 vcol; float vt; float4 vpt; };\n"
    "struct ObP { float2 res; float2 pad; float4 geo; float4 anim; float4 c0; float4 c1; float4 c2; int umode; int pad2[3]; float bs[64]; };\n"
    "float3 ob_ramp(constant ObP &p, float e) { return e < 0.5 ? mix(p.c0.xyz, p.c1.xyz, e * 2.0) : mix(p.c1.xyz, p.c2.xyz, (e - 0.5) * 2.0); }\n"
    "void ob_node(constant ObP &p, float3 u, thread float &e, thread float &d, thread float2 &s) {\n"
    "  float lat = abs(u.y); float bf = lat * 63.0; int b0 = int(bf); if (b0 > 62) b0 = 62; float fr = bf - float(b0); float lon = atan2(u.z, u.x);\n"
    "  e = (p.bs[b0] * (1.0 - fr) + p.bs[b0 + 1] * fr) * (0.65 + 0.35 * sin(lon * 3.0 + p.anim.y * 1.3 + lat * 5.0)); e = min(e, 1.0);\n"
    "  float R = p.geo.z; float rad = R * (1.0 + 0.06 * p.anim.z + 0.36 * e); float3 q = u * rad;\n"
    "  float ca = cos(p.anim.x), sa = sin(p.anim.x), ct = cos(0.42), st = sin(0.42);\n"
    "  float rx = q.x * ca + q.z * sa, rz = -q.x * sa + q.z * ca; float ry = q.y * ct - rz * st, rz2 = q.y * st + rz * ct;\n"
    "  float sc = 8.0 * R / (8.0 * R + rz2); s = float2(p.geo.x + rx * sc, p.geo.y + ry * sc);\n"
    "  d = clamp(0.5 - 0.5 * rz2 / (1.5 * R), 0.0, 1.0);\n"
    "}\n"
    "vertex ObOut vs_orb(ObIn in [[stage_in]], constant ObP &p [[buffer(1)]]) {\n"
    "  ObOut o; o.point_size = 1.0; o.vt = 0.0; o.vpt = float4(0.0);\n"
    "  float ea, da, eb, db, ec, dc; float2 sa2, sb2, sc2; ob_node(p, in.ua, ea, da, sa2);\n"
    "  float2 pos;\n"
    "  if (p.umode == 0) {\n"
    "    ob_node(p, in.ub, eb, db, sb2); ob_node(p, in.uc, ec, dc, sc2);\n"
    "    pos = in.mi.x < 0.5 ? sa2 : (in.mi.x < 1.5 ? sb2 : sc2);\n"
    "    float e = (ea + eb + ec) / 3.0, d = (da + db + dc) / 3.0; float al = (0.012 + 0.30 * e * e) * (0.25 + 0.75 * d);\n"
    "    o.vcol = float4(mix(ob_ramp(p, e), float3(1.0), 0.25), al < 0.01 ? 0.0 : al);\n"
    "  } else if (p.umode == 1) {\n"
    "    ob_node(p, in.ub, eb, db, sb2); float e = 0.5 * (ea + eb), d = 0.5 * (da + db); float2 dir = normalize(sb2 - sa2 + float2(1e-5)); float2 pp = float2(-dir.y, dir.x);\n"
    "    float w = 0.55 + 0.5 * d; pos = sa2 + pp * in.mi.x * w * 2.2; o.vt = in.mi.x;\n"
    "    o.vcol = float4(mix(ob_ramp(p, e), float3(1.0), 0.45), (0.10 + 0.50 * d) * (0.55 + 0.9 * e));\n"
    "  } else {\n"
    "    pos = sa2; float e = ea, d = da; float rr = 0.9 + 1.5 * d + 1.6 * e; float sg = 3.0 + 5.0 * e; float ag = e > 0.12 ? 0.30 * e * (0.3 + 0.7 * d) : 0.0;\n"
    "    float ext = max(rr + 1.0, 3.2 * sg * (ag > 0.0 ? 1.0 : 0.0)); o.point_size = 2.0 * ext;\n"
    "    o.vcol = float4(mix(ob_ramp(p, e), float3(1.0), 0.55), 1.0); o.vpt = float4(rr, sg, 0.25 + 0.70 * d, ag); o.vt = ext;\n"
    "  }\n"
    "  o.position = float4(pos.x / p.res.x * 2.0 - 1.0, 1.0 - pos.y / p.res.y * 2.0, 0.0, 1.0);\n"
    "  return o;\n"
    "}\n"
    "struct ObFrag { float4 position [[position]]; float4 vcol; float vt; float4 vpt; };\n"
    "fragment float4 fs_orb(ObFrag in [[stage_in]], float2 pc [[point_coord]], constant ObP &p [[buffer(1)]]) {\n"
    "  if (p.umode == 2) { float2 q = (pc - float2(0.5)) * 2.0 * in.vt; float dd = length(q);\n"
    "    float core = clamp(0.5 - (dd - in.vpt.x), 0.0, 1.0) * in.vpt.z; float glow = in.vpt.w * exp(-dd * dd / (2.0 * in.vpt.y * in.vpt.y));\n"
    "    return float4(in.vcol.rgb, core + glow); }\n"
    "  else if (p.umode == 1) { return float4(in.vcol.rgb, in.vcol.a * exp(-in.vt * in.vt * 3.0)); }\n"
    "  return in.vcol;\n"
    "}\n"
    /* ---------------------------------------------------------------- scopes */
    "struct ScP { float2 res; float lw; int pass_; float4 c0; float4 c1; float4 c2; };\n"
    "struct ScOut { float4 position [[position]]; float4 vcol; float vt; float vk; };\n"
    "constant int SC_CORNER[6] = { 0, 1, 2, 2, 1, 3 };\n"
    "vertex ScOut vs_scope(uint vid [[vertex_id]], uint iid [[instance_id]], constant ScP &p [[buffer(0)]], texture2d<float> vals [[texture(0)]], texture2d<float> info [[texture(1)]]) {\n"
    "  ScOut o; int seg_n = 95; int id = int(iid); int tile = id / seg_n; int seg = id % seg_n;\n"
    "  float4 ta = info.read(uint2(tile, 0)), tb = info.read(uint2(tile, 1));\n"
    "  int np = int(tb.z + 0.5);\n"
    "  if (seg >= np - 1 || tb.w < 0.5) { o.position = float4(2.0, 2.0, 2.0, 1.0); o.vcol = float4(0.0); o.vt = 0.0; o.vk = 1.0; return o; }\n"
    "  float v0 = vals.read(uint2(seg, tile)).r, v1 = vals.read(uint2(seg + 1, tile)).r;\n"
    "  float2 p0 = float2(ta.x + ta.z * float(seg) / float(np - 1), ta.y - v0 * ta.w), p1 = float2(ta.x + ta.z * float(seg + 1) / float(np - 1), ta.y - v1 * ta.w);\n"
    "  float2 dir = normalize(p1 - p0 + float2(1e-5, 0.0)); float2 pp = float2(-dir.y, dir.x);\n"
    "  int q = SC_CORNER[vid]; float end = float(q / 2), side = (q % 2 == 0) ? -1.0 : 1.0;\n"
    "  float w = (p.pass_ == 0 ? 3.4 : 1.0) * p.lw; float2 base = end < 0.5 ? p0 : p1; float2 pos = base + pp * side * w; o.vt = side;\n"
    "  int ci = int(tb.y + 0.5); float3 lc = ci == 0 ? p.c0.xyz : (ci == 1 ? p.c1.xyz : p.c2.xyz); float lvl = tb.x;\n"
    "  if (p.pass_ == 0) { o.vcol = float4(lc, 0.20 * lvl); o.vk = 3.5; } else { o.vcol = float4(mix(lc, float3(1.0), 0.3), 0.35 + 0.65 * lvl); o.vk = 3.0; }\n"
    "  o.position = float4(pos.x / p.res.x * 2.0 - 1.0, 1.0 - pos.y / p.res.y * 2.0, 0.0, 1.0);\n"
    "  return o;\n"
    "}\n"
    "fragment float4 fs_scope(ScOut in [[stage_in]]) { return float4(in.vcol.rgb, in.vcol.a * exp(-in.vt * in.vt * in.vk)); }\n"
    /* ---------------------------------------------------------------- voxel */
    "struct VxP { float4 cam; float4 geo; float4 c0; float4 c1; float4 c2; float4 fogc; float4 bg0; float lv[64]; };\n"
    "struct VxOut { float4 position [[position]]; float3 vcol; };\n"
    "constant int VX_FC[24] = { 0,1,2,3, 4,5,6,7, 0,3,7,4, 1,2,6,5, 0,1,5,4, 3,2,6,7 };\n"
    "constant int VX_QI[6] = { 0,1,2,0,2,3 };\n"
    "constant float3 VX_CORN[8] = { float3(0,0,0), float3(1,0,0), float3(1,1,0), float3(0,1,0), float3(0,0,1), float3(1,0,1), float3(1,1,1), float3(0,1,1) };\n"
    "vertex VxOut vs_vx(uint vid [[vertex_id]], uint iid [[instance_id]], constant VxP &p [[buffer(0)]]) {\n"
    "  VxOut o; int id = int(iid); int k = id % 15; int b = (id / 15) % 64; int r = id / (15 * 64);\n"
    "  int n = int(p.lv[b] * 15.0 + 0.5);\n"
    "  if (k >= n) { o.position = float4(2.0, 2.0, 2.0, 1.0); o.vcol = float3(0.0); return o; }\n"
    "  int face = int(vid) / 6; float3 q = VX_CORN[VX_FC[face * 4 + VX_QI[int(vid) % 6]]];\n"
    "  float CUBE = p.geo.z, SEAM = p.geo.w, Z0 = p.geo.x;\n"
    "  float wx = (float(b) - 32.0) + 0.08 + q.x * CUBE, wy = float(k) + SEAM + q.y * (1.0 - 2.0 * SEAM), z = Z0 + float(r) + q.z * CUBE;\n"
    "  float zn = Z0 - 2.0, zf = Z0 + p.geo.y + 4.0;\n"
    "  float nx = wx / z * p.cam.x, ny = p.cam.z + (p.cam.w - wy) / z * p.cam.y;\n"
    "  float4 gp = float4(nx * z, ny * z, (zf + zn) / (zf - zn) * z - 2.0 * zf * zn / (zf - zn), z);\n"
    "  o.position = float4(gp.x, -gp.y, (gp.z + gp.w) * 0.5, gp.w);\n"          /* flipped y, depth 0..1 */
    "  float u = float(b) / 63.0; float3 lo = u < 0.5 ? mix(p.c0.xyz, p.c1.xyz, u * 2.0) : mix(p.c1.xyz, p.c2.xyz, (u - 0.5) * 2.0);\n"
    "  float fr = float(r) / p.geo.y; float fg = fr * fr * 0.85; float3 col;\n"
    "  if (face == 5) col = mix(mix(lo, float3(1.0), 0.35), p.fogc.xyz, fg);\n"
    "  else if (face == 0) col = mix(mix(lo, float3(1.0), 0.10), p.fogc.xyz, fg * 0.9);\n"
    "  else col = mix(mix(lo, p.bg0.xyz, 0.45), p.fogc.xyz, fg);\n"
    "  o.vcol = col;\n"
    "  return o;\n"
    "}\n"
    "fragment float4 fs_vx(VxOut in [[stage_in]]) { return float4(in.vcol, 1.0); }\n"
    /* ---------------------------------------------------------------- 3D card */
    "struct C3In { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };\n"
    "struct C3Out { float4 position [[position]]; float3 vv; float2 vuv; float4 vcol; };\n"
    "struct C3P { float4x4 mvp; float4x4 mv; int umode; int pad[3]; };\n"
    "vertex C3Out vs_c3(C3In in [[stage_in]], constant C3P &p [[buffer(1)]]) {\n"
    "  C3Out o; float4 q = float4(in.pos, 1.0); o.vv = (p.mv * q).xyz; o.vuv = in.uv; o.vcol = in.col;\n"
    "  float4 gp = p.mvp * q; o.position = float4(gp.x, -gp.y, (gp.z + gp.w) * 0.5, gp.w);\n"
    "  return o;\n"
    "}\n"
    "fragment float4 fs_c3(C3Out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant C3P &p [[buffer(1)]]) {\n"
    "  float3 n = normalize(cross(dfdx(in.vv), dfdy(in.vv))); if (dot(n, -in.vv) < 0.0) n = -n;\n"
    "  float d = max(dot(n, normalize(float3(0.33, 0.52, 0.79))), 0.0);\n"
    "  float3 tc = tex.sample(smp, in.vuv).rgb;\n"
    "  float3 base = p.umode == 1 ? tc : in.vcol.rgb;\n"
    "  float sh = p.umode == 1 ? 0.62 + 0.55 * d : 0.36 + 0.64 * d;\n"
    "  return float4(base * sh, 1.0);\n"
    "}\n";

struct gfx_tex { id<MTLTexture> tex; id<MTLTexture> depth; id<MTLTexture> msc, msd; int w, h, flags; };     /* msc / msd: the multisampled colour + depth of a GFX_MSAA target (resolved into tex) */
struct gfx_mesh { id<MTLBuffer> vb, ib; int n; };

#define INFLIGHT 3
enum { FMT_WIN = 0, FMT_RGBA = 1 };
static id<MTLDevice> dev;
static id<MTLCommandQueue> queue;
static CAMetalLayer *layer;
static id<MTLLibrary> lib;
static id<MTLSamplerState> sampler;
static id<MTLDepthStencilState> dss_less;
static id<MTLRenderPipelineState> pso2d[2][2];         /* [target format][0 = alpha blend, 1 = additive] */
static id<MTLRenderPipelineState> pso_orb[2], pso_scope[2], pso_vx, pso_c3, pso_c3ms;
static id<MTLBuffer> vbuf[INFLIGHT];
static size_t vcap, voff;                              /* capacity of one frame's buffer, bytes used so far */
static dispatch_semaphore_t inflight;
static int slot;
static id<MTLCommandBuffer> cmd;
static id<MTLRenderCommandEncoder> enc;
static id<CAMetalDrawable> drawable;
static gfx_tex *cur_tex, *cur_target;
static int additive, win_active;                       /* win_active: a frame is open (the window pass is current or paused for an off-screen pass) */
static struct { float res[2]; float pxs; float rt; float rip[64]; } params;
static int fb_w, fb_h, vp_x, vp_y, vp_w, vp_h;
static float clear_rgb[3];

Uint32 gfx_window_flags(void) { return SDL_WINDOW_METAL; }
void gfx_window_attrs(void) {}
int gfx_vsync(void) { return 1; }
#if TARGET_OS_TV
#include <SDL2/SDL_syswm.h>
static float render_scale = 1.0f;
void gfx_set_render_scale(float sc) { render_scale = sc < 0.25f ? 0.25f : (sc > 1.0f ? 1.0f : sc); }
int gfx_refresh_hz(SDL_Window *win) {
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_UIKIT && info.info.uikit.window) { NSInteger r = info.info.uikit.window.screen.maximumFramesPerSecond; if (r >= 24 && r <= 240) return (int)r; }
    return 60;
}
/* Apple TV: the window is 1920 x 1080 points whatever the output; at 4K the screen scale is 2. SDL reports the size of the layer, which this program sets itself every frame,
 * so the native size is computed from the window and the screen. */
void gfx_native_size(SDL_Window *win, int *w, int *h) {
    int pw = 0, ph = 0; SDL_GetWindowSize(win, &pw, &ph);
    float ns = 1;
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_UIKIT && info.info.uikit.window) ns = (float)info.info.uikit.window.screen.nativeScale;
    if (ns < 1) ns = 1;
    *w = (int)((float)pw * ns + 0.5f); *h = (int)((float)ph * ns + 0.5f);
}
void gfx_drawable_size(SDL_Window *win, int *w, int *h) {
    gfx_native_size(win, w, h);
    *w = ((int)((float)*w * render_scale + 0.5f)) & ~1; *h = ((int)((float)*h * render_scale + 0.5f)) & ~1;     /* even sizes */
}
#else
void gfx_drawable_size(SDL_Window *win, int *w, int *h) { SDL_Metal_GetDrawableSize(win, w, h); }
void gfx_native_size(SDL_Window *win, int *w, int *h) { SDL_Metal_GetDrawableSize(win, w, h); }
int gfx_refresh_hz(SDL_Window *win) { (void)win; return 60; }
void gfx_set_render_scale(float sc) { (void)sc; }
#endif
static volatile float last_gpu_ms;
float gfx_last_gpu_ms(void) { return last_gpu_ms; }

/* ---------------------------------------------------------------- pipelines */
/* blend: 0 none, 1 alpha, 2 additive */
static int mk_samples = 1;                                  /* the sample count of the pipelines made next (GFX_MSAA_SAMPLES for the multisampled card) */
static id<MTLRenderPipelineState> mkpso(NSString *vs, NSString *fs, MTLVertexDescriptor *vd, MTLPixelFormat fmt, int blend, int depth) {
    MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = [lib newFunctionWithName:vs];
    d.fragmentFunction = [lib newFunctionWithName:fs];
    d.vertexDescriptor = vd;
    d.rasterSampleCount = (NSUInteger)mk_samples;
    MTLRenderPipelineColorAttachmentDescriptor *c = d.colorAttachments[0];
    c.pixelFormat = fmt;
    if (blend) {                                       /* glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA or GL_ONE): the same for colour and alpha */
        c.blendingEnabled = YES;
        c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = blend == 2 ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
    }
    if (depth) d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    NSError *err = nil;
    id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p) fprintf(stderr, "Metal pipeline %s: %s\n", vs.UTF8String, err.localizedDescription.UTF8String);
    return p;
}
static MTLVertexDescriptor *vdesc(int n, const MTLVertexFormat *f, const int *off, int stride) {
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    for (int i = 0; i < n; i++) { vd.attributes[i].format = f[i]; vd.attributes[i].offset = (NSUInteger)off[i]; vd.attributes[i].bufferIndex = 0; }
    vd.layouts[0].stride = (NSUInteger)stride; vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return vd;
}

int gfx_init(SDL_Window *win, int max_vtx) {
    @autoreleasepool {
        SDL_MetalView view = SDL_Metal_CreateView(win);
        if (!view) { fprintf(stderr, "Metal view: %s\n", SDL_GetError()); return -1; }
        layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(view);
        dev = MTLCreateSystemDefaultDevice();
        if (!layer || !dev) { fprintf(stderr, "Metal: no device\n"); return -1; }
        layer.device = dev;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;                                    /* the screenshot reads the frame back */
        queue = [dev newCommandQueue];
        NSError *err = nil;
        lib = [dev newLibraryWithSource:MSL options:nil error:&err];
        if (!lib) { fprintf(stderr, "Metal shader: %s\n", err.localizedDescription.UTF8String); return -1; }
        const MTLPixelFormat fm[2] = { MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA8Unorm };
        {   const MTLVertexFormat f[4] = { MTLVertexFormatFloat2, MTLVertexFormatFloat2, MTLVertexFormatFloat4, MTLVertexFormatFloat4 }; const int o[4] = { 0, 8, 16, 32 };
            MTLVertexDescriptor *vd = vdesc(4, f, o, (int)sizeof(gfx_vtx));
            for (int t = 0; t < 2; t++) for (int a = 0; a < 2; a++) { pso2d[t][a] = mkpso(@"vs2d", @"fs2d", vd, fm[t], a ? 2 : 1, 0); if (!pso2d[t][a]) return -1; } }
        {   const MTLVertexFormat f[4] = { MTLVertexFormatFloat3, MTLVertexFormatFloat3, MTLVertexFormatFloat3, MTLVertexFormatFloat2 }; const int o[4] = { 0, 12, 24, 36 };
            MTLVertexDescriptor *vd = vdesc(4, f, o, 44);
            for (int a = 0; a < 2; a++) { pso_orb[a] = mkpso(@"vs_orb", @"fs_orb", vd, fm[0], a ? 2 : 1, 0); if (!pso_orb[a]) return -1; } }
        for (int a = 0; a < 2; a++) { pso_scope[a] = mkpso(@"vs_scope", @"fs_scope", nil, fm[0], a ? 2 : 1, 0); if (!pso_scope[a]) return -1; }
        pso_vx = mkpso(@"vs_vx", @"fs_vx", nil, fm[1], 0, 1);
        {   const MTLVertexFormat f[3] = { MTLVertexFormatFloat3, MTLVertexFormatFloat2, MTLVertexFormatUChar4Normalized }; const int o[3] = { 0, 12, 20 };
            pso_c3 = mkpso(@"vs_c3", @"fs_c3", vdesc(3, f, o, 24), fm[1], 0, 1);
            if ([dev supportsTextureSampleCount:GFX_MSAA_SAMPLES]) {                      /* the same card pipeline for the multisampled target */
                mk_samples = GFX_MSAA_SAMPLES; pso_c3ms = mkpso(@"vs_c3", @"fs_c3", vdesc(3, f, o, 24), fm[1], 0, 1); mk_samples = 1;
            } }
        if (!pso_vx || !pso_c3) return -1;
        MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; sd.mipFilter = MTLSamplerMipFilterLinear;
        sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler = [dev newSamplerStateWithDescriptor:sd];
        MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
        dd.depthCompareFunction = MTLCompareFunctionLess; dd.depthWriteEnabled = YES;
        dss_less = [dev newDepthStencilStateWithDescriptor:dd];
        vcap = (size_t)max_vtx * sizeof(gfx_vtx);                       /* one frame's vertices: the batches of a frame lie behind each other */
        for (int i = 0; i < INFLIGHT; i++) vbuf[i] = [dev newBufferWithLength:vcap options:MTLResourceStorageModeShared];
        inflight = dispatch_semaphore_create(INFLIGHT);
    }
    return 0;
}
void gfx_shutdown(void) { enc = nil; cmd = nil; drawable = nil; }

/* ---------------------------------------------------------------- command buffer and passes */
static void ensure_cmd(void) {
    if (cmd) return;
    dispatch_semaphore_wait(inflight, DISPATCH_TIME_FOREVER);
    slot = (slot + 1) % INFLIGHT; voff = 0;
    cmd = [queue commandBuffer];
}
static void end_enc(void) { if (enc) { [enc endEncoding]; enc = nil; } }
static void commit_cmd(int present) {
    if (!cmd) return;
    end_enc();
    if (present && drawable) [cmd presentDrawable:drawable];
    dispatch_semaphore_t s = inflight;
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> c) { if (present) last_gpu_ms = (float)((c.GPUEndTime - c.GPUStartTime) * 1000.0); dispatch_semaphore_signal(s); }];
    [cmd commit];
}
static void gen_mips(id<MTLTexture> t) {
    id<MTLBlitCommandEncoder> be = [cmd blitCommandEncoder];
    [be generateMipmapsForTexture:t];
    [be endEncoding];
}
/* the window pass: first with the clear colour, afterwards (after an off-screen pass in the middle of the frame) with Load */
static void window_pass(int clear) {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = drawable.texture;
    rp.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(clear_rgb[0], clear_rgb[1], clear_rgb[2], 1);
    enc = [cmd renderCommandEncoderWithDescriptor:rp];
    MTLViewport v = { (double)vp_x, (double)(fb_h - vp_y - vp_h), (double)vp_w, (double)vp_h, 0, 1 };   /* Metal counts y from the top, gfx.h from the bottom */
    [enc setViewport:v];
    [enc setFragmentSamplerState:sampler atIndex:0];
}
/* an off-screen pass into t (cleared to r,g,b,a; depth cleared when the target has one); the caller draws, then calls offscreen_end */
static void offscreen_begin(gfx_tex *t, float r, float g, float b, float a) {
    ensure_cmd();
    end_enc();
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    if (t->msc) {                                                 /* a GFX_MSAA target: draw into the multisampled texture, resolve into tex at the end of the pass */
        rp.colorAttachments[0].texture = t->msc;
        rp.colorAttachments[0].resolveTexture = t->tex;
        rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        rp.colorAttachments[0].texture = t->tex;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, a);
    if (t->depth || t->msd) {
        rp.depthAttachment.texture = t->msd ? t->msd : t->depth;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp.depthAttachment.clearDepth = 1.0;
    }
    enc = [cmd renderCommandEncoderWithDescriptor:rp];
    MTLViewport v = { 0, 0, (double)t->w, (double)t->h, 0, 1 };
    [enc setViewport:v];
    [enc setFragmentSamplerState:sampler atIndex:0];
}
static void offscreen_end(gfx_tex *t) {
    end_enc();
    if (cmd && (t->flags & GFX_MIPS)) gen_mips(t->tex);
    if (win_active && cmd && !cur_target) window_pass(0);       /* back to the window pass in the middle of the frame */
}

/* ---------------------------------------------------------------- textures */
gfx_tex *gfx_tex_create(int w, int h, int fmt, const void *px, int flags) {
    @autoreleasepool {
        MTLPixelFormat pf = fmt == GFX_R8 ? MTLPixelFormatR8Unorm : MTLPixelFormatRGBA8Unorm;
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:(NSUInteger)w height:(NSUInteger)h mipmapped:(flags & GFX_MIPS) ? YES : NO];
        td.usage = MTLTextureUsageShaderRead | ((flags & GFX_TARGET) ? MTLTextureUsageRenderTarget : 0);
        if (flags & GFX_TARGET) {
#if TARGET_OS_OSX
            td.storageMode = MTLStorageModeManaged;
#else
            td.storageMode = MTLStorageModeShared;                   /* the cover picture is read back */
#endif
        }
        gfx_tex *g = (gfx_tex *)calloc(1, sizeof *g);
        if (!g) return NULL;
        g->w = w; g->h = h; g->flags = flags;
        g->tex = [dev newTextureWithDescriptor:td];
        if (!g->tex) { free(g); return NULL; }
        if ((flags & GFX_MSAA) && (flags & GFX_TARGET) && (flags & GFX_DEPTH) && pso_c3ms) {      /* multisampled colour + depth (private: they never leave the GPU; the colour is resolved into tex) */
            MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
            cd.textureType = MTLTextureType2DMultisample; cd.sampleCount = GFX_MSAA_SAMPLES; cd.usage = MTLTextureUsageRenderTarget; cd.storageMode = MTLStorageModePrivate;
            MTLTextureDescriptor *md = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
            md.textureType = MTLTextureType2DMultisample; md.sampleCount = GFX_MSAA_SAMPLES; md.usage = MTLTextureUsageRenderTarget; md.storageMode = MTLStorageModePrivate;
            g->msc = [dev newTextureWithDescriptor:cd]; g->msd = [dev newTextureWithDescriptor:md];
            if (!g->msc || !g->msd) { g->msc = nil; g->msd = nil; }                      /* no multisampling: drawn as before */
        }
        if ((flags & GFX_DEPTH) && !g->msd) {
            MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
            dd.usage = MTLTextureUsageRenderTarget; dd.storageMode = MTLStorageModePrivate;
            g->depth = [dev newTextureWithDescriptor:dd];
        }
        if (px) {
            MTLRegion rg = MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h);
            if (fmt == GFX_RGB565) {                                 /* expanded to RGBA8: 565 is not available on every Mac */
                const uint16_t *s = (const uint16_t *)px;
                uint8_t *rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
                for (int i = 0; i < w * h; i++) {
                    unsigned v = s[i], r = (v >> 11) & 31, gr = (v >> 5) & 63, b = v & 31;
                    rgba[4 * i] = (uint8_t)((r << 3) | (r >> 2)); rgba[4 * i + 1] = (uint8_t)((gr << 2) | (gr >> 4)); rgba[4 * i + 2] = (uint8_t)((b << 3) | (b >> 2)); rgba[4 * i + 3] = 255;
                }
                [g->tex replaceRegion:rg mipmapLevel:0 withBytes:rgba bytesPerRow:(NSUInteger)w * 4];
                free(rgba);
            } else {
                [g->tex replaceRegion:rg mipmapLevel:0 withBytes:px bytesPerRow:(NSUInteger)w * (fmt == GFX_R8 ? 1u : 4u)];
            }
            if ((flags & GFX_MIPS) && g->tex.mipmapLevelCount > 1) {      /* a 1 x 1 texture has one level only: nothing to generate (the Metal API validation stops there) */
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
                [be generateMipmapsForTexture:g->tex];
                [be endEncoding];
                [cb commit]; [cb waitUntilCompleted];
            }
        }
        return g;
    }
}
void gfx_tex_free(gfx_tex *t) {
    if (!t) return;
    if (cur_tex == t) cur_tex = NULL;
    t->tex = nil; t->depth = nil; t->msc = nil; t->msd = nil;
    free(t);
}

/* ---------------------------------------------------------------- frame */
void gfx_frame_begin(int fw, int fh, int vx, int vy, int vw, int vh, float r, float g, float b) {
    ensure_cmd();
    end_enc();
    fb_w = fw; fb_h = fh; vp_x = vx; vp_y = vy; vp_w = vw; vp_h = vh;
    clear_rgb[0] = r; clear_rgb[1] = g; clear_rgb[2] = b;
    additive = 0; win_active = 0;
    @autoreleasepool {
        layer.drawableSize = CGSizeMake(fw, fh);
        drawable = [layer nextDrawable];
        if (!drawable) return;
        window_pass(1);
        win_active = 1;
    }
}
void gfx_frame_end(SDL_Window *win) {
    (void)win;
    if (win_active) { commit_cmd(1); cmd = nil; }
    drawable = nil; win_active = 0;
}
void gfx_read_pixels(int w, int h, unsigned char *rgba) {
    id<MTLTexture> tex = nil; int bgra = 0;
    if (cur_target) tex = cur_target->tex;
    else if (win_active && drawable) { tex = drawable.texture; bgra = 1; }
    if (!tex || !cmd) { memset(rgba, 0, (size_t)w * (size_t)h * 4); return; }
    id<MTLCommandBuffer> waitcmd = cmd;
#if TARGET_OS_OSX
    end_enc();
    { id<MTLBlitCommandEncoder> be = [cmd blitCommandEncoder]; [be synchronizeResource:tex]; [be endEncoding]; }
#endif
    commit_cmd(bgra);                                                  /* finish the work so far, wait, read the picture back */
    cmd = nil; if (bgra) { win_active = 0; }
    [waitcmd waitUntilCompleted];
    size_t row = (size_t)w * 4;
    unsigned char *px = (unsigned char *)malloc(row * (size_t)h);
    if (!px) return;
    [tex getBytes:px bytesPerRow:row fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h) mipmapLevel:0];
    for (int y = 0; y < h; y++) {                                      /* top row first -> bottom row first (like glReadPixels) */
        const unsigned char *src = px + (size_t)(h - 1 - y) * row;
        unsigned char *dst = rgba + (size_t)y * row;
        if (bgra) for (int x = 0; x < w; x++) { dst[x * 4 + 0] = src[x * 4 + 2]; dst[x * 4 + 1] = src[x * 4 + 1]; dst[x * 4 + 2] = src[x * 4 + 0]; dst[x * 4 + 3] = src[x * 4 + 3]; }
        else memcpy(dst, src, row);
    }
    free(px);
}

/* ---------------------------------------------------------------- 2D */
void gfx_set_params(float res_w, float res_h, float pxs) { params.res[0] = res_w; params.res[1] = res_h; params.pxs = pxs; }
void gfx_set_texture(gfx_tex *t) { cur_tex = t; }
void gfx_set_additive(int on) { additive = on; }
void gfx_set_ripples(const float *src, float t) { memcpy(params.rip, src, sizeof params.rip); params.rt = t; }
void gfx_draw(const gfx_vtx *v, int n) {
    if (!enc || !cmd || n <= 0) return;
    size_t bytes = (size_t)n * sizeof(gfx_vtx);
    if (voff + bytes > vcap) return;                                   /* more than a frame can hold: dropped (the buffer is sized for that) */
    memcpy((char *)vbuf[slot].contents + voff, v, bytes);
    [enc setRenderPipelineState:pso2d[cur_target ? FMT_RGBA : FMT_WIN][additive ? 1 : 0]];
    [enc setVertexBuffer:vbuf[slot] offset:voff atIndex:0];
    [enc setVertexBytes:&params length:sizeof params atIndex:1];
    [enc setFragmentBytes:&params length:sizeof params atIndex:1];
    if (cur_tex) [enc setFragmentTexture:cur_tex->tex atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:(NSUInteger)n];
    voff += bytes;
}

/* 2D drawing into a target (the cover picture) */
int gfx_target_begin(gfx_tex *t, float r, float g, float b, float a) {
    if (!t) return 0;
    offscreen_begin(t, r, g, b, a);
    cur_target = t;
    return 1;
}
void gfx_target_end(gfx_tex *t) {
    cur_target = NULL;
    if (t) offscreen_end(t);
}

/* ---------------------------------------------------------------- orb */
static id<MTLBuffer> ob_buf[3];
static int ob_n[3];
void gfx_orb_init(const float *v[3], const int n[3]) {
    for (int p = 0; p < 3; p++) { ob_n[p] = n[p]; ob_buf[p] = [dev newBufferWithBytes:v[p] length:(NSUInteger)(44 * n[p]) options:MTLResourceStorageModeShared]; }
}
void gfx_orb_draw(float cx, float cy, float R, float spin, float t, float beat, const float *bs, const float *col) {
    if (!enc || cur_target || !ob_buf[0]) return;
    struct { float res[2], pad[2], geo[4], anim[4], c0[4], c1[4], c2[4]; int umode, pad2[3]; float bs[64]; } p;
    memset(&p, 0, sizeof p);
    p.res[0] = params.res[0]; p.res[1] = params.res[1];
    p.geo[0] = cx; p.geo[1] = cy; p.geo[2] = R;
    p.anim[0] = spin; p.anim[1] = t; p.anim[2] = beat;
    memcpy(p.c0, col, 3 * sizeof(float)); memcpy(p.c1, col + 3, 3 * sizeof(float)); memcpy(p.c2, col + 6, 3 * sizeof(float));
    memcpy(p.bs, bs, sizeof p.bs);
    [enc setRenderPipelineState:pso_orb[additive ? 1 : 0]];
    for (int m = 0; m < 3; m++) {
        p.umode = m;
        [enc setVertexBuffer:ob_buf[m] offset:0 atIndex:0];
        [enc setVertexBytes:&p length:sizeof p atIndex:1];
        [enc setFragmentBytes:&p length:sizeof p atIndex:1];
        [enc drawPrimitives:(m == 2 ? MTLPrimitiveTypePoint : MTLPrimitiveTypeTriangle) vertexStart:0 vertexCount:(NSUInteger)ob_n[m]];
    }
}

/* ---------------------------------------------------------------- scopes */
#define SC_NP 96
#define SC_MAXT 64
static id<MTLTexture> sc_vals[INFLIGHT], sc_info[INFLIGHT];       /* one pair per frame in flight: the CPU writes while the GPU still reads the older ones */
void gfx_scope_draw(const float *vals, const float *info, int nt, float lw, const float *col) {
    if (!enc || cur_target || nt <= 0) return;
    if (!sc_vals[slot]) {
        MTLTextureDescriptor *a = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:SC_NP height:SC_MAXT mipmapped:NO];
        MTLTextureDescriptor *b = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:SC_MAXT height:2 mipmapped:NO];
        a.usage = b.usage = MTLTextureUsageShaderRead;
        sc_vals[slot] = [dev newTextureWithDescriptor:a]; sc_info[slot] = [dev newTextureWithDescriptor:b];
    }
    [sc_vals[slot] replaceRegion:MTLRegionMake2D(0, 0, SC_NP, (NSUInteger)nt) mipmapLevel:0 withBytes:vals bytesPerRow:SC_NP * sizeof(float)];
    float tmp[2][SC_MAXT][4]; memset(tmp, 0, sizeof tmp);
    for (int t = 0; t < nt; t++) { memcpy(tmp[0][t], info + 8 * t, 4 * sizeof(float)); memcpy(tmp[1][t], info + 8 * t + 4, 4 * sizeof(float)); }
    [sc_info[slot] replaceRegion:MTLRegionMake2D(0, 0, SC_MAXT, 2) mipmapLevel:0 withBytes:tmp bytesPerRow:SC_MAXT * 4 * sizeof(float)];
    struct { float res[2]; float lw; int pass_; float c0[4], c1[4], c2[4]; } p;
    memset(&p, 0, sizeof p);
    p.res[0] = params.res[0]; p.res[1] = params.res[1]; p.lw = lw;
    memcpy(p.c0, col, 3 * sizeof(float)); memcpy(p.c1, col + 3, 3 * sizeof(float)); memcpy(p.c2, col + 6, 3 * sizeof(float));
    [enc setVertexTexture:sc_vals[slot] atIndex:0];
    [enc setVertexTexture:sc_info[slot] atIndex:1];
    for (int ps = 0; ps < 2; ps++) {                                   /* the glow adds, the core line blends */
        p.pass_ = ps;
        [enc setRenderPipelineState:pso_scope[ps == 0 ? 1 : 0]];
        [enc setVertexBytes:&p length:sizeof p atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6 instanceCount:(NSUInteger)(nt * (SC_NP - 1))];
    }
    additive = 0;
}

/* ---------------------------------------------------------------- voxel */
static gfx_tex *vx_tex;
gfx_tex *gfx_voxel_render(int w, int h, const float *cam, const float *geo, const float *lv, const float *c) {
    if (!vx_tex || vx_tex->w != w || vx_tex->h != h) {
        gfx_tex_free(vx_tex);
        vx_tex = gfx_tex_create(w, h, GFX_RGBA8, NULL, GFX_MIPS | GFX_TARGET | GFX_DEPTH);
        if (!vx_tex) return NULL;
    }
    struct { float cam[4], geo[4], c0[4], c1[4], c2[4], fogc[4], bg0[4]; float lv[64]; } p;
    memset(&p, 0, sizeof p);
    memcpy(p.cam, cam, sizeof p.cam); memcpy(p.geo, geo, sizeof p.geo);
    memcpy(p.c0, c, 3 * sizeof(float)); memcpy(p.c1, c + 3, 3 * sizeof(float)); memcpy(p.c2, c + 6, 3 * sizeof(float));
    memcpy(p.fogc, c + 9, 3 * sizeof(float)); memcpy(p.bg0, c + 12, 3 * sizeof(float));
    memcpy(p.lv, lv, sizeof p.lv);
    offscreen_begin(vx_tex, c[9], c[10], c[11], 0);
    [enc setRenderPipelineState:pso_vx];
    [enc setDepthStencilState:dss_less];
    [enc setVertexBytes:&p length:sizeof p atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:36 instanceCount:(NSUInteger)((int)geo[1] * 64 * 15)];
    offscreen_end(vx_tex);
    return vx_tex;
}

/* ---------------------------------------------------------------- 3D card */
gfx_mesh *gfx_mesh_create(const void *verts, int nverts, const uint16_t *idx, int nidx) {
    gfx_mesh *m = (gfx_mesh *)calloc(1, sizeof *m);
    if (!m) return NULL;
    if (nverts <= 0 || nidx <= 0) return m;                            /* an empty part (a model without a back picture): no buffers, nothing to draw (Metal gives nil for a zero length) */
    m->n = nidx;
    m->vb = [dev newBufferWithBytes:verts length:(NSUInteger)(24 * nverts) options:MTLResourceStorageModeShared];
    m->ib = [dev newBufferWithBytes:idx length:(NSUInteger)(sizeof(uint16_t) * (size_t)nidx) options:MTLResourceStorageModeShared];
    if (!m->vb || !m->ib) { m->vb = nil; m->ib = nil; m->n = 0; }
    return m;
}
void gfx_mesh_update_verts(gfx_mesh *m, const void *verts, int nverts) {
    if (!m || !m->vb || nverts <= 0) return;
    memcpy([m->vb contents], verts, (size_t)(24 * nverts));                    /* shared storage: the CPU writes it directly */
}
void gfx_card_render(gfx_tex *target, const float *mvp, const float *mv, gfx_mesh *board, gfx_mesh *front, gfx_tex *front_tex, gfx_mesh *back, gfx_tex *back_tex) {
    struct { float mvp[16], mv[16]; int umode, pad[3]; } p;
    memcpy(p.mvp, mvp, sizeof p.mvp); memcpy(p.mv, mv, sizeof p.mv); memset(p.pad, 0, sizeof p.pad);
    offscreen_begin(target, 0, 0, 0, 0);
    [enc setRenderPipelineState:target->msc ? pso_c3ms : pso_c3];
    [enc setDepthStencilState:dss_less];
    if (front_tex) [enc setFragmentTexture:front_tex->tex atIndex:0];                 /* the board pass does not sample, but a texture has to be bound */
    gfx_mesh *ms[3] = { board, front, back }; gfx_tex *ts[3] = { front_tex, front_tex, back_tex };
    for (int i = 0; i < 3; i++) {
        if (!ms[i] || ms[i]->n <= 0 || !ms[i]->ib || !ms[i]->vb || !ts[i]) continue;      /* nothing to draw: an indexed draw without an index buffer crashes the GPU driver */
        p.umode = i ? 1 : 0;
        [enc setVertexBuffer:ms[i]->vb offset:0 atIndex:0];
        [enc setVertexBytes:&p length:sizeof p atIndex:1];
        [enc setFragmentBytes:&p length:sizeof p atIndex:1];
        [enc setFragmentTexture:ts[i]->tex atIndex:0];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:(NSUInteger)ms[i]->n indexType:MTLIndexTypeUInt16 indexBuffer:ms[i]->ib indexBufferOffset:0];
    }
    offscreen_end(target);
}
