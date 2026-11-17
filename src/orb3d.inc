/* ORB view on the GPU (OpenGL 3.3 / WebGL2): the geodesic mesh sits once in vertex buffers (faces, edges as soft quads, nodes as points); the vertex
 * shader moves every node with the 64 band levels (the same maths as before), projects it into the 2D renderer's pixel space and colours it; the
 * CPU only smooths the levels and issues three draw calls instead of building and submitting about 3000 triangles per frame. Additive blending, so
 * no depth buffer is needed. GPL-3.0-or-later */
static const char *OB_VS =
    GLSL_VERSION
    "layout(location=0) in vec3 ua; layout(location=1) in vec3 ub; layout(location=2) in vec3 uc; layout(location=3) in vec2 mi;\n"
    "uniform vec2 res; uniform vec4 geo; uniform vec4 anim; uniform float bs[64]; uniform vec3 c0, c1, c2; uniform int umode;\n"
    "out vec4 vcol; out float vt; out vec4 vpt;\n"
    "vec3 ramp(float e){ return e<0.5?mix(c0,c1,e*2.0):mix(c1,c2,(e-0.5)*2.0); }\n"
    "void node(vec3 u, out float e, out float d, out vec2 s){\n"
    " float lat=abs(u.y); float bf=lat*63.0; int b0=int(bf); if(b0>62) b0=62; float fr=bf-float(b0); float lon=atan(u.z,u.x);\n"
    " e=(bs[b0]*(1.0-fr)+bs[b0+1]*fr)*(0.65+0.35*sin(lon*3.0+anim.y*1.3+lat*5.0)); e=min(e,1.0);\n"
    " float R=geo.z; float rad=R*(1.0+0.06*anim.z+0.36*e); vec3 p=u*rad;\n"
    " float ca=cos(anim.x), sa=sin(anim.x), ct=cos(0.42), st=sin(0.42);\n"
    " float rx=p.x*ca+p.z*sa, rz=-p.x*sa+p.z*ca; float ry=p.y*ct-rz*st, rz2=p.y*st+rz*ct;\n"
    " float sc=8.0*R/(8.0*R+rz2); s=vec2(geo.x+rx*sc, geo.y+ry*sc);\n"
    " d=clamp(0.5-0.5*rz2/(1.5*R),0.0,1.0); }\n"
    "void main(){\n"
    " float ea,da,eb,db,ec,dc; vec2 sa2,sb2,sc2; node(ua,ea,da,sa2);\n"
    " vec2 pos; vt=0.0; vpt=vec4(0.0);\n"
    " if(umode==0){\n"
    "  node(ub,eb,db,sb2); node(uc,ec,dc,sc2);\n"
    "  pos = mi.x<0.5?sa2:(mi.x<1.5?sb2:sc2);\n"
    "  float e=(ea+eb+ec)/3.0, d=(da+db+dc)/3.0; float al=(0.012+0.30*e*e)*(0.25+0.75*d);\n"
    "  vcol=vec4(mix(ramp(e),vec3(1.0),0.25), al<0.01?0.0:al);\n"
    " } else if(umode==1){\n"
    "  node(ub,eb,db,sb2); float e=0.5*(ea+eb), d=0.5*(da+db); vec2 dir=normalize(sb2-sa2+vec2(1e-5)); vec2 pp=vec2(-dir.y,dir.x);\n"
    "  float w=0.55+0.5*d; pos=sa2+pp*mi.x*w*2.2; vt=mi.x;\n"
    "  vcol=vec4(mix(ramp(e),vec3(1.0),0.45), (0.10+0.50*d)*(0.55+0.9*e));\n"
    " } else {\n"
    "  pos=sa2; float e=ea, d=da; float rr=0.9+1.5*d+1.6*e; float sg=3.0+5.0*e; float ag=e>0.12?0.30*e*(0.3+0.7*d):0.0;\n"
    "  float ext=max(rr+1.0,3.2*sg*(ag>0.0?1.0:0.0)); gl_PointSize=2.0*ext;\n"
    "  vcol=vec4(mix(ramp(e),vec3(1.0),0.55),1.0); vpt=vec4(rr,sg,0.25+0.70*d,ag); vt=ext;\n"
    " }\n"
    " gl_Position=vec4(pos.x/res.x*2.0-1.0, 1.0-pos.y/res.y*2.0, 0.0, 1.0); }\n";
static const char *OB_FS =
    GLSL_VERSION
    "in vec4 vcol; in float vt; in vec4 vpt; out vec4 o; uniform int umode;\n"
    "void main(){\n"
    " if(umode==2){ vec2 q=(gl_PointCoord-vec2(0.5))*2.0*vt; float dd=length(q);\n"
    "  float core=clamp(0.5-(dd-vpt.x),0.0,1.0)*vpt.z; float glow=vpt.w*exp(-dd*dd/(2.0*vpt.y*vpt.y));\n"
    "  o=vec4(vcol.rgb, core+glow); }\n"
    " else if(umode==1){ o=vec4(vcol.rgb, vcol.a*exp(-vt*vt*3.0)); }\n"
    " else { o=vcol; } }\n";

static GLuint ob_prog, ob_vao[3], ob_vbo[3];
static GLint ob_ures, ob_ugeo, ob_uanim, ob_ubs, ob_uc0, ob_uc1, ob_uc2, ob_umode;
static int ob_n[3];

static GLuint ob_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "orb shader: %s\n", log); }
    return s;
}
static void ob_gpu_init(void) {
    GLuint vs = ob_shader(GL_VERTEX_SHADER, OB_VS), fs = ob_shader(GL_FRAGMENT_SHADER, OB_FS);
    ob_prog = glCreateProgram(); glAttachShader(ob_prog, vs); glAttachShader(ob_prog, fs); glLinkProgram(ob_prog);
    GLint ok = 0; glGetProgramiv(ob_prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(ob_prog, sizeof log, NULL, log); fprintf(stderr, "orb link: %s\n", log); }
    glDeleteShader(vs); glDeleteShader(fs);
    ob_ures = glGetUniformLocation(ob_prog, "res"); ob_ugeo = glGetUniformLocation(ob_prog, "geo"); ob_uanim = glGetUniformLocation(ob_prog, "anim");
    ob_ubs = glGetUniformLocation(ob_prog, "bs"); ob_uc0 = glGetUniformLocation(ob_prog, "c0"); ob_uc1 = glGetUniformLocation(ob_prog, "c1");
    ob_uc2 = glGetUniformLocation(ob_prog, "c2"); ob_umode = glGetUniformLocation(ob_prog, "umode");
    typedef struct { float a[3], b[3], c[3], m[2]; } OV;            /* own node, the other nodes of the face / edge, small per-vertex value */
    OV *v[3]; int n[3];
    v[0] = (OV *)calloc((size_t)ob_nf * 3, sizeof(OV)); n[0] = 0;
    for (int f = 0; f < ob_nf; f++) for (int k = 0; k < 3; k++) {
        OV *o = &v[0][n[0]++];
        for (int j = 0; j < 3; j++) { o->a[j] = ob_u[ob_f[f][k]][j]; o->b[j] = ob_u[ob_f[f][(k + 1) % 3]][j]; o->c[j] = ob_u[ob_f[f][(k + 2) % 3]][j]; }
        o->m[0] = (float)k;
    }
    v[1] = (OV *)calloc((size_t)ob_ne * 6, sizeof(OV)); n[1] = 0;
    for (int e = 0; e < ob_ne; e++) {
        static const int corner[6][2] = { {0, 1}, {0, -1}, {1, 1}, {1, 1}, {0, -1}, {1, -1} };      /* (end, side): two triangles of the quad */
        for (int k = 0; k < 6; k++) {
            int self = ob_e[e][corner[k][0]], other = ob_e[e][1 - corner[k][0]];
            OV *o = &v[1][n[1]++];
            for (int j = 0; j < 3; j++) { o->a[j] = ob_u[self][j]; o->b[j] = ob_u[other][j]; }
            o->m[0] = (float)corner[k][1];
        }
    }
    v[2] = (OV *)calloc((size_t)ob_nv, sizeof(OV)); n[2] = 0;
    for (int i = 0; i < ob_nv; i++) { OV *o = &v[2][n[2]++]; for (int j = 0; j < 3; j++) o->a[j] = ob_u[i][j]; }
    glGenVertexArrays(3, ob_vao); glGenBuffers(3, ob_vbo);
    for (int p = 0; p < 3; p++) {
        ob_n[p] = n[p];
        glBindVertexArray(ob_vao[p]);
        glBindBuffer(GL_ARRAY_BUFFER, ob_vbo[p]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(OV) * (size_t)n[p]), v[p], GL_STATIC_DRAW);
        free(v[p]);
        for (int l = 0; l < 4; l++) {
            glEnableVertexAttribArray((GLuint)l);
            glVertexAttribPointer((GLuint)l, l < 3 ? 3 : 2, GL_FLOAT, 0, sizeof(OV), (const void *)(sizeof(float) * (l < 3 ? 3 * l : 9)));
        }
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
}
/* draws the orb: centre cx, cy and radius R in logical pixels, spin angle, time, beat, the 64 smoothed band levels */
static void ob_gpu_draw(float cx, float cy, float R, float spin, float t, float beat, const float *bs) {
    if (!ob_prog) ob_gpu_init();
    flush();
    glUseProgram(ob_prog);
    glUniform2f(ob_ures, view_w, view_h);
    glUniform4f(ob_ugeo, cx, cy, R, 0);
    glUniform4f(ob_uanim, spin, t, beat, 0);
    glUniform1fv(ob_ubs, 64, bs);
    glUniform3f(ob_uc0, VC[0].r, VC[0].g, VC[0].b); glUniform3f(ob_uc1, VC[1].r, VC[1].g, VC[1].b); glUniform3f(ob_uc2, VC[2].r, VC[2].g, VC[2].b);
#ifndef __EMSCRIPTEN__
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
    for (int p = 0; p < 3; p++) {
        glUniform1i(ob_umode, p);
        glBindVertexArray(ob_vao[p]);
        glDrawArrays(p == 2 ? GL_POINTS : GL_TRIANGLES, 0, ob_n[p]);
    }
    glUseProgram(prog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
}
