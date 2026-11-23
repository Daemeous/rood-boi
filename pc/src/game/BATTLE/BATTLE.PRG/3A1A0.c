#include "common.h"
#include "30DB0.h"
#include "3A1A0.h"
#include "../../SLUS_010.40/main.h"
#include <abs.h>
#include "pc/src/psx_types.h"

typedef struct {
    int unk0;
    int pad04[5];
    int pad18; /* the guest pointer to the probed actor was here: it is a real pointer now, after the struct */
    short unk1C[18];
    int unk40;
    D_800F45E0_t* actor __attribute__((packed, aligned(4))); /* 0x44: ends at 0x3BC + 0x4C, 8 bytes past the 1 KB pad */
} D_1F8003BC_t;
PSX_ASSERT_SIZE(D_1F8003BC_t, 0x4c);

typedef struct {
    short directionX;
    short unk2;
    short directionZ;
} vs_actors_FallDirection;

typedef struct {
    short unk0;
    short unk2;
    short unk4;
    short unk6;
} func_8008D2C0_t;

/* The block-scope static of the original function: the Zig reaches this storage through the views
 * (battle_statics.py, STATE.tsv). */
static func_8008D2C0_t func_800A4A24__sp10[4] __attribute__((used));

typedef struct {
    char unk0[6];
    short unk6;
} func_800A6660_t;

void func_800A29A0(D_800F4538_t*);
void vs_actors_saveActorGroundedTransform(D_800F4538_t*);
void vs_battle_aiReleaseAiDestinationTile();
u_int vs_actors_translateActorToTileAtRate(u_char, void*, int);
void vs_actors_actorRootOffset(D_800F4538_t*);
void vs_render_prepareActorCameraMatrices(MATRIX*);
void vs_actors_drawActorModel(void*, MATRIX*, int);
int vs_field_collectMovingPlatformPositions(func_8008D2C0_t*);
short vs_field_ceilingHeightAt(int arg0, int arg1);
D_800F4538_t* vs_field_findActorOccupyingTile(int, int, int, int);
short vs_field_floorHeightAt(int, int);
short vs_field_interpolatedFloorHeight(int, int);
u_int* vs_field_findCollisionCellAtPosition(SVECTOR*, int);
int vs_field_gimmickSupportHeightBelow(int, int, int);
int vs_field_gimmickSupportHeight(int, int);
int vs_field_movingPlatformContactHeight(int, int, int, int);
void vs_actors_turnActorToOverTime(int, short, int);
int vs_field_isActorPositionUnoccupied(D_800F45E0_t*, SVECTOR*);
void vs_field_updateGimmickMotion(D_800F45E0_t*, int);
int vs_field_sampleActorSupportHeight(SVECTOR*, int, int, int);
int vs_battle_aiRoomNavigationCeilingHeight(void);
int vs_actors_actorFacingDeltaToPoint(D_800F4538_t*, SVECTOR*);

extern u_int* vs_field_collisionProbeTile;
extern int D_800F49F4;
extern u_char vs_actors_supportingGimmickIndex;
extern u_char vs_field_collisionProbeDirection;
extern SVECTOR vs_field_collisionProbePosition;
extern char D_800F4B18;
extern u_char D_800E9278[];

void func_800A29A0(D_800F4538_t* arg0);

void func_800A2C48(D_800F4538_t* arg0);

void vs_actors_stepActorTransitionDisplacement(D_800F4538_t*);
void vs_actors_turnActorToAtRate(int, short, int);
void vs_actors_translateActorToOverTime(int, SVECTOR*, int);

void func_800A2CD4(D_800F4538_t* arg0);

void vs_actors_stepActorTransitionDisplacement(D_800F4538_t* arg0);

void vs_actors_setActorFallDestination(D_800F4538_t* arg0, vs_actors_FallDirection* arg1);

inline int vs_actors_findActorAtFlightPosition(int, SVECTOR*);
void vs_actors_separateActorFlightPlacement(int, SVECTOR*);
int vs_field_actorSupportHeight(int, int);
func_8008D2C0_t* func_800A4A24(int);
void vs_actors_saveActorGroundedTransform(D_800F4538_t*);

void vs_actors_placeActorInRoom(int actorId, vs_field_ActorPlacement* placement, int height, int mode);

inline int vs_actors_findActorAtFlightPosition(int arg0, SVECTOR* arg1);

void vs_actors_separateActorFlightPlacement(int arg0, SVECTOR* arg1);

int vs_field_actorSupportHeight(int arg0, int arg1);

void vs_field_snapRoomActorsToSupport(void);

void vs_actors_actorSavedPlacement(int arg0, int arg1, func_8006EBF8_t* arg2);

int vs_actors_findNearestActorAhead(int arg0, int arg1, int arg2);

extern int vs_field_gimmickInteractionDirection;
int vs_field_roomCeilingHeight(int, int);
int vs_field_canPlaceGimmickAtPoint(int, int, int);
int vs_field_findTopGimmickAtTile(int, int, int);

int vs_field_findInteractableGimmickAhead(int actorId);

int vs_field_roomCeilingHeight(int arg0, int arg1);
D_800F4538_t* func_800A3C00(D_800F45E0_t* arg0, u_int arg1);
int vs_field_canPlaceGimmickAtPoint(int x, int z, int minimumHeight);

int vs_field_roomCeilingHeight(int arg0, int arg1);

D_800F4538_t* func_800A3C00(D_800F45E0_t* arg0, u_int arg1);

D_800F4538_t* vs_field_findActorOccupyingTile(int arg0, int arg1, int arg2, int arg3);

int vs_field_canPlaceGimmickAtPoint(int x, int z, int minimumHeight);

extern int vs_field_interactingGimmickId;
extern u_char D_800E8FD0[];
void vs_audio_playActorSoundPair(D_800F4538_t*, int, int);
void vs_battle_aiReserveAiDestinationTile(int, int);
void vs_actors_turnActorTowardTileOverTime(int, func_8006EBF8_t_fields*, int);

int vs_field_beginGimmickInteraction(int actorId);

int vs_actors_anchorPoint(int, int, SVECTOR*, int);
void vs_battle_aiRemoveAiTileOccupant(D_800F45E0_t*);

void vs_field_applyGimmickInteractionCommand(D_800F4538_t* actor, int mode);

void vs_actors_updateActorPlatformContact(D_800F4538_t*, int);
int vs_field_isHangingActorObstructed(D_800F4538_t*);
int vs_field_checkPlatformActorObstruction(int contact, SVECTOR* point);

void vs_field_sampleActorCollisionPerimeter(D_800F4538_t*, int);
typedef struct {
    char prefix[0x2C];
    u_char contact, nextContact;
} collisionScratchView;

int vs_field_isHangingActorObstructed(D_800F4538_t* actor);

int vs_field_areGimmicksMoving(void);

void func_800A4828(int arg0, MATRIX* arg1);

void vs_actors_setDirectMovement(int, int, int);
typedef struct {
    unsigned mode : 2;
    unsigned fast : 1;
    unsigned reserved : 29;
} vs_battleMovementModeFlags;
void vs_actors_beginActorJumpMovement(int index, int direction, int distance);

inline func_8008D2C0_t* func_800A4A24(int arg0);

void vs_field_setMovingPlatformActor(int, int);
void vs_battle_aiClearAiContactOccupancy(int);
void vs_battle_aiSetAiContactOccupancy(int);

void vs_actors_updateActorPlatformContact(D_800F4538_t* actor, int mode);

void vs_actors_updateRoomActorMovement(void);

void vs_actors_applyActorGroundOffset(D_800F4538_t*); /* extern */
void vs_actors_applyActorFlightMovement(D_800F4538_t*); /* extern */
void vs_actors_applyActorMovementVector(D_800F4538_t*); /* extern */
void func_800A2CD4(D_800F4538_t*); /* extern */
int vs_field_isPointOnMovingPlatform(int, SVECTOR*); /* extern */
void vs_actors_clearActorMovementRequest(D_800F4538_t*); /* extern */
int vs_actors_moveActorWithCollision(int, int, int, SVECTOR*); /* extern */
void vs_actors_updateActorJumpAndFall(int, D_800F4538_t*); /* extern */
void vs_battle_aiClearActorAiNavigationReset(u_char); /* extern */

void vs_actors_updateActorMovement(int arg0);

int vs_field_isPointOnMovingPlatform(int arg0, SVECTOR* arg1);

void vs_actors_clearActorMovementRequest(D_800F4538_t* arg0);

extern short vs_field_collisionProbeHeight;
typedef struct {
    int words[2];
} probePosition;
extern int D_800E90BC;
extern u_char D_800E90C3[];
extern u_char D_800E90C4;
extern u_char D_800E90C6;
extern u_char D_800E91AC[];
extern u_char vs_field_areMovingPlatformsPaused[];
int vs_field_canActorHangFromLedge(D_800F4538_t*, SVECTOR*, SVECTOR*);
int vs_field_hasActorLedgeSideSupport(D_800F4538_t*);
int vs_actors_updateActorLocomotionAnimation(D_800F4538_t*, int, int);
int vs_field_resolveActorTerrainMotion(D_800F4538_t*, SVECTOR*, int*, int);
int func_800A9530(D_800F4538_t*, SVECTOR*);
void vs_actors_setActorMotion(int, SVECTOR*, int);
void vs_actors_setActorTranslation(int, SVECTOR*, int);
void vs_audio_playActorModelSound(vs_actors_ModelInstance*, int);
void vs_actors_updateActorSteering(D_800F4538_t*, int, func_800A6660_t*);

int vs_actors_moveActorWithCollision(int index, int direction, int distance, SVECTOR* input);

void vs_actors_updateActorSteering(D_800F4538_t* arg0, int arg1, func_800A6660_t* arg2);

int vs_field_canActorHangFromLedge(D_800F4538_t* actor, SVECTOR* offset, SVECTOR* motion);

int vs_field_hasActorLedgeSideSupport(D_800F4538_t* arg0);

extern u_char D_800E90CC[];
extern u_char D_800E90D0[];
void* vs_actors_findActorAnimationHeader(void*, int, u_short**);

int vs_actors_updateActorLocomotionAnimation(D_800F4538_t* actor, int speed, int direction);

int vs_field_sampleActorSupportHeight(SVECTOR* arg0, int arg1, int arg2, int arg3);

typedef struct {
    char prefix[0x1C];
    short heights[8];
    u_char blocked[8];
    u_char attributes[8];
    u_char centerAttribute;
} ActorCollisionSamples;
void vs_field_sampleActorCollisionPerimeter(D_800F4538_t* actor, int diagonal);

typedef struct {
    char prefix[0x2C];
    u_char contacts[8];
} collisionResponseScratch;

void vs_field_applyActorWallRepulsion(D_800F4538_t* actor, SVECTOR* motion);

int vs_field_classifyActorTerrainProbe(SVECTOR* arg0, int arg1);
int vs_field_canActorSeparateFromOverlaps(D_800F4538_t* actor, SVECTOR* motion);

int vs_field_resolveActorTerrainMotion(D_800F4538_t* actor, SVECTOR* motion, int* result, int probeOnly);

int vs_field_classifyActorTerrainProbe(SVECTOR* arg0, int arg1);

