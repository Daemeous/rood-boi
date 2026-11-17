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
void vs_battle_aiRecoverMovement(vs_battle_aiState*);
void vs_battle_aiForgetAiActionTarget(vs_battle_aiState* arg0);
extern vs_battle_aiState* vs_battle_aiActorAiStates[];
extern D_800F58BC_t* vs_battle_aiRoomAiState;

int vs_battle_aiCanTraverseTileEdge(vs_battle_aiState*, unsigned int, unsigned int, int);
int vs_battle_aiDestinationOccupancy(vs_battle_aiState*, unsigned int);
unsigned int vs_battle_aiFindGapLandingTile(vs_battle_aiState*, unsigned int, int, unsigned int);
unsigned int vs_battle_aiFindAdjacentMovementDestination( vs_battle_aiState* state, int first, int second, unsigned int direction);

int func_800E4CF4(vs_battle_aiState*, actionCandidateEntry*, int);
int vs_battle_aiRefreshAiActionUsability(vs_battle_aiState*, int, int);
void vs_battle_aiSelectActionMovementMode(vs_battle_aiState*);
int vs_battle_aiRefreshActionCandidateReadiness(vs_battle_aiState* state);

int vs_core_mathSquaredDistanceWithHeightOffsets(short*, short*, int, int);
int vs_field_isTerrainSegmentBlocked(SVECTOR*, SVECTOR*, int, int);
int vs_battle_aiTargetFacingCosine(SVECTOR*, SVECTOR*, int, u_int);

#define RELATION_STATES(id) ((vs_battle_aiState*)vs_nav.aiStates[id])

static inline int checkExtent(
    SVECTOR* from, SVECTOR* to, vs_battle_aiState* state, int id)
{
    vs_battle_aiState* other = RELATION_STATES(id);
    int extent;

    if (other) {
        extent = other->extent;
    } else {
        extent = 128;
    }
    return vs_field_isTerrainSegmentBlocked(from, to, extent, state->extent);
}

static inline int checkHeight(
    SVECTOR* from, SVECTOR* to, vs_battle_aiState* state, int id)
{
    vs_battle_aiState* other = RELATION_STATES(id);
    int height;
    int own;

    if (other) {
        height = ABS(other->height);
    } else {
        height = 64;
    }
    own = state->height;
    if (own < 0) {
        own = -own;
    }
    return vs_field_isTerrainSegmentBlocked(from, to, height, own);
}

void vs_battle_aiUpdateTargetPerception(vs_battle_aiState* state, int id);

typedef struct {
    u_int current : 16;
    u_int max : 16;
} actorUpdateHp;

typedef struct {
    char unk0[0x18];
    actorUpdateHp hp;
    int current;
    int max;
} actorUpdateStats;

static inline int historyRepeats(vs_battle_aiState* state)
{
    return state->historyX.w[0] == state->historyX.w[1]
        && state->historyZ.w[0] == state->historyZ.w[1];
}

static inline int historyStalled(vs_battle_aiState* state)
{
    return state->historyMask == 0xFF ? historyRepeats(state) : 0;
}

extern int D_800F58D4;
extern int D_800F58E0;
extern int D_800F16F4;
struct vs_battle_aiState;
struct vs_battle_aiState;
void vs_battle_aiResetActorAwarenessIndicator(struct vs_battle_aiState*);
void vs_battle_aiUnlinkEngagedActor(vs_battle_aiState*);
void vs_battle_aiUpdateEngagementBuildup(struct vs_battle_aiState*);
void vs_battle_aiRefreshAiResourceRestrictions(void*);
void vs_battle_aiUpdateTemporaryAwarenessIndicator(vs_battle_aiState*);
void vs_battle_aiTickActorDecisionTimers(vs_battle_aiState*);
void func_800E48F8(vs_battle_aiState*);
void func_800E48A8(vs_battle_aiState*, int, int);

void vs_battle_aiUpdatePerceivingActor(void);

extern int vs_battle_aiSavedMovementRandomState;

void vs_battle_aiSaveMovementRandomState(void);

