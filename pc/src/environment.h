/* The room's environment: a directional sun (or moon) and a sky
 * (pc/src/environment.cpp; lighting/README.md "Environment"; the lighting
 * docs: pc/docs/Vulkan_Renderer.md "Sun and sky").
 *
 * Per room, from the lighting layers: `env sun` and `env sky` items
 * (generated for rooms open to the sky by pc/tools/generate_lighting.py,
 * overridden by id like any item). A room without them has neither.
 *
 * Renderers read one packed record per frame (pc_env_gpu_get): five vec4s
 * appended to the trace UBOs (rt_lights.glsl `Trace`, probe_gi.inc
 * probe_trace_ubo) and the present UBO, uniforms on OpenGL; the GLSL in
 * pc/shaders/vulkan/environment.glsl reads them (sky radiance, the sun's
 * cone, the non-traced ambient term). World space, y down. */
#ifndef ROOD_ENVIRONMENT_H
#define ROOD_ENVIRONMENT_H

typedef struct {
    float sun_dir[4]; /* toward the sun (unit, world, y down); w: angular radius (radians) */
    float sun_col[4]; /* rgb * intensity (0: no sun); w: casts shadows (1) */
    float zenith[4]; /* sky radiance straight up, rgb * intensity; w: 1 sky on */
    float horizon[4]; /* at the horizon; w: how much the sun tints the sky around it */
    float ground[4]; /* below the horizon; w: strength of the ambient term where nothing is traced */
} pc_env_gpu;

#ifdef __cplusplus
extern "C" {
#endif
/* This frame's environment for the renderers (zeros when off: no sun, no
 * sky). Returns 1 when the sun or the sky is on. */
int pc_env_gpu_get(pc_env_gpu* out);
int pc_env_open_sky(void);
/* A hash of what changes the traced light (sun, sky): history resets. */
unsigned pc_env_hash(void);
/* The global switch (settings key `environment`, RPC environment {global}). */
extern int pc_env_global;
#ifdef __cplusplus
}
#endif
#endif
