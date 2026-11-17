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

int vs_field_collectMovingPlatformPositions(func_8008D2C0_t*);
void vs_battle_aiIssueActorCommand(vs_battle_aiState*, int);
struct vs_battle_aiState;
int vs_battle_aiCanFlyInDirection(struct vs_battle_aiState*, int);
struct vs_battle_aiState;
int func_800E0918(struct vs_battle_aiState*, int, int);
void vs_battle_aiRecoverMovement(vs_battle_aiState*);
void vs_battle_aiResolveDirectionalMovement(vs_battle_aiState*, int, int);
int vs_field_groundHeightAtTile(int, int);
int func_800E4764(vs_battle_aiState*, int*, int*);
void func_800E4B18(vs_battle_aiState*);
void vs_battle_aiReleaseActorAiReservations(vs_battle_aiState*);
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern D_800F5910_t* D_800F5910;
void vs_battle_aiSetTemporaryAwarenessIndicator(vs_battle_aiState* arg0, int arg1, int arg2);

int vs_battle_aiCanTraverseTileEdge(vs_battle_aiState*, unsigned int, unsigned int, int);
int vs_battle_aiDestinationOccupancy(vs_battle_aiState*, unsigned int);
unsigned int vs_core_mathSquaredVectorLength(int, int, int);
void vs_battle_aiDispatchAiLocomotionMode(void*, int, int);
int vs_battle_aiNextActorRandomMasked(int);
void vs_battle_aiProjectClearRadialDestination(vs_battle_aiState*, vs_battle_movementPosition, int, unsigned int);
int vs_battle_aiNextActorRandomMasked(int);

void vs_battle_aiResolveRouteGateExit(func_800DFA54_t* arg0, func_800DFA54_t* arg1);

void vs_battle_aiFindNearestRouteGateEntry(int arg0, int arg1, int arg2, func_800DFA54_t* arg3);

int vs_battle_aiFindRouteGateAtTile(int arg0, u_int arg1, int arg2);

typedef u_short routeEdgePair[2];

extern u_char D_800F175C[];
int vs_field_movingPlatformDwellTicks(int);
void vs_battle_aiSetAiContactOccupancy(int);

static inline u_int routeWord(routeEdgePair* pair) { return *(u_int*)pair; }

static inline u_short routeHalf(routeEdgePair* pair, int index) { return (*pair)[index]; }

static inline u_int routeTile(int z, int x)
{
    return (vs_nav.cells)[z][x];
}

static inline int routeSetArea(vs_battle_aiState* state, vs_battle_movementPosition position)
{
    state->area = routeTile(position.p.z, position.p.x) & 15;
    return 6;
}

int vs_battle_aiResolveNavigationAreaRoute(vs_battle_aiState* state, int index, int area);

typedef struct {
    unsigned char p0, p1, tested, p3, result[8];
} obstructionCache;
#define OBSTRUCTION_CACHE ((obstructionCache*)PC_PAD(0x3D0))
#define OBSTRUCTION_DIRECTIONS ((D_800F16EC_t*)PC_PAD(0x3EC))
#define OBSTRUCTION_STATES(id) ((vs_battle_aiState*)vs_nav.aiStates[id])

int vs_battle_aiFlightDirectionObstruction(vs_battle_aiState* state, int direction, int ahead);

#undef OBSTRUCTION_CACHE
#undef OBSTRUCTION_DIRECTIONS
#undef OBSTRUCTION_STATES

int vs_field_groundHeightAtTile(int, int);
typedef struct {
    unsigned char tested, valid;
} directionalCacheBits;
#define MOVEMENT_CHECK_CACHE ((directionalCacheBits*)PC_PAD(0x3D0))
#define MOVEMENT_CHECK_DIRECTIONS ((D_800F16EC_t*)PC_PAD(0x3EC))
#define MOVEMENT_CHECK_TILES (vs_nav.cells)
int vs_battle_aiCanFlyInDirection(vs_battle_aiState* state, int direction);

#undef MOVEMENT_CHECK_CACHE
#undef MOVEMENT_CHECK_DIRECTIONS
#undef MOVEMENT_CHECK_TILES

int vs_battle_aiLowestReachableGroundHeight(vs_battle_aiState* arg0, u_int arg1);

void vs_battle_aiMoveActorTowardDestinationTile();
void func_800E4B70();
unsigned int vs_core_mathSquaredTileDistance(unsigned int, unsigned int);
int vs_field_groundHeightAtPackedTile(unsigned int);
void func_800E4B10(void*);
int func_800E0918(vs_battle_aiState* state, int action, int mode);

typedef struct {
    char pad0[0x1E];
    short y;
    char pad20[0x3C];
    directionalCachePoint tile;
    char pad60[0x5DE];
    unsigned short height;
} movementStepActor;
int vs_battle_aiFlightDirectionObstruction(vs_battle_aiState*, int, int);
int vs_battle_aiMinimumAiGroundHeightAlongSegment(int, int, int, int);
void func_800E1238(vs_battle_aiState*, int);
#define MOVEMENT_STEP_TILES (vs_nav.cells)
#define MOVEMENT_STEP_RESULT (*(int*)PC_PAD(0x3C8))
void func_800E0A68(vs_battle_aiState* state, int action);

#undef MOVEMENT_STEP_TILES
#undef MOVEMENT_STEP_RESULT

void func_800E1238(vs_battle_aiState* arg0, int arg1);

void vs_battle_aiProjectClearRadialDestination(vs_battle_aiState* state, vs_battle_movementPosition source, int angle, unsigned int distance);

unsigned int vs_core_mathSquaredVectorLength(int, int, int);
unsigned int vs_core_mathSquaredTileDistance(unsigned int, unsigned int);
void vs_battle_aiMoveActorTowardDestinationTile(vs_battle_aiState*, int);
void func_800E153C(vs_battle_aiState* state, int unused);

void vs_battle_aiMoveAwayFromReference(vs_battle_aiState* state, int angle);

u_char vs_battle_aiFilterDirectionsByGroundHeight(u_char arg0, u_int arg1, int arg2);

#define TERRAIN_TILE_MASK (vs_nav.bitmap)
#define TERRAIN_MAP_WIDTH (*(unsigned char*)PC_PAD(0x3DC))
#define TERRAIN_MOVE_RESULT (*(int*)PC_PAD(0x3C8))
#define TERRAIN_TILES (vs_nav.cells)
struct vs_battle_aiState;
int vs_battle_aiResolveNavigationAreaRoute(struct vs_battle_aiState*, int, int);
void func_800E4BB8(vs_battle_aiState*);
void func_800E4B70(vs_battle_aiState*, int);
void vs_battle_aiMoveWithHeightFilteredDirections(vs_battle_aiState* state);

void vs_battle_aiUpdateNavigationAreaMovement(vs_battle_aiState* state);

#undef TERRAIN_TILE_MASK
#undef TERRAIN_MAP_WIDTH
#undef TERRAIN_MOVE_RESULT
#undef TERRAIN_TILES

#define WANDER_DIRECTIONS ((D_800F16EC_t*)PC_PAD(0x3EC))
#define WANDER_TILES (vs_nav.cells)

int vs_battle_aiIsAdjacentTileStepBlocked(unsigned int, int);
int vs_battle_aiCanWalkInDirection(vs_battle_aiState*, int);

static inline D_800F16EC_t* wanderDirection(int index)
{
    return WANDER_DIRECTIONS + index;
}

int vs_battle_aiChooseNextWanderTile(vs_battle_aiState* state, unsigned int tile, unsigned int maxCount, int arg3);

static inline int wanderRandomHeading(int direction)
{
    int offset = vs_battle_aiNextActorRandomMasked(512);
    int angle = direction << 9;
    offset += 0xFF00;
    return angle + offset;
}

static inline void wanderStartWalk(vs_battle_aiState* s)
{
    s->angle = -1;
    s->count = 0;
    s->cooldown = 60;
    s->mode++;
    vs_battle_aiRecoverMovement((vs_battle_aiState*)s);
}

void vs_battle_aiUpdateWanderMovement(vs_battle_aiState* state, unsigned int tile, unsigned int range);

#undef WANDER_DIRECTIONS
#undef WANDER_TILES

extern unsigned short (*D_800F58D8)[8];
void vs_battle_aiDispatchAiLocomotionMode(void*, int, int);
static inline unsigned int patrolWaypoint(unsigned short* row, int index)
{
    unsigned short* ptr = row;
    ptr += index;
    return *ptr;
}
void vs_battle_aiUpdateWaypointPatrol(vs_battle_aiState* state);

int func_800E4FB0(void*, int, int);
void vs_battle_aiDispatchAiLocomotionMode(void*, int, int);
int func_800E4180(void*, int, int, int);
void vs_battle_aiRefreshAiTargetTile(void*);

int vs_battle_aiUpdateTargetApproachMovement(vs_battle_aiState* s, int option);

int func_800E4180(void*, int, int, int);

int vs_battle_aiUpdateTargetFacing(vs_battle_aiState* context, int targetId);

typedef struct {
    char pad0[0x1C];
    short x, y, z;
} movementActorPosition;
void func_800E4B10(void*);
int vs_field_groundHeightFlagsAtTile(int, int);
void vs_battle_aiRecoverMovement(vs_battle_aiState* state);

typedef int (*movementPathTest)(vs_battle_aiState*, int);

int vs_battle_aiDirectionalBlockingActor(vs_battle_aiState*, int);
int vs_battle_aiCardinalPassabilityMask(vs_battle_aiState*, movementPathTest);

int vs_battle_aiFollowObstacleDetour(vs_battle_aiState* state, movementPathTest test, int mask);

typedef int (*movementApproachTest)(vs_battle_aiState*, int);
extern int (*D_800F1760[3][4])();
u_int func_800E49D8(vs_battle_aiState*, u_int, int, int);
int func_800E47E4(vs_battle_aiState*, u_int);
u_int vs_battle_aiFindAdjacentMovementDestination(vs_battle_aiState*, int, int, u_int);

void vs_battle_aiResolveDirectionalMovement(vs_battle_aiState* approach, int angleArg, int mode);

