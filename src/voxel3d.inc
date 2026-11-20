/* VOXEL view on the GPU (gfx.h): every cube of the field is an instance. The vertex shader builds it from gl_InstanceID and
 * gl_VertexID (row, band, height in the stack), looks the band's level up in a uniform array and moves cubes above the level out of the
 * picture. A depth buffer sorts the faces; the result goes into an off-screen texture that the 2D renderer shows. The CPU only smooths
 * the 64 levels and issues one draw call. GPL-3.0-or-later */
/* draws the cube field for the region aw x ah (logical px) into the off-screen texture and returns it; the camera is the one of the old 2D version:
 * screen x = cx + wx / z * fx, screen y = hor + (CAMY - wy) / z * fy */
static gfx_tex *vx_render(float aw, float ah, float fx, float fy, float horn, float camy, float z0, int nseg, float cube, float seam,
                           const float *lv, col c0, col c1, col c2, col fogc, col bg0) {
    float rs = g_mobile ? 1.0f : 1.5f;                      /* render scale against the pixels shown: the phone does not oversample */
    int w = (int)(aw * view_scale * rs), h = (int)(ah * view_scale * rs);
    if (w > 2048) { h = h * 2048 / w; w = 2048; }
    if (w < 64) w = 64; if (h < 32) h = 32;
    flush();                                                 /* the pending 2D vertices go first */
    float cam[4] = { 2.0f * fx / aw, 2.0f * fy / ah, horn, camy }, geo[4] = { z0, (float)nseg, cube, seam };
    float c[15] = { c0.r, c0.g, c0.b, c1.r, c1.g, c1.b, c2.r, c2.g, c2.b, fogc.r, fogc.g, fogc.b, bg0.r, bg0.g, bg0.b };
    return gfx_voxel_render(w, h, cam, geo, lv, c);
}
