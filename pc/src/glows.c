/* The game's glows as stable, smoothed light sources (glow_overrides.h;
 * design notes in pc/docs/Glows.md).
 *
 * ptgi.c hands over every glowing triangle of the frame (additive room
 * polygons, particles, object polygons) with the colour of its texels and a
 * source tag; here they become glows:
 *
 * - Room polygons are grouped once per room capture by connectivity (shared
 *   corners), large groups split into 128-unit pieces in polygon order. A
 *   group's position is its area-weighted centre (fixed), its colour and
 *   strength this frame's texels (a scrolling texture changes them, not the
 *   group). Before, all glowing triangles were re-clustered every frame,
 *   brightest first, so the save point's column fell apart into 2-4
 *   emitters that changed with its scrolling texture.
 * - Other triangles are grouped by source tag (a particle system, an object
 *   mesh, a packet texture); a source's triangles join the glow of the same
 *   source they are nearest to (96 units), the rest start new glows.
 * - Each glow keeps an id for the room visit and a key for room files
 *   (origin, tag, first position), and smooths its observation (position,
 *   colour, size: exponential, its category's seconds of game time; a
 *   jump of more than 192 units snaps). A glow fades in when its source
 *   appears and out when it goes (kept a second, so a flicker keeps its id).
 * - Overrides, mutes, the solo and light-actor links apply by id; the
 *   tracers get up to 32 glows (strongest, with hysteresis), in id order. */
#define _GNU_SOURCE
#include "glow_overrides.h"
#include "room_effects.h"
#include "scene_capture.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Per category (PC_GLOW_FIXTURE, _MAGIC, _EFFECT): enabled, intensity and
 * reach multipliers, casts shadows (never for effects), smoothing seconds;
 * room settings glow_<category>_<field> (room_settings.cpp). */
float pc_glow_category[3][5] = {
    { 1, 1, 1, 1, 0.15f },
    { 1, 1, 1, 1, 0.15f },
    { 1, 0.5f, 1, 0, 0.10f }, /* effects at half: their light now lasts its decay (a hit stays as bright overall) */
};
float pc_glow_energy, pc_glow_raw_energy, pc_glow_category_energy[3];
int pc_glow_category_count[3];
uint32_t pc_glow_casts;
/* Enclosing glows as columns (fit_column): 0 never (every glow a sphere at
 * its centre, as before), 1 their glowing side (default), 2 solid (RPC glows
 * {shapes}; ROOD_GLOW_SHAPES; comparisons). */
int pc_glow_shapes = -1;
/* A column's shadows (rt_lights.glsl emitter_shadow): rays aim at a core
 * this fraction of its radius (the fixture type's glow_shadow_core first;
 * RPC glows {shadow_core}); the light still comes from the whole side. */
float pc_glow_shadow_core = 0.25f;
/* ... and stop where they enter its clear middle, this fraction of its
 * radius: a character standing there hides none of its light (RPC glows
 * {shadow_clear}). */
float pc_glow_shadow_clear = 0.4f;
/* A column's luminance in the baked light's shadow weights (0: its colour's;
 * RPC glows {column_baked}). */
float pc_glow_column_baked = 3.0f;
extern float pc_emitter_radius;
extern unsigned pc_vsync_count(void) __attribute__((weak));
/* Light actors linked to a glow (lights.cpp): 1 drives its emission
 * (*scale, the actor's flicker), 2 suppresses it, 0 leaves it. */
extern int pc_lights_glow_drive(const char* key, const float p[3], float* scale) __attribute__((weak));
extern int pc_fixture_glow_mode(const char* type) __attribute__((weak));
extern int pc_fixture_glow_shape(const char* type) __attribute__((weak));
extern float pc_fixture_glow_shadow_core(const char* type) __attribute__((weak));

#define MAX_TRACKS 160
#define MAX_GROUPS 512
#define MAX_FRAGMENTS 8192
#define JOIN_RADIUS 96.0f
#define SNAP_DISTANCE 192.0f

typedef struct {
    uint32_t id, source; /* source: origin << 28 | tag (room: group tag) */
    int origin, group; /* room group / baked index, -1 */
    int cell[3], in_cell; /* a spread source's grid cell (see associate) */
    float home[3];
    char key[PC_GLOW_KEY];
    char kind[40]; /* name without the number */
    /* smoothed */
    float pos[3], rgb[3], size, power, strength;
    /* this frame's observation (kept while absent) */
    float obs_pos[3], obs_rgb[3], obs_size, obs_power, obs_strength;
    int observed, triangles, ever;
    int category, promoted; /* PC_GLOW_FIXTURE / _MAGIC / _EFFECT; promoted: an effect that stayed */
    float presence, absent, age, seen; /* seen: seconds observed */
    int traced;
    /* this update, after overrides */
    float out_rgb[3], out_size, out_reach;
    float out_centre[3], out_shape[4]; /* where and what the tracers sample: its centre and sphere, or a column */
    float out_shadow_core; /* a column's shadow core radius */
    float out_baked_weight; /* a column's weight in the baked light's shadows (0: by its colour) */
    int muted, overridden;
    char fixture_type[16]; /* from its override (the generated layer) */
} track;
static track tracks[MAX_TRACKS];
static int track_count;
static uint32_t next_id;

/* ---- room groups (once per room capture) ---- */
typedef struct {
    float centre[3], area, size;
    float strength; /* light scale: the 96-unit pieces it spans (see build_groups) */
    uint32_t tag;
    int animated, triangles;
    float seeds[64][3]; /* those pieces' seeds */
    /* Its column (fit_column): centre, half axis and radius (> 0 its side,
     * < 0 a solid disc); has_column: one was fitted; encloses: a character
     * can stand inside it (the save point). */
    float column_centre[3], column[4];
    int has_column, encloses;
} room_group;
static room_group groups[MAX_GROUPS];
static int group_count, groups_serial = -1, save_point_room;
static int* triangle_group; /* per room triangle: its group, -1 */
static int triangle_group_size;

static float dist2(const float a[3], const float b[3])
{
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
}
static float luminance(const float c[3]) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; }
static int find_root(int* parent, int i)
{
    while (parent[i] != i)
        i = parent[i] = parent[parent[i]];
    return i;
}
static int additive_room(const scene_triangle* t)
{
    unsigned mode = (t->tpage >> 5) & 3;
    return (t->code & 2) && (mode == 1 || mode == 3);
}
static float emitting_size(float area)
{
    /* Glow sprites are much larger than the flame behind them: the
     * emitting sphere is a fraction of their extent. */
    float size = 0.15f * sqrtf(area);
    return size < 2 ? 2 : size > 8 ? 8 : size;
}
/* Enclosing glows (pc/docs/Glows.md): a room group a character can stand
 * in (the save point: an octagonal column of light 128 units wide and 224
 * high around a glowing sigil) cannot be a point at its centre, which is
 * then inside the character: every shadow ray from the room to it hit them,
 * and the room went dark around Ashley (GitHub issue 2). Such a group gets a
 * vertical column the size of its glowing polygons, and the tracers sample
 * its side (rt_lights.glsl emitter_sample): a character inside hides the
 * part of it behind them, and still shadows what lies beyond them.
 *
 * Its upright polygons (normal within 45 degrees of horizontal) give the
 * column: the axis through the middle of their bounds, from their lowest to
 * their highest corner, the radius their corners' mean distance from the
 * axis. A group of mostly flat polygons is a solid disc as large as their
 * area (only when its fixture type or an override asks for a column). A
 * column encloses (and is one without being asked) when a character fits
 * in it and it stands on the floor: radius >= 24, at least 96 high, upright
 * polygons in at least six of eight sectors around its axis, and a room
 * polygon under the middle of its foot. Across the game's rooms that is
 * the 39 save points and one light shaft standing on a street (zone 32
 * room 3, fixture type T235, not a light: its type suppresses it); hanging
 * glass lanterns are too small and in the air, other shafts float, glowing
 * floor decals are flat. */
#define COLUMN_MIN_RADIUS 24.0f
#define COLUMN_MIN_HEIGHT 96.0f
static void fit_column(room_group* g, int index, const scene_triangle* room, const int* glowing, int n)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    float up_lo = 1e30f, up_hi = -1e30f, up_area = 0, flat_area = 0, flat_y = 0;
    for (int a = 0; a < n; a++) {
        if (triangle_group[glowing[a]] != index)
            continue;
        const scene_triangle* t = &room[glowing[a]];
        float e1[3], e2[3], x[3];
        for (int k = 0; k < 3; k++)
            e1[k] = t->vertex[1].position[k] - t->vertex[0].position[k],
            e2[k] = t->vertex[2].position[k] - t->vertex[0].position[k];
        x[0] = e1[1] * e2[2] - e1[2] * e2[1], x[1] = e1[2] * e2[0] - e1[0] * e2[2], x[2] = e1[0] * e2[1] - e1[1] * e2[0];
        float length = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]), area = 0.5f * length;
        int upright = length > 1e-6f && fabsf(x[1]) < 0.7071f * length;
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) {
                float v = t->vertex[j].position[k];
                lo[k] = fminf(lo[k], v), hi[k] = fmaxf(hi[k], v);
                if (upright && k == 1)
                    up_lo = fminf(up_lo, v), up_hi = fmaxf(up_hi, v);
            }
        if (upright)
            up_area += area;
        else {
            flat_area += area;
            flat_y += area * (t->vertex[0].position[1] + t->vertex[1].position[1] + t->vertex[2].position[1]) / 3.0f;
        }
    }
    g->has_column = g->encloses = 0;
    if (up_area + flat_area <= 0)
        return;
    float axis[2] = { 0.5f * (lo[0] + hi[0]), 0.5f * (lo[2] + hi[2]) };
    int column = up_area >= flat_area;
    float radius_sum = 0, radius_weight = 0;
    unsigned sectors = 0; /* around its axis, of eight */
    for (int a = 0; a < n; a++) {
        if (triangle_group[glowing[a]] != index)
            continue;
        const scene_triangle* t = &room[glowing[a]];
        float e1[3], e2[3], x[3];
        for (int k = 0; k < 3; k++)
            e1[k] = t->vertex[1].position[k] - t->vertex[0].position[k],
            e2[k] = t->vertex[2].position[k] - t->vertex[0].position[k];
        x[0] = e1[1] * e2[2] - e1[2] * e2[1], x[1] = e1[2] * e2[0] - e1[0] * e2[2], x[2] = e1[0] * e2[1] - e1[1] * e2[0];
        float length = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]), area = 0.5f * length;
        int upright = length > 1e-6f && fabsf(x[1]) < 0.7071f * length;
        if (upright != column)
            continue;
        for (int j = 0; j < 3; j++) {
            float dx = t->vertex[j].position[0] - axis[0], dz = t->vertex[j].position[2] - axis[1];
            radius_sum += area * sqrtf(dx * dx + dz * dz), radius_weight += area;
        }
        float dx = (t->vertex[0].position[0] + t->vertex[1].position[0] + t->vertex[2].position[0]) / 3.0f - axis[0];
        float dz = (t->vertex[0].position[2] + t->vertex[1].position[2] + t->vertex[2].position[2]) / 3.0f - axis[1];
        if (dx * dx + dz * dz > 1.0f)
            sectors |= 1u << ((int)floorf((atan2f(dz, dx) + 3.14159265f) / 6.2831853f * 8.0f) & 7);
    }
    if (column) {
        float radius = radius_weight > 0 ? radius_sum / radius_weight : 0;
        g->column_centre[0] = axis[0], g->column_centre[1] = 0.5f * (up_lo + up_hi), g->column_centre[2] = axis[1];
        g->column[0] = g->column[2] = 0, g->column[1] = 0.5f * (up_hi - up_lo), g->column[3] = radius;
        int around = 0;
        for (int k = 0; k < 8; k++)
            around += (sectors >> k) & 1;
        float foot[3] = { axis[0], up_hi, axis[1] }; /* y grows downwards */
        g->encloses = radius >= COLUMN_MIN_RADIUS && up_hi - up_lo >= COLUMN_MIN_HEIGHT && around >= 6
            && pc_scene_triangle_at(foot, 16.0f) >= 0;
    } else {
        float radius = sqrtf(flat_area / 3.14159265f);
        g->column_centre[0] = axis[0], g->column_centre[1] = flat_y / flat_area, g->column_centre[2] = axis[1];
        g->column[0] = g->column[1] = g->column[2] = 0, g->column[3] = -radius;
        g->encloses = 0;
    }
    g->has_column = fabsf(g->column[3]) > 0;
}
static void build_groups(void)
{
    int count;
    const scene_triangle* room = pc_scene_triangle_data(&count);
    group_count = 0;
    if (count > triangle_group_size) {
        free(triangle_group);
        triangle_group = malloc((size_t)count * sizeof *triangle_group);
        triangle_group_size = triangle_group ? count : 0;
    }
    if (!triangle_group)
        return;
    int* glowing = malloc((size_t)(count ? count : 1) * sizeof *glowing);
    int* parent = malloc((size_t)(count ? count : 1) * sizeof *parent);
    int n = 0;
    for (int i = 0; i < count; i++) {
        triangle_group[i] = -1;
        if (additive_room(&room[i]))
            glowing[n++] = i;
    }
    for (int a = 0; a < n; a++)
        parent[a] = a;
    /* Connected when their bounds come within 24 units (one fixture's
     * polygons: the save point's column, its caps and the sigil under it). */
    float (*box)[6] = malloc((size_t)(n ? n : 1) * sizeof *box);
    for (int a = 0; a < n; a++) {
        const scene_triangle* t = &room[glowing[a]];
        for (int k = 0; k < 3; k++) {
            box[a][k] = fminf(fminf(t->vertex[0].position[k], t->vertex[1].position[k]), t->vertex[2].position[k]) - 12.0f;
            box[a][k + 3] = fmaxf(fmaxf(t->vertex[0].position[k], t->vertex[1].position[k]), t->vertex[2].position[k]) + 12.0f;
        }
    }
    for (int a = 0; a < n && n <= 4096; a++)
        for (int b = a + 1; b < n; b++) {
            int touch = 1;
            for (int k = 0; k < 3 && touch; k++)
                touch = box[a][k] <= box[b][k + 3] && box[b][k] <= box[a][k + 3];
            if (touch) {
                int ra = find_root(parent, a), rb = find_root(parent, b);
                if (ra != rb)
                    parent[rb < ra ? ra : rb] = rb < ra ? rb : ra;
            }
        }
    /* Components in polygon order; one wider than 384 units splits into
     * pieces around seeds (members within 160 units of a seed's centre). */
    int* done = calloc((size_t)(n ? n : 1), sizeof *done);
    int animated_count;
    const int* animated = pc_scene_animated(&animated_count);
    for (int a = 0; a < n; a++) {
        if (done[a])
            continue;
        int root = find_root(parent, a);
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (int b = a; b < n; b++)
            if (find_root(parent, b) == root)
                for (int j = 0; j < 3; j++)
                    for (int k = 0; k < 3; k++) {
                        float v = room[glowing[b]].vertex[j].position[k];
                        lo[k] = fminf(lo[k], v), hi[k] = fmaxf(hi[k], v);
                    }
        float extent = sqrtf(dist2(lo, hi));
        int first_group = group_count;
        for (int b = a; b < n; b++) {
            if (done[b] || find_root(parent, b) != root)
                continue;
            const scene_triangle* t = &room[glowing[b]];
            float c[3];
            for (int k = 0; k < 3; k++)
                c[k] = (t->vertex[0].position[k] + t->vertex[1].position[k] + t->vertex[2].position[k]) / 3.0f;
            int g = first_group;
            if (extent > 384.0f)
                for (; g < group_count; g++)
                    if (dist2(c, groups[g].centre) < 160.0f * 160.0f)
                        break;
            if (g == group_count) {
                if (group_count == MAX_GROUPS)
                    continue;
                memset(&groups[g], 0, sizeof groups[g]);
                memcpy(groups[g].centre, c, sizeof c); /* the seed, until the members average it */
                groups[g].tag = 2166136261u;
                group_count++;
            }
            done[b] = 1;
            triangle_group[glowing[b]] = g;
            /* tag: the members' polygon offsets (the same every visit) */
            groups[g].tag = (groups[g].tag ^ (t->polygon * 2 + t->quad)) * 16777619u;
            groups[g].triangles++;
            for (int i = 0; i < animated_count; i++)
                groups[g].animated |= animated[i] == glowing[b];
            /* Its light: as many 96-unit pieces as it spans (what the
             * per-frame clustering this replaced made of it on average, each
             * piece a full emitter: a tall column stays as bright). */
            int piece = 0, pieces = (int)groups[g].strength;
            for (; piece < pieces; piece++)
                if (dist2(c, groups[g].seeds[piece]) < 96.0f * 96.0f)
                    break;
            if (piece == pieces && pieces < 64) {
                memcpy(groups[g].seeds[pieces], c, sizeof c);
                groups[g].strength = (float)(pieces + 1);
            }
        }
    }
    /* Area-weighted centres. */
    float sum[MAX_GROUPS][3];
    memset(sum, 0, sizeof sum);
    for (int i = 0; i < count; i++) {
        int g = triangle_group[i];
        if (g < 0)
            continue;
        const scene_triangle* t = &room[i];
        float e1[3], e2[3], x[3];
        for (int k = 0; k < 3; k++)
            e1[k] = t->vertex[1].position[k] - t->vertex[0].position[k],
            e2[k] = t->vertex[2].position[k] - t->vertex[0].position[k];
        x[0] = e1[1] * e2[2] - e1[2] * e2[1], x[1] = e1[2] * e2[0] - e1[0] * e2[2], x[2] = e1[0] * e2[1] - e1[1] * e2[0];
        float area = 0.5f * sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) + 1e-3f;
        for (int k = 0; k < 3; k++)
            sum[g][k] += area * (t->vertex[0].position[k] + t->vertex[1].position[k] + t->vertex[2].position[k]) / 3.0f;
        groups[g].area += area;
    }
    for (int g = 0; g < group_count; g++) {
        for (int k = 0; k < 3; k++)
            groups[g].centre[k] = sum[g][k] / groups[g].area;
        groups[g].size = emitting_size(groups[g].area);
        fit_column(&groups[g], g, room, glowing, n);
    }
    /* The save point: its column and sigil scroll (room texture effect
     * type 2, which only save points use). */
    int effects;
    const pc_uv_effect* fx = pc_uv_effects(&effects);
    save_point_room = 0;
    for (int i = 0; fx && i < effects; i++)
        save_point_room |= fx[i].type == 2;
    free(box);
    free(done);
    free(parent);
    free(glowing);
    if (getenv("ROOD_GI_DEBUG"))
        for (int g = 0; g < group_count; g++)
            fprintf(stderr,
                "[glows] room group %d: %d triangles at %.0f %.0f %.0f, area %.0f, strength %.0f%s; column at %.0f %.0f %.0f, "
                "half height %.0f, radius %.0f%s\n",
                g, groups[g].triangles, groups[g].centre[0], groups[g].centre[1], groups[g].centre[2], groups[g].area,
                groups[g].strength, groups[g].animated ? ", animated" : "", groups[g].column_centre[0],
                groups[g].column_centre[1], groups[g].column_centre[2], groups[g].column[1], groups[g].column[3],
                groups[g].encloses ? " (encloses)" : "");
}

/* ---- naming ---- */
static const char* origin_name(int origin)
{
    static const char* names[] = { "?", "room", "object", "particles", "packet", "baked" };
    return origin >= 1 && origin <= 5 ? names[origin] : "?";
}
static void classify(track* t)
{
    const float* c = t->obs_rgb;
    float size = t->obs_size;
    /* by hue (a dim ember is as warm as a bright one) */
    int warm = c[0] > 0.02f && c[0] >= 1.25f * c[1] && c[0] >= 1.5f * c[2];
    int cool = c[2] > 0.02f && c[2] >= 1.15f * c[0];
    /* The category: room polygons are fixtures (the save point and cool
     * glows magic); particles, objects and packets are transient effects
     * (attacks, spells, sparks) unless one stays (promote()). */
    if (t->origin == PC_GLOW_ORIGIN_ROOM || t->origin == PC_GLOW_ORIGIN_BAKED || t->promoted)
        t->category = (t->origin == PC_GLOW_ORIGIN_ROOM && t->group >= 0 && groups[t->group].animated && save_point_room)
                || (cool && t->origin != PC_GLOW_ORIGIN_BAKED)
            ? PC_GLOW_MAGIC
            : PC_GLOW_FIXTURE;
    else
        t->category = PC_GLOW_EFFECT;
    if (t->origin == PC_GLOW_ORIGIN_BAKED)
        snprintf(t->kind, sizeof t->kind, "Baked estimate");
    else if (t->origin == PC_GLOW_ORIGIN_ROOM && t->group >= 0 && groups[t->group].animated && save_point_room)
        snprintf(t->kind, sizeof t->kind, "Save point glow");
    else if (t->in_cell)
        snprintf(t->kind, sizeof t->kind, "%s", warm ? "Embers" : "Sparkles");
    else if (warm)
        snprintf(t->kind, sizeof t->kind, "%s", size < 3.5f ? "Flame (candle)" : size < 6.5f ? "Flame (torch)" : "Fire");
    else if (cool)
        snprintf(t->kind, sizeof t->kind, "Magic glow");
    else
        snprintf(t->kind, sizeof t->kind, "%s", t->origin == PC_GLOW_ORIGIN_ROOM ? "Room glow" : "Glow");
}

/* ---- tracks ---- */
static track* track_by_id(uint32_t id)
{
    for (int i = 0; i < track_count; i++)
        if (tracks[i].id == id)
            return &tracks[i];
    return NULL;
}
static track* new_track(uint32_t source, int group, const float p[3])
{
    if (track_count == MAX_TRACKS)
        return NULL;
    track* t = &tracks[track_count++];
    memset(t, 0, sizeof *t);
    t->id = ++next_id;
    t->source = source;
    t->origin = (int)(source >> 28);
    t->group = group;
    t->category = t->origin == PC_GLOW_ORIGIN_ROOM || t->origin == PC_GLOW_ORIGIN_BAKED ? PC_GLOW_FIXTURE : PC_GLOW_EFFECT;
    memcpy(t->home, p, sizeof t->home);
    memcpy(t->pos, p, sizeof t->pos);
    memcpy(t->obs_pos, p, sizeof t->obs_pos);
    snprintf(t->key, sizeof t->key, "%s:%07x@%d,%d,%d", origin_name(t->origin), source & 0x0fffffffu,
        (int)lrintf(p[0]), (int)lrintf(p[1]), (int)lrintf(p[2]));
    return t;
}

/* ---- overrides ---- */
#define MAX_OVERRIDES 64
static pc_glow_override overrides[MAX_OVERRIDES];
static int override_count;
static uint32_t solo_id;

static int parse_key(const char* key, int* origin, uint32_t* tag, float home[3])
{
    char name[16];
    unsigned t;
    int x, y, z;
    if (sscanf(key, "%15[a-z]:%x@%d,%d,%d", name, &t, &x, &y, &z) != 5)
        return 0;
    *origin = 0;
    for (int o = 1; o <= 5; o++)
        if (!strcmp(name, origin_name(o)))
            *origin = o;
    *tag = t;
    home[0] = (float)x, home[1] = (float)y, home[2] = (float)z;
    return *origin != 0;
}
uint32_t pc_gi_glow_by_key(const char* key)
{
    if (!key || !key[0])
        return 0;
    for (int i = 0; i < track_count; i++)
        if (!strcmp(tracks[i].key, key))
            return tracks[i].id;
    int origin;
    uint32_t tag;
    float home[3];
    if (!parse_key(key, &origin, &tag, home))
        return 0;
    /* The same source near the key's home, else the same origin there
     * (tags of meshes and particle systems may differ between runs). */
    for (int pass = 0; pass < 2; pass++) {
        uint32_t best = 0;
        float best_d = JOIN_RADIUS * JOIN_RADIUS;
        for (int i = 0; i < track_count; i++) {
            const track* t = &tracks[i];
            if (t->origin != origin || (pass == 0 && (t->source & 0x0fffffffu) != tag))
                continue;
            float d = fminf(dist2(t->home, home), dist2(t->pos, home));
            if (d < best_d)
                best_d = d, best = t->id;
        }
        if (best)
            return best;
    }
    return 0;
}
uint32_t pc_gi_glow_near(const float p[3], float radius)
{
    uint32_t best = 0;
    float best_d = radius * radius;
    for (int i = 0; i < track_count; i++) {
        float d = dist2(tracks[i].pos, p);
        if (d < best_d && tracks[i].category != PC_GLOW_EFFECT)
            best_d = d, best = tracks[i].id;
    }
    return best;
}
/* Bind each override to its glow (once per visit; by key, else by the
 * position of an old line, which then takes the glow's key). */
static void bind_overrides(void)
{
    for (int i = 0; i < override_count; i++) {
        pc_glow_override* o = &overrides[i];
        const track* bound = o->bound ? track_by_id(o->bound) : NULL;
        if (bound && bound->category != PC_GLOW_EFFECT)
            continue;
        uint32_t id = o->key[0] ? pc_gi_glow_by_key(o->key) : pc_gi_glow_near(o->position, JOIN_RADIUS);
        const track* found = id ? track_by_id(id) : NULL;
        if (found && found->category == PC_GLOW_EFFECT)
            id = 0; /* not (yet) a persistent glow */
        for (int j = 0; j < override_count && id; j++)
            if (j != i && overrides[j].bound == id)
                id = 0; /* another override has it */
        o->bound = id;
        const track* t = track_by_id(id);
        if (t && !o->key[0])
            snprintf(o->key, sizeof o->key, "%s", t->key);
    }
}
pc_glow_override* pc_gi_glow_override_id(uint32_t id, int create)
{
    const track* t = track_by_id(id);
    if (!t || t->category == PC_GLOW_EFFECT) /* effects: by category only */
        return NULL;
    for (int i = 0; i < override_count; i++)
        if (overrides[i].bound == id)
            return &overrides[i];
    if (!create || override_count == MAX_OVERRIDES)
        return NULL;
    pc_glow_override* o = &overrides[override_count++];
    memset(o, 0, sizeof *o);
    memcpy(o->position, t->pos, sizeof o->position);
    o->intensity = 1;
    o->tint[0] = o->tint[1] = o->tint[2] = 1;
    snprintf(o->key, sizeof o->key, "%s", t->key);
    o->bound = id;
    return o;
}
void pc_gi_glow_remove_id(uint32_t id)
{
    for (int i = 0; i < override_count; i++)
        if (overrides[i].bound == id) {
            overrides[i] = overrides[--override_count];
            return;
        }
}
int pc_gi_glow_soloed_id(uint32_t id) { return id && solo_id == id; }
void pc_gi_glow_solo_id(uint32_t id) { solo_id = solo_id == id ? 0 : id; }

pc_glow_override* pc_gi_glow_override(const float p[3], int create)
{
    return pc_gi_glow_override_id(pc_gi_glow_near(p, JOIN_RADIUS), create);
}
void pc_gi_glow_remove(const float p[3]) { pc_gi_glow_remove_id(pc_gi_glow_near(p, JOIN_RADIUS)); }
int pc_gi_emitter_soloed(const float p[3]) { return pc_gi_glow_soloed_id(pc_gi_glow_near(p, JOIN_RADIUS)); }
void pc_gi_emitter_solo(const float p[3]) { pc_gi_glow_solo_id(pc_gi_glow_near(p, JOIN_RADIUS)); }

void pc_gi_glows_reset(void)
{
    override_count = 0;
    solo_id = 0;
}
/* The editor's undo (editor_undo.cpp): the overrides as they are, and back. */
int pc_gi_glows_get(pc_glow_override* out, int max)
{
    int n = override_count < max ? override_count : max;
    memcpy(out, overrides, (size_t)n * sizeof *out);
    return n;
}
void pc_gi_glows_set(const pc_glow_override* in, int count)
{
    override_count = count < MAX_OVERRIDES ? count : MAX_OVERRIDES;
    memcpy(overrides, in, (size_t)override_count * sizeof *in);
}
int pc_gi_glows_parse(const char* line)
{
    pc_glow_override o;
    memset(&o, 0, sizeof o);
    if (sscanf(line, "glow %f %f %f %d %f %f %f %f %f %f", &o.position[0], &o.position[1], &o.position[2], &o.muted,
            &o.intensity, &o.tint[0], &o.tint[1], &o.tint[2], &o.size, &o.reach)
        != 10)
        return 0;
    const char* key = strstr(line, " key=");
    if (key)
        sscanf(key + 5, "%63s", o.key);
    const char* type = strstr(line, " fixture_type=");
    if (type)
        sscanf(type + 14, "%15s", o.fixture_type);
    const char* shape = strstr(line, " shape=");
    if (shape)
        o.shape = !strncmp(shape + 7, "sphere", 6) ? PC_GLOW_SHAPE_SPHERE
            : !strncmp(shape + 7, "column", 6)     ? PC_GLOW_SHAPE_COLUMN
                                                   : PC_GLOW_SHAPE_AUTO;
    if (override_count < MAX_OVERRIDES)
        overrides[override_count++] = o;
    return 1;
}
void pc_gi_glows_write(FILE* f)
{
    for (int i = 0; i < override_count; i++) {
        const pc_glow_override* o = &overrides[i];
        fprintf(f, "glow %.1f %.1f %.1f %d %.3f %.3f %.3f %.3f %.1f %.1f", o->position[0], o->position[1],
            o->position[2], o->muted, o->intensity, o->tint[0], o->tint[1], o->tint[2], o->size, o->reach);
        if (o->key[0])
            fprintf(f, " key=%s", o->key);
        if (o->fixture_type[0])
            fprintf(f, " fixture_type=%s", o->fixture_type);
        if (o->shape == PC_GLOW_SHAPE_SPHERE || o->shape == PC_GLOW_SHAPE_COLUMN)
            fprintf(f, " shape=%s", o->shape == PC_GLOW_SHAPE_SPHERE ? "sphere" : "column");
        fputc('\n', f);
    }
}

/* ---- the update ---- */
static pc_glow_fragment last_fragments[MAX_FRAGMENTS];
static uint32_t last_owner[MAX_FRAGMENTS];
static int last_fragment_count;
static unsigned last_vsync;
static double last_clock;
static int have_clock;

static double game_seconds(void)
{
    if (pc_vsync_count)
        return pc_vsync_count() / 59.94;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static int by_source(const void* a, const void* b)
{
    const pc_glow_fragment *x = a, *y = b;
    if (x->source != y->source)
        return x->source < y->source ? -1 : 1;
    return x->weight > y->weight ? -1 : x->weight < y->weight ? 1 : 0;
}
/* How far a source's triangles join one of its glows: particles (sparks and
 * embers come and go all over a fire) further than polygons. */
static float join_radius(uint32_t source)
{
    return (source >> 28) == PC_GLOW_ORIGIN_PARTICLES ? 192.0f : JOIN_RADIUS;
}
/* Sources spread over the room (a fire's embers drifting past the windows,
 * dozens at a time, each living a moment): once a source's triangles span
 * more than 384 units in a frame it is split by a fixed grid of 512-unit
 * columns (x, z), one glow per column it lights, kept for the visit (a few
 * seconds without embers before it goes): a handful of steady glows where
 * per-ember glows came and went every frame. Compact sources (a flame, a
 * torch carried along) keep one glow that follows them. */
#define CELL 512.0f
static uint32_t spread_sources[64];
static int spread_count;
static int spread(uint32_t source, const pc_glow_fragment* f, int first, int last)
{
    for (int i = 0; i < spread_count; i++)
        if (spread_sources[i] == source)
            return 1;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = first; i < last; i++)
        for (int k = 0; k < 3; k++)
            lo[k] = fminf(lo[k], f[i].p[k]), hi[k] = fmaxf(hi[k], f[i].p[k]);
    if (last - first < 2 || dist2(lo, hi) < 384.0f * 384.0f || spread_count == 64)
        return 0;
    spread_sources[spread_count++] = source;
    return 1;
}
/* Fragments of one non-room source [first, last) onto its tracks. */
static void associate(pc_glow_fragment* f, int first, int last, uint32_t* owner)
{
    uint32_t source = f[first].source;
    if (spread(source, f, first, last)) {
        for (int i = first; i < last; i++) {
            int cell[3];
            for (int k = 0; k < 3; k++)
                cell[k] = k == 1 ? 0 : (int)floorf(f[i].p[k] / CELL);
            track* t = NULL;
            for (int k = 0; k < track_count && !t; k++)
                if (tracks[k].source == source && tracks[k].in_cell && !memcmp(tracks[k].cell, cell, sizeof cell))
                    t = &tracks[k];
            if (!t) {
                float centre[3];
                for (int k = 0; k < 3; k++)
                    centre[k] = k == 1 ? f[i].p[1] : (cell[k] + 0.5f) * CELL;
                if (!(t = new_track(source, -1, centre)))
                    continue;
                memcpy(t->cell, cell, sizeof cell);
                t->in_cell = 1;
                memcpy(t->pos, f[i].p, sizeof t->pos); /* it starts where its first ember is */
                memcpy(t->obs_pos, f[i].p, sizeof t->obs_pos);
            }
            owner[i] = t->id;
        }
        return;
    }
    float radius = join_radius(source);
    for (int i = first; i < last; i++) {
        owner[i] = 0;
        float best_d = radius * radius;
        for (int k = 0; k < track_count; k++) {
            const track* t = &tracks[k];
            if (t->source != source)
                continue;
            float d = fminf(dist2(f[i].p, t->obs_pos), dist2(f[i].p, t->pos));
            if (d < best_d)
                best_d = d, owner[i] = t->id;
        }
    }
    /* The rest around seeds, brightest first (sorted so within a source). */
    for (int i = first; i < last; i++) {
        if (owner[i])
            continue;
        track* t = new_track(source, -1, f[i].p);
        if (!t)
            return;
        for (int j = i; j < last; j++)
            if (!owner[j] && dist2(f[j].p, f[i].p) < radius * radius)
                owner[j] = t->id;
    }
}
static void observe(const pc_glow_fragment* f, int count, const uint32_t* owner)
{
    static float sum_w[MAX_TRACKS], sum_p[MAX_TRACKS][3], sum_c[MAX_TRACKS][3], sum_a[MAX_TRACKS];
    static int tris[MAX_TRACKS], pieces[MAX_TRACKS];
    static float seeds[MAX_TRACKS][16][3];
    memset(pieces, 0, sizeof pieces);
    memset(sum_w, 0, sizeof sum_w);
    memset(sum_p, 0, sizeof sum_p);
    memset(sum_c, 0, sizeof sum_c);
    memset(sum_a, 0, sizeof sum_a);
    memset(tris, 0, sizeof tris);
    for (int i = 0; i < count; i++) {
        int k = 0;
        for (; k < track_count && tracks[k].id != owner[i]; k++)
            ;
        if (k == track_count)
            continue;
        float w = f[i].weight;
        sum_w[k] += w;
        sum_a[k] += f[i].area;
        tris[k]++;
        /* Its light: one full emitter per 96-unit piece it lights (as the
         * clustering this replaced gave, so a grid cell of embers or a wide
         * fire stays as bright as before); brightest first (sorted). */
        if (w > 1e-3f && pieces[k] < 16) {
            int p = 0;
            for (; p < pieces[k]; p++)
                if (dist2(f[i].p, seeds[k][p]) < 96.0f * 96.0f)
                    break;
            if (p == pieces[k])
                memcpy(seeds[k][pieces[k]++], f[i].p, sizeof seeds[k][0]);
        }
        for (int j = 0; j < 3; j++)
            sum_p[k][j] += f[i].p[j] * w, sum_c[k][j] += f[i].rgb[j] * w;
    }
    for (int k = 0; k < track_count; k++) {
        track* t = &tracks[k];
        if (t->origin == PC_GLOW_ORIGIN_BAKED)
            continue;
        t->observed = sum_w[k] > 1e-3f;
        t->triangles = tris[k];
        if (!t->observed)
            continue;
        for (int j = 0; j < 3; j++) {
            t->obs_rgb[j] = sum_c[k][j] / sum_w[k];
            /* room groups sit at their fixed centre */
            t->obs_pos[j] = t->group >= 0 ? groups[t->group].centre[j] : sum_p[k][j] / sum_w[k];
        }
        t->obs_size = t->group >= 0 ? groups[t->group].size : emitting_size(sum_a[k]);
        t->obs_power = sum_w[k];
        t->obs_strength = t->group >= 0 ? fmaxf(groups[t->group].strength, 1.0f) : (float)(pieces[k] ? pieces[k] : 1);
    }
}
/* An effect glow that stays (a fire's embers, a brazier's sprites: seen 80%
 * of 4 seconds or more) is a persistent glow from then on. Particles attached
 * to a character or an effect's matrix (attacks, spells) never are. */
static void promote(track* t)
{
    if (t->category != PC_GLOW_EFFECT || t->promoted || t->age < 4.0f || t->seen < 0.8f * t->age
        || (t->origin == PC_GLOW_ORIGIN_PARTICLES && (t->source & PC_GLOW_ATTACHED)))
        return;
    t->promoted = 1;
    classify(t);
}
static float category_tau(const track* t)
{
    float tau = pc_glow_category[t->category >= 0 && t->category < 3 ? t->category : PC_GLOW_EFFECT][4];
    return tau > 0 ? tau : 0;
}
static void smooth(float dt, int snap)
{
    for (int k = 0; k < track_count; k++) {
        track* t = &tracks[k];
        /* Exponential towards the observation; presence in as fast as the
         * smoothing, out slower (an attack's flash decays rather than
         * strobing; a flame's blinking sprites give a steady light). */
        float tau = category_tau(t);
        float a = snap || tau <= 0 ? 1.0f : 1.0f - expf(-dt / tau);
        float fade_in = snap ? 1.0f : 1.0f - expf(-dt / fmaxf(tau, 0.05f));
        float fade_out = snap ? 1.0f : 1.0f - expf(-dt / fmaxf(3.0f * tau, 0.3f));
        t->age += dt;
        t->seen += t->observed ? dt : 0;
        if (t->observed) {
            int first = !t->ever;
            if (first || dist2(t->obs_pos, t->pos) > SNAP_DISTANCE * SNAP_DISTANCE || a >= 1.0f) {
                memcpy(t->pos, t->obs_pos, sizeof t->pos);
                if (first || a >= 1.0f) {
                    memcpy(t->rgb, t->obs_rgb, sizeof t->rgb);
                    t->size = t->obs_size, t->power = t->obs_power, t->strength = t->obs_strength;
                }
            } else
                for (int j = 0; j < 3; j++)
                    t->pos[j] += a * (t->obs_pos[j] - t->pos[j]);
            if (!first && a < 1.0f) {
                for (int j = 0; j < 3; j++)
                    t->rgb[j] += a * (t->obs_rgb[j] - t->rgb[j]);
                t->size += a * (t->obs_size - t->size);
                t->power += a * (t->obs_power - t->power);
                t->strength += a * (t->obs_strength - t->strength);
            }
            if (first || t->age < 0.5f)
                classify(t);
            promote(t);
            t->ever = 1;
            t->absent = 0;
            t->presence += fade_in * (1.0f - t->presence);
        } else {
            t->absent += dt;
            t->presence -= fade_out * t->presence;
        }
        if (snap)
            t->presence = t->observed ? 1.0f : 0.0f;
    }
    /* Glows of other sources than the room's go after a second away. */
    int kept = 0;
    for (int k = 0; k < track_count; k++) {
        track* t = &tracks[k];
        int gone = t->origin != PC_GLOW_ORIGIN_ROOM && t->origin != PC_GLOW_ORIGIN_BAKED
                && t->absent > fmaxf(t->in_cell ? 5.0f : 1.0f, 4.0f * category_tau(t)) && t->presence < 0.02f;
        if (!gone)
            tracks[kept++] = *t;
    }
    track_count = kept;
}
/* Two glows of one source that came together are one (the younger goes). */
static void merge_duplicates(void)
{
    for (int a = 0; a < track_count; a++)
        for (int b = a + 1; b < track_count; b++) {
            track *x = &tracks[a], *y = &tracks[b];
            if (x->source != y->source || x->group >= 0 || x->in_cell || y->in_cell || !x->observed || !y->observed
                || dist2(x->obs_pos, y->obs_pos) > 0.25f * join_radius(x->source) * join_radius(x->source))
                continue;
            track* older = x->id < y->id ? x : y;
            track* younger = older == x ? y : x;
            float w = older->obs_power + younger->obs_power;
            for (int j = 0; j < 3; j++) {
                older->obs_pos[j] = (older->obs_pos[j] * older->obs_power + younger->obs_pos[j] * younger->obs_power) / w;
                older->obs_rgb[j] = (older->obs_rgb[j] * older->obs_power + younger->obs_rgb[j] * younger->obs_power) / w;
            }
            older->obs_power = w;
            older->triangles += younger->triangles;
            for (int i = 0; i < override_count; i++)
                if (overrides[i].bound == younger->id)
                    overrides[i].bound = older->id;
            if (solo_id == younger->id)
                solo_id = older->id;
            for (int i = 0; i < last_fragment_count; i++)
                if (last_owner[i] == younger->id)
                    last_owner[i] = older->id;
            int gone = (int)(younger - tracks);
            memmove(&tracks[gone], &tracks[gone + 1], (size_t)(track_count - gone - 1) * sizeof *tracks);
            track_count--; /* stable: tracks stay in id order */
            a = -1; /* start over */
            break;
        }
}
static float track_strength(const track* t)
{
    return t->origin == PC_GLOW_ORIGIN_BAKED || t->strength < 1 ? 1.0f : t->strength;
}
static int by_score_desc(const void* a, const void* b)
{
    float x = ((const float*)a)[0], y = ((const float*)b)[0];
    return x < y ? 1 : x > y ? -1 : 0;
}
int pc_glows_update(const pc_glow_fragment* fragments, int count, const float (*baked_pos)[4],
    const float (*baked_col)[4], int baked_count, float emitter_pos[][4], float emitter_col[][4],
    float emitter_shape[][4], float emitter_shadow[][4], int max)
{
    int snap = 0;
    if (pc_glow_shapes < 0) {
        const char* e = getenv("ROOD_GLOW_SHAPES");
        pc_glow_shapes = e && *e ? atoi(e) : 1;
    }
    if (pc_scene_serial() != groups_serial) {
        /* A new room: new groups, new glows; overrides find theirs again. */
        groups_serial = pc_scene_serial();
        build_groups();
        track_count = 0;
        spread_count = 0;
        for (int i = 0; i < override_count; i++)
            overrides[i].bound = 0;
        solo_id = 0;
        snap = 1;
        for (int g = 0; g < group_count; g++) {
            track* t = new_track(PC_GLOW_ORIGIN_ROOM << 28 | (groups[g].tag & 0x0fffffffu), g, groups[g].centre);
            if (t)
                t->size = t->obs_size = groups[g].size;
        }
    }
    double now = game_seconds();
    unsigned vsync = pc_vsync_count ? pc_vsync_count() : 0;
    int advance = !have_clock || snap || (pc_vsync_count ? vsync != last_vsync : now - last_clock > 1e-3);
    float dt = have_clock ? (float)(now - last_clock) : 0.0f;
    dt = dt < 0 ? 0 : dt > 0.5f ? 0.5f : dt;
    if (advance) {
        last_vsync = vsync, last_clock = now, have_clock = 1;
        /* Baked estimates: one glow each, as estimated. */
        for (int b = 0; b < baked_count; b++) {
            uint32_t source = PC_GLOW_ORIGIN_BAKED << 28 | (uint32_t)b;
            track* t = NULL;
            for (int k = 0; k < track_count && !t; k++)
                if (tracks[k].source == source)
                    t = &tracks[k];
            if (!t && !(t = new_track(source, b, baked_pos[b])))
                continue;
            memcpy(t->obs_pos, baked_pos[b], sizeof t->obs_pos);
            memcpy(t->obs_rgb, baked_col[b], sizeof t->obs_rgb);
            t->obs_size = baked_col[b][3];
            t->obs_power = 1e6f;
            t->observed = 1;
            t->out_reach = baked_pos[b][3];
        }
        int kept = 0; /* estimates no longer there (switched off) */
        for (int k = 0; k < track_count; k++)
            if (!(tracks[k].origin == PC_GLOW_ORIGIN_BAKED && tracks[k].group >= baked_count))
                tracks[kept++] = tracks[k];
        track_count = kept;
        /* This frame's fragments by source. */
        int n = count < MAX_FRAGMENTS ? count : MAX_FRAGMENTS;
        memcpy(last_fragments, fragments, (size_t)n * sizeof *fragments);
        last_fragment_count = n;
        qsort(last_fragments, (size_t)n, sizeof *last_fragments, by_source);
        for (int i = 0; i < n;) {
            const pc_glow_fragment* f = &last_fragments[i];
            int origin = (int)(f->source >> 28);
            if (origin == PC_GLOW_ORIGIN_ROOM) {
                unsigned index = f->source & 0x0fffffffu;
                int g = (int)index < triangle_group_size ? triangle_group[index] : -1;
                last_owner[i] = 0;
                for (int k = 0; k < track_count && g >= 0; k++)
                    if (tracks[k].group == g && tracks[k].origin == PC_GLOW_ORIGIN_ROOM)
                        last_owner[i] = tracks[k].id;
                i++;
                continue;
            }
            int j = i + 1;
            while (j < n && last_fragments[j].source == f->source)
                j++;
            int first = i, real = i;
            /* the weightless ones (black texels) belong to nothing */
            while (real < j && last_fragments[real].weight > 1e-3f)
                real++;
            if (real > first)
                associate(last_fragments, first, real, last_owner);
            for (int k = real; k < j; k++)
                last_owner[k] = 0;
            i = j;
        }
        observe(last_fragments, n, last_owner);
        merge_duplicates();
        smooth(dt, snap);
    }
    /* Overrides, linked actors and the solo, onto this update's values. */
    bind_overrides();
    int solo_alive = solo_id && track_by_id(solo_id);
    for (int k = 0; k < track_count; k++) {
        track* t = &tracks[k];
        t->muted = 0;
        t->overridden = 0;
        t->fixture_type[0] = 0;
        float scale = t->presence;
        int shape = PC_GLOW_SHAPE_AUTO;
        float core = pc_glow_shadow_core;
        float size_override = 0;
        memcpy(t->out_rgb, t->rgb, sizeof t->out_rgb);
        t->out_size = t->origin == PC_GLOW_ORIGIN_BAKED ? t->obs_size : t->size;
        if (t->origin != PC_GLOW_ORIGIN_BAKED)
            t->out_reach = pc_emitter_radius;
        for (int i = 0; i < override_count; i++) {
            pc_glow_override* o = &overrides[i];
            if (o->bound != t->id)
                continue;
            memcpy(o->position, t->pos, sizeof o->position);
            t->overridden = 1;
            t->muted = o->muted ? 1 : 0;
            /* A catalogued fixture's glow: its type's treatment (a cobweb, a
             * light shaft, a floor decal are not lights). */
            snprintf(t->fixture_type, sizeof t->fixture_type, "%s", o->fixture_type);
            if (!t->muted && o->fixture_type[0] && pc_fixture_glow_mode && pc_fixture_glow_mode(o->fixture_type) == 2)
                t->muted = 4;
            for (int j = 0; j < 3; j++)
                t->out_rgb[j] *= o->intensity * o->tint[j];
            if (o->size > 0 && t->out_size > 0)
                t->out_size = size_override = o->size;
            if (o->shape != PC_GLOW_SHAPE_AUTO)
                shape = o->shape;
            else if (o->fixture_type[0] && pc_fixture_glow_shape)
                shape = pc_fixture_glow_shape(o->fixture_type);
            if (o->fixture_type[0] && pc_fixture_glow_shadow_core && pc_fixture_glow_shadow_core(o->fixture_type) >= 0)
                core = pc_fixture_glow_shadow_core(o->fixture_type);
            if (o->reach > 0)
                t->out_reach = o->reach;
        }
        if (pc_lights_glow_drive && t->origin != PC_GLOW_ORIGIN_BAKED) {
            float drive = 1;
            int mode = pc_lights_glow_drive(t->key, t->pos, &drive);
            if (mode == 2 && !t->muted)
                t->muted = 1;
            else if (mode == 1)
                scale *= drive;
        }
        /* Its shape: a sphere at its centre, or the column of a group a
         * character can stand in (the type's or the override's choice
         * first; an override's size is the column's radius). */
        memcpy(t->out_centre, t->pos, sizeof t->out_centre);
        memset(t->out_shape, 0, sizeof t->out_shape);
        t->out_shadow_core = 0;
        const room_group* group = t->origin == PC_GLOW_ORIGIN_ROOM && t->group >= 0 && t->group < group_count
            ? &groups[t->group] : NULL;
        if (pc_glow_shapes > 0 && group && group->has_column
            && (shape == PC_GLOW_SHAPE_COLUMN || (shape == PC_GLOW_SHAPE_AUTO && group->encloses))) {
            memcpy(t->out_centre, group->column_centre, sizeof t->out_centre);
            memcpy(t->out_shape, group->column, sizeof t->out_shape);
            if (size_override > 0)
                t->out_shape[3] = copysignf(size_override, t->out_shape[3]);
            if (pc_glow_shapes == 2)
                t->out_shape[3] = -fabsf(t->out_shape[3]);
            t->out_shadow_core = fabsf(t->out_shape[3]) * (core < 0 ? 0 : core > 1 ? 1 : core);
        }
        /* Its category's switch and multipliers. */
        const float* c = pc_glow_category[t->category >= 0 && t->category < 3 ? t->category : PC_GLOW_EFFECT];
        if (t->origin != PC_GLOW_ORIGIN_BAKED) {
            scale *= c[1];
            t->out_reach *= c[2] > 0 ? c[2] : 0;
            if (c[0] < 0.5f && !t->muted)
                t->muted = 3;
        }
        for (int j = 0; j < 3; j++)
            t->out_rgb[j] *= scale;
        /* A column's share of the baked light around it (its weight in the
         * baked light's shadows, rt_trace.comp baked_visibility): the save
         * point's baked light is its own, however dark the scrolling texels
         * it was found by (a dim one only greyed the floor behind Ashley). */
        t->out_baked_weight = t->out_shape[3] > 0 && pc_glow_column_baked > 0 ? pc_glow_column_baked * scale : 0;
        if (solo_alive && t->id != solo_id && !t->muted)
            t->muted = 2;
    }
    /* The tracers' glows: the strongest (kept ones get a 1.5x edge, so two
     * of similar strength do not trade places), in id order. */
    static float rank[MAX_TRACKS][2];
    int candidates = 0;
    for (int k = 0; k < track_count; k++) {
        track* t = &tracks[k];
        int was = t->traced;
        t->traced = 0;
        if (t->muted || t->presence < 0.01f || luminance(t->out_rgb) < 1e-4f)
            continue;
        float score = (t->origin == PC_GLOW_ORIGIN_BAKED ? 1e9f : t->power * t->presence) * (was ? 1.5f : 1.0f);
        rank[candidates][0] = score, rank[candidates++][1] = (float)k;
    }
    qsort(rank, (size_t)candidates, sizeof rank[0], by_score_desc);
    for (int i = 0; i < candidates && i < max; i++)
        tracks[(int)rank[i][1]].traced = 1;
    /* At most `max` emitters' worth of light in all, as before (a hall of
     * lit windows: dozens of pieces). */
    float pieces = 0;
    for (int k = 0; k < track_count; k++)
        pieces += tracks[k].traced ? track_strength(&tracks[k]) : 0;
    float cap = pieces > (float)max ? (float)max / pieces : 1.0f;
    int out = 0;
    float energy = 0, raw = 0;
    pc_glow_casts = 0;
    for (int c = 0; c < 3; c++)
        pc_glow_category_energy[c] = 0, pc_glow_category_count[c] = 0;
    for (int k = 0; k < track_count && out < max; k++) {
        const track* t = &tracks[k];
        if (!t->traced)
            continue;
        /* Shadows: fixtures and magic glows as their category says (with
         * the global glow_shadows on); transient effects never (their
         * shadows jumped all over the place); baked estimates always. */
        if (t->origin == PC_GLOW_ORIGIN_BAKED
            || (t->category != PC_GLOW_EFFECT && pc_glow_category[t->category][3] > 0.5f))
            pc_glow_casts |= 1u << out;
        memcpy(emitter_pos[out], t->out_centre, 3 * sizeof(float));
        emitter_pos[out][3] = t->out_reach;
        memcpy(emitter_shape[out], t->out_shape, sizeof t->out_shape);
        emitter_shadow[out][0] = t->out_shadow_core, emitter_shadow[out][1] = t->out_baked_weight;
        emitter_shadow[out][2] = t->out_shape[3] > 0
            ? fmaxf(t->out_shadow_core, t->out_shape[3] * (pc_glow_shadow_clear < 0 ? 0 : pc_glow_shadow_clear > 1 ? 1 : pc_glow_shadow_clear))
            : 0;
        emitter_shadow[out][3] = 0;
        float strength = track_strength(t) * cap;
        for (int j = 0; j < 3; j++)
            emitter_col[out][j] = t->out_rgb[j] * strength;
        emitter_col[out][3] = t->out_size;
        energy += luminance(emitter_col[out]);
        if (t->category >= 0 && t->category < 3) {
            pc_glow_category_energy[t->category] += luminance(emitter_col[out]);
            pc_glow_category_count[t->category]++;
        }
        if (t->observed && t->presence > 0) {
            float r[3];
            for (int j = 0; j < 3; j++)
                r[j] = t->rgb[j] > 1e-6f ? emitter_col[out][j] / t->presence * t->obs_rgb[j] / t->rgb[j] : 0;
            raw += luminance(r);
        }
        out++;
    }
    pc_glow_energy = energy, pc_glow_raw_energy = raw;
    static int reported = -1, reported_tracks = -1;
    if (out != reported || track_count != reported_tracks) {
        fprintf(stderr, "[gi] %d glows (%d traced) from %d glowing triangles\n", track_count, out, count);
        reported = out, reported_tracks = track_count;
    }
    return out;
}

/* ---- listing ---- */
static void fill_info(const track* t, pc_glow_info* g)
{
    memset(g, 0, sizeof *g);
    g->id = t->id;
    snprintf(g->key, sizeof g->key, "%s", t->key);
    /* "#n" among glows of the same kind */
    int same = 0, index = 0;
    for (int k = 0; k < track_count; k++)
        if (!strcmp(tracks[k].kind, t->kind)) {
            same++;
            index += tracks[k].id <= t->id;
        }
    if (same > 1)
        snprintf(g->name, sizeof g->name, "%s #%d", t->kind[0] ? t->kind : "Glow", index);
    else
        snprintf(g->name, sizeof g->name, "%s", t->kind[0] ? t->kind : "Glow");
    g->origin = t->origin;
    memcpy(g->position, t->pos, 3 * sizeof(float));
    g->position[3] = t->out_reach;
    memcpy(g->color, t->out_rgb, 3 * sizeof(float));
    g->color[3] = t->origin == PC_GLOW_ORIGIN_BAKED ? -fabsf(t->out_size) : t->out_size;
    memcpy(g->raw_position, t->obs_pos, sizeof g->raw_position);
    memcpy(g->raw_color, t->obs_rgb, sizeof g->raw_color);
    g->strength = track_strength(t);
    g->category = t->category;
    g->casts_shadows = t->origin == PC_GLOW_ORIGIN_BAKED
        || (t->category != PC_GLOW_EFFECT && pc_glow_category[t->category][3] > 0.5f);
    g->presence = t->presence;
    g->observed = t->observed;
    g->triangles = t->triangles;
    g->muted = t->muted;
    g->overridden = t->overridden;
    g->traced = t->traced;
    snprintf(g->fixture_type, sizeof g->fixture_type, "%s", t->fixture_type);
    memcpy(g->shape_centre, t->out_centre, sizeof g->shape_centre);
    memcpy(g->shape, t->out_shape, sizeof g->shape);
    g->shadow_core = t->out_shadow_core;
}
int pc_gi_glow_list(pc_glow_info* out, int max)
{
    int n = 0;
    for (int k = 0; k < track_count && n < max; k++)
        if (tracks[k].ever || tracks[k].origin == PC_GLOW_ORIGIN_BAKED)
            fill_info(&tracks[k], &out[n++]);
    return n;
}
int pc_gi_glow_get(uint32_t id, pc_glow_info* out)
{
    const track* t = track_by_id(id);
    if (!t)
        return 0;
    fill_info(t, out);
    return 1;
}
/* Glows seen this visit (lit ones first), as positions and colours. */
int pc_gi_emitters(float position[][4], float color[][4], int muted[], int max)
{
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < track_count && n < max; k++) {
            const track* t = &tracks[k];
            if ((!t->ever && t->origin != PC_GLOW_ORIGIN_BAKED) || t->category == PC_GLOW_EFFECT)
                continue;
            int lit = t->presence >= 0.02f;
            if (lit != (pass == 0))
                continue;
            memcpy(position[n], t->pos, 3 * sizeof(float));
            position[n][3] = t->out_reach;
            memcpy(color[n], t->out_rgb, 3 * sizeof(float));
            color[n][3] = t->origin == PC_GLOW_ORIGIN_BAKED ? -fabsf(t->out_size) : t->out_size;
            muted[n++] = t->muted;
        }
    return n;
}
int pc_glows_fragments_json(char* out, int size)
{
    int used = snprintf(out, (size_t)size, "[");
    for (int i = 0; i < last_fragment_count && used < size - 200; i++) {
        const pc_glow_fragment* f = &last_fragments[i];
        used += snprintf(out + used, (size_t)(size - used),
            "%s{\"p\":[%.1f,%.1f,%.1f],\"w\":%.4g,\"area\":%.1f,\"rgb\":[%.3f,%.3f,%.3f],\"source\":\"%08x\",\"glow\":%u}",
            i ? "," : "", f->p[0], f->p[1], f->p[2], f->weight, f->area, f->rgb[0], f->rgb[1], f->rgb[2], f->source,
            last_owner[i]);
    }
    used += snprintf(out + used, (size_t)(size - used), "]");
    return used;
}
