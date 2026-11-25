/* The model studio (pc/docs/plans/2026-11-22-model-studio-and-paint-channels-design.md, phase P2).
 *
 * studio.cpp (the host: the Look, the camera, the catalog, RPC `studio`) holds the game with the photo
 * mode's pause (photo.h) and, at the start of every render, calls pc_studio_scene (studio.c, the game
 * side) once: it overwrites the loaded room's geometry block with a synthetic stage (a floor, a fillet
 * and a wall: a room-format block, so every consumer of the room sees it, ray tracing included), hides
 * the room's actors and builds the summoned model in a spare actor slot. Nothing of it needs undoing:
 * the render ends by rewinding the guest's state to the held moment (photo.cpp), so leaving the studio
 * leaves the game exactly as it was held. */
#ifndef ROOD_STUDIO_H
#define ROOD_STUDIO_H
#ifdef __cplusplus
extern "C" {
#endif

/* What pc_studio_scene builds. The files are host memory (read from the disc by studio.cpp). */
enum { PC_STUDIO_NONE = 0, PC_STUDIO_SHP = 1, PC_STUDIO_ZUD = 2, PC_STUDIO_WEP = 3 };
typedef struct {
    /* the stage, in world units about the player's feet (y down: the floor is at the player's y) */
    int stage;                    /* 0: leave the room as it is */
    float radius, fillet, height; /* the floor's, the curve's, the wall's */
    int grey;                     /* the stage's grey (0..255) */
    float facet;                  /* the widest a facet may be round the stage, units */
    float floor_step, wall_step;  /* the longest side of a floor ring's and a wall ring's facets (a facet is never clipped: keep them small) */
    int actor_light;              /* the light the game's own shading gives the model: 0xBBGGRR everywhere (0x808080: none) */
    /* the model */
    int kind;                     /* PC_STUDIO_* */
    const unsigned char* file;    /* SHP: the file; ZUD: the file; WEP: the weapon's file */
    unsigned size;
    const unsigned char* rig;     /* WEP: the SHP of the model that holds it (hidden) */
    unsigned rig_size;
    int model_id;                 /* the id the loader files it under (a number the room's actors do not use) */
    const unsigned char* base;    /* the model whose textures the SHP shares (its SHP file; NULL: it shares none) ... */
    unsigned base_size;
    int base_id;                  /* ... and the id the SHP names it by */
    int material;                 /* WEP: the weapon's material; SHP: its default material */
    int facing;                   /* 0..4095 */
    int animation;                /* the sequence to play (the loader's numbering); <0: none */
    int animation_frame;          /* > 0: start it that many frames in (events included: vs_actors_playActorAnimationAtFrame) */
    int backdrop;                 /* 1: no stage: the game's own background drawn in the one colour `backdrop_rgb` (0xBBGGRR) */
    int backdrop_rgb;
    int hide_others;              /* the room's actors are not drawn */
    int equipment;                /* ZUD: its weapon and shield (0: the model alone) */
} pc_studio_scene_cfg;

/* rood-studio (rood_studio.c): the studio without a game. pc_studio_app_frame gives the app this frame's state: the model asked for (kind, id; `serial`
 * changes with every summon and clear), the scene as the in-game studio would build it (the stage's measures, the backdrop, the facing, the equipment
 * switch), the camera (yaw and pitch in radians, distance to the target, fov in degrees), the look. */
typedef struct {
    int serial, has_item, id, framed, rig;
    char kind[12], look[24];
    pc_studio_scene_cfg scene;
    float yaw, pitch, distance, fov, target[3];
    int animation;        /* the animation asked for (-1: the game's own choice) */
    float animation_time; /* where in it, in frames */
    int holds;            /* the animation's bone holds are applied (data animation.holds) */
} pc_studio_app_view;
void pc_studio_app_begin(void);
int pc_studio_app_frame(pc_studio_app_view* view);
void pc_studio_app_measured(const float lo[3], const float hi[3], const unsigned* meshes, const unsigned long long* keys, int count);
void pc_studio_app_landed(void);
void pc_studio_app_animations(const int* ids, int count, int frames, int shown); /* the model's animations, the shown one's length and number */
int pc_studio_app_summon(const char* kind, int id, char* why, int size); /* 0, or -1 with the reason */
void pc_studio_app_set_look(const char* name);
void pc_studio_app_ui(int on); /* the app's screen (the panels): off for a picture asked for */

/* What it made (out): the actor slot of the model (-1 none), where it stands (world), a message. */
typedef struct {
    int slot, weapon_slot;
    float position[3];
    int bones;
    int animation, animation_frames; /* the sequence the game started (its numbering), and its length in frames */
    char message[160]; /* why it failed */
    char info[96];     /* what it built (the stage's size) */
} pc_studio_scene_out;

/* Game side (studio.c). Returns 0, or -1 with out->message. */
int pc_studio_scene(const pc_studio_scene_cfg* cfg, pc_studio_scene_out* out);
/* The room is loaded (a field with geometry). */
int pc_studio_room_ready(void);

/* actor_native.c: the world-space bounding box of every model drawn since pc_studio_bounds_reset (min, max;
 * valid when the box has points), and what studio.cpp has the renderers leave out (pc_studio_hide_bodies, 0 in play):
 * bit i the body of actor slot i (the weapon it holds is drawn), bit 16 the game's effect particles, bit 17 the
 * actors' drop shadows. */
void pc_studio_bounds_reset(void);
int pc_studio_bounds_get(float lo[3], float hi[3]);
extern int pc_studio_hide_bodies;
/* The meshes (render.h handles) and their content keys (model_key.h) the renderer drew since pc_studio_bounds_reset: the texel locator
 * (studio.cpp) reads their poses. Returns how many (at most max). */
int pc_studio_meshes(unsigned* mesh, unsigned long long* key, int max);

#ifdef __cplusplus
}
#endif
#endif
