/* The game's glows as light sources (glows.c; design in pc/docs/Glows.md).
 *
 * Glowing triangles (additive room polygons, particles, object polygons)
 * are grouped by what emitted them: a room polygon group (found once per
 * room capture), a particle system (effect_native.c's tag), an object's
 * mesh. Each source becomes one glow with a stable id for the room visit
 * and a persistent key for room files; its position, colour and size are
 * smoothed over time (per category, pc_glow_category), so sprites do not make
 * the light jump. Editor overrides (enabled, intensity, tint, size, reach),
 * mute/solo and light-actor links are keyed by that identity and saved per
 * room with its lights ("glow <key>" items of the lighting layers,
 * lighting/README.md). */
#ifndef ROOD_GLOW_OVERRIDES_H
#define ROOD_GLOW_OVERRIDES_H
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Where a glow comes from (top four bits of a source tag). */
enum {
    PC_GLOW_ORIGIN_ROOM = 1, /* additive room polygons (lamps, the save point) */
    PC_GLOW_ORIGIN_OBJECT = 2, /* additive polygons of a character or prop mesh */
    PC_GLOW_ORIGIN_PARTICLES = 3, /* a particle system (flames, sparks) */
    PC_GLOW_ORIGIN_PACKET = 4, /* effects drawn from packets (by texture) */
    PC_GLOW_ORIGIN_BAKED = 5, /* a light estimated from the baked vertex colours */
};

/* What a glow is (pc_glow_category): a persistent room light source
 * (candles, torches, lanterns, braziers, flames in room polygons), a
 * persistent magic glow (the save point), or a transient effect (attacks,
 * spells, hit sparks, particles: anything short-lived or owned by a battle
 * or effect system), which never casts shadows and is only controlled as a
 * category. */
enum { PC_GLOW_FIXTURE = 0, PC_GLOW_MAGIC = 1, PC_GLOW_EFFECT = 2 };
/* Source tag bit of particles attached to a character's or an effect's
 * matrix (effect_particles.c): always transient. */
#define PC_GLOW_ATTACHED 0x08000000u
/* Source tag bit of object glows from emissive texels (emissive.cpp: a
 * glowing crate, a floating block's circle): extra light the PS1's baked
 * vertex light never came from, so it never shadows that light (its
 * weight in the baked light's shadows is 0, emitter_shadow y < 0). Other
 * object glows (gpu_hw.c) keep it and the side bits clear. */
#define PC_GLOW_EMISSIVE 0x04000000u
/* ... and the side its surface faces (bits 23-25): 0 none (double sided),
 * 1 + 2 axis + (negative): +x, -x, +y, -y, +z, -z. glows.c gives such a
 * glow a disc facing that way, the size of its surface; the mesh's hash
 * is below them. */
#define PC_GLOW_SIDE_SHIFT 23
#define PC_GLOW_SIDE(source) (((source) & PC_GLOW_EMISSIVE) ? ((source) >> PC_GLOW_SIDE_SHIFT) & 7u : 0u)
/* An emissive object glow's surface (glows.c, emissive.cpp; room settings
 * glow_emissive_lift, glow_emissive_disc): how far in front of its surface
 * it sits, and its disc's radius as a fraction of the radius of a disc of
 * its area. */
extern float pc_glow_emissive_lift, pc_glow_emissive_disc;

/* A glow's emitting shape (pc_glow_override.shape, the fixture catalog's
 * glow_shape): auto (a column when its polygons enclose room for a
 * character, glows.c fit_column; else a sphere), always a sphere, a column
 * whenever one was fitted. */
enum { PC_GLOW_SHAPE_AUTO = 0, PC_GLOW_SHAPE_SPHERE = 1, PC_GLOW_SHAPE_COLUMN = 2 };

#define PC_GLOW_KEY 64
typedef struct {
    float position[3]; /* where the glow was (follows it; matches old lines) */
    int muted;
    float intensity; /* scales the glow's light (1: as found) */
    float tint[3]; /* multiplies its colour */
    float size; /* emitting sphere radius; 0 keeps the found size */
    float reach; /* falloff radius; 0 keeps the default (Glow reach) */
    char key[PC_GLOW_KEY]; /* the glow's persistent key ("" in old room files: by position) */
    uint32_t bound; /* the glow id it applies to this visit (0: not found yet) */
    /* The fixture type the glow belongs to (generated: "T061"; ""): the type's
     * glow treatment applies (pc/assets/fixtures/catalog.json: a cobweb's,
     * a light shaft's glow is suppressed), unless muted says otherwise. */
    char fixture_type[16];
    int shape; /* PC_GLOW_SHAPE_*: auto keeps the type's (else found) shape; " shape=sphere|column" */
} pc_glow_override;

/* One glow (pc_gi_glow_list). */
typedef struct {
    uint32_t id; /* stable while the room stays loaded */
    char key[PC_GLOW_KEY]; /* persistent: origin, source tag, home position */
    char name[48]; /* "Save point glow", "Flame (candle) #2", ... */
    int origin; /* PC_GLOW_ORIGIN_* */
    float position[4]; /* smoothed xyz; reach */
    float color[4]; /* smoothed rgb with overrides and presence applied; emitting radius (< 0: baked) */
    float raw_position[3]; /* this frame's observation (unsmoothed) */
    float raw_color[3];
    float presence; /* 0..1: fades in when the source appears, out when it goes */
    float strength; /* the tracers get color * strength (a room glow spanning several 96-unit pieces) */
    int observed; /* the source drew glowing triangles this frame */
    int triangles; /* how many */
    int muted; /* 1 muted, 2 left out by a solo, 3 its category is off, 4 its fixture type suppresses it */
    int overridden; /* has an override */
    int traced; /* among the emitters the tracers use this frame */
    int category; /* PC_GLOW_FIXTURE / _MAGIC / _EFFECT */
    int casts_shadows; /* its light may be shadowed (never for effects) */
    char fixture_type[16]; /* its fixture type (from the room's lighting layers), "" */
    float shape_centre[3]; /* where the tracers sample it: its centre, or its column's */
    float shape[4]; /* its column: half axis, radius (> 0 its side, < 0 solid); all 0: a sphere (color[3]) */
    float shadow_core; /* its column's shadow core radius */
} pc_glow_info;

/* Every glow of the room visit, in id order (up to max). */
int pc_gi_glow_list(pc_glow_info* out, int max);
/* The glow with this id (0 when gone). */
int pc_gi_glow_get(uint32_t id, pc_glow_info* out);
/* The glow nearest p within `radius` (0: none). */
uint32_t pc_gi_glow_near(const float p[3], float radius);
/* The glow whose persistent key is `key` (exact; else the same origin and
 * source nearest the key's home within 96 units); 0 when none. */
uint32_t pc_gi_glow_by_key(const char* key);
/* The override of glow `id`, created on demand (or NULL). */
pc_glow_override* pc_gi_glow_override_id(uint32_t id, int create);
void pc_gi_glow_remove_id(uint32_t id);
int pc_gi_glow_soloed_id(uint32_t id);
void pc_gi_glow_solo_id(uint32_t id); /* toggles */

/* Position-based forms (older callers): the glow nearest p. */
int pc_gi_emitters(float position[][4], float color[][4], int muted[], int max);
pc_glow_override* pc_gi_glow_override(const float p[3], int create);
void pc_gi_glow_remove(const float p[3]);
int pc_gi_emitter_soloed(const float p[3]);
void pc_gi_emitter_solo(const float p[3]);

void pc_gi_glows_reset(void);
/* "glow x y z muted intensity r g b size reach [key=KEY] [fixture_type=T]"
 * (room files; the key is new, lines without one match the glow nearest
 * their position). */
int pc_gi_glows_parse(const char* line);
void pc_gi_glows_write(FILE* f);
/* Every override as it is (the editor's undo), and replacing them all. */
int pc_gi_glows_get(pc_glow_override* out, int max);
void pc_gi_glows_set(const pc_glow_override* in, int count);

/* A fixture type's glow treatment (fixture_catalog.cpp): 0 leave, 1 drive,
 * 2 suppress, -1 unknown type. */
int pc_fixture_glow_mode(const char* type);
/* Its glow shape (PC_GLOW_SHAPE_*; auto for an unknown type). */
int pc_fixture_glow_shape(const char* type);
/* Its column's shadow core (a fraction of its radius; < 0: not set). */
float pc_fixture_glow_shadow_core(const char* type);

/* Per category: enabled, intensity multiplier, reach multiplier, casts
 * shadows (forced off for effects), smoothing seconds (room settings
 * glow_fixtures_* / glow_magic_* / glow_effects_*). */
extern float pc_glow_category[3][5];
/* Light energy this update (sum of the traced emitters' luminance), what the
 * same glows' unsmoothed observations give, and per category (RPC glows). */
extern float pc_glow_energy, pc_glow_raw_energy, pc_glow_category_energy[3];
extern int pc_glow_category_count[3];
/* The traced emitters that may cast shadows (bit i: emitter i), for the
 * tracers' glow shadows, baked-light shadows, haze and lighting signature. */
extern uint32_t pc_glow_casts;

/* ---- ptgi.c's side ---- */
typedef struct {
    float p[3], area, rgb[3], weight;
    uint32_t source; /* origin << 28 | tag; room: the room triangle index */
    float reach; /* its glow's reach (an emissive entry's light_reach; 0: the room's glow_reach) */
} pc_glow_fragment;
/* This frame's glowing triangles (room ones with source ROOM << 28 | its
 * index in pc_scene_triangle_data) and the baked estimates (xyz reach,
 * rgb -size); fills the tracers' emitters (overrides, mutes, solo applied;
 * at most max). Returns their count. */
int pc_glows_update(const pc_glow_fragment* fragments, int count, const float (*baked_pos)[4],
    const float (*baked_col)[4], int baked_count, float emitter_pos[][4], float emitter_col[][4],
    float emitter_shape[][4], float emitter_shadow[][4], int max);
/* Enclosing glows as columns: 0 never, 1 their glowing side, 2 solid
 * (glows.c; ROOD_GLOW_SHAPES, RPC glows {shapes}). */
extern int pc_glow_shapes;
/* Its shadows aim at a core this fraction of the column's radius (glows.c). */
extern float pc_glow_shadow_core;
/* ... and stop where they enter its clear middle (this fraction of its radius). */
extern float pc_glow_shadow_clear;
/* A column's luminance in the baked light's shadow weights (0: its colour's). */
extern float pc_glow_column_baked;
/* RPC: the last update's fragments, as JSON (debugging). */
int pc_glows_fragments_json(char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
