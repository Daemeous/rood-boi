#version 450
// MSAA (vk_hw.c): a single-sampled depth image (the native surfaces' depth)
// into every sample of its multisampled twin.
layout(binding = 0) uniform sampler2D depth_tex;
void main() {
    gl_FragDepth = texelFetch(depth_tex, ivec2(gl_FragCoord.xy), 0).r;
}
