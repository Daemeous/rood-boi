/* The port's particle system (pc/docs/Particles.md; not the game's own
 * effect particles): per room up to PC_PARTICLE_EMITTERS emitters
 * (`emitter <id>` items of the lighting layers, lighting/README.md
 * "Emitters"), each a look from an emitter asset (pc/assets/emitters) and
 * its own overrides, and their motes evaluated in closed form each present
 * (particle_math.h), lit, and splatted into a key image (a game pixel per
 * texel: the nearest mote) and a colour per mote that the present shaders
 * composite (pc/shaders/vulkan/particles.glsl). Vulkan: one compute pass
 * (vk_particles.inc); OpenGL: this file's CPU path. */
#ifndef ROOD_PARTICLES_H
#define ROOD_PARTICLES_H
#include <stdint.h>

#include "light_list.h"
#include "particle_math.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Settings (project_settings.cpp; Details > Atmosphere > Dust; RPC particles):
 * dust 0 Off (nothing evaluated or drawn: the picture as before), 1 Low
 * (each "dust" emitter's count x dust_low), 2 High (all); particles_glow
 * scales the glows' light on motes; particles_lights the strongest sources
 * each mote is lit by (1..4, each with a shadow ray), the sun on top;
 * particles_glow_shadows: glows' light is shadowed too; particles_cpu_rays
 * the OpenGL path's shadow rays a frame (its motes refreshed round-robin).
 * RPC only: particles_debug (1: every mote white, unlit: where they are),
 * particles_untraced (1: no shadow rays, as without a tracer: sources that
 * cast shadows give motes no light), particles_time (fixed seconds; -1 live). */
extern int pc_dust, pc_particles_lights, pc_particles_glow_shadows, pc_particles_cpu_rays, pc_particles_debug;
extern int pc_particles_untraced;
extern float pc_dust_low, pc_particles_glow, pc_particles_time;

/* OpenGL: this present's motes for the display area w x h (game pixels):
 * the key image (w x h, PC_PARTICLE_NONE where no mote) and the colours
 * (rgba per mote: rgb its colour, a its light's luminance). Returns 0 when
 * there are none (Dust Off, no emitter, no camera). */
int pc_particles_cpu_frame(int w, int h, const uint32_t** keys, const float** colours, int* count);
/* Vulkan: this present's records for the pass (pc_particles_gpu) and the
 * lights (`lights`, at most `max`); returns the motes (0: no pass). */
int pc_particles_gpu_frame(int w, int h, pc_particles_gpu* out, pc_light_gpu* lights, int max, int* light_count);
/* Vulkan: whether this frame's pass traced its shadow rays (the RPC's path):
 * 0 no, 1 against the path tracer's scene, 2 against one built for it. */
void pc_particles_gpu_traced(int traced);
/* RPC perf (lighting_quality.cpp pc_perf_json, "cpu_sections" "particles"): the
 * particles' CPU time a frame with motes since the last reset (OpenGL: all of
 * it; Vulkan: gathering the records), and how many frames. */
int pc_particles_cpu_perf(double* ms, int reset);
/* RPC particles (dev_rpc.c hands over the params object's text): 0, or -1 and a message. */
int pc_particles_rpc(const char* params, char* out, int size);
#ifdef __cplusplus
}
#endif
#endif
