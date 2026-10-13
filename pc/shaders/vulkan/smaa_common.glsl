#ifndef ROOD_SMAA_COMMON_GLSL
#define ROOD_SMAA_COMMON_GLSL
// SMAA 1x (Jimenez et al., the reference shader pc/third_party/smaa/SMAA.hlsl,
// MIT) through its GLSL porting macros; shared by the three passes
// (smaa_edges.frag, smaa_weights.frag, smaa_blend.frag). Every pass runs on
// fullscreen.vert and computes SMAA's vertex-stage offsets per pixel.
layout(push_constant) uniform Push {
    vec4 metrics; // 1 / width, 1 / height, width, height of the image
    ivec4 rect; // the display area the image shows (VRAM pixels)
    ivec4 info; // x: the target's scale, y: geometric edges only (aa_geometry.glsl)
} pc;
#define SMAA_RT_METRICS pc.metrics
#define SMAA_GLSL_4 1
#define SMAA_PRESET_HIGH 1
#include "SMAA.hlsl"
#endif
