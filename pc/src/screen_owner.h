/* A screen that owns the display (widescreen): when opaque 2D covers the whole 4:3 drawing area, what was drawn
 * before it in the table (the 3D scene, a native list) is hidden in the original, and so are the view's sides.
 * The 2D is compressed to the 4:3 part of the 16:9 view (gpu_hw.c), so it no longer covers the sides and the scene
 * shows there (the user's report: a chunk of the room beside the Game Over banner). The sides no opaque 2D of the
 * table reached are cleared when the picture completes.
 *
 * This is the bookkeeping only (which pixels opaque rectangles covered, which rows of the sides they reached, which
 * sprites are opaque); gpu_hw.c makes the fill. It reads 2D primitives' bounds, the decision point both packets and
 * draw items pass through (pc_hw_set_bounds), not the packet walk, and carries over unchanged when the walk goes:
 * a renderer that takes items calls the same functions for each rectangle item. */
#ifndef ROOD_SCREEN_OWNER_H
#define ROOD_SCREEN_OWNER_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Settings screen_owner (1: on) and screen_owner_fill (r g b, 0..255: what the cleared sides become). */
extern int pc_screen_owner;
extern int pc_screen_owner_fill[3];
/* Whether the rule is on: the setting, or ROOD_SCREEN_OWNER when set (0 or 1; it wins). */
int pc_screen_owner_on(void);
/* What the last tables did (RPC graphics: tests): completions seen, side runs cleared. */
extern long pc_screen_owner_completed, pc_screen_owner_cleared;

/* A table's start: nothing is covered. */
void pc_screen_owner_begin(void);
/* An opaque rectangle (x0, y0)-(x1, y1), exclusive ends, in the drawing area clip (x, y, w, h). 1 when, with it, the
 * whole area is covered (once for a table). */
int pc_screen_owner_cover(const int clip[4], int x0, int y0, int x1, int y1);
/* Opaque 2D reached the left or right side of the area's view in rows y0..y1 (exclusive end, VRAM rows): the sides' rows
 * it covered are left alone when they are cleared. */
void pc_screen_owner_side_drawn(const int clip[4], int right, int y0, int y1);
/* The rows of a side no opaque 2D of the table reached, as runs [first, end) of VRAM rows; how many. */
int pc_screen_owner_side_runs(const int clip[4], int right, int runs[][2], int max);

/* Whether every texel a textured rectangle samples (its page, CLUT and inclusive texel rectangle uv: u0, v0, u1, v1) is
 * drawn (not the value 0, which the GPU skips): the answer is kept per (page, CLUT, rectangle) until a write to VRAM
 * touches `tiles` (pc_screen_owner_vram_changed). `tiles` of the texels and CLUT are the caller's (gpu_hw.c tiles_for).
 * T5: reads the renderer's VRAM; it follows whatever replaces that array. */
int pc_screen_owner_texels_opaque(const uint16_t* vram, unsigned tpage, unsigned clut, const int uv[4], uint32_t tiles);
/* VRAM tiles (gpu_hw.c tiles_for) were written. */
void pc_screen_owner_vram_changed(uint32_t tiles);

#ifdef __cplusplus
}
#endif
#endif
