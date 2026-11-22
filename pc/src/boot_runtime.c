#define _GNU_SOURCE
#include "audio.h"
#include "view_state.h"
#include "present_service.h"
#include "render_jitter.h"
#include "pc_paths.h"
#include "psx_types.h"
#include "platform.h" /* pc_replace_file */
#include "frame_cost.h" /* ROOD_PERF sections */
#include "hw_meter.h"
#include "hw_cmd.h" /* the walk records the renderer calls that need no answer (T1 R1) */ /* ROOD_HW_METER: the renderer hooks counted (T1 plan R0) */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#else
#include <sys/mman.h>
#include <execinfo.h>
#include <ucontext.h>
#endif
static uint16_t vram[1024 * 512];
extern void pc_snapshot_resume_if_requested(void) __attribute__((weak)); /* snapshot.c (ROOD_SNAPSHOT) */
/* engine_host.c (build_boot.py --engine-library only): load the engine
 * library before the first call into it; reload it at a vsync. */
extern void pc_engine_start(void) __attribute__((weak));
extern void pc_engine_vsync(void) __attribute__((weak));
const uint16_t* pc_native_vram(void) { return vram; } /* the game's array: the hardware init's mirror source, snapshots */
/* Where the renderer drew (T5-6): the game's array holds only what transfers wrote and what read-backs copied from the target, so a cell
 * (64 x 16 pixels; bit `band` of column `c`) a primitive's pixels fell in is not the game's to compare until a transfer covers it whole
 * again. The oracle digest (snapshot.c) leaves those cells out. Saved state (it is what the digest is computed over, so a restore, a restart and a rewind must
 * bring it back with the array: the digest of the restored state is the saved one). */
static uint32_t vram_drawn[16];
/* A primitive's pixels, inclusive, inside the drawing area (the walk, boot_gpu_polygons.inc, boot_gpu_lines.inc and the rectangle code). */
void pc_vram_census_raster(int x0, int y0, int x1, int y1);
static void vram_note_drawn(int x0, int y0, int x1, int y1)
{
    pc_vram_census_raster(x0, y0, x1, y1);
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > 1023 ? 1023 : x1, y1 = y1 > 511 ? 511 : y1;
    for (int c = x0 >> 6; x0 <= x1 && y0 <= y1 && c <= x1 >> 6; c++)
        for (int b = y0 >> 4; b <= y1 >> 4; b++)
            vram_drawn[c] |= 1u << b;
}
/* A transfer wrote this rectangle: the cells it covers whole are exact again. */
static void vram_note_written(int x, int y, int w, int h)
{
    int c0 = (x + 63) >> 6, c1 = (x + w) >> 6, b0 = (y + 15) >> 4, b1 = (y + h) >> 4; /* [c0, c1) x [b0, b1) */
    for (int c = c0; c < c1 && c < 16; c++)
        for (int b = b0; b < b1 && b < 32; b++)
            vram_drawn[c] &= ~(1u << b);
}
int pc_vram_drawn_bit(int column, int band) { return (int)(vram_drawn[column & 15] >> (band & 31) & 1); } /* a cell (64 x 16) a primitive drew in */
/* The oracle's digest of the game's array: FNV-1a over the cells no primitive drew in (snapshot.c). */
uint64_t pc_vram_transfer_digest(uint64_t h)
{
    for (int b = 0; b < 32; b++)
        for (int row = 0; row < 16; row++)
            for (int c = 0; c < 16; c++) {
                if (vram_drawn[c] >> b & 1)
                    continue;
                const unsigned char* p = (const unsigned char*)(vram + (b * 16 + row) * 1024 + c * 64);
                for (int i = 0; i < 128; i++)
                    h = (h ^ p[i]) * 1099511628211ull;
            }
    return h;
}
/* The VRAM the renderer's readers (lighting, environment, path tracing, the text and prompt tools) see: the renderer's own copy
 * when the hardware renderer runs (gpu_hw.c, T5 S2), else the game's array (the CPU rasterizer owns VRAM). T1 R2. */
extern int pc_hw_active __attribute__((weak));
extern const uint16_t* pc_hw_renderer_vram(void) __attribute__((weak));
const uint16_t* pc_renderer_vram(void)
{
    return &pc_hw_active && pc_hw_active && pc_hw_renderer_vram ? pc_hw_renderer_vram() : vram;
}
/* RPC vram poke: a rectangle set to one value, as a stream upload (the game's array, the event, the renderer's copy). */
void pc_vram_transfer(int x, int y, int w, int h, void* data, int write);
void pc_vram_poke(int x, int y, int w, int h, unsigned value)
{
    uint16_t* rows = malloc((size_t)w * (size_t)h * sizeof *rows);
    if (!rows)
        return;
    for (int i = 0; i < w * h; i++)
        rows[i] = (uint16_t)value;
    pc_vram_transfer(x, y, w, h, rows, 1);
    free(rows);
}
/* Hardware rasterizer mirror (gpu_hw.c); no-ops until a GL context exists. */
/* The R4 harness (render_jitter.h): ROOD_RENDER_JITTER seeds sleeps where the game hands work to the renderer side. */
extern int pc_jitter_json(char*, int) __attribute__((weak));
extern void pc_jitter(int) __attribute__((weak));
extern int pc_hw_triangle(const int*, const int*, const int*, unsigned, unsigned, unsigned,
    unsigned, const int*, int, int);
extern void pc_hw_rect(int, int, int, int, int, int, unsigned, unsigned, unsigned, unsigned,
    unsigned, const int*, int, int);
extern void pc_hw_line(int, int, int, int, unsigned, unsigned, int, unsigned, const int*, int);
/* Native VRAM is about to be read here: copy back what only the hardware
 * target has (gpu_hw.c; absent in unit probes). */
extern void pc_hw_sync(int, int, int, int) __attribute__((weak));
extern void pc_hw_sync_store(int, int, int, int) __attribute__((weak));
extern void pc_hw_sync_texture(unsigned, unsigned, unsigned, int, int, int, int, const int*) __attribute__((weak));
extern void pc_hw_stale_copy(int, int, int, int, int, int) __attribute__((weak));
/* What a VRAM write replaces, kept for frame interpolation (gpu_hw.c). */
extern void pc_hw_before_write(int, int, int, int) __attribute__((weak));
extern void pc_hw_set_2d(int);
extern void pc_hw_set_bounds(int, int, int, int, unsigned, unsigned, unsigned, const int*, const int*);
extern void pc_hw_set_packet(const void*);
extern int pc_ui_precise_lookup(int, const unsigned*, int, float*) __attribute__((weak)); /* ui_upscale.c */
extern int pc_ui_precise_find(int, const unsigned*, int, float*) __attribute__((weak)); /* ui_upscale.c */
/* ui_text.c: battle number popups at the UI text scale. */
extern int pc_ui_text_popup_place(const uint32_t*, const uint32_t* (*)(const uint32_t*), float*) __attribute__((weak));
extern int pc_ui_text_popup_place_item(int, float*) __attribute__((weak));
extern int pc_frame_command_item(const void*) __attribute__((weak));
/* Widescreen HUD anchoring of the packet about to draw (gpu_hw.c). */
#include "gpu_attr.h"
extern void pc_hw_resolve(const void*, pc_hw_attr*) __attribute__((weak)); /* gpu_hw.c (absent in unit probes) */
extern void pc_hw_set_attr(const pc_hw_attr*, int, int) __attribute__((weak));
extern int pc_ui_bubble_find(const void*) __attribute__((weak)); /* ui_bubbles.c */
extern int pc_ui_precise_slot(const void*) __attribute__((weak)); /* ui_upscale.c */
static void attr_none(pc_hw_attr* a)
{
    *a = (pc_hw_attr) { .effect = -1, .span = -1, .opacity = 256, .bubble = -1, .precise = -1 };
}
/* Everything the walk asks about the packet whose tag word is at `tag`, settled once (the registries are keyed by address). */
static void resolve_attr(const void* tag, pc_hw_attr* a)
{
    attr_none(a);
    if (pc_hw_resolve)
        pc_hw_resolve(tag, a);
    if (pc_ui_bubble_find)
        a->bubble = (short)pc_ui_bubble_find(tag);
    if (pc_ui_precise_slot)
        a->precise = (short)pc_ui_precise_slot((const uint32_t*)tag + 1); /* (a glyph's command follows its tag) */
}
extern int pc_gte_projected(int, int);
extern int pc_gte_projected_depth(int, int, int*);
extern void pc_hw_set_depth(const int*);
extern void pc_hw_set_precise(const float*);
extern void pc_hw_set_flipped_uv(const int*, const int*);
extern int pc_gte_widescreen __attribute__((weak));
extern void pc_hw_set_narrowed(void) __attribute__((weak));
/* Widescreen: whether every vertex was projected (narrowed) this frame (the GTE
 * records projections in widescreen only). */
static int ot_front; /* (below: the packet being drawn is linked in front of the scene's slots) */
static int narrowed_2d(const int (*xy)[2], int count)
{
    if (!&pc_gte_widescreen || !pc_gte_widescreen || !pc_hw_set_narrowed || ot_front)
        return 0;
    for (int i = 0; i < count; ++i) {
        int found = pc_gte_projected(xy[i][0], xy[i][1]);
        /* Lines: within a pixel (the targeting sphere nudges each segment's
         * start by its slope, BATTLE 573B8.c func_800C0990). */
        for (int k = 0; count == 2 && !found && k < 9; ++k)
            found = k != 4 && pc_gte_projected(xy[i][0] + k % 3 - 1, xy[i][1] + k / 3 - 1);
        if (!found)
            return 0;
    }
    return 1;
}
extern int pc_scene_game_projection(float*, float*, float*);
static int stop_at_loading, stop_at_title, stop_at_opening, opening_ready;
static unsigned opening_frames, battle_frames, stop_at_battle_frame;
extern void pc_window_init(int, int);
extern void pc_window_tick(void);
extern void pc_window_frame(const uint16_t*, int, int, int, int);
int pc_skip_movies;
/* The publisher and developer logos before the first title (TITLE's
 * _displayPublisherAndDeveloper); skipped with the intro movie when the
 * skip_intro setting is on, for the first title only (pc_title_menu_ready). */
int pc_skip_logos;
static int intro_skipped_by_setting; /* 1: logos skipped; 2: and the movie (no --skip-movies) */
extern int pc_skip_intro __attribute__((weak)); /* project_settings.cpp */
static int display_x, display_y, display_w = 320, display_h = 224;
static int draw_x, draw_y, clip_x, clip_y, clip_w = 1024, clip_h = 512, tpage;
static unsigned int frames;
static unsigned texture_window, mask_set, mask_check;
void vs_main_exec(void);
/* Overlay statics restart on each overlay load (overlay_statics.c). */
extern void pc_overlay_statics_init(void);
extern void pc_overlay_statics_loaded(int);
/* Plugins (pc/src/lua, pc/docs/Extensions.md "Where Lua runs"); weak: probes link this file alone. */
extern void pc_lua_init(void) __attribute__((weak));
extern void pc_lua_tick(void) __attribute__((weak));
static void pc_dump_stack(int sig);
void pc_unimplemented(const char* name)
{
    fprintf(stderr, "[UNIMPLEMENTED] %s (battle frame %u)\n", name, battle_frames);
    pc_dump_stack(0); /* where it was reached: pc/tools/where.sh resolve */
    exit(2);
}
void pc_log(const char* message) { fprintf(stderr, "[boot] %s\n", message); }
/* A table index the data should not produce (a table declared with the extent the disc's data needs, pc/tools/event_table_scan.py): the access is refused
 * by the caller; this logs it once per table, with where it came from. */
void pc_table_index_refused(const char* table, long index, long length, const char* where, unsigned long first, unsigned long second)
{
    static const char* seen[16] __attribute__((section(".data.rood_host")));
    for (int i = 0; i < 16; i++) {
        if (seen[i] == table)
            return;
        if (!seen[i]) {
            seen[i] = table;
            break;
        }
    }
    fprintf(stderr, "[table] %s: index %ld is outside 0..%ld (%s %#lx, %#lx): refused\n", table, index, length - 1, where, first, second);
}
int pc_table_index_ok(const char* table, long index, long length, const char* where, unsigned long first, unsigned long second)
{
    if (index >= 0 && index < length)
        return 1;
    pc_table_index_refused(table, index, length, where, first, second);
    return 0;
}
/* An effect script's command byte past the table's 49 (vs_effects_runScript): the cursor is not on a command. The script ends;
 * reported at most once per 60 battle frames. */
void pc_effect_bad_opcode(int opcode, int cursor)
{
    static unsigned last __attribute__((section(".data.rood_host"))) = ~0u;
    if (last == ~0u || battle_frames - last >= 60) {
        fprintf(stderr, "[effect] command opcode %d at cursor %#x is out of range: the script ends (battle frame %u)\n", opcode, cursor,
            battle_frames);
        last = battle_frames;
    }
}
/* ROOD_SYNC_DEBUG: who reads VRAM back (StoreImage callers). */
void pc_store_image_debug(int x, int y, int w, int h, const void* caller)
{
    static int debug __attribute__((section(".data.rood_host"))) = -1; /* host setting: not saved state */
    if (debug < 0)
        debug = getenv("ROOD_SYNC_DEBUG") != NULL;
    if (debug)
        fprintf(stderr, "[sync] StoreImage %d,%d %dx%d from %p\n", x, y, w, h, caller);
}
/* The VRAM stream's digest (T5 S0, pc/docs/plans/2026-11-12-t5-vram-survey.md): every event that changes or reads VRAM
 * through a command (upload, readback, fill, copy) hashed with its rectangle, its payload and the number of draw packets
 * walked before it in the frame. The digest of a frame and the running one are what RPC vram_stream replies and
 * build_compare --stream folds into each sampled frame: two builds that feed the renderer the same writes in the same
 * order against the same draws agree. Host state, empty after a restore (a snapshot cuts at the vsync boundary). */
static struct {
    uint64_t frame, total, last;
    unsigned events, total_events, last_events, draws, frames;
} stream __attribute__((section(".data.rood_host"))) = { 0xcbf29ce484222325ull, 0xcbf29ce484222325ull, 0xcbf29ce484222325ull };
static void stream_mix(const void* data, size_t bytes)
{
    const unsigned char* p = data;
    for (size_t i = 0; i < bytes; i++) {
        stream.frame = (stream.frame ^ p[i]) * 0x100000001b3ull;
        stream.total = (stream.total ^ p[i]) * 0x100000001b3ull;
    }
}
/* The VRAM role census (T5-0, pc/docs/plans/2026-11-17-vram.md; RPC vram_census, pc/tools/vram_census.py): what touches each 64 x 64 block of the
 * array, by the call that does it: uploads, fills, copies (written to / read from), StoreImage reads, the rectangles the primitives drew ("raster"),
 * texel pages and CLUT rows the primitives sample, and the display rectangle (one count per vsync). The roles (texture, CLUT, frame, movie,
 * scratch) are read off the counts by the tool. Off until asked; a count is one event or primitive, not a pixel. Host state. */
enum { VC_UPLOAD, VC_FILL, VC_COPY_DST, VC_COPY_SRC, VC_STORE, VC_RASTER, VC_TEXEL, VC_CLUT, VC_DISPLAY, VC_KINDS };
static const char* const vcen_names[VC_KINDS] = { "upload", "fill", "copy_dst", "copy_src", "store", "raster", "texel", "clut", "display" };
static struct {
    int on;
    unsigned frames;
    unsigned long long cell[VC_KINDS][128];
} vcen __attribute__((section(".data.rood_host")));
static void vcen_rect(int kind, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    int x1 = x + w - 1, y1 = y + h - 1;
    x = x < 0 ? 0 : x, y = y < 0 ? 0 : y, x1 = x1 > 1023 ? 1023 : x1, y1 = y1 > 511 ? 511 : y1;
    for (int cy = y / 64; cy <= y1 / 64; cy++)
        for (int cx = x / 64; cx <= x1 / 64; cx++)
            ++vcen.cell[kind][cy * 16 + cx];
}
/* A primitive samples texels of the page `tpage` between (u0, v0) and (u1, v1) and, in a paletted mode, the CLUT row `clut`. */
void pc_vram_census_sample(unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1)
{
    if (!vcen.on)
        return;
    int mode = (int)(tpage >> 7 & 3), div = mode == 0 ? 4 : mode == 1 ? 2 : 1, px = (int)(tpage & 15) * 64, py = (int)(tpage >> 4 & 1) * 256;
    if (u0 > u1)
        u0 ^= u1, u1 ^= u0, u0 ^= u1;
    if (v0 > v1)
        v0 ^= v1, v1 ^= v0, v0 ^= v1;
    u0 = u0 < 0 ? 0 : u0, v0 = v0 < 0 ? 0 : v0, u1 = u1 > 255 ? 255 : u1, v1 = v1 > 255 ? 255 : v1;
    vcen_rect(VC_TEXEL, px + u0 / div, py + v0, (u1 - u0) / div + 1, v1 - v0 + 1);
    if (mode < 2)
        vcen_rect(VC_CLUT, (int)(clut & 63) * 16, (int)(clut >> 6 & 511), mode == 0 ? 16 : 256, 1);
}
/* A primitive was drawn (by the renderer) over [x0, x1] x [y0, y1], inside its drawing area. */
void pc_vram_census_raster(int x0, int y0, int x1, int y1)
{
    if (vcen.on)
        vcen_rect(VC_RASTER, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}
int pc_vram_census_json(char* out, int size, int on, int reset)
{
    int n = 0;
    if (on >= 0)
        vcen.on = on;
    n += snprintf(out + n, (size_t)(size - n), "{\"on\":%d,\"frames\":%u,\"cells\":{", vcen.on, vcen.frames);
    for (int c = 0, first = 1; c < 128; c++) {
        int any = 0;
        for (int k = 0; k < VC_KINDS; k++)
            any |= vcen.cell[k][c] != 0;
        if (!any || n > size - 400)
            continue;
        n += snprintf(out + n, (size_t)(size - n), "%s\"%d,%d\":{", first ? "" : ",", c % 16 * 64, c / 16 * 64);
        first = 0;
        for (int k = 0, f = 1; k < VC_KINDS; k++)
            if (vcen.cell[k][c])
                n += snprintf(out + n, (size_t)(size - n), "%s\"%s\":%llu", f ? "" : ",", vcen_names[k], vcen.cell[k][c]), f = 0;
        n += snprintf(out + n, (size_t)(size - n), "}");
    }
    n += snprintf(out + n, (size_t)(size - n), "}}");
    if (reset) {
        int keep = vcen.on;
        memset(&vcen, 0, sizeof vcen);
        vcen.on = keep;
    }
    return n;
}
static void stream_event(int kind, int x, int y, int w, int h, const void* payload, size_t bytes)
{
    int head[7] = { kind, x, y, w, h, (int)stream.draws, (int)bytes };
    if (kind != 6) { /* (a fence, kind 6, is a read-back the renderer made for a read: it is counted and logged, not part of what the game wrote; the digest is of the writes, so two builds that write alike have one digest whatever they read back) */
        stream_mix(head, sizeof head);
        if (payload && bytes)
            stream_mix(payload, bytes);
    }
    ++stream.events, ++stream.total_events;
    {   /* ROOD_VRAM_STREAM_LOG=<path>: every event of the process, one line each (the frame, kind, rectangle, draws before it, the payload's hash): where two runs' digests part */
        static FILE* log __attribute__((section(".data.rood_host")));
        static int tried __attribute__((section(".data.rood_host")));
        if (!tried) {
            const char* path = getenv("ROOD_VRAM_STREAM_LOG");
            char name[512];
            tried = 1;
            if (path) {
                snprintf(name, sizeof name, "%s.%d", path, (int)getpid());
                log = fopen(name, "w");
            }
        }
        if (log) {
            unsigned long long fh = 1469598103934665603ull;
            for (size_t i = 0; payload && i < bytes; i++)
                fh = (fh ^ ((const unsigned char*)payload)[i]) * 1099511628211ull;
            fprintf(log, "%u %d %d %d %d %d %u %zu %016llx\n", stream.frames, kind, x, y, w, h, stream.draws, bytes, fh);
            fflush(log);
        }
    }
    if (vcen.on) {
        if (kind == 1)
            vcen_rect(VC_UPLOAD, x, y, w, h);
        else if (kind == 2)
            vcen_rect(VC_STORE, x, y, w, h);
        else if (kind == 3 || kind == 4)
            vcen_rect(VC_FILL, x, y, w, h);
        else if (kind == 5 && payload)
            vcen_rect(VC_COPY_DST, x, y, w, h), vcen_rect(VC_COPY_SRC, ((const int*)payload)[0], ((const int*)payload)[1], w, h);
    }
}
/* T5 S3: the points where the game waits for the renderer. The CPU rasterizer and StoreImage read VRAM that only the GPU target
 * holds (gpu_hw.c: stale rectangles); the call copies the target back (pc_hw_syncs counts the copies). Each one that copied is a
 * fence event in the stream (kind 6: why, and the rectangle), counted by cause: 1 StoreImage, 2 a sprite sampling the frame
 * being drawn. They are the whole of the game-to-renderer read-back (S1's feedback list: the display-area bands of the frame
 * readers in TITLE 22C.c, MENU5 4D8.c and MENU7 260.c are StoreImage's); what a renderer on its own thread waits at. */
extern int pc_hw_syncs __attribute__((weak));
static unsigned fence_counts[3] __attribute__((section(".data.rood_host")));
static int fence_syncs(void) { return &pc_hw_syncs ? pc_hw_syncs : 0; }
static void fence_event(int cause, int before, int x, int y, int w, int h)
{
    if (fence_syncs() == before)
        return;
    if (pc_jitter)
        pc_jitter(PC_JITTER_FENCE);
    stream_event(6, x, y, w, h, &cause, sizeof cause);
    ++fence_counts[cause];
}
/* Each vsync (pc_tick, before the RPC poll): the completed frame's digest is latched. */
static void vcheck_vsync(void);
static void stream_vsync(void)
{
    pc_hwc_post(); /* (the renderer has the records before the frame is presented: the present request posts them anyway) */
    vcheck_vsync();
    if (vcen.on)
        vcen_rect(VC_DISPLAY, display_x, display_y, display_w, display_h), ++vcen.frames;
    stream.last = stream.frame, stream.last_events = stream.events;
    stream.frame = 0xcbf29ce484222325ull, stream.events = 0, stream.draws = 0, ++stream.frames;
}
/* ROOD_VRAM_STREAM_CHECK=1 (T5 S1): the completeness proof of the stream. A shadow array is built from the stream's commands
 * alone (upload, fill, copy; the draws never touch it) and compared with native VRAM each vsync over every pixel the CPU has not
 * touched since a command last wrote it. `taint` marks the pixels the GPU read-back replaced (gpu_hw.c pixels_to_vram ->
 * pc_vram_taint), carried by copies and cleared by uploads and fills. An untainted pixel that differs is a writer the stream
 * does not have. Debug only: 3 MB and a pass of 512 K pixels a vsync. */
static struct {
    int on, rebase;
    uint16_t* shadow;
    uint8_t* taint;
    unsigned long long bad_pixels;
    unsigned checked, bad_frames;
    int first_bad[6];
    unsigned long long rv_miss, rv_raster, rv_miss_cell[128], rv_raster_cell[128]; /* renderer VRAM (gpu_hw.c, T5 S2) against the game's: untainted / tainted pixels that differ, summed over vsyncs */
    unsigned long long bad_cell[128]; /* by 64x64 block of VRAM: cell = y / 64 * 16 + x / 64 */
} vcheck __attribute__((section(".data.rood_host"))) = { -1 };
static int vcheck_on(void)
{
    if (vcheck.on < 0) {
        vcheck.on = getenv("ROOD_VRAM_STREAM_CHECK") != NULL;
        if (vcheck.on) {
            vcheck.shadow = malloc(sizeof vram);
            vcheck.taint = calloc(1024 * 512, 1);
            memcpy(vcheck.shadow, vram, sizeof vram);
        }
    }
    return vcheck.on;
}
/* A state restore or a GPU read-back replaced VRAM from outside the commands: the shadow starts again from it. */
void pc_vram_stream_rebase(void)
{
    if (vcheck_on())
        vcheck.rebase = 1;
}
/* gpu_hw.c copied the GPU target's picture into native VRAM (the stale areas): not the stream's, so not compared. */
void pc_vram_taint(int x0, int y0, int x1, int y1)
{
    if (vcheck_on())
        for (int y = y0; y < y1; y++)
            memset(vcheck.taint + y * 1024 + x0, 1, (size_t)(x1 - x0));
}
static void vcheck_write(int x, int y, int w, int h, const uint16_t* source, int kind, const int* extra)
{
    if (!vcheck_on() || w <= 0 || h <= 0)
        return;
    uint16_t* sh = vcheck.shadow;
    uint8_t* tn = vcheck.taint;
    if (kind == 1) { /* upload: the rectangle as given (pc_vram_transfer checked its bounds) */
        for (int row = 0; row < h; row++) {
            memcpy(sh + (y + row) * 1024 + x, source + (size_t)row * w, (size_t)w * 2);
            memset(tn + (y + row) * 1024 + x, 0, (size_t)w);
        }
    } else if (kind == 3 || kind == 4) { /* fill (ClearImage's aligned fill wraps; the exact rectangle clips) */
        uint16_t color = (uint16_t)((extra[0] >> 3) | ((extra[1] >> 3) << 5) | ((extra[2] >> 3) << 10));
        if (kind == 3) {
            x &= 0x3f0, y &= 0x1ff, w = ((w & 0x3ff) + 0xf) & ~0xf, h &= 0x1ff;
            for (int row = 0; row < h; row++)
                for (int col = 0; col < w; col++) {
                    int i = ((y + row) & 511) * 1024 + ((x + col) & 1023);
                    sh[i] = color, tn[i] = 0;
                }
        } else {
            int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > 1024 ? 1024 : x + w, y1 = y + h > 512 ? 512 : y + h;
            for (int row = y0; row < y1; row++)
                for (int col = x0; col < x1; col++)
                    sh[row * 1024 + col] = color, tn[row * 1024 + col] = 0;
        }
    }
}
static void vcheck_copy(unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned w, unsigned h)
{
    if (!vcheck_on())
        return;
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; x += 16) {
            uint16_t block[16];
            uint8_t taints[16];
            unsigned count = w - x < 16 ? w - x : 16;
            for (unsigned j = 0; j < count; ++j) {
                int i = ((sy + y) & 511) * 1024 + ((sx + x + j) & 1023);
                block[j] = vcheck.shadow[i], taints[j] = vcheck.taint[i];
            }
            for (unsigned j = 0; j < count; ++j) {
                int i = ((dy + y) & 511) * 1024 + ((dx + x + j) & 1023);
                if (!mask_check || !(vcheck.shadow[i] & 0x8000))
                    vcheck.shadow[i] = block[j] | (mask_set ? 0x8000 : 0), vcheck.taint[i] = taints[j];
            }
        }
}
static void vcheck_vsync(void)
{
    if (!vcheck_on())
        return;
    if (vcheck.rebase) { /* a restore: start again from native VRAM */
        memcpy(vcheck.shadow, vram, sizeof vram);
        memset(vcheck.taint, 0, 1024 * 512);
        vcheck.rebase = 0;
        return;
    }
    int box[4] = { 1024, 512, -1, -1 };
    unsigned long long bad = 0;
    for (int i = 0; i < 1024 * 512; i++)
        if (!vcheck.taint[i] && vcheck.shadow[i] != vram[i]) {
            ++bad;
            int x = i & 1023, y = i >> 10;
            ++vcheck.bad_cell[y / 64 * 16 + x / 64];
            box[0] = x < box[0] ? x : box[0], box[1] = y < box[1] ? y : box[1];
            box[2] = x > box[2] ? x : box[2], box[3] = y > box[3] ? y : box[3];
        }
    {
        extern const uint16_t* pc_hw_renderer_vram(void) __attribute__((weak));
        const uint16_t* rv = pc_hw_renderer_vram ? pc_hw_renderer_vram() : NULL;
        for (int i = 0; rv && i < 1024 * 512; i++)
            if (rv[i] != vram[i]) {
                int c = (i >> 10) / 64 * 16 + (i & 1023) / 64;
                if (vcheck.taint[i])
                    ++vcheck.rv_raster, ++vcheck.rv_raster_cell[c];
                else
                    ++vcheck.rv_miss, ++vcheck.rv_miss_cell[c];
            }
    }
    ++vcheck.checked;
    if (bad) {
        if (!vcheck.bad_frames++)
            vcheck.first_bad[0] = (int)stream.frames, memcpy(vcheck.first_bad + 1, box, sizeof box), vcheck.first_bad[5] = (int)bad;
        vcheck.bad_pixels += bad;
        memcpy(vcheck.shadow, vram, sizeof vram); /* one report per cause, not one per frame after it */
    }
    const char* path = getenv("ROOD_VRAM_STREAM_CHECK_OUT"); /* the cell tables, rewritten every 60 vsyncs (a lane reads the last) */
    FILE* f = path && vcheck.checked % 60 == 0 ? fopen(path, "w") : NULL;
    if (f) {
        fprintf(f, "{\"frames\":%u,\"bad_pixels\":%llu,\"bad_cell\":{", vcheck.checked, vcheck.bad_pixels);
        for (int c = 0, n = 0; c < 128; c++)
            if (vcheck.bad_cell[c])
                fprintf(f, "%s\"%d,%d\":%llu", n++ ? "," : "", c % 16 * 64, c / 16 * 64, vcheck.bad_cell[c]);
        fprintf(f, "},\"rv_miss\":%llu,\"rv_raster\":%llu,\"rv_miss_cell\":{", vcheck.rv_miss, vcheck.rv_raster);
        for (int c = 0, n = 0; c < 128; c++)
            if (vcheck.rv_miss_cell[c])
                fprintf(f, "%s\"%d,%d\":%llu", n++ ? "," : "", c % 16 * 64, c / 16 * 64, vcheck.rv_miss_cell[c]);
        fprintf(f, "},\"rv_raster_cell\":{");
        for (int c = 0, n = 0; c < 128; c++)
            if (vcheck.rv_raster_cell[c])
                fprintf(f, "%s\"%d,%d\":%llu", n++ ? "," : "", c % 16 * 64, c / 16 * 64, vcheck.rv_raster_cell[c]);
        fprintf(f, "}}\n");
        fclose(f);
    }
}
/* RPC vram_stream {reset}: the running digest starts again (a route whose first frames are not exact, a game reaching its scene unpaced, takes its digest from where it is). */
void pc_vram_stream_reset(void)
{
    stream.total = 0xcbf29ce484222325ull, stream.total_events = 0;
}
/* RPC vram_stream: {frames, frame: {events, hash} (the last completed frame), total: {events, hash}}. */
int pc_vram_stream_json(char* out, int size)
{
    extern struct { unsigned long long replays, rewritten, unwritten, fell_back, valid, invalid_overlap, invalid_count; } pc_hw_stats __attribute__((weak));
    int n = snprintf(out, (size_t)size,
        "{\"frames\":%u,\"frame\":{\"events\":%u,\"hash\":\"%016llx\"},\"total\":{\"events\":%u,\"hash\":\"%016llx\"},"
        "\"check\":{\"on\":%d,\"frames\":%u,\"bad_frames\":%u,\"bad_pixels\":%llu,\"first_bad\":[%d,%d,%d,%d,%d,%d]},"
        "\"fences\":{\"store\":%u,\"texture\":%u}}",
        stream.frames, stream.last_events, (unsigned long long)stream.last, stream.total_events,
        (unsigned long long)stream.total, vcheck.on > 0, vcheck.checked, vcheck.bad_frames, vcheck.bad_pixels,
        vcheck.first_bad[0], vcheck.first_bad[1], vcheck.first_bad[2], vcheck.first_bad[3], vcheck.first_bad[4],
        vcheck.first_bad[5], fence_counts[1], fence_counts[2]);
    if (n > 2 && &pc_hw_stats && n + 280 < size) { /* + "renderer": the replay statistics (gpu_hw.c) */
        n -= 1;
        n += snprintf(out + n, (size_t)(size - n),
            ",\"renderer\":{\"replays\":%llu,\"rewritten\":%llu,\"unwritten\":%llu,\"fell_back\":%llu,"
            "\"valid\":%llu,\"invalid_overlap\":%llu,\"invalid_count\":%llu}}",
            pc_hw_stats.replays, pc_hw_stats.rewritten, pc_hw_stats.unwritten, pc_hw_stats.fell_back,
            pc_hw_stats.valid, pc_hw_stats.invalid_overlap, pc_hw_stats.invalid_count);
    }
    if (n > 2 && pc_jitter_json && n + 160 < size) { /* + "jitter" (render_jitter.c) */
        n -= 1;
        n += pc_jitter_json(out + n, size - n);
        out[n++] = '}', out[n] = 0;
    }
    return n;
}
void pc_vram_transfer(int x, int y, int w, int h, void* data, int write)
{
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > 1024 || y + h > 512) {
        fprintf(stderr, "VRAM transfer: %d,%d %dx%d\n", x, y, w, h);
        pc_unimplemented("VRAM rectangle out of bounds");
    }
    stream_event(write ? 1 : 2, x, y, w, h, write ? data : NULL, write ? (size_t)w * (size_t)h * 2 : 0);
    if (write)
        vcheck_write(x, y, w, h, data, 1, NULL);
    if (!write && pc_hw_sync_store) {
        int before = fence_syncs();
        pc_hwc_fence_store(x, y, w, h); /* StoreImage: the renderer has everything before it, and the pixels are in VRAM when it returns */
        fence_event(1, before, x, y, w, h);
    }
    if (write && pc_hw_before_write) {
        pc_hwc_before_write(x, y, w, h);
    }
    for (int row = 0; row < h; row++) {
        void* vp = vram + (y + row) * 1024 + x;
        void* dp = (uint16_t*)data + row * w;
        if (write)
            memcpy(vp, dp, (size_t)w * 2);
        else
            memcpy(dp, vp, (size_t)w * 2);
    }
    if (write) {
        vram_note_written(x, y, w, h);
        pc_hwc_vram_upload(x, y, w, h);
    }
}
/* GPU fill (GP0 0x02) as the hardware does it: x is rounded down to 16
 * pixels, the width rounded up to 16, both wrapped to VRAM (1024 x 512).
 * Out-of-range requests are logged once, not rejected. */
void pc_vram_clear(int x, int y, int w, int h, int r, int g, int b)
{
    stream_event(3, x, y, w, h, (int[]) { r, g, b }, 3 * sizeof(int));
    vcheck_write(x, y, w, h, NULL, 3, (int[]) { r, g, b });
    static int reported;
    if ((x < 0 || y < 0 || w < 0 || h < 0 || x + w > 1024 || y + h > 512) && !reported++)
        fprintf(stderr, "[gpu] VRAM fill %d,%d %dx%d wraps (hardware fill semantics)\n", x, y,
            w, h);
    uint16_t color = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
    x &= 0x3f0;
    y &= 0x1ff;
    w = ((w & 0x3ff) + 0xf) & ~0xf;
    h &= 0x1ff;
    uint64_t cost = pc_cost_begin();
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            vram[((y + row) & 511) * 1024 + ((x + col) & 1023)] = color;
    pc_cost_end(PC_COST_RASTER, cost);
    /* Mirror as up to four in-range pieces of the wrapped rectangle. */
    for (int part = 0; part < 4; part++) {
        int px = part & 1 ? 0 : x, py = part & 2 ? 0 : y;
        int pw = part & 1 ? x + w - 1024 : (x + w > 1024 ? 1024 - x : w);
        int ph = part & 2 ? y + h - 512 : (y + h > 512 ? 512 - y : h);
        if (pw > 0 && ph > 0) {
            vram_note_written(px, py, pw, ph);
            pc_hwc_mark(px, py, px + pw - 1, py + ph - 1);
            pc_hwc_vram_fill(px, py, pw, ph, r, g, b);
        }
    }
}
/* Flat rectangle (GP0 0x60) at its exact size, clipped to VRAM (libgpu's
 * ClearImage draws one for areas that aren't 64-pixel aligned). */
void pc_vram_rect(int x, int y, int w, int h, int r, int g, int b)
{
    stream_event(4, x, y, w, h, (int[]) { r, g, b }, 3 * sizeof(int));
    vcheck_write(x, y, w, h, NULL, 4, (int[]) { r, g, b });
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > 1024 ? 1024 : x + w, y1 = y + h > 512 ? 512 : y + h;
    if (x1 <= x0 || y1 <= y0)
        return;
    uint16_t color = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
    uint64_t cost = pc_cost_begin();
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            vram[row * 1024 + col] = color;
    pc_cost_end(PC_COST_RASTER, cost);
    vram_note_written(x0, y0, x1 - x0, y1 - y0);
    pc_hwc_mark(x0, y0, x1 - 1, y1 - 1);
    pc_hwc_vram_fill(x0, y0, x1 - x0, y1 - y0, r, g, b);
}
static void copy_file(const char* from, const char* to)
{
    FILE *src = fopen(from, "rb"), *dst = fopen(to, "wb");
    if (src && dst) {
        int c;
        while ((c = fgetc(src)) != EOF)
            fputc(c, dst);
    }
    if (src)
        fclose(src);
    if (dst)
        fclose(dst);
}
/* Displayed pixel (x, y) as 8-bit RGB: BGR555, or in 24-bit display mode
 * (PutDispEnv isrgb24, the intro movie) bytes R, G, B packed across VRAM
 * halfwords from the display's left edge. */
int pc_display_rgb24;
static void display_pixel(int x, int y, unsigned char* rgb)
{
    const uint16_t* row = vram + ((y + display_y) & 511) * 1024;
    if (pc_display_rgb24) {
        int byte = display_x * 2 + x * 3;
        for (int i = 0; i < 3; i++, byte++) {
            uint16_t p = row[(byte >> 1) & 1023];
            rgb[i] = byte & 1 ? p >> 8 : p & 0xFF;
        }
        return;
    }
    uint16_t p = row[(x + display_x) & 1023];
    rgb[0] = ((p & 31) * 255) / 31;
    rgb[1] = (((p >> 5) & 31) * 255) / 31;
    rgb[2] = (((p >> 10) & 31) * 255) / 31;
}
/* loading.ppm is written when something copies it (the checkpoints, the
 * ROOD_SNAP_EVERY and frame-100/400 copies, the first frame), else every
 * 64th presented frame (by the frame count, not the clock: the game's own
 * path buffers rotate with each write, saved state), and at exit when a
 * newer frame was presented since: writing it for every frame cost ~0.15 ms
 * a vsync. */
static int display_unwritten __attribute__((section(".data.rood_host"))) = 0; /* host: a frame not in the file yet */
static void write_display(void)
{
    uint64_t cost = pc_cost_begin();
    FILE* f = fopen(pc_out_path("loading.ppm"), "wb");
    if (!f) {
        perror("framebuffer");
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", display_w, display_h);
    for (int y = 0; y < display_h; y++) {
        unsigned char row[1024 * 3];
        for (int x = 0; x < display_w && x < 1024; x++)
            display_pixel(x, y, row + x * 3);
        fwrite(row, 3, display_w < 1024 ? display_w : 1024, f);
    }
    fclose(f);
    display_unwritten = 0;
    pc_cost_end(PC_COST_DEBUG_PPM, cost);
    pc_cost_count(PC_COUNT_DEBUG_PPM, 1);
}
static void write_display_at_exit(void)
{
    if (display_unwritten)
        write_display();
}
/* A displayed frame (PutDispEnv, SetDispMask(1), the checkpoints). */
void pc_present(void)
{
    static int exit_hook __attribute__((section(".data.rood_host"))) = 0; /* host */
    if (!exit_hook) {
        exit_hook = 1;
        atexit(write_display_at_exit);
    }
    /* Debug aid: ROOD_SNAP_EVERY=N copies the displayed frame to
     * snap-<battle frame>.ppm in ROOD_OUT (pc/build/boot) every N battle frames. */
    static int snap_every __attribute__((section(".data.rood_host"))) = -1; /* host setting */
    static unsigned snapped;
    if (snap_every < 0)
        snap_every = getenv("ROOD_SNAP_EVERY") ? atoi(getenv("ROOD_SNAP_EVERY")) : 0;
    int snap = snap_every > 0
        && (battle_frames ? battle_frames % snap_every == 0 && snapped != battle_frames : frames % snap_every == 0);
    display_unwritten = 1;
    if (snap || frames % 64 == 0 || frames == 100 || frames == 400 || stop_at_loading)
        write_display();
    if (snap_every > 0 && battle_frames && battle_frames % snap_every == 0
        && snapped != battle_frames) {
        char name[24];
        snapped = battle_frames;
        snprintf(name, sizeof name, "snap-%05u.ppm", battle_frames);
        copy_file(pc_out_path("loading.ppm"), pc_out_path(name));
    } else if (snap_every > 0 && !battle_frames && frames % snap_every == 0) {
        /* Before the battle loop (title, load screen) count presented frames. */
        char name[24];
        snprintf(name, sizeof name, "title-%05u.ppm", frames);
        copy_file(pc_out_path("loading.ppm"), pc_out_path(name));
    }
    if (frames == 100 || frames == 400) {
        char name[24];
        snprintf(name, sizeof name, "frame-%u.ppm", frames);
        copy_file(pc_out_path("loading.ppm"), pc_out_path(name));
    }
    if (frames++ == 0)
        fprintf(stderr, "[boot] original game submitted display frame: %s\n", pc_out_path("loading.ppm"));
    if (stop_at_loading) {
        pc_log("loading-screen checkpoint reached (not title boot)");
        exit(0);
    }
}
/* A displayed frame that is copied or renamed right after (the checkpoints). */
static void pc_present_now(void)
{
    pc_present();
    if (display_unwritten)
        write_display();
}
/* Debug aid: `kill -USR1 <pid>` prints the native call stack; resolve the
 * addresses with pc/tools/where.sh. */
/* ROOD_STACK_SELFTEST=1: libc calls made on the game's stack, from the game's entry and from the host tick the game calls (the
 * string, number and sort routines the host uses: SIMD versions that fault when the stack is misaligned). */
static void stack_selftest(const char* where)
{
    char text[64] = "seed:2500:350", number[32];
    volatile unsigned long long sink = 0;
    sink += (unsigned long long)(uintptr_t)strchr(text, ':') + strlen(text) + strtoull("123", NULL, 10);
    snprintf(number, sizeof number, "%f %d", 1.5, (int)sink);
    float v[4] = { 3, 1, 2, 4 };
    qsort(v, 4, sizeof v[0], (int (*)(const void*, const void*))strcmp);
    memcpy(text, number, 8);
    fprintf(stderr, "[stack] selftest at %s: rsp %% 16 = %lu, ok (%s)\n", where, (unsigned long)((uintptr_t)__builtin_frame_address(0) % 16), number);
}
static void pc_dump_stack(int sig)
{
    void* frames[64];
#ifdef _WIN32
    int n = CaptureStackBackTrace(0, 62, frames, NULL);
#else
    /* The frame-pointer chain (the build keeps frame pointers), each link checked: backtrace() unwinds by CFI, which the game's
     * own stack (mapped below 2 GB, run_game_on_low_stack) does not describe past its first frame: it ran off the stack and
     * the report of a stop (pc_unimplemented) died of a segmentation fault instead of printing. */
    int n = 0;
    void** fp = __builtin_frame_address(0);
    while (n < 64 && fp && ((uintptr_t)fp & 7) == 0) {
        frames[n++] = fp[1];
        void** next = fp[0];
        if (next <= fp || (uintptr_t)next - (uintptr_t)fp > (1u << 20))
            break;
        fp = next;
    }
#endif
    char line[32];
    (void)sig;
    write(2, "[stack]", 7);
    for (int i = 0; i < n; i++) {
        int len = snprintf(line, sizeof line, " %p", frames[i]);
        write(2, line, len);
    }
    write(2, "\n", 1);
}
/* Every exit() the process makes says so on stderr (a quit, a checkpoint, an error exit): a game that is
 * gone without a [crash] line then either took a signal that cannot be caught (SIGKILL, SIGTERM and SIGINT:
 * SDL's own handlers are off) or left through _exit/abort. */
static void pc_exit_note(void) { fputs("[exit] process ending (exit)\n", stderr); }
#ifndef _WIN32
/* A signal from outside that ends the game (SIGTERM, SIGINT, SIGHUP, SIGPIPE) names its sender's pid on
 * stderr, then takes its default action: the log says who stopped the game. */
static void pc_signal_note(int sig, siginfo_t* info, void* context)
{
    (void)context;
    char line[96];
    int len = snprintf(line, sizeof line, "[exit] signal %d from pid %d (uid %d, code %d)\n", sig,
        (int)info->si_pid, (int)info->si_uid, info->si_code);
    write(2, line, len);
    signal(sig, SIG_DFL);
    raise(sig);
}
static void pc_signal_notes(void)
{
    struct sigaction action = {0};
    action.sa_sigaction = pc_signal_note;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    static const int signals[] = {SIGTERM, SIGINT, SIGHUP, SIGPIPE};
    for (unsigned i = 0; i < sizeof signals / sizeof *signals; i++)
        sigaction(signals[i], &action, NULL);
}
#endif
/* Fatal signals print the native call stack (resolve it with
 * pc/tools/where.sh resolve) and the faulting battle frame, then re-raise. */
static void pc_crash(int sig)
{
    char line[96];
    int len = snprintf(line, sizeof line, "[crash] signal %d (%s) at battle frame %u\n", sig,
        sig == SIGSEGV ? "segmentation fault" : sig == SIGFPE ? "arithmetic" : "fatal",
        battle_frames);
    write(2, line, len);
    pc_dump_stack(sig);
    signal(sig, SIG_DFL);
    raise(sig);
}
/* psx_set() (pc/src/psx_types.h) was given a host pointer with no 4-byte
 * guest address: a PS1-layout field cannot hold it. Stop with the stack. */
__attribute__((noreturn)) void pc_psx_bad_pointer(const void* pointer)
{
    fprintf(stderr, "[lp64] %p has no guest address (psx_set on a PS1-layout field)\n", pointer);
    abort(); /* pc_crash prints the stack */
}
/* The game's stack on 64 bits. The PS1's stack was guest RAM, and game code
 * keeps addresses of its locals in 4-byte fields (psx_ptr members, guest
 * words, effect spawn records built on the stack), so the game thread runs on
 * a stack mapped below 2 GB (MAP_32BIT), where every address fits 32 bits. */
#ifdef _WIN32
/* Windows cannot place a thread's or a fiber's stack, so the main thread
 * switches to one allocated below 2 GB: rood_call_on_stack(top, fn) calls
 * fn with rsp = top. Its unwind info (frame pointer rbp) lets stack walks
 * and exception dispatch cross back to the original stack. */
extern void rood_call_on_stack(void* top, void (*fn)(void));
__asm__(".text\n"
        ".globl rood_call_on_stack\n"
        ".def rood_call_on_stack; .scl 2; .type 32; .endef\n"
        ".seh_proc rood_call_on_stack\n"
        "rood_call_on_stack:\n"
        "    pushq %rbp\n"
        "    .seh_pushreg %rbp\n"
        "    movq %rsp, %rbp\n"
        "    .seh_setframe %rbp, 0\n"
        "    .seh_endprologue\n"
        "    movq %rcx, %rsp\n"
        "    subq $32, %rsp\n" /* the callee's register home area */
        "    callq *%rdx\n"
        "    movq %rbp, %rsp\n"
        "    popq %rbp\n"
        "    ret\n"
        ".seh_endproc\n");
/* Fatal exceptions in the game's own code print [crash] and the stack like
 * the Linux signal handler. A vectored handler runs before the frame-based
 * search (which cannot leave the switched stack); exceptions elsewhere (a
 * driver's own handled ones) are left alone. */
extern char __ImageBase[];
static LONG WINAPI pc_exception(EXCEPTION_POINTERS* info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    uintptr_t at = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION
        && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION
        && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    if (at < (uintptr_t)__ImageBase || at >= ROOD_GUEST_RAM_BASE)
        return EXCEPTION_CONTINUE_SEARCH;
    char line[160];
    int len = snprintf(line, sizeof line,
        "[crash] exception 0x%08lx (%s) at %p, address %p, battle frame %u\n", (unsigned long)code,
        code == EXCEPTION_ACCESS_VIOLATION ? "access violation" : code == EXCEPTION_INT_DIVIDE_BY_ZERO ? "arithmetic" : "fatal",
        (void*)at, code == EXCEPTION_ACCESS_VIOLATION ? (void*)info->ExceptionRecord->ExceptionInformation[1] : NULL,
        battle_frames);
    write(2, line, len);
    /* The faulting frame first: the backtrace below starts in this handler. */
    pc_dump_stack(0);
    _exit(3);
}
/* Reserved in main before the window: graphics drivers reserve much of the
 * address space below 2 GB when they load. At Linux's fixed address when it
 * is free: a save state (snapshot.c) holds the game's stack and resumes it in
 * a new process, where every saved frame pointer and return address must mean
 * the same; elsewhere (taken) only resuming in a new process is lost. Host
 * statics (.data.rood_host): a snapshot of this unit's state must not carry
 * another process's. */
#define GAME_STACK_BASE 0x60000000u
static char* game_stack __attribute__((section(".data.rood_host"))) = NULL;
static int game_stack_fixed __attribute__((section(".data.rood_host"))) = 0;
static const size_t game_stack_size = 64u << 20;
static int reserve_game_stack(void)
{
    game_stack = VirtualAlloc((void*)(uintptr_t)GAME_STACK_BASE, game_stack_size, MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);
    game_stack_fixed = game_stack == (char*)(uintptr_t)GAME_STACK_BASE;
    if (!game_stack_fixed)
        fprintf(stderr, "[snapshot] game stack: 0x%x is taken; save states load only in this session\n",
            GAME_STACK_BASE);
    for (uintptr_t at = 0x02000000; !game_stack && at + game_stack_size <= 0x7f000000; at += 1u << 20)
        game_stack = VirtualAlloc((void*)at, game_stack_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return game_stack != NULL;
}
/* snapshot.c: the stack's range. Unlike Linux, a stack elsewhere still saves
 * and loads in this process; a file made there names its address, which a
 * new process (at another address) refuses as a range it does not have. */
char* pc_game_stack(size_t* size)
{
    *size = game_stack_size;
    return game_stack;
}
/* Stack bounds in the thread information block, for code that checks them
 * (exception dispatch, drivers): the game stack's, while it runs. */
void pc_game_stack_entered(void)
{
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    tib->StackBase = game_stack + game_stack_size;
    tib->StackLimit = game_stack;
}
static void run_game_on_low_stack(void)
{
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    void *base = tib->StackBase, *limit = tib->StackLimit;
    pc_game_stack_entered();
    rood_call_on_stack(game_stack + game_stack_size - 64, vs_main_exec);
    tib->StackBase = base;
    tib->StackLimit = limit;
}
#else
/* The switch between the host and the game's stack: host state (the registers and stack addresses of this process), not the
 * game's, so not in a save state or its hash. */
static ucontext_t game_context __attribute__((section(".data.rood_host"))), host_context __attribute__((section(".data.rood_host")));
static void run_game(void)
{
    if (getenv("ROOD_STACK_SELFTEST"))
        stack_selftest("game entry");
    vs_main_exec();
}
/* The game's stack at a fixed address (both word sizes), reserved in main
 * before the window: a save state (snapshot.c) holds the game's stack and
 * resumes it in a new process, where every saved frame pointer and return
 * address must mean the same. Elsewhere (taken) only snapshots are lost. */
#define GAME_STACK_BASE 0x60000000u
#define GAME_STACK_SIZE (64u << 20)
static char* game_stack;
static int game_stack_fixed;
static void reserve_game_stack(void)
{
    void* p = mmap((void*)(uintptr_t)GAME_STACK_BASE, GAME_STACK_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != MAP_FAILED && (uintptr_t)p == GAME_STACK_BASE) {
        game_stack = p;
        game_stack_fixed = 1;
        return;
    }
    if (p != MAP_FAILED)
        munmap(p, GAME_STACK_SIZE); /* an old kernel's hint, not the address */
    fprintf(stderr, "[snapshot] game stack: 0x%x is taken; save states are off\n", GAME_STACK_BASE);
    FILE* maps = fopen("/proc/self/maps", "r"); /* by what */
    char line[512];
    while (maps && fgets(line, sizeof line, maps)) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && lo < GAME_STACK_BASE + GAME_STACK_SIZE && hi > GAME_STACK_BASE)
            fprintf(stderr, "[snapshot]   %s", line);
    }
    if (maps)
        fclose(maps);
    p = mmap(NULL, GAME_STACK_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (sizeof(void*) == 8 ? MAP_32BIT : 0), -1, 0);
    if (p == MAP_FAILED || (uintptr_t)p + GAME_STACK_SIZE > ROOD_GUEST_RAM_BASE) {
        perror("game stack below 2 GB");
        exit(1);
    }
    game_stack = p;
}
/* snapshot.c: the stack's range (NULL when it is not at its fixed address). */
char* pc_game_stack(size_t* size)
{
    *size = GAME_STACK_SIZE;
    return game_stack_fixed ? game_stack : NULL;
}
void pc_game_stack_entered(void) { } /* Windows keeps stack bounds per thread */
static void run_game_on_low_stack(void)
{
    getcontext(&game_context);
    game_context.uc_stack.ss_sp = game_stack;
    game_context.uc_stack.ss_size = GAME_STACK_SIZE;
    game_context.uc_link = &host_context;
    makecontext(&game_context, run_game, 0);
#ifdef __x86_64__
    /* The game starts with the callee-saved registers the host had (a host stack address in rbp, host values in r12-r15). They
     * would travel into the game's frames and into the jump buffers the game takes (pc_title_jump, pc_battle_jump: their
     * frame-pointer word), which are game state: a save state then differs from run to run by host addresses. The game
     * starts from zeroed ones. */
    game_context.uc_mcontext.gregs[REG_RBP] = game_context.uc_mcontext.gregs[REG_R12] = 0;
    game_context.uc_mcontext.gregs[REG_R13] = game_context.uc_mcontext.gregs[REG_R14] = game_context.uc_mcontext.gregs[REG_R15] = 0;
#endif
    swapcontext(&host_context, &game_context);
}
#endif
/* Guest memory at its PS1 addresses (RAM, scratchpad, the I/O page); NULL
 * when a range is taken. */
/* See psx_types.h: where the game's reads through the original's NULLs land. */
unsigned char rood_null_page[0x10000] __attribute__((aligned(16)));
/* The port arena's place, for game/common/port_arena.zig (the one definition is psx_types.h's). */
const uintptr_t rood_arena_base = ROOD_ARENA_BASE;
const unsigned rood_arena_size = ROOD_ARENA_SIZE;
const uintptr_t rood_port_base = ROOD_PORT_BASE;
const unsigned rood_port_size = ROOD_PORT_SIZE;

static void* map_guest(uintptr_t address, size_t size)
{
#ifdef _WIN32
    return VirtualAlloc((void*)address, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* p = mmap((void*)address, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}
#ifndef _WIN32
/* The scratchpad and the I/O page are part of the image (build_boot.py links
 * this section at 0x1f800000): the kernel starts the heap (brk) at a random
 * point up to 1 GB past the image's end (x86-64), and the pinned game region
 * (snapshot_layout.py) ends a little over 100 MB below 0x1f800000, so the
 * heap the C and C++ runtimes allocate before main covered the pages about
 * once in 5000 launches ("guest mapping: File exists"). Ending the image at
 * them puts the heap above them. */
__asm__(".section rood_guest_pages,\"aw\",@nobits\n"
        ".balign 4096\n"
        "pc_guest_pages: .space 0x2000\n"
        ".previous");
extern char pc_guest_pages[];
#endif
int main(int argc, char** argv)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, pc_exception);
    setvbuf(stderr, NULL, _IONBF, 0); /* the log, as on Linux */
#else
    signal(SIGUSR1, pc_dump_stack);
    signal(SIGBUS, pc_crash);
    pc_signal_notes();
#endif
    atexit(pc_exit_note);
    signal(SIGSEGV, pc_crash);
    signal(SIGFPE, pc_crash);
    signal(SIGILL, pc_crash);
    signal(SIGABRT, pc_crash); /* e.g. stack protector failures */
    int window = 0, fast = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--loading-checkpoint"))
            stop_at_loading = 1;
        else if (!strcmp(argv[i], "--title-checkpoint"))
            stop_at_title = 1;
        else if (!strcmp(argv[i], "--opening-checkpoint"))
            stop_at_opening = 1;
        else if (!strcmp(argv[i], "--battle-frame-checkpoint")) {
            char* end;
            if (++i == argc) {
                fprintf(stderr, "--battle-frame-checkpoint requires a frame number\n");
                return 1;
            }
            unsigned long value = strtoul(argv[i], &end, 10);
            if (*end || !value || value > 1000000) {
                fprintf(stderr, "invalid battle checkpoint frame: %s\n", argv[i]);
                return 1;
            }
            stop_at_battle_frame = value;
        } else if (!strcmp(argv[i], "--skip-movies"))
            pc_skip_movies = 1;
        else if (!strcmp(argv[i], "--window"))
            window = 1;
        else if (!strcmp(argv[i], "--fast"))
            fast = 1;
        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    /* Guest memory first: the window's graphics driver (Vulkan and its
     * layers especially) maps memory of its own, which must not land in the
     * guest's fixed ranges. */
    void* ram = map_guest(ROOD_GUEST_RAM_BASE, ROOD_GUEST_RAM_SIZE);
    void* arenas = map_guest(ROOD_ARENA_BASE, ROOD_ARENA_SIZE);
#ifdef _WIN32
    /* Windows reserves in 64 KB granules: the scratchpad and the I/O page
     * (below) are one allocation. */
    void* scratch = map_guest(0x1f800000, 0x2000);
    void* io = scratch;
#else
    if (pc_guest_pages != (char*)(uintptr_t)0x1f800000) {
        fprintf(stderr, "guest mapping: rood_guest_pages is linked at %p, not 0x1f800000 (build_boot.py)\n",
            (void*)pc_guest_pages);
        return 1;
    }
    void* scratch = pc_guest_pages;
    /* I/O page: the sound driver writes SPU registers (0x1F801C00..) here;
     * pc/src/audio_spu.cpp synchronises them into the software SPU. */
    void* io = pc_guest_pages + 0x1000;
#endif
    if (!ram || !arenas || !scratch || !io) {
        perror("guest mapping");
        return 1;
    }
#ifdef _WIN32
    if (!reserve_game_stack()) {
        fprintf(stderr, "game stack below 2 GB: no free range\n");
        return 1;
    }
#else
    reserve_game_stack();
#endif
    if (pc_engine_start)
        pc_engine_start(); /* the engine library, once the fixed ranges are taken */
    pc_window_init(window, fast); /* loads the settings (dev_overlay.cpp) */
    if (&pc_skip_intro && pc_skip_intro) {
        intro_skipped_by_setting = pc_skip_movies ? 1 : 2;
        pc_skip_movies = pc_skip_logos = 1;
        pc_log("skip_intro: starting at the title (no logos, no intro movie)");
    }
    if (pc_lua_init)
        pc_lua_init(); /* after the settings (their plugin order and grants), on this thread */
    pc_audio_output_start();
    FILE* f = fopen("data/SLUS_010.40", "rb");
    if (!f) {
        perror("original executable");
        return 1;
    }
    unsigned char header[2048];
    if (fread(header, 1, sizeof header, f) != sizeof header
        || memcmp(header, "PS-X EXE", 8))
        return 1;
    uint32_t address, size;
    memcpy(&address, header + 0x18, 4);
    memcpy(&size, header + 0x1c, 4);
    if (address < ROOD_GUEST_RAM_BASE || address >= (ROOD_GUEST_RAM_BASE + ROOD_GUEST_RAM_SIZE) || size > (ROOD_GUEST_RAM_BASE + ROOD_GUEST_RAM_SIZE) - address)
        return 1;
    if (fread((void*)(uintptr_t)address, 1, size, f) != size)
        return 1;
    fclose(f);
    pc_log(sizeof(void*) == 8
            ? "64-bit native game startup; original RAM data loaded, MIPS code is non-executable"
            : "32-bit native game startup; original RAM data loaded, MIPS code is non-executable");
    pc_log(window ? "backend: keyboard/gamepad input, file-backed memory cards (pc/save)"
                  : "backend: disconnected controllers, file-backed memory cards (pc/save)");
    pc_overlay_statics_init();
    if (pc_snapshot_resume_if_requested)
        pc_snapshot_resume_if_requested(); /* ROOD_SNAPSHOT: does not return (snapshot.c) */
    run_game_on_low_stack();
    return 0;
}
int pc_disc_read(int lba, unsigned int size, void* dest)
{
    if (lba < 0)
        pc_unimplemented("negative disc LBA");
    FILE* f = fopen("disks/SLUS-01040.bin", "rb");
    if (!f) {
        perror("disc image");
        exit(1);
    }
    unsigned char sector[2352];
    int first = lba;
    fprintf(stderr, "[disc] LBA %d, %u bytes -> %p\n", lba, size, dest);
    while (size) {
        if (fseek(f, (long)lba * 2352, SEEK_SET)
            || fread(sector, 1, sizeof sector, f) != sizeof sector)
            pc_unimplemented("disc read failed");
        unsigned int offset = sector[15] == 2 ? 24 : 16;
        if (sector[15] != 1 && sector[15] != 2)
            pc_unimplemented("unsupported sector format");
        unsigned int n = size < 2048 ? size : 2048;
        memcpy(dest, sector + offset, n);
        dest = (unsigned char*)dest + n;
        size -= n;
        ++lba;
    }
    fclose(f);
    pc_overlay_statics_loaded(first);
    return 1;
}
extern int pc_gte_centre __attribute__((weak)); /* PsyX_GTE.cpp: widescreen narrows about it */
/* The display area's size (the picture shows it: the field's 320 x 224; the HUD layer's
 * coordinates, pc/src/hud/hud_draw.cpp). */
void pc_display_size(int* w, int* h)
{
    *w = display_w;
    *h = display_h;
}
/* The stream digest (T4 slice S0, pc/docs/plans/2026-11-12-t4-survey.md): FNV-1a over every word the walk hands to
 * commands(), a record for each native marker draw (list, run, clip, offset) and each draw or display environment set,
 * closed at every vsync into a ring. Two builds that describe each frame the same way give the same digests,
 * whatever builds the description (packets, items, native lists): the meter of the drawing-list migration. Read over
 * the dev RPC `stream` (dev_rpc.c); `on` 0 stops hashing. */
enum { stream_ring_size = 256 };
typedef struct {
    unsigned vsync, words, records;
    uint64_t digest;
} stream_frame;
/* (the digest and the adopted table are the host's: not saved state, which would restore another process's heap pointers) */
#define PC_HOST __attribute__((section(".data.rood_host")))
static stream_frame stream_ring[stream_ring_size] PC_HOST;
static unsigned stream_closed PC_HOST;
static uint64_t stream_acc PC_HOST = 1469598103934665603ull, stream_chain PC_HOST = 1469598103934665603ull;
static unsigned stream_words PC_HOST, stream_records PC_HOST;
/* The draw stream's digest (RPC stream, build_compare --stream): a test meter, off unless asked (ROOD_STREAM_DIGEST=1, or RPC stream {on: true}). */
int pc_stream_on PC_HOST = 0; /* the producer's: the walk records digest events while it is on */
static int stream_on_c PC_HOST = 0; /* the renderer's copy (the C_STREAM_ON record): what the digest's own functions and the renderer's notes go by */
static int stream_tracing(void);
__attribute__((constructor)) static void stream_digest_env(void)
{
    const char* v = getenv("ROOD_STREAM_DIGEST");
    if (v && *v && *v != '0')
        pc_stream_on = stream_on_c = 1;
}
static void digest_mix(uint32_t w)
{
    {   /* ROOD_STREAM_WORDS=<path>: every word the digest takes, with the frame it goes into (the index of the frame being built), in <path>.<pid>: where two runs' digests part without the stderr trace (which changes the run) */
        static FILE* wlog __attribute__((section(".data.rood_host")));
        static int wtried __attribute__((section(".data.rood_host")));
        if (!wtried) {
            const char* path = getenv("ROOD_STREAM_WORDS");
            char name[512];
            wtried = 1;
            if (path) {
                snprintf(name, sizeof name, "%s.%d", path, (int)getpid());
                wlog = fopen(name, "wb");
            }
        }
        if (wlog) {
            uint32_t rec[2] = { stream_closed, w };
            fwrite(rec, sizeof rec, 1, wlog);
        }
    }
    if (stream_tracing())
        fprintf(stderr, "[stream mix] %08x acc %016llx\n", w, (unsigned long long)stream_acc);
    for (int i = 0; i < 4; i++) {
        stream_acc = (stream_acc ^ (w & 0xff)) * 1099511628211ull;
        w >>= 8;
    }
}
static unsigned vsyncs;
static int stream_trace PC_HOST = -2; /* ROOD_STREAM_TRACE=V: the records of the frame closed at vsync V, on stderr (to find where two builds part) */
/* What does not change the picture by itself waits until something draws, so that how a producer splits its commands into packets
 * (a draw mode and a texture window as one packet or two, a packet that sets state and nothing else) does not show: the state
 * words (GP0 E1 to E6, the last of each code, hashed in code order: they commute) and the hardware path's per-packet notes
 * (the last of each kind) are hashed just before the next primitive word, no-op words never. */
static uint32_t stream_state[6] PC_HOST, stream_current[6] PC_HOST; /* (waiting; the GPU's own) */
static unsigned stream_state_mask PC_HOST, stream_known PC_HOST;
enum { stream_deferred_kinds = 2 };
static struct {
    int set, n;
    uint32_t tag;
    int v[12];
} stream_deferred[stream_deferred_kinds] PC_HOST;
static int stream_tracing(void)
{
    if (stream_trace == -2)
        stream_trace = getenv("ROOD_STREAM_TRACE") ? atoi(getenv("ROOD_STREAM_TRACE")) : -1;
    static int index = -2;
    if (index == -2)
        index = getenv("ROOD_STREAM_TRACE_IDX") ? atoi(getenv("ROOD_STREAM_TRACE_IDX")) : -1;
    if (index >= 0) /* (ROOD_STREAM_TRACE_IDX=N: the Nth frame the digest closes, wherever its events are applied) */
        return (unsigned)index == stream_closed;
    return stream_trace >= 0 && (unsigned)stream_trace == vsyncs + 1;
}
static void stream_trace_record(uint32_t tag, const int* v, int n)
{
    if (stream_trace == -2)
        stream_trace = getenv("ROOD_STREAM_TRACE") ? atoi(getenv("ROOD_STREAM_TRACE")) : -1;
    if (stream_tracing()) {
        fprintf(stderr, "[stream %u] tag %x:", vsyncs + 1, tag);
        for (int i = 0; i < n && i < 12; i++)
            fprintf(stderr, " %x", v[i]);
        fprintf(stderr, "\n");
    }
}
/* The digest's state is the renderer's side of the stream (T1 R4e): the walk's events (a record, a deferred note, a packet's words, a frame's close,
 * the digest being switched on or off) are records, applied in order with the renderer's own notes (gpu_hw.c pc_stream_note: the packet's
 * attributes, a triangle's result), so the digest is what the inline run's was and the walk never waits for the renderer. */
void pc_hwc_stream(int op, unsigned tag, const int* v, int n);          /* hw_cmd.c: a digest event as a record */
void pc_hwc_stream_words(const uint32_t* p, unsigned n);               /* a packet's words */
static void stream_flush_pending(void)
{
    for (int k = 0; k < 6; k++)
        if (stream_state_mask >> k & 1) {
            digest_mix(stream_state[k]);
            if (stream_tracing())
                fprintf(stderr, "[stream %u] state %08x\n", vsyncs + 1, stream_state[k]);
        }
    stream_state_mask = 0;
    for (int k = 0; k < stream_deferred_kinds; k++)
        if (stream_deferred[k].set) {
            stream_trace_record(stream_deferred[k].tag, stream_deferred[k].v, stream_deferred[k].n);
            digest_mix(0x5e5e0000u | stream_deferred[k].tag);
            for (int i = 0; i < stream_deferred[k].n; i++)
                digest_mix((uint32_t)stream_deferred[k].v[i]);
            ++stream_records;
            stream_deferred[k].set = 0;
        }
}
static void stream_record_now(uint32_t tag, const int* v, int n)
{
    if (!stream_on_c)
        return;
    stream_flush_pending();
    stream_trace_record(tag, v, n);
    digest_mix(0x5e5e0000u | tag);
    for (int i = 0; i < n; i++)
        digest_mix((uint32_t)v[i]);
    ++stream_records;
}
/* A note that waits for the next primitive (kind 0: the packet's anchor, opacity and screen; 1: its bubble's suppression). */
static void stream_defer_now(int kind, uint32_t tag, const int* v, int n)
{
    if (!stream_on_c)
        return;
    stream_deferred[kind].set = 1, stream_deferred[kind].tag = tag, stream_deferred[kind].n = n > 12 ? 12 : n;
    memcpy(stream_deferred[kind].v, v, (size_t)stream_deferred[kind].n * sizeof *v);
}
static void stream_commands_now(const uint32_t* p, unsigned n);
static void stream_close_now(unsigned vsync);
/* The walk's side: a digest event while the digest is on is a record. */
/* ROOD_STREAM_SYNC=1: the digest as it was, each event applying what is pending first and running at once (to compare the two). */
static int stream_direct(void)
{
    static int mode = -1;
    if (mode < 0)
        mode = getenv("ROOD_STREAM_SYNC") != NULL;
    return mode;
}
static void stream_record(uint32_t tag, const int* v, int n)
{
    if (!pc_stream_on)
        return;
    if (stream_direct())
        PC_HWC_SYNC(), stream_record_now(tag, v, n);
    else
        pc_hwc_stream(0, tag, v, n);
}
static void stream_defer(int kind, uint32_t tag, const int* v, int n)
{
    if (!pc_stream_on)
        return;
    if (stream_direct())
        PC_HWC_SYNC(), stream_defer_now(kind, tag, v, n);
    else
        pc_hwc_stream(1 + kind, tag, v, n);
}
static void stream_set_on(int on)
{
    if (stream_direct())
        PC_HWC_SYNC(), stream_on_c = on != 0;
    pc_stream_on = on != 0;
    if (!stream_direct())
        pc_hwc_stream(4, 0, &on, 1);
}
/* The renderer's side of the records (hw_cmd.c applies them): op 0 a record, 1 and 2 a deferred note of kind 0 and 1, 3 a frame's close, 4 on or off. */
void pc_stream_apply(int op, unsigned tag, const int* v, int n)
{
    switch (op) {
    case 0: stream_record_now(tag, v, n); break;
    case 1: stream_defer_now(0, tag, v, n); break;
    case 2: stream_defer_now(1, tag, v, n); break;
    case 3: stream_close_now(tag); break;
    case 4: stream_on_c = v[0] != 0; break;
    }
}
void pc_stream_apply_words(const uint32_t* p, unsigned n)
{
    if (!stream_on_c)
        return;
    if (stream_tracing()) { /* (ROOD_STREAM_TRACE: the words too) */
        fprintf(stderr, "[stream %u] words %u:", vsyncs + 1, n);
        for (unsigned k = 0; k < n; k++)
            fprintf(stderr, " %08x", p[k]);
        fprintf(stderr, "\n");
    }
    stream_commands_now(p, n);
    stream_words += n;
}
/* The hardware path's decisions about a packet (gpu_hw.c: anchor, opacity, coverage, bubbles, text placement): what the
 * words alone do not say, so the digest sees them too. Called by the renderer, in order with the walk's own events. */
void pc_stream_note(unsigned tag, const int* v, int n)
{
    if (tag == 3)
        stream_defer_now(0, 0x103, v, n);
    else
        stream_record_now(0x100 | tag, v, n);
}
static void stream_close_now(unsigned vsync)
{
    stream_flush_pending();
    stream_known = 0; /* (a frame's digest stands alone: the state the GPU was left in is not part of it) */
    stream_frame* f = &stream_ring[stream_closed++ % stream_ring_size];
    f->vsync = vsync, f->words = stream_words, f->records = stream_records, f->digest = stream_acc;
    if (stream_tracing())
        fprintf(stderr, "[stream %u] close at vsync %u: %u words, %u records, digest %016llx\n", vsyncs, vsync, stream_words, stream_records, (unsigned long long)stream_acc);
    stream_chain = (stream_chain ^ stream_acc) * 1099511628211ull;
    stream_acc = 1469598103934665603ull, stream_words = stream_records = 0;
}
static void stream_close(unsigned vsync)
{
    if (stream_direct())
        PC_HWC_SYNC(), stream_close_now(vsync);
    else
        pc_hwc_stream(3, vsync, NULL, 0);
}
/* The ring, oldest first (at most `max`), the chain over every closed frame, and how many frames closed. */
int pc_stream_read(int on, int reset, unsigned* closed, uint64_t* chain, unsigned* vs, unsigned* words, unsigned* records,
    uint64_t* digests, int max)
{
    pc_hwc_flush(); /* (every event of the walk is applied: the digest's state is the renderer's) */
    if (on >= 0)
        pc_stream_on = on != 0, stream_on_c = pc_stream_on;
    if (reset) {
        stream_closed = 0, stream_chain = 1469598103934665603ull, stream_acc = 1469598103934665603ull;
        stream_words = stream_records = 0;
    }
    int n = stream_closed < stream_ring_size ? (int)stream_closed : stream_ring_size;
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++) {
        const stream_frame* f = &stream_ring[(stream_closed - (unsigned)n + (unsigned)i) % stream_ring_size];
        vs[i] = f->vsync, words[i] = f->words, records[i] = f->records, digests[i] = f->digest;
    }
    *closed = stream_closed, *chain = stream_chain;
    return n;
}
void pc_display_env(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)
        pc_unimplemented("display dimensions");
    if (&pc_gte_centre)
        pc_gte_centre = w / 2;
    display_x = x;
    display_y = y;
    display_w = w;
    display_h = h;
    { int v[4] = { x, y, w, h }; stream_record(1, v, 4); }
    if (frames)
        pc_present();
}
void pc_draw_env(int x, int y, int w, int h, int ox, int oy, int tp)
{
    clip_x = x;
    clip_y = y;
    clip_w = w;
    clip_h = h;
    draw_x = ox;
    draw_y = oy;
    tpage = tp;
    { int v[7] = { x, y, w, h, ox, oy, tp }; stream_record(2, v, 7); }
}
/* The command word of the sprite or polygon the hardware path is being
 * handed (gpu_hw.c: which game string a glyph belongs to, ui_text.c). */
const void* pc_gpu_command;
/* Set while a packet DrawPrim hands over is drawn (not an ordering table):
 * ui_text.c tells the memory card screens' signs from the info style's. */
int pc_gpu_immediate;
/* The ordering table's packet being drawn (its tag word; NULL for DrawPrim's
 * single packets) and the one linked after a packet (NULL at the end or
 * outside guest memory): ui_text.c reads a popup number's following digits. */
static const uint32_t* drawing_tag;
/* The sub-pixel corners recorded for the command being drawn (ui_upscale.c): the slot the table's adoption found for its packet
 * (an adopted table's first command), -2 to ask by the command's address (a packet drawn where it lies). */
static int walk_precise PC_HOST = -2;
static int precise_slot_for(const uint32_t* p);
/* The walk's key for a packet it reads a copy of: the copy's address is not the registries' (key_delta: guest - copy; 0 for
 * a single packet drawn where it lies). */
static intptr_t key_delta PC_HOST;
#define GUEST(q) ((const void*)((uintptr_t)(q) + (uintptr_t)key_delta))

static const uint32_t* frame_next_packet(const uint32_t* tag);
/* The packet after `tag`: in an adopted table, the frame's next entry; NULL for a packet drawn where it lies (DrawPrim, a lowered draw item:
 * nothing is linked after it). */
static const uint32_t* next_packet(const uint32_t* tag)
{
    const uint32_t* next = frame_next_packet(tag);
    return next && next != (const uint32_t*)1 ? next : NULL;
}
/* (which sparse register of guest memory a link names: the 32-bit build also followed native addresses) */
static int round_px(float v)
{
    int i = (int)v;
    return v - (float)i >= 0.5f ? i + 1 : v - (float)i < -0.5f ? i - 1 : i;
}
/* button_roles.c (battle builds): how far across a primitive samples the
 * game's ○ / × button where Circle and Cross are exchanged (0: as drawn). */
extern int pc_button_roles_texels(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1)
    __attribute__((weak));
#include "boot_gpu_polygons.inc"
#include "boot_gpu_lines.inc"

/* The words a packet hands the GPU, into the digest. Only what the GPU ignores is left out: a textured primitive with the raw
 * bit (code bit 0) is not modulated, so its colour bytes are never read (in an old packet they were whatever the memory held,
 * an item sets none); a modulated textured primitive keeps its colour. The top halves of a polygon's third and fourth texel
 * words are padding the GPU does not read either. */
static void stream_commands_now(const uint32_t* p, unsigned n)
{
    unsigned i = 0;
    while (i < n) {
        unsigned code = p[i] >> 24, len = 1;
        if (code >= 0xe1 && code <= 0xe6) { /* a state word: waits (the last of its code) until something draws; one that sets
                                              * what the GPU already has is no change and is left out */
            unsigned k = code - 0xe1;
            if (!(stream_known & 1u << k) || stream_current[k] != p[i]) {
                stream_state[k] = p[i], stream_state_mask |= 1u << k;
                stream_current[k] = p[i], stream_known |= 1u << k;
            }
            ++i;
            continue;
        }
        if (code == 0) { /* a no-op word */
            ++i;
            continue;
        }
        stream_flush_pending();
        if ((code & 0xe0) == 0x20) { /* polygons */
            unsigned v = code & 8 ? 4 : 3, tex = code >> 2 & 1, shade = code >> 4 & 1;
            len = 1 + v * (1 + tex) + (shade ? v - 1 : 0);
        } else if ((code & 0xe0) == 0x60) { /* sprites and rectangles */
            len = 2 + (code >> 2 & 1) + ((code & 0x18) == 0);
        }
        if (len > n - i)
            len = n - i;
        if ((code & 0xe0) == 0x20 && (code & 4) && stream_known & 1u) { /* a textured polygon's own page word (its second corner's texel word) sets the GPU's page bits */
            unsigned at = 4 + (code >> 4 & 1);
            if (at < len)
                stream_current[0] = (stream_current[0] & ~0x1ffu) | ((p[i + at] >> 16) & 0x1ffu);
        }
        int raw_textured = ((code & 0xe0) == 0x20 || (code & 0xe0) == 0x60) && (code & 4) && (code & 1);
        /* (a line's command byte uses bits 4 (shaded), 3 (poly-line) and 1 (semi-transparent) only: line_command, boot_gpu_lines.inc) */
        digest_mix(raw_textured ? p[i] & 0xff000000u : (code >= 0x40 && code <= 0x5f) ? p[i] & ~0x05000000u : p[i]);
        for (unsigned k = 1; k < len; k++) {
            uint32_t w = p[i + k];
            /* (the second to fourth corners' colour words of a shaded polygon have no code: their top bytes are leftovers) */
            if ((code & 0xe0) == 0x20 && (code & 0x10)) {
                unsigned tex = code >> 2 & 1, corners = code & 8 ? 4 : 3;
                for (unsigned c = 1; c < corners; c++)
                    if (k == (tex ? 3 : 2) * c)
                        w &= 0xffffffu;
            }
            /* (the third and fourth corners' texel words have nothing in their top halves: the packet memory's leftovers) */
            if ((code & 0xe0) == 0x20 && (code & 4)) {
                unsigned shade = code >> 4 & 1, corners = code & 8 ? 4 : 3;
                for (unsigned c = 2; c < corners; c++)
                    if (k == 2 + (2 + shade) * c)
                        w &= 0xffffu;
            }
            digest_mix(w);
        }
        i += len;
    }
}
static void commands(uint32_t* p, unsigned n)
{
    unsigned i = 0;
    if (pc_stream_on) {
        if (stream_direct())
            PC_HWC_SYNC(), pc_stream_apply_words(p, n);
        else
            pc_hwc_stream_words(p, n);
    }
    while (i < n) {
        unsigned code = p[i] >> 24;
        if (code == 0xe1) {
            tpage = p[i++] & 0x7ff;
            continue;
        }
        if (code == 0xe2) {
            texture_window = p[i] & 0xfffff;
            ++i;
            continue;
        }
        if (code == 0xe6) {
            mask_set = p[i] & 1;
            mask_check = p[i] & 2;
            ++i;
            continue;
        }
        if ((code & 0xe0) == 0x80) {
            if (n - i < 4)
                pc_unimplemented("truncated VRAM copy");
            unsigned sx = p[i + 1] & 1023, sy = (p[i + 1] >> 16) & 511;
            unsigned dx = p[i + 2] & 1023, dy = (p[i + 2] >> 16) & 511;
            unsigned w = ((p[i + 3] - 1) & 1023) + 1;
            unsigned h = (((p[i + 3] >> 16) - 1) & 511) + 1;
            stream_event(5, (int)dx, (int)dy, (int)w, (int)h, (int[]) { (int)sx, (int)sy }, 2 * sizeof(int));
            vcheck_copy(sx, sy, dx, dy, w, h);
            if (pc_hw_stale_copy)
                pc_hwc_stale_copy((int)sx, (int)sy, (int)dx, (int)dy, (int)w, (int)h);
            if (pc_hw_before_write) {
                pc_hwc_before_write((int)dx, (int)dy, (int)w, (int)h);
            }
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; x += 16) {
                    uint16_t block[16];
                    unsigned count = w - x < 16 ? w - x : 16;
                    for (unsigned j = 0; j < count; ++j)
                        block[j] = vram[((sy + y) & 511) * 1024 + ((sx + x + j) & 1023)];
                    for (unsigned j = 0; j < count; ++j) {
                        uint16_t* dst =
                            &vram[((dy + y) & 511) * 1024 + ((dx + x + j) & 1023)];
                        if (!mask_check || !(*dst & 0x8000))
                            *dst = block[j] | (mask_set ? 0x8000 : 0);
                    }
                }
            {   /* (a copy out of a drawn cell is no more exact than its source) */
                int drawn = 0;
                for (int c = (int)sx >> 6; c <= (int)(sx + w - 1) >> 6 && c < 16; c++)
                    for (int b = (int)sy >> 4; b <= (int)(sy + h - 1) >> 4 && b < 32; b++)
                        drawn |= vram_drawn[c] >> b & 1;
                if (drawn)
                    vram_note_drawn((int)dx, (int)dy, (int)(dx + w - 1), (int)(dy + h - 1));
                else
                    vram_note_written((int)dx, (int)dy, (int)w, (int)h);
            }
            pc_hwc_mark(dx, dy, dx + w - 1, dy + h - 1);
            pc_hwc_vram_copy((int)sx, (int)sy, (int)dx, (int)dy, (int)w, (int)h, (int)mask_set, mask_check ? 1 : 0);
            i += 4;
            continue;
        }
        /* GP0 0x00/0x01 and the undocumented 0x03-0x1E occupy one FIFO word
         * and draw nothing (PSX-SPX); 0x02 (fill) remains unsupported. */
        if (code == 0x00 || code == 0x01 || (code >= 0x03 && code <= 0x1e)) {
            ++i;
            continue;
        }
        /* GP0 0xE0, 0xE7-0xEF and the undefined 0xF0-0xFF also take one word
         * and draw nothing (as DuckStation decodes them). The memory-card
         * screen sends 0xFF when its fade colour overflows the command byte. */
        if (code == 0xe0 || (code >= 0xe7 && code <= 0xff)) {
            static int reported;
            if (code >= 0xf0 && !reported++)
                fprintf(stderr, "[gpu] undefined GP0 0x%02x ignored as on hardware\n", code);
            ++i;
            continue;
        }
        if (code >= 0x20 && code <= 0x3f) {
            i += polygon_command(p + i, n - i);
            continue;
        }
        if (code >= 0x40 && code <= 0x5f) {
            i += line_command(p + i, n - i);
            continue;
        }
        if (code == 0xe3) {
            int right = clip_x + clip_w, bottom = clip_y + clip_h;
            clip_x = p[i] & 1023;
            clip_y = (p[i] >> 10) & 511;
            clip_w = right - clip_x;
            clip_h = bottom - clip_y;
            ++i;
            continue;
        }
        if (code == 0xe4) {
            clip_w = (p[i] & 1023) + 1 - clip_x;
            clip_h = ((p[i] >> 10) & 511) + 1 - clip_y;
            ++i;
            continue;
        }
        if (code == 0xe5) {
            draw_x = (int32_t)(p[i] << 21) >> 21;
            draw_y = (int32_t)(p[i] << 10) >> 21;
            ++i;
            continue;
        }
        /* Rectangles: GP0 0x60-0x7F. Bits 3-4 select variable size (from a
         * size word), 1x1, 8x8 or 16x16; bit 2 textured, bit 1 semi, bit 0 raw. */
        if ((code & 0xe0) == 0x60) {
            int tex = (code & 4) != 0, size = (code >> 3) & 3;
            unsigned words = 2 + tex + (size == 0);
            if (n - i < words)
                pc_unimplemented("truncated sprite packet");
            int x = (int16_t)p[i + 1] + draw_x, y = (int16_t)(p[i + 1] >> 16) + draw_y;
            unsigned wh = size == 0 ? p[i + words - 1] : size == 1 ? 0x10001u : size == 2 ? 0x80008u : 0x100010u;
            /* The GPU keeps 10 bits of the width and 9 of the height (PSX-SPX,
             * as DuckStation decodes them). The game relies on it: MENU0's LV
             * picker overwrites a "--" sprite's texpage word with 0x64602000
             * (84.c, levelledSpell), so the GPU reads a sprite from there whose
             * size word is the real one's UV/CLUT (0x37FE70C0: 192x510, at
             * y 0x6400, off screen). */
            unsigned w = wh & 0x3ff, h = (wh >> 16) & 0x1ff;
            if (w != (wh & 0xffff) || h != wh >> 16) {
                static int reported;
                if (!reported++)
                    fprintf(stderr, "[gpu] sprite size 0x%08x masked to %ux%u as on hardware\n", wh, w, h);
            }
            unsigned uv = tex ? p[i + 2] : 0;
            if (tex && pc_button_roles_texels) /* the game's ○ / × follow the button roles */
                uv += (unsigned)pc_button_roles_texels(vram, tpage, uv >> 16, (int)(uv & 255), (int)((uv >> 8) & 255),
                    (int)(uv & 255) + (int)w - 1, (int)((uv >> 8) & 255) + (int)h - 1);
            if (tex && pc_hw_sync_texture) {
                int clip[4] = { clip_x, clip_y, clip_w, clip_h }, before = fence_syncs();
                pc_hwc_fence_texture(tpage, uv >> 16, texture_window, uv & 255, (uv >> 8) & 255,
                    (int)(uv & 255) + (int)w - 1, (int)((uv >> 8) & 255) + (int)h - 1, clip);
                fence_event(2, before, (int)(uv & 255), (int)((uv >> 8) & 255), (int)w, (int)h);
            }
            vram_note_drawn(x > clip_x ? x : clip_x, y > clip_y ? y : clip_y,
                (x + (int)w < clip_x + clip_w ? x + (int)w : clip_x + clip_w) - 1,
                (y + (int)h < clip_y + clip_h ? y + (int)h : clip_y + clip_h) - 1);
            {
                int clip[4] = { clip_x, clip_y, clip_w, clip_h };
                pc_hwc_set_2d(1); /* sprites and tiles are screen-space */
                int texels[4] = { (int)(uv & 255), (int)((uv >> 8) & 255), (int)(uv & 255) + (int)w - 1, (int)((uv >> 8) & 255) + (int)h - 1 };
                pc_hwc_set_bounds(x, y, x + (int)w, y + (int)h, code, tpage, uv >> 16, tex ? texels : NULL, clip);
                pc_gpu_command = GUEST(p + i);
                pc_hwc_rect(x, y, (int)w, (int)h, uv & 255, (uv >> 8) & 255, p[i] & 0xffffff, code,
                    tpage, uv >> 16, texture_window, clip, mask_set, mask_check);
                pc_gpu_command = NULL;
            }
            i += words;
            continue;
        }
        fprintf(stderr, "GPU opcode 0x%02x (%u words remain)\n", code, n - i);
        pc_unimplemented("GPU command");
    }
}
/* Native draw lists (render.c) sit in the ordering table as marker packets:
 * GPU no-ops here, drawn by the hardware path at their place. */
extern int render_is_marker(const uint32_t*) __attribute__((weak));
extern void pc_hw_native_marker(uint32_t, int, int, const int*, unsigned, int) __attribute__((weak));
extern int render_translucent_marker(const uint32_t*, uint32_t*, int*, int*) __attribute__((weak));
extern void pc_hw_native_translucent(uint32_t, int, int, int, int, const int*, unsigned, int) __attribute__((weak));
/* Vector talk bubbles (ui_bubbles.c): their packets are drawn here as on the
 * PS1 but left out of the hardware target, which draws the shapes instead. */
extern void pc_ui_bubble_packet(int, const void*, int, int, const int*) __attribute__((weak));
extern int pc_hw_suppress __attribute__((weak)); /* gpu_hw.c: the packet is left out of the hardware picture */
extern int pc_frame_drawing_hidden __attribute__((weak)); /* draw_items.c */
extern int pc_frame_drawing_anchor __attribute__((weak)), pc_frame_drawing_opacity __attribute__((weak));
extern int pc_frame_drawing_fade[5] __attribute__((weak));
extern const float* pc_frame_drawing_span __attribute__((weak));
extern const float* pc_frame_drawing_wire __attribute__((weak));
/* Typed draw items (draw_items.c) placed in the ordering table by marker packets: drawn where the marker is. */
extern int pc_frame_marker(const uint32_t*, int*, int*) __attribute__((weak));
extern void pc_frame_draw_run(int, int) __attribute__((weak));
extern int pc_frame_run_pages(int, int, unsigned, int (*)(unsigned)) __attribute__((weak));
extern int pc_frame_run_has_native(int, int) __attribute__((weak));
extern void pc_frame_splice_table(const void*, const void*) __attribute__((weak));
extern int pc_frame_table_order(const void*) __attribute__((weak));
extern void pc_frame_table_run(int, const void**, const void**, const void**) __attribute__((weak));
extern int pc_frame_table_check(const void*) __attribute__((weak));
/* gpu_hw.c: the ordering table about to be drawn holds native draws, and
 * starts in drawing area `clip` (x, y, w, h). */
extern void pc_hw_ot_native_area(const int*) __attribute__((weak));
/* The ordering table of a DrawOTag, adopted: every packet on its chain copied, once, at the start of the walk (T4 slice S1,
 * pc/docs/plans/2026-11-12-t4-survey.md). The walk then reads this frame, not the guest memory the game built it in: what
 * a packet is (its words), where it came from (`guest`, the key the address-keyed registries and readers still use until
 * they take attributes) and what the layers said about it are all settled here. */
typedef struct {
    uintptr_t guest;  /* the packet's address in guest memory (a key: the registries of gpu_hw.c, ui_text and the bubbles) */
    uint32_t off;     /* where its tag word is in `words` */
    uint8_t front;    /* its layer: 1 linked in front of the scene's slots (menus, HUD: 2D whatever its vertices), 0 in the scene */
    uint8_t g2d_known; /* photo mode: the entry is an ordering table slot of a frame (game2d_entry) */
    uint8_t g2d_hidden; /* photo mode: the game's 2D is hidden here (the last slot seen lies below the hidden ones) */
    pc_hw_attr attr;  /* the hardware path's ranges it lies in (gpu_hw.c pc_hw_resolve) */
} walk_entry;
static struct {
    walk_entry* entry;
    unsigned count, capacity;
    uint32_t* words;
    size_t used, word_capacity;
} frame PC_HOST = { 0 };
static int game2d_entry(const uint32_t* p, int* hidden);
/* A table drawn where an entry of another is (the text boxes' page of the table, after its slot in the frame's): draw_items.c orders its runs
 * there. */
void pc_frame_table_after(const void* at, const void* head, const void* last)
{
    (void)last;
    if (pc_frame_splice_table)
        pc_frame_splice_table(at, head);
}
static const uint32_t *ot_scene, *ot_top; /* (below: the table's scene base and top) */
/* The frame's entry for packet `p`: its words, where it came from, its layer and what the layers said about it. */
static void adopt_push(const uint32_t* p, int photo, int front, int* hidden, const uint32_t* ref)
{
    unsigned len = *p >> 24;
    if (frame.count == frame.capacity) {
        frame.capacity = frame.capacity ? frame.capacity * 2 : 4096;
        frame.entry = realloc(frame.entry, frame.capacity * sizeof *frame.entry);
    }
    if (frame.used + 1 + len > frame.word_capacity) {
        frame.word_capacity = (frame.used + 1 + len) * 2 + 65536;
        frame.words = realloc(frame.words, frame.word_capacity * sizeof *frame.words);
    }
    if (!frame.entry || !frame.words)
        pc_unimplemented("out of memory adopting an ordering table");
    walk_entry* e = &frame.entry[frame.count++];
    memset(e, 0, sizeof *e);
    e->guest = (uintptr_t)p;
    e->off = (uint32_t)frame.used;
    memcpy(frame.words + frame.used, p, (1 + (size_t)len) * sizeof *p);
    frame.used += 1 + len;
    e->front = (uint8_t)front;
    resolve_attr(p, &e->attr);
    if (photo) {
        e->g2d_known = (uint8_t)game2d_entry(p, hidden);
        e->g2d_hidden = (uint8_t)*hidden;
    }
    (void)ref;
}
/* The ordering table of a DrawOTag, taken in: the frame is the runs placed in it, in the order their slots give (draw_items.c
 * pc_frame_table_order), each run's marker packet copied once at the start of the draw (T4 slice S1, pc/docs/plans/2026-11-12-t4-survey.md): what a packet
 * is (its words), where it came from (`guest`, the key the address-keyed registries and readers still use until they take attributes) and
 * what the layers said about it are settled here. Nothing is linked: a table holds no packets (a link in one is the tripwire's). */
static void adopt(const void* head, int photo)
{
    int front = 0, hidden = 0, n;
    frame.count = 0, frame.used = 0;
    if (pc_frame_table_check)
        pc_frame_table_check(head);
    n = pc_frame_table_order ? pc_frame_table_order(head) : -1;
    if (n < 0)
        pc_unimplemented("an ordering table drawn that was not made");
    for (int k = 0; k < n; k++) {
        const void *packet, *entry, *at;
        pc_frame_table_run(k, &packet, &entry, &at);
        const uint32_t* ref = at ? at : entry; /* (the slot the run draws at: its layer and, in photo mode, the game 2D it belongs to) */
        if (ref && ot_scene && (const uint32_t*)ref <= ot_top && (const uint32_t*)ref >= ot_scene - 32)
            front = (const uint32_t*)ref < ot_scene;
        hidden = 0;
        if (photo && ref)
            game2d_entry(ref, &hidden);
        adopt_push(packet, photo, front, &hidden, ref);
    }
}
/* The entry after the one whose tag word is `tag` (a pointer into the frame's words): its tag, 1 for none, NULL when `tag` is
 * not in the frame. */
static const uint32_t* frame_next_packet(const uint32_t* tag)
{
    if (!frame.count || tag < frame.words || tag >= frame.words + frame.used)
        return NULL;
    uint32_t off = (uint32_t)(tag - frame.words);
    unsigned lo = 0, hi = frame.count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (frame.entry[mid].off < off)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= frame.count || frame.entry[lo].off != off)
        return NULL;
    return lo + 1 < frame.count ? frame.words + frame.entry[lo + 1].off : (const uint32_t*)1;
}
static int precise_slot_for(const uint32_t* p)
{
    if (walk_precise == -2)
        return pc_ui_precise_slot ? pc_ui_precise_slot(GUEST(p)) : -1;
    return drawing_tag && drawing_tag + 1 == p ? walk_precise : -1;
}
/* Whether the adopted table holds a native draw (a PC_ITEM_NATIVE item's marker). */
static int ot_has_native(void)
{
    for (unsigned i = 0; i < frame.count; i++) {
        const uint32_t* p = frame.words + frame.entry[i].off;
        int first, count;
        if (pc_frame_marker && pc_frame_marker(p, &first, &count) && pc_frame_run_has_native(first, count))
            return 1;
    }
    return 0;
}
static void draw_packet(void* data, int chain);
/* The ordering table's layers. The battle code (BATTLE 146C.c) clears an
 * ordering table of 0x822 entries and keeps its scene slots' base in the
 * scratchpad (vs_scratch.unk4, ot + 18; the screen transitions' ot + 16):
 * 3D (models, room, effects at projected points: the targeting sphere's
 * lines at ot + depth) links at or behind it, and the slots in front of it
 * hold screen furniture (menus, HUD: vs_battle_addTile and friends link at
 * unk4 - 12, unk8 = ot + 2). So a primitive in a front slot is 2D however
 * its vertices fall: the projection log (narrowed_2d) matches any pixel the
 * GTE projected to this frame, and with a model on screen (the menus'
 * Ashley) a menu's lines and bars landed on her vertices often enough that
 * pieces of them were taken for 3D and drawn uncompressed in widescreen
 * (bars came apart, slivers beside the screen). ot_scene: that base when
 * the ordering table being drawn has it (it lies below the table's last
 * entry, where DrawOTag starts, by at most ot_span entries), else NULL. */
static const uint32_t *ot_scene, *ot_top;
enum { ot_span = 0x1000 };
static void ot_layers_begin(const void* top)
{
    uintptr_t scene = (uintptr_t)PC_OT_BASE;
    ot_top = top;
    ot_scene = scene <= (uintptr_t)top && (uintptr_t)top - scene < ot_span * 4u ? (const uint32_t*)scene : NULL;
    ot_front = 0;
}
extern int pc_photo_hide_game2d __attribute__((weak));
extern int pc_photo_game2d_slots __attribute__((weak));
void pc_draw_packet(void* data, int chain)
{
    uint64_t cost = pc_cost_begin();
    if (chain == 1) {
        ot_layers_begin(data);
        adopt(data, &pc_photo_hide_game2d && pc_photo_hide_game2d && &pc_photo_game2d_slots);
    } else if (chain)
        ot_layers_begin(data);
    if (chain == 1 && pc_hw_ot_native_area && render_is_marker && ot_has_native()) {
        int clip[4] = { clip_x, clip_y, clip_w, clip_h };
        pc_hwc_ot_native_area(clip);
    }
    pc_hw_meter_walk(1);
    draw_packet(data, chain);
    pc_hw_meter_walk(0);
    if (chain)
        ot_scene = NULL, ot_front = 0;
    pc_cost_end(PC_COST_DRAW, cost);
}
/* A packet lowered from a draw item (draw_items.c): drawn as one member of an ordering table. */
void pc_draw_item_packet(void* packet)
{
    pc_hw_meter_walk(1);
    draw_packet(packet, 2);
    PC_HWC_SYNC();
    pc_hw_meter_walk(0);
}
/* The photo mode's hidden game 2D (photo.cpp, rood.ui.hide{game2d}): in the field's ordering tables
 * (vs_render_legacyOrderingTables, 0x822 entries each, drawn from the far end), the packets linked at entries below
 * pc_photo_game2d_slots (the HUD, text and window layer: vs_scratch.unk4 is entry 18, the game's 2D
 * slots lie within 16 entries of it) are not drawn; native lists' markers and mode packets still are. */
extern int pc_photo_hide_game2d __attribute__((weak));
extern int pc_photo_game2d_slots __attribute__((weak));
extern void* vs_render_legacyOrderingTables[2] __attribute__((weak)); /* (absent in the GPU probe) */
static int game2d_entry(const uint32_t* p, int* hidden)
{
    if (!(uintptr_t)vs_render_legacyOrderingTables)
        return 0;
    for (int b = 0; b < 2; b++) {
        const uint32_t* ot = vs_render_legacyOrderingTables[b];
        if (ot && p >= ot && p < ot + 0x822) {
            *hidden = p - ot < pc_photo_game2d_slots;
            return 1;
        }
    }
    return 0;
}
/* The game's 2D anchored in the world (the bubbles over actors, ui_emotes.c; the status words and numbers
 * over heads, ui_labels.h): the battle page's images (VRAM (448, 256)), linked at the actors' depths, out
 * of reach of pc_photo_game2d_slots. A packet of primitives that draws from that page (its polygons' tpage,
 * a sprite's: the packet's or the draw mode's) is one of them; 1 when it is. */
static int battle_page(unsigned tp) { return (tp & 15) == 448 / 64 && (tp >> 4 & 1) == 1; }
static int world2d_packet(const uint32_t* p)
{
    unsigned n = p[0] >> 24, i = 1, tp = (unsigned)tpage;
    int found = 0;
    while (i <= n) {
        unsigned code = p[i] >> 24, words;
        if (code == 0xe1) {
            tp = p[i] & 0x7ff;
            i++;
            continue;
        }
        if ((code >= 0xe2 && code <= 0xe6) || code == 0)
            words = 1;
        else if ((code & 0xe0) == 0x20) { /* polygons */
            unsigned v = code & 8 ? 4 : 3, tex = code >> 2 & 1, shade = code >> 4 & 1;
            words = 1 + v * (1 + tex) + (shade ? v - 1 : 0);
            if (tex && i + 4 + shade <= n && battle_page(p[i + 4 + shade] >> 16))
                found = 1;
        } else if ((code & 0xe0) == 0x60) { /* sprites and rectangles */
            unsigned tex = code >> 2 & 1;
            words = 2 + tex + ((code & 0x18) == 0);
            if (tex && battle_page(tp))
                found = 1;
        } else
            return 0; /* lines, copies, anything else: drawn */
        i += words;
    }
    return found;
}
static int mode_packet(const uint32_t* p)
{
    unsigned n = p[0] >> 24;
    for (unsigned i = 1; i <= n; i++)
        if (!((p[i] >> 24) >= 0xE1 && (p[i] >> 24) <= 0xE6))
            return 0;
    return 1;
}
/* chain: 1 an ordering table, walked by its links; 0 a single packet DrawPrim hands over; 2 a single packet a
 * typed draw item lowered to (draw_items.c), drawn as a member of an ordering table. */
static void draw_packet(void* data, int chain)
{
    int hide2d = chain == 1 && &pc_photo_hide_game2d && pc_photo_hide_game2d && &pc_photo_game2d_slots, hidden = 0;
    unsigned count_entries = chain == 1 ? frame.count : 1;
    intptr_t outer_delta = key_delta;
    for (unsigned at = 0; at < count_entries; at++) {
        const walk_entry* en = chain == 1 ? &frame.entry[at] : NULL;
        uint32_t* p = en ? frame.words + en->off : data;
        const uint32_t* guest = en ? (const uint32_t*)en->guest : p;
        uint32_t tag = *p;
        key_delta = (intptr_t)((uintptr_t)guest - (uintptr_t)p);
        if (en)
            ot_front = en->front;
        ++stream.draws;
        uint32_t t_list;
        int t_first, t_count;
        int run_first, run_count;
        int world2d_run = hide2d && pc_frame_marker && pc_frame_marker(p, &run_first, &run_count)
            && pc_frame_run_pages(run_first, run_count, (unsigned)tpage, battle_page);
        hidden = en ? en->g2d_hidden : 0;
        int native_run = pc_frame_marker && pc_frame_marker(p, &run_first, &run_count) && pc_frame_run_has_native(run_first, run_count);
        if (hide2d && !en->g2d_known && (world2d_run || ((hidden || world2d_packet(p))
            && !(render_is_marker && render_is_marker(p))
            && !(render_translucent_marker && render_translucent_marker(p, &t_list, &t_first, &t_count))
            && !native_run
            && !mode_packet(p)))) {
            continue;
        }
        pc_hw_attr single;
        const pc_hw_attr* attr = en ? &en->attr : (resolve_attr(guest, &single), &single);
        if (!en && chain == 2 && &pc_frame_drawing_anchor) {
            /* a draw item carries its own anchor and opacity (draw_items.c), not a range of the arena's addresses */
            if (pc_frame_drawing_anchor >= 0)
                single.anchor = pc_frame_drawing_anchor & ~0x20, single.exact = (pc_frame_drawing_anchor & 0x20) != 0;
            if (pc_frame_drawing_opacity >= 0)
                single.opacity = (short)pc_frame_drawing_opacity;
            if (&pc_frame_drawing_fade && pc_frame_drawing_fade[4])
                single.has_fade = 1, single.fade_y[0] = (short)pc_frame_drawing_fade[0], single.fade_y[1] = (short)pc_frame_drawing_fade[1],
                    single.fade_a[0] = (unsigned char)pc_frame_drawing_fade[2], single.fade_a[1] = (unsigned char)pc_frame_drawing_fade[3];
            if (&pc_frame_drawing_span && pc_frame_drawing_span)
                single.has_span = 1, memcpy(single.span_shape, pc_frame_drawing_span, sizeof single.span_shape);
            if (&pc_frame_drawing_wire && pc_frame_drawing_wire)
                single.has_wire = 1, memcpy(single.wire, pc_frame_drawing_wire, sizeof single.wire);
        }
        /* An item marker is not a GPU packet: what it places is hashed as the items are lowered and drawn, so a run's index
         * (which moves when a producer becomes items) and its packet's own notes are not part of the digest. */
        int marker_first, marker_count;
        int item_marker = pc_frame_marker && pc_frame_marker(p, &marker_first, &marker_count);
        int stream_was = pc_stream_on;
        if (item_marker)
            stream_set_on(0);
        pc_hwc_set_attr(attr, draw_x, display_w);
        if (pc_ui_bubble_packet) {
            int clip[4] = { clip_x, clip_y, clip_w, clip_h };
            pc_ui_bubble_packet(attr->bubble, p, draw_x, draw_y, clip);
            int suppressed = &pc_hw_suppress ? pc_hw_suppress : 0;
            stream_defer(1, 0x104, &suppressed, 1);
        }
        if (chain == 2 && pc_frame_drawing_hidden && &pc_hw_suppress) /* a draw item the hardware picture leaves out (a rain drop room geometry hides) */
            pc_hw_suppress = 1;
        if (render_is_marker && pc_hw_native_marker && render_is_marker(p)) {
            int clip[4] = { clip_x, clip_y, clip_w, clip_h };
            { int v[8] = { (int)p[2], draw_x, draw_y, clip[0], clip[1], clip[2], clip[3], (int)texture_window | (int)mask_set << 24 };
              stream_record(3, v, 8); }
            pc_hwc_native_marker(p[2], draw_x, draw_y, clip, texture_window, (int)mask_set);
        }
        {
            int run, run_count;
            if (pc_frame_marker && pc_frame_marker(p, &run, &run_count)) {
                intptr_t mine = key_delta;
                stream_set_on(stream_was);
                pc_frame_draw_run(run, run_count);
                stream_set_on(0);
                key_delta = mine;
            }
        }
        uint32_t list;
        int first, count;
        if (render_translucent_marker && pc_hw_native_translucent && render_translucent_marker(p, &list, &first, &count)) {
            int clip[4] = { clip_x, clip_y, clip_w, clip_h };
            { int v[9] = { (int)list, first, count, draw_x, draw_y, clip[0], clip[1], clip[2], clip[3] };
              stream_record(4, v, 9); }
            pc_hwc_native_translucent(list, first, count, draw_x, draw_y, clip, texture_window, (int)mask_set);
        }
        pc_gpu_immediate = !chain;
        drawing_tag = chain ? p : NULL; /* (the frame's copy: what follows it is the frame's next entry) */
        walk_precise = en ? attr->precise : -2;
        commands(p + 1, tag >> 24);
        stream_set_on(stream_was);
        walk_precise = -2;
        drawing_tag = NULL;
        pc_gpu_immediate = 0;
        if (!chain) /* (an adopted table ends with its entries: a marker taken in by its slot has no link to follow) */
            break;
    }
    key_delta = outer_delta;
}

extern int pc_title_input_ready; /* (below) */
void pc_title_menu_ready(void)
{
    pc_title_input_ready = 1;
    pc_present_now();
    pc_log("TITLE MENU READY: original game entered title input loop");
    if (intro_skipped_by_setting) {
        /* Later titles (after the idle demo, the ending) play as the original. */
        if (intro_skipped_by_setting == 2)
            pc_skip_movies = 0;
        intro_skipped_by_setting = pc_skip_logos = 0;
    }
    if (stop_at_title) {
        pc_log("title checkpoint reached (stop at title): exit(0)");
        exit(0);
    }
}


extern void pc_rpc_poll(void);
extern void pc_cd_tick(void) __attribute__((weak));
extern void pc_fog_vsync(void) __attribute__((weak)); /* atmosphere.cpp: the interactive fog steps */
static unsigned vsyncs;
unsigned pc_vsync_count(void) { return vsyncs; }
unsigned pc_battle_frame_count(void) { return battle_frames; }
/* Developer RPC screenshot: the displayed frame as a binary PPM. */
int pc_capture_display(const char* path, int* width, int* height)
{
    if (pc_hw_sync)
        pc_present_post(&(pc_present_request) { PC_PRESENT_SYNC, vram, display_x, display_y, display_w, display_h });
    FILE* f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", display_w, display_h);
    for (int y = 0; y < display_h; y++) {
        unsigned char row[1024 * 3];
        for (int x = 0; x < display_w && x < 1024; x++)
            display_pixel(x, y, row + x * 3);
        fwrite(row, 3, display_w < 1024 ? display_w : 1024, f);
    }
    *width = display_w;
    *height = display_h;
    return fclose(f) ? -1 : 0;
}
/* Present the current frame again (paused developer view). */
void pc_window_redraw(void) { pc_window_frame(vram, display_x, display_y, display_w, display_h); }
/* Battle charging skip (charge_skip.c): this vsync judged (before its pacing),
 * and whether it is presented. */
extern void pc_charge_skip_vsync(void) __attribute__((weak));
extern int pc_charge_skip_hidden(void) __attribute__((weak));
extern int pc_photo_rendering __attribute__((weak));       /* photo.cpp: a photo mode render runs */
extern void pc_state_diff_vsync(void) __attribute__((weak));
static void tick_frame(void);
static void tick_frame_work(void);
/* One vsync of the host, called by the game through pc_vsync_boundary
 * (boot_sdk.c's pc_vblank_wait, snapshot.c). */
void pc_tick(void)
{
    ++vsyncs;
    if (vsyncs == 2 && getenv("ROOD_STACK_SELFTEST"))
        stack_selftest("the host tick");
    stream_close(vsyncs);
    stream_vsync();
    pc_hw_meter_vsync();
    pc_hwc_frame_room();
    pc_cost_vsync();
    if (pc_state_diff_vsync)
        pc_state_diff_vsync(); /* compare the completed frame before RPC changes */
    pc_rpc_poll();
    if (pc_engine_vsync)
        pc_engine_vsync(); /* an engine reload the RPC asked for, here: no engine frame is on the stack */
    tick_frame();
}
/* snapshot.c: a resumed save state continues the vsync it was saved in
 * (inside pc_rpc_poll) on this process's frames, then returns to the game. */
void pc_tick_resumed(void)
{
    pc_rpc_poll();
    if (pc_engine_vsync)
        pc_engine_vsync();
    tick_frame();
}
static void tick_frame(void)
{
    uint64_t cost = pc_cost_begin();
    tick_frame_work();
    pc_cost_game_resumes(cost);
}
static void tick_frame_work(void)
{
    /* A photo mode render (photo.cpp): the game runs from the frozen moment, nothing to see (no
     * present), no plugin tick (game time stays the moment's), no charging skip; then it is rewound. */
    int photo = &pc_photo_rendering && pc_photo_rendering;
    if (pc_fog_vsync)
        pc_fog_vsync(); /* once per game vsync, after the RPC's changes, before the frame */
    if (pc_lua_tick && !photo)
        pc_lua_tick(); /* plugins' tick handlers and game-time timers, after the RPC's changes */
    if (pc_charge_skip_vsync && !photo)
        pc_charge_skip_vsync();
    pc_window_tick();
    if (pc_cd_tick)
        pc_cd_tick(); /* before the audio: this field's XA sectors */
    pc_audio_frame();
    if (photo || (pc_charge_skip_hidden && pc_charge_skip_hidden())) {
        /* Not presented: the window keeps the last picture; the frame ends as at a present. */
        if (&pc_hw_active && pc_hw_active)
            pc_hwc_skip_present();
        return;
    }
    /* The images of this vsync, and its fractions when interpolating, are the present service's (present_service.h). */
    pc_present_post(&(pc_present_request) { PC_PRESENT_FRAME, vram, display_x, display_y, display_w, display_h });
}

/* What the game is in, for host features that only belong in play (post.h:
 * depth of field): 1 the title (from boot, and again on every return to it,
 * boot_sdk.c vs_overlay_jumpToTitle), 2 an attract demo (the title's idle
 * cutscenes, modes 3 and 4), 0 a game started from the title. */
int pc_game_phase = 1;
/* The title menu takes input (from pc_title_menu_ready until a mode starts): the cutscene player's
 * New Game recipe presses Start then (cutscene.cpp). */
int pc_title_input_ready;
void pc_title_start(int state)
{
    pc_title_input_ready = 0;
    fprintf(stderr, "[title] starting mode %d (0=new game, 3/4=idle cutscene)\n", state);
    pc_game_phase = state == 3 || state == 4 ? 2 : 0;
}
int pc_title_cursor; /* the title menu's item under the cursor (0 New Game, 1 Continue, ...) */
void pc_title_selection(int item)
{
    pc_title_cursor = item;
    fprintf(stderr, "[title] selected menu item %d\n", item);
}

/* How far into the field the game thinks it is. The game reads this as a per-frame budget (the AI's actor
 * loop, the credits' script): a fixed step per read from each vblank, never the host's clock, so how much
 * the game does in a frame does not depend on how fast the machine is that frame. */
/* The cutscene player's take (cutscene.cpp) logs what this returned per vsync and, replaying a part of
 * the take, returns the logged values instead: the one host-clock input of game state, made exact. */
extern int pc_take_scanlines(int value) __attribute__((weak));
static int scanlines_now(void)
{
    static unsigned lines, vblank;
    if (vblank != vsyncs) {
        vblank = vsyncs;
        lines = 0;
    }
    lines += 8;
    return lines < 263 ? (int)lines : 262; /* an NTSC field's lines */
}
int vs_core_elapsedFrameCounterTicks(void)
{
    int value = scanlines_now();
    return pc_take_scanlines ? pc_take_scanlines(value) : value;
}

/* The game's code pinned for save states (64 bits: pc/tools/snapshot_layout.py). */
extern char __start_rood_game_text[] __attribute__((weak)), __stop_rood_game_text[] __attribute__((weak));
/* The executable's own code (its image), whatever its size: not a PS1 address. */
static int native_code(uintptr_t address)
{
#ifdef _WIN32
    return address >= (uintptr_t)__ImageBase && address < ROOD_GUEST_RAM_BASE;
#else
    extern char __executable_start[], etext[];
    return address >= (uintptr_t)__executable_start && address < (uintptr_t)etext;
#endif
}
void pc_require_native_callback(unsigned address, const char* context, unsigned opcode)
{
    int pinned = __start_rood_game_text && address >= (uintptr_t)__start_rood_game_text
              && address < (uintptr_t)__stop_rood_game_text;
    if (!pinned && !native_code(address)) {
        fprintf(stderr, "[callback] %s: opcode 0x%02x, target 0x%08x\n", context, opcode,
            address);
        pc_unimplemented(context);
    }
}

/* EFFPURGE's _configurePlg calls this for every 0 entry. The original table
 * has NULL entries on purpose (the PS1 stores NULL for that slot): only an
 * effect with an entry that has no native function stops here. */
extern unsigned char pc_plg_effect_unresolved[] __attribute__((weak));
extern unsigned pc_plg_effect_count __attribute__((weak));
void pc_effect_plugin_unimplemented(unsigned effect)
{
    if (&pc_plg_effect_count && effect < pc_plg_effect_count && !pc_plg_effect_unresolved[effect])
        return;
    fprintf(stderr, "[effect] native PLG renderer required for effect %u\n", effect);
    pc_unimplemented("effect-specific PLG callback relocation");
}

/* Observe the original opening card (GIM 26) at full brightness. No script,
 * visibility, camera or timing state is changed to reach this checkpoint. */
void pc_opening_image_ready(int image)
{
    if (image == 26)
        opening_ready = 1;
}
/* ui/ashley_bridge.c (battle builds): whether the ordering table just drawn
 * holds the modern menu's Ashley model; gpu_hw.c tags the frame with it. */
extern int pc_ui_ashley_ot_model(void) __attribute__((weak));
extern void pc_hwc_frame_menu_model(int on) __attribute__((weak));
void pc_battle_frame_complete(void)
{
    if (pc_ui_ashley_ot_model && pc_hwc_frame_menu_model)
        pc_hwc_frame_menu_model(pc_ui_ashley_ot_model());
    ++battle_frames;
    if (battle_frames % 300 == 0)
        fprintf(
            stderr, "[boot] original battle loop completed %u frames\n", battle_frames);
    if (stop_at_battle_frame && battle_frames >= stop_at_battle_frame) {
        display_x = clip_x;
        display_y = clip_y;
        display_w = clip_w;
        display_h = clip_h;
        pc_present_now();
        if (pc_hw_sync) {
        pc_present_post(&(pc_present_request) { PC_PRESENT_SYNC, vram, display_x, display_y, display_w, display_h });
        pc_present_now();
    }
    if (pc_replace_file(pc_out_path("loading.ppm"), pc_out_path("battle-checkpoint.ppm"))) {
            perror("battle checkpoint");
            exit(1);
        }
        fprintf(stderr,
            "[boot] BATTLE FRAME CHECKPOINT: %u original frames rendered; not a gameplay completion claim\n",
            battle_frames);
        exit(0);
    }
    if (!stop_at_opening || !opening_ready)
        return;
    /* Allow the original caption fade to complete over normal battle frames. */
    if (++opening_frames < 60)
        return;
    display_x = clip_x;
    display_y = clip_y;
    display_w = clip_w;
    display_h = clip_h;
    pc_present_now();
    if (pc_hw_sync) {
        pc_present_post(&(pc_present_request) { PC_PRESENT_SYNC, vram, display_x, display_y, display_w, display_h });
        pc_present_now();
    }
    if (pc_replace_file(pc_out_path("loading.ppm"), pc_out_path("opening-checkpoint.ppm"))) {
        perror("opening checkpoint");
        exit(1);
    }
    pc_log(
        "OPENING SCRIPT CHECKPOINT: original VKP Headquarters card rendered; not gameplay");
    exit(0);
}
