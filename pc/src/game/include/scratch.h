#pragma once
#include <stddef.h>
#include <libgte.h>
#include "pc/src/psx_types.h"

typedef struct {
    /* 0x00 */ VECTOR position;
    /* 0x10 */ VECTOR lookAt;
    /* 0x20 */ VECTOR angles;
    /* 0x30 */ int farClip;
} vs_camera_t;

/* The packet cursor and the ordering table (the PS1 kept them in the scratchpad's first four words, at 0x1F800000):
 * real pointers, in a global defined in 146C.c. unk0 is where the next packet is built, unk4 the ordering table's
 * base, unk8 and unkC the tables the menus link into. */
typedef struct {
    /* 0x00 */ void* unk0;
    /* 0x04 */ void* unk4;
    /* 0x08 */ void* unk8;
    /* 0x0C */ void* unkC;
} vs_scratch_t;

/* The scene's view: the camera and the view matrix (the PS1 kept them in the scratchpad after the packet cursor,
 * at 0x1F800010; the offsets inside are the same). Defined in 146C.c. */
typedef struct {
    /* 0x00 */ u_int firstPersonViewFacing;
    /* 0x04 */ MATRIX viewMatrix;
    /* 0x24 */ vs_camera_t camera;
} vs_view_t;
PSX_ASSERT_SIZE(vs_view_t, 0x58);

extern vs_scratch_t vs_scratch;
/* The scratchpad's remaining bytes: the per-file scratch views, the actor transform and AI slots and the work
 * areas of collision and the GTE (the PS1 kept them in its scratchpad). They overlay one another, so they are one
 * block, indexed by their PS1 offset from 0x1F800000 (0x68..0x3FF; the bytes below are vs_scratch and vs_view).
 * Defined in 146C.c. */
extern unsigned char vs_pad[0x408];
/* A T at PS1 offset `offset` of the pad (checked at compile time to lie inside it): the overlays that stay shared. */
static inline void* vs_pad_at_(unsigned long offset)
{
    unsigned char* base = vs_pad;
    return base + offset;
}
#ifndef VS_PAD_AT
#define VS_PAD_AT(T, offset) ((T*)vs_pad_at_((offset) + 0 * sizeof(char[((offset) + sizeof(T) <= 0x408) ? 1 : -1])))
#endif
/* The navigation pointers the AI reads, real pointers (memory lane S3b: they were 4-byte guest words in vs_pad at 0x37C,
 * 0x3BC, 0x3C0, 0x3C4 and 0x3F4, filled each actor update by pc_actor_scratch; the overlays at those offsets no longer
 * share them). game/common/pad.zig mirrors the layout. */
typedef struct {
    void* aiStates[17];            /* vs_battle_aiState*, by actor id (was 0x37C) */
    unsigned short (*floors)[32];  /* the room's floor grid, D_800F58D0 (was 0x3BC) */
    unsigned short (*cells)[32];   /* the navigation cells, D_800F58B8 (was 0x3C0) */
    unsigned char* bitmap;         /* the occupancy bitmap, D_800F58CC (was 0x3C4) */
    unsigned char* exits;          /* the room's exit table, D_800F58C0, non-null when the occupancy bitmap is active (was 0x3F4) */
} vs_nav_t;
extern vs_nav_t vs_nav;
/* The AI movement frame (memory lane S3b): what the movement code reads and keeps between its steps, filled each actor
 * update by pc_actor_scratch. It was the pad's bytes 0x3C8..0x3FF, shared with the collision and animation overlays
 * at the same offsets (only one of them lives at a time on the PS1); one owner and one lifetime make it its own struct.
 * game/common/pad.zig Move mirrors the layout. */
typedef struct {
    /* 0x3C8 */ int command;                  /* the action vs_battle_aiIssueActorCommand is carrying out */
    /* 0x3CC */ unsigned char gap3CC[4];
    union {
        /* 0x3D0 */ unsigned cacheWord;       /* the three caches below, cleared together */
        struct {
            unsigned char flyTested;          /* per direction: the passability of the direction is known */
            unsigned char flyPassable;        /* and what it is (vs_battle_aiCanFlyInDirection) */
            unsigned char blockTested;        /* per direction: its blocking actor is known (vs_battle_aiDirectionalBlockingActor) */
            unsigned char gap3D3;
        };
    };
    /* 0x3D4 */ unsigned char blocker[8];     /* that actor's id + 1, by direction */
    /* 0x3DC */ unsigned char width;          /* the room's navigation grid size */
    /* 0x3DD */ unsigned char depth;
    /* 0x3DE */ unsigned char gap3DE[2];
    /* 0x3E0 */ signed char lookup[12];       /* the 3x3 neighbourhood's step directions (D_800F16E0..) */
    /* 0x3EC */ unsigned char directions[8];  /* a direction's packed step: 2-bit dx, 2 unused, 2-bit dz (D_800F16EC_t) */
    /* 0x3F4 */ unsigned char gap3F4[4];
    /* 0x3F8 */ unsigned randomState;         /* the movement random step's seed (its low half) ... */
    /* 0x3FC */ unsigned randomIncrement;     /* ... and increment */
} vs_move_t;
PSX_ASSERT_SIZE(vs_move_t, 0x38);
extern vs_move_t vs_move;
/* The actor transform block (memory lane S3b, second step): the registers the actor transform functions hand one another
 * (the object offset and angles, the bone matrix and its view position, the weapon transform, the shadow's projected points),
 * 0xA4 bytes, the PS1 pad's 0x35C..0x3FF. Several views overlay it (actorTransformScratch, boneTransformScratch,
 * cameraTransformScratch, weaponTransformScratch, boneMatrixScratch, D_1F80035C_t), indexed from its start. Defined in 146C.c;
 * game/common/pad.zig `xform` is the typed access. */
extern unsigned char vs_xform[0xA4];
#define VS_XFORM(T) ((T*)((0 * sizeof(char[sizeof(T) <= 0xA4 ? 1 : -1])) + (char*)(void*)vs_xform))
/* The block as the drop shadow (actor_shadow.c) and the weapon draw use it: the shadow's 18 projected points (screen xy at 0x14, depth at
 * 0x5C), the anchor it was placed at (x, height and z, in the words the last points overwrite once placed), and the weapon's texture word
 * (tpage and clut) at 0x34. */
typedef struct {
    char gap0[0x14];
    union {
        unsigned xy[18];
        struct {
            unsigned gap14[(0x34 - 0x14) / 4];
            unsigned texture; /* 0x34 */
            unsigned gap38[(0x54 - 0x38) / 4];
            short anchorX;  /* 0x54 */
            short anchorHeight; /* 0x56 */
            unsigned anchorZ;   /* 0x58 */
        };
    };
    unsigned depth[18];
} vs_xform_shadow_t;
PSX_ASSERT_SIZE(vs_xform_shadow_t, 0xA4);
/* The actor collision workspace (vs_field_*'s samples, probes and exclusion marks), 0x4C bytes, the pad's 0x3BC..0x407:
 * the views are collisionScratchView, ActorCollisionSamples, D_1F8003BC_t, actorCollisionExclusion, collisionResponseScratch. */
extern unsigned char vs_collision[0x4C];
extern vs_view_t vs_view;
