#version 450
#extension GL_GOOGLE_include_directive : require
// SMAA pass 2: blending weights from the edges' patterns (the reference
// SMAABlendingWeightCalculationPS with its area and search lookup textures).
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 weights;
layout(binding = 0) uniform sampler2D edges_tex; // bilinear (SMAA reads two edges per tap)
layout(binding = 1) uniform sampler2D area_tex; // bilinear
layout(binding = 2) uniform sampler2D search_tex; // nearest
#include "smaa_common.glsl"
void main() {
    vec2 pixcoord;
    vec4 offset[3];
    SMAABlendingWeightCalculationVS(uv, pixcoord, offset);
    weights = SMAABlendingWeightCalculationPS(uv, pixcoord, offset, edges_tex, area_tex, search_tex, vec4(0.0));
}
