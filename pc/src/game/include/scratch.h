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
#define PC_PAD(offset) (vs_pad + (offset))
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
extern vs_view_t vs_view;
