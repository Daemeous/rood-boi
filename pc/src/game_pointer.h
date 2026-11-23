/* Whether a pointer the game holds lies in the game's arenas (psx_types.h rood_guest_memory), where the actors and effect lists live: the one
 * definition, read by every host bridge that follows the game's pointers (the HUD queries, the player state, dev state). During room changes
 * and the attract demo the slots can hold stale values: only a word-aligned object wholly inside an arena is followed. */
#ifndef ROOD_GAME_POINTER_H
#define ROOD_GAME_POINTER_H
#include "psx_types.h"
static inline int pc_game_pointer_ok(const void* p, __SIZE_TYPE__ size)
{
    __UINTPTR_TYPE__ a = (__UINTPTR_TYPE__)p;
    return rood_guest_memory(a, size) && !(a & 3);
}
#endif
