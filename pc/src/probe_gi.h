/* Probe-based global illumination (DDGI; pc/docs/Probe_GI.md): a grid of
 * irradiance probes fitted to the room, updated by ray tracing (Vulkan ray
 * queries, or compute over a BVH on Vulkan without ray tracing and on
 * OpenGL 4.3) and sampled per pixel by the present pass in place of the
 * path tracer's indirect light. Settings keys probe_spacing,
 * probe_rays, probe_hysteresis, probe_budget, probe_show (RPC graphics, same
 * names; RPC probe_gi for state and dumps). gi_method itself is a lighting
 * quality key (lighting_quality.h). */
#ifndef ROOD_PROBE_GI_H
#define ROOD_PROBE_GI_H
#include "lighting_quality.h" /* gi_method: PC_GI_PROBES */
#ifdef __cplusplus
extern "C" {
#endif

/* gi_method (lighting_quality.h: off, screen, probes, path) selects the
 * probe field with PC_GI_PROBES; whether this renderer can run it (the
 * presets fall back without it). */
int pc_probe_gi_available(void);
extern float pc_probe_spacing; /* world units between probes (the grid fits the room; budget-limited) */
extern int pc_probe_rays; /* rays per probe per update (rotated every update) */
extern float pc_probe_hysteresis; /* weight of the history per update (0.97: ~30 updates) */
extern int pc_probe_budget; /* most probes per room (spacing grows to fit) */
extern int pc_probe_show; /* editor: draw the probes (1 active, 2 also inactive ones) */

typedef struct {
    int valid; /* the field is converged enough to sample and matches the room */
    int backend; /* 0 none, 1 Vulkan ray queries, 2 Vulkan compute (BVH), 3 OpenGL compute (BVH) */
    int counts[3], probes, active, rays;
    float origin[3], spacing[3];
    unsigned updates; /* since the room was fitted */
    unsigned converged; /* updates since the last reset (room, light change) */
    unsigned rebuilds, light_resets;
    int sampled; /* the last present sampled the field */
} pc_probe_stats;
void pc_probe_get_stats(pc_probe_stats* out);
/* RPC probe_gi {dump}: the field to `path` (see probe_gi.inc). 0 on success. */
int pc_probe_dump(const char* path);
/* Editor: draw the probes over the view (an ImDrawList*), with the view's
 * column-major view and projection matrices and window rectangle. */
void pc_probe_draw(const float view[16], const float projection[16], const float rect[4], void* draw_list);
/* Developer overlay: the probe settings (lighting quality panel, under GI method). */
void pc_probe_panel(void);

#ifdef __cplusplus
}
#endif
#endif
