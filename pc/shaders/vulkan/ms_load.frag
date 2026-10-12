#version 450
// MSAA (antialias.h, vk_hw.c): the single-sampled target's colours, G-buffer
// and depth over a drawing area into every sample of the multisampled images
// before 3D is drawn there (the images share the target's pixel coordinates).
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normal_out;
layout(location = 2) out vec4 albedo_out;
layout(location = 3) out vec4 shade_out;
layout(binding = 0) uniform sampler2D color_tex;
layout(binding = 1) uniform sampler2D normal_tex;
layout(binding = 2) uniform sampler2D albedo_tex;
layout(binding = 3) uniform sampler2D shade_tex;
layout(binding = 4) uniform sampler2D depth_tex;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    color = texelFetch(color_tex, p, 0);
    normal_out = texelFetch(normal_tex, p, 0);
    albedo_out = texelFetch(albedo_tex, p, 0);
    shade_out = texelFetch(shade_tex, p, 0);
    gl_FragDepth = texelFetch(depth_tex, p, 0).r;
}
