/* SPDX-License-Identifier: GPL-3.0-or-later */
/* gfx_gl.c - the OpenGL backend of gfx.h: OpenGL 3.3 core on the desktop, OpenGL ES 3 / WebGL 2 in the browser and on iOS. All GL code of ngPlayer is here:
 * the batched 2D renderer and the shaders of the orb, the scopes, the voxel field and the 3D sound card. */
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== OpenGL loader */
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(__EMSCRIPTEN__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define NG_GLES 1                                  /* OpenGL ES 3: the browser (WebGL 2) and iOS; the desktop uses its own loader for GL 3.3 core */
#endif
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
static int gl_load(void) { return 0; }
#define GLSL_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#elif defined(NG_GLES)
#include <OpenGLES/ES3/gl.h>                       /* iOS (deprecated by Apple, still works) */
static int gl_load(void) { return 0; }
#define GLSL_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define GLSL_VERSION "#version 330 core\n"

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
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
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
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_DEPTH_TEST 0x0B71
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_RGB 0x1907
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_STATIC_DRAW 0x88E4
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_MAX_SAMPLES 0x8D57
#define GL_RGBA8 0x8058
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE0 0x84C0
#define GL_CULL_FACE 0x0B44
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_R32F 0x822E
#define GL_RGBA32F 0x8814
#define GL_NEAREST 0x2600
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_POINTS 0

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
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint *)) \
    X(void, glBindVertexArray, (GLuint)) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) \
    X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(void, glEnableVertexAttribArray, (GLuint)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, unsigned char, GLsizei, const void *)) \
    X(void, glGenerateMipmap, (GLenum)) \
    X(void, glDisable, (GLenum)) \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, glDepthFunc, (GLenum)) \
    X(void, glActiveTexture, (GLenum)) \
    X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *)) \
    X(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei)) \
    X(void, glVertexAttribDivisor, (GLuint, GLuint)) \
    X(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glUniform1fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, glUniformMatrix4fv, (GLint, GLsizei, unsigned char, const GLfloat *)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *)) \
    X(void, glBindFramebuffer, (GLenum, GLuint)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, glGenRenderbuffers, (GLsizei, GLuint *)) \
    X(void, glBindRenderbuffer, (GLenum, GLuint)) \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, glGetIntegerv, (GLenum, GLint *)) \
    X(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLuint, GLenum)) \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, glDeleteTextures, (GLsizei, const GLuint *)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum))

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

#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
int ios_gl_buffers(SDL_Window *win, unsigned *fb, unsigned *cb);                   /* ios_glue.m: the window's framebuffer and colour renderbuffer */
#endif

/* ================================================================== shaders */
static const char *VS =
    GLSL_VERSION
    "layout(location=0) in vec2 pos; layout(location=1) in vec2 uv; layout(location=2) in vec4 col;"
    "layout(location=3) in vec4 par;\n"
    "uniform vec2 res; out vec2 vuv; out vec4 vcol; out vec4 vpar;\n"
    "void main(){ vuv=uv; vcol=col; vpar=par;"
    " gl_Position=vec4(pos.x/res.x*2.0-1.0, 1.0-pos.y/res.y*2.0, 0.0, 1.0); }\n";

static const char *FS =
    GLSL_VERSION
    "in vec2 vuv; in vec4 vcol; in vec4 vpar; out vec4 o;\n"
    "uniform sampler2D tex; uniform float pxs; uniform vec4 rip[16]; uniform float rt;\n"
    "float sdbox(vec2 p, vec2 h, float r){ vec2 q=abs(p)-h+vec2(r); return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r; }\n"
    "void main(){\n"
    " int m=int(vpar.x+0.5);\n"
    " if(m==0){ o=vcol; }\n"
    " else if(m==1){ float a=texture(tex,vuv).r; o=vec4(vcol.rgb,vcol.a*a); }\n"
    " else if(m==2){ float d=sdbox(vuv,vpar.yz,vpar.w); o=vec4(vcol.rgb,vcol.a*clamp(0.5-d*pxs,0.0,1.0)); }\n"
    " else if(m==3){ float t=abs(vuv.y); o=vec4(vcol.rgb,vcol.a*exp(-t*t*vpar.y)); }\n"
    " else if(m==7){ float d0=sdbox(vuv,vpar.yz,vpar.w); float mk=clamp(0.5-d0*pxs,0.0,1.0);\n"
    "  float s=0.0, e=0.0;\n"
    "  for(int i=0;i<16;i++){ vec4 r=rip[i]; if(r.z>0.001){ vec2 q=(vuv-r.xy)*vec2(1.0,1.5); float d=length(q);\n"
    "   s+=sin(d*0.62-rt*4.0+r.w)*exp(-d*0.05)*r.z; e+=exp(-d*d*0.02)*r.z; } }\n"
    "  float v=clamp(0.5+0.55*s,0.0,1.0);\n"
    "  vec3 c=vec3(0.015,0.03,0.24)+vec3(0.08,0.26,0.70)*v*v+vec3(0.25,0.45,0.55)*clamp(e*0.30,0.0,1.0)*v;\n"
    "  float sl=0.84+0.16*step(1.0,mod(floor(vuv.y),2.0));\n"
    "  o=vec4(c*sl*vcol.rgb,vcol.a*mk); }\n"
    " else if(m==8){ vec4 t=texture(tex,vuv); o=vec4(t.rgb*vcol.rgb,t.a*vcol.a); }\n"
    " else if(m==5){ float d=sdbox(vuv,vpar.yz,vpar.w); float a=clamp(1.0-d/vpar.w,0.0,1.0); o=vec4(vcol.rgb,vcol.a*a*a); }\n"
    " else { o=vec4(vcol.rgb,vcol.a*exp(-dot(vuv,vuv)/(2.0*vpar.y*vpar.y))); }\n"
    "}\n";
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

/* ================================================================== state */
struct gfx_tex { GLuint id, fbo, depth; int w, h, flags; GLuint msfbo, mscol, msdep; };     /* ms*: the multisampled buffers of a GFX_MSAA target (resolved into the texture) */
struct gfx_mesh { GLuint vao, vbo, ebo; int n; };

static SDL_GLContext ctx;
static GLuint fb0, cb0;                      /* the framebuffer of the window: 0, except on iOS where SDL draws into its own framebuffer */
static GLuint prog, vao, vbo;                /* the 2D renderer */
static GLint u_res, u_pxs, u_tex, u_rip, u_rt;
static int add_blend, vsync_ok;
static float res_w = 1, res_h = 1;
static int vp[4];                            /* viewport of the 2D pass in the window */
static gfx_tex *cur2d;                       /* the texture of the 2D draws */

static GLuint compile(GLenum type, const char *src, const char *what) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "%s shader: %s\n", what, log); }
    return s;
}
static GLuint link_prog(const char *vsrc, const char *fsrc, const char *what) {
    GLuint vs = compile(GL_VERTEX_SHADER, vsrc, what), fs = compile(GL_FRAGMENT_SHADER, fsrc, what);
    GLuint p = glCreateProgram(); glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, sizeof log, NULL, log); fprintf(stderr, "%s link: %s\n", what, log); }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}
/* back to the 2D renderer after a 3D pass: its program, vertex array, buffer and texture */
static void bind_2d(void) {
    glUseProgram(prog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur2d ? cur2d->id : 0);
}

/* ================================================================== window and frame */
int gfx_target_v_flipped(void) { return 1; }
Uint32 gfx_window_flags(void) { return SDL_WINDOW_OPENGL; }
void gfx_window_attrs(void) {
#ifdef NG_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
}
int gfx_vsync(void) { return vsync_ok; }
void gfx_drawable_size(SDL_Window *win, int *w, int *h) { SDL_GL_GetDrawableSize(win, w, h); }

int gfx_init(SDL_Window *win, int max_vtx) {
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "OpenGL-3.3-Kontext: %s\n", SDL_GetError()); return -1; }
    if (gl_load()) return -1;
#if defined(__APPLE__) && TARGET_OS_IPHONE
    { unsigned fb = 0, cb = 0; if (!ios_gl_buffers(win, &fb, &cb)) { fb0 = fb; cb0 = cb; } }      /* iOS: the window's framebuffer is not 0 */
#endif
    vsync_ok = SDL_GL_SetSwapInterval(-1) == 0 || SDL_GL_SetSwapInterval(1) == 0;
    prog = link_prog(VS, FS, "2D");
    u_res = glGetUniformLocation(prog, "res");
    u_pxs = glGetUniformLocation(prog, "pxs");
    u_tex = glGetUniformLocation(prog, "tex");
    u_rip = glGetUniformLocation(prog, "rip");
    u_rt = glGetUniformLocation(prog, "rt");
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(gfx_vtx) * (size_t)max_vtx), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(gfx_vtx), (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(gfx_vtx), (void *)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(gfx_vtx), (void *)16);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, 0, sizeof(gfx_vtx), (void *)32);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return 0;
}
void gfx_shutdown(void) { if (ctx) SDL_GL_DeleteContext(ctx); ctx = NULL; }

void gfx_frame_begin(int fw, int fh, int vx, int vy, int vw, int vh, float r, float g, float b) {
    glBindFramebuffer(GL_FRAMEBUFFER, fb0);
    glViewport(0, 0, fw, fh);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vx, vy, vw, vh);
    vp[0] = vx; vp[1] = vy; vp[2] = vw; vp[3] = vh;
}
void gfx_frame_end(SDL_Window *win) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    glBindFramebuffer(GL_FRAMEBUFFER, fb0); glBindRenderbuffer(GL_RENDERBUFFER, cb0);      /* what SDL expects at the swap */
#endif
    SDL_GL_SwapWindow(win);
}
void gfx_read_pixels(int w, int h, unsigned char *rgba) {
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

/* ================================================================== 2D */
void gfx_set_params(float w, float h, float pxs) {
    res_w = w; res_h = h;
    glUseProgram(prog);
    glUniform2f(u_res, w, h);
    glUniform1f(u_pxs, pxs);
    glUniform1i(u_tex, 0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
}
void gfx_set_texture(gfx_tex *t) {
    cur2d = t;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t ? t->id : 0);
}
void gfx_set_additive(int on) {
    add_blend = on;
    glBlendFunc(GL_SRC_ALPHA, on ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
}
void gfx_set_ripples(const float *src, float t) {
    glUniform4fv(u_rip, 16, src);
    glUniform1f(u_rt, t);
}
void gfx_draw(const gfx_vtx *v, int n) {
    if (n <= 0) return;
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((size_t)n * sizeof(gfx_vtx)), v);
    glDrawArrays(GL_TRIANGLES, 0, n);
}

/* ================================================================== textures and render targets */
gfx_tex *gfx_tex_create(int w, int h, int fmt, const void *px, int flags) {
    gfx_tex *t = (gfx_tex *)calloc(1, sizeof *t);
    t->w = w; t->h = h; t->flags = flags;
    GLint ifmt = GL_RGBA; GLenum f = GL_RGBA, ty = GL_UNSIGNED_BYTE;
    if (fmt == GFX_R8) { ifmt = GL_R8; f = GL_RED; }
    else if (fmt == GFX_RGB565) { ifmt = GL_RGB; f = GL_RGB; ty = GL_UNSIGNED_SHORT_5_6_5; }
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, f, ty, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (flags & GFX_MIPS) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (px && (flags & GFX_MIPS)) glGenerateMipmap(GL_TEXTURE_2D);
    if (flags & GFX_TARGET) {
        GLint ms = 0;
        if ((flags & GFX_MSAA) && (flags & GFX_DEPTH)) { glGetIntegerv(GL_MAX_SAMPLES, &ms); if (ms > GFX_MSAA_SAMPLES) ms = GFX_MSAA_SAMPLES; }       /* no multisampling on a device without it: the target is drawn as before */
        if (ms >= 2) {
            glGenRenderbuffers(1, &t->mscol); glBindRenderbuffer(GL_RENDERBUFFER, t->mscol); glRenderbufferStorageMultisample(GL_RENDERBUFFER, ms, GL_RGBA8, w, h);
            glGenRenderbuffers(1, &t->msdep); glBindRenderbuffer(GL_RENDERBUFFER, t->msdep); glRenderbufferStorageMultisample(GL_RENDERBUFFER, ms, GL_DEPTH_COMPONENT24, w, h);
            glGenFramebuffers(1, &t->msfbo); glBindFramebuffer(GL_FRAMEBUFFER, t->msfbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, t->mscol);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->msdep);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "render target: incomplete multisample framebuffer\n"); glDeleteFramebuffers(1, &t->msfbo); glDeleteRenderbuffers(1, &t->mscol); glDeleteRenderbuffers(1, &t->msdep); t->msfbo = t->mscol = t->msdep = 0; ms = 0; }
        }
        if ((flags & GFX_DEPTH) && ms < 2) {
            glGenRenderbuffers(1, &t->depth);
            glBindRenderbuffer(GL_RENDERBUFFER, t->depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        }
        glGenFramebuffers(1, &t->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->id, 0);
        if (t->depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "render target: incomplete framebuffer\n");
        glBindFramebuffer(GL_FRAMEBUFFER, fb0);
    }
    glBindTexture(GL_TEXTURE_2D, cur2d ? cur2d->id : 0);
    return t;
}
void gfx_tex_free(gfx_tex *t) {
    if (!t) return;
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->depth) glDeleteRenderbuffers(1, &t->depth);
    if (t->msfbo) glDeleteFramebuffers(1, &t->msfbo);
    if (t->mscol) glDeleteRenderbuffers(1, &t->mscol);
    if (t->msdep) glDeleteRenderbuffers(1, &t->msdep);
    glDeleteTextures(1, &t->id);
    if (cur2d == t) cur2d = NULL;
    free(t);
}
int gfx_target_begin(gfx_tex *t, float r, float g, float b, float a) {
    if (!t || !t->fbo) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    return 1;
}
void gfx_target_end(gfx_tex *t) {
    glBindFramebuffer(GL_FRAMEBUFFER, fb0);
    if (t && (t->flags & GFX_MIPS)) { glBindTexture(GL_TEXTURE_2D, t->id); glGenerateMipmap(GL_TEXTURE_2D); }
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    bind_2d();
}

/* ================================================================== orb */
static GLuint ob_prog, ob_vao[3], ob_vbo[3];
static GLint ob_ures, ob_ugeo, ob_uanim, ob_ubs, ob_uc0, ob_uc1, ob_uc2, ob_umode;
static int ob_n[3];
void gfx_orb_init(const float *v[3], const int n[3]) {
    ob_prog = link_prog(OB_VS, OB_FS, "orb");
    ob_ures = glGetUniformLocation(ob_prog, "res"); ob_ugeo = glGetUniformLocation(ob_prog, "geo"); ob_uanim = glGetUniformLocation(ob_prog, "anim");
    ob_ubs = glGetUniformLocation(ob_prog, "bs"); ob_uc0 = glGetUniformLocation(ob_prog, "c0"); ob_uc1 = glGetUniformLocation(ob_prog, "c1");
    ob_uc2 = glGetUniformLocation(ob_prog, "c2"); ob_umode = glGetUniformLocation(ob_prog, "umode");
    const GLsizei stride = 11 * sizeof(float);                     /* own node, the other nodes of the face / edge, small per-vertex value */
    glGenVertexArrays(3, ob_vao); glGenBuffers(3, ob_vbo);
    for (int p = 0; p < 3; p++) {
        ob_n[p] = n[p];
        glBindVertexArray(ob_vao[p]);
        glBindBuffer(GL_ARRAY_BUFFER, ob_vbo[p]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)stride * (size_t)n[p]), v[p], GL_STATIC_DRAW);
        for (int l = 0; l < 4; l++) {
            glEnableVertexAttribArray((GLuint)l);
            glVertexAttribPointer((GLuint)l, l < 3 ? 3 : 2, GL_FLOAT, 0, stride, (const void *)(sizeof(float) * (l < 3 ? 3 * l : 9)));
        }
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
}
void gfx_orb_draw(float cx, float cy, float R, float spin, float t, float beat, const float *bs, const float *col) {
    if (!ob_prog) return;
    glUseProgram(ob_prog);
    glUniform2f(ob_ures, res_w, res_h);
    glUniform4f(ob_ugeo, cx, cy, R, 0);
    glUniform4f(ob_uanim, spin, t, beat, 0);
    glUniform1fv(ob_ubs, 64, bs);
    glUniform3f(ob_uc0, col[0], col[1], col[2]); glUniform3f(ob_uc1, col[3], col[4], col[5]); glUniform3f(ob_uc2, col[6], col[7], col[8]);
#ifndef NG_GLES
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
    for (int p = 0; p < 3; p++) {
        glUniform1i(ob_umode, p);
        glBindVertexArray(ob_vao[p]);
        glDrawArrays(p == 2 ? GL_POINTS : GL_TRIANGLES, 0, ob_n[p]);
    }
    bind_2d();
}

/* ================================================================== scopes */
#define SC_NP 96
#define SC_MAXT 64
static GLuint sc_prog, sc_vao, sc_tv, sc_ti;
static GLint sc_ures, sc_uvals, sc_uinfo, sc_upass, sc_uc0, sc_uc1, sc_uc2, sc_ulw;
void gfx_scope_draw(const float *vals, const float *info, int nt, float lw, const float *col) {
    if (!nt) return;
    if (!sc_prog) {
        sc_prog = link_prog(SC_VS, SC_FS, "scope");
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
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, sc_tv);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SC_NP, nt, GL_RED, GL_FLOAT, vals);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, sc_ti);
    for (int t = 0; t < nt; t++) { glTexSubImage2D(GL_TEXTURE_2D, 0, t, 0, 1, 1, GL_RGBA, GL_FLOAT, info + 8 * t); glTexSubImage2D(GL_TEXTURE_2D, 0, t, 1, 1, 1, GL_RGBA, GL_FLOAT, info + 8 * t + 4); }
    glUseProgram(sc_prog);
    glUniform2f(sc_ures, res_w, res_h); glUniform1i(sc_uvals, 1); glUniform1i(sc_uinfo, 2); glUniform1f(sc_ulw, lw);
    glUniform3f(sc_uc0, col[0], col[1], col[2]); glUniform3f(sc_uc1, col[3], col[4], col[5]); glUniform3f(sc_uc2, col[6], col[7], col[8]);
    glBindVertexArray(sc_vao);
    for (int p = 0; p < 2; p++) {
        glBlendFunc(GL_SRC_ALPHA, p == 0 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);      /* the glow adds, the core line blends */
        glUniform1i(sc_upass, p);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, nt * (SC_NP - 1));
    }
    glActiveTexture(GL_TEXTURE0);
    bind_2d();
    gfx_set_additive(0);
}

/* ================================================================== voxel */
static GLuint vx_prog, vx_vao;
static gfx_tex *vx_tex;
static GLint vx_cam, vx_geo, vx_ulv, vx_c0, vx_c1, vx_c2, vx_fog, vx_bg0;
gfx_tex *gfx_voxel_render(int w, int h, const float *cam, const float *geo, const float *lv, const float *c) {
    if (!vx_prog) {
        vx_prog = link_prog(VX_VS, VX_FS, "voxel");
        vx_cam = glGetUniformLocation(vx_prog, "cam"); vx_geo = glGetUniformLocation(vx_prog, "geo"); vx_ulv = glGetUniformLocation(vx_prog, "lv");
        vx_c0 = glGetUniformLocation(vx_prog, "c0"); vx_c1 = glGetUniformLocation(vx_prog, "c1"); vx_c2 = glGetUniformLocation(vx_prog, "c2");
        vx_fog = glGetUniformLocation(vx_prog, "fogc"); vx_bg0 = glGetUniformLocation(vx_prog, "bg0");
        glGenVertexArrays(1, &vx_vao);
    }
    if (!vx_tex || vx_tex->w != w || vx_tex->h != h) {
        gfx_tex_free(vx_tex);
        vx_tex = gfx_tex_create(w, h, GFX_RGBA8, NULL, GFX_MIPS | GFX_TARGET | GFX_DEPTH);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, vx_tex->fbo);
    glViewport(0, 0, w, h);
    glClearColor(c[9], c[10], c[11], 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glUseProgram(vx_prog);
    glUniform4f(vx_cam, cam[0], cam[1], cam[2], cam[3]);
    glUniform4f(vx_geo, geo[0], geo[1], geo[2], geo[3]);
    glUniform1fv(vx_ulv, 64, lv);
    glUniform3f(vx_c0, c[0], c[1], c[2]); glUniform3f(vx_c1, c[3], c[4], c[5]); glUniform3f(vx_c2, c[6], c[7], c[8]);
    glUniform3f(vx_fog, c[9], c[10], c[11]); glUniform3f(vx_bg0, c[12], c[13], c[14]);
    glBindVertexArray(vx_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, (int)geo[1] * 64 * 15);
    glDisable(GL_DEPTH_TEST);
    gfx_target_end(vx_tex);
    return vx_tex;
}

/* ================================================================== 3D card */
static GLuint c3_prog;
static GLint c3_mvp, c3_mv, c3_umode, c3_utex;
gfx_mesh *gfx_mesh_create(const void *verts, int nverts, const uint16_t *idx, int nidx) {
    typedef struct { float x, y, z, u, v; unsigned char r, g, b, a; } V3;
    gfx_mesh *m = (gfx_mesh *)calloc(1, sizeof *m);
    m->n = nidx;
    glGenVertexArrays(1, &m->vao); glGenBuffers(1, &m->vbo); glGenBuffers(1, &m->ebo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(V3) * (size_t)nverts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint16_t) * (size_t)nidx), idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(V3), (const void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(V3), (const void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, sizeof(V3), (const void *)(5 * sizeof(float)));
    glBindVertexArray(0);
    return m;
}
void gfx_mesh_update_verts(gfx_mesh *m, const void *verts, int nverts) {
    typedef struct { float x, y, z, u, v; unsigned char r, g, b, a; } V3;
    if (!m || !m->vbo || nverts <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(V3) * (size_t)nverts), verts); glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void gfx_card_render(gfx_tex *target, const float *mvp, const float *mv, gfx_mesh *board, gfx_mesh *front, gfx_tex *front_tex, gfx_mesh *back, gfx_tex *back_tex) {
    if (!c3_prog) {
        c3_prog = link_prog(C3_VS, C3_FS, "card3d");
        c3_mvp = glGetUniformLocation(c3_prog, "mvp"); c3_mv = glGetUniformLocation(c3_prog, "mv");
        c3_umode = glGetUniformLocation(c3_prog, "umode"); c3_utex = glGetUniformLocation(c3_prog, "tex");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target->msfbo ? target->msfbo : target->fbo);       /* a GFX_MSAA target: draw into the multisampled buffers, resolve below */
    glViewport(0, 0, target->w, target->h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glUseProgram(c3_prog);
    glUniformMatrix4fv(c3_mvp, 1, 0, mvp); glUniformMatrix4fv(c3_mv, 1, 0, mv);
    glUniform1i(c3_utex, 0);
    glBindVertexArray(board->vao); glUniform1i(c3_umode, 0);
    glDrawElements(GL_TRIANGLES, board->n, GL_UNSIGNED_SHORT, (const void *)0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, front_tex->id);
    glBindVertexArray(front->vao); glUniform1i(c3_umode, 1);
    glDrawElements(GL_TRIANGLES, front->n, GL_UNSIGNED_SHORT, (const void *)0);
    glBindTexture(GL_TEXTURE_2D, back_tex->id);
    glBindVertexArray(back->vao);
    glDrawElements(GL_TRIANGLES, back->n, GL_UNSIGNED_SHORT, (const void *)0);
    glDisable(GL_DEPTH_TEST);
    if (target->msfbo) {                                                                  /* resolve the samples into the texture (same size, so the blit only averages) */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target->msfbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->fbo);
        glBlitFramebuffer(0, 0, target->w, target->h, 0, 0, target->w, target->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    gfx_target_end(target);
}

/* Apple TV helpers: the OpenGL backend has no use for them */
void  gfx_native_size(SDL_Window *win, int *w, int *h) { SDL_GL_GetDrawableSize(win, w, h); }
int   gfx_refresh_hz(SDL_Window *win) { (void)win; return 60; }
void  gfx_set_render_scale(float s) { (void)s; }
float gfx_last_gpu_ms(void) { return 0; }
