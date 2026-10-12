#version 450
#extension GL_GOOGLE_include_directive : require
// Supersampling's reduction (aa ssaa): the lit image, drawn at the internal
// resolution, filtered down to the view (downsample.glsl).
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D src;
layout(push_constant) uniform Push {
    vec2 src_size;
    vec2 dst_size;
    int filter_;
} pc;
#include "downsample.glsl"
void main() {
    color = vec4(downsample(src, uv, pc.src_size, pc.dst_size, pc.filter_), 1.0);
}
