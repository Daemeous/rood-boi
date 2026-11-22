#include "common.h"
#include "3A1A0.h"
#include "4A0A8.h"
#include "5BF94.h"
#include "6E644.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "pc/src/game/SLUS_010.40/overlay.h"
#include "build/src/include/lbas.h"
#include <abs.h>
#include <stdlib.h>
#include "pc/src/psx_types.h"
#include "ai_state.h"

short vs_field_floorHeightAt(int, int);
void vs_battle_aiIssueActorCommand(vs_battle_aiState*, int);
void vs_battle_aiFloodFillAiTerrainHeights(int, int, int);
extern D_800F16EC_t D_800F16EC[8];
extern vs_battle_aiState* vs_battle_aiActorAiStates[];
extern u_short (*D_800F58B8)[32];
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern u_short (*D_800F58D0)[32];
extern unsigned short* D_800F58C8;
void vs_core_memoryClearAlignedWords(void*, int);
int vs_battle_aiRebuildActorActionPlans(void* arg0);

int vs_battle_calculateRangeShapeExtent(func_800E5568_t* arg0);

int func_800E5600(int arg0, int actionId, u_int arg2);

int vs_battle_calculateActionRangeSquared(vs_battle_aiState* arg0, int actionId);

typedef union {
    unsigned int raw;
    unsigned char bytes[4];
} actionMetadataWord;
typedef struct {
    actionMetadataWord flags;
    char pad[8];
    unsigned int flagsC;
    char rest[36];
} actionMetadataAction;
void func_800E5998(void);

void func_800E5A74(int* arg0, func_800E5A74_t* arg1);

typedef int actorSettingsCallback(vs_battle_aiState*);
extern unsigned int vs_battle_aiPerceptionRangesSquared[8];
extern unsigned short vs_battle_aiViewAngles[][2], D_800F17C8[][2];
extern unsigned char D_800F17D8[], D_80069C18[];
extern actorSettingsEntry vs_battle_aiWanderTimingPresets[8]; /* indexed by three bits of an actor settings word */
extern int D_800F58C0;
extern unsigned short* D_800F58C8;
extern int (*vs_battle_aiDecisionCallbacks[])(vs_battle_aiState*);
const unsigned char D_80069C08[4] = { 9, 7, 0, 0 };
int func_800E7F8C(vs_battle_aiState*);
void vs_battle_aiAllocateOrbitingBossState(vs_battle_aiState*);
void func_800E5998(void);

void vs_battle_aiApplyActorAiSettings(vs_battle_aiState* state, actorSettingsWord* settings);

void vs_battle_aiFloodFillAiTerrainHeights(int arg0, int arg1, int arg2);

int vs_battle_aiFindAdjacentAiGroundHeight(int arg0, int arg1);

void vs_battle_aiBuildAiSpecialTerrainHeights(void);

void vs_battle_aiInitializeRoomAiMap(void);

void vs_battle_aiInitializeRoomNavigation(void);

extern int D_800F590C;
void vs_actors_anchorPoint(unsigned char, int, void*, int);
int func_800A152C(int, int, int);

void vs_core_memoryClearAlignedWords(void*, int);
void func_800E6828(vs_battle_aiState*);

void vs_battle_aiInitializeActorAiRelations(vs_battle_aiState*);
void func_800E72D0(void);

int vs_battle_aiCreateActorAiState(vs_battle_actor* actor, int options);

