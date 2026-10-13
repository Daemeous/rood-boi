#version 450
#extension GL_GOOGLE_include_directive : require
// SMAA pass 3: each pixel blended with its neighbours across its edges by the
// weights (the reference SMAANeighborhoodBlendingPS). Alpha (the depth of
// field's blur radius, 9999 on UI) passes through: no edge ever touches UI.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_tex; // bilinear
layout(binding = 1) uniform sampler2D blend_tex; // bilinear
#include "smaa_common.glsl"
void main() {
    vec4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    color = SMAANeighborhoodBlendingPS(uv, offset, color_tex, blend_tex);
}
