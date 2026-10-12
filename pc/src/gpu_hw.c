/* Upscaled hardware rasterizer (OpenGL 3.3).
 *
 * The CPU rasterizer in boot_runtime.c remains the reference: it keeps native
 * VRAM exact for StoreImage, effects and texture decoding. Every primitive,
 * fill, copy and upload is mirrored here into an RGBA8 render target of
 * (1024 x 512) * scale, which is what the window displays. Textures are always
 * decoded from native VRAM (an R16UI texture), re-uploaded per 64x256 tile only
 * when a draw samples a tile the CPU changed since the last upload. Colors
 * keep 8-bit precision (no 5-bit quantization or dithering), and the PS1's
 * semi-transparency modes map to GL blending; textured primitives draw their
 * opaque and translucent texels in separate passes, as the hardware decides
 * per texel. Mask-bit testing is not mirrored yet (reported once if used). */
#define _POSIX_C_SOURCE 200809L
#include "gl_api.h"
#include <stddef.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render_gl.h"
#include "glow_overrides.h"
#include "ui_upscale.h" /* 2D / UI texture magnification (MMPX) */
#include "ui_text.h"    /* 2D / UI: glyphs drawn from fonts (ui_text) */
#ifdef ROOD_VULKAN
#include "vk_hooks.h" /* ROOD_RENDERER=vulkan: the GL calls below go to vk_hw.c */
#endif
extern int pc_present_rate; /* below */
extern float pc_present_phase;

typedef struct {
    float x, y; /* VRAM pixels; fractional only for widescreen-compressed 2D */
    float z;    /* GTE view depth (SZ) for projected 3D vertices, 0 for 2D */
    uint8_t r, g, b, flags; /* FLAG_* */
    uint16_t u, v;
    uint16_t tpage, clut;
    uint32_t window;
    int8_t nx, ny, nz, lit; /* view-space face normal; lit = 1 for 3D surfaces */
    uint8_t uv_min[2], uv_max[2]; /* the primitive's texel bounds (filtering stays inside) */
    int16_t ui[2]; /* FLAG_UI: the magnified region's atlas origin - uv_min, in texels (ui_upscale.c) */
} hw_vertex;

/* FLAG_UI: a 2D texture region magnified into the UI atlas (pc_ui_upscale). */
/* FLAG_CLAMP: 2D, texels kept inside the primitive's [uv_min, uv_max - 1]. */
enum { FLAG_TEXTURED = 1, FLAG_RAW = 2, FLAG_SEMI = 4, FLAG_MASK = 8, FLAG_UI = 16, FLAG_CLAMP = 32 };
#define MAX_VERTICES 65536

int pc_hw_active;
int pc_hw_scale = 1;
/* Widescreen (16:9 Hor+): the GTE compresses projected X by 3/4
 * (pc_gte_widescreen); 2D primitives, which the GTE did not project, are
 * compressed here about the drawing area's centre so they keep their shape. */
extern int pc_gte_widescreen;
static int primitive_2d, primitive_room, primitive_cover;
/* Widescreen: untextured 2D reaching the screen's right (1) or left (-1)
 * edge (menu bars, title bars) stretches to that edge of the view from its
 * compressed other end (pc_hw_set_bounds); primitive_span is its x0, x1. */
static int primitive_extend;
static float primitive_span[2];
/* 2D whose vertices the GTE projected (narrowed) this frame although the
 * projection log is off (pc_hw_set_narrowed): it is not compressed again. */
static int primitive_narrowed;
static uintptr_t current_packet; /* the GPU packet being drawn (pc_hw_set_packet) */
static int primitive_uv_shift[2]; /* pc_hw_set_flipped_uv */
void pc_hw_set_2d(int is_2d)
{
    primitive_2d = is_2d;
    primitive_cover = 0;
    primitive_extend = 0;
    primitive_narrowed = 0;
    primitive_uv_shift[0] = primitive_uv_shift[1] = 0;
}
/* Widescreen with the projection log off (every 3D draw native): 2D packets
 * whose vertices all came from the GTE this frame (the map screen's rooms,
 * effects drawn as packets) are already narrowed by the projection; they
 * keep their positions instead of being compressed a second time. */
static int layout_enabled(void);
void pc_hw_set_narrowed(void) { primitive_narrowed = layout_enabled(); }
/* A flipped 2D sprite drawn as a polygon (U or V decreasing along X or Y,
 * no change along the other axis), as beetle-psx and DuckStation detect it.
 * The PS1 takes a pixel's texel at its top-left corner: a quad whose U runs
 * from 87 down to 63 over 24 pixels samples 87..64. Sampled at pixel centres
 * (as this renderer does, more finely when upscaled) it would sample 86..63,
 * one texel past the image (the neighbour in the page shows as a line at
 * the edge, or the image shifts by a texel). Shifting that coordinate by one
 * samples what the PS1 does at native scale and stays inside it upscaled.
 * Called after pc_hw_set_2d for 2D textured polygons with the packet's
 * first three vertices (xy, uv pairs); applies to the whole polygon. */
void pc_hw_set_flipped_uv(const int* xy, const int* uv)
{
    float abx = (float)(xy[2] - xy[0]), aby = (float)(xy[3] - xy[1]);
    float bcx = (float)(xy[4] - xy[2]), bcy = (float)(xy[5] - xy[3]);
    float cax = (float)(xy[0] - xy[4]), cay = (float)(xy[1] - xy[5]);
    float area = bcx * cay - bcy * cax;
    int tex_area = (uv[2] - uv[0]) * (uv[5] - uv[1]) - (uv[4] - uv[0]) * (uv[3] - uv[1]);
    primitive_uv_shift[0] = primitive_uv_shift[1] = 0;
    if (area == 0.0f || !tex_area)
        return;
    for (int k = 0; k < 2; k++) {
        /* Plane derivatives (scaled by the area; only signs and zeros matter). */
        float ddx = -aby * (float)uv[4 + k] - bcy * (float)uv[k] - cay * (float)uv[2 + k];
        float ddy = abx * (float)uv[4 + k] + bcx * (float)uv[k] + cax * (float)uv[2 + k];
        if (area < 0.0f)
            ddx = -ddx, ddy = -ddy;
        if ((ddx < 0.0f && ddy == 0.0f) || (ddy < 0.0f && ddx == 0.0f))
            primitive_uv_shift[k] = 1;
    }
}
/* Widescreen: 2D that stands for the whole screen draws uncompressed, over
 * the full 16:9 view. The game draws its full-screen effects over the 4:3
 * screen (0..320); compressed like other 2D they covered only the view's
 * central 3/4 and its sides kept the picture underneath (the old room
 * during a door fade, the unfaded scene beside a black centre). A 2D
 * primitive covers the view when it
 *
 * - spans the drawing area's full width (a few pixels' tolerance): fades,
 *   flashes, the battle-mode message bar, backdrops;
 * - is untextured and reaches well past the top or bottom: wipe quads (sized
 *   beyond the screen and rotated) and fans of gradient triangles about the
 *   centre, as a whole;
 * - is a full-height sprite at the screen's left or right edge: the pieces a
 *   full-screen picture is drawn in (the map's background, 256 + 64);
 * - is a screen copy: 15-bit texels sampling a frame buffer (a recent
 *   display area, or a VRAM copy of one) about where it draws (the map's
 *   rippling transition redraws the last frame in strips; menus dim a copy
 *   of the field). In widescreen the frame buffers are read from the target
 *   (pc_hw_set_display marks them stale), so the copy is the widescreen
 *   picture and draws 1:1.
 *
 * Called by the CPU rasterizer after pc_hw_set_2d with the primitive's bounds
 * (x1, y1 exclusive, drawing offset applied) and, textured, the texel
 * rectangle it samples (u0, v0, u1, v1 inclusive; NULL: untextured).
 * ROOD_WIDESCREEN_DEBUG=1 logs them. */
int pc_widescreen_cover = -1; /* ROOD_WIDESCREEN_COVER=0: compress them too (as before; for comparisons) */
static int cover_enabled(void)
{
    if (pc_widescreen_cover < 0)
        pc_widescreen_cover = !getenv("ROOD_WIDESCREEN_COVER") || *getenv("ROOD_WIDESCREEN_COVER") != '0';
    return pc_widescreen_cover;
}
/* Frame buffer pictures in VRAM: x, y, w, h and the VRAM position of the
 * screen's top-left corner (a copy keeps its source's). */
typedef struct {
    int rect[4], origin[2];
} screen_picture;
#define MAX_SCREEN_AREAS 2
#define MAX_SCREEN_COPIES 8
static screen_picture screen_areas[MAX_SCREEN_AREAS], screen_copies[MAX_SCREEN_COPIES];
static int screen_area_count, screen_copy_count;
static int rects_overlap(const int* a, const int* b)
{
    return a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
}
/* The picture whose pixels r covers (the larger share), or NULL. */
static const screen_picture* screen_find(const int* r)
{
    const screen_picture* best = NULL;
    int best_area = 0;
    for (int list = 0; list < 2; list++) {
        const screen_picture* p = list ? screen_copies : screen_areas;
        for (int i = 0, n = list ? screen_copy_count : screen_area_count; i < n; i++) {
            const int* q = p[i].rect;
            int w = (r[0] + r[2] < q[0] + q[2] ? r[0] + r[2] : q[0] + q[2]) - (r[0] > q[0] ? r[0] : q[0]);
            int h = (r[1] + r[3] < q[1] + q[3] ? r[1] + r[3] : q[1] + q[3]) - (r[1] > q[1] ? r[1] : q[1]);
            if (w > 0 && h > 0 && w * h > best_area)
                best = &p[i], best_area = w * h;
        }
    }
    return best;
}
/* Add to a list of `limit` pictures (the oldest goes when it is full). */
static void screen_list_add(screen_picture* list, int* count, int limit, const screen_picture* p)
{
    for (int i = 0; i < *count; i++)
        if (!memcmp(&list[i], p, sizeof *p))
            return;
    if (*count == limit) {
        memmove(list, list + 1, sizeof *list * (size_t)(limit - 1));
        --*count;
    }
    list[(*count)++] = *p;
}
static void screen_display(int x, int y, int w, int h)
{
    screen_picture p = { { x, y, w, h }, { x, y } };
    if (w > 0 && h > 0)
        screen_list_add(screen_areas, &screen_area_count, MAX_SCREEN_AREAS, &p);
}
/* A VRAM write: a copy from a frame buffer makes its destination a screen
 * copy; anything written over a screen copy ends it (uploads and fills too). */
static void screen_copy_write(int sx, int sy, int dx, int dy, int w, int h, int copy)
{
    int src[4] = { sx, sy, w, h }, dst[4] = { dx, dy, w, h };
    for (int i = 0; i < screen_copy_count;)
        if (rects_overlap(screen_copies[i].rect, dst)) {
            memmove(&screen_copies[i], &screen_copies[i + 1], sizeof *screen_copies * (size_t)(screen_copy_count - i - 1));
            --screen_copy_count;
        } else
            ++i;
    const screen_picture* from = copy ? screen_find(src) : NULL;
    if (from) {
        screen_picture p = { { dx, dy, w, h }, { from->origin[0] + dx - sx, from->origin[1] + dy - sy } };
        screen_list_add(screen_copies, &screen_copy_count, MAX_SCREEN_COPIES, &p);
    }
}
/* Widescreen layout of compressed 2D (ROOD_WIDESCREEN_LAYOUT=0 turns it off,
 * for comparisons): it is clipped to the compressed drawing area, so what
 * the game parks beyond the screen's edges (hidden by the PS1's drawing
 * area) stays hidden instead of showing in the view's sides, and untextured
 * shapes that reach one edge of the screen (menu and title bars running off
 * its right side) stretch to that edge of the view. */
int pc_widescreen_layout = -1;
static int layout_enabled(void)
{
    if (pc_widescreen_layout < 0)
        pc_widescreen_layout = !getenv("ROOD_WIDESCREEN_LAYOUT") || *getenv("ROOD_WIDESCREEN_LAYOUT") != '0';
    return pc_widescreen_layout;
}
/* RPC widescreen_trace: log every 2D primitive of the next N ordering
 * tables (bounds relative to the drawing area, how widescreen places it). */
int pc_widescreen_trace;
static int primitive_anchor;
static void trace_bounds(int x0, int y0, int x1, int y1, unsigned code, unsigned tpage, const int* uv, const int* clip)
{
    if (!pc_widescreen_trace)
        return;
    char texture[64] = "";
    if (uv)
        snprintf(texture, sizeof texture, " textured (page %d,%d depth %u, uv %d,%d-%d,%d)", (int)(tpage & 15) * 64,
            (int)((tpage >> 4) & 1) * 256, (tpage >> 7) & 3, uv[0], uv[1], uv[2], uv[3]);
    fprintf(stderr, "[widescreen trace] code 0x%02x (%d,%d)-(%d,%d) area %d,%d %dx%d%s: %s\n", code & 0xff,
        x0 - clip[0], y0 - clip[1], x1 - clip[0], y1 - clip[1], clip[0], clip[1], clip[2], clip[3], texture,
        primitive_narrowed ? "projected" : primitive_cover ? "cover"
        : primitive_anchor ? (primitive_anchor == 1 ? "anchor left" : primitive_anchor == 2 ? "anchor right" : "fill")
        : primitive_extend > 0 ? "extend right" : primitive_extend < 0 ? "extend left" : "compress");
}
void pc_hw_set_bounds(int x0, int y0, int x1, int y1, unsigned code, unsigned tpage, const int* uv, const int* clip)
{
    if (!primitive_2d || clip[2] <= 0 || clip[3] <= 0)
        return;
    if (primitive_narrowed) {
        trace_bounds(x0, y0, x1, y1, code, tpage, uv, clip);
        return;
    }
    if (layout_enabled() && !uv) {
        /* Reaching one edge (within a pixel; a bar's drop shadow runs a
         * little past it) from at least a tenth of the screen away. */
        int left = clip[0], right = clip[0] + clip[2], reach = clip[2] / 10;
        if (x1 >= right - 1 && x0 > left + 1 && right - x0 >= reach)
            primitive_extend = 1;
        else if (x0 <= left + 1 && x1 < right - 1 && x1 - left >= reach)
            primitive_extend = -1;
        primitive_span[0] = (float)x0;
        primitive_span[1] = (float)x1;
    }
    static int debug = -1, logged;
    if (debug < 0)
        debug = getenv("ROOD_WIDESCREEN_DEBUG") && *getenv("ROOD_WIDESCREEN_DEBUG") == '1';
    if (!cover_enabled()) {
        trace_bounds(x0, y0, x1, y1, code, tpage, uv, clip);
        return;
    }
    int tolerance = clip[2] / 64; /* 5 pixels at 320 */
    int wide = x0 <= clip[0] + tolerance && x1 >= clip[0] + clip[2] - tolerance;
    /* Untextured shapes reaching well past the top or bottom (wipes; fans
     * of gradient triangles about the centre) are screen effects as a whole. */
    int tall = !uv && (y0 < clip[1] - 32 || y1 > clip[1] + clip[3] + 32 || (y0 < clip[1] && y1 > clip[1] + clip[3]));
    /* Full-height sprites at the screen's left or right edge: the pieces a
     * full-screen picture is drawn in (the map's background, 256 + 64). */
    int backdrop = (code & 0xe0) == 0x60 && uv && y0 <= clip[1] + tolerance && y1 >= clip[1] + clip[3] - tolerance
        && (x0 <= clip[0] + tolerance || x1 >= clip[0] + clip[2] - tolerance) && x1 - x0 >= clip[2] / 8;
    int copy = 0;
    if (uv && ((tpage >> 7) & 3) >= 2) {
        /* 15-bit texels (depth 3 reads as 15-bit too): the sampled VRAM
         * is the texture page plus UV; a screen copy samples the screen
         * position it draws at. */
        int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
        int sampled[4] = { tx + uv[0], ty + uv[1], uv[2] - uv[0] + 1, uv[3] - uv[1] + 1 };
        const screen_picture* p = screen_find(sampled);
        if (p) {
            int dx = (sampled[0] - p->origin[0]) - (x0 - clip[0]), dy = (sampled[1] - p->origin[1]) - (y0 - clip[1]);
            copy = dx >= -40 && dx <= 40 && dy >= -16 && dy <= 16; /* (the map ripples by up to 32) */
        }
    }
    primitive_cover = wide || tall || backdrop || copy;
    if (primitive_cover)
        primitive_extend = 0;
    trace_bounds(x0, y0, x1, y1, code, tpage, uv, clip);
    /* (Debug: 2D drawn compressed although it reaches well past the screen's
     * sides, e.g. a full-screen effect in pieces.) */
    if (debug && !primitive_cover && pc_gte_widescreen && (x0 < clip[0] - 8 || x1 > clip[0] + clip[2] + 8)
        && x1 - x0 >= clip[2] / 4 && logged < 400) {
        static int seen[64][5], seen_count;
        int key[5] = { (int)code, x0 - clip[0], y0 - clip[1], x1 - clip[0], y1 - clip[1] };
        for (int i = 0; i < seen_count; i++)
            if (!memcmp(seen[i], key, sizeof key))
                return;
        memcpy(seen[seen_count], key, sizeof key);
        seen_count = (seen_count + 1) % 64;
        ++logged;
        fprintf(stderr, "[widescreen] compressed 2D code 0x%02x (%d,%d)-(%d,%d) reaches past the screen (area %d,%d %dx%d)%s\n",
            code & 0xff, key[1], key[2], key[3], key[4], clip[0], clip[1], clip[2], clip[3], uv ? " textured" : "");
    }
    if (debug && primitive_cover && logged < 400) {
        static int seen[64][5], seen_count;
        int key[5] = { (int)code, x0 - clip[0], y0 - clip[1], x1 - clip[0], y1 - clip[1] };
        for (int i = 0; i < seen_count; i++)
            if (!memcmp(seen[i], key, sizeof key))
                return;
        memcpy(seen[seen_count], key, sizeof key);
        seen_count = (seen_count + 1) % 64;
        ++logged;
        fprintf(stderr, "[widescreen] full-view 2D code 0x%02x (%d,%d)-(%d,%d) in area %d,%d %dx%d: %s\n", code & 0xff,
            key[1], key[2], key[3], key[4], clip[0], clip[1], clip[2], clip[3],
            wide ? "full width" : tall ? "past the top or bottom" : backdrop ? "full-screen picture piece" : "a copy of the screen");
    }
}
void pc_hw_set_room(int room) { primitive_room = room; }

/* Room packet ranges (func_8009723C) recorded while an OT is built; at each
 * DrawOTag the recorded set becomes the active one for that OT. */
#define MAX_ROOM_RANGES 16
static uintptr_t pending_ranges[MAX_ROOM_RANGES][2], active_ranges[MAX_ROOM_RANGES][2];
static int pending_count, active_count;
void pc_hw_room_packets(const void* start, const void* end)
{
    if (pending_count < MAX_ROOM_RANGES && end > start) {
        pending_ranges[pending_count][0] = (uintptr_t)start;
        pending_ranges[pending_count][1] = (uintptr_t)end;
        ++pending_count;
    }
}
/* Each room packet's vertex depths (SZ) and screen positions, recorded as
 * room_geometry.c writes it and keyed by its address: exact, where finding them by screen
 * position in the GTE's projection log can pick another projection to the
 * same pixel or miss one. An address is rewritten with its packet. */
#define ROOM_DEPTH_SLOTS 65536
static uintptr_t room_depth_key[ROOM_DEPTH_SLOTS];
static uint16_t room_depth_value[ROOM_DEPTH_SLOTS][4];
static uint32_t room_depth_xy[ROOM_DEPTH_SLOTS][4]; /* the screen positions they belong to */
static unsigned room_depth_slot(uintptr_t key) { return ((unsigned)(key >> 2) * 2654435761u) >> 16; }
extern int pc_gte_log;
void pc_hw_room_depths(const void* packet, const unsigned* xy, const unsigned* sz, int count)
{
    if (!pc_gte_log)
        return; /* the reconstruction is retired (pc_reconstruct) */
    uintptr_t key = (uintptr_t)packet;
    unsigned home = room_depth_slot(key), s = home;
    for (int probe = 0; probe < 8; probe++, s = (s + 1) & (ROOM_DEPTH_SLOTS - 1))
        if (room_depth_key[s] == key || !room_depth_key[s])
            break;
    if (room_depth_key[s] != key && room_depth_key[s])
        s = home; /* crowded: take the home slot */
    room_depth_key[s] = key;
    for (int i = 0; i < 4; i++) {
        room_depth_value[s][i] = (uint16_t)sz[i < count ? i : count - 1];
        room_depth_xy[s][i] = xy[i < count ? i : count - 1];
    }
}
int pc_hw_room_depth(const void* packet, const unsigned* xy, int count, int* sz)
{
    uintptr_t key = (uintptr_t)packet;
    unsigned s = room_depth_slot(key);
    for (int probe = 0; probe < 8; probe++, s = (s + 1) & (ROOM_DEPTH_SLOTS - 1)) {
        if (!room_depth_key[s])
            return 0;
        if (room_depth_key[s] == key) {
            /* Only if the packet still holds what was recorded: the address
             * may since carry another frame's packet. */
            for (int i = 0; i < count; i++)
                if (room_depth_xy[s][i] != xy[i])
                    return 0;
            for (int i = 0; i < 4; i++)
                sz[i] = room_depth_value[s][i];
            return 1;
        }
    }
    return 0;
}
/* Room packet ranges whose polygons a native draw list covers
 * (room_geometry.c with pc_native_room 1, room_native.c): the CPU's
 * reference VRAM still draws them, the hardware path does not once the
 * list drew (its opaque polygons; the semi-transparent ones too when the
 * list draws them at translucent markers). Active per OT like the above. */
typedef struct {
    uintptr_t start, end;
    uint32_t list;
    int drawn, translucent;
} native_range;
static native_range pending_native[MAX_ROOM_RANGES], active_native[MAX_ROOM_RANGES];
static int pending_native_count, active_native_count;
void pc_hw_native_room_packets(const void* start, const void* end, uint32_t list)
{
    if (pending_native_count < MAX_ROOM_RANGES && end > start) {
        native_range* r = &pending_native[pending_native_count++];
        r->start = (uintptr_t)start;
        r->end = (uintptr_t)end;
        r->list = list;
        r->drawn = r->translucent = 0;
    }
}
/* The packet's covered polygons: 1 opaque ones, 2 also semi-transparent
 * ones, 0 none. */
static int native_covered(uintptr_t p)
{
    for (int i = 0; i < active_native_count; i++)
        if (p >= active_native[i].start && p < active_native[i].end)
            return !active_native[i].drawn ? 0 : active_native[i].translucent ? 2 : 1;
    return 0;
}
/* Object packet ranges (actor_native.c: one per model drawn by
 * func_800B3CF0/func_800B4594, with the native list built from the same
 * model, or list 0 with native objects off). At the list's marker the range
 * becomes covered when the list drew; the hardware path then skips the
 * range's opaque triangles (the CPU's reference VRAM still draws them). */
#define MAX_OBJECT_RANGES 512
typedef struct {
    uintptr_t start, end;
    uint32_t list;
    int covered; /* 1 opaque polygons, 2 also semi-transparent ones */
    int effects; /* drop shadows or effects (pc_hw_effect_packets): RENDER_EFFECTS_* */
} object_range;
static object_range pending_objects[MAX_OBJECT_RANGES], active_objects[MAX_OBJECT_RANGES];
static int pending_object_count, active_object_count, last_object_range;
void pc_hw_object_packets(const void* start, const void* end, uint32_t list)
{
    if (pending_object_count < MAX_OBJECT_RANGES && end > start) {
        object_range* r = &pending_objects[pending_object_count++];
        r->start = (uintptr_t)start;
        r->end = (uintptr_t)end;
        r->list = list;
        r->covered = 0;
        r->effects = 0;
    }
}
/* Drop shadow or effect packets (effect_native.c): [start, end) of kind
 * RENDER_EFFECTS_*, drawn by effects list `list` (0: native off). They are
 * covered once one of the list's translucent markers drew; the hardware
 * path then skips them all (every polygon of an effects list is native). */
void pc_hw_effect_packets(const void* start, const void* end, uint32_t list, int kind)
{
    if (pending_object_count < MAX_OBJECT_RANGES && end > start) {
        object_range* r = &pending_objects[pending_object_count++];
        r->start = (uintptr_t)start;
        r->end = (uintptr_t)end;
        r->list = list;
        r->covered = 0;
        r->effects = kind > 0 ? kind : 1;
    }
}
/* The object range holding packet `p`, or NULL. */
static object_range* object_range_of(uintptr_t p)
{
    if (!active_object_count)
        return NULL;
    if (last_object_range < active_object_count) {
        object_range* r = &active_objects[last_object_range];
        if (p >= r->start && p < r->end)
            return r;
    }
    for (int i = 0; i < active_object_count; i++)
        if (p >= active_objects[i].start && p < active_objects[i].end) {
            last_object_range = i;
            return &active_objects[i];
        }
    return NULL;
}
/* Native lists drawn in this ordering table. */
#define MAX_DRAWN_LISTS 256
static uint32_t drawn_lists[MAX_DRAWN_LISTS];
static int drawn_list_count;
static void cover_objects(uint32_t list, int translucent)
{
    for (int i = 0; i < active_object_count; i++)
        if (active_objects[i].list == list)
            active_objects[i].covered = translucent ? 2 : 1;
}
static void cover_room(uint32_t list, int translucent)
{
    for (int i = 0; i < active_native_count; i++)
        if (active_native[i].list == list)
            active_native[i].drawn = 1, active_native[i].translucent = translucent;
}
/* Room triangles drawn here from packets, and those left to native lists,
 * in the frame being drawn (render_stats); the same for objects. */
static render_stats packet_stats;
/* What is still reconstructed, by the code that projected it (ROOD_OTHER_DEBUG=1:
 * the GTE caller of the packet's first vertex, its GPU code and blend mode;
 * reported every 300 frames, resolve with addr2line -f -e the binary). */
const void* pc_hw_caller;
#define OTHER_CALLERS 64
static struct {
    const void* caller;
    unsigned code, mode;
    long count;
} other_callers[OTHER_CALLERS];
static int other_debug = -1, other_caller_count, other_frames;
static void note_other(unsigned code, unsigned tpage)
{
    if (other_debug < 0)
        other_debug = getenv("ROOD_OTHER_DEBUG") && atoi(getenv("ROOD_OTHER_DEBUG"));
    if (!other_debug)
        return;
    unsigned mode = code & 2 ? (tpage >> 5) & 3 : 4;
    for (int i = 0; i < other_caller_count; i++)
        if (other_callers[i].caller == pc_hw_caller && other_callers[i].code == (code & 0xfc)
            && other_callers[i].mode == mode) {
            other_callers[i].count++;
            return;
        }
    if (other_caller_count < OTHER_CALLERS) {
        other_callers[other_caller_count].caller = pc_hw_caller;
        other_callers[other_caller_count].code = code & 0xfc;
        other_callers[other_caller_count].mode = mode;
        other_callers[other_caller_count++].count = 1;
    }
}
static void report_other(void)
{
    if (other_debug <= 0 || ++other_frames % 300)
        return;
    for (int i = 0; i < other_caller_count; i++)
        fprintf(stderr, "[other] %ld triangles in 300 frames: caller %p code %02x %s\n", other_callers[i].count,
            other_callers[i].caller, other_callers[i].code,
            other_callers[i].mode == 4 ? "opaque" : (const char*[]) { "B/2+F/2", "B+F", "B-F", "B+F/4" }[other_callers[i].mode]);
    other_caller_count = 0;
}
int pc_hw_is_room_packet(const void* p)
{
    for (int i = 0; i < active_count; i++)
        if ((uintptr_t)p >= active_ranges[i][0] && (uintptr_t)p < active_ranges[i][1])
            return 1;
    return 0;
}

/* Widescreen HUD anchoring: packet ranges of screen furniture that hugs a
 * screen edge (the status display at the left, the area map at the right),
 * recorded while an OT is built and made active at its DrawOTag like the room
 * ranges. Their primitives are scaled by 3/4 about that edge of the screen
 * instead of the drawing area's centre, so they keep their shape and sit at
 * the edges of the 16:9 picture. Nested ranges are allowed: the innermost
 * wins (PC_ANCHOR_NONE inside a range keeps the centred behaviour).
 * PC_ANCHOR_FILL: a full-screen picture (a cutscene still) is magnified by
 * 4/3 about the screen's centre instead, so it fills the 16:9 view in its
 * own proportions (its top and bottom eighths fall outside). */
enum { PC_ANCHOR_NONE, PC_ANCHOR_LEFT, PC_ANCHOR_RIGHT, PC_ANCHOR_FILL };
#define MAX_ANCHOR_RANGES 32
typedef struct {
    uintptr_t start, end;
    int anchor;
} anchor_range;
static anchor_range pending_anchors[MAX_ANCHOR_RANGES], active_anchors[MAX_ANCHOR_RANGES];
static int pending_anchor_count, active_anchor_count;
/* primitive_anchor (above): of the packet being drawn */
static float anchor_left, anchor_right; /* the screen's edges in VRAM X */
void pc_hw_anchor_packets(const void* start, const void* end, int anchor)
{
    /* Ranges marked 0x10 belong to the widescreen layout fixes
     * (pc/src/widescreen.h): off with them (ROOD_WIDESCREEN_LAYOUT=0). */
    if (anchor & 0x10) {
        if (!layout_enabled())
            return;
        anchor &= 0x0f;
    }
    if (pending_anchor_count < MAX_ANCHOR_RANGES && end > start) {
        anchor_range* r = &pending_anchors[pending_anchor_count++];
        r->start = (uintptr_t)start;
        r->end = (uintptr_t)end;
        r->anchor = anchor;
    }
}
/* The next primitives come from GPU packet `p`, drawn with the screen at
 * VRAM X screen_x..screen_x + screen_w (drawing offset, display width). */
void pc_hw_anchor_packet(const void* p, int screen_x, int screen_w)
{
    uintptr_t best = UINTPTR_MAX;
    primitive_anchor = PC_ANCHOR_NONE;
    for (int i = 0; i < active_anchor_count; i++) {
        const anchor_range* r = &active_anchors[i];
        if ((uintptr_t)p >= r->start && (uintptr_t)p < r->end && r->end - r->start < best) {
            best = r->end - r->start;
            primitive_anchor = r->anchor;
        }
    }
    anchor_left = (float)screen_x;
    anchor_right = (float)(screen_x + screen_w);
}
/* Anchored line quads: their final X (pc_hw_line), for the next triangle. */
static float line_x[3];
static int has_line_x;
/* VRAM X of an anchored primitive in widescreen: 3/4 about its edge. */
static float anchor_x(float x)
{
    float edge = primitive_anchor == PC_ANCHOR_LEFT ? anchor_left : anchor_right;
    return edge + (x - edge) * 0.75f;
}

/* Native lists hide the packets drawn after them where those are behind
 * (pc_native_occlusion): the room list's depth is copied to room_depth
 * (surface_valid), object lists depth-test against it and add their own
 * (object_fbo: the colour target with room_depth as its depth), and the draw
 * shader tests 3D fragments against it inside occlusion_rect. */
static int occlusion_on, occlusion_rect[4], surface_valid;
static GLuint room_depth_texture, room_depth_fbo, object_fbo, object_replay_fbo, before_texture, before_fbo;

/* Dynamic shadow casters: world-space triangles of opaque 3D primitives that
 * are not room geometry (characters, props), collected while an OT draws;
 * the previous OT's complete set is what the shadow pass uses. */
#define MAX_CASTER_TRIANGLES 32768
static float caster_collect[MAX_CASTER_TRIANGLES * 9], caster_ready[MAX_CASTER_TRIANGLES * 9];
static int collect_count, ready_count, caster_serial;
/* Their surface (colours, texture coordinates and state), index for index,
 * so the editor view can draw characters textured. */
typedef struct {
    uint8_t rgb[9], uv[6], code;
    uint16_t tpage, clut;
} pc_hw_surface;
static pc_hw_surface surface_collect[MAX_CASTER_TRIANGLES], surface_ready[MAX_CASTER_TRIANGLES];
const float* pc_hw_casters(int* count, int* serial)
{
    *count = ready_count;
    *serial = caster_serial;
    return caster_ready;
}
const void* pc_hw_caster_surfaces(void) { return surface_ready; }
/* Glowing things: additive translucent 3D triangles that are not room
 * geometry (flames, glows, effects), in world space with their surfaces;
 * the path tracer turns them into light sources. */
#define MAX_EMISSIVE_TRIANGLES 4096
static float emissive_collect[MAX_EMISSIVE_TRIANGLES * 9], emissive_ready[MAX_EMISSIVE_TRIANGLES * 9];
static pc_hw_surface emissive_surface_collect[MAX_EMISSIVE_TRIANGLES], emissive_surface_ready[MAX_EMISSIVE_TRIANGLES];
static int emissive_count, emissive_ready_count;
/* What emitted each one (glows.c identity): PC_GLOW_ORIGIN_* in the top
 * four bits, the source's tag below (glow_overrides.h). */
static uint32_t emissive_source_collect[MAX_EMISSIVE_TRIANGLES], emissive_source_ready[MAX_EMISSIVE_TRIANGLES];
const float* pc_hw_emissives(int* count, const void** surfaces)
{
    *count = emissive_ready_count;
    *surfaces = emissive_surface_ready;
    return emissive_ready;
}
const uint32_t* pc_hw_emissive_sources(void) { return emissive_source_ready; }
static uint32_t texture_tag(unsigned tpage, unsigned clut)
{
    return ((tpage & 0x19f) * 0x9e3779b1u ^ clut * 0x85ebca6bu) & 0x0fffffffu;
}
static int has_pending_translucent;
static void flush(void);
/* The reconstruction of 3D from packets (the GTE projection log and its
 * lookups, room depth records, caster capture from projected packets,
 * nearest-triangle matching for interpolation, which all follow from packets
 * having depth) serves only packets no native draw covers. It is off when
 * every native switch is on and pc_reconstruct is 0: packets that still
 * reach the hardware path (2D, and effects other than particles) then draw
 * as 2D, in their ordering-table place. */
static int reconstruct_active(void)
{
    return pc_reconstruct || !pc_native_room || !pc_native_actors || !pc_native_blend || !pc_native_objects
        || !pc_native_shadows || !pc_native_effects;
}
void pc_hw_begin_ot(void)
{
    if (pc_widescreen_trace > 0 && !--pc_widescreen_trace)
        fprintf(stderr, "[widescreen trace] end\n");
    pc_gte_log = !pc_hw_active || reconstruct_active();
    if (has_pending_translucent)
        flush(); /* the previous table's last translucent range */
    memcpy(active_ranges, pending_ranges, sizeof active_ranges);
    active_count = pending_count;
    pending_count = 0;
    memcpy(active_native, pending_native, (size_t)pending_native_count * sizeof *active_native);
    active_native_count = pending_native_count;
    pending_native_count = 0;
    occlusion_on = surface_valid = 0;
    drawn_list_count = 0;
    memcpy(active_objects, pending_objects, (size_t)pending_object_count * sizeof *active_objects);
    active_object_count = pending_object_count;
    pending_object_count = 0;
    last_object_range = 0;
    memcpy(active_anchors, pending_anchors, sizeof active_anchors);
    active_anchor_count = pending_anchor_count;
    pending_anchor_count = 0;
    primitive_anchor = PC_ANCHOR_NONE;
    if (collect_count) {
        memcpy(caster_ready, caster_collect, (size_t)collect_count * 9 * sizeof(float));
        memcpy(surface_ready, surface_collect, (size_t)collect_count * sizeof *surface_ready);
        ready_count = collect_count;
        ++caster_serial;
    }
    if (collect_count || emissive_count) {
        memcpy(emissive_ready, emissive_collect, (size_t)emissive_count * 9 * sizeof(float));
        memcpy(emissive_surface_ready, emissive_surface_collect, (size_t)emissive_count * sizeof *emissive_surface_ready);
        memcpy(emissive_source_ready, emissive_source_collect, (size_t)emissive_count * sizeof *emissive_source_ready);
        emissive_ready_count = emissive_count;
    }
    collect_count = 0;
    emissive_count = 0;
}
extern int pc_scene_camera_get(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy);
extern int pc_scene_camera_inverse(float inverse[3][3]);
extern int pc_scene_game_camera(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy);
extern int pc_scene_game_inverse(float inverse[3][3]);
extern int pc_scene_game_projection(float* h, float* ofx, float* ofy);
static GLuint native_texture, target_texture, target_fbo, copy_texture, copy_fbo, depth_texture;
/* G-buffer beside the VRAM colour target, for lighting in the present pass:
 * view-space normals (rgb = n * 0.5 + 0.5, a = 1 for lit 3D surfaces) and
 * albedo (the unlit texel, or the vertex colour of untextured surfaces). */
static GLuint normal_texture, albedo_texture, shade_texture; /* shade: vertex colour (editor view modes) */
static const GLenum all_buffers[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
    GL_COLOR_ATTACHMENT3 };
extern int pc_scene_projection(float* h, float* ofx, float* ofy);
static GLuint draw_program, upload_program, vertex_buffer, vertex_array, empty_array;
static GLint u_draw_occlusion, u_draw_occlusion_rect, u_draw_room_depth;
static GLint u_draw_filtering, u_draw_pass, u_draw_vram, u_draw_perspective, u_upload_vram, u_upload_rect, u_upload_scale;
static GLint u_draw_ui_scale, u_draw_ui_atlas;
/* Perspective-correct texturing (off by default: the PS1 maps affinely). */
int pc_hw_perspective;
/* Bilinear texture filtering on 3D surfaces (off: the PS1 samples nearest). */
int pc_hw_texture_filter;
static int pending_depth[3], has_pending_depth;
static float pending_precise[6];
static int has_pending_precise;
/* Sub-pixel vertex positions (PGXP-style); off restores PS1 vertex snapping. */
int pc_hw_subpixel = 1;
void pc_hw_set_precise(const float* xy)
{
    memcpy(pending_precise, xy, sizeof pending_precise);
    has_pending_precise = 1;
}
static hw_vertex batch[MAX_VERTICES];
static int batch_count, batch_blend = -2;
static int batch_clip[4] = { -1, -1, -1, -1 };
static uint32_t dirty_tiles = 0xffffffffu; /* 16 x 2 tiles of 64 x 256 */
static const uint16_t* native_vram;
/* The CPU's VRAM (textures and CLUTs as the game loaded them). */
const uint16_t* pc_hw_native_vram(void) { return native_vram; }

static const char* draw_vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in uvec4 a_color;\n"
    "layout(location = 2) in uvec2 a_uv;\n"
    "layout(location = 3) in uvec2 a_page;\n"
    "layout(location = 4) in uint a_window;\n"
    "layout(location = 5) in vec4 a_normal;\n"
    "layout(location = 6) in uvec4 a_bounds;\n"
    "layout(location = 7) in ivec2 a_ui;\n"
    "uniform int perspective;\n" /* perspective-correct UVs/colours for 3D */
    "noperspective out vec3 v_color_affine;\n"
    "noperspective out vec2 v_uv_affine;\n"
    "smooth out vec3 v_color_persp;\n"
    "smooth out vec2 v_uv_persp;\n"
    "flat out uvec4 v_state;\n" /* flags, tpage, clut, window */
    "flat out vec4 v_normal;\n"
    "flat out uvec4 v_bounds;\n"
    "flat out ivec2 v_ui;\n"
    "smooth out float v_z_persp;\n"
    "noperspective out float v_z_affine;\n"
    "void main() {\n"
    "    v_z_persp = v_z_affine = a_pos.z;\n"
    "    v_normal = a_normal;\n"
    "    v_bounds = a_bounds;\n"
    "    v_ui = a_ui;\n"
    "    v_color_affine = v_color_persp = vec3(a_color.rgb) / 255.0;\n"
    "    v_uv_affine = v_uv_persp = vec2(a_uv);\n"
    "    v_state = uvec4(a_color.a, a_page, a_window);\n"
    /* Depth: GTE SZ (0..65535) for 3D, nearest (0) for 2D/UI. The clip w
     * only changes interpolation (positions are divided back out). */
    "    float depth = a_pos.z > 0.0 ? a_pos.z / 65536.0 : 0.0;\n"
    "    float w = (perspective != 0 && a_pos.z > 0.0) ? a_pos.z / 1024.0 : 1.0;\n"
    "    gl_Position = vec4((a_pos.x / 512.0 - 1.0) * w, (a_pos.y / 256.0 - 1.0) * w, (depth * 2.0 - 1.0) * w, w);\n"
    "}\n";
static const char* draw_fragment_source =
    "#version 330 core\n"
    "noperspective in vec3 v_color_affine;\n"
    "noperspective in vec2 v_uv_affine;\n"
    "smooth in vec3 v_color_persp;\n"
    "smooth in vec2 v_uv_persp;\n"
    "flat in uvec4 v_state;\n"
    "flat in vec4 v_normal;\n"
    "flat in uvec4 v_bounds;\n" /* u min, v min, u max, v max */
    "flat in ivec2 v_ui;\n"
    "smooth in float v_z_persp;\n"
    "noperspective in float v_z_affine;\n"
    "uniform int perspective;\n"
    /* Behind a native list's surface (room_depth, SZ / 65536) by more than
     * a small tolerance: hidden. Depth here is exact per pixel when
     * perspective-correct (the written depth stays the PS1-like affine one). */
    "uniform int occlusion;\n"
    "uniform vec4 occlusion_rect;\n" /* scaled pixels: x0, y0, x1, y1 */
    "uniform sampler2D room_depth;\n"
    "uniform int filtering;\n" /* bilinear texels on 3D surfaces */
    /* 2D regions magnified into the UI atlas (ui_upscale.c): the texel's
     * block at ui_scale per texel, clamped to the region; 0: nearest. */
    "uniform int ui_scale;\n"
    "uniform usampler2D ui_atlas;\n"
    "layout(location = 0) out vec4 color;\n"
    "layout(location = 1) out vec4 normal_out;\n"
    "layout(location = 2) out vec4 albedo_out;\n"
    "layout(location = 3) out vec4 shade_out;\n"
    "uniform usampler2D vram;\n"
    "uniform int pass;\n" /* 0 all, 1 opaque texels only, 2 translucent texels only */
    "uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    /* One texel through the texture window and the page's colour depth. */
    "uint texel_at(uint u, uint v, uint tpage, uint clut, uint win) {\n"
    "    uint mx = (win & 31u) * 8u, my = ((win >> 5) & 31u) * 8u;\n"
    "    u = ((u & ~mx) | ((((win >> 10) & 31u) * 8u) & mx)) & 255u;\n"
    "    v = ((v & ~my) | ((((win >> 15) & 31u) * 8u) & my)) & 255u;\n"
    "    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;\n"
    "    int cx = int(clut & 63u) * 16, cy = int((clut >> 6) & 511u);\n"
    "    uint depth = (tpage >> 7) & 3u;\n"
    "    if (depth == 0u)\n"
    "        return fetch(cx + int((fetch(tx + int(u >> 2), ty + int(v)) >> ((u & 3u) * 4u)) & 15u), cy);\n"
    "    if (depth == 1u)\n"
    "        return fetch(cx + int((fetch(tx + int(u >> 1), ty + int(v)) >> ((u & 1u) * 8u)) & 255u), cy);\n"
    "    return fetch(tx + int(u), ty + int(v));\n"
    "}\n"
    "vec3 rgb(uint t) { return vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)); }\n"
    "void main() {\n"
    "    if (occlusion != 0 && v_z_affine > 0.0 && all(greaterThanEqual(gl_FragCoord.xy, occlusion_rect.xy))\n"
    "        && all(lessThan(gl_FragCoord.xy, occlusion_rect.zw))) {\n"
    "        float room = texelFetch(room_depth, ivec2(gl_FragCoord.xy), 0).r * 65536.0;\n"
    "        float z = perspective != 0 ? v_z_persp : v_z_affine;\n"
    /* Room packets (translucent room polygons) are the same mesh: almost no
     * slack; characters, decals and drop shadows get some. */
    "        float slack = v_normal.w > 0.9 ? room * 0.002 + 1.0 : room * 0.02 + 8.0;\n"
    "        if (room < 65535.0 && z > room + slack) discard;\n"
    "    }\n"
    "    uint flags = v_state.x, tpage = v_state.y, clut = v_state.z, win = v_state.w;\n"
    "    bool semi = (flags & 4u) != 0u;\n"
    "    uint mask = (flags & 8u) >> 3;\n"
    "    vec3 v_color = perspective != 0 ? v_color_persp : v_color_affine;\n"
    "    vec2 v_uv = perspective != 0 ? v_uv_persp : v_uv_affine;\n"
    "    vec3 c = v_color;\n"
    "    vec3 albedo = v_color;\n"
    "    vec4 shade = vec4(v_color, 0.5);\n"
    "    if ((flags & 1u) != 0u) {\n"
    "        uint t;\n"
    "        if ((flags & 16u) != 0u && ui_scale > 0) {\n"
    "            ivec2 lo = ivec2(v_bounds.xy), hi = max(ivec2(v_bounds.zw) - 1, lo);\n"
    "            ivec2 q = clamp(ivec2(floor(v_uv * float(ui_scale) + 0.0001)), lo * ui_scale, (hi + 1) * ui_scale - 1);\n"
    "            t = texelFetch(ui_atlas, q + v_ui * ui_scale, 0).r;\n"
    "        } else {\n"
    "            ivec2 q = ivec2(floor(v_uv + 0.0001));\n"
    /* FLAG_CLAMP (2D): the texels the PS1 samples, [min, max - 1]; the far
     * edge is never reached (finer samples than whole pixels would take it). */
    "            if ((flags & 32u) != 0u) q = clamp(q, ivec2(v_bounds.xy), max(ivec2(v_bounds.zw) - 1, ivec2(v_bounds.xy)));\n"
    "            t = texel_at(uint(q.x) & 255u, uint(q.y) & 255u, tpage, clut, win);\n"
    "        }\n"
    "        vec3 texel = rgb(t);\n"
    "        if (filtering != 0 && v_normal.w > 0.5) {\n"
    /* Bilinear between the four nearest texels, kept inside the primitive's
     * texel bounds (pages are atlases); transparent texels drop out and the
     * pixel is cut where they dominate. */
    "            vec2 p = v_uv - 0.5;\n"
    "            vec2 f = fract(p);\n"
    "            ivec2 base = ivec2(floor(p));\n"
    "            vec3 sum = vec3(0.0);\n"
    "            float cover = 0.0;\n"
    "            for (int k = 0; k < 4; k++) {\n"
    "                ivec2 q = clamp(base + ivec2(k & 1, k >> 1), ivec2(v_bounds.xy), ivec2(v_bounds.zw));\n"
    "                uint tk = texel_at(uint(q.x) & 255u, uint(q.y) & 255u, tpage, clut, win);\n"
    "                float w = ((k & 1) != 0 ? f.x : 1.0 - f.x) * ((k >> 1) != 0 ? f.y : 1.0 - f.y);\n"
    "                if (tk != 0u) { sum += rgb(tk) * w; cover += w; }\n"
    "            }\n"
    "            if (cover < 0.5) discard;\n"
    "            texel = sum / cover;\n"
    "            if (t == 0u) t = 0x7fffu;\n" /* a kept edge pixel over a clear texel: opaque */
    "        }\n"
    "        if (t == 0u) discard;\n"
    "        semi = semi && (t & 0x8000u) != 0u;\n"
    "        mask |= t >> 15;\n"
    "        c = (flags & 2u) != 0u ? texel / 31.0 : min(texel * v_color * (255.0 / 128.0), 31.0) / 31.0;\n"
    "        albedo = texel / 31.0;\n"
    "        shade = (flags & 2u) != 0u ? vec4(vec3(128.0 / 255.0), 1.0) : vec4(v_color, 1.0);\n"
    "    }\n"
    "    if ((pass == 1 && semi) || (pass == 2 && !semi)) discard;\n"
    "    color = vec4(c, float(mask));\n"
    "    bool lit = v_normal.w > 0.5;\n"
    /* a: 1 room surface, 0.75 something moving (character, prop), 0 unlit */
    "    normal_out = lit ? vec4(normalize(v_normal.xyz) * 0.5 + 0.5, v_normal.w > 0.9 ? 1.0 : 0.75) : vec4(0.0);\n"
    "    albedo_out = lit ? vec4(albedo, 1.0) : vec4(0.0);\n"
    "    shade_out = lit ? shade : vec4(0.0);\n"
    "}\n";
/* Copy a native VRAM rectangle into the scaled target (uploads, LoadImage). */
static const char* upload_vertex_source =
    "#version 330 core\n"
    "uniform ivec4 rect;\n"
    "void main() {\n"
    "    vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "    vec2 xy = vec2(rect.xy) + p * vec2(rect.zw);\n"
    "    gl_Position = vec4(xy.x / 512.0 - 1.0, xy.y / 256.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char* upload_fragment_source =
    "#version 330 core\n"
    "out vec4 color;\n"
    "uniform usampler2D vram;\n"
    "uniform float scale;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(floor(gl_FragCoord.xy / scale));\n"
    "    uint t = texelFetch(vram, ivec2(p.x & 1023, p.y & 511), 0).r;\n"
    "    color = vec4(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u), 31.0 * float(t >> 15)) / 31.0;\n"
    "}\n";

/* Depth for post effects: written in draw order (the PS1 sorts back to
 * front), never tested. 2D primitives write 0 (nearest), fills reset to 1. */
static void make_depth(GLuint fbo, GLuint* texture)
{
    gl_GenTextures(1, texture);
    gl_BindTexture(GL_TEXTURE_2D, *texture);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1024 * pc_hw_scale, 512 * pc_hw_scale, 0,
        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, *texture, 0);
}
static GLuint make_color(GLenum attachment)
{
    GLuint texture;
    gl_GenTextures(1, &texture);
    gl_BindTexture(GL_TEXTURE_2D, texture);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024 * pc_hw_scale, 512 * pc_hw_scale, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, NULL);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, 0);
    return texture;
}
static void make_gbuffer(GLuint fbo, GLuint* normal, GLuint* albedo, GLuint* shade)
{
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    *normal = make_color(GL_COLOR_ATTACHMENT1);
    *albedo = make_color(GL_COLOR_ATTACHMENT2);
    *shade = make_color(GL_COLOR_ATTACHMENT3);
    gl_DrawBuffers(4, all_buffers);
    static const float zero[4] = { 0, 0, 0, 0 };
    gl_ClearBufferfv(GL_COLOR, 1, zero);
    gl_ClearBufferfv(GL_COLOR, 2, zero);
    gl_ClearBufferfv(GL_COLOR, 3, zero);
}
static void make_target(GLuint* texture, GLuint* fbo)
{
    gl_GenTextures(1, texture);
    gl_BindTexture(GL_TEXTURE_2D, *texture);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024 * pc_hw_scale, 512 * pc_hw_scale, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, NULL);
    gl_GenFramebuffers(1, fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
}

/* The replay target: a second colour/G-buffer/depth set that interpolated
 * frames are drawn into (see "frame interpolation" below). */
static struct {
    GLuint fbo, color, normal, albedo, shade, depth;
} replay;
static void make_replay_target(void)
{
    make_target(&replay.color, &replay.fbo);
    make_depth(replay.fbo, &replay.depth);
    make_gbuffer(replay.fbo, &replay.normal, &replay.albedo, &replay.shade);
}
static void forget_recordings(void);
/* The native lists' own depth (occlusion of the packets drawn after them). */
static void make_room_depth(void)
{
    gl_GenFramebuffers(1, &room_depth_fbo);
    make_depth(room_depth_fbo, &room_depth_texture);
    gl_DrawBuffer(GL_NONE);
    gl_ReadBuffer(GL_NONE);
}
/* The colour/G-buffer textures of a target with room_depth as the depth:
 * where object lists draw. */
static void make_object_fbo(GLuint* fbo, GLuint color, GLuint normal, GLuint albedo, GLuint shade)
{
    gl_GenFramebuffers(1, fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    GLuint textures[4] = { color, normal, albedo, shade };
    for (int i = 0; i < 4; i++)
        gl_FramebufferTexture2D(GL_FRAMEBUFFER, all_buffers[i], GL_TEXTURE_2D, textures[i], 0);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, room_depth_texture, 0);
    gl_DrawBuffers(4, all_buffers);
    if (gl_CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[gpu] object target incomplete: native objects off\n");
        gl_DeleteFramebuffers(1, fbo);
        *fbo = 0;
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
static void make_object_fbos(void)
{
    gl_GenFramebuffers(1, &before_fbo);
    make_depth(before_fbo, &before_texture);
    gl_DrawBuffer(GL_NONE);
    gl_ReadBuffer(GL_NONE);
    make_object_fbo(&object_fbo, target_texture, normal_texture, albedo_texture, shade_texture);
    make_object_fbo(&object_replay_fbo, replay.color, replay.normal, replay.albedo, replay.shade);
}

/* Called once the GL context exists. ROOD_SCALE picks the internal resolution
 * multiple (1-8, default 4); ROOD_SCALE=0 disables the hardware path. */
void pc_hw_init(const uint16_t* vram)
{
    const char* setting = getenv("ROOD_SCALE");
    int scale = setting ? atoi(setting) : 4;
    if (scale < 1)
        return;
    pc_hw_scale = scale > 8 ? 8 : scale;
    native_vram = vram;
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        if (!pc_vk_hw_init(pc_hw_scale, sizeof(hw_vertex)))
            return;
        /* GL names become tokens for the Vulkan targets (vk_hooks.h). */
        target_fbo = target_texture = VK_TOKEN_LIVE;
        replay.fbo = replay.color = VK_TOKEN_REPLAY;
        room_depth_fbo = room_depth_texture = VK_TOKEN_ROOM_DEPTH;
        before_fbo = before_texture = VK_TOKEN_BEFORE;
        object_fbo = VK_TOKEN_OBJECT;
        object_replay_fbo = VK_TOKEN_OBJECT_REPLAY;
        native_texture = VK_TOKEN_VRAM;
        goto natives;
    }
#endif
    draw_program = pc_gl_program(draw_vertex_source, draw_fragment_source);
    upload_program = pc_gl_program(upload_vertex_source, upload_fragment_source);
    if (!draw_program || !upload_program)
        return;
    u_draw_pass = gl_GetUniformLocation(draw_program, "pass");
    u_draw_vram = gl_GetUniformLocation(draw_program, "vram");
    u_draw_perspective = gl_GetUniformLocation(draw_program, "perspective");
    u_draw_filtering = gl_GetUniformLocation(draw_program, "filtering");
    u_draw_occlusion = gl_GetUniformLocation(draw_program, "occlusion");
    u_draw_occlusion_rect = gl_GetUniformLocation(draw_program, "occlusion_rect");
    u_draw_room_depth = gl_GetUniformLocation(draw_program, "room_depth");
    u_draw_ui_scale = gl_GetUniformLocation(draw_program, "ui_scale");
    u_draw_ui_atlas = gl_GetUniformLocation(draw_program, "ui_atlas");
    gl_UseProgram(draw_program);
    gl_Uniform1i(u_draw_room_depth, 1); /* never unit 0 (the VRAM usampler) */
    gl_Uniform1i(u_draw_ui_atlas, 2);
    gl_UseProgram(0);
    u_upload_vram = gl_GetUniformLocation(upload_program, "vram");
    u_upload_rect = gl_GetUniformLocation(upload_program, "rect");
    u_upload_scale = gl_GetUniformLocation(upload_program, "scale");

    gl_GenTextures(1, &native_texture);
    gl_BindTexture(GL_TEXTURE_2D, native_texture);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    make_target(&target_texture, &target_fbo);
    make_depth(target_fbo, &depth_texture);
    make_gbuffer(target_fbo, &normal_texture, &albedo_texture, &shade_texture);
    if (gl_CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[gpu] %dx render target unavailable; using native presentation\n",
            pc_hw_scale);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    gl_ClearColor(0, 0, 0, 0);
    gl_ClearDepth(1.0);
    gl_Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    make_target(&copy_texture, &copy_fbo);
    make_replay_target();
    make_room_depth();
    make_object_fbos();
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
#ifdef ROOD_VULKAN
natives:
#endif
    {
        const char* rate = getenv("ROOD_PRESENT_RATE");
        if (rate)
            pc_present_rate = atoi(rate) < 1 ? 1 : atoi(rate) > 8 ? 8 : atoi(rate);
        const char* native = getenv("ROOD_NATIVE_ROOM");
        if (native)
            pc_native_room = atoi(native) < 0 ? 0 : atoi(native) > 2 ? 2 : atoi(native);
        native = getenv("ROOD_NATIVE_ACTORS");
        if (native)
            pc_native_actors = atoi(native) < 0 ? 0 : atoi(native) > 2 ? 2 : atoi(native);
        native = getenv("ROOD_NATIVE_BLEND");
        if (native)
            pc_native_blend = atoi(native) != 0;
        native = getenv("ROOD_NATIVE_OBJECTS");
        if (native)
            pc_native_objects = atoi(native) != 0;
        native = getenv("ROOD_NATIVE_SHADOWS");
        if (native)
            pc_native_shadows = atoi(native) != 0;
        native = getenv("ROOD_NATIVE_EFFECTS");
        if (native)
            pc_native_effects = atoi(native) != 0;
        native = getenv("ROOD_RECONSTRUCT");
        if (native)
            pc_reconstruct = atoi(native) != 0;
    }
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_hw_active = 1;
        fprintf(stderr, "[gpu] hardware rasterizer at %dx (%dx%d VRAM target), Vulkan\n", pc_hw_scale,
            1024 * pc_hw_scale, 512 * pc_hw_scale);
        return;
    }
#endif

    gl_GenBuffers(1, &vertex_buffer);
    gl_GenVertexArrays(1, &vertex_array);
    gl_GenVertexArrays(1, &empty_array);
    gl_BindVertexArray(vertex_array);
    gl_BindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    gl_EnableVertexAttribArray(0);
    gl_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(hw_vertex), (void*)offsetof(hw_vertex, x));
    gl_EnableVertexAttribArray(1);
    gl_VertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, sizeof(hw_vertex), (void*)offsetof(hw_vertex, r));
    gl_EnableVertexAttribArray(2);
    gl_VertexAttribIPointer(2, 2, GL_UNSIGNED_SHORT, sizeof(hw_vertex), (void*)offsetof(hw_vertex, u));
    gl_EnableVertexAttribArray(3);
    gl_VertexAttribIPointer(3, 2, GL_UNSIGNED_SHORT, sizeof(hw_vertex), (void*)offsetof(hw_vertex, tpage));
    gl_EnableVertexAttribArray(4);
    gl_VertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(hw_vertex), (void*)offsetof(hw_vertex, window));
    gl_EnableVertexAttribArray(5);
    gl_VertexAttribPointer(5, 4, GL_BYTE, GL_TRUE, sizeof(hw_vertex), (void*)offsetof(hw_vertex, nx));
    gl_EnableVertexAttribArray(6);
    gl_VertexAttribIPointer(6, 4, GL_UNSIGNED_BYTE, sizeof(hw_vertex), (void*)offsetof(hw_vertex, uv_min));
    gl_EnableVertexAttribArray(7);
    gl_VertexAttribIPointer(7, 2, GL_SHORT, sizeof(hw_vertex), (void*)offsetof(hw_vertex, ui));
    gl_BindVertexArray(0);
    pc_hw_active = 1;
    fprintf(stderr, "[gpu] hardware rasterizer at %dx (%dx%d VRAM target)\n", pc_hw_scale,
        1024 * pc_hw_scale, 512 * pc_hw_scale);
}

/* ---- native VRAM tile tracking ---- */

/* Tiles touched by the inclusive rectangle; coordinates wrap like VRAM. */
static uint32_t tiles_for(int x0, int y0, int x1, int y1)
{
    if (x1 < x0 || y1 < y0)
        return 0;
    if (x1 - x0 >= 1023)
        x0 = 0, x1 = 1023;
    if (y1 - y0 >= 511)
        y0 = 0, y1 = 511;
    uint32_t mask = 0;
    for (int y = y0;;) {
        for (int x = x0;;) {
            mask |= 1u << (((y & 511) >> 8) * 16 + ((x & 1023) >> 6));
            if (x >= x1)
                break;
            x = (x | 63) + 1 > x1 ? x1 : (x | 63) + 1;
        }
        if (y >= y1)
            break;
        y = (y | 255) + 1 > y1 ? y1 : (y | 255) + 1;
    }
    return mask;
}
/* The CPU rasterizer changed native VRAM in this rectangle (inclusive). */
void pc_hw_mark(int x0, int y0, int x1, int y1)
{
    if (pc_hw_active) {
        uint32_t tiles = tiles_for(x0, y0, x1, y1);
        dirty_tiles |= tiles;
        pc_ui_upscale_tiles_changed(tiles);
    }
}
static void upload_tiles(uint32_t tiles)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_upload_tiles(native_vram, tiles);
        dirty_tiles &= ~tiles;
        return;
    }
#endif
    gl_BindTexture(GL_TEXTURE_2D, native_texture);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 2);
    gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 1024);
    for (int tile = 0; tile < 32; tile++)
        if (tiles & (1u << tile)) {
            int x = (tile & 15) * 64, y = (tile >> 4) * 256;
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, x, y, 64, 256, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                native_vram + y * 1024 + x);
        }
    gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    dirty_tiles &= ~tiles;
}

/* ---- batching ---- */

static void set_scissor(const int* clip)
{
    int s = pc_hw_scale;
    gl_Enable(GL_SCISSOR_TEST);
    gl_Scissor(clip[0] * s, clip[1] * s, clip[2] > 0 ? clip[2] * s : 0, clip[3] > 0 ? clip[3] * s : 0);
}
static void set_blend(int mode)
{
    gl_Enable(GL_BLEND);
    gl_BlendEquation(mode == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
    switch (mode) {
    case 0: /* B/2 + F/2 */
        gl_BlendColor(0, 0, 0, 0.5f);
        gl_BlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
        break;
    case 3: /* B + F/4 */
        gl_BlendColor(0, 0, 0, 0.25f);
        gl_BlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
        break;
    default: /* B + F, B - F */
        gl_BlendFunc(GL_ONE, GL_ONE);
        break;
    }
}
static void record_batch(void);
static void draw_batch(GLuint fbo, const hw_vertex* vertices, int count, int blend, const int* clip)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_draw_batch(fbo, vertices, count, blend, clip, occlusion_on, occlusion_rect);
        return;
    }
#endif
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_Viewport(0, 0, 1024 * pc_hw_scale, 512 * pc_hw_scale);
    set_scissor(clip);
    gl_UseProgram(draw_program);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, native_texture);
    gl_Uniform1i(u_draw_vram, 0);
    gl_BindVertexArray(vertex_array);
    gl_BindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(hw_vertex)), vertices, GL_STREAM_DRAW);
    gl_Uniform1i(u_draw_perspective, pc_hw_perspective);
    gl_Uniform1i(u_draw_filtering, pc_hw_texture_filter);
    {
        GLuint atlas = pc_ui_upscale_gl_texture();
        gl_Uniform1i(u_draw_ui_scale, atlas ? pc_ui_upscale_draw_factor() : 0);
        gl_ActiveTexture(GL_TEXTURE2);
        gl_BindTexture(GL_TEXTURE_2D, atlas ? atlas : native_texture);
        gl_ActiveTexture(GL_TEXTURE0);
    }
    gl_Uniform1i(u_draw_occlusion, occlusion_on);
    if (occlusion_on) {
        int s = pc_hw_scale;
        gl_Uniform4f(u_draw_occlusion_rect, (float)(occlusion_rect[0] * s), (float)(occlusion_rect[1] * s),
            (float)((occlusion_rect[0] + occlusion_rect[2]) * s), (float)((occlusion_rect[1] + occlusion_rect[3]) * s));
        gl_ActiveTexture(GL_TEXTURE1);
        gl_BindTexture(GL_TEXTURE_2D, room_depth_texture);
        gl_ActiveTexture(GL_TEXTURE0);
    }
    gl_Disable(GL_BLEND);
    gl_Enable(GL_DEPTH_TEST); /* GL_ALWAYS: write depth in draw order, never reject */
    gl_DepthFunc(GL_ALWAYS);
    gl_DepthMask(GL_TRUE);
    if (blend < 0) {
        gl_Uniform1i(u_draw_pass, 0);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
    } else {
        gl_Uniform1i(u_draw_pass, 1);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
        set_blend(blend & 3);
        /* Translucent texels keep the depth behind them and the surface
         * behind them. 2D ones replace it with "UI" (unlit, no haze), except
         * additive ones: effect sprites only add light, and marked as UI
         * they cut dark holes in the haze in their own shapes. */
        int additive = (blend & 3) == 1 || (blend & 3) == 3;
        gl_DepthMask(blend >= 4 && !additive ? GL_TRUE : GL_FALSE);
        gl_ColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_ColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_ColorMaski(3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_Uniform1i(u_draw_pass, 2);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
        gl_Disable(GL_BLEND);
        gl_ColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl_ColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl_ColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    gl_DepthMask(GL_TRUE);
    gl_Disable(GL_DEPTH_TEST);
    gl_BindVertexArray(0);
    gl_Disable(GL_SCISSOR_TEST);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
/* Translucent markers reached in a row that continue each other's range of
 * one list (a model's buckets, far to near) draw as one range: pending
 * until anything else draws. */
static struct {
    uint32_t list;
    int first, count, clip[4], ox, oy, mask;
    unsigned window;
} pending_translucent;
static int has_pending_translucent;
static int draw_translucent(GLuint fbo, uint32_t list, int first, int count, const int* clip, int ox, int oy,
    unsigned window, int mask, const render_camera* camera, float t);
static void record_translucent(uint32_t list, int first, int count, const int* clip, int ox, int oy, unsigned window,
    int mask);
static void flush(void)
{
    if (batch_count) {
        record_batch();
        pc_ui_upscale_prepare(); /* the magnified 2D regions it samples */
        draw_batch(target_fbo, batch, batch_count, batch_blend, batch_clip);
        batch_count = 0;
    }
    if (has_pending_translucent) {
        has_pending_translucent = 0;
        const typeof(pending_translucent)* t = &pending_translucent;
        if (draw_translucent(target_fbo, t->list, t->first, t->count, t->clip, t->ox, t->oy, t->window, t->mask, NULL,
                1.0f))
            record_translucent(t->list, t->first, t->count, t->clip, t->ox, t->oy, t->window, t->mask);
    }
}
/* Native VRAM columns a texture page spans by depth (4, 8, 15/16 bit). */
static uint32_t sampled_tiles(unsigned tpage, unsigned clut)
{
    int tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256, depth = (tpage >> 7) & 3;
    int width = depth == 0 ? 64 : depth == 1 ? 128 : 256; /* halfword columns */
    uint32_t tiles = tiles_for(tx, ty, tx + width - 1, ty + 255);
    if (depth < 2) {
        int cx = (clut & 63) * 16, cy = (clut >> 6) & 511;
        tiles |= tiles_for(cx, cy, cx + (depth ? 255 : 15), cy);
    }
    return tiles;
}
static int report_mask_check;
/* Depth (GTE SZ) for the next pc_hw_triangle only; primitives without it are 2D. */
void pc_hw_set_depth(const int* depth)
{
    memcpy(pending_depth, depth, sizeof pending_depth);
    has_pending_depth = 1;
}
void pc_hw_set_packet(const void* packet);
static void record_triangle(const float* world, unsigned code, unsigned tpage, unsigned clut, const int* uv);
/* Queue one triangle; state is the CPU rasterizer's at the time of drawing.
 * Returns 1 when a native draw covers it and the CPU's reference VRAM is
 * not kept for its kind (pc_native_room / pc_native_actors 2): the CPU
 * rasterizer may skip it (its area is then stale until pc_hw_sync). */
static void mark_stale(const int* clip);
enum { STALE_MISSING = 1, STALE_PLACED = 2 }; /* (stale_kinds, below) */
static void mark_stale_kind(const int* clip, int kind);
int pc_hw_triangle(const int* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage,
    unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check)
{
    if (!pc_hw_active)
        return 0;
    if (mask_check && !report_mask_check++)
        fprintf(stderr, "[gpu] mask-bit test is not mirrored by the hardware path yet\n");
    int other = 0; /* neither room nor object packet: still reconstructed */
    int ui = primitive_2d; /* screen furniture: its texture may be magnified (pc_ui_upscale) */
    if (primitive_room && current_packet) {
        ui = 0; /* rects and lines carry no packet; 2D or not (no projection log) */
        /* Room polygons that a native list draws (opaque ones at the list,
         * semi-transparent ones at their translucent markers). */
        int covered = active_native_count ? native_covered(current_packet) : 0;
        if (covered > ((code & 2) ? 1 : 0)) {
            if (code & 2)
                ++packet_stats.skipped_translucent_triangles;
            else
                ++packet_stats.skipped_room_triangles;
            has_pending_depth = has_pending_precise = 0;
            if (pc_native_room == 2) {
                mark_stale(clip);
                return 1;
            }
            return 0;
        }
        if (code & 2)
            ++packet_stats.legacy_translucent_triangles;
        ++packet_stats.legacy_room_triangles;
    } else if (!primitive_room || !current_packet) {
        /* Object polygons (characters, weapons, props) that a native list
         * drew (whether or not the projection log made them 3D). */
        object_range* range = active_object_count ? object_range_of(current_packet) : NULL;
        if (range)
            ui = 0; /* characters, props, drop shadows, effects */
        if (range && range->effects) {
            if (range->covered) {
                ++packet_stats.skipped_effect_triangles;
                has_pending_depth = has_pending_precise = 0;
                if (pc_native_actors == 2) {
                    mark_stale(clip);
                    return 1;
                }
                return 0;
            }
            ++packet_stats.legacy_effect_triangles;
            other = !primitive_2d;
        } else if (range) {
            if (range->covered > ((code & 2) ? 1 : 0)) {
                if (code & 2)
                    ++packet_stats.skipped_translucent_triangles;
                else
                    ++packet_stats.skipped_object_triangles;
                has_pending_depth = has_pending_precise = 0;
                if (pc_native_actors == 2) {
                    mark_stale(clip);
                    return 1;
                }
                return 0;
            }
            if (code & 2)
                ++packet_stats.legacy_translucent_triangles;
            ++packet_stats.legacy_object_triangles;
        } else
            other = !primitive_2d;
    }
    if (has_pending_translucent)
        flush();
    /* Widescreen: what the target holds where this draws is not what the
     * CPU rasterizer draws there (2D placed for the view, Hor+ 3D), so a
     * texture sampling it (a screen copy of rows of the frame being drawn,
     * as the memory-card screens' text box does) reads it back first, as
     * it does for displayed frames (pc_hw_set_display). */
    if (pc_gte_widescreen && layout_enabled())
        mark_stale_kind(clip, STALE_PLACED);
    int textured = (code & 4) != 0, semi = (code & 2) != 0;
    /* Widescreen: an anchored primitive (HUD at a screen edge; its GTE
     * projections were not narrowed) scales about that edge, its drawing
     * area with it; other 2D compresses about the drawing area's centre,
     * except screen-covering 2D (pc_hw_set_bounds), which spans the view. */
    int fill = pc_gte_widescreen && primitive_anchor == PC_ANCHOR_FILL && !primitive_cover && layout_enabled();
    int anchored = pc_gte_widescreen && primitive_anchor != PC_ANCHOR_NONE && primitive_anchor != PC_ANCHOR_FILL
        && !primitive_cover;
    int extend = pc_gte_widescreen && primitive_2d && !anchored && !fill && !primitive_cover ? primitive_extend : 0;
    int compress = pc_gte_widescreen && primitive_2d && !anchored && !fill && !primitive_cover && !extend
        && !primitive_narrowed;
    float middle = clip[1] + clip[3] * 0.5f; /* PC_ANCHOR_FILL magnifies Y about it */
    float centre = clip[0] + clip[2] * 0.5f;
    int area[4];
    if (anchored) {
        float left = anchor_x((float)clip[0]), right = anchor_x((float)(clip[0] + clip[2]));
        area[0] = (int)floorf(left);
        area[1] = clip[1];
        area[2] = (int)ceilf(right) - area[0];
        area[3] = clip[3];
    } else if ((compress || extend) && layout_enabled()) {
        /* The drawing area compressed like its contents (an extended side
         * reaches the view's edge). */
        float left = extend < 0 ? (float)clip[0] : centre + (clip[0] - centre) * 0.75f;
        float right = extend > 0 ? (float)(clip[0] + clip[2]) : centre + (clip[0] + clip[2] - centre) * 0.75f;
        area[0] = (int)floorf(left + 0.001f);
        area[1] = clip[1];
        area[2] = (int)ceilf(right - 0.001f) - area[0];
        area[3] = clip[3];
    } else
        memcpy(area, clip, sizeof area);
    /* Extended: the far end compressed, the near edge of the screen at the
     * view's edge, linearly in between. */
    float extend_from = 0.0f, extend_scale = 1.0f;
    if (extend > 0) {
        float right = (float)(clip[0] + clip[2]);
        extend_from = centre + (primitive_span[0] - centre) * 0.75f;
        extend_scale = (right - extend_from) / (right - primitive_span[0]);
    } else if (extend < 0) {
        float left = (float)clip[0];
        extend_from = centre + (primitive_span[1] - centre) * 0.75f;
        extend_scale = (extend_from - left) / (primitive_span[1] - left);
    }
    int depth[3] = { 0, 0, 0 };
    if (has_pending_depth && !primitive_2d)
        memcpy(depth, pending_depth, sizeof depth);
    has_pending_depth = 0;
    if (other && (depth[0] | depth[1] | depth[2])) {
        ++packet_stats.legacy_other_triangles;
        note_other(code, tpage);
    }
    /* Blend mode, + 4 for translucent screen furniture (no depth): that
     * replaces the depth and surface behind it, so post effects and lights
     * treat it as UI. */
    int blend = semi ? (int)((tpage >> 5) & 3) + (depth[0] | depth[1] | depth[2] ? 0 : 4) : -1;
    if (blend != batch_blend || memcmp(area, batch_clip, sizeof batch_clip)
        || batch_count + 3 > MAX_VERTICES) {
        flush();
        batch_blend = blend;
        memcpy(batch_clip, area, sizeof batch_clip);
    }
    if (textured) {
        uint32_t needed = sampled_tiles(tpage, clut) & dirty_tiles;
        if (needed) {
            flush();
            upload_tiles(needed);
        }
    }
    uint8_t flags = (uint8_t)((textured ? FLAG_TEXTURED : 0) | (textured && (code & 1) ? FLAG_RAW : 0)
        | (semi ? FLAG_SEMI : 0) | (mask_set ? FLAG_MASK : 0));
    /* Positions: unrounded for projected 3D vertices, else the packet's. */
    float pos[6];
    for (int i = 0; i < 6; i++)
        pos[i] = (float)xy[i];
    /* 2D packets carry whole pixels unless the game placed them finer
     * (sub-pixel text, pc_ui_subpixel). */
    if (has_pending_precise && (!primitive_2d || pc_ui_subpixel) && pc_hw_subpixel)
        memcpy(pos, pending_precise, sizeof pos);
    has_pending_precise = 0;
    /* View-space face normal from the projected vertices: invert the GTE
     * projection (sx = ofx + H x / z, X narrowed by 3/4 in widescreen). The
     * drawing area's origin is the framebuffer's screen origin. */
    int8_t normal[4] = { 0, 0, 0, 0 };
    float h, ofx, ofy, world[9];
    int has_world = 0;
    if (depth[0] > 0 && depth[1] > 0 && depth[2] > 0 && pc_scene_game_projection(&h, &ofx, &ofy)) {
        float view[3][3], hx = h * (pc_gte_widescreen ? 0.75f : 1.0f);
        for (int i = 0; i < 3; i++) {
            view[i][2] = (float)depth[i];
            view[i][0] = (pos[i * 2] - clip[0] - ofx) * view[i][2] / hx;
            view[i][1] = (pos[i * 2 + 1] - clip[1] - ofy) * view[i][2] / h;
        }
        float a[3], b[3], n[3];
        for (int k = 0; k < 3; k++)
            a[k] = view[1][k] - view[0][k], b[k] = view[2][k] - view[0][k];
        n[0] = a[1] * b[2] - a[2] * b[1];
        n[1] = a[2] * b[0] - a[0] * b[2];
        n[2] = a[0] * b[1] - a[1] * b[0];
        float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        /* World space (view -> world through the camera), for frame
         * interpolation and for the shadow casters below. */
        float r[3][3], t[3], ch, cx, cy, inverse[3][3];
        if (pc_scene_game_camera(r, t, &ch, &cx, &cy) && pc_scene_game_inverse(inverse)) {
            has_world = 1;
            for (int i = 0; i < 3; i++) {
                float d[3] = { view[i][0] - t[0], view[i][1] - t[1], view[i][2] - t[2] };
                for (int k = 0; k < 3; k++)
                    world[i * 3 + k] = inverse[k][0] * d[0] + inverse[k][1] * d[1] + inverse[k][2] * d[2];
            }
        }
        /* Additive (B + F, B + F/4) non-room triangles glow. */
        int additive = (code & 2) && (((tpage >> 5) & 3) == 1 || ((tpage >> 5) & 3) == 3);
        if (has_world && !primitive_room && additive && emissive_count < MAX_EMISSIVE_TRIANGLES) {
            emissive_source_collect[emissive_count] = PC_GLOW_ORIGIN_PACKET << 28 | texture_tag(tpage, clut);
            memcpy(emissive_collect + emissive_count * 9, world, sizeof world);
            pc_hw_surface* sf = &emissive_surface_collect[emissive_count++];
            for (int i = 0; i < 9; i++)
                sf->rgb[i] = (uint8_t)rgb[i];
            for (int i = 0; i < 6; i++)
                sf->uv[i] = textured ? (uint8_t)uv[i] : 0;
            sf->code = (uint8_t)code;
            sf->tpage = (uint16_t)tpage;
            sf->clut = (uint16_t)clut;
        }
        /* Characters and props cast shadows: opaque non-room 3D triangles. */
        if (has_world && !primitive_room && !(code & 2) && collect_count < MAX_CASTER_TRIANGLES) {
            memcpy(caster_collect + collect_count * 9, world, sizeof world);
            pc_hw_surface* sf = &surface_collect[collect_count];
            for (int i = 0; i < 9; i++)
                sf->rgb[i] = (uint8_t)rgb[i];
            for (int i = 0; i < 6; i++)
                sf->uv[i] = textured ? (uint8_t)uv[i] : 0;
            sf->code = (uint8_t)code;
            sf->tpage = (uint16_t)tpage;
            sf->clut = (uint16_t)clut;
            ++collect_count;
        }
        if (length > 1e-6f) {
            if (n[0] * view[0][0] + n[1] * view[0][1] + n[2] * view[0][2] > 0)
                length = -length; /* face the camera */
            for (int k = 0; k < 3; k++)
                normal[k] = (int8_t)(n[k] / length * 127.0f);
            normal[3] = primitive_room ? 127 : 100; /* lit; room or moving (G-buffer alpha) */
        }
    }
    record_triangle(has_world ? world : NULL, code, tpage, clut, uv);
    /* 2D: a flipped sprite's coordinates (pc_hw_set_flipped_uv), and texels
     * kept inside the primitive (FLAG_CLAMP). */
    int texcoord[6];
    for (int i = 0; i < 6; i++)
        texcoord[i] = uv[i] + (textured ? primitive_uv_shift[i & 1] : 0);
    uv = texcoord;
    if (textured && !(depth[0] | depth[1] | depth[2]))
        flags |= FLAG_CLAMP;
    int bounds[4] = { 255, 255, 0, 0 };
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 2; k++) {
            int c = uv[i * 2 + k] < 0 ? 0 : uv[i * 2 + k] > 255 ? 255 : uv[i * 2 + k];
            bounds[k] = c < bounds[k] ? c : bounds[k];
            bounds[k + 2] = c > bounds[k + 2] ? c : bounds[k + 2];
        }
    /* 2D: the texels it samples, magnified (MMPX) or, for glyphs, drawn from
     * a font (ui_text). The far bound is an edge
     * the rasterizer never reaches: [min, max - 1], as the shader clamps. */
    int16_t ui_offset[2] = { 0, 0 };
    if (pc_ui_upscale || pc_ui_text) {
        pc_ui_upscale_draw_area(area);
        if (ui && textured && !(depth[0] | depth[1] | depth[2])) {
            int u1 = bounds[2] > bounds[0] ? bounds[2] - 1 : bounds[0], v1 = bounds[3] > bounds[1] ? bounds[3] - 1 : bounds[1];
            int found = pc_ui_upscale_lookup(native_vram, tpage, clut, window, bounds[0], bounds[1], u1, v1, pc_hw_scale,
                ui_offset);
            if (found < 0) {
                /* The atlas is full: draw what uses it, then start it afresh
                 * (recorded frames sample it: no replays of them). */
                flush();
                pc_ui_upscale_reset();
                forget_recordings();
                found = pc_ui_upscale_lookup(native_vram, tpage, clut, window, bounds[0], bounds[1], u1, v1, pc_hw_scale,
                    ui_offset);
            }
            if (found > 0)
                flags |= FLAG_UI;
        }
    }
    for (int i = 0; i < 3; i++) {
        hw_vertex* v = &batch[batch_count++];
        v->x = anchored ? (has_line_x ? line_x[i] : anchor_x(pos[i * 2]))
            : compress   ? centre + (pos[i * 2] - centre) * 0.75f
            : extend > 0 ? extend_from + (pos[i * 2] - primitive_span[0]) * extend_scale
            : extend < 0 ? extend_from - (primitive_span[1] - pos[i * 2]) * extend_scale
                         : pos[i * 2];
        v->y = fill ? middle + (pos[i * 2 + 1] - middle) * (4.0f / 3.0f) : pos[i * 2 + 1];
        v->z = (float)depth[i];
        v->r = (uint8_t)rgb[i * 3];
        v->g = (uint8_t)rgb[i * 3 + 1];
        v->b = (uint8_t)rgb[i * 3 + 2];
        v->flags = flags;
        v->u = (uint16_t)uv[i * 2];
        v->v = (uint16_t)uv[i * 2 + 1];
        v->tpage = (uint16_t)tpage;
        memcpy(&v->nx, normal, 4);
        v->clut = (uint16_t)clut;
        v->window = window;
        v->uv_min[0] = (uint8_t)bounds[0], v->uv_min[1] = (uint8_t)bounds[1];
        v->uv_max[0] = (uint8_t)bounds[2], v->uv_max[1] = (uint8_t)bounds[3];
        v->ui[0] = ui_offset[0], v->ui[1] = ui_offset[1];
    }
    if (flags & FLAG_UI) {
        const hw_vertex* t = &batch[batch_count - 3];
        pc_ui_upscale_note(fminf(t[0].x, fminf(t[1].x, t[2].x)), fminf(t[0].y, fminf(t[1].y, t[2].y)),
            fmaxf(t[0].x, fmaxf(t[1].x, t[2].x)), fmaxf(t[0].y, fmaxf(t[1].y, t[2].y)));
    }
    has_line_x = 0;
    return 0;
}
/* Rectangle (sprite or tile) as two triangles; uv spans the rectangle 1:1. */
void pc_hw_rect(int x, int y, int w, int h, int u, int v, unsigned rgb, unsigned code,
    unsigned tpage, unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check)
{
    int xy[6], uvs[6], colors[9];
    pc_hw_set_packet(NULL); /* not matched between frames */
    for (int i = 0; i < 9; i++)
        colors[i] = (rgb >> ((i % 3) * 8)) & 255;
    int corners[4][2] = { { 0, 0 }, { w, 0 }, { 0, h }, { w, h } };
    static const int order[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 3; i++) {
            const int* c = corners[order[t][i]];
            xy[i * 2] = x + c[0];
            xy[i * 2 + 1] = y + c[1];
            uvs[i * 2] = u + c[0];
            uvs[i * 2 + 1] = v + c[1];
        }
        pc_hw_triangle(xy, uvs, colors, code, tpage, clut, window, clip, mask_set, mask_check);
    }
}
/* One-pixel-wide line segment as a quad (lines are never textured): span
 * the major axis end to end, including the end pixel, one pixel thick. */
void pc_hw_line(int x0, int y0, int x1, int y1, unsigned c0, unsigned c1, int semi, unsigned tpage,
    const int* clip, int mask_set)
{
    int dx = x1 - x0, dy = y1 - y0;
    int p[4][2];
    pc_hw_set_packet(NULL);
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        int a = x0 + (dx < 0), b = x1 + (dx >= 0);
        int q[4][2] = { { a, y0 }, { b, y1 }, { a, y0 + 1 }, { b, y1 + 1 } };
        memcpy(p, q, sizeof p);
    } else {
        int a = y0 + (dy < 0), b = y1 + (dy >= 0);
        int q[4][2] = { { x0, a }, { x1, b }, { x0 + 1, a }, { x1 + 1, b } };
        memcpy(p, q, sizeof p);
    }
    /* Anchored in widescreen: move each end pixel's centre to its anchored
     * position and keep the line one pixel thick (scaling the quad would
     * thin vertical lines below a pixel). */
    float shift[4] = { 0, 0, 0, 0 };
    int anchored = pc_gte_widescreen && primitive_anchor != PC_ANCHOR_NONE && primitive_anchor != PC_ANCHOR_FILL;
    if (anchored) {
        float s0 = anchor_x(x0 + 0.5f) - 0.5f - (float)x0, s1 = anchor_x(x1 + 0.5f) - 0.5f - (float)x1;
        shift[0] = shift[2] = s0;
        shift[1] = shift[3] = s1;
    }
    int colors[4][3];
    for (int ch = 0; ch < 3; ch++) {
        colors[0][ch] = colors[2][ch] = (c0 >> (ch * 8)) & 255;
        colors[1][ch] = colors[3][ch] = (c1 >> (ch * 8)) & 255;
    }
    static const int order[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
    int uv[6] = { 0 };
    for (int t = 0; t < 2; t++) {
        int xy[6], rgb[9];
        for (int i = 0; i < 3; i++) {
            xy[i * 2] = p[order[t][i]][0];
            xy[i * 2 + 1] = p[order[t][i]][1];
            memcpy(rgb + i * 3, colors[order[t][i]], sizeof colors[0]);
            line_x[i] = (float)xy[i * 2] + shift[order[t][i]];
        }
        has_line_x = anchored && pc_hw_active;
        pc_hw_triangle(xy, uv, rgb, semi ? 2 : 0, tpage, 0, 0, clip, mask_set, 0);
    }
}

/* ---- VRAM operations ---- */

/* Fill (GP0 0x02 / ClearImage) with hardware wrapping already resolved by
 * the caller into an in-range rectangle. */
static void record_fill(int x, int y, int w, int h, int r, int g, int b);
static void record_write(int x, int y, int w, int h);
static void fill_target(GLuint fbo, int x, int y, int w, int h, int r, int g, int b);
void pc_hw_fill(int x, int y, int w, int h, int r, int g, int b)
{
    if (!pc_hw_active || w <= 0 || h <= 0)
        return;
    flush();
    record_fill(x, y, w, h, r, g, b);
    screen_copy_write(0, 0, x, y, w, h, 0);
    fill_target(target_fbo, x, y, w, h, r, g, b);
}
static void fill_target(GLuint fbo, int x, int y, int w, int h, int r, int g, int b)
{
    int s = pc_hw_scale;
    /* A fill over a native list's area clears what hid packets there. */
    if (occlusion_on && x < occlusion_rect[0] + occlusion_rect[2] && occlusion_rect[0] < x + w
        && y < occlusion_rect[1] + occlusion_rect[3] && occlusion_rect[1] < y + h)
        occlusion_on = 0;
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_fill(fbo, x, y, w, h, r, g, b);
        return;
    }
#endif
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_Enable(GL_SCISSOR_TEST);
    gl_Scissor(x * s, y * s, w * s, h * s);
    /* Fills store 5-bit colours, clear the G-buffer (unlit) and reset depth. */
    const float fill[4] = { (r >> 3) / 31.0f, (g >> 3) / 31.0f, (b >> 3) / 31.0f, 0 };
    static const float zero[4] = { 0, 0, 0, 0 }, far_depth = 1.0f;
    gl_ClearBufferfv(GL_COLOR, 0, fill);
    gl_ClearBufferfv(GL_COLOR, 1, zero);
    gl_ClearBufferfv(GL_COLOR, 2, zero);
    gl_ClearBufferfv(GL_COLOR, 3, zero);
    gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
    gl_Disable(GL_SCISSOR_TEST);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
static int widescreen_screen_upload(int x, int y, int w, int h);
/* Native VRAM rectangle (already written by the CPU) into the scaled target. */
void pc_hw_upload(int x, int y, int w, int h)
{
    if (!pc_hw_active || w <= 0 || h <= 0)
        return;
    flush();
    record_write(x, y, w, h);
    screen_copy_write(0, 0, x, y, w, h, 0);
    pc_hw_mark(x, y, x + w - 1, y + h - 1);
    upload_tiles(dirty_tiles & tiles_for(x, y, x + w - 1, y + h - 1));
    if (widescreen_screen_upload(x, y, w, h))
        return;
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_upload_rect(x, y, w, h);
        return;
    }
#endif
    gl_BindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    gl_Viewport(0, 0, 1024 * pc_hw_scale, 512 * pc_hw_scale);
    gl_UseProgram(upload_program);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, native_texture);
    gl_Uniform1i(u_upload_vram, 0);
    gl_Uniform4i(u_upload_rect, x, y, w, h);
    gl_Uniform1f(u_upload_scale, (float)pc_hw_scale);
    gl_BindVertexArray(empty_array);
    gl_DrawBuffers(1, all_buffers); /* the image itself; then mark it unlit */
    gl_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_BindVertexArray(0);
    gl_DrawBuffers(4, all_buffers);
    {
        static const float zero[4] = { 0, 0, 0, 0 };
        int s = pc_hw_scale;
        gl_Enable(GL_SCISSOR_TEST);
        gl_Scissor(x * s, y * s, w * s, h * s);
        gl_ClearBufferfv(GL_COLOR, 1, zero);
        gl_ClearBufferfv(GL_COLOR, 2, zero);
        gl_ClearBufferfv(GL_COLOR, 3, zero);
        gl_Disable(GL_SCISSOR_TEST);
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
/* VRAM-to-VRAM copy (GP0 0x80) in the scaled target, through a scratch copy
 * so overlapping rectangles read the source before it is overwritten. */
void pc_hw_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    if (!pc_hw_active || w <= 0 || h <= 0)
        return;
    flush();
    record_write(dx, dy, w, h);
    screen_copy_write(sx, sy, dx, dy, w, h, 1);
    int s = pc_hw_scale;
    if (sx + w > 1024 || sy + h > 512 || dx + w > 1024 || dy + h > 512) {
        /* Wrapping copies are rare; take them from the exact native result. */
        pc_hw_mark(dx, dy, dx + w - 1, dy + h - 1);
        pc_hw_upload(dx, dy, dx + w > 1024 ? 1024 - dx : w, dy + h > 512 ? 512 - dy : h);
        return;
    }
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_copy(sx, sy, dx, dy, w, h);
        return;
    }
#endif
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, copy_fbo);
    gl_BlitFramebuffer(sx * s, sy * s, (sx + w) * s, (sy + h) * s, 0, 0, w * s, h * s,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, copy_fbo);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fbo);
    gl_DrawBuffers(1, all_buffers); /* a blit writes every draw buffer: colour only */
    gl_BlitFramebuffer(0, 0, w * s, h * s, dx * s, dy * s, (dx + w) * s, (dy + h) * s,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl_DrawBuffers(4, all_buffers);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Change the internal resolution at runtime: rebuild the targets and refill
 * them from native VRAM (one frame shows native-resolution content). */
int pc_hw_set_scale(int scale)
{
    if (!pc_hw_active || scale < 1 || scale > 8 || scale == pc_hw_scale)
        return pc_hw_scale;
    flush();
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        occlusion_on = surface_valid = 0;
        pc_hw_scale = scale;
        pc_vk_hw_resize(scale);
        forget_recordings();
        dirty_tiles = 0xffffffffu;
        pc_hw_upload(0, 0, 1024, 512);
        fprintf(stderr, "[gpu] internal resolution %dx\n", scale);
        return scale;
    }
#endif
    gl_DeleteFramebuffers(1, &target_fbo);
    gl_DeleteTextures(1, &target_texture);
    gl_DeleteFramebuffers(1, &copy_fbo);
    gl_DeleteTextures(1, &copy_texture);
    gl_DeleteTextures(1, &depth_texture);
    gl_DeleteTextures(1, &normal_texture);
    gl_DeleteTextures(1, &albedo_texture);
    gl_DeleteTextures(1, &shade_texture);
    gl_DeleteFramebuffers(1, &replay.fbo);
    GLuint replay_textures[5] = { replay.color, replay.normal, replay.albedo, replay.shade, replay.depth };
    gl_DeleteTextures(5, replay_textures);
    gl_DeleteFramebuffers(1, &room_depth_fbo);
    gl_DeleteTextures(1, &room_depth_texture);
    if (object_fbo)
        gl_DeleteFramebuffers(1, &object_fbo);
    if (object_replay_fbo)
        gl_DeleteFramebuffers(1, &object_replay_fbo);
    object_fbo = object_replay_fbo = 0;
    gl_DeleteFramebuffers(1, &before_fbo);
    gl_DeleteTextures(1, &before_texture);
    occlusion_on = surface_valid = 0;
    pc_hw_scale = scale;
    make_target(&target_texture, &target_fbo);
    make_depth(target_fbo, &depth_texture);
    make_gbuffer(target_fbo, &normal_texture, &albedo_texture, &shade_texture);
    gl_ClearDepth(1.0);
    gl_Clear(GL_DEPTH_BUFFER_BIT);
    make_target(&copy_texture, &copy_fbo);
    make_replay_target();
    make_room_depth();
    make_object_fbos();
    forget_recordings();
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    dirty_tiles = 0xffffffffu;
    pc_hw_upload(0, 0, 1024, 512);
    fprintf(stderr, "[gpu] internal resolution %dx\n", scale);
    return scale;
}

/* ---- native draw lists (render.c) ---- */

static void record_native(uint32_t list, const int* clip, int ox, int oy, unsigned window, int mask);
/* Draw list `list` into `fbo` (the live or the replay target), then keep a
 * room list's depth as the native surfaces (occlusion, object lists). An
 * object list needs those surfaces from earlier in the same ordering table
 * (else its model stays packets). `t` blends palettes (replays). Returns
 * whether the list drew. */
static int draw_native(GLuint fbo, uint32_t list, const int* clip, int ox, int oy, unsigned window, int mask,
    const render_camera* camera, float t)
{
    int objects = render_list_objects(list); /* 1 objects, 2 another room block (layered) */
    if (objects < 0 || (objects && !surface_valid) || (objects == 1 && !pc_native_actors)
        || (objects == 2 && !pc_native_objects))
        return 0;
    if (dirty_tiles)
        upload_tiles(dirty_tiles);
    render_target target;
    target.scale = pc_hw_scale;
    memcpy(target.clip, clip, sizeof target.clip);
    target.ox = ox, target.oy = oy;
    target.window = window;
    target.mask_set = mask;
    target.vram = native_texture;
    target.perspective = pc_hw_perspective;
    target.filtering = pc_hw_texture_filter;
    target.fbo = fbo;
    target.object_fbo = fbo == replay.fbo ? object_replay_fbo : object_fbo;
    target.surface_depth = room_depth_texture;
    target.surface_before = 0;
    if (objects == 1 && pc_native_actor_order) {
        /* The surfaces before this list, for its PS1-ordered instances. */
        int s = pc_hw_scale, x0 = clip[0] * s, y0 = clip[1] * s, x1 = (clip[0] + clip[2]) * s,
            y1 = (clip[1] + clip[3]) * s;
#ifdef ROOD_VULKAN
        if (pc_vk_on())
            pc_vk_hw_copy_depth(room_depth_fbo, before_fbo, x0, y0, x1, y1);
        else
#endif
        {
            gl_BindFramebuffer(GL_READ_FRAMEBUFFER, room_depth_fbo);
            gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, before_fbo);
            gl_BlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        target.surface_before = before_texture;
    }
    target.camera = camera;
    target.t = t;
    target.count_stats = camera == NULL;
    int drawn = render_draw_list(list, &target);
    if (drawn < 0)
        return 0;
    if (objects)
        return 1;
    if (drawn > 0) {
        int s = pc_hw_scale, x0 = clip[0] * s, y0 = clip[1] * s, x1 = (clip[0] + clip[2]) * s,
            y1 = (clip[1] + clip[3]) * s;
#ifdef ROOD_VULKAN
        if (pc_vk_on())
            pc_vk_hw_copy_depth(fbo, room_depth_fbo, x0, y0, x1, y1);
        else
#endif
        {
            gl_BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
            gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, room_depth_fbo);
            gl_BlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        surface_valid = 1;
        occlusion_on = pc_native_occlusion != 0;
        memcpy(occlusion_rect, clip, sizeof occlusion_rect);
    }
    return 1;
}
/* An object list's polygons become this frame's shadow casters (world
 * space, both faces): real geometry instead of the packets' projections. */
static void collect_caster(void* context, const float world[9], const uint8_t rgb[9], const uint8_t uv[6],
    unsigned code, unsigned tpage, unsigned clut)
{
    (void)context;
    if (collect_count >= MAX_CASTER_TRIANGLES)
        return;
    memcpy(caster_collect + collect_count * 9, world, 9 * sizeof(float));
    pc_hw_surface* sf = &surface_collect[collect_count++];
    memcpy(sf->rgb, rgb, sizeof sf->rgb);
    memcpy(sf->uv, uv, sizeof sf->uv);
    sf->code = (uint8_t)code;
    sf->tpage = (uint16_t)tpage;
    sf->clut = (uint16_t)clut;
}
/* Additive semi-transparent polygons of a native object list glow, as the
 * packet path's additive non-room triangles do. */
static void collect_emissive(void* context, const float world[9], const uint8_t rgb[9], const uint8_t uv[6],
    unsigned code, unsigned tpage, unsigned clut)
{
    int effects = context != NULL; /* an effects list (particles) */
    int mode = (int)(tpage >> 5) & 3;
    if (!(code & 2) || (mode != 1 && mode != 3) || emissive_count >= MAX_EMISSIVE_TRIANGLES)
        return;
    /* Its source: a particle system's tag, else the object's mesh (meshes
     * of effects lists live one frame: their texture instead). */
    uint32_t source, mesh;
    render_emit_tag(&source, &mesh);
    emissive_source_collect[emissive_count] = effects
        ? PC_GLOW_ORIGIN_PARTICLES << 28
              | (source ? ((source * 0x9e3779b1u) & 0x07ffffffu) | (source & PC_GLOW_ATTACHED) : texture_tag(tpage, clut) & 0x07ffffffu)
        : PC_GLOW_ORIGIN_OBJECT << 28 | ((mesh * 0x9e3779b1u) & 0x0fffffffu);
    memcpy(emissive_collect + emissive_count * 9, world, 9 * sizeof(float));
    pc_hw_surface* sf = &emissive_surface_collect[emissive_count++];
    memcpy(sf->rgb, rgb, sizeof sf->rgb);
    memcpy(sf->uv, uv, sizeof sf->uv);
    sf->code = (uint8_t)code;
    sf->tpage = (uint16_t)tpage;
    sf->clut = (uint16_t)clut;
}
/* The GPU packet walker reached a native list's marker (render_list_link),
 * drawing with this drawing area, offset, texture window and mask state. */
void pc_hw_native_marker(uint32_t list, int ox, int oy, const int* clip, unsigned window, int mask)
{
    if (!pc_hw_active)
        return;
    /* A list several draws joined has a marker per draw: the first one
     * reached draws it. */
    for (int i = 0; i < drawn_list_count; i++)
        if (drawn_lists[i] == list)
            return;
    flush();
    if (!draw_native(target_fbo, list, clip, ox, oy, window, mask, NULL, 1.0f))
        return; /* an object list that could not draw: its packets draw */
    record_native(list, clip, ox, oy, window, mask);
    if (drawn_list_count < MAX_DRAWN_LISTS)
        drawn_lists[drawn_list_count++] = list;
    if (pc_native_room == 2 || pc_native_actors == 2)
        mark_stale(clip); /* drawn here only: native VRAM lacks it */
    int translucent = render_list_translucent_count(list) > 0;
    if (render_list_objects(list) == 1) {
        cover_objects(list, translucent);
        render_list_triangles(list, collect_caster, NULL);
        if (translucent)
            render_list_translucent_triangles(list, collect_emissive, NULL);
    } else
        cover_room(list, translucent);
}
/* A translucent marker (render_list_link_translucent): entries of a list
 * drawn earlier in this ordering table. */
static int draw_translucent(GLuint fbo, uint32_t list, int first, int count, const int* clip, int ox, int oy,
    unsigned window, int mask, const render_camera* camera, float t)
{
    if (dirty_tiles)
        upload_tiles(dirty_tiles);
    render_target target;
    memset(&target, 0, sizeof target);
    target.scale = pc_hw_scale;
    memcpy(target.clip, clip, sizeof target.clip);
    target.ox = ox, target.oy = oy;
    target.window = window;
    target.mask_set = mask;
    target.vram = native_texture;
    target.perspective = pc_hw_perspective;
    target.filtering = pc_hw_texture_filter;
    target.fbo = fbo;
    target.surface_depth = room_depth_texture;
    target.camera = camera;
    target.t = t;
    target.count_stats = camera == NULL;
    return render_draw_translucent(list, first, count, &target) >= 0;
}
void pc_hw_native_translucent(uint32_t list, int first, int count, int ox, int oy, const int* clip, unsigned window,
    int mask)
{
    if (!pc_hw_active || !surface_valid)
        return;
    int drawn = 0;
    for (int i = 0; i < drawn_list_count && !drawn; i++)
        drawn = drawn_lists[i] == list;
    if (!drawn && render_list_objects(list) == 3) {
        /* An effects list (drop shadows, particles): no list marker; its
         * first translucent marker drawn in this ordering table covers its
         * packets, and its additive polygons glow. */
        int kind = render_list_effects(list);
        if ((kind == RENDER_EFFECTS_SHADOWS && !pc_native_shadows)
            || (kind == RENDER_EFFECTS_SPRITES && !pc_native_effects))
            return;
        cover_objects(list, 2);
        if (drawn_list_count < MAX_DRAWN_LISTS)
            drawn_lists[drawn_list_count++] = list;
        static int effects_context;
        render_list_translucent_triangles(list, collect_emissive, &effects_context);
        drawn = 1;
    }
    if (!drawn)
        return; /* its list did not draw: the packets do */
    typeof(pending_translucent)* t = &pending_translucent;
    if (has_pending_translucent && t->list == list && t->first + t->count == first && t->ox == ox && t->oy == oy
        && !memcmp(t->clip, clip, sizeof t->clip) && t->window == window && t->mask == mask) {
        t->count += count;
        return;
    }
    flush();
    t->list = list, t->first = first, t->count = count;
    memcpy(t->clip, clip, sizeof t->clip);
    t->ox = ox, t->oy = oy, t->window = window, t->mask = mask;
    has_pending_translucent = 1;
}

/* ---- keeping native VRAM current ----
 *
 * Without reference packets (pc_native_room / pc_native_actors 2) the CPU's
 * native VRAM lacks what native draws covered. Every drawing area that
 * happened to is remembered as stale; before the CPU reads VRAM there
 * (StoreImage, VRAM copies, textures sampled by any primitive, RPC
 * screenshots, checkpoints) pc_hw_sync copies the scaled target back into
 * native VRAM (the centre sample of each native pixel, 5 bits per channel as
 * the CPU rasterizer quantizes, the mask bit from alpha), so readers see
 * what the window shows. */
#define MAX_STALE 8
static int stale_rects[MAX_STALE][4], stale_count;
/* Why each is stale: STALE_MISSING, native VRAM lacks what native draws
 * drew; STALE_PLACED, widescreen placed what is drawn there differently
 * (2D compressed or spread over the view, Hor+ 3D). StoreImage reads back
 * only the first: the CPU then reads 4:3 2D as the game drew it, so what it
 * loads back after changing it (the title's copyright fading in over its
 * background, its menu over its background) matches the 2D drawn by the
 * game, compressed. */
static int stale_kinds[MAX_STALE];
static GLuint sync_fbo, sync_texture;
int pc_hw_syncs; /* rectangles copied back (diagnostics) */
static void mark_stale(const int* clip) { mark_stale_kind(clip, STALE_MISSING); }
static void mark_stale_kind(const int* clip, int kind)
{
    if (clip[2] <= 0 || clip[3] <= 0)
        return;
    int x0 = clip[0] < 0 ? 0 : clip[0], y0 = clip[1] < 0 ? 0 : clip[1];
    int x1 = clip[0] + clip[2] > 1024 ? 1024 : clip[0] + clip[2];
    int y1 = clip[1] + clip[3] > 512 ? 512 : clip[1] + clip[3];
    if (x1 <= x0 || y1 <= y0)
        return;
    for (int i = 0; i < stale_count; i++) {
        int* r = stale_rects[i];
        if (x0 >= r[0] && y0 >= r[1] && x1 <= r[0] + r[2] && y1 <= r[1] + r[3]) {
            stale_kinds[i] |= kind; /* already stale */
            return;
        }
        if (r[0] >= x0 && r[1] >= y0 && r[0] + r[2] <= x1 && r[1] + r[3] <= y1) {
            r[0] = x0, r[1] = y0, r[2] = x1 - x0, r[3] = y1 - y0; /* grows */
            stale_kinds[i] |= kind;
            return;
        }
    }
    if (stale_count == MAX_STALE) {
        /* Full: merge the newest into the last (a bounding box). */
        int* r = stale_rects[MAX_STALE - 1];
        int a0 = r[0] < x0 ? r[0] : x0, b0 = r[1] < y0 ? r[1] : y0;
        int a1 = r[0] + r[2] > x1 ? r[0] + r[2] : x1, b1 = r[1] + r[3] > y1 ? r[1] + r[3] : y1;
        r[0] = a0, r[1] = b0, r[2] = a1 - a0, r[3] = b1 - b0;
        stale_kinds[MAX_STALE - 1] |= kind;
        return;
    }
    stale_kinds[stale_count] = kind;
    int* r = stale_rects[stale_count++];
    r[0] = x0, r[1] = y0, r[2] = x1 - x0, r[3] = y1 - y0;
}
static void sync_kinds(int x, int y, int w, int h, int kinds);
void pc_hw_sync(int x, int y, int w, int h) { sync_kinds(x, y, w, h, STALE_MISSING | STALE_PLACED); }
/* StoreImage (boot_runtime.c): only what native VRAM lacks. */
void pc_hw_sync_store(int x, int y, int w, int h)
{
    sync_kinds(x, y, w, h, layout_enabled() ? STALE_MISSING : STALE_MISSING | STALE_PLACED);
}
static void sync_kinds(int x, int y, int w, int h, int kinds)
{
    if (!pc_hw_active || !stale_count || w <= 0 || h <= 0)
        return;
    for (int i = 0; i < stale_count;) {
        int* r = stale_rects[i];
        if (!(stale_kinds[i] & kinds)) {
            ++i;
            continue;
        }
        int x0 = x > r[0] ? x : r[0], y0 = y > r[1] ? y : r[1];
        int x1 = x + w < r[0] + r[2] ? x + w : r[0] + r[2], y1 = y + h < r[1] + r[3] ? y + h : r[1] + r[3];
        if (x1 <= x0 || y1 <= y0) {
            ++i;
            continue;
        }
        static int debug = -1;
        if (debug < 0)
            debug = getenv("ROOD_SYNC_DEBUG") != NULL;
        if (debug)
            fprintf(stderr, "[sync] read %d,%d %dx%d: stale %d,%d %dx%d from %p\n", x, y, w, h, r[0], r[1], r[2], r[3],
                __builtin_return_address(0));
        /* Copy the whole stale rectangle back: it is then current. */
        x0 = r[0], y0 = r[1], x1 = r[0] + r[2], y1 = r[1] + r[3];
        flush();
        static unsigned char pixels[1024 * 512 * 4];
#ifdef ROOD_VULKAN
        if (pc_vk_on()) {
            pc_vk_hw_sync_read(x0, y0, x1, y1, pixels);
            goto read;
        }
#endif
        if (!sync_fbo) {
            gl_GenTextures(1, &sync_texture);
            gl_BindTexture(GL_TEXTURE_2D, sync_texture);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            gl_GenFramebuffers(1, &sync_fbo);
            gl_BindFramebuffer(GL_FRAMEBUFFER, sync_fbo);
            gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sync_texture, 0);
        }
        int s = pc_hw_scale;
        gl_BindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
        gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, sync_fbo);
        gl_BlitFramebuffer(x0 * s, y0 * s, x1 * s, y1 * s, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl_BindFramebuffer(GL_READ_FRAMEBUFFER, sync_fbo);
        gl_PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl_ReadPixels(x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
#ifdef ROOD_VULKAN
    read:;
#endif
        uint16_t* vram = (uint16_t*)(uintptr_t)native_vram;
        for (int row = 0; row < y1 - y0; row++) {
            const unsigned char* src = pixels + (size_t)row * (x1 - x0) * 4;
            uint16_t* dst = vram + (size_t)(y0 + row) * 1024 + x0;
            for (int col = 0; col < x1 - x0; col++, src += 4)
                dst[col] = (uint16_t)((src[0] >> 3) | ((src[1] >> 3) << 5) | ((src[2] >> 3) << 10)
                    | (src[3] >= 128 ? 0x8000 : 0));
        }
        dirty_tiles |= tiles_for(x0, y0, x1 - 1, y1 - 1); /* the sampled copy follows */
        pc_ui_upscale_tiles_changed(tiles_for(x0, y0, x1 - 1, y1 - 1));
        ++pc_hw_syncs;
        memmove(r, stale_rects[i + 1], (size_t)(stale_count - i - 1) * sizeof stale_rects[0]);
        memmove(&stale_kinds[i], &stale_kinds[i + 1], (size_t)(stale_count - i - 1) * sizeof stale_kinds[0]);
        --stale_count;
    }
}
/* A VRAM copy (the hardware target copies too): a stale source makes the
 * destination stale (copied back when something reads it), rather than
 * reading the target back for every copy (the game copies two pixels from
 * the screen's corner every frame). */
void pc_hw_stale_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    for (int i = 0; i < stale_count; i++) {
        const int* r = stale_rects[i];
        if (sx < r[0] + r[2] && r[0] < sx + w && sy < r[1] + r[3] && r[1] < sy + h) {
            int clip[4] = { dx, dy, w, h };
            mark_stale_kind(clip, stale_kinds[i]);
            return;
        }
    }
}
/* A primitive is about to sample texels [u0, u1] x [v0, v1] of texture page
 * `tpage` through texture window `window` (and CLUT `clut`). */
void pc_hw_sync_texture(unsigned tpage, unsigned clut, unsigned window, int u0, int v0, int u1, int v1)
{
    if (!stale_count)
        return;
    int tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256, depth = (tpage >> 7) & 3;
    if ((window & 0x3ff) || u0 < 0 || v0 < 0 || u1 > 255 || v1 > 255 || u1 < u0 || v1 < v0)
        u0 = v0 = 0, u1 = v1 = 255; /* windowed or wrapping: the whole page */
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int x0 = tx + (u0 >> shift), x1 = tx + (u1 >> shift);
    pc_hw_sync(x0, ty + v0, (x1 > 1023 ? 1023 : x1) - x0 + 1, v1 - v0 + 1);
    if (depth < 2) {
        if (getenv("ROOD_SYNC_DEBUG"))
            fprintf(stderr, "[sync] texture page %x clut %x window %x uv %d,%d..%d,%d packet %p\n", tpage, clut, window,
                u0, v0, u1, v1, (void*)current_packet);
        pc_hw_sync((clut & 63) * 16, (clut >> 6) & 511, depth ? 256 : 16, 1);
    }
}

/* ---- frame interpolation ----
 *
 * The game draws 30 frames a second on a 60 Hz display. Each frame's draw
 * stream is recorded as it is mirrored here (batches with their state, fills,
 * and per triangle its world-space position and surface). When a new frame B
 * is presented after frame A, a frame halfway between them is replayed into a
 * second target and shown first; B itself follows on the next vsync. The
 * camera is interpolated, so every 3D triangle moves smoothly; triangles of
 * moving things (characters) are matched to A's nearest triangle with the
 * same surface (code, texture page, CLUT, UVs) and interpolated in world
 * space. Frames that write the display some other way
 * (uploads, copies: movies, screen effects) are shown as they are. */
int pc_hw_interpolate = 1;
/* Images presented per vsync (1-8; ROOD_PRESENT_RATE, RPC graphics
 * present_rate): with more than one, boot_runtime.c presents again at even
 * fractions of the vsync (pc_present_phase), each an in-between at its own
 * time: groundwork for frame rates above 60 (the game still runs at its
 * own rate). */
int pc_present_rate = 1;
float pc_present_phase;
#define MAX_RECORDED_VERTICES (3 * MAX_VERTICES)
#define MAX_RECORDED_OPS 4096
#define MATCH_SLOTS 131072
typedef struct {
    float world[9];
    uint32_t surface;  /* hash of code, tpage, clut and UVs */
    uint8_t has_world, keyed;
} recorded_triangle;
typedef struct {
    /* 1: fill (rect, rgb in vertex fields); 2: native list (first = id,
     * count/blend = offset); 3: translucent entries (as 2, range in
     * range_first/range_count) */
    int fill;
    int blend, clip[4], first, count;
    unsigned window, mask;
    int range_first, range_count;
    /* Batches (fill 0): the native VRAM they sample (texels, CLUTs; x0, y0,
     * x1, y1 exclusive, empty when x1 <= x0), where they drew, and whether
     * any triangle is 3D (has a world position). */
    int16_t texels[4], cluts[4], bounds[4];
    uint8_t has_world;
} recorded_op;
typedef struct {
    hw_vertex vertices[MAX_RECORDED_VERTICES];
    recorded_triangle triangles[MAX_RECORDED_VERTICES / 3];
    recorded_op ops[MAX_RECORDED_OPS];
    int vertex_count, triangle_count, op_count, overflow, writes;
    int write_rects[16][5]; /* x, y, w, h, ops drawn before it */
    float r[3][3], t[3], h, ofx, ofy;
    int widescreen, valid;
    uint32_t slots[MATCH_SLOTS]; /* surface -> start of its run in by_surface + 1 */
    int by_surface[MAX_RECORDED_VERTICES / 3], grouped; /* keyed triangles sorted by surface */
} recording;
/* Three slots: the frame being recorded, the newest finished frame (B)
 * and the one before it (A). */
static recording recordings[3];
static int recording_index, newest = 1, previous = 2;
static unsigned arrival[3]; /* vsync each finished frame was presented at */
static int display_rect[4]; /* the display area (pc_hw_set_display) */
static int shown_rect[4];   /* the area presented: the display area, or B's area for its replay */
/* Widescreen: a picture the game loads straight into a frame buffer (the
 * title screen's background, loaded before the title's display is set;
 * pc_hw_upload_mode 2) is copied 1:1 and would spread over the 16:9 view.
 * At the first present that shows it, the display area is drawn again from
 * native VRAM compressed about its centre like other 2D (the 2D the game
 * drew over it since is compressed there too), the sides black, so it keeps
 * its proportions. */
static int picture_rect[4], picture_pending;
/* Draw native VRAM rectangle (x, y, w, h) into the target compressed about
 * the centre of screen area `screen` (as 2D; 15-bit texture pages are 256
 * texels square from multiples of 64; black texels stay transparent). */
static void draw_compressed_rect(int x, int y, int w, int h, const int* screen)
{
    int anchor = primitive_anchor;
    primitive_anchor = PC_ANCHOR_NONE;
    for (int ty = y; ty < y + h;) {
        int py = ty & ~255, rows = (py + 256 < y + h ? py + 256 : y + h) - ty;
        for (int tx = x; tx < x + w;) {
            int px = tx & ~63, cols = x + w - tx;
            cols = cols > 256 - (tx - px) ? 256 - (tx - px) : cols;
            unsigned tpage = (unsigned)(px >> 6) | (unsigned)((py >> 8) << 4) | (2u << 7);
            pc_hw_set_2d(1);
            pc_hw_rect(tx, ty, cols, rows, tx - px, ty - py, 0x808080, 0x65, tpage, 0, 0, screen, 0, 0);
            tx += cols;
        }
        ty += rows;
    }
    pc_hw_set_2d(0);
    primitive_anchor = anchor;
    flush();
}
/* Widescreen: the game code says what its next LoadImage is
 * (pc_hw_upload_mode, pc/src/widescreen.h): 1, a 2D image loaded into the
 * shown frame buffer (the title's copyright fading in), drawn compressed
 * like other 2D instead of 1:1; 2, a picture loaded into a frame buffer
 * before it is shown (the title's background), drawn compressed at the
 * first present that shows it (widescreen_picture). */
static int upload_mode;
void pc_hw_upload_mode(int mode) { upload_mode = mode; }
static int widescreen_screen_upload(int x, int y, int w, int h)
{
    int mode = upload_mode;
    upload_mode = 0;
    if (mode == 2) {
        picture_rect[0] = x, picture_rect[1] = y, picture_rect[2] = w, picture_rect[3] = h;
        picture_pending = pc_gte_widescreen && layout_enabled();
        return 0; /* (1:1 until it is shown) */
    }
    const int* d = display_rect;
    if (mode != 1 || !pc_gte_widescreen || !layout_enabled() || x < d[0] || y < d[1] || x + w > d[0] + d[2]
        || y + h > d[1] + d[3])
        return 0;
    static int logged;
    if (getenv("ROOD_WIDESCREEN_DEBUG") && logged++ < 40)
        fprintf(stderr, "[widescreen] image loaded into the screen %d,%d %dx%d: drawn compressed\n", x, y, w, h);
    int rect[4] = { x, y, w, h };
    mark_stale_kind(rect, STALE_PLACED);
    draw_compressed_rect(x, y, w, h, d);
    return 1;
}
static void widescreen_picture(const int* d)
{
    const int* p = picture_rect;
    /* Widescreen switched while the picture shows: drawn again (from native
     * VRAM: the picture and the 2D the game drew over it) 1:1 or compressed. */
    static int compressed;
    int wanted = pc_gte_widescreen && layout_enabled();
    if (d[2] <= 0 || d[3] <= 0 || p[2] <= 0)
        return;
    if (p[0] > d[0] || p[0] + p[2] < d[0] + d[2] || p[1] > d[1] + d[3] / 4 || p[1] + p[3] < d[1] + d[3] * 3 / 4) {
        if (!picture_pending)
            picture_rect[2] = 0; /* shown and left: forgotten */
        return; /* not shown (yet) */
    }
    if (!picture_pending && wanted == compressed)
        return;
    picture_pending = 0;
    compressed = wanted;
    if (!wanted) {
        flush();
        pc_hw_upload(d[0], d[1], d[2], d[3]);
        return;
    }
    int y0 = p[1] > d[1] ? p[1] : d[1], y1 = p[1] + p[3] < d[1] + d[3] ? p[1] + p[3] : d[1] + d[3];
    static int logged;
    if (getenv("ROOD_WIDESCREEN_DEBUG") && logged++ < 20)
        fprintf(stderr, "[widescreen] picture loaded into the display %d,%d %dx%d: drawn compressed\n", p[0], p[1],
            p[2], p[3]);
    flush();
    record_fill(d[0], y0, d[2], y1 - y0, 0, 0, 0);
    fill_target(target_fbo, d[0], y0, d[2], y1 - y0, 0, 0, 0);
    int clip[4] = { d[0], y0, d[2], y1 - y0 };
    draw_compressed_rect(d[0], y0, d[2], y1 - y0, clip);
}
static int presenting_replay;
static float present_blend = 1.0f; /* the replay's t while presenting_replay */
/* The next triangles come from this GPU packet (boot_polygons.inc). */
void pc_hw_set_packet(const void* packet)
{
    current_packet = (uintptr_t)packet;
}
static recording* recording_now(void) { return &recordings[recording_index]; }
static void record_triangle(const float* world, unsigned code, unsigned tpage, unsigned clut, const int* uv)
{
    recording* rec = recording_now();
    if (rec->triangle_count >= MAX_RECORDED_VERTICES / 3) {
        rec->overflow = 1;
        return;
    }
    recorded_triangle* t = &rec->triangles[rec->triangle_count++];
    t->has_world = world != NULL;
    if (world)
        memcpy(t->world, world, sizeof t->world);
    uint32_t h = 2166136261u;
    uint32_t parts[9] = { code & 0xfd, tpage & 0x1ff, clut, (uint32_t)uv[0], (uint32_t)uv[1], (uint32_t)uv[2],
        (uint32_t)uv[3], (uint32_t)uv[4], (uint32_t)uv[5] };
    for (int i = 0; i < 9; i++)
        h = (h ^ parts[i]) * 16777619u;
    t->surface = h;
    t->keyed = current_packet != 0 && !primitive_room; /* the room never moves */
}
static void record_batch(void)
{
    recording* rec = recording_now();
    if (rec->vertex_count + batch_count > MAX_RECORDED_VERTICES || rec->op_count >= MAX_RECORDED_OPS) {
        rec->overflow = 1;
        return;
    }
    recorded_op* op = &rec->ops[rec->op_count++];
    op->fill = 0;
    op->blend = batch_blend;
    memcpy(op->clip, batch_clip, sizeof op->clip);
    op->first = rec->vertex_count;
    op->count = batch_count;
    memcpy(rec->vertices + rec->vertex_count, batch, (size_t)batch_count * sizeof *batch);
    /* What it samples and covers (replay_frame: rewritten since?). */
    int texels[4] = { 1024, 512, 0, 0 }, cluts[4] = { 1024, 512, 0, 0 }, bounds[4] = { 1024, 512, 0, 0 };
    op->has_world = 0;
    for (int v = 0; v < batch_count; v++) {
        const hw_vertex* p = &batch[v];
        int x = (int)floorf(p->x), y = (int)floorf(p->y), x1 = (int)ceilf(p->x) + 1, y1 = (int)ceilf(p->y) + 1;
        bounds[0] = x < bounds[0] ? x : bounds[0], bounds[1] = y < bounds[1] ? y : bounds[1];
        bounds[2] = x1 > bounds[2] ? x1 : bounds[2], bounds[3] = y1 > bounds[3] ? y1 : bounds[3];
        if (v % 3 == 0 && rec->triangles[(rec->vertex_count + v) / 3].has_world)
            op->has_world = 1;
        if (v % 3 || !(p->flags & FLAG_TEXTURED))
            continue;
        int tx = (p->tpage & 15) * 64, ty = ((p->tpage >> 4) & 1) * 256, depth = (p->tpage >> 7) & 3;
        int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
        int u0 = p->uv_min[0], v0 = p->uv_min[1], u1 = p->uv_max[0], v1 = p->uv_max[1];
        if (p->window & 0x3ff)
            u0 = v0 = 0, u1 = v1 = 255; /* windowed: the whole page */
        int r[4] = { tx + (u0 >> shift), ty + v0, tx + (u1 >> shift) + 1, ty + v1 + 1 };
        texels[0] = r[0] < texels[0] ? r[0] : texels[0], texels[1] = r[1] < texels[1] ? r[1] : texels[1];
        texels[2] = r[2] > texels[2] ? r[2] : texels[2], texels[3] = r[3] > texels[3] ? r[3] : texels[3];
        if (depth < 2) {
            int cx = (p->clut & 63) * 16, cy = (p->clut >> 6) & 511;
            int c[4] = { cx, cy, cx + (depth ? 256 : 16), cy + 1 };
            cluts[0] = c[0] < cluts[0] ? c[0] : cluts[0], cluts[1] = c[1] < cluts[1] ? c[1] : cluts[1];
            cluts[2] = c[2] > cluts[2] ? c[2] : cluts[2], cluts[3] = c[3] > cluts[3] ? c[3] : cluts[3];
        }
    }
    for (int i = 0; i < 4; i++) {
        op->texels[i] = (int16_t)texels[i];
        op->cluts[i] = (int16_t)cluts[i];
        op->bounds[i] = (int16_t)(bounds[i] < -4096 ? -4096 : bounds[i] > 4096 ? 4096 : bounds[i]);
    }
    rec->vertex_count += batch_count;
}
/* Whether a VRAM write (x, y, w, h) changed what batch `op` sampled. */
static int write_hits(const int* write, const int16_t* r)
{
    return r[2] > r[0] && write[0] < r[2] && r[0] < write[0] + write[2] && write[1] < r[3] && r[1] < write[1] + write[3];
}
/* A 2D batch of `b` whose texels or CLUT were rewritten after it drew (by
 * `b` itself later in the frame, or by `later`, the frame being recorded):
 * the game swaps a CLUT in for one sprite and back (the memory card menu's
 * save thumbnails), so redrawn now it would sample the wrong colours. */
static int op_rewritten(const recording* b, int o, const recording* later)
{
    const recorded_op* op = &b->ops[o];
    if (op->fill || op->has_world)
        return 0; /* 3D: redrawn moved, with the textures as they are now */
    for (int pass = 0; pass < 2; pass++) {
        const recording* rec = pass ? later : b;
        if (!rec)
            continue;
        for (int i = 0; i < rec->writes && i < 16; i++) {
            const int* w = rec->write_rects[i];
            if (rec == b && w[4] <= o)
                continue; /* written before it drew */
            if (write_hits(w, op->texels) || write_hits(w, op->cluts))
                return 1;
        }
    }
    return 0;
}
static void record_fill(int x, int y, int w, int h, int r, int g, int b)
{
    recording* rec = recording_now();
    if (rec->op_count >= MAX_RECORDED_OPS) {
        rec->overflow = 1;
        return;
    }
    recorded_op* op = &rec->ops[rec->op_count++];
    op->fill = 1;
    op->clip[0] = x, op->clip[1] = y, op->clip[2] = w, op->clip[3] = h;
    op->first = r, op->count = g, op->blend = b;
}
static void record_native(uint32_t list, const int* clip, int ox, int oy, unsigned window, int mask)
{
    recording* rec = recording_now();
    if (rec->op_count >= MAX_RECORDED_OPS) {
        rec->overflow = 1;
        return;
    }
    recorded_op* op = &rec->ops[rec->op_count++];
    op->fill = 2;
    memcpy(op->clip, clip, sizeof op->clip);
    op->first = (int)list, op->count = ox, op->blend = oy;
    op->window = window, op->mask = (unsigned)mask;
}
static void record_translucent(uint32_t list, int first, int count, const int* clip, int ox, int oy, unsigned window,
    int mask)
{
    recording* rec = recording_now();
    if (rec->op_count >= MAX_RECORDED_OPS) {
        rec->overflow = 1;
        return;
    }
    recorded_op* op = &rec->ops[rec->op_count++];
    op->fill = 3;
    memcpy(op->clip, clip, sizeof op->clip);
    op->first = (int)list, op->count = ox, op->blend = oy;
    op->window = window, op->mask = (unsigned)mask;
    op->range_first = first, op->range_count = count;
}
static void record_write(int x, int y, int w, int h)
{
    recording* rec = recording_now();
    if (rec->writes < 16) {
        int* q = rec->write_rects[rec->writes];
        q[0] = x, q[1] = y, q[2] = w, q[3] = h, q[4] = rec->op_count;
    }
    ++rec->writes;
}
static const recorded_triangle* sort_base;
static int compare_surface(const void* a, const void* b)
{
    const recorded_triangle *p = &sort_base[*(const int*)a], *q = &sort_base[*(const int*)b];
    return p->surface < q->surface ? -1 : p->surface > q->surface;
}
/* The draw of `rec` (with `rooms`, its native room list draws only) with
 * the most area inside rect (x, y, w, h), or NULL. */
static const recorded_op* recording_op(const recording* rec, const int* rect, int rooms, int* overlap)
{
    const recorded_op* best = NULL;
    *overlap = 0;
    for (int o = 0; o < rec->op_count; o++) {
        const recorded_op* op = &rec->ops[o];
        if (rooms && (op->fill != 2 || render_list_objects((uint32_t)op->first) != 0))
            continue;
        const int* c = op->clip;
        int w = (c[0] + c[2] < rect[0] + rect[2] ? c[0] + c[2] : rect[0] + rect[2]) - (c[0] > rect[0] ? c[0] : rect[0]);
        int h = (c[1] + c[3] < rect[1] + rect[3] ? c[1] + c[3] : rect[1] + rect[3]) - (c[1] > rect[1] ? c[1] : rect[1]);
        if (w > 0 && h > 0 && w * h > *overlap)
            *overlap = w * h, best = op;
    }
    return best;
}
static uint32_t recording_room_list(const recording* rec, const int* rect, int* overlap)
{
    const recorded_op* op = recording_op(rec, rect, 1, overlap);
    return op ? (uint32_t)op->first : 0;
}
/* The camera `rec` was drawn with: its room list's (the game builds a
 * frame's ordering table with the camera of that frame, while the GTE
 * camera captured for the scene already belongs to the frame after it),
 * else the scene camera (packet path). */
static int recording_camera(const recording* rec, float r[3][3], float t[3], float* h, float* ofx, float* ofy)
{
    static const int everywhere[4] = { -4096, -4096, 8192, 8192 };
    int overlap;
    render_camera camera;
    uint32_t list = recording_room_list(rec, everywhere, &overlap);
    if (list && render_list_camera(list, &camera)) {
        memcpy(r, camera.rotation, sizeof camera.rotation);
        memcpy(t, camera.translation, sizeof camera.translation);
        *h = camera.h, *ofx = camera.ofx, *ofy = camera.ofy;
        return 1;
    }
    return pc_scene_game_camera(r, t, h, ofx, ofy);
}
/* Close the recording: drawing that rewrote the display area by other means
 * (uploads, copies) makes it unusable; group keyed triangles by surface. */
static void finish_recording(recording* rec)
{
    rec->valid = !rec->overflow && rec->op_count > 0;
    /* A write matters when it lands on what this frame already drew (it is
     * then part of the picture the replay cannot redraw); tiny ones (sync
     * pixels) never matter. */
    for (int i = 0; i < rec->writes && i < 16 && rec->valid; i++)
        for (int k = 0; k < rec->write_rects[i][4] && rec->write_rects[i][2] * rec->write_rects[i][3] >= 64; k++) {
            const int *a = rec->write_rects[i], *c = rec->ops[k].clip;
            if (a[0] < c[0] + c[2] && c[0] < a[0] + a[2] && a[1] < c[1] + c[3] && c[1] < a[1] + a[3]) {
                rec->valid = 0;
                break;
            }
        }
    if (rec->writes > 16)
        rec->valid = 0;
    if (!recording_camera(rec, rec->r, rec->t, &rec->h, &rec->ofx, &rec->ofy))
        rec->valid = 0;
    rec->widescreen = pc_gte_widescreen;
    memset(rec->slots, 0, sizeof rec->slots);
    rec->grouped = 0;
    if (!rec->valid)
        return;
    for (int i = 0; i < rec->triangle_count; i++)
        if (rec->triangles[i].keyed && rec->triangles[i].has_world)
            rec->by_surface[rec->grouped++] = i;
    sort_base = rec->triangles;
    qsort(rec->by_surface, (size_t)rec->grouped, sizeof *rec->by_surface, compare_surface);
    /* One slot per surface: where its run starts in by_surface. */
    for (int i = 0; i < rec->grouped; i++) {
        uint32_t surface = rec->triangles[rec->by_surface[i]].surface;
        if (i && rec->triangles[rec->by_surface[i - 1]].surface == surface)
            continue;
        for (uint32_t slot = (surface * 2654435761u) & (MATCH_SLOTS - 1);; slot = (slot + 1) & (MATCH_SLOTS - 1))
            if (!rec->slots[slot]) {
                rec->slots[slot] = (uint32_t)i + 1;
                break;
            }
    }
}
/* The triangle of `rec` with the same surface as `tri` that is nearest to it
 * (identical surfaces abound: barrel lids, a character's mirrored limbs, and
 * the set drawn changes as things come into view, so order cannot match). */
static const recorded_triangle* find_match(const recording* rec, const recorded_triangle* tri)
{
    for (uint32_t slot = (tri->surface * 2654435761u) & (MATCH_SLOTS - 1);; slot = (slot + 1) & (MATCH_SLOTS - 1)) {
        uint32_t v = rec->slots[slot];
        if (!v)
            return NULL;
        if (rec->triangles[rec->by_surface[v - 1]].surface != tri->surface)
            continue;
        const recorded_triangle* best = NULL;
        float best_distance = 1e30f;
        for (int i = (int)v - 1, n = 0; i < rec->grouped && n < 256; i++, n++) {
            const recorded_triangle* c = &rec->triangles[rec->by_surface[i]];
            if (c->surface != tri->surface)
                break;
            float distance = 0;
            for (int k = 0; k < 9; k++)
                distance = fmaxf(distance, fabsf(c->world[k] - tri->world[k]));
            if (distance < best_distance)
                best_distance = distance, best = c;
        }
        return best;
    }
}
/* Camera halfway (t) between two recordings, for the present pass too. */
static float replay_r[3][3], replay_t[3], replay_h, replay_ofx, replay_ofy;
int pc_hw_replay_camera(float r[3][3], float t[3])
{
    if (!presenting_replay)
        return 0;
    memcpy(r, replay_r, sizeof replay_r);
    memcpy(t, replay_t, sizeof replay_t);
    return 1;
}
int pc_hw_replay_projection(float* h, float* ofx, float* ofy)
{
    if (!presenting_replay)
        return 0;
    *h = replay_h, *ofx = replay_ofx, *ofy = replay_ofy;
    return 1;
}
/* The finished recording whose drawing is in the area being presented. The
 * game draws each frame into the buffer that is not on screen and shows it
 * a frame later, so this is the one before the newest (A) on the vsync B
 * arrives, whose ordering table was built a frame before that: the scene's
 * current GTE camera is ahead of the picture by up to two game frames while
 * the camera moves. Interpolated frames present B's area (B, replayed).
 * *replayed: the replay redrew that area, else it is the live picture. */
static const recording* shown_recording(int* replayed, uint32_t* room)
{
    const recording *a = &recordings[previous], *b = &recordings[newest];
    int overlap_a = 0, overlap_b = 0;
    uint32_t room_b = recording_room_list(b, shown_rect, &overlap_b);
    uint32_t room_a = recording_room_list(a, shown_rect, &overlap_a);
    *replayed = 0;
    if (room_b && overlap_b >= overlap_a) {
        *replayed = presenting_replay;
        *room = room_b;
        return b;
    }
    if (room_a) {
        *room = room_a;
        return a;
    }
    return NULL;
}
/* The camera the presented picture's surfaces were drawn with (scene
 * capture's camera queries take it first): what reconstructs world
 * positions from its depth (lighting, path tracing, picking) must use it,
 * or surfaces move along the view rays and lateral to them while the camera
 * moves. 0 when no native room list drew the display area. */
int pc_hw_view_camera(float r[3][3], float t[3], float* h, float* ofx, float* ofy)
{
    int replayed;
    uint32_t room;
    render_camera camera;
    if (!shown_recording(&replayed, &room))
        return 0;
    if (replayed) {
        memcpy(r, replay_r, sizeof replay_r);
        memcpy(t, replay_t, sizeof replay_t);
        *h = replay_h, *ofx = replay_ofx, *ofy = replay_ofy;
        return 1;
    }
    if (!render_list_camera(room, &camera))
        return 0;
    memcpy(r, camera.rotation, sizeof camera.rotation);
    memcpy(t, camera.translation, sizeof camera.translation);
    *h = camera.h, *ofx = camera.ofx, *ofy = camera.ofy;
    return 1;
}
static void project(const float r[3][3], const float t[3], const float* w, float h, float hx, float ofx, float ofy,
    float out[3])
{
    float v[3];
    for (int i = 0; i < 3; i++)
        v[i] = r[i][0] * w[0] + r[i][1] * w[1] + r[i][2] * w[2] + t[i];
    out[2] = v[2];
    out[0] = v[2] > 1 ? ofx + hx * v[0] / v[2] : 0;
    out[1] = v[2] > 1 ? ofy + h * v[1] / v[2] : 0;
}
/* The live target's rectangle (VRAM pixels, x1/y1 exclusive) into the replay
 * target: picture, G-buffer and depth, as B drew them. */
static void copy_to_replay(int x0, int y0, int x1, int y1)
{
    int s = pc_hw_scale;
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_replay_copy(x0 * s, y0 * s, x1 * s, y1 * s);
        return;
    }
#endif
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, replay.fbo);
    for (int i = 0; i < 4; i++) {
        gl_ReadBuffer(all_buffers[i]);
        gl_DrawBuffers(1, &all_buffers[i]);
        gl_BlitFramebuffer(x0 * s, y0 * s, x1 * s, y1 * s, x0 * s, y0 * s, x1 * s, y1 * s, GL_COLOR_BUFFER_BIT,
            GL_NEAREST);
    }
    gl_BlitFramebuffer(x0 * s, y0 * s, x1 * s, y1 * s, x0 * s, y0 * s, x1 * s, y1 * s, GL_DEPTH_BUFFER_BIT,
        GL_NEAREST);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, replay.fbo);
    gl_DrawBuffers(4, all_buffers);
    gl_BindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    gl_DrawBuffers(4, all_buffers);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
/* Draw B as it would look at fraction `t` of the way from A. */
static void replay_frame(const recording* a, const recording* b, float t)
{
    static hw_vertex scratch[MAX_VERTICES];
    for (int i = 0; i < 3; i++) {
        replay_t[i] = a->t[i] + (b->t[i] - a->t[i]) * t;
        for (int k = 0; k < 3; k++)
            replay_r[i][k] = a->r[i][k] + (b->r[i][k] - a->r[i][k]) * t;
    }
    /* The projection too (cutscene cameras zoom: H changes frame to frame). */
    replay_h = a->h + (b->h - a->h) * t;
    replay_ofx = a->ofx + (b->ofx - a->ofx) * t;
    replay_ofy = a->ofy + (b->ofy - a->ofy) * t;
    float hx = b->h * (b->widescreen ? 0.75f : 1.0f), replay_hx = replay_h * (b->widescreen ? 0.75f : 1.0f);
    /* Start from B over the displayed area and everything B drew, so
     * anything not replayed is B: images the game uploads straight into
     * the framebuffer (save thumbnails, icons) are not draw commands. */
    int x0 = shown_rect[0], y0 = shown_rect[1], x1 = shown_rect[0] + shown_rect[2],
        y1 = shown_rect[1] + shown_rect[3], s = pc_hw_scale;
    if (shown_rect[2] <= 0 || shown_rect[3] <= 0)
        x0 = 1024, y0 = 512, x1 = 0, y1 = 0;
    for (int o = 0; o < b->op_count; o++) {
        const int* c = b->ops[o].clip;
        x0 = c[0] < x0 ? c[0] : x0, y0 = c[1] < y0 ? c[1] : y0;
        x1 = c[0] + c[2] > x1 ? c[0] + c[2] : x1, y1 = c[1] + c[3] > y1 ? c[1] + c[3] : y1;
    }
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > 1024 ? 1024 : x1, y1 = y1 > 512 ? 512 : y1;
    copy_to_replay(x0, y0, x1, y1);
    int occlusion_was = occlusion_on, surface_was = surface_valid, occlusion_rect_was[4];
    memcpy(occlusion_rect_was, occlusion_rect, sizeof occlusion_rect_was);
    occlusion_on = surface_valid = 0;
    for (int o = 0; o < b->op_count; o++) {
        const recorded_op* op = &b->ops[o];
        if (op->fill == 2 || op->fill == 3) {
            render_camera camera;
            memcpy(camera.rotation, replay_r, sizeof camera.rotation);
            memcpy(camera.translation, replay_t, sizeof camera.translation);
            camera.h = replay_h, camera.ofx = replay_ofx, camera.ofy = replay_ofy;
            camera.widescreen = b->widescreen;
            if (op->fill == 3) {
                if (surface_valid)
                    draw_translucent(replay.fbo, (uint32_t)op->first, op->range_first, op->range_count, op->clip,
                        op->count, op->blend, op->window, (int)op->mask, &camera, t);
            } else
                draw_native(replay.fbo, (uint32_t)op->first, op->clip, op->count, op->blend, op->window, (int)op->mask,
                    &camera, t);
            continue;
        }
        if (op_rewritten(b, o, recording_now())) {
            /* Its texels changed since: B's own pixels where it drew. */
            int x0 = op->bounds[0] > op->clip[0] ? op->bounds[0] : op->clip[0];
            int y0 = op->bounds[1] > op->clip[1] ? op->bounds[1] : op->clip[1];
            int x1 = op->bounds[2] < op->clip[0] + op->clip[2] ? op->bounds[2] : op->clip[0] + op->clip[2];
            int y1 = op->bounds[3] < op->clip[1] + op->clip[3] ? op->bounds[3] : op->clip[1] + op->clip[3];
            x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > 1024 ? 1024 : x1, y1 = y1 > 512 ? 512 : y1;
            if (x1 > x0 && y1 > y0)
                copy_to_replay(x0, y0, x1, y1);
            continue;
        }
        if (op->fill) {
            fill_target(replay.fbo, op->clip[0], op->clip[1], op->clip[2], op->clip[3], op->first, op->count,
                op->blend);
            continue;
        }
        memcpy(scratch, b->vertices + op->first, (size_t)op->count * sizeof *scratch);
        for (int v = 0; v < op->count; v += 3) {
            const recorded_triangle* tri = &b->triangles[(op->first + v) / 3];
            if (!tri->has_world)
                continue;
            const recorded_triangle* old = tri->keyed ? find_match(a, tri) : NULL;
            float world[9];
            memcpy(world, tri->world, sizeof world);
            if (old && old->has_world) {
                float jump = 0;
                for (int k = 0; k < 9; k++)
                    jump = fmaxf(jump, fabsf(old->world[k] - tri->world[k]));
                if (jump < 256)
                    for (int k = 0; k < 9; k++)
                        world[k] = old->world[k] + (tri->world[k] - old->world[k]) * t;
            }
            float view[3][3];
            int ok = 1;
            for (int k = 0; k < 3; k++) {
                float was[3], now[3];
                project(b->r, b->t, tri->world + k * 3, b->h, hx, b->ofx, b->ofy, was);
                project(replay_r, replay_t, world + k * 3, replay_h, replay_hx, replay_ofx, replay_ofy, now);
                if (was[2] <= 1 || now[2] <= 1) {
                    ok = 0;
                    break;
                }
                hw_vertex* out = &scratch[v + k];
                out->x += now[0] - was[0];
                out->y += now[1] - was[1];
                out->z = now[2];
                view[k][2] = now[2];
                view[k][0] = (now[0] - replay_ofx) * now[2] / replay_hx;
                view[k][1] = (now[1] - replay_ofy) * now[2] / replay_h;
            }
            if (!ok) {
                memcpy(&scratch[v], &b->vertices[op->first + v], 3 * sizeof *scratch);
                continue;
            }
            if (scratch[v].lit) {
                float e1[3], e2[3], n[3];
                for (int k = 0; k < 3; k++)
                    e1[k] = view[1][k] - view[0][k], e2[k] = view[2][k] - view[0][k];
                n[0] = e1[1] * e2[2] - e1[2] * e2[1];
                n[1] = e1[2] * e2[0] - e1[0] * e2[2];
                n[2] = e1[0] * e2[1] - e1[1] * e2[0];
                float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (length > 1e-6f) {
                    if (n[0] * view[0][0] + n[1] * view[0][1] + n[2] * view[0][2] > 0)
                        length = -length;
                    for (int k = 0; k < 3; k++) {
                        int8_t c = (int8_t)(n[k] / length * 127.0f);
                        int8_t* dst = k == 0 ? &scratch[v].nx : k == 1 ? &scratch[v].ny : &scratch[v].nz;
                        *dst = c;
                    }
                    for (int k = 1; k < 3; k++) {
                        scratch[v + k].nx = scratch[v].nx;
                        scratch[v + k].ny = scratch[v].ny;
                        scratch[v + k].nz = scratch[v].nz;
                    }
                }
            }
        }
        draw_batch(replay.fbo, scratch, op->count, op->blend, op->clip);
    }
    occlusion_on = occlusion_was;
    surface_valid = surface_was;
    memcpy(occlusion_rect, occlusion_rect_was, sizeof occlusion_rect);
}

/* The display area being presented (VRAM pixels), for the replay. */
void pc_hw_set_display(int x, int y, int w, int h)
{
    display_rect[0] = x, display_rect[1] = y, display_rect[2] = w, display_rect[3] = h;
    widescreen_picture(display_rect);
    memcpy(shown_rect, display_rect, sizeof shown_rect);
    screen_display(x, y, w, h);
    /* Widescreen: the target's picture is not the CPU's (2D compressed or
     * spread over the view only here), so whatever reads the frame buffer
     * (screen copies sampled as textures, StoreImage) takes it from the
     * target. */
    if (pc_gte_widescreen && cover_enabled())
        mark_stale_kind(display_rect, STALE_PLACED);
}
/* Developer RPC (ui_upscale redraw): draw the newest recorded frame again,
 * as recorded (t = 1), into the replay target, so drawing options can be
 * compared on the very same frame. `area`: the drawing area it covers. */
static int force_replay;
int pc_hw_redraw(int area[4])
{
    if (!pc_hw_active)
        return -1;
    flush();
    static const int everywhere[4] = { -4096, -4096, 8192, 8192 };
    int overlap;
    recording* b = &recordings[newest];
    const recorded_op* op = recording_op(b, everywhere, 0, &overlap);
    if (!op)
        return -1;
    memcpy(area, op->clip, 4 * sizeof(int));
    replay_frame(&recordings[previous], b, 1.0f);
    force_replay = 1; /* the next interpolated image is drawn afresh */
    return 0;
}
/* The display area (RPC ui_upscale reports it for crops of gpu_dump). */
void pc_hw_display_rect(int r[4]) { memcpy(r, display_rect, sizeof display_rect); }
/* After pc_hw_target: the origin of the area to present from the target it
 * returned (an interpolated frame of a double-buffered game is B's
 * off-screen area, not the displayed one). */
void pc_hw_present_origin(int* x, int* y)
{
    *x = shown_rect[0], *y = shown_rect[1];
}
/* Finish the frame's drawing; returns the scaled target for presentation
 * (and pc_hw_present_origin the area to present from it). With
 * interpolation, the vsyncs between two frames show the newest frame B
 * blended from the frame A before it by t = vsyncs into A..B / (B's interval
 * after A): at 30 fps 1/2 then B, at 15 fps 1/4, 1/2, 3/4, B, so motion
 * stays even whatever the game's frame rate (cutscenes vary). */
extern unsigned pc_vsync_count(void);
static GLuint hw_target(void);
int pc_hw_present_lists(uint32_t* lists, int max, float* t);
GLuint pc_hw_target(void)
{
    GLuint target = hw_target();
    if (render_anim_trace_remaining() > 0) {
        uint32_t lists[64];
        float t;
        int n = pc_hw_present_lists(lists, 64, &t);
        render_anim_trace_present(pc_vsync_count(), presenting_replay, t, lists, n);
    }
    return target;
}
static GLuint hw_target(void)
{
    flush();
    pc_ui_upscale_frame(); /* a presented image: the 2D cache's clock */
    presenting_replay = 0;
    memcpy(shown_rect, display_rect, sizeof shown_rect);
    recording* now = recording_now();
    unsigned vsync = pc_vsync_count();
    if (now->op_count || now->triangle_count) {
        packet_stats.stale_rects = stale_count;
        packet_stats.vram_syncs = pc_hw_syncs;
        render_frame_end(&packet_stats);
        report_other();
        memset(&packet_stats, 0, sizeof packet_stats);
        finish_recording(now);
        previous = newest;
        newest = recording_index;
        arrival[newest] = vsync;
        recording_index = 3 - previous - newest;
        recording* next = recording_now();
        next->vertex_count = next->triangle_count = next->op_count = next->overflow = next->writes = 0;
        next->valid = 0;
    }
    recording *a = &recordings[previous], *b = &recordings[newest];
    int matching = a->valid && b->valid && a->widescreen == b->widescreen && fabsf(a->h - b->h) < 0.25f * b->h;
    /* A camera cut (a jump in rotation or position) is shown as is. */
    float turn = 0, move = 0, size = 0;
    for (int i = 0; i < 3; i++) {
        move += (a->t[i] - b->t[i]) * (a->t[i] - b->t[i]);
        size += b->t[i] * b->t[i];
        for (int k = 0; k < 3; k++)
            turn = fmaxf(turn, fabsf(a->r[i][k] - b->r[i][k]));
    }
    matching = matching && turn < 0.2f && move < 0.04f * size + 64.0f * 64.0f;
    unsigned interval = arrival[newest] - arrival[previous];
    /* At 60 fps there is nothing between vsyncs to show, unless images are
     * presented between them (pc_present_rate). */
    unsigned shortest = pc_present_phase > 0 ? 1 : 2;
    if (!pc_hw_interpolate || !matching || interval < shortest || interval > 8)
        return target_texture; /* 60 fps, a cut, or stale: the newest frame as drawn */
    /* Where B is. The game double-buffers: it draws B into the buffer that
     * is not on screen and shows A meanwhile, flipping to B when it has
     * drawn the frame after it. Then A is shown as is when B arrives, and the
     * vsyncs after that show B's area of the replay target blended from A
     * toward B (the game shows B next: no latency added). Drawn into the
     * displayed buffer itself, B is shown from its arrival, blended. */
    static const int everywhere[4] = { -4096, -4096, 8192, 8192 };
    int overlap, behind = 0, shift[2] = { 0, 0 };
    recording_op(b, display_rect, 0, &overlap);
    if (!overlap) {
        /* The buffers' offset: from the room list's drawing area in each
         * (else their largest draws). */
        const recorded_op* a_area = recording_op(a, display_rect, 1, &overlap);
        const recorded_op* b_area = recording_op(b, everywhere, 1, &overlap);
        if (!a_area || !b_area) {
            a_area = recording_op(a, display_rect, 0, &overlap);
            b_area = recording_op(b, everywhere, 0, &overlap);
        }
        if (!a_area || !b_area)
            return target_texture;
        shift[0] = b_area->clip[0] - a_area->clip[0], shift[1] = b_area->clip[1] - a_area->clip[1];
        if (display_rect[0] + shift[0] < 0 || display_rect[1] + shift[1] < 0
            || display_rect[0] + shift[0] + display_rect[2] > 1024 || display_rect[1] + shift[1] + display_rect[3] > 512)
            return target_texture;
        behind = 1;
    }
    float t = ((float)(vsync - arrival[newest] + !behind) + pc_present_phase) / (float)interval;
    if (t <= 0.0f || t >= 1.0f)
        return target_texture;
    /* Present (and light, trace) B's area: its picture, G-buffer, camera and
     * object lists. */
    shown_rect[0] += shift[0], shown_rect[1] += shift[1];
    /* The same blend is drawn once (paused redraws present repeatedly). */
    static int drawn_slot = -1;
    static unsigned drawn_vsync, drawn_arrival;
    static float drawn_phase;
    if (drawn_slot != newest || drawn_vsync != vsync || drawn_arrival != arrival[newest]
        || drawn_phase != pc_present_phase || force_replay) {
        force_replay = 0;
        replay_frame(a, b, t);
        drawn_slot = newest, drawn_vsync = vsync, drawn_arrival = arrival[newest], drawn_phase = pc_present_phase;
    }
    presenting_replay = 1;
    present_blend = t;
    return replay.color;
}
/* The object lists (characters, weapons, props) of the frame being
 * presented and the blend their palettes are drawn at (1: as recorded;
 * replays blend from the frame before): the path tracer's moving geometry. */
int pc_hw_present_lists(uint32_t* lists, int max, float* t)
{
    /* The recording whose picture is shown (its surfaces are in the
     * G-buffer), at the blend it was drawn with. */
    int replayed;
    uint32_t room;
    const recording* b = shown_recording(&replayed, &room);
    if (!b)
        b = &recordings[newest], replayed = presenting_replay;
    int n = 0;
    *t = replayed ? present_blend : 1.0f;
    for (int o = 0; o < b->op_count && n < max; o++) {
        uint32_t id = (uint32_t)b->ops[o].first;
        if (b->ops[o].fill != 2 || render_list_objects(id) != 1)
            continue;
        int seen = 0;
        for (int i = 0; i < n && !seen; i++)
            seen = lists[i] == id;
        if (!seen)
            lists[n++] = id;
    }
    return n;
}
GLuint pc_hw_depth_texture(void) { return presenting_replay ? replay.depth : depth_texture; }
void pc_hw_gbuffer(GLuint* normal, GLuint* albedo)
{
    *normal = presenting_replay ? replay.normal : normal_texture;
    *albedo = presenting_replay ? replay.albedo : albedo_texture;
}
/* Vertex colour per pixel: rgb as drawn (0..1, 128/255 = unlit), a = 1
 * textured (the colour modulates the texel by 255/128), 0.5 untextured
 * (it is the surface colour), 0 not a lit surface. */
GLuint pc_hw_shade_texture(void) { return presenting_replay ? replay.shade : shade_texture; }
/* The present pass is done with the frame (its camera override ends). */
/* After each present: light animation (lights.cpp) samples its clock
 * afresh for the next image, so every pass of one image sees the same lights. */
extern void pc_lights_new_frame(void) __attribute__((weak));
void pc_hw_present_done(void)
{
    presenting_replay = 0;
    if (pc_lights_new_frame)
        pc_lights_new_frame();
}
static void forget_recordings(void)
{
    for (int i = 0; i < 3; i++) {
        recording* rec = &recordings[i];
        rec->vertex_count = rec->triangle_count = rec->op_count = rec->overflow = rec->writes = 0;
        rec->valid = 0;
    }
}


/* Developer RPC: write the whole scaled VRAM target as a PPM (row 0 = VRAM
 * row 0); `replay`: the last interpolated frame's target instead. Returns 0
 * on success. */
int pc_hw_dump(const char* path, int replay_target)
{
    if (!pc_hw_active)
        return -1;
    flush();
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return pc_vk_hw_dump(path, replay_target);
#endif
    int w = 1024 * pc_hw_scale, h = 512 * pc_hw_scale;
    unsigned char* pixels = malloc((size_t)w * h * 4);
    FILE* f = pixels ? fopen(path, "wb") : NULL;
    if (!f) {
        free(pixels);
        return -1;
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, replay_target ? replay.fbo : target_fbo);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl_PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl_ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++)
        fwrite(pixels + i * 4, 1, 3, f);
    free(pixels);
    return fclose(f) ? -1 : 0;
}

/* Editor picking: depth (GTE SZ, 0 = 2D/UI or nothing) and view-space normal
 * of the scaled target at native VRAM coordinates. Returns 1 for a lit 3D
 * surface. */
/* One target pixel's G-buffer normal (alpha: 255 room, 191 moving, 140 an
 * editor cutaway ghost, below 128 nothing) and depth. */
static int pick_pixel(int x, int y, unsigned char n[4], float* depth)
{
    if (x < 0 || y < 0 || x >= 1024 * pc_hw_scale || y >= 512 * pc_hw_scale)
        return 0;
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return pc_vk_hw_pick(x, y, depth, n);
#endif
    gl_BindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    gl_PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT1);
    gl_ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, n);
    gl_ReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}
static int ghost_alpha(unsigned a) { return a >= 128 && a < 166; }
int pc_hw_pick(float vram_x, float vram_y, float* sz, float normal[3])
{
    if (!pc_hw_active)
        return 0;
    flush();
    int x = (int)(vram_x * pc_hw_scale), y = (int)(vram_y * pc_hw_scale);
    unsigned char n[4];
    float depth = 1;
    if (!pick_pixel(x, y, n, &depth))
        return 0;
    /* An editor cutaway ghost (render.c): pick what it lets through, a
     * nearby pixel the dither left to the surface behind it. */
    for (int r = 1; r <= 3 && ghost_alpha(n[3]); r++)
        for (int dy = -r; dy <= r && ghost_alpha(n[3]); dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (dx > -r && dx < r && dy > -r && dy < r)
                    continue; /* the ring at distance r */
                unsigned char m[4];
                float d = 1;
                if (pick_pixel(x + dx, y + dy, m, &d) && !ghost_alpha(m[3])) {
                    memcpy(n, m, sizeof m);
                    depth = d;
                    break;
                }
            }
    *sz = depth * 65536.0f;
    for (int k = 0; k < 3; k++)
        normal[k] = n[k] / 127.5f - 1.0f;
    return n[3] > 127 && depth < 1.0f && *sz >= 0.5f;
}

/* Native VRAM as an R16UI texture, fully up to date (editor view texturing). */
GLuint pc_hw_native_texture(void)
{
    if (!pc_hw_active)
        return 0;
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return 0; /* the editor's GL view has no Vulkan counterpart yet */
#endif
    flush();
    if (dirty_tiles)
        upload_tiles(dirty_tiles);
    return native_texture;
}
