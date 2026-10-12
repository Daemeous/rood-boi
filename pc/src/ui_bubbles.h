/* 2D lines and talk bubbles at the internal resolution (README "2D / UI").
 *
 * ui_lines: how GPU line primitives (GP0 0x40-0x5F: menu rules, the map,
 * targeting, the talk bubbles' outlines) reach the internal resolution
 * (gpu_hw.c pc_hw_line). joined (default): one native pixel thick at any
 * scale, end pixels whole so polyline joins close, reaching the edges of
 * polygons drawn along them; legacy: the earlier one-pixel quads (gaps and
 * light dots along outlines when upscaled), for comparisons.
 *
 * ui_outline_boost: 2D lines (and vector bubble outlines) this many output
 * pixels thicker, 0-3 (a bolder inked look; 0 as the game).
 *
 * ui_bubbles: original (default) draws the talk bubbles as the game's
 * packets; vector draws each bubble the game builds this frame (its own
 * corners, edges, tail, size and zoom, BATTLE.PRG func_800CD3E4) as one
 * shape: the fill from the game's textured polygons and its paper texture
 * sampled nearest, the drop shadow, and an anti-aliased outline
 * ui_bubble_outline native pixels thick (ui_bubbles.c). */
#ifndef ROOD_UI_BUBBLES_H
#define ROOD_UI_BUBBLES_H

#ifdef __cplusplus
extern "C" {
#endif

enum { PC_UI_LINES_LEGACY = 0, PC_UI_LINES_JOINED = 1 };
enum { PC_UI_BUBBLES_ORIGINAL = 0, PC_UI_BUBBLES_VECTOR = 1 };
extern int pc_ui_lines;           /* PC_UI_LINES_* */
extern int pc_ui_outline_boost;   /* output pixels, 0-3 */
extern int pc_ui_bubbles;         /* PC_UI_BUBBLES_* */
extern float pc_ui_bubble_outline; /* vector outline thickness, native pixels (0.5-4) */
extern const char* const pc_ui_lines_names[2];
extern const char* const pc_ui_bubbles_names[2];

/* gpu_hw.c: a 2D triangle the port builds, at unrounded VRAM positions
 * (drawing offset applied), through the packet path. cover: coverage
 * blended (uv[0], uv[2], uv[4]: each vertex's coverage 0..256; untextured). */
void pc_hw_shape_triangle(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage,
    unsigned clut, unsigned window, const int* clip, int mask_set, int cover);
/* Native (VRAM) pixels per presented output pixel. */
float pc_hw_native_per_output(void);
extern long pc_hw_lines_drawn; /* line segments the hardware path drew (tests) */
/* Set while the packet being drawn is one the hardware path leaves out
 * (the CPU rasterizer still draws it): a vector bubble's original packets. */
extern int pc_hw_suppress;

/* The game (BATTLE.PRG, under ROOD_PC): text box `id`'s rectangle this
 * frame in 16.16 (func_800CB83C), and its frame's packets [start, end)
 * built from `shape` (func_800CD3E4). */
void pc_bubble_rect(int id, int x, int y, int w, int h);
void pc_bubble_frame(int id, const void* box, const void* shape, const void* start, const void* end);
/* boot_runtime.c: a GPU packet is about to be drawn (draw offset, drawing
 * area); sets pc_hw_suppress for a vector bubble's packets and draws the
 * bubble's shapes in their place. */
void pc_ui_bubble_packet(const void* packet, int draw_x, int draw_y, const int* clip);
/* A new ordering table (pc_hw_begin_ot). */
void pc_ui_bubbles_frame(void);

typedef struct {
    long frames, verified, mismatched, drawn;
    int count; /* bubbles recorded, newest last (up to 8) */
    struct {
        int id, shape, flip, tail, valid, drawn, points;
        float bounds[4];   /* outline points' bounds (screen pixels): x0, y0, x1, y1, as drawn */
        int game_bounds[4]; /* the game's whole-pixel points' bounds */
        float thickness;    /* outline thickness drawn, native pixels */
        unsigned line_rgb;
    } bubble[8];
} pc_ui_bubbles_stats;
void pc_ui_bubbles_get_stats(pc_ui_bubbles_stats* s);
/* The newest drawn bubble's outline path (screen pixels, closed), for tests:
 * up to max points; returns the count. */
int pc_ui_bubble_path(int index, float (*points)[2], int max);

#ifdef __cplusplus
}
#endif
#endif
