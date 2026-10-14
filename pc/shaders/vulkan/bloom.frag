#version 450
#extension GL_GOOGLE_include_directive : require
// Bloom's image passes and the exposure meter (vk_present.c; post_bloom.glsl):
// mode 0 the prefilter (lit image -> half size, bright part), 1 down a level,
// 2 up a level (this level + a tent of the smaller one, x scale), 3 the
// convolution's FFT domain (a padded square: the picture in its middle),
// 4 the meter.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D source;
layout(binding = 1) uniform sampler2D level; // mode 2: this level's own downsample
layout(binding = 2) uniform sampler2D unused_;
layout(push_constant) uniform Push {
    vec4 p0; // source texel size, mode, scale
    vec4 p1; // exposure, threshold, knee, tent radius
    vec4 p2; // mode 3: the picture's offset and size in the domain (domain uv)
} pc;
#include "post_bloom.glsl"
void main()
{
    int mode = int(pc.p0.z);
    vec2 texel = pc.p0.xy;
    if (mode == 0 || mode == 1) {
        color = vec4(pb_down(source, uv, texel, mode == 0, pc.p1.xyz), 1.0);
    } else if (mode == 2) {
        color = vec4((texture(level, uv).rgb + pb_up(source, uv, texel, pc.p1.w)) * pc.p0.w, 1.0);
    } else if (mode == 3) {
        vec2 p = (uv - pc.p2.xy) / pc.p2.zw;
        color = any(lessThan(p, vec2(0.0))) || any(greaterThan(p, vec2(1.0))) ? vec4(0.0)
                                                                               : vec4(texture(source, p).rgb, 0.0);
    } else {
        color = vec4(pb_meter(source, uv, texel), 0.0, 1.0);
    }
}
