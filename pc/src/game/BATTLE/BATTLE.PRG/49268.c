#include "common.h"
#include <rand.h>
#include <abs.h>
#include <inline_c.h>
#include "146C.h"
#include "3A1A0.h"
#include "44F14.h"
#include "../../SLUS_010.40/main.h"
#include "pc/src/psx_types.h"

extern struct {
    char prefix[0x1FD0];
    int scales[3];
} D_800F2458;

typedef struct {
    char prefix[0x34];
    u_short tpage, clut;
    char pad38[0x14];
    SVECTOR translation, position;
    psx_ptr(MATRIX) parent;
    int pad60;
    MATRIX matrix;
    SVECTOR rotation;
    VECTOR scale;
    psx_ptr(char) model;
} weaponTransformScratch;
PSX_ASSERT_SIZE(weaponTransformScratch, 0xa0);
extern MATRIX vs_actors_skeletonPose[];
extern MATRIX D_800F3C78;
void vs_core_mathBuildEulerRotationMatrixQuadrant(SVECTOR*, MATRIX*);
void vs_core_mathPremultiplyRotationMatrix(MATRIX*, MATRIX*);
void vs_core_mathScaleMatrixColumns(MATRIX*, VECTOR*);
int vs_render_projectModelVertices(void*, MATRIX*, int, void*);
void vs_render_drawModelGouraudPolygons(void*, void*, void*, void*);
void vs_render_drawModelGouraudPolygonsWithOverrides(void*, void*, void*, void*);
int vs_actors_anchorPoint(int, int, SVECTOR*, int);

void vs_actors_placeGimmick(D_800F45E0_t* object, MATRIX* camera);

typedef struct {
    char prefix[0x20];
    MATRIX matrix;
} weaponMatrix;
typedef struct {
    char prefix[0xA0];
    SVECTOR rotation;
} weaponRotation;
extern MATRIX vs_actors_skeletonPose[];
void vs_core_mathBuildEulerRotationMatrixQuadrant(SVECTOR*, MATRIX*);
void vs_core_mathPremultiplyRotationMatrix(MATRIX*, MATRIX*);
void vs_core_mathScaleMatrixColumns(MATRIX*, VECTOR*);
int vs_render_projectModelVertices(void*, MATRIX*, int, void*);
short vs_event_eventPlaybackState(void);
void func_800B516C(void*, void*, void*, void*);
void vs_render_drawModelFlatPolygons(void*, void*, void*, void*);

void vs_actors_buildWeaponBoneChain(vs_battle_wepModels_t* weapon, MATRIX* source);

typedef struct {
    char prefix[0x4C];
    SVECTOR translation;
} boneTransformScratch;
extern MATRIX vs_actors_skeletonPose[];
void vs_core_mathPremultiplyRotationMatrix(MATRIX*, MATRIX*);
