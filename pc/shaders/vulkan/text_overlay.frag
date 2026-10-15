#version 450
#extension GL_GOOGLE_include_directive : require
// The text overlay (text_overlay.vert): each pixel's coverage of the glyph's
// edge at the window's resolution, coloured as the packet shader colours it
// (hw_frag.glsl), blended over the presented picture.
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in uvec4 v_state; // flags, tpage, clut, window
layout(location = 3) flat in uvec4 v_bounds;
layout(location = 4) flat in ivec2 v_ui;
layout(push_constant) uniform Push {
    vec4 rect;
    int pass, ui_scale;
    float text_factor;
} pc;
layout(binding = 0) uniform usampler2D vram;
layout(binding = 1) uniform usampler2D ui_atlas;
layout(location = 0) out vec4 color;
#include "vram.glsl"
#include "text_dist.glsl"
void main() {
    vec2 footprint = fwidth(v_uv);
    uint flags = v_state.x, clut = v_state.z;
    ivec2 lo = ivec2(v_bounds.xy), hi = max(ivec2(v_bounds.zw) - 1, lo);
    int s = pc.ui_scale;
    ivec2 o = v_ui * s;
    ivec2 q = clamp(ivec2(floor(v_uv * float(s) + 0.0001)), lo * s, (hi + 1) * s - 1);
    uint t = texelFetch(ui_atlas, q + o, 0).r;
    if ((t & 0x80000000u) == 0u) discard;
    float d = text_distance(ui_atlas, v_uv * float(s) + vec2(o), lo * s + o, (hi + 1) * s - 1 + o);
    vec3 texel;
    float alpha = text_colour(t, d, max(max(footprint.x, footprint.y), 1.0 / 256.0), clut, texel, t);
    if (alpha < 0.002 || t == 0u) discard;
    bool semi = (flags & 4u) != 0u && (t & 0x8000u) != 0u;
    if ((pc.pass == 4 && semi) || (pc.pass == 5 && !semi)) discard;
    vec3 c = (flags & 2u) != 0u ? texel / 31.0 : min(texel * v_color * (255.0 / 128.0), 31.0) / 31.0;
    color = vec4(c, alpha * (pc.pass == 5 ? pc.text_factor : 1.0));
}
