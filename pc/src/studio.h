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
    int backdrop;                 /* 1: no stage: the game's own background drawn in the one colour `backdrop_rgb` (0xBBGGRR) */
    int backdrop_rgb;
    int hide_others;              /* the room's actors are not drawn */
    int equipment;                /* ZUD: its weapon and shield (0: the model alone) */
} pc_studio_scene_cfg;

/* What it made (out): the actor slot of the model (-1 none), where it stands (world), a message. */
typedef struct {
    int slot, weapon_slot;
    float position[3];
    int bones;
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

#ifdef __cplusplus
}
#endif
#endif
