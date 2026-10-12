/* Native room geometry (phase 2): the room's groups as render.h meshes,
 * built once per room from the same packed data func_8009723C walks
 * (room_geometry.c; decoded as scene_capture.c does), and submitted each
 * frame as instances in one draw list linked into the ordering table where
 * the room's polygons would be. room_geometry.c keeps deciding which groups
 * are visible (hide flags, the alternate set, the cutaway facing masks) and
 * calls in here for each one. Opaque polygons draw with the list; each
 * semi-transparent polygon the game keeps (its back-face and ordering-table
 * tests) draws at a translucent marker linked right where its packet is
 * (pc_native_blend), so it keeps its place among everything else in the
 * ordering table. The room packets are all still built for the CPU's
 * reference VRAM (pc_native_room 1), or only the semi-transparent ones (2);
 * the hardware path skips those the native draws cover.
 *
 * Other blocks drawn through func_8009723C (func_8008E19C's animated floor
 * quads) become layered lists (pc_native_objects): room rules, drawn over
 * the native surfaces so far instead of resetting depth. Their geometry is
 * rewritten between calls (position, brightness), so their meshes are keyed
 * by a hash of the whole block and kept for a few frames. */
#define _POSIX_C_SOURCE 200809L
#include "psx_types.h"
#include "render.h"
#include "room_editor.h"
#include "room_effects.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned int CFC2(int reg);
extern int pc_gte_widescreen;
extern int pc_hw_active;
extern const void* pc_scene_room_data(void) __attribute__((weak));
extern void pc_room_render_rules(int* near_bucket, int* far_bucket, int* lod_bucket) __attribute__((weak));
extern void pc_hw_native_room_packets(const void* start, const void* end, uint32_t list) __attribute__((weak));

typedef struct {
    short m[3][3];
    short pad;
    int t[3];
} gte_matrix; /* libgte MATRIX */

/* ---- meshes, per room data block ---- */

typedef struct {
    unsigned word, keep; /* a geometry word and the bits of it compared */
} word_mask;
typedef struct {
    unsigned offset, shift;
    render_mesh mesh;
    int16_t* polygon; /* geometry polygon index -> mesh polygon (-1: none) */
    unsigned polygons;
    unsigned words; /* the geometry block's (counts and polygons) */
    unsigned* snapshot; /* its words when the mesh was written */
    uint32_t checked; /* frame serial of the last comparison (mesh_follow) */
    /* Animated materials (room_effects.c): the effects' generation the mesh
     * was tagged with, and the UV bytes they rewrite (not compared; sorted
     * by word). */
    unsigned generation;
    word_mask* mask;
    int mask_count;
} group_mesh;
typedef struct {
    const void* data;
    unsigned signature;
    int room; /* the current room (else a layered block) */
    uint32_t used; /* frame serial */
    group_mesh* meshes;
    int count, capacity;
    int failed;
} room_block;
#define MAX_BLOCKS 32
static room_block blocks[MAX_BLOCKS];
static int block_count;

static unsigned hash_words(unsigned hash, const unsigned* words, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        hash = (hash ^ words[i]) * 16777619u;
    return hash;
}
/* The room's signature: group geometry offsets and the first words of each
 * geometry block (rooms are not rewritten while shown). A layered block's
 * covers all of its geometry (func_8008E19C rewrites its quad each call). */
static unsigned signature(const unsigned char* data, int whole)
{
    unsigned count = *(const unsigned*)data, hash = count * 2654435761u;
    for (unsigned i = 0; i < count && i < 4096; ++i) {
        const unsigned char* group = data + 4 + i * 0x40;
        unsigned offset = *(const unsigned*)(group + 0x1c);
        const unsigned* geometry = (const unsigned*)(data + offset);
        hash = (hash ^ offset ^ (*(const unsigned*)group & 0x800)) * 16777619u;
        unsigned words = 4;
        if (whole && geometry[0] <= 256 && geometry[1] <= 256)
            words = 2 + geometry[0] * 8 + geometry[1] * 10;
        hash = hash_words(hash, geometry, words);
    }
    return hash;
}
static void forget_block(room_block* b)
{
    for (int i = 0; i < b->count; i++) {
        render_mesh_destroy(b->meshes[i].mesh);
        free(b->meshes[i].polygon);
        free(b->meshes[i].mask);
        free(b->meshes[i].snapshot);
    }
    free(b->meshes);
    memset(b, 0, sizeof *b);
}
/* A geometry block's words: the counts and every polygon (8 words per
 * triangle, 10 per quad); 0 when the counts are not a room block's. */
static unsigned geometry_words(const unsigned* geometry)
{
    unsigned tris = geometry[0], quads = geometry[1];
    if (tris > 65535 || quads > 65535 || !(tris + quads) || tris + quads > 32767)
        return 0;
    return 2 + tris * 8 + quads * 10;
}
/* The animated materials of the geometry block at byte `offset` of `data`
 * (room_effects.c): tags[i] the effect (1-based) rewriting polygon i, and
 * the words of their UV bytes for the hash mask (`mask`, sorted). Returns
 * the masked words. */
static int effect_tags(const void* data, unsigned offset, const unsigned* geometry, uint8_t* tags, word_mask* mask,
    int max_mask)
{
    unsigned tris = geometry[0], quads = geometry[1];
    memset(tags, 0, tris + quads);
    int count = 0, masks = 0;
    const pc_uv_effect* effects = pc_uv_effects(&count);
    for (int i = 0; i < count; i++) {
        int effect = pc_uv_effect_at(data, effects[i].polygon);
        if (!effect || effects[i].polygon < offset + 8)
            continue;
        unsigned at = effects[i].polygon - (offset + 8), index;
        if (at < tris * 32 && !(at % 32))
            index = at / 32;
        else if (at >= tris * 32 && at - tris * 32 < quads * 40 && !((at - tris * 32) % 40))
            index = tris + (at - tris * 32) / 40;
        else
            continue;
        tags[index] = (uint8_t)effect;
        /* u0 v0, u1 v1 (low halves of words 6, 7), u2 / v2 (top bytes of 4,
         * 5), u3 / v3 (top bytes of 8, 9). */
        unsigned w = 2 + at / 4;
        int quad = index >= tris;
        static const word_mask bytes[6] = { { 4, 0x00ffffffu }, { 5, 0x00ffffffu }, { 6, 0xffff0000u },
            { 7, 0xffff0000u }, { 8, 0x00ffffffu }, { 9, 0x00ffffffu } };
        for (int k = 0; k < (quad ? 6 : 4) && masks < max_mask; k++)
            mask[masks++] = (word_mask) { w + bytes[k].word, bytes[k].keep };
    }
    /* sorted by word (insertion: a few dozen entries) */
    for (int i = 1; i < masks; i++)
        for (int j = i; j > 0 && mask[j - 1].word > mask[j].word; j--) {
            word_mask t = mask[j];
            mask[j] = mask[j - 1];
            mask[j - 1] = t;
        }
    return masks;
}
/* The tags and mask of a geometry block into `g`; returns the tags (NULL:
 * none or out of memory; the caller frees them). */
static uint8_t* tag_mesh(const void* data, unsigned offset, const unsigned* geometry, word_mask** mask, int* masks)
{
    word_mask scratch[PC_UV_EFFECTS_MAX * 6];
    *mask = NULL;
    *masks = 0;
    uint8_t* tags = malloc(geometry[0] + geometry[1]);
    if (!tags)
        return NULL;
    int n = effect_tags(data, offset, geometry, tags, scratch, PC_UV_EFFECTS_MAX * 6);
    if (n && (*mask = malloc((size_t)n * sizeof **mask))) {
        memcpy(*mask, scratch, (size_t)n * sizeof **mask);
        *masks = n;
    } else if (n) {
        free(tags); /* untagged: the hash then follows every byte */
        return NULL;
    }
    return tags;
}
/* All of a geometry block's polygons in group-local coordinates, decoded as
 * room_geometry.c does; `map` (geometry polygon index -> mesh polygon) may be
 * NULL; `tags` (may be NULL): each polygon's animated material, whose
 * polygons keep the effect's base UVs. Returns the polygon count (0: none or
 * out of memory). */
static int decode_polygons(const unsigned* geometry, unsigned shift, render_polygon** out_polygons, int16_t* map,
    const uint8_t* tags)
{
    unsigned tris = geometry[0], quads = geometry[1], total = tris + quads;
    render_polygon* polygons = calloc(total, sizeof *polygons);
    *out_polygons = polygons;
    if (!polygons)
        return 0;
    int count = 0;
    const unsigned* poly = geometry + 2;
    for (unsigned i = 0; i < total; ++i) {
        int quad = i >= tris;
        unsigned code = poly[3] >> 24;
        if (map)
            map[i] = (int16_t)count;
        render_polygon* out = &polygons[count++];
        int x = (short)poly[0], y = (short)(poly[0] >> 16), z = (short)poly[1];
        const signed char* d = (const signed char*)poly + 6;
        const signed char* deltas[4] = { NULL, d, d + 3, (const signed char*)(poly + 8) };
        unsigned colors[4] = { poly[3], poly[4], poly[5], quad ? poly[9] : 0 };
        unsigned uv[4] = { poly[6], poly[7], (poly[4] >> 24) | ((poly[5] >> 24) << 8),
            quad ? (poly[8] >> 24) | ((poly[9] >> 24) << 8) : 0 };
        for (int k = 0; k < (quad ? 4 : 3); k++) {
            int vx = x, vy = y, vz = z;
            if (k) {
                vx += (int)((unsigned)(int)deltas[k][0] << shift);
                vy += (int)((unsigned)(int)deltas[k][1] << shift);
                vz += (int)((unsigned)(int)deltas[k][2] << shift);
            }
            /* the GTE takes 16-bit vertex components */
            out->position[k][0] = (float)(short)vx;
            out->position[k][1] = (float)(short)vy;
            out->position[k][2] = (float)(short)vz;
            out->rgb[k][0] = colors[k] & 0xff;
            out->rgb[k][1] = (colors[k] >> 8) & 0xff;
            out->rgb[k][2] = (colors[k] >> 16) & 0xff;
            out->uv[k][0] = uv[k] & 0xff;
            out->uv[k][1] = (uv[k] >> 8) & 0xff;
        }
        if (tags && tags[i]) {
            int effects = 0;
            const pc_uv_effect* e = &pc_uv_effects(&effects)[tags[i] - 1];
            for (int k = 0; k < (quad ? 4 : 3); k++)
                out->uv[k][0] = e->u[k], out->uv[k][1] = e->v[k];
            out->uv_effect = tags[i];
        }
        out->tpage = (uint16_t)(poly[7] >> 16);
        out->clut = (uint16_t)(poly[6] >> 16);
        /* Bit 0 of the data's code byte is the double-sided flag; the
         * packet clears it (textures are always modulated). Bit 1:
         * semi-transparent (drawn at translucent markers). */
        out->code = (uint8_t)(code & 0xfe);
        out->flags = (uint8_t)((quad ? RENDER_POLY_QUAD : 0) | (code & 1 ? RENDER_POLY_DOUBLE_SIDED : 0));
        poly += quad ? 10 : 8;
    }
    return count;
}
/* One mesh per distinct geometry block (groups may share one): all of its
 * polygons in group-local coordinates, decoded as room_geometry.c does. */
static group_mesh* mesh_for(room_block* b, unsigned offset, unsigned shift)
{
    for (int i = 0; i < b->count; i++)
        if (b->meshes[i].offset == offset && b->meshes[i].shift == shift)
            return &b->meshes[i];
    const unsigned* geometry = (const unsigned*)((const unsigned char*)b->data + offset);
    unsigned words = geometry_words(geometry);
    render_mesh mesh = 0;
    int16_t* map = NULL;
    word_mask* mask = NULL;
    int masks = 0;
    unsigned total = 0;
    if (words) {
        total = geometry[0] + geometry[1];
        render_polygon* polygons = NULL;
        map = malloc(total * sizeof *map);
        uint8_t* tags = b->room ? tag_mesh(b->data, offset, geometry, &mask, &masks) : NULL;
        int count = map ? decode_polygons(geometry, shift, &polygons, map, tags) : 0;
        if (count)
            mesh = render_mesh_create(polygons, count);
        free(polygons);
        free(tags);
    }
    if (!mesh) {
        free(map);
        free(mask);
        map = NULL;
        mask = NULL;
        masks = 0;
        total = 0;
    }
    if (b->count == b->capacity) {
        int capacity = b->capacity ? b->capacity * 2 : 64;
        group_mesh* grown = realloc(b->meshes, (size_t)capacity * sizeof *grown);
        if (!grown) {
            render_mesh_destroy(mesh);
            free(map);
            free(mask);
            return NULL;
        }
        b->meshes = grown;
        b->capacity = capacity;
    }
    group_mesh* g = &b->meshes[b->count++];
    g->offset = offset;
    g->shift = shift;
    g->mesh = mesh;
    g->polygon = map;
    g->polygons = total;
    g->snapshot = mesh ? malloc((size_t)words * sizeof *g->snapshot) : NULL;
    if (g->snapshot)
        memcpy(g->snapshot, geometry, (size_t)words * sizeof *g->snapshot);
    g->words = g->snapshot ? words : 0; /* (without a snapshot: not followed) */
    g->mask = mask;
    g->mask_count = masks;
    g->generation = pc_uv_effects_generation();
    g->checked = render_frame_serial();
    return g;
}
/* Room texture animations rewrite polygons in the room data while it is
 * shown (BATTLE 2842C.c: func_80091C6C steps a UV atlas frame, func_80091E10
 * scrolls UVs, both writing the polygon's UV bytes; the save point's sigil
 * is one). Those effects are animated materials (room_effects.c: the mesh
 * keeps base UVs, the draw list carries the offsets), so their bytes are
 * left out of the comparison here. Anything else rewriting the block (an
 * effect found not modelled, other game code) is followed as a fallback: a
 * mesh compares its geometry block with a copy once per frame and
 * re-decodes it when the words changed (render_mesh_update; render_stats
 * mesh_updates). */
static void mesh_follow(room_block* b, group_mesh* g)
{
    uint32_t serial = render_frame_serial();
    if (!g->mesh || !g->words || g->checked == serial)
        return;
    g->checked = serial;
    const unsigned* geometry = (const unsigned*)((const unsigned char*)b->data + g->offset);
    if (geometry_words(geometry) != g->words)
        return; /* counts rewritten: not a room block any more */
    unsigned generation = b->room ? pc_uv_effects_generation() : g->generation;
    uint8_t* tags = NULL;
    if (generation != g->generation) {
        /* The modelled effects changed: new tags and mask. */
        free(g->mask);
        tags = tag_mesh(b->data, g->offset, geometry, &g->mask, &g->mask_count);
    }
    /* The animated materials' UV bytes follow the data (not compared). */
    for (int m = 0; m < g->mask_count; m++) {
        unsigned w = g->mask[m].word, keep = g->mask[m].keep;
        if (w < g->words)
            g->snapshot[w] = (g->snapshot[w] & keep) | (geometry[w] & ~keep);
    }
    if (!memcmp(g->snapshot, geometry, (size_t)g->words * sizeof *geometry) && generation == g->generation) {
        free(tags);
        return;
    }
    memcpy(g->snapshot, geometry, (size_t)g->words * sizeof *geometry); /* (a failed update is not retried) */
    if (!tags && b->room) {
        word_mask* unused;
        int unused_count;
        tags = tag_mesh(b->data, g->offset, geometry, &unused, &unused_count);
        free(unused);
    }
    g->generation = generation;
    render_polygon* polygons = NULL;
    int count = decode_polygons(geometry, g->shift, &polygons, NULL, tags);
    if (count)
        render_mesh_update(g->mesh, polygons, count);
    free(polygons);
    free(tags);
}
/* The block's meshes (built on first use); NULL when it cannot be native. */
static room_block* block_for(const void* data, int room, uint32_t serial)
{
    unsigned sig = signature(data, !room);
    room_block* oldest = NULL;
    for (int i = 0; i < block_count; i++) {
        room_block* b = &blocks[i];
        if (b->data == data && b->signature == sig && b->room == room) {
            b->used = serial;
            return b->failed ? NULL : b;
        }
        if (!oldest || (uint32_t)(serial - b->used) > (uint32_t)(serial - oldest->used))
            oldest = b;
    }
    /* A new room replaces the old one at once; layered blocks stay a few
     * frames (replays of the frames before may still draw them). */
    for (int i = 0; i < block_count;) {
        room_block* b = &blocks[i];
        if ((room && b->room) || (!b->room && (uint32_t)(serial - b->used) > 8)) {
            forget_block(b);
            blocks[i] = blocks[--block_count];
            memset(&blocks[block_count], 0, sizeof blocks[block_count]);
        } else
            ++i;
    }
    room_block* b;
    if (block_count < MAX_BLOCKS)
        b = &blocks[block_count++];
    else {
        b = oldest;
        for (int i = 0; i < block_count; i++)
            if (!blocks[i].room && (uint32_t)(serial - blocks[i].used) >= (uint32_t)(serial - b->used))
                b = &blocks[i];
        forget_block(b);
    }
    b->data = data;
    b->signature = sig;
    b->room = room;
    b->used = serial;
    unsigned count = *(const unsigned*)data;
    int polygons = 0, meshes = 0, translucent = 0;
    for (unsigned g = 0; g < count; g++) {
        const unsigned char* group = (const unsigned char*)data + 4 + g * 0x40;
        unsigned flags = *(const unsigned*)group;
        group_mesh* m = mesh_for(b, *(const unsigned*)(group + 0x1c), flags & 0x800 ? 0 : 3);
        if (!m)
            b->failed = 1;
    }
    for (int i = 0; i < b->count; i++) {
        int n = render_mesh_polygons(b->meshes[i].mesh);
        if (n > 0) {
            polygons += n, ++meshes;
            const unsigned* geometry = (const unsigned*)((const unsigned char*)data + b->meshes[i].offset);
            const unsigned* poly = geometry + 2;
            for (unsigned k = 0; k < geometry[0] + geometry[1]; k++) {
                translucent += (poly[3] >> 25) & 1;
                poly += k >= geometry[0] ? 10 : 8;
            }
        }
    }
    static int logged_blocks;
    if (room || logged_blocks++ < 4 || !(serial & 255))
        fprintf(stderr, "[render] native %s: %u groups, %d meshes, %d polygons (%d semi-transparent)\n",
            room ? "room" : "room block", count, meshes, polygons, translucent);
    return b->failed ? NULL : b;
}

/* ---- per frame ---- */

static uint32_t list; /* this call's list (0: legacy) */
static int list_mode, list_blend;
static room_block* list_block;
static const unsigned* list_first; /* the first packet of the call (tint range) */
static double camera_inverse[3][3], camera_translation[3];
static int near_bucket, far_bucket;
static uint32_t last_list;
static int instance_index; /* of the group being walked (-1: none) */
static const group_mesh* instance_mesh;
static int translucent_failed;
static float next_cut, next_cut_previous; /* pc_room_native_group_cut */
static int next_cut_ghost;

extern int pc_live_camera_active(void) __attribute__((weak));
/* func_8009723C begins: returns pc_native_room's mode when the native list
 * takes this block's opaque polygons, 0 to draw them all as packets. */
int pc_room_native_begin(const void* data, unsigned mode, const unsigned* first)
{
    list = 0;
    instance_index = -1;
    instance_mesh = NULL;
    next_cut = next_cut_previous = 0, next_cut_ghost = 0;
    /* The room's texture effects this frame (animated materials, and the
     * ray-traced scene's UVs). */
    pc_uv_effects_frame(data);
    if (!pc_native_room || !pc_hw_active)
        return 0;
    /* The current room (func_8008B1FC); other blocks drawn with this
     * function (func_8008E19C) are layered over it. */
    int room = pc_scene_room_data && data == pc_scene_room_data();
    if (!room && !pc_native_objects)
        return 0;
    unsigned count = *(const unsigned*)data;
    if (!count || count > 4096)
        return 0;
    room_block* block = block_for(data, room, render_frame_serial());
    if (!block)
        return 0;
    /* The camera: the view matrix func_8008B1FC set (scratch 0x1F800014)
     * and the GTE projection. */
    const gte_matrix* view = (const gte_matrix*)(uintptr_t)0x1f800014;
    render_camera camera;
    double r[3][3];
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++)
            camera.rotation[a][b] = (float)(r[a][b] = view->m[a][b] / 4096.0);
        camera.translation[a] = (float)(camera_translation[a] = view->t[a]);
    }
    camera.h = (float)(CFC2(26) & 0xffff);
    camera.ofx = (int)CFC2(24) / 65536.0f;
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
    render_rules rules = { 8, 0x7ff, 0x200, (int)(mode & 1) };
    if (pc_room_render_rules)
        pc_room_render_rules(&rules.near_bucket, &rules.far_bucket, &rules.lod_bucket);
    rules.layered = !room;
    near_bucket = rules.near_bucket, far_bucket = rules.far_bucket;
    if (room) {
        static render_rules logged;
        if (logged.far_bucket != rules.far_bucket || logged.lod_bucket != rules.lod_bucket
            || logged.subdivision != rules.subdivision) { /* the near bucket animates with the camera */
            logged = rules;
            fprintf(stderr, "[render] room rules: buckets %d..%d, UV halving from %d, subdivision %d\n",
                rules.near_bucket, rules.far_bucket, rules.lod_bucket, rules.subdivision);
        }
    }
    /* (The markers keep the table's range: near_bucket/far_bucket above.)
     * The editor's live free camera: the room's ordering-table range is the
     * game camera's (its near bucket animates with the camera, the far end
     * is the table's); the renderer clips at its own near plane instead, so
     * flying close to or far from the room keeps it. The game's own tests
     * for the semi-transparent polygons it keeps still apply. */
    if (pc_live_camera_active && pc_live_camera_active()) {
        rules.near_bucket = 0;
        rules.far_bucket = 1 << 20;
    }
    list = render_list_begin(&camera, &rules);
    if (!list)
        return 0;
    if (room) {
        int effects = 0;
        const render_uv_effect* table = pc_uv_effects_table(&effects);
        if (effects)
            render_list_uv_effects(list, table, effects);
    }
    list_mode = pc_native_room;
    list_blend = pc_native_blend;
    list_block = block;
    list_first = first;
    instance_index = -1;
    translucent_failed = 0;
    return list_mode;
}
/* The next group's dithered share left out (gameplay_walls.c's fades and
 * ghosts; room_geometry.c sets it before each pc_room_native_group). */
void pc_room_native_group_cut(float cut, float cut_previous, int ghost)
{
    next_cut = cut, next_cut_previous = cut_previous, next_cut_ghost = ghost;
}
/* A visible group (room_geometry.c decided): its camera-composed matrix
 * (group + 0x20) and the packet flags `gp` (bias in the top half,
 * subdivision level in bits 13-14). */
void pc_room_native_group(const void* group_record, unsigned gp)
{
    instance_mesh = NULL;
    if (!list)
        return;
    const unsigned char* group = group_record;
    unsigned flags = *(const unsigned*)group;
    group_mesh* m = mesh_for(list_block, *(const unsigned*)(group + 0x1c), flags & 0x800 ? 0 : 3);
    if (!m || !m->mesh)
        return;
    mesh_follow(list_block, m);
    const gte_matrix* mv = (const gte_matrix*)(group + 0x20);
    render_instance instance;
    memset(&instance, 0, sizeof instance);
    instance.mesh = m->mesh;
    /* world = camera^-1 * model-view */
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            double sum = 0;
            for (int k = 0; k < 3; k++)
                sum += camera_inverse[a][k] * (mv->m[k][b] / 4096.0);
            instance.rotation[a][b] = (float)sum;
        }
        double sum = 0;
        for (int k = 0; k < 3; k++)
            sum += camera_inverse[a][k] * ((double)mv->t[k] - camera_translation[k]);
        instance.translation[a] = (float)sum;
    }
    instance.bias = (int)gp >> 16;
    instance.subdivision_level = (int)((gp >> 13) & 3);
    instance.cut = next_cut, instance.cut_previous = next_cut_previous, instance.cut_ghost = next_cut_ghost;
    next_cut = next_cut_previous = 0, next_cut_ghost = 0;
    render_list_add(list, &instance);
    ++instance_index;
    instance_mesh = m;
}
/* room_geometry.c kept semi-transparent polygon `index` of the current
 * group: its packet was just linked at `ot_entry` (the bucket `bucket`, or
 * for a subdivided polygon the top level's, which may be out of range).
 * Links a translucent marker in front of it; returns the next free packet
 * word. `halve`: the game halved its UVs. */
unsigned* pc_room_native_translucent(unsigned index, unsigned* packet, unsigned* ot, int bucket, int halve,
    const unsigned* limit)
{
    if (!list || !list_blend || !instance_mesh || translucent_failed || index >= instance_mesh->polygons
        || instance_mesh->polygon[index] < 0)
        return packet;
    if (bucket < near_bucket)
        bucket = near_bucket;
    int layered = list_block && !list_block->room; /* its list's marker is a bucket nearer */
    if (bucket > far_bucket - 1 - layered)
        bucket = far_bucket - 1 - layered;
    if (bucket < 0 || packet + 5 > limit) {
        translucent_failed = 1; /* all of them stay packets */
        return packet;
    }
    uint32_t entry = ((uint32_t)instance_index << 16) | (uint16_t)instance_mesh->polygon[index];
    uint8_t flags = RENDER_TRANSLUCENT_DECIDED | (halve ? RENDER_TRANSLUCENT_HALVE : 0);
    int first = render_list_translucent(list, &entry, &flags, 1);
    if (first < 0) {
        translucent_failed = 1;
        return packet;
    }
    return render_list_link_translucent(list, first, 1, ot + bucket, packet);
}
/* func_8009723C ends with its packets written up to `packet`: link the
 * list's marker into the room's ordering table at the farthest room bucket
 * (drawn before any room polygon), mark the range for the hardware path,
 * and return the next free packet word. */
unsigned* pc_room_native_end(unsigned* packet, unsigned* ot)
{
    if (!list)
        return packet;
    /* A layered block one bucket nearer: after the room's marker (it is
     * drawn over the room). */
    int layered = list_block && !list_block->room;
    int bucket = far_bucket - 1 - layered;
    if (bucket > 0x7ff - layered)
        bucket = 0x7ff - layered;
    if (bucket < near_bucket)
        bucket = near_bucket;
    if (translucent_failed) {
        /* Markers already linked draw nothing; the packets draw. */
        static int logged;
        if (!logged++)
            fprintf(stderr, "[render] room translucent polygons stay packets (packet space)\n");
        render_list_translucent_clear(list);
    }
    if (pc_hw_native_room_packets && list_first && packet > list_first)
        pc_hw_native_room_packets(list_first, packet, list);
    packet = render_list_link(list, ot + bucket, packet);
    last_list = list_block && list_block->room ? list : 0;
    list = 0;
    instance_mesh = NULL;
    return packet;
}
/* func_80098D6C tints the room packets of [start, ...): the same tint for
 * the native list built with them. */
void pc_room_native_tint(const unsigned* start, int dr, int dg, int db, int fade)
{
    if (last_list && start == list_first) {
        render_list_tint(last_list, dr, dg, db, fade);
        static int logged[4];
        if (memcmp(logged, (int[4]) { dr, dg, db, fade }, sizeof logged)) {
            memcpy(logged, (int[4]) { dr, dg, db, fade }, sizeof logged);
            fprintf(stderr, "[render] room tint %d %d %d fade %d\n", dr, dg, db, fade);
        }
    }
}

/* ---- the editor's view into rooms (live free camera only) ----
 *
 * Vagrant Story builds its rooms for a camera that stays inside them,
 * looking at Ashley from above:
 *
 * - Every wall group carries a facing mask (flags low byte, one bit per 45
 *   degrees of camera yaw); scene_submit.inc sets the camera's three bits
 *   (left, front, right) each frame and func_8009723C draws only the groups
 *   whose mask has all three: the walls between the camera and the room are
 *   left out ("cutaway walls").
 * - Each wall group has a black shell group (every vertex colour 0, the same
 *   facing mask, flag 0x8000, ordering-table bias -12): the wall's thickness,
 *   its top and its outside, drawn black over whatever lies beyond the room.
 *   The game camera only ever sees them as the black void around the room.
 *
 * The free camera used to keep every group, so from outside or above the
 * near walls and their black shells covered the room. While the free camera
 * flies, func_8009723C asks here instead (room_geometry.c): the facing mask
 * comes from the free camera's own yaw (pc_editor_game_cutaway), black shell
 * groups are skipped (pc_editor_hide_masks), and groups hidden in the
 * editor's Outliner stay hidden. Gameplay never calls these. The ray-traced
 * scene (scene_capture.c) keeps the whole room, so shadows are unchanged. */
int pc_editor_hide_masks = 1;
int pc_editor_game_cutaway = 1;

#define EDITOR_MAX_GROUPS 4096
static const void* editor_data; /* the room block the per-group state belongs to */
static unsigned editor_count, editor_signature;
static unsigned char editor_hidden[EDITOR_MAX_GROUPS]; /* hidden in the Outliner */
static signed char editor_black[EDITOR_MAX_GROUPS]; /* -1 unknown, else 0/1 */

static unsigned editor_block_signature(const void* data, unsigned count)
{
    const unsigned* w = (const unsigned*)data;
    unsigned h = 2166136261u;
    /* The groups' geometry offsets (word 7 of each 16-word record). */
    for (unsigned i = 0; i < count; ++i)
        h = (h ^ w[1 + i * 16 + 7]) * 16777619u;
    return h ^ count;
}
/* The per-group state follows the room block: a new room clears it. */
static int editor_block(const void* data)
{
    if (!data)
        return 0;
    unsigned count = *(const unsigned*)data;
    if (!count || count > EDITOR_MAX_GROUPS)
        return 0;
    unsigned sig = editor_block_signature(data, count);
    if (data != editor_data || count != editor_count || sig != editor_signature) {
        editor_data = data;
        editor_count = count;
        editor_signature = sig;
        memset(editor_hidden, 0, sizeof editor_hidden);
        memset(editor_black, -1, sizeof editor_black);
    }
    return 1;
}
/* A black shell: every polygon of the group has all vertex colours 0 (the
 * texel is modulated to black) and is not a raw texture. */
static int group_is_black(const void* data, unsigned index)
{
    if (editor_black[index] >= 0)
        return editor_black[index];
    const unsigned char* group = (const unsigned char*)data + 4 + index * 0x40;
    const unsigned* geometry = (const unsigned*)((const unsigned char*)data + *(const unsigned*)(group + 0x1c));
    unsigned tris = geometry[0], quads = geometry[1];
    int black = tris + quads > 0 && tris + quads < 65536;
    const unsigned* poly = geometry + 2;
    for (unsigned i = 0; black && i < tris + quads; ++i) {
        int quad = i >= tris;
        if ((poly[3] & 0xffffff) || (poly[4] & 0xffffff) || (poly[5] & 0xffffff)
            || (quad && (poly[9] & 0xffffff)) || ((poly[3] >> 24) & 1))
            black = 0;
        poly += quad ? 10 : 8;
    }
    editor_black[index] = (signed char)black;
    return black;
}
/* A black shell group of the current room block (gameplay_walls.c keeps
 * them hidden). */
int pc_room_group_black(const void* data, unsigned index)
{
    if (!editor_block(data) || index >= editor_count)
        return 0;
    return group_is_black(data, index);
}
/* The facing mask func_8009723C tests while the free camera flies: the bits
 * scene_submit.inc sets for a game camera with the free camera's yaw (from
 * the view matrix func_8007ACB0 built from it), or 0x10000 (as first-person
 * view: every group) with pc_editor_game_cutaway off. */
unsigned pc_room_editor_facing(void)
{
    if (!pc_editor_game_cutaway)
        return 0x10000;
    const gte_matrix* view = (const gte_matrix*)(uintptr_t)0x1f800014;
    /* Row 2 is the view direction in world space; the game's yaw is that of
     * the direction from the target to the camera, ratan2(x, z). */
    double x = -view->m[2][0], z = -view->m[2][2];
    if (x == 0 && z == 0)
        return 0x10000; /* straight down or up: no yaw */
    double turns = atan2(x, z) / (2 * 3.14159265358979323846);
    int yaw = ((int)floor(turns * 4096 + 0.5) + 4096) & 0xfff;
    unsigned left = 1u << (((0x9c0 - yaw) & 0xfff) >> 9);
    unsigned right = 1u << (((0x840 - yaw) & 0xfff) >> 9);
    unsigned front = 1u << (((0x900 - yaw) & 0xfff) >> 9);
    return left | right | front;
}
/* func_8009723C, free camera: 1 when group `index` of block `data` is left
 * out (a black shell, or hidden in the Outliner). */
int pc_room_editor_hidden(const void* data, unsigned index)
{
    if (!pc_scene_room_data || data != pc_scene_room_data() || !editor_block(data) || index >= editor_count)
        return 0;
    if (editor_hidden[index])
        return 1;
    return pc_editor_hide_masks && group_is_black(data, index);
}
/* The current room's groups (Outliner, RPC room_groups). */
int pc_room_editor_groups(pc_room_group_info* out, int max)
{
    const void* data = pc_scene_room_data ? pc_scene_room_data() : NULL;
    if (!editor_block(data))
        return 0;
    unsigned facing = pc_room_editor_facing();
    int n = 0;
    for (unsigned i = 0; i < editor_count && n < max; ++i, ++n) {
        const unsigned char* group = (const unsigned char*)data + 4 + i * 0x40;
        const unsigned* geometry = (const unsigned*)((const unsigned char*)data + *(const unsigned*)(group + 0x1c));
        unsigned flags = *(const unsigned*)group;
        pc_room_group_info* g = &out[n];
        g->flags = flags;
        g->polygons = geometry[0] + geometry[1] < 65536 ? (int)(geometry[0] + geometry[1]) : 0;
        g->black = group_is_black(data, i);
        g->hidden = editor_hidden[i];
        g->game_hidden = (flags & 0x1100) != 0;
        g->cutaway = !(facing >> 16) && ((flags ^ 255) & facing & 255) != 0;
        const int* position = (const int*)(group + 4);
        for (int k = 0; k < 3; k++)
            g->position[k] = position[k];
    }
    return n;
}
/* Hide or show group `index` in the free camera's view; -1 shows them all. */
void pc_room_editor_set_hidden(int index, int hidden)
{
    const void* data = pc_scene_room_data ? pc_scene_room_data() : NULL;
    if (!editor_block(data))
        return;
    if (index < 0)
        memset(editor_hidden, 0, sizeof editor_hidden);
    else if ((unsigned)index < editor_count)
        editor_hidden[index] = hidden != 0;
}
