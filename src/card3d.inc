/* Rotating 3D sound card (Gravis Ultrasound GF1 / Ultrasound PnP with AMD InterWave, Terratec EWS64, AdLib Gold) in the header, rendered on the GPU (OpenGL 3.3 / WebGL2):
 * the baked model (card3d_data.h, made from the real 3D models by tools/bake_card3d.py) lives in vertex/index buffers, is drawn with a small
 * shader (flat shading from screen space derivatives, the photo of the board as texture) and a depth buffer into an off-screen texture, which
 * the 2D renderer then shows like any picture. GPL-3.0-or-later */
#include <math.h>
#include "card3d_data.h"
#include "a500_data.h"      /* the Amiga 500 replaces the drawn Paula chip */
#define C3_N 5

typedef struct { const int16_t *v; const uint8_t *vc, *pal; const uint16_t *idx; int nv, nt; const int16_t *tv; const uint16_t *tuv, *tidx; int ntt, ntv; const uint16_t *tex; int tw, th; float aspect; const int16_t *bv; const uint16_t *buv, *bidx; int nbv, nbt; const uint8_t *bpal, *bpix; int bw, bh; float tilt, fit; } C3Model;   /* tilt and fit: 0 = the defaults of the sound cards (0.17 rad, 0.95) */
static const C3Model C3M[C3_N] = {
    { c3_GF1_v, c3_GF1_vc, c3_GF1_pal, c3_GF1_i, C3_GF1_NV, C3_GF1_NT, c3_GF1_tv, c3_GF1_tuv, c3_GF1_ti, C3_GF1_NTT, C3_GF1_NTV, c3_GF1_tex, C3_GF1_TW, C3_GF1_TH, C3_GF1_ASPECT, c3_GF1_bv, c3_GF1_buv, c3_GF1_bi, C3_GF1_NBV, C3_GF1_NBT, c3_GF1_bpal, c3_GF1_bpix, C3_GF1_BW, C3_GF1_BH },
    { c3_IW_v, c3_IW_vc, c3_IW_pal, c3_IW_i, C3_IW_NV, C3_IW_NT, c3_IW_tv, c3_IW_tuv, c3_IW_ti, C3_IW_NTT, C3_IW_NTV, c3_IW_tex, C3_IW_TW, C3_IW_TH, C3_IW_ASPECT, c3_IW_bv, c3_IW_buv, c3_IW_bi, C3_IW_NBV, C3_IW_NBT, c3_IW_bpal, c3_IW_bpix, C3_IW_BW, C3_IW_BH },
    { c3_EWS_v, c3_EWS_vc, c3_EWS_pal, c3_EWS_i, C3_EWS_NV, C3_EWS_NT, c3_EWS_tv, c3_EWS_tuv, c3_EWS_ti, C3_EWS_NTT, C3_EWS_NTV, c3_EWS_tex, C3_EWS_TW, C3_EWS_TH, C3_EWS_ASPECT, c3_EWS_bv, c3_EWS_buv, c3_EWS_bi, C3_EWS_NBV, C3_EWS_NBT, c3_EWS_bpal, c3_EWS_bpix, C3_EWS_BW, C3_EWS_BH },
    { c3_GOLD_v, c3_GOLD_vc, c3_GOLD_pal, c3_GOLD_i, C3_GOLD_NV, C3_GOLD_NT, c3_GOLD_tv, c3_GOLD_tuv, c3_GOLD_ti, C3_GOLD_NTT, C3_GOLD_NTV, c3_GOLD_tex, C3_GOLD_TW, C3_GOLD_TH, C3_GOLD_ASPECT, c3_GOLD_bv, c3_GOLD_buv, c3_GOLD_bi, C3_GOLD_NBV, C3_GOLD_NBT, c3_GOLD_bpal, c3_GOLD_bpix, C3_GOLD_BW, C3_GOLD_BH },
    /* Amiga 500: lies flat, so it is seen from above at 0.50 rad and turns about its vertical axis; the box aspect (1.75 after C3_BOX) holds the diagonal of the turning case, fit leaves room for the perspective */
    { c3_A500_v, c3_A500_vc, c3_A500_pal, c3_A500_i, C3_A500_NV, C3_A500_NT, c3_A500_tv, c3_A500_tuv, c3_A500_ti, C3_A500_NTT, C3_A500_NTV, c3_A500_tex, C3_A500_TW, C3_A500_TH, 2.19f, c3_A500_bv, c3_A500_buv, c3_A500_bi, C3_A500_NBV, C3_A500_NBT, c3_A500_bpal, c3_A500_bpix, C3_A500_BW, C3_A500_BH, 0.50f, 0.72f },
};
#define C3_BOX 0.80f                                        /* box aspect = model aspect * this: head room for the edge-on view */
static float c3_box_aspect(int mi) { return C3M[mi].aspect * C3_BOX; }

static const char *C3_VS =
    GLSL_VERSION
    "layout(location=0) in vec3 pos; layout(location=1) in vec2 uv; layout(location=2) in vec4 col;\n"
    "uniform mat4 mvp; uniform mat4 mv; out vec3 vv; out vec2 vuv; out vec4 vcol;\n"
    "void main(){ vec4 p=vec4(pos,1.0); vv=(mv*p).xyz; vuv=uv; vcol=col; gl_Position=mvp*p; }\n";
static const char *C3_FS =
    GLSL_VERSION
    "in vec3 vv; in vec2 vuv; in vec4 vcol; out vec4 o; uniform sampler2D tex; uniform int umode;\n"
    "void main(){\n"
    " vec3 n=normalize(cross(dFdx(vv),dFdy(vv))); if(dot(n,-vv)<0.0) n=-n;\n"
    " float d=max(dot(n,normalize(vec3(0.33,0.52,0.79))),0.0);\n"
    " vec3 base=umode==1?texture(tex,vuv).rgb:vcol.rgb;\n"
    " float sh=umode==1?0.62+0.55*d:0.36+0.64*d;\n"
    " o=vec4(base*sh,1.0); }\n";

typedef struct { GLuint vao[3], vbo[3], ebo[3], tex, btex, fbo, color, depth, fbo2, color2, depth2; int ready, w, h; } C3Gl;   /* the second target (…2) is the picture of the Specs window: the card swings there instead of turning */
static C3Gl c3gl[C3_N];
static GLuint c3_prog;
static GLint c3_mvp, c3_mv, c3_umode, c3_utex;
static float c3_ang;
static int c3_first = 1;

static GLuint c3_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "card3d shader: %s\n", log); }
    return s;
}

static void c3_matrices(float ang, float tilt, float fit, float aspect, float *mvp, float *mv) {
    const float D = 7.0f, zn = 2.5f, zf = 12.0f;
    float ca = cosf(ang), sa = sinf(ang), ct = cosf(tilt), st = sinf(tilt);
    /* model -> view (column major): rotate about the vertical axis, tilt about the horizontal axis, move away from the camera */
    float M[16] = { ca, st * sa, -ct * sa, 0,   0, ct, st, 0,   sa, -st * ca, ct * ca, 0,   0, 0, -D, 1 };
    for (int i = 0; i < 16; i++) mv[i] = M[i];
    float fx = fit * D, fy = -fit * D * aspect;           /* fy negative: the picture is upside down in the texture */
    float P[16] = { fx, 0, 0, 0,  0, fy, 0, 0,  0, 0, -(zf + zn) / (zf - zn), -1,  0, 0, -2 * zf * zn / (zf - zn), 0 };
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) { float s = 0; for (int k = 0; k < 4; k++) s += P[k * 4 + r] * M[c * 4 + k]; mvp[c * 4 + r] = s; }
}

static void c3_setup(int mi) {
    C3Gl *g = &c3gl[mi]; const C3Model *m = &C3M[mi];
    if (g->ready) return;
    if (!c3_prog) {
        GLuint vs = c3_shader(GL_VERTEX_SHADER, C3_VS), fs = c3_shader(GL_FRAGMENT_SHADER, C3_FS);
        c3_prog = glCreateProgram(); glAttachShader(c3_prog, vs); glAttachShader(c3_prog, fs); glLinkProgram(c3_prog);
        GLint ok = 0; glGetProgramiv(c3_prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(c3_prog, sizeof log, NULL, log); fprintf(stderr, "card3d link: %s\n", log); }
        glDeleteShader(vs); glDeleteShader(fs);
        c3_mvp = glGetUniformLocation(c3_prog, "mvp"); c3_mv = glGetUniformLocation(c3_prog, "mv");
        c3_umode = glGetUniformLocation(c3_prog, "umode"); c3_utex = glGetUniformLocation(c3_prog, "tex");
    }
    const float Q = 1.0f / 20000.0f;
    typedef struct { float x, y, z, u, v; unsigned char r, g, b, a; } V3;
    int counts[3] = { m->nv, m->ntv, m->nbv }, tcnt[3] = { m->nt, m->ntt, m->nbt };
    glGenVertexArrays(3, g->vao); glGenBuffers(3, g->vbo); glGenBuffers(3, g->ebo);
    for (int p = 0; p < 3; p++) {
        int n = counts[p];
        V3 *vb3 = (V3 *)malloc(sizeof(V3) * (size_t)n);
        for (int i = 0; i < n; i++) {
            const int16_t *s = p == 2 ? m->bv : (p ? m->tv : m->v);
            V3 *o = &vb3[i];
            o->x = s[3 * i] * Q; o->y = s[3 * i + 1] * Q; o->z = s[3 * i + 2] * Q + (p == 1 ? 0.0002f : 0.0f);   /* the front texture sits a hair above the board (the back quad is baked a hair below) */
            { const uint16_t *uv = p == 2 ? m->buv : m->tuv; o->u = p ? uv[2 * i] / 65535.0f : 0; o->v = p ? uv[2 * i + 1] / 65535.0f : 0; }
            if (p) { o->r = o->g = o->b = 255; } else { const uint8_t *pl = m->pal + 3 * m->vc[i]; o->r = pl[0]; o->g = pl[1]; o->b = pl[2]; }
            o->a = 255;
        }
        glBindVertexArray(g->vao[p]);
        glBindBuffer(GL_ARRAY_BUFFER, g->vbo[p]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(V3) * (size_t)n), vb3, GL_STATIC_DRAW);
        free(vb3);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ebo[p]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint16_t) * 3 * (size_t)tcnt[p]), p == 2 ? m->bidx : (p ? m->tidx : m->idx), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(V3), (const void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(V3), (const void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, sizeof(V3), (const void *)(5 * sizeof(float)));
    }
    glBindVertexArray(0);
    glGenTextures(1, &g->tex);                                       /* the photo of the board */
    glBindTexture(GL_TEXTURE_2D, g->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m->tw, m->th, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, m->tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    {                                                                /* the back of the board: 4 bit indexed picture, made from the model's parts by the bake tool */
        uint16_t *px = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)m->bw * (size_t)m->bh);
        for (int i = 0; i < m->bw * m->bh; i++) {
            int idx = (i & 1) ? (m->bpix[i >> 1] & 15) : (m->bpix[i >> 1] >> 4);
            const uint8_t *pl = m->bpal + 3 * idx;
            px[i] = (uint16_t)(((pl[0] >> 3) << 11) | ((pl[1] >> 2) << 5) | (pl[2] >> 3));
        }
        glGenTextures(1, &g->btex);
        glBindTexture(GL_TEXTURE_2D, g->btex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m->bw, m->bh, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px);
        free(px);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    g->w = g_mobile ? 320 : 512; g->h = (int)(g->w / c3_box_aspect(mi) + 0.5f);       /* off-screen target, shown at about half this size: smooth edges through the mip maps */
    for (int t = 0; t < 2; t++) {                                    /* target 0: the turning card of the header, target 1: the swinging card of the Specs window */
        GLuint *col = t ? &g->color2 : &g->color, *dep = t ? &g->depth2 : &g->depth, *fb = t ? &g->fbo2 : &g->fbo;
        glGenTextures(1, col);
        glBindTexture(GL_TEXTURE_2D, *col);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g->w, g->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenRenderbuffers(1, dep);
        glBindRenderbuffer(GL_RENDERBUFFER, *dep);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g->w, g->h);
        glGenFramebuffers(1, fb);
        glBindFramebuffer(GL_FRAMEBUFFER, *fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *col, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *dep);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "card3d: incomplete framebuffer\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    g->ready = 1;
}

/* draws the card at the angle ang into the given target */
static void c3_render(int mi, float ang, GLuint fbo, int fw, int fh) {
    C3Gl *g = &c3gl[mi]; const C3Model *m = &C3M[mi];
    float mvp[16], mv[16];
    c3_matrices(ang, m->tilt > 0 ? m->tilt : 0.17f, m->fit > 0 ? m->fit : 0.95f, (float)g->w / (float)g->h, mvp, mv);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, g->w, g->h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glUseProgram(c3_prog);
    glUniformMatrix4fv(c3_mvp, 1, 0, mvp); glUniformMatrix4fv(c3_mv, 1, 0, mv);
    glUniform1i(c3_utex, 0);
    glBindVertexArray(g->vao[0]); glUniform1i(c3_umode, 0);
    glDrawElements(GL_TRIANGLES, m->nt * 3, GL_UNSIGNED_SHORT, (const void *)0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex);
    glBindVertexArray(g->vao[1]); glUniform1i(c3_umode, 1);
    glDrawElements(GL_TRIANGLES, m->ntt * 3, GL_UNSIGNED_SHORT, (const void *)0);
    glBindTexture(GL_TEXTURE_2D, g->btex);
    glBindVertexArray(g->vao[2]);
    glDrawElements(GL_TRIANGLES, m->nbt * 3, GL_UNSIGNED_SHORT, (const void *)0);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, fbo == g->fbo2 ? g->color2 : g->color);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);                                           /* the 2D renderer uploads into the buffer that is bound */
    glViewport(0, 0, fw, fh);
}
/* once per frame, before the 2D pass: advance the turn and draw the card into its texture (only while a card is shown) */
static void card3d_frame(int mi, int fw, int fh) {
    c3_setup(mi);
    static int last_mi = -1;
    const char *fix = getenv("NGPLAYER_TEST_CARDANG");
    if (fix) c3_ang = (float)atof(fix) * 0.017453293f;
    else if (!g_paused) c3_ang += (float)g_dt * 0.55f;
    else if (!c3_first && last_mi == mi) return;                                 /* paused: keep the last picture */
    c3_first = 0; last_mi = mi;
    c3_render(mi, c3_ang, c3gl[mi].fbo, fw, fh);
}
/* the card of the Specs window: it does not turn but swings to and fro and shows only its front */
static float c3_swing_t;
#define C3_SWING_AMP 0.50f          /* radians to each side (about 29 degrees) */
static void card3d_frame_swing(int mi, int fw, int fh) {
    c3_setup(mi);
    const char *fix = getenv("NGPLAYER_TEST_SWING");                             /* test: a fixed phase in degrees of the swing */
    if (fix) c3_swing_t = (float)atof(fix) * 0.017453293f / 1.1f; else c3_swing_t += (float)g_dt;
    c3_render(mi, C3_SWING_AMP * sinf(c3_swing_t * 1.1f), c3gl[mi].fbo2, fw, fh);
}
static GLuint c3_texture_swing(int mi) { return c3gl[mi].color2; }
static GLuint c3_texture(int mi) { return c3gl[mi].color; }
