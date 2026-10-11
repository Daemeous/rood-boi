/* No-op hardware-rasterizer and GTE-log hooks for gpu_probe, which tests the
 * CPU rasterizer in boot_runtime.c without the OpenGL path. */
void pc_hw_mark(int x0, int y0, int x1, int y1) { (void)x0, (void)y0, (void)x1, (void)y1; }
int pc_hw_triangle(const int* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage,
    unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check)
{
    (void)xy, (void)uv, (void)rgb, (void)code, (void)tpage, (void)clut, (void)window, (void)clip;
    (void)mask_set, (void)mask_check;
    return 0;
}
void pc_hw_rect(int x, int y, int w, int h, int u, int v, unsigned rgb, unsigned code,
    unsigned tpage, unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check)
{
    (void)x, (void)y, (void)w, (void)h, (void)u, (void)v, (void)rgb, (void)code, (void)tpage;
    (void)clut, (void)window, (void)clip, (void)mask_set, (void)mask_check;
}
void pc_hw_line(int x0, int y0, int x1, int y1, unsigned c0, unsigned c1, int semi, unsigned tpage,
    const int* clip, int mask_set)
{
    (void)x0, (void)y0, (void)x1, (void)y1, (void)c0, (void)c1, (void)semi, (void)tpage;
    (void)clip, (void)mask_set;
}
void pc_hw_fill(int x, int y, int w, int h, int r, int g, int b)
{
    (void)x, (void)y, (void)w, (void)h, (void)r, (void)g, (void)b;
}
void pc_hw_upload(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
void pc_hw_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    (void)sx, (void)sy, (void)dx, (void)dy, (void)w, (void)h;
}
void pc_hw_set_2d(int is_2d) { (void)is_2d; }
void pc_hw_set_depth(const int* depth) { (void)depth; }
int pc_gte_projected(int sx, int sy) { return (void)sx, (void)sy, 0; }
int pc_gte_projected_depth(int sx, int sy, int* sz) { return (void)sx, (void)sy, (void)sz, 0; }
void pc_hw_set_room(int room) { (void)room; }
void pc_hw_set_packet(const void* p) { (void)p; }
int pc_hw_is_room_packet(const void* p) { return (void)p, 0; }
void pc_hw_set_precise(const float* xy) { (void)xy; }
int pc_gte_projected_precise(int sx, int sy, int* sz, float* fx, float* fy)
{
    return (void)sx, (void)sy, (void)sz, (void)fx, (void)fy, 0;
}
int pc_gte_lookup_view[3];
int pc_gte_log = 1;
int pc_scene_projection(float* h, float* ofx, float* ofy) { return (void)h, (void)ofx, (void)ofy, 0; }
int pc_scene_game_projection(float* h, float* ofx, float* ofy) { return (void)h, (void)ofx, (void)ofy, 0; }
int pc_gte_projected_candidates(int sx, int sy, int* slots, unsigned* sequences, int max)
{
    return (void)sx, (void)sy, (void)slots, (void)sequences, (void)max, 0;
}
void pc_gte_projection_entry(int s, int sx, int sy, int* sz, float* fx, float* fy)
{
    (void)s, (void)sx, (void)sy, (void)sz, (void)fx, (void)fy;
}
const void* pc_gte_projection_caller(int s) { return (void)s, (const void*)0; }
const void* pc_hw_caller;
