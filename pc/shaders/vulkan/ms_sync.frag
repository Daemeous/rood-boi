#version 450
// MSAA (vk_hw.c): the multisampled native surfaces' depth into the
// single-sampled one that shaders read (sample 0 of each pixel).
layout(binding = 0) uniform sampler2DMS depth_ms;
void main() {
    gl_FragDepth = texelFetch(depth_ms, ivec2(gl_FragCoord.xy), 0).r;
}
