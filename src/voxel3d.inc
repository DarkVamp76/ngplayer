/* VOXEL view on the GPU (OpenGL 3.3 / WebGL2): every cube of the field is an instance. The vertex shader builds it from gl_InstanceID and
 * gl_VertexID (row, band, height in the stack), looks the band's level up in a uniform array and moves cubes above the level out of the
 * picture. A depth buffer sorts the faces; the result goes into an off-screen texture that the 2D renderer shows. The CPU only smooths
 * the 64 levels and issues one draw call. GPL-3.0-or-later */
static const char *VX_VS =
    GLSL_VERSION
    "uniform vec4 cam; uniform vec4 geo; uniform float lv[64]; uniform vec3 c0, c1, c2, fogc, bg0;\n"
    "out vec3 vcol;\n"
    "const int FC[24] = int[24](0,1,2,3, 4,5,6,7, 0,3,7,4, 1,2,6,5, 0,1,5,4, 3,2,6,7);\n"
    "const int QI[6] = int[6](0,1,2,0,2,3);\n"
    "const vec3 CORN[8] = vec3[8](vec3(0,0,0),vec3(1,0,0),vec3(1,1,0),vec3(0,1,0),vec3(0,0,1),vec3(1,0,1),vec3(1,1,1),vec3(0,1,1));\n"
    "void main(){\n"
    " int id=gl_InstanceID; int k=id%15; int b=(id/15)%64; int r=id/(15*64);\n"
    " int n=int(lv[b]*15.0+0.5);\n"
    " if(k>=n){ gl_Position=vec4(2.0,2.0,2.0,1.0); vcol=vec3(0.0); return; }\n"
    " int face=gl_VertexID/6; vec3 q=CORN[FC[face*4+QI[gl_VertexID%6]]];\n"
    " float CUBE=geo.z, SEAM=geo.w, Z0=geo.x;\n"
    " float wx=(float(b)-32.0)+0.08+q.x*CUBE, wy=float(k)+SEAM+q.y*(1.0-2.0*SEAM), z=Z0+float(r)+q.z*CUBE;\n"
    " float zn=Z0-2.0, zf=Z0+geo.y+4.0;\n"
    " float nx=wx/z*cam.x, ny=cam.z+(cam.w-wy)/z*cam.y;\n"
    " gl_Position=vec4(nx*z, ny*z, (zf+zn)/(zf-zn)*z-2.0*zf*zn/(zf-zn), z);\n"
    " float u=float(b)/63.0; vec3 lo=u<0.5?mix(c0,c1,u*2.0):mix(c1,c2,(u-0.5)*2.0);\n"
    " float fr=float(r)/geo.y; float fg=fr*fr*0.85; vec3 col;\n"
    " if(face==5) col=mix(mix(lo,vec3(1.0),0.35),fogc,fg);\n"
    " else if(face==0) col=mix(mix(lo,vec3(1.0),0.10),fogc,fg*0.9);\n"
    " else col=mix(mix(lo,bg0,0.45),fogc,fg);\n"
    " vcol=col; }\n";
static const char *VX_FS = GLSL_VERSION "in vec3 vcol; out vec4 o; void main(){ o=vec4(vcol,1.0); }\n";

static GLuint vx_prog, vx_vao, vx_fbo, vx_color, vx_depth;
static int vx_w, vx_h;
static GLint vx_cam, vx_geo, vx_ulv, vx_c0, vx_c1, vx_c2, vx_fog, vx_bg0;
static int g_vpw, g_vph;                                   /* size of the window's framebuffer, set once per frame */
static int g_vpx, g_vpy;                                   /* its offset in the window (iOS: the safe area, below the notch and the home bar) */

static GLuint vx_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "voxel shader: %s\n", log); }
    return s;
}

/* draws the cube field for the region aw x ah (logical px) into the off-screen texture and returns it; the camera is the one of the old 2D version:
 * screen x = cx + wx / z * fx, screen y = hor + (CAMY - wy) / z * fy */
static GLuint vx_render(float aw, float ah, float fx, float fy, float horn, float camy, float z0, int nseg, float cube, float seam,
                        const float *lv, col c0, col c1, col c2, col fogc, col bg0) {
    if (!vx_prog) {
        GLuint vs = vx_shader(GL_VERTEX_SHADER, VX_VS), fs = vx_shader(GL_FRAGMENT_SHADER, VX_FS);
        vx_prog = glCreateProgram(); glAttachShader(vx_prog, vs); glAttachShader(vx_prog, fs); glLinkProgram(vx_prog);
        GLint ok = 0; glGetProgramiv(vx_prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(vx_prog, sizeof log, NULL, log); fprintf(stderr, "voxel link: %s\n", log); }
        glDeleteShader(vs); glDeleteShader(fs);
        vx_cam = glGetUniformLocation(vx_prog, "cam"); vx_geo = glGetUniformLocation(vx_prog, "geo"); vx_ulv = glGetUniformLocation(vx_prog, "lv");
        vx_c0 = glGetUniformLocation(vx_prog, "c0"); vx_c1 = glGetUniformLocation(vx_prog, "c1"); vx_c2 = glGetUniformLocation(vx_prog, "c2");
        vx_fog = glGetUniformLocation(vx_prog, "fogc"); vx_bg0 = glGetUniformLocation(vx_prog, "bg0");
        glGenVertexArrays(1, &vx_vao);
    }
    float rs = g_mobile ? 1.0f : 1.5f;                      /* render scale against the pixels shown: the phone does not oversample */
    int w = (int)(aw * view_scale * rs), h = (int)(ah * view_scale * rs);
    if (w > 2048) { h = h * 2048 / w; w = 2048; }
    if (w < 64) w = 64; if (h < 32) h = 32;
    if (w != vx_w || h != vx_h) {
        if (!vx_fbo) { glGenTextures(1, &vx_color); glGenRenderbuffers(1, &vx_depth); glGenFramebuffers(1, &vx_fbo); }
        glBindTexture(GL_TEXTURE_2D, vx_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, vx_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, vx_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vx_color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, vx_depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "voxel: incomplete framebuffer\n");
        vx_w = w; vx_h = h;
    }
    flush();                                                 /* the pending 2D vertices go first */
    glBindFramebuffer(GL_FRAMEBUFFER, vx_fbo);
    glViewport(0, 0, vx_w, vx_h);
    glClearColor(fogc.r, fogc.g, fogc.b, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glUseProgram(vx_prog);
    glUniform4f(vx_cam, 2.0f * fx / aw, 2.0f * fy / ah, horn, camy);
    glUniform4f(vx_geo, z0, (float)nseg, cube, seam);
    glUniform1fv(vx_ulv, 64, lv);
    glUniform3f(vx_c0, c0.r, c0.g, c0.b); glUniform3f(vx_c1, c1.r, c1.g, c1.b); glUniform3f(vx_c2, c2.r, c2.g, c2.b);
    glUniform3f(vx_fog, fogc.r, fogc.g, fogc.b); glUniform3f(vx_bg0, bg0.r, bg0.g, bg0.b);
    glBindVertexArray(vx_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, nseg * 64 * 15);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fb0);
    glBindTexture(GL_TEXTURE_2D, vx_color);
    glGenerateMipmap(GL_TEXTURE_2D);
    glViewport(g_vpx, g_vpy, g_vpw, g_vph);                  /* back to the 2D pass */
    glUseProgram(prog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindTexture(GL_TEXTURE_2D, fonttex);
    return vx_color;
}
