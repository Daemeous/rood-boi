// The probe volume of the probe-based GI (DDGI; probe_gi.inc, vk_probes.inc),
// a std140 block shared by the update passes (Vulkan and OpenGL 4.3) and
// the Vulkan present pass. The OpenGL 3.3 present shader declares the
// members it samples with as plain uniforms of the same names instead.
#ifndef PROBE_VOLUME_GLSL
#define PROBE_VOLUME_GLSL
layout(std140, binding = 12) uniform ProbeVolume {
    vec4 pv_origin; // world position of probe (0, 0, 0); w: 1 when the field is valid
    vec4 pv_spacing; // xyz: spacing per axis; w: the largest (depth normalization)
    ivec4 pv_counts; // probes per axis x, y, z; atlas columns
    ivec4 pv_info; // probe count, rays per probe, update index, flags (PV_*)
    vec4 pv_bias; // normal bias, view bias (world units), max ray distance, depth sharpness
    vec4 pv_blend; // hysteresis (irradiance), hysteresis (depth), change threshold, multi-bounce gain
    vec4 pv_rotation[3]; // this update's ray rotation (columns)
    vec4 pv_flicker[16]; // per light: x luminance at its base intensity, y current / base intensity
    ivec4 pv_bvh; // BVH roots (room, casters: -1 none), light count
    vec4 pv_relocate; // min front-face distance, backface fraction, max offset (fraction of spacing), unused
    vec4 pv_eye; // the camera (probe_apply.comp's view bias)
};
#endif
