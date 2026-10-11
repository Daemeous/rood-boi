/* Room texture effects as animated materials (room_effects.c; design notes
 * in pc/docs/Native_Rendering.md, "Room texture animations"). */
#ifndef ROOD_ROOM_EFFECTS_H
#define ROOD_ROOM_EFFECTS_H
#include "render.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_UV_EFFECTS_MAX 64 /* the game's effect slots (D_800F1DD8[0..63]) */
/* 1 (default): effects are animated materials; 0: none modelled (the meshes
 * re-decode every rewrite, as before; for comparisons). */
extern int pc_uv_effects_enabled;

/* One effect of the current room that rewrites a polygon's UVs. */
typedef struct {
    int type; /* 0 UV atlas frames (func_80091C6C), 2 UV scroll (func_80091E10) */
    int slot; /* the game's effect slot (D_800F1DD8 index) */
    unsigned polygon; /* the polygon's byte offset in the room's geometry section */
    int quad;
    /* 1: the polygon's UV bytes are the effect's base UVs plus one offset,
     * drawn as an animated material; 0: not modelled (the meshes re-decode
     * the rewritten bytes instead, room_native.c mesh_follow). */
    int modelled;
    int running; /* the game's slot is active and enabled (-1: unknown) */
    int delay, tick; /* the effect steps when its tick counter passes delay */
    uint8_t u[4], v[4]; /* base UVs (the offset is added to them) */
    /* UV scroll: window base, region size, the visible window's size, the
     * step per tick, and the wrap period (the steps from the window base
     * until the scroll wraps back to it). */
    int base[2], size[2], window[2], step[2], period[2];
    /* UV atlas frames: frames, columns, frame size, the current frame. */
    int frames, columns, frame_size[2], frame;
    int game_offset[2]; /* the offset in the polygon's bytes (the game's frame) */
    int offset[2]; /* the offset drawn (overrides applied) */
    float shift[2]; /* motion since the previous game frame (frame interpolation) */
    /* Smooth scrolls (pc_interp_uv 2): how far the tick counter is towards
     * the next step (texels), and the motion of offset + lead since the
     * previous game frame. */
    float lead[2], smooth[2];
    float speed; /* override: speed multiplier (1: the game's) */
    int freeze; /* override: hold the current offset */
} pc_uv_effect;

/* Called once per game frame with the room data block func_8009723C draws
 * (the room's geometry section): reads the room's effect section when the
 * room changed and the game's effect state, so the effects' offsets follow
 * the game exactly (timing, pauses, save/load). Returns the effect count. */
int pc_uv_effects_frame(const void* data);
/* The table for the room's draw list (render_list_uv_effects): entry i
 * for effect i + 1 (zeros for effects not modelled). */
const render_uv_effect* pc_uv_effects_table(int* count);
/* The current room's effects (NULL when none). */
const pc_uv_effect* pc_uv_effects(int* count);
/* The effect (1-based) of the modelled effect rewriting the polygon at byte
 * `offset` of the room data block `data`, 0 when none. */
int pc_uv_effect_at(const void* data, unsigned offset);
/* Changes when the set of modelled effects changes (a new room, an effect
 * found not modelled): meshes tagged before re-decode. */
unsigned pc_uv_effects_generation(void);

/* Overrides for the current room (saved with the room's lights file as
 * "uv_effect <index> <speed> <freeze>" lines; index is 1-based). */
void pc_uv_effect_override(int effect, float speed, int freeze);
void pc_uv_effects_reset(void);
int pc_uv_effects_parse(const char* line);
void pc_uv_effects_write(FILE* f);
int pc_uv_effects_overrides(void);

#ifdef __cplusplus
}
#endif
#endif
