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
    vec4 tilt_; // band centre (0..1 from top), half width, radius; w: near blur share
    vec4 fog_; // rgb colour, a = density
    vec4 fog_range_; // SZ where fog starts and saturates
    vec4 room_lo_, room_hi_;
    vec4 projection_; // H (widescreen-narrowed for x), H
    vec4 screen_offset_; // OFX, OFY
    vec4 inverse_rotation_[3]; // view -> world: R^-1 (columns)
    vec4 normal_to_world_[3]; // R^T (columns)
    vec4 camera_translation_;
    ivec4 haze_grid; // GX, GY, GZ, on (the froxel volume, vk_rt.inc)
    vec4 haze_range; // znear, log(zfar / znear)
    // Lighting quality (lighting_quality.cpp): unshadowed haze density (0
    // off), its anisotropy, screen-space AO radius and strength; shadow map
    // filter (0 hard, 1 PCF, 2 PCSS), screen-space AO on; light size (PCSS).
    vec4 quality0;
    ivec4 quality1;
    vec4 quality2;
    // The room's sun and sky (environment.h pc_env_gpu), and how they light
    // here: x the sun's shadows (-2 traced: in gi.a, 0 its shadow map, -1
    // none), y 1 the sky's ambient term (nothing traced carries the sky this
    // frame), z 1 any on; the sun's shadow map (vk_shadows.c): its matrix,
    // far point and radius, its tile in the atlas (x, y, size).
    vec4 env_[5];
    ivec4 env_mode;
    mat4 env_sun_matrix_;
    vec4 env_sun_light_;
    vec4 env_sun_tile;
    // The room's medium for the unshadowed haze (haze Low; quality0.x 1):
    // the uniform haze, the ground fog, the fog volumes (medium.glsl).
    vec4 medium_[167];
    // The fog volumes' own renderer (vk_fogvol.inc): its image's size, on, the debug view (1 their
    // light alone, 2 their transmittance); its pixels per display pixel.
    ivec4 fogvol;
    vec4 fogvol_scale;
    // The modern menu's model view (vk_present.c backdrop_keep): 2D pixels show the kept picture of the
    // world (backdrop.x 2) or the ground (1) under the menu's ground colour (rgb) at its opacity (a).
    vec4 backdrop_color;
    ivec4 backdrop; // y: the model view's studio light (menu_studio.glsl)
    vec4 studio_[9]; // its rig (ui/menu_light.h pc_menu_light_pack: PC_MENU_LIGHT_VEC4S)
    ivec4 particles_; // the particles (vk_particles.inc): on, the display's size in game pixels
    vec4 look_; // post.h pc_post_frame: the soft shoulder's start and width, tilt-shift's feather
    // Material response (emissive.h, pc/docs/Materials.md): x global specular
    // strength, y roughness bias, z 1 the material target is lit (0: the
    // specular lobe off, the picture exactly as without materials), w 1 a
    // traced reflection result is bound (binding 24, at GI resolution).
    vec4 material_;
} u;
layout(binding = 23) uniform sampler2D material_tex; // roughness, specular, metal, 1 (material.glsl)
layout(binding = 24) uniform sampler2D reflect_tex; // traced reflections (rgb), GI resolution
layout(binding = 20) uniform sampler2D backdrop_tex;
layout(binding = 21) uniform usampler2D particle_keys; // the particles' key image (particles.glsl)
layout(binding = 22) uniform sampler2D particle_colours;
#include "particles.glsl"
#define MEDIUM(i) u.medium_[i]
layout(binding = 17) uniform sampler3D medium_tex; // the medium's texture (atmosphere.h pc_medium_texture)
#define MEDIUM_TEX medium_tex
#include "medium.glsl"
#define MENU_STUDIO(i) u.studio_[i]
#include "light_shape.glsl"
#include "menu_studio.glsl"
#define env_sun_dir u.env_[0]
#define env_sun_col u.env_[1]
#define env_zenith u.env_[2]
#define env_horizon u.env_[3]
#define env_ground u.env_[4]
#define env_sun_matrix u.env_sun_matrix_
#define env_sun_light u.env_sun_light_
#define env_sun_texel (1.0 / u.env_sun_tile.z)
// Every dynamic light (vk_present.c, any number: light_count), world space.
struct Light {
    vec4 pos; // xyz, radius
    vec4 col; // rgb * intensity, type (0 point, 1 spot, 2 rect)
    vec4 right, up; // rect half extents; right.w the light's own size (soft shadows)
    vec4 dir; // direction, cos(half cone)
    vec4 world; // shadow map lookups: position, radius
    ivec4 info; // x: -2 ray-traced visibility (gi alpha), >= 0 its shadow map, else unshadowed
    vec4 pattern_cdf[64]; // same std430 stride as light_list.h
    vec4 pattern_dir;
};
layout(std430, binding = 16) readonly buffer LightList { Light lights[]; };
#define WINDOW_LIGHT(i) lights[i]
#include "window_pattern.glsl"
layout(binding = 1) uniform sampler2D target;
layout(binding = 2) uniform sampler2D depth_tex;
layout(binding = 3) uniform sampler2D normal_tex;
layout(binding = 4) uniform sampler2D albedo_tex;
layout(binding = 5) uniform sampler2D shade_tex;
layout(binding = 6) uniform sampler2D gi_tex;
layout(binding = 7) uniform sampler2D gi_position;
layout(binding = 8) uniform sampler2D gi_normal;
layout(binding = 9) uniform sampler2D gi_extra;
layout(binding = 11) uniform sampler2D shadow_atlas; // vk_shadows.c: SLOTS rows of six 512 x 512 cube faces, the sun's tile
#define ENV_SUN_MAP(st) texture(shadow_atlas, (u.env_sun_tile.xy + clamp(st, vec2(0.5 / u.env_sun_tile.z), vec2(1.0 - 0.5 / u.env_sun_tile.z)) * u.env_sun_tile.z) / vec2(textureSize(shadow_atlas, 0))).r
#include "environment.glsl"
layout(binding = 18) uniform sampler2D fogvol_light; // rgb the fog volumes' light, a their transmittance
layout(binding = 19) uniform sampler2D fogvol_aux; // x the scene's view depth the march stopped at
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
#define shoulder u.look_.xy
#define tilt_feather u.look_.z
#define fog u.fog_
#define fog_range u.fog_range_.xy
#define room_lo u.room_lo_.xyz
#define room_hi u.room_hi_.xyz
#define projection u.projection_.xyz
#define screen_offset u.screen_offset_.xy
#define camera_translation u.camera_translation_.xyz
mat3 inverse_rotation() { return mat3(u.inverse_rotation_[0].xyz, u.inverse_rotation_[1].xyz, u.inverse_rotation_[2].xyz); }
mat3 normal_to_world() { return mat3(u.normal_to_world_[0].xyz, u.normal_to_world_[1].xyz, u.normal_to_world_[2].xyz); }
int light_shadow(int i) { return lights[i].info.x; }
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
// Unshadowed haze between the eye and `world` (haze Low; High traces it):
// the room's medium (medium.glsl) at 8 jittered steps of the part of the
// ray it can be in, every light's in-scattering (unshadowed) and the sun's
// (through its shadow map where it has one), the traced haze's phase
// function and units, Beer-Lambert between the steps.
vec3 low_haze(vec3 c, vec3 world) {
    float g = medium_g();
    vec3 eye = inverse_rotation() * -camera_translation;
    vec3 v = world - eye; float length_ = length(v); vec3 dir = v / max(length_, 1e-3);
    vec2 span = medium_span(eye, dir, length_);
    if (!(span.y > span.x)) return c;
    const int STEPS = 8;
    float step_ = (span.y - span.x) / float(STEPS), jitter = ign(gl_FragCoord.xy + 17.0);
    vec3 sum = vec3(0.0);
    float transmittance = 1.0;
    for (int k = 0; k < STEPS; k++) {
        vec3 x = eye + dir * (span.x + (float(k) + jitter) * step_);
        vec3 scatter, emit;
        float extinction = medium_at(x, scatter, emit);
        if (extinction <= 0.0) continue;
        vec3 here = vec3(0.0);
        for (int i = 0; i < light_count; i++) {
            vec3 centre = window_aperture(i) ? window_centre(i, x) : lights[i].pos.xyz;
            vec3 to = centre - x; float dl = length(to); vec3 l = to / max(dl, 1e-3);
            float f = clamp(dl / lights[i].pos.w, 0.0, 1.0);
            float a = (1.0 - f * f) * (1.0 - f * f);
            a *= light_shape(lights[i].col.w, lights[i].pos.xyz, lights[i].dir, lights[i].right, lights[i].up, x, l);
            if (window_aperture(i)) a *= window_transmission(i, x);
            here += lights[i].col.rgb * a * medium_phase(dot(dir, l), g);
        }
        if (env_sun_on())
            here += env_sun_col.rgb * medium_phase(dot(dir, env_sun_dir.xyz), g)
                  * (u.env_mode.x == 0 ? medium_sun_visibility(env_sun_map_point(x, fract(jitter + float(k) * 0.618034))) : 1.0);
        float a = medium_step(extinction, step_, transmittance);
        sum += transmittance * (scatter * here + emit) * ((1.0 - a) / extinction);
        transmittance *= a;
    }
    return c * transmittance + medium_limit_light(sum);
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
// ---- material response (pc/docs/Materials.md "Shading") ----
// GGX (Trowbridge-Reitz) with the height-correlated Smith visibility, in the
// lights' own units: the diffuse term is albedo x light x cosine (no 1 / pi),
// so the specular lobe carries pi to match. `alpha` is roughness squared.
float ggx_d(float nh, float alpha) {
    float a2 = alpha * alpha, d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-7);
}
float smith_vis(float nl, float nv, float alpha) {
    float a2 = alpha * alpha;
    float gv = nl * sqrt(nv * nv * (1.0 - a2) + a2), gl = nv * sqrt(nl * nl * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}
vec3 schlick(vec3 f0, float f90, float c) { float k = pow(1.0 - c, 5.0); return f0 + (vec3(f90) - f0) * k; }
// The specular reflectance of a light from direction l (without its cosine,
// which the caller's attenuation already lacks): pi D V F. `widen`: the
// light's angular size as a roughness floor (sphere lights, Karis 2013),
// with the lobe renormalised so a larger source is not brighter.
vec3 spec_brdf(vec3 n, vec3 v, vec3 l, float alpha, vec3 f0, float f90, float widen) {
    vec3 h = normalize(l + v);
    float nl = max(dot(n, l), 0.0), nv = max(dot(n, v), 1e-4), nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
    if (nl <= 0.0) return vec3(0.0);
    float a = clamp(alpha + widen, alpha, 1.0), norm = (alpha / a) * (alpha / a);
    return 3.14159265 * ggx_d(nh, a) * norm * smith_vis(nl, nv, a) * schlick(f0, f90, vh) * nl;
}
// Split-sum environment term (Karis' analytic fit): scale and bias of F0.
vec2 env_brdf(float roughness, float nv) {
    vec4 r = roughness * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}
// A rect light's point nearest the reflection ray (its specular centre).
vec3 rect_spec_point(int i, vec3 p, vec3 r) {
    vec3 c = lights[i].pos.xyz, rr = lights[i].right.xyz, ru = lights[i].up.xyz;
    vec3 pn = cross(rr, ru);
    float dn = dot(r, pn);
    vec3 hit = abs(dn) > 1e-6 ? p + r * max(dot(c - p, pn) / dn, 0.0) : c;
    vec3 rel = hit - c;
    return c + rr * clamp(dot(rel, rr) / dot(rr, rr), -1.0, 1.0) + ru * clamp(dot(rel, ru) / dot(ru, ru), -1.0, 1.0);
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
// A void pixel's haze (medium.glsl medium_void_pixel): the traced haze's froxels, else haze Low, only
// over the share `reach` of its ray (medium_void_reach) that crosses the room's interior.
vec3 void_haze(vec3 c, ivec2 q, float d, float reach) {
    if (reach <= 0.0) return c;
    if (u.haze_grid.w != 0) return hazed(c, q, d * reach);
    if (u.quality0.x > 0.0) return low_haze(c, mix(inverse_rotation() * -camera_translation, world_at(q, d * 65536.0), reach));
    return c;
}
// The fog volumes between the eye and view depth z at display position
// `display`: their march image (a lower resolution) upsampled with a joint
// bilateral filter (the 2 x 2 march pixels around, weighted bilinearly and
// by how close the depth they marched to is to this pixel's; with none
// close, the closest one), so fog never bleeds across an edge.
vec4 fogvol_at(vec2 display, float z) {
    vec2 sp = display * u.fogvol_scale.xy - 0.5;
    ivec2 b = ivec2(floor(sp)), size = u.fogvol.xy;
    vec2 f = sp - vec2(b);
    vec4 sum = vec4(0.0), best = vec4(0.0, 0.0, 0.0, 1.0);
    float weight = 0.0, best_dz = 1e30;
    for (int y = 0; y <= 1; y++)
        for (int x = 0; x <= 1; x++) {
            ivec2 h = clamp(b + ivec2(x, y), ivec2(0), size - 1);
            float zh = texelFetch(fogvol_aux, h, 0).x, dz = abs(zh - z);
            vec4 c = texelFetch(fogvol_light, h, 0);
            float w = (x == 1 ? f.x : 1.0 - f.x) * (y == 1 ? f.y : 1.0 - f.y) + 1e-3;
            w *= exp(-dz / (0.03 * min(z, zh) + 4.0));
            sum += c * w; weight += w;
            if (dz < best_dz) { best_dz = dz; best = c; }
        }
    vec4 r = weight > 1e-4 ? sum / weight : best;
    return any(isnan(r)) ? vec4(0.0, 0.0, 0.0, 1.0) : r;
}
vec3 fogvol_apply(vec3 c, ivec2 q, float d) {
    bool far = d >= 1.0 || d * 65536.0 < 0.5;
    vec4 f = fogvol_at((vec2(q) + 0.5) / float(scale) - vec2(rect.xy), far ? 1e6 : d * 65536.0);
    return c * clamp(f.a, 0.0, 1.0) + max(f.rgb, vec3(0.0));
}
void main() {
    float d = depth_at(uv);
    // The void around the room (medium.glsl medium_void_pixel): the room's haze and ground fog only up to
    // where the ray leaves the interior (none past a cut-away wall; all of it in front of a black doorway).
    bool void_px = medium_void_pixel(d, texelFetch(shade_tex, texel(uv), 0));
    float reach = void_px && d < 1.0 && d * 65536.0 >= 0.5
        ? medium_void_reach(inverse_rotation() * -camera_translation, world_at(texel(uv), d * 65536.0)) : 0.0;
    if (u.fogvol.w != 0) { // fog_volume_debug: 1 the volumes' light alone, 2 their transmittance
        bool ui_ = d * 65536.0 < 0.5;
        vec4 f = ui_ || u.fogvol.z == 0 ? vec4(0.0, 0.0, 0.0, 1.0) : fogvol_at((vec2(texel(uv)) + 0.5) / float(scale) - vec2(rect.xy), d >= 1.0 ? 1e6 : d * 65536.0);
        color = vec4(u.fogvol.w == 2 ? vec3(clamp(f.a, 0.0, 1.0)) : min(f.rgb, vec3(1.0)), ui_ ? 9999.0 : 0.0);
        return;
    }
    bool haze_done = false, fogvol_done = false, particles_done = false;
    if (u.quality1.z != 0) { // haze_debug 1: the haze's light alone (over black); 2: where the room's air is
        bool ui_ = d * 65536.0 < 0.5;
        if (u.quality1.z == 2) { // red: a void pixel; green: the room's interior at the surface (medium_interior);
                                 // blue: a void pixel's reach (the share of its ray that keeps the room's air)
            float inside = !ui_ && d < 1.0 ? medium_interior(world_at(texel(uv), d * 65536.0)) : 0.0;
            color = vec4(!ui_ && void_px ? 1.0 : 0.0, inside, !ui_ && void_px ? reach : 0.0, ui_ ? 9999.0 : 0.0);
            return;
        }
        vec3 hz = vec3(0.0);
        if (!ui_ && void_px) hz = void_haze(vec3(0.0), texel(uv), min(d, 1.0), reach);
        else if (!ui_ && u.haze_grid.w != 0) hz = hazed(vec3(0.0), texel(uv), min(d, 1.0));
        else if (!ui_ && u.quality0.x > 0.0) hz = low_haze(vec3(0.0), world_at(texel(uv), min(d, 0.999) * 65536.0));
        if (!ui_ && u.fogvol.z != 0) hz += fogvol_apply(vec3(0.0), texel(uv), d);
        color = vec4(min(hz, vec3(1.0)), ui_ ? 9999.0 : 0.0);
        return;
    }
    if (show_depth != 0) { // far black, UI magenta, 3D log-grey near-bright
        float sz = d * 65536.0;
        color = d >= 1.0 ? vec4(0.0, 0.0, 0.0, 1.0)
              : sz < 0.5 ? vec4(0.6, 0.0, 0.6, 1.0)
              : vec4(vec3(1.0 - clamp((log2(sz) - 6.0) / 8.0, 0.0, 1.0)), 1.0);
        return;
    }
    vec3 c = base(uv);
    // Emissive texels (post bit 16; emissive.h): the shade target's alpha 0.25
    // marks them, its rgb is their emission / 2. Kept above the lit result
    // (never darkened), above 1 for bloom when the grade pass runs (bit 32).
    bool em_px = false, lit_done = false;
    vec3 em = vec3(0.0);
    if ((post & 16) != 0 && view_mode != 1) {
        vec4 es = texelFetch(shade_tex, texel(uv), 0);
        if (abs(es.a - 0.25) < 0.05 && d > 0.0) { em_px = true; em = es.rgb * 2.0; }
    }
    // The menu's model view: its own studio light (menu_studio.glsl), not the room's.
    if (u.backdrop.y != 0 && view_mode == 0) {
        ivec2 q = texel(uv);
        vec4 n = texelFetch(normal_tex, q, 0);
        if (n.a > 0.5 && d > 0.0) {
            float z = texelFetch(depth_tex, q, 0).r * 65536.0;
            vec2 s = (vec2(q) + 0.5) / float(scale) - vec2(rect.xy) - screen_offset;
            vec3 p = vec3(s.x * z / projection.x, s.y * z / projection.y, z);
            c = menu_studio(texelFetch(albedo_tex, q, 0).rgb, smooth_normal(q, normalize(n.xyz * 2.0 - 1.0), z), p);
            if (em_px) c = max(c, em);
            else if ((post & 8) == 0) c = mix(c, 1.0 - shoulder.y * exp(-(c - shoulder.x) / shoulder.y), step(shoulder.x, c));
            if (any(isnan(c)) || any(isinf(c))) c = base(uv);
            lit_done = haze_done = fogvol_done = particles_done = true;
        }
    }
    if (!lit_done && (light_count > 0 || baked != 1.0 || gi_on != 0 || view_mode != 0 || u.quality1.y != 0 || u.env_mode.z != 0) && view_mode != 1) {
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
            // Material response: this pixel's roughness, specular, metal (material.glsl).
            vec4 mat = u.material_.z > 0.5 ? texelFetch(material_tex, q, 0) : vec4(0.0);
            bool has_mat = mat.a > 0.5;
            float m_rough = clamp(mat.r + u.material_.y, 0.03, 1.0), m_alpha = m_rough * m_rough;
            vec3 m_view = normalize(eye_world - world);
            vec3 m_f0 = mix(vec3(0.04 * mat.g), albedo * mat.g, mat.b);
            float m_f90 = mix(mat.g, 1.0, mat.b);
            vec3 spec = vec3(0.0);
            vec3 m_r = reflect(-m_view, world_normal);
            for (int i = 0; i < light_count; i++) {
                vec3 centre = lights[i].pos.xyz;
                if (window_aperture(i)) centre = window_centre(i, world);
                else if (lights[i].col.w > 1.5) { // rect: nearest point on the emitter
                    vec3 rr = lights[i].right.xyz, ru = lights[i].up.xyz, rel = world - centre;
                    centre += rr * clamp(dot(rel, rr) / dot(rr, rr), -1.0, 1.0)
                            + ru * clamp(dot(rel, ru) / dot(ru, ru), -1.0, 1.0);
                }
                vec3 to = centre - world;
                float dl = length(to);
                vec3 l = to / max(dl, 1e-3);
                float x = clamp(dl / lights[i].pos.w, 0.0, 1.0);
                float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(world_normal, l), 0.0);
                // a spot's edge, a rect's front face and barn doors (light_shape.glsl)
                a *= light_shape(lights[i].col.w, lights[i].pos.xyz, lights[i].dir, lights[i].right, lights[i].up, world, l);
                if (window_aperture(i)) a *= window_transmission(i, world);
                if (light_shadow(i) == -2) // ray-traced
                    a *= clamp(gi.a, 0.0, 1.0);
                else if (light_shadow(i) >= 0 && a > 0.0) { // shadow map (no ray tracing)
                    float cosine = clamp(dot(world_normal, l), 0.05, 1.0);
                    a *= map_visibility(light_shadow(i), world + world_normal * max(2.0, lights[i].world.w * 0.01), lights[i].world,
                        lights[i].col.w < 1.5 && lights[i].right.w > 0.0 ? lights[i].right.w : u.quality2.x, min(sqrt(1.0 - cosine * cosine) / cosine, 4.0));
                }
                sum += lights[i].col.rgb * a;
                if (has_mat && a > 0.0) {
                    // The light's attenuation without its cosine (the diffuse term's own, untouched).
                    float ndl = max(dot(world_normal, l), 0.0);
                    float att = ndl > 1e-3 ? a / ndl : 0.0;
                    vec3 ls = l, ps = centre;
                    if (lights[i].col.w > 1.5 && !window_aperture(i)) { ps = rect_spec_point(i, world, m_r); ls = normalize(ps - world); }
                    float size = lights[i].right.w > 0.0 ? lights[i].right.w : u.quality2.x;
                    float widen = lights[i].col.w > 1.5 ? 0.0 : clamp(size / max(3.0 * distance(ps, world), 1.0), 0.0, 1.0);
                    spec += lights[i].col.rgb * att * spec_brdf(world_normal, m_view, ls, m_alpha, m_f0, m_f90, widen);
                }
            }
            // The sun: traced visibility (the trace weighs it into gi.a with the lights), else
            // unshadowed (with the screen-space AO when on); the sky's ambient where nothing traces it.
            if (env_sun_on())
                sum += env_sun_unshadowed(world_normal) * (u.env_mode.x == -2 ? clamp(gi.a, 0.0, 1.0)
                    : u.env_mode.x == 0 ? env_sun_map_visibility(world, world_normal, ign(gl_FragCoord.xy) * 6.2831853) : ao);
            if (u.env_mode.y != 0)
                sum += env_ambient(world_normal) * ao;
            if (has_mat && env_sun_on()) { // the sun's highlight: its disc's angular size widens the lobe
                float sv = u.env_mode.x == -2 ? clamp(gi.a, 0.0, 1.0)
                    : u.env_mode.x == 0 ? env_sun_map_visibility(world, world_normal, ign(gl_FragCoord.xy) * 6.2831853) : ao;
                spec += env_sun_col.rgb * sv * spec_brdf(world_normal, m_view, env_sun_dir.xyz, m_alpha, m_f0, m_f90, env_sun_dir.w);
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
            if (has_mat) {
                // Energy: what the lobe reflects (Fresnel at the view) is not diffused; metal diffuses nothing.
                float nv = max(dot(world_normal, m_view), 1e-4);
                vec2 ab = env_brdf(m_rough, nv);
                vec3 f_env = m_f0 * ab.x + m_f90 * ab.y;
                vec3 kd = (1.0 - f_env) * (1.0 - mat.b);
                // Traced reflections (the path tracer's specular rays, GI resolution).
                if (u.material_.w > 0.5) spec += texture(reflect_tex, ((vec2(q) + 0.5) / float(scale) - vec2(rect.xy)) / vec2(rect.zw)).rgb * f_env * ao;
                spec *= u.material_.x;
                if (view_mode == 6) { color = vec4(mat.rgb, 1.0); return; }
                // 8: the traced reflections alone (before Fresnel), magenta when none is bound
                if (view_mode == 8) { color = u.material_.w > 0.5 ? vec4(min(texture(reflect_tex, ((vec2(q) + 0.5) / float(scale) - vec2(rect.xy)) / vec2(rect.zw)).rgb, vec3(1.0)), 1.0) : vec4(1.0, 0.0, 1.0, 1.0); return; }
                if (view_mode == 7) { color = vec4(min(spec, vec3(1.0)), 1.0); return; }
                c = c * baked * baked_vis * ao + albedo * sum * mix(vec3(1.0), kd, min(u.material_.x, 1.0)) + spec;
            } else {
                if (view_mode >= 6 && view_mode <= 8) { color = vec4(0.0, 0.0, 0.0, 1.0); return; }
                c = c * baked * baked_vis * ao + albedo * sum;
            }
            if (em_px) c = max(c, em);
            lit_done = true;
            if (u.particles_.x != 0 && view_mode == 0) { // the motes in front of it, before the fog and haze
                c += particles_at(particle_keys, particle_colours, u.particles_.yz, uv, d);
                particles_done = true;
            }
            if (u.fogvol.z != 0) { c = fogvol_apply(c, q, d); fogvol_done = true; }
            if (void_px) { c = void_haze(c, q, d, reach); haze_done = true; } // the void: clean darkness, as on the PS1
            else if (u.haze_grid.w != 0) { c = hazed(c, q, d); haze_done = true; }
            else if (u.quality0.x > 0.0) { c = low_haze(c, world); haze_done = true; }
            // Soft shoulder above 0.8 (post shoulder_start, _width) instead of clipping at white (lit pixels only).
            if ((post & 8) == 0 && !em_px) c = mix(c, 1.0 - shoulder.y * exp(-(c - shoulder.x) / shoulder.y), step(shoulder.x, c)); // 8: HDR out
            if (any(isnan(c)) || any(isinf(c))) c = base(uv);
        }
    }
    bool ui = d * 65536.0 < 0.5;
    if (em_px && !lit_done) c = max(c, em); // unlit: the raster's colour has it up to 1
    bool over = (post & 8) != 0 || (em_px && (post & 32) != 0);
    if (ui && u.backdrop.x != 0) // the menu's model view: the world behind, under the ground
        c = mix(u.backdrop.x == 2 ? texture(backdrop_tex, uv).rgb : u.backdrop_color.rgb, u.backdrop_color.rgb,
            u.backdrop_color.a);
    // Unlit 3D pixels (effects, particles, glows) get the same haze as their
    // neighbours: no dark cut-outs around them. The fog volumes also cover
    // where nothing is drawn (their far side).
    if (u.particles_.x != 0 && !particles_done && view_mode == 0) // unlit pixels, and where nothing is drawn
        c += particles_at(particle_keys, particle_colours, u.particles_.yz, uv, d);
    if (u.fogvol.z != 0 && !fogvol_done && !ui && view_mode != 1) c = fogvol_apply(c, texel(uv), d);
    if (void_px && !haze_done && !ui && d < 1.0 && view_mode != 1) c = void_haze(c, texel(uv), d, reach);
    else if (u.haze_grid.w != 0 && !haze_done && !ui && !void_px && d < 1.0 && view_mode != 1) c = hazed(c, texel(uv), d);
    else if (u.quality0.x > 0.0 && !haze_done && !ui && !void_px && d < 1.0 && view_mode != 1)
        c = low_haze(c, world_at(texel(uv), d * 65536.0));
    vec3 out_c = over && !ui ? max(c, vec3(0.0)) : min(c, vec3(1.0));
    if ((post & 7) == 0 || ui) { color = vec4(out_c, ui ? 9999.0 : 0.0); return; }
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
        if (coc < 0.0) coc *= u.tilt_.w; // the share in front of the focal plane (post.h near_blur)
    }
    if ((post & 2) != 0) {
        float t = tilt.z * smoothstep(tilt.y, tilt.y + tilt_feather, abs(uv.y - tilt.x));
        coc = abs(coc) >= t ? coc : t;
    }
    color = vec4(over ? max(c, vec3(0.0)) : min(c, vec3(1.0)), coc);
}
