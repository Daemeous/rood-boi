/* The draw walk's commands to the hardware renderer (hw_cmd.h). Host state: nothing here is game state. */
#include "hw_cmd.h"
#include "renderer_rq.h"
#include "hw_registry.h"
#include "render_thread.h"
#include "ui_text.h"
#include "render.h"
#include "dev_state.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* The renderer's hooks, applied by the consumer (gpu_hw.c). */
extern void pc_hw_set_2d(int);
extern void pc_hw_set_narrowed(void) __attribute__((weak));
extern void pc_hw_set_precise(const float*);
extern void pc_hw_set_depth(const int*);
extern void pc_hw_set_packet(const void*);
extern void pc_hw_set_flipped_uv(const int*, const int*);
extern void pc_hw_set_bounds(int, int, int, int, unsigned, unsigned, unsigned, const int*, const int*);
extern void pc_hw_set_attr(const pc_hw_attr*, int, int);
extern void pc_hw_set_widescreen(int);
extern void pc_hw_set_settings(const int*);
extern void pc_hw_set_exact(const float*, int);
extern void pc_hw_clear_exact(void);
extern void pc_hw_skip_present_now(unsigned);
extern unsigned pc_ph_vsync(void) __attribute__((weak));
extern unsigned pc_vsync_count(void);
extern int pc_hw_perspective, pc_hw_texture_filter, pc_hw_subpixel, pc_hw_interpolate, pc_hw_menu_studio;
extern void pc_hw_frame_menu_model(int);
extern void pc_hw_set_suppress(int);
extern void pc_hw_set_call(unsigned, float);
extern int pc_gte_widescreen;
extern __thread int pc_present_inside;
extern void pc_hw_native_marker(unsigned, int, int, const int*, unsigned, int) __attribute__((weak));
extern void pc_hw_native_translucent(unsigned, int, int, int, int, const int*, unsigned, int) __attribute__((weak));
extern void pc_hw_ot_native_area(const int*) __attribute__((weak));
extern void pc_hw_rect(int, int, int, int, int, int, unsigned, unsigned, unsigned, unsigned, unsigned, const int*, int, int);
extern void pc_hw_line(int, int, int, int, unsigned, unsigned, int, unsigned, const int*, int);
extern void pc_hw_mark_raster(int, int, int, int);
extern void pc_hw_mark(int, int, int, int);
extern void pc_hw_before_write(int, int, int, int) __attribute__((weak));
extern void pc_hw_stale_copy(int, int, int, int, int, int) __attribute__((weak));
extern int pc_hw_text_quad_decide(int, const float*, const int*, unsigned, unsigned, unsigned, const int*, pc_ui_text_piece_info*);
extern void pc_hw_text_piece_apply(const pc_ui_text_piece_info*, unsigned, unsigned, unsigned, int, int, const void*);
extern void pc_hw_text_piece_end(void);
extern void pc_emissive_note_instance(uint32_t, const void*, int, int, unsigned, unsigned);
extern void pc_emissive_mesh_gone(uint32_t);
extern void pc_emissive_room_mesh(uint32_t, unsigned, unsigned, unsigned);
extern void pc_emissive_frame_room(int valid, int zone, int room);
extern void pc_hw_fill(int, int, int, int, int, int, int, const uint16_t*);
extern void pc_hw_upload(int, int, int, int, const uint16_t*);
extern void pc_hw_copy(int, int, int, int, int, int, const uint16_t*);
extern void pc_hw_upload_mode(int);
extern void pc_hw_sync_texture(unsigned, unsigned, unsigned, int, int, int, int, const int*);
extern void pc_stream_apply(int op, unsigned tag, const int* v, int n); /* boot_runtime.c: the digest's side of a record */
extern void pc_stream_apply_words(const uint32_t* p, unsigned n);
extern void pc_hw_begin_ot(void);
extern void pc_hw_end_ot(void);
extern int pc_hw_raster_skip_enabled(void);
extern int pc_hw_active;
extern const uint16_t* pc_native_vram(void);
extern int pc_gpu_immediate; /* DrawPrim, not an ordering table: ui_text identifies the info signs by it */
extern const void* pc_gpu_command; /* the packet a rectangle or text quad draws (boot_runtime.c) */

enum { C_SET_2D = 1, C_SET_NARROWED, C_SET_PRECISE, C_SET_DEPTH, C_SET_PACKET, C_SET_FLIPPED_UV, C_SET_BOUNDS, C_SET_ATTR, C_NATIVE_MARKER, C_NATIVE_TRANSLUCENT, C_OT_NATIVE_AREA, C_RECT, C_LINE, C_TRIANGLE, C_MARK_RASTER, C_MARK, C_RASTER_POST, C_BEFORE_WRITE, C_STALE_COPY, C_TEXT_PIECE, C_TEXT_PIECE_DONE, C_EMISSIVE_NOTE, C_EMISSIVE_GONE, C_EMISSIVE_ROOM_MESH, C_FRAME_ROOM, C_VRAM_FILL, C_VRAM_UPLOAD, C_VRAM_COPY, C_SET_WIDESCREEN, C_UPLOAD_MODE, C_BEGIN_OT, C_END_OT, C_SET_SUPPRESS, C_TEXTURE_FENCE, C_STREAM, C_STREAM_WORDS, C_FRAME_MENU_MODEL, C_SET_SETTINGS, C_SET_EXACT, C_CLEAR_EXACT, C_SKIP_PRESENT };

struct pc_hwc_mirror_t pc_hwc_mirror = { .cur_effect = -1, .poly_effect = -1, .settings = { 1, 0, 1, 1, 1 } }; /* (the settings: the renderer's defaults, gpu_hw.c) */
int pc_hwc_pending;
int pc_hwc_stale_pending;
/* ROOD_HWC_INLINE=1: every record is applied at once, as the walk called the hooks before the buffer (the proof's reference:
 * the same binary, identical frames expected with and without). */
static int inline_mode = -1;
static int inline_now(void)
{
    if (inline_mode < 0)
        inline_mode = getenv("ROOD_HWC_INLINE") != NULL;
    return inline_mode;
}
/* A batch: the records of a stretch of the walk and the pixel payload of its raster posts (a pool the apply empties, and a block of its own
 * for a post too big for it). The producer fills the current batch; a flush hands it over (applied in place when there is no renderer
 * thread, posted to it otherwise, and the producer goes on in a free one). */
#define POOL_PIXELS (1 << 21)
typedef struct {
    unsigned char buffer[1 << 18]; /* a walk's records: about 40 bytes each, a few thousand a frame at most */
    int pending;                   /* bytes recorded (while the batch is current, pc_hwc_pending is the count) */
    uint16_t pool[POOL_PIXELS];
    size_t pool_used;
    int busy;                      /* posted and not yet applied */
} batch;
enum { BATCHES = 3 };
static batch batches[BATCHES];
static batch* current = &batches[0];
#define ROOM_FOR(n) ((int)sizeof current->buffer - pc_hwc_pending >= (n) + 1)
static void hwc_post(void);

static unsigned char* record_raw(int type, int size)
{
    if (!ROOM_FOR(size))
        hwc_post();
    unsigned char* at = current->buffer + pc_hwc_pending;
    at[0] = (unsigned char)type;
    pc_hwc_pending += 1 + size;
    return at + 1;
}
/* The widescreen flag (the 16:9 Hor+ layout the renderer's drawing reads) travels in the stream: a record when it changed, ahead of the
 * record that follows (and at each present's header fill), so the renderer never reads the game's variable. */
void pc_hwc_sync_widescreen(void)
{
    if (pc_gte_widescreen == pc_hwc_mirror.widescreen || pc_present_inside || pc_rt_on_thread())
        return; /* (inside a present the game's variable is not the stream's to read: the canary poisons it) */
    pc_hwc_mirror.widescreen = pc_gte_widescreen;
    memcpy(record_raw(C_SET_WIDESCREEN, sizeof(int)), &pc_hwc_mirror.widescreen, sizeof(int));
}
/* The suppress flag (the game side leaves this packet out of the hardware picture: ui_bubbles) travels in the stream like the widescreen flag: a record
 * when it changed, ahead of the record that follows, so the renderer never reads the game's variable. */
void pc_hwc_sync_suppress(void)
{
    if (pc_hw_suppress == pc_hwc_mirror.suppress || pc_present_inside || pc_rt_on_thread())
        return;
    pc_hwc_mirror.suppress = pc_hw_suppress;
    memcpy(record_raw(C_SET_SUPPRESS, sizeof(int)), &pc_hwc_mirror.suppress, sizeof(int));
}
/* The draw settings the renderer reads (the settings perspective_textures, smooth_textures, subpixel, interpolate and the menu studio) travel in the
 * stream like the widescreen flag: a record when one changed, ahead of the record that follows (and at each header fill), so the renderer never
 * reads the game's variables (the settings code and the overlay write them on the game thread). */
void pc_hwc_sync_settings(void)
{
    int now[5] = { pc_hw_perspective, pc_hw_texture_filter, pc_hw_subpixel, pc_hw_interpolate, pc_hw_menu_studio };
    if (!memcmp(now, pc_hwc_mirror.settings, sizeof now) || pc_present_inside || pc_rt_on_thread())
        return;
    memcpy(pc_hwc_mirror.settings, now, sizeof now);
    memcpy(record_raw(C_SET_SETTINGS, sizeof now), now, sizeof now);
}
static unsigned char* record(int type, int size)
{
    pc_hwc_sync_settings();
    pc_hwc_sync_suppress();
    pc_hwc_sync_widescreen();
    return record_raw(type, size);
}

/* The frame being drawn holds the modern menu's model view (ui/ashley_bridge.c): the renderer tags its recording with it. */
void pc_hwc_frame_menu_model(int on)
{
    memcpy(record(C_FRAME_MENU_MODEL, sizeof on), &on, sizeof on);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_set_2d(int is_2d)
{
    pc_hwc_mirror.is_2d = is_2d;
    memcpy(record(C_SET_2D, sizeof is_2d), &is_2d, sizeof is_2d);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_set_narrowed(void)
{
    record(C_SET_NARROWED, 0);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_set_precise(const float* xy6)
{
    memcpy(record(C_SET_PRECISE, 6 * sizeof(float)), xy6, 6 * sizeof(float));
    if (inline_now())
        pc_hwc_flush();
}
/* A 2D triangle the port builds at unrounded positions (ui_bubbles): the exact positions and the coverage flag, the triangle, then the positions
 * cleared, as records (the decision of the triangle is the producer's). */
void pc_hwc_shape_triangle(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip, int mask_set, int cover)
{
    int ixy[6];
    for (int i = 0; i < 6; i++)
        ixy[i] = (int)floorf(xy[i] + 0.5f);
    pc_hwc_set_packet(NULL);
    struct {
        float xy[6];
        int cover;
    } e;
    memcpy(e.xy, xy, sizeof e.xy), e.cover = cover;
    memcpy(record(C_SET_EXACT, sizeof e), &e, sizeof e);
    pc_hwc_triangle(ixy, uv, rgb, code, tpage, clut, window, clip, mask_set, 0);
    record(C_CLEAR_EXACT, 0);
    if (inline_now())
        pc_hwc_flush();
}
/* A frame ends without being presented (photo rendering, the hidden charge skip): the batch is closed as at a present, at the vsync the game is at. */
void pc_hwc_skip_present(void)
{
    unsigned vsync = pc_ph_vsync ? pc_ph_vsync() : pc_vsync_count();
    memcpy(record(C_SKIP_PRESENT, sizeof vsync), &vsync, sizeof vsync);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_set_depth(const int* depth3)
{
    memcpy(record(C_SET_DEPTH, 3 * sizeof(int)), depth3, 3 * sizeof(int));
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_set_packet(const void* packet)
{
    pc_hwc_mirror.poly_effect = pc_hwc_mirror.cur_effect;
    memcpy(record(C_SET_PACKET, sizeof packet), &packet, sizeof packet);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_set_flipped_uv(const int* xy6, const int* uv6)
{
    unsigned char* at = record(C_SET_FLIPPED_UV, 12 * sizeof(int));
    memcpy(at, xy6, 6 * sizeof(int));
    memcpy(at + 6 * sizeof(int), uv6, 6 * sizeof(int));
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_set_bounds(int x0, int y0, int x1, int y1, unsigned code, unsigned tpage, unsigned clut, const int* uv4, const int* clip4)
{
    struct {
        int x0, y0, x1, y1;
        unsigned code, tpage, clut;
        int has_uv, uv[4], clip[4];
    } b = { x0, y0, x1, y1, code, tpage, clut, uv4 != NULL, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    if (uv4)
        memcpy(b.uv, uv4, sizeof b.uv);
    memcpy(b.clip, clip4, sizeof b.clip);
    memcpy(record(C_SET_BOUNDS, sizeof b), &b, sizeof b);
    if (inline_now())
        pc_hwc_flush();
}

void pc_hwc_set_attr(const pc_hw_attr* attr, int screen_x, int screen_w)
{
    struct {
        pc_hw_attr attr;
        int x, w;
    } a = { *attr, screen_x, screen_w };
    pc_hwc_mirror.cur_effect = attr->effect;
    memcpy(record(C_SET_ATTR, sizeof a), &a, sizeof a);
    if (inline_now())
        pc_hwc_flush();
}
/* An ordering table starts and ends (DrawOTag, a frame's items): the producer's side (the registrations, the suppress flag, the
 * bubbles' frame) now, and the renderer's as a record. The record is applied at once: the producer's stale queries (raster_skip, the
 * triangle decisions' marks) read the table's state the begin and the end reset. */
void pc_hwc_begin_ot(void)
{
    pc_hwr_begin_ot();
    record(C_BEGIN_OT, 0);
}
void pc_hwc_end_ot(void)
{
    pc_hwr_end_ot();
    record(C_END_OT, 0);
}
void pc_hwc_native_marker(unsigned list, int draw_x, int draw_y, const int* clip4, unsigned window, int mask_set)
{
    pc_hwr_marker(list, clip4); /* (the coverage and stale marks the renderer's draw will make, assumed) */
    struct {
        unsigned list;
        int x, y, clip[4];
        unsigned window;
        int mask;
    } m = { list, draw_x, draw_y, { 0, 0, 0, 0 }, window, mask_set };
    memcpy(m.clip, clip4, sizeof m.clip);
    memcpy(record(C_NATIVE_MARKER, sizeof m), &m, sizeof m);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_native_translucent(unsigned list, int first, int count, int draw_x, int draw_y, const int* clip4, unsigned window,
    int mask_set)
{
    pc_hwr_translucent(list);
    struct {
        unsigned list;
        int first, count, x, y, clip[4];
        unsigned window;
        int mask;
    } m = { list, first, count, draw_x, draw_y, { 0, 0, 0, 0 }, window, mask_set };
    memcpy(m.clip, clip4, sizeof m.clip);
    memcpy(record(C_NATIVE_TRANSLUCENT, sizeof m), &m, sizeof m);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_ot_native_area(const int* clip4)
{
    if (pc_hw_active && pc_hwr_stale.in_ot && pc_hw_raster_skip_enabled())
        pc_stale_mark(&pc_hwr_stale, clip4, PC_STALE_MISSING); /* (the renderer marks its own set at the record) */
    memcpy(record(C_OT_NATIVE_AREA, 4 * sizeof(int)), clip4, 4 * sizeof(int));
    if (inline_now())
        pc_hwc_flush();
}

typedef struct {
    int x, y, w, h, u, v;
    unsigned color, code, tpage, clut, window;
    int clip[4], mask_set, mask_check;
    const void* command;
    int immediate;
    unsigned string; /* the printer's string and advance the packet belongs to, read where the walk is (the call table is the game's) */
    float advance;
} rect_record;
void pc_hwc_rect(int x, int y, int w, int h, int u, int v, unsigned color, unsigned code, unsigned tpage, unsigned clut,
    unsigned window, const int* clip4, int mask_set, int mask_check)
{
    rect_record r = { x, y, w, h, u, v, color, code, tpage, clut, window, { 0, 0, 0, 0 }, mask_set, mask_check, pc_gpu_command, pc_gpu_immediate, 0, 0 };
    {
        pc_ui_text_piece_info call = { 0 };
        if (pc_ui_text_piece_call(pc_gpu_command, &call))
            r.string = call.string, r.advance = call.advance;
    }
    memcpy(r.clip, clip4, sizeof r.clip);
    memcpy(record(C_RECT, sizeof r), &r, sizeof r);
    if (inline_now())
        pc_hwc_flush();
}
typedef struct {
    int x0, y0, x1, y1;
    unsigned ca, cb;
    int semi;
    unsigned tpage;
    int clip[4], mask_set;
} line_record;
void pc_hwc_line(int x0, int y0, int x1, int y1, unsigned ca, unsigned cb, int semi, unsigned tpage, const int* clip4,
    int mask_set)
{
    line_record r = { x0, y0, x1, y1, ca, cb, semi, tpage, { 0, 0, 0, 0 }, mask_set };
    memcpy(r.clip, clip4, sizeof r.clip);
    memcpy(record(C_LINE, sizeof r), &r, sizeof r);
    if (inline_now())
        pc_hwc_flush();
}

typedef struct {
    int xy[6], uv[6], rgb[9];
    unsigned code, tpage, clut, window;
    int clip[4], mask_set, mask_check;
    pc_hw_tri_decision decision;
} triangle_record;
int pc_hwc_triangle(const int* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip, int mask_set, int mask_check)
{
    triangle_record t;
    pc_hw_triangle_decide(uv, code, tpage, clip, pc_hwc_mirror.is_2d, pc_hwc_mirror.poly_effect, &pc_hwr_stale, pc_hw_suppress, &t.decision);
    if (t.decision.action != PC_HW_TRI_IGNORE) {
        memcpy(t.xy, xy, sizeof t.xy), memcpy(t.uv, uv, sizeof t.uv), memcpy(t.rgb, rgb, sizeof t.rgb);
        t.code = code, t.tpage = tpage, t.clut = clut, t.window = window;
        memcpy(t.clip, clip, sizeof t.clip);
        t.mask_set = mask_set, t.mask_check = mask_check;
        memcpy(record(C_TRIANGLE, sizeof t), &t, sizeof t);
        if (inline_now())
            pc_hwc_flush();
    }
    return t.decision.skip;
}

/* The pending rasterized rectangles (the producer's list; the renderer's is kept in step by the mark records) and the pixel
 * payload of the posts: a pool the flush empties, and a block of its own for a post too big for it. */
static int pend[64][4], pend_count;
void pc_hwc_mark_raster(int x0, int y0, int x1, int y1)
{
    if (pc_hw_active) {
        if (pend_count == 64)
            pc_hwc_raster_post();
        int* r = pend[pend_count++];
        r[0] = x0, r[1] = y0, r[2] = x1, r[3] = y1;
    }
    int rect[4] = { x0, y0, x1, y1 };
    memcpy(record(C_MARK_RASTER, sizeof rect), rect, sizeof rect);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_mark(int x0, int y0, int x1, int y1)
{
    int rect[4] = { x0, y0, x1, y1 };
    memcpy(record(C_MARK, sizeof rect), rect, sizeof rect);
    if (inline_now())
        pc_hwc_flush();
}
typedef struct {
    int count;
    size_t offset;    /* into the pool, or ... */
    uint16_t* block;  /* ... a block of its own (freed when applied) */
    int rects[64][4]; /* clipped to the VRAM */
} post_record;
void pc_hwc_raster_post(void)
{
    if (!pend_count)
        return;
    const uint16_t* vram = pc_native_vram();
    post_record q;
    size_t total = 0;
    q.count = 0;
    for (int i = 0; i < pend_count; i++) {
        const int* r = pend[i];
        int x0 = r[0] < 0 ? 0 : r[0], y0 = r[1] < 0 ? 0 : r[1], x1 = r[2] > 1023 ? 1023 : r[2], y1 = r[3] > 511 ? 511 : r[3];
        if (x1 < x0 || y1 < y0)
            continue;
        q.rects[q.count][0] = x0, q.rects[q.count][1] = y0, q.rects[q.count][2] = x1, q.rects[q.count][3] = y1;
        ++q.count;
        total += (size_t)(x1 - x0 + 1) * (size_t)(y1 - y0 + 1);
    }
    pend_count = 0;
    if (!q.count)
        return;
    uint16_t* at;
    q.block = NULL, q.offset = 0;
    if (total > POOL_PIXELS - current->pool_used)
        hwc_post(); /* the pool is empty in the next batch */
    unsigned char* slot = record(C_RASTER_POST, sizeof q); /* (may hand the batch over: before the pool is used) */
    if (total <= POOL_PIXELS - current->pool_used) {
        q.offset = current->pool_used;
        at = current->pool + current->pool_used;
        current->pool_used += total;
    } else
        at = q.block = malloc(total * sizeof *at);
    for (int i = 0; i < q.count; i++) {
        int x0 = q.rects[i][0], y0 = q.rects[i][1], x1 = q.rects[i][2], y1 = q.rects[i][3], w = x1 - x0 + 1;
        for (int y = y0; y <= y1; y++, at += w)
            memcpy(at, vram + (size_t)y * 1024 + x0, (size_t)w * sizeof *at);
    }
    memcpy(slot, &q, sizeof q);
    if (inline_now())
        pc_hwc_flush();
}

typedef struct {
    pc_ui_text_piece_info info;
    unsigned rgb, code, window;
    int mask_set, mask_check;
    const void* command;
    int immediate;
} text_piece_record;
static int text_emitting; /* the producer's knowledge: a piece's triangles are being drawn */
int pc_hwc_text_quad(const float* xy, const int* uv, const int* rgb, unsigned code, unsigned tpage, unsigned clut, unsigned window,
    const int* clip, int mask_set, int mask_check)
{
    text_piece_record q;
    if (!pc_hw_text_quad_decide(text_emitting, xy, uv, code, tpage, clut, clip, &q.info))
        return 0;
    pc_ui_text_piece_call(pc_gpu_command, &q.info);
    q.rgb = (unsigned)(rgb[0] | rgb[1] << 8 | rgb[2] << 16), q.code = code, q.window = window;
    q.mask_set = mask_set, q.mask_check = mask_check, q.command = pc_gpu_command, q.immediate = pc_gpu_immediate;
    memcpy(record(C_TEXT_PIECE, sizeof q), &q, sizeof q);
    text_emitting = 1;
    if (inline_now())
        pc_hwc_flush();
    return 1;
}
void pc_hwc_text_piece_done(void)
{
    text_emitting = 0;
    record(C_TEXT_PIECE_DONE, 0);
    if (inline_now())
        pc_hwc_flush();
}

void pc_hwc_before_write(int x, int y, int w, int h)
{
    int v[4] = { x, y, w, h };
    memcpy(record(C_BEFORE_WRITE, sizeof v), v, sizeof v);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_stale_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    int v[6] = { sx, sy, dx, dy, w, h };
    pc_stale_copy(&pc_hwr_stale, sx, sy, dx, dy, w, h);
    memcpy(record(C_STALE_COPY, sizeof v), v, sizeof v);
    pc_hwc_stale_pending = 1;
    if (inline_now())
        pc_hwc_flush();
}
/* The read-backs are requests (renderer_rq.h): the renderer replies with the pixels and this side puts them in the game's array. */
static void fence(pc_rq* rq)
{
    pc_rq_readback reply;
    rq->out = &reply, rq->out_bytes = sizeof reply;
    pc_renderer_rq(rq);
    for (int i = 0; i < reply.count; i++) /* (what was read back is no longer stale: the producer's copy forgets it) */
        pc_stale_readback(&pc_hwr_stale, reply.rect[i].x, reply.rect[i].y, reply.rect[i].w, reply.rect[i].h);
    pc_rq_readback_apply(&reply, (uint16_t*)(uintptr_t)pc_native_vram());
}
void pc_hwc_fence_store(int x, int y, int w, int h)
{
    pc_rq rq = { .kind = PC_RQ_SYNC_STORE, .arg = { x, y, w, h } };
    fence(&rq);
}
/* The stream digest\'s events (boot_runtime.c): records, in order with everything else, applied by the renderer. */
typedef struct {
    int op, n;
    unsigned tag;
    int v[32]; /* (a record has up to 16 values, the vertices digest note; a deferred note, 12) */
} stream_record_t;
void pc_hwc_stream(int op, unsigned tag, const int* v, int n)
{
    stream_record_t q = { op, n > 32 ? 32 : n, tag, { 0 } };
    if (v && q.n > 0)
        memcpy(q.v, v, (size_t)q.n * sizeof *v);
    memcpy(record(C_STREAM, sizeof q), &q, sizeof q);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_stream_words(const uint32_t* p, unsigned n)
{
    unsigned char* at = record(C_STREAM_WORDS, (int)(sizeof n + n * sizeof *p));
    memcpy(at, &n, sizeof n);
    memcpy(at + sizeof n, p, n * sizeof *p);
    if (inline_now())
        pc_hwc_flush();
}
typedef struct {
    unsigned tpage, clut, window;
    int u0, v0, u1, v1;
    int has_clip, clip[4];
} texture_fence_record;
void pc_hwc_fence_texture(unsigned tpage, unsigned clut, unsigned window, int u0, int v0, int u1, int v1, const int* clip4)
{
    /* The producer's stale list says whether the renderer could read anything back for this texture: if not, the fence is a record (the renderer
     * still does its own-copy bookkeeping for the primitive), not a round trip. */
    if (!pc_hwr_texture_stale(tpage, clut, window, u0, v0, u1, v1, clip4, pc_hwc_mirror.widescreen)) {
        texture_fence_record q = { tpage, clut, window, u0, v0, u1, v1, clip4 != NULL, { 0, 0, 0, 0 } };
        if (clip4)
            memcpy(q.clip, clip4, sizeof q.clip);
        memcpy(record(C_TEXTURE_FENCE, sizeof q), &q, sizeof q);
        if (inline_now())
            pc_hwc_flush();
        return;
    }
    pc_rq rq = { .kind = PC_RQ_SYNC_TEXTURE, .arg = { (int)tpage, (int)clut, (int)window, u0, v0, u1, v1 }, .in = clip4, .in_bytes = clip4 ? 4 * sizeof(int) : 0 };
    fence(&rq);
}
void pc_hwc_drain(void)
{
    pc_rq rq = { .kind = PC_RQ_DRAIN };
    pc_renderer_rq(&rq);
}
void pc_hwc_resync(void)
{
    pc_stale_set stale;
    pc_rq rq = { .kind = PC_RQ_RESYNC, .in = pc_native_vram(), .in_bytes = 1024 * 512 * sizeof(uint16_t), .out = &stale, .out_bytes = sizeof stale };
    pc_renderer_rq(&rq);
    pc_hwr_stale = stale; /* (a restore may have changed what the renderer holds stale) */
}
void pc_hwc_fence_sync(int x, int y, int w, int h)
{
    pc_rq rq = { .kind = PC_RQ_SYNC, .arg = { x, y, w, h } };
    fence(&rq);
}
/* ---- the emissive matcher's inputs (hw_cmd.h) ---- */
/* The instance keys already noted, as emissive.cpp keys them (a mesh and, with a texture override, its page and CLUT): the
 * producer sends a note once, as the matcher counts it once. An open-addressing table, rebuilt when a mesh goes. */
static uint64_t* noted;
static size_t noted_cap, noted_count;
static int noted_find(uint64_t ik, size_t* at)
{
    size_t i = (size_t)(ik * 0x9e3779b97f4a7c15ull >> 40) & (noted_cap - 1);
    while (noted[i]) {
        if (noted[i] == ik + 1)
            return *at = i, 1;
        i = (i + 1) & (noted_cap - 1);
    }
    return *at = i, 0;
}
static void noted_add(uint64_t ik)
{
    if ((noted_count + 1) * 2 > noted_cap) {
        uint64_t* old = noted;
        size_t old_cap = noted_cap;
        noted_cap = noted_cap ? noted_cap * 2 : 4096;
        noted = calloc(noted_cap, sizeof *noted);
        noted_count = 0;
        for (size_t i = 0; i < old_cap; i++)
            if (old[i])
                noted_add(old[i] - 1);
        free(old);
    }
    size_t at;
    if (!noted_find(ik, &at))
        noted[at] = ik + 1, ++noted_count;
}
typedef struct {
    uint32_t mesh;
    int count, override;
    unsigned tpage, clut;
    void* polygons; /* a copy, freed when applied */
} emissive_note_record;
void pc_hwc_emissive_note(uint32_t mesh, const void* polygons, int count, int override, unsigned tpage, unsigned clut)
{
    if (!mesh || !polygons || count <= 0)
        return;
    int texture = (override & RENDER_OVERRIDE_TEXTURE) != 0;
    uint64_t ik = (uint64_t)mesh << 32 | (texture ? ((uint64_t)(tpage & 0xffff) << 16 | (clut & 0xffff)) : 0xffffffffu);
    size_t at;
    if (noted_cap && noted_find(ik, &at))
        return;
    noted_add(ik);
    emissive_note_record q = { mesh, count, override, tpage, clut, malloc((size_t)count * sizeof(render_polygon)) };
    if (!q.polygons)
        return;
    memcpy(q.polygons, polygons, (size_t)count * sizeof(render_polygon));
    memcpy(record(C_EMISSIVE_NOTE, sizeof q), &q, sizeof q);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_emissive_mesh_gone(uint32_t mesh)
{
    if (noted_count) { /* forget the mesh's keys: rebuild without them */
        uint64_t* old = noted;
        size_t old_cap = noted_cap;
        noted = calloc(noted_cap, sizeof *noted);
        noted_count = 0;
        for (size_t i = 0; i < old_cap; i++)
            if (old[i] && (uint32_t)((old[i] - 1) >> 32) != mesh)
                noted_add(old[i] - 1);
        free(old);
    }
    memcpy(record(C_EMISSIVE_GONE, sizeof mesh), &mesh, sizeof mesh);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_emissive_room_mesh(uint32_t mesh, unsigned offset, unsigned tris, unsigned quads)
{
    unsigned v[4] = { mesh, offset, tris, quads };
    memcpy(record(C_EMISSIVE_ROOM_MESH, sizeof v), v, sizeof v);
    if (inline_now())
        pc_hwc_flush();
}
/* The room the frame is drawn in: sent when it changes, and with every ordering table's start. */
void pc_hwc_frame_room(void)
{
    static int last[3] = { -2, 0, 0 };
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    if (pc_dev_state)
        pc_dev_state(&s);
    int v[3] = { s.valid != 0, s.valid ? s.zone : -1, s.valid ? s.room : -1 };
    if (!memcmp(v, last, sizeof v))
        return;
    memcpy(last, v, sizeof v);
    memcpy(record(C_FRAME_ROOM, sizeof v), v, sizeof v);
    if (inline_now())
        pc_hwc_flush();
}

void pc_hwc_upload_mode(int mode)
{
    memcpy(record(C_UPLOAD_MODE, sizeof mode), &mode, sizeof mode);
    if (inline_now())
        pc_hwc_flush();
}
/* ---- VRAM events with their payload (hw_cmd.h) ---- */
/* The rectangle the game's array holds after the event (rows in order, wrapping at the edges as the hardware does), taken into the
 * pool (or a block of its own) the way a raster post's pixels are. */
typedef struct {
    size_t offset;
    uint16_t* block;
} vram_pixels;
static unsigned char* vram_reserve(int kind, size_t record_bytes, size_t pixels)
{
    if (pixels > POOL_PIXELS - current->pool_used)
        hwc_post(); /* the pool is empty in the next batch */
    return record(kind, (int)record_bytes); /* (may hand the batch over: before the pool is used) */
}
static void vram_take(vram_pixels* p, int x, int y, int w, int h)
{
    size_t total = (size_t)w * (size_t)h;
    uint16_t* at;
    p->block = NULL, p->offset = 0;
    if (total <= POOL_PIXELS - current->pool_used) {
        p->offset = current->pool_used;
        at = current->pool + current->pool_used;
        current->pool_used += total;
    } else
        at = p->block = malloc(total * sizeof *at);
    const uint16_t* vram = pc_native_vram();
    for (int row = 0; row < h; row++, at += w) {
        if (x >= 0 && y >= 0 && x + w <= 1024 && y + h <= 512)
            memcpy(at, vram + (size_t)(y + row) * 1024 + x, (size_t)w * sizeof *at);
        else
            for (int col = 0; col < w; col++)
                at[col] = vram[(size_t)((y + row) & 511) * 1024 + (size_t)((x + col) & 1023)];
    }
}
static const uint16_t* vram_pixels_of(const batch* b, const vram_pixels* p) { return p->block ? p->block : b->pool + p->offset; }
typedef struct {
    vram_pixels p;
    int x, y, w, h, r, g, b;
} vram_fill_record;
typedef struct {
    vram_pixels p;
    int x, y, w, h;
} vram_upload_record;
typedef struct {
    vram_pixels p;
    int sx, sy, dx, dy, w, h;
} vram_copy_record;
void pc_hwc_vram_fill(int x, int y, int w, int h, int r, int g, int b)
{
    if (!pc_hw_active || w <= 0 || h <= 0)
        return;
    vram_fill_record q = { { 0, NULL }, x, y, w, h, r, g, b };
    unsigned char* slot = vram_reserve(C_VRAM_FILL, sizeof q, (size_t)w * (size_t)h);
    vram_take(&q.p, x, y, w, h);
    memcpy(slot, &q, sizeof q);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_vram_upload(int x, int y, int w, int h)
{
    if (!pc_hw_active || w <= 0 || h <= 0)
        return;
    vram_upload_record q = { { 0, NULL }, x, y, w, h };
    unsigned char* slot = vram_reserve(C_VRAM_UPLOAD, sizeof q, (size_t)w * (size_t)h);
    vram_take(&q.p, x, y, w, h);
    memcpy(slot, &q, sizeof q);
    if (inline_now())
        pc_hwc_flush();
}
void pc_hwc_vram_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    if (!pc_hw_active || w <= 0 || h <= 0)
        return;
    vram_copy_record q = { { 0, NULL }, sx, sy, dx, dy, w, h };
    unsigned char* slot = vram_reserve(C_VRAM_COPY, sizeof q, (size_t)w * (size_t)h);
    vram_take(&q.p, dx, dy, w, h);
    memcpy(slot, &q, sizeof q);
    if (inline_now())
        pc_hwc_flush();
}

/* Apply the records in order. The pending count is cleared first: the hooks called here begin with PC_HWC_SYNC and must find
 * nothing to apply. */
static void apply_batch(batch* b)
{
    int end = b->pending, at = 0;
    while (at < end) {
        const unsigned char* r = b->buffer + at;
        int type = r[0];
        ++r;
        switch (type) {
        case C_SET_2D: {
            int v;
            memcpy(&v, r, sizeof v);
            pc_hw_set_2d(v);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_NARROWED:
            if (pc_hw_set_narrowed)
                pc_hw_set_narrowed();
            at += 1;
            break;
        case C_SET_PRECISE: {
            float v[6];
            memcpy(v, r, sizeof v);
            pc_hw_set_precise(v);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_DEPTH: {
            int v[3];
            memcpy(v, r, sizeof v);
            pc_hw_set_depth(v);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_PACKET: {
            const void* v;
            memcpy(&v, r, sizeof v);
            pc_hw_set_packet(v);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_FLIPPED_UV: {
            int v[12];
            memcpy(v, r, sizeof v);
            pc_hw_set_flipped_uv(v, v + 6);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_BOUNDS: {
            struct {
                int x0, y0, x1, y1;
                unsigned code, tpage, clut;
                int has_uv, uv[4], clip[4];
            } b;
            memcpy(&b, r, sizeof b);
            pc_hw_set_bounds(b.x0, b.y0, b.x1, b.y1, b.code, b.tpage, b.clut, b.has_uv ? b.uv : NULL, b.clip);
            at += 1 + (int)sizeof b;
            break;
        }
        case C_FRAME_MENU_MODEL: {
            int on;
            memcpy(&on, r, sizeof on);
            pc_hw_frame_menu_model(on);
            at += 1 + (int)sizeof on;
            break;
        }
        case C_STREAM: {
            stream_record_t q;
            memcpy(&q, r, sizeof q);
            pc_stream_apply(q.op, q.tag, q.v, q.n);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_STREAM_WORDS: {
            unsigned n;
            memcpy(&n, r, sizeof n);
            uint32_t words[256];
            memcpy(words, r + sizeof n, n * sizeof *words);
            pc_stream_apply_words(words, n);
            at += 1 + (int)(sizeof n + n * sizeof *words);
            break;
        }
        case C_TEXTURE_FENCE: {
            texture_fence_record q;
            memcpy(&q, r, sizeof q);
            pc_hw_sync_texture(q.tpage, q.clut, q.window, q.u0, q.v0, q.u1, q.v1, q.has_clip ? q.clip : NULL);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_SET_SUPPRESS: {
            int v;
            memcpy(&v, r, sizeof v);
            pc_hw_set_suppress(v);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_EXACT: {
            struct {
                float xy[6];
                int cover;
            } e;
            memcpy(&e, r, sizeof e);
            pc_hw_set_exact(e.xy, e.cover);
            at += 1 + (int)sizeof e;
            break;
        }
        case C_CLEAR_EXACT:
            pc_hw_clear_exact();
            at += 1;
            break;
        case C_SKIP_PRESENT: {
            unsigned v;
            memcpy(&v, r, sizeof v);
            pc_hw_skip_present_now(v);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_SETTINGS: {
            int v[5];
            memcpy(v, r, sizeof v);
            pc_hw_set_settings(v);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_SET_WIDESCREEN: {
            int w;
            memcpy(&w, r, sizeof w);
            pc_hw_set_widescreen(w);
            at += 1 + (int)sizeof w;
            break;
        }
        case C_SET_ATTR: {
            struct {
                pc_hw_attr attr;
                int x, w;
            } a;
            memcpy(&a, r, sizeof a);
            pc_hw_set_attr(&a.attr, a.x, a.w);
            at += 1 + (int)sizeof a;
            break;
        }
        case C_NATIVE_MARKER: {
            struct {
                unsigned list;
                int x, y, clip[4];
                unsigned window;
                int mask;
            } m;
            memcpy(&m, r, sizeof m);
            if (pc_hw_native_marker)
                pc_hw_native_marker(m.list, m.x, m.y, m.clip, m.window, m.mask);
            at += 1 + (int)sizeof m;
            break;
        }
        case C_NATIVE_TRANSLUCENT: {
            struct {
                unsigned list;
                int first, count, x, y, clip[4];
                unsigned window;
                int mask;
            } m;
            memcpy(&m, r, sizeof m);
            if (pc_hw_native_translucent)
                pc_hw_native_translucent(m.list, m.first, m.count, m.x, m.y, m.clip, m.window, m.mask);
            at += 1 + (int)sizeof m;
            break;
        }
        case C_OT_NATIVE_AREA: {
            int clip[4];
            memcpy(clip, r, sizeof clip);
            if (pc_hw_ot_native_area)
                pc_hw_ot_native_area(clip);
            at += 1 + (int)sizeof clip;
            break;
        }
        case C_RECT: {
            rect_record q;
            memcpy(&q, r, sizeof q);
            const void* was = pc_gpu_command; /* the hook reads the packet it draws from here */
            pc_gpu_command = q.command;
            int was_immediate = pc_gpu_immediate;
            pc_gpu_immediate = q.immediate;
            pc_hw_set_call(q.string, q.advance);
            pc_hw_rect(q.x, q.y, q.w, q.h, q.u, q.v, q.color, q.code, q.tpage, q.clut, q.window, q.clip, q.mask_set, q.mask_check);
            pc_hw_set_call(0, -1.0f);
            pc_gpu_immediate = was_immediate;
            pc_gpu_command = was;
            at += 1 + (int)sizeof q;
            break;
        }
        case C_LINE: {
            line_record q;
            memcpy(&q, r, sizeof q);
            pc_hw_line(q.x0, q.y0, q.x1, q.y1, q.ca, q.cb, q.semi, q.tpage, q.clip, q.mask_set);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_TRIANGLE: {
            triangle_record t;
            memcpy(&t, r, sizeof t);
            pc_hw_triangle_apply(t.xy, t.uv, t.rgb, t.code, t.tpage, t.clut, t.window, t.clip, t.mask_set, t.mask_check,
                &t.decision);
            at += 1 + (int)sizeof t;
            break;
        }
        case C_TEXT_PIECE: {
            text_piece_record q;
            memcpy(&q, r, sizeof q);
            int was_immediate = pc_gpu_immediate;
            pc_gpu_immediate = q.immediate;
            pc_hw_text_piece_apply(&q.info, q.rgb, q.code, q.window, q.mask_set, q.mask_check, q.command);
            pc_gpu_immediate = was_immediate;
            at += 1 + (int)sizeof q;
            break;
        }
        case C_TEXT_PIECE_DONE:
            pc_hw_text_piece_end();
            at += 1;
            break;
        case C_EMISSIVE_NOTE: {
            emissive_note_record q;
            memcpy(&q, r, sizeof q);
            pc_emissive_note_instance(q.mesh, q.polygons, q.count, q.override, q.tpage, q.clut);
            free(q.polygons);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_EMISSIVE_GONE: {
            uint32_t mesh;
            memcpy(&mesh, r, sizeof mesh);
            pc_emissive_mesh_gone(mesh);
            at += 1 + (int)sizeof mesh;
            break;
        }
        case C_EMISSIVE_ROOM_MESH: {
            unsigned v[4];
            memcpy(v, r, sizeof v);
            pc_emissive_room_mesh(v[0], v[1], v[2], v[3]);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_FRAME_ROOM: {
            int v[3];
            memcpy(v, r, sizeof v);
            pc_emissive_frame_room(v[0], v[1], v[2]);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_BEGIN_OT:
            pc_hw_begin_ot();
            at += 1;
            break;
        case C_END_OT:
            pc_hw_end_ot();
            at += 1;
            break;
        case C_UPLOAD_MODE: {
            int mode;
            memcpy(&mode, r, sizeof mode);
            pc_hw_upload_mode(mode);
            at += 1 + (int)sizeof mode;
            break;
        }
        case C_VRAM_FILL: {
            vram_fill_record q;
            memcpy(&q, r, sizeof q);
            pc_hw_fill(q.x, q.y, q.w, q.h, q.r, q.g, q.b, vram_pixels_of(b, &q.p));
            free(q.p.block);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_VRAM_UPLOAD: {
            vram_upload_record q;
            memcpy(&q, r, sizeof q);
            pc_hw_upload(q.x, q.y, q.w, q.h, vram_pixels_of(b, &q.p));
            free(q.p.block);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_VRAM_COPY: {
            vram_copy_record q;
            memcpy(&q, r, sizeof q);
            pc_hw_copy(q.sx, q.sy, q.dx, q.dy, q.w, q.h, vram_pixels_of(b, &q.p));
            free(q.p.block);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_MARK_RASTER: {
            int rect[4];
            memcpy(rect, r, sizeof rect);
            pc_hw_mark_raster(rect[0], rect[1], rect[2], rect[3]);
            at += 1 + (int)sizeof rect;
            break;
        }
        case C_MARK: {
            int rect[4];
            memcpy(rect, r, sizeof rect);
            pc_hw_mark(rect[0], rect[1], rect[2], rect[3]);
            at += 1 + (int)sizeof rect;
            break;
        }
        case C_RASTER_POST: {
            post_record q;
            memcpy(&q, r, sizeof q);
            pc_hw_raster_apply(q.rects, q.count, q.block ? q.block : b->pool + q.offset);
            free(q.block);
            at += 1 + (int)sizeof q;
            break;
        }
        case C_BEFORE_WRITE: {
            int v[4];
            memcpy(v, r, sizeof v);
            if (pc_hw_before_write)
                pc_hw_before_write(v[0], v[1], v[2], v[3]);
            at += 1 + (int)sizeof v;
            break;
        }
        case C_STALE_COPY: {
            int v[6];
            memcpy(v, r, sizeof v);
            if (pc_hw_stale_copy)
                pc_hw_stale_copy(v[0], v[1], v[2], v[3], v[4], v[5]);
            at += 1 + (int)sizeof v;
            break;
        }
        default:
            return; /* (cannot happen: the records are this file's) */
        }
    }
    b->pool_used = 0; /* (the payloads are applied) */
}
struct apply_args {
    batch* b;
};
static void apply_call(void* arg)
{
    batch* b = ((struct apply_args*)arg)->b;
    apply_batch(b);
    __atomic_store_n(&b->busy, 0, __ATOMIC_RELEASE);
}
/* Hand the current batch over: the renderer applies it in order (in place, or on its thread), and the producer's count starts again. The
 * records the apply's own hooks find pending are none: the count is cleared first. */
static void hwc_post(void)
{
    if (pc_rt_on_thread() || !pc_hwc_pending)
        return;
    batch* b = current;
    b->pending = pc_hwc_pending;
    pc_hwc_pending = 0;
    pc_hwc_stale_pending = 0;
    struct apply_args args = { b };
    if (pc_render_thread >= 2) {
        __atomic_store_n(&b->busy, 1, __ATOMIC_RELAXED);
        pc_rt_post(apply_call, &args, sizeof args);
        /* the next free batch (the renderer gives them back as it applies) */
        for (;;) {
            for (int k = 1; k <= BATCHES; k++)
                if (!__atomic_load_n(&batches[(b - batches + k) % BATCHES].busy, __ATOMIC_ACQUIRE)) {
                    current = &batches[(b - batches + k) % BATCHES];
                    return;
                }
            pc_rt_drain();
        }
    }
    if (pc_render_thread)
        pc_rt_call(apply_call, &args); /* (lockstep: the thread applies it, the game thread waits) */
    else
        apply_call(&args);
}
void pc_hwc_post(void) { hwc_post(); }
/* Before a hook that reads or writes the renderer's state from the game thread (PC_HWC_SYNC): everything recorded is applied, and with a
 * renderer thread running on its own, everything posted before (frames included) is done. */
extern int pc_hw_stale_matches(const pc_stale_set* p);
/* ROOD_STALE_CHECK=1: the producer's stale set against the renderer's wherever a flush has just applied everything (they run the same marks). */
static void stale_check(void)
{
    static int on = -1;
    static int shown;
    if (on < 0)
        on = getenv("ROOD_STALE_CHECK") != NULL;
    if (!on || pc_render_thread >= 2 || pc_hwc_pending)
        return;
    static unsigned long long checks, differ, nonempty, in_table;
    ++checks;
    nonempty += pc_hwr_stale.count > 0 || pc_hwr_stale.ot_count > 0;
    in_table += pc_hwr_stale.in_ot;
    if (!pc_hw_stale_matches(&pc_hwr_stale) && ++differ && shown++ < 20)
        fprintf(stderr, "[stale] the producer's set differs from the renderer's after check %llu (%llu differ); producer %d rects, in_ot %d, ot %d\n", checks,
            differ, pc_hwr_stale.count, pc_hwr_stale.in_ot, pc_hwr_stale.ot_count);
    if (checks % 5000 == 0)
        fprintf(stderr, "[stale] %llu checks, %llu differ, %llu with something stale, %llu inside a table\n", checks, differ, nonempty, in_table);
}
/* ROOD_RT_TRACE=3: who calls the draining flush from the game thread (a histogram by return address, printed at exit). */
static struct {
    void* at;
    unsigned long long n;
} flush_callers[32];
static void flush_caller_note(void* at)
{
    for (int i = 0; i < 32; i++) {
        if (flush_callers[i].at == at || !flush_callers[i].at) {
            flush_callers[i].at = at, ++flush_callers[i].n;
            return;
        }
    }
}
void pc_hwc_flush_report(void)
{
    for (int i = 0; i < 32; i++)
        if (flush_callers[i].at)
            fprintf(stderr, "[render]   flush from %p: %llu\n", flush_callers[i].at, flush_callers[i].n);
}
void pc_hwc_flush(void)
{
    if (pc_rt_on_thread())
        return;
    if (pc_render_thread >= 2)
        flush_caller_note(__builtin_return_address(0));
    hwc_post();
    if (pc_render_thread >= 2)
        pc_rt_drain();
    stale_check();
}
