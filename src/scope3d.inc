/* SCOPES on the GPU (OpenGL 3.3 / WebGL2): the CPU still finds the trigger and the gain of every voice (cheap), but only writes the 96 curve values per
 * voice into a small float texture; one instanced draw call then builds all line segments of all voices (a soft wide glow pass and a thin core pass)
 * in the vertex shader. Before, every voice's two polylines were generated on the CPU as about 1100 vertices. GPL-3.0-or-later */
#define SC_NP 96
#define SC_MAXT 64
static const char *SC_VS =
    GLSL_VERSION
    "uniform vec2 res; uniform sampler2D vals; uniform sampler2D info; uniform int pass_; uniform vec3 c0, c1, c2; uniform float lw;\n"
    "out vec4 vcol; out float vt; out float vk;\n"
    "void main(){\n"
    " int seg_n=95; int id=gl_InstanceID; int tile=id/seg_n; int seg=id%seg_n;\n"
    " vec4 ta=texelFetch(info,ivec2(tile,0),0), tb=texelFetch(info,ivec2(tile,1),0);\n"
    " int np=int(tb.z+0.5);\n"
    " if(seg>=np-1 || tb.w<0.5){ gl_Position=vec4(2.0,2.0,2.0,1.0); vcol=vec4(0.0); vt=0.0; vk=1.0; return; }\n"
    " float v0=texelFetch(vals,ivec2(seg,tile),0).r, v1=texelFetch(vals,ivec2(seg+1,tile),0).r;\n"
    " vec2 p0=vec2(ta.x+ta.z*float(seg)/float(np-1), ta.y-v0*ta.w), p1=vec2(ta.x+ta.z*float(seg+1)/float(np-1), ta.y-v1*ta.w);\n"
    " vec2 dir=normalize(p1-p0+vec2(1e-5,0.0)); vec2 pp=vec2(-dir.y,dir.x);\n"
    " int vi=gl_VertexID; int corner[6]=int[6](0,1,2,2,1,3); int q=corner[vi]; float end=float(q/2), side=(q%2==0)?-1.0:1.0;\n"
    " float w=(pass_==0?3.4:1.0)*lw; vec2 base=end<0.5?p0:p1; vec2 pos=base+pp*side*w; vt=side;\n"
    " int ci=int(tb.y+0.5); vec3 lc=ci==0?c0:(ci==1?c1:c2); float lvl=tb.x;\n"
    " if(pass_==0){ vcol=vec4(lc,0.20*lvl); vk=3.5; } else { vcol=vec4(mix(lc,vec3(1.0),0.3),0.35+0.65*lvl); vk=3.0; }\n"
    " gl_Position=vec4(pos.x/res.x*2.0-1.0, 1.0-pos.y/res.y*2.0, 0.0, 1.0); }\n";
static const char *SC_FS = GLSL_VERSION "in vec4 vcol; in float vt; in float vk; out vec4 o; void main(){ o=vec4(vcol.rgb, vcol.a*exp(-vt*vt*vk)); }\n";

static GLuint sc_prog, sc_vao, sc_tv, sc_ti;
static GLint sc_ures, sc_uvals, sc_uinfo, sc_upass, sc_uc0, sc_uc1, sc_uc2, sc_ulw;
static float sc_vals[SC_MAXT][SC_NP], sc_info[SC_MAXT][2][4];
static int sc_nt;

static GLuint sc_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "scope shader: %s\n", log); }
    return s;
}
static void sc_begin(void) { sc_nt = 0; }
/* queue one voice: area bx..bx+bw, centre line mid, amplitude amp, np points (values already scaled to -1..1), level, colour index, visible */
static void sc_add(const float *v, int np, float bx, float mid, float bw, float amp, float lvl, int ci, int vis) {
    if (sc_nt >= SC_MAXT) return;
    for (int j = 0; j < SC_NP; j++) sc_vals[sc_nt][j] = j < np ? v[j] : 0;
    sc_info[sc_nt][0][0] = bx; sc_info[sc_nt][0][1] = mid; sc_info[sc_nt][0][2] = bw; sc_info[sc_nt][0][3] = amp;
    sc_info[sc_nt][1][0] = lvl; sc_info[sc_nt][1][1] = (float)ci; sc_info[sc_nt][1][2] = (float)np; sc_info[sc_nt][1][3] = (float)vis;
    sc_nt++;
}
static void sc_draw(float lw) {
    if (!sc_nt) return;
    if (!sc_prog) {
        GLuint vs = sc_shader(GL_VERTEX_SHADER, SC_VS), fs = sc_shader(GL_FRAGMENT_SHADER, SC_FS);
        sc_prog = glCreateProgram(); glAttachShader(sc_prog, vs); glAttachShader(sc_prog, fs); glLinkProgram(sc_prog);
        GLint ok = 0; glGetProgramiv(sc_prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(sc_prog, sizeof log, NULL, log); fprintf(stderr, "scope link: %s\n", log); }
        glDeleteShader(vs); glDeleteShader(fs);
        sc_ures = glGetUniformLocation(sc_prog, "res"); sc_uvals = glGetUniformLocation(sc_prog, "vals"); sc_uinfo = glGetUniformLocation(sc_prog, "info");
        sc_upass = glGetUniformLocation(sc_prog, "pass_"); sc_uc0 = glGetUniformLocation(sc_prog, "c0"); sc_uc1 = glGetUniformLocation(sc_prog, "c1"); sc_uc2 = glGetUniformLocation(sc_prog, "c2");
        sc_ulw = glGetUniformLocation(sc_prog, "lw");
        glGenVertexArrays(1, &sc_vao);
        glGenTextures(1, &sc_tv); glGenTextures(1, &sc_ti);
        glBindTexture(GL_TEXTURE_2D, sc_tv);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, SC_NP, SC_MAXT, 0, GL_RED, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, sc_ti);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SC_MAXT, 2, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    flush();
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, sc_tv);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SC_NP, sc_nt, GL_RED, GL_FLOAT, sc_vals);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, sc_ti);
    for (int t = 0; t < sc_nt; t++) { glTexSubImage2D(GL_TEXTURE_2D, 0, t, 0, 1, 1, GL_RGBA, GL_FLOAT, sc_info[t][0]); glTexSubImage2D(GL_TEXTURE_2D, 0, t, 1, 1, 1, GL_RGBA, GL_FLOAT, sc_info[t][1]); }
    glUseProgram(sc_prog);
    glUniform2f(sc_ures, view_w, view_h); glUniform1i(sc_uvals, 1); glUniform1i(sc_uinfo, 2); glUniform1f(sc_ulw, lw);
    glUniform3f(sc_uc0, VC[0].r, VC[0].g, VC[0].b); glUniform3f(sc_uc1, VC[1].r, VC[1].g, VC[1].b); glUniform3f(sc_uc2, VC[2].r, VC[2].g, VC[2].b);
    glBindVertexArray(sc_vao);
    for (int p = 0; p < 2; p++) {
        set_additive(p == 0);                                  /* the glow adds, the core line blends */
        glBlendFunc(GL_SRC_ALPHA, p == 0 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glUniform1i(sc_upass, p);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, sc_nt * (SC_NP - 1));
    }
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(prog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindTexture(GL_TEXTURE_2D, fonttex);
    set_additive(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
