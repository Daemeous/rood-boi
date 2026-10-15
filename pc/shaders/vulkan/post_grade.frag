#version 450
#extension GL_GOOGLE_include_directive : require
// The post stack's grade pass (vk_present.c; post_grade.glsl): the lit image
// (after anti-aliasing and depth of field) graded into the next image or the
// window's view. 2D/UI pixels (alpha 9999) pass through untouched.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(std140, binding = 0) uniform Grade {
    vec4 pg_[40]; // post.h pc_post_gpu
} u;
layout(binding = 1) uniform sampler2D image;
layout(binding = 2) uniform sampler2D bloom_tex;
layout(binding = 3) uniform sampler2D lut_tex;
layout(push_constant) uniform Push {
    ivec2 origin; // the output's top-left corner (the window's view, or 0)
    int last; // writing the window: alpha 1
} pc;
#define PG(i) u.pg_[i]
vec4 pg_input(vec2 st) { return texture(image, st); }
vec3 pg_bloom(vec2 uv) { return texture(bloom_tex, uv * PG(25).xy + PG(25).zw).rgb; }
vec3 pg_lut(vec3 c)
{
    float n = PG(8).w, b = c.b * (n - 1.0), b0 = floor(b);
    vec2 p = vec2((c.r * (n - 1.0) + 0.5 + b0 * n) / (n * n), (c.g * (n - 1.0) + 0.5) / n);
    vec3 lo = texture(lut_tex, p).rgb, hi = texture(lut_tex, p + vec2(b0 < n - 1.0 ? 1.0 / n : 0.0, 0.0)).rgb;
    return mix(lo, hi, b - b0);
}
#include "post_grade.glsl"
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy) - pc.origin;
    vec4 c0 = texelFetch(image, p, 0);
    if (c0.a > 9000.0) {
        color = vec4(c0.rgb, pc.last != 0 ? 1.0 : c0.a);
        return;
    }
    vec2 at = (vec2(p) + 0.5) / PG_LIT;
    color = vec4(pg_pixel(c0, at, at, vec2(1.0)), pc.last != 0 ? 1.0 : 0.0);
}
