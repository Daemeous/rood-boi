#version 450
#extension GL_GOOGLE_include_directive : require
// MSAA (antialias.h, vk_hw.c): the multisampled 3D back into the
// single-sampled target (ms_resolve.glsl).
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normal_out;
layout(location = 2) out vec4 albedo_out;
layout(location = 3) out vec4 shade_out;
layout(binding = 0) uniform sampler2DMS color_ms;
layout(binding = 1) uniform sampler2DMS normal_ms;
layout(binding = 2) uniform sampler2DMS albedo_ms;
layout(binding = 3) uniform sampler2DMS shade_ms;
layout(binding = 4) uniform sampler2DMS depth_ms;
layout(push_constant) uniform Push {
    int samples;
} pc;
#include "ms_resolve.glsl"
void main() {
    ms_resolve(ivec2(gl_FragCoord.xy), pc.samples, color, normal_out, albedo_out, shade_out, gl_FragDepth);
}
