#include "common.h"
#include "3A1A0.h"
#include <libgpu.h>
#include <inline_c.h>
#include <abs.h>
#ifdef ROOD_PC
#include "pc/src/psx_types.h"
#include "pc/src/widescreen.h"
#endif

void func_8009DF3C(int, int);
int func_800A152C(int, int, int);
int func_800A17BC(int, int, void*, int*);
int func_800A1C10(int arg0, int arg1, u_short* arg2, int arg3);
MATRIX* func_800A1DE8(int, int, MATRIX*);
void vs_actors_turnActorToAtRate(int, int, int);

extern MATRIX D_800F49B8;

int func_800A141C(int arg0, int arg1, int arg2, int arg3);

int func_800A152C(int arg0, int arg1, int arg2);

int vs_actors_boneTagValue(int arg0, int arg1, int arg2);

int func_800A1720(int arg0, int arg1, int* arg2, int* arg3);

int func_800A17BC(int arg0, int arg1, void* arg2, int* arg3);

int vs_actors_anchorPoint(int arg0, int arg1, SVECTOR* arg2, int arg3);

int func_800A1AF8(int arg0, int arg1, u_short* arg2, int arg3);

int func_800A1B9C(int arg0, int arg1, u_short* arg2, int arg3);

int func_800A1C10(int actorId, int bone, u_short* result, int endpoint);

typedef struct {
    char prefix[0x4C];
    SVECTOR position;
    SVECTOR offset;
    char padding[8];
    MATRIX matrix;
} boneMatrixScratch;

void vs_actors_transformRootOffset(D_800F4538_t*);
void vs_core_mathMultiplyMatrixIntoRight(MATRIX*, MATRIX*);

MATRIX* func_800A1DE8(int actorId, int bone, MATRIX* unused);

SVECTOR* func_800A4A24(int);
int vs_field_actorSupportHeight(int, int);
void vs_audio_playActorModelSound(vs_actors_ModelInstance*, int);
void vs_actors_playActorAnimation(int, int, int, int);

extern u_char vs_actors_jumpHangFrames[];
extern u_char vs_actors_jumpHeightAdjustments[];

void vs_actors_beginActorJumpToTile(D_800F4538_t* actor, u_char* target, int speed, short facing);

void vs_actors_playActorTurnAnimation(int arg0, int arg1);

void func_800A2574(int arg0, short arg1);

int vs_field_sampleActorSupportHeight(SVECTOR*, int, int, int);
void* vs_field_findCollisionCellAtPosition(SVECTOR*, int);
void vs_actors_updateActorJumpAndFall(int, void*);

void vs_actors_applyActorGroundOffset(D_800F4538_t* actor);

void vs_actors_turnActorToOverTime(int, int, int);

void vs_actors_applyActorFlightMovement(D_800F4538_t* actor);

void vs_actors_applyActorMovementVector(D_800F4538_t* arg0);

