#include "common.h"
#include <rand.h>
#include <abs.h>
#include <inline_c.h>
#include "146C.h"
#include "3A1A0.h"
#include "44F14.h"
#include "../../SLUS_010.40/main.h"
#include "pc/src/psx_types.h"

void func_8008EB04(int*, int*);
int vs_field_findTopGimmickAtTile(int, int, int);
/* Returns the animation record's guest address as an int (40564.c). */
void* vs_actors_findActorAnimationHeader(D_800F4538_t*, u_char, u_short**);
void vs_actors_alignPoseRotationBranches(SVECTOR*, SVECTOR*, int);
void vs_actors_blendActorPose(D_800F4538_unkC54*, D_800F4538_unkC54*, int, int, int);

extern u_char D_800E91A4[];
extern u_char D_800E9278[];

void vs_actors_textureOverlayMove(int index, u_char* arg);
void vs_field_applyGimmickInteractionCommand(D_800F4538_t* actor, int mode);
void vs_audio_playActorSoundPair(D_800F4538_t*, int, int);
int vs_actors_targetPoint(int, int, SVECTOR*, int);
void vs_audio_playActorSurfaceContactSound(int arg0, D_800F4538_t* arg1, int arg2);
void vs_actors_initializeActorAnimationPose(void*, void*);
int vs_effects_startEffectTrack(int);
int vs_actors_setTextureAnimation(int, int, int, int);
void func_800AE46C();
void func_800AE474();
void vs_actors_setActorBonePoseLock(D_800F4538_t* arg0, int arg1, int arg2);
void func_800CF8BC(void);
short vs_event_eventPlaybackState(void);
extern u_char vs_actors_animationEventLengths[];
extern u_char D_800E92DC[];
extern int D_800F1868;
extern u_short D_800E9A28[][4];

int vs_actors_runAnimationEvents(D_800F4538_t* actor, D_800F4538_unkC54* state, int mode);

void func_800AE46C(void);

void func_800AE474(void);

void vs_actors_restartActorAnimationEvents(D_800F4538_t* arg0);

void vs_audio_playActorModelSound(vs_actors_ModelInstance* arg0, int arg1);

void vs_audio_playActorModelSoundVariant(int arg0, int arg1);

void vs_audio_playActorSoundPair(D_800F4538_t* arg0, int arg1, int arg2);

void vs_audio_playGimmickSoundPair(void* arg0, int arg1, int arg2);

void vs_audio_playActorSurfaceContactSound(int arg0, D_800F4538_t* arg1, int arg2);

void vs_actors_blendActorPoseHalfway(D_800F4538_unkC54* dst, D_800F4538_unkC54* src, int count);

typedef struct {
    char prefix[0x3A];
    u_short angleX, angleY;
    char padding[0x16];
    SVECTOR offset;
    char padding2[8];
    MATRIX matrix;
    SVECTOR rotation;
} actorTransformScratch;
extern struct {
    char prefix[0x1FD0];
    int scales[3];
} D_800F2458;
void vs_actors_transformRootOffset(D_800F4538_t*);

void vs_actors_actorRootOffset(D_800F4538_t* actor);

extern MATRIX vs_render_cameraBasisMatrix;
extern MATRIX D_800F4910;
extern MATRIX D_800F49B8;

/* The block-scope static of the original function: the Zig reaches this storage through the views
 * (battle_statics.py, STATE.tsv). */
static MATRIX func_800AEF94__zero __attribute__((used));
static int func_800AEF94__reported __attribute__((used));
static int func_800AE47C__reported __attribute__((used));
void vs_core_mathMultiplyMatrixIntoRight(MATRIX*, MATRIX*);
typedef struct {
    char prefix[0x64];
    MATRIX matrix;
    char gap[8];
    VECTOR scale;
} cameraTransformScratch;

void vs_render_prepareActorCameraMatrices(MATRIX* source);

void func_800AEEC4(D_800F4538_t* arg0) __attribute__((unused));
void func_800AEEC4(D_800F4538_t* arg0);

void vs_actors_stepTextureAnimations(void*);
void vs_actors_stepTextureScrolling(void*);
void vs_actors_updatePalettePulse(void*, void*, int);
void vs_actors_updateActorAppearanceTransition(void*);
void vs_actors_updateActorRoute(int, D_800F4538_t*);
typedef struct objectAnimationState objectAnimationState;
void vs_field_updateGimmickMotion(objectAnimationState*, int);
void vs_actors_placeGimmick(D_800F45E0_t*, MATRIX*);
extern int D_800E9308;
extern u_char D_800F4B18, D_800F4B19;

struct frameScratch {
    int unused;
    int depth;
};

void vs_actors_updateAndDrawRoomModels(MATRIX* camera);

void vs_actors_updateActorAnimationBlend(D_800F4538_t* arg0);

void vs_actors_alignPoseRotationBranches(SVECTOR* dst, SVECTOR* src, int count);

void func_800AF960(D_800F45E0_t* arg0) __attribute__((unused));
void func_800AF960(D_800F45E0_t* arg0);

struct animationFrameScratch {
    char prefix[8];
    int first, last;
    int pad10; /* the guest pointer was here: a real pointer follows, 0x3EC + 0x14 .. 0x1C */
    u_char* data __attribute__((packed, aligned(4))); /* bytes of the animation data: any alignment */
};
PSX_ASSERT_SIZE(struct animationFrameScratch, 0x1c);
void vs_actors_decodeAnimationTrack(int, SVECTOR*, int);
void func_800AFA28(D_800F4538_t* actor, D_800F4538_unkC54* state, int mode);

void vs_actors_decodeAnimationTrack(int offset, SVECTOR* value, int rotation);

int vs_field_gimmickSupportHeight(int x, int z);
int vs_gte_rsqrt(int);
short vs_event_eventPlaybackState(void);
extern u_char vs_actors_supportingGimmickIndex;

typedef struct {
    int unk0;
    int unk4;
} intPair;

void func_800B002C(D_800F4538_t* actor, int arg1);

void vs_actors_transformRootOffset(D_800F4538_t* actor);

struct objectAnimationState {
    char prefix[8];
    int flags;
    char padC[3];
    u_char id, pad10;
    signed char delay;
    u_char parent, pad13;
    int pad14;
    short rotationTicks, mode;
    SVECTOR position;
    short rx, ry, rz, pad2A;
    short sx, sy, sz, pad32;
    short dx, dy, dz, orientation;
    short drx, dry, drz;
    char pad42[0x1A];
    u_char tileX, pad5D, tileZ, pad5F;
    char pad60[0x10C];
    u_char material, durability, pad16E, parentObject, frame;
};
int vs_field_updateGimmickReactionAnimation();
int vs_field_canPlaceGimmickAtPoint(int, int, int);
int vs_field_sampleActorSupportHeight(SVECTOR*, int, int, int);
void vs_battle_aiAddAiTileOccupant(void*);
void vs_battle_aiReleaseAiDestinationTile(int, int);
void vs_field_updateGimmickMotion(objectAnimationState* object, int unused);

typedef struct {
    char prefix[8];
    u_int flags;
    char paddingC[3];
    u_char objectId;
    char padding10[0xA];
    short mode;
    short x, y, z, padding22;
    short rotationX, rotationY, rotationZ, padding2A;
    short scaleX, scaleY, scaleZ, padding32, padding34;
    short deltaY, padding38, orientation;
    char padding3C[0xF0];
    char savedState[32];
    char state[32];
    char padding16C[2];
    u_char material, padding16F, frame;
} objectMotionContext;
extern u_char D_800E91AC[], D_800E91BC[], D_800E91CC[], D_800E91DC[];
extern u_char D_800E91EC[], D_800E91F0[], D_800E91FC[], D_800E9208[];
extern u_char D_800E9218[], D_800E9228[], D_800E9238[], D_800E9248[];
extern u_char D_800E9258[], D_800E9268[];
int vs_field_setGimmickOrientation(int, int);

int vs_field_updateGimmickReactionAnimation(objectMotionContext* object);

int vs_field_gimmickSupportHeightBelow(int arg0, int arg1, int arg2);

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
extern int sprintf(char*, const char*, ...);

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
extern void pc_log(const char*);
