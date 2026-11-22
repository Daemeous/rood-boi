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

void vs_battle_aiDisengageActor(vs_battle_aiState*);
void vs_battle_aiTryEngageActor(vs_battle_aiState*);
int vs_battle_aiIsActionCooldownReady(vs_battle_aiState*, func_800E78F4_t2*);
void vs_battle_aiIssueActorCommand(vs_battle_aiState*, int);
void vs_battle_aiRecoverMovement(vs_battle_aiState*);
int func_800E7698(vs_battle_aiState*);
void vs_battle_aiUpdateBossOrbitTracking(vs_battle_aiState*);
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern int D_800F5918;
extern int D_800F591C;
extern D_800F5920_t* D_800F5920;

unsigned int vs_core_mathSquaredVectorLength(int, int, int);

int vs_battle_aiIsEngagementReady(vs_battle_aiState*);
unsigned int vs_core_mathSquaredVectorLength(int, int, int);

extern unsigned int vs_battle_aiPerceptionRangesSquared[8];
extern unsigned char D_800F17D8[], D_80069C18[];
extern actorSettingsEntry vs_battle_aiWanderTimingPresets[8]; /* indexed by three bits of an actor settings word */
int func_800E7F8C(vs_battle_aiState*);

typedef struct {
    short facing;
    unsigned char state, flag3;
    char pad4[6];
    unsigned char flagA, frame;
    char padC[4];
    short angle;
    char pad12[6];
    short x, z;
} radialMotion;
typedef struct {
    char pad[8];
    unsigned int flags;
} radialMotionActor;
extern short D_800F17DC[][2];
unsigned int vs_core_mathSquaredVectorLength(int, int, int);
int func_800E7698(vs_battle_aiState* state);

int func_800E78F4(vs_battle_aiState* arg0);

typedef struct {
    unsigned short facing;
    unsigned char state, flag3;
    int radiusSquared;
    char pad8[4];
    short stateC, stateE, angle, height;
    unsigned char phase;
    char pad15[7];
    unsigned char initialized;
} trackingMotion;
typedef struct {
    char pad[0x1C];
    short x, y, z;
} trackingMotionActor;
int rand(void);
int vs_gte_rsqrt(int);
unsigned int vs_core_mathSquaredVectorLength(int, int, int);
void vs_battle_aiUpdateBossOrbitTracking(vs_battle_aiState* state);

int func_800E7F8C(vs_battle_aiState* arg0);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/7EE98", D_80069C18);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/7EE98", vs_battle_aiPerceptionRangesSquared);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/7EE98", vs_battle_aiWanderTimingPresets);
