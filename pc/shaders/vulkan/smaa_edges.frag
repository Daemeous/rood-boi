#version 450
#extension GL_GOOGLE_include_directive : require
// SMAA pass 1: colour edges (the reference SMAAColorEdgeDetectionPS) kept
// only where the geometry breaks (aa_geometry.glsl), so texel boundaries and
// the 2D/UI never get blended. The edges image is cleared first: the
// reference discards pixels without edges.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec2 edges;
layout(binding = 0) uniform sampler2D color_tex;
layout(binding = 1) uniform sampler2D aa_depth_tex;
layout(binding = 2) uniform sampler2D aa_normal_tex;
#include "smaa_common.glsl"
#define AA_RECT pc.rect
#define AA_SCALE pc.info.x
vec2 aa_picture(vec2 st) { return st; }
#include "aa_geometry.glsl"
void main() {
    vec4 offset[3];
    SMAAEdgeDetectionVS(uv, offset);
    vec2 e = SMAAColorEdgeDetectionPS(uv, offset, color_tex);
    if (pc.info.y != 0)
        e *= aa_geometry_edges(uv, pc.metrics.xy);
    if (dot(e, vec2(1.0)) == 0.0)
        discard;
    edges = e;
}
