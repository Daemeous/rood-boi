#version 450
#extension GL_GOOGLE_include_directive : require
// Present from the hardware rasterizer's scaled VRAM target with lighting,
// GI, haze and the post effects' inputs (gl_present.c's scaled shader).
// Depth is GTE SZ / 65536 (0 = 2D/UI, 1 = far); effects leave UI alone.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(std140, binding = 0) uniform Present {
    ivec4 rect; // display x, y, width, height in VRAM pixels
    ivec4 i0; // scale, filtered (0 nearest, 1 bilinear, 2 supersample), show_depth, post
    ivec4 i1; // view_mode, light_count, gi_on (bits: 1 add, 2 show alone, 4 traced visibility, 8 show it, 16 baked visibility, 32 show it, 64 probe field, 128 a GI result is bound, 256 its rgb is the probe field), unused
    ivec4 i2; // gi_size.xy
    vec4 view_; // viewport size in window pixels
    vec4 f0; // focus_sz, dof_radius, aperture, baked
    vec4 f1; // gi_strength, fog_density, shadow_softness, normal_smoothing
    vec4 tilt_; // band centre (0..1 from top), half width, radius
    vec4 fog_; // rgb colour, a = density
    vec4 fog_range_; // SZ where fog starts and saturates
    vec4 room_lo_, room_hi_;
    vec4 projection_; // H (widescreen-narrowed for x), H
    vec4 screen_offset_; // OFX, OFY
    vec4 inverse_rotation_[3]; // view -> world: R^-1 (columns)
    vec4 normal_to_world_[3]; // R^T (columns)
    vec4 camera_translation_;
    vec4 light_pos[16]; // view-space xyz, radius
    vec4 light_col[16]; // rgb * intensity, type (0 point, 1 spot, 2 rect)
    vec4 light_right[16];
    vec4 light_up[16];
    vec4 light_dir[16]; // view-space direction, cos(half cone)
    vec4 light_world[16];
    ivec4 light_shadow_[4]; // -2 ray-traced visibility (gi alpha), else unshadowed
    ivec4 haze_grid; // GX, GY, GZ, on (the froxel volume, vk_rt.inc)
    vec4 haze_range; // znear, log(zfar / znear)
    // Lighting quality (lighting_quality.cpp): unshadowed haze density (0
    // off), its anisotropy, screen-space AO radius and strength; shadow map
    // filter (0 hard, 1 PCF, 2 PCSS), screen-space AO on; light size (PCSS).
    vec4 quality0;
    ivec4 quality1;
    vec4 quality2;
} u;
layout(binding = 1) uniform sampler2D target;
layout(binding = 2) uniform sampler2D depth_tex;
layout(binding = 3) uniform sampler2D normal_tex;
layout(binding = 4) uniform sampler2D albedo_tex;
layout(binding = 5) uniform sampler2D shade_tex;
layout(binding = 6) uniform sampler2D gi_tex;
layout(binding = 7) uniform sampler2D gi_position;
layout(binding = 8) uniform sampler2D gi_normal;
layout(binding = 9) uniform sampler2D gi_extra;
layout(binding = 11) uniform sampler2D shadow_atlas; // vk_shadows.c: SLOTS rows of six 512 x 512 cube faces
layout(binding = 10) uniform sampler2D haze_volume; // GZ tiles of GX x GY: rgb in-scattered light, a transmittance
// Probe GI (gi_method probes; vk_probes.inc): the volume (binding 12) and its atlases.
#include "probe_volume.glsl"
#define PROBE_SAMPLING
layout(binding = 13) uniform sampler2D probe_irradiance;
layout(binding = 14) uniform sampler2D probe_depth;
layout(binding = 15) uniform sampler2D probe_data;
#include "probe_common.glsl"
#define rect u.rect
#define scale u.i0.x
#define filtered u.i0.y
#define show_depth u.i0.z
#define post u.i0.w
#define view_mode u.i1.x
#define light_count u.i1.y
#define gi_on u.i1.z
#define gi_size u.i2.xy
#define view u.view_.xy
#define focus_sz u.f0.x
#define dof_radius u.f0.y
#define aperture u.f0.z
#define baked u.f0.w
#define gi_strength u.f1.x
#define fog_density u.f1.y
#define normal_smoothing u.f1.w
#define tilt u.tilt_.xyz
#define fog u.fog_
#define fog_range u.fog_range_.xy
#define room_lo u.room_lo_.xyz
#define room_hi u.room_hi_.xyz
#define projection u.projection_.xyz
#define screen_offset u.screen_offset_.xy
#define camera_translation u.camera_translation_.xyz
#define light_pos u.light_pos
#define light_col u.light_col
#define light_right u.light_right
#define light_up u.light_up
#define light_dir u.light_dir
mat3 inverse_rotation() { return mat3(u.inverse_rotation_[0].xyz, u.inverse_rotation_[1].xyz, u.inverse_rotation_[2].xyz); }
mat3 normal_to_world() { return mat3(u.normal_to_world_[0].xyz, u.normal_to_world_[1].xyz, u.normal_to_world_[2].xyz); }
int light_shadow(int i) { return u.light_shadow_[i >> 2][i & 3]; }
// Depth- and crease-aware average of nearby normals (screen space). The
// target's rows are VRAM rows in both APIs, so texel offsets match GL's.
vec3 smooth_normal(ivec2 q, vec3 n, float z) {
    if (normal_smoothing <= 0.0) return n;
    vec3 sum = n;
    float r = normal_smoothing * float(scale);
    for (int k = 0; k < 12; k++) {
        float a = float(k) * 0.5236, f = (k & 1) != 0 ? 1.0 : 0.55;
        ivec2 o = q + ivec2(vec2(cos(a), sin(a)) * r * f);
        vec4 m = texelFetch(normal_tex, o, 0);
        if (m.a < 0.5) continue;
        vec3 mn = normalize(m.xyz * 2.0 - 1.0);
        float mz = texelFetch(depth_tex, o, 0).r * 65536.0;
        float w = step(0.64, dot(n, mn)) * exp(-abs(mz - z) / (0.03 * z + 1.0));
        sum += mn * w;
    }
    return normalize(sum);
}
vec2 full() { return vec2(1024.0, 512.0) * float(scale); }
ivec2 texel(vec2 uv_) {
    vec2 size = vec2(rect.zw * scale);
    ivec2 p = clamp(ivec2(floor(uv_ * size)), ivec2(0), ivec2(size) - 1);
    return ivec2((rect.x * scale + p.x) % (1024 * scale), (rect.y * scale + p.y) % (512 * scale));
}
float depth_at(vec2 uv_) { return texelFetch(depth_tex, texel(uv_), 0).r; }
vec3 base(vec2 uv_) {
    vec2 size = vec2(rect.zw * scale);
    if (filtered == 2) { // average every target texel under this window pixel
        vec2 per = size / view;
        ivec2 n = clamp(ivec2(ceil(per - 0.001)), ivec2(1), ivec2(8));
        vec2 origin = uv_ * size - per * 0.5;
        vec3 sum = vec3(0.0);
        for (int j = 0; j < n.y; j++)
            for (int i = 0; i < n.x; i++) {
                ivec2 p = clamp(ivec2(origin + (vec2(i, j) + 0.5) * per / vec2(n)), ivec2(0), ivec2(size) - 1);
                sum += texelFetch(target, ivec2((rect.x * scale + p.x) % (1024 * scale), (rect.y * scale + p.y) % (512 * scale)), 0).rgb;
            }
        return sum / float(n.x * n.y);
    }
    if (filtered == 1) { // bilinear inside the display area
        vec2 p = clamp(uv_ * size, vec2(0.5), size - 0.5) + vec2(rect.xy * scale);
        return texture(target, p / full()).rgb;
    }
    return texelFetch(target, texel(uv_), 0).rgb;
}
// A cube-map lookup in shadow slot `slot` (GL face selection and
// orientation, as the faces were rendered).
float stored(int slot, vec3 d) {
    vec3 a = abs(d); float face, sc, tc, ma;
    if (a.x >= a.y && a.x >= a.z) { ma = a.x; face = d.x > 0.0 ? 0.0 : 1.0; sc = d.x > 0.0 ? -d.z : d.z; tc = -d.y; }
    else if (a.y >= a.z) { ma = a.y; face = d.y > 0.0 ? 2.0 : 3.0; sc = d.x; tc = d.y > 0.0 ? d.z : -d.z; }
    else { ma = a.z; face = d.z > 0.0 ? 4.0 : 5.0; sc = d.z > 0.0 ? d.x : -d.x; tc = -d.y; }
    vec2 st = 0.5 * (vec2(sc, tc) / ma + 1.0);
    vec2 px = clamp(st * 512.0, vec2(0.5), vec2(511.5)) + vec2(face, float(slot)) * 512.0;
    return texture(shadow_atlas, px / vec2(textureSize(shadow_atlas, 0))).r;
}
// Interleaved gradient noise: a per-pixel rotation for the sample patterns.
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
// Shadow-map visibility (gl_present.c the same). Filter 0: one tap; 1 (PCF):
// 8 taps on a small cone around the light direction; 2 (PCSS): a blocker
// search over the light's size, then a cone as wide as the penumbra the
// blockers' distance gives (sharp at contact, softer further away). `slope`:
// tan of the angle between the surface normal and the light (biases).
float map_visibility(int slot, vec3 world, vec4 light, float size, float slope) {
    vec3 d = world - light.xyz;
    float dw = max(length(d), 1e-3), dist = dw / light.w;
    vec3 axis = d / dw;
    vec3 t = normalize(cross(abs(axis.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), axis));
    vec3 b = cross(axis, t);
    int filter_ = u.quality1.x;
    if (filter_ == 0)
        return dist - 0.004 <= stored(slot, axis) ? 1.0 : 0.0;
    float lit = 0.0;
    if (filter_ == 1) {
        for (int k = 0; k < 8; k++) {
            float a = float(k) * 0.785398 + 0.39;
            vec3 dir = axis + (t * cos(a) + b * sin(a)) * u.f1.z * (0.5 + 0.5 * float(k & 1));
            lit += dist - 0.004 <= stored(slot, dir) ? 1.0 : 0.0;
        }
        return lit / 8.0;
    }
    float rot = ign(gl_FragCoord.xy) * 6.2831853;
    float search = clamp(2.0 * size / dw, 0.01, 0.15);
    float blockers = 0.0, sum = 0.0, search_bias = 0.004 + dist * search * slope * 0.5;
    for (int k = 0; k < 8; k++) {
        float r = sqrt((float(k) + 0.5) / 8.0), a = float(k) * 2.39996323 + rot;
        float s = stored(slot, axis + (t * cos(a) + b * sin(a)) * r * search);
        if (s < dist - search_bias) { blockers += 1.0; sum += s; }
    }
    if (blockers == 0.0)
        return 1.0;
    float blocker = sum / blockers;
    float width = clamp(2.0 * size * (dist - blocker) / (max(blocker, 1e-3) * dist * light.w), 0.25 * u.f1.z, 0.15);
    float bias = 0.004 + dist * width * slope * 0.5;
    for (int k = 0; k < 16; k++) {
        float r = sqrt((float(k) + 0.5) / 16.0), a = float(k) * 2.39996323 + rot;
        lit += dist - bias <= stored(slot, axis + (t * cos(a) + b * sin(a)) * r * width) ? 1.0 : 0.0;
    }
    return lit / 16.0;
}
// Screen-space ambient occlusion from the depth buffer: points in the
// hemisphere around the surface, projected through the GTE camera; one that
// lies behind the depth there (within the radius) is occluded.
float screen_ao(vec3 world, vec3 wn) {
    float radius = u.quality0.z, strength = u.quality0.w;
    mat3 to_view = transpose(normal_to_world()); // R: world -> view (with camera_translation)
    vec3 t = normalize(cross(abs(wn.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), wn)), b = cross(wn, t);
    float rot = ign(gl_FragCoord.xy) * 6.2831853, occlusion = 0.0;
    const int N = 12;
    for (int k = 0; k < N; k++) {
        float f = (float(k) + 0.5) / float(N), a = float(k) * 2.39996323 + rot;
        float r = sqrt(f);
        vec3 dir = (t * cos(a) + b * sin(a)) * r * 0.9 + wn * (sqrt(1.0 - f * 0.81) + 0.1);
        vec3 v = to_view * (world + dir * radius * mix(0.2, 1.0, f)) + camera_translation;
        if (v.z < 1.0) continue;
        vec2 s = v.xy * projection.xy / v.z + screen_offset;
        if (any(lessThan(s, vec2(0.0))) || any(greaterThanEqual(s, vec2(rect.zw)))) continue;
        ivec2 q = ivec2((s + vec2(rect.xy)) * float(scale));
        q = ivec2(q.x % (1024 * scale), q.y % (512 * scale));
        float sz = texelFetch(depth_tex, q, 0).r * 65536.0;
        if (sz < 0.5) continue; // UI
        float behind = v.z - sz;
        if (behind > 1.0) occlusion += 1.0 - smoothstep(radius, radius * 2.0, behind);
    }
    return 1.0 - strength * occlusion / float(N);
}
// Unshadowed haze between the eye and `world` inside the room's bounds
// (haze Low; High traces it): every light's in-scattering at 6 jittered
// steps, the traced haze's phase function and units (ptgi.c haze()).
vec3 low_haze(vec3 c, vec3 world) {
    float density = u.quality0.x, g = u.quality0.y;
    vec3 eye = inverse_rotation() * -camera_translation;
    vec3 v = world - eye; float length_ = length(v); vec3 dir = v / max(length_, 1e-3);
    vec3 inv = 1.0 / dir, t0 = (room_lo - eye) * inv, t1 = (room_hi - eye) * inv;
    float enter = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));
    float leave = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), length_));
    if (!(leave > enter)) return c;
    float step_ = (leave - enter) / 6.0, jitter = ign(gl_FragCoord.xy + 17.0);
    vec3 sum = vec3(0.0);
    for (int k = 0; k < 6; k++) {
        float tt = (float(k) + jitter) * step_;
        vec3 x = eye + dir * (enter + tt);
        vec3 here = vec3(0.0);
        for (int i = 0; i < light_count; i++) {
            vec3 to = light_pos[i].xyz - x; float dl = length(to); vec3 l = to / max(dl, 1e-3);
            float f = clamp(dl / light_pos[i].w, 0.0, 1.0);
            float a = (1.0 - f * f) * (1.0 - f * f);
            if (light_col[i].w > 1.5) a *= max(dot(-l, light_dir[i].xyz), 0.0);
            else if (light_col[i].w > 0.5) a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));
            float mu = dot(dir, l);
            here += light_col[i].rgb * a * (1.0 - g * g) / pow(1.0 + g * g + 2.0 * g * mu, 1.5);
        }
        sum += here * exp(-density * tt);
    }
    return c * exp(-density * (leave - enter)) + sum * density * step_;
}
vec3 world_at(ivec2 q, float z) {
    vec2 s = (vec2(q) + 0.5) / float(scale) - vec2(rect.xy) - screen_offset;
    return inverse_rotation() * (vec3(s.x * z / projection.x, s.y * z / projection.y, z) - camera_translation);
}
// The GI result at this pixel: its 2 x 2 GI pixels weighted bilinearly and by
// how well their surface matches this one (joint bilateral); when none
// matches (silhouettes, thin geometry), the best-matching of the 3 x 3 around.
float gi_match(ivec2 h, vec3 world, vec3 wn) {
    vec4 gp = texelFetch(gi_position, h, 0);
    if (gp.w < 0.5) return 0.0;
    vec3 gn = texelFetch(gi_normal, h, 0).xyz;
    return exp(-abs(dot(gp.xyz - world, wn)) / 4.0) * pow(max(dot(gn, wn), 0.0), 8.0);
}
vec4 gi_at(vec2 display, vec3 world, vec3 wn, out vec4 extra_out) {
    vec2 sp = display / vec2(rect.zw) * vec2(gi_size) - 0.5;
    ivec2 b = ivec2(floor(sp)); vec2 f = sp - vec2(b);
    vec4 sum = vec4(0.0), extra = vec4(0.0); float weight = 0.0;
    for (int y = 0; y <= 1; y++)
        for (int x = 0; x <= 1; x++) {
            ivec2 h = clamp(b + ivec2(x, y), ivec2(0), gi_size - 1);
            float w = ((x == 1 ? f.x : 1.0 - f.x) * (y == 1 ? f.y : 1.0 - f.y) + 1e-3) * gi_match(h, world, wn);
            sum += texelFetch(gi_tex, h, 0) * w; extra += texelFetch(gi_extra, h, 0) * w; weight += w;
        }
    if (weight < 1e-4) {
        ivec2 c = clamp(ivec2(floor(sp + 0.5)), ivec2(0), gi_size - 1), best = c;
        float best_w = -1.0;
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) {
                ivec2 h = clamp(c + ivec2(x, y), ivec2(0), gi_size - 1);
                float w = gi_match(h, world, wn) - 1e-3 * float(abs(x) + abs(y));
                if (w > best_w) { best_w = w; best = h; }
            }
        if (texelFetch(gi_position, best, 0).w > 0.5) {
            sum = texelFetch(gi_tex, best, 0); extra = texelFetch(gi_extra, best, 0); weight = 1.0;
        }
    }
    // extra: r baked-light visibility, gba light from the glows.
    extra_out = weight > 1e-5 ? extra / weight : vec4(1.0, 0.0, 0.0, 0.0);
    return weight > 1e-5 ? sum / weight : vec4(0.0, 0.0, 0.0, 1.0);
}
// Haze between the eye and view depth z at display position `display`
// (froxel volume: bilinear in the tile, linear between slices).
vec4 haze_at(vec2 display, float z) {
    vec2 tile = vec2(u.haze_grid.xy);
    vec2 xy = clamp(display / vec2(rect.zw) * tile, vec2(0.5), tile - 0.5);
    float k = clamp(log(max(z, 1.0) / u.haze_range.x) / u.haze_range.y * float(u.haze_grid.z) - 1.0, 0.0, float(u.haze_grid.z - 1));
    float k0 = floor(k), f = k - k0, k1 = min(k0 + 1.0, float(u.haze_grid.z - 1));
    vec2 size = vec2(textureSize(haze_volume, 0));
    vec4 a = texture(haze_volume, (xy + vec2(k0 * tile.x, 0.0)) / size);
    vec4 b = texture(haze_volume, (xy + vec2(k1 * tile.x, 0.0)) / size);
    return mix(a, b, f);
}
vec3 hazed(vec3 c, ivec2 q, float d) {
    vec4 h = haze_at((vec2(q) + 0.5) / float(scale) - vec2(rect.xy), d * 65536.0);
    return any(isnan(h)) ? c : c * clamp(h.a, 0.0, 1.0) + max(h.rgb, vec3(0.0));
}
void main() {
    float d = depth_at(uv);
    bool haze_done = false;
    if (show_depth != 0) { // far black, UI magenta, 3D log-grey near-bright
        float sz = d * 65536.0;
        color = d >= 1.0 ? vec4(0.0, 0.0, 0.0, 1.0)
              : sz < 0.5 ? vec4(0.6, 0.0, 0.6, 1.0)
              : vec4(vec3(1.0 - clamp((log2(sz) - 6.0) / 8.0, 0.0, 1.0)), 1.0);
        return;
    }
    vec3 c = base(uv);
    if ((light_count > 0 || baked != 1.0 || gi_on != 0 || view_mode != 0 || u.quality1.y != 0) && view_mode != 1) {
        ivec2 q = texel(uv);
        vec4 n = texelFetch(normal_tex, q, 0);
        if (n.a > 0.5 && d > 0.0) { // lit 3D surface (not under UI): its view-space position
            float z = texelFetch(depth_tex, q, 0).r * 65536.0;
            vec2 s = (vec2(q) + 0.5) / float(scale) - vec2(rect.xy) - screen_offset;
            vec3 p = vec3(s.x * z / projection.x, s.y * z / projection.y, z);
            vec3 normal = smooth_normal(q, normalize(n.xyz * 2.0 - 1.0), z);
            vec3 albedo = texelFetch(albedo_tex, q, 0).rgb;
            vec3 sum = vec3(0.0);
            vec3 world = inverse_rotation() * (p - camera_translation);
            vec3 world_normal = normalize(normal_to_world() * normal);
            vec4 extra = vec4(1.0, 0.0, 0.0, 0.0);
            vec4 gi = (gi_on & 128) != 0 ? gi_at((vec2(q) + 0.5) / float(scale) - vec2(rect.xy), world, world_normal, extra) : vec4(0.0, 0.0, 0.0, 1.0);
            // gi_on 64: indirect light from the probe field (in place of the path-traced gi.rgb).
            vec3 eye_world = inverse_rotation() * -camera_translation;
            if ((gi_on & 64) != 0) gi.rgb += probe_irradiance_at(world, world_normal, normalize(world - eye_world));
            float baked_vis = (gi_on & 16) != 0 ? extra.r : 1.0;
            float ao = u.quality1.y != 0 ? screen_ao(world, world_normal) : 1.0;
            for (int i = 0; i < light_count; i++) {
                vec3 centre = light_pos[i].xyz;
                if (light_col[i].w > 1.5) { // rect: nearest point on the emitter
                    vec3 rr = light_right[i].xyz, ru = light_up[i].xyz, rel = world - centre;
                    centre += rr * clamp(dot(rel, rr) / dot(rr, rr), -1.0, 1.0)
                            + ru * clamp(dot(rel, ru) / dot(ru, ru), -1.0, 1.0);
                }
                vec3 to = centre - world;
                float dl = length(to);
                vec3 l = to / max(dl, 1e-3);
                float x = clamp(dl / light_pos[i].w, 0.0, 1.0);
                float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(world_normal, l), 0.0);
                if (light_col[i].w > 1.5) // rect: emits from its front face
                    a *= max(dot(-l, light_dir[i].xyz), 0.0);
                else if (light_col[i].w > 0.5) // spot: smooth cone edge
                    a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));
                if (light_shadow(i) == -2) // ray-traced
                    a *= clamp(gi.a, 0.0, 1.0);
                else if (light_shadow(i) >= 0 && a > 0.0) { // shadow map (no ray tracing)
                    float cosine = clamp(dot(world_normal, l), 0.05, 1.0);
                    a *= map_visibility(light_shadow(i), world + world_normal * max(2.0, u.light_world[i].w * 0.01), u.light_world[i],
                        light_right[i].w > 0.0 ? light_right[i].w : u.quality2.x, min(sqrt(1.0 - cosine * cosine) / cosine, 4.0));
                }
                sum += light_col[i].rgb * a;
            }
            if ((gi_on & 2) != 0) { color = vec4(min((gi.rgb + extra.gba) * gi_strength, vec3(1.0)), 1.0); return; }
            if ((gi_on & 8) != 0) { color = vec4(vec3(gi.a), 1.0); return; }
            if ((gi_on & 32) != 0) { color = vec4(vec3(baked_vis * ao), 1.0); return; }
            if ((gi_on & 1) != 0) sum += (gi.rgb * ao + extra.gba) * gi_strength; // indirect + glows
            else if ((gi_on & (64 | 256)) != 0) sum += gi.rgb * ao * gi_strength; // probes alone (no traced GI result)
            vec4 shade = texelFetch(shade_tex, q, 0);
            vec3 vertex_light = shade.a > 0.75 ? shade.rgb * (255.0 / 128.0) : shade.rgb;
            if (view_mode == 3) { color = vec4(shade.a > 0.75 ? albedo : shade.rgb, 1.0); return; }
            if (view_mode == 4) { color = vec4(min(shade.a > 0.75 ? vertex_light * 0.5 : shade.rgb, vec3(1.0)), 1.0); return; }
            if (view_mode == 5) { color = vec4(world_normal * 0.5 + 0.5, 1.0); return; }
            if (view_mode == 2) { c = vertex_light * 0.5; albedo = vec3(0.5); }
            c = c * baked * baked_vis * ao + albedo * sum;
            if (u.haze_grid.w != 0) { c = hazed(c, q, d); haze_done = true; }
            else if (u.quality0.x > 0.0 && light_count > 0) { c = low_haze(c, world); haze_done = true; }
            // Soft shoulder above 0.8 instead of clipping at white (lit pixels only).
            c = mix(c, 1.0 - 0.2 * exp(-(c - 0.8) / 0.2), step(0.8, c));
            if (any(isnan(c)) || any(isinf(c))) c = base(uv);
        }
    }
    bool ui = d * 65536.0 < 0.5;
    // Unlit 3D pixels (effects, particles, glows) get the same haze as their
    // neighbours: no dark cut-outs around them.
    if (u.haze_grid.w != 0 && !haze_done && !ui && d < 1.0 && view_mode != 1) c = hazed(c, texel(uv), d);
    else if (u.quality0.x > 0.0 && !haze_done && !ui && d < 1.0 && view_mode != 1 && light_count > 0)
        c = low_haze(c, world_at(texel(uv), d * 65536.0));
    if (post == 0 || ui) { color = vec4(min(c, vec3(1.0)), ui ? 9999.0 : 0.0); return; }
    float sz = d >= 1.0 ? 65535.0 : d * 65536.0;
    if ((post & 4) != 0) {
        float f = clamp((sz - fog_range.x) / max(fog_range.y - fog_range.x, 1.0), 0.0, 1.0);
        c = mix(c, fog.rgb, f * fog.a);
    }
    // Blur radius for the blur pass (alpha), signed: negative in front of the
    // focal plane.
    float coc = 0.0;
    if ((post & 1) != 0) {
        float focus = focus_sz;
        if (focus <= 0.0) { // auto: mean log depth of 3D samples near the centre
            float sum = 0.0, n = 0.0;
            for (int i = 0; i < 9; i++) {
                float fd = depth_at(vec2(0.5, 0.55) + vec2(i % 3 - 1, i / 3 - 1) * 0.04);
                if (fd * 65536.0 >= 0.5 && fd < 1.0) { sum += log2(fd * 65536.0); n += 1.0; }
            }
            focus = n > 0.0 ? exp2(sum / n) : 1024.0;
        }
        coc = dof_radius * clamp(aperture * (1.0 - focus / sz), -1.0, 1.0);
    }
    if ((post & 2) != 0) {
        float t = tilt.z * smoothstep(tilt.y, tilt.y + 0.25, abs(uv.y - tilt.x));
        coc = abs(coc) >= t ? coc : t;
    }
    color = vec4(min(c, vec3(1.0)), coc);
}
