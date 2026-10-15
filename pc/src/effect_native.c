/* Native drop shadows and effect particles (phase 2 step 4; design notes in
 * pc/docs/Native_Rendering.md, "Drop shadows and effects").
 *
 * Both still build their PS1 packets (the CPU's reference VRAM draws them);
 * what is added here is an effects list (render_rules.effects) per draw:
 * every polygon goes into the list's translucent sequence, and translucent
 * markers linked into the ordering table where the packets are draw them
 * in the PS1's order, in world space, hidden behind the native surfaces
 * before them. The first marker drawn covers the draw's packets, which the
 * hardware path then skips; if no marker can draw (no room list earlier in
 * that ordering table, the switch turned off) the packets draw as before.
 *
 * - Drop shadows (actor_shadow.c, func_800B4B14): three rings of untextured
 *   Gouraud triangles under each actor, subtractive (B-F). The ring is a
 *   fixed disc (D_800E9A68: 18 vertices in the XZ plane, D_800E9AD4: 24
 *   faces); the game transforms it by camera x terrain slope x actor yaw x
 *   size. Native: one mesh per light value (the rings' colours: light,
 *   light / 2 and black), an instance whose world transform is camera^-1 x
 *   that matrix (and the actor's previous one, so frame interpolation moves
 *   it with the actor), and the triangles the game kept (its NCLIP and
 *   bucket tests) as decided entries, one marker per bucket.
 * - Particles (effect_particles.c, func_800D6E44): per node a centre
 *   projected through the view (or a parent's) matrix and textured quads
 *   (POLY_FT4) placed around it in screen space, sized by the projection
 *   over the depth. Native: sprites (RENDER_POLY_SPRITE) at the centre's
 *   world position with the corners' screen offsets and the depth they were
 *   measured at, in a transient mesh per call; one marker per node at the
 *   node's bucket. Nodes the game puts on fixed layers (depth modes 5-8:
 *   the far end or in front of the whole table) stay packets. */
#define _POSIX_C_SOURCE 200809L
#include "psx_types.h"
#include "render.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int pc_hw_active;
extern void pc_hw_effect_packets(const void* start, const void* end, uint32_t list, int kind) __attribute__((weak));
extern int pc_native_camera(render_camera* out);
extern void pc_native_world_transform(const void* matrix, render_transform* out);
extern void pc_native_world_point(const double view[3], float out[3]);

static int compare_desc(const void* a, const void* b)
{
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? 1 : x > y ? -1 : 0;
}

/* ---- drop shadows ---- */

/* The disc (D_800E9A68) and its faces (D_800E9AD4), passed by actor_shadow.c
 * (battle data: absent from the plain build). */
static const unsigned *D_800E9A68, *D_800E9AD4;
#define SHADOW_FACES 24
static render_mesh shadow_meshes[256]; /* by light */
static unsigned shadow_data_hash;

static unsigned shadow_hash(void)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < 18; i++)
        h = (h ^ D_800E9A68[i]) * 16777619u;
    for (int i = 0; i < SHADOW_FACES; i++)
        h = (h ^ D_800E9AD4[i]) * 16777619u;
    return h;
}
/* The disc with the rings' colours for `light` (func_800B4B14: the inner
 * ring light at the centre and light / 2 at its edge, the middle ring
 * light / 2 to black, the outer ring black at its first vertex and light / 2
 * at the others). Untextured, subtractive. */
static render_mesh shadow_mesh(int light)
{
    unsigned hash = shadow_hash();
    if (hash != shadow_data_hash) {
        for (int i = 0; i < 256; i++)
            if (shadow_meshes[i]) {
                render_mesh_destroy(shadow_meshes[i]);
                shadow_meshes[i] = 0;
            }
        shadow_data_hash = hash;
    }
    if (shadow_meshes[light] && render_mesh_polygons(shadow_meshes[light]) == SHADOW_FACES)
        return shadow_meshes[light];
    render_polygon polygons[SHADOW_FACES];
    memset(polygons, 0, sizeof polygons);
    for (int i = 0; i < SHADOW_FACES; i++) {
        render_polygon* p = &polygons[i];
        int first = i < 8 ? light : i < 16 ? light >> 1 : 0;
        int others = i < 8 ? light >> 1 : i < 16 ? 0 : light >> 1;
        for (int k = 0; k < 3; k++) {
            unsigned offset = (D_800E9AD4[i] >> (k * 8)) & 255;
            unsigned v = offset < 72 && !(offset & 3) ? offset >> 2 : 0;
            unsigned word = D_800E9A68[v];
            p->position[k][0] = (float)(short)(word & 0xffff);
            p->position[k][1] = 0;
            p->position[k][2] = (float)(short)(word >> 16);
            int c = k ? others : first;
            p->rgb[k][0] = p->rgb[k][1] = p->rgb[k][2] = (uint8_t)c;
        }
        p->code = 0x32; /* POLY_G3, semi-transparent */
        p->tpage = 2 << 5; /* B-F (draw mode 0xE1000640) */
        p->flags = RENDER_POLY_DOUBLE_SIDED;
    }
    shadow_meshes[light] = render_mesh_create(polygons, SHADOW_FACES);
    return shadow_meshes[light];
}
/* Each actor's shadow transform from the frame before (interpolation). */
#define SHADOW_RECORDS 64
static struct {
    const void* actor;
    uint32_t serial;
    render_transform transform;
} shadow_records[SHADOW_RECORDS];

unsigned* pc_shadow_native(const void* actor, const void* matrix, int light, const unsigned char* faces,
    const unsigned short* buckets, int count, unsigned* start, unsigned* end, unsigned* ot, const unsigned* disc,
    const unsigned* disc_faces)
{
    D_800E9A68 = disc;
    D_800E9AD4 = disc_faces;
    uint32_t list = 0;
    if (pc_native_shadows && pc_hw_active && count > 0 && light >= 0 && light < 256) {
        render_camera camera;
        render_mesh mesh = pc_native_camera(&camera) ? shadow_mesh(light) : 0;
        if (mesh) {
            render_rules rules;
            memset(&rules, 0, sizeof rules);
            rules.far_bucket = 0x800;
            rules.lod_bucket = 0x7fffffff;
            rules.effects = RENDER_EFFECTS_SHADOWS;
            list = render_list_begin(&camera, &rules);
        }
        if (list) {
            uint32_t serial = render_frame_serial();
            render_transform now, previous;
            pc_native_world_transform(matrix, &now);
            int slot = -1, has_previous = 0;
            for (int i = 0; i < SHADOW_RECORDS && slot < 0; i++)
                if (shadow_records[i].actor == actor)
                    slot = i;
            if (slot >= 0 && shadow_records[slot].serial == serial - 1) {
                previous = shadow_records[slot].transform;
                has_previous = 1;
            }
            if (slot < 0 || shadow_records[slot].serial != serial) {
                if (slot < 0) {
                    slot = 0;
                    for (int i = 1; i < SHADOW_RECORDS; i++)
                        if ((uint32_t)(serial - shadow_records[i].serial)
                            > (uint32_t)(serial - shadow_records[slot].serial))
                            slot = i;
                }
                shadow_records[slot].actor = actor;
                shadow_records[slot].serial = serial;
                shadow_records[slot].transform = now;
            }
            render_instance instance;
            memset(&instance, 0, sizeof instance);
            instance.mesh = mesh;
            instance.palette = &now;
            instance.previous = has_previous ? &previous : NULL;
            instance.bones = 1;
            render_list_add(list, &instance);
            /* The PS1's order: far buckets first, within a bucket the later
             * triangle first (each is linked at the bucket's head). */
            uint32_t keys[SHADOW_FACES], entries[SHADOW_FACES];
            uint8_t flags[SHADOW_FACES];
            for (int i = 0; i < count; i++)
                keys[i] = (uint32_t)buckets[i] << 8 | faces[i];
            qsort(keys, (size_t)count, sizeof *keys, compare_desc);
            for (int i = 0; i < count; i++) {
                entries[i] = keys[i] & 0xff;
                flags[i] = RENDER_TRANSLUCENT_DECIDED;
            }
            int first = render_list_translucent(list, entries, flags, count);
            if (first < 0)
                list = 0;
            else
                for (int k = 0; k < count;) {
                    int bucket = (int)(keys[k] >> 8), run = k;
                    while (run < count && (int)(keys[run] >> 8) == bucket)
                        ++run;
                    end = render_list_link_translucent(list, first + k, run - k, ot + bucket, end);
                    k = run;
                }
        }
    }
    if (pc_hw_effect_packets)
        pc_hw_effect_packets(start, end, list, RENDER_EFFECTS_SHADOWS);
    return end;
}

/* ---- moving platforms' shadows (room_shadows.inc, func_8008CEB0) ---- */

/* The square under a room's moving platform (MPD section 12: lifts, floating
 * blocks): POLY_F4, semi-transparent, flat grey, after a draw mode packet of
 * B-F, so it darkens what is under it by its colour. Its size and colour
 * follow `shade` (255 minus half the platform's height above the floor), so
 * one mesh per shade, in the XZ plane about the square's centre. */
static render_mesh platform_meshes[256];
static render_mesh platform_mesh(int shade)
{
    if (platform_meshes[shade] && render_mesh_polygons(platform_meshes[shade]) == 1)
        return platform_meshes[shade];
    render_polygon p;
    memset(&p, 0, sizeof p);
    float r = (float)(32 + shade / 8); /* the game's half size */
    static const signed char corners[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } }; /* its vertex order */
    for (int k = 0; k < 4; k++) {
        p.position[k][0] = corners[k][0] * r;
        p.position[k][2] = corners[k][1] * r;
        p.rgb[k][0] = p.rgb[k][1] = p.rgb[k][2] = (uint8_t)(shade / 2 + 16);
    }
    p.code = 0x2a; /* POLY_F4, semi-transparent */
    p.tpage = 2 << 5; /* B-F (draw mode 0xE1000240) */
    p.flags = RENDER_POLY_QUAD | RENDER_POLY_DOUBLE_SIDED; /* (the game culls nothing) */
    platform_meshes[shade] = render_mesh_create(&p, 1);
    return platform_meshes[shade];
}
/* By section 12 index: each square as last drawn (frame interpolation takes
 * the frame before; tests read where it is: platform_shadow_smoke.py). */
#define PLATFORM_RECORDS 16
struct pc_platform_shadow_record {
    uint32_t serial;
    int32_t shade;
    float centre[3];
} pc_platform_shadow_records[PLATFORM_RECORDS];
static uint32_t platform_list, platform_serial;

/* One platform's square, `index` its section 12 entry, centred at `centre`
 * (world), linked at `ot_entry` (its bucket) as the game links its packets:
 * a translucent marker written at `packet`; returns the word after it (the
 * packet itself when it stays a packet). */
unsigned* pc_platform_shadow_native(int index, const int centre[3], int shade, unsigned* ot_entry, unsigned* packet)
{
    if (!pc_native_shadows || !pc_hw_active || shade < 0 || shade > 255)
        return packet;
    uint32_t serial = render_frame_serial();
    if (platform_serial != serial || !platform_list) {
        /* The first square of this frame: an effects list for them all. */
        platform_serial = serial;
        platform_list = 0;
        render_camera camera;
        if (!pc_native_camera(&camera))
            return packet;
        render_rules rules;
        memset(&rules, 0, sizeof rules);
        rules.far_bucket = 0x800;
        rules.lod_bucket = 0x7fffffff;
        rules.effects = RENDER_EFFECTS_SHADOWS;
        platform_list = render_list_begin(&camera, &rules);
        if (!platform_list)
            return packet;
    }
    render_mesh mesh = platform_mesh(shade);
    if (!mesh)
        return packet;
    render_transform now, previous;
    memset(&now, 0, sizeof now);
    for (int a = 0; a < 3; a++) {
        now.rotation[a][a] = 1;
        now.translation[a] = (float)centre[a];
    }
    previous = now;
    int has_previous = 0;
    if (index >= 0 && index < PLATFORM_RECORDS) {
        if (pc_platform_shadow_records[index].serial == serial - 1) {
            memcpy(previous.translation, pc_platform_shadow_records[index].centre, sizeof previous.translation);
            has_previous = 1;
        }
        pc_platform_shadow_records[index].serial = serial;
        pc_platform_shadow_records[index].shade = shade;
        memcpy(pc_platform_shadow_records[index].centre, now.translation, sizeof now.translation);
    }
    render_instance instance;
    memset(&instance, 0, sizeof instance);
    instance.mesh = mesh;
    instance.palette = &now;
    instance.previous = has_previous ? &previous : NULL;
    instance.bones = 1;
    int count = render_list_translucent_count(platform_list);
    if (count < 0)
        return packet;
    render_list_add(platform_list, &instance);
    /* The instance's index in the list is the number of squares so far
     * (one instance and one entry each). */
    uint32_t entry = (uint32_t)count << 16;
    uint8_t flag = RENDER_TRANSLUCENT_DECIDED; /* the game kept it (its depth range test) */
    int first = render_list_translucent(platform_list, &entry, &flag, 1);
    if (first < 0)
        return packet;
    return render_list_link_translucent(platform_list, first, 1, ot_entry, packet);
}
/* The walk is done: the squares' packets [start, end) are left to the list
 * once its first marker draws. */
void pc_platform_shadows_end(const void* start, const void* end)
{
    if (pc_hw_effect_packets)
        pc_hw_effect_packets(start, end, platform_serial == render_frame_serial() ? platform_list : 0,
            RENDER_EFFECTS_SHADOWS);
}

/* ---- particles ---- */

#define MAX_SPRITES 4096
#define MAX_NODES 1024
static render_polygon sprites[MAX_SPRITES];
static int sprite_count;
/* Nodes in chunks of RENDER_MAX_BONES: one transient mesh and instance per
 * chunk, a palette entry per node holding its centre (frame interpolation
 * moves each sprite from where its node was the frame before). */
#define CHUNK RENDER_MAX_BONES
static struct {
    int first, count, bucket;
    float world[3], previous[3];
} nodes[MAX_NODES];
static int node_count;
/* Where each node was in the last frame drawn: node address -> centre and
 * age (a node reused for a new particle starts younger). Open addressing. */
#define NODE_TABLE 2048
typedef struct {
    const void* node;
    int age;
    float world[3];
} node_place;
static node_place places[2][NODE_TABLE];
static int places_now; /* the table being filled this frame */
static uint32_t places_serial = ~0u;
static unsigned node_hash(const void* node) { return (unsigned)(((uintptr_t)node >> 2) * 2654435761u) % NODE_TABLE; }
static const node_place* place_before(const void* node)
{
    const node_place* table = places[!places_now];
    for (unsigned h = node_hash(node), n = 0; n < NODE_TABLE; n++, h = (h + 1) % NODE_TABLE) {
        if (!table[h].node)
            return NULL;
        if (table[h].node == node)
            return &table[h];
    }
    return NULL;
}
static void place_now(const void* node, int age, const float world[3])
{
    node_place* table = places[places_now];
    for (unsigned h = node_hash(node), n = 0; n < NODE_TABLE; n++, h = (h + 1) % NODE_TABLE)
        if (!table[h].node || table[h].node == node) {
            table[h].node = node;
            table[h].age = age;
            memcpy(table[h].world, world, sizeof table[h].world);
            return;
        }
}
/* Packet ranges of the native nodes (consecutive nodes merged). */
#define MAX_RANGES 64
static unsigned* ranges[MAX_RANGES][2];
static int range_count;
static int collecting;
static uint32_t camera_serial = ~0u;
static int camera_ok;

/* func_800D6E44 starts its walk. */
void pc_particles_native_begin(void)
{
    sprite_count = node_count = range_count = 0;
    collecting = pc_native_effects && pc_hw_active;
    /* A new frame: this frame's places replace the last frame's (the walk
     * runs once per frame; a frame without it forgets them). */
    uint32_t serial = render_frame_serial();
    if (places_serial != serial) {
        int fresh = places_serial + 1 == serial;
        places_now = !places_now;
        if (!fresh)
            memset(places[!places_now], 0, sizeof places[0]);
        memset(places[places_now], 0, sizeof places[0]);
        places_serial = serial;
    }
}
/* One node's packets [start, end): an E1 draw mode, `tiles` POLY_FT4 quads,
 * an E1; its centre at view-space `view` (GTE units, depth `sz`), projected
 * to screen position `xy` (packed); linked at ordering-table index
 * `bucket` (`layered`: one of the fixed layers, which stays packets). */
void pc_particles_native_node(unsigned* start, unsigned* end, int tiles, const int view[3], int sz, unsigned xy,
    int bucket, int layered, const void* node, int age, unsigned source)
{
    int native = collecting && !layered && bucket >= 0 && bucket < 0x800 && sz > 0 && tiles > 0
              && node_count < MAX_NODES && sprite_count + tiles <= MAX_SPRITES
              && (range_count < MAX_RANGES || ranges[range_count - 1][1] == start);
    if (native && camera_serial != render_frame_serial()) {
        render_camera camera; /* also sets the camera inverse for pc_native_world_point */
        camera_ok = pc_native_camera(&camera);
        camera_serial = render_frame_serial();
    }
    if (!native || !camera_ok) {
        if (pc_hw_effect_packets)
            pc_hw_effect_packets(start, end, 0, RENDER_EFFECTS_SPRITES);
        return;
    }
    double centre[3] = { view[0] + 0.5, view[1] + 0.5, view[2] + 0.5 };
    float world[3];
    pc_native_world_point(centre, world);
    short cx = (short)(xy & 0xffff), cy = (short)(xy >> 16);
    /* Where the node was the frame before: the same node, not younger (a
     * reused node is a new particle), not far away. */
    const node_place* before = node ? place_before(node) : NULL;
    float d = before ? fabsf(before->world[0] - world[0]) + fabsf(before->world[1] - world[1])
                           + fabsf(before->world[2] - world[2])
                     : 0;
    int moved = before && before->age <= age && d < 256.0f;
    /* ROOD_PARTICLE_DEBUG=1: per 60 frames, nodes drawn, nodes found where
     * they were the frame before and their mean step (world units). */
    static int debug = -1, frames, total, found;
    static double steps;
    if (debug < 0)
        debug = getenv("ROOD_PARTICLE_DEBUG") != NULL;
    if (debug) {
        static uint32_t counted = ~0u;
        if (counted != render_frame_serial() && ++frames % 60 == 0) {
            fprintf(stderr, "[particles] %d nodes, %d followed from the frame before, mean step %.2f units\n", total,
                found, found ? steps / found : 0.0);
            total = found = 0;
            steps = 0;
        }
        counted = render_frame_serial();
        ++total;
        if (moved)
            ++found, steps += d;
    }
    memcpy(nodes[node_count].world, world, sizeof world);
    memcpy(nodes[node_count].previous, moved ? before->world : world, sizeof world);
    if (node)
        place_now(node, age, world);
    int bone = node_count % CHUNK;
    nodes[node_count].first = sprite_count;
    nodes[node_count].count = tiles;
    nodes[node_count++].bucket = bucket;
    const unsigned* q = start + 3; /* after the E1 packet */
    for (int t = 0; t < tiles; t++, q += 10) {
        render_polygon* p = &sprites[sprite_count++];
        memset(p, 0, sizeof *p);
        static const int xy_word[4] = { 2, 4, 6, 8 }, uv_word[4] = { 3, 5, 7, 9 };
        for (int k = 0; k < 4; k++) {
            unsigned c = q[xy_word[k]], uv = q[uv_word[k]];
            /* at the node's palette entry (its centre) */
            p->bone[k] = (uint8_t)bone;
            p->offset[k][0] = (int16_t)((short)(c & 0xffff) - cx);
            p->offset[k][1] = (int16_t)((short)(c >> 16) - cy);
            p->uv[k][0] = uv & 0xff;
            p->uv[k][1] = (uv >> 8) & 0xff;
            p->rgb[k][0] = q[1] & 0xff;
            p->rgb[k][1] = (q[1] >> 8) & 0xff;
            p->rgb[k][2] = (q[1] >> 16) & 0xff;
        }
        p->offset_depth = (uint16_t)(sz > 65535 ? 65535 : sz);
        p->code = (uint8_t)(q[1] >> 24);
        p->clut = (uint16_t)(q[3] >> 16);
        p->tpage = (uint16_t)(q[5] >> 16);
        p->flags = RENDER_POLY_QUAD | RENDER_POLY_SPRITE | RENDER_POLY_DOUBLE_SIDED;
        p->source = source; /* its particle system (glows.c) */
    }
    if (range_count && ranges[range_count - 1][1] == start)
        ranges[range_count - 1][1] = end;
    else {
        ranges[range_count][0] = start;
        ranges[range_count++][1] = end;
    }
}
/* The walk is done: the sprites become a transient mesh in an effects list,
 * a marker per node at its bucket (linked after the node's packets: drawn
 * before them, so it covers them). `packet` is the free packet space;
 * returns it past the markers. */
unsigned* pc_particles_native_end(unsigned* packet, unsigned* ot)
{
    uint32_t list = 0;
    if (collecting && node_count) {
        render_camera camera;
        if (pc_native_camera(&camera)) {
            render_rules rules;
            memset(&rules, 0, sizeof rules);
            rules.far_bucket = 0x800;
            rules.lod_bucket = 0x7fffffff;
            rules.effects = RENDER_EFFECTS_SPRITES;
            list = render_list_begin(&camera, &rules);
        }
        /* One instance per chunk of nodes: its own transient mesh (the
         * chunk's sprites) and a palette entry per node at its centre, with
         * where it was the frame before. */
        for (int c = 0; list && c * CHUNK < node_count; c++) {
            int n0 = c * CHUNK, n1 = n0 + CHUNK < node_count ? n0 + CHUNK : node_count;
            int s0 = nodes[n0].first, s1 = nodes[n1 - 1].first + nodes[n1 - 1].count;
            render_mesh mesh = render_mesh_transient(sprites + s0, s1 - s0);
            if (!mesh) {
                list = 0;
                break;
            }
            render_transform now[CHUNK], was[CHUNK];
            memset(now, 0, sizeof now);
            for (int n = n0; n < n1; n++) {
                render_transform* x = &now[n - n0];
                x->rotation[0][0] = x->rotation[1][1] = x->rotation[2][2] = 1;
                memcpy(x->translation, nodes[n].world, sizeof x->translation);
                was[n - n0] = *x;
                memcpy(was[n - n0].translation, nodes[n].previous, sizeof x->translation);
            }
            render_instance instance;
            memset(&instance, 0, sizeof instance);
            instance.mesh = mesh;
            instance.palette = now;
            instance.previous = was;
            instance.bones = n1 - n0;
            render_list_add(list, &instance);
        }
        if (list) {
            static uint32_t entries[MAX_SPRITES];
            for (int n = 0; n < node_count; n++) {
                /* The node's quads are linked at its bucket's head one by
                 * one: the last is drawn first (instance: the node's chunk,
                 * polygon: within the chunk's mesh). */
                int chunk = n / CHUNK, base = nodes[chunk * CHUNK].first;
                for (int t = 0; t < nodes[n].count; t++)
                    entries[t] = (uint32_t)chunk << 16 | (uint32_t)(nodes[n].first + nodes[n].count - 1 - t - base);
                int first = render_list_translucent(list, entries, NULL, nodes[n].count);
                if (first < 0)
                    break;
                packet = render_list_link_translucent(list, first, nodes[n].count, ot + nodes[n].bucket, packet);
            }
        }
    }
    for (int r = 0; r < range_count; r++)
        if (pc_hw_effect_packets)
            pc_hw_effect_packets(ranges[r][0], ranges[r][1], list, RENDER_EFFECTS_SPRITES);
    collecting = 0;
    return packet;
}
