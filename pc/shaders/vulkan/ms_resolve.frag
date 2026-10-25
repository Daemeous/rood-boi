#version 450
#extension GL_GOOGLE_include_directive : require
// MSAA (antialias.h, vk_hw.c): the multisampled 3D back into the
// single-sampled target (ms_resolve.glsl).
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normal_out;
layout(location = 2) out vec4 albedo_out;
layout(location = 3) out vec4 shade_out;
layout(location = 4) out vec4 material_out;
layout(binding = 0) uniform sampler2DMS color_ms;
layout(binding = 1) uniform sampler2DMS normal_ms;
layout(binding = 2) uniform sampler2DMS albedo_ms;
layout(binding = 3) uniform sampler2DMS shade_ms;
layout(binding = 4) uniform sampler2DMS depth_ms;
layout(binding = 5) uniform sampler2DMS material_ms; // material.glsl
layout(push_constant) uniform Push {
    int samples;
} pc;
#include "ms_resolve.glsl"
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float depth;
    ms_resolve(p, pc.samples, color, normal_out, albedo_out, shade_out, depth);
    gl_FragDepth = depth;
    // The material of the representative sample (the one whose depth it took).
    material_out = texelFetch(material_ms, p, 0);
    for (int i = 0; i < pc.samples; i++)
        if (texelFetch(depth_ms, p, i).r == depth) { material_out = texelFetch(material_ms, p, i); break; }
}
