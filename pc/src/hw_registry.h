/* The producer-owned registrations of an ordering table (hw_registry.c). */
#ifndef PC_HW_REGISTRY_H
#define PC_HW_REGISTRY_H
#include <stdint.h>
#include "gpu_attr.h"
typedef struct {
    uintptr_t start, end;
    uint32_t list;
    int covered;
} effect_range;
typedef struct {
    uintptr_t packet;
    int length; /* x1 - x0 of the line it was made for */
    float top[2], bottom[2];
} span_shape;

/* The table starts: what was registered while it was built is the active set (called at DrawOTag). */
void pc_hwr_activate(void);
void pc_hwr_forget_pending(void); /* a restored state does not own what the abandoned timeline registered */
const effect_range* pc_hwr_effect(int index); /* NULL: none */
void pc_hwr_cover_effects(uint32_t list);
/* The producer's side of a table's start (the registrations, the bubbles' frame, the suppress flag), and of the native list
 * markers it records (the coverage assumed). */
void pc_hwr_begin_ot(void);
void pc_hwr_marker(uint32_t list, const int* clip);
void pc_hwr_translucent(uint32_t list);
const span_shape* pc_hwr_span_shape_of(uintptr_t packet);
/* The stale areas (T1 R4e): where the renderer's target holds what native VRAM lacks. Two copies run the same marks: the renderer's (read-backs,
 * own copies) and the producer's (what the CPU rasterizer may leave out: raster_skip). Kinds: STALE_MISSING (native VRAM lacks what a native
 * draw made), STALE_PLACED (the target's picture is placed for the view). */
#define PC_MAX_STALE 8
enum { PC_STALE_MISSING = 1, PC_STALE_PLACED = 2 };
/* Two lists, one a kind: what native VRAM lacks (MISSING: a native draw made it; the producer keeps this list too) and where the target's picture
 * is placed for the view (PLACED: the renderer's own marks, which the producer never needs). The marks of the first all come from the
 * records' own inputs, so both copies run them in the same order and stay equal; the second can be marked by the renderer's internal
 * work (side-paint clearing, a widescreen upload, a display change) without anything having to be told. */
typedef struct {
    int rects[PC_MAX_STALE][4], count;         /* MISSING: x, y, w, h */
    int placed[PC_MAX_STALE][4], placed_count; /* PLACED: x, y, w, h */
    int ot[PC_MAX_STALE][4], ot_count, in_ot;  /* the table being drawn's own MISSING marks: x0, y0, x1, y1 */
} pc_stale_set;
void pc_stale_mark(pc_stale_set* set, const int* clip, int kind);
int pc_stale_covers(const pc_stale_set* set, const int* clip);
/* Entry i of a list (0 MISSING, 1 PLACED) was read back as [x0, x1) x [y0, y1): the table's marks it covered go, and the entry. */
void pc_stale_remove_entry(pc_stale_set* set, int list, int i, int x0, int y0, int x1, int y1);
/* A read-back of the rectangle (x, y, w, h) was made by the renderer: the producer's copy forgets the MISSING entry that is that rectangle. */
void pc_stale_readback(pc_stale_set* set, int x, int y, int w, int h);
void pc_stale_copy(pc_stale_set* set, int sx, int sy, int dx, int dy, int w, int h);
extern pc_stale_set pc_hwr_stale; /* the producer's */
void pc_hwr_end_ot(void);
int pc_hwr_texture_stale(unsigned tpage, unsigned clut, unsigned window, int u0, int v0, int u1, int v1, const int* clip, int widescreen);
#endif
