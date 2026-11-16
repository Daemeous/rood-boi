/* 7DDDC.s and 7C7E0.s: actor navigation frame preparation. Native C keeps
 * its own stack; scratchpad data layout and explicit missing callees remain. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include "game/BATTLE/BATTLE.PRG/ai_state.h"
extern unsigned char* vs_battle_aiActorAiStates[];
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern unsigned D_800F58D0, D_800F58B8, D_800F58CC, D_800F58C0, vs_battle_aiSavedMovementRandomState;
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

void pc_actor_scratch(unsigned* scratch)
{
    for (int i = 16; i >= 0; --i)
        scratch[(0x37c / 4) + i] = psx_to_int(unsigned, vs_battle_aiActorAiStates[i]);
    scratch[0x3bc / 4] = D_800F58D0;
    scratch[0x3c0 / 4] = D_800F58B8;
    scratch[0x3c4 / 4] = D_800F58CC;
    scratch[0x3f4 / 4] = D_800F58C0;
    scratch[0x3f8 / 4] = vs_battle_aiSavedMovementRandomState;
    scratch[0x3e0 / 4] = D_800F16E0;
    scratch[0x3e4 / 4] = D_800F16E4;
    scratch[0x3e8 / 4] = D_800F16E8;
    scratch[0x3ec / 4] = D_800F16EC;
    scratch[0x3f0 / 4] = D_800F16F0;
    /* Without a navigation record (vs_battle_aiUpdateActorNavigationOverrides does not test it, unlike
     * func_800E511C) the original reads word 4 of address 0: PS1 RAM, the
     * kernel area, read here through guest RAM. */
    scratch[0x3dc / 4] = vs_battle_aiRoomAiState ? *(unsigned*)&vs_battle_aiRoomAiState->unk4 : *(unsigned*)(rood_null_page + 4);
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

