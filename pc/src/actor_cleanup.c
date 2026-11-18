/* Native translations of actor-unload assembly in 7E05C.s, 7DE94.s,
 * 7BDD4.s and 6FA0C.s. Guest structure offsets retain the original ABI. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include "game/BATTLE/BATTLE.PRG/ai_state.h"
extern unsigned char* vs_battle_aiActorAiStates[];
/* Guest words at 0x800F58xx: 32-bit guest addresses. */
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern unsigned short (*D_800F58B8)[32];
extern unsigned D_800F590C;
extern void* D_800F5920;
extern void vs_battle_aiFreeActorNavigationOverride(unsigned);
extern void vs_battle_setActorMarker(int, int);
extern void vs_battle_aiUnlinkEngagedActor(void*);
extern void vs_main_freeHeap(void*);

void vs_battle_aiReleaseAiDestinationTile(int x, int z);

void vs_battle_aiResetActorAwarenessIndicator(void* actor);

void vs_battle_aiReleaseActorAiReservations(void* actor);

void vs_battle_aiFreeActorAiState(void* actor);


extern unsigned short (*D_800F58D0)[32];
extern unsigned char* D_800F58C0;
void vs_battle_aiFreeRoomAiState(void);


/* 7E05C.s: the room's height limit (word 0x14 of the vs_battle_aiRoomAiState record), or 0
 * when no record is loaded. Used when placing actors on room entry. */
int vs_battle_aiRoomNavigationCeilingHeight(void);

