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
void vs_battle_aiForgetAiActionTarget(vs_battle_aiState* arg0);
void vs_battle_aiReleaseActorAiReservations(vs_battle_aiState*);
extern vs_battle_aiState* vs_battle_aiActorAiStates[];
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern D_800F5920_t* D_800F5920;
void vs_battle_aiSetTemporaryAwarenessIndicator(vs_battle_aiState* arg0, int arg1, int arg2);

int vs_battle_aiDestinationOccupancy(vs_battle_aiState*, unsigned int);

int vs_core_mathSquaredDistanceWithHeightOffsets(short*, short*, int, int);
extern int D_800F58E0;
extern int D_800F16F4;

int vs_battle_aiCanApproachEngagedTarget(vs_battle_aiState*, actionCandidateEntry*);
int vs_field_groundHeightAtWorldPosition(int, int);
int vs_core_mathSquaredDistanceWithHeightOffsets(short*, short*, int, int);
unsigned int vs_core_mathSquaredVectorLength(int, int, int);
void vs_battle_aiWanderAroundTile(vs_battle_aiState*, unsigned int, int);
void vs_battle_aiMoveToTile(vs_battle_aiState*, unsigned int, int);
int vs_battle_aiFleeVisibleEnemies(vs_battle_aiState*, int);
int vs_battle_aiApproachActor(vs_battle_aiState*, int, int, int);
int vs_battle_aiTrackActorPosition(vs_battle_aiState*, int);
int vs_battle_aiMoveForActionCandidate(vs_battle_aiState* state, actionCandidateEntry* entry);

int vs_battle_aiApproachActor(vs_battle_aiState*, int, int, int);
int vs_battle_aiTrackActorPosition(vs_battle_aiState*, int);
int vs_battle_aiUpdateCombatPursuit(vs_battle_aiState* state);

typedef union {
    unsigned int raw;
    unsigned char b[4];
} da4bcPoint;

typedef struct {
    char pad0[28];
    short x;
    char pad1E[2];
    short z;
    char pad22[58];
    vs_battle_movementPosition tile;
} da4bcModel;

typedef struct {
    unsigned int target : 4, pad : 28;
} da4bcEntryTarget;

int vs_battle_aiFindActorByRelationship(void*, void*, int);
void vs_battle_aiWanderAroundSavedCenter(void*);
void vs_battle_aiPatrolWaypoints(void*);
void vs_battle_aiDispatchAiLocomotionMode(void*, int, int);
int vs_battle_aiNextActorRandomMasked(int);
void vs_battle_aiProjectClearRadialDestination(vs_battle_aiState*, vs_battle_movementPosition, int, unsigned int);

int vs_battle_aiTryApproachPlannedTarget(vs_battle_aiState* state, da4bcEntry* entry);

int vs_battle_aiUpdatePatrolCombatBehavior(vs_battle_aiState* state);

int vs_battle_aiCanExecuteActionCandidate(vs_battle_aiState*, func_800DCAA0_t*);
int vs_battle_aiIsEngagementReady(vs_battle_aiState*);
int vs_battle_aiDoesActionRequireCooldown(vs_battle_aiState*, func_800DCAA0_t*);
int vs_battle_aiNextActorRandomMasked(int);
extern int D_800F58E0;

int vs_battle_aiTryExecuteActionCandidate(vs_battle_aiState* context, func_800DCAA0_t* action);

typedef struct actionDispatchModel {
    char pad0[8];
    unsigned int flags;
    char padC[6];
    unsigned char id;
    char pad13[7];
    short status;
    short position[3];
} actionDispatchModel;
extern int D_800F16F4;
void vs_battle_aiDispatchMovementBehavior(vs_battle_aiState*);
void func_800DB7B4(vs_battle_aiState*);
int vs_battle_aiTryExecutePlannedActions(vs_battle_aiState* state);

void vs_battle_aiUpdateActorDecisions(void);

int vs_battle_aiTryRecoverBlockedMovement(vs_battle_aiState* state);

