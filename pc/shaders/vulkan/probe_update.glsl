// The probe GI's update passes (Vulkan and OpenGL 4.3 compute): the volume,
// the per-probe state and this update's ray directions.
#ifndef PROBE_UPDATE_GLSL
#define PROBE_UPDATE_GLSL
#include "probe_volume.glsl"
#include "probe_common.glsl"
// Per probe: xyz offset from its grid point (relocation), w 1 active, 0 inactive.
layout(std430, binding = 8) buffer ProbeState { vec4 probe_state[]; };
// Ray i of n on the unit sphere (spherical Fibonacci), turned by this update's rotation.
vec3 probe_ray_direction(int i, int n) {
    const float golden = 0.61803398875;
    float phi = 6.28318530718 * fract(float(i) * golden);
    float cos_theta = 1.0 - (2.0 * float(i) + 1.0) / float(n);
    float sin_theta = sqrt(clamp(1.0 - cos_theta * cos_theta, 0.0, 1.0));
    vec3 d = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    return normalize(mat3(pv_rotation[0].xyz, pv_rotation[1].xyz, pv_rotation[2].xyz) * d);
}
vec3 probe_position(int p) { return probe_grid_position(probe_coords(p)) + probe_state[p].xyz; }
#endif
