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

/* The actor transform block as the host units read it (the game's vs_xform, scratch.h): the weapon draw's texture word (tpage and
 * clut) is at 0x34. */
extern unsigned char vs_xform[0xA4];
typedef struct {
    unsigned char gap[0x34];
    unsigned texture;
} pc_xform_texture_t;
#define PC_XFORM_TEXTURE (((const pc_xform_texture_t*)(const void*)vs_xform)->texture)
/* A T at PS1 offset `offset` of the pad (checked at compile time to lie inside it): the overlays that stay shared. */
static inline void* pc_pad_at_(unsigned long offset)
{
    unsigned char* base = vs_pad;
    return base + offset;
}
#ifndef VS_PAD_AT
#define VS_PAD_AT(T, offset) ((T*)pc_pad_at_((offset) + 0 * sizeof(char[((offset) + sizeof(T) <= 0x408) ? 1 : -1])))
#endif
