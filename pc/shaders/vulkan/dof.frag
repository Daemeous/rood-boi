#version 450
#extension GL_GOOGLE_include_directive : require
// Depth of field's passes (vk_present.c; dof.glsl): the pass in DP(1).x.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D src0;
layout(binding = 1) uniform sampler2D src1;
layout(binding = 2) uniform sampler2D src2;
layout(push_constant) uniform Push {
    vec4 dp[8]; // post.h pc_post_dof_gpu
} pc;
#define DP(i) pc.dp[i]
#include "dof.glsl"
void main() { color = dof_main(src0, src1, src2, uv); }
