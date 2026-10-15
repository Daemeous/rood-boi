/* Native characters, weapons and props (phase 2 steps 2 and 3; design
 * notes in pc/docs/Native_Rendering.md).
 *
 * Every skinned model the battle code draws goes through func_800B3A68
 * (actor_vertices.c: bone vertex groups through the per-bone GTE matrices
 * the game computed) and then one packet builder:
 *
 *   func_800B3CF0 (weapon_packets.c)  flat-lit textured records, the model's
 *                                     colour from the room lighting
 *   func_800B4594 (actor_packets.c)   per-vertex coloured records
 *   func_800B516C (weapon_packets.c)  the flat records in material groups
 *                                     (alternate CLUT per group), optionally
 *                                     the whole model semi-transparent
 *   func_800B5C30 (actor_packets.c)   the same for the coloured records
 *
 * All four call in here once their packets are written:
 *
 * - the model's records become a render.h mesh with a bone index per vertex
 *   (built once and cached by the model's address and a hash of its data,
 *   so a model buffer that is reloaded with something else gets a new mesh;
 *   meshes unused for a while are destroyed, generation handles keep old
 *   lists from drawing a reused slot);
 * - the per-bone matrices (camera-composed, as the GTE used them) become a
 *   world-space bone palette: camera^-1 * matrix, the room's convention; the
 *   same model's palette from the frame before goes along for frame
 *   interpolation;
 * - one list per model draw, its marker linked into the ordering table at
 *   the model's farthest bucket (before all of its packets), and the packet
 *   range reported to the hardware path, which skips the packets the native
 *   draws cover (pc_native_actors 1: the CPU's reference VRAM still gets
 *   them);
 * - semi-transparent polygons (record flag 16, and every polygon of a
 *   blended model) go into the list's translucent sequence, one translucent
 *   marker per ordering-table bucket they fall into, linked at that bucket
 *   in front of the model's packets there (pc_native_blend): they draw
 *   exactly where the PS1 draws them relative to everything else. */
#define _POSIX_C_SOURCE 200809L
#include "psx_types.h"
#include "render.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned int CFC2(int reg);
extern float pc_gte_view_ofx(void);
extern int pc_gte_widescreen;
extern int pc_hw_active;
extern void pc_hw_object_packets(const void* start, const void* end, uint32_t list) __attribute__((weak));
extern void render_count_object_mesh_built(void);
/* emissive.cpp: the model a mesh draws (its content hash: emissive scopes). */
extern void pc_emissive_model_mesh(uint32_t mesh, uint32_t model) __attribute__((weak));
extern int pc_live_camera_active(void) __attribute__((weak));

typedef struct {
    short m[3][3];
    short pad;
    int t[3];
} gte_matrix; /* libgte MATRIX */

/* The packet builders (pc_object_native_draw's `kind`). */
enum {
    KIND_FLAT, /* func_800B3CF0 */
    KIND_COLORED, /* func_800B4594 */
    KIND_FLAT_GROUPS, /* func_800B516C */
    KIND_COLORED_GROUPS, /* func_800B5C30 */
};
#define COLORED(kind) ((kind) == KIND_COLORED || (kind) == KIND_COLORED_GROUPS)
#define GROUPS(kind) ((kind) >= KIND_FLAT_GROUPS)
static const char* const kind_names[4] = { "flat", "coloured", "flat grouped", "coloured grouped" };

/* ---- the model being drawn (func_800B3A68) ---- */

static const void *last_object, *last_model;
static const gte_matrix* last_matrices;
void pc_object_native_matrices(const void* object, const void* matrices, const void* model)
{
    last_object = object;
    last_matrices = matrices;
    last_model = model;
}

/* ---- meshes ---- */

/* Per mesh polygon, what its ordering-table bucket needs each frame. */
typedef struct {
    uint16_t vertex[4];
    uint8_t quad;
    uint8_t semi; /* semi-transparent record (drawn at translucent markers) */
    uint8_t twosided; /* never culled (double-sided or split) */
    int8_t offset; /* OT index - depth: -1 plain, bias - 5 mixed */
    int8_t bias; /* added to the depth before the 0x7c0 test */
} sort_info;
typedef struct {
    const void* model;
    int kind;
    unsigned hash;
    render_mesh mesh; /* 0: the model could not be built (stays packets) */
    int bones;
    uint32_t used; /* frame serial */
    sort_info* sort; /* one per mesh polygon */
    int polygons, semi;
} model_mesh;
#define MESH_CACHE 256
static model_mesh cache[MESH_CACHE];
static int cache_count;

typedef struct {
    const unsigned* groups;
    unsigned group_count, vertex_count;
    const short* vertices; /* SVECTOR: x, y, z, pad */
    const unsigned* records;
    unsigned short counts[3]; /* triangles, quads, mixed (object bytes 2..7) */
    /* grouped builders: material group counts at the model, the object's
     * alternate-CLUT mask (byte 0x64) */
    const unsigned short* group_counts;
    unsigned mask;
} model_view;

static unsigned hash_words(unsigned hash, const unsigned* words, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        hash = (hash ^ words[i]) * 16777619u;
    return hash;
}
/* Words of the polygon records, walked as the packet builders walk them
 * when every face is drawn; 0 when a record's own size disagrees (the PS1
 * would walk such data differently depending on culling). */
static unsigned record_words(const model_view* m, int kind)
{
    const unsigned* r = m->records;
    for (int section = 0; section < 3; section++)
        for (unsigned face = 0; face < m->counts[section]; face++) {
            int quad;
            unsigned words;
            if (!COLORED(kind)) {
                quad = section == 1 || (section == 2 && (r[0] & 255) == 0x2c);
                words = quad ? 5 : 4;
                if (section == 2 && ((r[0] >> 8) & 255) != words * 4)
                    return 0;
            } else {
                quad = section == 1 || (section == 2 && (r[2] >> 24) == 0x3c);
                words = quad ? 8 : 6;
                if (section == 2 && (r[3] >> 24) != words * 4)
                    return 0;
            }
            r += words;
        }
    return (unsigned)(r - m->records);
}
/* The grouped builders' material groups (func_800B516C next_group,
 * func_800B5C30 actor_blend_group): per section up to eight counts read
 * from the model (alternate CLUT when the mask bit for the group is set),
 * then the remainder of the section with the model's CLUT. func_800B516C
 * skips an empty section's eight counts, func_800B5C30 does not. Writes 1
 * per record that takes the alternate CLUT; returns 0 when the counts
 * disagree with the sections. */
static int group_cluts(const model_view* m, int kind, uint8_t* alternate)
{
    const unsigned short* counts = m->group_counts;
    unsigned record = 0;
    for (int section = 0; section < 3; section++) {
        unsigned total = m->counts[section];
        if (!total) {
            if (kind == KIND_FLAT_GROUPS && section < 2)
                counts += 8;
            continue;
        }
        int counter = 9, sum = 0;
        unsigned done = 0;
        while (--counter >= 0) {
            int count, alt = 0;
            if (!counter) {
                count = (int)total - sum;
                if (!count)
                    break;
            } else {
                count = section < 2 ? *(const short*)counts : *counts;
                ++counts;
                sum += count;
                if (!count)
                    continue;
                alt = (m->mask >> (counter - 1)) & 1;
            }
            if (count < 0 || done + (unsigned)count > total)
                return 0;
            memset(alternate + record + done, alt, (size_t)count);
            done += (unsigned)count;
        }
        if (done != total)
            return 0;
        record += total;
    }
    return 1;
}
static int vertex_of(const model_view* m, unsigned offset, unsigned* index)
{
    if ((offset & 3) || (offset >> 2) >= m->vertex_count)
        return 0;
    *index = offset >> 2;
    return 1;
}
/* Decode the model's polygons (bone space, a bone per vertex). */
static render_mesh build_mesh(const model_view* m, int kind, const unsigned char* bone_of, int* bones,
    sort_info** sort_out, int* polygons_out, int* semi_out)
{
    unsigned total = (unsigned)m->counts[0] + m->counts[1] + m->counts[2];
    render_polygon* polygons = calloc(total ? total : 1, sizeof *polygons);
    sort_info* sort = calloc(total ? total : 1, sizeof *sort);
    uint8_t* alternate = calloc(total ? total : 1, 1);
    if (!polygons || !sort || !alternate) {
        free(polygons);
        free(sort);
        free(alternate);
        return 0;
    }
    int count = 0, ok = 1, semi = 0;
    if (GROUPS(kind))
        ok = group_cluts(m, kind, alternate);
    const unsigned* r = m->records;
    unsigned record = 0;
    for (int section = 0; section < 3 && ok; section++)
        for (unsigned face = 0; face < m->counts[section] && ok; face++, record++) {
            render_polygon* p = &polygons[count];
            memset(p, 0, sizeof *p);
            unsigned offsets[4], uv[4], colors[4] = { 0, 0, 0, 0 }, flags = 0, code;
            int quad, split = 0;
            if (!COLORED(kind)) {
                quad = section == 1 || (section == 2 && (r[0] & 255) == 0x2c);
                if (section == 2)
                    flags = r[0] >> 16;
                offsets[0] = r[1] & 0xffff, offsets[1] = r[1] >> 16;
                offsets[2] = r[2] & 0xffff, offsets[3] = r[2] >> 16;
                uv[0] = r[3] & 0xffff, uv[1] = r[3] >> 16;
                uv[2] = quad ? r[4] & 0xffff : r[2] >> 16;
                uv[3] = quad ? r[4] >> 16 : 0;
                split = section == 2 && (flags & 3) == 3;
                if (split && !quad)
                    ok = 0;
                code = quad ? 0x2c : 0x24;
                for (int k = 0; k < 4; k++)
                    colors[k] = 0x808080; /* replaced by the model's colour */
                r += quad ? 5 : 4;
            } else {
                quad = section == 1 || (section == 2 && (r[2] >> 24) == 0x3c);
                if (section == 2)
                    flags = r[4] >> 24;
                offsets[0] = r[0] & 0xffff, offsets[1] = r[0] >> 16;
                offsets[2] = r[1] & 0xffff, offsets[3] = r[1] >> 16;
                for (int k = 0; k < (quad ? 4 : 3); k++)
                    colors[k] = r[2 + k];
                unsigned uv01 = r[quad ? 6 : 5], uv23 = quad ? r[7] : r[1] >> 16;
                uv[0] = uv01 & 0xffff, uv[1] = uv01 >> 16, uv[2] = uv23 & 0xffff, uv[3] = uv23 >> 16;
                code = r[2] >> 24;
                r += quad ? 8 : 6;
            }
            /* Record flag 16: semi-transparent with the record's blend bits
             * (func_800B5C30 keeps neither: only the code's own bit). */
            unsigned page_bits = flags & 0x60;
            if (kind == KIND_COLORED_GROUPS)
                page_bits = 0;
            else if (flags & 16)
                code |= 2;
            for (int k = 0; k < (quad ? 4 : 3) && ok; k++) {
                unsigned v;
                if (!vertex_of(m, offsets[k], &v)) {
                    ok = 0;
                    break;
                }
                const short* xyz = m->vertices + v * 4;
                p->position[k][0] = xyz[0];
                p->position[k][1] = xyz[1];
                p->position[k][2] = xyz[2];
                p->bone[k] = bone_of[v];
                sort[count].vertex[k] = (uint16_t)v;
                p->uv[k][0] = uv[k] & 0xff;
                p->uv[k][1] = (uv[k] >> 8) & 0xff;
                p->rgb[k][0] = colors[k] & 0xff;
                p->rgb[k][1] = (colors[k] >> 8) & 0xff;
                p->rgb[k][2] = (colors[k] >> 16) & 0xff;
            }
            p->tpage = (uint16_t)page_bits; /* the model's page is the instance's */
            p->clut = alternate[record] ? 0x480 : 0; /* added to the instance's CLUT */
            p->code = (uint8_t)code;
            p->flags = (uint8_t)((quad ? RENDER_POLY_QUAD : 0) | (split ? RENDER_POLY_SPLIT : 0)
                | (!split && (flags & 1) ? RENDER_POLY_DOUBLE_SIDED : 0));
            p->bias = (uint8_t)(split ? 4 : section == 2 ? ((flags >> 2) & 3) * 4 : 0);
            sort[count].quad = (uint8_t)quad;
            sort[count].semi = (code & 2) != 0;
            sort[count].twosided = (uint8_t)((flags & 1) != 0);
            sort[count].offset = (int8_t)(section == 2 && !split ? (int)((flags >> 2) & 3) * 4 - 5 : -1);
            sort[count].bias = (int8_t)(section == 2 && !split ? (int)((flags >> 2) & 3) * 4 : 0);
            semi += (code & 2) != 0;
            ++count;
        }
    free(alternate);
    render_mesh mesh = ok && count ? render_mesh_create(polygons, count) : 0;
    if (mesh) {
        *sort_out = sort;
        *polygons_out = count;
        *semi_out = semi;
    } else
        free(sort);
    {
        int quads = 0, sided = 0, split = 0, layered = 0;
        for (int i = 0; i < count; i++) {
            quads += !!(polygons[i].flags & RENDER_POLY_QUAD);
            sided += !!(polygons[i].flags & RENDER_POLY_DOUBLE_SIDED);
            split += !!(polygons[i].flags & RENDER_POLY_SPLIT);
            layered += polygons[i].bias != 0;
        }
        fprintf(stderr,
            "[render] %s model: %u/%u/%u records, %d polygons (%d semi-transparent, %d quads, %d double-sided, %d "
            "split, %d biased), %u vertices%s\n",
            kind_names[kind], m->counts[0], m->counts[1], m->counts[2], count, semi, quads, sided, split, layered,
            m->vertex_count, mesh ? "" : ": stays packets");
    }
    free(polygons);
    int most = 0;
    for (unsigned v = 0; v < m->vertex_count; v++)
        most = bone_of[v] + 1 > most ? bone_of[v] + 1 : most;
    *bones = most;
    return mesh;
}
static model_mesh* mesh_for(const void* object, const void* model, int kind, uint32_t serial)
{
    model_view m;
    const unsigned char* obj = object;
    m.group_count = obj[1];
    memcpy(m.counts, obj + 2, sizeof m.counts);
    m.groups = psx_load(const unsigned*, (const char*)model + 0x34);
    m.vertices = psx_load(const short*, (const char*)model + 0x38);
    m.records = psx_load(const unsigned*, (const char*)model + 0x3c);
    m.group_counts = model;
    m.mask = GROUPS(kind) ? obj[0x64] : 0;
    if (!m.group_count || !m.groups || !m.vertices || !m.records)
        return NULL;
    m.vertex_count = m.groups[m.group_count - 1] >> 16;
    if (!m.vertex_count || m.vertex_count > 768)
        return NULL;
    unsigned words = record_words(&m, kind);
    unsigned hash = (2166136261u ^ (unsigned)kind) * 16777619u;
    hash = hash_words(hash, (const unsigned*)(const void*)obj, 2); /* group count, section counts */
    hash = hash_words(hash, m.groups, m.group_count);
    hash = hash_words(hash, (const unsigned*)(const void*)m.vertices, m.vertex_count * 2);
    hash = hash_words(hash, m.records, words);
    if (GROUPS(kind)) {
        hash = (hash ^ m.mask) * 16777619u;
        hash = hash_words(hash, (const unsigned*)model, 12); /* 24 group counts */
    }
    model_mesh* oldest = NULL;
    for (int i = 0; i < cache_count; i++) {
        model_mesh* c = &cache[i];
        if (c->model == model && c->kind == kind && c->hash == hash) {
            c->used = serial;
            return c;
        }
        if (!oldest || (uint32_t)(serial - c->used) > (uint32_t)(serial - oldest->used))
            oldest = c;
    }
    model_mesh* c;
    if (cache_count < MESH_CACHE)
        c = &cache[cache_count++];
    else {
        c = oldest;
        render_mesh_destroy(c->mesh);
        free(c->sort);
    }
    memset(c, 0, sizeof *c);
    c->model = model;
    c->kind = kind;
    c->hash = hash;
    c->used = serial;
    if (!words && (m.counts[0] || m.counts[1] || m.counts[2])) {
        static int logged;
        if (logged++ < 8)
            fprintf(stderr, "[render] model %p: record sizes disagree with their kind; stays packets\n", model);
        return c;
    }
    unsigned char bone_of[768];
    unsigned cursor = 0;
    for (unsigned g = 0; g < m.group_count; g++) {
        unsigned end = m.groups[g] >> 16, bone = m.groups[g] & 0xffff;
        if (end < cursor || end > m.vertex_count || bone >= RENDER_MAX_BONES)
            return c;
        for (; cursor < end; cursor++)
            bone_of[cursor] = (unsigned char)bone;
    }
    c->mesh = build_mesh(&m, kind, bone_of, &c->bones, &c->sort, &c->polygons, &c->semi);
    if (c->mesh && pc_emissive_model_mesh)
        pc_emissive_model_mesh(c->mesh, hash);
    render_count_object_mesh_built();
    return c;
}
/* Meshes of models not drawn for a while go (their buffers may hold
 * something else by now). */
static void evict(uint32_t serial)
{
    for (int i = 0; i < cache_count;) {
        if ((uint32_t)(serial - cache[i].used) > 120) {
            render_mesh_destroy(cache[i].mesh);
            free(cache[i].sort);
            cache[i] = cache[--cache_count];
        } else
            ++i;
    }
}

/* ---- palettes: this frame's, and the frame before's for interpolation ---- */

typedef struct {
    const void *object, *model, *matrices;
    int occurrence; /* the same model drawn again in one frame */
    uint32_t serial;
    int bones;
    render_transform palette[RENDER_MAX_BONES];
    /* Held poses (pc_interp_holds): the segment being blended (from `from`
     * to `palette` over `frames` game frames, `k` of them shown so far),
     * the frames the pose has been held since it last changed, the hold
     * before that change (1-3, else 0) and how many changes in a row came
     * after such a hold. */
    render_transform from[RENDER_MAX_BONES];
    int frames, k, held, last_hold, streak;
} palette_record;
#define PALETTE_RECORDS 128
static palette_record records[PALETTE_RECORDS];
static int record_count;

/* Poses held for some game frames between changes: an animation keyed (or
 * stepped by a counter) below the game's frame rate, e.g. idle breathing in
 * event scenes changes every other frame at 30 fps. Frame-to-frame blending
 * then moves in one frame and stands still the next. When the last changes
 * came after holds (1-3 frames each), the next change is spread over the
 * hold: blended from the pose shown when it arrived to the new one across
 * hold + 1 game frames (the shorter of the last two holds; one frame of
 * extra lag for those slow motions, none for anything that changes every
 * frame). A change that comes sooner starts from the pose shown at that
 * moment (no snap). */
int pc_interp_holds = 1;
/* Unchanged: the same matrices from the game (only float noise; a moving
 * camera's fixed-point rounding already counts as a change). */
static int palettes_equal(const render_transform* a, const render_transform* b, int bones)
{
    for (int i = 0; i < bones; i++)
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++)
                if (fabsf(a[i].rotation[r][c] - b[i].rotation[r][c]) > 1e-5f)
                    return 0;
            if (fabsf(a[i].translation[r] - b[i].translation[r]) > 0.01f)
                return 0;
        }
    return 1;
}
/* The record holds the previous frame's palette; `palette` is this frame's.
 * Sets what to blend from and the instance's blend mapping. */
static void hold_segment(palette_record* rec, const render_transform* palette, int bones, render_transform* from,
    render_instance* instance)
{
    int segment = rec->frames > 1 && rec->k + 1 < rec->frames;
    if (palettes_equal(rec->palette, palette, bones)) {
        rec->held++;
        if (segment) {
            /* the hold continues the blend toward the pose it reached */
            rec->k++;
            memcpy(from, rec->from, (size_t)bones * sizeof *from);
            instance->blend_offset = (float)rec->k;
            instance->blend_scale = 1.0f / (float)rec->frames;
            return;
        }
        memcpy(from, rec->palette, (size_t)bones * sizeof *from);
        rec->frames = 1;
        return;
    }
    /* A change: the pose shown at its arrival (the segment's end so far). */
    if (segment)
        render_blend_palette(rec->from, rec->palette, bones, (float)(rec->k + 1) / (float)rec->frames, from);
    else
        memcpy(from, rec->palette, (size_t)bones * sizeof *from);
    /* Holding between changes for a while (each hold 1-3 frames): spread
     * this change over the shorter of the last two holds. */
    int hold = rec->held >= 1 && rec->held <= 3 ? rec->held : 0;
    rec->streak = hold ? rec->streak + 1 : 0;
    int shorter = hold && rec->last_hold && rec->last_hold < hold ? rec->last_hold : hold;
    rec->last_hold = hold;
    rec->held = 0;
    rec->frames = pc_interp_holds && rec->streak >= 2 ? shorter + 1 : 1;
    rec->k = 0;
    memcpy(rec->from, from, (size_t)bones * sizeof *from);
    if (rec->frames > 1) {
        instance->blend_offset = 0;
        instance->blend_scale = 1.0f / (float)rec->frames;
    }
}

/* ---- per draw ---- */

static double camera_inverse[3][3], camera_translation[3];
static uint32_t camera_serial = ~0u;
static render_camera camera;

static int read_camera(uint32_t serial)
{
    const gte_matrix* view = (const gte_matrix*)(uintptr_t)0x1f800014;
    double r[3][3];
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++)
            camera.rotation[a][b] = (float)(r[a][b] = view->m[a][b] / 4096.0);
        camera.translation[a] = (float)(camera_translation[a] = view->t[a]);
    }
    camera.h = (float)(CFC2(26) & 0xffff);
    camera.ofx = pc_gte_view_ofx(); /* widescreen: as narrowed (PsyX_GTE.cpp) */
    camera.ofy = (int)CFC2(25) / 65536.0f;
    camera.widescreen = pc_gte_widescreen;
    double c[3][3] = {
        { r[1][1] * r[2][2] - r[1][2] * r[2][1], r[0][2] * r[2][1] - r[0][1] * r[2][2], r[0][1] * r[1][2] - r[0][2] * r[1][1] },
        { r[1][2] * r[2][0] - r[1][0] * r[2][2], r[0][0] * r[2][2] - r[0][2] * r[2][0], r[0][2] * r[1][0] - r[0][0] * r[1][2] },
        { r[1][0] * r[2][1] - r[1][1] * r[2][0], r[0][1] * r[2][0] - r[0][0] * r[2][1], r[0][0] * r[1][1] - r[0][1] * r[1][0] },
    };
    double det = r[0][0] * c[0][0] + r[0][1] * c[1][0] + r[0][2] * c[2][0];
    if (det > -1e-9 && det < 1e-9)
        return 0;
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            camera_inverse[a][b] = c[a][b] / det;
    camera_serial = serial;
    return 1;
}
/* world = camera^-1 * (matrix - camera translation) */
static void world_transform(const gte_matrix* mv, render_transform* out)
{
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            double sum = 0;
            for (int k = 0; k < 3; k++)
                sum += camera_inverse[a][k] * (mv->m[k][b] / 4096.0);
            out->rotation[a][b] = (float)sum;
        }
        double sum = 0;
        for (int k = 0; k < 3; k++)
            sum += camera_inverse[a][k] * ((double)mv->t[k] - camera_translation[k]);
        out->translation[a] = (float)sum;
    }
}
/* For the other native submitters (effect_native.c): the game's camera this
 * frame as a render_camera, and a GTE matrix (camera-composed, as the GTE
 * uses it) as a model -> world transform. */
int pc_native_camera(render_camera* out)
{
    if (!read_camera(render_frame_serial()))
        return 0;
    *out = camera;
    return 1;
}
void pc_native_world_transform(const void* matrix, render_transform* out)
{
    world_transform((const gte_matrix*)matrix, out);
}
/* A view-space point (GTE units) in world space, with the camera of the last
 * pc_native_camera. */
void pc_native_world_point(const double view[3], float out[3])
{
    for (int a = 0; a < 3; a++) {
        double sum = 0;
        for (int k = 0; k < 3; k++)
            sum += camera_inverse[a][k] * (view[k] - camera_translation[k]);
        out[a] = (float)sum;
    }
}
/* The list being filled: a model drawn again right after itself in the same
 * frame (BATTLE/44F14.c draws some actors twice through func_800B28A8: a
 * second, slightly offset copy with its own colour and an ordering-table
 * shift) joins the same list, so the PS1 order interleaves both copies as
 * the ordering table does. */
#define GROUP_INSTANCES 8
#define GROUP_POLYGONS 8192
static struct {
    const void *object, *matrices;
    uint32_t serial, list;
    const unsigned* ot; /* the ordering table pointer of the first draw */
    int instances, count;
    uint64_t keys[GROUP_POLYGONS];
} group;
static uint32_t entries[GROUP_POLYGONS];
/* This draw's semi-transparent polygons: bucket << 32 | record order. */
static uint64_t translucent_keys[GROUP_POLYGONS];
static int translucent_count;
static int draw_bucket; /* the farthest bucket any polygon of this draw sorts into */
static int submitted_index; /* this draw's instance in its list */

static int compare_keys(const void* a, const void* b)
{
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? 1 : x > y ? -1 : 0; /* larger first */
}
/* The polygon's depth as its builder sorts it (quads (SZ0..3) >> 4,
 * triangles AVSZ3). */
static int polygon_depth(const sort_info* s, const unsigned* depths, int zsf3)
{
    unsigned sum = depths[s->vertex[0]] + depths[s->vertex[1]] + depths[s->vertex[2]];
    return s->quad ? (int)((sum + depths[s->vertex[3]]) >> 4) : (int)(((long long)zsf3 * sum) >> 12);
}
/* The builders' back-face test: NCLIP of the first triangle's projected
 * positions <= 0 culls (double-sided records excepted). */
static int facing_away(const sort_info* s, const unsigned* xy)
{
    long long x0 = (short)xy[s->vertex[0]], y0 = (short)(xy[s->vertex[0]] >> 16);
    long long x1 = (short)xy[s->vertex[1]], y1 = (short)(xy[s->vertex[1]] >> 16);
    long long x2 = (short)xy[s->vertex[2]], y2 = (short)(xy[s->vertex[2]] >> 16);
    return (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) <= 0;
}
/* Returns the list the model went into (0: it stays packets). */
static uint32_t submit(int kind, const void* object, const void* model, unsigned color, unsigned texture,
    const unsigned* xy, const unsigned* depths, const unsigned* ot)
{
    uint32_t serial = render_frame_serial();
    static uint32_t evicted = ~0u;
    if (evicted != serial) {
        evict(serial);
        evicted = serial;
    }
    translucent_count = 0;
    draw_bucket = -1;
    /* The grouped builders blend the whole model when object byte 9 selects
     * a mode (1-6 B-F, 7 and up B+F). */
    int blend = 0;
    if (GROUPS(kind)) {
        unsigned mode = ((const unsigned char*)object)[9] & 0xf;
        blend = !mode ? 0 : mode < 7 ? RENDER_BLEND(2) : RENDER_BLEND(1);
    }
    if (blend && !pc_native_blend)
        return 0;
    model_mesh* c = mesh_for(object, model, kind, serial);
    if (!c || !c->mesh || !c->bones)
        return 0;
    /* The camera may change between the room and the models (the free
     * camera, cutscenes): read it for every draw. */
    if (!read_camera(serial))
        return 0;
    int painter = pc_native_actor_order && c->sort && c->polygons <= 0xffff;
    int merge = painter && group.list && group.object == object && group.matrices == last_matrices
        && group.serial == serial && group.instances < GROUP_INSTANCES && group.count + c->polygons <= GROUP_POLYGONS
        && render_list_objects(group.list) == 1;
    uint32_t list = merge ? group.list : 0;
    if (!list) {
        render_rules rules;
        memset(&rules, 0, sizeof rules);
        rules.far_bucket = 0x7c0;
        rules.lod_bucket = 0x7fffffff;
        rules.objects = 1;
        /* The live free camera: no ordering-table far limit (the renderer
         * clips at its own planes). */
        if (pc_live_camera_active && pc_live_camera_active())
            rules.far_bucket = 1 << 20;
        rules.zsf3 = (int)(short)CFC2(29);
        list = render_list_begin(&camera, &rules);
        if (!list)
            return 0;
        group.object = object, group.matrices = last_matrices;
        group.serial = serial, group.list = list, group.ot = ot;
        group.instances = group.count = 0;
    }
    render_transform palette[RENDER_MAX_BONES];
    for (int b = 0; b < c->bones; b++)
        world_transform(&last_matrices[b], &palette[b]);
    /* The previous frame's palette of the same draw (the n-th draw of this
     * model with these matrices in the frame). */
    palette_record* rec = NULL;
    int occurrence = 0;
    for (int i = 0; i < record_count; i++) {
        palette_record* q = &records[i];
        if (q->object == object && q->model == model && q->matrices == last_matrices && q->serial == serial)
            occurrence = q->occurrence + 1 > occurrence ? q->occurrence + 1 : occurrence;
    }
    for (int i = 0; i < record_count && !rec; i++) {
        palette_record* q = &records[i];
        if (q->object == object && q->model == model && q->matrices == last_matrices && q->occurrence == occurrence
            && q->serial != serial)
            rec = q;
    }
    render_transform previous[RENDER_MAX_BONES];
    render_instance instance;
    memset(&instance, 0, sizeof instance);
    int has_previous = rec && rec->serial == serial - 1 && rec->bones == c->bones;
    if (has_previous)
        hold_segment(rec, palette, c->bones, previous, &instance);
    if (!rec) {
        if (record_count < PALETTE_RECORDS)
            rec = &records[record_count++];
        else {
            rec = &records[0];
            for (int i = 1; i < record_count; i++)
                if ((uint32_t)(serial - records[i].serial) > (uint32_t)(serial - rec->serial))
                    rec = &records[i];
        }
    }
    if (!has_previous) /* a new or interrupted run: no hold history */
        rec->frames = 1, rec->k = rec->held = rec->last_hold = rec->streak = 0;
    rec->object = object, rec->model = model, rec->matrices = last_matrices;
    rec->occurrence = occurrence;
    rec->serial = serial;
    rec->bones = c->bones;
    memcpy(rec->palette, palette, (size_t)c->bones * sizeof *palette);

    instance.mesh = c->mesh;
    instance.palette = palette;
    instance.previous = has_previous ? previous : NULL;
    instance.bones = c->bones;
    /* Flat models take the model's colour; a blended coloured model takes
     * the tint on every vertex. */
    int flat = !COLORED(kind) || blend;
    instance.override = RENDER_OVERRIDE_TEXTURE | (flat ? RENDER_OVERRIDE_COLOR : 0);
    instance.color[0] = color & 0xff;
    instance.color[1] = (color >> 8) & 0xff;
    instance.color[2] = (color >> 16) & 0xff;
    instance.tpage = (uint16_t)(texture & 0xffff);
    instance.clut = (uint16_t)(texture >> 16);
    instance.blend = blend;
    instance.owner = (uint32_t)(uintptr_t)object;
    render_list_add(list, &instance);
    int index = group.instances++;

    /* Every polygon's ordering-table index from the depths func_800B3A68
     * just computed: the PS1's paint order (opaque polygons, PS1 order
     * within the list: plus the table pointer's shift since the group's
     * first draw) and the buckets of the translucent ones. */
    int shift = (int)(ot - group.ot);
    int zsf3 = (int)(short)CFC2(29);
    int translucent = pc_native_blend && (blend || c->semi);
    for (int p = 0; c->sort && p < c->polygons; p++) {
        const sort_info* s = &c->sort[p];
        int depth = polygon_depth(s, depths, zsf3);
        int semi = blend || s->semi;
        if (semi && translucent) {
            /* Culled or beyond the far bucket: no packet, no entry. */
            if ((!s->twosided && facing_away(s, xy)) || depth + s->bias >= 0x7c0)
                continue;
            int bucket = depth + s->offset;
            bucket = bucket < 0 ? 0 : bucket > 0x7bf ? 0x7bf : bucket;
            draw_bucket = bucket > draw_bucket ? bucket : draw_bucket;
            if (translucent_count < GROUP_POLYGONS)
                translucent_keys[translucent_count++] = ((uint64_t)(uint32_t)bucket << 32) | (uint32_t)p;
            continue;
        }
        if (semi)
            continue;
        if (depth + s->bias < 0x7c0) {
            int own = depth + s->offset;
            draw_bucket = own > draw_bucket ? own : draw_bucket;
        }
        if (painter) {
            int bucket = depth + s->offset + shift + 0x10000;
            bucket = bucket < 0 ? 0 : bucket;
            group.keys[group.count++] = ((uint64_t)(uint32_t)bucket << 32) | ((uint64_t)(uint32_t)index << 16)
                                      | (uint32_t)p;
        }
    }
    if (painter) {
        /* The PS1's draw order this frame, far first; within a bucket the
         * later packet first (each is linked at the bucket's head). */
        static uint64_t sorted[GROUP_POLYGONS];
        memcpy(sorted, group.keys, (size_t)group.count * sizeof *sorted);
        qsort(sorted, (size_t)group.count, sizeof *sorted, compare_keys);
        for (int k = 0; k < group.count; k++)
            entries[k] = (uint32_t)(sorted[k] & 0xffffffffu);
        render_list_order(list, entries, group.count);
    }
    submitted_index = index;
    return list;
}
/* A packet builder finished one model: its packets are [start, the packet
 * pointer). `projected` is func_800B3A68's output (screen positions, then
 * the depths at + 0xc00). `kind`: the builder (KIND_*). */
void pc_object_native_draw(int kind, const void* projected, const void* object, const void* model,
    const void* color, unsigned* start)
{
    unsigned* end = psx_load(unsigned*, (uintptr_t)0x1f800000);
    uint32_t list = 0;
    unsigned texture = *(const unsigned*)(uintptr_t)0x1f800390;
    if (kind < KIND_FLAT || kind > KIND_COLORED_GROUPS)
        kind = KIND_FLAT;
    if (pc_native_actors && pc_hw_active && object == last_object && model == last_model && last_matrices) {
        const unsigned* xy = projected;
        const unsigned* depths = (const unsigned*)((const char*)projected + 0xc00);
        unsigned* ot = psx_load(unsigned*, (uintptr_t)0x1f800004);
        /* func_800B4594 takes no colour (per-vertex records). */
        unsigned rgb = color && kind != KIND_COLORED ? *(const unsigned*)color & 0xffffff : 0x808080;
        list = submit(kind, object, model, rgb, texture, xy, depths, ot);
        if (list) {
            /* The translucent polygons: one marker per bucket, in front of
             * the model's packets there; within a bucket the later record
             * first, as the builder's links at the bucket's head leave them. */
            if (translucent_count) {
                qsort(translucent_keys, (size_t)translucent_count, sizeof *translucent_keys, compare_keys);
                for (int k = 0; k < translucent_count;) {
                    int bucket = (int)(translucent_keys[k] >> 32), run = k;
                    while (run < translucent_count && (int)(translucent_keys[run] >> 32) == bucket) {
                        entries[run - k] = ((uint32_t)submitted_index << 16) | (uint32_t)(translucent_keys[run] & 0xffff);
                        ++run;
                    }
                    int first = render_list_translucent(list, entries, NULL, run - k);
                    if (first >= 0)
                        end = render_list_link_translucent(list, first, run - k, ot + bucket, end);
                    k = run;
                }
            }
            /* The list's marker at the model's farthest bucket (the largest
             * vertex depth / 4, or a biased polygon's bucket beyond it),
             * before every packet and translucent marker of the model. A
             * list that several draws joined gets a marker per draw; the
             * first one the GPU reaches draws it. */
            unsigned count = ((const unsigned char*)object)[1];
            const unsigned* groups = psx_load(const unsigned*, (const char*)model + 0x34);
            unsigned vertices = count ? groups[count - 1] >> 16 : 0, most = 0;
            for (unsigned v = 0; v < vertices && v < 768; v++)
                most = depths[v] > most ? depths[v] : most;
            int bucket = (int)(most >> 2);
            if (draw_bucket > bucket)
                bucket = draw_bucket;
            if (bucket > 0x7bf)
                bucket = 0x7bf;
            end = render_list_link(list, ot + bucket, end);
            psx_store((uintptr_t)0x1f800000, end);
        }
    }
    if (pc_hw_object_packets)
        pc_hw_object_packets(start, end, list);
}
