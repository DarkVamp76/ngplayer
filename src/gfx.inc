/* ================================================================== OpenGL loader */

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef char GLchar;
typedef long GLsizeiptr;
typedef long GLintptr;

#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RED 0x1903
#define GL_R8 0x8229
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 4
#define GL_ARRAY_BUFFER 0x8892
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05

#define GLFUNCS(X) \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glClear, (GLuint)) \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glEnable, (GLenum)) \
    X(void, glBlendFunc, (GLenum, GLenum)) \
    X(void, glGenTextures, (GLsizei, GLuint *)) \
    X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
    X(void, glPixelStorei, (GLenum, GLint)) \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, glCompileShader, (GLuint)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint, GLuint)) \
    X(void, glLinkProgram, (GLuint)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, glUseProgram, (GLuint)) \
    X(void, glDeleteShader, (GLuint)) \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
    X(void, glUniform1i, (GLint, GLint)) \
    X(void, glUniform1f, (GLint, GLfloat)) \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint *)) \
    X(void, glBindVertexArray, (GLuint)) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) \
    X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(void, glEnableVertexAttribArray, (GLuint)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, unsigned char, GLsizei, const void *)) \
    X(void, glGenerateMipmap, (GLenum))

#define X(ret, name, args) static ret(*name) args;
GLFUNCS(X)
#undef X

static int gl_load(void) {
#define X(ret, name, args) \
    name = (ret(*) args)SDL_GL_GetProcAddress(#name); \
    if (!name) { fprintf(stderr, "missing OpenGL function %s\n", #name); return -1; }
    GLFUNCS(X)
#undef X
    return 0;
}

/* ================================================================== batched 2D renderer */

typedef struct { float r, g, b, a; } col;
typedef struct { float x, y, u, v, r, g, b, a, m, p1, p2, p3; } vtx;

#define MAXV 200000
static vtx *vb;
static int nv;
static int add_blend;
static GLuint prog, vao, vbo, fonttex;
static GLint u_res, u_pxs, u_tex;
static float view_w, view_h, view_scale = 1;
static int glyph_idx[256];

static const char *VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos; layout(location=1) in vec2 uv; layout(location=2) in vec4 col;"
    "layout(location=3) in vec4 par;\n"
    "uniform vec2 res; out vec2 vuv; out vec4 vcol; out vec4 vpar;\n"
    "void main(){ vuv=uv; vcol=col; vpar=par;"
    " gl_Position=vec4(pos.x/res.x*2.0-1.0, 1.0-pos.y/res.y*2.0, 0.0, 1.0); }\n";

static const char *FS =
    "#version 330 core\n"
    "in vec2 vuv; in vec4 vcol; in vec4 vpar; out vec4 o;\n"
    "uniform sampler2D tex; uniform float pxs;\n"
    "float sdbox(vec2 p, vec2 h, float r){ vec2 q=abs(p)-h+vec2(r); return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r; }\n"
    "void main(){\n"
    " int m=int(vpar.x+0.5);\n"
    " if(m==0){ o=vcol; }\n"
    " else if(m==1){ float a=texture(tex,vuv).r; o=vec4(vcol.rgb,vcol.a*a); }\n"
    " else if(m==2){ float d=sdbox(vuv,vpar.yz,vpar.w); o=vec4(vcol.rgb,vcol.a*clamp(0.5-d*pxs,0.0,1.0)); }\n"
    " else if(m==3){ float t=abs(vuv.y); o=vec4(vcol.rgb,vcol.a*exp(-t*t*vpar.y)); }\n"
    " else if(m==5){ float d=sdbox(vuv,vpar.yz,vpar.w); float a=clamp(1.0-d/vpar.w,0.0,1.0); o=vec4(vcol.rgb,vcol.a*a*a); }\n"
    " else { o=vec4(vcol.rgb,vcol.a*exp(-dot(vuv,vuv)/(2.0*vpar.y*vpar.y))); }\n"
    "}\n";

static void flush(void) {
    if (!nv) return;
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((size_t)nv * sizeof(vtx)), vb);
    glDrawArrays(GL_TRIANGLES, 0, nv);
    nv = 0;
}

static void set_additive(int on) {
    if (on == add_blend) return;
    flush();
    add_blend = on;
    glBlendFunc(GL_SRC_ALPHA, on ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
}

static void V(float x, float y, float u, float v, col c, float m, float p1, float p2, float p3) {
    if (nv >= MAXV) flush();
    vtx *t = &vb[nv++];
    t->x = x; t->y = y; t->u = u; t->v = v;
    t->r = c.r; t->g = c.g; t->b = c.b; t->a = c.a;
    t->m = m; t->p1 = p1; t->p2 = p2; t->p3 = p3;
}

static col C(float r, float g, float b, float a) { col c = { r, g, b, a }; return c; }
static col alpha(col c, float a) { c.a *= a; return c; }
static col mix(col a, col b, float t) {
    return C(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

static void rect_grad(float x, float y, float w, float h, col top, col bot) {
    V(x, y, 0, 0, top, 0, 0, 0, 0); V(x + w, y, 0, 0, top, 0, 0, 0, 0); V(x, y + h, 0, 0, bot, 0, 0, 0, 0);
    V(x + w, y, 0, 0, top, 0, 0, 0, 0); V(x + w, y + h, 0, 0, bot, 0, 0, 0, 0); V(x, y + h, 0, 0, bot, 0, 0, 0, 0);
}
static void rect(float x, float y, float w, float h, col c) { rect_grad(x, y, w, h, c, c); }

/* quad with SDF-local coordinates: uv is the offset from the centre */
static void sdf_quad(float x, float y, float w, float h, float pad, col c, float mode, float p1, float p2, float p3) {
    float hx = w / 2 + pad, hy = h / 2 + pad, cx = x + w / 2, cy = y + h / 2;
    V(cx - hx, cy - hy, -hx, -hy, c, mode, p1, p2, p3); V(cx + hx, cy - hy, hx, -hy, c, mode, p1, p2, p3);
    V(cx - hx, cy + hy, -hx, hy, c, mode, p1, p2, p3);  V(cx + hx, cy - hy, hx, -hy, c, mode, p1, p2, p3);
    V(cx + hx, cy + hy, hx, hy, c, mode, p1, p2, p3);   V(cx - hx, cy + hy, -hx, hy, c, mode, p1, p2, p3);
}
static void rrect(float x, float y, float w, float h, float r, col c) { sdf_quad(x, y, w, h, 1.0f, c, 2, w / 2, h / 2, r); }
static void shadow(float x, float y, float w, float h, float r, float soft, col c) {
    sdf_quad(x, y, w, h, soft, c, 5, w / 2, h / 2, soft + r * 0); (void)r;
}
static void blob(float cx, float cy, float sigma, col c) {
    float e = sigma * 3.2f;
    sdf_quad(cx - e, cy - e, e * 2, e * 2, 0, c, 6, sigma, 0, 0);
}
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, col c) {
    V(x0, y0, 0, 0, c, 0, 0, 0, 0); V(x1, y1, 0, 0, c, 0, 0, 0, 0); V(x2, y2, 0, 0, c, 0, 0, 0, 0);
}

/* soft anti-aliased polyline, xy = n points; w half width in px, k = falloff */
static void polyline(const float *xy, int n, float w, float k, col c) {
    if (n < 2) return;
    float px[2] = { 0, 0 };
    float nx[1100], ny[1100];
    if (n > 1100) n = 1100;
    for (int i = 0; i < n; i++) {
        float dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;
        if (i > 0) { dx0 = xy[2 * i] - xy[2 * i - 2]; dy0 = xy[2 * i + 1] - xy[2 * i - 1]; }
        if (i < n - 1) { dx1 = xy[2 * i + 2] - xy[2 * i]; dy1 = xy[2 * i + 3] - xy[2 * i + 1]; }
        float l0 = sqrtf(dx0 * dx0 + dy0 * dy0), l1 = sqrtf(dx1 * dx1 + dy1 * dy1);
        float ax = l0 > 1e-6f ? -dy0 / l0 : 0, ay = l0 > 1e-6f ? dx0 / l0 : 0;
        float bx = l1 > 1e-6f ? -dy1 / l1 : 0, by = l1 > 1e-6f ? dx1 / l1 : 0;
        float sx = ax + bx, sy = ay + by, sl = sqrtf(sx * sx + sy * sy);
        if (sl < 1e-4f) { sx = ax + bx; sy = ay + by; if (l1 > 0) { sx = bx; sy = by; } else { sx = ax; sy = ay; } sl = 1; }
        sx /= sl; sy /= sl;
        float d = sx * (l0 > 0 ? ax : bx) + sy * (l0 > 0 ? ay : by);   /* miter length compensation */
        float m = d > 0.5f ? 1.0f / d : 2.0f;
        nx[i] = sx * m; ny[i] = sy * m;
    }
    (void)px;
    for (int i = 0; i < n - 1; i++) {
        float x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * i + 2], y1 = xy[2 * i + 3];
        float a0x = x0 + nx[i] * w, a0y = y0 + ny[i] * w, a1x = x0 - nx[i] * w, a1y = y0 - ny[i] * w;
        float b0x = x1 + nx[i + 1] * w, b0y = y1 + ny[i + 1] * w, b1x = x1 - nx[i + 1] * w, b1y = y1 - ny[i + 1] * w;
        V(a0x, a0y, 0, 1, c, 3, k, 0, 0); V(a1x, a1y, 0, -1, c, 3, k, 0, 0); V(b0x, b0y, 0, 1, c, 3, k, 0, 0);
        V(a1x, a1y, 0, -1, c, 3, k, 0, 0); V(b1x, b1y, 0, -1, c, 3, k, 0, 0); V(b0x, b0y, 0, 1, c, 3, k, 0, 0);
    }
}

/* ---- text (Latin-1 codes) */
static float text_width(const char *s, float size) {
    float w = 0, sc = size / FONT_PX;
    for (; *s; s++) {
        int gi = glyph_idx[(unsigned char)*s];
        if (gi >= 0) w += font_glyphs[gi].adv * sc;
    }
    return w;
}

static float text(float x, float y, float size, col c, const char *s) {
    float sc = size / FONT_PX, x0 = x;
    for (; *s; s++) {
        int gi = glyph_idx[(unsigned char)*s];
        if (gi < 0) continue;
        const font_glyph *g = &font_glyphs[gi];
        if (g->w && g->h) {
            float gx = x + g->xoff * sc, gy = y + g->yoff * sc, gw = g->w * sc, gh = g->h * sc;
            float u0 = (float)g->x / FONT_ATLAS_W, v0 = (float)g->y / FONT_ATLAS_H;
            float u1 = (float)(g->x + g->w) / FONT_ATLAS_W, v1 = (float)(g->y + g->h) / FONT_ATLAS_H;
            V(gx, gy, u0, v0, c, 1, 0, 0, 0); V(gx + gw, gy, u1, v0, c, 1, 0, 0, 0); V(gx, gy + gh, u0, v1, c, 1, 0, 0, 0);
            V(gx + gw, gy, u1, v0, c, 1, 0, 0, 0); V(gx + gw, gy + gh, u1, v1, c, 1, 0, 0, 0); V(gx, gy + gh, u0, v1, c, 1, 0, 0, 0);
        }
        x += g->adv * sc;
    }
    return x - x0;
}

static float text_right(float xr, float y, float size, col c, const char *s) {
    float w = text_width(s, size);
    text(xr - w, y, size, c, s);
    return w;
}

/* UTF-8 -> Latin-1 where possible (file names) */
static void utf8_to_latin1(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < cap; p++) {
        if (*p < 0x80) out[o++] = (char)*p;
        else if ((*p == 0xC2 || *p == 0xC3) && (p[1] & 0xC0) == 0x80) { out[o++] = (char)(((*p & 3) << 6) | (p[1] & 0x3F)); p++; }
        else if (*p >= 0xC0) { out[o++] = '?'; while ((p[1] & 0xC0) == 0x80) p++; }
    }
    out[o] = 0;
}

static void fit_text(char *s, float maxw, float size) {
    if (text_width(s, size) <= maxw) return;
    size_t n = strlen(s);
    while (n > 1) {
        s[--n] = 0;
        char tmp[300];
        snprintf(tmp, sizeof tmp, "%s...", s);
        if (text_width(tmp, size) <= maxw) { strcpy(s, tmp); return; }
    }
}

/* ================================================================== palette */
