/* ORB view on the GPU (gfx.h): the geodesic mesh sits once in vertex buffers (faces, edges as soft quads, nodes as points); the vertex
 * shader moves every node with the 64 band levels (the same maths as before), projects it into the 2D renderer's pixel space and colours it; the
 * CPU only smooths the levels and issues three draw calls instead of building and submitting about 3000 triangles per frame. Additive blending, so
 * no depth buffer is needed. GPL-3.0-or-later */
static int ob_ready;
/* the three vertex sets of the mesh go to the backend once */
static void ob_gpu_init(void) {
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
    const float *vp3[3] = { (const float *)v[0], (const float *)v[1], (const float *)v[2] };
    gfx_orb_init(vp3, n);
    for (int p = 0; p < 3; p++) free(v[p]);
    ob_ready = 1;
}
/* draws the orb: centre cx, cy and radius R in logical pixels, spin angle, time, beat, the 64 smoothed band levels */
static void ob_gpu_draw(float cx, float cy, float R, float spin, float t, float beat, const float *bs) {
    if (!ob_ready) ob_gpu_init();
    flush();
    float c[9] = { VC[0].r, VC[0].g, VC[0].b, VC[1].r, VC[1].g, VC[1].b, VC[2].r, VC[2].g, VC[2].b };
    gfx_orb_draw(cx, cy, R, spin, t, beat, bs, c);
}
