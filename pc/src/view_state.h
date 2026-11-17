#pragma once
/* The scene's view as the host units read it (the game's vs_view, scratch.h: the first-person/facing word, the
 * view matrix and the camera, 0x58 bytes). The host units do not include the game's headers. */
extern unsigned char vs_view[0x58];
#define PC_VIEW_FACING (*(const unsigned*)(vs_view + 0x00))
#define PC_VIEW_MATRIX (vs_view + 0x04)
/* The packet cursor and the ordering table as the host units read them (the game's vs_scratch, scratch.h: four
 * real pointers; unk0 is where the next packet is built, unk4 the ordering table's base). */
extern void* vs_scratch[4];
#define PC_PACKET_CURSOR (vs_scratch[0])
#define PC_OT_BASE (vs_scratch[1])
/* The scratchpad's remaining bytes: the per-file scratch views, the actor transform and AI slots and the work
 * areas of collision and the GTE (the PS1 kept them in its scratchpad). They overlay one another, so they are one
 * block, indexed by their PS1 offset from 0x1F800000 (0x68..0x3FF; the bytes below are vs_scratch and vs_view).
 * (The game's vs_pad, scratch.h.) */
extern unsigned char vs_pad[0x408];

#define PC_PAD(offset) (vs_pad + (offset))
