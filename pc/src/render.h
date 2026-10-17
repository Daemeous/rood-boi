/* Native 3D submission (phase 2 of pc/docs/Modernization_Plan.md; design
 * notes in pc/docs/Native_Rendering.md).
 *
 * Game-side code registers immutable meshes once (PS1 polygons: positions,
 * normals, UVs, baked vertex colours, texture page/CLUT, GPU code and
 * per-polygon flags) and submits instances of them each frame into a draw
 * list with a world transform. A list carries the camera (the game's GTE view
 * matrix and projection) and the PS1 rules its polygons follow (ordering-table
 * near/far range, distance UV halving). The list is drawn where its marker
 * packet sits in the frame's ordering table, so it keeps its place among the
 * packets the game still builds (2D, effects, drop shadows): it goes
 * into the same scaled VRAM target, G-buffer and depth buffer as the rest of
 * the frame (gpu_hw.c), depth-tested with exact per-pixel depth, and packets
 * drawn after it are hidden where the native geometry is in front of them.
 *
 * Everything here runs on the game thread (the one that owns the GL context).
 * Without the hardware rasterizer (ROOD_SCALE=0, software presentation) the
 * API reports itself unavailable and callers keep their packet paths. */
#ifndef ROOD_RENDER_H
#define ROOD_RENDER_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- meshes ---- */

enum {
    RENDER_POLY_QUAD = 1, /* four vertices (the PS1's 0,1,2 + 1,3,2 triangles) */
    RENDER_POLY_DOUBLE_SIDED = 2, /* never back-face culled */
    /* Object rules only: a non-planar quad split along the diagonal the PS1
     * picks from its projected corners (weapon_packets.c split_quad), each
     * triangle culled and sorted on its own. */
    RENDER_POLY_SPLIT = 4,
    /* A screen-aligned sprite (effect particles: the PS1 projects a centre
     * and places the corners in screen space around it): position[0] is the
     * centre, `offset` the corners' screen offsets in pixels when the centre
     * was at depth `offset_depth` (they scale with offset_depth / depth, as
     * the game's sprites do). Drawn facing the camera at the centre's depth. */
    RENDER_POLY_SPRITE = 8,
};
#define RENDER_MAX_BONES 64
typedef struct {
    float position[4][3]; /* model (bone) space; [3] unused for triangles */
    float normal[3]; /* model space; zero: computed from the positions */
    uint8_t uv[4][2];
    uint8_t rgb[4][3]; /* baked PS1 vertex colour (128 = texel as is) */
    uint16_t tpage, clut; /* GPU texture page (blend mode bits 5-6, depth 7-8) and CLUT */
    uint8_t code; /* GPU polygon command: bit 1 semi-transparent, bit 2 textured, bit 0 raw */
    uint8_t flags; /* RENDER_POLY_* */
    uint8_t bone[4]; /* palette entry per vertex (0 for single-transform meshes) */
    uint8_t bias; /* ordering-table bias in buckets (object rules; 0-15) */
    int16_t offset[4][2]; /* RENDER_POLY_SPRITE: corner offsets (pixels) */
    uint16_t offset_depth; /* RENDER_POLY_SPRITE: the depth (SZ) they were measured at */
    /* An animated material (render_list_uv_effects): 0 none, else the entry
     * of the list's UV effect table whose offset the vertices' UVs take
     * (uv = (uv + offset) & 255 per vertex, as the game writes them). */
    uint8_t uv_effect;
    /* What emitted it, for glows (glows.c): a particle system's tag
     * (effect_native.c), 0 for none (the instance's mesh stands for it). */
    uint32_t source;
} render_polygon;
/* A mesh handle (0 = none). Handles carry a generation: a destroyed mesh's
 * handle stays invalid even after its slot is reused. */
typedef uint32_t render_mesh;
render_mesh render_mesh_create(const render_polygon* polygons, int count);
/* A mesh for this frame only (effects rebuilt every frame): destroyed by the
 * renderer once the frames that may replay it are gone. */
render_mesh render_mesh_transient(const render_polygon* polygons, int count);
/* Replace a live mesh's polygons (same count; the handle stays valid):
 * returns 0 when the handle is stale or the count differs. */
int render_mesh_update(render_mesh mesh, const render_polygon* polygons, int count);
void render_mesh_destroy(render_mesh mesh);
int render_mesh_polygons(render_mesh mesh); /* -1 when the handle is stale */
/* Emissive texels limited to some surfaces (emissive.cpp, pc/docs/Emissive.md
 * Scope): per polygon, the scope bits of the entries it may glow with
 * (bits[i] for polygon i; NULL: none). Kept across render_mesh_update. */
int render_mesh_emissive_scope(render_mesh mesh, const uint16_t* bits);

/* ---- cameras and lists ---- */

typedef struct {
    /* world -> GTE view space: view = rotation * world + translation. The
     * rows keep the GTE matrix's scales (X carries the pixel aspect). */
    float rotation[3][3], translation[3];
    float h, ofx, ofy; /* GTE H and screen offset (pixels) */
    int widescreen; /* 16:9 Hor+: H x * 3/4 (ofx: pc_gte_view_ofx, narrowed about the screen centre) */
} render_camera;
/* The PS1 rules room polygons follow (BATTLE/2EA3C.s): a polygon is kept when
 * its sort bucket (largest vertex depth >> 2, plus the instance's bias) lies
 * in [near_bucket, far_bucket); beyond lod_bucket its UVs are halved and it
 * samples the page before its own (the game's half-size copies), unless the
 * PS1 would subdivide it (subdivision enabled, large on screen and within the
 * instance's subdivision range). */
typedef struct {
    int near_bucket, far_bucket, lod_bucket;
    int subdivision; /* the room's subdivision switch (func_8009723C mode bit 0) */
    /* Object rules (characters, weapons, props: BATTLE/4B16C.s): 0 for the
     * room. A polygon is culled when its first triangle's NCLIP is <= 0 (no
     * quad fallback), sorted by its average depth (quads (SZ0..3) >> 4,
     * triangles AVSZ3 with `zsf3`) plus its own bias and kept below
     * far_bucket; no UV halving or subdivision. The list does not reset
     * depth: it is depth-tested against the native surfaces drawn before it
     * in the frame (the room, other objects) and becomes part of them, and
     * its G-buffer marks moving geometry. */
    int objects;
    int zsf3; /* GTE ZSF3 (AVSZ3 scale, 4.12) */
    /* Room rules drawn over the native surfaces so far (another room block,
     * func_8008E19C's): no depth reset; depth-tested against the room and the
     * objects before it and added to them, as object lists are. */
    int layered;
    /* An effects list (RENDER_EFFECTS_*): translucent entries only (every
     * polygon of it, semi-transparent or not, is drawn at translucent
     * markers; the list itself has no marker). Its markers draw once the
     * room list of the ordering table drew, hidden behind the native
     * surfaces before them with the slack the packet path gives decals
     * (sprites: behind the nearest point of the sprite), opaque texels
     * unlit (G-buffer normal alpha 0) with their depth. */
    int effects;
} render_rules;
enum {
    RENDER_EFFECTS_SHADOWS = 1, /* drop shadows (actor_shadow.c), platforms' squares (room_shadows.inc) */
    RENDER_EFFECTS_SPRITES = 2, /* particles and effect sprites */
};
typedef struct {
    float rotation[3][3], translation[3];
} render_transform;
enum {
    RENDER_OVERRIDE_COLOR = 1, /* every vertex takes `color` (flat-shaded models) */
    /* texture page `tpage` | the polygon's tpage bits, CLUT `clut` + the
     * polygon's clut (16-bit: a material group's alternate CLUT offset) */
    RENDER_OVERRIDE_TEXTURE = 2,
};
/* render_instance.blend: the whole instance semi-transparent (the blended
 * model builders): every polygon becomes textured, modulated and
 * semi-transparent, the PS1 blend mode bits (0 B/2+F/2, 1 B+F, 2 B-F,
 * 3 B+F/4) replacing the polygon's own (the override page is still ORed
 * in). 0: polygons keep their own code. */
#define RENDER_BLEND(mode) (1 + ((mode) & 3))
typedef struct {
    render_mesh mesh;
    float rotation[3][3], translation[3]; /* model -> world (no palette) */
    int bias; /* ordering-table bias (buckets) */
    int subdivision_level; /* 0-3 */
    /* Bone palette (model -> world per bone, copied by render_list_add):
     * when `bones` > 0 it replaces rotation/translation. `previous` is the
     * same model's palette in the frame before (same count), or NULL: frame
     * interpolation blends between them. */
    const render_transform* palette;
    const render_transform* previous;
    int bones;
    int override; /* RENDER_OVERRIDE_* */
    uint8_t color[3];
    uint16_t tpage, clut;
    int blend; /* RENDER_BLEND(mode) or 0 */
    /* Diagnostics only (RPC anim_trace): the game object the instance
     * belongs to (e.g. the actor's address), 0 when none. */
    uint32_t owner;
    /* Frame interpolation over several game frames (a pose the game holds
     * for some frames between changes): the palette is drawn at blend
     * (blend_offset + t) * blend_scale between `previous` and `palette`
     * instead of t (0 scale: 1, the plain frame-to-frame blend). The live
     * frame (t = 1) is then drawn partway too. */
    float blend_offset, blend_scale;
    /* A share of the instance's pixels left out by an ordered dither (room
     * groups under gameplay_walls smart: fades and ghosts,
     * gameplay_walls.c): 0 solid. `cut_previous` is the share in the frame
     * before (frame interpolation blends the two); `cut_ghost` keeps the
     * polygons' edges and marks the pixels drawn as a ghost (G-buffer). */
    float cut, cut_previous;
    int cut_ghost;
} render_instance;
/* Start a list for this frame; returns its id (nonzero), 0 when native
 * drawing is unavailable. The camera and rules are copied. */
uint32_t render_list_begin(const render_camera* camera, const render_rules* rules);
/* The camera list `id` was begun with (the camera its frame is drawn with);
 * 0 when the list is gone. */
int render_list_camera(uint32_t list, render_camera* out);
void render_list_add(uint32_t list, const render_instance* instance);
/* Vertex colour adjustment for the whole list (func_80098D6C on the room
 * packets): fade toward each colour's luminance by fade/4096, then add the
 * signed offsets, clamped to 0..255. */
void render_list_tint(uint32_t list, int dr, int dg, int db, int fade);
/* Object lists: the polygons of the list's instances in the order the PS1
 * draws them this frame (its ordering-table order; entries are instance
 * index << 16 | polygon index; copied, replacing an earlier order). The list
 * then paints in that order, as the PS1 does, and is depth-tested only
 * against the surfaces drawn before it (room, other models): models built
 * from intersecting shells (hair over a scalp, a coat over a body), and
 * layered copies of one model, look as they do on the PS1 and stay exact
 * against the world. Without an order the instances depth-test themselves. */
void render_list_order(uint32_t list, const uint32_t* order, int count);
/* Animated materials (room texture effects, room_effects.c): the UV
 * offsets the game's effects apply this frame, entry i for polygons with
 * uv_effect i + 1. Each polygon's vertex UVs become (uv + offset) & 255 (the
 * bytes the game writes, so the frame matches the packet path exactly).
 * Frame interpolation draws the offset at t as offset - (1 - t) * shift
 * (`shift`: the unwrapped motion since the previous game frame; 0 snaps),
 * texels past the effect's window (`base` + `size` texels on each axis,
 * size 0: none) wrapping back by `period` texels, as the game's scroll
 * wraps. Smooth scrolls (pc_interp_uv 2) draw offset + lead - (1 - t) *
 * smooth instead: `lead` is how far the effect's tick counter is towards
 * its next step (in texels), `smooth` the motion of offset + lead since the
 * previous game frame, so an effect stepping every few game frames moves
 * at a constant speed. Copied; replaces an earlier table. */
#define RENDER_MAX_UV_EFFECTS 64
typedef struct {
    int16_t offset[2];
    float shift[2];
    float lead[2], smooth[2];
    uint8_t base[2];
    uint16_t size[2], period[2];
} render_uv_effect;
void render_list_uv_effects(uint32_t list, const render_uv_effect* effects, int count);
/* Animated materials under frame interpolation: 0 the UV offsets step with
 * the game's frames; 1 (default) drawn partway between game frames, every
 * game frame exact; 2 smooth: a constant speed through the frames an
 * effect holds between steps (game frames are then exact only where the
 * effect steps; off without frame interpolation). */
extern int pc_interp_uv;
/* Whether list `id` uses the object rules (1), the room's (0), is a layered
 * room block (2), an effects list (3), or is gone (-1). */
int render_list_objects(uint32_t list);
/* An effects list's kind (RENDER_EFFECTS_*), 0 for other lists, -1 when gone. */
int render_list_effects(uint32_t list);
/* Every polygon of the list's instances (both faces, no culling) as world
 * space triangles with their surface: shadow casters for the path tracer.
 * Returns the number of triangles passed to `emit`. */
typedef void (*render_triangle_sink)(void* context, const float world[9], const uint8_t rgb[9], const uint8_t uv[6],
    unsigned code, unsigned tpage, unsigned clut);
int render_list_triangles(uint32_t list, render_triangle_sink emit, void* context);
/* ... only the instances whose mesh `keep` accepts (it gets the same context;
 * the others cost nothing: not transformed). */
int render_list_triangles_where(uint32_t list, int (*keep)(void* context, uint32_t mesh), render_triangle_sink emit,
    void* context);
/* Finished frames so far (game-side code keys "the previous frame" on it). */
uint32_t render_frame_serial(void);
/* Link the list into a PS1 ordering table: writes a three-word marker
 * packet (a GPU no-op on any other path) at `packet` and links it at the
 * head of `*ot_entry`. Returns the word after the packet. */
uint32_t* render_list_link(uint32_t list, uint32_t* ot_entry, uint32_t* packet);

/* ---- translucent polygons ----
 * Semi-transparent polygons (the polygon's code bit 1, or an instance's
 * blend) are never drawn with the list. Each one is drawn where the PS1
 * would draw it: the list keeps a sequence of polygon entries (instance
 * index << 16 | polygon index) and translucent markers in the ordering table
 * draw ranges of it, in sequence order, with the PS1's semi-transparency:
 * texels with the STP bit (all pixels when untextured) blend with the blend
 * mode of their texture page and write neither depth nor the G-buffer;
 * the polygon's opaque texels draw as opaque ones do. Translucent pixels
 * are hidden behind the native surfaces drawn before them (room, objects),
 * not behind each other. A list's translucent markers draw only when the
 * list itself drew earlier in the same ordering table, so a list's own
 * marker must come first (link it at a bucket no nearer than any of its
 * translucent markers'). */
enum {
    /* The game decided this polygon is drawn (the room's back-face and
     * ordering-table tests): no culling here. */
    RENDER_TRANSLUCENT_DECIDED = 1,
    /* With DECIDED: the room's distance UV halving applies. */
    RENDER_TRANSLUCENT_HALVE = 2,
};
/* Append entries (with RENDER_TRANSLUCENT_* flags each, or NULL for none)
 * to the list's translucent sequence; returns the index of the first, -1
 * when the list is gone or out of memory. */
int render_list_translucent(uint32_t list, const uint32_t* entries, const uint8_t* flags, int count);
/* Entries in the translucent sequence so far (-1 when the list is gone). */
int render_list_translucent_count(uint32_t list);
/* Drop the sequence (markers already linked then draw nothing). */
void render_list_translucent_clear(uint32_t list);
/* Link a translucent marker (five words) that draws entries
 * [first, first + count) of the sequence; returns the word after it. */
uint32_t* render_list_link_translucent(uint32_t list, int first, int count, uint32_t* ot_entry, uint32_t* packet);
/* The list's semi-transparent polygons (as render_list_triangles, with the
 * semi-transparency bit in `code` and the blend mode in `tpage`): glowing
 * surfaces for the path tracer. */
int render_list_translucent_triangles(uint32_t list, render_triangle_sink emit, void* context);
/* Inside a render_triangle_sink: the polygon's source tag (render_polygon
 * .source) and its instance's mesh handle (glow identity, glows.c). */
void render_emit_tag(uint32_t* source, uint32_t* mesh);
/* Inside a render_triangle_sink: the polygon the triangle belongs to (its
 * four UVs: the texture's UV box, emissive_editor.cpp). */
const render_polygon* render_emit_polygon(void);
/* ... and its index in its mesh. */
int render_emit_polygon_index(void);

/* Blend two palettes of one model (bones each) at t as frame interpolation
 * does (pc_interp_skeleton: rotations through the skeleton found in them;
 * else linear). */
void render_blend_palette(const render_transform* was, const render_transform* now, int bones, float t,
    render_transform* out);
/* Frame interpolation switches: rotations through the skeleton (1, default)
 * or linear matrix blending (0); poses held for 1-3 game frames between
 * changes (an animation keyed below the game's frame rate) spread over the
 * whole hold (1, default; actor_native.c) or stepped (0). */
extern int pc_interp_skeleton, pc_interp_holds;

/* ---- animation trace (RPC anim_trace) ----
 * For the next `count` presented images, every object instance of the lists
 * shown (pc_hw_present_lists) with its game frame, blend t, whether it had a
 * previous palette, whether the jump test refused to blend, and its palettes
 * (as drawn, the game frame's, the previous game frame's) go to `path`
 * (binary; pc/tools/anim_trace.py reads and classifies it). Returns 0 on
 * success. count 0 stops a trace. */
int render_anim_trace(const char* path, int count);
/* Presented images still to trace (0: idle). */
int render_anim_trace_remaining(void);
/* Called once per presented image by the hardware path. */
void render_anim_trace_present(unsigned vsync, int replayed, float t, const uint32_t* lists, int count);

/* ---- statistics (the last finished frame) ---- */

typedef struct {
    int lists, instances; /* native lists drawn, instances submitted */
    int native_triangles; /* triangles of submitted instances (before culling) */
    int legacy_room_triangles; /* room triangles the hardware path drew from packets */
    int skipped_room_triangles; /* room packet triangles left to the native draw */
    int meshes, mesh_polygons; /* live meshes and their polygons */
    int mesh_updates; /* render_mesh_update calls since startup (the fallback for UV rewrites not modelled) */
    int uv_effects; /* animated materials submitted with the frame's lists (render_list_uv_effects) */
    /* Objects (characters, weapons, props; actor_native.c): */
    int object_lists, object_instances, object_triangles; /* drawn natively */
    int legacy_object_triangles; /* object packet triangles the hardware path drew */
    int skipped_object_triangles; /* object packet triangles left to native lists */
    int object_meshes_built; /* object meshes built in the frame (cache misses) */
    int interpolated_instances; /* instances drawn with a blended palette (replays) */
    /* Translucent polygons (render_list_translucent): */
    int translucent_markers, translucent_polygons; /* drawn natively (room and objects) */
    int legacy_translucent_triangles; /* semi-transparent room/object packet triangles drawn */
    int skipped_translucent_triangles; /* semi-transparent packet triangles left to native draws */
    /* Other room blocks (render_rules.layered: func_8008E19C's): */
    int layered_lists, layered_instances;
    /* 3D triangles of packets that are neither room nor object packets
     * (effects, drop shadows, particles: what is still reconstructed). */
    int legacy_other_triangles;
    /* Drop shadows and effects (render_rules.effects): markers and polygons
     * drawn natively, packet triangles left to them, and effect packet
     * triangles the hardware path drew (native off, or its marker could
     * not draw: no room list before it). */
    int shadow_markers, shadow_polygons;
    int effect_markers, effect_polygons;
    int skipped_effect_triangles, legacy_effect_triangles;
    /* Without reference packets: native VRAM rectangles copied back from
     * the hardware target for readers (StoreImage, textures, copies) since
     * startup, and areas stale at the end of the frame. */
    int vram_syncs, stale_rects;
} render_stats;
void render_stats_get(render_stats* stats);

/* ---- switches ---- */

/* Room geometry: 0 legacy packets; 1 native (the room packets are still
 * built and rasterized by the CPU for its reference VRAM, but the hardware
 * path skips the opaque ones); 2 native only (no opaque room packets at all,
 * covered semi-transparent ones not rasterized by the CPU: native VRAM is
 * copied back from the hardware target where it is read, gpu_hw.c
 * pc_hw_sync). Default 2; ROOD_NATIVE_ROOM sets it at startup. */
extern int pc_native_room;
/* Characters, weapons and props drawn by func_800B3CF0/func_800B4594: 0
 * legacy packets; 1 native (their packets are still built and rasterized by
 * the CPU for its reference VRAM; the hardware path skips the opaque ones
 * when the native list drew); 2 (default) as 1 without the CPU's
 * rasterization of covered packets (drop shadows and particles too). Needs
 * the native room in the same frame (its depth). ROOD_NATIVE_ACTORS sets it
 * at startup. */
extern int pc_native_actors;
/* Native characters paint their own polygons in the PS1's order (1,
 * default: as the PS1 shows models built from intersecting shells) or
 * depth-test them against each other (0). Against the room and other
 * models their depth is exact either way. */
extern int pc_native_actor_order;
/* Packets drawn after a native list are hidden where they are behind it
 * (with a small tolerance, so decals and drop shadows on the floor stay). */
extern int pc_native_occlusion;
/* Room lists (1, default): room polygons on or within a small slack of the
 * nearest room surface paint in the PS1's ordering-table order (documents
 * on a table, notices on walls, rugs) instead of z-fighting; real
 * occlusion stays per pixel. 0: plain depth test. Settings key
 * native_room_order, ROOD_NATIVE_ROOM_ORDER at startup, RPC graphics
 * room_order (room_slack, room_slack_units
 * tune pc_native_room_slack: relative, GTE units). */
extern int pc_native_room_order;
extern float pc_native_room_slack[2];
/* Semi-transparent polygons of the native room and objects (the room's
 * window light and glass, characters' translucent records, the blended
 * builders func_800B516C/func_800B5C30): 0 legacy packets; 1 native
 * (translucent markers in ordering-table order; their packets are still
 * built for the CPU's reference VRAM). Needs pc_native_room / actors for
 * the lists they belong to. ROOD_NATIVE_BLEND sets it at startup. */
extern int pc_native_blend;
/* Room blocks other than the current room drawn through func_8009723C
 * (func_8008E19C's animated floor quads): 0 legacy packets; 1 native
 * layered lists. ROOD_NATIVE_OBJECTS sets it at startup. */
extern int pc_native_objects;
/* Drop shadows (actor_shadow.c, func_800B4B14) and the squares under moving
 * platforms (room_shadows.inc, func_8008CEB0): 0 legacy packets; 1 native
 * world-space triangles at translucent markers (their packets are still
 * built for the CPU's reference VRAM). ROOD_NATIVE_SHADOWS sets it. */
extern int pc_native_shadows;
/* Effect particles (effect_particles.c, func_800D6E44): 0 legacy packets;
 * 1 native sprites at their world positions. ROOD_NATIVE_EFFECTS sets it. */
extern int pc_native_effects;
/* The reconstruction of 3D from packets (GTE projection log lookups, room
 * depth records, caster capture from packets, nearest-triangle matching for
 * frame interpolation): 1 always; 0 retired whenever every native switch
 * above is on (it then serves nothing: what still reaches the hardware
 * path as packets draws as 2D in its ordering-table place). ROOD_RECONSTRUCT
 * sets it. */
extern int pc_reconstruct;

#ifdef __cplusplus
}
#endif
#endif
