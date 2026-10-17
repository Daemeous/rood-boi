/* OpenGL presentation for the SDL window: an OpenGL 4.6 (or 3.3) core
 * context draws the PS1 display area from a VRAM texture, aspect-correct,
 * into a resizable window. SDL loads libGL/libEGL at run time, so machines
 * (or Xvfb servers) without OpenGL fall back to the software surface in
 * boot_window.c. ROOD_RENDERER=software (or x11, its old name) forces that
 * fallback. */
#define _POSIX_C_SOURCE 200809L
#include "platform.h" /* pc_replace_file */
#include "gl_api.h"
#include "dev_state.h"
#include "light_shape.h"
#include "gl_shaders.h" /* PC_GLSL_PROBE_COMMON: the probe GI lookup (probe_gi.inc) */
#include "environment.h"
#include "atmosphere.h"
#include "lighting_quality.h"
#include "antialias.h"
#include "post.h"
#include "emissive.h"
#include "particles.h"
#include "ui/menu_light.h" /* the model view's studio light */
#include <SDL3/SDL.h>
#include <math.h> /* powf: brightness */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- GL entry points (gl_api.h), resolved through SDL_GL_GetProcAddress ---- */
#define DEFINE(ret, name, args) ret(*gl_##name) args;
GL_FUNCTIONS(DEFINE)
GL_COMPUTE_FUNCTIONS(DEFINE)
#undef DEFINE
int pc_gl_compute;
static char renderer_name[160]; /* GL_RENDERER (pc_gl_renderer) */

static SDL_Window* window;
static SDL_GLContext context;
static GLuint program, vram_texture, vertex_array;
static GLint u_rect, u_vram, u_rgb24;
extern int pc_display_masked __attribute__((weak)); /* boot_sdk.c SetDispMask */
extern int pc_display_rgb24;
static int window_w, window_h;
int pc_gl_active;

static GLuint compile(GLenum type, const char* source)
{
    GLuint shader = gl_CreateShader(type);
    gl_ShaderSource(shader, 1, &source, NULL);
    gl_CompileShader(shader);
    GLint ok;
    gl_GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl_GetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "[gl] shader: %s\n", log);
        return 0;
    }
    return shader;
}

GLuint pc_gl_program(const char* vertex, const char* fragment)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs)
        return 0;
    GLuint program = gl_CreateProgram();
    gl_AttachShader(program, vs);
    gl_AttachShader(program, fs);
    gl_LinkProgram(program);
    GLint linked;
    gl_GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        gl_GetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "[gl] link: %s\n", log);
        return 0;
    }
    return program;
}

/* Vertex, geometry and fragment stages (the native renderer, render.c). */
GLuint pc_gl_program3(const char* vertex, const char* geometry, const char* fragment)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vertex), gs = compile(GL_GEOMETRY_SHADER, geometry),
           fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !gs || !fs)
        return 0;
    GLuint program = gl_CreateProgram();
    gl_AttachShader(program, vs);
    gl_AttachShader(program, gs);
    gl_AttachShader(program, fs);
    gl_LinkProgram(program);
    GLint linked;
    gl_GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[4096];
        gl_GetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "[gl] link: %s\n", log);
        return 0;
    }
    return program;
}

GLuint pc_gl_compute_program(const char* source)
{
    if (!pc_gl_compute)
        return 0;
    GLuint cs = compile(GL_COMPUTE_SHADER, source);
    if (!cs)
        return 0;
    GLuint program = gl_CreateProgram();
    gl_AttachShader(program, cs);
    gl_LinkProgram(program);
    GLint linked;
    gl_GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[4096];
        gl_GetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "[gl] compute link: %s\n", log);
        return 0;
    }
    return program;
}

static const char* vertex_source =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    uv = vec2(p.x, 1.0 - p.y);\n" /* 0..1 across the view (p reaches 2 off-screen), top row first */
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
/* VRAM is BGR555 with the mask bit on top, stored as one 16-bit integer.
 * In 24-bit display mode (the intro movie) pixels are R, G, B bytes packed
 * across those halfwords, starting at the display's left edge. */
static const char* fragment_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform usampler2D vram;\n"
    "uniform ivec4 rect;\n" /* display x, y, width, height in VRAM pixels */
    "uniform int rgb24;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(floor(uv * vec2(rect.zw)));\n"
    "    p = clamp(p, ivec2(0), rect.zw - 1);\n"
    "    if (rgb24 != 0) {\n"
    "        int row = (rect.y + p.y) & 511, byte = rect.x * 2 + p.x * 3;\n"
    "        vec3 c;\n"
    "        for (int i = 0; i < 3; i++, byte++) {\n"
    "            uint v = texelFetch(vram, ivec2((byte >> 1) & 1023, row), 0).r;\n"
    "            c[i] = float((byte & 1) != 0 ? v >> 8 : v & 255u) / 255.0;\n"
    "        }\n"
    "        color = vec4(c, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    uint v = texelFetch(vram, ivec2((rect.x + p.x) & 1023, (rect.y + p.y) & 511), 0).r;\n"
    "    color = vec4(float(v & 31u), float((v >> 5) & 31u), float((v >> 10) & 31u), 31.0) / 31.0;\n"
    "}\n";

/* Present from the hardware rasterizer's scaled VRAM target, with optional
 * depth-based post effects. Depth is GTE SZ / 65536 (0 = 2D/UI, 1 = far);
 * effects leave UI pixels alone. */
static const char* scaled_fragment_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D target;\n"
    "uniform ivec4 rect;\n"
    "uniform int scale;\n"
    "uniform int filtered;\n" /* 0 nearest, 1 bilinear, 2 supersample (box) */
    "uniform vec2 view;\n" /* viewport size in window pixels */
    "uniform int show_depth;\n"
    "uniform sampler2D depth_tex;\n"
    "uniform int post;\n" /* bit 0 depth of field, 1 tilt-shift, 2 fog, 3 HDR out (the grade pass tone maps) */
    "uniform float focus_sz;\n" /* 0 = auto (screen centre) */
    "uniform float dof_radius;\n" /* maximum blur radius in window pixels */
    "uniform float aperture;\n" /* blur growth away from the focal plane */
    "uniform float near_blur;\n" /* the share of it in front of the focal plane */
    "uniform vec3 tilt;\n" /* band centre (0..1 from top), half width, radius */
    "uniform vec4 fog;\n" /* rgb colour, a = density */
    "uniform vec2 fog_range;\n" /* SZ where fog starts and saturates */
    /* The modern menu's model view (backdrop_frame below): 2D pixels show the held picture of the world
     * (mode 2) or the ground (mode 1) under the menu's ground colour (rgb) at its opacity (a). */
    "uniform sampler2D backdrop_tex;\n"
    "uniform vec4 backdrop_color;\n"
    "uniform int backdrop_mode;\n"
    "uniform int menu_studio_on;\n" /* the model view's studio light (menu_studio.glsl) */
    "uniform vec4 menu_studio_rig[9];\n" /* its rig (ui/menu_light.h pc_menu_light_pack) */
    "#define MENU_STUDIO(i) menu_studio_rig[i]\n"
    "uniform sampler2D normal_tex;\n" /* G-buffer: view normal (a = lit 3D surface) */
    "uniform sampler2D albedo_tex;\n"
    "uniform sampler2D shade_tex;\n" /* vertex colour; a: 1 textured, 0.5 untextured */
    "uniform int view_mode;\n" /* editor: 0 lit, 1 unlit, 2 lighting only, 3 base colour, 4 vertex colour, 5 normals */
    "uniform int light_count;\n"
    "uniform vec4 light_pos[64];\n" /* view-space xyz, radius */
    "uniform vec4 light_col[64];\n" /* rgb * intensity, type (0 point, 1 spot, 2 rect) */
    "uniform vec4 light_right[64];\n" /* rect: view-space half extent along its width */
    "uniform vec4 light_up[64];\n" /* rect: view-space half extent along its height */
    "uniform vec4 light_dir[64];\n" /* view-space direction, cos(half cone) */
    "uniform float baked;\n" /* scale on the game's baked lighting */
    "uniform sampler2D gi_tex;\n" /* indirect irradiance over the display area (ptgi.c) */
    "uniform int gi_on;\n" /* 1 add it, 2 show it alone, 4 its alpha is shadow visibility */
    "uniform sampler2D gi_position;\n" /* the GI pixels' world positions (w = surface) */
    "uniform sampler2D gi_extra;\n" /* r: how much baked light reaches the surface past characters, gba: haze light */
    "uniform float fog_density;\n" /* 1: the traced haze (ptgi.c) is on; its medium in medium_ */
    "uniform vec3 room_lo, room_hi;\n"
    /* Length of the eye-to-p segment inside the room's bounds. */
    "float inside_room(vec3 eye, vec3 p) {\n"
    "    vec3 v = p - eye; float l = length(v); vec3 inv = l / v;\n"
    "    vec3 t0 = (room_lo - eye) * inv, t1 = (room_hi - eye) * inv;\n"
    "    float enter = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));\n"
    "    float leave = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), l));\n"
    "    return max(leave - enter, 0.0);\n"
    "}\n"
    "uniform sampler2D gi_normal;\n"
    "uniform ivec2 gi_size;\n"
    "uniform float gi_strength;\n"
    "uniform vec3 projection;\n" /* H (widescreen-narrowed for x), H, unused */
    "uniform vec2 screen_offset;\n" /* OFX, OFY */
    "uniform mat3 inverse_rotation;\n" /* view -> world: R^-1 */
    "uniform mat3 normal_to_world;\n" /* R^T: view normals (R's rows differ in scale) */
    "uniform vec3 camera_translation;\n"
    "uniform vec4 light_world[64];\n" /* world xyz, radius */
    /* Probe GI (gi_method probes, probe_gi.inc; gi_on 64): the volume and its atlases. */
    "uniform vec4 pv_origin, pv_spacing, pv_bias;\n"
    "uniform ivec4 pv_counts;\n"
    "uniform sampler2D probe_irradiance, probe_depth, probe_data;\n"
    "#define PROBE_SAMPLING\n"
    PC_GLSL_PROBE_COMMON
    "uniform int light_shadow[64];\n" /* cube slot or -1 */
    "uniform samplerCube shadow0, shadow1, shadow2, shadow3;\n"
    "uniform float shadow_softness;\n"
    /* Lighting quality (lighting_quality.cpp): unshadowed haze density (0
     * off), anisotropy, screen-space AO radius, strength; shadow map filter
     * (0 hard, 1 PCF, 2 PCSS), screen-space AO on; light size (PCSS). */
    "uniform vec4 quality0;\n"
    "uniform ivec4 quality1;\n"
    "uniform vec4 quality2;\n"
    /* The room's sun and sky (environment.h; vk scaled.frag the same): env_mode x the sun's
     * shadows (-2 traced: in gi.a, 0 its shadow map, -1 none), y the sky's ambient term, z any on. */
    "uniform vec4 env_[5];\n"
    "uniform ivec4 env_mode;\n"
    "uniform sampler2D sun_map;\n" /* shadows.c pc_shadows_sun_update */
    "uniform mat4 env_sun_matrix;\n"
    "uniform vec4 env_sun_light;\n"
    "uniform float env_sun_texel;\n"
    "#define ENV_SUN_MAP(st) texture(sun_map, st).r\n"
    "#define env_sun_dir env_[0]\n"
    "#define env_sun_col env_[1]\n"
    "#define env_zenith env_[2]\n"
    "#define env_horizon env_[3]\n"
    "#define env_ground env_[4]\n" PC_GLSL_ENVIRONMENT
    /* The room's medium (atmosphere.h pc_medium_gpu; medium.glsl): the haze,
     * the ground fog, the fog volumes (haze Low marches it; the traced haze's
     * transmittance). */
    "uniform vec4 medium_[166];\n"
    "#define MEDIUM(i) medium_[i]\n"
    "uniform sampler3D medium_tex;\n" /* atmosphere.h pc_medium_texture (pc_gl_medium_texture) */
    "#define MEDIUM_TEX medium_tex\n" PC_GLSL_MEDIUM PC_GLSL_MENU_STUDIO
    "float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }\n"
    "uniform float normal_smoothing;\n" /* radius in native pixels, 0 = flat faces */
    /* Depth- and crease-aware average of nearby normals (screen space). */
    "vec3 smooth_normal(ivec2 q, vec3 n, float z) {\n"
    "    if (normal_smoothing <= 0.0) return n;\n"
    "    vec3 sum = n;\n"
    "    float r = normal_smoothing * float(scale);\n"
    "    for (int k = 0; k < 12; k++) {\n"
    "        float a = float(k) * 0.5236, f = (k & 1) != 0 ? 1.0 : 0.55;\n"
    "        ivec2 o = q + ivec2(vec2(cos(a), sin(a)) * r * f);\n"
    "        vec4 m = texelFetch(normal_tex, o, 0);\n"
    "        if (m.a < 0.5) continue;\n"
    "        vec3 mn = normalize(m.xyz * 2.0 - 1.0);\n"
    "        float mz = texelFetch(depth_tex, o, 0).r * 65536.0;\n"
    "        float w = step(0.64, dot(n, mn)) * exp(-abs(mz - z) / (0.03 * z + 1.0));\n"
    "        sum += mn * w;\n"
    "    }\n"
    "    return normalize(sum);\n"
    "}\n"
    "float stored(int slot, vec3 d) {\n" /* constant sampler indices for GLSL 3.30 */
    "    if (slot == 0) return texture(shadow0, d).r;\n"
    "    if (slot == 1) return texture(shadow1, d).r;\n"
    "    if (slot == 2) return texture(shadow2, d).r;\n"
    "    return texture(shadow3, d).r;\n"
    "}\n"
    /* Shadow-map visibility. Filter 0: one tap; 1 (PCF): 8 taps on a small
     * cone around the light direction; 2 (PCSS): a blocker search over the
     * light's size, then a cone as wide as the penumbra the blockers'
     * distance gives. `slope`: tan of the normal-to-light angle (biases). */
    "float visibility(int slot, vec3 world, vec4 light, float size, float slope) {\n"
    "    vec3 d = world - light.xyz;\n"
    "    float dw = max(length(d), 1e-3), dist = dw / light.w;\n"
    "    vec3 axis = d / dw;\n"
    "    vec3 t = normalize(cross(abs(axis.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), axis));\n"
    "    vec3 b = cross(axis, t);\n"
    "    if (quality1.x == 0) return dist - 0.004 <= stored(slot, axis) ? 1.0 : 0.0;\n"
    "    float lit = 0.0;\n"
    "    if (quality1.x == 1) {\n"
    "        for (int k = 0; k < 8; k++) {\n"
    "            float a = float(k) * 0.785398 + 0.39;\n"
    "            vec3 dir = axis + (t * cos(a) + b * sin(a)) * shadow_softness * (0.5 + 0.5 * float(k & 1));\n"
    "            lit += dist - 0.004 <= stored(slot, dir) ? 1.0 : 0.0;\n"
    "        }\n"
    "        return lit / 8.0;\n"
    "    }\n"
    "    float rot = ign(gl_FragCoord.xy) * 6.2831853;\n"
    "    float search = clamp(2.0 * size / dw, 0.01, 0.15);\n"
    "    float blockers = 0.0, sum = 0.0, search_bias = 0.004 + dist * search * slope * 0.5;\n"
    "    for (int k = 0; k < 8; k++) {\n"
    "        float r = sqrt((float(k) + 0.5) / 8.0), a = float(k) * 2.39996323 + rot;\n"
    "        float s = stored(slot, axis + (t * cos(a) + b * sin(a)) * r * search);\n"
    "        if (s < dist - search_bias) { blockers += 1.0; sum += s; }\n"
    "    }\n"
    "    if (blockers == 0.0) return 1.0;\n"
    "    float blocker = sum / blockers;\n"
    "    float width = clamp(2.0 * size * (dist - blocker) / (max(blocker, 1e-3) * dist * light.w), 0.25 * shadow_softness, 0.15);\n"
    "    float bias = 0.004 + dist * width * slope * 0.5;\n"
    "    for (int k = 0; k < 16; k++) {\n"
    "        float r = sqrt((float(k) + 0.5) / 16.0), a = float(k) * 2.39996323 + rot;\n"
    "        lit += dist - bias <= stored(slot, axis + (t * cos(a) + b * sin(a)) * r * width) ? 1.0 : 0.0;\n"
    "    }\n"
    "    return lit / 16.0;\n"
    "}\n"
    "vec2 full() { return vec2(1024.0, 512.0) * float(scale); }\n"
    "ivec2 texel(vec2 u) {\n"
    "    vec2 size = vec2(rect.zw * scale);\n"
    "    ivec2 p = clamp(ivec2(floor(u * size)), ivec2(0), ivec2(size) - 1);\n"
    "    return ivec2((rect.x * scale + p.x) % (1024 * scale), (rect.y * scale + p.y) % (512 * scale));\n"
    "}\n"
    "float depth_at(vec2 u) { return texelFetch(depth_tex, texel(u), 0).r; }\n"
    /* Screen-space AO (lighting quality, no ray tracing): hemisphere points
     * projected through the GTE camera; behind the depth there = occluded. */
    "float screen_ao(vec3 world, vec3 wn) {\n"
    "    float radius = quality0.z, strength = quality0.w;\n"
    "    mat3 to_view = transpose(normal_to_world);\n"
    "    vec3 t = normalize(cross(abs(wn.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), wn)), b = cross(wn, t);\n"
    "    float rot = ign(gl_FragCoord.xy) * 6.2831853, occlusion = 0.0;\n"
    "    for (int k = 0; k < 12; k++) {\n"
    "        float f = (float(k) + 0.5) / 12.0, a = float(k) * 2.39996323 + rot;\n"
    "        float r = sqrt(f);\n"
    "        vec3 dir = (t * cos(a) + b * sin(a)) * r * 0.9 + wn * (sqrt(1.0 - f * 0.81) + 0.1);\n"
    "        vec3 v = to_view * (world + dir * radius * mix(0.2, 1.0, f)) + camera_translation;\n"
    "        if (v.z < 1.0) continue;\n"
    "        vec2 s = v.xy * projection.xy / v.z + screen_offset;\n"
    "        if (any(lessThan(s, vec2(0.0))) || any(greaterThanEqual(s, vec2(rect.zw)))) continue;\n"
    "        ivec2 q = ivec2((s + vec2(rect.xy)) * float(scale));\n"
    "        q = ivec2(q.x % (1024 * scale), q.y % (512 * scale));\n"
    "        float sz = texelFetch(depth_tex, q, 0).r * 65536.0;\n"
    "        if (sz < 0.5) continue;\n"
    "        float behind = v.z - sz;\n"
    "        if (behind > 1.0) occlusion += 1.0 - smoothstep(radius, radius * 2.0, behind);\n"
    "    }\n"
    "    return 1.0 - strength * occlusion / 12.0;\n"
    "}\n"
    PC_LIGHT_SHAPE_GLSL /* a spot's edge, a rect's face and barn doors (light_shape.h) */
    /* Unshadowed haze (haze Low; scaled.frag the same): the room's medium at
     * 8 jittered steps of the part of the ray it can be in, every light's
     * in-scattering and the sun's (through its map where it has one). */
    "vec3 low_haze(vec3 c, vec3 world) {\n"
    "    float g = medium_g();\n"
    "    vec3 eye = inverse_rotation * -camera_translation;\n"
    "    vec3 v = world - eye; float length_ = length(v); vec3 dir = v / max(length_, 1e-3);\n"
    "    vec2 span = medium_span(eye, dir, length_);\n"
    "    if (!(span.y > span.x)) return c;\n"
    "    float step_ = (span.y - span.x) / 8.0, jitter = ign(gl_FragCoord.xy + 17.0);\n"
    "    vec3 sum = vec3(0.0); float transmittance = 1.0;\n"
    "    for (int k = 0; k < 8; k++) {\n"
    "        vec3 x = eye + dir * (span.x + (float(k) + jitter) * step_), here = vec3(0.0);\n"
    "        vec3 scatter, emit;\n"
    "        float extinction = medium_at(x, scatter, emit);\n"
    "        if (extinction <= 0.0) continue;\n"
    "        for (int i = 0; i < light_count; i++) {\n"
    "            vec3 to = light_pos[i].xyz - x; float dl = length(to); vec3 l = to / max(dl, 1e-3);\n"
    "            float f = clamp(dl / light_pos[i].w, 0.0, 1.0);\n"
    "            float a = (1.0 - f * f) * (1.0 - f * f);\n"
    "            a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], x, l);\n"
    "            here += light_col[i].rgb * a * medium_phase(dot(dir, l), g);\n"
    "        }\n"
    "        if (env_sun_on())\n"
    "            here += env_sun_col.rgb * medium_phase(dot(dir, env_sun_dir.xyz), g)\n"
    "                  * (env_mode.x == 0 ? medium_sun_visibility(env_sun_map_point(x, fract(jitter + float(k) * 0.618034))) : 1.0);\n"
    "        float a = medium_step(extinction, step_, transmittance);\n"
    "        sum += transmittance * (scatter * here + emit) * ((1.0 - a) / extinction);\n"
    "        transmittance *= a;\n"
    "    }\n"
    "    return c * transmittance + medium_limit_light(sum);\n"
    "}\n"
    /* The medium's transmittance from the eye to `world` (16 steps, offset
     * per pixel by interleaved gradient noise: a fine dither, never the
     * slices fixed steps leave), for the traced haze, whose light ptgi.c marched. */
    "float medium_transmittance(vec3 world) {\n"
    "    vec3 eye = inverse_rotation * -camera_translation;\n"
    "    vec3 v = world - eye; float length_ = length(v); vec3 dir = v / max(length_, 1e-3);\n"
    "    vec2 span = medium_span(eye, dir, length_);\n"
    "    if (!(span.y > span.x)) return 1.0;\n"
    "    float step_ = (span.y - span.x) / 16.0, depth = 0.0, jitter = ign(gl_FragCoord.xy + 5.0);\n"
    "    for (int k = 0; k < 16; k++) {\n"
    "        vec3 scatter, emit;\n"
    "        depth += medium_at(eye + dir * (span.x + (float(k) + jitter) * step_), scatter, emit) * step_;\n"
    "    }\n"
    "    return medium_limit_transmittance(exp(-depth));\n"
    "}\n"
    "vec3 world_at(ivec2 q, float z) {\n"
    "    vec2 s = (vec2(q) + 0.5) / float(scale) - vec2(rect.xy) - screen_offset;\n"
    "    return inverse_rotation * (vec3(s.x * z / projection.x, s.y * z / projection.y, z) - camera_translation);\n"
    "}\n"
    /* A void pixel's haze (medium.glsl medium_void_pixel; scaled.frag the same): the room's medium marched
     * unshadowed (haze Low's march, whichever haze is on) over the share `reach` of its ray
     * (medium_void_reach) that crosses the room's interior. */
    "vec3 void_haze(vec3 c, ivec2 q, float d, float reach) {\n"
    "    if (reach <= 0.0 || (quality0.x <= 0.0 && ((gi_on & 128) == 0 || fog_density <= 0.0))) return c;\n"
    "    return low_haze(c, mix(inverse_rotation * -camera_translation, world_at(q, d * 65536.0), reach));\n"
    "}\n"
    /* The fog volumes' own renderer (atmosphere.h pc_fogvol_gpu; fog_volume.glsl), OpenGL's lower
     * quality: marched per pixel in this pass (fogvol_.y steps), the sample offset blue noise fixed per
     * pixel (a still picture stays still), each light's shadow an analytic estimate (the volume's mean
     * density over its depth toward the light) instead of Vulkan's light grid. */
    "uniform vec4 fogvol_[179];\n"
    "uniform ivec4 fogvol_on;\n" /* x on, y steps, z the debug view (1 their light alone, 2 their transmittance) */
    "#define FOGVOL(i) fogvol_[i]\n" PC_GLSL_FOG_VOLUME
    /* The port's particles (particles.h): the key image (a game pixel per texel) and their colours.
     * PC_NO_PARTICLES (fewer than 21 texture units: they use 19 and 20): none, nothing sampled. */
    "uniform ivec4 particles_on;\n" /* x on, y z the display's size in game pixels */
    "#ifndef PC_NO_PARTICLES\n"
    "uniform usampler2D particle_keys;\n"
    "uniform sampler2D particle_colours;\n"
    PC_GLSL_PARTICLES
    "#else\n"
    "#define particles_at(keys, colours, display, uv, d) vec3(0.0)\n"
    "#endif\n"
    "void fv_shadows(int v, vec3 x, vec3 r, out vec4 ta, out vec4 tb) {\n"
    "    int f = fv_base(v);\n"
    "    float sigma = MEDIUM(fv_medium(v)).w * 0.6 * FOGVOL(f + 3).y * (1.0 - 0.5 * FOGVOL(f + 2).w), t[8];\n"
    "    int lights = int(FOGVOL(f).w + 0.5);\n"
    "    for (int i = 0; i < 6; i++) {\n"
    "        t[i] = 1.0;\n"
    "        if (i >= lights) continue;\n"
    "        vec3 l; float dist;\n"
    "        fv_light(v, i, x, l, dist);\n"
    "        t[i] = exp(-sigma * max(min(fv_hit(v, x, l, dist).y, dist), 0.0));\n"
    "    }\n"
    "    t[6] = FOGVOL(1).w > 0.5 ? exp(-sigma * max(fv_hit(v, x, FOGVOL(2).xyz, 1e6).y, 0.0)) : 1.0;\n"
    "    t[7] = exp(-sigma * max(fv_hit(v, x, vec3(0.0, -1.0, 0.0), 1e6).y, 0.0));\n"
    "    ta = vec4(t[0], t[1], t[2], t[3]); tb = vec4(t[4], t[5], t[6], t[7]);\n"
    "}\n"
    /* The volumes between the eye and depth d (SZ / 65536) at target texel q: rgb light, a transmittance. */
    "vec4 fogvol_at(ivec2 q, float d) {\n"
    "    bool far = d >= 1.0 || d * 65536.0 < 0.5;\n"
    "    vec3 eye = inverse_rotation * -camera_translation, ray = world_at(q, 1.0) - eye;\n"
    "    float stretch = length(ray), fog_t, t_max = far ? 1e6 : d * 65536.0 * stretch;\n"
    "    vec3 dir = ray / max(stretch, 1e-9);\n"
    /* the volumes confined to the room stop where a void pixel's ray leaves its interior (medium.glsl) */
    "    float t_confined = medium_confined_reach(eye, dir, t_max, d, texelFetch(shade_tex, q, 0));\n"
    "    return fv_march(eye, dir, t_max, t_confined, ign(gl_FragCoord.xy + 31.0), fogvol_on.y, fog_t);\n"
    "}\n"
    "vec3 fogvol_apply(vec3 c, ivec2 q, float d) { vec4 f = fogvol_at(q, d); return c * f.a + f.rgb; }\n"
    "vec3 base(vec2 u) {\n"
    "    vec2 size = vec2(rect.zw * scale);\n"
    "    if (filtered == 2) {\n" /* average every target texel under this window pixel */
    "        vec2 per = size / view;\n"
    "        ivec2 n = clamp(ivec2(ceil(per - 0.001)), ivec2(1), ivec2(8));\n"
    "        vec2 origin = u * size - per * 0.5;\n"
    "        vec3 sum = vec3(0.0);\n"
    "        for (int j = 0; j < n.y; j++)\n"
    "            for (int i = 0; i < n.x; i++) {\n"
    "                ivec2 p = clamp(ivec2(origin + (vec2(i, j) + 0.5) * per / vec2(n)), ivec2(0), ivec2(size) - 1);\n"
    "                sum += texelFetch(target, ivec2((rect.x * scale + p.x) % (1024 * scale), (rect.y * scale + p.y) % (512 * scale)), 0).rgb;\n"
    "            }\n"
    "        return sum / float(n.x * n.y);\n"
    "    }\n"
    "    if (filtered == 1) {\n" /* bilinear inside the display area */
    "        vec2 p = clamp(u * size, vec2(0.5), size - 0.5) + vec2(rect.xy * scale);\n"
    "        return texture(target, p / full()).rgb;\n"
    "    }\n"
    "    return texelFetch(target, texel(u), 0).rgb;\n"
    "}\n"
    "vec3 smooth_at(vec2 u) {\n" /* bilinear tap for blurs */
    "    vec2 size = vec2(rect.zw * scale);\n"
    "    vec2 p = clamp(u * size, vec2(0.5), size - 0.5) + vec2(rect.xy * scale);\n"
    "    return texture(target, p / full()).rgb;\n"
    "}\n"
    "const vec2 disk[16] = vec2[16](vec2(-0.613, 0.617), vec2(0.170, -0.040), vec2(-0.299, 0.791),\n"
    "    vec2(0.645, 0.493), vec2(-0.651, 0.717), vec2(0.421, 0.027), vec2(-0.817, -0.271),\n"
    "    vec2(-0.705, -0.668), vec2(0.977, -0.108), vec2(0.063, -0.548), vec2(0.250, 0.430),\n"
    "    vec2(0.319, -0.884), vec2(-0.430, -0.072), vec2(-0.102, 0.255), vec2(0.556, -0.548), vec2(-0.920, 0.250));\n"
    /* The GI result at this pixel: its 2 x 2 GI pixels weighted bilinearly
     * and by how well their surface matches this one (joint bilateral). */
    "vec4 gi_at(vec2 display, vec3 world, vec3 wn, out vec4 extra_out) {\n"
    "    vec2 sp = display / vec2(rect.zw) * vec2(gi_size) - 0.5;\n"
    "    ivec2 b = ivec2(floor(sp)); vec2 f = sp - vec2(b);\n"
    "    vec4 sum = vec4(0.0), extra = vec4(0.0); float weight = 0.0;\n"
    /* Haze is smooth in space, not tied to one surface: plain bilinear over
     * the room's surfaces (w 1). Characters and effects (w 2) drop their
     * history every frame and, traced on a quarter of the screen at a time,
     * mostly hold no haze sample: taken from them they showed dark, cut out
     * of the haze in their own shapes. */
    "    vec3 haze = vec3(0.0), any_haze = vec3(0.0); float haze_weight = 0.0, any_weight = 0.0;\n"
    "    for (int y = -1; y <= 2; y++)\n"
    "        for (int x = -1; x <= 2; x++) {\n"
    "            ivec2 h = clamp(b + ivec2(x, y), ivec2(0), gi_size - 1);\n"
    "            vec3 e = texelFetch(gi_extra, h, 0).gba;\n"
    "            float kind = texelFetch(gi_position, h, 0).w;\n"
    "            if (e.r < 0.0 || kind < 0.5) continue;\n"
    "            float w = max(1.0 - abs(float(x) - f.x), 0.05) * max(1.0 - abs(float(y) - f.y), 0.05);\n"
    "            if (kind < 1.5) { haze += e * w; haze_weight += w; }\n"
    "            any_haze += e * w; any_weight += w;\n"
    "        }\n"
    "    haze = haze_weight > 0.0 ? haze / haze_weight : any_weight > 0.0 ? any_haze / any_weight : vec3(0.0);\n"
    /* The 2 x 2 around the pixel, then the ring around it if none match:
     * a silhouette or a polygon thinner than a GI pixel. */
    "    for (int pass = 0; pass < 2 && weight < 1e-5; pass++)\n"
    "        for (int y = -1; y <= 2; y++)\n"
    "            for (int x = -1; x <= 2; x++) {\n"
    "                bool inner = x >= 0 && x <= 1 && y >= 0 && y <= 1;\n"
    "                if (inner != (pass == 0)) continue;\n"
    "                ivec2 h = clamp(b + ivec2(x, y), ivec2(0), gi_size - 1);\n"
    "                vec4 gp = texelFetch(gi_position, h, 0);\n"
    "                if (gp.w < 0.5) continue;\n"
    "                vec3 gn = texelFetch(gi_normal, h, 0).xyz;\n"
    "                float w = inner ? (x == 1 ? f.x : 1.0 - f.x) * (y == 1 ? f.y : 1.0 - f.y) + 0.01 : 0.01;\n"
    "                w *= exp(-abs(dot(gp.xyz - world, wn)) / 4.0) * pow(max(dot(gn, wn), 0.0), 8.0);\n"
    "                sum += texelFetch(gi_tex, h, 0) * w; extra += texelFetch(gi_extra, h, 0) * w; weight += w;\n"
    "            }\n"
    /* Nothing similar nearby: neutral rather than a neighbour's shadow. */
    "    extra_out = vec4(weight > 1e-5 ? extra.r / weight : 1.0, haze);\n"
    "    return weight > 1e-5 ? sum / weight : vec4(0.0, 0.0, 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "    float d = depth_at(uv);\n"
    "    bool void_px = medium_void_pixel(d, texelFetch(shade_tex, texel(uv), 0));\n" /* the void: the room's air only up to its reach (medium.glsl) */
    "    float reach = void_px && d < 1.0 && d * 65536.0 >= 0.5\n"
    "        ? medium_void_reach(inverse_rotation * -camera_translation, world_at(texel(uv), d * 65536.0)) : 0.0;\n"
    "    if (show_depth != 0) {\n" /* far black, UI magenta, 3D log-grey near-bright */
    "        float sz = d * 65536.0;\n"
    "        color = d >= 1.0 ? vec4(0.0, 0.0, 0.0, 1.0)\n"
    "              : sz < 0.5 ? vec4(0.6, 0.0, 0.6, 1.0)\n"
    "              : vec4(vec3(1.0 - clamp((log2(sz) - 6.0) / 8.0, 0.0, 1.0)), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    if (fogvol_on.z != 0) {\n" /* fog_volume_debug (scaled.frag the same) */
    "        bool ui_ = d * 65536.0 < 0.5;\n"
    "        vec4 f = ui_ || fogvol_on.x == 0 ? vec4(0.0, 0.0, 0.0, 1.0) : fogvol_at(texel(uv), d);\n"
    "        color = vec4(fogvol_on.z == 2 ? vec3(clamp(f.a, 0.0, 1.0)) : min(f.rgb, vec3(1.0)), ui_ ? 9999.0 : 0.0);\n"
    "        return;\n"
    "    }\n"
    "    if (quality1.z != 0) {\n" /* haze_debug 1: the haze's light alone; 2: where the room's air is (scaled.frag the same) */
    "        bool ui_ = d * 65536.0 < 0.5; vec3 hz = vec3(0.0);\n"
    "        if (quality1.z == 2) {\n"
    "            float inside = !ui_ && d < 1.0 ? medium_interior(world_at(texel(uv), d * 65536.0)) : 0.0;\n"
    "            color = vec4(!ui_ && void_px ? 1.0 : 0.0, inside, !ui_ && void_px ? reach : 0.0, ui_ ? 9999.0 : 0.0);\n"
    "            return;\n"
    "        }\n"
    "        if (!ui_ && void_px) hz = void_haze(vec3(0.0), texel(uv), min(d, 1.0), reach);\n"
    "        else if (!ui_ && (gi_on & 128) != 0 && fog_density > 0.0) {\n" /* traced (ptgi.c): its GI pixel */
    "            vec2 display = (vec2(texel(uv)) + 0.5) / float(scale) - vec2(rect.xy);\n"
    "            hz = max(texelFetch(gi_extra, clamp(ivec2(display / vec2(rect.zw) * vec2(gi_size)), ivec2(0), gi_size - 1), 0).gba, vec3(0.0));\n"
    "        } else if (!ui_ && quality0.x > 0.0) hz = low_haze(vec3(0.0), world_at(texel(uv), min(d, 0.999) * 65536.0));\n"
    "        if (!ui_ && fogvol_on.x != 0) hz += fogvol_apply(vec3(0.0), texel(uv), d);\n"
    "        color = vec4(min(hz, vec3(1.0)), ui_ ? 9999.0 : 0.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 c = base(uv);\n"
    "    bool haze_done = false, fogvol_done = false, lit_done = false, particles_done = false;\n"
    /* Emissive texels (post bit 16; emissive.h): the shade target's alpha
     * 0.25 marks them, its rgb is their emission / 2. Kept above the lit
     * result (never darkened), above 1 for bloom when the grade pass runs
     * (post bit 32). */
    "    bool em_px = false; vec3 em = vec3(0.0);\n"
    "    if ((post & 16) != 0 && view_mode != 1) {\n"
    "        vec4 es = texelFetch(shade_tex, texel(uv), 0);\n"
    "        if (abs(es.a - 0.25) < 0.05 && d > 0.0) { em_px = true; em = es.rgb * 2.0; }\n"
    "    }\n"
    /* The menu's model view: its own studio light (menu_studio.glsl), not the room's. */
    "    if (menu_studio_on != 0 && view_mode == 0) {\n"
    "        ivec2 q = texel(uv);\n"
    "        vec4 n = texelFetch(normal_tex, q, 0);\n"
    "        if (n.a > 0.5 && d > 0.0) {\n"
    "            float z = texelFetch(depth_tex, q, 0).r * 65536.0;\n"
    "            vec2 s = (vec2(q) + 0.5) / float(scale) - vec2(rect.xy) - screen_offset;\n"
    "            vec3 p = vec3(s.x * z / projection.x, s.y * z / projection.y, z);\n"
    "            c = menu_studio(texelFetch(albedo_tex, q, 0).rgb, smooth_normal(q, normalize(n.xyz * 2.0 - 1.0), z), p);\n"
    "            if (em_px) c = max(c, em);\n"
    "            else if ((post & 8) == 0) c = mix(c, 1.0 - 0.2 * exp(-(c - 0.8) / 0.2), step(0.8, c));\n"
    "            if (any(isnan(c)) || any(isinf(c))) c = base(uv);\n"
    "            lit_done = haze_done = fogvol_done = particles_done = true;\n"
    "        }\n"
    "    }\n"
    "    if (!lit_done && (light_count > 0 || baked != 1.0 || gi_on != 0 || view_mode != 0 || quality1.y != 0 || env_mode.z != 0) && view_mode != 1) {\n"
    "        ivec2 q = texel(uv);\n"
    "        vec4 n = texelFetch(normal_tex, q, 0);\n"
    "        if (n.a > 0.5 && d > 0.0) {\n" /* lit 3D surface (not under UI): reconstruct its view-space position */
    "            float z = texelFetch(depth_tex, q, 0).r * 65536.0;\n"
    "            vec2 s = (vec2(q) + 0.5) / float(scale) - vec2(rect.xy) - screen_offset;\n"
    "            vec3 p = vec3(s.x * z / projection.x, s.y * z / projection.y, z);\n"
    "            vec3 normal = smooth_normal(q, normalize(n.xyz * 2.0 - 1.0), z);\n"
    "            vec3 albedo = texelFetch(albedo_tex, q, 0).rgb;\n"
    "            vec3 sum = vec3(0.0);\n"
    /* World position for shadows, nudged off the surface along its normal. */
    "            vec3 world = inverse_rotation * (p - camera_translation);\n"
    "            vec3 world_normal = normalize(normal_to_world * normal);\n"
    "            vec4 extra = vec4(1.0, 0.0, 0.0, 0.0);\n"
    "            vec4 gi = (gi_on & 128) != 0 ? gi_at((vec2(q) + 0.5) / float(scale) - vec2(rect.xy), world, world_normal, extra) : vec4(0.0, 0.0, 0.0, 1.0);\n"
    /* gi_on 64: indirect light from the probe field (in place of the path-traced gi.rgb). */
    "            if ((gi_on & 64) != 0) gi.rgb += probe_irradiance_at(world, world_normal, normalize(world - inverse_rotation * -camera_translation));\n"
    "            float baked_vis = (gi_on & 16) != 0 ? extra.r : 1.0;\n"
    "            float ao = quality1.y != 0 ? screen_ao(world, world_normal) : 1.0;\n"
    "            for (int i = 0; i < light_count; i++) {\n"
    "                vec3 centre = light_pos[i].xyz;\n"
    "                if (light_col[i].w > 1.5) {\n" /* rect: nearest point on the emitter */
    "                    vec3 rr = light_right[i].xyz, ru = light_up[i].xyz, rel = world - centre;\n"
    "                    centre += rr * clamp(dot(rel, rr) / dot(rr, rr), -1.0, 1.0)\n"
    "                            + ru * clamp(dot(rel, ru) / dot(ru, ru), -1.0, 1.0);\n"
    "                }\n"
    "                vec3 to = centre - world;\n"
    "                float d = length(to);\n"
    "                vec3 l = to / max(d, 1e-3);\n"
    "                float x = clamp(d / light_pos[i].w, 0.0, 1.0);\n"
    "                float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(world_normal, l), 0.0);\n"
    "                a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], world, l);\n"
    "                if (light_shadow[i] == -2)\n" /* ray-traced (ptgi.c) */
    "                    a *= clamp(gi.a, 0.0, 1.0);\n"
    "                else if (light_shadow[i] >= 0 && a > 0.0) {\n"
    "                    float cosine = clamp(dot(world_normal, l), 0.05, 1.0);\n"
    "                    a *= visibility(light_shadow[i], world + world_normal * max(2.0, light_world[i].w * 0.01), light_world[i],\n"
    "                        light_col[i].w < 1.5 && light_right[i].w > 0.0 ? light_right[i].w : quality2.x, min(sqrt(1.0 - cosine * cosine) / cosine, 4.0));\n"
    "                }\n"
    "                sum += light_col[i].rgb * a;\n"
    "            }\n"
    /* The sun (traced visibility, else unshadowed with the screen-space AO) and the sky's ambient. */
    "            if (env_sun_on())\n"
    "                sum += env_sun_unshadowed(world_normal) * (env_mode.x == -2 ? clamp(gi.a, 0.0, 1.0)\n"
    "                    : env_mode.x == 0 ? env_sun_map_visibility(world, world_normal, ign(gl_FragCoord.xy) * 6.2831853) : ao);\n"
    "            if (env_mode.y != 0)\n"
    "                sum += env_ambient(world_normal) * ao;\n"
    "            if ((gi_on & 2) != 0) { color = vec4(min(gi.rgb * gi_strength, vec3(1.0)), 1.0); return; }\n"
    "            if ((gi_on & 8) != 0) { color = vec4(vec3(gi.a), 1.0); return; }\n"
    "            if ((gi_on & 32) != 0) { color = vec4(vec3(baked_vis * ao), 1.0); return; }\n"
    "            if ((gi_on & 1) != 0) sum += gi.rgb * gi_strength * ao;\n"
    "            else if ((gi_on & 64) != 0) sum += gi.rgb * gi_strength * ao;\n" /* probes alone (no traced result) */
    /* View modes: the texture as white (the lighting alone), or one input. */
    "            vec4 shade = texelFetch(shade_tex, q, 0);\n"
    "            vec3 vertex_light = shade.a > 0.75 ? shade.rgb * (255.0 / 128.0) : shade.rgb;\n"
    "            if (view_mode == 3) { color = vec4(shade.a > 0.75 ? albedo : shade.rgb, 1.0); return; }\n"
    "            if (view_mode == 4) { color = vec4(min(shade.a > 0.75 ? vertex_light * 0.5 : shade.rgb, vec3(1.0)), 1.0); return; }\n"
    "            if (view_mode == 5) { color = vec4(world_normal * 0.5 + 0.5, 1.0); return; }\n"
    "            if (view_mode == 2) { c = vertex_light * 0.5; albedo = vec3(0.5); }\n"
    "            c = c * baked * baked_vis * ao + albedo * sum;\n"
    "            if (em_px) c = max(c, em);\n"
    "            lit_done = true;\n"
    "            if (particles_on.x != 0 && view_mode == 0) {\n" /* the motes in front of it, before the fog and haze */
    "                c += particles_at(particle_keys, particle_colours, particles_on.yz, uv, d);\n"
    "                particles_done = true;\n"
    "            }\n"
    "            if (fogvol_on.x != 0) { c = fogvol_apply(c, q, d); fogvol_done = true; }\n" /* the fog volumes, then the haze */
    "            if (void_px) { c = void_haze(c, q, d, reach); haze_done = true; }\n" /* the void: clean darkness, as on the PS1 */
    "            else if ((gi_on & 128) != 0 && fog_density > 0.0) {\n" /* the traced haze (ptgi.c) between the eye and the surface */
    "                float transmittance_ = medium_transmittance(world);\n"
    "                c = c * transmittance_ + max(extra.gba, vec3(0.0));\n"
    "                haze_done = true;\n"
    "            } else if (quality0.x > 0.0) {\n" /* haze Low */
    "                c = low_haze(c, world);\n"
    "                haze_done = true;\n"
    "            }\n"
    /* Soft shoulder above 0.8 instead of clipping at white (lit pixels only). */
    "            if ((post & 8) == 0 && !em_px) c = mix(c, 1.0 - 0.2 * exp(-(c - 0.8) / 0.2), step(0.8, c));\n"
    "            if (any(isnan(c)) || any(isinf(c))) c = base(uv);\n" /* never a black hole */
    "        }\n"
    "    }\n"
    "    bool ui = d * 65536.0 < 0.5;\n"
    "    if (em_px && !lit_done) c = max(c, em);\n" /* unlit: the raster's colour has it up to 1 */
    "    bool over = (post & 8) != 0 || (em_px && (post & 32) != 0);\n"
    "    if (ui && backdrop_mode != 0)\n" /* the menu's model view: the world behind, under the ground */
    "        c = mix(backdrop_mode == 2 ? texture(backdrop_tex, vec2(uv.x, 1.0 - uv.y)).rgb : backdrop_color.rgb,\n"
    "            backdrop_color.rgb, backdrop_color.a);\n"
    "    if (particles_on.x != 0 && !particles_done && view_mode == 0)\n" /* unlit pixels, and where nothing is drawn */
    "        c += particles_at(particle_keys, particle_colours, particles_on.yz, uv, d);\n"
    "    if (fogvol_on.x != 0 && !fogvol_done && !ui && view_mode != 1) c = fogvol_apply(c, texel(uv), d);\n" /* unlit, and where nothing is drawn */
    "    if (void_px && !haze_done && !ui && d < 1.0 && view_mode != 1) c = void_haze(c, texel(uv), d, reach);\n"
    "    else if (quality0.x > 0.0 && !haze_done && !ui && !void_px && d < 1.0 && view_mode != 1)\n" /* unlit 3D pixels */
    "        c = low_haze(c, world_at(texel(uv), d * 65536.0));\n"
    "    vec3 out_c = over && !ui ? max(c, vec3(0.0)) : min(c, vec3(1.0));\n"
    "    if ((post & 7) == 0 || ui) { color = vec4(out_c, ui ? 9999.0 : 0.0); return; }\n"
    "    float sz = d >= 1.0 ? 65535.0 : d * 65536.0;\n"
    "    if ((post & 4) != 0) {\n"
    "        float f = clamp((sz - fog_range.x) / max(fog_range.y - fog_range.x, 1.0), 0.0, 1.0);\n"
    "        c = mix(c, fog.rgb, f * fog.a);\n"
    "    }\n"
    /* Blur radius for the blur pass (alpha), signed: negative in front of the
     * focal plane. A thin lens: the blur circle grows with |1 - focus / z|. */
    "    float coc = 0.0;\n"
    "    if ((post & 1) != 0) {\n"
    "        float focus = focus_sz;\n"
    "        if (focus <= 0.0) {\n" /* auto: mean log depth of 3D samples near the centre */
    "            float sum = 0.0, n = 0.0;\n"
    "            for (int i = 0; i < 9; i++) {\n"
    "                float fd = depth_at(vec2(0.5, 0.55) + vec2(i % 3 - 1, i / 3 - 1) * 0.04);\n"
    "                if (fd * 65536.0 >= 0.5 && fd < 1.0) { sum += log2(fd * 65536.0); n += 1.0; }\n"
    "            }\n"
    "            focus = n > 0.0 ? exp2(sum / n) : 1024.0;\n"
    "        }\n"
    "        coc = dof_radius * clamp(aperture * (1.0 - focus / sz), -1.0, 1.0);\n"
    "        if (coc < 0.0) coc *= near_blur;\n"
    "    }\n"
    "    if ((post & 2) != 0) {\n"
    "        float t = tilt.z * smoothstep(tilt.y, tilt.y + 0.25, abs(uv.y - tilt.x));\n"
    "        coc = abs(coc) >= t ? coc : t;\n"
    "    }\n"
    "    color = vec4(over ? max(c, vec3(0.0)) : min(c, vec3(1.0)), coc);\n"
    "}\n";
static GLuint scaled_program;
static GLint u_scaled_rect, u_scaled_scale, u_scaled_target, u_scaled_filtered, u_scaled_view;
static GLint u_gi_tex, u_gi_on, u_gi_strength, u_gi_position, u_gi_normal, u_gi_size, u_gi_extra, u_fog_density,
    u_room_lo, u_room_hi;
extern void pc_gi_room_bounds(float lo[3], float hi[3]);
extern float pc_fog_density, pc_fog_anisotropy, pc_light_size;
extern int pc_fog_steps;
extern void pc_scene_bounds(float lo[3], float hi[3]);
extern GLuint pc_gi_extra(int editor);
extern int pc_baked_shadows_active;
extern void pc_gi_surface(GLuint* position, GLuint* normal, int* w, int* h);
extern int pc_gi_enabled, pc_rt_shadows;
extern GLuint pc_gi_update(int x, int y, int w, int h);
extern int pc_probe_gl_bind(GLuint program, int first) __attribute__((weak));
extern void pc_probe_gl_update(void) __attribute__((weak));
extern int pc_gi_debug;
extern float pc_gi_strength;
static GLint u_normal_to_world, u_inverse_rotation, u_camera_translation, u_light_world, u_light_shadow, u_shadow[4],
    u_shadow_softness;
float pc_shadow_softness = 0.02f;
float pc_normal_smoothing = 4.0f;
static GLint u_normal_smoothing;
extern int pc_scene_camera_get(float (*)[3], float*, float*, float*, float*);
extern int pc_shadows_update(int, const float (*)[4], const int*, int*);
extern void pc_shadows_bind(int);
extern int pc_shadows_sun_update(const float dir[3], float matrix[16], float light[4]);
extern void pc_shadows_sun_bind(int unit);
static GLint u_shade_tex, u_view_mode;
int pc_gl_view_mode; /* editor view mode (see the present shader's view_mode) */
static GLint u_normal_tex, u_albedo_tex, u_light_count, u_light_pos, u_light_col, u_light_dir,
    u_baked, u_projection, u_screen_offset;
extern void pc_hw_gbuffer(GLuint*, GLuint*);
extern GLuint pc_hw_shade_texture(void);
#define GL_LIGHTS 64 /* the present shader's light arrays */
extern void pc_lights_shadow_pick(int count, int* wants, int slots);
extern int pc_lights_view(float (*)[4], float (*)[4], float (*)[4], float (*)[4], int*, float (*)[4],
    float (*)[4], int);
static GLint u_medium, u_medium_tex, u_fogvol, u_fogvol_on, u_particle_keys, u_particle_colours, u_particles_on;
/* The medium's texture (atmosphere.h pc_medium_texture: the detail noise,
 * the interactive fog's fields) for the present's Low haze and ptgi.c's
 * traced haze: uploaded whole once, then its field slices when they change. */
GLuint pc_gl_medium_texture(void)
{
    static GLuint texture;
    static unsigned uploaded;
    unsigned serial;
    const unsigned char* texels = pc_medium_texture(&serial);
    const int s = PC_MEDIUM_TEX_SIZE;
    if (!texture) {
        gl_GenTextures(1, &texture);
        gl_BindTexture(GL_TEXTURE_3D, texture);
        gl_TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_TexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, s, s, PC_MEDIUM_TEX_DEPTH, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        uploaded = serial;
    } else if (serial != uploaded) {
        gl_BindTexture(GL_TEXTURE_3D, texture);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_TexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, PC_MEDIUM_TEX_NOISE, s, s, PC_MEDIUM_TEX_LAYERS, GL_RGBA,
            GL_UNSIGNED_BYTE, texels + (size_t)s * s * 4 * PC_MEDIUM_TEX_NOISE);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        uploaded = serial;
    }
    return texture;
}
/* The particles' key image (R32UI, a game pixel per texel) and colours (RGBA32F, 256 x 64)
 * for the present (particles.h pc_particles_cpu_frame), on units 19 and 20 (18: the menu backdrop). Always bound,
 * so the usampler never shares a unit with a float sampler. */
static int particles_units = 1; /* the driver has the units 19 and 20 they use (pc_gl_create_window) */
static void particles_bind(int on, const uint32_t* keys, int w, int h, const float* colours, int count)
{
    static GLuint key_tex, colour_tex;
    static int key_w, key_h;
    if (!key_tex) {
        gl_GenTextures(1, &key_tex);
        gl_GenTextures(1, &colour_tex);
        static const uint32_t none = PC_PARTICLE_NONE;
        gl_BindTexture(GL_TEXTURE_2D, key_tex);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 1, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &none);
        key_w = key_h = 1;
        gl_BindTexture(GL_TEXTURE_2D, colour_tex);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, PC_PARTICLE_COLOUR_W, PC_PARTICLES_MAX / PC_PARTICLE_COLOUR_W, 0,
            GL_RGBA, GL_FLOAT, NULL);
    }
    gl_ActiveTexture(GL_TEXTURE0 + 19);
    gl_BindTexture(GL_TEXTURE_2D, key_tex);
    if (on) {
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (key_w != w || key_h != h) {
            gl_TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, keys);
            key_w = w, key_h = h;
        } else {
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, keys);
        }
    }
    gl_ActiveTexture(GL_TEXTURE0 + 20);
    gl_BindTexture(GL_TEXTURE_2D, colour_tex);
    if (on && count > 0) {
        int rows = (count + PC_PARTICLE_COLOUR_W - 1) / PC_PARTICLE_COLOUR_W, full = count / PC_PARTICLE_COLOUR_W;
        if (full)
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PC_PARTICLE_COLOUR_W, full, GL_RGBA, GL_FLOAT, colours);
        if (rows > full) /* the last row's motes (the rest of it is never fetched) */
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, full, count - full * PC_PARTICLE_COLOUR_W, 1, GL_RGBA, GL_FLOAT,
                colours + (size_t)full * PC_PARTICLE_COLOUR_W * 4);
    }
    gl_ActiveTexture(GL_TEXTURE0);
}
static GLint u_light_right, u_light_up, u_quality0, u_quality1, u_quality2, u_env, u_env_mode, u_sun_map,
    u_sun_matrix, u_sun_light, u_sun_texel;
extern float pc_lights_baked;
extern int pc_scene_camera_inverse(float inverse[3][3]);
extern int pc_scene_projection(float*, float*, float*);
/* Depth of field / tilt-shift blur over the lit image (rgb, a = signed blur
 * radius in pixels, 9999 for UI). A gather that respects each tap's own blur:
 * a tap contributes where its blur circle reaches this pixel, and taps behind
 * this pixel are limited to its own radius, so sharp subjects don't bleed
 * into a blurred background while blurred foreground spreads over them. */
static const char* blur_fragment_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D lit;\n"
    "uniform vec2 view;\n"
    "uniform float max_radius;\n"
    "uniform int keep_mask;\n" /* a grade pass follows: alpha 9999 stays on 2D/UI */
    "const float golden = 2.39996323;\n"
    "void main() {\n"
    "    vec2 st = vec2(uv.x, 1.0 - uv.y);\n" /* framebuffer rows count from the bottom */
    "    vec4 c0 = texture(lit, st);\n"
    "    if (c0.a > 9000.0) { color = vec4(c0.rgb, keep_mask != 0 ? 9999.0 : 1.0); return; }\n"
    "    float r0 = abs(c0.a), radius = r0;\n"
    /* Blurred foreground nearby may cover this pixel: look for it. */
    "    for (int i = 0; i < 12; i++) {\n"
    "        float a = float(i) * 0.5236;\n"
    "        vec4 t = texture(lit, st + vec2(cos(a), sin(a)) * max_radius * (i % 2 == 0 ? 0.5 : 1.0) / view);\n"
    "        if (t.a < 0.0 && t.a < c0.a) radius = max(radius, -t.a);\n"
    "    }\n"
    "    if (radius < 0.5) { color = vec4(c0.rgb, 1.0); return; }\n"
    "    vec3 sum = c0.rgb;\n"
    "    float weight = 1.0;\n"
    "    for (int i = 0; i < 48; i++) {\n" /* golden-angle spiral over the disc */
    "        float r = sqrt((float(i) + 0.5) / 48.0) * radius, a = float(i) * golden;\n"
    "        vec2 o = vec2(cos(a), sin(a)) * r;\n"
    "        vec4 t = texture(lit, st + o / view);\n"
    "        if (t.a > 9000.0) continue;\n" /* never smear UI into the scene */
    "        float rt = abs(t.a);\n"
    "        if (t.a > c0.a) rt = min(rt, r0);\n"
    "        float w = clamp(rt - r + 1.0, 0.0, 1.0);\n"
    "        sum += t.rgb * w;\n"
    "        weight += w;\n"
    "    }\n"
    "    color = vec4(sum / weight, 1.0);\n"
    "}\n";
/* FXAA (3.11-style): find the edge through each pixel from luma contrast,
 * search along it for its ends, and resample across it in proportion. */
static const char* fxaa_fragment_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform vec2 texel;\n"
    "float luma(vec2 p) { return dot(texture(image, p).rgb, vec3(0.299, 0.587, 0.114)); }\n"
    "void main() {\n"
    "    vec2 st = vec2(uv.x, 1.0 - uv.y);\n" /* framebuffer rows count from the bottom */
    "    vec4 centre = texture(image, st);\n" /* alpha: the blur radius for depth of field, after */
    "    vec3 middle = centre.rgb;\n"
    "    float m = dot(middle, vec3(0.299, 0.587, 0.114));\n"
    "    float n = luma(st + vec2(0.0, texel.y)), s = luma(st - vec2(0.0, texel.y));\n"
    "    float e = luma(st + vec2(texel.x, 0.0)), w = luma(st - vec2(texel.x, 0.0));\n"
    "    float high = max(m, max(max(n, s), max(e, w))), low = min(m, min(min(n, s), min(e, w)));\n"
    "    float range = high - low;\n"
    "    if (range < max(0.0312, high * 0.125)) { color = vec4(middle, centre.a); return; }\n"
    "    float nw = luma(st + vec2(-texel.x, texel.y)), ne = luma(st + texel);\n"
    "    float sw = luma(st - texel), se = luma(st + vec2(texel.x, -texel.y));\n"
    "    float horizontal = abs(-2.0 * w + nw + sw) + 2.0 * abs(-2.0 * m + n + s) + abs(-2.0 * e + ne + se);\n"
    "    float vertical = abs(-2.0 * n + nw + ne) + 2.0 * abs(-2.0 * m + w + e) + abs(-2.0 * s + sw + se);\n"
    "    bool across_y = horizontal >= vertical;\n" /* the edge runs along x: step across in y */
    "    float l1 = across_y ? s : w, l2 = across_y ? n : e;\n"
    "    float g1 = l1 - m, g2 = l2 - m;\n"
    "    bool first = abs(g1) >= abs(g2);\n"
    "    float gradient = 0.25 * max(abs(g1), abs(g2));\n"
    "    float step_size = across_y ? texel.y : texel.x;\n"
    "    float local = 0.5 * ((first ? l1 : l2) + m);\n"
    "    if (first) step_size = -step_size;\n"
    "    vec2 edge = st + (across_y ? vec2(0.0, 0.5 * step_size) : vec2(0.5 * step_size, 0.0));\n"
    "    vec2 along = across_y ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);\n"
    "    vec2 p1 = edge - along, p2 = edge + along;\n"
    "    float e1 = luma(p1) - local, e2 = luma(p2) - local;\n"
    "    bool done1 = abs(e1) >= gradient, done2 = abs(e2) >= gradient;\n"
    "    const float quality[12] = float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);\n"
    "    for (int i = 0; i < 12 && !(done1 && done2); i++) {\n"
    "        if (!done1) { p1 -= along * quality[i]; e1 = luma(p1) - local; done1 = abs(e1) >= gradient; }\n"
    "        if (!done2) { p2 += along * quality[i]; e2 = luma(p2) - local; done2 = abs(e2) >= gradient; }\n"
    "    }\n"
    "    float d1 = across_y ? st.x - p1.x : st.y - p1.y, d2 = across_y ? p2.x - st.x : p2.y - st.y;\n"
    "    bool nearer1 = d1 < d2;\n"
    "    float offset = 0.5 - min(d1, d2) / (d1 + d2);\n"
    "    bool correct = ((nearer1 ? e1 : e2) < 0.0) != (m < local);\n"
    "    offset = correct ? offset : 0.0;\n"
    "    float average = (2.0 * (n + s + e + w) + nw + ne + sw + se) / 12.0;\n"
    "    float sub = clamp(abs(average - m) / range, 0.0, 1.0);\n"
    "    sub = (-2.0 * sub + 3.0) * sub * sub;\n"
    "    offset = max(offset, sub * sub * 0.75);\n"
    "    vec2 p = st + (across_y ? vec2(0.0, offset * step_size) : vec2(offset * step_size, 0.0));\n"
    "    color = vec4(texture(image, p).rgb, centre.a);\n"
    "}\n";
/* SMAA 1x (antialias.h; the reference shader pc/third_party/smaa/SMAA.hlsl,
 * MIT, through its GLSL 3 macros), three passes as on Vulkan
 * (pc/shaders/vulkan/smaa_*.frag). SMAA works in image coordinates: st is
 * uv with rows counted from the bottom, as GL stores the images. */
#define SMAA_GL_HEADER \
    "#version 330 core\n" \
    "in vec2 uv;\n" \
    "uniform vec4 metrics;\n" /* 1 / width, 1 / height, width, height */ \
    "#define SMAA_RT_METRICS metrics\n" \
    "#define SMAA_GLSL_3 1\n" \
    "#define SMAA_PRESET_HIGH 1\n" PC_GLSL_SMAA
static const char* smaa_edges_source = SMAA_GL_HEADER
    "out vec2 edges;\n"
    "uniform sampler2D color_tex;\n"
    "uniform sampler2D aa_depth_tex;\n"
    "uniform sampler2D aa_normal_tex;\n"
    "uniform ivec4 rect;\n" /* the display area (VRAM pixels) */
    "uniform ivec4 info;\n" /* scale, geometric edges only */
    "#define AA_RECT rect\n"
    "#define AA_SCALE info.x\n"
    "vec2 aa_picture(vec2 st) { return vec2(st.x, 1.0 - st.y); }\n" PC_GLSL_AA_GEOMETRY
    "void main() {\n"
    "    vec2 st = vec2(uv.x, 1.0 - uv.y);\n"
    "    vec4 offset[3];\n"
    "    SMAAEdgeDetectionVS(st, offset);\n"
    "    vec2 e = SMAAColorEdgeDetectionPS(st, offset, color_tex);\n"
    "    if (info.y != 0) e *= aa_geometry_edges(st, metrics.xy);\n"
    "    if (dot(e, vec2(1.0)) == 0.0) discard;\n"
    "    edges = e;\n"
    "}\n";
static const char* smaa_weights_source = SMAA_GL_HEADER
    "out vec4 weights;\n"
    "uniform sampler2D edges_tex;\n"
    "uniform sampler2D area_tex;\n"
    "uniform sampler2D search_tex;\n"
    "void main() {\n"
    "    vec2 st = vec2(uv.x, 1.0 - uv.y), pixcoord;\n"
    "    vec4 offset[3];\n"
    "    SMAABlendingWeightCalculationVS(st, pixcoord, offset);\n"
    "    weights = SMAABlendingWeightCalculationPS(st, pixcoord, offset, edges_tex, area_tex, search_tex, vec4(0.0));\n"
    "}\n";
static const char* smaa_blend_source = SMAA_GL_HEADER
    "out vec4 color;\n"
    "uniform sampler2D color_tex;\n"
    "uniform sampler2D blend_tex;\n"
    "void main() {\n"
    "    vec2 st = vec2(uv.x, 1.0 - uv.y);\n"
    "    vec4 offset;\n"
    "    SMAANeighborhoodBlendingVS(st, offset);\n"
    "    color = SMAANeighborhoodBlendingPS(st, offset, color_tex, blend_tex);\n"
    "}\n";
/* Supersampling's reduction to the view (pc/shaders/vulkan/downsample.glsl). */
static const char* downsample_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D src;\n"
    "uniform vec2 src_size;\n"
    "uniform vec2 dst_size;\n"
    "uniform int filter_;\n" PC_GLSL_DOWNSAMPLE
    "void main() { color = vec4(downsample(src, vec2(uv.x, 1.0 - uv.y), src_size, dst_size, filter_), 1.0); }\n";
static GLuint smaa_edges_program, smaa_weights_program, smaa_blend_program, downsample_program;
static GLuint smaa_area_texture, smaa_search_texture;
static GLint u_smaa_metrics[3], u_smaa_rect, u_smaa_info, u_down_src_size, u_down_dst_size, u_down_filter;
static GLuint blur_program, fxaa_program;
static GLint u_blur_lit, u_blur_view, u_blur_radius, u_aperture, u_fxaa_image, u_fxaa_texel;
/* The post chain's lit-sized images (half floats; alpha = the depth of
 * field's signed blur radius, 9999 on UI), used in turn: pass n writes
 * chain[n & 1]. SMAA's edges and weights beside them. */
typedef struct {
    GLuint fbo, texture;
    int w, h;
} view_target;
static view_target chain[2], smaa_edges, smaa_weights;
static GLint u_scaled_show_depth, u_scaled_depth, u_post, u_focus, u_dof_radius, u_tilt, u_fog, u_fog_range;
static GLint u_backdrop_tex, u_backdrop_color, u_backdrop_mode, u_menu_studio_on, u_menu_studio_rig;
/* Post effects: this present's resolved profile (post.h, post_profile.cpp). */
static pc_post_frame post_frame;
static GLint u_near_blur, u_blur_keep;
extern GLuint pc_hw_depth_texture(void);
int pc_gl_show_depth;
/* Presentation settings: filter 0 nearest, 1 bilinear, 2 supersample (default:
 * box-filters the scaled target down to the window, i.e. SSAA); aspect 0
 * native, 1 stretch, 2 integer scale. */
int pc_gl_filter = 2, pc_gl_aspect;
extern void pc_hw_text_overlay(int view_x, int view_y, int view_w, int view_h, int to_view);
/* Where the game image was last drawn (window pixels, top-left origin) and
 * the native display area it shows, for the lights editor's projection. */
int pc_gl_view[4], pc_gl_display[4];
extern int pc_gte_widescreen;
extern int pc_hw_perspective;
extern void pc_overlay_init(SDL_Window*, SDL_GLContext);
extern void pc_settings_env_override(const char* key) __attribute__((weak)); /* project_settings.cpp */
extern void pc_overlay_frame(int, int);
extern int pc_editor_separate(void) __attribute__((weak));
extern void pc_editor_render(int, int) __attribute__((weak));
extern int pc_hw_active, pc_hw_scale;
extern void pc_hw_init(const uint16_t* vram);
extern GLuint pc_hw_target(void);
extern void pc_hw_present_done(void);
extern void pc_hw_supersample(int view_h, int display_h);
extern void pc_hw_set_display(int x, int y, int w, int h);
extern void pc_hw_present_origin(int* x, int* y);
extern const uint16_t* pc_native_vram(void);

/* A window (and its context) that could not be used: no GL presentation. */
static SDL_Window* abandon(SDL_Window* win)
{
    if (context)
        SDL_GL_DestroyContext(context);
    context = NULL;
    SDL_DestroyWindow(win);
    return NULL;
}
/* Create the game window with an OpenGL 4.6 (or 3.3) core context; NULL
 * when unavailable. */
SDL_Window* pc_gl_create_window(const char* title, int width, int height)
{
    const char* renderer = getenv("ROOD_RENDERER");
    if (renderer && (!strcmp(renderer, "x11") || !strcmp(renderer, "software")))
        return NULL;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, 40);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 40);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_Window* win = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!win) {
        fprintf(stderr, "[gl] no OpenGL window: %s\n", SDL_GetError());
        return NULL;
    }
    /* 4.6 core (compute shaders for the path tracer), else 3.3 core. */
    static const int versions[2][2] = { { 4, 6 }, { 3, 3 } };
    for (int v = 0; v < 2 && !context; v++) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, versions[v][0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, versions[v][1]);
        context = SDL_GL_CreateContext(win);
    }
    if (!context || !SDL_GL_MakeCurrent(win, context)) {
        fprintf(stderr, "[gl] no OpenGL 3.3 core context: %s\n", SDL_GetError());
        return abandon(win);
    }
#define LOAD(ret, name, args)                                                                   \
    gl_##name = (ret(*) args)SDL_GL_GetProcAddress("gl" #name);                              \
    if (!gl_##name) {                                                                       \
        fprintf(stderr, "[gl] missing gl%s\n", #name);                                      \
        return abandon(win);                                                                \
    }
    GL_FUNCTIONS(LOAD)
#undef LOAD
#define LOAD_OPTIONAL(ret, name, args)                                                          \
    gl_##name = (ret(*) args)SDL_GL_GetProcAddress("gl" #name);                              \
    compute &= gl_##name != NULL;
    {
        int major = 0, compute = 1;
        const char* version = (const char*)gl_GetString(GL_VERSION);
        if (version)
            major = atoi(version);
        GL_COMPUTE_FUNCTIONS(LOAD_OPTIONAL)
        pc_gl_compute = compute && major >= 4 && !getenv("ROOD_NO_COMPUTE");
    }
#undef LOAD_OPTIONAL
    program = pc_gl_program(vertex_source, fragment_source);
    {
        /* The particles' textures are on units 19 and 20: with fewer units the present compiles
         * without them (PC_NO_PARTICLES) and draws none (particles_units). */
        GLint units = 0;
        gl_GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        particles_units = units >= 21;
        if (!particles_units)
            fprintf(stderr, "[gl] %d texture units: no particles (they need 21)\n", (int)units);
        const char* version = "#version 330 core\n";
        size_t skip = strlen(version);
        char* source = NULL;
        if (!particles_units && !strncmp(scaled_fragment_source, version, skip)) {
            size_t n = strlen(scaled_fragment_source);
            source = malloc(n + 64);
            if (source)
                snprintf(source, n + 64, "%s#define PC_NO_PARTICLES\n%s", version, scaled_fragment_source + skip);
        }
        scaled_program = pc_gl_program(vertex_source, source ? source : scaled_fragment_source);
        free(source);
    }
    blur_program = pc_gl_program(vertex_source, blur_fragment_source);
    fxaa_program = pc_gl_program(vertex_source, fxaa_fragment_source);
    if (!program || !scaled_program || !blur_program || !fxaa_program)
        return abandon(win);
    u_scaled_rect = gl_GetUniformLocation(scaled_program, "rect");
    u_scaled_scale = gl_GetUniformLocation(scaled_program, "scale");
    u_scaled_target = gl_GetUniformLocation(scaled_program, "target");
    u_scaled_filtered = gl_GetUniformLocation(scaled_program, "filtered");
    u_scaled_view = gl_GetUniformLocation(scaled_program, "view");
    u_scaled_show_depth = gl_GetUniformLocation(scaled_program, "show_depth");
    u_scaled_depth = gl_GetUniformLocation(scaled_program, "depth_tex");
    u_post = gl_GetUniformLocation(scaled_program, "post");
    u_normal_tex = gl_GetUniformLocation(scaled_program, "normal_tex");
    u_albedo_tex = gl_GetUniformLocation(scaled_program, "albedo_tex");
    u_shade_tex = gl_GetUniformLocation(scaled_program, "shade_tex");
    u_view_mode = gl_GetUniformLocation(scaled_program, "view_mode");
    u_light_count = gl_GetUniformLocation(scaled_program, "light_count");
    u_light_pos = gl_GetUniformLocation(scaled_program, "light_pos");
    u_light_col = gl_GetUniformLocation(scaled_program, "light_col");
    u_light_dir = gl_GetUniformLocation(scaled_program, "light_dir");
    u_baked = gl_GetUniformLocation(scaled_program, "baked");
    u_gi_tex = gl_GetUniformLocation(scaled_program, "gi_tex");
    u_gi_on = gl_GetUniformLocation(scaled_program, "gi_on");
    u_gi_strength = gl_GetUniformLocation(scaled_program, "gi_strength");
    u_gi_position = gl_GetUniformLocation(scaled_program, "gi_position");
    u_gi_extra = gl_GetUniformLocation(scaled_program, "gi_extra");
    u_fog_density = gl_GetUniformLocation(scaled_program, "fog_density");
    u_room_lo = gl_GetUniformLocation(scaled_program, "room_lo");
    u_room_hi = gl_GetUniformLocation(scaled_program, "room_hi");
    u_gi_normal = gl_GetUniformLocation(scaled_program, "gi_normal");
    u_gi_size = gl_GetUniformLocation(scaled_program, "gi_size");
    u_projection = gl_GetUniformLocation(scaled_program, "projection");
    u_screen_offset = gl_GetUniformLocation(scaled_program, "screen_offset");
    u_inverse_rotation = gl_GetUniformLocation(scaled_program, "inverse_rotation");
    u_normal_to_world = gl_GetUniformLocation(scaled_program, "normal_to_world");
    u_camera_translation = gl_GetUniformLocation(scaled_program, "camera_translation");
    u_light_world = gl_GetUniformLocation(scaled_program, "light_world");
    u_light_right = gl_GetUniformLocation(scaled_program, "light_right");
    u_light_up = gl_GetUniformLocation(scaled_program, "light_up");
    u_light_shadow = gl_GetUniformLocation(scaled_program, "light_shadow");
    u_shadow_softness = gl_GetUniformLocation(scaled_program, "shadow_softness");
    u_quality0 = gl_GetUniformLocation(scaled_program, "quality0");
    u_medium = gl_GetUniformLocation(scaled_program, "medium_");
    u_medium_tex = gl_GetUniformLocation(scaled_program, "medium_tex");
    u_fogvol = gl_GetUniformLocation(scaled_program, "fogvol_");
    u_fogvol_on = gl_GetUniformLocation(scaled_program, "fogvol_on");
    u_particle_keys = gl_GetUniformLocation(scaled_program, "particle_keys");
    u_particle_colours = gl_GetUniformLocation(scaled_program, "particle_colours");
    u_particles_on = gl_GetUniformLocation(scaled_program, "particles_on");
    u_quality1 = gl_GetUniformLocation(scaled_program, "quality1");
    u_quality2 = gl_GetUniformLocation(scaled_program, "quality2");
    u_env = gl_GetUniformLocation(scaled_program, "env_");
    u_env_mode = gl_GetUniformLocation(scaled_program, "env_mode");
    u_sun_map = gl_GetUniformLocation(scaled_program, "sun_map");
    u_sun_matrix = gl_GetUniformLocation(scaled_program, "env_sun_matrix");
    u_sun_light = gl_GetUniformLocation(scaled_program, "env_sun_light");
    u_sun_texel = gl_GetUniformLocation(scaled_program, "env_sun_texel");
    u_normal_smoothing = gl_GetUniformLocation(scaled_program, "normal_smoothing");
    for (int i = 0; i < 4; i++) {
        char name[16];
        snprintf(name, sizeof name, "shadow%d", i);
        u_shadow[i] = gl_GetUniformLocation(scaled_program, name);
    }
    u_focus = gl_GetUniformLocation(scaled_program, "focus_sz");
    u_dof_radius = gl_GetUniformLocation(scaled_program, "dof_radius");
    u_aperture = gl_GetUniformLocation(scaled_program, "aperture");
    u_near_blur = gl_GetUniformLocation(scaled_program, "near_blur");
    u_blur_lit = gl_GetUniformLocation(blur_program, "lit");
    u_blur_view = gl_GetUniformLocation(blur_program, "view");
    u_blur_radius = gl_GetUniformLocation(blur_program, "max_radius");
    u_blur_keep = gl_GetUniformLocation(blur_program, "keep_mask");
    u_fxaa_image = gl_GetUniformLocation(fxaa_program, "image");
    u_fxaa_texel = gl_GetUniformLocation(fxaa_program, "texel");
    u_tilt = gl_GetUniformLocation(scaled_program, "tilt");
    u_fog = gl_GetUniformLocation(scaled_program, "fog");
    u_fog_range = gl_GetUniformLocation(scaled_program, "fog_range");
    u_backdrop_tex = gl_GetUniformLocation(scaled_program, "backdrop_tex");
    u_backdrop_color = gl_GetUniformLocation(scaled_program, "backdrop_color");
    u_backdrop_mode = gl_GetUniformLocation(scaled_program, "backdrop_mode");
    u_menu_studio_on = gl_GetUniformLocation(scaled_program, "menu_studio_on");
    u_menu_studio_rig = gl_GetUniformLocation(scaled_program, "menu_studio_rig");
    u_rect = gl_GetUniformLocation(program, "rect");
    u_vram = gl_GetUniformLocation(program, "vram");
    u_rgb24 = gl_GetUniformLocation(program, "rgb24");
    gl_GenTextures(1, &vram_texture);
    gl_BindTexture(GL_TEXTURE_2D, vram_texture);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    gl_GenVertexArrays(1, &vertex_array);
    SDL_GL_SetSwapInterval(0); /* the frame loop paces to 59.94 Hz itself */
    window = win;
    SDL_GetWindowSizeInPixels(win, &window_w, &window_h);
    pc_gl_active = 1;
    snprintf(renderer_name, sizeof renderer_name, "%s", (const char*)gl_GetString(GL_RENDERER));
    fprintf(stderr, "[gl] %s, OpenGL %s%s\n", gl_GetString(GL_RENDERER), gl_GetString(GL_VERSION),
        pc_gl_compute ? " (compute)" : "");
    pc_hw_init(pc_native_vram());
    pc_overlay_init(win, context);
    /* Perspective-correct textures are the default (ROOD_PERSPECTIVE=0
     * restores the PS1's affine mapping); 16:9 is opt-in (ROOD_WIDESCREEN=1).
     * The variables apply only when set: the settings (just loaded by
     * pc_overlay_init) keep their widescreen / perspective_textures. */
    const char* widescreen = getenv("ROOD_WIDESCREEN");
    if (widescreen && *widescreen) {
        pc_gte_widescreen = *widescreen == '1';
        if (pc_settings_env_override)
            pc_settings_env_override("widescreen"); /* not saved as the user's (project_settings.cpp) */
    }
    const char* perspective = getenv("ROOD_PERSPECTIVE");
    if (perspective && *perspective) {
        pc_hw_perspective = *perspective != '0';
        if (pc_settings_env_override)
            pc_settings_env_override("perspective_textures");
    }
    return win;
}

void pc_gl_resize(int width, int height)
{
    window_w = width;
    window_h = height;
}

static int ensure_target_format(view_target* v, int w, int h, GLenum format)
{
    if (v->w == w && v->h == h)
        return 1;
    if (!v->fbo) {
        gl_GenFramebuffers(1, &v->fbo);
        gl_GenTextures(1, &v->texture);
    }
    gl_BindTexture(GL_TEXTURE_2D, v->texture);
    gl_TexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format == GL_RG8 ? GL_RG : GL_RGBA,
        format == GL_RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_BindFramebuffer(GL_FRAMEBUFFER, v->fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, v->texture, 0);
    int ok = gl_CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    v->w = ok ? w : 0, v->h = ok ? h : 0;
    return ok;
}
static int ensure_view_target(view_target* v, int w, int h) { return ensure_target_format(v, w, h, GL_RGBA16F); }
/* Pass `stage` of the post chain draws into the window's view (the last of
 * `stages`) or the next lit-sized image; returns that image's texture (the
 * next pass's input). */
static view_target brightness_target; /* the chain's last image before the brightness curve, view-sized RGBA16F */
static int brightness_stage; /* this frame's chain ends with the brightness curve (below) */
static GLuint stage_begin(int stage, int stages, int lit_w, int lit_h, int view_x, int view_y, int view_w, int view_h)
{
    if (stage + 1 >= stages) {
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_Viewport(view_x, view_y, view_w, view_h);
        return 0;
    }
    if (brightness_stage && stage + 2 >= stages) { /* settings brightness: its 16-bit input, view-sized */
        gl_BindFramebuffer(GL_FRAMEBUFFER, brightness_target.fbo);
        gl_Viewport(0, 0, view_w, view_h);
        return brightness_target.texture;
    }
    view_target* v = &chain[stage & 1];
    gl_BindFramebuffer(GL_FRAMEBUFFER, v->fbo);
    gl_Viewport(0, 0, lit_w, lit_h);
    return v->texture;
}
/* SMAA's programs and lookup textures, made on first use (antialias.c). */
static GLuint lookup_texture(GLenum format, GLenum base, const unsigned char* data, int w, int h, GLint filter)
{
    GLuint texture;
    gl_GenTextures(1, &texture);
    gl_BindTexture(GL_TEXTURE_2D, texture);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_TexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, base, GL_UNSIGNED_BYTE, data);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}
static int smaa_ready(void)
{
    static int state; /* 0 not yet, 1 ready, -1 failed */
    if (state)
        return state > 0;
    state = -1;
    smaa_edges_program = pc_gl_program(vertex_source, smaa_edges_source);
    smaa_weights_program = pc_gl_program(vertex_source, smaa_weights_source);
    smaa_blend_program = pc_gl_program(vertex_source, smaa_blend_source);
    if (!smaa_edges_program || !smaa_weights_program || !smaa_blend_program) {
        fprintf(stderr, "[gl] SMAA unavailable (its shaders did not build)\n");
        return 0;
    }
    GLuint programs[3] = { smaa_edges_program, smaa_weights_program, smaa_blend_program };
    for (int i = 0; i < 3; i++)
        u_smaa_metrics[i] = gl_GetUniformLocation(programs[i], "metrics");
    u_smaa_rect = gl_GetUniformLocation(smaa_edges_program, "rect");
    u_smaa_info = gl_GetUniformLocation(smaa_edges_program, "info");
    static const char* const samplers[3][3] = { { "color_tex", "aa_depth_tex", "aa_normal_tex" },
        { "edges_tex", "area_tex", "search_tex" }, { "color_tex", "blend_tex", NULL } };
    for (int i = 0; i < 3; i++) {
        gl_UseProgram(programs[i]);
        for (int k = 0; k < 3; k++)
            if (samplers[i][k])
                gl_Uniform1i(gl_GetUniformLocation(programs[i], samplers[i][k]), k);
    }
    gl_UseProgram(0);
    int aw, ah, sw, sh;
    const unsigned char* area = pc_smaa_area_texture(&aw, &ah);
    const unsigned char* search = pc_smaa_search_texture(&sw, &sh);
    smaa_area_texture = lookup_texture(GL_RG8, GL_RG, area, aw, ah, GL_LINEAR);
    smaa_search_texture = lookup_texture(GL_R8, GL_RED, search, sw, sh, GL_NEAREST);
    state = 1;
    return 1;
}
static int downsample_ready(void)
{
    static int state;
    if (!state) {
        downsample_program = pc_gl_program(vertex_source, downsample_source);
        state = downsample_program ? 1 : -1;
        if (downsample_program) {
            u_down_src_size = gl_GetUniformLocation(downsample_program, "src_size");
            u_down_dst_size = gl_GetUniformLocation(downsample_program, "dst_size");
            u_down_filter = gl_GetUniformLocation(downsample_program, "filter_");
            gl_UseProgram(downsample_program);
            gl_Uniform1i(gl_GetUniformLocation(downsample_program, "src"), 0);
            gl_UseProgram(0);
        }
    }
    return state > 0;
}
#include "post_gl.inc" /* the post stack: grade pass, Gaussian bloom, meter */

/* ---- Brightness (settings brightness 0..100, System > Display) ----
 * 50 leaves the picture untouched: no pass runs (bit-identical). Otherwise
 * the post chain gets one more stage: what would have gone to the window
 * (the lit image after anti-aliasing, blur and supersampling's downsample,
 * or the VRAM picture) goes to a view-sized 16-bit float image instead, and
 * brightness.glsl's curve draws that into the window, so the curve runs
 * before the 8-bit rounding (no posterized darks). Its slope at black is
 * 2.5^((brightness - 50) / 50): 2.5 at 100, 0.4 at 0. It is the game's
 * picture only (3D, the game's own 2D and menus, movies): the overlay drawn
 * after it (modern menus, editor, developer panels) and the letterbox bars
 * keep their colours. vk_present.c does the same. */
int pc_present_brightness = 50;
int pc_brightness_get(void) { return pc_present_brightness; }
void pc_brightness_set(int value) { pc_present_brightness = value < 0 ? 0 : value > 100 ? 100 : value; }
float pc_present_brightness_factor(void) { return powf(2.5f, (pc_present_brightness - 50) / 50.0f); }
static const char* brightness_fragment_source =
    "#version 330 core\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform ivec2 origin;\n"
    "uniform float factor;\n" PC_GLSL_BRIGHTNESS
    "void main() { color = vec4(brightness(texelFetch(image, ivec2(gl_FragCoord.xy) - origin, 0).rgb, factor), 1.0); }\n";
static GLuint brightness_program;
static GLint u_brightness_origin, u_brightness_factor;
/* 1: this frame's post chain ends with the brightness stage (made on first use). */
static int brightness_on(int view_w, int view_h)
{
    static int state; /* 0 not yet, 1 ready, -1 failed */
    if (pc_present_brightness == 50 || state < 0 || view_w <= 0 || view_h <= 0)
        return 0;
    if (!state) {
        brightness_program = pc_gl_program(vertex_source, brightness_fragment_source);
        state = brightness_program ? 1 : -1;
        if (!brightness_program) {
            fprintf(stderr, "[gl] brightness unavailable (its shader did not build)\n");
            return 0;
        }
        u_brightness_origin = gl_GetUniformLocation(brightness_program, "origin");
        u_brightness_factor = gl_GetUniformLocation(brightness_program, "factor");
        gl_UseProgram(brightness_program);
        gl_Uniform1i(gl_GetUniformLocation(brightness_program, "image"), 0);
    }
    return ensure_view_target(&brightness_target, view_w, view_h);
}
/* The last stage: the 16-bit image through the curve into the window's view. */
/* ---- The modern menu's backdrop (Ashley tab) ----
 * While the menu's Ashley model shows, the world is frozen and not drawn: the
 * frame holds the model (the only 3D) over a flat 2D tile (ui/ashley_bridge.c
 * clear_behind; gpu_hw.c tags those frames, pc_hw_present_menu_model). So
 * the tab is see-through like the others, every present while the modern
 * menu is up and shows the world keeps its finished picture (after the post
 * chain, before game text and the brightness curve: what the window shows
 * there), and a present of the model view puts that picture under the
 * menu's ground colour and opacity in place of the tile's 2D pixels, in the
 * present shader, before anti-aliasing (the model's edges blend with the
 * world). The post stack runs neutral on those presents (post_profile.cpp),
 * so the model is never graded while the picture behind keeps the post it
 * was shown with. The shell then draws no ground of its own there
 * (ui_draw.cpp). */
extern int pc_ui_menu_active(void) __attribute__((weak));               /* ui/menu_bridge.c */
extern int pc_ui_menu_backdrop(float rgba[4]) __attribute__((weak));    /* ui/ui_theme.cpp: ground, opacity */
extern int pc_hw_present_menu_model(void) __attribute__((weak));        /* gpu_hw.c */
extern int pc_hw_present_menu_studio(void) __attribute__((weak));       /* gpu_hw.c: its studio light */
extern int pc_ui_menu_model_light(pc_menu_light* out) __attribute__((weak)); /* ui/menu_light.cpp: its rig */
static view_target backdrop;
static int backdrop_valid;
static unsigned backdrop_keeps; /* pictures kept so far (each world present while the menu is up) */
/* 0 while no picture of the world is kept for the model view, else the count of pictures kept so far
 * (ashley_bridge.c waits for one taken with the HUD slid out; RPC ui_menu backdrop_kept). */
int pc_gl_menu_backdrop_kept(void) { return backdrop_valid ? (int)(backdrop_keeps & 0x3FFFFFFF) + 1 : 0; }
/* Before the present shader's draw: 0 not the model view; 1 the ground alone (no picture kept); 2 the
 * picture kept (bound to unit 18). */
static int backdrop_bind(void)
{
    float rgba[4] = { 0, 0, 0, 1 };
    int mode = 0;
    if (pc_hw_present_menu_model && pc_hw_present_menu_model() && pc_ui_menu_backdrop && pc_ui_menu_backdrop(rgba))
        mode = backdrop_valid ? 2 : 1;
    if (mode == 1)
        rgba[3] = 1;
    gl_Uniform1i(u_backdrop_mode, mode);
    int studio = pc_hw_present_menu_studio && pc_hw_present_menu_studio();
    gl_Uniform1i(u_menu_studio_on, studio);
    if (studio) { /* the rig as data (theme.json "menu_model_light"; RPC graphics menu_light) */
        pc_menu_light light;
        float rig[PC_MENU_LIGHT_VEC4S][4];
        if (!pc_ui_menu_model_light || !pc_ui_menu_model_light(&light))
            pc_ui_menu_model_light_defaults(&light);
        pc_menu_light_pack(&light, rig);
        gl_Uniform4fv(u_menu_studio_rig, PC_MENU_LIGHT_VEC4S, &rig[0][0]);
    }
    gl_Uniform4f(u_backdrop_color, rgba[0], rgba[1], rgba[2], rgba[3]);
    gl_Uniform1i(u_backdrop_tex, 18);
    gl_ActiveTexture(GL_TEXTURE18);
    gl_BindTexture(GL_TEXTURE_2D, mode == 2 ? backdrop.texture : 0);
    gl_ActiveTexture(GL_TEXTURE0);
    return mode;
}
/* After the post chain: keep the finished picture of the world while the modern menu is up (read from
 * the chain's last image: the brightness curve's input, or the window's view). */
static void backdrop_keep(int view_x, int view_y, int view_w, int view_h)
{
    if (!pc_ui_menu_active || !pc_ui_menu_active()) {
        backdrop_valid = 0;
        if (backdrop.fbo) { /* the menu closed: give the picture's memory back */
            gl_DeleteFramebuffers(1, &backdrop.fbo);
            gl_DeleteTextures(1, &backdrop.texture);
            backdrop.fbo = backdrop.texture = 0;
            backdrop.w = backdrop.h = 0;
        }
        return;
    }
    if ((pc_hw_present_menu_model && pc_hw_present_menu_model()) || !ensure_view_target(&backdrop, view_w, view_h))
        return;
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, brightness_stage ? brightness_target.fbo : 0);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, backdrop.fbo);
    int x = brightness_stage ? 0 : view_x, y = brightness_stage ? 0 : view_y;
    gl_BlitFramebuffer(x, y, x + view_w, y + view_h, 0, 0, view_w, view_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    backdrop_valid = 1;
    ++backdrop_keeps;
}

static void brightness_draw(int view_x, int view_y)
{
    gl_UseProgram(brightness_program);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, brightness_target.texture);
    gl_Uniform2i(u_brightness_origin, view_x, view_y);
    gl_Uniform1f(u_brightness_factor, pc_present_brightness_factor());
    gl_DrawArrays(GL_TRIANGLES, 0, 3);
}

/* RPC present_dump: the next presented window image goes to this PPM
 * (what the player sees, read back before the swap; works where screen
 * capture does not, e.g. under Wayland). With a count, that many
 * consecutive images go to <path>.0000.ppm, <path>.0001.ppm, ... */
char pc_present_capture[400];
int pc_present_burst;
static int burst_index;
/* Presented images so far (RPC status "presents": tests wait for a redraw
 * with it instead of sleeping). */
unsigned pc_presents;
/* Images that reached the window: swapped (GL), vkQueuePresentKHR succeeded
 * (vk_present.c). Headless Vulkan has none. RPC status "window_presents". */
unsigned pc_window_presents;
/* The image appears under its name only when complete (written beside it,
 * then renamed), so a test can read it as soon as it exists. */
void pc_write_ppm_atomic(const char* name, const unsigned char* pixels, int w, int h, int stride, int flip)
{
    char part[440];
    snprintf(part, sizeof part, "%s.part", name);
    FILE* f = fopen(part, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    unsigned char* row = malloc((size_t)w * 3);
    for (int i = 0; i < h && row; i++) {
        const unsigned char* src = pixels + (size_t)(flip ? h - 1 - i : i) * w * stride;
        for (int x = 0; x < w; x++)
            memcpy(row + x * 3, src + (size_t)x * stride, 3);
        fwrite(row, 1, (size_t)w * 3, f);
    }
    free(row);
    fclose(f);
    pc_replace_file(part, name);
}
/* The eyedropper (editor_colour.cpp): the mean of the 3 x 3 pixels around
 * window pixel (x, y) of the next presented image (display values 0..1,
 * the overlay included, as present_dump sees it). Both presenters read it
 * at present time (capture_present). */
static int sample_state; /* 0 none, 1 asked for, 2 read */
static int sample_xy[2];
static float sample_rgb[3];
int pc_present_sample_request(float x, float y)
{
    sample_xy[0] = (int)x, sample_xy[1] = (int)y;
    sample_state = 1;
    return 1;
}
int pc_present_sample_result(float rgb[3])
{
    if (sample_state != 2)
        return 0;
    memcpy(rgb, sample_rgb, sizeof sample_rgb);
    sample_state = 0;
    return 1;
}
/* For the presenters: the pixel asked for (0: none), and its 3 x 3 read (`stride` bytes per pixel, RGB first). */
int pc_present_sample_pending(int xy[2])
{
    if (sample_state != 1)
        return 0;
    xy[0] = sample_xy[0], xy[1] = sample_xy[1];
    return 1;
}
void pc_present_sample_store(const unsigned char* pixels, int stride)
{
    float sum[3] = { 0, 0, 0 };
    for (int i = 0; i < 9; i++)
        for (int k = 0; k < 3; k++)
            sum[k] += pixels[i * stride + k];
    for (int k = 0; k < 3; k++)
        sample_rgb[k] = sum[k] / (9 * 255.0f);
    sample_state = 2;
}
static void write_capture(const char* name, const unsigned char* pixels, int w, int h)
{
    pc_write_ppm_atomic(name, pixels, w, h, 3, 1);
}
/* A burst reads back through a ring of pixel-pack buffers and writes each
 * image a few frames later, so capturing does not stall on the GPU and
 * leaves the frame timing (and so interpolation) as in normal play. */
#define CAPTURE_RING 4
static GLuint capture_buffers[CAPTURE_RING];
static struct {
    char name[420];
    int w, h, used;
} capture_slots[CAPTURE_RING];
static int capture_next;
static void drain_capture(int slot)
{
    if (!capture_slots[slot].used)
        return;
    int w = capture_slots[slot].w, h = capture_slots[slot].h;
    unsigned char* pixels = malloc((size_t)w * h * 3);
    if (pixels) {
        gl_BindBuffer(GL_PIXEL_PACK_BUFFER, capture_buffers[slot]);
        gl_GetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)w * h * 3, pixels);
        gl_BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        write_capture(capture_slots[slot].name, pixels, w, h);
        free(pixels);
    }
    capture_slots[slot].used = 0;
}
static void capture_present(void)
{
    ++pc_presents;
    int sample_at[2];
    if (pc_present_sample_pending(sample_at)) { /* the eyedropper's pixel (GL rows count from the bottom) */
        unsigned char px[27];
        int x = sample_at[0] - 1, y = window_h - 2 - sample_at[1];
        x = x < 0 ? 0 : x > window_w - 3 ? window_w - 3 : x;
        y = y < 0 ? 0 : y > window_h - 3 ? window_h - 3 : y;
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl_ReadPixels(x, y, 3, 3, GL_RGB, GL_UNSIGNED_BYTE, px);
        pc_present_sample_store(px, 3);
    }
    int slot = capture_next;
    if (!pc_present_capture[0]) {
        /* Finish a burst's images still in flight. */
        for (int i = 0; i < CAPTURE_RING; i++)
            drain_capture((slot + i) % CAPTURE_RING);
        return;
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl_PixelStorei(GL_PACK_ALIGNMENT, 1);
    if (pc_present_burst <= 0) {
        unsigned char* pixels = malloc((size_t)window_w * window_h * 3);
        if (pixels) {
            gl_ReadPixels(0, 0, window_w, window_h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
            write_capture(pc_present_capture, pixels, window_w, window_h);
            free(pixels);
        }
        pc_present_capture[0] = 0;
        return;
    }
    if (!capture_buffers[0])
        gl_GenBuffers(CAPTURE_RING, capture_buffers);
    drain_capture(slot); /* the oldest, CAPTURE_RING frames ago */
    gl_BindBuffer(GL_PIXEL_PACK_BUFFER, capture_buffers[slot]);
    gl_BufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)window_w * window_h * 3, NULL, GL_STREAM_READ);
    gl_ReadPixels(0, 0, window_w, window_h, GL_RGB, GL_UNSIGNED_BYTE, 0);
    gl_BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    snprintf(capture_slots[slot].name, sizeof capture_slots[slot].name, "%s.%04d.ppm", pc_present_capture,
        burst_index++);
    capture_slots[slot].w = window_w, capture_slots[slot].h = window_h, capture_slots[slot].used = 1;
    capture_next = (slot + 1) % CAPTURE_RING;
    if (--pc_present_burst <= 0) {
        burst_index = 0;
        pc_present_capture[0] = 0;
    }
}

/* Wait for the GPU to finish the frame before handing it to the display
 * server. Under XWayland without explicit sync (NVIDIA), the compositor
 * can otherwise take the buffer while it is still being drawn: polygons
 * not yet drawn show as black for a frame, more often the heavier the GPU
 * work. Off by default (it did not cure the black tiles, and costs
 * frame time); ROOD_FINISH=1 or the RPC graphics flag "finish" turns it on. */
int pc_gl_finish = -1;
static void finish_frame(void)
{
    if (pc_gl_finish < 0)
        pc_gl_finish = getenv("ROOD_FINISH") != NULL;
    if (pc_gl_finish)
        gl_Finish();
}

/* The game image's rectangle in the window (pc_gl_view, top-left origin):
 * 4:3 (16:9 widescreen) letterboxed, stretched, or integer-scaled. */
/* Window margins (left, top, right, bottom, in pixels) the game image keeps
 * clear: the editor docks its panels there. */
int pc_gl_inset[4];
static void update_view(int* out_w, int* out_h)
{
    int area_x = pc_gl_inset[0], area_y = pc_gl_inset[1];
    int area_w = window_w - pc_gl_inset[0] - pc_gl_inset[2], area_h = window_h - pc_gl_inset[1] - pc_gl_inset[3];
    if (area_w < 64 || area_h < 48)
        area_x = area_y = 0, area_w = window_w, area_h = window_h;
    int view_w = area_w, view_h = area_h;
    if (pc_gl_aspect == 0 || pc_gl_aspect == 2) {
        /* 4:3, or 16:9 when the GTE projects widescreen (Hor+); movie frames
         * (24-bit display) are 4:3 pictures: pillarboxed in widescreen. */
        int wide = pc_gte_widescreen && !pc_display_rgb24;
        int aw = wide ? 16 : 4, ah = wide ? 9 : 3;
        view_h = area_w * ah / aw;
        if (view_h > area_h) {
            view_h = area_h;
            view_w = area_h * aw / ah;
        }
        int base_w = wide ? 427 : 320;
        if (pc_gl_aspect == 2 && view_w >= base_w && view_h >= 240) {
            int n = view_w / base_w < view_h / 240 ? view_w / base_w : view_h / 240;
            view_w = base_w * n;
            view_h = 240 * n;
        }
    }
    pc_gl_view[0] = area_x + (area_w - view_w) / 2;
    pc_gl_view[1] = area_y + (area_h - view_h) / 2; /* top-left origin */
    pc_gl_view[2] = view_w;
    pc_gl_view[3] = view_h;
    *out_w = view_w, *out_h = view_h;
}

/* Draw the display area (x, y, w, h in VRAM) letterboxed at 4:3. */
/* The GL_RENDERER string (lighting quality: integrated GPU detection). */

const char* pc_gl_renderer(void) { return renderer_name; }

/* Frames in flight: at most this many presented frames may be queued on the
 * GPU before the next one waits for the oldest to finish. Swap interval 0
 * (the frame loop paces itself) and headless EGL pbuffers do not throttle,
 * so when the GPU cannot keep up (the path tracer on a slow or busy GPU),
 * the CPU queued frame after frame, every 16 ms even while paused: seconds
 * of latency, and after ~20 s queued the NVIDIA driver gave up on the work
 * and the image read back (present_dump) or shown was black. With the
 * limit, latency stays at a couple of frames and every frame is drawn.
 * ROOD_FRAMES_IN_FLIGHT (1..8) changes the depth; 0 is the old unlimited
 * queue (to reproduce the failure, tests/gi_lag_smoke.py). */
static GLsync frame_fences[8];
static int fence_next, frames_in_flight = -1;
static double fence_wait_ms; /* RPC perf: time the CPU waited for the GPU */
static unsigned fence_waits;
static void wait_fence(GLsync fence)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    /* Unbounded, in 1 s steps: a frame is never skipped, only waited for. */
    while (gl_ClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull) == GL_TIMEOUT_EXPIRED)
        ;
    clock_gettime(CLOCK_MONOTONIC, &b);
    fence_wait_ms += (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
    ++fence_waits;
}
/* After the swap: fence this frame, then wait for the one `frames_in_flight`
 * frames ago (so the next frame starts with fewer than that queued). */
static void limit_frames_in_flight(void)
{
    if (frames_in_flight < 0) {
        const char* env = getenv("ROOD_FRAMES_IN_FLIGHT");
        frames_in_flight = env ? atoi(env) : 2;
        frames_in_flight = frames_in_flight < 0 ? 2 : frames_in_flight > 8 ? 8 : frames_in_flight;
    }
    if (!frames_in_flight)
        return;
    GLsync* slot = &frame_fences[fence_next];
    if (*slot) {
        wait_fence(*slot);
        gl_DeleteSync(*slot);
    }
    *slot = gl_FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    fence_next = (fence_next + 1) % frames_in_flight;
}
/* RPC perf: presented frames (counted here, not by the timer queries, which
 * skip frames while earlier ones are in flight) and the CPU's waits. */
static unsigned presented_frames;
void pc_gl_present_stats(unsigned* presents, double* wait_ms, unsigned* waits, int reset)
{
    *presents = presented_frames, *wait_ms = fence_wait_ms, *waits = fence_waits;
    if (reset)
        presented_frames = 0, fence_wait_ms = 0, fence_waits = 0;
}
static void swap(void)
{
    SDL_GL_SwapWindow(window);
    ++presented_frames;
    ++pc_window_presents;
    limit_frames_in_flight();
}

/* ROOD_PERF=1: GPU time between swaps (the frame's drawing and presentation)
 * with timer queries, read a few frames later (RPC perf). */
static void gpu_timer(int begin)
{
    static int perf = -1, active, next;
    static GLuint queries[4];
    static int pending[4];
    if (perf < 0) {
        perf = getenv("ROOD_PERF") != NULL;
        if (perf)
            gl_GenQueries(4, queries);
    }
    if (!perf)
        return;
    if (!begin) {
        if (active)
            gl_EndQuery(GL_TIME_ELAPSED);
        active = 0;
        for (int i = 0; i < 4; i++) {
            GLint ready = 0;
            if (!pending[i])
                continue;
            gl_GetQueryObjectiv(queries[i], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready) {
                GLuint64 ns = 0;
                gl_GetQueryObjectui64v(queries[i], GL_QUERY_RESULT, &ns);
                pc_perf_gpu_add(ns * 1e-6);
                pending[i] = 0;
            }
        }
        return;
    }
    if (pending[next])
        return; /* still in flight: skip this frame */
    gl_BeginQuery(GL_TIME_ELAPSED, queries[next]);
    pending[next] = 1;
    active = 1;
    next = (next + 1) & 3;
}

void pc_gl_present(const uint16_t* vram, int x, int y, int w, int h)
{
    static int last[4];
    pc_lighting_sync(); /* lighting quality: first-run preset, Custom on changes */
    if (getenv("ROOD_GL_DEBUG") && (last[0] != x || last[1] != y || last[2] != w || last[3] != h)) {
        fprintf(stderr, "[gl] display %d,%d %dx%d\n", x, y, w, h);
        last[0] = x, last[1] = y, last[2] = w, last[3] = h;
    }
    /* Editor view (F2): our own render of the captured room replaces the game's. */
    if (pc_editor_separate && pc_editor_separate()) {
        int view_w, view_h;
        update_view(&view_w, &view_h); /* the window may have been resized meanwhile */
        if (pc_hw_active)
            pc_hw_target();
        pc_hw_present_done();
        pc_editor_render(window_w, window_h);
        pc_overlay_frame(window_w, window_h);
        capture_present();
        finish_frame();
        swap();
        return;
    }
    if (pc_hw_active && w > 0 && h > 0) { /* supersampling: the internal resolution the view needs */
        int view_w, view_h;
        update_view(&view_w, &view_h);
        pc_hw_supersample(view_h, h);
    }
    /* Finish the hardware target first: its flush sets its own viewport. */
    if (pc_hw_active)
        pc_hw_set_display(x, y, w, h);
    GLuint target = pc_hw_active ? pc_hw_target() : 0;
    pc_post_frame_get(&post_frame); /* the post profile for this present */
    /* The grade pass's programs, made now (not between the present shader's uniforms). */
    int grade_ready = post_frame.grade && pc_hw_active && post_ready();
    /* 24-bit display (movie frames from the MDEC): the scaled target only
     * holds 15-bit pixels, so present native VRAM. */
    if (pc_display_rgb24)
        target = 0;
    /* An interpolated frame may come from another area of the target (the
     * off-screen buffer the game shows next). */
    if (target)
        pc_hw_present_origin(&x, &y);
    gl_Viewport(0, 0, window_w, window_h);
    gl_ClearColor(0, 0, 0, 1);
    gl_Clear(GL_COLOR_BUFFER_BIT);
    if (w > 0 && h > 0 && !(&pc_display_masked && pc_display_masked)) { /* SetDispMask(0): black */
        int view_w, view_h;
        update_view(&view_w, &view_h);
        gl_Viewport(pc_gl_view[0], window_h - pc_gl_view[1] - view_h, view_w, view_h);
        pc_gl_display[0] = x, pc_gl_display[1] = y, pc_gl_display[2] = w, pc_gl_display[3] = h;
        /* The lit image: view-sized, or with supersampling the display area at the internal resolution. */
        int lit_w = view_w, lit_h = view_h, ssaa = 0;
        if (target && pc_aa_ssaa > 1.0f && w * pc_hw_scale > view_w && h * pc_hw_scale > view_h && downsample_ready())
            ssaa = 1, lit_w = w * pc_hw_scale, lit_h = h * pc_hw_scale;
        /* Settings brightness: one more stage (below); set up before this frame binds its programs and textures. */
        brightness_stage = brightness_on(view_w, view_h);
        gl_ActiveTexture(GL_TEXTURE0);
        gl_BindVertexArray(vertex_array);
        if (target) {
            /* Indirect light first (compute passes change GL state). */
            GLuint gi = pc_gl_show_depth ? 0 : pc_gi_update(x, y, w, h);
            /* The particles (particles.cpp): evaluated, lit and splatted on the CPU after the
             * path tracer built its scene (their shadow rays use it); composited below. */
            const uint32_t* particle_keys = NULL;
            const float* particle_colours = NULL;
            int particle_count = 0;
            int particles_on = !pc_gl_show_depth && particles_units
                && pc_particles_cpu_frame(w, h, &particle_keys, &particle_colours, &particle_count);
            if (!pc_gl_show_depth && pc_probe_gl_update)
                pc_probe_gl_update(); /* the probe field when the path tracer did not update it */
            gl_BindTexture(GL_TEXTURE_2D, target);
            /* Linear for bilinear presentation and blur taps; nearest paths use texelFetch. */
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_UseProgram(scaled_program);
            gl_Uniform1i(u_scaled_target, 0);
            gl_Uniform4i(u_scaled_rect, x, y, w, h);
            gl_Uniform1i(u_scaled_scale, pc_hw_scale);
            gl_Uniform1i(u_scaled_filtered, pc_gl_filter);
            gl_Uniform2f(u_scaled_view, (float)lit_w, (float)lit_h);
            gl_Uniform1i(u_scaled_show_depth, pc_gl_show_depth);
            const pc_post_frame* pf = &post_frame;
            /* 8: HDR out, when the grade pass (below) runs and tone maps (post.h hdr). */
            gl_Uniform1i(u_post, pf->flags | (pf->hdr && !pc_gl_show_depth && grade_ready ? 8 : 0)
                    | (pc_emissive_active() ? 16 : 0) | (!pc_gl_show_depth && grade_ready ? 32 : 0));
            gl_Uniform1f(u_focus, pf->focus_sz);
            gl_Uniform1f(u_dof_radius, pf->dof_radius * lit_h / 720.0f);
            gl_Uniform1f(u_aperture, pf->aperture);
            gl_Uniform1f(u_near_blur, pf->near_blur);
            gl_Uniform3f(u_tilt, pf->tilt[0], pf->tilt[1], pf->tilt[2] * lit_h / 720.0f);
            gl_Uniform4f(u_fog, pf->fog[0], pf->fog[1], pf->fog[2], pf->fog[3]);
            gl_Uniform2f(u_fog_range, pf->fog_range[0], pf->fog_range[1]);
            backdrop_bind(); /* the menu's model view: the world behind it (above) */
            {
                /* Lighting: world-space lights and the projection to invert. */
                /* Up to GL_LIGHTS lights (the most important past that: lights.cpp);
                 * with OpenGL's path-traced visibility, the 16 its trace takes
                 * (ptgi.c), so the visibility matches; otherwise every light lit
                 * unshadowed and shadow cubes for the most important casters. */
                static float lp[GL_LIGHTS][4], lc[GL_LIGHTS][4], ld[GL_LIGHTS][4], lw[GL_LIGHTS][4],
                    lr[GL_LIGHTS][4], lu[GL_LIGHTS][4];
                static int wants[GL_LIGHTS], slots[GL_LIGHTS];
                float h, ox, oy;
                int count = 0;
                if (pc_scene_projection(&h, &ox, &oy)) {
                    count = pc_lights_view(lp, lc, ld, lw, wants, lr, lu, gi && pc_rt_shadows ? 16 : GL_LIGHTS);
                    if (gi && pc_rt_shadows) {
                        for (int i = 0; i < count; i++)
                            slots[i] = wants[i] ? -2 : -1; /* traced visibility, not cube maps */
                    } else {
                        /* Shadow cubes render into their own framebuffer; restore ours. */
                        pc_lights_shadow_pick(count, wants, 4);
                        pc_shadows_update(count, lw, wants, slots);
                    }
                    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
                    gl_Viewport(pc_gl_view[0], window_h - pc_gl_view[1] - view_h, view_w, view_h);
                    gl_UseProgram(scaled_program);
                    gl_BindVertexArray(vertex_array);
                    float r[3][3], t[3], hh, a, b, inverse[9];
                    float r_inverse[3][3];
                    if (pc_scene_camera_get(r, t, &hh, &a, &b) && pc_scene_camera_inverse(r_inverse)) {
                        /* Column-major mat3 of R^-1. */
                        for (int col = 0; col < 3; col++)
                            for (int row = 0; row < 3; row++)
                                inverse[col * 3 + row] = r_inverse[row][col];
                        gl_UniformMatrix3fv(u_inverse_rotation, 1, GL_FALSE, inverse);
                        float transpose[9]; /* column-major R^T: column j = row j of R */
                        for (int col = 0; col < 3; col++)
                            for (int row = 0; row < 3; row++)
                                transpose[col * 3 + row] = r[col][row];
                        gl_UniformMatrix3fv(u_normal_to_world, 1, GL_FALSE, transpose);
                        gl_Uniform3f(u_camera_translation, t[0], t[1], t[2]);
                    }
                    gl_Uniform3f(u_projection, h * (pc_gte_widescreen ? 0.75f : 1.0f), h, 0);
                    gl_Uniform2f(u_screen_offset, ox, oy);
                }
                gl_Uniform1i(u_light_count, count);
                gl_Uniform4fv(u_light_pos, GL_LIGHTS, &lp[0][0]);
                gl_Uniform4fv(u_light_col, GL_LIGHTS, &lc[0][0]);
                gl_Uniform4fv(u_light_dir, GL_LIGHTS, &ld[0][0]);
                gl_Uniform4fv(u_light_world, GL_LIGHTS, &lw[0][0]);
                gl_Uniform4fv(u_light_right, GL_LIGHTS, &lr[0][0]);
                gl_Uniform4fv(u_light_up, GL_LIGHTS, &lu[0][0]);
                for (int i = count; i < GL_LIGHTS; i++)
                    slots[i] = -1;
                gl_Uniform1iv(u_light_shadow, GL_LIGHTS, slots);
                gl_Uniform1f(u_shadow_softness, pc_shadow_softness);
                gl_Uniform1f(u_normal_smoothing, pc_normal_smoothing);
                pc_shadows_bind(4);
                for (int i = 0; i < 4; i++)
                    gl_Uniform1i(u_shadow[i], 4 + i);
                gl_Uniform1f(u_baked, pc_lighting_baked_scale(pc_lights_baked, count));
                /* Lighting quality: haze Low (or High without a traced haze this
                 * frame), screen-space AO, the shadow map filter. */
                /* The room's medium (atmosphere.h): the traced haze's (ptgi.c
                 * marched its light; the present applies its transmittance) or
                 * haze Low's. */
                static pc_medium_gpu medium;
                int traced_haze = gi && pc_fog_steps > 0 && pc_haze_quality == PC_HAZE_HIGH, medium_on;
                {
                    float lo[3], hi[3];
                    if (gi)
                        pc_gi_room_bounds(lo, hi);
                    else
                        pc_scene_bounds(lo, hi);
                    medium_on = !pc_gl_show_depth
                        && pc_medium_gpu_get(&medium,
                            traced_haze ? pc_lighting_traced_haze_density() : pc_lighting_low_haze_density(0), lo, hi)
                        /* the uniform haze alone with nothing to light it would only darken: as before, none */
                        && (traced_haze || count > 0 || pc_env_gpu_get(&(pc_env_gpu){ 0 }) || pc_medium_has_fog());
                    gl_Uniform4fv(u_medium, PC_MEDIUM_VEC4S, &medium.v[0][0]);
                    /* the fog volumes' own renderer: their lighting (their shapes are in the medium above) */
                    static pc_medium_gpu fv_medium;
                    static pc_fogvol_gpu fv;
                    int fv_count = pc_gl_show_depth ? 0 : pc_fogvol_gpu_get(&fv_medium, &fv, NULL, lo, hi);
                    if (fv_count > 0 && memcmp(fv_medium.v[8], medium.v[8], sizeof(float) * 4 * 5 * PC_FOGVOL_MAX))
                        gl_Uniform4fv(u_medium, PC_MEDIUM_VEC4S, &fv_medium.v[0][0]); /* (no haze record this frame) */
                    gl_Uniform4fv(u_fogvol, PC_FOGVOL_VEC4S, &fv.v[0][0]);
                    gl_Uniform4i(u_fogvol_on, fv_count > 0, pc_fog_volume_steps_gl < 4 ? 4 : pc_fog_volume_steps_gl > 128 ? 128 : pc_fog_volume_steps_gl,
                        pc_fog_volume_debug, 0);
                    gl_ActiveTexture(GL_TEXTURE17);
                    gl_BindTexture(GL_TEXTURE_3D, pc_gl_medium_texture());
                    gl_Uniform1i(u_medium_tex, 17);
                    gl_ActiveTexture(GL_TEXTURE0);
                }
                gl_Uniform4f(u_quality0, medium_on && !traced_haze ? 1.0f : 0.0f, pc_fog_anisotropy, pc_ao_radius,
                    pc_ao_strength);
                gl_Uniform4i(u_quality1, pc_shadow_map_filter, pc_lighting_ssao(gi), pc_haze_debug, 0);
                gl_Uniform4f(u_quality2, pc_light_size, 0, 0, 0);
                if (!gi) { /* the unshadowed haze fills the room's bounds too */
                    float lo[3], hi[3];
                    pc_scene_bounds(lo, hi);
                    gl_Uniform3f(u_room_lo, lo[0], lo[1], lo[2]);
                    gl_Uniform3f(u_room_hi, hi[0], hi[1], hi[2]);
                }
                gl_ActiveTexture(GL_TEXTURE8);
                gl_BindTexture(GL_TEXTURE_2D, gi);
                gl_Uniform1i(u_gi_tex, 8);
                /* 128: a traced result is bound; 64: the probe field (probe_gi.inc, units 13-15). */
                int probes = pc_probe_gl_bind ? pc_probe_gl_bind(scaled_program, 13) : 0;
                gl_Uniform1i(u_gi_on, (gi ? (pc_gi_enabled ? (pc_gi_debug == 1 ? 2 : 1) : 0) | (pc_rt_shadows ? 4 : 0)
                        | (pc_gi_debug == 2 ? 8 : 0) | (pc_baked_shadows_active ? 16 : 0) | (pc_gi_debug == 3 ? 32 : 0) | 128 : 0)
                        | (probes ? 64 | (pc_gi_debug == 1 ? 2 : 0) : 0));
                {
                    /* The room's sun and sky: ptgi.c's trace weighs a shadow-casting sun into
                     * the visibility; the sky's ambient where nothing traces the sky. */
                    pc_env_gpu env;
                    int env_on = pc_env_gpu_get(&env);
                    int sun_mode = -1; /* unshadowed */
                    if (env.sun_col[3] > 0.5f && gi && pc_rt_shadows)
                        sun_mode = -2; /* traced */
                    else if (env.sun_col[3] > 0.5f) { /* a shadow map (shadows.c) */
                        float matrix[16], light[4];
                        if (pc_shadows_sun_update(env.sun_dir, matrix, light)) {
                            sun_mode = 0;
                            gl_UseProgram(scaled_program); /* the map's pass used its own */
                            gl_BindVertexArray(vertex_array);
                            gl_Viewport(pc_gl_view[0], window_h - pc_gl_view[1] - view_h, view_w, view_h);
                            gl_UniformMatrix4fv(u_sun_matrix, 1, GL_FALSE, matrix);
                            gl_Uniform4fv(u_sun_light, 1, light);
                            gl_Uniform1f(u_sun_texel, 1.0f / 1024.0f);
                            pc_shadows_sun_bind(16);
                            gl_Uniform1i(u_sun_map, 16);
                        }
                    }
                    gl_Uniform4fv(u_env, 5, &env.sun_dir[0]);
                    gl_Uniform4i(u_env_mode, sun_mode, env.zenith[3] > 0.5f && !(gi && pc_gi_enabled) && !probes,
                        env_on, 0);
                }
                if (gi) {
                    GLuint gp, gn;
                    int gw, gh;
                    pc_gi_surface(&gp, &gn, &gw, &gh);
                    gl_ActiveTexture(GL_TEXTURE9);
                    gl_BindTexture(GL_TEXTURE_2D, gp);
                    gl_Uniform1i(u_gi_position, 9);
                    gl_ActiveTexture(GL_TEXTURE10);
                    gl_BindTexture(GL_TEXTURE_2D, gn);
                    gl_Uniform1i(u_gi_normal, 10);
                    gl_Uniform2i(u_gi_size, gw, gh);
                    gl_ActiveTexture(GL_TEXTURE11);
                    gl_BindTexture(GL_TEXTURE_2D, pc_gi_extra(0));
                    gl_Uniform1i(u_gi_extra, 11);
                    gl_Uniform1f(u_fog_density, traced_haze && medium_on ? 1.0f : 0.0f); /* haze High: traced */
                    float lo[3], hi[3];
                    pc_gi_room_bounds(lo, hi);
                    gl_Uniform3f(u_room_lo, lo[0], lo[1], lo[2]);
                    gl_Uniform3f(u_room_hi, hi[0], hi[1], hi[2]);
                }
                gl_Uniform1f(u_gi_strength, pc_gi_strength);
                GLuint normal, albedo;
                pc_hw_gbuffer(&normal, &albedo);
                gl_ActiveTexture(GL_TEXTURE2);
                gl_BindTexture(GL_TEXTURE_2D, normal);
                gl_Uniform1i(u_normal_tex, 2);
                gl_ActiveTexture(GL_TEXTURE3);
                gl_BindTexture(GL_TEXTURE_2D, albedo);
                gl_Uniform1i(u_albedo_tex, 3);
                gl_ActiveTexture(GL_TEXTURE12);
                gl_BindTexture(GL_TEXTURE_2D, pc_hw_shade_texture());
                gl_Uniform1i(u_shade_tex, 12);
                gl_Uniform1i(u_view_mode, pc_gl_view_mode);
                gl_ActiveTexture(GL_TEXTURE0);
            }
            gl_ActiveTexture(GL_TEXTURE1);
            gl_BindTexture(GL_TEXTURE_2D, pc_hw_depth_texture());
            gl_Uniform1i(u_scaled_depth, 1);
            if (particles_units) {
                particles_bind(particles_on, particle_keys, w, h, particle_colours, particle_count);
                gl_Uniform1i(u_particle_keys, 19);
                gl_Uniform1i(u_particle_colours, 20);
            }
            gl_Uniform4i(u_particles_on, particles_on, w, h, 0);
            /* The shade target also marks emissive texels (post bit 16), lit or not. */
            gl_ActiveTexture(GL_TEXTURE12);
            gl_BindTexture(GL_TEXTURE_2D, pc_hw_shade_texture());
            gl_Uniform1i(u_shade_tex, 12);
            gl_ActiveTexture(GL_TEXTURE0);
        } else {
            gl_BindTexture(GL_TEXTURE_2D, vram_texture);
            gl_PixelStorei(GL_UNPACK_ALIGNMENT, 2);
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512, GL_RED_INTEGER, GL_UNSIGNED_SHORT, vram);
            gl_UseProgram(program);
            gl_Uniform1i(u_vram, 0);
            gl_Uniform4i(u_rect, x, y, w, h);
            gl_Uniform1i(u_rgb24, pc_display_rgb24);
        }
        /* The post chain (antialias.h): the lit image -> [FXAA | SMAA] -> [depth of field blur] ->
         * [supersampling's downsample] -> the window. */
        int post = target && (pc_gl_aa == PC_AA_FXAA || (pc_gl_aa == PC_AA_SMAA && smaa_ready())) ? pc_gl_aa : PC_AA_OFF;
        int blur = target && !pc_gl_show_depth && (post_frame.flags & 3);
        /* The post stack's grade pass (post.h): only when an effect is on; never on the depth view. */
        int grade = target && !pc_gl_show_depth && grade_ready;
        pc_post_active_grade = grade;
        if (!grade)
            pc_post_active_bloom = 0;
        int stages = 1 + (post != PC_AA_OFF) + blur + grade + ssaa;
        if ((stages > 1 && !ensure_view_target(&chain[0], lit_w, lit_h))
            || (stages > 2 && !ensure_view_target(&chain[1], lit_w, lit_h)))
            stages = 1, post = PC_AA_OFF, blur = ssaa = grade = 0; /* (lit_w/h only matter with stages > 1) */
        if (post == PC_AA_SMAA
            && (!ensure_target_format(&smaa_edges, lit_w, lit_h, GL_RG8)
                || !ensure_target_format(&smaa_weights, lit_w, lit_h, GL_RGBA8))) {
            post = PC_AA_OFF;
            stages = 1 + blur + grade + ssaa;
        }
        stages += brightness_stage;
        int view_x = pc_gl_view[0], view_y = window_h - pc_gl_view[1] - view_h;
        int stage = 0;
        GLuint input = stage_begin(stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
        gl_DrawArrays(GL_TRIANGLES, 0, 3);
        if (post == PC_AA_FXAA) {
            GLuint source = input;
            input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
            gl_UseProgram(fxaa_program);
            gl_ActiveTexture(GL_TEXTURE0);
            gl_BindTexture(GL_TEXTURE_2D, source);
            gl_Uniform1i(u_fxaa_image, 0);
            gl_Uniform2f(u_fxaa_texel, 1.0f / lit_w, 1.0f / lit_h);
            gl_DrawArrays(GL_TRIANGLES, 0, 3);
        } else if (post == PC_AA_SMAA) {
            GLuint source = input;
            float metrics[4] = { 1.0f / lit_w, 1.0f / lit_h, (float)lit_w, (float)lit_h };
            /* 1: edges (the reference discards pixels without any: cleared first). */
            gl_BindFramebuffer(GL_FRAMEBUFFER, smaa_edges.fbo);
            gl_Viewport(0, 0, lit_w, lit_h);
            gl_ClearColor(0, 0, 0, 0);
            gl_Clear(GL_COLOR_BUFFER_BIT);
            gl_UseProgram(smaa_edges_program);
            gl_Uniform4fv(u_smaa_metrics[0], 1, metrics);
            gl_Uniform4i(u_smaa_rect, x, y, w, h);
            gl_Uniform4i(u_smaa_info, pc_hw_scale, pc_aa_geometry_edges && !pc_gl_show_depth, 0, 0);
            GLuint normal, albedo;
            pc_hw_gbuffer(&normal, &albedo);
            GLuint inputs[3] = { source, pc_hw_depth_texture(), normal };
            for (int i = 2; i >= 0; i--) {
                gl_ActiveTexture(GL_TEXTURE0 + i);
                gl_BindTexture(GL_TEXTURE_2D, inputs[i]);
            }
            gl_DrawArrays(GL_TRIANGLES, 0, 3);
            /* 2: blending weights. */
            gl_BindFramebuffer(GL_FRAMEBUFFER, smaa_weights.fbo);
            gl_UseProgram(smaa_weights_program);
            gl_Uniform4fv(u_smaa_metrics[1], 1, metrics);
            GLuint lookups[3] = { smaa_edges.texture, smaa_area_texture, smaa_search_texture };
            for (int i = 2; i >= 0; i--) {
                gl_ActiveTexture(GL_TEXTURE0 + i);
                gl_BindTexture(GL_TEXTURE_2D, lookups[i]);
            }
            gl_DrawArrays(GL_TRIANGLES, 0, 3);
            /* 3: neighbourhood blending. */
            input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
            gl_UseProgram(smaa_blend_program);
            gl_Uniform4fv(u_smaa_metrics[2], 1, metrics);
            gl_ActiveTexture(GL_TEXTURE1);
            gl_BindTexture(GL_TEXTURE_2D, smaa_weights.texture);
            gl_ActiveTexture(GL_TEXTURE0);
            gl_BindTexture(GL_TEXTURE_2D, source);
            gl_DrawArrays(GL_TRIANGLES, 0, 3);
        }
        if (blur) {
            GLuint source = input;
            input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
            gl_UseProgram(blur_program);
            gl_ActiveTexture(GL_TEXTURE0);
            gl_BindTexture(GL_TEXTURE_2D, source);
            gl_Uniform1i(u_blur_lit, 0);
            gl_Uniform2f(u_blur_view, (float)lit_w, (float)lit_h);
            float most = post_frame.dof_radius > post_frame.tilt[2] ? post_frame.dof_radius : post_frame.tilt[2];
            gl_Uniform1f(u_blur_radius, most * lit_h / 720.0f);
            gl_Uniform1i(u_blur_keep, grade);
            gl_DrawArrays(GL_TRIANGLES, 0, 3);
        }
        if (grade) {
            GLuint source = input;
            input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
            int last = stage + 1 >= stages;
            gl_grade(&post_frame, source, lit_w, lit_h, view_w, view_h, last ? view_x : 0, last ? view_y : 0, last);
        }
        if (ssaa) {
            GLuint source = input;
            stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
            gl_UseProgram(downsample_program);
            gl_ActiveTexture(GL_TEXTURE0);
            gl_BindTexture(GL_TEXTURE_2D, source);
            gl_Uniform2f(u_down_src_size, (float)lit_w, (float)lit_h);
            gl_Uniform2f(u_down_dst_size, (float)view_w, (float)view_h);
            gl_Uniform1i(u_down_filter, pc_aa_downsample);
            gl_DrawArrays(GL_TRIANGLES, 0, 3);
        }
        /* The modern menu's backdrop: keep this picture of the world (backdrop_keep, above). */
        if (target && !pc_gl_show_depth)
            backdrop_keep(view_x, view_y, view_w, view_h);
        /* Text held back for the window's resolution (gpu_hw.c ui_text_overlay), over the picture, before the
         * brightness curve. */
        if (target) { /* (into the chain's last image, its view as the viewport) */
            gl_BindFramebuffer(GL_FRAMEBUFFER, brightness_stage ? brightness_target.fbo : 0);
            if (brightness_stage)
                gl_Viewport(0, 0, view_w, view_h);
            else
                gl_Viewport(view_x, view_y, view_w, view_h);
            pc_hw_text_overlay(view_x, view_y, view_w, view_h, brightness_stage);
        }
        if (brightness_stage) { /* settings brightness (above) */
            stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
            brightness_draw(view_x, view_y);
        }
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_BindVertexArray(0);
    }
    gl_Viewport(0, 0, window_w, window_h);
    pc_overlay_frame(window_w, window_h);
    if (pc_hw_active)
        pc_hw_present_done();
    capture_present();
    finish_frame();
    gpu_timer(0);
    swap();
    gpu_timer(1);
}
