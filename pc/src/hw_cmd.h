/* The draw walk's commands to the hardware renderer (T1 plan R1, pc/docs/plans/2026-11-12-t1-renderer-thread.md).
 *
 * The walk (boot_runtime.c, boot_polygons.inc, boot_lines.inc, ui_bubbles.c) is the producer: it appends a record for each
 * renderer call that carries its whole description and needs no answer, instead of calling the hook. The renderer is the
 * consumer: it applies the records, in order, by calling the hooks (gpu_hw.c), at the first of
 *   - any other hook of the renderer (every pc_hw_* hook begins with PC_HWC_SYNC through PC_HW_METER),
 *   - the end of a walk (pc_draw_packet, pc_draw_item_packet),
 *   - a full buffer.
 * Today the consumer runs inline on the game thread, so the split is invisible: the renderer sees the same calls in the same
 * order with the same arguments (pointer arguments are copied into the record). When the consumer becomes a thread the
 * buffer is what it reads.
 *
 * The records so far: the primitive state setters (pc_hw_set_2d, _narrowed, _precise, _depth, _packet, _flipped_uv,
 * _bounds) the draw calls without an answer (_rect, _line) and the walk's native-list marks (pc_hw_anchor_packet, _native_marker, _native_translucent, _ot_native_area). */
#ifndef ROOD_HW_CMD_H
#define ROOD_HW_CMD_H
#include <stdint.h>
#include "gpu_attr.h"
#include "hw_registry.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The primitive state the walk has set (what the setter records will make the renderer's), as the producer side reads it. */
extern struct pc_hwc_mirror_t {
    int is_2d, cur_effect, poly_effect; /* the current packet's effect range (pc_hwc_set_attr), and its value at the last set_packet */
    int widescreen, suppress; /* (widescreen: the flag the renderer last got, pc_gte_widescreen as of the last record) */
} pc_hwc_mirror;
/* A triangle's treatment, decided on the producer side (gpu_hw.c pc_hw_triangle_decide): ignore it (the renderer is off), drop
 * the pending depth and precise positions (left out, or suppressed), or draw it with this classification. */
enum { PC_HW_TRI_IGNORE, PC_HW_TRI_RESET, PC_HW_TRI_DRAW };
typedef struct {
    int skip;   /* 1: a native draw covers it and the CPU rasterizer may leave it out */
    int action; /* PC_HW_TRI_* */
    int ui, other;
    int suppress;   /* the suppress flag it was decided with (the digest takes it) */
    int mark;       /* it marked the drawing area stale (the renderer marks its own set when it applies the decision) */
    unsigned stats; /* bits: the packet counters the renderer adds when it applies the triangle (gpu_hw.c) */
} pc_hw_tri_decision;
void pc_hw_triangle_decide(const int* uv, unsigned code, unsigned tpage, const int* clip, int is_2d, int effect, pc_stale_set* stale,
    int suppress, pc_hw_tri_decision* out);
void pc_hw_raster_apply(const int (*rects)[4], int count, const uint16_t* pixels);
void pc_hw_triangle_apply(const int* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut,
    unsigned window, const int* clip, int mask_set, int mask_check, const pc_hw_tri_decision* decision);
/* A triangle: decided now, queued for the renderer; returns the decision's skip (the CPU rasterizer's answer). */
int pc_hwc_triangle(const int* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip, int mask_set, int mask_check);

extern int pc_hwc_pending; /* bytes recorded and not yet applied */
void pc_hwc_flush(void);
void pc_hwc_post(void); /* hand the recorded batch to the renderer without waiting for it */
/* A stale_copy record is waiting: pc_hw_raster_skip (the producer's own query, which does not apply the records) reads the table it
 * changes, so it applies them first when this is set. */
extern int pc_hwc_stale_pending;
/* The first thing a hook does: apply what the walk recorded before it. */
extern int pc_gte_widescreen;
extern int pc_hw_suppress;
void pc_hwc_sync_widescreen(void);
void pc_hwc_sync_suppress(void);
extern int pc_render_thread;
#define PC_HWC_SYNC() ((void)(pc_gte_widescreen != pc_hwc_mirror.widescreen ? (pc_hwc_sync_widescreen(), 0) : 0), (void)(pc_hw_suppress != pc_hwc_mirror.suppress ? (pc_hwc_sync_suppress(), 0) : 0), (void)(pc_hwc_pending || pc_render_thread >= 2 ? (pc_hwc_flush(), 0) : 0))
/* The same, only when a stale_copy record is waiting (the producer's own queries). */
#define PC_HWC_SYNC_STALE() ((void)(pc_hwc_stale_pending || pc_render_thread >= 2 ? (pc_hwc_flush(), 0) : 0))

void pc_hwc_frame_menu_model(int on);
void pc_hwc_set_2d(int is_2d);
void pc_hwc_set_narrowed(void);
void pc_hwc_set_precise(const float* xy6);
void pc_hwc_set_depth(const int* depth3);
void pc_hwc_set_packet(const void* packet);
void pc_hwc_set_flipped_uv(const int* xy6, const int* uv6);
/* The walk's marks for the native lists: anchor, native list at a marker packet, translucent run, the drawing area a table's native
 * draws leave stale. */
/* The packet about to be drawn, with its attributes by value (gpu_attr.h; resolved when its table was adopted): the renderer's
 * set_attr, and the producer's own copy for the decisions that need them. */
void pc_hwc_set_attr(const pc_hw_attr* attr, int screen_x, int screen_w);
void pc_hwc_native_marker(unsigned list, int draw_x, int draw_y, const int* clip4, unsigned window, int mask_set);
void pc_hwc_native_translucent(unsigned list, int first, int count, int draw_x, int draw_y, const int* clip4, unsigned window,
    int mask_set);
void pc_hwc_ot_native_area(const int* clip4);
/* The draw calls that return nothing: a sprite or tile rectangle (the packet it came from, pc_gpu_command, travels with it) and
 * a line. */
void pc_hwc_rect(int x, int y, int w, int h, int u, int v, unsigned color, unsigned code, unsigned tpage, unsigned clut,
    unsigned window, const int* clip4, int mask_set, int mask_check);
void pc_hwc_line(int x0, int y0, int x1, int y1, unsigned ca, unsigned cb, int semi, unsigned tpage, const int* clip4,
    int mask_set);
/* The CPU rasterizer's output (T5 S3b): the rectangles a primitive draws are noted (mark_raster) and posted before the next hook
 * (raster_post). The pending list is the producer's; a post carries the rectangles' pixels as the game's VRAM array holds them
 * at that moment, so the renderer needs nothing of the game's memory. pc_hwc_mark is the dirty mark of a rectangle written by
 * anything else (LoadImage, a copy, a fill). */
void pc_hwc_mark_raster(int x0, int y0, int x1, int y1);
void pc_hwc_raster_post(void);
void pc_hwc_mark(int x0, int y0, int x1, int y1);
void pc_hwc_begin_ot(void);
void pc_hwc_end_ot(void);
void pc_hwc_upload_mode(int mode); /* what the next LoadImage is (widescreen.h PC_UPLOAD_AS) */
/* The VRAM hooks (T1 R1b): what the game's VRAM commands tell the renderer. before_write (what the write about to happen will replace,
 * for the write log) and stale_copy (a VRAM copy carries staleness) are records. The two read-backs are fences: they apply every
 * record, then run on the renderer and return when the rectangle's pixels are in the game's VRAM array (the game continues with
 * them): sync_store (StoreImage) and sync_texture (a sprite or polygon about to sample what only the GPU target holds). */
/* The VRAM events (T1 R4e): fill, upload (LoadImage) and copy carry the rectangle they leave in the game's array, so the renderer applies
 * them without reading the game's memory. Call them where pc_hw_fill, pc_hw_upload and pc_hw_copy were called. */
void pc_hwc_vram_fill(int x, int y, int w, int h, int r, int g, int b);
void pc_hwc_vram_upload(int x, int y, int w, int h);
void pc_hwc_vram_copy(int sx, int sy, int dx, int dy, int w, int h);
void pc_hwc_before_write(int x, int y, int w, int h);
void pc_hwc_stale_copy(int sx, int sy, int dx, int dy, int w, int h);
void pc_hwc_fence_store(int x, int y, int w, int h);
void pc_hwc_fence_sync(int x, int y, int w, int h); /* a capture: the rectangle of the display read back */
/* The fences of the snapshot, rewind, photo and reload paths (renderer_rq.h): pc_hwc_drain returns when every record is applied and the
 * GPU is idle; pc_hwc_resync rebuilds the renderer's state from the game's VRAM after a restore. */
void pc_hwc_drain(void);
void pc_hwc_resync(void);
void pc_hwc_fence_texture(unsigned tpage, unsigned clut, unsigned window, int u0, int v0, int u1, int v1, const int* clip4);
/* A scaled glyph cell (ui_text layout): 1 when the layout takes it. The producer decides; the piece and its end are records. The
 * caller draws the quad's triangles, then calls pc_hwc_text_piece_done when it got 1. */
int pc_hwc_text_quad(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip, int mask_set, int mask_check);
void pc_hwc_text_piece_done(void);
/* The emissive matcher's inputs from the game side (T1 R4d). emissive.cpp is the renderer's: it matches the textures the walk's
 * meshes use, against the renderer's VRAM copy, once per frame. What it needs from the game arrives as records, in order with
 * the draws: the mesh uses (copied: the mesh may be destroyed before the renderer applies the note), a mesh's end, a mesh's
 * kind, and the room the frame is drawn in (the frame value its scopes follow). */
void pc_hwc_emissive_note(uint32_t mesh, const void* polygons, int count, int override, unsigned tpage, unsigned clut);
void pc_hwc_emissive_mesh_gone(uint32_t mesh);
void pc_hwc_emissive_room_mesh(uint32_t mesh, unsigned offset, unsigned tris, unsigned quads);
void pc_hwc_frame_room(void);
/* uv4 may be NULL (an untextured rectangle). */
void pc_hwc_sync_widescreen(void); /* a record when pc_gte_widescreen changed (the header fill calls it) */
void pc_hwc_set_bounds(int x0, int y0, int x1, int y1, unsigned code, unsigned tpage, const int* uv4, const int* clip4);

#ifdef __cplusplus
}
#endif
#endif
