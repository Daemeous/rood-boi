/* The dynamic lights as the Vulkan renderer reads them: every light of the
 * room in one storage buffer (no fixed budget; pc/docs/Vulkan_Renderer.md,
 * "Many lights"). Layout = the shaders' `Light` (scaled.frag, std430) and
 * rt_lights.glsl's (the first five vectors and info). */
#ifndef ROOD_LIGHT_LIST_H
#define ROOD_LIGHT_LIST_H

/* Lights per frame the Vulkan paths take (a room has at most a few hundred). */
#define PC_LIGHTS_MAX 1024

typedef struct {
    float pos[4]; /* world xyz, radius */
    float col[4]; /* rgb * intensity, type (0 point, 1 spot, 2 rect) */
    float right[4], up[4]; /* rect half extents; right.w: own emitting radius; up.w: shadow skip */
    float dir[4]; /* direction, cos(half cone) */
    float world[4]; /* present: shadow map lookups (position, radius) */
    int info[4]; /* x: shadows (trace: casts; present: -2 traced, >= 0 shadow map slot, -1 none) */
} pc_light_gpu;

#ifdef __cplusplus
extern "C" {
#endif
/* All the renderers' lights this frame (pc_lights_world with no budget),
 * packed; returns the count (at most max). */
int pc_lights_list(pc_light_gpu* out, int max);
/* Of `count` lights just listed (pc_lights_list order), keep `wants` set only
 * for the `slots` most important shadow casters (hysteresis: no popping).
 * For shadow maps, which have a fixed number of slots. */
void pc_lights_shadow_pick(int count, int* wants, int slots);
#ifdef __cplusplus
}
#endif
#endif
