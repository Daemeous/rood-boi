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

int vs_battle_previewActionAgainstLimb(int, int, int, int, void*);
int vs_battle_previewActionAgainstActor(int, int, int, int, SVECTOR*, SVECTOR*, void*);
void vs_battle_aiApplyActionRelationshipChanges(func_800DEEA4_t*);
struct actionStatSnapshot;
void vs_battle_aiApplySimulatedActionResults(struct actionStatSnapshot*);
void vs_battle_aiRememberAiActionTarget(vs_battle_aiState*);
void vs_battle_aiForgetAiActionTarget(vs_battle_aiState* arg0);
extern vs_battle_aiState* vs_battle_aiActorAiStates[];
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern u_short (*D_800F58D0)[32];
extern func_800DEEA4_t* vs_battle_aiPlanningWorkspace;
extern D_800F5920_t* D_800F5920;

int func_800E4CF4(vs_battle_aiState*, actionCandidateEntry*, int);
int vs_battle_aiRefreshAiActionUsability(vs_battle_aiState*, int, int);
void vs_battle_aiSelectActionMovementMode(vs_battle_aiState*);
int vs_battle_aiRefreshActionCandidateReadiness(vs_battle_aiState* state);
struct vs_battle_aiState;
void vs_battle_aiResetActorAwarenessIndicator(struct vs_battle_aiState*);
void vs_battle_aiRefreshAiResourceRestrictions(void*);
int vs_field_groundHeightAtWorldPosition(int, int);
unsigned int vs_core_mathSquaredVectorLength(int, int, int);
void vs_battle_aiSetActorAwarenessIndicator(int, int);

typedef struct actionStatSnapshot {
    int unknown;
    short hp, maxHP, mp, maxMP;
    unsigned int status;
    short value10, limit10, value14, limit14;
} actionStatSnapshot;
typedef struct {
    unsigned char actor;
    char pad1;
    unsigned char value2;
    char pad3;
    short hp, mp;
    char pad8[12];
    unsigned int addStatus, removeStatus, modes;
    char pad20[24];
    short value10;
    char pad3A[2];
    short value14;
    char pad3E[2];
    int skip;
} actionStatDelta;
typedef struct {
    unsigned short action;
    char pad2[2];
    actionStatDelta first;
    char pad48[2];
    unsigned short count;
    actionStatDelta entries[1];
} actionStatApplication;
typedef struct {
    unsigned int flags;
    char pad4[48];
} actionCostView;

void vs_battle_aiApplySimulatedStatDelta(short*, short*, int, int);
void vs_battle_aiApplySimulatedActionResults(actionStatSnapshot* stats);

void func_800DCAA0(vs_battle_aiState* arg0, void* arg1, func_800DCAA0_t* arg2, int arg3);

void func_800DCBD8(func_800DCAA0_t* arg0);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/74088", D_80069B68);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/74088", D_80069BBC);

typedef struct {
    char pad[14];
    unsigned short flags;
} actionScoreMetadata;
void vs_battle_aiCaptureActorConditionTable(actionStatSnapshot*);
int vs_battle_aiScoreActorCondition(int, int, int, int);
const signed char D_80069BD0[] = { -51, -39, -39, -39, -26, -51, 50, -13, 12, -13, 12, 25,
    -51, -26, -45, -26, -39, 76, 19, -13, 12, 12, 12, 12, 12, 12, 12, 12, 12, 0, 0, 0 };
void func_800DCC94(vs_battle_aiState* state, actionCandidateEntry* candidate, int mode, actionScoreMetadata* action);

typedef struct {
    char pad0[20];
    unsigned int effect1, effect2;
    char pad1C[24];
} actionReactionAction;
typedef struct {
    char pad0[10];
    unsigned char flag;
} actionReactionMotion;
int vs_battle_aiIsActionBeneficialToActor(int, int);
int vs_battle_aiIsActionHarmfulToActor(int, int);
void vs_battle_aiTryBeginAshleyAlliance(vs_battle_aiState*, int);
void vs_battle_aiEndAshleyAlliance(vs_battle_aiState*);
void vs_battle_aiMakeTargetHostile(vs_battle_aiState*, int);
void vs_battle_aiApplyActorActionReaction(vs_battle_aiState* state, actionStatApplication* app);

void vs_battle_aiPropagateActorRelationships(vs_battle_aiState* state);

/* Local view of the vs_main_actions flag word at 0xC */
typedef struct {
    char unk0[0xC];
    u_int unkC_0 : 23;
    u_int flag23 : 1;
    u_int unkC_24 : 4;
    u_int flag28 : 1;
    u_int flag29 : 1;
    u_int unkC_30 : 2;
    char unk10[0x24];
} actionChoiceInfo;

typedef struct {
    unsigned char x, y, z, flags;
} nearestWaypointDestination;
extern unsigned short* D_800F58C8;
static inline unsigned int nearestWaypointValue(unsigned short* row, int index)
{
    unsigned short* p = row;
    p += index;
    return *p;
}
static inline unsigned short* nearestWaypointAddress(unsigned short* row, int index)
{
    row += index;
    return row;
}
void vs_battle_aiFindNearestActiveWaypoint(vs_battle_aiState* state, nearestWaypointDestination* destination);

extern u_char (*D_800F5904)[256];
void vs_core_memoryClearAlignedWords(void*, int);
void vs_battle_aiCopyActionCandidate(actionCandidateEntry*, actionCandidateEntry*);
void vs_battle_aiAppendActionCandidate(vs_battle_aiState*, actionCandidateEntry*);
void vs_battle_aiCacheFallbackActionCandidate(actionCandidateEntry*, actionChoiceInfo*);

static inline vs_battle_uiEquipment_limb* actorLimb(vs_battle_actor2* stats, int limb)
{
    return (vs_battle_uiEquipment_limb*)((char*)stats->limbs + limb * sizeof(vs_battle_uiEquipment_limb));
}

typedef struct {
    int mask;
    u_short unk4;
    u_short interval;
    int unk8;
    int unkC;
} statusEffectTick;
typedef struct {
    statusEffectTick entries[1];
} statusEffectTable;

extern u_int D_80069BBC[];
void vs_battle_aiRefreshAiResourceRestrictions(void*);

void vs_battle_aiAdvancePlanningSimulation(int elapsed);

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

extern int D_800F58F0;
extern int D_800F58F4;
extern vs_battle_actor2* vs_battle_aiPlanningStatsSnapshot;
extern actionCandidateEntry (*D_800F58EC)[16];
extern actionCandidateEntry* D_800F58F8;
void vs_battle_aiCopyPlanningActorStats(vs_battle_actor2* src, vs_battle_actor2* dst);
void vs_battle_aiSortActionCandidates(vs_battle_aiState*);
void vs_battle_aiPromoteRandomizedActionCandidate(vs_battle_aiState*, int);

static inline int snapshotActorStats(void)
{
    vs_battle_actor* actor;

    vs_battle_aiPlanningStatsSnapshot = vs_main_allocHeapR(sizeof(vs_battle_actor2) * 16);
    if (vs_battle_aiPlanningStatsSnapshot == NULL) {
        return 0;
    }
    for (actor = vs_battle_actors[0]; actor != NULL; actor = actor->next) {
        vs_battle_aiCopyPlanningActorStats(actor->unk3C, &vs_battle_aiPlanningStatsSnapshot[actor->id]);
    }
    return 1;
}

static inline void restoreActorStats(void)
{
    vs_battle_actor* actor;

    for (actor = vs_battle_actors[0]; actor != NULL; actor = actor->next) {
        vs_battle_aiCopyPlanningActorStats(&vs_battle_aiPlanningStatsSnapshot[actor->id], actor->unk3C);
    }
    vs_main_freeHeapR(vs_battle_aiPlanningStatsSnapshot);
}

#undef MIN
#undef MAX

void vs_battle_aiReconcileCompletedActionPlan(func_800DEEA4_t* arg0);

typedef struct actorEvaluationStats {
    char pad0[24];
    short hp;
    char pad1A[23];
    unsigned char f31;
    char pad32[1];
    unsigned char f33;
} actorEvaluationStats;
typedef struct actorEvaluationContext {
    char pad0[4];
    unsigned char id;
    char pad5[63];
    int f44;
} actorEvaluationContext;
extern u_char (*D_800F5904)[256];
void vs_battle_aiSetActorVerticalMovementSteps(int, int, int);
void vs_battle_aiResetActorAwarenessIndicator(vs_battle_aiState*);
int vs_battle_aiRebuildActorActionPlans(void* arg0);

void vs_battle_aiNotifyActionRelationshipChanges(func_800DEEA4_t* arg0);

