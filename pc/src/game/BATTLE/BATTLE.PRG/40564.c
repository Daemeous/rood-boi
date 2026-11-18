#include "common.h"
#include "40564.h"
#include "146C.h"
#include "3A1A0.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include <abs.h>
#include "pc/src/psx_types.h"

typedef struct {
    u_char frame;
    u_char duration;
} vs_actors_TextureAnimationKey;

/* An animation block as it is on disc: offsets from the block's own start. */
typedef struct {
    u_int dataOffset;  /* the packed streams */
    u_int indexOffset; /* the table of frame entries, one byte per animation */
    u_short unk8[0];
} func_800AD494_t;
PSX_ASSERT_SIZE(func_800AD494_t, 0x8);

typedef struct {
    short x;
    short y;
    short z;
    short id;
} func_8008D2C0_t;

/* The block-scope static of the original function: the Zig reaches this storage through the views
 * (battle_statics.py, STATE.tsv). */
static func_8008D2C0_t func_800A4A24__platforms[4] __attribute__((used));

void pc_frame_move(unsigned* packet, const short* rect, int x, int y);
int vs_field_collectMovingPlatformPositions(func_8008D2C0_t*);
void vs_actors_playActorAnimation(int, int, int, int);
void vs_actors_targetPoint(int, int, SVECTOR*, int);
void func_800A1720(int, int, void*, int*);
int vs_field_sampleActorSupportHeight(SVECTOR*, int, int, int);
void vs_field_sampleActorCollisionPerimeter(void*, int);
void vs_actors_setActorTranslation(int arg0, SVECTOR* arg1, int arg2);
void vs_actors_setActorYawTurn(int, short, int);
void vs_actors_setActorRotation(int arg0, SVECTOR* arg1, int arg2);
void vs_actors_setActorTileTranslation(int, func_8006EBF8_t_fields*, int, int);
void vs_actors_setActorTileTurn(int, func_8006EBF8_t_fields*, int, int);
void vs_actors_textureFrameMove(D_800F4538_t*, int, int);

extern u_char vs_actors_sharedSequenceBankIndex;
extern vs_actors_TextureAnimationKey D_800E909C[][28];
extern vs_actors_TextureAnimationKey D_800E916C[][8];
extern u_char D_800F2450[];
extern void* D_800F4768;
extern char D_800F49DC;
extern u_char vs_battle_specialActionPitch;
extern int D_800F49F4;
extern u_char vs_actors_supportingGimmickIndex;
extern u_char vs_field_collisionProbeDirection;
extern short vs_field_platformContactSearchDepth;

_mpdRoomSection3* vs_field_findCollisionCellAtPosition(SVECTOR* arg0, int arg1);

int vs_field_isActorPositionUnoccupied(D_800F4538_t* arg0, SVECTOR* arg1);
typedef struct {
    char prefix[4];
    u_char excluded;
} actorCollisionExclusion;

int vs_field_isActorPositionUnoccupied(D_800F4538_t* actor, SVECTOR* position);

int vs_field_canActorSeparateFromOverlaps(D_800F4538_t* actor, SVECTOR* motion);

int vs_field_findTopGimmickAtTile(int arg0, int arg1, int arg2);

int vs_field_gimmickSupportHeight(int arg0, int arg1);

int vs_field_movingPlatformContactHeight(int arg0, int arg1, int arg2, int arg3);

// Inlined copy of 3A1A0 func_800A4A24.
// BUG: returns the address of a stack variable
static inline func_8008D2C0_t* func_800A4A24(int id)
{
#ifdef ROOD_PC
    /* See 3A1A0.c: keep the returned array readable on the host. */
    static func_8008D2C0_t platforms[4];
#else
    func_8008D2C0_t platforms[4];
#endif
    int count;
    int i;
    id -= 2;

    count = vs_field_collectMovingPlatformPositions(platforms);

    for (i = 0; i < count; ++i) {
        if (platforms[i].id == id) {
            return &platforms[i];
        }
    }
#ifdef ROOD_PC
    /* No matching platform: see 3A1A0.c (reads of PS1 address 0). */
    return (func_8008D2C0_t*)rood_null_page;
#else
    return NULL;
#endif
}

int func_800A9530(D_800F4538_t* actor, SVECTOR* offset);

void vs_actors_moveActorToTileAtRate(int arg0, func_8006EBF8_t_fields* arg1, int arg2, int arg3);

short vs_field_interpolatedFloorHeight(int, int);

static inline int scanFloor(int arg0, int arg1)
{
    int var_t0;
    int i;

    var_t0 = 0xBB8;
    vs_actors_supportingGimmickIndex = 0;

    for (i = 0; i < 16; ++i) {

        D_800F45E0_t* temp_v1 = vs_field_gimmickInstances[i];

        if (temp_v1 == NULL) {
            continue;
        }

        if (!temp_v1->unk8_0 && (temp_v1->unk5C == arg0) && (temp_v1->unk5E == arg1)
            && !temp_v1->unk9_0 && (temp_v1->unk1A != 0xFE)
            && (temp_v1->unk1E < var_t0)) {

            var_t0 = temp_v1->unk1E;
            vs_actors_supportingGimmickIndex = i;
        }
    }

    var_t0 -= 128;

    if (var_t0 > 0) {
        var_t0 = 0;
    }

    return var_t0;
}

void vs_actors_moveActorToPositionAtRate(int arg0, SVECTOR* arg1, int arg2, int arg3);

void vs_actors_translateActorToTileAtRate(int arg0, func_8006EBF8_t_fields* arg1, int arg2);

void vs_actors_turnActorTowardTileAtRate(int arg0, func_8006EBF8_t_fields* arg1, int arg2);

void vs_actors_translateActorByAtRate(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_translateActorToAtRate(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_turnActorByAtRate(int arg0, short arg1, int arg2);

void vs_actors_turnActorToAtRate(int arg0, short arg1, int arg2);

void vs_actors_rotateActorByAtRate(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_rotateActorToAtRate(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_setActorTileTranslation(int arg0, func_8006EBF8_t_fields* arg1, int arg2, int arg3);

void vs_actors_turnActorTowardTileOverTime(int arg0, func_8006EBF8_t_fields* arg1, int arg2);

void vs_actors_setActorTileTurn(int arg0, func_8006EBF8_t_fields* arg1, int arg2, int arg3);

void vs_actors_setActorMotion(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_translateActorToOverTime(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_setActorTranslation(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_turnActorToOverTime(int arg0, u_short arg1, int arg2);

void vs_actors_turnActorToAtAngularSpeed(int arg0, short arg1, int arg2);

void vs_actors_setActorYawTurn(int arg0, short arg1, int arg2);

void vs_actors_setActorRotation(int arg0, SVECTOR* arg1, int arg2);

void vs_actors_scaleActorToOverTime(int arg0, SVECTOR* arg1, int arg2);

void func_800AACDC(void);

int vs_actors_setTextureAnimation(int actorId, int index, int value, int mode);

int vs_actors_stepTextureAnimations(D_800F4538_t* actor);

extern u_char vs_actors_textureSlotOrigins[];
extern u_char vs_actors_textureSlotRowOffsets[];

typedef struct {
    u_char overlay;
    u_char x;
    u_char y;
    u_char columns;
} func_800AB098_t;

void vs_actors_textureFrameMove(D_800F4538_t* actor, int index, int frame);

void vs_actors_restartsTextureFrames(int arg0);

void vs_actors_textureOverlayMove(int index, u_char* arg);

void vs_actors_stepTextureScrolling(void* actor);

void vs_actors_updatePalettePulse(u_char* actor, u_char* object, int arg2);

void vs_actors_anchorPoint(int, int, SVECTOR*, int);
void vs_field_applyActorWallRepulsion(void*, void*);
int vs_actors_stepRgbTowardTarget(u_char* arg0, u_char* arg1, int arg2);
int vs_actors_applyPaletteTransition(u_short* colors, int count, int amount, int mode, u_short* reference);
void vs_actors_updateActorJumpAndFall(int arg0, D_800F4538_t* arg1);
extern u_char D_800F4B19;

void vs_actors_updateActorAppearanceTransition(D_800F45E0_t* arg0);

int vs_actors_stepRgbTowardTarget(u_char* arg0, u_char* arg1, int arg2);

int vs_actors_applyPaletteTransition(u_short* colors, int count, int amount, int mode, u_short* reference);

void vs_actors_startActorAppearanceTransition(int arg0, int arg1);

void vs_actors_setActorBonePoseLock(D_800F4538_t* arg0, int arg1, int arg2);

void vs_actors_clearActorBonePoseLocks(D_800F4538_t* arg0);

void vs_actors_updateActorRoute(int arg0, D_800F4538_t* arg1);

void func_8006F450(SVECTOR*);
void vs_field_setMovingPlatformActor(int, int);
short vs_field_ceilingHeightAt(int, int);
void vs_actors_saveActorGroundedTransform(D_800F45E0_t*);
void vs_audio_playActorSurfaceContactSound(int, D_800F4538_t*, int);
void vs_battle_aiReleaseAiDestinationTile();
void func_800E6764(int);
void vs_battle_aiReleaseActorAiContactOccupancy(int);
extern u_int* vs_field_collisionProbeTile;
extern u_char D_800E9278[];
extern u_char D_800E92E8[];
extern u_char D_800E92F0[];
extern u_char D_800E92F8[];
extern u_char D_800E9300[];

typedef struct {
    u_int unk0 : 24;
    u_int timer : 8;
} func_800AC690_timer_t;

typedef struct {
    u_int unk0 : 16;
    u_int state : 3;
    u_int unk13 : 13;
} func_800AC690_flags_t;

typedef struct {
    u_int contact : 4;
    u_int unk4 : 28;
} func_800AC690_contact_t;

void vs_actors_updateActorJumpAndFall(int arg0, D_800F4538_t* actor);

typedef struct {
    int unk0;
    int unk4;
} func_800ACF54_t;

void vs_actors_saveActorGroundedTransform(D_800F45E0_t* arg0);

inline void vs_actors_decodeInitialPoseVector(SVECTOR* arg0, u_char* arg1, int arg2);

void vs_actors_restartActorAnimationEvents(D_800F4538_t*);

void vs_actors_initializeActorAnimationPose(void* arg0, void* arg1);

void* vs_actors_findActorAnimationHeader(void* arg0, int arg1, u_short** arg2);

void vs_audio_actorSoundPanAndVolume(int arg0, int* arg1, int* arg2);

