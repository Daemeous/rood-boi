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
#include "hd_textures.h"
#include "ui_upscale.h" /* 2D / UI texture magnification (MMPX) */
#include "ui_text.h"    /* 2D / UI: glyphs drawn from fonts (ui_text) */
#include "ui_bubbles.h" /* 2D / UI: joined lines, vector talk bubbles */
extern void pc_ui_bubbles_frame(void) __attribute__((weak));
#include "button_prompts.h" /* 2D / UI: button prompts drawn from icon fonts */
#include "antialias.h" /* anti-aliasing: supersampling's scale, MSAA */
#include "emissive.h" /* emissive texels: the slot map the draw shader reads */
#include "frame_cost.h" /* ROOD_PERF sections */
#include "gl_shaders.h" /* PC_GLSL_MS_RESOLVE (pc/shaders/vulkan/ms_resolve.glsl) */
#ifdef ROOD_VULKAN
#include "vk_hooks.h" /* ROOD_RENDERER=vulkan: the GL calls below go to vk_hw.c */
#endif
extern void pc_settings_env_override(const char* key) __attribute__((weak)); /* project_settings.cpp */
extern int pc_present_rate; /* below */
extern int pc_hw_scale_setting; /* below */
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
/* FLAG_COVER: untextured 2D shape with coverage (anti-aliased edges, ui_bubbles.c):
 * U is the coverage, 0..256, blended over what is drawn (BLEND_COVER). */
enum { FLAG_TEXTURED = 1, FLAG_RAW = 2, FLAG_SEMI = 4, FLAG_MASK = 8, FLAG_UI = 16, FLAG_CLAMP = 32, FLAG_COVER = 64 };
/* Batch blend for FLAG_COVER: colour over the target by coverage, the
 * target's mask bit kept (above the PS1 modes 0-3 and their 2D + 4). */
#define BLEND_COVER 12
/* Batch blends for text and icons from distance texels (ui_text.h), + the
 * PS1 mode (0-3) of a semi-transparent primitive or 4: anti-aliased by
 * coverage at the target's pixels (draw_batch: the solid pixels' G-buffer
 * and depth as UI, then the colour over by coverage, the mask bit kept). */
#define BLEND_TEXT 16
/* Batch blend for a screen fade (pc_hw_triangle: a full-screen flat
 * subtractive 2D quad, the door fade of BATTLE _drawScreenTransition): it
 * subtracts from the 2D under it (depth "UI") and from what fills and uploads
 * left (far depth) only; the 3D keeps its depth and surface (so it stays
 * lit) and the presenter fades it after lighting and post-processing
 * (pc_hw_present_fade, the grade pass). */
#define BLEND_FADE 13
static void record_fade(float amount, const float rect[4]);
static uint32_t edge_generation; /* 2D polygon edges of this ordering table (pc_hw_line) */
#define MAX_VERTICES 65536

int pc_hw_active;
int pc_hw_scale = 1;
/* Frames left unpresented (battle charging's skip, charge_skip.c): each
 * still ends as at a present (pc_hw_skip_present). pc_hw_history_cut counts
 * the skips that ended: the temporal filters (path-traced GI's history, NRD;
 * ptgi.c, vk_rt.inc) restart at the first frame presented after one, and
 * that frame is shown as drawn, not blended from the one before (the camera
 * and the scene jumped while nothing was shown). */
unsigned pc_hw_history_cut;
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
enum { PC_ANCHOR_NONE, PC_ANCHOR_LEFT, PC_ANCHOR_RIGHT, PC_ANCHOR_FILL, PC_ANCHOR_SCENE, PC_ANCHOR_COVER };
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
        : primitive_anchor == PC_ANCHOR_SCENE ? "scene (compressed, whole view)"
        : primitive_anchor ? (primitive_anchor == 1 ? "anchor left" : primitive_anchor == 2 ? "anchor right" : "fill")
        : primitive_extend > 0 ? "extend right" : primitive_extend < 0 ? "extend left" : "compress");
}
static void side_paint_primitive(int x0, int y0, int x1, int y1, unsigned code, const int* clip);
static int side_paint_count;
static void side_paint_forget(int x, int y, int w, int h);
void pc_hw_set_bounds(int x0, int y0, int x1, int y1, unsigned code, unsigned tpage, const int* uv, const int* clip)
{
    if (!primitive_2d || clip[2] <= 0 || clip[3] <= 0)
        return;
    if (primitive_narrowed) {
        trace_bounds(x0, y0, x1, y1, code, tpage, uv, clip);
        side_paint_primitive(x0, y0, x1, y1, code, clip);
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
        side_paint_primitive(x0, y0, x1, y1, code, clip);
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
    /* 2D of the 3D view (PC_ANCHOR_SCENE) keeps its place among the rest of
     * the view: a polygon of it never stands for the screen, however large
     * (the background sphere's triangles reach far past the top and bottom;
     * drawn uncompressed they left gaps between them and the compressed
     * ones). A screen-wide rectangle in it is a clear (the black tile behind
     * the sky panorama) and covers the view. */
    if (primitive_anchor == PC_ANCHOR_SCENE) {
        tall = backdrop = copy = 0;
        wide = wide && (code & 0xe0) == 0x60;
    }
    primitive_cover = wide || tall || backdrop || copy || primitive_anchor == PC_ANCHOR_COVER;
    if (primitive_cover || primitive_anchor == PC_ANCHOR_SCENE)
        primitive_extend = 0;
    trace_bounds(x0, y0, x1, y1, code, tpage, uv, clip);
    side_paint_primitive(x0, y0, x1, y1, code, clip);
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
/* (The anchor kinds: above, with primitive_anchor.) */
/* PC_ANCHOR_SCENE: 2D the game draws as part of the 3D view (the sky
 * panorama's tiles, particles at projected points): compressed about the
 * centre like the narrowed 3D, but clipped to the whole view, not to the
 * compressed drawing area, so what the game draws past the 4:3 screen's
 * sides fills the 16:9 view's sides. PC_ANCHOR_COVER: screen-space 2D
 * spread over the whole screen (the floating dust motes), drawn over the
 * whole view like screen-covering 2D. Both off with ROOD_WIDESCREEN_SCENE=0
 * (RPC graphics widescreen_scene; for comparisons). */
int pc_widescreen_scene = -1;
static int scene_enabled(void)
{
    if (pc_widescreen_scene < 0)
        pc_widescreen_scene = !getenv("ROOD_WIDESCREEN_SCENE") || *getenv("ROOD_WIDESCREEN_SCENE") != '0';
    return pc_widescreen_scene;
}
/* Scaled about a screen edge (the HUD's anchors). */
static int edge_anchored(void)
{
    return pc_gte_widescreen && (primitive_anchor == PC_ANCHOR_LEFT || primitive_anchor == PC_ANCHOR_RIGHT);
}
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
    if ((anchor == PC_ANCHOR_SCENE || anchor == PC_ANCHOR_COVER) && !scene_enabled())
        return;
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
/* MSAA (antialias.h; "MSAA" below): the scope (target_fbo or replay.fbo while
 * its drawing area lives in the multisampled twins), its area (VRAM pixels
 * x0, y0, x1, y1, exclusive) and its helpers. */
static GLuint ms_scope;
static int ms_rect[4];
static void ms_end(void);
static void ms_end_over(int x, int y, int w, int h);
static int ms_touches(int x, int y, int w, int h);
static int ms_holds(GLuint fbo, const int* clip);
static GLuint ms_target(int object);

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
/* ui_text_layout line (ui_text.h): glyph sprites buffered until something
 * else is drawn, then laid out as lines (text_run_flush, below). */
static int text_piece_count, text_emitting;
static void text_run_flush(void);
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
/* The drawing areas native draws left stale in the ordering table being
 * drawn (STALE_MISSING; pc_hw_begin_ot .. pc_hw_end_ot). What the CPU
 * rasterizer would draw inside one is replaced by the target's picture
 * before anything reads it there (pc_hw_sync): it is not drawn
 * (pc_hw_raster_skip). Only this table's own marks count, so the choice
 * is the same in a game resumed from a save state (whose host state starts
 * empty) as in the game that saved it. */
#define MAX_OT_STALE 8
static int ot_stale[MAX_OT_STALE][4], ot_stale_count, in_ot;
void pc_hw_end_ot(void) { in_ot = 0, ot_stale_count = 0; }
void pc_hw_begin_ot(void)
{
    in_ot = 1, ot_stale_count = 0;
    if (text_piece_count && !text_emitting)
        text_run_flush(); /* the previous table's last text */
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
    pc_hw_suppress = 0;
    ++edge_generation;
    if (pc_ui_bubbles_frame)
        pc_ui_bubbles_frame();
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
static GLint u_draw_ui_scale, u_draw_ui_atlas, u_draw_text_factor, u_draw_emissive_map, u_draw_emissive_on;
/* Perspective-correct texturing: on by default (settings perspective_textures;
 * ROOD_PERSPECTIVE=0 or 0 there restores the PS1's affine mapping). */
int pc_hw_perspective = 1;
/* Bilinear texture filtering on 3D surfaces (off: the PS1 samples nearest). */
int pc_hw_texture_filter;
static int pending_depth[3], has_pending_depth;
static float pending_precise[6];
static int has_pending_precise;
/* The next triangle's exact positions (and coverage blending), set by
 * pc_hw_shape_triangle for shapes the port builds: joined lines, vector
 * talk bubbles (ui_bubbles.c). */
static float exact_xy[6];
static int has_exact, exact_cover;
/* A laid-out glyph's quad (text_run_flush): its unrounded positions and its
 * image's atlas origin (ui_text_layout line). */
static float text_xy[6];
static int has_text_xy, text_use_image;
static int16_t text_offset[2];
static int text_bounds[4]; /* the image's texels (0, 0, w, h) */
/* Text drawn three ways in one recorded frame, so redraws (RPC ui_upscale
 * redraw) compare the layouts and the original on the very same frame: the
 * original glyph sprites (drawn while the layout is off: ui_text original,
 * 1x), the glyphs laid out each in its cell (ui_text_layout glyph) and as
 * lines (line), tagged in the vertices' texture window word (bits 0-19 are
 * the window) and filtered by draw_batch. */
#define TEXT_TAG_GLYPH (1u << 29)
#define TEXT_TAG_CELL (1u << 30)
#define TEXT_TAG_LINE (1u << 31)
static uint32_t triangle_tag;
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
/* Native VRAM the sampled copy (native_texture) lacks, in bands of 16 rows
 * of each tile column (64 halfwords): bit b of dirty_bands[column] is rows
 * 16 b .. 16 b + 15. dirty_tiles sums them up per 64 x 256 tile (16 x 2).
 * A draw uploads only the bands it samples: a CLUT row under the frame
 * buffer no longer brings the frame buffer's half of its tile along. */
static uint32_t dirty_bands[16] = { [0 ... 15] = 0xffffffffu };
static uint32_t dirty_tiles = 0xffffffffu;
/* Own copies (pc_hw_sync_texture): the rectangles of the target the
 * primitive being drawn samples, copied into the sampled copy of VRAM at its
 * first triangle (after its uploads); own_bands: where that copy is the
 * target's, not native VRAM (dirty again from the next textured primitive). */
static int own_rects[4][4], own_count, own_active;
static uint32_t own_bands[16];
static void own_apply(void);
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
    "noperspective centroid out vec3 v_color_affine;\n"
    "noperspective centroid out vec2 v_uv_affine;\n"
    "smooth centroid out vec3 v_color_persp;\n"
    "smooth centroid out vec2 v_uv_persp;\n"
    "flat out uvec4 v_state;\n" /* flags, tpage, clut, window */
    "flat out vec4 v_normal;\n"
    "flat out uvec4 v_bounds;\n"
    "flat out ivec2 v_ui;\n"
    "smooth centroid out float v_z_persp;\n"
    "noperspective centroid out float v_z_affine;\n"
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
    "#version 330 core\n#define ROOD_GL 1\n"
    "noperspective centroid in vec3 v_color_affine;\n"
    "noperspective centroid in vec2 v_uv_affine;\n"
    "smooth centroid in vec3 v_color_persp;\n"
    "smooth centroid in vec2 v_uv_persp;\n"
    "flat in uvec4 v_state;\n"
    "flat in vec4 v_normal;\n"
    "flat in uvec4 v_bounds;\n" /* u min, v min, u max, v max */
    "flat in ivec2 v_ui;\n"
    "smooth centroid in float v_z_persp;\n"
    "noperspective centroid in float v_z_affine;\n"
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
    /* Text from distance texels (BLEND_TEXT, passes 3-5): the colour pass's
     * alpha factor for the PS1 mode (B/2 + F/2: 0.5, B + F/4: 0.25). */
    "uniform float text_factor;\n"
    /* Emissive texels (emissive.h; lit 3D only): the slot map, on. */
    "uniform usampler2D emissive_map;\n"
    "uniform int emissive_on;\n"
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
    PC_GLSL_TEXT_DIST PC_GLSL_EMISSIVE PC_GLSL_HD_TEXTURES
    "void main() {\n"
    /* A pixel's footprint in texels (text from distance texels: edges one pixel wide). */
    "    vec2 footprint = fwidth(perspective != 0 ? v_uv_persp : v_uv_affine);\n"
    "    float text_alpha = -1.0;\n"
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
    "        vec3 texel;\n"
    "        if ((flags & 16u) != 0u && ui_scale > 0) {\n"
    "            ivec2 lo = ivec2(v_bounds.xy), hi = max(ivec2(v_bounds.zw) - 1, lo);\n"
    "            ivec2 q = clamp(ivec2(floor(v_uv * float(ui_scale) + 0.0001)), lo * ui_scale, (hi + 1) * ui_scale - 1);\n"
    "            t = texelFetch(ui_atlas, q + v_ui * ui_scale, 0).r;\n"
    "            texel = rgb(t);\n"
    "            if ((t & 0x80000000u) != 0u) {\n" /* text: anti-aliased by its distance */
    "                ivec2 o = v_ui * ui_scale;\n"
    "                float d = text_distance(ui_atlas, v_uv * float(ui_scale) + vec2(o), lo * ui_scale + o,\n"
    "                    (hi + 1) * ui_scale - 1 + o);\n"
    "                text_alpha = text_colour(t, d, max(max(footprint.x, footprint.y), 1.0 / 64.0), clut, texel, t);\n"
    "                if (text_alpha < 0.002) discard;\n"
    "            }\n"
    "        } else {\n"
    "            ivec2 q = ivec2(floor(v_uv + 0.0001));\n"
    /* FLAG_CLAMP (2D): the texels the PS1 samples, [min, max - 1]; the far
     * edge is never reached (finer samples than whole pixels would take it). */
    "            if ((flags & 32u) != 0u) q = clamp(q, ivec2(v_bounds.xy), max(ivec2(v_bounds.zw) - 1, ivec2(v_bounds.xy)));\n"
    "            t = texel_at(uint(q.x) & 255u, uint(q.y) & 255u, tpage, clut, win);\n"
    "            texel = rgb(t);\n"
    "        }\n"
    "        vec4 hd;\n"
    "        bool has_hd = ((flags & 16u) == 0u || text_alpha < 0.0) && hd_texel(clamp(v_uv, vec2(v_bounds.xy), max(vec2(v_bounds.zw) - 0.0001, vec2(v_bounds.xy))), tpage, clut, win, hd);\n"
    "        if (!has_hd && filtering != 0 && v_normal.w > 0.5) {\n"
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
    "        if (has_hd) hd_apply(hd, texel, t);\n"
    "        if (t == 0u) discard;\n"
    "        semi = semi && (t & 0x8000u) != 0u;\n"
    "        mask |= t >> 15;\n"
    "        c = (flags & 2u) != 0u ? texel / 31.0 : min(texel * v_color * (255.0 / 128.0), 31.0) / 31.0;\n"
    "        albedo = texel / 31.0;\n"
    "        shade = (flags & 2u) != 0u ? vec4(vec3(128.0 / 255.0), 1.0) : vec4(v_color, 1.0);\n"
    /* Emissive texels: at least their own colour x tint x strength, never
     * darkened; the G-buffer's shade carries the emission (a 0.25, rgb / 2). */
    "        if (emissive_on != 0 && v_normal.w > 0.5 && (flags & 16u) == 0u) {\n"
    "            ivec2 eq = ivec2(floor(v_uv + 0.0001));\n"
    "            vec3 em = emissive_texel(uint(eq.x) & 255u, uint(eq.y) & 255u, tpage, win, t, 0u);\n" /* packets: no scope bits */
    "            if (any(greaterThan(em, vec3(0.0)))) { c = max(c, min(em, vec3(1.0))); shade = vec4(min(em * 0.5, vec3(1.0)), 0.25); }\n"
    "        }\n"
    "    }\n"
    "    if ((pass == 1 && semi) || (pass == 2 && !semi)) discard;\n"
    /* Text: 3 its solid pixels' G-buffer and depth (opaque texels, half
     * covered and more), 4 its opaque texels' colour, 5 its translucent
     * texels' colour, by coverage (texels of a recorded frame redrawn with
     * ui_text original: whole). */
    "    if ((pass == 3 && (semi || (text_alpha >= 0.0 && text_alpha < 0.5))) || (pass == 4 && semi)\n"
    "        || (pass == 5 && !semi)) discard;\n"
    "    color = vec4(c, float(mask));\n"
    "    if (pass >= 4) color.a = (text_alpha < 0.0 ? 1.0 : text_alpha) * (pass == 5 ? text_factor : 1.0);\n"
    /* FLAG_COVER (untextured 2D shapes, BLEND_COVER): U is the coverage. */
    "    if ((flags & 65u) == 64u) {\n"
    "        float cover = clamp(v_uv.x / 256.0, 0.0, 1.0);\n"
    "        if (cover < 0.002) discard;\n"
    "        color = vec4(c, cover);\n"
    "    }\n"
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
    const char* backend = getenv("ROOD_RENDERER");
    if (backend && !strcmp(backend, "null"))
        return; /* CPU rasterization owns VRAM; no graphics target, even if called explicitly. */
    const char* setting = getenv("ROOD_SCALE");
    int scale = setting ? atoi(setting) : 4;
    if (scale < 1)
        return;
    pc_hw_scale = scale > 8 ? 8 : scale;
    pc_hw_scale_setting = pc_hw_scale;
    if (setting && pc_settings_env_override)
        pc_settings_env_override("internal_resolution"); /* not saved as the user's (project_settings.cpp) */
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
    u_draw_text_factor = gl_GetUniformLocation(draw_program, "text_factor");
    u_draw_emissive_map = gl_GetUniformLocation(draw_program, "emissive_map");
    u_draw_emissive_on = gl_GetUniformLocation(draw_program, "emissive_on");
    gl_UseProgram(draw_program);
    gl_Uniform1i(u_draw_room_depth, 1); /* never unit 0 (the VRAM usampler) */
    gl_Uniform1i(u_draw_ui_atlas, 2);
    gl_Uniform1i(u_draw_emissive_map, 7);
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
        if (rate) {
            pc_present_rate = atoi(rate) < 0 ? 0 : atoi(rate) > 8 ? 8 : atoi(rate); /* 0: match the display */
            if (pc_settings_env_override)
                pc_settings_env_override("present_rate");
        }
        const char* native = getenv("ROOD_NATIVE_ROOM");
        if (native) {
            pc_native_room = atoi(native) < 0 ? 0 : atoi(native) > 2 ? 2 : atoi(native);
            if (pc_settings_env_override)
                pc_settings_env_override("native_room");
        }
        native = getenv("ROOD_NATIVE_ACTORS");
        if (native) {
            pc_native_actors = atoi(native) < 0 ? 0 : atoi(native) > 2 ? 2 : atoi(native);
            if (pc_settings_env_override)
                pc_settings_env_override("native_actors");
        }
        native = getenv("ROOD_NATIVE_BLEND");
        if (native) {
            pc_native_blend = atoi(native) != 0;
            if (pc_settings_env_override)
                pc_settings_env_override("native_blend");
        }
        native = getenv("ROOD_NATIVE_OBJECTS");
        if (native) {
            pc_native_objects = atoi(native) != 0;
            if (pc_settings_env_override)
                pc_settings_env_override("native_objects");
        }
        native = getenv("ROOD_NATIVE_ROOM_ORDER");
        if (native) {
            pc_native_room_order = atoi(native) != 0;
            if (pc_settings_env_override)
                pc_settings_env_override("native_room_order");
        }
        native = getenv("ROOD_NATIVE_SHADOWS");
        if (native) {
            pc_native_shadows = atoi(native) != 0;
            if (pc_settings_env_override)
                pc_settings_env_override("native_shadows");
        }
        native = getenv("ROOD_NATIVE_EFFECTS");
        if (native) {
            pc_native_effects = atoi(native) != 0;
            if (pc_settings_env_override)
                pc_settings_env_override("native_effects");
        }
        native = getenv("ROOD_RECONSTRUCT");
        if (native) {
            pc_reconstruct = atoi(native) != 0;
            if (pc_settings_env_override)
                pc_settings_env_override("reconstruct");
        }
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
/* The bands (dirty_bands' layout) of the inclusive rectangle, added to
 * `bands`; coordinates wrap like VRAM. */
static void bands_for(int x0, int y0, int x1, int y1, uint32_t bands[16])
{
    if (x1 < x0 || y1 < y0)
        return;
    if (x1 - x0 >= 1023)
        x0 = 0, x1 = 1023;
    if (y1 - y0 >= 511)
        y0 = 0, y1 = 511;
    uint32_t rows = 0;
    for (int y = y0;;) {
        rows |= 1u << ((y & 511) >> 4);
        if (y >= y1)
            break;
        y = (y | 15) + 1 > y1 ? y1 : (y | 15) + 1;
    }
    for (int x = x0;;) {
        bands[(x & 1023) >> 6] |= rows;
        if (x >= x1)
            break;
        x = (x | 63) + 1 > x1 ? x1 : (x | 63) + 1;
    }
}
static uint32_t tiles_of_bands(const uint32_t bands[16])
{
    uint32_t tiles = 0;
    for (int c = 0; c < 16; c++)
        tiles |= (bands[c] & 0xffffu ? 1u << c : 0) | (bands[c] >> 16 ? 1u << (16 + c) : 0);
    return tiles;
}
static void bands_of_tiles(uint32_t tiles, uint32_t bands[16])
{
    for (int c = 0; c < 16; c++)
        bands[c] |= (tiles & (1u << c) ? 0xffffu : 0) | (tiles & (1u << (16 + c)) ? 0xffff0000u : 0);
}
/* Native VRAM changed in this rectangle (inclusive): the sampled copy lacks it. */
static void mark_dirty(int x0, int y0, int x1, int y1)
{
    bands_for(x0, y0, x1, y1, dirty_bands);
    dirty_tiles |= tiles_for(x0, y0, x1, y1);
}
static void mark_dirty_tiles(uint32_t tiles)
{
    bands_of_tiles(tiles, dirty_bands);
    dirty_tiles |= tiles;
}
/* The CPU rasterizer changed native VRAM in this rectangle (inclusive). */
void pc_hw_mark(int x0, int y0, int x1, int y1)
{
    if (pc_hw_active) {
        uint32_t tiles = tiles_for(x0, y0, x1, y1);
        mark_dirty(x0, y0, x1, y1);
        pc_ui_upscale_tiles_changed(tiles);
        pc_emissive_vram_dirty(tiles);
    }
}
/* The sampled copy of VRAM: `bands` (dirty_bands' layout) from `vram`
 * (1024 x 512 halfwords), as one copy per run of bands in a column, all in
 * one call. */
static void upload_bands_from(const uint16_t* vram, const uint32_t bands[16])
{
    int rects[16 * 16][4], count = 0, halfwords = 0;
    for (int c = 0; c < 16; c++)
        for (int first = 0; first < 32; first++) {
            if (!(bands[c] >> first & 1))
                continue;
            int last = first;
            while (last < 31 && (bands[c] >> (last + 1) & 1))
                ++last;
            rects[count][0] = c * 64, rects[count][1] = first * 16;
            rects[count][2] = 64, rects[count][3] = (last - first + 1) * 16;
            halfwords += rects[count][2] * rects[count][3];
            ++count;
            first = last;
        }
    if (!count)
        return;
    uint64_t cost = pc_cost_begin();
    pc_cost_count(PC_COUNT_UPLOADS, 1);
    pc_cost_count(PC_COUNT_UPLOAD_KB, (unsigned)(halfwords / 512));
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        pc_vk_hw_upload_rects(vram, rects, count);
    else
#endif
    {
        gl_BindTexture(GL_TEXTURE_2D, native_texture);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 2);
        gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 1024);
        for (int i = 0; i < count; i++) {
            const int* r = rects[i];
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, r[0], r[1], r[2], r[3], GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                vram + r[1] * 1024 + r[0]);
        }
        gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    pc_cost_end(PC_COST_UPLOAD, cost);
}
static void upload_tiles_from(const uint16_t* vram, uint32_t tiles)
{
    uint32_t bands[16] = { 0 };
    bands_of_tiles(tiles, bands);
    upload_bands_from(vram, bands);
}
/* Whole tiles from native VRAM (also where it is not dirty: a replay drew
 * its own copy there). */
static void upload_tiles(uint32_t tiles)
{
    uint32_t bands[16] = { 0 };
    bands_of_tiles(tiles, bands);
    upload_bands_from(native_vram, bands);
    for (int c = 0; c < 16; c++)
        dirty_bands[c] &= ~bands[c];
    dirty_tiles = tiles_of_bands(dirty_bands);
}
/* `bands` from native VRAM, dirty or not (a replay drew its own copy there). */
static void upload_bands(const uint32_t bands[16])
{
    upload_bands_from(native_vram, bands);
    for (int c = 0; c < 16; c++)
        dirty_bands[c] &= ~bands[c];
    dirty_tiles = tiles_of_bands(dirty_bands);
}
/* Whether the sampled copy lacks any of `want` (dirty_bands' layout). */
static int dirty_in(const uint32_t want[16])
{
    for (int c = 0; c < 16; c++)
        if (want[c] & dirty_bands[c])
            return 1;
    return 0;
}
/* What the sampled copy lacks of `want`, from native VRAM (the caller
 * flushes its batch first). */
static void upload_dirty(const uint32_t want[16])
{
    uint32_t bands[16];
    for (int c = 0; c < 16; c++) {
        bands[c] = want[c] & dirty_bands[c];
        dirty_bands[c] &= ~bands[c];
    }
    upload_bands_from(native_vram, bands);
    dirty_tiles = tiles_of_bands(dirty_bands);
}
/* The bands an own copy left (the target's, not native VRAM) are dirty again. */
static void own_fold(void)
{
    int any = 0;
    for (int c = 0; c < 16; c++) {
        any |= own_bands[c] != 0;
        dirty_bands[c] |= own_bands[c];
        own_bands[c] = 0;
    }
    if (any)
        dirty_tiles = tiles_of_bands(dirty_bands);
    own_active = 0;
}
static void upload_all_dirty(void)
{
    own_fold();
    uint32_t all[16];
    memset(all, 0xff, sizeof all);
    upload_dirty(all);
}

/* ---- emissive texels ---- */

/* The slot map (emissive.h, emissive.glsl): matched and uploaded before a
 * game frame's first draw (replays draw with the newest). */
static GLuint emissive_texture;
static int emissive_shader_on;
static uint32_t emissive_frame = 0xffffffffu;
static void emissive_sync(void)
{
    uint32_t frame = render_frame_serial();
    /* Once per game frame, and at once on a setting or catalog change (a
     * paused game's redraws show it). */
    if (frame == emissive_frame && !pc_emissive_pending())
        return;
    emissive_frame = frame;
    int r0, r1;
    uint64_t cost = pc_cost_begin();
    emissive_shader_on = pc_emissive_sync(&r0, &r1);
    pc_cost_end(PC_COST_EMISSIVE, cost);
    const uint8_t* map = pc_emissive_map();
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        if (r1 > r0)
            pc_vk_hw_upload_emissive(map, r0, r1);
        return;
    }
#endif
    if (!emissive_texture) {
        gl_GenTextures(1, &emissive_texture);
        gl_BindTexture(GL_TEXTURE_2D, emissive_texture);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, PC_EMISSIVE_MAP_W, PC_EMISSIVE_MAP_H, 0, GL_RED_INTEGER,
            GL_UNSIGNED_BYTE, NULL);
        r0 = 0, r1 = PC_EMISSIVE_MAP_H;
    }
    if (r1 > r0) {
        gl_BindTexture(GL_TEXTURE_2D, emissive_texture);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, r0, PC_EMISSIVE_MAP_W, r1 - r0, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
            map + (size_t)r0 * PC_EMISSIVE_MAP_W);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
}
/* For render.c's native lists: the map's texture (OpenGL) and whether the
 * shaders should read it. */
int pc_hw_emissive(unsigned* texture)
{
    emissive_sync();
    if (texture)
        *texture = emissive_texture;
    return emissive_shader_on;
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
    /* Text drawn three ways (TEXT_TAG_*): the way the options choose now. */
    {
        uint32_t drop = !(pc_ui_text_layout_on(pc_hw_scale) && pc_ui_upscale_draw_factor())
                          ? TEXT_TAG_CELL | TEXT_TAG_LINE
                          : pc_ui_text_layout == PC_UI_TEXT_LAYOUT_LINE ? TEXT_TAG_GLYPH | TEXT_TAG_CELL
                                                                        : TEXT_TAG_GLYPH | TEXT_TAG_LINE;
        int i = 0;
        while (i < count && !(vertices[i].window & drop))
            i += 3;
        if (i < count) {
            static hw_vertex kept[MAX_VERTICES];
            int n = 0;
            for (i = 0; i + 2 < count; i += 3)
                if (!(vertices[i].window & drop)) {
                    memcpy(kept + n, vertices + i, 3 * sizeof *kept);
                    n += 3;
                }
            if (!n)
                return;
            vertices = kept, count = n;
        }
    }
    /* MSAA: inside the scope the twins take it (2D at the pixel centres). */
    int centre = 0;
    if (ms_holds(fbo, clip)) {
        fbo = ms_target(0);
        for (int i = 0; i < count && !centre; i++)
            centre = vertices[i].z <= 0.0f;
    } else if (ms_scope == fbo)
        ms_end();
    for (int i = 0; i < count; i += 3)
        if (vertices[i].flags & 1) pc_hd_use(vertices[i].tpage, vertices[i].clut);
    emissive_sync();
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
    pc_hd_gl_bind(draw_program);
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
        gl_ActiveTexture(GL_TEXTURE7);
        gl_BindTexture(GL_TEXTURE_2D, emissive_texture);
        gl_ActiveTexture(GL_TEXTURE0);
        gl_Uniform1i(u_draw_emissive_on, emissive_shader_on);
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
    if (centre) /* every sample written as single-sampled (coverage at the pixel centre) */
        gl_Disable(GL_MULTISAMPLE);
    gl_Enable(GL_DEPTH_TEST); /* GL_ALWAYS: write depth in draw order, never reject */
    gl_DepthFunc(GL_ALWAYS);
    gl_DepthMask(GL_TRUE);
    if (blend < 0) {
        gl_Uniform1i(u_draw_pass, 0);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
    } else if (blend >= BLEND_TEXT) {
        /* Text from distance texels: its solid pixels (opaque texels half
         * covered and more) mark the G-buffer and depth as UI, as 2D does;
         * then its colour over the target by coverage (the mask bit kept),
         * opaque texels "over", translucent ones by the PS1 mode. */
        int mode = blend - BLEND_TEXT;
        gl_ColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_Uniform1i(u_draw_pass, 3);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
        gl_DepthMask(GL_FALSE);
        gl_ColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        for (GLuint i = 1; i < 4; i++)
            gl_ColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_Enable(GL_BLEND);
        gl_BlendEquation(GL_FUNC_ADD);
        gl_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl_Uniform1i(u_draw_pass, 4);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
        if (mode < 4) {
            gl_BlendEquation(mode == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
            gl_BlendFunc(GL_SRC_ALPHA, mode == 0 ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
            gl_Uniform1f(u_draw_text_factor, mode == 0 ? 0.5f : mode == 3 ? 0.25f : 1.0f);
            gl_Uniform1i(u_draw_pass, 5);
            gl_DrawArrays(GL_TRIANGLES, 0, count);
        }
        gl_Disable(GL_BLEND);
        for (GLuint i = 0; i < 4; i++)
            gl_ColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    } else if (blend == BLEND_FADE) {
        /* A screen fade: subtracted from what is not 3D under it only
         * (depth 0, and far: drawn twice, pc_hw_triangle). */
        set_blend(2);
        gl_DepthFunc(GL_EQUAL);
        gl_DepthMask(GL_FALSE);
        for (GLuint i = 1; i < 4; i++)
            gl_ColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_Uniform1i(u_draw_pass, 2);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
        gl_Disable(GL_BLEND);
        for (GLuint i = 1; i < 4; i++)
            gl_ColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    } else if (blend == BLEND_COVER) {
        /* Coverage over the target (anti-aliased 2D shapes): colour only,
         * the mask bit (alpha) and the surface behind kept, depth "UI". */
        gl_Enable(GL_BLEND);
        gl_BlendEquation(GL_FUNC_ADD);
        gl_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl_ColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        gl_ColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_ColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_ColorMaski(3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_Uniform1i(u_draw_pass, 2);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
        gl_Disable(GL_BLEND);
        for (GLuint i = 0; i < 4; i++)
            gl_ColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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
    gl_Enable(GL_MULTISAMPLE);
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
static int overlay_on, overlay_count;
static void overlay_hold(void);
static void overlay_bake_under(const hw_vertex* v, int count, const int* clip);
static int overlay_bake_rect(int x0, int y0, int x1, int y1);
static void overlay_bake_all(void);
static void flush(void)
{
    if (text_piece_count && !text_emitting)
        text_run_flush(); /* buffered text first: it was drawn before what follows */
    if (batch_count) {
        pc_ui_upscale_prepare(); /* the magnified 2D regions it samples */
        if (overlay_on && batch_blend >= BLEND_TEXT)
            overlay_hold(); /* text: at the window's resolution, unless something comes over it */
        else {
            overlay_bake_under(batch, batch_count, batch_clip);
            record_batch();
            draw_batch(target_fbo, batch, batch_count, batch_blend, batch_clip);
        }
        batch_count = 0;
    }
    if (has_pending_translucent) {
        has_pending_translucent = 0;
        const typeof(pending_translucent)* t = &pending_translucent;
        overlay_bake_rect(t->clip[0], t->clip[1], t->clip[0] + t->clip[2], t->clip[1] + t->clip[3]);
        if (draw_translucent(target_fbo, t->list, t->first, t->count, t->clip, t->ox, t->oy, t->window, t->mask, NULL,
                1.0f))
            record_translucent(t->list, t->first, t->count, t->clip, t->ox, t->oy, t->window, t->mask);
    }
}
/* ---- text at the window's resolution (ui_text_overlay) ----
 *
 * Text and icons from distance texels (BLEND_TEXT) drawn into the target
 * are anti-aliased at the internal resolution; a window larger than that
 * scales them up again (4x internal in a window six times the PS1's: a
 * pixel and a half each, nearest). With the overlay on and the window finer
 * than the target, such batches are held back instead (overlay[]) and drawn
 * by the presenters at the window's own resolution over the finished
 * picture (pc_hw_text_overlay). The order of drawing stays the game's: a
 * held-back triangle is drawn into the target ("baked", recorded with the
 * frame where it is drawn) as soon as anything else would draw where it is
 * (a primitive, a fill, an upload or copy, a native list) or anything reads
 * that area (a copy's source, a readback to native VRAM); one nothing came
 * near is still on top of the picture at the end of the frame, so drawing
 * it last, finer, is the same picture. Redraws of a recorded frame (RPC
 * ui_upscale redraw) draw the held-back text into it, dumps of the target
 * bake it first. */
#define OVERLAY_MAX 32768 /* triangles (each glyph three times: the original sprite, in its cell, as lines) */
typedef struct {
    hw_vertex v[3];
    int clip[4];      /* its drawing area */
    int blend;        /* BLEND_TEXT + mode */
    int box[4];       /* x0, y0, x1, y1 (exclusive): where it can draw, VRAM pixels, a pixel around */
} overlay_triangle;
static overlay_triangle overlay[OVERLAY_MAX];
static uint64_t overlay_cells[32]; /* 32 rows of 64 columns of 16 x 16 VRAM pixels: any held there */
static uint8_t overlay_pick[OVERLAY_MAX];
static long overlay_baked, overlay_presented; /* triangles (RPC ui_text: tests) */
static void record_vertices(const hw_vertex* v, int count, int blend, const int* clip, int triangles);
static void unrecord_triangles(int count);

static void overlay_cells_set(const int* b)
{
    for (int r = b[1] >> 4; r <= (b[3] - 1) >> 4 && r < 32; r++)
        for (int c = b[0] >> 4; c <= (b[2] - 1) >> 4 && c < 64; c++)
            if (r >= 0 && c >= 0)
                overlay_cells[r] |= 1ull << c;
}
static int overlay_cells_hit(int x0, int y0, int x1, int y1)
{
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > 1024 ? 1024 : x1, y1 = y1 > 512 ? 512 : y1;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    uint64_t columns = 0;
    for (int c = x0 >> 4; c <= (x1 - 1) >> 4; c++)
        columns |= 1ull << c;
    for (int r = y0 >> 4; r <= (y1 - 1) >> 4; r++)
        if (overlay_cells[r] & columns)
            return 1;
    return 0;
}
/* Draw the picked triangles (overlay_pick) into `fbo`, in their order, a
 * batch per run of one blend and drawing area; `record`: as part of the
 * frame being recorded (where they are drawn now). */
static void overlay_draw_picked(GLuint fbo, int record)
{
    static hw_vertex v[MAX_VERTICES];
    int n = 0, blend = 0, clip[4] = { 0, 0, 0, 0 };
    for (int i = 0; i <= overlay_count; i++) {
        const overlay_triangle* o = i < overlay_count && overlay_pick[i] ? &overlay[i] : NULL;
        if (i < overlay_count && !o)
            continue;
        if (n && (!o || o->blend != blend || memcmp(o->clip, clip, sizeof clip) || n + 3 > MAX_VERTICES)) {
            if (record)
                record_vertices(v, n, blend, clip, 1);
            pc_ui_upscale_prepare();
            draw_batch(fbo, v, n, blend, clip);
            n = 0;
        }
        if (!o)
            break;
        blend = o->blend;
        memcpy(clip, o->clip, sizeof clip);
        memcpy(v + n, o->v, sizeof o->v);
        n += 3;
    }
}
/* Remove the picked triangles. */
static void overlay_remove_picked(void)
{
    int kept = 0;
    memset(overlay_cells, 0, sizeof overlay_cells);
    for (int i = 0; i < overlay_count; i++) {
        if (overlay_pick[i])
            continue;
        if (kept != i)
            overlay[kept] = overlay[i];
        overlay_cells_set(overlay[kept].box);
        ++kept;
    }
    overlay_count = kept;
}
/* Bake the held-back triangles that may draw inside (x0, y0)-(x1, y1); 0
 * when none. */
static int overlay_bake_rect(int x0, int y0, int x1, int y1)
{
    if (!overlay_count || !overlay_cells_hit(x0, y0, x1, y1))
        return 0;
    int any = 0;
    for (int i = 0; i < overlay_count; i++) {
        const int* b = overlay[i].box;
        overlay_pick[i] = b[0] < x1 && x0 < b[2] && b[1] < y1 && y0 < b[3];
        any |= overlay_pick[i];
    }
    if (any) {
        for (int i = 0; i < overlay_count; i++)
            overlay_baked += overlay_pick[i];
        overlay_draw_picked(target_fbo, 1);
        overlay_remove_picked();
    }
    return any;
}
static void overlay_bake_all(void)
{
    if (overlay_count)
        overlay_bake_rect(-4096, -4096, 4096, 4096);
}
/* Before `count` vertices draw in drawing area `clip`: bake what they reach. */
static void overlay_bake_under(const hw_vertex* v, int count, const int* clip)
{
    if (!overlay_count)
        return;
    int cx0 = clip[0], cy0 = clip[1], cx1 = clip[0] + clip[2], cy1 = clip[1] + clip[3];
    if (!overlay_cells_hit(cx0, cy0, cx1, cy1))
        return;
    memset(overlay_pick, 0, (size_t)overlay_count);
    int any = 0;
    for (int t = 0; t + 2 < count; t += 3) {
        float x0 = fminf(v[t].x, fminf(v[t + 1].x, v[t + 2].x)), x1 = fmaxf(v[t].x, fmaxf(v[t + 1].x, v[t + 2].x));
        float y0 = fminf(v[t].y, fminf(v[t + 1].y, v[t + 2].y)), y1 = fmaxf(v[t].y, fmaxf(v[t + 1].y, v[t + 2].y));
        int b[4] = { (int)floorf(x0), (int)floorf(y0), (int)ceilf(x1) + 1, (int)ceilf(y1) + 1 };
        b[0] = b[0] > cx0 ? b[0] : cx0, b[1] = b[1] > cy0 ? b[1] : cy0;
        b[2] = b[2] < cx1 ? b[2] : cx1, b[3] = b[3] < cy1 ? b[3] : cy1;
        if (b[2] <= b[0] || b[3] <= b[1] || !overlay_cells_hit(b[0], b[1], b[2], b[3]))
            continue;
        for (int i = 0; i < overlay_count; i++) {
            const int* o = overlay[i].box;
            if (!overlay_pick[i] && o[0] < b[2] && b[0] < o[2] && o[1] < b[3] && b[1] < o[3])
                overlay_pick[i] = 1, any = 1;
        }
    }
    if (any) {
        for (int i = 0; i < overlay_count; i++)
            overlay_baked += overlay_pick[i];
        overlay_draw_picked(target_fbo, 1);
        overlay_remove_picked();
    }
}
/* Hold back the current batch (BLEND_TEXT) instead of drawing it. */
static void overlay_hold(void)
{
    int triangles = batch_count / 3;
    if (overlay_count + triangles > OVERLAY_MAX)
        overlay_bake_all(); /* (full: what is held is drawn) */
    if (overlay_count + triangles > OVERLAY_MAX) {
        record_batch();
        draw_batch(target_fbo, batch, batch_count, batch_blend, batch_clip);
        return;
    }
    for (int t = 0; t < triangles; t++) {
        overlay_triangle* o = &overlay[overlay_count++];
        memcpy(o->v, batch + t * 3, sizeof o->v);
        memcpy(o->clip, batch_clip, sizeof o->clip);
        o->blend = batch_blend;
        float x0 = fminf(o->v[0].x, fminf(o->v[1].x, o->v[2].x)), x1 = fmaxf(o->v[0].x, fmaxf(o->v[1].x, o->v[2].x));
        float y0 = fminf(o->v[0].y, fminf(o->v[1].y, o->v[2].y)), y1 = fmaxf(o->v[0].y, fmaxf(o->v[1].y, o->v[2].y));
        o->box[0] = (int)floorf(x0) - 1, o->box[1] = (int)floorf(y0) - 1;
        o->box[2] = (int)ceilf(x1) + 1, o->box[3] = (int)ceilf(y1) + 1;
        /* (never past its drawing area) */
        o->box[0] = o->box[0] > batch_clip[0] ? o->box[0] : batch_clip[0];
        o->box[1] = o->box[1] > batch_clip[1] ? o->box[1] : batch_clip[1];
        o->box[2] = o->box[2] < batch_clip[0] + batch_clip[2] ? o->box[2] : batch_clip[0] + batch_clip[2];
        o->box[3] = o->box[3] < batch_clip[1] + batch_clip[3] ? o->box[3] : batch_clip[1] + batch_clip[3];
        overlay_cells_set(o->box);
    }
    unrecord_triangles(triangles); /* (recorded where they are baked, if they are) */
}
/* Whether to hold text back (at each present, for the view presented): the
 * overlay on, text or icons from fonts, and the window finer than the
 * target. Turned off, what is held is drawn. */
static void overlay_update(void)
{
    float per_output = pc_hw_native_per_output();
    int on = pc_ui_text_overlay && pc_hw_active && (pc_ui_text || pc_button_prompts_active() || pc_ui_bubbles)
          && per_output > 0
          && 1.0f / per_output > (float)pc_hw_scale * 1.01f;
    if (!on && overlay_on)
        overlay_bake_all();
    overlay_on = on;
}

/* The VRAM a triangle with texel coordinates `uv` samples (its texels, a
 * texel around them, and its CLUT row), dirty_bands' layout; through a
 * texture window, or reaching a page edge (it may wrap), its whole page. */
static void sampled_bands(unsigned tpage, unsigned clut, unsigned window, const int* uv, uint32_t bands[16])
{
    int tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256, depth = (tpage >> 7) & 3;
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int u0 = 0, v0 = 0, u1 = 255, v1 = 255;
    if (!(window & 0x3ff)) {
        u0 = v0 = 255, u1 = v1 = 0;
        for (int i = 0; i < 3; i++) {
            u0 = uv[i * 2] < u0 ? uv[i * 2] : u0, u1 = uv[i * 2] > u1 ? uv[i * 2] : u1;
            v0 = uv[i * 2 + 1] < v0 ? uv[i * 2 + 1] : v0, v1 = uv[i * 2 + 1] > v1 ? uv[i * 2 + 1] : v1;
        }
        if (u0 <= 0 || u1 >= 255)
            u0 = 0, u1 = 255;
        else
            --u0, ++u1;
        if (v0 <= 0 || v1 >= 255)
            v0 = 0, v1 = 255;
        else
            --v0, ++v1;
    }
    memset(bands, 0, 16 * sizeof *bands);
    bands_for(tx + (u0 >> shift), ty + v0, tx + (u1 >> shift), ty + v1, bands);
    if (depth < 2) {
        int cx = (clut & 63) * 16, cy = (clut >> 6) & 511;
        bands_for(cx, cy, cx + (depth ? 255 : 15), cy, bands);
    }
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
int pc_hw_suppress; /* the packet being drawn is left out here (ui_bubbles.h) */
/* 2D polygon edges drawn in this ordering table (whole VRAM pixels): a
 * joined line along one reaches it (pc_hw_line). Open addressing, cleared
 * by generation at each table. */
#define EDGE_SLOTS 8192
static uint64_t edge_keys[EDGE_SLOTS];
static uint32_t edge_generations[EDGE_SLOTS], edge_generation = 1;
static uint64_t edge_key(int x0, int y0, int x1, int y1)
{
    uint32_t a = (uint32_t)(x0 & 0xFFFF) | (uint32_t)(y0 & 0xFFFF) << 16;
    uint32_t b = (uint32_t)(x1 & 0xFFFF) | (uint32_t)(y1 & 0xFFFF) << 16;
    return a < b ? (uint64_t)a << 32 | b : (uint64_t)b << 32 | a;
}
static int edge_find(uint64_t key, int add)
{
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40) & (EDGE_SLOTS - 1);
    for (int probe = 0; probe < 32; probe++, h = (h + 1) & (EDGE_SLOTS - 1)) {
        if (edge_generations[h] != edge_generation) {
            if (add) {
                edge_generations[h] = edge_generation;
                edge_keys[h] = key;
            }
            return 0;
        }
        if (edge_keys[h] == key)
            return 1;
    }
    return 0;
}
static void edge_note(int x0, int y0, int x1, int y1)
{
    if (pc_ui_lines == PC_UI_LINES_JOINED && pc_hw_scale > 1 && (x0 != x1 || y0 != y1))
        edge_find(edge_key(x0, y0, x1, y1), 1);
}
/* A triangle of a full-screen fade: one flat grey over the whole drawing area. */
static int screen_fade(const int* xy, const int* rgb, const int* clip)
{
    for (int k = 1; k < 9; k++)
        if (rgb[k] != rgb[0])
            return 0;
    int x0 = xy[0], x1 = xy[0], y0 = xy[1], y1 = xy[1];
    for (int k = 1; k < 3; k++) {
        x0 = xy[k * 2] < x0 ? xy[k * 2] : x0, x1 = xy[k * 2] > x1 ? xy[k * 2] : x1;
        y0 = xy[k * 2 + 1] < y0 ? xy[k * 2 + 1] : y0, y1 = xy[k * 2 + 1] > y1 ? xy[k * 2 + 1] : y1;
    }
    return rgb[0] > 0 && x0 <= clip[0] && y0 <= clip[1] && x1 >= clip[0] + clip[2] - 1 && y1 >= clip[1] + clip[3] - 1;
}
int pc_hw_triangle(const int* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage,
    unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check)
{
    if (text_piece_count && !text_emitting)
        text_run_flush(); /* buffered text was drawn before this */
    if (!pc_hw_active)
        return 0;
    if (pc_hw_suppress) {
        has_pending_depth = has_pending_precise = 0;
        return 0;
    }
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
    if (side_paint_count && !primitive_2d)
        side_paint_forget(clip[0], clip[1], clip[2], clip[3]); /* 3D (Hor+) spans the view */
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
    int anchored = edge_anchored() && !primitive_cover;
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
    } else if ((compress || extend) && layout_enabled() && primitive_anchor != PC_ANCHOR_SCENE) {
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
    /* 2D: a flipped sprite's coordinates (pc_hw_set_flipped_uv), and texels
     * kept inside the primitive (FLAG_CLAMP). */
    int texcoord[6], ui_flags = 0;
    for (int i = 0; i < 6; i++)
        texcoord[i] = uv[i] + (textured ? primitive_uv_shift[i & 1] : 0);
    if (textured && !(depth[0] | depth[1] | depth[2]))
        ui_flags |= FLAG_CLAMP;
    int bounds[4] = { 255, 255, 0, 0 };
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 2; k++) {
            int c = texcoord[i * 2 + k] < 0 ? 0 : texcoord[i * 2 + k] > 255 ? 255 : texcoord[i * 2 + k];
            bounds[k] = c < bounds[k] ? c : bounds[k];
            bounds[k + 2] = c > bounds[k + 2] ? c : bounds[k + 2];
        }
    /* 2D: the texels it samples, magnified (MMPX) or, for text and icons,
     * drawn from a font (ui_text, button_prompts: distance texels, drawn
     * anti-aliased in batches of their own, BLEND_TEXT). The far bound is an
     * edge the rasterizer never reaches: [min, max - 1], as the shader clamps. */
    int16_t ui_offset[2] = { 0, 0 };
    int distance = 0;
    if (pc_ui_upscale || pc_ui_text || pc_button_prompts_active() || pc_ui_bubbles) {
        pc_ui_upscale_draw_area(area);
        if (has_text_xy && text_use_image && textured) {
            /* A laid-out glyph: its image, looked up by text_run_flush. */
            ui_offset[0] = text_offset[0], ui_offset[1] = text_offset[1];
            memcpy(bounds, text_bounds, sizeof bounds); /* (the whole image: its strips sample across rows) */
            ui_flags |= FLAG_UI;
            distance = 1;
        } else if (ui && textured && !(depth[0] | depth[1] | depth[2])) {
            int u1 =bounds[2] > bounds[0] ? bounds[2] - 1 : bounds[0], v1 = bounds[3] > bounds[1] ? bounds[3] - 1 : bounds[1];
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
                ui_flags |= FLAG_UI;
            distance = found == 2;
        }
    }
    /* Blend mode, + 4 for translucent screen furniture (no depth): that
     * replaces the depth and surface behind it, so post effects and lights
     * treat it as UI. */
    int blend = semi ? (int)((tpage >> 5) & 3) + (depth[0] | depth[1] | depth[2] ? 0 : 4) : -1;
    if (exact_cover)
        blend = BLEND_COVER;
    else if (blend == 2 + 4 && !textured && screen_fade(xy, rgb, clip))
        blend = BLEND_FADE; /* recorded with where it lands (below) */
    if (distance)
        blend = BLEND_TEXT + (semi ? (int)((tpage >> 5) & 3) : 4);
    if (blend != batch_blend || memcmp(area, batch_clip, sizeof batch_clip)
        || batch_count + (blend == BLEND_FADE ? 6 : 3) > MAX_VERTICES) {
        flush();
        batch_blend = blend;
        memcpy(batch_clip, area, sizeof batch_clip);
    }
    if (textured) {
        if (!own_active)
            own_fold(); /* (a textured primitive drawn without pc_hw_sync_texture) */
        uint32_t needed[16];
        sampled_bands(tpage, clut, window, uv, needed);
        if (dirty_in(needed)) {
            flush();
            upload_dirty(needed);
        }
        if (own_count)
            own_apply();
    }
    uint8_t flags = (uint8_t)((textured ? FLAG_TEXTURED : 0) | (textured && (code & 1) ? FLAG_RAW : 0)
        | (semi || exact_cover ? FLAG_SEMI : 0) | (mask_set ? FLAG_MASK : 0) | (exact_cover ? FLAG_COVER : 0)
        | ui_flags);
    /* Positions: unrounded for projected 3D vertices, else the packet's. */
    float pos[6];
    for (int i = 0; i < 6; i++)
        pos[i] = (float)xy[i];
    /* 2D packets carry whole pixels unless the game placed them finer
     * (sub-pixel text, pc_ui_subpixel). */
    if (has_pending_precise && (!primitive_2d || pc_ui_subpixel) && pc_hw_subpixel)
        memcpy(pos, pending_precise, sizeof pos);
    has_pending_precise = 0;
    /* Shapes the port builds itself (joined lines, vector bubbles): their
     * own unrounded positions (pc_hw_shape_triangle). */
    if (has_exact)
        memcpy(pos, exact_xy, sizeof pos);
    if (has_text_xy)
        memcpy(pos, text_xy, sizeof pos);
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
    if (primitive_2d && !has_exact && !has_text_xy && !(depth[0] | depth[1] | depth[2]))
        for (int i = 0; i < 3; i++)
            edge_note(xy[i * 2], xy[i * 2 + 1], xy[(i + 1) % 3 * 2], xy[(i + 1) % 3 * 2 + 1]);
    uv = texcoord;
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
        v->window = window | triangle_tag;
        v->uv_min[0] = (uint8_t)bounds[0], v->uv_min[1] = (uint8_t)bounds[1];
        v->uv_max[0] = (uint8_t)bounds[2], v->uv_max[1] = (uint8_t)bounds[3];
        v->ui[0] = ui_offset[0], v->ui[1] = ui_offset[1];
    }
    if (blend == BLEND_FADE) { /* the fade and the part of the drawing area it covers (widescreen: the view, or not) */
        const hw_vertex* t = &batch[batch_count - 3];
        float rect[4] = { (fminf(t[0].x, fminf(t[1].x, t[2].x)) - clip[0]) / clip[2],
            (fminf(t[0].y, fminf(t[1].y, t[2].y)) - clip[1]) / clip[3],
            (fmaxf(t[0].x, fmaxf(t[1].x, t[2].x)) - clip[0]) / clip[2],
            (fmaxf(t[0].y, fmaxf(t[1].y, t[2].y)) - clip[1]) / clip[3] };
        record_fade(rgb[0] / 255.0f, rect);
        /* Again at far depth (z 65536: depth 1, what fills and uploads
         * leave): a backdrop the game filled or uploaded (ClearImage,
         * LoadImage) is no 3D either. Subtracted at depth 0 alone, the 2D
         * drawn over it darkened while it kept its brightness. */
        memcpy(&batch[batch_count], &batch[batch_count - 3], 3 * sizeof *batch);
        for (int i = 0; i < 3; i++)
            batch[batch_count + i].z = 65536.0f;
        batch_count += 3;
        record_triangle(NULL, code, tpage, clut, uv);
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
static void rect_triangles(int x, int y, int w, int h, int u, int v, unsigned rgb, unsigned code,
    unsigned tpage, unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check);

/* ---- ui_text_layout line: runs of glyph sprites laid out as lines ----
 *
 * While line layout is on (ui_text.h), a 2D sprite that is (a strip of) a
 * dialogue or menu glyph is drawn as itself, tagged TEXT_TAG_GLYPH (drawn
 * only while the layout is off: redraws of the same frame compare), and
 * buffered with the drawing state it was drawn in. The next primitive of
 * any other kind (or the end of the batch) ends the run: ui_text.c lays its
 * glyphs out as lines and each piece is drawn again as its laid-out quad,
 * tagged TEXT_TAG_LINE, in its order and state, before that primitive. */
#define TEXT_PIECES 4096
typedef struct {
    pc_ui_text_piece_info info;
    unsigned rgb, code, window;
    int mask_set, mask_check;
    int is_2d, room, cover, extend, narrowed, anchor, uv_shift[2];
    float span[2];
    int sprite; /* a sprite (else a scaled quad) */
} text_piece;
static text_piece text_pieces[TEXT_PIECES];

/* A quad's two triangles at unrounded corners (x0, y0)-(x1, y1) over texels
 * (u0, v0)-(u1, v1) (exclusive ends), in the current state; `image`: from
 * the laid-out glyph image at text_offset, else as any 2D region. */
static void text_quad(float x0, float y0, float x1, float y1, int u0, int v0, int u1, int v1, int image,
    const text_piece* p)
{
    float corners[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
    int texels[4][2] = { { u0, v0 }, { u1, v0 }, { u0, v1 }, { u1, v1 } };
    static const int order[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
    int colors[9];
    for (int k = 0; k < 9; k++)
        colors[k] = (p->rgb >> ((k % 3) * 8)) & 255;
    pc_hw_set_packet(NULL);
    for (int t = 0; t < 2; t++) {
        int xy[6], uv[6];
        for (int k = 0; k < 3; k++) {
            int c = order[t][k];
            text_xy[k * 2] = corners[c][0], text_xy[k * 2 + 1] = corners[c][1];
            xy[k * 2] = (int)floorf(corners[c][0] + 0.5f), xy[k * 2 + 1] = (int)floorf(corners[c][1] + 0.5f);
            uv[k * 2] = texels[c][0], uv[k * 2 + 1] = texels[c][1];
        }
        has_text_xy = 1;
        text_use_image = image;
        pc_hw_triangle(xy, uv, colors, p->code, p->info.tpage, p->info.clut, p->window, p->info.clip, p->mask_set,
            p->mask_check);
        has_text_xy = text_use_image = 0;
    }
}

/* Each buffered piece drawn as its laid-out quad (or, not laid out, as the
 * original), in its order and drawing state. */
static void text_emit(int count, const pc_ui_text_quad* quads)
{
    for (int i = 0; i < count; i++) {
        const text_piece* p = &text_pieces[i];
        const pc_ui_text_quad* q = &quads[i];
        primitive_2d = p->is_2d, primitive_room = p->room, primitive_cover = p->cover;
        primitive_extend = p->extend, primitive_narrowed = p->narrowed, primitive_anchor = p->anchor;
        primitive_uv_shift[0] = p->uv_shift[0], primitive_uv_shift[1] = p->uv_shift[1];
        primitive_span[0] = p->span[0], primitive_span[1] = p->span[1];
        has_line_x = has_exact = exact_cover = has_pending_depth = has_pending_precise = 0;
        const pc_ui_text_piece_info* in = &p->info;
        int found = 0;
        if (q->image >= 0) {
            found = pc_ui_upscale_lookup_image(q->image, pc_hw_scale, text_offset);
            if (found < 0) { /* the atlas is full: as pc_hw_triangle does */
                flush();
                pc_ui_upscale_reset();
                forget_recordings();
                found = pc_ui_upscale_lookup_image(q->image, pc_hw_scale, text_offset);
            }
        }
        if (found > 0) {
            text_bounds[0] = text_bounds[1] = 0, text_bounds[2] = q->w, text_bounds[3] = q->h;
            text_quad(q->x, q->y, q->x + q->w * q->k, q->y + (q->row1 - q->row0) * q->k, 0, q->row0, q->w, q->row1, 1,
                p);
        }
        else if (p->sprite) /* not laid out: the original */
            rect_triangles((int)in->x, (int)in->y, 12, in->h, in->u, in->v, p->rgb, p->code, in->tpage, in->clut,
                p->window, in->clip, p->mask_set, p->mask_check);
        else
            text_quad(in->x, in->y, in->x + 12 * in->k, in->y + in->h * in->k, in->u, in->v, in->u + 12,
                in->v + in->h, 0, p);
    }
}

static void text_run_flush(void)
{
    if (!text_piece_count || text_emitting)
        return;
    int count = text_piece_count;
    text_piece_count = 0;
    text_emitting = 1;
    /* The state of whatever is being drawn now, restored after. */
    int was_2d = primitive_2d, was_room = primitive_room, was_cover = primitive_cover, was_extend = primitive_extend;
    int was_narrowed = primitive_narrowed, was_anchor = primitive_anchor, was_suppress = pc_hw_suppress;
    int was_shift[2] = { primitive_uv_shift[0], primitive_uv_shift[1] };
    float was_span[2] = { primitive_span[0], primitive_span[1] };
    uintptr_t was_packet = current_packet;
    int was_line_x = has_line_x, was_exact = has_exact, was_cover_exact = exact_cover;
    int was_depth = has_pending_depth, was_precise = has_pending_precise;
    float saved_line_x[3], saved_exact[6], saved_precise[6];
    int saved_depth[3];
    memcpy(saved_line_x, line_x, sizeof saved_line_x);
    memcpy(saved_exact, exact_xy, sizeof saved_exact);
    memcpy(saved_precise, pending_precise, sizeof saved_precise);
    memcpy(saved_depth, pending_depth, sizeof saved_depth);

    static pc_ui_text_piece_info infos[TEXT_PIECES];
    static pc_ui_text_quad quads[TEXT_PIECES];
    for (int i = 0; i < count; i++)
        infos[i] = text_pieces[i].info;
    pc_hw_suppress = 0;
    /* Each glyph in its cell (ui_text_layout glyph), then as lines (line). */
    for (int mode = PC_UI_TEXT_LAYOUT_GLYPH; mode <= PC_UI_TEXT_LAYOUT_LINE; mode++) {
        pc_ui_text_layout_run(native_vram, infos, count, pc_hw_scale, mode, quads);
        triangle_tag = mode == PC_UI_TEXT_LAYOUT_LINE ? TEXT_TAG_LINE : TEXT_TAG_CELL;
        text_emit(count, quads);
    }
    triangle_tag = 0;
    primitive_2d = was_2d, primitive_room = was_room, primitive_cover = was_cover, primitive_extend = was_extend;
    primitive_narrowed = was_narrowed, primitive_anchor = was_anchor, pc_hw_suppress = was_suppress;
    primitive_uv_shift[0] = was_shift[0], primitive_uv_shift[1] = was_shift[1];
    primitive_span[0] = was_span[0], primitive_span[1] = was_span[1];
    current_packet = was_packet;
    has_line_x = was_line_x, has_exact = was_exact, exact_cover = was_cover_exact;
    has_pending_depth = was_depth, has_pending_precise = was_precise;
    memcpy(line_x, saved_line_x, sizeof saved_line_x);
    memcpy(exact_xy, saved_exact, sizeof saved_exact);
    memcpy(pending_precise, saved_precise, sizeof saved_precise);
    memcpy(pending_depth, saved_depth, sizeof saved_depth);
    text_emitting = 0;
}

/* Buffer a piece (the drawing state now is its own) and start drawing its
 * original tagged TEXT_TAG_GLYPH (pc_hw_text_piece_done ends that). */
/* The command word of the sprite or polygon being drawn (boot_runtime.c):
 * which printer string a glyph piece belongs to (ui_text.c). */
extern const void* pc_gpu_command __attribute__((weak));
static void text_piece_add(const pc_ui_text_piece_info* info, int sprite, unsigned rgb, unsigned code,
    unsigned window, int mask_set, int mask_check)
{
    if (text_piece_count == TEXT_PIECES)
        text_run_flush();
    text_piece* p = &text_pieces[text_piece_count++];
    p->info = *info;
    pc_ui_text_piece_call(&pc_gpu_command ? pc_gpu_command : NULL, &p->info);
    p->sprite = sprite;
    p->rgb = rgb, p->code = code, p->window = window, p->mask_set = mask_set, p->mask_check = mask_check;
    p->is_2d = primitive_2d, p->room = primitive_room, p->cover = primitive_cover, p->extend = primitive_extend;
    p->narrowed = primitive_narrowed, p->anchor = primitive_anchor;
    p->uv_shift[0] = primitive_uv_shift[0], p->uv_shift[1] = primitive_uv_shift[1];
    p->span[0] = primitive_span[0], p->span[1] = primitive_span[1];
    text_emitting = 1; /* (the original does not end the run) */
    triangle_tag = TEXT_TAG_GLYPH;
}
void pc_hw_text_piece_done(void)
{
    triangle_tag = 0;
    text_emitting = 0;
}
/* A 2D textured quad (boot_polygons.inc), corners in order 0 1 / 2 3 at
 * unrounded screen positions: 1 when it is a scaled glyph cell the layout
 * takes (its triangles, drawn next, are the original: the caller then calls
 * pc_hw_text_piece_done). */
int pc_hw_text_quad(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut,
    unsigned window, const int* clip, int mask_set, int mask_check)
{
    if (text_emitting || !pc_hw_active || pc_hw_suppress || !(code & 4) || !pc_ui_text_layout_on(pc_hw_scale))
        return 0;
    float x0 = xy[0], y0 = xy[1], x1 = xy[6], y1 = xy[7];
    if (xy[2] != x1 || xy[3] != y0 || xy[4] != x0 || xy[5] != y1 || x1 <= x0 || y1 <= y0)
        return 0; /* not an axis-aligned rectangle */
    int u0 = uv[0], v0 = uv[1];
    if (uv[2] != u0 + 12 || uv[3] != v0 || uv[4] != u0 || uv[5] != v0 + 12 || uv[6] != u0 + 12 || uv[7] != v0 + 12)
        return 0; /* not a whole cell, upright */
    float k = (x1 - x0) / 12.0f;
    if (fabsf((y1 - y0) / 12.0f - k) > 0.01f * k || !pc_ui_text_piece(native_vram, tpage, clut, u0, v0, 12, 12))
        return 0;
    pc_ui_text_piece_info info = { x0, y0, k, u0, v0, 12, tpage, clut, { clip[0], clip[1], clip[2], clip[3] } };
    text_piece_add(&info, 0, (unsigned)(rgb[0] | rgb[1] << 8 | rgb[2] << 16), code, window, mask_set, mask_check);
    return 1;
}

/* A battle status word's glyph sprite (ui_text.c pc_text_status: SLOW!,
 * 3 CHAINS): drawn as itself tagged TEXT_TAG_GLYPH, then as its glyph's
 * image where the word is set, tagged for both layouts (the word is set the
 * same either way); 1 when it was one. */
static int status_rect(int x, int y, int w, int h, int u, int v, unsigned rgb, unsigned code, unsigned tpage,
    unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check)
{
    pc_ui_text_quad q;
    if (!pc_ui_text_status_quad(native_vram, &pc_gpu_command ? pc_gpu_command : NULL, u, v, w, h, tpage, clut, &q))
        return 0;
    triangle_tag = TEXT_TAG_GLYPH;
    rect_triangles(x, y, w, h, u, v, rgb, code, tpage, clut, window, clip, mask_set, mask_check);
    triangle_tag = 0;
    int found = pc_ui_upscale_lookup_image(q.image, pc_hw_scale, text_offset);
    if (found < 0) { /* the atlas is full: as pc_hw_triangle does */
        flush();
        pc_ui_upscale_reset();
        forget_recordings();
        found = pc_ui_upscale_lookup_image(q.image, pc_hw_scale, text_offset);
    }
    if (found > 0) {
        text_piece p;
        memset(&p, 0, sizeof p);
        p.info = (pc_ui_text_piece_info){ (float)x, (float)y, 1.0f, u, v, h, tpage, clut,
            { clip[0], clip[1], clip[2], clip[3] } };
        p.rgb = rgb, p.code = code, p.window = window, p.mask_set = mask_set, p.mask_check = mask_check;
        text_bounds[0] = text_bounds[1] = 0, text_bounds[2] = q.w, text_bounds[3] = q.h;
        float x0 = (float)x + q.x, y0 = (float)y + q.y;
        for (int tag = 0; tag < 2; tag++) {
            triangle_tag = tag ? TEXT_TAG_LINE : TEXT_TAG_CELL;
            text_quad(x0, y0, x0 + q.w * q.k, y0 + q.h * q.k, 0, 0, q.w, q.h, 1, &p);
        }
        triangle_tag = 0;
    }
    return 1;
}

void pc_hw_rect(int x, int y, int w, int h, int u, int v, unsigned rgb, unsigned code,
    unsigned tpage, unsigned clut, unsigned window, const int* clip, int mask_set, int mask_check)
{
    if (!text_emitting && pc_hw_active && !pc_hw_suppress && (code & 4) && pc_ui_text_layout_on(pc_hw_scale)
        && status_rect(x, y, w, h, u, v, rgb, code, tpage, clut, window, clip, mask_set, mask_check))
        return;
    if (!text_emitting && pc_hw_active && !pc_hw_suppress && (code & 4) && pc_ui_text_layout_on(pc_hw_scale)
        && pc_ui_text_piece(native_vram, tpage, clut, u, v, w, h)) {
        pc_ui_text_piece_info info = { (float)x, (float)y, 1.0f, u, v, h, tpage, clut,
            { clip[0], clip[1], clip[2], clip[3] } };
        text_piece_add(&info, 1, rgb, code, window, mask_set, mask_check);
        rect_triangles(x, y, w, h, u, v, rgb, code, tpage, clut, window, clip, mask_set, mask_check);
        pc_hw_text_piece_done();
        return;
    }
    rect_triangles(x, y, w, h, u, v, rgb, code, tpage, clut, window, clip, mask_set, mask_check);
}

static void rect_triangles(int x, int y, int w, int h, int u, int v, unsigned rgb, unsigned code,
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
/* ui_lines legacy: one-pixel-wide line segment as a quad (lines are never
 * textured): span the major axis end to end, including the end pixel, one
 * pixel thick (upscaled, its slanted ends leave gaps at polyline joins and
 * miss the edges of polygons drawn along it). */
static void line_legacy(int x0, int y0, int x1, int y1, unsigned c0, unsigned c1, int semi, unsigned tpage,
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
    int anchored = edge_anchored();
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

/* A 2D triangle the port builds (joined lines, vector bubbles): unrounded
 * positions (VRAM pixels, drawing offset applied), drawn through the
 * packet path (widescreen placement, batching, recording). cover: coverage
 * blending (FLAG_COVER), uv[0], uv[2], uv[4] each vertex's coverage 0..256;
 * anchored_x: final X of each vertex for widescreen-anchored 2D (NULL:
 * placed like the packet's other 2D). */
static void shape_triangle(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage,
    unsigned clut, unsigned window, const int* clip, int mask_set, int cover, const float* anchored_x)
{
    int ixy[6];
    for (int i = 0; i < 6; i++)
        ixy[i] = (int)floorf(xy[i] + 0.5f);
    memcpy(exact_xy, xy, sizeof exact_xy);
    has_exact = 1;
    exact_cover = cover;
    if (anchored_x) {
        memcpy(line_x, anchored_x, sizeof line_x);
        has_line_x = pc_hw_active;
    }
    pc_hw_triangle(ixy, uv, rgb, code, tpage, clut, window, clip, mask_set, 0);
    has_exact = exact_cover = has_line_x = 0;
}
void pc_hw_shape_triangle(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage,
    unsigned clut, unsigned window, const int* clip, int mask_set, int cover)
{
    pc_hw_set_packet(NULL);
    shape_triangle(xy, uv, rgb, code, tpage, clut, window, clip, mask_set, cover, NULL);
}
int pc_ui_lines = 1;         /* PC_UI_LINES_*: joined (default) or legacy */
const char* const pc_ui_lines_names[2] = { "legacy", "joined" };
int pc_ui_outline_boost = 0; /* 2D lines this many output pixels thicker (0-3) */
long pc_hw_lines_drawn;      /* line segments drawn here (RPC ui_bubbles: tests) */
extern int pc_gl_view[4] __attribute__((weak)); /* the presented view (gl_present.c / vk_present.c) */
static int display_rect[4];                     /* (below) */
/* Native (VRAM) pixels per output pixel: the display area's height over the
 * presented view's. */
float pc_hw_native_per_output(void)
{
    int view_h = &pc_gl_view ? pc_gl_view[3] : 0, display_h = display_rect[3] > 0 ? display_rect[3] : 240;
    return view_h > 0 ? (float)display_h / (float)view_h : 1.0f / (float)pc_hw_scale;
}
/* An axis-aligned quad (x0, y0)-(x1, y1) in one colour, or a general one
 * (q: 4 corners, order 0 1 / 2 3) with per-corner colours. */
static void line_quad(const float q[4][2], const int colors[4][3], int semi, unsigned tpage, const int* clip,
    int mask_set, const float* anchor_shift, float ax0, float ax1)
{
    static const int order[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
    int uv[6] = { 0 };
    for (int t = 0; t < 2; t++) {
        float xy[6], ax[3];
        int rgb[9];
        for (int i = 0; i < 3; i++) {
            const float* c = q[order[t][i]];
            xy[i * 2] = c[0];
            xy[i * 2 + 1] = c[1];
            memcpy(rgb + i * 3, colors[order[t][i]], sizeof colors[0]);
            if (anchor_shift) {
                /* Widescreen-anchored HUD lines: each end's shift (one pixel
                 * thick, as legacy), linearly in between. */
                float f = ax1 != ax0 ? (c[0] - ax0) / (ax1 - ax0) : 0.0f;
                f = f < 0 ? 0 : f > 1 ? 1 : f;
                ax[i] = c[0] + anchor_shift[0] + (anchor_shift[1] - anchor_shift[0]) * f;
            }
        }
        shape_triangle(xy, uv, rgb, semi ? 2 : 0, tpage, 0, 0, clip, mask_set, 0, anchor_shift ? ax : NULL);
    }
}
/* A line segment (GP0 0x40-0x5F) at the internal resolution (ui_lines
 * joined): what it covers at 1x, crisp at any scale and closed at joins.
 *
 * - The band: the pixel-centre line (end pixel included) one native pixel
 *   across its minor axis, so 1x samples the CPU rasterizer's pixels (ties
 *   round away from the start as it does) and 4x is the same line, scale
 *   pixels thick.
 * - Upscaled, a line along the edge of a 2D polygon drawn before it in
 *   the same ordering table (the game outlines its talk bubbles' fill this
 *   way) also reaches that edge, which runs through the pixels' corners: at
 *   1x the edge's pixels are the line's, upscaled the fill's sliver showed
 *   outside the line (light dots along the outline). Other lines keep one
 *   pixel across.
 * - Upscaled, each end pixel is drawn whole (what the PS1 covers there), so
 *   segments meeting at a pixel join without notches whatever their angles.
 *   Not for semi-transparent lines (the overlap would blend twice).
 * - ui_outline_boost: every edge moved out by half the boost (output pixels). */
void pc_hw_line(int x0, int y0, int x1, int y1, unsigned c0, unsigned c1, int semi, unsigned tpage,
    const int* clip, int mask_set)
{
    if (pc_hw_suppress || !pc_hw_active)
        return;
    ++pc_hw_lines_drawn;
    if (pc_ui_lines == PC_UI_LINES_LEGACY) {
        line_legacy(x0, y0, x1, y1, c0, c1, semi, tpage, clip, mask_set);
        return;
    }
    pc_hw_set_packet(NULL);
    int dx = x1 - x0, dy = y1 - y0;
    int xmajor = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    int upscaled = pc_hw_scale > 1;
    float boost = pc_ui_outline_boost > 0 ? (float)pc_ui_outline_boost * pc_hw_native_per_output() * 0.5f : 0.0f;
    /* Major axis a, minor b. */
    float a0 = (float)(xmajor ? x0 : y0), b0 = (float)(xmajor ? y0 : x0);
    float a1 = (float)(xmajor ? x1 : y1), b1 = (float)(xmajor ? y1 : x1);
    float da = a1 - a0, db = b1 - b0;
    float slope = da != 0.0f ? db / da : 0.0f;
    /* The CPU rasterizer rounds halves away from the start: shift the band
     * that way by half the smallest step from a tie to a non-tie (1 / 2
     * major steps), well above the GPU's sub-pixel snapping. */
    float steps = fabsf(da) > 1.0f ? fabsf(da) : 1.0f;
    float tie = db > 0 ? 0.5f / steps : db < 0 ? -0.5f / steps : 0.0f;
    /* Upscaled, a hair wider (1/256 pixel) on both sides: where an edge of
     * a polygon drawn along the line passes exactly through a sample, the
     * line takes it, not the polygon. */
    float hair = upscaled ? 1.0f / 256.0f : 0.0f;
    /* Along the edge of a 2D polygon drawn before it (a fill it outlines),
     * the band reaches the edge where it runs outside (slopes < 0). */
    int edge = upscaled && slope < 0 && edge_find(edge_key(x0, y0, x1, y1), 0);
    float up = 0.5f + boost + hair + (edge ? -0.5f * slope + fabsf(tie) : 0.0f), down = 0.5f + boost + hair;
    float ends[2] = { a0 < a1 ? a0 : a1, (a0 > a1 ? a0 : a1) + 1.0f };
    int colors[2][3];
    for (int ch = 0; ch < 3; ch++) {
        colors[0][ch] = (c0 >> (ch * 8)) & 255;
        colors[1][ch] = (c1 >> (ch * 8)) & 255;
    }
    float q[4][2];
    int qc[4][3];
    for (int e = 0; e < 2; e++) {
        float a = ends[e];
        float centre = b0 + 0.5f + slope * (a - a0 - 0.5f) + tie;
        float b[2] = { centre - up, centre + down };
        const int* c = colors[(a0 <= a1) == (e == 0) ? 0 : 1];
        for (int k = 0; k < 2; k++) {
            float* v = q[e + k * 2];
            v[0] = xmajor ? a : b[k];
            v[1] = xmajor ? b[k] : a;
            memcpy(qc[e + k * 2], c, sizeof qc[0]);
        }
    }
    int anchored = edge_anchored();
    float shift[2] = { 0, 0 };
    if (anchored) {
        shift[0] = anchor_x(x0 + 0.5f) - 0.5f - (float)x0;
        shift[1] = anchor_x(x1 + 0.5f) - 0.5f - (float)x1;
    }
    float sx0 = (float)x0 + 0.5f, sx1 = (float)x1 + 0.5f;
    line_quad(q, qc, semi, tpage, clip, mask_set, anchored ? shift : NULL, sx0, sx1);
    if ((upscaled || boost > 0) && !semi) {
        const int ends_xy[2][2] = { { x0, y0 }, { x1, y1 } };
        for (int e = 0; e < (dx || dy ? 2 : 1); e++) {
            float x = (float)ends_xy[e][0], y = (float)ends_xy[e][1];
            float sq[4][2] = { { x - boost, y - boost }, { x + 1 + boost, y - boost }, { x - boost, y + 1 + boost },
                { x + 1 + boost, y + 1 + boost } };
            int sc[4][3];
            for (int i = 0; i < 4; i++)
                memcpy(sc[i], colors[e], sizeof sc[0]);
            line_quad(sq, sc, 0, tpage, clip, mask_set, anchored ? shift : NULL, sx0, sx1);
        }
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
    overlay_bake_rect(x, y, x + w, y + h);
    record_fill(x, y, w, h, r, g, b);
    screen_copy_write(0, 0, x, y, w, h, 0);
    side_paint_forget(x, y, w, h);
    fill_target(target_fbo, x, y, w, h, r, g, b);
}
static void fill_target(GLuint fbo, int x, int y, int w, int h, int r, int g, int b)
{
    int s = pc_hw_scale;
    /* A fill over a native list's area clears what hid packets there. */
    if (occlusion_on && x < occlusion_rect[0] + occlusion_rect[2] && occlusion_rect[0] < x + w
        && y < occlusion_rect[1] + occlusion_rect[3] && occlusion_rect[1] < y + h)
        occlusion_on = 0;
    if (ms_scope == fbo) { /* MSAA: a fill inside the scope clears the twins there */
        int area[4] = { x, y, w, h };
        if (ms_holds(fbo, area))
            fbo = ms_target(0);
        else
            ms_end_over(x, y, w, h);
    }
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
/* ---- widescreen: the view's sides beside extended 2D ----
 *
 * Compressed 2D leaves the sides of the 16:9 view (beyond the drawing
 * area's compressed edges) to what covers the view: the 3D, full-view 2D,
 * fills. Untextured 2D reaching the screen's edge extends into them
 * (primitive_extend: menu bars). On the PS1 a screen whose background is 2D
 * (the title's memory card screen) erases its bars by drawing that
 * background again over the whole 4:3 screen; compressed, it never reaches
 * the sides, so what bars had painted there stayed in the frame buffers for
 * good (each buffer its own: after the card screen closed, the right ends
 * of its three save bars; above the first save, the end of the "MEMORY CARD
 * slot 2" bar it slid away from, different in each buffer, so it flickered
 * at 30 Hz; GitHub issue #14).
 *
 * So the sides an extension painted are remembered (side_paint: VRAM
 * rectangles, x1/y1 exclusive), and opaque compressed 2D rectangles that
 * reach the screen's edge (they cover the very pixels the extension
 * continues) clear the remembered side beside their rows to black, as it
 * is beside compressed 2D: the extension goes with the bar it continued.
 * Whatever else draws over a remembered side (3D, full-view and anchored
 * 2D, fills, uploads, copies) makes it theirs: forgotten, never cleared. */
#define MAX_SIDE_PAINT 32
static int side_paint[MAX_SIDE_PAINT][4], side_paint_count;
static void side_paint_add(int x0, int y0, int x1, int y1)
{
    if (x1 <= x0 || y1 <= y0)
        return;
    for (int i = 0; i < side_paint_count; i++) {
        int* r = side_paint[i];
        if (x0 >= r[0] && y0 >= r[1] && x1 <= r[2] && y1 <= r[3])
            return; /* already there */
        if (r[0] >= x0 && r[1] >= y0 && r[2] <= x1 && r[3] <= y1) {
            r[0] = x0, r[1] = y0, r[2] = x1, r[3] = y1; /* grows */
            return;
        }
    }
    if (side_paint_count == MAX_SIDE_PAINT) { /* full: the oldest goes (never cleared) */
        memmove(side_paint, side_paint + 1, sizeof side_paint[0] * (MAX_SIDE_PAINT - 1));
        --side_paint_count;
    }
    int* r = side_paint[side_paint_count++];
    r[0] = x0, r[1] = y0, r[2] = x1, r[3] = y1;
}
/* Something else drew over VRAM rectangle (x, y, w, h). */
static void side_paint_forget(int x, int y, int w, int h)
{
    for (int i = 0; i < side_paint_count;) {
        const int* r = side_paint[i];
        if (x < r[2] && r[0] < x + w && y < r[3] && r[1] < y + h) {
            memmove(side_paint[i], side_paint[i + 1], sizeof side_paint[0] * (size_t)(side_paint_count - i - 1));
            --side_paint_count;
        } else
            ++i;
    }
}
/* Clear what is remembered inside (x0, y0)-(x1, y1) to black (in the
 * frame's draw order: recorded, so the in-between images clear it too). */
static void side_paint_clear(int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < side_paint_count;) {
        int r[4];
        memcpy(r, side_paint[i], sizeof r);
        int a0 = r[0] > x0 ? r[0] : x0, b0 = r[1] > y0 ? r[1] : y0;
        int a1 = r[2] < x1 ? r[2] : x1, b1 = r[3] < y1 ? r[3] : y1;
        if (a1 <= a0 || b1 <= b0) {
            ++i;
            continue;
        }
        flush();
        overlay_bake_rect(a0, b0, a1, b1);
        record_fill(a0, b0, a1 - a0, b1 - b0, 0, 0, 0);
        fill_target(target_fbo, a0, b0, a1 - a0, b1 - b0, 0, 0, 0);
        int stale[4] = { a0, b0, a1 - a0, b1 - b0 };
        mark_stale_kind(stale, STALE_PLACED);
        /* What is left of it: above, below, left and right of the cleared part. */
        memmove(side_paint[i], side_paint[i + 1], sizeof side_paint[0] * (size_t)(side_paint_count - i - 1));
        --side_paint_count;
        int left_over[4][4] = { { r[0], r[1], r[2], b0 }, { r[0], b1, r[2], r[3] }, { r[0], b0, a0, b1 },
            { a1, b0, r[2], b1 } };
        for (int k = 0; k < 4; k++)
            side_paint_add(left_over[k][0], left_over[k][1], left_over[k][2], left_over[k][3]);
        i = 0; /* (the list changed) */
    }
}
/* A 2D primitive's bounds (pc_hw_set_bounds, its placement decided). */
static void side_paint_primitive(int x0, int y0, int x1, int y1, unsigned code, const int* clip)
{
    if (!pc_hw_active || pc_hw_suppress)
        return;
    if (!pc_gte_widescreen || !layout_enabled()) {
        side_paint_count = 0;
        return;
    }
    int left = clip[0], right = clip[0] + clip[2], top = clip[1], bottom = clip[1] + clip[3];
    y0 = y0 > top ? y0 : top, y1 = y1 < bottom ? y1 : bottom;
    if (y1 <= y0)
        return;
    float centre = clip[0] + clip[2] * 0.5f;
    int inner_left = (int)ceilf(centre + (left - centre) * 0.75f - 0.001f);
    int inner_right = (int)floorf(centre + (right - centre) * 0.75f + 0.001f);
    int anchored = edge_anchored() && !primitive_cover;
    if (primitive_cover || primitive_narrowed || primitive_anchor == PC_ANCHOR_SCENE
        || (primitive_anchor == PC_ANCHOR_FILL && !primitive_cover)) {
        /* Full-view, projected or view-wide 2D: it may draw anywhere in the area. */
        if (side_paint_count)
            side_paint_forget(primitive_cover ? x0 : left, y0, primitive_cover ? x1 - x0 : right - left, y1 - y0);
        return;
    }
    if (anchored) {
        if (side_paint_count) {
            int a = (int)floorf(anchor_x((float)x0)), b = (int)ceilf(anchor_x((float)x1));
            side_paint_forget(a, y0, b - a, y1 - y0);
        }
        return;
    }
    if (primitive_extend > 0)
        side_paint_add(inner_right, y0, right, y1);
    else if (primitive_extend < 0)
        side_paint_add(left, y0, inner_left, y1);
    else if (side_paint_count && (code & 0xe0) == 0x60 && !(code & 2)) {
        /* An opaque rectangle (sprite, tile) over the screen's edge column. */
        if (x1 >= right - 1 && x0 < right)
            side_paint_clear(inner_right, y0, right, y1);
        if (x0 <= left + 1 && x1 > left)
            side_paint_clear(left, y0, inner_left, y1);
    }
}
static int widescreen_screen_upload(int x, int y, int w, int h);
/* Native VRAM rectangle (already written by the CPU) into the scaled target. */
void pc_hw_upload(int x, int y, int w, int h)
{
    if (!pc_hw_active || w <= 0 || h <= 0)
        return;
    flush();
    overlay_bake_rect(x, y, x + w, y + h);
    if (ms_scope == target_fbo)
        ms_end_over(x, y, w, h);
    record_write(x, y, w, h);
    screen_copy_write(0, 0, x, y, w, h, 0);
    side_paint_forget(x, y, w, h);
    pc_hw_mark(x, y, x + w - 1, y + h - 1);
    {
        uint32_t rect[16] = { 0 };
        own_fold();
        bands_for(x, y, x + w - 1, y + h - 1, rect);
        upload_dirty(rect);
    }
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
    overlay_bake_rect(sx, sy, sx + w, sy + h); /* what it reads */
    overlay_bake_rect(dx, dy, dx + w, dy + h);
    if (ms_scope == target_fbo && (ms_touches(sx, sy, w, h) || ms_touches(dx, dy, w, h)))
        ms_end();
    record_write(dx, dy, w, h);
    screen_copy_write(sx, sy, dx, dy, w, h, 1);
    side_paint_forget(dx, dy, w, h);
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

/* The internal resolution chosen (settings internal_resolution, the
 * overlay, RPC scale); supersampling (antialias.h) may run pc_hw_scale
 * above it, by the scale it last asked for. */
int pc_hw_scale_setting = 4;
/* internal_resolution auto ("Match window", pc_hw_set_scale(0)): the
 * setting follows the window, one internal pixel per window pixel or finer
 * (up to 8x), at the view size the presenters last gave. */
int pc_hw_scale_auto;
static int supersample_scale;
static int apply_scale(int scale);
/* A detail view (pc_hw_detail_view): one internal pixel per window pixel
 * or finer, at the view size the presenters last gave. */
static int detail_view, last_view_h, last_display_h;
static int window_scale(int view_h, int display_h)
{
    int s = view_h > 0 && display_h > 0 ? (int)ceilf((float)view_h / (float)display_h - 0.01f) : pc_hw_scale_setting;
    return s < 1 ? 1 : s > 8 ? 8 : s;
}
int pc_hw_set_scale(int scale)
{
    if (!pc_hw_active || scale < 0 || scale > 8)
        return pc_hw_scale;
    pc_hw_scale_auto = scale == 0;
    if (pc_hw_scale_auto)
        scale = window_scale(last_view_h, last_display_h);
    pc_hw_scale_setting = scale;
    return apply_scale(supersample_scale > scale ? supersample_scale : scale);
}
/* Supersampling at a view `view_h` window pixels tall showing a display
 * area `display_h` VRAM pixels tall: the scale it needs, or the setting
 * again when it is off (the presenters, before drawing a frame). */
void pc_hw_supersample(int view_h, int display_h)
{
    if (!pc_hw_active)
        return;
    last_view_h = view_h, last_display_h = display_h;
    if (pc_hw_scale_auto && view_h > 0 && display_h > 0)
        pc_hw_scale_setting = window_scale(view_h, display_h); /* "Match window": follows the window */
    supersample_scale = pc_aa_ssaa_scale(view_h, display_h);
    if (detail_view && view_h > 0 && display_h > 0) {
        int detail = (int)ceilf((float)view_h / (float)display_h - 0.01f);
        detail = detail > 8 ? 8 : detail;
        if (detail > supersample_scale)
            supersample_scale = detail;
    }
    int want = supersample_scale > pc_hw_scale_setting ? supersample_scale : pc_hw_scale_setting;
    if (want != pc_hw_scale)
        apply_scale(want);
}
/* A view that magnifies a small 3D subject (the modern menu's Ashley model
 * fills a column of the screen; the field shows him a few dozen lines
 * tall): while on, the internal resolution is at least the window's (up
 * to 8x), so the model is drawn at the view's own resolution instead of
 * being stretched from the setting's (a 4x target on a 2160-line window:
 * 2.25 window pixels per internal pixel, soft texels and edges). The
 * setting (and supersampling) come back when it is off. Applied at once,
 * not at the next present: the frame drawn after the call is drawn at the
 * new scale (a change refills the target from native VRAM). */
void pc_hw_detail_view(int on)
{
    if (!pc_hw_active || detail_view == !!on)
        return;
    detail_view = !!on;
    if (last_view_h > 0 && last_display_h > 0)
        pc_hw_supersample(last_view_h, last_display_h);
}
/* A save state was resumed (snapshot.c): native VRAM is the saved game's.
 * Refill the target from it (what was drawn at the internal resolution
 * shows at native resolution until the game draws it again) and forget
 * what was recorded for the previous frame (interpolation). */
void pc_hw_resync(void)
{
    if (!pc_hw_active)
        return;
    flush();
    ms_end();
    forget_recordings();
    mark_dirty_tiles(0xffffffffu);
    pc_hw_upload(0, 0, 1024, 512);
}
/* Change the internal resolution at runtime: rebuild the targets and refill
 * them from native VRAM (one frame shows native-resolution content). */
static int apply_scale(int scale)
{
    if (!pc_hw_active || scale < 1 || scale > 8 || scale == pc_hw_scale)
        return pc_hw_scale;
    flush();
    overlay_bake_all(); /* (into the old target: the game draws its text again) */
    ms_end();
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        occlusion_on = surface_valid = 0;
        pc_hw_scale = scale;
        pc_vk_hw_resize(scale);
        forget_recordings();
        mark_dirty_tiles(0xffffffffu);
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
    mark_dirty_tiles(0xffffffffu);
    pc_hw_upload(0, 0, 1024, 512);
    fprintf(stderr, "[gpu] internal resolution %dx\n", scale);
    return scale;
}

/* ---- MSAA (antialias.h) ----
 *
 * The 3D is drawn into multisampled twins of the target (colours, G-buffer,
 * depth, and the native surfaces' depth) over one drawing area at a time,
 * "the scope": the first native list drawn in an area loads the target
 * there into every sample; native lists, their translucent markers, and the
 * fills and packets inside the area then draw into the twins (2D with every
 * sample at the pixel centre: exactly as single-sampled); anything else
 * that touches the area (VRAM copies and uploads, readers, the replay copy,
 * a dump, presenting) first resolves it back (pc/shaders/vulkan/
 * ms_resolve.glsl: averaged colour, one representative surface for the
 * lighting). The fragment shaders run once per pixel (no sample shading),
 * so textures stay one texel per pixel; only coverage is multisampled.
 * Native surfaces' depth read as a texture (occlusion, an object list's
 * second pass, translucent markers) stays single-sampled: kept in step
 * (sample 0) whenever its twin changes. */
static int ms_sync_clip[4]; /* the drawing area of the object list being drawn (surface_sync) */
/* OpenGL's twins: textures, framebuffers and programs. */
static GLuint ms_fbo, ms_object_fbo, ms_room_fbo, ms_textures[6], ms_load_program, ms_resolve_program;
static GLint u_ms_load_textures[5], u_ms_resolve_textures[5], u_ms_resolve_samples;
static int ms_gl_samples, ms_gl_width, ms_gl_height;
static const char* ms_vertex_source =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char* ms_load_source = /* pc/shaders/vulkan/ms_load.frag */
    "#version 330 core\n"
    "layout(location = 0) out vec4 color;\n"
    "layout(location = 1) out vec4 normal_out;\n"
    "layout(location = 2) out vec4 albedo_out;\n"
    "layout(location = 3) out vec4 shade_out;\n"
    "uniform sampler2D color_tex, normal_tex, albedo_tex, shade_tex, depth_tex;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    color = texelFetch(color_tex, p, 0);\n"
    "    normal_out = texelFetch(normal_tex, p, 0);\n"
    "    albedo_out = texelFetch(albedo_tex, p, 0);\n"
    "    shade_out = texelFetch(shade_tex, p, 0);\n"
    "    gl_FragDepth = texelFetch(depth_tex, p, 0).r;\n"
    "}\n";
static const char* ms_resolve_source = /* pc/shaders/vulkan/ms_resolve.frag */
    "#version 330 core\n"
    "layout(location = 0) out vec4 color;\n"
    "layout(location = 1) out vec4 normal_out;\n"
    "layout(location = 2) out vec4 albedo_out;\n"
    "layout(location = 3) out vec4 shade_out;\n"
    "uniform sampler2DMS color_ms, normal_ms, albedo_ms, shade_ms, depth_ms;\n"
    "uniform int samples;\n" PC_GLSL_MS_RESOLVE
    "void main() {\n"
    "    float depth;\n"
    "    ms_resolve(ivec2(gl_FragCoord.xy), samples, color, normal_out, albedo_out, shade_out, depth);\n"
    "    gl_FragDepth = depth;\n"
    "}\n";
static int gl_ms_max(void)
{
    static int max = -1;
    if (max < 0) {
        GLint color = 0, depth = 0;
        gl_GetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &color);
        gl_GetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &depth);
        int most = color < depth ? color : depth;
        max = most >= 8 ? 8 : most >= 4 ? 4 : most >= 2 ? 2 : 0;
    }
    return max;
}
int pc_aa_msaa_max(void)
{
    if (!pc_hw_active)
        return 0;
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return pc_vk_hw_ms_max();
#endif
    return gl_ms_max();
}
/* The samples MSAA runs with now: the setting, halved until it runs here. */
static int msaa_samples(void)
{
    int want = pc_aa_msaa, max = pc_aa_msaa_max();
    while (want > max && want > 1)
        want /= 2;
    return want >= 2 ? want : 0;
}
/* RPC graphics msaa_active: the samples the 3D was last drawn with (0: off). */
static int ms_drawn_samples;
int pc_aa_msaa_active(void) { return msaa_samples() ? ms_drawn_samples : 0; }
static int gl_ms_make(int samples, int width, int height)
{
    if (ms_gl_samples == samples && ms_gl_width >= width && ms_gl_height >= height)
        return 1;
    if (!ms_load_program) {
        ms_load_program = pc_gl_program(ms_vertex_source, ms_load_source);
        ms_resolve_program = pc_gl_program(ms_vertex_source, ms_resolve_source);
        if (!ms_load_program || !ms_resolve_program)
            return 0;
        static const char* const loads[5] = { "color_tex", "normal_tex", "albedo_tex", "shade_tex", "depth_tex" };
        static const char* const resolves[5] = { "color_ms", "normal_ms", "albedo_ms", "shade_ms", "depth_ms" };
        for (int i = 0; i < 5; i++) {
            u_ms_load_textures[i] = gl_GetUniformLocation(ms_load_program, loads[i]);
            u_ms_resolve_textures[i] = gl_GetUniformLocation(ms_resolve_program, resolves[i]);
        }
        u_ms_resolve_samples = gl_GetUniformLocation(ms_resolve_program, "samples");
    }
    if (ms_fbo) {
        gl_DeleteFramebuffers(1, &ms_fbo);
        gl_DeleteFramebuffers(1, &ms_object_fbo);
        gl_DeleteFramebuffers(1, &ms_room_fbo);
        gl_DeleteTextures(6, ms_textures);
        ms_fbo = ms_object_fbo = ms_room_fbo = 0;
        ms_gl_samples = 0;
    }
    gl_GenTextures(6, ms_textures);
    for (int i = 0; i < 6; i++) {
        gl_BindTexture(GL_TEXTURE_2D_MULTISAMPLE, ms_textures[i]);
        gl_TexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, i < 4 ? GL_RGBA8 : GL_DEPTH_COMPONENT24, width,
            height, GL_TRUE);
    }
    gl_BindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    GLuint fbos[3];
    gl_GenFramebuffers(3, fbos);
    ms_fbo = fbos[0], ms_object_fbo = fbos[1], ms_room_fbo = fbos[2];
    int ok = 1;
    for (int f = 0; f < 3; f++) {
        gl_BindFramebuffer(GL_FRAMEBUFFER, fbos[f]);
        if (f < 2) {
            for (int i = 0; i < 4; i++)
                gl_FramebufferTexture2D(GL_FRAMEBUFFER, all_buffers[i], GL_TEXTURE_2D_MULTISAMPLE, ms_textures[i], 0);
            gl_DrawBuffers(4, all_buffers);
        } else {
            gl_DrawBuffer(GL_NONE);
            gl_ReadBuffer(GL_NONE);
        }
        gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE,
            ms_textures[f == 0 ? 4 : 5], 0);
        ok = ok && gl_CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        fprintf(stderr, "[gpu] %dx MSAA targets incomplete: MSAA off\n", samples);
        return 0;
    }
    ms_gl_samples = samples, ms_gl_width = width, ms_gl_height = height;
    return 1;
}
/* A fullscreen pass of `program` over scaled rectangle r into `fbo`. */
static void gl_ms_pass(GLuint fbo, GLuint program, const int* r, int colours)
{
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_Viewport(r[0], r[1], r[2] - r[0], r[3] - r[1]);
    gl_Enable(GL_SCISSOR_TEST);
    gl_Scissor(r[0], r[1], r[2] - r[0], r[3] - r[1]);
    gl_Enable(GL_DEPTH_TEST);
    gl_DepthFunc(GL_ALWAYS);
    gl_DepthMask(GL_TRUE);
    gl_Disable(GL_BLEND);
    gl_ColorMask(colours, colours, colours, colours);
    gl_UseProgram(program);
    gl_BindVertexArray(empty_array);
    gl_DrawArrays(GL_TRIANGLES, 0, 3);
    gl_BindVertexArray(0);
    gl_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_Disable(GL_DEPTH_TEST);
    gl_Disable(GL_SCISSOR_TEST);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
static void gl_ms_load(GLuint fbo, const int* r, int room)
{
    int is_replay = fbo == replay.fbo;
    GLuint from[5] = { is_replay ? replay.color : target_texture, is_replay ? replay.normal : normal_texture,
        is_replay ? replay.albedo : albedo_texture, is_replay ? replay.shade : shade_texture,
        is_replay ? replay.depth : depth_texture };
    gl_UseProgram(ms_load_program);
    for (int i = 0; i < 5; i++) {
        gl_ActiveTexture(GL_TEXTURE0 + i);
        gl_BindTexture(GL_TEXTURE_2D, from[i]);
        gl_Uniform1i(u_ms_load_textures[i], i);
    }
    gl_ms_pass(ms_fbo, ms_load_program, r, 1);
    if (room) { /* the native surfaces' depth into its twin (only the depth is written) */
        for (int i = 0; i < 5; i++) {
            gl_ActiveTexture(GL_TEXTURE0 + i);
            gl_BindTexture(GL_TEXTURE_2D, room_depth_texture);
        }
        gl_ms_pass(ms_room_fbo, ms_load_program, r, 0);
    }
    gl_ActiveTexture(GL_TEXTURE0);
}
static void gl_ms_resolve(GLuint fbo, const int* r)
{
    gl_UseProgram(ms_resolve_program);
    for (int i = 0; i < 5; i++) {
        gl_ActiveTexture(GL_TEXTURE0 + i);
        gl_BindTexture(GL_TEXTURE_2D_MULTISAMPLE, ms_textures[i]);
        gl_Uniform1i(u_ms_resolve_textures[i], i);
    }
    gl_Uniform1i(u_ms_resolve_samples, ms_gl_samples);
    gl_ms_pass(fbo, ms_resolve_program, r, 1);
    for (int i = 0; i < 5; i++) {
        gl_ActiveTexture(GL_TEXTURE0 + i);
        gl_BindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    }
    gl_ActiveTexture(GL_TEXTURE0);
}
/* Depth between framebuffers over scaled rectangle r (same sample counts:
 * a copy; multisampled into single: a resolve). */
static void gl_depth_blit(GLuint from, GLuint to, const int* r)
{
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, from);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
    gl_BlitFramebuffer(r[0], r[1], r[2], r[3], r[0], r[1], r[2], r[3], GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
/* The scope's area in scaled pixels. */
static void ms_scaled(const int* r, int* out)
{
    int s = pc_hw_scale;
    out[0] = r[0] * s, out[1] = r[1] * s, out[2] = r[2] * s, out[3] = r[3] * s;
}
/* Resolve the scope back into its target (nothing open: nothing to do). */
static void ms_end(void)
{
    if (!ms_scope)
        return;
    int r[4];
    ms_scaled(ms_rect, r);
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        if (surface_valid) /* the surfaces' twin into the single-sampled surfaces, once */
            pc_vk_hw_ms_sync_room(r[0], r[1], r[2], r[3]);
        pc_vk_hw_ms_resolve(ms_scope, r[0], r[1], r[2], r[3]);
    } else
#endif
        gl_ms_resolve(ms_scope, r);
    ms_scope = 0;
}
/* Whether native rectangle x, y, w, h touches the scope's area. */
static int ms_touches(int x, int y, int w, int h)
{
    return ms_scope && x < ms_rect[2] && ms_rect[0] < x + w && y < ms_rect[3] && ms_rect[1] < y + h;
}
/* Resolve when native rectangle x, y, w, h touches the scope's area. */
static void ms_end_over(int x, int y, int w, int h)
{
    if (ms_touches(x, y, w, h))
        ms_end();
}
/* Draws into `fbo` over drawing area `clip`: whether they go into the
 * twins (the scope is fbo's and holds clip). */
static int ms_holds(GLuint fbo, const int* clip)
{
    return ms_scope && ms_scope == fbo && clip[0] >= ms_rect[0] && clip[1] >= ms_rect[1]
        && clip[0] + clip[2] <= ms_rect[2] && clip[1] + clip[3] <= ms_rect[3];
}
/* 3D for `fbo` over `clip`: open (or keep) the scope there. Returns whether
 * the draw goes into the twins. */
static int ms_begin(GLuint fbo, const int* clip)
{
    int samples = msaa_samples();
    if (!samples) {
        ms_end();
        return 0;
    }
    int x0 = clip[0] < 0 ? 0 : clip[0], y0 = clip[1] < 0 ? 0 : clip[1];
    int x1 = clip[0] + clip[2] > 1024 ? 1024 : clip[0] + clip[2], y1 = clip[1] + clip[3] > 512 ? 512 : clip[1] + clip[3];
    if (x1 <= x0 || y1 <= y0)
        return 0;
    int area[4] = { x0, y0, x1 - x0, y1 - y0 };
    if (ms_holds(fbo, area))
        return 1;
    ms_end();
    /* The twins cover the target from (0, 0) to past the area (64-pixel steps). */
    int s = pc_hw_scale, w = ((x1 + 63) & ~63) * s, h = ((y1 + 63) & ~63) * s;
    w = w > 1024 * s ? 1024 * s : w, h = h > 512 * s ? 512 * s : h;
    /* At most 1 GiB of twins (four colour images, two depths): fewer samples past that. */
    while (samples > 2 && (double)w * h * samples * 24 > 1073741824.0)
        samples /= 2;
    if ((double)w * h * samples * 24 > 1073741824.0)
        return 0;
    int made;
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        made = pc_vk_hw_ms_make(samples, w, h);
    else
#endif
        made = gl_ms_make(samples, w, h);
    if (!made)
        return 0;
    ms_rect[0] = x0, ms_rect[1] = y0, ms_rect[2] = x1, ms_rect[3] = y1;
    int r[4];
    ms_scaled(ms_rect, r);
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        pc_vk_hw_ms_load(fbo, r[0], r[1], r[2], r[3], surface_valid);
    else
#endif
        gl_ms_load(fbo, r, surface_valid);
    ms_scope = fbo;
    ms_drawn_samples = samples;
    return 1;
}
/* The twins' framebuffers (Vulkan tokens or GL names). */
static GLuint ms_target(int object)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return object ? VK_TOKEN_MS_OBJECT : VK_TOKEN_MS;
#endif
    return object ? ms_object_fbo : ms_fbo;
}
/* The native surfaces' depth twin into the single-sampled one (sample 0),
 * over native rectangle clip. */
static void ms_sync_room(const int* clip)
{
    int r[4] = { clip[0], clip[1], clip[0] + clip[2], clip[1] + clip[3] };
    ms_scaled(r, r);
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_ms_sync_room(r[0], r[1], r[2], r[3]);
        return;
    }
#endif
    r[2] = r[2] > ms_gl_width ? ms_gl_width : r[2], r[3] = r[3] > ms_gl_height ? ms_gl_height : r[3];
    if (r[2] > r[0] && r[3] > r[1])
        gl_depth_blit(ms_room_fbo, room_depth_fbo, r);
}
static void ms_surface_sync(void) { ms_sync_room(ms_sync_clip); }

/* ---- native draw lists (render.c) ---- */

static void record_native(uint32_t list, const int* clip, int ox, int oy, unsigned window, int mask);
/* An object list reached before any room list of its ordering table (a
 * world-frozen menu draws its model over 2D only: MENU4's Status, the
 * modern menu's Ashley tab): nothing 3D was drawn before it, so the native
 * surfaces are empty (far) over its drawing area, as a room list would
 * leave them before its first polygon. The model then draws natively (at
 * the internal resolution, anti-aliased, interpolated) instead of as 2D
 * packets. A room list after it in the same table paints over it, as on the
 * PS1. */
static void empty_surfaces(const int* clip)
{
    int s = pc_hw_scale, x0 = clip[0] * s, y0 = clip[1] * s, x1 = (clip[0] + clip[2]) * s,
        y1 = (clip[1] + clip[3]) * s;
    ms_end(); /* MSAA: the next scope loads the cleared surfaces into its twin */
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        pc_vk_hw_clear_depth(room_depth_fbo, x0, y0, x1, y1);
    else
#endif
    {
        static const float far_depth = 1.0f;
        gl_BindFramebuffer(GL_FRAMEBUFFER, room_depth_fbo);
        gl_Enable(GL_SCISSOR_TEST);
        gl_Scissor(x0, y0, x1 - x0, y1 - y0);
        gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
        gl_Disable(GL_SCISSOR_TEST);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    surface_valid = 1;
}
/* Draw list `list` into `fbo` (the live or the replay target), then keep a
 * room list's depth as the native surfaces (occlusion, object lists). An
 * object list tests against those surfaces from earlier in the same
 * ordering table (none yet: empty_surfaces). `t` blends palettes (replays).
 * Returns whether the list drew. */
static int draw_native(GLuint fbo, uint32_t list, const int* clip, int ox, int oy, unsigned window, int mask,
    const render_camera* camera, float t)
{
    int objects = render_list_objects(list); /* 1 objects, 2 another room block (layered) */
    if (objects == 1 && !surface_valid && pc_native_room && pc_native_actors)
        empty_surfaces(clip);
    if (objects < 0 || (objects && !surface_valid) || (objects == 1 && !pc_native_actors)
        || (objects == 2 && !pc_native_objects))
        return 0;
    if (dirty_tiles)
        upload_all_dirty();
    int ms = ms_begin(fbo, clip); /* MSAA: into the multisampled twins */
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
    target.fbo = ms ? ms_target(0) : fbo;
    target.object_fbo = ms ? ms_target(1) : fbo == replay.fbo ? object_replay_fbo : object_fbo;
    target.surface_depth = room_depth_texture;
    target.surface_before = 0;
    target.camera = camera;
    target.t = t;
    /* MSAA: an object list's depth copies cover only where it draws. */
    int area[4];
    memcpy(area, clip, sizeof area);
    if (ms && objects)
        render_list_screen_bounds(list, &target, area);
    /* MSAA: Vulkan's shaders read the surfaces' twins (sample 0); OpenGL's
     * read the single-sampled surfaces, kept in step between an object
     * list's passes. */
    int ms_twins = 0;
    if (ms) {
#ifdef ROOD_VULKAN
        if (pc_vk_on()) {
            ms_twins = 1;
            target.surface_depth = VK_TOKEN_MS_ROOM_DEPTH;
        } else
#endif
        {
            memcpy(ms_sync_clip, area, sizeof ms_sync_clip);
            target.surface_sync = ms_surface_sync;
        }
    }
    if (objects == 1 && pc_native_actor_order) {
        /* The surfaces before this list, for its PS1-ordered instances. */
        int s = pc_hw_scale, x0 = area[0] * s, y0 = area[1] * s, x1 = (area[0] + area[2]) * s,
            y1 = (area[1] + area[3]) * s;
#ifdef ROOD_VULKAN
        if (pc_vk_on())
            pc_vk_hw_copy_depth(ms_twins ? VK_TOKEN_MS_ROOM_DEPTH : room_depth_fbo,
                ms_twins ? VK_TOKEN_MS_BEFORE : before_fbo, x0, y0, x1, y1);
        else
#endif
        {
            gl_BindFramebuffer(GL_READ_FRAMEBUFFER, room_depth_fbo);
            gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, before_fbo);
            gl_BlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        target.surface_before = before_texture;
#ifdef ROOD_VULKAN
        if (ms_twins)
            target.surface_before = VK_TOKEN_MS_BEFORE;
#endif
    }
    target.count_stats = camera == NULL;
    int drawn = render_draw_list(list, &target);
    if (drawn < 0)
        return 0;
    if (objects)
        return 1;
    if (drawn > 0) {
        int s = pc_hw_scale, x0 = clip[0] * s, y0 = clip[1] * s, x1 = (clip[0] + clip[2]) * s,
            y1 = (clip[1] + clip[3]) * s;
        if (ms) { /* the twins' depth into the surfaces' twin (OpenGL: and sample 0 into the surfaces) */
#ifdef ROOD_VULKAN
            if (pc_vk_on())
                pc_vk_hw_copy_depth(VK_TOKEN_MS, VK_TOKEN_MS_ROOM_DEPTH, x0, y0, x1, y1);
            else
#endif
            {
                int r[4] = { x0 < 0 ? 0 : x0, y0 < 0 ? 0 : y0, x1 > ms_gl_width ? ms_gl_width : x1,
                    y1 > ms_gl_height ? ms_gl_height : y1 };
                if (r[2] > r[0] && r[3] > r[1])
                    gl_depth_blit(ms_fbo, ms_room_fbo, r);
                ms_sync_room(clip);
            }
        } else
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
        : PC_GLOW_ORIGIN_OBJECT << 28 | ((mesh * 0x9e3779b1u) & 0x0fffffffu & ~PC_GLOW_EMISSIVE); /* not an emissive texel glow */
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
    overlay_bake_rect(clip[0], clip[1], clip[0] + clip[2], clip[1] + clip[3]);
    if (!draw_native(target_fbo, list, clip, ox, oy, window, mask, NULL, 1.0f))
        return; /* an object list that could not draw: its packets draw */
    record_native(list, clip, ox, oy, window, mask);
    side_paint_forget(clip[0], clip[1], clip[2], clip[3]); /* (the 3D spans the view) */
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
        upload_all_dirty();
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
    target.fbo = ms_begin(fbo, clip) ? ms_target(0) : fbo; /* MSAA: into the twins */
    target.surface_depth = room_depth_texture;
#ifdef ROOD_VULKAN
    if (target.fbo != fbo && pc_vk_on())
        target.surface_depth = VK_TOKEN_MS_ROOM_DEPTH; /* read multisampled (sample 0) */
#endif
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
    if (in_ot && (kind & STALE_MISSING)) {
        int known = 0;
        for (int i = 0; i < ot_stale_count && !known; i++) {
            const int* r = ot_stale[i];
            known = x0 >= r[0] && y0 >= r[1] && x1 <= r[2] && y1 <= r[3];
        }
        if (!known && ot_stale_count < MAX_OT_STALE) {
            int* r = ot_stale[ot_stale_count++];
            r[0] = x0, r[1] = y0, r[2] = x1, r[3] = y1;
        }
    }
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
/* The colour of `fbo` (the live target, or the replay's) over the native
 * rectangle (x0, y0)-(x1, y1), exclusive, scaled down to native pixels
 * (nearest, as GL's blit) into `pixels` (RGBA8 rows of x1 - x0). */
static void read_native_pixels_gl(GLuint fbo, int x0, int y0, int x1, int y1, unsigned char* pixels);
static void read_native_pixels(GLuint fbo, int x0, int y0, int x1, int y1, unsigned char* pixels)
{
    uint64_t cost = pc_cost_begin();
    pc_cost_count(PC_COUNT_READBACKS, 1);
    read_native_pixels_gl(fbo, x0, y0, x1, y1, pixels);
    pc_cost_end(PC_COST_READBACK, cost);
}
static void read_native_pixels_gl(GLuint fbo, int x0, int y0, int x1, int y1, unsigned char* pixels)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_hw_sync_read(fbo == replay.fbo, x0, y0, x1, y1, pixels);
        return;
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
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, sync_fbo);
    gl_BlitFramebuffer(x0 * s, y0 * s, x1 * s, y1 * s, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, sync_fbo);
    gl_PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl_ReadPixels(x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
/* RGBA8 rows (read_native_pixels) into 1024-wide 16-bit VRAM rows: 5 bits per
 * channel as the CPU rasterizer quantizes, the mask bit from alpha. */
static void pixels_to_vram(const unsigned char* pixels, int x0, int y0, int x1, int y1, uint16_t* vram)
{
    for (int row = 0; row < y1 - y0; row++) {
        const unsigned char* src = pixels + (size_t)row * (x1 - x0) * 4;
        uint16_t* dst = vram + (size_t)(y0 + row) * 1024 + x0;
        for (int col = 0; col < x1 - x0; col++, src += 4)
            dst[col] = (uint16_t)((src[0] >> 3) | ((src[1] >> 3) << 5) | ((src[2] >> 3) << 10)
                | (src[3] >= 128 ? 0x8000 : 0));
    }
}
/* The sampled copy of VRAM (native_texture: what draws decode texels from)
 * over the native rectangle (x0, y0)-(x1, y1), exclusive, from the colour of
 * the live target (replay: the replay target) on the GPU: what
 * read_native_pixels, pixels_to_vram and an upload make of it, without the
 * GPU's work so far being waited for. Native VRAM is not changed. 0: not
 * here (ROOD_GPU_VRAM_COPY=0, or Vulkan without R16 storage images): the
 * caller reads back. */
static GLuint vram_copy_fbo, vram_copy_program;
static GLint u_vram_copy_target, u_vram_copy_scale;
static const char* vram_copy_vertex_source =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char* vram_copy_fragment_source = /* (pc/shaders/vulkan/vram_from_target.comp) */
    "#version 330 core\n"
    "out uvec4 vram;\n"
    "uniform sampler2D target;\n"
    "uniform int scale;\n"
    "void main() {\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy);\n"
    "    uvec4 c = uvec4(round(texelFetch(target, at * scale + scale / 2, 0) * 255.0));\n"
    "    vram = uvec4((c.r >> 3) | ((c.g >> 3) << 5) | ((c.b >> 3) << 10) | (c.a >= 128u ? 0x8000u : 0u));\n"
    "}\n";
/* ROOD_RASTER_SKIP=0: the CPU rasterizer draws everything (pc_hw_raster_skip). */
static int raster_skip_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = !getenv("ROOD_RASTER_SKIP") || atoi(getenv("ROOD_RASTER_SKIP")) != 0;
    return enabled;
}
static int vram_copy_enabled = -1;
static int own_copies_enabled(void)
{
    if (vram_copy_enabled < 0)
        vram_copy_enabled = !getenv("ROOD_GPU_VRAM_COPY") || atoi(getenv("ROOD_GPU_VRAM_COPY")) != 0;
    return vram_copy_enabled;
}
static int vram_from_target(int replay_target, int x0, int y0, int x1, int y1)
{
    if (!own_copies_enabled())
        return 0;
    if (x1 <= x0 || y1 <= y0)
        return 1;
    uint64_t cost = pc_cost_begin();
    int done;
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        done = pc_vk_hw_vram_from_target(replay_target, x0, y0, x1, y1);
    else
#endif
    {
        if (!vram_copy_program) {
            vram_copy_program = pc_gl_program(vram_copy_vertex_source, vram_copy_fragment_source);
            if (!vram_copy_program) {
                vram_copy_enabled = 0;
                return 0;
            }
            u_vram_copy_target = gl_GetUniformLocation(vram_copy_program, "target");
            u_vram_copy_scale = gl_GetUniformLocation(vram_copy_program, "scale");
            gl_GenFramebuffers(1, &vram_copy_fbo);
            gl_BindFramebuffer(GL_FRAMEBUFFER, vram_copy_fbo);
            gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, native_texture, 0);
        }
        gl_BindFramebuffer(GL_FRAMEBUFFER, vram_copy_fbo);
        gl_DrawBuffers(1, all_buffers);
        gl_Viewport(0, 0, 1024, 512);
        gl_Enable(GL_SCISSOR_TEST);
        gl_Scissor(x0, y0, x1 - x0, y1 - y0);
        gl_Disable(GL_BLEND);
        gl_Disable(GL_DEPTH_TEST);
        gl_UseProgram(vram_copy_program);
        gl_ActiveTexture(GL_TEXTURE0);
        gl_BindTexture(GL_TEXTURE_2D, replay_target ? replay.color : target_texture);
        gl_Uniform1i(u_vram_copy_target, 0);
        gl_Uniform1i(u_vram_copy_scale, pc_hw_scale);
        gl_BindVertexArray(empty_array);
        gl_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl_BindVertexArray(0);
        gl_Disable(GL_SCISSOR_TEST);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        done = 1;
    }
    pc_cost_end(PC_COST_GPU_COPY, cost);
    pc_cost_count(PC_COUNT_GPU_COPIES, (unsigned)done);
    return done;
}
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
        overlay_bake_rect(x0, y0, x1, y1);
        if (ms_scope == target_fbo)
            ms_end_over(x0, y0, x1 - x0, y1 - y0);
        static unsigned char pixels[1024 * 512 * 4];
        read_native_pixels(target_fbo, x0, y0, x1, y1, pixels);
        pixels_to_vram(pixels, x0, y0, x1, y1, (uint16_t*)(uintptr_t)native_vram);
        mark_dirty(x0, y0, x1 - 1, y1 - 1); /* the sampled copy follows */
        pc_ui_upscale_tiles_changed(tiles_for(x0, y0, x1 - 1, y1 - 1));
        pc_emissive_vram_dirty(tiles_for(x0, y0, x1 - 1, y1 - 1));
        ++pc_hw_syncs;
        /* Native VRAM is current there now: the CPU rasterizer draws there again. */
        for (int k = 0; k < ot_stale_count;) {
            const int* o = ot_stale[k];
            if (o[0] < x1 && x0 < o[2] && o[1] < y1 && y0 < o[3])
                memmove(ot_stale[k], ot_stale[k + 1], (size_t)(--ot_stale_count - k) * sizeof ot_stale[0]);
            else
                ++k;
        }
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
/* Whether what the CPU rasterizer draws inside drawing area `clip` lands
 * where native VRAM is stale anyway: in a drawing area a native draw of
 * this ordering table left missing (read back from the target before
 * anything reads it there; ot_stale). */
static int stale_covers(const int* clip)
{
    int x0 = clip[0] < 0 ? 0 : clip[0], y0 = clip[1] < 0 ? 0 : clip[1];
    int x1 = clip[0] + clip[2] > 1024 ? 1024 : clip[0] + clip[2];
    int y1 = clip[1] + clip[3] > 512 ? 512 : clip[1] + clip[3];
    if (!in_ot)
        return 0;
    if (x1 <= x0 || y1 <= y0)
        return 1; /* draws nothing */
    for (int i = 0; i < ot_stale_count; i++) {
        const int* r = ot_stale[i];
        if (x0 >= r[0] && y0 >= r[1] && x1 <= r[2] && y1 <= r[3])
            return 1;
    }
    return 0;
}
/* boot_runtime.c: the ordering table being drawn holds native draws (they
 * leave its drawing area stale, mark_stale) and starts in drawing area
 * `clip`. The target's picture is what native VRAM lacks there from the
 * table's start: the CPU rasterizer leaves out the 2D drawn there before
 * the native draws too (the backdrop: the whole screen each frame). */
void pc_hw_ot_native_area(const int* clip)
{
    if (pc_hw_active && in_ot && raster_skip_enabled())
        mark_stale(clip);
}
/* The CPU rasterizer is about to draw over (x0, y0)-(x1, y1), inclusive,
 * already clipped to its drawing area: 1 when that is left out (stale_covers:
 * the target's picture replaces it before anything reads it). */
int pc_hw_raster_skip(int x0, int y0, int x1, int y1)
{
    if (!pc_hw_active || !ot_stale_count || !raster_skip_enabled())
        return 0;
    int clip[4] = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
    return stale_covers(clip);
}
/* The stale parts of rectangle (x, y, w, h) become own copies. */
static void own_add(int x, int y, int w, int h)
{
    for (int i = 0; i < stale_count; i++) {
        const int* r = stale_rects[i];
        int x0 = x > r[0] ? x : r[0], y0 = y > r[1] ? y : r[1];
        int x1 = x + w < r[0] + r[2] ? x + w : r[0] + r[2], y1 = y + h < r[1] + r[3] ? y + h : r[1] + r[3];
        if (x1 <= x0 || y1 <= y0)
            continue;
        if (own_count == 4) { /* (more than the texels and the CLUT met: one box) */
            int* b = own_rects[3];
            int a0 = b[0] < x0 ? b[0] : x0, a1 = b[1] < y0 ? b[1] : y0;
            int c0 = b[0] + b[2] > x1 ? b[0] + b[2] : x1, c1 = b[1] + b[3] > y1 ? b[1] + b[3] : y1;
            b[0] = a0, b[1] = a1, b[2] = c0 - a0, b[3] = c1 - a1;
            continue;
        }
        int* b = own_rects[own_count++];
        b[0] = x0, b[1] = y0, b[2] = x1 - x0, b[3] = y1 - y0;
    }
}
/* At the primitive's first triangle: its own copies, from the target as
 * drawn so far (or, where the GPU cannot, read back as before). */
static void own_apply(void)
{
    flush();
    for (int i = 0; i < own_count; i++) {
        const int* r = own_rects[i];
        overlay_bake_rect(r[0], r[1], r[0] + r[2], r[1] + r[3]);
        if (ms_scope == target_fbo)
            ms_end_over(r[0], r[1], r[2], r[3]);
        if (vram_from_target(0, r[0], r[1], r[0] + r[2], r[1] + r[3]))
            bands_for(r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1, own_bands);
        else {
            uint32_t bands[16] = { 0 };
            pc_hw_sync(r[0], r[1], r[2], r[3]);
            bands_for(r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1, bands);
            upload_dirty(bands);
        }
    }
    own_count = 0;
}
/* A primitive drawing in drawing area `clip` (x, y, w, h) is about to
 * sample texels [u0, u1] x [v0, v1] of texture page `tpage` through texture
 * window `window` (and CLUT `clut`). Where they are stale (the target drew
 * there, native VRAM lacks it) they are read back into native VRAM, so the
 * CPU rasterizer and the GPU sample what the window shows. When what the
 * CPU rasterizer draws lands in stale VRAM too (a menu or sphere sampling
 * rows of the picture it draws over), only the GPU needs them: they are
 * copied on the GPU (own copies) and nothing waits for the GPU's frame. */
static int sync_debug = -1;
void pc_hw_sync_texture(unsigned tpage, unsigned clut, unsigned window, int u0, int v0, int u1, int v1, const int* clip)
{
    own_fold();
    own_count = 0;
    if (!stale_count)
        return;
    int tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256, depth = (tpage >> 7) & 3;
    if ((window & 0x3ff) || u0 < 0 || v0 < 0 || u1 > 255 || v1 > 255 || u1 < u0 || v1 < v0)
        u0 = v0 = 0, u1 = v1 = 255; /* windowed or wrapping: the whole page */
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int x0 = tx + (u0 >> shift), x1 = tx + (u1 >> shift);
    if (sync_debug < 0)
        sync_debug = getenv("ROOD_SYNC_DEBUG") != NULL;
    if (clip && own_copies_enabled() && stale_covers(clip)) {
        own_add(x0, ty + v0, (x1 > 1023 ? 1023 : x1) - x0 + 1, v1 - v0 + 1);
        if (depth < 2)
            own_add((clut & 63) * 16, (clut >> 6) & 511, depth ? 256 : 16, 1);
        own_active = own_count > 0;
        if (sync_debug && own_count)
            fprintf(stderr, "[sync] own copy: texture page %x clut %x uv %d,%d..%d,%d into %d,%d %dx%d\n", tpage, clut,
                u0, v0, u1, v1, clip[0], clip[1], clip[2], clip[3]);
        return;
    }
    pc_hw_sync(x0, ty + v0, (x1 > 1023 ? 1023 : x1) - x0 + 1, v1 - v0 + 1);
    if (depth < 2) {
        if (sync_debug)
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
#define WRITE_KEEP 4096 /* VRAM halfwords a write may replace and be undone (op_rewritten) */
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
    /* What each small write replaced (pc_hw_before_write), so a replayed
     * batch whose texels or CLUT it rewrote samples them as it drew. */
    uint8_t write_kept[16];
    uint16_t write_old[16][WRITE_KEEP];
    float r[3][3], t[3], h, ofx, ofy;
    int widescreen, valid;
    float fade, fade_rect[4]; /* a screen fade's amount (0..1; record_fade) and where it covers */
    int menu_model; /* the modern menu's Ashley model view (pc_hw_frame_menu_model) */
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
/* The frame being drawn has a screen fade of this amount (the largest). */
static void record_fade(float amount, const float rect[4])
{
    recording* rec = recording_now();
    if (rec->fade <= 0)
        memcpy(rec->fade_rect, rect, sizeof rec->fade_rect);
    for (int k = 0; k < 2; k++) {
        rec->fade_rect[k] = fminf(rec->fade_rect[k], fmaxf(rect[k], 0.0f));
        rec->fade_rect[k + 2] = fmaxf(rec->fade_rect[k + 2], fminf(rect[k + 2], 1.0f));
    }
    rec->fade = amount > rec->fade ? amount : rec->fade;
}
static float present_fade, present_fade_rect[4]; /* the shown picture's (hw_target) */
/* The modern menu's Ashley model view (ui/ashley_bridge.c): the frame just
 * drawn (its ordering table) is the menu's model alone over a flat 2D tile,
 * the world frozen and not drawn. Tagged per frame, so what a present shows
 * decides: the post stack runs neutral on it (post_profile.cpp) and the
 * presenters put the held picture of the world behind the model (gl_present.c
 * / vk_present.c backdrop) on exactly the presents that show such a frame. */
static int present_menu_model;
void pc_hw_frame_menu_model(int on)
{
    if (pc_hw_active)
        recording_now()->menu_model = on != 0;
}
int pc_hw_present_menu_model(void) { return pc_hw_active && present_menu_model; }
/* The model view's lighting (menu_studio.glsl): 1 (default) its own studio
 * light in place of the room's and of the game's colours for the model (the
 * vertex colours; ashley_bridge.c leaves out the offset copy the game draws
 * as a rim); 0 the game's, lit by the room (RPC graphics menu_studio). */
int pc_hw_menu_studio = 1;
int pc_hw_present_menu_studio(void) { return pc_hw_menu_studio && pc_hw_present_menu_model(); }
/* The shown picture's screen fade (0: none) and where it lands (u0, v0, u1, v1 of the display area). */
float pc_hw_present_fade(float rect[4])
{
    if (rect)
        memcpy(rect, present_fade_rect, sizeof present_fade_rect);
    return present_fade;
}
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
static void record_batch(void) { record_vertices(batch, batch_count, batch_blend, batch_clip, 0); }
/* Held-back text leaves the frame's triangle records (the current batch's,
 * the last ones recorded). */
static void unrecord_triangles(int count)
{
    recording* rec = recording_now();
    rec->triangle_count = rec->triangle_count >= count ? rec->triangle_count - count : 0;
}
/* A batch of `count` vertices drawn now; `triangles`: their 2D triangle
 * records too, before those of the batch being gathered (text held back
 * and drawn later: overlay_draw_picked). */
static void record_vertices(const hw_vertex* batch, int batch_count, int batch_blend, const int* batch_clip,
    int triangles)
{
    recording* rec = recording_now();
    if (rec->vertex_count + batch_count > MAX_RECORDED_VERTICES || rec->op_count >= MAX_RECORDED_OPS
        || (triangles && rec->triangle_count + batch_count / 3 > MAX_RECORDED_VERTICES / 3)) {
        rec->overflow = 1;
        return;
    }
    if (triangles) {
        int at = rec->vertex_count / 3, n = batch_count / 3, ahead = rec->triangle_count - at;
        if (ahead < 0)
            ahead = 0, at = rec->triangle_count;
        memmove(&rec->triangles[at + n], &rec->triangles[at], (size_t)ahead * sizeof *rec->triangles);
        for (int i = 0; i < n; i++) {
            recorded_triangle* t = &rec->triangles[at + i];
            memset(t, 0, sizeof *t);
            t->surface = 0x7e47u; /* (2D: never matched between frames) */
        }
        rec->triangle_count += n;
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
/* The CPU is about to write VRAM rectangle (x, y, w, h) (LoadImage, a VRAM
 * copy; boot_runtime.c): what it holds now, for the write's record. */
static int pending_write[4], pending_kept;
static uint16_t pending_old[WRITE_KEEP];
void pc_hw_before_write(int x, int y, int w, int h)
{
    pending_kept = 0;
    if (!pc_hw_active || !native_vram || w <= 0 || h <= 0 || w * h > WRITE_KEEP || x < 0 || y < 0 || x + w > 1024
        || y + h > 512)
        return;
    for (int row = 0; row < h; row++)
        memcpy(pending_old + row * w, native_vram + (size_t)(y + row) * 1024 + x, (size_t)w * sizeof *pending_old);
    pending_write[0] = x, pending_write[1] = y, pending_write[2] = w, pending_write[3] = h;
    pending_kept = 1;
}
static void record_write(int x, int y, int w, int h)
{
    recording* rec = recording_now();
    int kept = pending_kept && pending_write[0] == x && pending_write[1] == y && pending_write[2] == w
        && pending_write[3] == h;
    pending_kept = 0;
    if (rec->writes < 16) {
        int* q = rec->write_rects[rec->writes];
        q[0] = x, q[1] = y, q[2] = w, q[3] = h, q[4] = rec->op_count;
        rec->write_kept[rec->writes] = (uint8_t)kept;
        if (kept)
            memcpy(rec->write_old[rec->writes], pending_old, (size_t)(w * h) * sizeof *pending_old);
    }
    ++rec->writes;
}
static const recorded_triangle* sort_base;
static int compare_surface(const void* a, const void* b)
{
    const recorded_triangle *p = &sort_base[*(const int*)a], *q = &sort_base[*(const int*)b];
    return p->surface < q->surface ? -1 : p->surface > q->surface;
}
/* The draw of `rec` (with `rooms` 1, its native room list draws only; 2,
 * its native object lists only) with the most area inside rect (x, y, w,
 * h), or NULL. */
static const recorded_op* recording_op(const recording* rec, const int* rect, int rooms, int* overlap)
{
    const recorded_op* best = NULL;
    *overlap = 0;
    for (int o = 0; o < rec->op_count; o++) {
        const recorded_op* op = &rec->ops[o];
        if (rooms && (op->fill != 2 || render_list_objects((uint32_t)op->first) != (rooms == 2)))
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
    /* No room (a world-frozen menu's model over 2D): its object lists'
     * camera, which the replay draws them with; lists drawn with different
     * cameras are not interpolated (shown as drawn). */
    if (!list) {
        render_camera other;
        for (int o = 0; o < rec->op_count; o++) {
            uint32_t id = (uint32_t)rec->ops[o].first;
            if (rec->ops[o].fill != 2 || render_list_objects(id) != 1)
                continue;
            if (!list) {
                list = id;
                if (!render_list_camera(list, &camera))
                    return 0;
            } else if (id != list && (!render_list_camera(id, &other)
                                         || memcmp(&other, &camera, sizeof camera)))
                return 0;
        }
    }
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
    /* No room in either (a world-frozen menu's model over 2D: the modern
     * menu's Ashley tab, MENU4's Status): the object lists, drawn with the
     * camera recording_camera gives the frame and its replay. Without
     * this, live images were lit through the scene's GTE camera and
     * interpolated ones through the replay's (the menu's): the lighting,
     * haze and path tracing reconstructed the model at other places on
     * every other image (the model flickered). */
    if (!room_a && !room_b) {
        const recorded_op* op_b = recording_op(b, shown_rect, 2, &overlap_b);
        const recorded_op* op_a = recording_op(a, shown_rect, 2, &overlap_a);
        room_b = op_b ? (uint32_t)op_b->first : 0;
        room_a = op_a ? (uint32_t)op_a->first : 0;
    }
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
 * moves. 0 when no native room list (nor, without one, object list) drew
 * the display area. */
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
    ms_end(); /* MSAA: the live picture complete, the replay's twins drawn afresh */
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
/* A 2D batch of `b` that samples what `b` drew before it (its texels inside
 * an earlier draw's area): the game copies parts of its own picture (the
 * original Status screen redraws the rows under its information line from
 * the frame buffer each frame). Drawn live, it sampled the target as drawn
 * so far (pc_hw_sync), widescreen's compressed 2D included. */
static int op_feedback(const recording* b, int o)
{
    const recorded_op* op = &b->ops[o];
    const int16_t* r = op->texels;
    if (op->fill || op->has_world || r[2] <= r[0] || r[3] <= r[1])
        return 0;
    for (int k = 0; k < o; k++) {
        const recorded_op* p = &b->ops[k];
        int c[4] = { p->clip[0], p->clip[1], p->clip[0] + p->clip[2], p->clip[1] + p->clip[3] };
        if (!p->fill) /* (a batch: where it drew, inside its drawing area) */
            for (int i = 0; i < 2; i++) {
                c[i] = p->bounds[i] > c[i] ? p->bounds[i] : c[i];
                c[i + 2] = p->bounds[i + 2] < c[i + 2] ? p->bounds[i + 2] : c[i + 2];
            }
        if (c[0] < r[2] && r[0] < c[2] && c[1] < r[3] && r[1] < c[3])
            return 1;
    }
    return 0;
}
/* For a replayed op_feedback batch: the sampled copy of VRAM over its texels
 * becomes the replay target as replayed so far (what the live draw sampled,
 * with this replay's moved 3D), as pc_hw_sync makes it for the live one.
 * Redrawn from native VRAM instead (the CPU's 4:3 picture: 2D not
 * compressed; or a newer frame's drawing), it drew misplaced copies of the
 * text at 16:9. Returns the tiles to upload from native VRAM again after
 * the draw. */
/* The replay's own copy of VRAM, for batches that sample VRAM other than it is now. */
static uint16_t replay_vram[1024 * 512];
static void replay_vram_from_native(uint32_t tiles)
{
    for (int tile = 0; tile < 32; tile++)
        if (tiles & (1u << tile)) {
            int x = (tile & 15) * 64, y = (tile >> 4) * 256;
            for (int row = 0; row < 256; row++)
                memcpy(replay_vram + (y + row) * 1024 + x, native_vram + (y + row) * 1024 + x,
                    64 * sizeof *replay_vram);
        }
}
static uint32_t replay_sample_own(const int16_t* texels, uint32_t restore[16])
{
    int x0 = texels[0] < 0 ? 0 : texels[0], y0 = texels[1] < 0 ? 0 : texels[1];
    int x1 = texels[2] > 1024 ? 1024 : texels[2], y1 = texels[3] > 512 ? 512 : texels[3];
    if (x1 <= x0 || y1 <= y0)
        return 0;
    if (ms_scope == replay.fbo)
        ms_end_over(x0, y0, x1 - x0, y1 - y0);
    uint32_t tiles = tiles_for(x0, y0, x1 - 1, y1 - 1);
    /* On the GPU: the rest of those tiles as native VRAM is now (as the
     * read-back's upload below makes them), then the replay's picture. */
    {
        uint32_t rect[16] = { 0 };
        bands_for(x0, y0, x1 - 1, y1 - 1, rect);
        upload_dirty(rect);
    }
    if (vram_from_target(1, x0, y0, x1, y1)) {
        bands_for(x0, y0, x1 - 1, y1 - 1, restore); /* native VRAM there again after the draw */
        return 0;
    }
    static unsigned char pixels[1024 * 512 * 4];
    read_native_pixels(replay.fbo, x0, y0, x1, y1, pixels);
    replay_vram_from_native(tiles);
    pixels_to_vram(pixels, x0, y0, x1, y1, replay_vram);
    upload_tiles_from(replay_vram, tiles);
    return tiles;
}
/* For a replayed batch whose texels or CLUT were rewritten since it drew
 * (op_rewritten: the memory card screens swap the CLUT row their pictures
 * share in for each save's thumbnail and back): the sampled copy of VRAM as
 * it drew, native VRAM with those writes undone, newest first (`later`'s,
 * the frame being recorded, then `b`'s after the batch). Returns the tiles
 * to upload from native VRAM again after the draw; 0 when a write was not
 * kept (too large, or more than 16 in a frame): the caller then keeps B's
 * pixels there. */
static uint32_t replay_unwrite(const recording* b, int o, const recording* later)
{
    const recorded_op* op = &b->ops[o];
    uint32_t tiles = 0;
    for (int pass = 0; pass < 2; pass++) {
        const recording* rec = pass ? b : later;
        if (!rec)
            continue;
        if (rec->writes > 16)
            return 0;
        for (int i = rec->writes - 1; i >= 0; i--) {
            const int* w = rec->write_rects[i];
            if (rec == b && w[4] <= o)
                break; /* written before it drew */
            if (!write_hits(w, op->texels) && !write_hits(w, op->cluts))
                continue;
            if (!rec->write_kept[i])
                return 0;
            tiles |= tiles_for(w[0], w[1], w[0] + w[2] - 1, w[1] + w[3] - 1);
        }
    }
    if (!tiles)
        return 0;
    replay_vram_from_native(tiles);
    for (int pass = 0; pass < 2; pass++) {
        const recording* rec = pass ? b : later;
        if (!rec)
            continue;
        for (int i = rec->writes - 1; i >= 0; i--) {
            const int* w = rec->write_rects[i];
            if (rec == b && w[4] <= o)
                break;
            if (!write_hits(w, op->texels) && !write_hits(w, op->cluts))
                continue;
            for (int row = 0; row < w[3]; row++)
                memcpy(replay_vram + (size_t)(w[1] + row) * 1024 + w[0], rec->write_old[i] + row * w[2],
                    (size_t)w[2] * sizeof *replay_vram);
        }
    }
    upload_tiles_from(replay_vram, tiles);
    return tiles;
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
        uint32_t unwritten = 0;
        if (op_rewritten(b, o, recording_now()) && !(unwritten = replay_unwrite(b, o, recording_now()))) {
            /* Its texels changed since and cannot be undone: B's own pixels
             * where it drew (with everything B drew over them later: the
             * later batches draw there again, translucent ones twice). */
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
        uint32_t restore[16] = { 0 };
        uint32_t own = op_feedback(b, o) ? replay_sample_own(op->texels, restore) : 0;
        draw_batch(replay.fbo, scratch, op->count, op->blend, op->clip);
        if (own | unwritten)
            upload_tiles(own | unwritten); /* (native VRAM again) */
        upload_bands(restore);
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
static int redraw_text = 1; /* pc_hw_redraw draws the held-back text into the picture */
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
    if (overlay_count && redraw_text) { /* text held back for the window: drawn into the picture (as it would be) */
        memset(overlay_pick, 1, (size_t)overlay_count);
        overlay_draw_picked(replay.fbo, 0);
    }
    ms_end();
    force_replay = 1; /* the next interpolated image is drawn afresh */
    return 0;
}
/* Developer RPC (redraw {present: true}): pc_hw_redraw, then present that
 * image (B's drawing area of the replay target) until the game draws a new
 * frame: a paused game shows the same frame under each drawing option
 * (anti-aliasing comparisons, aa_smoke.py). */
static int redraw_shown, redraw_origin[2];
int pc_hw_redraw_present(void)
{
    int area[4];
    redraw_text = 0; /* (presented: the overlay draws it, at the window's resolution) */
    int failed = pc_hw_redraw(area);
    redraw_text = 1;
    if (failed)
        return -1;
    redraw_shown = 1;
    redraw_origin[0] = area[0], redraw_origin[1] = area[1];
    return 0;
}
/* ---- the text overlay, presented (gl_present.c, vk_present.c) ---- */
static const char* overlay_vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in uvec4 a_color;\n"
    "layout(location = 2) in uvec2 a_uv;\n"
    "layout(location = 3) in uvec2 a_page;\n"
    "layout(location = 4) in uint a_window;\n"
    "layout(location = 6) in uvec4 a_bounds;\n"
    "layout(location = 7) in ivec2 a_ui;\n"
    "uniform vec4 rect;\n" /* the presented area: x, y, width, height (VRAM pixels) */
    "out vec3 v_color;\n"
    "out vec2 v_uv;\n"
    "flat out uvec4 v_state;\n"
    "flat out uvec4 v_bounds;\n"
    "flat out ivec2 v_ui;\n"
    "void main() {\n"
    "    v_color = vec3(a_color.rgb) / 255.0;\n"
    "    v_uv = vec2(a_uv);\n"
    "    v_state = uvec4(a_color.a, a_page, a_window);\n"
    "    v_bounds = a_bounds;\n"
    "    v_ui = a_ui;\n"
    "    vec2 p = (a_pos.xy - rect.xy) / rect.zw;\n"
    "    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);\n" /* the area's top row at the top */
    "}\n";
static const char* overlay_fragment_source =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "in vec2 v_uv;\n"
    "flat in uvec4 v_state;\n"
    "flat in uvec4 v_bounds;\n"
    "flat in ivec2 v_ui;\n"
    "uniform usampler2D vram;\n"
    "uniform usampler2D ui_atlas;\n"
    "uniform int ui_scale;\n"
    "uniform int pass;\n" /* 4: opaque texels "over", 5: translucent texels by the PS1 mode */
    "uniform float text_factor;\n"
    "out vec4 color;\n"
    "uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "vec3 rgb(uint t) { return vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)); }\n"
    PC_GLSL_TEXT_DIST
    "void main() {\n"
    "    vec2 footprint = fwidth(v_uv);\n"
    "    uint flags = v_state.x, clut = v_state.z;\n"
    "    ivec2 lo = ivec2(v_bounds.xy), hi = max(ivec2(v_bounds.zw) - 1, lo);\n"
    "    int s = ui_scale;\n"
    "    ivec2 o = v_ui * s;\n"
    "    ivec2 q = clamp(ivec2(floor(v_uv * float(s) + 0.0001)), lo * s, (hi + 1) * s - 1);\n"
    "    uint t = texelFetch(ui_atlas, q + o, 0).r;\n"
    "    if ((t & 0x80000000u) == 0u) discard;\n"
    "    float d = text_distance(ui_atlas, v_uv * float(s) + vec2(o), lo * s + o, (hi + 1) * s - 1 + o);\n"
    "    vec3 texel;\n"
    "    float alpha = text_colour(t, d, max(max(footprint.x, footprint.y), 1.0 / 256.0), clut, texel, t);\n"
    "    if (alpha < 0.002 || t == 0u) discard;\n"
    "    bool semi = (flags & 4u) != 0u && (t & 0x8000u) != 0u;\n"
    "    if ((pass == 4 && semi) || (pass == 5 && !semi)) discard;\n"
    "    vec3 c = (flags & 2u) != 0u ? texel / 31.0 : min(texel * v_color * (255.0 / 128.0), 31.0) / 31.0;\n"
    "    color = vec4(c, alpha * (pass == 5 ? text_factor : 1.0));\n"
    "}\n";
static GLuint overlay_program;
static GLint u_overlay_rect, u_overlay_scale, u_overlay_pass, u_overlay_factor;
/* One run of held-back triangles of one blend, over the presented picture
 * (GL: the bound framebuffer and viewport; clip: the drawing area, window
 * pixels from the view's top-left). */
static void overlay_present_gl(const hw_vertex* v, int count, int blend, const float* rect, const float* clip)
{
    if (!overlay_program) {
        static int failed;
        if (failed)
            return;
        overlay_program = pc_gl_program(overlay_vertex_source, overlay_fragment_source);
        if (!overlay_program) {
            failed = 1;
            fprintf(stderr, "[gl] text overlay unavailable (its shader did not build)\n");
            return;
        }
        u_overlay_rect = gl_GetUniformLocation(overlay_program, "rect");
        u_overlay_scale = gl_GetUniformLocation(overlay_program, "ui_scale");
        u_overlay_pass = gl_GetUniformLocation(overlay_program, "pass");
        u_overlay_factor = gl_GetUniformLocation(overlay_program, "text_factor");
        gl_UseProgram(overlay_program);
        gl_Uniform1i(gl_GetUniformLocation(overlay_program, "vram"), 0);
        gl_Uniform1i(gl_GetUniformLocation(overlay_program, "ui_atlas"), 2);
    }
    GLint view[4];
    gl_GetIntegerv(GL_VIEWPORT, view);
    gl_UseProgram(overlay_program);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, native_texture);
    gl_ActiveTexture(GL_TEXTURE2);
    gl_BindTexture(GL_TEXTURE_2D, pc_ui_upscale_gl_texture());
    gl_ActiveTexture(GL_TEXTURE0);
    gl_Uniform4f(u_overlay_rect, rect[0], rect[1], rect[2], rect[3]);
    gl_Uniform1i(u_overlay_scale, pc_ui_upscale_draw_factor());
    gl_BindVertexArray(vertex_array);
    gl_BindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(hw_vertex)), v, GL_STREAM_DRAW);
    gl_Disable(GL_DEPTH_TEST);
    gl_Enable(GL_SCISSOR_TEST);
    int x0 = (int)floorf(clip[0]), x1 = (int)ceilf(clip[2]), y0 = (int)floorf(clip[1]), y1 = (int)ceilf(clip[3]);
    gl_Scissor(view[0] + x0, view[1] + view[3] - y1, x1 - x0, y1 - y0); /* (GL: rows from the bottom) */
    gl_Enable(GL_BLEND);
    gl_BlendEquation(GL_FUNC_ADD);
    gl_BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    gl_Uniform1i(u_overlay_pass, 4);
    gl_DrawArrays(GL_TRIANGLES, 0, count);
    int mode = blend - BLEND_TEXT;
    if (mode < 4) {
        gl_BlendEquation(mode == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        gl_BlendFuncSeparate(GL_SRC_ALPHA, mode == 0 ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE, GL_ZERO, GL_ONE);
        gl_Uniform1f(u_overlay_factor, mode == 0 ? 0.5f : mode == 3 ? 0.25f : 1.0f);
        gl_Uniform1i(u_overlay_pass, 5);
        gl_DrawArrays(GL_TRIANGLES, 0, count);
        gl_BlendEquation(GL_FUNC_ADD);
    }
    gl_Disable(GL_BLEND);
    gl_Disable(GL_SCISSOR_TEST);
    gl_BindVertexArray(0);
    gl_UseProgram(0);
}

/* The held-back text inside the presented area, at the window's resolution
 * (the presenters, before the brightness curve: into the bound framebuffer
 * and viewport, GL; into the open rendering, view (view_x, view_y) view_w x
 * view_h of the window image, or of the brightness input with to_view,
 * Vulkan). */
void pc_hw_text_overlay(int view_x, int view_y, int view_w, int view_h, int to_view)
{
    if (!pc_hw_active || !overlay_count || view_w <= 0 || view_h <= 0 || shown_rect[2] <= 0 || shown_rect[3] <= 0)
        return;
    const int* r = shown_rect;
    float rect[4] = { (float)r[0], (float)r[1], (float)r[2], (float)r[3] };
    uint32_t drop = pc_ui_text == PC_UI_TEXT_FONT ? (pc_ui_text_layout == PC_UI_TEXT_LAYOUT_LINE ? TEXT_TAG_GLYPH | TEXT_TAG_CELL
                                                                                                 : TEXT_TAG_GLYPH | TEXT_TAG_LINE)
                                                  : TEXT_TAG_CELL | TEXT_TAG_LINE;
    static hw_vertex v[MAX_VERTICES];
    int n = 0, blend = 0, clip[4] = { 0, 0, 0, 0 };
    for (int i = 0; i <= overlay_count; i++) {
        const overlay_triangle* o = i < overlay_count ? &overlay[i] : NULL;
        if (o && (o->box[2] <= r[0] || o->box[0] >= r[0] + r[2] || o->box[3] <= r[1] || o->box[1] >= r[1] + r[3]
                     || (o->v[0].window & drop)))
            continue;
        if (n && (!o || o->blend != blend || memcmp(o->clip, clip, sizeof clip) || n + 3 > MAX_VERTICES)) {
            /* The drawing area in window pixels from the view's top-left. */
            float sx = view_w / rect[2], sy = view_h / rect[3];
            float area[4] = { (clip[0] - rect[0]) * sx, (clip[1] - rect[1]) * sy, (clip[0] + clip[2] - rect[0]) * sx,
                (clip[1] + clip[3] - rect[1]) * sy };
            area[0] = fmaxf(area[0], 0), area[1] = fmaxf(area[1], 0);
            area[2] = fminf(area[2], (float)view_w), area[3] = fminf(area[3], (float)view_h);
            if (area[2] > area[0] && area[3] > area[1]) {
#ifdef ROOD_VULKAN
                if (pc_vk_on())
                    pc_vk_hw_text_overlay(v, n, blend - BLEND_TEXT, rect, area, view_x, view_y, to_view);
                else
#endif
                    overlay_present_gl(v, n, blend, rect, area);
                overlay_presented += n / 3;
            }
            n = 0;
        }
        if (!o)
            break;
        blend = o->blend;
        memcpy(clip, o->clip, sizeof clip);
        memcpy(v + n, o->v, sizeof o->v);
        n += 3;
    }
    (void)view_x, (void)view_y, (void)to_view;
}
int pc_hw_text_overlay_held(void) { return overlay_on ? overlay_count : -1; }
void pc_hw_text_overlay_stats(long* held, long* baked, long* presented)
{
    *held = overlay_on ? overlay_count : -1, *baked = overlay_baked, *presented = overlay_presented;
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
/* Game frames finished so far: the frame a presented picture shows (film
 * grain's seed, post.h: the same on interpolated presents and redraws). */
static unsigned frame_serial;
unsigned pc_hw_frame_serial(void) { return frame_serial; }
/* The vsync's end: what was drawn since the last one is a finished frame. */
static void close_frame(unsigned vsync)
{
    recording* now = recording_now();
    if (now->op_count || now->triangle_count) {
        packet_stats.stale_rects = stale_count;
        packet_stats.vram_syncs = pc_hw_syncs;
        render_frame_end(&packet_stats);
        report_other();
        memset(&packet_stats, 0, sizeof packet_stats);
        finish_recording(now);
        ++frame_serial;
        pc_hd_frame();
        previous = newest;
        newest = recording_index;
        arrival[newest] = vsync;
        recording_index = 3 - previous - newest;
        recording* next = recording_now();
        next->vertex_count = next->triangle_count = next->op_count = next->overflow = next->writes = 0;
        next->fade = 0;
        next->menu_model = 0;
        next->valid = 0;
        redraw_shown = 0; /* a new frame: the redrawn one is history */
    }
}
/* A vsync the window does not present (charge_skip.c): its frame ends as at
 * a present, so the next one records on its own. */
void pc_hw_skip_present(void)
{
    if (!pc_hw_active)
        return;
    flush();
    ms_end(); /* MSAA: the frame complete in the target */
    close_frame(pc_vsync_count());
}
static GLuint hw_target(void)
{
    overlay_update(); /* (for this view: whether the next frame's text is held back) */
    flush();
    ms_end(); /* MSAA: the frame complete in the target */
    pc_ui_upscale_frame(); /* a presented image: the 2D cache's clock */
    presenting_replay = 0;
    memcpy(shown_rect, display_rect, sizeof shown_rect);
    unsigned vsync = pc_vsync_count();
    close_frame(vsync);
    static unsigned cut_seen;
    int cut = cut_seen != pc_hw_history_cut; /* the first present after a skip */
    cut_seen = pc_hw_history_cut;
    present_fade = recordings[newest].fade; /* the newest frame's screen fade (interpolated below) */
    memcpy(present_fade_rect, recordings[newest].fade_rect, sizeof present_fade_rect);
    present_menu_model = recordings[newest].menu_model;
    if (redraw_shown) { /* pc_hw_redraw_present */
        shown_rect[0] = redraw_origin[0], shown_rect[1] = redraw_origin[1];
        presenting_replay = 1;
        present_blend = 1.0f;
        return replay.color;
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
    if (!pc_hw_interpolate || !matching || interval < shortest || interval > 8 || cut)
        return target_texture; /* 60 fps, a cut, stale, or after a skip: the newest frame as drawn */
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
        ms_end();
        drawn_slot = newest, drawn_vsync = vsync, drawn_arrival = arrival[newest], drawn_phase = pc_present_phase;
    }
    presenting_replay = 1;
    present_blend = t;
    present_fade = recordings[previous].fade + (recordings[newest].fade - recordings[previous].fade) * t;
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
/* Every native list of the frame being presented (room, other room blocks,
 * objects; not effects): the editor's texture picking (emissive_editor.cpp). */
int pc_hw_present_all_lists(uint32_t* lists, int max)
{
    int replayed;
    uint32_t room;
    const recording* b = shown_recording(&replayed, &room);
    if (!b)
        b = &recordings[newest];
    int n = 0;
    for (int o = 0; o < b->op_count && n < max; o++) {
        uint32_t id = (uint32_t)b->ops[o].first;
        if (b->ops[o].fill != 2 || render_list_objects(id) < 0 || render_list_effects(id) != 0)
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
        rec->valid = rec->menu_model = 0;
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
    if (!replay_target)
        overlay_bake_all(); /* (the target as the game drew it) */
    ms_end();
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
    for (size_t i = 0; i < (size_t)w * h; i++) /* RGBA to RGB in place, written at once */
        memmove(pixels + i * 3, pixels + i * 4, 3);
    fwrite(pixels, 3, (size_t)w * h, f);
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
    ms_end();
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
        upload_all_dirty();
    return native_texture;
}
