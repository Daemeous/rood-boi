/* No-op hardware-rasterizer and GTE-log hooks for gpu_probe, which tests the
 * CPU rasterizer in boot_runtime.c without the OpenGL path. The draw walk's commands to the renderer (hw_cmd.h) are no-ops too: nothing
 * is recorded, so nothing is pending, and a triangle is never "covered" (the CPU rasterizer draws it). */
#include "../src/hw_cmd.h"
int pc_hwc_pending, pc_hwc_stale_pending;
struct pc_hwc_mirror_t pc_hwc_mirror;
void pc_hw_meter_walk(int enter) { (void)enter; }
int pc_gte_widescreen; /* (the flag the stream carries to the renderer: never changes here) */
int pc_render_thread;
void pc_hwc_sync_widescreen(void) {}
void pc_hwc_post(void) {}
void pc_hwc_frame_menu_model(int on) { (void)on; }
void pc_hwc_flush(void) {}
int pc_hwc_triangle(const int* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip, int mask_set, int mask_check)
{
    (void)xy, (void)uv, (void)rgb, (void)code, (void)tpage, (void)clut, (void)window, (void)clip, (void)mask_set, (void)mask_check;
    return 0;
}
void pc_hwc_set_2d(int is_2d) { (void)is_2d; }
void pc_hwc_upload_mode(int mode) { (void)mode; }
void pc_hwc_sync_suppress(void) {}
extern void pc_stream_apply(int op, unsigned tag, const int* v, int n);
extern void pc_stream_apply_words(const unsigned* p, unsigned n);
void pc_hwc_stream(int op, unsigned tag, const int* v, int n) { pc_stream_apply(op, tag, v, n); }
void pc_hwc_stream_words(const unsigned* p, unsigned n) { pc_stream_apply_words(p, n); }
void pc_hwc_begin_ot(void) {}
void pc_hwc_end_ot(void) {}
void pc_hwc_set_narrowed(void) {}
void pc_hwc_set_precise(const float* xy6) { (void)xy6; }
void pc_hwc_set_depth(const int* depth3) { (void)depth3; }
void pc_hwc_set_packet(const void* packet) { (void)packet; }
void pc_hwc_set_flipped_uv(const int* xy6, const int* uv6) { (void)xy6, (void)uv6; }
void pc_hwc_set_attr(const pc_hw_attr* attr, int screen_x, int screen_w) { (void)attr, (void)screen_x, (void)screen_w; }
void pc_hwc_native_marker(unsigned list, int draw_x, int draw_y, const int* clip4, unsigned window, int mask_set)
{
    (void)list, (void)draw_x, (void)draw_y, (void)clip4, (void)window, (void)mask_set;
}
void pc_hwc_native_translucent(unsigned list, int first, int count, int draw_x, int draw_y, const int* clip4, unsigned window,
    int mask_set)
{
    (void)list, (void)first, (void)count, (void)draw_x, (void)draw_y, (void)clip4, (void)window, (void)mask_set;
}
void pc_hwc_ot_native_area(const int* clip4) { (void)clip4; }
void pc_hwc_rect(int x, int y, int w, int h, int u, int v, unsigned color, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip4, int mask_set, int mask_check)
{
    (void)x, (void)y, (void)w, (void)h, (void)u, (void)v, (void)color, (void)code, (void)tpage, (void)clut, (void)window, (void)clip4;
    (void)mask_set, (void)mask_check;
}
void pc_hwc_line(int x0, int y0, int x1, int y1, unsigned ca, unsigned cb, int semi, unsigned tpage, const int* clip4, int mask_set)
{
    (void)x0, (void)y0, (void)x1, (void)y1, (void)ca, (void)cb, (void)semi, (void)tpage, (void)clip4, (void)mask_set;
}
void pc_hwc_mark_raster(int x0, int y0, int x1, int y1) { (void)x0, (void)y0, (void)x1, (void)y1; }
void pc_hwc_raster_post(void) {}
void pc_hwc_mark(int x0, int y0, int x1, int y1) { (void)x0, (void)y0, (void)x1, (void)y1; }
void pc_hwc_before_write(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
void pc_hwc_stale_copy(int sx, int sy, int dx, int dy, int w, int h) { (void)sx, (void)sy, (void)dx, (void)dy, (void)w, (void)h; }
void pc_hwc_fence_store(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
void pc_hwc_fence_sync(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
void pc_hwc_fence_texture(unsigned tpage, unsigned clut, unsigned window, int u0, int v0, int u1, int v1, const int* clip4)
{
    (void)tpage, (void)clut, (void)window, (void)u0, (void)v0, (void)u1, (void)v1, (void)clip4;
}
int pc_hwc_text_quad(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip, int mask_set, int mask_check)
{
    (void)xy, (void)uv, (void)rgb, (void)code, (void)tpage, (void)clut, (void)window, (void)clip, (void)mask_set, (void)mask_check;
    return 0;
}
void pc_hwc_text_piece_done(void) {}
void pc_hw_mark(int x0, int y0, int x1, int y1) { (void)x0, (void)y0, (void)x1, (void)y1; }
void pc_hw_mark_raster(int x0, int y0, int x1, int y1) { (void)x0, (void)y0, (void)x1, (void)y1; }
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
void pc_hwc_vram_fill(int x, int y, int w, int h, int r, int g, int b)
{
    (void)x, (void)y, (void)w, (void)h, (void)r, (void)g, (void)b;
}
void pc_hwc_vram_upload(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
void pc_hwc_vram_copy(int sx, int sy, int dx, int dy, int w, int h, int mask_set, int mask_check)
{
    (void)sx, (void)sy, (void)dx, (void)dy, (void)w, (void)h, (void)mask_set, (void)mask_check;
}
void pc_hw_set_2d(int is_2d) { (void)is_2d; }
void pc_hw_set_bounds(int x0, int y0, int x1, int y1, unsigned code, unsigned tpage, unsigned clut, const int* uv, const int* clip)
{
    (void)x0, (void)y0, (void)x1, (void)y1, (void)code, (void)tpage, (void)clut, (void)uv, (void)clip;
}
void pc_hw_set_depth(const int* depth) { (void)depth; }
int pc_gte_projected(int sx, int sy) { return (void)sx, (void)sy, 0; }
int pc_gte_projected_depth(int sx, int sy, int* sz) { return (void)sx, (void)sy, (void)sz, 0; }
void pc_hw_set_packet(const void* p) { (void)p; }
void pc_hw_set_precise(const float* xy) { (void)xy; }
void pc_hw_set_flipped_uv(const int* xy, const int* uv) { (void)xy, (void)uv; }
int pc_scene_projection(float* h, float* ofx, float* ofy) { return (void)h, (void)ofx, (void)ofy, 0; }
int pc_scene_game_projection(float* h, float* ofx, float* ofy) { return (void)h, (void)ofx, (void)ofy, 0; }
void* vs_scratch[4]; /* the packet cursor and ordering-table base (146C.c in the game) */
void pc_hwc_set_bounds(int x0, int y0, int x1, int y1, unsigned code, unsigned tpage, unsigned clut, const int* uv4, const int* clip4)
{
    (void)x0, (void)y0, (void)x1, (void)y1, (void)code, (void)tpage, (void)clut, (void)uv4, (void)clip4;
}
