#ifndef ROOD_HW_FRAG_GLSL
#define ROOD_HW_FRAG_GLSL
// Packet primitives into the scaled VRAM target (gpu_hw.c's draw shader).
layout(location = 0) noperspective centroid in vec3 v_color_affine;
layout(location = 1) noperspective centroid in vec2 v_uv_affine;
layout(location = 2) smooth centroid in vec3 v_color_persp;
layout(location = 3) smooth centroid in vec2 v_uv_persp;
layout(location = 4) flat in uvec4 v_state;
layout(location = 5) flat in vec4 v_normal;
layout(location = 6) flat in uvec4 v_bounds; // u min, v min, u max, v max
layout(location = 7) smooth centroid in float v_z_persp;
layout(location = 8) noperspective centroid in float v_z_affine;
layout(location = 9) flat in ivec2 v_ui; // FLAG_UI: atlas origin - v_bounds.xy, in texels
layout(push_constant) uniform Push {
    vec4 occlusion_rect; // scaled pixels: x0, y0, x1, y1
    int perspective, filtering, occlusion, pass; // pass: 0 all, 1 opaque texels only, 2 translucent texels only;
    // text from distance texels (gpu_hw.c BLEND_TEXT): 3 its solid pixels' G-buffer and depth, 4 its opaque
    // texels' colour, 5 its translucent texels' colour, by coverage
    int ui_scale; // the UI atlas's magnification (pc/src/ui_upscale.c), 0: none
    float text_factor; // pass 5: the PS1 mode's alpha factor (B/2 + F/2: 0.5, B + F/4: 0.25)
    int emissive; // emissive texels: the slot map (binding 3) is read
} pc;
layout(binding = 0) uniform usampler2D vram;
#ifdef SURFACE_MS // MSAA (hw_ms.frag): the native surfaces' depth multisampled, sample 0
layout(binding = 1) uniform sampler2DMS room_depth;
#else
layout(binding = 1) uniform sampler2D room_depth;
#endif
layout(binding = 2) uniform usampler2D ui_atlas; // 2D regions magnified with MMPX
layout(binding = 3) uniform usampler2D emissive_map; // emissive texels' slot map (emissive.glsl)
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normal_out;
layout(location = 2) out vec4 albedo_out;
layout(location = 3) out vec4 shade_out;
layout(binding = 4) uniform usampler2D hd_words;
layout(binding = 5) uniform usampler2D hd_palettes;
layout(binding = 6) uniform usampler2D hd_table;
layout(binding = 7) uniform sampler2D hd_atlas;
#include "hd_textures.glsl"
#include "vram.glsl"
#include "text_dist.glsl"
#include "emissive.glsl"
void main() {
    // A pixel's footprint in texels (text from distance texels: edges one pixel wide).
    vec2 footprint = fwidth(pc.perspective != 0 ? v_uv_persp : v_uv_affine);
    float text_alpha = -1.0;
    // Behind a native list's surface (room_depth, SZ / 65536) by more than a
    // small tolerance: hidden.
    if (pc.occlusion != 0 && v_z_affine > 0.0 && all(greaterThanEqual(gl_FragCoord.xy, pc.occlusion_rect.xy))
        && all(lessThan(gl_FragCoord.xy, pc.occlusion_rect.zw))) {
        float room = texelFetch(room_depth, ivec2(gl_FragCoord.xy), 0).r * 65536.0;
        float z = pc.perspective != 0 ? v_z_persp : v_z_affine;
        float slack = v_normal.w > 0.9 ? room * 0.002 + 1.0 : room * 0.02 + 8.0;
        if (room < 65535.0 && z > room + slack) discard;
    }
    uint flags = v_state.x, tpage = v_state.y, clut = v_state.z, win = v_state.w;
    bool semi = (flags & 4u) != 0u;
    uint mask = (flags & 8u) >> 3;
    vec3 v_color = pc.perspective != 0 ? v_color_persp : v_color_affine;
    vec2 v_uv = pc.perspective != 0 ? v_uv_persp : v_uv_affine;
    vec3 c = v_color;
    vec3 albedo = v_color;
    vec4 shade = vec4(v_color, 0.5);
    if ((flags & 1u) != 0u) {
        uint t;
        vec3 texel;
        if ((flags & 16u) != 0u && pc.ui_scale > 0) {
            // The texel's block in the atlas, clamped to the region ([min, max - 1]).
            ivec2 lo = ivec2(v_bounds.xy), hi = max(ivec2(v_bounds.zw) - 1, lo);
            ivec2 q = clamp(ivec2(floor(v_uv * float(pc.ui_scale) + 0.0001)), lo * pc.ui_scale, (hi + 1) * pc.ui_scale - 1);
            t = texelFetch(ui_atlas, q + v_ui * pc.ui_scale, 0).r;
            texel = rgb(t);
            if ((t & 0x80000000u) != 0u) { // text: anti-aliased by its distance
                ivec2 o = v_ui * pc.ui_scale;
                float d = text_distance(ui_atlas, v_uv * float(pc.ui_scale) + vec2(o), lo * pc.ui_scale + o,
                    (hi + 1) * pc.ui_scale - 1 + o);
                text_alpha = text_colour(t, d, max(max(footprint.x, footprint.y), 1.0 / 64.0), clut, texel, t);
                if (text_alpha < 0.002) discard;
            }
        } else {
            ivec2 q = ivec2(floor(v_uv + 0.0001));
            // FLAG_CLAMP (2D): the texels the PS1 samples, [min, max - 1].
            if ((flags & 32u) != 0u) q = clamp(q, ivec2(v_bounds.xy), max(ivec2(v_bounds.zw) - 1, ivec2(v_bounds.xy)));
            t = texel_at(uint(q.x) & 255u, uint(q.y) & 255u, tpage, clut, win);
            texel = rgb(t);
        }
        vec4 hd;
        bool has_hd = ((flags & 16u) == 0u || text_alpha < 0.0) && hd_texel(clamp(v_uv, vec2(v_bounds.xy), max(vec2(v_bounds.zw) - 0.0001, vec2(v_bounds.xy))), tpage, clut, win, hd);
        if (!has_hd && pc.filtering != 0 && v_normal.w > 0.5) {
            vec2 p = v_uv - 0.5;
            vec2 f = fract(p);
            ivec2 base = ivec2(floor(p));
            vec3 sum = vec3(0.0);
            float cover = 0.0;
            for (int k = 0; k < 4; k++) {
                ivec2 q = clamp(base + ivec2(k & 1, k >> 1), ivec2(v_bounds.xy), ivec2(v_bounds.zw));
                uint tk = texel_at(uint(q.x) & 255u, uint(q.y) & 255u, tpage, clut, win);
                float w = ((k & 1) != 0 ? f.x : 1.0 - f.x) * ((k >> 1) != 0 ? f.y : 1.0 - f.y);
                if (tk != 0u) { sum += rgb(tk) * w; cover += w; }
            }
            if (cover < 0.5) discard;
            texel = sum / cover;
            if (t == 0u) t = 0x7fffu;
        }
        if (has_hd) hd_apply(hd, texel, t);
        if (t == 0u) discard;
        semi = semi && (t & 0x8000u) != 0u;
        mask |= t >> 15;
        c = (flags & 2u) != 0u ? texel / 31.0 : min(texel * v_color * (255.0 / 128.0), 31.0) / 31.0;
        albedo = texel / 31.0;
        shade = (flags & 2u) != 0u ? vec4(vec3(128.0 / 255.0), 1.0) : vec4(v_color, 1.0);
        // Emissive texels: at least their own colour x tint x strength, never
        // darkened; the G-buffer's shade carries the emission (a 0.25, rgb / 2).
        if (pc.emissive != 0 && v_normal.w > 0.5 && (flags & 16u) == 0u) {
            ivec2 eq = ivec2(floor(v_uv + 0.0001));
            vec3 em = emissive_texel(uint(eq.x) & 255u, uint(eq.y) & 255u, tpage, win, t, 0u); // (no scope bits: packets)
            if (any(greaterThan(em, vec3(0.0)))) { c = max(c, min(em, vec3(1.0))); shade = vec4(min(em * 0.5, vec3(1.0)), 0.25); }
        }
    }
    if ((pc.pass == 1 && semi) || (pc.pass == 2 && !semi)) discard;
    // (texels of a recorded frame redrawn with ui_text original: whole)
    if ((pc.pass == 3 && (semi || (text_alpha >= 0.0 && text_alpha < 0.5))) || (pc.pass == 4 && semi)
        || (pc.pass == 5 && !semi)) discard;
    color = vec4(c, float(mask));
    if (pc.pass >= 4) color.a = (text_alpha < 0.0 ? 1.0 : text_alpha) * (pc.pass == 5 ? pc.text_factor : 1.0);
    // FLAG_COVER (untextured 2D shapes, gpu_hw.c BLEND_COVER): U is the coverage.
    if ((flags & 65u) == 64u) {
        float cover = clamp(v_uv.x / 256.0, 0.0, 1.0);
        if (cover < 0.002) discard;
        color = vec4(c, cover);
    }
    bool lit = v_normal.w > 0.5;
    // a: 1 room surface, 0.75 something moving (character, prop), 0 unlit
    normal_out = lit ? vec4(normalize(v_normal.xyz) * 0.5 + 0.5, v_normal.w > 0.9 ? 1.0 : 0.75) : vec4(0.0);
    albedo_out = lit ? vec4(albedo, 1.0) : vec4(0.0);
    shade_out = lit ? shade : vec4(0.0);
}
#endif
