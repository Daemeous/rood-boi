/* Hardware renderer hook meter (T1 plan R0, pc/docs/plans/2026-11-12-t1-renderer-thread.md): counts, times and classifies
 * every pc_hw_* hook the game thread calls into the renderer, per frame, so the split of the draw walk into a producer
 * (commands) and a consumer (the renderer) can be sized before it is built.
 *
 *   ROOD_HW_METER=1        count every hook (RPC hw_meter reports them); with ROOD_PERF=1 also time them
 *   ROOD_HW_QUEUE_CHECK=1  as above, and at each vsync report any hook called inside the draw walk that is not a command,
 *                          native-list hand-over, query or VRAM event (the classes the plan's section 1 lists)
 *
 * Classes: A draw command or state setter of the walk; N native list hand-over; Q query whose value the game acts on;
 * V VRAM event or read-back; P the present path; S setting or replay value; T tooling, init and readers.
 * A, N, Q and V are "listed": what the producer must be able to record. Off with both variables unset: a load and a branch. */
#ifndef ROOD_HW_METER_H
#define ROOD_HW_METER_H
#include "hw_cmd.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PC_HW_HOOKS(X) \
    X(set_2d, 'A') \
    X(set_narrowed, 'A') \
    X(set_flipped_uv, 'A') \
    X(set_bounds, 'A') \
    X(set_attr, 'A') \
    X(drain, 'S') \
    X(casters, 'N') \
    X(caster_surfaces, 'N') \
    X(emissives, 'N') \
    X(emissive_sources, 'N') \
    X(end_ot, 'A') \
    X(begin_ot, 'A') \
    X(set_precise, 'A') \
    X(native_vram, 'T') \
    X(renderer_vram, 'T') \
    X(init, 'T') \
    X(mark, 'V') \
    X(raster_post, 'V') \
    X(mark_raster, 'V') \
    X(set_depth, 'A') \
    X(triangle, 'A') \
    X(rect, 'A') \
    X(shape_triangle, 'A') \
    X(native_per_output, 'N') \
    X(line, 'A') \
    X(fill, 'V') \
    X(upload, 'V') \
    X(copy, 'V') \
    X(set_scale, 'S') \
    X(supersample, 'S') \
    X(detail_view, 'S') \
    X(resync, 'V') \
    X(native_marker, 'A') \
    X(native_translucent, 'A') \
    X(sync, 'V') \
    X(sync_store, 'V') \
    X(stale_copy, 'V') \
    X(ot_native_area, 'A') \
    X(raster_skip, 'Q') \
    X(sync_texture, 'V') \
    X(upload_mode, 'V') \
    X(set_packet, 'A') \
    X(present_world, 'P') \
    X(frame_menu_model, 'A') \
    X(present_menu_model, 'P') \
    X(present_menu_studio, 'P') \
    X(present_fade, 'P') \
    X(before_write, 'V') \
    X(replay_camera, 'S') \
    X(replay_projection, 'S') \
    X(view_camera, 'S') \
    X(set_display, 'P') \
    X(redraw, 'P') \
    X(redraw_present, 'P') \
    X(text_overlay, 'P') \
    X(text_overlay_held, 'P') \
    X(text_overlay_stats, 'P') \
    X(display_rect, 'P') \
    X(present_origin, 'P') \
    X(target, 'P') \
    X(frame_serial, 'P') \
    X(skip_present, 'P') \
    X(present_lists, 'P') \
    X(present_all_lists, 'P') \
    X(depth_texture, 'T') \
    X(gbuffer, 'T') \
    X(shade_texture, 'T') \
    X(present_done, 'P') \
    X(dump, 'T') \
    X(pick, 'T') \
    X(native_texture, 'T') \
    X(photo_prepare, 'T') \
    X(photo_drop, 'T') \
    X(photo_mark, 'T') \
    X(photo_restore, 'T')

enum {
#define X(name, cls) PC_HW_##name,
    PC_HW_HOOKS(X)
#undef X
    PC_HW_HOOK_COUNT
};

extern int pc_hw_meter_on;
typedef struct {
    int id;
    unsigned long long start;
} pc_hw_meter_scope;
pc_hw_meter_scope pc_hw_meter_begin(int id);
void pc_hw_meter_end(pc_hw_meter_scope* scope);
/* First statement of a hook: applies the commands the walk recorded before it (hw_cmd.h), then counts the call and, when
 * timing, the time to its return (every exit). */
#define PC_HW_METER(name) \
    pc_hw_meter_scope pc_hw_meter_scope_ __attribute__((cleanup(pc_hw_meter_end))) = (PC_HWC_SYNC(), pc_hw_meter_begin(PC_HW_##name))

/* As PC_HW_METER for a hook that is the producer's own query (it reads only the frame's tables: pc_hw_raster_skip): it does not
 * apply the recorded commands first. */
#define PC_HW_METER_NOSYNC(name) \
    pc_hw_meter_scope pc_hw_meter_scope_ __attribute__((cleanup(pc_hw_meter_end))) = pc_hw_meter_begin(PC_HW_##name)

/* The reasons a return-value hook gave its answer (pc_hw_triangle, pc_hw_raster_skip), with what each depends on. */
enum {
    PC_HW_WHY_TRI_INACTIVE,         /* the hardware path is off */
    PC_HW_WHY_TRI_SUPPRESSED,       /* suppressed (classic/UI claim) or the free camera's afterimage */
    PC_HW_WHY_TRI_EFFECT_STALE,     /* effect polygon covered by a native list (1) */
    PC_HW_WHY_TRI_DRAWN,            /* reconstructed and queued for the GPU, the CPU rasterizer draws it too (0) */
    PC_HW_WHY_SKIP_INACTIVE,        /* hardware path off, or no stale table this ordering table */
    PC_HW_WHY_SKIP_DISABLED,        /* ROOD_RASTER_SKIP=0 */
    PC_HW_WHY_SKIP_COVERED,         /* the rectangle lies inside a stale area: left out (1) */
    PC_HW_WHY_SKIP_NOT_COVERED,     /* drawn by the CPU rasterizer (0) */
    PC_HW_WHY_OT_STALE_READBACK,    /* not an answer: a stale area of the ordering table dropped because a read-back made it current */
    PC_HW_WHY_COUNT
};
void pc_hw_meter_result(int hook, int why, int value);

/* The draw walk (pc_draw_packet, pc_draw_item_packet): the hooks called inside it are the producer's work. */
void pc_hw_meter_walk(int enter);
/* Each game vsync (pc_tick): the frame's counts are closed. */
void pc_hw_meter_vsync(void);
/* The RPC hw_meter reply; returns the bytes written. */
int pc_hw_meter_json(char* out, int size, int reset);

#ifdef __cplusplus
}
#endif
#endif
