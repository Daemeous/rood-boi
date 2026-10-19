/* Frame cost (ROOD_PERF=1): where the host's time goes per game vsync, by
 * wall clock, without a sampling profiler. Sections are timed around the
 * passes that run on the game thread (the CPU rasterizer, the emissive
 * matching, VRAM uploads, read-backs, fence waits, the presenter); counters
 * count what they did (tiles uploaded, texels hashed, read-backs). RPC perf
 * reports both per vsync since its last {reset: true} ("cpu_sections",
 * "cpu_counts"; lighting_quality.cpp pc_perf_json). With ROOD_PERF unset
 * every call is a load and a branch. */
#ifndef ROOD_FRAME_COST_H
#define ROOD_FRAME_COST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_COST_GAME,      /* the game between ticks (its logic and drawing) */
    PC_COST_HOST,      /* the host's vsync work (pc_tick: pacing, audio, presenting) */
    PC_COST_DRAW,      /* ordering tables and packets (pc_draw_packet), mirror included */
    PC_COST_RASTER,    /* the CPU rasterizer's pixel loops (native VRAM) */
    PC_COST_EMISSIVE,  /* emissive matching (pc_emissive_sync) */
    PC_COST_UPLOAD,    /* native VRAM tiles copied for the GPU */
    PC_COST_READBACK,  /* the GPU target read back into native VRAM (pc_hw_sync) */
    PC_COST_GPU_COPY,  /* the target copied into the GPU's VRAM on the GPU (no read-back) */
    PC_COST_FENCE,     /* waiting for a frame in flight (Vulkan fences) */
    PC_COST_PRESENT,   /* the presenter (pc_window_frame) */
    PC_COST_DEBUG_PPM, /* the display's debug image (loading.ppm) */
    PC_COST_SECTIONS
};
enum {
    PC_COUNT_UPLOAD_KB,  /* KB of native VRAM uploaded */
    PC_COUNT_UPLOADS,    /* upload calls (each a copy into the frame's arena) */
    PC_COUNT_READBACKS,  /* read-backs (each waits for the GPU) */
    PC_COUNT_GPU_COPIES, /* target-to-VRAM copies on the GPU (instead of read-backs) */
    PC_COUNT_HASHED,     /* texels fingerprinted by the emissive matching */
    PC_COUNT_DEBUG_PPM,  /* debug images written */
    PC_COUNT_PASSES,     /* Vulkan: rendering begun (each a render pass on the GPU) */
    PC_COUNT_BARRIERS,   /* Vulkan: full memory barriers recorded */
    PC_COUNT_RASTER_PX,  /* pixels the CPU rasterizer visited (bounding boxes) */
    PC_COUNT_SKIPPED_PX, /* pixels it left out (stale: the target's replace them; pc_hw_raster_skip) */
    PC_COUNTERS
};

extern int pc_cost_on; /* ROOD_PERF set (read at start) */
uint64_t pc_cost_clock(void); /* monotonic ns */
static inline uint64_t pc_cost_begin(void) { return pc_cost_on ? pc_cost_clock() : 0; }
void pc_cost_end(int section, uint64_t start);
void pc_cost_count(int counter, unsigned n);
/* pc_tick: the game reached a vsync (its section ends; an RPC pause after
 * this is nobody's cost). */
void pc_cost_vsync(void);
/* pc_tick's host work is over (it began at `start`): the game runs again. */
void pc_cost_game_resumes(uint64_t start);
/* `,"cpu_vsyncs":N,"cpu_sections":{...},"cpu_counts":{...}`: ms and counts
 * per vsync since the last reset ("vsync" = "game" + "host": the frame's
 * wall time unpaced); `extra` adds sections measured elsewhere (e.g.
 * "\"particles\":0.1000") or NULL. Returns the bytes written. */
int pc_cost_json(char* out, int size, int reset, const char* extra);

#ifdef __cplusplus
}
#endif
#endif
