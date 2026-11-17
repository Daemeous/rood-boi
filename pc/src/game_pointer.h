/* Whether a pointer the game holds lies in the game's memory: the one definition, read by every host bridge that follows
 * the game's pointers (the HUD queries, the player state, dev state). The game's memory is the mapped window
 * ROOD_GUEST_RAM_BASE .. + ROOD_GUEST_RAM_SIZE (psx_types.h): the PS1's 2 MB and the heap arena beside it, so a region
 * that moves inside the window moves for every caller. During room changes and the attract demo the slots can hold stale
 * values: only a word-aligned object wholly inside the window (past the first 64 KB: the kernel's) is followed. */
#ifndef ROOD_GAME_POINTER_H
#define ROOD_GAME_POINTER_H
#include "psx_types.h"
static inline int pc_game_pointer_ok(const void* p, __SIZE_TYPE__ size)
{
    __UINTPTR_TYPE__ a = (__UINTPTR_TYPE__)p;
    return a >= ROOD_GUEST_RAM_BASE + 0x10000ul && a + size <= (__UINTPTR_TYPE__)ROOD_GUEST_RAM_BASE + ROOD_GUEST_RAM_SIZE && !(a & 3);
}
#endif
