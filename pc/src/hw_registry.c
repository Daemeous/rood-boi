/* The registrations of an ordering table (T1 R4e): the packet ranges the game's code records while it builds a table (the room, a native
 * room list, an object, an effects list, an anchor, an opacity, a scanline shape) and the table that becomes active at DrawOTag. They
 * are the producer's: the walk's code registers them and reads them back (pc_hw_resolve gives each packet its attributes by value), and the
 * renderer is told what it needs in the records, never by looking in these tables. */
#include "hw_registry.h"
#include "render.h"
#include "hw_meter.h"
#include "gpu_attr.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Widescreen placement kinds of packet ranges (widescreen.h PC_ANCHOR_*, low bits). */
enum { PC_ANCHOR_NONE, PC_ANCHOR_LEFT, PC_ANCHOR_RIGHT, PC_ANCHOR_FILL, PC_ANCHOR_SCENE, PC_ANCHOR_COVER, PC_ANCHOR_PROJECTED, PC_ANCHOR_SLIDE };
extern int pc_hw_active;
extern int pc_hw_suppress;
extern void pc_ui_bubbles_frame(void) __attribute__((weak));
int pc_hw_layout_enabled(void); /* gpu_hw.c: the widescreen layout fixes are on */
int pc_hw_scene_enabled(void);  /* gpu_hw.c: the widescreen scene anchors are on */
int pc_hw_slide_enabled(void);  /* gpu_hw.c: the menus' slide-in anchors are on (setting menu_slide) */

/* Drop shadow and effect packet ranges (effect_native.c): [start, end) of the markers and lowered items of effects list `list`
 * (0: no list took them, the items draw as they are). Recorded while an ordering table is built and made active at its DrawOTag.
 * They are covered once one of the list's translucent markers drew; the hardware path then skips them all (every polygon of an
 * effects list is native). Items in a range are world geometry, never screen furniture. */
#define MAX_EFFECT_RANGES 512
static effect_range pending_effects[MAX_EFFECT_RANGES], active_effects[MAX_EFFECT_RANGES];
static int pending_effect_count, active_effect_count, last_effect_range;
void pc_hw_effect_packets(const void* start, const void* end, uint32_t list)
{
    if (pending_effect_count < MAX_EFFECT_RANGES && end > start) {
        effect_range* r = &pending_effects[pending_effect_count++];
        r->start = (uintptr_t)start;
        r->end = (uintptr_t)end;
        r->list = list;
        r->covered = 0;
    }
}
/* The effect range holding packet `p`, or -1. */
static int effect_range_index(uintptr_t p)
{
    if (!active_effect_count)
        return -1;
    if (last_effect_range < active_effect_count) {
        effect_range* r = &active_effects[last_effect_range];
        if (p >= r->start && p < r->end)
            return last_effect_range;
    }
    for (int i = 0; i < active_effect_count; i++)
        if (p >= active_effects[i].start && p < active_effects[i].end) {
            last_effect_range = i;
            return i;
        }
    return -1;
}
void pc_hwr_cover_effects(uint32_t list)
{
    for (int i = 0; i < active_effect_count; i++)
        if (active_effects[i].list == list)
            active_effects[i].covered = 1;
}
const effect_range* pc_hwr_effect(int index) { return index >= 0 && index < active_effect_count ? &active_effects[index] : NULL; }

#define MAX_ANCHOR_RANGES 32
typedef struct {
    uintptr_t start, end;
    int anchor;
    int exact; /* PC_ANCHOR_EXACT: placed by data, no heuristics */
} anchor_range;
static anchor_range pending_anchors[MAX_ANCHOR_RANGES], active_anchors[MAX_ANCHOR_RANGES];
static int pending_anchor_count, active_anchor_count;

/* Set while the classic layer's parity gate draws into its shadow (pc/src/hud/hud_bridge.c): those packets are
 * given back and the space reused, so they are anchored nowhere. */
int pc_hw_anchor_muted, pc_hw_anchor_muted_as = -1; /* (muted: what the range would have recorded) */
int pc_hw_anchor_recorded(int anchor);
extern void pc_frame_anchor_markers(const void* start, const void* end, int anchor) __attribute__((weak));
void pc_hw_anchor_packets(const void* start, const void* end, int anchor)
{
    if (pc_hw_anchor_muted) {
        if (end > start)
            pc_hw_anchor_muted_as = pc_hw_anchor_recorded(anchor);
        return;
    }
    int exact = (anchor & 0x20) != 0; /* PC_ANCHOR_EXACT */
    anchor &= ~0x20;
    /* Ranges marked 0x10 belong to the widescreen layout fixes
     * (pc/src/widescreen.h): off with them (ROOD_WIDESCREEN_LAYOUT=0). */
    if (anchor & 0x10) {
        if (!pc_hw_layout_enabled())
            return;
        anchor &= 0x0f;
    }
    if (((anchor == PC_ANCHOR_SCENE || anchor == PC_ANCHOR_COVER) && !pc_hw_scene_enabled()) || (anchor == PC_ANCHOR_SLIDE && !pc_hw_slide_enabled()))
        return;
    if (end > start && pc_frame_anchor_markers) /* (the draw items a marker in the range places are anchored as the packets were) */
        pc_frame_anchor_markers(start, end, anchor | (exact ? 0x20 : 0));
    if (pending_anchor_count < MAX_ANCHOR_RANGES && end > start) {
        anchor_range* r = &pending_anchors[pending_anchor_count++];
        r->start = (uintptr_t)start;
        r->end = (uintptr_t)end;
        r->anchor = anchor;
        r->exact = exact;
    }
}
/* The anchor a draw item in a range anchored `anchor` carries (draw_items.c pc_frame_anchor): the same filters as a range of
 * packets has, then the value the draw uses (PC_ANCHOR_*, with 0x20 for exact); -1 when the range records nothing (muted, or
 * its kind is off). An item holds it as a field, so no range of the item arena's addresses is kept. */
int pc_hw_item_anchor(int anchor)
{
    if (pc_hw_anchor_muted) {
        pc_hw_anchor_muted_as = pc_hw_anchor_recorded(anchor);
        return -1;
    }
    int exact = (anchor & 0x20) != 0;
    anchor &= ~0x20;
    if (anchor & 0x10) {
        if (!pc_hw_layout_enabled())
            return -1;
        anchor &= 0x0f;
    }
    if (((anchor == PC_ANCHOR_SCENE || anchor == PC_ANCHOR_COVER) && !pc_hw_scene_enabled()) || (anchor == PC_ANCHOR_SLIDE && !pc_hw_slide_enabled()))
        return -1;
    return anchor | (exact ? 0x20 : 0);
}
/* What a range anchored `anchor` records (pc/src/classic: a classic draw's
 * anchor, compared with the game's): 0 (centred) when it records nothing. */
int pc_hw_anchor_recorded(int anchor)
{
    anchor &= ~0x20;
    if (anchor & 0x10) {
        if (!pc_hw_layout_enabled())
            return 0;
        anchor &= 0x0f;
    }
    if (((anchor == PC_ANCHOR_SCENE || anchor == PC_ANCHOR_COVER) && !pc_hw_scene_enabled()) || (anchor == PC_ANCHOR_SLIDE && !pc_hw_slide_enabled()))
        return 0;
    return anchor;
}
/* The anchor of a packet of the ordering table being built (the innermost
 * pending range holding it; 0: none, centred): the parity gate's. */
int pc_hw_anchor_of(const void* packet)
{
    uintptr_t best = UINTPTR_MAX;
    int anchor = PC_ANCHOR_NONE;
    for (int i = 0; i < pending_anchor_count; i++) {
        const anchor_range* r = &pending_anchors[i];
        if ((uintptr_t)packet >= r->start && (uintptr_t)packet < r->end && r->end - r->start < best) {
            best = r->end - r->start;
            anchor = r->anchor;
        }
    }
    return anchor;
}
/* Opacity: packet ranges of 2D drawn at an opacity the PS1 composites on the
 * CPU (BATTLE _renderTextImmediate: a text box's text fading out, read back
 * from the frame buffer, blended and uploaded again; the port draws it as
 * glyph sprites instead, 5BF94.c ROOD_PC), recorded while an OT is built and
 * made active at its DrawOTag like the anchor ranges. Their primitives are
 * drawn "over" at that opacity (FLAG_FADE). */
#define MAX_OPACITY_RANGES 16
typedef struct {
    uintptr_t start, end;
    int opacity; /* 0..256 */
} opacity_range;
static opacity_range pending_opacities[MAX_OPACITY_RANGES], active_opacities[MAX_OPACITY_RANGES];
static int pending_opacity_count, active_opacity_count;
/* Whether 2D at an opacity is drawn as primitives (the hardware renderer;
 * setting opacity_2d 0: the game's own read-back and upload, for comparisons;
 * ROOD_OPACITY_2D, when set, wins: 0 off, anything else on). */
extern int pc_opacity_2d; /* project_settings.cpp */
int pc_hw_opacity_on(void)
{
    const char* env = getenv("ROOD_OPACITY_2D");
    int on = env && *env ? *env != '0' : pc_opacity_2d != 0;
    return pc_hw_active && on;
}
/* The packets [start, end) drawn at `opacity` (16.16: 0x10000 opaque). */
void pc_hw_opacity_packets(const void* start, const void* end, int opacity)
{
    if (pending_opacity_count < MAX_OPACITY_RANGES && end > start) {
        opacity_range* r = &pending_opacities[pending_opacity_count++];
        r->start = (uintptr_t)start;
        r->end = (uintptr_t)end;
        r->opacity = opacity <= 0 ? 0 : opacity >= 0x10000 ? 256 : opacity >> 8;
    }
}
/* Scanline fills drawn as one shape (ui_lines joined, upscaled): a fill the
 * game draws as a horizontal line per row (the area-targeting floor fill,
 * map_view.c) steps by a whole native row at its slanted edges, 4 pixels
 * and more at the internal resolution, where the projected shape it fills
 * is smooth. Its builder tells each row's packet how its ends join the
 * rows above and below (pc_hw_span_shape: the left and right ends at the
 * row's top and bottom edges, relative to the line's own ends, x0 and
 * x1 + 1, which hold at the row's middle); the row is then drawn as that
 * shape (two trapezoids) instead of a rectangle. Rows meet where their
 * neighbours' ends meet, so the fill has no gaps or overlaps (it is
 * semi-transparent) and its edges run straight between the rows' middles.
 * At 1x the lines are drawn as they are (the PS1's pixels). */
#define MAX_SPAN_SHAPES 2048
static span_shape pending_spans[MAX_SPAN_SHAPES], active_spans[MAX_SPAN_SHAPES];
static int pending_span_count, active_span_count;
void pc_hw_span_shape(const void* packet, int length, float top_left, float top_right, float bottom_left,
    float bottom_right)
{
    if (pending_span_count < MAX_SPAN_SHAPES) {
        span_shape* s = &pending_spans[pending_span_count++];
        s->packet = (uintptr_t)packet;
        s->length = length;
        s->top[0] = top_left, s->top[1] = top_right;
        s->bottom[0] = bottom_left, s->bottom[1] = bottom_right;
    }
}
static int compare_span_shapes(const void* a, const void* b)
{
    uintptr_t x = ((const span_shape*)a)->packet, y = ((const span_shape*)b)->packet;
    return x < y ? -1 : x > y;
}
const span_shape* pc_hwr_span_shape_of(uintptr_t packet)
{
    int lo = 0, hi = active_span_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (active_spans[mid].packet == packet)
            return &active_spans[mid];
        if (active_spans[mid].packet < packet)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

/* The attributes of the packet whose tag word is at `p` (guest memory for a table's packets, the lowered arena for items):
 * the effect, anchor, opacity and span ranges it lies in. */
void pc_hw_resolve(const void* p, pc_hw_attr* a)
{
    uintptr_t best = UINTPTR_MAX, at = (uintptr_t)p;
    a->effect = (short)effect_range_index(at);
    a->anchor = PC_ANCHOR_NONE;
    a->exact = 0;
    for (int i = 0; i < active_anchor_count; i++) {
        const anchor_range* r = &active_anchors[i];
        if (at >= r->start && at < r->end && r->end - r->start < best) {
            best = r->end - r->start;
            a->anchor = r->anchor;
            a->exact = (unsigned char)r->exact;
        }
    }
    a->opacity = 256;
    for (int i = 0; i < active_opacity_count; i++)
        if (at >= active_opacities[i].start && at < active_opacities[i].end)
            a->opacity = (short)active_opacities[i].opacity;
    a->span = -1;
    for (int i = 0; i < active_span_count; i++)
        if (active_spans[i].packet == at) {
            a->span = (short)i;
            /* (by value: the renderer reads the shape from the attribute, not from this table) */
            a->has_span = 1;
            a->span_shape[0] = (float)active_spans[i].length, a->span_shape[1] = active_spans[i].top[0], a->span_shape[2] = active_spans[i].top[1];
            a->span_shape[3] = active_spans[i].bottom[0], a->span_shape[4] = active_spans[i].bottom[1];
            break;
        }
}

void pc_hwr_activate(void)
{
    memcpy(active_effects, pending_effects, (size_t)pending_effect_count * sizeof *active_effects);
    active_effect_count = pending_effect_count;
    pending_effect_count = 0;
    last_effect_range = 0;
    memcpy(active_anchors, pending_anchors, sizeof active_anchors);
    active_anchor_count = pending_anchor_count;
    pending_anchor_count = 0;
    memcpy(active_opacities, pending_opacities, sizeof active_opacities);
    active_opacity_count = pending_opacity_count;
    pending_opacity_count = 0;
    memcpy(active_spans, pending_spans, (size_t)pending_span_count * sizeof *active_spans);
    active_span_count = pending_span_count;
    pending_span_count = 0;
    qsort(active_spans, (size_t)active_span_count, sizeof *active_spans, compare_span_shapes);
}
void pc_hwr_forget_pending(void) { pending_effect_count = pending_anchor_count = pending_opacity_count = pending_span_count = 0; }

/* ---- what the producer knows of the renderer's coverage (T1 R4e) ----
 * A native list's marker covers the packet ranges of its list when the renderer draws it. The producer sets the flags itself, at the marker
 * it records, assuming the draw happens by the rules the renderer's draw_native and markers go by: the list's kind and the settings
 * that allow it, and whether the room surfaces exist (a room list that drew something, or the empty ones an object list makes). The
 * renderer counts a covered marker whose draw failed anyway (hw_meter: covered_render_failed). */
#define MAX_DRAWN 256
static uint32_t drawn_p[MAX_DRAWN];
static int drawn_p_count, surface_valid_p;
static int drawn_p_has(uint32_t list)
{
    for (int i = 0; i < drawn_p_count; i++)
        if (drawn_p[i] == list)
            return 1;
    return 0;
}
void pc_hwr_begin_ot(void)
{
    pc_hwr_activate();
    surface_valid_p = 0, drawn_p_count = 0;
    pc_hwr_stale.in_ot = 1, pc_hwr_stale.ot_count = 0;
    pc_hw_suppress = 0;
    if (pc_ui_bubbles_frame)
        pc_ui_bubbles_frame();
}
void pc_hwr_marker(uint32_t list, const int* clip)
{
    if (!pc_hw_active || drawn_p_has(list))
        return;
    int objects = render_list_objects(list);
    if (objects == 1 && !surface_valid_p)
        surface_valid_p = 1; /* (the empty surfaces the object list makes) */
    if (objects < 0 || (objects && !surface_valid_p))
        return;
    if (objects == 0 && render_list_instances(list) > 0)
        surface_valid_p = 1; /* (a room list that drew something: its depth is the surfaces') */
    if (drawn_p_count < MAX_DRAWN)
        drawn_p[drawn_p_count++] = list;
    pc_stale_mark(&pc_hwr_stale, clip, PC_STALE_MISSING); /* drawn here only: native VRAM lacks it */
}
void pc_hwr_translucent(uint32_t list)
{
    if (!pc_hw_active || !surface_valid_p)
        return;
    if (!drawn_p_has(list) && render_list_objects(list) == 3) {
        pc_hwr_cover_effects(list);
        if (drawn_p_count < MAX_DRAWN)
            drawn_p[drawn_p_count++] = list;
    }
}

/* ---- the stale areas ---- */
pc_stale_set pc_hwr_stale;
static void cells_set(uint32_t* cells, int x0, int y0, int x1, int y1) /* pixels, exclusive, inside the VRAM */
{
    for (int c = x0 >> 6; c <= (x1 - 1) >> 6; c++)
        for (int b = y0 >> 4; b <= (y1 - 1) >> 4; b++)
            cells[c] |= 1u << b;
}
void pc_stale_mark(pc_stale_set* set, const int* clip, int kind)
{
    if (clip[2] <= 0 || clip[3] <= 0)
        return;
    int x0 = clip[0] < 0 ? 0 : clip[0], y0 = clip[1] < 0 ? 0 : clip[1];
    int x1 = clip[0] + clip[2] > 1024 ? 1024 : clip[0] + clip[2];
    int y1 = clip[1] + clip[3] > 512 ? 512 : clip[1] + clip[3];
    if (x1 <= x0 || y1 <= y0)
        return;
    if (kind & (PC_STALE_MISSING | PC_STALE_DRAWN)) {
        if ((kind & PC_STALE_MISSING) && set->in_ot) {
            int known = 0;
            for (int i = 0; i < set->ot_count && !known; i++) {
                const int* r = set->ot[i];
                known = x0 >= r[0] && y0 >= r[1] && x1 <= r[2] && y1 <= r[3];
            }
            if (!known && set->ot_count < PC_MAX_STALE) {
                int* r = set->ot[set->ot_count++];
                r[0] = x0, r[1] = y0, r[2] = x1, r[3] = y1;
            }
        }
        cells_set(set->missing, x0, y0, x1, y1);
    }
    if (kind & PC_STALE_PLACED)
        cells_set(set->placed, x0, y0, x1, y1);
}
int pc_stale_covers(const pc_stale_set* set, const int* clip)
{
    int x0 = clip[0] < 0 ? 0 : clip[0], y0 = clip[1] < 0 ? 0 : clip[1];
    int x1 = clip[0] + clip[2] > 1024 ? 1024 : clip[0] + clip[2];
    int y1 = clip[1] + clip[3] > 512 ? 512 : clip[1] + clip[3];
    if (!set->in_ot)
        return 0;
    if (x1 <= x0 || y1 <= y0)
        return 1; /* draws nothing */
    for (int i = 0; i < set->ot_count; i++) {
        const int* r = set->ot[i];
        if (x0 >= r[0] && y0 >= r[1] && x1 <= r[2] && y1 <= r[3])
            return 1;
    }
    return 0;
}
int pc_stale_any(const pc_stale_set* set)
{
    for (int c = 0; c < 16; c++)
        if (set->missing[c] | set->placed[c])
            return 1;
    return 0;
}
int pc_stale_cells(const uint32_t* cells)
{
    int n = 0;
    for (int c = 0; c < 16; c++)
        n += __builtin_popcount(cells[c]);
    return n;
}
int pc_stale_bbox(const uint32_t* cells, int x, int y, int w, int h, int* out)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > 1024 ? 1024 : x + w, y1 = y + h > 512 ? 512 : y + h;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    int c0 = x0 >> 6, c1 = (x1 - 1) >> 6, b0 = y0 >> 4, b1 = (y1 - 1) >> 4;
    uint32_t bands = (b1 - b0 == 31 ? ~0u : ((1u << (b1 - b0 + 1)) - 1)) << b0;
    int minc = 99, maxc = -1, minb = 99, maxb = -1;
    for (int c = c0; c <= c1; c++) {
        uint32_t bits = cells[c] & bands;
        if (!bits)
            continue;
        minc = c < minc ? c : minc, maxc = c;
        int lo = __builtin_ctz(bits), hi = 31 - __builtin_clz(bits);
        minb = lo < minb ? lo : minb, maxb = hi > maxb ? hi : maxb;
    }
    if (maxc < 0)
        return 0;
    out[0] = minc * 64, out[1] = minb * 16, out[2] = (maxc + 1) * 64, out[3] = (maxb + 1) * 16;
    return 1;
}
int pc_stale_meets(const uint32_t* cells, int x, int y, int w, int h)
{
    int box[4];
    return pc_stale_bbox(cells, x, y, w, h, box);
}
void pc_stale_clear(uint32_t* cells, int x0, int y0, int x1, int y1)
{
    for (int c = x0 >> 6; c <= (x1 - 1) >> 6 && c < 16; c++)
        for (int b = y0 >> 4; b <= (y1 - 1) >> 4 && b < 32; b++)
            cells[c] &= ~(1u << b);
}
void pc_stale_readback(pc_stale_set* set, int x, int y, int w, int h)
{
    for (int k = 0; k < set->ot_count;) {
        const int* o = set->ot[k];
        if (o[0] < x + w && x < o[2] && o[1] < y + h && y < o[3])
            memmove(set->ot[k], set->ot[k + 1], (size_t)(--set->ot_count - k) * sizeof set->ot[0]);
        else
            ++k;
    }
    pc_stale_clear(set->missing, x, y, x + w, y + h);
}
void pc_stale_copy(pc_stale_set* set, int sx, int sy, int dx, int dy, int w, int h)
{
    int clip[4] = { dx, dy, w, h };
    if (pc_stale_meets(set->missing, sx, sy, w, h))
        pc_stale_mark(set, clip, PC_STALE_MISSING);
    if (pc_stale_meets(set->placed, sx, sy, w, h))
        pc_stale_mark(set, clip, PC_STALE_PLACED);
}
void pc_hwr_end_ot(void)
{
    pc_hwr_stale.in_ot = 0, pc_hwr_stale.ot_count = 0;
}

/* Whether the texture a primitive is about to sample may need the renderer to read back (pc_hw_sync_texture): decided from the producer's stale
 * list, so the walk asks the renderer only when it can matter. It follows the renderer's own rules: nothing stale (the common case) is
 * nothing to do; a primitive whose drawing area is stale anyway takes own copies on the renderer's side; otherwise a stale area that meets
 * the texels or the CLUT is read back. The PLACED list is the renderer's alone, so with widescreen on the answer is always "ask". */
int pc_hw_own_copies_enabled(void);
int pc_hwr_texture_stale(unsigned tpage, unsigned clut, unsigned window, int u0, int v0, int u1, int v1, const int* clip, int widescreen)
{
    if (widescreen)
        return 1;
    if (!pc_stale_meets(pc_hwr_stale.missing, 0, 0, 1024, 512))
        return 0;
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256, depth = (int)((tpage >> 7) & 3);
    if ((window & 0x3ff) || u0 < 0 || v0 < 0 || u1 > 255 || v1 > 255 || u1 < u0 || v1 < v0)
        u0 = v0 = 0, u1 = v1 = 255;
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int x0 = tx + (u0 >> shift), x1 = tx + (u1 >> shift);
    if (pc_hw_own_copies_enabled())
        return 0; /* (own copies, on the renderer's side: no read-back) */
    int rects[2][4] = { { x0, ty + v0, (x1 > 1023 ? 1023 : x1) - x0 + 1, v1 - v0 + 1 }, { (int)(clut & 63) * 16, (int)((clut >> 6) & 511), depth ? 256 : 16, 1 } };
    for (int k = 0; k < (depth < 2 ? 2 : 1); k++) {
        const int* q = rects[k];
        if (q[2] <= 0 || q[3] <= 0)
            continue;
        if (pc_stale_meets(pc_hwr_stale.missing, q[0], q[1], q[2], q[3]))
            return 1;
    }
    return 0;
}

