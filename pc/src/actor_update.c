/* 7DDDC.s and 7C7E0.s: actor navigation frame preparation. Native C keeps
 * its own stack; scratchpad data layout and explicit missing callees remain. */
#include <string.h>
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include "game/BATTLE/BATTLE.PRG/ai_state.h"
extern unsigned char* vs_battle_aiActorAiStates[];
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern unsigned short (*D_800F58D0)[32];
extern unsigned short (*D_800F58B8)[32];
extern unsigned char *D_800F58CC, *D_800F58C0;
extern unsigned vs_battle_aiSavedMovementRandomState;
extern unsigned D_800F16E0, D_800F16E4, D_800F16E8, D_800F16EC, D_800F16F0;
extern void pc_unimplemented(const char*);
extern int vs_core_elapsedFrameCounterTicks(void);
extern int vs_core_mathSquaredVectorLength(int, int, int);
extern void vs_battle_aiIssueActorCommand(void*, int);
extern void vs_battle_aiDispatchAiLocomotionMode(void*, int, int);
extern void vs_battle_aiSaveMovementRandomState(void);
extern void vs_main_freeHeapR(void*);
extern int ratan2(int, int);
extern void vs_battle_aiUpdateNavigationAreaMovement(void*, int);
extern void* vs_main_allocHeapR(unsigned);
extern void vs_core_memoryClearAlignedWords(void*, unsigned);
extern void vs_battle_aiDisengageActor(void*);
extern void vs_battle_aiResetActorAwarenessIndicator(void*);
extern void vs_battle_aiUnlinkEngagedActor(void*);
extern void pc_route_continuation(void*, unsigned); /* battle_movement.c */

void pc_actor_scratch(void)
{
    for (int i = 16; i >= 0; --i)
        vs_nav.aiStates[i] = vs_battle_aiActorAiStates[i];
    vs_nav.floors = D_800F58D0;
    vs_nav.cells = D_800F58B8;
    vs_nav.bitmap = D_800F58CC;
    vs_nav.exits = D_800F58C0;
    vs_move.randomState = vs_battle_aiSavedMovementRandomState;
    memcpy(vs_move.lookup, &D_800F16E0, 4);
    memcpy(vs_move.lookup + 4, &D_800F16E4, 4);
    memcpy(vs_move.lookup + 8, &D_800F16E8, 4);
    memcpy(vs_move.directions, &D_800F16EC, 4);
    memcpy(vs_move.directions + 4, &D_800F16F0, 4);
    /* Without a navigation record (vs_battle_aiUpdateActorNavigationOverrides does not test it, unlike
     * func_800E511C) the original reads word 4 of address 0: PS1 RAM, the
     * kernel area, read here through guest RAM. */
    unsigned room = vs_battle_aiRoomAiState ? *(unsigned*)&vs_battle_aiRoomAiState->unk4 : *(unsigned*)(rood_null_page + 4);
    vs_move.width = room & 0xFF;
    vs_move.depth = room >> 8 & 0xFF;
    vs_move.gap3DE[0] = room >> 16 & 0xFF;
    vs_move.gap3DE[1] = room >> 24;
}
void vs_battle_aiCacheActorAiWorkingState(void);

void vs_battle_aiFreeActorNavigationOverride(unsigned id);

/* vs_battle_aiActorNavigationOverrideStatus: vs_battle_aiHasActorNavigationState with $a3 = 1. 0 when actor `id`'s route
 * record is on its target tile, 1 while it is elsewhere, 2 when there is no
 * navigation grid, state or route. (The game's C caller passes no argument;
 * the PS1 relied on $a0 still holding the actor id, see 146C.c.) */
int vs_battle_aiActorNavigationOverrideStatus(unsigned id);

/* vs_battle_aiCopyActorNavigationOverrideState: copy actor `id`'s navigation state into `route` and reset
 * its route fields. */
/* vs_battle_aiSetActorNavigationOverride: send actor `id` towards (x, z). Releases its reservations,
 * then allocates its route record on first use (0x484 bytes, heap 7) and
 * initializes it with the target. Returns 0 without a grid, state or memory.
 * As in the assembly, an existing route record is left alone: the state
 * record is copied onto itself and takes the target instead. */
int vs_battle_aiSetActorNavigationOverride(unsigned id, int x, int z);

void vs_battle_aiUpdateActorNavigationOverrides(void);

