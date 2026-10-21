// Editor view: the captured room rendered by us from a free camera (F2).
//
// The game pauses; this view draws the room capture (scene_capture.c) with
// the game's textures (decoded from native VRAM exactly as gpu_hw.c does),
// baked vertex colours, smooth per-vertex normals and the dynamic lights with
// their shadow cubes, all in world space. Controls follow Unreal's:
//   right mouse + mouse   look          right mouse + W A S D / Q E   fly
//   right mouse + wheel   flight speed  wheel                        dolly
//   Alt + left drag       orbit         middle drag                  pan
//   F                     focus the selected light
// The lights editor (lights.cpp) works here too through pc_editor_view and
// pc_editor_pick (world position and normal under the cursor).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern "C" {
#include "gl_api.h"
#include "light_shape.h"
#ifdef ROOD_VULKAN
#include "vk_hooks.h"
#else
static inline int pc_vk_on(void) { return 0; }
#endif
}
#include "imgui.h"
#include "ImGuizmo.h"
#include "scene_capture.h"
#include "dev_state.h"
#include "room_editor.h"
#include "lighting_quality.h"
#include "ui/ui_shell.h" /* pc_ui_shell_wants_mouse */

extern "C" {
void render_cutaway(int on, const float focus[3], float amount) __attribute__((weak));
GLuint pc_hw_native_texture(void);
const float* pc_hw_casters(int* count, int* serial);
const void* pc_hw_caster_surfaces(void);
int pc_shadows_update(int count, const float world[][4], const int* wants, int* slot_out);
void pc_shadows_bind(int first_unit);
int pc_lights_world(float position[][4], float color[][4], float direction[][4], float extent_right[][4],
    float extent_up[][4], int shadows[], int max);
int pc_lights_selected_position(float out[3], float* radius);
void pc_rpc_set_paused(int paused);
int pc_overlay_look_delta(float* dx, float* dy);
extern float pc_lights_baked, pc_shadow_softness;
extern int pc_hw_active, pc_lights_enabled;
extern int pc_gl_view[4], pc_gl_display[4];
extern int pc_gi_enabled, pc_rt_shadows, pc_gi_debug, pc_baked_shadows, pc_baked_shadows_active;
GLuint pc_gi_extra(int editor);
extern float pc_gi_strength, pc_gi_resolution, pc_fog_density;
void pc_gi_room_bounds(float lo[3], float hi[3]);
GLuint pc_gi_update_world(GLuint world_position, GLuint world_normal, int w, int h, float scale,
    const float view_projection[16], const float eye[3], GLuint* position, GLuint* normal, int size[2]);
}

namespace {
bool active;
bool flying; /* right mouse held in the view: the overlay holds the mouse in relative mode */
/* Live: the free camera drives the game's own rendering (its camera is
 * replaced in func_8007ACB0, the game keeps running); otherwise the
 * captured room is drawn by this file's renderer with the game paused. */
bool live = true;
float eye[3], yaw, pitch, fov_y = 25, speed = 600;
float look_sensitivity = 0.12f; // degrees per pixel of a 1080-line screen (scaled to the window)
float pivot[3];
float stretch = 1; // the game's horizontal focal length over its vertical (pixel aspect)
float centre[2]; // projection centre offset in NDC (the game's OFX/OFY need not be central)
bool show_actors = true, textured_actors = true, show_grid = true, hide_cutaway = true;
float grid_height;

GLuint program, vertex_array, vertex_buffer, actor_array, actor_buffer;
GLuint fbo, color_texture, position_texture, normal_texture, depth_buffer;
int fbo_size[2];
int uploaded_serial = -1, vertex_count, semi_first[4], semi_count[4], uploaded_actors = -1, actor_vertices;
float last_view[16], last_projection[16], last_rect[4];

struct Vertex {
    float position[3];
    float normal[3];
    uint8_t rgb[4];
    uint16_t u, v, tpage, clut;
    uint32_t code;
};

const char* vertex_source = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec4 a_texture; // u, v, tpage, clut
layout(location = 4) in uint a_code;
uniform mat4 view_projection;
out vec3 v_world;
out vec3 v_normal;
out vec3 v_color;
out vec2 v_uv;
flat out uvec3 v_state; // tpage, clut, code
flat out uint v_mask;
void main() {
    v_mask = uint(a_color.a * 255.0 + 0.5);
    v_world = a_position;
    v_normal = a_normal;
    v_color = a_color.rgb;
    v_uv = vec2(a_texture.xy);
    v_state = uvec3(a_texture.zw, a_code);
    gl_Position = view_projection * vec4(a_position, 1.0);
}
)";
const char* fragment_source = R"(#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec3 v_color;
in vec2 v_uv;
flat in uvec3 v_state;
flat in uint v_mask;
uniform uint facing; // the game's cutaway direction; 0 shows every wall
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 picked; // world xyz, 1 where geometry
layout(location = 2) out vec4 normal_out; // world normal (for the path tracer)
uniform int prepass; // 1: surfaces only (position, normal) for the path tracer
uniform sampler2D gi_tex; // ptgi.c: rgb indirect light, a shadow visibility
uniform sampler2D gi_position;
uniform sampler2D gi_normal;
uniform sampler2D gi_extra; // r: baked light reaching the surface past characters, gba: haze light
uniform float fog_density;
uniform vec3 room_lo, room_hi;
float inside_room(vec3 eye, vec3 p) {
    vec3 v = p - eye;
    float l = length(v);
    vec3 inv = l / v;
    vec3 t0 = (room_lo - eye) * inv, t1 = (room_hi - eye) * inv;
    float enter = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));
    float leave = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), l));
    return max(leave - enter, 0.0);
}
uniform ivec2 gi_size;
uniform int gi_on; // 1 add, 2 show alone, 4 traced shadows, 8 show visibility
uniform float gi_strength;
uniform vec2 target_size;
uniform usampler2D vram;
uniform int textured; // 2 always (room), 1 when the primitive is (actors), 0 grey
uniform vec3 camera;
uniform int light_count;
uniform vec4 light_pos[16];   // world xyz, radius
uniform vec4 light_col[16];   // rgb * intensity, type
uniform vec4 light_dir[16];   // world direction, cos(half cone)
uniform vec4 light_right[16];
uniform vec4 light_up[16];
uniform int light_shadow[16];
uniform samplerCube shadow0, shadow1, shadow2, shadow3;
uniform float shadow_softness;
uniform float baked;
uniform float semi_scale; // 0.25 for the B + F/4 blend mode
uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }
float stored(int slot, vec3 d) {
    if (slot == 0) return texture(shadow0, d).r;
    if (slot == 1) return texture(shadow1, d).r;
    if (slot == 2) return texture(shadow2, d).r;
    return texture(shadow3, d).r;
}
float visibility(int slot, vec3 world, vec4 light) {
    vec3 d = world - light.xyz;
    float dist = length(d) / light.w;
    vec3 axis = normalize(d);
    vec3 t = normalize(cross(abs(axis.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), axis));
    vec3 b = cross(axis, t);
    float lit = 0.0;
    for (int k = 0; k < 8; k++) {
        float a = float(k) * 0.785398 + 0.39;
        vec3 dir = axis + (t * cos(a) + b * sin(a)) * shadow_softness * (0.5 + 0.5 * float(k & 1));
        lit += dist - 0.004 <= stored(slot, dir) ? 1.0 : 0.0;
    }
    return lit / 8.0;
}
// GI at this pixel: its 2 x 2 GI pixels weighted bilinearly and by surface match.
vec4 gi_at(vec3 world, vec3 n, out vec4 extra_out) {
    vec2 sp = gl_FragCoord.xy / target_size * vec2(gi_size) - 0.5;
    ivec2 b = ivec2(floor(sp));
    vec2 f = sp - vec2(b);
    vec4 sum = vec4(0.0), extra = vec4(0.0);
    float weight = 0.0;
    // The 2 x 2 around the pixel, then the ring around it if none match.
    for (int pass = 0; pass < 2 && weight < 1e-5; pass++)
        for (int y = -1; y <= 2; y++)
            for (int x = -1; x <= 2; x++) {
                bool inner = x >= 0 && x <= 1 && y >= 0 && y <= 1;
                if (inner != (pass == 0)) continue;
                ivec2 h = clamp(b + ivec2(x, y), ivec2(0), gi_size - 1);
                vec4 gp = texelFetch(gi_position, h, 0);
                if (gp.w < 0.5) continue;
                vec3 gn = texelFetch(gi_normal, h, 0).xyz;
                float w = inner ? (x == 1 ? f.x : 1.0 - f.x) * (y == 1 ? f.y : 1.0 - f.y) + 0.01 : 0.01;
                w *= exp(-abs(dot(gp.xyz - world, n)) / 4.0) * pow(max(dot(gn, n), 0.0), 8.0);
                sum += texelFetch(gi_tex, h, 0) * w;
                extra += texelFetch(gi_extra, h, 0) * w;
                weight += w;
            }
    // Nothing similar nearby: neutral rather than a neighbour's shadow.
    extra_out = weight > 1e-5 ? extra / weight : vec4(1.0, 0.0, 0.0, 0.0);
    return weight > 1e-5 ? sum / weight : vec4(0.0, 0.0, 0.0, 1.0);
}
)" PC_LIGHT_SHAPE_GLSL R"(
void main() {
    if (facing != 0u && (((v_mask ^ 255u) & facing & 255u) != 0u)) discard; // as func_8009723C
    vec3 albedo = vec3(0.55), base = albedo;
    uint tpage = v_state.x, clut = v_state.y, code = v_state.z;
    if (textured == 2 || (textured == 1 && (code & 4u) != 0u)) {
        uint u = uint(floor(v_uv.x + 0.0001)) & 255u, v = uint(floor(v_uv.y + 0.0001)) & 255u;
        int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;
        int cx = int(clut & 63u) * 16, cy = int((clut >> 6) & 511u);
        uint depth = (tpage >> 7) & 3u, t;
        if (depth == 0u)
            t = fetch(cx + int((fetch(tx + int(u >> 2), ty + int(v)) >> ((u & 3u) * 4u)) & 15u), cy);
        else if (depth == 1u)
            t = fetch(cx + int((fetch(tx + int(u >> 1), ty + int(v)) >> ((u & 1u) * 8u)) & 255u), cy);
        else
            t = fetch(tx + int(u), ty + int(v));
        if (t == 0u) discard;
        albedo = vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)) / 31.0;
        base = (code & 1u) != 0u ? albedo : min(albedo * v_color * (255.0 / 128.0), 1.0);
    } else if (textured == 1) {
        albedo = base = v_color; // flat or Gouraud-shaded primitive
    }
    vec3 n = normalize(v_normal);
    if (dot(n, camera - v_world) < 0.0) n = -n; // two-sided, like the PS1
    picked = vec4(v_world, 1.0);
    normal_out = vec4(n, textured == 2 ? 1.0 : 2.0); // w 2: a character or prop
    if (prepass != 0) { color = vec4(0.0); return; }
    vec4 extra = vec4(1.0, 0.0, 0.0, 0.0);
    vec4 gi = gi_on != 0 ? gi_at(v_world, n, extra) : vec4(0.0, 0.0, 0.0, 1.0);
    float baked_vis = (gi_on & 16) != 0 ? extra.r : 1.0;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < light_count; i++) {
        vec3 centre = light_pos[i].xyz;
        if (light_col[i].w > 1.5) {
            vec3 rr = light_right[i].xyz, ru = light_up[i].xyz, rel = v_world - centre;
            centre += rr * clamp(dot(rel, rr) / max(dot(rr, rr), 1e-6), -1.0, 1.0)
                    + ru * clamp(dot(rel, ru) / max(dot(ru, ru), 1e-6), -1.0, 1.0);
        }
        vec3 to = centre - v_world;
        float d = length(to);
        vec3 l = to / max(d, 1e-3);
        float x = clamp(d / light_pos[i].w, 0.0, 1.0);
        float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);
        a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], v_world, l);
        if (light_shadow[i] == -2) // ray-traced (ptgi.c)
            a *= clamp(gi.a, 0.0, 1.0);
        else if (light_shadow[i] >= 0 && a > 0.0)
            a *= visibility(light_shadow[i], v_world + n * max(2.0, light_pos[i].w * 0.01), light_pos[i]);
        sum += light_col[i].rgb * a;
    }
    if ((gi_on & 2) != 0) { color = vec4(min(gi.rgb * gi_strength, vec3(1.0)), 1.0); return; }
    if ((gi_on & 8) != 0) { color = vec4(vec3(gi.a), 1.0); return; }
    if ((gi_on & 32) != 0) { color = vec4(vec3(baked_vis), 1.0); return; }
    if ((gi_on & 1) != 0) sum += gi.rgb * gi_strength;
    vec3 c = base * baked * baked_vis + albedo * sum;
    if (gi_on != 0 && fog_density > 0.0) // volumetric haze between the eye and the surface
        c = c * exp(-fog_density * inside_room(camera, v_world)) + max(extra.gba, vec3(0.0));
    c = mix(c, 1.0 - 0.2 * exp(-(c - 0.8) / 0.2), step(0.8, c));
    color = vec4(c * semi_scale, 0.5); // alpha is the B/2 + F/2 blend's factor
}
)";

GLint u_facing, u_view_projection, u_vram, u_textured, u_camera, u_light_count, u_light_pos, u_light_col, u_light_dir,
    u_light_right, u_light_up, u_light_shadow, u_shadow[4], u_softness, u_baked, u_semi_scale, u_prepass, u_gi_tex, u_gi_position, u_gi_normal,
    u_gi_size, u_gi_on, u_gi_strength, u_target_size, u_gi_extra, u_fog_density, u_room_lo, u_room_hi;
extern "C" GLuint pc_gl_program(const char* vertex, const char* fragment);

bool init()
{
    static int state;
    if (state)
        return state > 0;
    state = -1;
    program = pc_gl_program(vertex_source, fragment_source);
    if (!program)
        return false;
#define U(name) gl_GetUniformLocation(program, name)
    u_view_projection = U("view_projection");
    u_vram = U("vram");
    u_textured = U("textured");
    u_camera = U("camera");
    u_light_count = U("light_count");
    u_light_pos = U("light_pos");
    u_light_col = U("light_col");
    u_light_dir = U("light_dir");
    u_light_right = U("light_right");
    u_light_up = U("light_up");
    u_light_shadow = U("light_shadow");
    u_softness = U("shadow_softness");
    u_baked = U("baked");
    u_semi_scale = U("semi_scale");
    u_facing = U("facing");
    u_prepass = U("prepass");
    u_gi_tex = U("gi_tex");
    u_gi_position = U("gi_position");
    u_gi_normal = U("gi_normal");
    u_gi_extra = U("gi_extra");
    u_fog_density = U("fog_density");
    u_room_lo = U("room_lo");
    u_room_hi = U("room_hi");
    u_gi_size = U("gi_size");
    u_gi_on = U("gi_on");
    u_gi_strength = U("gi_strength");
    u_target_size = U("target_size");
    for (int i = 0; i < 4; i++) {
        char name[16];
        snprintf(name, sizeof name, "shadow%d", i);
        u_shadow[i] = U(name);
    }
#undef U
    gl_GenVertexArrays(1, &vertex_array);
    gl_GenBuffers(1, &vertex_buffer);
    gl_GenVertexArrays(1, &actor_array);
    gl_GenBuffers(1, &actor_buffer);
    GLuint arrays[2] = { vertex_array, actor_array }, buffers[2] = { vertex_buffer, actor_buffer };
    for (int k = 0; k < 2; k++) {
        gl_BindVertexArray(arrays[k]);
        gl_BindBuffer(GL_ARRAY_BUFFER, buffers[k]);
        gl_EnableVertexAttribArray(0);
        gl_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
        gl_EnableVertexAttribArray(1);
        gl_VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        gl_EnableVertexAttribArray(2);
        gl_VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, rgb));
        gl_EnableVertexAttribArray(3);
        gl_VertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, u));
        gl_EnableVertexAttribArray(4);
        gl_VertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)offsetof(Vertex, code));
    }
    gl_BindVertexArray(0);
    gl_GenFramebuffers(1, &fbo);
    gl_GenTextures(1, &color_texture);
    gl_GenTextures(1, &position_texture);
    gl_GenTextures(1, &normal_texture);
    gl_GenRenderbuffers(1, &depth_buffer);
    state = 1;
    return true;
}

void ensure_target(int w, int h)
{
    if (fbo_size[0] == w && fbo_size[1] == h)
        return;
    fbo_size[0] = w, fbo_size[1] = h;
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_BindTexture(GL_TEXTURE_2D, color_texture);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture, 0);
    gl_BindTexture(GL_TEXTURE_2D, position_texture);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, position_texture, 0);
    gl_BindTexture(GL_TEXTURE_2D, normal_texture);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, normal_texture, 0);
    gl_BindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
    gl_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Smooth normals over a triangle list: average the face normals sharing a
 * position, but only those within ~50 degrees of the face's own (creases
 * stay hard). Winding is not consistent, so neighbours are sign-aligned. */
void smooth_normals(std::vector<Vertex>& vertices)
{
    int count = (int)vertices.size() / 3;
    std::vector<float> face(count * 3);
    for (int i = 0; i < count; i++) {
        const float *a = vertices[i * 3].position, *b = vertices[i * 3 + 1].position, *c = vertices[i * 3 + 2].position;
        float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int k = 0; k < 3; k++)
            face[i * 3 + k] = l > 1e-6f ? n[k] / l : 0;
    }
    // Bucket triangle corners by rounded position.
    struct Key {
        int x, y, z;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    std::vector<std::vector<int>> buckets(8192);
    auto hash = [](const Key& k) { return (unsigned)(k.x * 73856093 ^ k.y * 19349663 ^ k.z * 83492791) & 8191; };
    auto key_of = [](const float* p) { return Key { (int)lroundf(p[0]), (int)lroundf(p[1]), (int)lroundf(p[2]) }; };
    for (int c = 0; c < count * 3; c++)
        buckets[hash(key_of(vertices[c].position))].push_back(c);
    for (int c = 0; c < count * 3; c++) {
        Key k = key_of(vertices[c].position);
        float n[3] = { 0, 0, 0 };
        const float* own = &face[(c / 3) * 3];
        for (int corner : buckets[hash(k)]) {
            if (!(key_of(vertices[corner].position) == k))
                continue;
            const float* other = &face[(corner / 3) * 3];
            float d = own[0] * other[0] + own[1] * other[1] + own[2] * other[2];
            if (fabsf(d) < 0.64f)
                continue;
            float s = d < 0 ? -1.0f : 1.0f;
            for (int q = 0; q < 3; q++)
                n[q] += other[q] * s;
        }
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int q = 0; q < 3; q++)
            vertices[c].normal[q] = l > 1e-6f ? n[q] / l : own[q];
    }
}

void upload_scene()
{
    int count;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    int serial = pc_scene_serial();
    if (serial == uploaded_serial)
        return;
    uploaded_serial = serial;
    std::vector<Vertex> vertices(count * 3);
    for (int i = 0; i < count; i++)
        for (int j = 0; j < 3; j++) {
            const scene_vertex& sv = tris[i].vertex[j];
            Vertex& v = vertices[i * 3 + j];
            memcpy(v.position, sv.position, sizeof v.position);
            v.rgb[0] = sv.rgb[0], v.rgb[1] = sv.rgb[1], v.rgb[2] = sv.rgb[2];
            v.rgb[3] = (uint8_t)(tris[i].flags & 0xff); // cutaway direction mask
            v.u = sv.u, v.v = sv.v, v.tpage = tris[i].tpage, v.clut = tris[i].clut;
            v.code = tris[i].code;
        }
    smooth_normals(vertices);
    /* Opaque triangles first, then translucent ones grouped by blend mode
     * (drawn afterwards, blended, like the game's glows and glass). */
    std::vector<Vertex> sorted;
    sorted.reserve(vertices.size());
    for (int group = -1; group < 4; group++) {
        if (group >= 0)
            semi_first[group] = (int)sorted.size();
        for (int i = 0; i < count; i++) {
            int semi = tris[i].code & 2 ? (tris[i].tpage >> 5) & 3 : -1;
            if (semi == group)
                sorted.insert(sorted.end(), vertices.begin() + i * 3, vertices.begin() + i * 3 + 3);
        }
        if (group < 0)
            vertex_count = (int)sorted.size();
        else
            semi_count[group] = (int)sorted.size() - semi_first[group];
    }
    vertices.swap(sorted);
    gl_BindVertexArray(vertex_array);
    gl_BindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    gl_BindVertexArray(0);
}

/* Characters and props: the hardware path's per-frame casters with their
 * surfaces (vertex colours as lit by the game, texture coordinates). */
struct CasterSurface {
    uint8_t rgb[9], uv[6], code;
    uint16_t tpage, clut;
};
void upload_actors()
{
    int count, serial;
    const float* positions = pc_hw_casters(&count, &serial);
    const CasterSurface* surfaces = (const CasterSurface*)pc_hw_caster_surfaces();
    if (serial == uploaded_actors)
        return;
    uploaded_actors = serial;
    std::vector<Vertex> vertices(count * 3);
    for (int i = 0; i < count; i++) {
        const CasterSurface& sf = surfaces[i];
        for (int j = 0; j < 3; j++) {
            Vertex& v = vertices[i * 3 + j];
            memset(&v, 0, sizeof v);
            memcpy(v.position, positions + i * 9 + j * 3, sizeof v.position);
            v.rgb[0] = sf.rgb[j * 3], v.rgb[1] = sf.rgb[j * 3 + 1], v.rgb[2] = sf.rgb[j * 3 + 2];
            v.rgb[3] = 255;
            v.u = sf.uv[j * 2], v.v = sf.uv[j * 2 + 1], v.tpage = sf.tpage, v.clut = sf.clut;
            v.code = sf.code;
        }
    }
    smooth_normals(vertices);
    gl_BindVertexArray(actor_array);
    gl_BindBuffer(GL_ARRAY_BUFFER, actor_buffer);
    gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STREAM_DRAW);
    gl_BindVertexArray(0);
    actor_vertices = (int)vertices.size();
}

/* World direction the camera looks along (y grows downward; pitch > 0 is up). */
void forward_vector(float f[3])
{
    f[0] = cosf(pitch) * sinf(yaw);
    f[1] = -sinf(pitch);
    f[2] = cosf(pitch) * cosf(yaw);
}
void basis(float f[3], float r[3], float u[3])
{
    forward_vector(f);
    const float up[3] = { 0, -1, 0 };
    r[0] = f[1] * up[2] - f[2] * up[1];
    r[1] = f[2] * up[0] - f[0] * up[2];
    r[2] = f[0] * up[1] - f[1] * up[0];
    float l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int k = 0; k < 3; k++)
        r[k] /= l > 1e-6f ? l : 1;
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];
}
/* Column-major GL view (lookAt) and perspective projection. */
void matrices(float aspect, float view[16], float projection[16])
{
    float f[3], r[3], u[3];
    basis(f, r, u);
    memset(view, 0, 16 * sizeof(float));
    for (int k = 0; k < 3; k++) {
        view[k * 4 + 0] = r[k];
        view[k * 4 + 1] = u[k];
        view[k * 4 + 2] = -f[k];
    }
    view[12] = -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]);
    view[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    view[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    view[15] = 1;
    float n = 8, far = 65536, t = 1.0f / tanf(fov_y * 0.5f * 3.14159265f / 180);
    memset(projection, 0, 16 * sizeof(float));
    projection[0] = t / aspect * stretch;
    projection[5] = t;
    projection[10] = -(far + n) / (far - n);
    projection[11] = -1;
    projection[14] = -2 * far * n / (far - n);
    projection[8] = -centre[0];
    projection[9] = -centre[1];
}

/* Start from the game camera: same position, direction and field of view. */
void from_game_camera()
{
    float r[3][3], t[3], h, ox, oy;
    if (!pc_scene_camera_get(r, t, &h, &ox, &oy))
        return;
    /* The rows carry different scales (X is stretched for the 320 x 224
     * display's pixel aspect), so invert properly: eye = -R^-1 t. */
    float c[3][3] = {
        { r[1][1] * r[2][2] - r[1][2] * r[2][1], r[0][2] * r[2][1] - r[0][1] * r[2][2], r[0][1] * r[1][2] - r[0][2] * r[1][1] },
        { r[1][2] * r[2][0] - r[1][0] * r[2][2], r[0][0] * r[2][2] - r[0][2] * r[2][0], r[0][2] * r[1][0] - r[0][0] * r[1][2] },
        { r[1][0] * r[2][1] - r[1][1] * r[2][0], r[0][1] * r[2][0] - r[0][0] * r[2][1], r[0][0] * r[1][1] - r[0][1] * r[1][0] },
    };
    float det = r[0][0] * c[0][0] + r[0][1] * c[1][0] + r[0][2] * c[2][0];
    if (fabsf(det) < 1e-9f)
        return;
    for (int k = 0; k < 3; k++)
        eye[k] = -(c[k][0] * t[0] + c[k][1] * t[1] + c[k][2] * t[2]) / det;
    float scale[3];
    for (int i = 0; i < 3; i++)
        scale[i] = sqrtf(r[i][0] * r[i][0] + r[i][1] * r[i][1] + r[i][2] * r[i][2]);
    float f[3] = { r[2][0] / scale[2], r[2][1] / scale[2], r[2][2] / scale[2] };
    yaw = atan2f(f[0], f[2]);
    pitch = asinf(-f[1]);
    /* sy = OFY + H (R1 w + t1) / (R2 w + t2) over the display height maps to
     * the view rect; X has its own focal length. */
    float display_w = pc_gl_display[2] > 0 ? (float)pc_gl_display[2] : 320;
    float display_h = pc_gl_display[3] > 0 ? (float)pc_gl_display[3] : 240;
    float focal_y = h * scale[1] / scale[2];
    fov_y = 2 * atanf(display_h / (2 * focal_y)) * 180 / 3.14159265f;
    stretch = scale[0] / scale[1] * (display_h * 4) / (display_w * 3);
    centre[0] = 2 * ox / display_w - 1;
    centre[1] = -(2 * oy / display_h - 1);
    for (int k = 0; k < 3; k++)
        pivot[k] = eye[k] + f[k] * 1500;
    grid_height = 0;
}

/* The photo mode's camera (photo.cpp, rood.camera): the live free camera driven by a plugin while the
 * game is paused. It borrows the editor's camera (eye, yaw, pitch, the projection) for its time and gives
 * it back as it was; the editor itself (active, its panel) is not turned on. Roll and the field of view
 * are the game's own: the roll the view matrix takes (vs_scratch.camera.angles.vz) and the projection
 * distance (vs_battle_setProjectionDistance), set in func_8007ACB0's hook each frame (the game's state
 * of a render is rewound after it, photo.cpp). */
bool photo;
float photo_roll;          /* degrees */
float photo_fov;           /* degrees of the vertical field of view; 0: the game's */
float photo_game_fov = 25; /* the game camera's when the photo camera began */
int photo_game_h;          /* its projection distance (vs_main_projectionDistance) */
struct EditorCamera {
    bool active, live;
    float eye[3], yaw, pitch, fov_y, pivot[3], stretch, centre[2];
} editor_saved;
} // namespace

extern "C" int vs_main_projectionDistance __attribute__((weak));
extern "C" void vs_battle_setProjectionDistance(int) __attribute__((weak));

extern "C" int pc_editor_active(void) { return active; }
extern "C" int pc_editor_flying(void) { return active && flying; }
/* photo.cpp: 0 without the hardware renderer (the live camera needs it). */
extern "C" int pc_editor_photo_begin(void)
{
    if (!pc_hw_active)
        return 0;
    if (photo)
        return 1;
    editor_saved.active = active, editor_saved.live = live;
    memcpy(editor_saved.eye, eye, sizeof eye);
    editor_saved.yaw = yaw, editor_saved.pitch = pitch, editor_saved.fov_y = fov_y;
    memcpy(editor_saved.pivot, pivot, sizeof pivot);
    editor_saved.stretch = stretch;
    memcpy(editor_saved.centre, centre, sizeof centre);
    from_game_camera();
    photo_game_fov = fov_y;
    photo_game_h = &vs_main_projectionDistance ? vs_main_projectionDistance : 0;
    photo_roll = 0, photo_fov = 0;
    photo = true;
    return 1;
}
extern "C" void pc_editor_photo_end(void)
{
    if (!photo)
        return;
    photo = false;
    active = editor_saved.active, live = editor_saved.live;
    memcpy(eye, editor_saved.eye, sizeof eye);
    yaw = editor_saved.yaw, pitch = editor_saved.pitch, fov_y = editor_saved.fov_y;
    memcpy(pivot, editor_saved.pivot, sizeof pivot);
    stretch = editor_saved.stretch;
    memcpy(centre, editor_saved.centre, sizeof centre);
}
/* The pose (radians; fov: degrees, the game's when none is set). */
extern "C" int pc_editor_photo_get(float out_eye[3], float* out_yaw, float* out_pitch, float* roll, float* fov)
{
    if (!photo)
        return 0;
    memcpy(out_eye, eye, sizeof eye);
    *out_yaw = yaw, *out_pitch = pitch;
    *roll = photo_roll;
    *fov = photo_fov > 0 ? photo_fov : photo_game_fov;
    return 1;
}
extern "C" void pc_editor_photo_set(const float in_eye[3], float in_yaw, float in_pitch, float roll, float fov)
{
    if (!photo)
        return;
    memcpy(eye, in_eye, sizeof eye);
    yaw = in_yaw;
    pitch = in_pitch > 1.55f ? 1.55f : in_pitch < -1.55f ? -1.55f : in_pitch;
    photo_roll = roll;
    photo_fov = fabsf(fov - photo_game_fov) < 1e-3f ? 0 : fov;
}
/* The separate renderer draws the view (not the game). */
extern "C" int pc_editor_separate(void) { return active && !live; }

/* func_8007ACB0: the free camera's position and a target ahead of it, in
 * the game's units (world units << 12), for the live camera. */
struct PsxVector {
    int32_t vx, vy, vz, pad;
};
extern "C" int pc_live_camera_active(void) { return (active && live) || photo; }

/* The cutaway's focus: fixed over RPC (editor_view focus), else the selected
 * light or glow, else Ashley (the middle of her body). */
static bool focus_fixed;
static float focus_point[3];
static bool cutaway_focus(float out[3])
{
    float radius;
    if (focus_fixed) {
        memcpy(out, focus_point, sizeof focus_point);
        return true;
    }
    if (pc_lights_selected_position(out, &radius))
        return true;
    if (&pc_dev_state) {
        struct pc_dev_state s;
        memset(&s, 0, sizeof s);
        pc_dev_state(&s);
        if (s.valid && s.has_player) {
            out[0] = (float)s.x, out[1] = (float)s.y - 80, out[2] = (float)s.z;
            return true;
        }
    }
    return false;
}
extern "C" int pc_editor_cutaway_focus(float out[3]) { return cutaway_focus(out); }

/* Is the focus hidden from the eye? Five rays (to the focus and to four
 * points a little around it, across the line of sight) against the room
 * triangles the view draws: front faces of shown groups, opaque; hits in
 * the last stretch before the focus are its own fixture (a lantern's cage, a
 * candle's niche) and do not count. Hidden when most rays are blocked. */
namespace {
struct CutTriangle {
    float a[3], e1[3], e2[3], lo[3], hi[3];
    uint16_t group;
    uint32_t flags;
    bool opaque;
};
std::vector<CutTriangle> cut_triangles;
int cut_serial = -1, cut_count = -1;
int cutaway_hidden; /* after hysteresis */
float cutaway_amount; /* the cut faded in (0..1) */
int hidden_frames, visible_frames;

bool focus_hidden(const float from[3], const float focus[3])
{
    int count;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    if (pc_scene_serial() != cut_serial || count != cut_count) {
        cut_serial = pc_scene_serial(), cut_count = count;
        cut_triangles.resize(count);
        for (int t = 0; t < count; t++) {
            CutTriangle& c = cut_triangles[t];
            const float *a = tris[t].vertex[0].position, *b = tris[t].vertex[1].position, *d = tris[t].vertex[2].position;
            for (int k = 0; k < 3; k++) {
                c.a[k] = a[k], c.e1[k] = b[k] - a[k], c.e2[k] = d[k] - a[k];
                c.lo[k] = fminf(a[k], fminf(b[k], d[k]));
                c.hi[k] = fmaxf(a[k], fmaxf(b[k], d[k]));
            }
            c.group = tris[t].group;
            c.flags = tris[t].flags;
            c.opaque = !(tris[t].code & 2);
        }
    }
    if (!count)
        return false;
    static pc_room_group_info groups[4096];
    int group_count = pc_room_editor_groups(groups, 4096);
    /* Front faces as the PS1 tests them: the game camera's handedness. */
    float gr[3][3], gt[3], h, ox, oy, facing_sign = 1;
    if (pc_scene_camera_get(gr, gt, &h, &ox, &oy)) {
        float det = gr[0][0] * (gr[1][1] * gr[2][2] - gr[1][2] * gr[2][1]) - gr[0][1] * (gr[1][0] * gr[2][2] - gr[1][2] * gr[2][0])
            + gr[0][2] * (gr[1][0] * gr[2][1] - gr[1][1] * gr[2][0]);
        facing_sign = det < 0 ? -1.0f : 1.0f;
    }
    /* The rays' ends: the focus, and 4 points 12 units around it. */
    float d[3] = { focus[0] - from[0], focus[1] - from[1], focus[2] - from[2] };
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist < 32)
        return false;
    float f[3] = { d[0] / dist, d[1] / dist, d[2] / dist }, side[3], up[3];
    float ref[3] = { 0, fabsf(f[1]) > 0.9f ? 0.0f : 1.0f, fabsf(f[1]) > 0.9f ? 1.0f : 0.0f };
    side[0] = f[1] * ref[2] - f[2] * ref[1], side[1] = f[2] * ref[0] - f[0] * ref[2], side[2] = f[0] * ref[1] - f[1] * ref[0];
    float sl = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    for (int k = 0; k < 3; k++)
        side[k] /= sl;
    up[0] = f[1] * side[2] - f[2] * side[1], up[1] = f[2] * side[0] - f[0] * side[2], up[2] = f[0] * side[1] - f[1] * side[0];
    const int rays = 5;
    float end[rays][3], dir[rays][3], len[rays];
    static const float offsets[rays][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    float lo[3], hi[3];
    for (int k = 0; k < 3; k++)
        lo[k] = hi[k] = from[k];
    for (int r = 0; r < rays; r++) {
        float l = 0;
        for (int k = 0; k < 3; k++) {
            end[r][k] = focus[k] + (side[k] * offsets[r][0] + up[k] * offsets[r][1]) * 12;
            dir[r][k] = end[r][k] - from[k];
            l += dir[r][k] * dir[r][k];
            lo[k] = fminf(lo[k], end[r][k]), hi[k] = fmaxf(hi[k], end[r][k]);
        }
        len[r] = sqrtf(l);
        for (int k = 0; k < 3; k++)
            dir[r][k] /= len[r];
    }
    /* The focus's own surroundings: the last 24 units (or 5%). */
    float skip = fmaxf(24.0f, dist * 0.05f);
    bool blocked[rays] = {};
    int blocked_count = 0;
    for (int t = 0; t < count && blocked_count < rays; t++) {
        const CutTriangle& c = cut_triangles[t];
        if (!c.opaque || (c.flags & 0x1100) || c.hi[0] < lo[0] || c.lo[0] > hi[0] || c.hi[1] < lo[1] || c.lo[1] > hi[1]
            || c.hi[2] < lo[2] || c.lo[2] > hi[2])
            continue;
        if (c.group < group_count) {
            const pc_room_group_info& g = groups[c.group];
            if (g.hidden || g.cutaway || (g.black && pc_editor_hide_masks))
                continue;
        }
        float n[3] = { c.e1[1] * c.e2[2] - c.e1[2] * c.e2[1], c.e1[2] * c.e2[0] - c.e1[0] * c.e2[2],
            c.e1[0] * c.e2[1] - c.e1[1] * c.e2[0] };
        float to[3] = { c.a[0] - from[0], c.a[1] - from[1], c.a[2] - from[2] };
        if (facing_sign * (n[0] * to[0] + n[1] * to[1] + n[2] * to[2]) <= 0)
            continue; /* a back face: the PS1 does not draw it */
        for (int r = 0; r < rays; r++) {
            if (blocked[r])
                continue;
            const float* v = dir[r];
            float p[3] = { v[1] * c.e2[2] - v[2] * c.e2[1], v[2] * c.e2[0] - v[0] * c.e2[2], v[0] * c.e2[1] - v[1] * c.e2[0] };
            float det = c.e1[0] * p[0] + c.e1[1] * p[1] + c.e1[2] * p[2];
            if (fabsf(det) < 1e-8f)
                continue;
            float inv = 1 / det, tv[3] = { from[0] - c.a[0], from[1] - c.a[1], from[2] - c.a[2] };
            float u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv;
            if (u < 0 || u > 1)
                continue;
            float q[3] = { tv[1] * c.e1[2] - tv[2] * c.e1[1], tv[2] * c.e1[0] - tv[0] * c.e1[2], tv[0] * c.e1[1] - tv[1] * c.e1[0] };
            float w = (v[0] * q[0] + v[1] * q[1] + v[2] * q[2]) * inv;
            if (w < 0 || u + w > 1)
                continue;
            float hit = (c.e2[0] * q[0] + c.e2[1] * q[1] + c.e2[2] * q[2]) * inv;
            if (hit > 1 && hit < len[r] - skip) {
                blocked[r] = true;
                ++blocked_count;
            }
        }
    }
    return blocked_count >= 3;
}
/* Once per game frame: hidden after 2 frames in a row, seen again after 10
 * (no flicker at a wall's edge), and the cut fading over 5 frames. */
void update_cutaway(bool has_focus, const float focus[3])
{
    bool want = false;
    if (has_focus && pc_editor_cutaway) {
        bool hidden = !pc_editor_cutaway_auto || focus_hidden(eye, focus);
        hidden_frames = hidden ? hidden_frames + 1 : 0;
        visible_frames = hidden ? 0 : visible_frames + 1;
        if (hidden_frames >= 2 || !pc_editor_cutaway_auto)
            cutaway_hidden = 1;
        else if (visible_frames >= 10)
            cutaway_hidden = 0;
        want = cutaway_hidden;
    } else {
        cutaway_hidden = 0;
        hidden_frames = visible_frames = 0;
    }
    float target = want ? 1.0f : 0.0f;
    cutaway_amount += fmaxf(-0.2f, fminf(0.2f, target - cutaway_amount));
}
} // namespace
extern "C" int pc_editor_cutaway_state(int* hidden, float* amount)
{
    if (hidden)
        *hidden = cutaway_hidden;
    if (amount)
        *amount = (active && live) || photo ? cutaway_amount : 0;
    return (active && live) || photo;
}
/* RPC editor_view: a fixed focus (fixed 1) or the automatic one (0). */
extern "C" void pc_editor_set_focus(int fixed, const float point[3])
{
    focus_fixed = fixed != 0;
    if (fixed)
        memcpy(focus_point, point, sizeof focus_point);
}

extern "C" int pc_live_camera(PsxVector* position, PsxVector* look_at)
{
    if ((!active || !live) && !photo) {
        if (render_cutaway)
            render_cutaway(0, nullptr, 0);
        cutaway_amount = 0;
        cutaway_hidden = hidden_frames = visible_frames = 0;
        return 0;
    }
    float focus[3];
    bool has_focus = cutaway_focus(focus);
    update_cutaway(has_focus, focus);
    if (render_cutaway)
        render_cutaway(has_focus, focus, cutaway_amount);
    static int told;
    if (!told++)
        fprintf(stderr, "[editor] live camera drives the game's view\n");
    float f[3];
    forward_vector(f);
    position->vx = (int32_t)(eye[0] * 4096), position->vy = (int32_t)(eye[1] * 4096), position->vz = (int32_t)(eye[2] * 4096);
    look_at->vx = (int32_t)((eye[0] + f[0] * 1024) * 4096);
    look_at->vy = (int32_t)((eye[1] + f[1] * 1024) * 4096);
    look_at->vz = (int32_t)((eye[2] + f[2] * 1024) * 4096);
    if (photo) {
        /* The roll func_8007ACB0 turns the view by next (vs_scratch.camera.angles.vz, 4096 a turn). */
        *(volatile int32_t*)(uintptr_t)(0x1F800000u + 0x34 + 0x20 + 8) = (int32_t)lroundf(photo_roll * 4096.0f / 360.0f);
        if (photo_fov > 0 && photo_game_h > 0 && vs_battle_setProjectionDistance) {
            float h = photo_game_h * tanf(photo_game_fov * 3.14159265f / 360.0f) / tanf(photo_fov * 3.14159265f / 360.0f);
            vs_battle_setProjectionDistance((int)lroundf(h < 16 ? 16 : h > 8192 ? 8192 : h));
        }
    }
    return 1;
}

extern "C" void pc_editor_toggle(void)
{
    if (!pc_hw_active)
        return;
    active = !active;
    if (active) {
        from_game_camera();
        int count;
        pc_scene_triangle_data(&count);
        fprintf(stderr, "[editor] view on: %d room triangles, eye %.0f %.0f %.0f, fov %.1f\n", count, eye[0],
            eye[1], eye[2], fov_y);
    }
    if (!live)
        pc_rpc_set_paused(active);
}

/* The editor's view for the lights tools. */
extern "C" int pc_editor_view(float view[16], float projection[16], float rect[4])
{
    if (!active || live) /* live: the tools use the game camera, which is ours */
        return 0;
    memcpy(view, last_view, sizeof last_view);
    memcpy(projection, last_projection, sizeof last_projection);
    memcpy(rect, last_rect, sizeof last_rect);
    return 1;
}
extern "C" int pc_editor_pick(float x, float y, float world[3], float normal[3])
{
    x -= last_rect[0], y -= last_rect[1];
    if (!active || live || x < 0 || y < 0 || x >= fbo_size[0] || y >= fbo_size[1])
        return 0;
    float p[4], q[4];
    int px = (int)x, py = fbo_size[1] - 1 - (int)y;
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT1);
    gl_ReadPixels(px, py, 1, 1, GL_RGBA, GL_FLOAT, p);
    if (p[3] < 0.5f) {
        gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }
    // Normal from neighbouring world positions (screen-space derivative).
    gl_ReadPixels(px + 1 < fbo_size[0] ? px + 1 : px - 1, py, 1, 1, GL_RGBA, GL_FLOAT, q);
    float r[4];
    gl_ReadPixels(px, py + 1 < fbo_size[1] ? py + 1 : py - 1, 1, 1, GL_RGBA, GL_FLOAT, r);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    float a[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] }, b[3] = { r[0] - p[0], r[1] - p[1], r[2] - p[2] };
    float n[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    float to_eye[3] = { eye[0] - p[0], eye[1] - p[1], eye[2] - p[2] };
    if (n[0] * to_eye[0] + n[1] * to_eye[1] + n[2] * to_eye[2] < 0)
        l = -l;
    for (int k = 0; k < 3; k++) {
        world[k] = p[k];
        normal[k] = fabsf(l) > 1e-6f ? n[k] / l : 0;
    }
    return 1;
}

extern "C" int pc_editor_handle_drag(void); /* editor_handles.cpp: a handle is dragged (Alt keeps a rect symmetric) */
/* Camera input (called each overlay frame while the editor is active). */
extern "C" void pc_editor_input(void)
{
    if (!active)
        return;
    ImGuiIO& io = ImGui::GetIO();
    float f[3], r[3], u[3];
    basis(f, r, u);
    bool over_ui = io.WantCaptureMouse || pc_ui_shell_wants_mouse(); /* or the modern menus over the game picture */
    /* Radians per pixel: the same hand movement turns as far on any screen. */
    float turn = look_sensitivity * 3.14159265f / 180.0f * 1080.0f / (io.DisplaySize.y > 1 ? io.DisplaySize.y : 1080.0f);
    float dt = io.DeltaTime > 0 ? io.DeltaTime : 1.0f / 60;
    /* While flying the pointer is hidden and held (relative mouse mode), so
     * looking is not stopped by the window or screen edge. */
    float mouse_dx = io.MouseDelta.x, mouse_dy = io.MouseDelta.y;
    pc_overlay_look_delta(&mouse_dx, &mouse_dy);
    flying = !over_ui && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    /* While a field is typed in, the keys are its text (no flying, no F). */
    bool typing = io.WantTextInput;
    if (flying) {
        yaw += mouse_dx * turn;
        pitch -= mouse_dy * turn;
        pitch = pitch > 1.55f ? 1.55f : pitch < -1.55f ? -1.55f : pitch;
        float move[3] = { 0, 0, 0 };
        auto add = [&](const float* v, float s) {
            for (int k = 0; k < 3; k++)
                move[k] += v[k] * s;
        };
        const float world_up[3] = { 0, -1, 0 };
        if (!typing) {
            if (ImGui::IsKeyDown(ImGuiKey_W)) add(f, 1);
            if (ImGui::IsKeyDown(ImGuiKey_S)) add(f, -1);
            if (ImGui::IsKeyDown(ImGuiKey_D)) add(r, 1);
            if (ImGui::IsKeyDown(ImGuiKey_A)) add(r, -1);
            if (ImGui::IsKeyDown(ImGuiKey_E)) add(world_up, 1);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) add(world_up, -1);
        }
        float boost = io.KeyShift ? 3.0f : 1.0f;
        for (int k = 0; k < 3; k++)
            eye[k] += move[k] * speed * boost * dt;
        if (io.MouseWheel != 0) /* as in UE: the wheel while flying sets the speed */
            speed = fmaxf(50, fminf(8000, speed * (io.MouseWheel > 0 ? 1.25f : 0.8f)));
    }
    if (!over_ui && io.KeyAlt && !pc_editor_handle_drag() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        // Orbit about the pivot: rotate the eye around it, keep looking at it.
        float d[3] = { eye[0] - pivot[0], eye[1] - pivot[1], eye[2] - pivot[2] };
        float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        yaw += mouse_dx * turn;
        pitch -= mouse_dy * turn;
        pitch = pitch > 1.55f ? 1.55f : pitch < -1.55f ? -1.55f : pitch;
        forward_vector(f);
        for (int k = 0; k < 3; k++)
            eye[k] = pivot[k] - f[k] * distance;
    }
    if (!over_ui && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0)) {
        float scale = speed * 0.002f;
        for (int k = 0; k < 3; k++) {
            float delta = -r[k] * mouse_dx * scale + u[k] * mouse_dy * scale;
            eye[k] += delta;
            pivot[k] += delta;
        }
    }
    float focus[3], radius;
    if (!typing && !ImGui::IsAnyItemActive() && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)
        && pc_lights_selected_position(focus, &radius)) {
        float distance = fmaxf(radius * 1.2f, 300);
        memcpy(pivot, focus, sizeof pivot);
        for (int k = 0; k < 3; k++)
            eye[k] = focus[k] - f[k] * distance;
    }
}

/* Editor panel. */
extern "C" void pc_editor_panel(void)
{
    ImGui::SeparatorText("Free camera");
    bool on = active;
    if (ImGui::Checkbox("Free camera (F2, or right-drag in the view)", &on))
        pc_editor_toggle();
    bool separate = !live;
    /* The legacy separate renderer is OpenGL only; on Vulkan the free camera
     * always drives the live view (which has everything it had: GI, haze,
     * picking, gizmos). */
    bool vulkan = pc_vk_on() != 0;
    if (vulkan && !live) {
        if (active)
            pc_editor_toggle();
        live = true;
        separate = false;
    }
    if (vulkan)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox(vulkan ? "Separate renderer (OpenGL only; legacy)" : "Separate renderer (pauses the game; legacy)",
            &separate)) {
        if (active)
            pc_editor_toggle();
        live = !separate;
    }
    if (vulkan)
        ImGui::EndDisabled();
    if (!active) {
        ImGui::TextDisabled("The free camera drives the game's own live rendering; Play returns to the game camera.");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to game camera"))
        from_game_camera();
    ImGui::SameLine();
    if (ImGui::Button("Centre projection"))
        centre[0] = centre[1] = 0, stretch = 1;
    ImGui::SliderFloat("Field of view", &fov_y, 10, 100, "%.0f deg");
    ImGui::SliderFloat("Fly speed", &speed, 50, 8000, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Look sensitivity", &look_sensitivity, 0.02f, 0.5f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Hide cutaway walls", &hide_cutaway);
    ImGui::SameLine();
    ImGui::Checkbox("Characters", &show_actors);
    ImGui::SameLine();
    ImGui::Checkbox("Textured", &textured_actors);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &show_grid);
    if (show_grid)
        ImGui::DragFloat("Grid height", &grid_height, 4, -4096, 4096, "%.0f");
    ImGui::TextDisabled("RMB+mouse look, RMB+WASD/QE fly (Shift faster), RMB+wheel speed,\n"
                        "wheel dolly, Alt+LMB orbit, MMB pan, F focus selected light.");
}

/* Draw the editor view over the whole window (called by pc_gl_present). */
extern "C" void pc_editor_render(int window_w, int window_h)
{
    if (!init() || window_w <= 0 || window_h <= 0)
        return;
    upload_scene();
    upload_actors();
    // The game's view rect (letterboxed like the game), else the whole window.
    int rx = 0, ry = 0, rw = window_w, rh = window_h;
    if (pc_gl_view[2] > 0 && pc_gl_view[3] > 0 && pc_gl_view[0] + pc_gl_view[2] <= window_w
        && pc_gl_view[1] + pc_gl_view[3] <= window_h)
        rx = pc_gl_view[0], ry = pc_gl_view[1], rw = pc_gl_view[2], rh = pc_gl_view[3];
    ensure_target(rw, rh);
    float view[16], projection[16];
    matrices((float)rw / rh, view, projection);
    memcpy(last_view, view, sizeof view);
    memcpy(last_projection, projection, sizeof projection);
    last_rect[0] = (float)rx, last_rect[1] = (float)ry, last_rect[2] = (float)rw, last_rect[3] = (float)rh;
    float vp[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += projection[k * 4 + r] * view[c * 4 + k];
            vp[c * 4 + r] = s;
        }
    // Lights and their shadow cubes, in world space.
    static float lp[16][4], lc[16][4], ld[16][4], lr[16][4], lu[16][4];
    static int wants[16], slots[16];
    int count = pc_lights_world(lp, lc, ld, lr, lu, wants, 16);

    GLuint vram = pc_hw_native_texture();
    unsigned facing = pc_scene_facing();
    static const float sky[4] = { 0.06f, 0.06f, 0.08f, 1 }, none[4] = { 0, 0, 0, 0 }, far_depth = 1;
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_Viewport(0, 0, rw, rh);
    gl_Disable(GL_BLEND);
    gl_Disable(GL_SCISSOR_TEST);
    gl_UseProgram(program);
    gl_UniformMatrix4fv(u_view_projection, 1, GL_FALSE, vp);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, vram);
    gl_Uniform1i(u_vram, 0);
    gl_Uniform3f(u_camera, eye[0], eye[1], eye[2]);
    gl_Uniform1i(u_gi_on, 0);
    // Path tracing (ptgi.c) first: a prepass writes the surfaces it starts from.
    GLuint gi = 0, gi_position = 0, gi_normal = 0;
    int gi_size[2] = { 1, 1 };
    if (pc_gl_compute && (pc_gi_enabled || pc_rt_shadows || pc_baked_shadows)) {
        static const GLenum surfaces[3] = { GL_NONE, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        gl_DrawBuffers(3, surfaces);
        gl_ClearBufferfv(GL_COLOR, 1, none);
        gl_ClearBufferfv(GL_COLOR, 2, none);
        gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
        gl_Enable(GL_DEPTH_TEST);
        gl_DepthFunc(GL_LESS);
        gl_DepthMask(GL_TRUE);
        gl_Uniform1i(u_prepass, 1);
        gl_Uniform1i(u_textured, 2);
        gl_Uniform1ui(u_facing, hide_cutaway && !(facing >> 16) ? facing & 255 : 0);
        gl_BindVertexArray(vertex_array);
        gl_DrawArrays(GL_TRIANGLES, 0, vertex_count);
        if (show_actors && actor_vertices) {
            gl_Uniform1i(u_textured, textured_actors ? 1 : 0);
            gl_Uniform1ui(u_facing, 0);
            gl_BindVertexArray(actor_array);
            gl_DrawArrays(GL_TRIANGLES, 0, actor_vertices);
        }
        gl_BindVertexArray(0);
        gl_Disable(GL_DEPTH_TEST);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        float gi_scale = pc_gi_resolution * 0.25f;
        gi = pc_gi_update_world(position_texture, normal_texture, rw, rh, gi_scale > 1 ? 1 : gi_scale, vp, eye,
            &gi_position, &gi_normal, gi_size);
        gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl_Viewport(0, 0, rw, rh);
        gl_UseProgram(program);
    }
    if (gi && pc_rt_shadows) {
        for (int i = 0; i < count; i++)
            slots[i] = wants[i] ? -2 : -1; // traced visibility, not cube maps
    } else {
        pc_shadows_update(count, lp, wants, slots);
        gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl_Viewport(0, 0, rw, rh);
        gl_UseProgram(program);
    }
    for (int i = count; i < 16; i++)
        slots[i] = -1;
    gl_Uniform1i(u_prepass, 0);
    gl_Uniform1i(u_gi_on, gi ? (pc_gi_enabled ? (pc_gi_debug == 1 ? 2 : 1) : 0) | (pc_rt_shadows ? 4 : 0)
            | (pc_gi_debug == 2 ? 8 : 0) | (pc_baked_shadows_active ? 16 : 0) | (pc_gi_debug == 3 ? 32 : 0) : 0);
    gl_Uniform1f(u_gi_strength, pc_gi_strength);
    gl_Uniform1f(u_fog_density, pc_lighting_traced_haze_density()); /* lighting quality: haze High */
    {
        float lo[3], hi[3];
        pc_gi_room_bounds(lo, hi);
        gl_Uniform3f(u_room_lo, lo[0], lo[1], lo[2]);
        gl_Uniform3f(u_room_hi, hi[0], hi[1], hi[2]);
    }
    gl_Uniform2f(u_target_size, (float)rw, (float)rh);
    gl_Uniform2i(u_gi_size, gi_size[0], gi_size[1]);
    GLuint gi_textures[4] = { gi, gi_position, gi_normal, gi ? pc_gi_extra(1) : 0 };
    GLint gi_uniforms[4] = { u_gi_tex, u_gi_position, u_gi_normal, u_gi_extra };
    for (int i = 0; i < 4; i++) {
        gl_ActiveTexture(GL_TEXTURE8 + i);
        gl_BindTexture(GL_TEXTURE_2D, gi_textures[i]);
        gl_Uniform1i(gi_uniforms[i], 8 + i);
    }
    gl_ActiveTexture(GL_TEXTURE0);
    static const GLenum buffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    gl_DrawBuffers(3, buffers);
    gl_ClearBufferfv(GL_COLOR, 0, sky);
    gl_ClearBufferfv(GL_COLOR, 1, none);
    gl_ClearBufferfv(GL_COLOR, 2, none);
    gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
    gl_Enable(GL_DEPTH_TEST);
    gl_DepthFunc(GL_LESS);
    gl_DepthMask(GL_TRUE);
    gl_Disable(GL_BLEND);
    gl_Disable(GL_SCISSOR_TEST);
    gl_Uniform3f(u_camera, eye[0], eye[1], eye[2]);
    gl_Uniform1i(u_light_count, count);
    gl_Uniform4fv(u_light_pos, 16, &lp[0][0]);
    gl_Uniform4fv(u_light_col, 16, &lc[0][0]);
    gl_Uniform4fv(u_light_dir, 16, &ld[0][0]);
    gl_Uniform4fv(u_light_right, 16, &lr[0][0]);
    gl_Uniform4fv(u_light_up, 16, &lu[0][0]);
    gl_Uniform1iv(u_light_shadow, 16, slots);
    gl_Uniform1f(u_softness, pc_shadow_softness);
    gl_Uniform1f(u_baked, pc_lights_enabled ? pc_lights_baked : 1.0f); /* Original: the PS1 baked light */
    gl_Uniform1f(u_semi_scale, 1);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, vram);
    gl_Uniform1i(u_vram, 0);
    pc_shadows_bind(4);
    for (int i = 0; i < 4; i++)
        gl_Uniform1i(u_shadow[i], 4 + i);
    gl_Uniform1i(u_textured, 2);
    gl_Uniform1ui(u_facing, hide_cutaway && !(facing >> 16) ? facing & 255 : 0);
    gl_BindVertexArray(vertex_array);
    gl_DrawArrays(GL_TRIANGLES, 0, vertex_count);
    if (show_actors && actor_vertices) {
        gl_Uniform1i(u_textured, textured_actors ? 1 : 0);
        gl_Uniform1ui(u_facing, 0);
        gl_BindVertexArray(actor_array);
        gl_DrawArrays(GL_TRIANGLES, 0, actor_vertices);
    }
    // Translucent room surfaces: unlit, blended as the PS1 does, no depth
    // writes, and not pickable.
    gl_Uniform1i(u_textured, 2);
    gl_Uniform1ui(u_facing, hide_cutaway && !(facing >> 16) ? facing & 255 : 0);
    gl_Uniform1i(u_light_count, 0);
    gl_Uniform1i(u_gi_on, 0);
    gl_BindVertexArray(vertex_array);
    gl_DepthMask(GL_FALSE);
    gl_ColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gl_ColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gl_Enable(GL_BLEND);
    for (int mode = 0; mode < 4; mode++) {
        if (!semi_count[mode])
            continue;
        gl_BlendEquation(mode == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        if (mode == 0)
            gl_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        else
            gl_BlendFunc(GL_ONE, GL_ONE);
        gl_Uniform1f(u_semi_scale, mode == 3 ? 0.25f : 1.0f);
        gl_DrawArrays(GL_TRIANGLES, semi_first[mode], semi_count[mode]);
    }
    gl_Disable(GL_BLEND);
    gl_BlendEquation(GL_FUNC_ADD);
    gl_ColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_ColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_DepthMask(GL_TRUE);
    gl_BindVertexArray(0);
    gl_Disable(GL_DEPTH_TEST);
    // Present: blit the colour attachment to the window.
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl_Viewport(0, 0, window_w, window_h);
    gl_ClearColor(0, 0, 0, 1);
    gl_Clear(GL_COLOR_BUFFER_BIT);
    int gy = window_h - ry - rh; // GL rows count from the bottom
    gl_BlitFramebuffer(0, 0, rw, rh, rx, gy, rx + rw, gy + rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl_Viewport(0, 0, window_w, window_h);
}

/* Floor grid (overlay frame, after ImGuizmo::BeginFrame). */
extern "C" void pc_editor_grid(void)
{
    if (!active || live || !show_grid)
        return;
    /* ImGuizmo draws one line per unit out to the given size: scale the
     * matrix so lines are 64 world units apart, 64 lines each way. */
    const float step = 64;
    float grid[16] = { step, 0, 0, 0, 0, step, 0, 0, 0, 0, step, 0, 0, grid_height, 0, 1 };
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(last_rect[0], last_rect[1], last_rect[2], last_rect[3]);
    ImGuizmo::DrawGrid(last_view, last_projection, grid, 64.0f);
}

/* RPC editor_camera: read or set the free camera (world units, degrees);
 * setting it turns the free camera on. Returns 1 when it is active. */
extern "C" int pc_editor_camera(int set, float position[3], float* yaw_degrees, float* pitch_degrees)
{
    if (set) {
        if (!active)
            pc_editor_toggle();
        memcpy(eye, position, sizeof eye);
        yaw = *yaw_degrees * 3.14159265f / 180;
        pitch = *pitch_degrees * 3.14159265f / 180;
    } else {
        memcpy(position, eye, sizeof eye);
        *yaw_degrees = yaw * 180 / 3.14159265f;
        *pitch_degrees = pitch * 180 / 3.14159265f;
    }
    return active;
}
