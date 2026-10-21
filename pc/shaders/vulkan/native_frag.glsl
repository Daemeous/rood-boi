#ifndef ROOD_NATIVE_FRAG_GLSL
#define ROOD_NATIVE_FRAG_GLSL
// Native lists (render.c's fragment stage): VRAM texels as the packet path
// samples them, the same G-buffer, exact depth.
#include "native_common.glsl"
layout(location = 0) noperspective centroid in vec3 v_color_affine;
layout(location = 1) noperspective centroid in vec2 v_uv_affine;
layout(location = 2) smooth centroid in vec3 v_color_persp;
layout(location = 3) smooth centroid in vec2 v_uv_persp;
layout(location = 4) smooth centroid in float v_z;
layout(location = 5) flat in uvec4 v_state;
layout(location = 6) flat in vec3 v_normal;
layout(location = 7) flat in uvec4 v_bounds;
layout(location = 8) smooth centroid in vec3 v_view;
layout(location = 9) flat in int v_cut;
layout(location = 10) noperspective in vec3 v_bary;
layout(location = 11) flat in uint v_order;
layout(binding = 2) uniform usampler2D vram;
layout(binding = 6) uniform usampler2D emissive_map; // emissive texels' slot map (emissive.glsl)
// MSAA (native_ms.frag): the surfaces' depth read from multisampled twins, sample 0.
#ifdef SURFACE_MS
layout(binding = 3) uniform sampler2DMS surface_depth;
layout(binding = 4) uniform sampler2DMS surface_before;
#else
layout(binding = 3) uniform sampler2D surface_depth;
layout(binding = 4) uniform sampler2D surface_before; // the native surfaces' depth before this list
#endif
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normal_out;
layout(location = 2) out vec4 albedo_out;
layout(location = 3) out vec4 shade_out;
#include "vram.glsl"
#include "emissive.glsl"
// An animated material between game frames: texels past the effect's
// window wrap back by its period (the game's scroll wraps there).
ivec2 uv_wrap(ivec2 t, uint e, float k) {
    for (int a = 0; a < 2; a++) {
        if (uv_effects[e].window[a + 2] <= 0) continue;
        float r = float(t[a]) - floor(float(uv_effects[e].window[a]) * k);
        int p = int(floor(uv_effects[e].shift[a + 2] * k));
        if (r >= floor(float(uv_effects[e].window[a + 2]) * k)) t[a] -= p; else if (r < 0.0) t[a] += p;
    }
    return t;
}
// 4 x 4 ordered dither threshold (Bayer) in [0, 1).
float bayer4(ivec2 q) {
    int a = (q.x ^ q.y) & 3, b = q.y & 3;
    return float(((a & 1) << 3) | ((b & 1) << 2) | ((a >> 1) << 1) | (b >> 1)) / 16.0 + 1.0 / 32.0;
}
// Occlusion's dither (fixed to the screen's pixels, the same every frame):
// 8 x 8 Bayer, or interleaved gradient noise (render.c the same).
float bayer8(ivec2 q) {
    int a = (q.x ^ q.y) & 7, b = q.y & 7;
    return float(((a & 1) << 5) | ((b & 1) << 4) | ((a & 2) << 2) | ((b & 2) << 1) | ((a & 4) >> 1) | ((b & 4) >> 2)) / 64.0 + 1.0 / 128.0;
}
float occl_threshold(ivec2 q) {
    return u.occl_params.w > 0.5 ? fract(52.9829189 * fract(dot(vec2(q) + 0.5, vec2(0.06711056, 0.00583715)))) : bayer8(q);
}
void main() {
    // The editor's cutaway: inside the cone from the eye to the focus (its
    // radius at the focus cut_focus.w: a circle of constant size on screen),
    // a ghost out to 3/4 of the radius (cut_scale.w of its pixels kept, and
    // its polygon edges), blending to solid at the radius (render.c).
    vec3 bary_step = fwidth(v_bary); // outside the branch: derivatives stay defined
    bool ghost = false;
    if ((v_cut & 1) != 0) {
        vec3 f = u.cut_focus.xyz;
        float t = clamp(dot(v_view, f) / max(dot(f, f), 1e-6), 0.0, 1.0);
        float keep = clamp((length(v_view - f * t) / max(u.cut_focus.w * t, 1e-3) - 0.75) * 4.0, 0.0, 1.0);
        float cover = mix(u.cut_scale.w, 1.0, keep);
        if (u.cut_scale.w > 0.0 && cover < 1.0) {
            vec3 d = v_bary / max(bary_step, vec3(1e-6)); // pixels to each edge
            int e = v_cut >> 1;
            float near_edge = min(min((e & 1) != 0 ? d.x : 1e9, (e & 2) != 0 ? d.y : 1e9), (e & 4) != 0 ? d.z : 1e9);
            if (near_edge < 1.0) cover = 1.0;
        }
        if (cover < bayer4(ivec2(gl_FragCoord.xy))) discard;
        ghost = keep < 0.5;
    }
    // A dithered share of the whole instance left out (gameplay smart walls
    // fading, or ghosts with their edges kept; render.c instance_cut).
    if (u.instance_cut.x > 0.0) {
        float cover = 1.0 - u.instance_cut.x;
        if (u.instance_cut.y > 0.0) {
            vec3 d = v_bary / max(bary_step, vec3(1e-6));
            int e = v_cut >> 1;
            float near_edge = min(min((e & 1) != 0 ? d.x : 1e9, (e & 2) != 0 ? d.y : 1e9), (e & 4) != 0 ? d.z : 1e9);
            if (near_edge < 1.0) cover = 1.0;
            ghost = true;
        }
        if (cover < bayer4(ivec2(gl_FragCoord.xy))) discard;
    }
    // Occlusion in play: a polygon between the eye and a subject, nearer than
    // it, inside its ellipse on the screen, keeps occl_params.x of its pixels
    // at the centre, more toward the rim, all beyond it.
    // Semi-transparent texels (a save point's light, glass) hide nothing:
    // never faded (decided below, once the texel is known).
    float occl_cover = 1.0;
    if (u.occl_params.z > 0.0 && u.occl_instance.x == 0.0 && (v_cut >> 4) != 0 && v_view.z > 1.0) {
        vec2 sp = v_view.xy / v_view.z;
        float fade = 0.0;
        for (int i = 0; i < 4; i++) {
            vec4 s = u.occl_subject[i];
            if (((v_cut >> (4 + i)) & 1) == 0 || s.w <= 0.0 || v_view.z > s.z - u.occl_shape[i].z) continue;
            vec2 d = (sp - s.xy / s.z) * s.z / max(u.occl_shape[i].xy, vec2(1.0));
            fade = max(fade, (1.0 - smoothstep(1.0 - u.occl_params.y, 1.0, length(d))) * s.w);
        }
        occl_cover = 1.0 - fade * (1.0 - u.occl_params.x);
    }
    float depth = clamp(v_z / 65536.0, 0.0, 1.0);
    // Objects and other room blocks are painted after the room surfaces they
    // lie on (nearer buckets): tested as if room_slack nearer.
    float tested = u.layer_slack != 0 ? max(depth * (1.0 - u.room_slack.x) - u.room_slack.y / 65536.0, 0.0) : depth;
    // PS1 order within a model: hidden only by what was drawn before the
    // model (the room, other models); its own polygons paint over each other.
    // Occlusion's silhouettes: a subject's own pixels hidden by a surface drawn
    // before it (by more than 8 units) are drawn, in a dither of
    // occl_instance.y (more toward its rim), in occl_tint, unlit, at the
    // surface's depth.
    bool silhouette = false;
    float silhouette_depth = 0.0;
    if (u.painter != 0) {
        float before = texelFetch(surface_before, ivec2(gl_FragCoord.xy), 0).r;
        if (tested > before) {
            if (u.occl_instance.y <= 0.0 || u.depth_pass != 0 || v_z < before * 65536.0 + 8.0) discard;
            float rim = smoothstep(0.25, 0.9, 1.0 - abs(dot(normalize(v_normal), normalize(-v_view))));
            float cover = u.occl_instance.y * mix(1.0, rim, u.occl_instance.z);
            if (cover < occl_threshold(ivec2(gl_FragCoord.xy) + ivec2(3, 5))) discard;
            silhouette = true;
            silhouette_depth = before;
        }
    }
    if (u.depth_pass != 0 && u.painter == 0 && tested > texelFetch(surface_depth, ivec2(gl_FragCoord.xy), 0).r + 1.0 / 4194304.0)
        discard;
    if (u.surface_test != 0) {
        float surface_z = texelFetch(surface_depth, ivec2(gl_FragCoord.xy), 0).r;
        // Room translucent polygons lie on or against room surfaces: the
        // packet path's room slack.
        if (u.surface_test == 1 ? tested > surface_z + 1.0 / 4194304.0
            : u.surface_test == 2 ? surface_z < 1.0 && v_z > surface_z * 65536.0 * 1.002 + 1.0
            // drop shadows and effects: the packet path's slack for decals
            : u.surface_test == 3 ? surface_z < 1.0 && v_z > surface_z * 65536.0 * 1.02 + 8.0
            // a room list's ordered pass: only fragments on (or within the
            // slack of) the nearest room surface take part
                                  : v_z > surface_z * 65536.0 * (1.0 + u.room_slack.x) + u.room_slack.y)
            discard;
    }
    uint code = v_state.x, tpage = v_state.y, clut = v_state.z;
    vec3 v_color = u.perspective != 0 ? v_color_persp : v_color_affine;
    vec2 v_uv = u.perspective != 0 ? v_uv_persp : v_uv_affine;
    vec3 c = v_color, albedo = v_color;
    vec4 shade = vec4(v_color, 0.5);
    uint mask = uint(u.mask_set);
    bool semi = (code & 2u) != 0u;
    if ((code & 4u) != 0u) {
        // Stay inside the polygon's UV range (pages are atlases).
        uvec2 at = uvec2(clamp(floor(v_uv + 0.0001), vec2(v_bounds.xy), vec2(v_bounds.zw)));
        uint e = v_state.w & 255u;
        float k = (v_state.w & 256u) != 0u ? 0.5 : 1.0;
        vec2 sh = e != 0u ? uv_effects[e].shift.xy * k : vec2(0.0);
        bool moving = sh != vec2(0.0);
        if (moving) at = uvec2(uv_wrap(ivec2(floor(clamp(v_uv + 0.0001, vec2(v_bounds.xy), vec2(v_bounds.zw) + 0.999) + sh)), e, k));
        uint t = texel_at(at.x & 255u, at.y & 255u, tpage, clut, u.window);
        vec3 texel = rgb(t);
        if (u.filtering != 0) {
            vec2 q = v_uv - 0.5 + sh;
            vec2 f = fract(q);
            ivec2 base = ivec2(floor(q));
            vec3 sum = vec3(0.0);
            float cover = 0.0;
            for (int i = 0; i < 4; i++) {
                ivec2 s = clamp(base + ivec2(i & 1, i >> 1), ivec2(floor(vec2(v_bounds.xy) + sh)), ivec2(floor(vec2(v_bounds.zw) + sh)));
                if (moving) s = uv_wrap(s, e, k);
                uint tk = texel_at(uint(s.x) & 255u, uint(s.y) & 255u, tpage, clut, u.window);
                float w = ((i & 1) != 0 ? f.x : 1.0 - f.x) * ((i >> 1) != 0 ? f.y : 1.0 - f.y);
                if (tk != 0u) { sum += rgb(tk) * w; cover += w; }
            }
            if (cover < 0.5) discard;
            texel = sum / cover;
            if (t == 0u) t = 0x7fffu;
        }
        if (t == 0u) discard;
        // Only texels with the STP bit are semi-transparent.
        semi = semi && (t & 0x8000u) != 0u;
        mask |= t >> 15;
        c = (code & 1u) != 0u ? texel / 31.0 : min(texel * v_color * (255.0 / 128.0), 31.0) / 31.0;
        albedo = texel / 31.0;
        shade = (code & 1u) != 0u ? vec4(vec3(128.0 / 255.0), 1.0) : vec4(v_color, 1.0);
        // Emissive texels (emissive.glsl): never darkened; the shade carries the emission.
        if (u.emissive != 0) {
            vec3 em = emissive_texel(at.x & 255u, at.y & 255u, tpage, u.window, t, v_state.w >> 16);
            if (any(greaterThan(em, vec3(0.0)))) { c = max(c, min(em, vec3(1.0))); shade = vec4(min(em * 0.5, vec3(1.0)), 0.25); }
        }
    }
    if ((u.semi_pass == 1 && semi) || (u.semi_pass == 2 && !semi)) discard;
    if (occl_cover < 1.0 && !semi && occl_cover < occl_threshold(ivec2(gl_FragCoord.xy))) discard;
    if (silhouette) {
        color = vec4(u.occl_tint.rgb, float(mask));
        normal_out = vec4(0.5, 0.5, 0.0, 0.0); // unlit
        albedo_out = vec4(u.occl_tint.rgb, 1.0);
        shade_out = vec4(u.occl_tint.rgb, 0.5);
        gl_FragDepth = silhouette_depth;
        return;
    }
    color = vec4(c, float(mask));
    normal_out = vec4(normalize(v_normal) * 0.5 + 0.5, ghost ? 0.55 : u.surface);
    albedo_out = vec4(albedo, 1.0);
    shade_out = shade;
    gl_FragDepth = u.room_order != 0 ? float(v_order) / 16777215.0 : u.layer_slack == 2 ? tested : depth;
}
#endif
