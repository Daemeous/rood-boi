/* Native 3D submission: meshes, draw lists and their OpenGL drawing into the
 * hardware rasterizer's scaled VRAM target (render.h; design notes in
 * pc/docs/Native_Rendering.md).
 *
 * A mesh is a vertex buffer of PS1 polygons, four vertices each (triangles
 * repeat their last), drawn as GL_LINES_ADJACENCY so a geometry shader sees
 * the whole polygon. The vertex stage moves each vertex into GTE view space
 * through its bone's model-view transform (one per instance for the room, a
 * palette for characters); the geometry stage applies the PS1's per-polygon
 * rules on real view-space positions (back faces, the ordering-table range,
 * the room's distance UV halving), then emits the polygon's triangles
 * projected exactly as the GTE projects (sx = OFX + H x / z, X * 3/4 in
 * widescreen) with real clipping and exact per-pixel depth (GTE SZ / 65536,
 * the encoding the rest of the frame and the present pass use). The fragment
 * stage samples native VRAM like gpu_hw.c's draw shader (nearest texels
 * through the CLUT and texture window, vertex colour modulation, optional
 * bilinear) and writes the same G-buffer: colour, view-space normal, albedo,
 * shade. */
#define _POSIX_C_SOURCE 200809L
#include "render_gl.h"
#include "room_editor.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ROOD_VULKAN
#include "vk_hooks.h" /* ROOD_RENDERER=vulkan: vk_render.inc, included at the end */
static int vk_draw_list(uint32_t id, const render_target* target);
static int vk_draw_translucent(uint32_t id, int first, int count, const render_target* target);
#endif

extern int pc_hw_active;
int pc_native_room = 2; /* native without the reference packets (VRAM readers sync from the target) */
int pc_native_actors = 2;
int pc_native_actor_order = 1;
/* Room lists resolve near-coplanar surfaces in the PS1's draw order
 * (render_draw_list): a decal on a table, a notice on a wall. 0: plain
 * depth test. The slack: how far behind the nearest room surface (relative,
 * plus GTE units) a polygon still counts as lying on it. */
int pc_native_room_order = 1;
float pc_native_room_slack[2] = { 0.004f, 2.0f };
int pc_native_occlusion = 1;
int pc_native_blend = 1;
int pc_native_objects = 1;
int pc_native_shadows = 1;
int pc_native_effects = 1;
int pc_reconstruct = 0;

/* ---- meshes ---- */

typedef struct {
    float position[3], normal[3];
    uint8_t r, g, b, code;
    uint8_t u, v, flags, bone;
    uint16_t tpage, clut;
    uint8_t bias, uv_effect, pad[2];
} mesh_vertex;
typedef struct {
    uint32_t generation; /* odd while live */
    GLuint vao, vbo;
    int polygons, triangles;
    render_polygon* cpu; /* the polygons (world-space triangles for the path tracer) */
} mesh_slot;
static mesh_slot* meshes;
static int mesh_count, live_meshes, live_polygons, mesh_updates;
static render_stats counting, finished; /* this frame's, the last finished frame's */

static mesh_slot* lookup(render_mesh mesh)
{
    uint32_t index = mesh & 0xffff, generation = mesh >> 16;
    if (!mesh || (int)index > mesh_count || !index)
        return NULL;
    mesh_slot* slot = &meshes[index - 1];
    return (slot->generation & 0xffff) == generation && (slot->generation & 1) ? slot : NULL;
}
static void face_normal(const render_polygon* p, float n[3])
{
    /* Newell's method: the plane normal of the 3 or 4 vertices, in their
     * winding (0, 1, 3, 2 around a quad). */
    static const int ring3[3] = { 0, 1, 2 }, ring4[4] = { 0, 1, 3, 2 };
    const int* ring = p->flags & RENDER_POLY_QUAD ? ring4 : ring3;
    int count = p->flags & RENDER_POLY_QUAD ? 4 : 3;
    n[0] = n[1] = n[2] = 0;
    for (int i = 0; i < count; i++) {
        const float *a = p->position[ring[i]], *b = p->position[ring[(i + 1) % count]];
        n[0] += (a[1] - b[1]) * (a[2] + b[2]);
        n[1] += (a[2] - b[2]) * (a[0] + b[0]);
        n[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (int k = 0; k < 3; k++)
        n[k] = length > 0 ? n[k] / length : 0;
}
/* Four vertices per polygon (triangles repeat their last). */
static mesh_vertex* mesh_vertices(const render_polygon* polygons, int count)
{
    mesh_vertex* vertices = calloc((size_t)count * 4, sizeof *vertices);
    if (!vertices)
        return NULL;
    for (int i = 0; i < count; i++) {
        const render_polygon* p = &polygons[i];
        float n[3] = { p->normal[0], p->normal[1], p->normal[2] };
        int sprite = (p->flags & RENDER_POLY_SPRITE) != 0;
        if (!sprite && n[0] == 0 && n[1] == 0 && n[2] == 0)
            face_normal(p, n);
        for (int k = 0; k < 4; k++) {
            int s = (p->flags & RENDER_POLY_QUAD) || k < 3 ? k : 2;
            mesh_vertex* v = &vertices[i * 4 + k];
            memcpy(v->position, p->position[sprite ? 0 : s], sizeof v->position);
            memcpy(v->normal, n, sizeof n);
            if (sprite) {
                /* The vertex's screen offset and the depth it was measured
                 * at, in place of the normal (sprites face the camera). */
                v->normal[0] = p->offset[s][0];
                v->normal[1] = p->offset[s][1];
                v->normal[2] = p->offset_depth;
            }
            v->r = p->rgb[s][0], v->g = p->rgb[s][1], v->b = p->rgb[s][2];
            v->code = p->code;
            v->u = p->uv[s][0], v->v = p->uv[s][1];
            v->flags = p->flags;
            v->bone = p->bone[s];
            v->tpage = p->tpage, v->clut = p->clut;
            v->bias = p->bias;
            v->uv_effect = p->uv_effect;
        }
    }
    return vertices;
}
render_mesh render_mesh_create(const render_polygon* polygons, int count)
{
    if (!pc_hw_active || count <= 0)
        return 0;
    for (int i = 0; i < count; i++)
        for (int k = 0; k < 4; k++)
            if (polygons[i].bone[k] >= RENDER_MAX_BONES)
                return 0;
    int index = 0;
    while (index < mesh_count && (meshes[index].generation & 1))
        ++index;
    if (index == mesh_count) {
        if (mesh_count == 0xffff)
            return 0;
        mesh_slot* grown = realloc(meshes, (size_t)(mesh_count + 1) * sizeof *meshes);
        if (!grown)
            return 0;
        meshes = grown;
        memset(&meshes[mesh_count++], 0, sizeof *meshes);
    }
    mesh_vertex* vertices = mesh_vertices(polygons, count);
    render_polygon* cpu = malloc((size_t)count * sizeof *cpu);
    if (!vertices || !cpu) {
        free(vertices);
        free(cpu);
        return 0;
    }
    memcpy(cpu, polygons, (size_t)count * sizeof *cpu);
    mesh_slot* slot = &meshes[index];
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        slot->vao = 0;
        slot->vbo = pc_vk_mesh_create(vertices, (size_t)count * 4 * sizeof *vertices);
        if (!slot->vbo) {
            free(vertices);
            free(cpu);
            return 0;
        }
        goto created;
    }
#endif
    gl_GenVertexArrays(1, &slot->vao);
    gl_GenBuffers(1, &slot->vbo);
    gl_BindVertexArray(slot->vao);
    gl_BindBuffer(GL_ARRAY_BUFFER, slot->vbo);
    gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * 4 * sizeof *vertices), vertices, GL_STATIC_DRAW);
    gl_EnableVertexAttribArray(0);
    gl_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex), (void*)offsetof(mesh_vertex, position));
    gl_EnableVertexAttribArray(1);
    gl_VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex), (void*)offsetof(mesh_vertex, normal));
    gl_EnableVertexAttribArray(2);
    gl_VertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(mesh_vertex), (void*)offsetof(mesh_vertex, r));
    gl_EnableVertexAttribArray(3);
    gl_VertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, sizeof(mesh_vertex), (void*)offsetof(mesh_vertex, u));
    gl_EnableVertexAttribArray(4);
    gl_VertexAttribIPointer(4, 2, GL_UNSIGNED_SHORT, sizeof(mesh_vertex), (void*)offsetof(mesh_vertex, tpage));
    gl_EnableVertexAttribArray(5);
    gl_VertexAttribIPointer(5, 1, GL_UNSIGNED_BYTE, sizeof(mesh_vertex), (void*)offsetof(mesh_vertex, bias));
    gl_EnableVertexAttribArray(6);
    gl_VertexAttribIPointer(6, 1, GL_UNSIGNED_BYTE, sizeof(mesh_vertex), (void*)offsetof(mesh_vertex, uv_effect));
    gl_BindVertexArray(0);
    gl_BindBuffer(GL_ARRAY_BUFFER, 0);
#ifdef ROOD_VULKAN
created:
#endif
    free(vertices);
    slot->generation = ((slot->generation + 1) | 1) & 0xffff;
    slot->polygons = count;
    slot->cpu = cpu;
    slot->triangles = 0;
    for (int i = 0; i < count; i++)
        slot->triangles += polygons[i].flags & RENDER_POLY_QUAD ? 2 : 1;
    ++live_meshes;
    live_polygons += count;
    return (slot->generation << 16) | (uint32_t)(index + 1);
}
/* New contents for a live mesh with the same polygon count (the game
 * rewrote its data: room texture animations change UVs in place). The
 * handle stays valid; on Vulkan the vertices move to a new range and the
 * old one is freed once the frames in flight that read it are done. */
int render_mesh_update(render_mesh mesh, const render_polygon* polygons, int count)
{
    mesh_slot* slot = lookup(mesh);
    if (!slot || count != slot->polygons)
        return 0;
    for (int i = 0; i < count; i++)
        for (int k = 0; k < 4; k++)
            if (polygons[i].bone[k] >= RENDER_MAX_BONES)
                return 0;
    mesh_vertex* vertices = mesh_vertices(polygons, count);
    if (!vertices)
        return 0;
    size_t bytes = (size_t)count * 4 * sizeof *vertices;
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        uint32_t vbo = pc_vk_mesh_create(vertices, bytes);
        free(vertices);
        if (!vbo)
            return 0;
        pc_vk_mesh_destroy(slot->vbo);
        slot->vbo = vbo;
        memcpy(slot->cpu, polygons, (size_t)count * sizeof *slot->cpu);
        ++mesh_updates;
        return 1;
    }
#endif
    gl_BindBuffer(GL_ARRAY_BUFFER, slot->vbo);
    gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, vertices, GL_STATIC_DRAW);
    gl_BindBuffer(GL_ARRAY_BUFFER, 0);
    free(vertices);
    memcpy(slot->cpu, polygons, (size_t)count * sizeof *slot->cpu);
    ++mesh_updates;
    return 1;
}
void render_mesh_destroy(render_mesh mesh)
{
    mesh_slot* slot = lookup(mesh);
    if (!slot)
        return;
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        pc_vk_mesh_destroy(slot->vbo);
    else
#endif
    {
        gl_DeleteBuffers(1, &slot->vbo);
        gl_DeleteVertexArrays(1, &slot->vao);
    }
    slot->vao = slot->vbo = 0;
    free(slot->cpu);
    slot->cpu = NULL;
    slot->generation = (slot->generation + 1) & 0xffff; /* even: free */
    --live_meshes;
    live_polygons -= slot->polygons;
}
/* Transient meshes: destroyed once no replayable list can name them (the
 * list ring keeps a few frames; replays redraw the newest). */
#define TRANSIENT_FRAMES 12
static struct {
    render_mesh mesh;
    uint32_t serial;
} *transient;
static int transient_count, transient_capacity;
static uint32_t frame_serial;
render_mesh render_mesh_transient(const render_polygon* polygons, int count)
{
    render_mesh mesh = render_mesh_create(polygons, count);
    if (!mesh)
        return 0;
    if (transient_count == transient_capacity) {
        int capacity = transient_capacity ? transient_capacity * 2 : 64;
        void* grown = realloc(transient, (size_t)capacity * sizeof *transient);
        if (!grown) {
            render_mesh_destroy(mesh);
            return 0;
        }
        transient = grown;
        transient_capacity = capacity;
    }
    transient[transient_count].mesh = mesh;
    transient[transient_count++].serial = frame_serial;
    return mesh;
}
static void expire_transient(void)
{
    int kept = 0;
    for (int i = 0; i < transient_count; i++) {
        if ((uint32_t)(frame_serial - transient[i].serial) > TRANSIENT_FRAMES)
            render_mesh_destroy(transient[i].mesh);
        else
            transient[kept++] = transient[i];
    }
    transient_count = kept;
}
int render_mesh_polygons(render_mesh mesh)
{
    mesh_slot* slot = lookup(mesh);
    return slot ? slot->polygons : -1;
}

/* ---- lists ----
 * A ring of recent lists: frame interpolation redraws the newest frame's
 * lists (with an interpolated camera and blended palettes) after the game
 * has begun the next. A frame has one room list and a list per drawn model,
 * so the ring holds several frames' worth. */

#define LIST_RING 1024
typedef struct {
    render_mesh mesh;
    int bias, subdivision_level;
    int first, bones; /* palette in the list's transforms; the previous one follows when has_previous */
    int has_previous;
    int override;
    uint8_t color[3];
    uint16_t tpage, clut;
    int blend;
    uint32_t owner; /* diagnostics (anim_trace) */
    /* Skeleton blending: each bone's parent as found in the two palettes
     * (SKELETON_ROOT: blended on its own). */
    uint8_t parent[RENDER_MAX_BONES];
    /* The bone of another instance of this frame that carries this one's
     * root (a weapon in a hand): list id (0: none), instance, bone. */
    uint32_t carrier_list;
    uint16_t carrier_instance;
    uint8_t carrier_bone;
    float blend_offset, blend_scale; /* render_instance's (scale 1 when 0) */
    float cut, cut_previous; /* render_instance's dithered share left out */
    int cut_ghost;
} list_instance;
typedef struct {
    uint32_t id;
    uint32_t serial; /* render_frame_serial() when begun: the game frame */
    render_camera camera;
    render_rules rules;
    int tint[4];
    list_instance* instances;
    int count, capacity;
    render_transform* transforms;
    int transform_count, transform_capacity;
    uint32_t* order; /* PS1 draw order: instance << 16 | polygon */
    int order_used, order_capacity;
    uint32_t* translucent; /* translucent sequence: instance << 16 | polygon */
    uint8_t* translucent_flags; /* RENDER_TRANSLUCENT_* per entry */
    int translucent_used, translucent_capacity;
    render_uv_effect uv_effects[RENDER_MAX_UV_EFFECTS]; /* animated materials (render_list_uv_effects) */
    int uv_effect_count;
} draw_list;
static draw_list lists[LIST_RING];
static uint32_t next_id = 1;
static GLuint program;
static int program_failed;

static draw_list* find_list(uint32_t id)
{
    draw_list* list = &lists[id % LIST_RING];
    return id && list->id == id ? list : NULL;
}
static int ensure_program(void);
int render_list_camera(uint32_t id, render_camera* out)
{
    draw_list* list = find_list(id);
    if (!list)
        return 0;
    *out = list->camera;
    return 1;
}
uint32_t render_list_begin(const render_camera* camera, const render_rules* rules)
{
    if (!pc_hw_active || !ensure_program())
        return 0;
    uint32_t id = next_id;
    next_id = (next_id + 1) & 0xffffff;
    if (!next_id)
        next_id = 1;
    draw_list* list = &lists[id % LIST_RING];
    list->id = id;
    list->serial = frame_serial;
    list->camera = *camera;
    list->rules = *rules;
    memset(list->tint, 0, sizeof list->tint);
    list->count = 0;
    list->transform_count = 0;
    list->order_used = 0;
    list->translucent_used = 0;
    list->uv_effect_count = 0;
    return id;
}
int pc_interp_uv = 1;
void render_list_uv_effects(uint32_t id, const render_uv_effect* effects, int count)
{
    draw_list* list = find_list(id);
    if (!list)
        return;
    if (count > RENDER_MAX_UV_EFFECTS)
        count = RENDER_MAX_UV_EFFECTS;
    list->uv_effect_count = count > 0 ? count : 0;
    if (count > 0)
        memcpy(list->uv_effects, effects, (size_t)count * sizeof *effects);
    counting.uv_effects += list->uv_effect_count;
}
/* The shaders' table of animated materials for a draw at blend t (entry 0:
 * none; entry i + 1: the list's effect i): integer offsets, the window
 * (base, size) and, while interpolating, the part of the offset still to
 * come (-(1 - t) * shift) with the wrap period. Returns the entries. */
typedef struct {
    int32_t offset[4]; /* u, v, -, - */
    int32_t window[4]; /* base u, base v, size u, size v */
    float shift[4]; /* fractional offset u, v (texels), period u, v */
} uv_effect_gpu;
extern int pc_hw_interpolate;
static int uv_effect_table(const draw_list* list, float t, uv_effect_gpu* out)
{
    memset(out, 0, sizeof *out);
    int n = list->uv_effect_count;
    float back = pc_interp_uv && t < 1.0f ? 1.0f - (t > 0 ? t : 0) : 0;
    int smooth = pc_interp_uv == 2 && pc_hw_interpolate;
    for (int i = 0; i < n; i++) {
        const render_uv_effect* e = &list->uv_effects[i];
        uv_effect_gpu* g = &out[i + 1];
        memset(g, 0, sizeof *g);
        for (int a = 0; a < 2; a++) {
            g->offset[a] = e->offset[a];
            g->window[a] = e->base[a];
            g->window[a + 2] = e->size[a];
            g->shift[a] = smooth ? e->lead[a] - back * e->smooth[a] : -back * e->shift[a];
            g->shift[a + 2] = e->period[a];
        }
    }
    return n + 1;
}
/* The offsets the room list's first animated material was drawn with, per
 * draw (RPC uv_effects "drawn": what frame interpolation does with them). */
#define UV_TRACE 512
static float uv_trace[UV_TRACE][4]; /* t, offset u + fraction, offset v + fraction, - */
static int uv_trace_count;
static void uv_trace_record(const draw_list* list, float t)
{
    if (!list->uv_effect_count || list->rules.objects || list->rules.layered || list->rules.effects)
        return;
    uv_effect_gpu table[2];
    draw_list one = *list; /* (only the first entry) */
    one.uv_effect_count = 1;
    uv_effect_table(&one, t, table);
    float* r = uv_trace[uv_trace_count++ % UV_TRACE];
    r[0] = t;
    r[1] = (float)table[1].offset[0] + table[1].shift[0];
    r[2] = (float)table[1].offset[1] + table[1].shift[1];
    r[3] = 0;
}
int render_uv_trace(float (*out)[4], int max)
{
    int n = uv_trace_count < UV_TRACE ? uv_trace_count : UV_TRACE;
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        memcpy(out[i], uv_trace[(uv_trace_count - n + i) % UV_TRACE], sizeof out[i]);
    uv_trace_count = 0;
    return n;
}
static render_transform* reserve_transforms(draw_list* list, int count)
{
    if (list->transform_count + count > list->transform_capacity) {
        int capacity = list->transform_capacity ? list->transform_capacity : 16;
        while (capacity < list->transform_count + count)
            capacity *= 2;
        render_transform* grown = realloc(list->transforms, (size_t)capacity * sizeof *grown);
        if (!grown)
            return NULL;
        list->transforms = grown;
        list->transform_capacity = capacity;
    }
    render_transform* out = &list->transforms[list->transform_count];
    list->transform_count += count;
    return out;
}
/* A palette entry at blend t (linear in the affine transform: each vertex
 * moves on the straight line between its two positions, as the packet
 * path's matched triangles did). */
static void blend_transform(const render_transform* a, const render_transform* b, float t, render_transform* out)
{
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++)
            out->rotation[i][k] = a->rotation[i][k] + (b->rotation[i][k] - a->rotation[i][k]) * t;
        out->translation[i] = a->translation[i] + (b->translation[i] - a->translation[i]) * t;
    }
}
/* ---- skeleton blending (frame interpolation of bone palettes) ----
 * Linear blending of the affine transforms shortens bones that turn fast
 * (a 96 degree sword swing in one game frame: 31% shorter halfway) and
 * moves joints on chords. Instead each bone's rotation (its matrix with the
 * column scales divided out) is slerped relative to its parent's, the
 * rest of the matrix (scales, fixed-point skew) lerped, and each joint is
 * carried by its blended parent
 * (its offset in the parent's frame lerped), so limbs swing on arcs at full
 * length. The game builds bone i at parent * (-length, 0, 0) with
 * Rz*Ry*Rx angles keyed linearly per 30 Hz tick (func_800B28A8,
 * func_800AFDE8); the hierarchy is not passed in but found in the data:
 * each bone's parent is the earlier bone in whose frame its offset stays
 * the same in both palettes. Both palettes are reproduced exactly at t = 0
 * and 1 whatever the parents found, since offsets and relative rotations
 * come from the palettes themselves. pc_interp_skeleton 0: linear. */
int pc_interp_skeleton = 1;
#define SKELETON_ROOT 0xff
static int inverse3(const float m[3][3], float out[3][3])
{
    float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1], c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2],
          c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (fabsf(det) < 1e-12f)
        return 0;
    float k = 1.0f / det;
    out[0][0] = c00 * k, out[1][0] = c01 * k, out[2][0] = c02 * k;
    out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
    out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
    out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
    out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
    out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
    out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
    return 1;
}
/* `x`'s translation relative to `parent`, in the parent's frame. */
static int local_offset(const render_transform* parent, const render_transform* x, float out[3])
{
    float inv[3][3];
    if (!inverse3(parent->rotation, inv))
        return 0;
    float d[3] = { x->translation[0] - parent->translation[0], x->translation[1] - parent->translation[1],
        x->translation[2] - parent->translation[2] };
    for (int a = 0; a < 3; a++)
        out[a] = inv[a][0] * d[0] + inv[a][1] * d[1] + inv[a][2] * d[2];
    return 1;
}
static void find_parents(const render_transform* now, const render_transform* was, int bones, uint8_t* parent)
{
    for (int i = 0; i < bones; i++) {
        parent[i] = SKELETON_ROOT;
        float best = 0;
        for (int p = 0; p < i; p++) {
            float a[3], b[3];
            if (!local_offset(&now[p], &now[i], a) || !local_offset(&was[p], &was[i], b))
                continue;
            float size = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
            float err = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
            /* the offset stays (GTE rounding: about a unit), prefer the nearest */
            float score = err + 0.001f * size;
            if (err <= 3.0f + 0.01f * size && (parent[i] == SKELETON_ROOT || score < best))
                parent[i] = (uint8_t)p, best = score;
        }
    }
}
/* The rotation of a bone matrix (its columns normalised; the game scales
 * columns) as a unit quaternion (w, x, y, z); `scale`: the column lengths. */
static int to_quaternion(const float m[3][3], float q[4], float scale[3])
{
    float r[3][3];
    for (int c = 0; c < 3; c++) {
        scale[c] = sqrtf(m[0][c] * m[0][c] + m[1][c] * m[1][c] + m[2][c] * m[2][c]);
        if (scale[c] < 1e-6f)
            return 0;
        for (int k = 0; k < 3; k++)
            r[k][c] = m[k][c] / scale[c];
    }
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
              + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det <= 0.5f)
        return 0; /* mirrored or degenerate: blended linearly */
    float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0) {
        float k = 0.5f / sqrtf(trace + 1.0f);
        q[0] = 0.25f / k, q[1] = (r[2][1] - r[1][2]) * k, q[2] = (r[0][2] - r[2][0]) * k, q[3] = (r[1][0] - r[0][1]) * k;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float k = 2.0f * sqrtf(1.0f + r[0][0] - r[1][1] - r[2][2]);
        q[0] = (r[2][1] - r[1][2]) / k, q[1] = 0.25f * k, q[2] = (r[0][1] + r[1][0]) / k, q[3] = (r[0][2] + r[2][0]) / k;
    } else if (r[1][1] > r[2][2]) {
        float k = 2.0f * sqrtf(1.0f + r[1][1] - r[0][0] - r[2][2]);
        q[0] = (r[0][2] - r[2][0]) / k, q[1] = (r[0][1] + r[1][0]) / k, q[2] = 0.25f * k, q[3] = (r[1][2] + r[2][1]) / k;
    } else {
        float k = 2.0f * sqrtf(1.0f + r[2][2] - r[0][0] - r[1][1]);
        q[0] = (r[1][0] - r[0][1]) / k, q[1] = (r[0][2] + r[2][0]) / k, q[2] = (r[1][2] + r[2][1]) / k, q[3] = 0.25f * k;
    }
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; k++)
        q[k] /= n;
    return 1;
}
static void from_quaternion(const float q[4], float m[3][3])
{
    float w = q[0], x = q[1], y = q[2], z = q[3];
    m[0][0] = 1 - 2 * (y * y + z * z), m[0][1] = 2 * (x * y - w * z), m[0][2] = 2 * (x * z + w * y);
    m[1][0] = 2 * (x * y + w * z), m[1][1] = 1 - 2 * (x * x + z * z), m[1][2] = 2 * (y * z - w * x);
    m[2][0] = 2 * (x * z - w * y), m[2][1] = 2 * (y * z + w * x), m[2][2] = 1 - 2 * (x * x + y * y);
}
static void quaternion_multiply(const float a[4], const float b[4], float out[4])
{
    out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}
static void slerp(const float a[4], const float b[4], float t, float out[4])
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], sign = 1;
    if (d < 0)
        d = -d, sign = -1; /* the shorter way round */
    float wa = 1 - t, wb = t;
    if (d < 0.9995f) {
        float angle = acosf(d), s = sinf(angle);
        wa = sinf((1 - t) * angle) / s, wb = sinf(t * angle) / s;
    }
    float n = 0;
    for (int k = 0; k < 4; k++) {
        out[k] = wa * a[k] + wb * sign * b[k];
        n += out[k] * out[k];
    }
    n = sqrtf(n);
    for (int k = 0; k < 4; k++)
        out[k] /= n;
}
/* Bone `i` (was, now -> out, with quaternions qa, qb -> qo) carried by its
 * parent (pw, pn -> po, quaternions pqa, pqb -> pqo): the relative rotation
 * parent^-1 * bone slerped, the offset in the parent's frame lerped. */
static int blend_child(const render_transform* pw, const render_transform* pn, const render_transform* po,
    const float pqa[4], const float pqb[4], const float pqo[4], const render_transform* was, const render_transform* now,
    const float qa[4], const float qb[4], float t, render_transform* out, float qo[4])
{
    float oa[3], ob[3];
    if (!local_offset(pw, was, oa) || !local_offset(pn, now, ob))
        return 0;
    float ia[4] = { pqa[0], -pqa[1], -pqa[2], -pqa[3] }, ib[4] = { pqb[0], -pqb[1], -pqb[2], -pqb[3] };
    float la[4], lb[4], lo[4];
    quaternion_multiply(ia, qa, la);
    quaternion_multiply(ib, qb, lb);
    slerp(la, lb, t, lo);
    quaternion_multiply(pqo, lo, qo);
    for (int a = 0; a < 3; a++) {
        float sum = po->translation[a];
        for (int k = 0; k < 3; k++)
            sum += po->rotation[a][k] * (oa[k] + (ob[k] - oa[k]) * t);
        out->translation[a] = sum;
    }
    return 1;
}
#define SKELETON_CARRIER 0xfe
/* Blend a whole palette (`was` -> `now`) at t with the parents found;
 * `carrier` (was, now, blended), when not NULL, carries the bones whose
 * parent is SKELETON_CARRIER (a weapon on its holder's hand). */
static void blend_skeleton(const render_transform* was, const render_transform* now, const uint8_t* parent, int bones,
    float t, const render_transform* carrier, render_transform* out)
{
    float qa[RENDER_MAX_BONES][4], qb[RENDER_MAX_BONES][4], qo[RENDER_MAX_BONES][4];
    float scale[3], cqa[4], cqb[4], cqo[4], unused[3];
    unsigned char ok[RENDER_MAX_BONES];
    int carried = carrier && to_quaternion(carrier[0].rotation, cqa, unused)
               && to_quaternion(carrier[1].rotation, cqb, unused) && to_quaternion(carrier[2].rotation, cqo, unused);
    for (int i = 0; i < bones; i++) {
        ok[i] = to_quaternion(was[i].rotation, qa[i], scale) && to_quaternion(now[i].rotation, qb[i], scale);
        int p = parent[i];
        int done = 0;
        if (ok[i]) {
            if (p == SKELETON_CARRIER)
                done = carried
                    && blend_child(&carrier[0], &carrier[1], &carrier[2], cqa, cqb, cqo, &was[i], &now[i], qa[i], qb[i],
                        t, &out[i], qo[i]);
            else if (p != SKELETON_ROOT)
                done = ok[p]
                    && blend_child(&was[p], &now[p], &out[p], qa[p], qb[p], qo[p], &was[i], &now[i], qa[i], qb[i], t,
                        &out[i], qo[i]);
            if (!done) {
                /* on its own: slerped rotation, lerped position */
                slerp(qa[i], qb[i], t, qo[i]);
                for (int k = 0; k < 3; k++)
                    out[i].translation[k] = was[i].translation[k] + (now[i].translation[k] - was[i].translation[k]) * t;
                done = 1;
            }
        }
        if (!ok[i]) {
            blend_transform(&was[i], &now[i], t, &out[i]); /* degenerate: linear, as before */
            continue;
        }
        /* The rest of each matrix after its rotation (Q^T M: the column
         * scales, and the small skew of the GTE's fixed-point matrices) is
         * blended linearly, so both palettes come back exactly at t = 0
         * and 1 and an unchanged bone stays exactly as it was. */
        float m[3][3], ma[3][3], mb[3][3];
        from_quaternion(qo[i], m);
        from_quaternion(qa[i], ma);
        from_quaternion(qb[i], mb);
        float rest[3][3];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                float a = 0, b = 0;
                for (int k = 0; k < 3; k++) {
                    a += ma[k][r] * was[i].rotation[k][c];
                    b += mb[k][r] * now[i].rotation[k][c];
                }
                rest[r][c] = a + (b - a) * t;
            }
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                out[i].rotation[r][c] = m[r][0] * rest[0][c] + m[r][1] * rest[1][c] + m[r][2] * rest[2][c];
    }
}
/* An attachment: the bone of an instance drawn earlier this frame (an
 * object list) in whose frame `in`'s root keeps its offset in both
 * palettes while that bone turns (a weapon or shield in a hand: the
 * game places them from the holder's bone matrix, func_800B217C). Blending
 * the root through it keeps the weapon in the hand mid-swing. */
static void find_carrier(const draw_list* self, list_instance* in, const render_transform* now,
    const render_transform* was)
{
    if (!pc_interp_skeleton)
        return;
    float best = 0;
    for (uint32_t back = 0; back < 64; back++) {
        uint32_t id = (self->id - back) & 0xffffff;
        const draw_list* list = find_list(id);
        if (!list || list->serial != self->serial)
            break;
        if (!list->rules.objects)
            continue;
        int count = list == self ? list->count - 1 : list->count; /* not itself */
        for (int k = 0; k < count; k++) {
            const list_instance* c = &list->instances[k];
            if (!c->has_previous || (in->owner && c->owner == in->owner))
                continue;
            const render_transform* cn = &list->transforms[c->first];
            const render_transform* cw = cn + c->bones;
            for (int b = 0; b < c->bones; b++) {
                float a[3], o[3];
                if (!local_offset(&cn[b], &now[0], a) || !local_offset(&cw[b], &was[0], o))
                    continue;
                float size = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
                float err = sqrtf((a[0] - o[0]) * (a[0] - o[0]) + (a[1] - o[1]) * (a[1] - o[1]) + (a[2] - o[2]) * (a[2] - o[2]));
                if (size > 256.0f || err > 3.0f + 0.02f * size || (in->carrier_list && err + 0.001f * size >= best))
                    continue;
                /* The carrier moved enough to tell: a root left where it was
                 * would be well off its offset in the carrier's new frame
                 * (else any bone at rest would do). The attached root may
                 * turn on its own (weapons keep their own angles). */
                float still[3];
                if (!local_offset(&cn[b], &was[0], still))
                    continue;
                float moved = sqrtf((still[0] - o[0]) * (still[0] - o[0]) + (still[1] - o[1]) * (still[1] - o[1])
                                    + (still[2] - o[2]) * (still[2] - o[2]));
                if (moved < 8.0f * (3.0f + 0.02f * size))
                    continue;
                in->carrier_list = list->id;
                in->carrier_instance = (uint16_t)k;
                in->carrier_bone = (uint8_t)b;
                best = err + 0.001f * size;
            }
        }
    }
    if (in->carrier_list)
        in->parent[0] = SKELETON_CARRIER;
}
void render_blend_palette(const render_transform* was, const render_transform* now, int bones, float t,
    render_transform* out)
{
    if (bones <= 0 || bones > RENDER_MAX_BONES)
        return;
    if (!pc_interp_skeleton) {
        for (int b = 0; b < bones; b++)
            blend_transform(&was[b], &now[b], t, &out[b]);
        return;
    }
    uint8_t parent[RENDER_MAX_BONES];
    find_parents(now, was, bones, parent);
    blend_skeleton(was, now, parent, bones, t, NULL, out);
}
void render_list_add(uint32_t id, const render_instance* instance)
{
    draw_list* list = find_list(id);
    if (!list)
        return;
    if (list->count == list->capacity) {
        int capacity = list->capacity ? list->capacity * 2 : 64;
        list_instance* grown = realloc(list->instances, (size_t)capacity * sizeof *grown);
        if (!grown)
            return;
        list->instances = grown;
        list->capacity = capacity;
    }
    int bones = instance->bones > 0 ? instance->bones : 1;
    if (bones > RENDER_MAX_BONES)
        return;
    int previous = instance->bones > 0 && instance->previous;
    int first = list->transform_count;
    render_transform* palette = reserve_transforms(list, bones * (previous ? 2 : 1));
    if (!palette)
        return;
    if (instance->bones > 0) {
        memcpy(palette, instance->palette, (size_t)bones * sizeof *palette);
        if (previous)
            memcpy(palette + bones, instance->previous, (size_t)bones * sizeof *palette);
    } else {
        memcpy(palette->rotation, instance->rotation, sizeof palette->rotation);
        memcpy(palette->translation, instance->translation, sizeof palette->translation);
    }
    list_instance* in = &list->instances[list->count++];
    in->mesh = instance->mesh;
    in->bias = instance->bias;
    in->subdivision_level = instance->subdivision_level;
    in->first = first;
    in->bones = bones;
    in->has_previous = previous;
    in->override = instance->override;
    memcpy(in->color, instance->color, sizeof in->color);
    in->tpage = instance->tpage;
    in->clut = instance->clut;
    in->blend = instance->blend;
    in->owner = instance->owner;
    in->blend_offset = instance->blend_offset;
    in->cut = instance->cut;
    in->cut_previous = instance->cut_previous;
    in->cut_ghost = instance->cut_ghost;
    in->blend_scale = instance->blend_scale > 0 ? instance->blend_scale : 1.0f;
    in->carrier_list = 0;
    if (previous && list->rules.objects) {
        find_parents(palette, palette + bones, bones, in->parent);
        if (in->parent[0] == SKELETON_ROOT)
            find_carrier(list, in, palette, palette + bones);
    } else
        memset(in->parent, SKELETON_ROOT, sizeof in->parent); /* each entry on its own (particles) */
}
void render_list_order(uint32_t id, const uint32_t* order, int count)
{
    draw_list* list = find_list(id);
    if (!list)
        return;
    list->order_used = 0;
    if (count > list->order_capacity) {
        uint32_t* grown = realloc(list->order, (size_t)count * sizeof *grown);
        if (!grown)
            return;
        list->order = grown;
        list->order_capacity = count;
    }
    if (count > 0)
        memcpy(list->order, order, (size_t)count * sizeof *order);
    list->order_used = count > 0 ? count : 0;
}
void render_list_tint(uint32_t id, int dr, int dg, int db, int fade)
{
    draw_list* list = find_list(id);
    if (list) {
        list->tint[0] = dr, list->tint[1] = dg, list->tint[2] = db, list->tint[3] = fade;
    }
}
int render_list_objects(uint32_t id)
{
    draw_list* list = find_list(id);
    return !list ? -1 : list->rules.objects ? 1 : list->rules.layered ? 2 : list->rules.effects ? 3 : 0;
}
int render_list_effects(uint32_t id)
{
    draw_list* list = find_list(id);
    return list ? list->rules.effects : -1;
}
int render_list_translucent(uint32_t id, const uint32_t* entries, const uint8_t* flags, int count)
{
    draw_list* list = find_list(id);
    if (!list || count < 0)
        return -1;
    if (list->translucent_used + count > list->translucent_capacity) {
        int capacity = list->translucent_capacity ? list->translucent_capacity : 64;
        while (capacity < list->translucent_used + count)
            capacity *= 2;
        uint32_t* grown = realloc(list->translucent, (size_t)capacity * sizeof *grown);
        if (!grown)
            return -1;
        list->translucent = grown;
        uint8_t* grown_flags = realloc(list->translucent_flags, (size_t)capacity);
        if (!grown_flags)
            return -1;
        list->translucent_flags = grown_flags;
        list->translucent_capacity = capacity;
    }
    int first = list->translucent_used;
    if (count) {
        memcpy(list->translucent + first, entries, (size_t)count * sizeof *entries);
        if (flags)
            memcpy(list->translucent_flags + first, flags, (size_t)count);
        else
            memset(list->translucent_flags + first, 0, (size_t)count);
    }
    list->translucent_used += count;
    return first;
}
int render_list_translucent_count(uint32_t id)
{
    draw_list* list = find_list(id);
    return list ? list->translucent_used : -1;
}
void render_list_translucent_clear(uint32_t id)
{
    draw_list* list = find_list(id);
    if (list)
        list->translucent_used = 0;
}
/* The marker: GP0 no-ops to every packet consumer except the native draw
 * hook in pc_draw_packet (boot_runtime.c), which recognises the word. */
#define RENDER_MARKER 0x00524e44u
#define RENDER_TRANSLUCENT_MARKER 0x00544e44u
/* Link a `words`-word packet (after its tag) at the head of `*ot_entry`. */
static void link_packet(uint32_t* ot_entry, uint32_t* packet, uint32_t words)
{
    packet[0] = (*ot_entry & 0xffffff) | (words << 24);
    *ot_entry = (*ot_entry & 0xff000000) | ((uint32_t)(uintptr_t)packet & 0xffffff);
}
uint32_t* render_list_link(uint32_t list, uint32_t* ot_entry, uint32_t* packet)
{
    packet[1] = RENDER_MARKER;
    packet[2] = list & 0xffffff;
    link_packet(ot_entry, packet, 2);
    return packet + 3;
}
int render_is_marker(const uint32_t* packet) { return (packet[0] >> 24) == 2 && packet[1] == RENDER_MARKER; }
/* Every word after the tag has a zero top byte: GPU no-ops elsewhere. */
uint32_t* render_list_link_translucent(uint32_t list, int first, int count, uint32_t* ot_entry, uint32_t* packet)
{
    packet[1] = RENDER_TRANSLUCENT_MARKER;
    packet[2] = list & 0xffffff;
    packet[3] = (uint32_t)first & 0xffffff;
    packet[4] = (uint32_t)count & 0xffffff;
    link_packet(ot_entry, packet, 4);
    return packet + 5;
}
int render_translucent_marker(const uint32_t* packet, uint32_t* list, int* first, int* count)
{
    if ((packet[0] >> 24) != 4 || packet[1] != RENDER_TRANSLUCENT_MARKER)
        return 0;
    *list = packet[2];
    *first = (int)packet[3];
    *count = (int)packet[4];
    return 1;
}

/* Whether the palettes of `in` jump between the previous frame and this
 * one (teleport, respawn into the same slot): not blended. */
static int instance_jumps(const draw_list* list, const list_instance* in)
{
    if (!in->has_previous)
        return 0;
    const render_transform* now = &list->transforms[in->first];
    const render_transform* was = now + in->bones;
    float jump = 0;
    for (int b = 0; b < in->bones; b++)
        for (int k = 0; k < 3; k++)
            jump = fmaxf(jump, fabsf(now[b].translation[k] - was[b].translation[k]));
    return jump > 512.0f;
}
/* The palette to draw `in` with at blend t into `out`; returns 1 when it
 * was blended. A jump (teleport, respawn into the same slot) is not. */
static int instance_palette(const draw_list* list, const list_instance* in, float t, render_transform* out)
{
    const render_transform* now = &list->transforms[in->first];
    if (in->has_previous)
        t = (in->blend_offset + t) * in->blend_scale; /* a blend over several game frames */
    if (!in->has_previous || t >= 1.0f || instance_jumps(list, in)) {
        memcpy(out, now, (size_t)in->bones * sizeof *out);
        return 0;
    }
    const render_transform* was = now + in->bones;
    if (pc_interp_skeleton) {
        /* the carrier's bone (was, now, blended at t) */
        render_transform carrier[3], palette[RENDER_MAX_BONES];
        const draw_list* holder = in->carrier_list ? find_list(in->carrier_list) : NULL;
        const list_instance* c = holder && in->carrier_instance < holder->count ? &holder->instances[in->carrier_instance] : NULL;
        int carried = c && c->has_previous && in->carrier_bone < c->bones && !instance_jumps(holder, c)
                   && instance_palette(holder, c, t, palette);
        if (carried) {
            carrier[1] = holder->transforms[c->first + in->carrier_bone];
            carrier[0] = holder->transforms[c->first + c->bones + in->carrier_bone];
            carrier[2] = palette[in->carrier_bone];
        }
        blend_skeleton(was, now, in->parent, in->bones, t, carried ? carrier : NULL, out);
    }
    else
        for (int b = 0; b < in->bones; b++)
            blend_transform(&was[b], &now[b], t, &out[b]);
    return 1;
}
/* Where list `id` draws in `target` (native VRAM pixels x, y, w, h within
 * its drawing area; MSAA copies and syncs only that much): each instance's
 * per-bone boxes of its polygons through the palette it is drawn with,
 * projected. Returns 0 (with the whole drawing area) when that cannot be
 * told (a vertex at or behind the eye), 1 otherwise (w = 0: nothing). */
int render_list_screen_bounds(uint32_t id, const render_target* target, int out[4])
{
    memcpy(out, target->clip, 4 * sizeof(int));
    draw_list* list = find_list(id);
    if (!list)
        return 0;
    const render_camera* cam = target->camera ? target->camera : &list->camera;
    float hx = cam->h * (list->camera.widescreen ? 0.75f : 1.0f);
    float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
    static float box[RENDER_MAX_BONES][6];
    render_transform palette[RENDER_MAX_BONES];
    for (int i = 0; i < list->count; i++) {
        const list_instance* in = &list->instances[i];
        mesh_slot* mesh = lookup(in->mesh);
        if (!mesh || in->bones <= 0 || in->bones > RENDER_MAX_BONES)
            continue;
        for (int b = 0; b < in->bones; b++)
            box[b][0] = box[b][1] = box[b][2] = 1e30f, box[b][3] = box[b][4] = box[b][5] = -1e30f;
        for (int p = 0; p < mesh->polygons; p++) {
            const render_polygon* poly = &mesh->cpu[p];
            int corners = poly->flags & RENDER_POLY_QUAD ? 4 : 3;
            for (int c = 0; c < corners; c++) {
                int b = poly->bone[c] < in->bones ? poly->bone[c] : 0;
                for (int k = 0; k < 3; k++) {
                    box[b][k] = fminf(box[b][k], poly->position[c][k]);
                    box[b][k + 3] = fmaxf(box[b][k + 3], poly->position[c][k]);
                }
            }
        }
        instance_palette(list, in, target->t, palette);
        for (int b = 0; b < in->bones; b++) {
            if (box[b][0] > box[b][3])
                continue;
            for (int c = 0; c < 8; c++) {
                float m[3] = { box[b][(c & 1) ? 3 : 0], box[b][(c & 2) ? 4 : 1], box[b][(c & 4) ? 5 : 2] }, w[3], v[3];
                for (int k = 0; k < 3; k++)
                    w[k] = palette[b].rotation[k][0] * m[0] + palette[b].rotation[k][1] * m[1]
                         + palette[b].rotation[k][2] * m[2] + palette[b].translation[k];
                for (int k = 0; k < 3; k++)
                    v[k] = cam->rotation[k][0] * w[0] + cam->rotation[k][1] * w[1] + cam->rotation[k][2] * w[2]
                         + cam->translation[k];
                if (v[2] <= 1.0f)
                    return 0;
                float sx = cam->ofx + hx * v[0] / v[2], sy = cam->ofy + cam->h * v[1] / v[2];
                lo[0] = fminf(lo[0], sx), lo[1] = fminf(lo[1], sy);
                hi[0] = fmaxf(hi[0], sx), hi[1] = fmaxf(hi[1], sy);
            }
        }
    }
    if (lo[0] > hi[0]) {
        out[2] = out[3] = 0;
        return 1;
    }
    /* Into the drawing area (the drawing offset), with a margin. */
    int x0 = (int)floorf(lo[0]) + target->ox - 4, y0 = (int)floorf(lo[1]) + target->oy - 4;
    int x1 = (int)ceilf(hi[0]) + target->ox + 4, y1 = (int)ceilf(hi[1]) + target->oy + 4;
    const int* clip = target->clip;
    x0 = x0 < clip[0] ? clip[0] : x0, y0 = y0 < clip[1] ? clip[1] : y0;
    x1 = x1 > clip[0] + clip[2] ? clip[0] + clip[2] : x1, y1 = y1 > clip[1] + clip[3] ? clip[1] + clip[3] : y1;
    out[0] = x0, out[1] = y0, out[2] = x1 > x0 ? x1 - x0 : 0, out[3] = y1 > y0 ? y1 - y0 : 0;
    return 1;
}
/* The polygon's effective code and texture page in instance `in` (blend,
 * texture override), as the geometry shader computes them. */
static unsigned effective_code(const list_instance* in, const render_polygon* poly)
{
    return in->blend ? (poly->code | 6u) & ~1u : poly->code;
}
static unsigned effective_tpage(const list_instance* in, const render_polygon* poly)
{
    unsigned tpage = poly->tpage;
    if (in->blend) /* the instance's mode replaces the polygon's */
        tpage = (tpage & ~0x60u) | (unsigned)((in->blend - 1) & 3) << 5;
    if (in->override & RENDER_OVERRIDE_TEXTURE)
        tpage |= in->tpage;
    return tpage;
}
static unsigned effective_clut(const list_instance* in, const render_polygon* poly)
{
    return in->override & RENDER_OVERRIDE_TEXTURE ? (in->clut + poly->clut) & 0xffffu : poly->clut;
}
/* A sprite's corners in world space: as the geometry shader places them
 * (facing the list's camera at the centre's depth), back through the camera. */
static int sprite_corners(const draw_list* list, const render_polygon* poly, const render_transform* x,
    float world[4][3])
{
    const render_camera* cam = &list->camera;
    float c[3], view[3];
    for (int k = 0; k < 3; k++)
        c[k] = x->rotation[k][0] * poly->position[0][0] + x->rotation[k][1] * poly->position[0][1]
             + x->rotation[k][2] * poly->position[0][2] + x->translation[k];
    for (int a = 0; a < 3; a++)
        view[a] = cam->rotation[a][0] * c[0] + cam->rotation[a][1] * c[1] + cam->rotation[a][2] * c[2]
                + cam->translation[a];
    float z = floorf(view[2]);
    if (z < 1.0f || cam->h <= 0)
        return 0;
    double r[3][3], inv[3][3];
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            r[a][b] = cam->rotation[a][b];
    double det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
               + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det > -1e-12 && det < 1e-12)
        return 0;
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) {
            int a1 = (b + 1) % 3, a2 = (b + 2) % 3, b1 = (a + 1) % 3, b2 = (a + 2) % 3;
            inv[a][b] = (r[a1][b1] * r[a2][b2] - r[a1][b2] * r[a2][b1]) / det;
        }
    float hx = cam->h * (cam->widescreen ? 0.75f : 1.0f);
    float k = poly->offset_depth ? (float)poly->offset_depth / z : 1.0f;
    float sx = floorf(hx * floorf(view[0]) / z), sy = floorf(cam->h * floorf(view[1]) / z);
    for (int v = 0; v < 4; v++) {
        float q[3] = { (sx + poly->offset[v][0] * k * (hx / cam->h)) * z / hx, (sy + poly->offset[v][1] * k) * z / cam->h,
            z };
        for (int a = 0; a < 3; a++) {
            double sum = 0;
            for (int b = 0; b < 3; b++)
                sum += inv[a][b] * (q[b] - cam->translation[b]);
            world[v][a] = (float)sum;
        }
    }
    return 1;
}
static uint32_t emit_source, emit_mesh;
void render_emit_tag(uint32_t* source, uint32_t* mesh)
{
    *source = emit_source;
    *mesh = emit_mesh;
}
static int emit_triangles(uint32_t id, int translucent, render_triangle_sink emit, void* context)
{
    draw_list* list = find_list(id);
    if (!list)
        return 0;
    int emitted = 0;
    render_transform palette[RENDER_MAX_BONES];
    for (int i = 0; i < list->count; i++) {
        const list_instance* in = &list->instances[i];
        mesh_slot* mesh = lookup(in->mesh);
        if (!mesh || !mesh->cpu)
            continue;
        instance_palette(list, in, 1.0f, palette);
        for (int p = 0; p < mesh->polygons; p++) {
            const render_polygon* poly = &mesh->cpu[p];
            unsigned code = effective_code(in, poly);
            if (!(code & 2) != !translucent)
                continue;
            float world[4][3];
            int count = poly->flags & RENDER_POLY_QUAD ? 4 : 3;
            if (poly->flags & RENDER_POLY_SPRITE) {
                if (!sprite_corners(list, poly, &palette[poly->bone[0] < in->bones ? poly->bone[0] : 0], world))
                    continue;
                count = 4;
            } else
                for (int v = 0; v < count; v++) {
                const render_transform* x = &palette[poly->bone[v] < in->bones ? poly->bone[v] : 0];
                for (int k = 0; k < 3; k++)
                    world[v][k] = x->rotation[k][0] * poly->position[v][0] + x->rotation[k][1] * poly->position[v][1]
                                + x->rotation[k][2] * poly->position[v][2] + x->translation[k];
            }
            unsigned tpage = effective_tpage(in, poly), clut = effective_clut(in, poly);
            emit_source = poly->source, emit_mesh = in->mesh;
            static const int corners[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
            for (int t = 0; t < (count == 4 ? 2 : 1); t++) {
                float tri[9];
                uint8_t rgb[9], uv[6];
                for (int c = 0; c < 3; c++) {
                    int v = corners[t][c];
                    memcpy(tri + c * 3, world[v], sizeof world[v]);
                    for (int k = 0; k < 3; k++)
                        rgb[c * 3 + k] = in->override & RENDER_OVERRIDE_COLOR ? in->color[k] : poly->rgb[v][k];
                    uv[c * 2] = poly->uv[v][0], uv[c * 2 + 1] = poly->uv[v][1];
                }
                emit(context, tri, rgb, uv, code, tpage, clut);
                ++emitted;
            }
        }
    }
    return emitted;
}
int render_list_triangles(uint32_t id, render_triangle_sink emit, void* context)
{
    return emit_triangles(id, 0, emit, context);
}
int render_list_translucent_triangles(uint32_t id, render_triangle_sink emit, void* context)
{
    return emit_triangles(id, 1, emit, context);
}

/* ---- animation trace (RPC anim_trace) ----
 * File: "ANTR", version, then per presented image a frame header and its
 * instances (see pc/tools/anim_trace.py, which reads it). Palette entries
 * are 12 floats (rotation rows, then translation). */
static FILE* trace_file;
static int trace_left;
int render_anim_trace(const char* path, int count)
{
    if (trace_file)
        fclose(trace_file);
    trace_file = NULL;
    trace_left = 0;
    if (count <= 0)
        return 0;
    trace_file = fopen(path, "wb");
    if (!trace_file)
        return -1;
    const uint32_t header[2] = { 0x52544e41u /* "ANTR" */, 3 };
    fwrite(header, sizeof header, 1, trace_file);
    trace_left = count;
    return 0;
}
int render_anim_trace_remaining(void) { return trace_left; }
static void trace_palette(const render_transform* p, int bones)
{
    float f[RENDER_MAX_BONES * 12];
    for (int b = 0; b < bones; b++) {
        for (int r = 0; r < 3; r++)
            memcpy(f + b * 12 + r * 3, p[b].rotation[r], 3 * sizeof(float));
        memcpy(f + b * 12 + 9, p[b].translation, 3 * sizeof(float));
    }
    fwrite(f, sizeof(float) * 12, (size_t)bones, trace_file);
}
void render_anim_trace_present(unsigned vsync, int replayed, float t, const uint32_t* lists, int count)
{
    if (!trace_file || trace_left <= 0)
        return;
    int instances = 0;
    for (int l = 0; l < count; l++) {
        const draw_list* list = find_list(lists[l]);
        instances += list ? list->count : 0;
    }
    /* frame: vsync, serial of the newest finished frame, replayed, t,
     * instances, phase (the fraction of the vsync: extra presents) */
    extern float pc_present_phase;
    uint32_t frame[6] = { vsync, frame_serial, (uint32_t)replayed, 0, (uint32_t)instances, 0 };
    memcpy(&frame[3], &t, sizeof t);
    memcpy(&frame[5], &pc_present_phase, sizeof pc_present_phase);
    fwrite(frame, sizeof frame, 1, trace_file);
    for (int l = 0; l < count; l++) {
        const draw_list* list = find_list(lists[l]);
        for (int i = 0; list && i < list->count; i++) {
            const list_instance* in = &list->instances[i];
            render_transform drawn[RENDER_MAX_BONES];
            int blended = instance_palette(list, in, t, drawn);
            /* instance: mesh, owner, list id, list serial, index in list,
             * bones | has_previous << 8 | jumped << 9 | blended << 10 |
             * blend << 12, carrier list id, carrier instance << 8 | bone */
            uint32_t head[8] = { in->mesh, in->owner, list->id, list->serial, (uint32_t)i,
                (uint32_t)in->bones | (uint32_t)in->has_previous << 8 | (uint32_t)instance_jumps(list, in) << 9
                    | (uint32_t)blended << 10 | (uint32_t)(in->blend & 7) << 12,
                in->carrier_list, (uint32_t)in->carrier_instance << 8 | in->carrier_bone };
            fwrite(head, sizeof head, 1, trace_file);
            const render_transform* now = &list->transforms[in->first];
            trace_palette(drawn, in->bones);
            trace_palette(now, in->bones);
            if (in->has_previous)
                trace_palette(now + in->bones, in->bones);
        }
    }
    if (--trace_left <= 0) {
        fclose(trace_file);
        trace_file = NULL;
    }
}

/* ---- statistics ---- */

void render_frame_end(const render_stats* hardware)
{
    finished = counting;
    finished.legacy_room_triangles = hardware->legacy_room_triangles;
    finished.skipped_room_triangles = hardware->skipped_room_triangles;
    finished.legacy_object_triangles = hardware->legacy_object_triangles;
    finished.skipped_object_triangles = hardware->skipped_object_triangles;
    finished.legacy_translucent_triangles = hardware->legacy_translucent_triangles;
    finished.skipped_translucent_triangles = hardware->skipped_translucent_triangles;
    finished.legacy_other_triangles = hardware->legacy_other_triangles;
    finished.skipped_effect_triangles = hardware->skipped_effect_triangles;
    finished.legacy_effect_triangles = hardware->legacy_effect_triangles;
    finished.vram_syncs = hardware->vram_syncs;
    finished.stale_rects = hardware->stale_rects;
    memset(&counting, 0, sizeof counting);
    ++frame_serial;
    expire_transient();
}
uint32_t render_frame_serial(void) { return frame_serial; }
/* Game-side code counts its mesh builds here (actor_native.c). */
void render_count_object_mesh_built(void) { counting.object_meshes_built++; }
void render_stats_get(render_stats* stats)
{
    *stats = finished;
    stats->meshes = live_meshes;
    stats->mesh_polygons = live_polygons;
    stats->mesh_updates = mesh_updates;
}

/* ---- the editor's cutaway ---- */

/* While the editor's free camera flies (editor_view.cpp sets it every game
 * frame), room polygons standing between the eye and a focus point (the
 * selected light or glow, else Ashley) are cut inside a cone of radius
 * pc_editor_cutaway_radius at the focus, narrowing to the eye (a circle of
 * constant size on screen), with a dithered rim. Cut is a ghost, not a
 * hole: a share of the surface's pixels stays (pc_editor_cutaway_opacity,
 * an ordered dither, so the G-buffer, depth and lighting stay exact for the
 * pixels drawn) and so do its polygon edges, so the room keeps its shape
 * instead of opening onto the black void around it. editor_view.cpp cuts
 * only while the focus is hidden from the eye and fades the cut in and out
 * (`amount`). Ghost pixels carry G-buffer normal alpha 0.55 (a surface,
 * lit as moving geometry), which picking looks through (gpu_hw.c).
 * Room lists only (characters, effects and the ray-traced scene are not
 * touched); off for the game camera. */
int pc_editor_cutaway = 1;
float pc_editor_cutaway_radius = 320;
float pc_editor_cutaway_opacity = 0.3f;
int pc_editor_cutaway_auto = 1;
static int cutaway_on;
static float cutaway_focus[3], cutaway_amount;
void render_cutaway(int on, const float focus[3], float amount)
{
    cutaway_on = on && pc_editor_cutaway && pc_editor_cutaway_radius > 0 && amount > 0.001f;
    cutaway_amount = amount < 0 ? 0 : amount > 1 ? 1 : amount;
    if (cutaway_on)
        memcpy(cutaway_focus, focus, sizeof cutaway_focus);
}
/* The shaders' cut_focus (the focus in the camera's view space with the
 * rows' scales divided out, and the radius; w 0: off) and cut_scale (xyz:
 * 1 / the rows' scales; w: the share of a cut surface's pixels kept, 0 a
 * hole as before ghosts, edges drawn when above 0). */
static void cutaway_params(const draw_list* list, const render_camera* cam, float focus[4], float scale[4])
{
    memset(focus, 0, 4 * sizeof *focus);
    for (int a = 0; a < 3; a++) {
        float length = sqrtf(cam->rotation[a][0] * cam->rotation[a][0] + cam->rotation[a][1] * cam->rotation[a][1]
            + cam->rotation[a][2] * cam->rotation[a][2]);
        scale[a] = length > 1e-9f ? 1.0f / length : 1.0f;
    }
    scale[3] = 1;
    if (!cutaway_on || list->rules.objects || list->rules.effects)
        return;
    float opacity = pc_editor_cutaway_opacity < 0 ? 0 : pc_editor_cutaway_opacity > 1 ? 1 : pc_editor_cutaway_opacity;
    scale[3] = 1 - cutaway_amount * (1 - opacity);
    for (int a = 0; a < 3; a++)
        focus[a] = (cam->rotation[a][0] * cutaway_focus[0] + cam->rotation[a][1] * cutaway_focus[1]
                       + cam->rotation[a][2] * cutaway_focus[2] + cam->translation[a])
            * scale[a];
    focus[3] = pc_editor_cutaway_radius;
}

/* ---- drawing ---- */

static const char* vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec3 a_normal;\n"
    "layout(location = 2) in uvec4 a_color;\n" /* r, g, b, GPU code */
    "layout(location = 3) in uvec4 a_uvf;\n" /* u, v, polygon flags, bone */
    "layout(location = 4) in uvec2 a_page;\n" /* tpage, clut */
    "layout(location = 5) in uint a_bias;\n" /* ordering-table bias (objects) */
    "layout(location = 6) in uint a_effect;\n" /* animated material (uv_offset entry, 0: none) */
    "uniform mat3 bone_rotation[64];\n" /* model -> GTE view, per bone */
    "uniform vec3 bone_translation[64];\n"
    "out vec3 g_pos;\n" /* GTE view space, before the GTE's truncation */
    "out vec3 g_normal;\n"
    "flat out uvec4 g_color;\n"
    "flat out uvec4 g_uvf;\n"
    "flat out uvec2 g_page;\n"
    "flat out uint g_bias;\n"
    "flat out uint g_effect;\n"
    "void main() {\n"
    "    uint b = min(a_uvf.w, 63u);\n"
    "    g_pos = bone_rotation[b] * a_pos + bone_translation[b];\n"
    "    g_normal = a_normal; g_color = a_color; g_uvf = a_uvf; g_page = a_page; g_bias = a_bias; g_effect = a_effect;\n"
    "}\n";
static const char* geometry_source =
    "#version 330 core\n"
    "layout(lines_adjacency) in;\n"
    "layout(triangle_strip, max_vertices = 6) out;\n"
    "in vec3 g_pos[];\n"
    "in vec3 g_normal[];\n"
    "flat in uvec4 g_color[];\n"
    "flat in uvec4 g_uvf[];\n"
    "flat in uvec2 g_page[];\n"
    "flat in uint g_bias[];\n"
    "flat in uint g_effect[];\n"
    "uniform ivec4 uv_offset[65];\n" /* animated materials: the UV offset per entry (render_list_uv_effects) */
    "uniform int uv_count;\n" /* entries (0 is none) */
    "uniform mat3 normal_matrix;\n" /* room: inverse transpose of the model-view rotation */
    "uniform vec4 projection;\n" /* H * (3/4 in widescreen), H, OFX + draw x, OFY + draw y */
    "uniform vec2 offset;\n" /* OFX, OFY (screen space, for the PS1 area test) */
    "uniform ivec4 rules;\n" /* near bucket, far bucket, LOD bucket, instance bias */
    "uniform ivec2 split;\n" /* subdivision switch, instance subdivision level */
    "uniform vec4 tint;\n" /* dr, dg, db, fade (func_80098D6C) */
    "uniform vec2 clip_z;\n" /* near, far planes (view z) */
    "uniform int objects;\n" /* object rules (characters) */
    "uniform int zsf3;\n"
    "uniform ivec4 override_color;\n" /* rgb, on */
    "uniform ivec4 override_texture;\n" /* tpage, clut, on */
    "uniform ivec2 instance_blend;\n" /* on, tpage blend bits (render_instance.blend) */
    "uniform int translucent;\n" /* 0: opaque polygons only, 1: semi-transparent ones only, 2: all (effects) */
    "uniform ivec2 forced;\n" /* room translucent entries: decided by the game, UVs halved */
    "uniform int order;\n" /* room lists: the instance's first polygon in the list | sequence shift << 24 */
    "noperspective centroid out vec3 v_color_affine;\n"
    "noperspective centroid out vec2 v_uv_affine;\n"
    "smooth centroid out vec3 v_color_persp;\n"
    "smooth centroid out vec2 v_uv_persp;\n"
    "smooth centroid out float v_z;\n"
    "flat out uint v_order;\n" /* room polygons: PS1 draw order key (bucket << 10 | sequence; smaller paints later) */
    "flat out uvec4 v_state;\n" /* flags, tpage, clut, 0 */
    "flat out vec3 v_normal;\n"
    "flat out uvec4 v_bounds;\n"
    "uniform vec4 cut_focus;\n" /* the editor's cutaway: focus (view space, rows' scales divided out), radius */
    "uniform vec4 cut_scale;\n" /* 1 / the view rows' scales; w: ghost coverage */
    "smooth centroid out vec3 v_view;\n"
    "flat out int v_cut;\n" /* bit 0: cut; bits 1-3: the triangle's real edges (opposite corner 0, 1, 2) */
    "noperspective out vec3 v_bary;\n" /* the corner weights (a ghost's edges) */
    "vec3 p[4];\n"
    "vec2 uv[4];\n"
    "vec3 col[4];\n"
    "vec2 s[4];\n"
    "uint tpage, clut, pcode;\n"
    "uint effect = 0u, halved = 0u;\n"
    "vec3 n;\n"
    "float z_near = 0.0;\n" /* sprites: tested and written at their nearest point */
    "int cut = 0;\n"
    "int tri_edges = 7, corner = 0;\n" /* a quad's diagonal is no edge */
    "uint order_key = 0u;\n"
    "vec2 screen(vec3 q) { return vec2(offset.x + projection.x * q.x / q.z, offset.y + projection.y * q.y / q.z); }\n"
    "float nclip(vec2 a, vec2 b, vec2 c) { return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y); }\n"
    "void emit(int i) {\n"
    "    vec3 q = p[i];\n"
    "    float a = clip_z.y + clip_z.x, b = clip_z.y - clip_z.x;\n"
    "    gl_Position = vec4((projection.z * q.z + projection.x * q.x) / 512.0 - q.z,\n"
    "                       (projection.w * q.z + projection.y * q.y) / 256.0 - q.z,\n"
    "                       (a * q.z - 2.0 * clip_z.y * clip_z.x) / b, q.z);\n"
    "    v_color_affine = v_color_persp = col[i] / 255.0;\n"
    "    v_uv_affine = v_uv_persp = uv[i];\n"
    "    v_z = q.z - z_near;\n"
    "    v_state = uvec4(pcode & 7u, tpage, clut, effect | (halved << 8));\n"
    "    v_normal = n;\n"
    "    v_view = q * cut_scale.xyz;\n"
    "    v_cut = cut | (tri_edges << 1);\n"
    "    v_bary = vec3(corner == 0 ? 1.0 : 0.0, corner == 1 ? 1.0 : 0.0, corner == 2 ? 1.0 : 0.0);\n"
    "    v_order = order_key;\n"
    "    EmitVertex();\n"
    "}\n"
    "void triangle(int a, int b, int c) {\n"
    "    vec2 lo = min(min(uv[a], uv[b]), uv[c]), hi = max(max(uv[a], uv[b]), uv[c]);\n"
    "    v_bounds = uvec4(clamp(lo, 0.0, 255.0), clamp(hi, 0.0, 255.0));\n"
    "    corner = 0; emit(a); corner = 1; emit(b); corner = 2; emit(c);\n"
    "    EndPrimitive();\n"
    "}\n"
    /* Facing of triangle a, b, c: NCLIP on the GTE's whole-pixel screen
     * positions; behind the camera, the sign of det(p0, p1, p2). */
    "float facing(int a, int b, int c, float zmin) {\n"
    "    return zmin > 0.0 ? nclip(s[a], s[b], s[c]) : dot(cross(p[b] - p[a], p[c] - p[a]), p[a]);\n"
    "}\n"
    /* The editor's cutaway (room lists, free camera): a polygon whose
     * plane separates the eye from the focus stands between them; the
     * fragment stage cuts it inside the cone from the eye to the focus.
     * Floors under the focus and walls behind it never separate the two. */
    "void cutaway_test(bool quad) {\n"
    "    if (cut_focus.w <= 0.0) return;\n"
    "    vec3 a = p[0] * cut_scale.xyz, b = p[1] * cut_scale.xyz, c = p[2] * cut_scale.xyz;\n"
    "    vec3 pn = cross(b - a, c - a);\n"
    "    if (quad && dot(pn, pn) < 1e-3) pn = cross(p[3] * cut_scale.xyz - b, c - b);\n"
    "    float l = length(pn);\n"
    "    if (l < 1e-6) return;\n"
    "    pn /= l;\n"
    "    float eye = dot(pn, -a), focus = dot(pn, cut_focus.xyz - a);\n"
    "    if (eye * focus < 0.0 && abs(focus) > 8.0) cut = 1;\n"
    "}\n"
    "int sz(int i) { return int(clamp(p[i].z, 0.0, 65535.0)); }\n"
    /* Object rules (BATTLE/4B16C.s func_800B3CF0/func_800B4594). */
    "void object_triangle(int a, int b, int c, float zmin, int bias) {\n"
    "    if (facing(a, b, c, zmin) <= 0.0) return;\n"
    "    if (((zsf3 * (sz(a) + sz(b) + sz(c))) >> 12) + bias >= rules.y) return;\n"
    "    triangle(a, b, c);\n"
    "}\n"
    "void objects_main(uint flags, bool quad, bool twosided) {\n"
    "    if (override_color.w != 0) for (int i = 0; i < 4; i++) col[i] = vec3(override_color.rgb);\n"
    "    float zmin = min(min(p[0].z, p[1].z), min(p[2].z, quad ? p[3].z : p[2].z));\n"
    "    for (int i = 0; i < 4; i++) s[i] = zmin > 0.0 ? floor(screen(p[i])) : vec2(0.0);\n"
    /* Normal: Newell's over the unrounded view positions (0, 1, 3, 2). */
    "    vec3 q[4] = vec3[4](g_pos[0], g_pos[1], quad ? g_pos[3] : g_pos[2], g_pos[2]);\n"
    "    int count = quad ? 4 : 3;\n"
    "    n = vec3(0.0);\n"
    "    for (int i = 0; i < count; i++) {\n"
    "        vec3 a = q[i], b = q[(i + 1) % count];\n"
    "        n += vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));\n"
    "    }\n"
    "    float length_ = length(n);\n"
    "    n = length_ > 1e-6 ? n / length_ : vec3(0.0, 0.0, -1.0);\n"
    "    if (dot(n, p[0]) > 0.0) n = -n;\n"
    "    int bias = int(g_bias[0]);\n"
    "    if ((flags & 4u) != 0u) {\n"
    /* The PS1 picks the diagonal from the GTE's OP of the corners' screen
     * positions and depths: (v3 - v0) . ((v2 - v0) x (v1 - v0)). */
    "        vec3 c0 = vec3(s[0], float(sz(0)));\n"
    "        float d = dot(vec3(s[3], float(sz(3))) - c0, cross(vec3(s[2], float(sz(2))) - c0, vec3(s[1], float(sz(1))) - c0));\n"
    "        if (d < 0.0) { object_triangle(0, 1, 2, zmin, bias); object_triangle(1, 3, 2, zmin, bias); }\n"
    "        else { object_triangle(0, 1, 3, zmin, bias); object_triangle(0, 3, 2, zmin, bias); }\n"
    "        return;\n"
    "    }\n"
    "    if (!twosided && facing(0, 1, 2, zmin) <= 0.0) return;\n"
    "    int depth = quad ? (sz(0) + sz(1) + sz(2) + sz(3)) >> 4 : (zsf3 * (sz(0) + sz(1) + sz(2))) >> 12;\n"
    "    if (depth + bias >= rules.y) return;\n"
    "    triangle(0, 1, 2);\n"
    "    if (quad) triangle(1, 3, 2);\n"
    "}\n"
    /* A screen-aligned sprite (RENDER_POLY_SPRITE): the centre projected as
     * the GTE projects it, the corners at their screen offsets scaled by
     * the depth they were measured at over the centre's depth (X narrowed
     * in widescreen like everything the GTE projects), all at the centre's
     * depth; tested against the surfaces at the sprite's nearest point. */
    "void sprite_main() {\n"
    "    vec3 c = floor(g_pos[0]);\n"
    "    if (c.z < 1.0) return;\n"
    "    vec2 sc = floor(screen(c));\n"
    "    float k = g_normal[0].z > 0.0 ? g_normal[0].z / c.z : 1.0, r = 0.0;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        vec2 d = g_normal[i].xy * k;\n"
    "        r = max(r, length(d));\n"
    "        d.x *= projection.x / projection.y;\n"
    "        vec2 q = sc + d;\n"
    "        p[i] = vec3((q.x - offset.x) * c.z / projection.x, (q.y - offset.y) * c.z / projection.y, c.z);\n"
    "    }\n"
    "    z_near = min(r * c.z / projection.y, c.z - 1.0);\n"
    "    n = vec3(0.0, 0.0, -1.0);\n"
    "    triangle(0, 1, 2);\n"
    "    triangle(1, 3, 2);\n"
    "}\n"
    "void main() {\n"
    "    uint code = g_color[0].a, flags = g_uvf[0].z;\n"
    /* A blended instance: textured, modulated, semi-transparent. */
    "    if (instance_blend.x != 0) code = (code | 6u) & ~1u;\n"
    /* Semi-transparent polygons draw at their translucent markers. */
    "    if (translucent != 2 && ((code & 2u) != 0u) != (translucent != 0)) return;\n"
    "    pcode = code;\n"
    "    bool quad = (flags & 1u) != 0u, twosided = (flags & 2u) != 0u;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        p[i] = floor(g_pos[i]);\n"
    "        uv[i] = vec2(g_uvf[i].xy);\n"
    "        col[i] = vec3(g_color[i].rgb);\n"
    "    }\n"
    /* An animated material: the offset the game's effect writes into the
     * polygon's UV bytes this frame, added per vertex with the bytes' wrap. */
    "    effect = g_effect[0];\n"
    "    if (effect != 0u && int(effect) < uv_count) {\n"
    "        for (int i = 0; i < 4; i++) uv[i] = vec2((g_uvf[i].xy + uvec2(uv_offset[effect].xy)) & 255u);\n"
    "    } else effect = 0u;\n"
    "    tpage = g_page[0].x;\n"
    "    clut = g_page[0].y;\n"
    /* A blended instance's mode replaces the polygon's own bits. */
    "    if (instance_blend.x != 0) tpage = (tpage & ~0x60u) | uint(instance_blend.y);\n"
    "    if (override_texture.z != 0) { tpage |= uint(override_texture.x); clut = (uint(override_texture.y) + clut) & 0xffffu; }\n"
    "    if ((flags & 8u) != 0u) { sprite_main(); return; }\n"
    "    if (objects != 0) { objects_main(flags, quad, twosided); return; }\n"
    /* Sort depth: the largest vertex SZ (the GTE saturates to 0..65535). */
    "    float zmax = max(max(p[0].z, p[1].z), p[2].z);\n"
    "    if (quad) zmax = max(zmax, p[3].z);\n"
    "    int sz = int(clamp(floor(zmax), 0.0, 65535.0));\n"
    /* Back faces: NCLIP < 0 on the GTE's whole-pixel screen positions, as
     * the PS1 tests them (edge-on slivers have area 0 there and are kept:
     * they fill seams); behind the camera, the sign of det(p0, p1, p2),
     * which is NCLIP's for vertices in front of it. A quad whose first
     * triangle faces away is kept if its other one (3, 2, 1) does not. */
    "    float zmin = min(min(p[0].z, p[1].z), min(p[2].z, quad ? p[3].z : p[2].z));\n"
    "    for (int i = 0; i < 4; i++) s[i] = zmin > 0.0 ? floor(screen(p[i])) : vec2(0.0);\n"
    /* Translucent room polygons the game already kept (its back-face and
     * ordering-table tests, its UV halving decision). */
    "    if (forced.x != 0) {\n"
    "        if (forced.y != 0) {\n"
    "            for (int i = 0; i < 4; i++) uv[i] = floor(uv[i] * 0.5);\n"
    "            tpage = (tpage - 1u) & 0xffffu;\n"
    "            halved = 1u;\n"
    "        }\n"
    "    } else {\n"
    "    float a0 = facing(0, 1, 2, zmin);\n"
    "    float a1 = !quad ? a0 : facing(3, 2, 1, zmin);\n"
    "    if (!twosided && a0 < 0.0 && (!quad || a1 < 0.0)) return;\n"
    /* Ordering-table range. */
    "    int bucket = sz >> 2;\n"
    "    if (bucket < rules.x) return;\n"
    "    bucket += rules.w;\n"
    "    if (bucket < rules.x || bucket >= rules.y) return;\n"
    /* The PS1 paints the room back to front by bucket; within a bucket the
     * polygon linked first paints last (linking at the bucket's head). */
    "    order_key = (uint(clamp(bucket, 0, 16383)) << 10)\n"
    "        | min(uint(((order & 0xffffff) + gl_PrimitiveIDIn) >> (order >> 24)), 1023u);\n"
    /* Distance UV halving, except for polygons the PS1 subdivides (large
     * on screen and near: their leaves keep full UVs). */
    "    bool subdivided = false;\n"
    "    if (split.x != 0 && split.y - (sz >> 9) > 0 && zmin > 0.0) {\n"
    "        float area = a0;\n"
    "        if (!quad && area < 0.0) area = -area;\n"
    "        if (quad && area < 0.0 && !twosided) area = a1;\n"
    "        subdivided = area < 0.0 || area >= 512.0;\n" /* (unsigned) area >= 0x200 */
    "    }\n"
    "    if (!subdivided && bucket >= rules.z) {\n"
    "        for (int i = 0; i < 4; i++) uv[i] = floor(uv[i] * 0.5);\n"
    "        tpage = (tpage - 1u) & 0xffffu;\n"
    "        halved = 1u;\n"
    "    }\n"
    "    }\n"
    "    if (tint != vec4(0.0)) {\n"
    "        for (int i = 0; i < 4; i++) {\n"
    "            vec3 c = col[i];\n"
    "            if (tint.w != 0.0) {\n"
    "                float grey = (c.r * 5.0 + c.g * 9.0 + c.b * 2.0) / 16.0;\n"
    "                c = clamp(floor(c + (grey - c) * tint.w / 4096.0), 0.0, 255.0);\n"
    "            }\n"
    "            col[i] = clamp(c + tint.xyz, 0.0, 255.0);\n"
    "        }\n"
    "    }\n"
    /* View-space normal facing the camera, as gpu_hw.c reconstructs it. */
    "    n = normal_matrix * g_normal[0];\n"
    "    float length_ = length(n);\n"
    "    n = length_ > 1e-6 ? n / length_ : vec3(0.0, 0.0, -1.0);\n"
    "    if (dot(n, p[0]) > 0.0) n = -n;\n"
    "    cutaway_test(quad);\n"
    "    tri_edges = quad ? 6 : 7;\n"
    "    triangle(0, 1, 2);\n"
    "    tri_edges = 5;\n"
    "    if (quad) triangle(1, 3, 2);\n"
    "}\n";
static const char* fragment_source =
    "#version 330 core\n"
    "noperspective centroid in vec3 v_color_affine;\n"
    "noperspective centroid in vec2 v_uv_affine;\n"
    "smooth centroid in vec3 v_color_persp;\n"
    "smooth centroid in vec2 v_uv_persp;\n"
    "smooth centroid in float v_z;\n"
    "flat in uint v_order;\n"
    "flat in uvec4 v_state;\n"
    "flat in vec3 v_normal;\n"
    "flat in uvec4 v_bounds;\n"
    "smooth centroid in vec3 v_view;\n"
    "flat in int v_cut;\n"
    "noperspective in vec3 v_bary;\n"
    "uniform vec4 cut_focus;\n"
    "uniform vec4 cut_scale;\n"
    "uniform vec4 instance_cut;\n" /* render_instance.cut: share left out (x), ghost edges (y) */
    "uniform int perspective;\n"
    "uniform int filtering;\n"
    "uniform uint window;\n"
    "uniform int mask_set;\n"
    "uniform usampler2D vram;\n"
    "uniform float surface;\n" /* G-buffer normal alpha: 1 room, 0.75 moving (objects) */
    "uniform int depth_pass;\n" /* objects, pass 2: depth of the visible fragments only */
    "uniform sampler2D surface_depth;\n"
    "uniform int painter;\n" /* objects drawn in PS1 order (render_list_order) */
    "uniform sampler2D surface_before;\n" /* the native surfaces' depth before this list */
    "uniform int semi_pass;\n" /* translucent draws: 1 opaque texels only, 2 translucent texels only */
    "uniform int surface_test;\n" /* translucent draws: hidden behind surface_depth (1 exact, 2 room tolerance, 3 decal slack);
                                     * 4: a room list's ordered pass (near-coplanar slack, room_slack) */
    "uniform vec2 room_slack;\n" /* surface_test 4: relative, absolute (GTE units) */
    "uniform int room_order;\n" /* a room list's ordered pass: depth is the PS1 draw order key */
    "uniform int layer_slack;\n" /* objects, other room blocks: tested room_slack nearer (1), and written so into the surfaces (2) */
    "uniform ivec4 uv_window[65];\n" /* animated materials: base u, v, size u, v */
    "uniform vec4 uv_shift[65];\n" /* interpolated offset still to come u, v; wrap period u, v */
    "layout(location = 0) out vec4 color;\n"
    "layout(location = 1) out vec4 normal_out;\n"
    "layout(location = 2) out vec4 albedo_out;\n"
    "layout(location = 3) out vec4 shade_out;\n"
    /* Texel decoding as gpu_hw.c's draw shader. */
    "uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "uint texel_at(uint u, uint v, uint tpage, uint clut, uint win) {\n"
    "    uint mx = (win & 31u) * 8u, my = ((win >> 5) & 31u) * 8u;\n"
    "    u = ((u & ~mx) | ((((win >> 10) & 31u) * 8u) & mx)) & 255u;\n"
    "    v = ((v & ~my) | ((((win >> 15) & 31u) * 8u) & my)) & 255u;\n"
    "    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;\n"
    "    int cx = int(clut & 63u) * 16, cy = int((clut >> 6) & 511u);\n"
    "    uint depth = (tpage >> 7) & 3u;\n"
    "    if (depth == 0u)\n"
    "        return fetch(cx + int((fetch(tx + int(u >> 2), ty + int(v)) >> ((u & 3u) * 4u)) & 15u), cy);\n"
    "    if (depth == 1u)\n"
    "        return fetch(cx + int((fetch(tx + int(u >> 1), ty + int(v)) >> ((u & 1u) * 8u)) & 255u), cy);\n"
    "    return fetch(tx + int(u), ty + int(v));\n"
    "}\n"
    "vec3 rgb(uint t) { return vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)); }\n"
    /* An animated material between game frames: texels past the effect's
     * window wrap back by its period (the game's scroll wraps there). */
    "ivec2 uv_wrap(ivec2 t, uint e, float k) {\n"
    "    for (int a = 0; a < 2; a++) {\n"
    "        if (uv_window[e][a + 2] <= 0) continue;\n"
    "        float r = float(t[a]) - floor(float(uv_window[e][a]) * k);\n"
    "        int p = int(floor(uv_shift[e][a + 2] * k));\n"
    "        if (r >= floor(float(uv_window[e][a + 2]) * k)) t[a] -= p; else if (r < 0.0) t[a] += p;\n"
    "    }\n"
    "    return t;\n"
    "}\n"
    /* 4 x 4 ordered dither threshold (Bayer) in [0, 1). */
    "float bayer4(ivec2 q) {\n"
    "    int a = (q.x ^ q.y) & 3, b = q.y & 3;\n"
    "    return float(((a & 1) << 3) | ((b & 1) << 2) | ((a >> 1) << 1) | (b >> 1)) / 16.0 + 1.0 / 32.0;\n"
    "}\n"
    "void main() {\n"
    /* The editor's cutaway: inside the cone from the eye to the focus (its
     * radius at the focus cut_focus.w: a circle of constant size on
     * screen), a ghost out to 3/4 of the radius (cut_scale.w of its pixels
     * kept, and its edges), blending to solid at the radius. */
    "    vec3 bary_step = fwidth(v_bary);\n" /* outside the branch: derivatives stay defined */
    "    bool ghost = false;\n"
    "    if ((v_cut & 1) != 0) {\n"
    "        vec3 f = cut_focus.xyz;\n"
    "        float t = clamp(dot(v_view, f) / max(dot(f, f), 1e-6), 0.0, 1.0);\n"
    "        float keep = clamp((length(v_view - f * t) / max(cut_focus.w * t, 1e-3) - 0.75) * 4.0, 0.0, 1.0);\n"
    "        float cover = mix(cut_scale.w, 1.0, keep);\n"
    "        if (cut_scale.w > 0.0 && cover < 1.0) {\n"
    "            vec3 d = v_bary / max(bary_step, vec3(1e-6));\n" /* pixels to each edge */
    "            int e = v_cut >> 1;\n"
    "            float near_edge = min(min((e & 1) != 0 ? d.x : 1e9, (e & 2) != 0 ? d.y : 1e9), (e & 4) != 0 ? d.z : 1e9);\n"
    "            if (near_edge < 1.0) cover = 1.0;\n"
    "        }\n"
    "        if (cover < bayer4(ivec2(gl_FragCoord.xy))) discard;\n"
    "        ghost = keep < 0.5;\n"
    "    }\n"
    /* A dithered share of the whole instance left out (gameplay smart
     * walls fading, or ghosts with their edges kept). */
    "    if (instance_cut.x > 0.0) {\n"
    "        float cover = 1.0 - instance_cut.x;\n"
    "        if (instance_cut.y > 0.0) {\n"
    "            vec3 d = v_bary / max(bary_step, vec3(1e-6));\n"
    "            int e = v_cut >> 1;\n"
    "            float near_edge = min(min((e & 1) != 0 ? d.x : 1e9, (e & 2) != 0 ? d.y : 1e9), (e & 4) != 0 ? d.z : 1e9);\n"
    "            if (near_edge < 1.0) cover = 1.0;\n"
    "            ghost = true;\n"
    "        }\n"
    "        if (cover < bayer4(ivec2(gl_FragCoord.xy))) discard;\n"
    "    }\n"
    "    float depth = clamp(v_z / 65536.0, 0.0, 1.0);\n"
    /* Objects and other room blocks are painted after the room surfaces
     * they lie on (their buckets are nearer): tested as if room_slack
     * nearer, so a model or floor marker on a surface wins as on the PS1. */
    "    float tested = layer_slack != 0 ? max(depth * (1.0 - room_slack.x) - room_slack.y / 65536.0, 0.0) : depth;\n"
    /* PS1 order within a model: hidden only by what was drawn before the
     * model (the room, other models); its own polygons paint over each
     * other in the order drawn. */
    "    if (painter != 0 && tested > texelFetch(surface_before, ivec2(gl_FragCoord.xy), 0).r)\n"
    "        discard;\n"
    "    if (depth_pass != 0 && painter == 0 && tested > texelFetch(surface_depth, ivec2(gl_FragCoord.xy), 0).r + 1.0 / 4194304.0)\n"
    "        discard;\n"
    "    if (surface_test != 0) {\n"
    "        float surface_z = texelFetch(surface_depth, ivec2(gl_FragCoord.xy), 0).r;\n"
    /* Room translucent polygons lie on or against room surfaces (glass in
     * its frame, light over a floor): the packet path's room slack. */
    "        if (surface_test == 1 ? tested > surface_z + 1.0 / 4194304.0\n"
    "            : surface_test == 2 ? surface_z < 1.0 && v_z > surface_z * 65536.0 * 1.002 + 1.0\n"
    /* Drop shadows and effects: the packet path's slack for things on or
     * against surfaces (decals, shadows on the floor). */
    "            : surface_test == 3 ? surface_z < 1.0 && v_z > surface_z * 65536.0 * 1.02 + 8.0\n"
    /* A room list's ordered pass: only fragments on (or within the slack
     * of) the nearest room surface take part. */
    "                                : v_z > surface_z * 65536.0 * (1.0 + room_slack.x) + room_slack.y)\n"
    "            discard;\n"
    "    }\n"
    "    uint code = v_state.x, tpage = v_state.y, clut = v_state.z;\n"
    "    vec3 v_color = perspective != 0 ? v_color_persp : v_color_affine;\n"
    "    vec2 v_uv = perspective != 0 ? v_uv_persp : v_uv_affine;\n"
    "    vec3 c = v_color, albedo = v_color;\n"
    "    vec4 shade = vec4(v_color, 0.5);\n"
    "    uint mask = uint(mask_set);\n"
    "    bool semi = (code & 2u) != 0u;\n"
    "    if ((code & 4u) != 0u) {\n"
    /* Interpolation at the polygon's edge pixels can land just outside its
     * UV range: stay inside it (pages are atlases of unrelated images). */
    "        uvec2 at = uvec2(clamp(floor(v_uv + 0.0001), vec2(v_bounds.xy), vec2(v_bounds.zw)));\n"
    "        uint e = v_state.w & 255u;\n"
    "        float k = (v_state.w & 256u) != 0u ? 0.5 : 1.0;\n"
    "        vec2 sh = e != 0u ? uv_shift[e].xy * k : vec2(0.0);\n"
    "        bool moving = sh != vec2(0.0);\n"
    "        if (moving) at = uvec2(uv_wrap(ivec2(floor(clamp(v_uv + 0.0001, vec2(v_bounds.xy), vec2(v_bounds.zw) + 0.999) + sh)), e, k));\n"
    "        uint t = texel_at(at.x & 255u, at.y & 255u, tpage, clut, window);\n"
    "        vec3 texel = rgb(t);\n"
    "        if (filtering != 0) {\n"
    "            vec2 q = v_uv - 0.5 + sh;\n"
    "            vec2 f = fract(q);\n"
    "            ivec2 base = ivec2(floor(q));\n"
    "            vec3 sum = vec3(0.0);\n"
    "            float cover = 0.0;\n"
    "            for (int i = 0; i < 4; i++) {\n"
    "                ivec2 s = clamp(base + ivec2(i & 1, i >> 1), ivec2(floor(vec2(v_bounds.xy) + sh)), ivec2(floor(vec2(v_bounds.zw) + sh)));\n"
    "                if (moving) s = uv_wrap(s, e, k);\n"
    "                uint tk = texel_at(uint(s.x) & 255u, uint(s.y) & 255u, tpage, clut, window);\n"
    "                float w = ((i & 1) != 0 ? f.x : 1.0 - f.x) * ((i >> 1) != 0 ? f.y : 1.0 - f.y);\n"
    "                if (tk != 0u) { sum += rgb(tk) * w; cover += w; }\n"
    "            }\n"
    "            if (cover < 0.5) discard;\n"
    "            texel = sum / cover;\n"
    "            if (t == 0u) t = 0x7fffu;\n"
    "        }\n"
    "        if (t == 0u) discard;\n"
    /* Only texels with the STP bit are semi-transparent. */
    "        semi = semi && (t & 0x8000u) != 0u;\n"
    "        mask |= t >> 15;\n"
    "        c = (code & 1u) != 0u ? texel / 31.0 : min(texel * v_color * (255.0 / 128.0), 31.0) / 31.0;\n"
    "        albedo = texel / 31.0;\n"
    "        shade = (code & 1u) != 0u ? vec4(vec3(128.0 / 255.0), 1.0) : vec4(v_color, 1.0);\n"
    "    }\n"
    "    if ((semi_pass == 1 && semi) || (semi_pass == 2 && !semi)) discard;\n"
    "    color = vec4(c, float(mask));\n"
    "    normal_out = vec4(normalize(v_normal) * 0.5 + 0.5, ghost ? 0.55 : surface);\n"
    "    albedo_out = vec4(albedo, 1.0);\n"
    "    shade_out = shade;\n"
    "    gl_FragDepth = room_order != 0 ? float(v_order) / 16777215.0 : layer_slack == 2 ? tested : depth;\n"
    "}\n";
static GLint u_bone_rotation, u_bone_translation, u_normal_matrix, u_projection, u_offset, u_rules, u_split, u_tint,
    u_clip_z, u_perspective, u_filtering, u_window, u_mask_set, u_vram, u_objects, u_zsf3, u_override_color,
    u_override_texture, u_surface, u_depth_pass, u_surface_depth, u_painter, u_surface_before, u_instance_blend,
    u_translucent, u_forced, u_semi_pass, u_surface_test, u_order, u_room_slack, u_room_order, u_layer_slack, u_cut_focus, u_cut_scale, u_instance_cut, u_uv_offset, u_uv_count, u_uv_window,
    u_uv_shift;
static GLuint element_buffer; /* PS1 draw order, streamed per list */

static int ensure_program(void)
{
    if (program)
        return 1;
    if (program_failed)
        return 0;
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        program = 1; /* vk_render.inc builds its pipelines on first use */
        return 1;
    }
#endif
    program = pc_gl_program3(vertex_source, geometry_source, fragment_source);
    if (!program) {
        program_failed = 1;
        fprintf(stderr, "[render] native drawing unavailable (shader)\n");
        return 0;
    }
#define U(name) u_##name = gl_GetUniformLocation(program, #name)
    U(normal_matrix);
    U(projection);
    U(offset);
    U(rules);
    U(split);
    U(tint);
    U(clip_z);
    U(perspective);
    U(filtering);
    U(window);
    U(mask_set);
    U(vram);
    U(objects);
    U(zsf3);
    U(override_color);
    U(override_texture);
    U(surface);
    U(depth_pass);
    U(surface_depth);
    U(painter);
    U(surface_before);
    U(instance_blend);
    U(translucent);
    U(forced);
    U(semi_pass);
    U(surface_test);
    U(order);
    U(room_slack);
    U(room_order);
    U(layer_slack);
    U(cut_focus);
    U(cut_scale);
    U(instance_cut);
#undef U
    gl_GenBuffers(1, &element_buffer);
    u_bone_rotation = gl_GetUniformLocation(program, "bone_rotation[0]");
    u_bone_translation = gl_GetUniformLocation(program, "bone_translation[0]");
    u_uv_offset = gl_GetUniformLocation(program, "uv_offset[0]");
    u_uv_count = gl_GetUniformLocation(program, "uv_count");
    u_uv_window = gl_GetUniformLocation(program, "uv_window[0]");
    u_uv_shift = gl_GetUniformLocation(program, "uv_shift[0]");
    return 1;
}

/* Model -> view for each palette entry (camera * world), uploaded as the
 * bone uniforms; the room's normal matrix from entry 0. */
static void upload_palette(const render_camera* cam, const render_transform* palette, int bones, int room)
{
    static float rot[RENDER_MAX_BONES * 9], tr[RENDER_MAX_BONES * 3];
    double r0[3][3];
    for (int b = 0; b < bones; b++) {
        const render_transform* w = &palette[b];
        double r[3][3], t[3];
        for (int a = 0; a < 3; a++) {
            for (int c = 0; c < 3; c++) {
                r[a][c] = 0;
                for (int k = 0; k < 3; k++)
                    r[a][c] += (double)cam->rotation[a][k] * w->rotation[k][c];
            }
            t[a] = cam->translation[a];
            for (int k = 0; k < 3; k++)
                t[a] += (double)cam->rotation[a][k] * w->translation[k];
        }
        for (int a = 0; a < 3; a++) {
            for (int c = 0; c < 3; c++)
                rot[b * 9 + c * 3 + a] = (float)r[a][c]; /* column-major */
            tr[b * 3 + a] = (float)t[a];
        }
        if (!b)
            memcpy(r0, r, sizeof r0);
    }
    gl_UniformMatrix3fv(u_bone_rotation, bones, GL_FALSE, rot);
    gl_Uniform3fv(u_bone_translation, bones, tr);
    if (room) {
        /* Normals: the inverse transpose (the cofactor matrix; scale does
         * not matter, the shader normalises). */
        float cof[9];
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) {
                int a1 = (a + 1) % 3, a2 = (a + 2) % 3, b1 = (b + 1) % 3, b2 = (b + 2) % 3;
                cof[b * 3 + a] = (float)(r0[a1][b1] * r0[a2][b2] - r0[a1][b2] * r0[a2][b1]);
            }
        gl_UniformMatrix3fv(u_normal_matrix, 1, GL_FALSE, cof);
    }
}
/* render_instance.cut at blend t (frame interpolation between game frames). */
static float instance_cut(const list_instance* in, float t)
{
    if (in->cut <= 0 && in->cut_previous <= 0)
        return 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float cut = in->cut_previous + (in->cut - in->cut_previous) * t;
    return cut < 0 ? 0 : cut > 1 ? 1 : cut;
}
/* Uniforms and vertex array for one instance; returns its mesh. */
static mesh_slot* use_instance(const draw_list* list, const list_instance* in, const render_target* target,
    const render_camera* cam, int pass, int* interpolated)
{
    mesh_slot* mesh = lookup(in->mesh);
    if (!mesh)
        return NULL;
    render_transform palette[RENDER_MAX_BONES];
    if (instance_palette(list, in, target->t, palette) && pass == 0)
        ++*interpolated;
    upload_palette(cam, palette, in->bones, !list->rules.objects);
    gl_Uniform4i(u_rules, list->rules.near_bucket, list->rules.far_bucket, list->rules.lod_bucket, in->bias);
    gl_Uniform2i(u_split, list->rules.subdivision, in->subdivision_level);
    gl_Uniform4i(u_override_color, in->color[0], in->color[1], in->color[2], in->override & RENDER_OVERRIDE_COLOR ? 1 : 0);
    gl_Uniform4i(u_override_texture, in->tpage, in->clut, in->override & RENDER_OVERRIDE_TEXTURE ? 1 : 0, 0);
    gl_Uniform2i(u_instance_blend, in->blend != 0, in->blend ? ((in->blend - 1) & 3) << 5 : 0);
    gl_Uniform4f(u_instance_cut, instance_cut(in, target->t), in->cut_ghost ? 1.0f : 0.0f, 0, 0);
    gl_BindVertexArray(mesh->vao);
    return mesh;
}
/* The room order key's sequence (within a bucket): the list's polygons in
 * the order the game walks them, shifted right to fit 10 bits. */
static int order_shift(const draw_list* list)
{
    int total = 0, shift = 0;
    for (int i = 0; i < list->count; i++) {
        mesh_slot* mesh = lookup(list->instances[i].mesh);
        if (mesh)
            total += mesh->polygons;
    }
    while (shift < 20 && (total - 1) >> shift >= 1024)
        ++shift;
    return shift;
}
static void draw_instances(const draw_list* list, const render_target* target, const render_camera* cam, int pass,
    int* interpolated)
{
    int painter = list->order_used > 0 && list->rules.objects && target->surface_before;
    gl_Uniform1i(u_painter, painter);
    if (!painter) {
        int base = 0, shift = order_shift(list);
        for (int i = 0; i < list->count; i++) {
            if (list->instances[i].blend)
                continue; /* wholly semi-transparent: drawn at translucent markers */
            mesh_slot* mesh = use_instance(list, &list->instances[i], target, cam, pass, interpolated);
            if (mesh) {
                gl_Uniform1i(u_order, base | shift << 24);
                gl_DrawArrays(GL_LINES_ADJACENCY, 0, mesh->polygons * 4);
                base += mesh->polygons;
            }
        }
    } else {
        /* PS1 order: each polygon paints over the ones before it (across
         * the list's instances); only the surfaces from before the list
         * hide it. One element range per run of the same instance. */
        static uint32_t* indices;
        static int capacity;
        if (list->order_used * 4 > capacity) {
            capacity = list->order_used * 4;
            uint32_t* grown = realloc(indices, (size_t)capacity * sizeof *indices);
            if (!grown)
                return;
            indices = grown;
        }
        for (int k = 0; k < list->order_used; k++)
            for (int c = 0; c < 4; c++)
                indices[k * 4 + c] = (list->order[k] & 0xffff) * 4 + (unsigned)c;
        gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
        gl_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)list->order_used * 4 * sizeof *indices), indices,
            GL_STREAM_DRAW);
        gl_DepthFunc(GL_ALWAYS);
        int current = -1;
        mesh_slot* mesh = NULL;
        for (int k = 0; k < list->order_used;) {
            int instance = (int)(list->order[k] >> 16), run = k;
            while (run < list->order_used && (int)(list->order[run] >> 16) == instance)
                ++run;
            if (instance < list->count && !list->instances[instance].blend) {
                if (instance != current) {
                    mesh = use_instance(list, &list->instances[instance], target, cam, pass, interpolated);
                    current = instance;
                    /* the element buffer is vertex array state */
                    gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
                }
                int valid = mesh != NULL;
                for (int q = k; valid && q < run; q++)
                    valid = (int)(list->order[q] & 0xffff) < mesh->polygons;
                if (valid)
                    gl_DrawElements(GL_LINES_ADJACENCY, (run - k) * 4, GL_UNSIGNED_INT,
                        (void*)(uintptr_t)((size_t)k * 4 * sizeof *indices));
            }
            k = run;
        }
        gl_BindVertexArray(0);
        gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        gl_DepthFunc(pass == 0 ? GL_LESS : GL_ALWAYS);
    }
    if (target->count_stats && pass == 0)
        for (int i = 0; i < list->count; i++) {
            mesh_slot* mesh = lookup(list->instances[i].mesh);
            if (!mesh)
                continue;
            if (list->rules.objects) {
                counting.object_instances++;
                counting.object_triangles += mesh->triangles;
            } else if (list->rules.layered) {
                counting.layered_instances++;
            } else {
                counting.instances++;
                counting.native_triangles += mesh->triangles;
            }
        }
}

/* Objects and other room blocks lying on room surfaces (pc_native_room_order):
 * 1 when they test with the slack. */
static int layer_slack(const draw_list* list)
{
    return pc_native_room_order && (list->rules.objects || list->rules.layered) && !list->rules.effects;
}
/* State shared by list and translucent draws: framebuffer, viewport,
 * drawing area, program and the list's uniforms. */
static const render_camera* begin_draw(const draw_list* list, const render_target* target, GLuint fbo)
{
    const render_camera* cam = target->camera ? target->camera : &list->camera;
    int scale = target->scale;
    const int* clip = target->clip;
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_Viewport(0, 0, 1024 * scale, 512 * scale);
    gl_Enable(GL_SCISSOR_TEST);
    gl_Scissor(clip[0] * scale, clip[1] * scale, clip[2] > 0 ? clip[2] * scale : 0, clip[3] > 0 ? clip[3] * scale : 0);
    gl_DepthMask(GL_TRUE);
    gl_Enable(GL_DEPTH_TEST);
    gl_DepthFunc(GL_LESS);
    gl_Disable(GL_BLEND);
    gl_UseProgram(program);
    gl_ActiveTexture(GL_TEXTURE1); /* never the depth attached for pass 1 */
    gl_BindTexture(GL_TEXTURE_2D, 0);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, target->vram);
    gl_Uniform1i(u_vram, 0);
    gl_Uniform1i(u_surface_depth, 1);
    gl_Uniform1i(u_surface_before, 2);
    gl_Uniform1i(u_depth_pass, 0);
    gl_Uniform1i(u_painter, 0);
    gl_Uniform1i(u_translucent, 0);
    gl_Uniform2i(u_forced, 0, 0);
    gl_Uniform1i(u_semi_pass, 0);
    gl_Uniform1i(u_surface_test, 0);
    gl_Uniform1i(u_order, 0);
    gl_Uniform1i(u_room_order, 0);
    gl_Uniform2f(u_room_slack, pc_native_room_slack[0], pc_native_room_slack[1]);
    gl_Uniform1i(u_layer_slack, layer_slack(list));
    gl_Uniform1i(u_perspective, target->perspective);
    gl_Uniform1i(u_filtering, target->filtering);
    gl_Uniform1ui(u_window, target->window);
    gl_Uniform1i(u_mask_set, target->mask_set);
    float hx = cam->h * (list->camera.widescreen ? 0.75f : 1.0f);
    gl_Uniform4f(u_projection, hx, cam->h, cam->ofx + (float)target->ox, cam->ofy + (float)target->oy);
    gl_Uniform2f(u_offset, cam->ofx, cam->ofy);
    gl_Uniform4f(u_tint, (float)list->tint[0], (float)list->tint[1], (float)list->tint[2], (float)list->tint[3]);
    gl_Uniform2f(u_clip_z, 1.0f, 65536.0f);
    gl_Uniform1i(u_objects, list->rules.objects != 0);
    gl_Uniform1i(u_zsf3, list->rules.zsf3);
    gl_Uniform1f(u_surface, list->rules.objects ? 0.75f : 1.0f);
    float cut_focus[4], cut_scale[4];
    cutaway_params(list, cam, cut_focus, cut_scale);
    gl_Uniform4fv(u_cut_focus, 1, cut_focus);
    gl_Uniform4fv(u_cut_scale, 1, cut_scale);
    /* Animated materials (render_list_uv_effects). */
    static uv_effect_gpu table[RENDER_MAX_UV_EFFECTS + 1];
    static GLint offsets[(RENDER_MAX_UV_EFFECTS + 1) * 4], windows[(RENDER_MAX_UV_EFFECTS + 1) * 4];
    static GLfloat shifts[(RENDER_MAX_UV_EFFECTS + 1) * 4];
    int entries = uv_effect_table(list, target->t, table);
    for (int i = 0; i < entries; i++)
        for (int k = 0; k < 4; k++) {
            offsets[i * 4 + k] = table[i].offset[k];
            windows[i * 4 + k] = table[i].window[k];
            shifts[i * 4 + k] = table[i].shift[k];
        }
    gl_Uniform1i(u_uv_count, entries);
    gl_Uniform4iv(u_uv_offset, entries, offsets);
    gl_Uniform4iv(u_uv_window, entries, windows);
    gl_Uniform4fv(u_uv_shift, entries, shifts);
    return cam;
}
static void end_draw(void)
{
    gl_BindVertexArray(0);
    gl_Disable(GL_DEPTH_TEST);
    gl_Disable(GL_SCISSOR_TEST);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int draw_room_ordered(draw_list* list, const render_target* target)
{
    static const float far_depth = 1.0f;
    const render_camera* cam = begin_draw(list, target, target->object_fbo);
    uv_trace_record(list, target->t);
    int interpolated = 0;
    /* A: the nearest surface into the surfaces' depth. */
    gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
    gl_ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    draw_instances(list, target, cam, 0, &interpolated);
    gl_Disable(GL_SCISSOR_TEST);
    if (target->surface_sync) /* MSAA: pass A's depth into the surfaces pass B reads */
        target->surface_sync();
    gl_Enable(GL_SCISSOR_TEST);
    /* B: colour and G-buffer, PS1 order among the surfaces on it. */
    gl_BindFramebuffer(GL_FRAMEBUFFER, target->fbo);
    gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
    gl_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, target->surface_depth);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_Uniform1i(u_surface_test, 4);
    gl_Uniform1i(u_room_order, 1);
    draw_instances(list, target, cam, 1, &interpolated);
    gl_Uniform1i(u_surface_test, 0);
    gl_Uniform1i(u_room_order, 0);
    /* C: the real depth of the surfaces drawn. */
    gl_ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gl_DepthFunc(GL_ALWAYS);
    gl_Uniform1i(u_depth_pass, 1);
    draw_instances(list, target, cam, 1, &interpolated);
    gl_Uniform1i(u_depth_pass, 0);
    gl_DepthFunc(GL_LESS);
    gl_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, 0);
    gl_ActiveTexture(GL_TEXTURE0);
    end_draw();
    if (target->count_stats)
        counting.lists++;
    else
        counting.interpolated_instances += interpolated;
    return list->count;
}
/* Draw list `id` into the target (render_gl.h). A room list resets depth
 * over the drawing area first: in the ordering table, what came before it
 * is behind it. An object list (or a layered room list) is depth-tested
 * against the native surfaces drawn so far (target->surface_depth) and adds
 * itself to them. Returns the instances drawn, or -1 when the list is gone. */
int render_draw_list(uint32_t id, const render_target* target)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return vk_draw_list(id, target);
#endif
    draw_list* list = find_list(id);
    if (!list || !program)
        return -1;
    int layered = list->rules.objects || list->rules.layered;
    if (layered && (!target->object_fbo || !target->surface_depth))
        return -1;
    /* A room list in PS1 order (pc_native_room_order): pass A finds the
     * nearest room surface per pixel (the native surfaces' depth, through
     * object_fbo); pass B paints the polygons on it (within the slack) with
     * the PS1's draw order deciding between them (the order key as depth,
     * smaller paints later); pass C writes the winning depth into the frame
     * (what post effects, lighting and the copy to the surfaces read). Real
     * occlusion stays per pixel; only near-coplanar surfaces (a document on
     * a table, a notice on a wall) follow the ordering table. */
    if (!layered && pc_native_room_order && target->object_fbo && target->surface_depth)
        return draw_room_ordered(list, target);
    const render_camera* cam = begin_draw(list, target, layered ? target->object_fbo : target->fbo);
    uv_trace_record(list, target->t);
    if (!layered) {
        static const float far_depth = 1.0f;
        gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
    }
    if (list->rules.objects) {
        gl_ActiveTexture(GL_TEXTURE2);
        gl_BindTexture(GL_TEXTURE_2D, target->surface_before);
        gl_ActiveTexture(GL_TEXTURE0);
    }
    int interpolated = 0;
    if (layered && layer_slack(list))
        gl_Uniform1i(u_layer_slack, 2); /* pass 1 writes the surfaces' depth */
    draw_instances(list, target, cam, 0, &interpolated);
    if (layered) {
        gl_Uniform1i(u_layer_slack, layer_slack(list));
        /* Pass 2: the visible fragments' depth into the frame's depth. */
        if (target->surface_sync) { /* MSAA: pass 1's depth into the surfaces pass 2 reads */
            gl_Disable(GL_SCISSOR_TEST);
            target->surface_sync();
            gl_Enable(GL_SCISSOR_TEST);
        }
        gl_BindFramebuffer(GL_FRAMEBUFFER, target->fbo);
        gl_ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_DepthFunc(GL_ALWAYS);
        gl_ActiveTexture(GL_TEXTURE1);
        gl_BindTexture(GL_TEXTURE_2D, target->surface_depth);
        gl_ActiveTexture(GL_TEXTURE0);
        gl_Uniform1i(u_depth_pass, 1);
        draw_instances(list, target, cam, 1, &interpolated);
        gl_Uniform1i(u_depth_pass, 0);
        gl_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl_DepthFunc(GL_LESS);
        gl_ActiveTexture(GL_TEXTURE1);
        gl_BindTexture(GL_TEXTURE_2D, 0);
        gl_ActiveTexture(GL_TEXTURE2);
        gl_BindTexture(GL_TEXTURE_2D, 0);
        gl_ActiveTexture(GL_TEXTURE0);
    }
    end_draw();
    if (target->count_stats) {
        if (list->rules.objects)
            counting.object_lists++;
        else if (list->rules.layered)
            counting.layered_lists++;
        else
            counting.lists++;
    } else
        counting.interpolated_instances += interpolated;
    return list->count;
}

/* The PS1's semi-transparency equations (as gpu_hw.c's set_blend). */
static void set_blend(int mode)
{
    gl_Enable(GL_BLEND);
    gl_BlendEquation(mode == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
    switch (mode) {
    case 0: /* B/2 + F/2 */
        gl_BlendColor(0, 0, 0, 0.5f);
        gl_BlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
        break;
    case 3: /* B + F/4 */
        gl_BlendColor(0, 0, 0, 0.25f);
        gl_BlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
        break;
    default: /* B + F, B - F */
        gl_BlendFunc(GL_ONE, GL_ONE);
        break;
    }
}
/* Entries [first, first + count) of the list's translucent sequence
 * (render_gl.h). */
int render_draw_translucent(uint32_t id, int first, int count, const render_target* target)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return vk_draw_translucent(id, first, count, target);
#endif
    draw_list* list = find_list(id);
    if (!list || !program || !target->surface_depth)
        return -1;
    if (first < 0)
        first = 0;
    if (first + count > list->translucent_used)
        count = list->translucent_used - first;
    if (count <= 0)
        return 0;
    static uint32_t* indices;
    static int capacity;
    if (count * 4 > capacity) {
        int grown_capacity = count * 4 > 1024 ? count * 4 : 1024;
        uint32_t* grown = realloc(indices, (size_t)grown_capacity * sizeof *indices);
        if (!grown)
            return -1;
        indices = grown;
        capacity = grown_capacity;
    }
    const uint32_t* entries = list->translucent + first;
    const uint8_t* flags = list->translucent_flags + first;
    for (int k = 0; k < count; k++)
        for (int c = 0; c < 4; c++)
            indices[k * 4 + c] = (entries[k] & 0xffff) * 4 + (unsigned)c;
    const render_camera* cam = begin_draw(list, target, target->fbo);
    gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
    gl_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * 4 * sizeof *indices), indices, GL_STREAM_DRAW);
    gl_DepthFunc(GL_ALWAYS); /* PS1 order; hidden only behind the surfaces before */
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, target->surface_depth);
    gl_ActiveTexture(GL_TEXTURE0);
    int effects = list->rules.effects;
    gl_Uniform1i(u_translucent, effects ? 2 : 1);
    gl_Uniform1i(u_surface_test, list->rules.objects ? 1 : effects ? 3 : 2);
    if (effects)
        gl_Uniform1f(u_surface, 0.0f); /* unlit: effects light nothing up themselves */
    int interpolated = 0, drawn = 0, current = -1;
    mesh_slot* mesh = NULL;
    for (int k = 0; k < count;) {
        /* A run: one instance, the same flags and blend mode (one GL blend
         * state; the packet path batches the same way). */
        int instance = (int)(entries[k] >> 16);
        if (instance >= list->count) {
            ++k;
            continue;
        }
        const list_instance* in = &list->instances[instance];
        if (instance != current) {
            mesh = use_instance(list, in, target, cam, 0, &interpolated);
            current = instance;
            gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
        }
        int mode = -1, run = k;
        for (; run < count && (int)(entries[run] >> 16) == instance && flags[run] == flags[k]; run++) {
            int polygon = (int)(entries[run] & 0xffff);
            if (!mesh || polygon >= mesh->polygons)
                break;
            int m = (int)(effective_tpage(in, &mesh->cpu[polygon]) >> 5) & 3;
            if (mode >= 0 && m != mode)
                break;
            mode = m;
        }
        if (run == k) { /* a stale entry */
            ++k;
            continue;
        }
        gl_Uniform2i(u_forced, (flags[k] & RENDER_TRANSLUCENT_DECIDED) != 0, (flags[k] & RENDER_TRANSLUCENT_HALVE) != 0);
        void* offset = (void*)(uintptr_t)((size_t)k * 4 * sizeof *indices);
        /* Opaque texels (no STP bit) as opaque surfaces... */
        gl_Uniform1i(u_semi_pass, 1);
        gl_DepthMask(GL_TRUE);
        gl_DrawElements(GL_LINES_ADJACENCY, (run - k) * 4, GL_UNSIGNED_INT, offset);
        /* ...then the semi-transparent ones blended, keeping the depth and
         * the surface behind them. */
        gl_Uniform1i(u_semi_pass, 2);
        set_blend(mode);
        gl_DepthMask(GL_FALSE);
        gl_ColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_ColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_ColorMaski(3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl_DrawElements(GL_LINES_ADJACENCY, (run - k) * 4, GL_UNSIGNED_INT, offset);
        gl_Disable(GL_BLEND);
        gl_ColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl_ColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl_ColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        drawn += run - k;
        k = run;
    }
    gl_DepthMask(GL_TRUE);
    gl_BindVertexArray(0);
    gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gl_Uniform1i(u_translucent, 0);
    gl_Uniform1i(u_semi_pass, 0);
    gl_Uniform1i(u_surface_test, 0);
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, 0);
    gl_ActiveTexture(GL_TEXTURE0);
    end_draw();
    if (target->count_stats) {
        if (effects == RENDER_EFFECTS_SHADOWS) {
            counting.shadow_markers++;
            counting.shadow_polygons += drawn;
        } else if (effects) {
            counting.effect_markers++;
            counting.effect_polygons += drawn;
        } else {
            counting.translucent_markers++;
            counting.translucent_polygons += drawn;
        }
    }
    return drawn;
}
#ifdef ROOD_VULKAN
#include "vk_render.inc"
#endif
