/* Host-only actor transform preparation, recovered from the retained decomp
 * candidate r12-d/b28/v12.c (see docs/decomp-blockers.md). The original matching
 * build keeps its assembly. This source is not claimed to match the PS1 binary;
 * validate rendered actors and less common animation branches as the port grows. */
#include "common.h"
#include <rand.h>
#include <abs.h>
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/44F14.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/30DB0.h"
#include "pc/src/psx_types.h"
#include "pc/src/gte_value.h"

/* The object's size, for tests that read a whole object. */
const unsigned pc_object_size = sizeof(D_800F4538_t);
/* The offsets tests mask or read in an object (ui_equip_smoke): the bone matrices, unk6FC, the animation buffers. */
const unsigned pc_object_offset_bones = __builtin_offsetof(D_800F4538_t, bones);
const unsigned pc_object_offset_bones_end = __builtin_offsetof(D_800F4538_t, bones) + sizeof(((D_800F4538_t*)0)->bones);
const unsigned pc_object_offset_unk6FC = __builtin_offsetof(D_800F4538_t, unk6FC);
const unsigned pc_object_offset_unk704 = __builtin_offsetof(D_800F4538_t, unk704);
const unsigned pc_object_offset_unk11A4 = __builtin_offsetof(D_800F4538_t, unk11A4);
const unsigned pc_object_offset_unk17FD = __builtin_offsetof(D_800F4538_t, unk17FD);
/* The movement request func_800B64CC makes (analog_move_smoke): command, angle word, speed. */
const unsigned pc_object_offset_unk1848 = __builtin_offsetof(D_800F4538_t, unk1848);

typedef struct {
    char prefix[0x30];
    CVECTOR color;
    u_short tpage;
    u_short unk36;
    u_short unk38;
    u_short angleX, angleY;
    char padding[0xE];
    SVECTOR translation;
    SVECTOR offset;
    psx_ptr(MATRIX) parent;
    int pad60;
    MATRIX matrix;
    SVECTOR rotation;
    VECTOR scale;
    psx_ptr(D_800F4538_unk68) model;
} actorTransformScratch;
PSX_ASSERT_SIZE(actorTransformScratch, 0xa0);
extern struct {
    char prefix[0x1800];
    MATRIX boneMatrices[42];
    VECTOR boneScales[42];
} D_800F2458;
extern MATRIX vs_actors_skeletonPose[];

typedef struct {
    int unk0;
    int unk4;
} actorIdentity;

typedef struct {
    u_char header[8];
    u_int flags;
} actorFlags;

typedef struct {
    u_short step;
    u_short angle;
} boneSpin;

extern char D_800E8FC0;
void vs_core_mathBuildEulerRotationMatrixQuadrant(SVECTOR*, MATRIX*);
void vs_core_mathPremultiplyRotationMatrix(MATRIX*, MATRIX*);
void vs_core_mathScaleMatrixColumns(MATRIX*, VECTOR*);
int vs_render_projectModelVertices(void*, MATRIX*, int, void*);
short vs_event_eventPlaybackState(void);
void func_800B516C(void*, void*, void*, void*);
void vs_render_drawModelFlatPolygons(void*, void*, void*, void*);
void vs_render_drawModelGouraudPolygons(void*, void*, void*, void*);
void func_800B4B14(void*);
void vs_render_drawModelGouraudPolygonsWithOverrides(void*, void*, void*, void*);
void func_8008C458(int, int, void*, void*);
int vs_gte_rsqrt(int);

typedef struct {
    char prefix[0x4C];
    SVECTOR translation;
} boneTransformScratch;
static inline void vs_actors_applyLockedBoneTransform(void* actorData, MATRIX* parent, int bone, int saved)
{
    D_800F4538_t* actor = actorData;
    boneTransformScratch* scratch = (void*)PC_PAD(0x35C);
    MATRIX* matrix = (void*)PC_PAD(0x3C0);
    MATRIX* savedMatrix = (MATRIX*)actor->unk1890[saved - 1]; /* the saved bone matrices follow unk1890 */
    vs_gte_view view;
    vs_gte_result transformed;
    *matrix = *savedMatrix;
    vs_core_mathPremultiplyRotationMatrix(parent, matrix);
    view = vs_gte_view_of(parent, VS_GTE_NO_SCREEN);
    scratch->translation.vx = (u_short)savedMatrix->t[0];
    scratch->translation.vy = (u_short)savedMatrix->t[1];
    scratch->translation.vz = (u_short)savedMatrix->t[2];
    transformed = vs_gte_transform(&view, &scratch->translation);
    matrix->t[0] = transformed.mac[1];
    matrix->t[1] = transformed.mac[2];
    matrix->t[2] = transformed.mac[3];
    D_800F2458.boneMatrices[bone] = *matrix;
    actor->bones[bone] = *matrix;
}

void func_800B28A8(void* actorData, MATRIX* parent, char arg2)
{
    D_800F4538_t* actor = actorData;
    actorTransformScratch* scratch = (void*)PC_PAD(0x35C);
    D_800F4538_t* source;
    int _[2] __attribute__((unused));
    actorIdentity savedIdentity;
    int i;
    int value;
    int parentBone;
    int dx;
    int dz;
    int angle;
    int length;
    vs_view_t* frame;
    int blend;
    u_int flags;
    vs_gte_view view;
    vs_gte_result transformed;

    psx_set(scratch->model, actor->unk0.unk68);
    view = vs_gte_view_of(parent, VS_GTE_NO_SCREEN);
    *(int*)&scratch->translation = *(int*)&scratch->offset;
    *(int*)&scratch->translation.vz = *(int*)&scratch->offset.vz;
    transformed = vs_gte_transform(&view, &scratch->translation);
    scratch->rotation.vx = actor->unk704.unk0[0].vx >> 1;
    scratch->rotation.vy = actor->unk704.unk0[0].vy >> 1;
    scratch->rotation.vz = actor->unk704.unk0[0].vz >> 1;
    scratch->matrix.t[0] = transformed.mac[1];
    scratch->matrix.t[1] = transformed.mac[2];
    scratch->matrix.t[2] = transformed.mac[3];
    scratch->rotation.vx += actor->unk0.unk24;
    scratch->rotation.vy += actor->unk0.facing;
    scratch->rotation.vz += actor->unk0.unk28;

    if (VS_OBJECT_FLAGS5AC(actor) & 0x200000) {
        if ((short)vs_event_eventPlaybackState() == 4 || (((actorFlags*)actor)->flags & 4)) {
            if (arg2 == 0) {
                flags = VS_OBJECT_FLAGS5AC(actor);
                if (!(flags & 0x400000)) {
                    value = actor->unk1802[0] + 1;
                    if (actor->unk1802[1] < value) {
                        VS_OBJECT_FLAGS5AC(actor) = flags | 0x400000;
                        value = actor->unk1802[1];
                    }
                } else {
                    value = actor->unk1802[0] - 1;
                    if (value < 0) {
                        VS_OBJECT_FLAGS5AC(actor) = flags & ~0x400000;
                        value = 0;
                    }
                }
                actor->unk1802[0] = value;
            }
        }
        scratch->rotation.vy += actor->unk1802[0] >> 1;
    }

    *(int*)&scratch->rotation &= 0x0FFF0FFF;
    *(int*)&scratch->rotation.vz &= 0xFFF;
    vs_core_mathBuildEulerRotationMatrixQuadrant(&scratch->rotation, &scratch->matrix);
    vs_core_mathPremultiplyRotationMatrix(parent, &scratch->matrix);

    if (!(((actorFlags*)actor)->flags & 0x2000)) {
        scratch->scale.vx = actor->unk183C;
        scratch->scale.vy = actor->unk183C;
        scratch->scale.vz = actor->unk183C;
        vs_core_mathScaleMatrixColumns(&scratch->matrix, &scratch->scale);
    }
    if (*(int*)&actor->unk0.unk2C != 0x10001000 || actor->unk0.unk30 != 0x1000) {
        scratch->scale.vx = actor->unk0.unk2E;
        scratch->scale.vy = actor->unk0.unk30;
        scratch->scale.vz = actor->unk0.unk2C;
        vs_core_mathScaleMatrixColumns(&scratch->matrix, &scratch->scale);
    }
    *vs_actors_skeletonPose = scratch->matrix;

    D_800F2458.boneScales[0].vx = scratch->scale.vx = actor->unk704.unk150[0].vx;
    D_800F2458.boneScales[0].vy = scratch->scale.vy = actor->unk704.unk150[0].vy;
    D_800F2458.boneScales[0].vz = scratch->scale.vz = actor->unk704.unk150[0].vz;
    if (scratch->scale.vx != 64 || scratch->scale.vy != 64 || scratch->scale.vz != 64) {
        scratch->scale.vx <<= 6;
        scratch->scale.vy <<= 6;
        scratch->scale.vz <<= 6;
        vs_core_mathScaleMatrixColumns(&scratch->matrix, &scratch->scale);
    }
    actor->bones[0] = scratch->matrix;
    scratch->translation.vx = scratch->translation.vy = 0;

    for (i = 1; i < actor->unk0.nBones; ++i) {
        value = psx_get(scratch->model)->armatures[i].unkC;
        if (value != 0) {
            vs_actors_applyLockedBoneTransform(actor, parent, i, value);
        } else {
            parentBone = psx_get(scratch->model)->armatures[i].unk4;
            psx_set(scratch->parent, &actor->bones[parentBone]);
            view = vs_gte_view_of(psx_get(scratch->parent), VS_GTE_NO_SCREEN);
            length = (u_short)psx_get(scratch->model)->armatures[parentBone].unk0;
            scratch->translation.vy = 0;
            scratch->translation.vz = 0;
            scratch->translation.vx = -length;
            transformed = vs_gte_transform(&view, &scratch->translation);
            scratch->rotation.vx = actor->unk704.unk0[i].vx >> 1;
            scratch->rotation.vy = actor->unk704.unk0[i].vy >> 1;
            scratch->rotation.vz = actor->unk704.unk0[i].vz >> 1;
            value = (i + psx_get(scratch->model)->armatures)->unk8;

            switch (value) {
            case 0:
                break;
            case 1:
                scratch->rotation.vx -= scratch->angleX >> 1;
                scratch->rotation.vz += scratch->angleX >> 3;
                break;
            case 2:
                scratch->rotation.vx -= scratch->angleX >> 1;
                scratch->rotation.vy -= scratch->angleX >> 1;
                scratch->rotation.vz += scratch->angleX >> 3;
                if (scratch->angleX != 0) {
                    scratch->rotation.vy += scratch->angleY >> 3;
                } else {
                    scratch->rotation.vy -= scratch->angleY >> 3;
                }
                scratch->rotation.vz += scratch->angleY >> 2;
                break;
            case 3:
            case 4:
            case 5:
            case 6:
                if (((actorFlags*)actor)->flags & 4) {
                    boneSpin* spin = (boneSpin*)&actor->unk1864.unk10 + value;
                    scratch->rotation.vx = spin->angle += spin->step;
                }
                break;
            case 7:
                frame = &vs_view;
                scratch->rotation.vx = frame->camera.angles.vx;
                scratch->rotation.vy = frame->camera.angles.vy - 0x400;
                scratch->rotation.vz = frame->camera.angles.vz;
            case 8:
            case 9:
                frame = &vs_view;
                dx = frame->camera.position.vx / ONE - scratch->offset.vx;
                dz = frame->camera.position.vz / ONE - scratch->offset.vz;
                if (dx == 0 && dz == 0) {
                    angle = 0;
                } else {
                    angle = -ratan2(dz, dx) & 0xFFF;
                }
                scratch->rotation.vy = angle;
                if (value == 8) {
                    dx = frame->camera.position.vx / ONE - scratch->offset.vx;
                    dz = frame->camera.position.vz / ONE - scratch->offset.vz;
                    dx *= dx;
                    if (dx < 0) {
                        dx = -dx;
                    }
                    dz *= dz;
                    if (dz < 0) {
                        dz = -dz;
                    }
                    dx = vs_gte_rsqrt(dx + dz);
                    dz = frame->camera.position.vy / ONE;
                    angle = scratch->offset.vy;
                    scratch->rotation.vz = ratan2(dz - angle, dx) & 0xFFF;
                } else {
                    scratch->rotation.vz = 0;
                }
                break;
            }

            scratch->matrix.t[0] = transformed.mac[1];
            scratch->matrix.t[1] = transformed.mac[2];
            scratch->matrix.t[2] = transformed.mac[3];
            *(int*)&scratch->rotation &= 0x0FFF0FFF;
            *(int*)&scratch->rotation.vz &= 0xFFF;
            if (value >= 7) {
                RotMatrixYXZ_gte(&scratch->rotation, &scratch->matrix);
                vs_core_mathPremultiplyRotationMatrix(parent, &scratch->matrix);
                scratch->scale.vx = actor->unk183C;
                scratch->scale.vy = actor->unk183C;
                scratch->scale.vz = actor->unk183C;
                vs_core_mathScaleMatrixColumns(&scratch->matrix, &scratch->scale);
                scratch->scale.vx = actor->unk0.unk2E;
                scratch->scale.vy = actor->unk0.unk30;
                scratch->scale.vz = actor->unk0.unk2C;
                vs_core_mathScaleMatrixColumns(&scratch->matrix, &scratch->scale);
            } else {
                vs_core_mathBuildEulerRotationMatrixQuadrant(&scratch->rotation, &scratch->matrix);
                vs_core_mathPremultiplyRotationMatrix(&D_800F2458.boneMatrices[parentBone], &scratch->matrix);
            }

            if (*(int*)&actor->unk704.unk150[i] == 0x400040
                && actor->unk704.unk150[i].vz == 64) {
                D_800F2458.boneScales[i].vx = scratch->scale.vx =
                    D_800F2458.boneScales[parentBone].vx;
                D_800F2458.boneScales[i].vy = scratch->scale.vy =
                    D_800F2458.boneScales[parentBone].vy;
                D_800F2458.boneScales[i].vz = scratch->scale.vz =
                    D_800F2458.boneScales[parentBone].vz;
            } else {
                scratch->scale.vx = D_800F2458.boneScales[parentBone].vx;
                scratch->scale.vy = D_800F2458.boneScales[parentBone].vy;
                scratch->scale.vz = D_800F2458.boneScales[parentBone].vz;
                scratch->scale.vx *= actor->unk704.unk150[i].vx;
                scratch->scale.vy *= actor->unk704.unk150[i].vy;
                scratch->scale.vz *= actor->unk704.unk150[i].vz;
                scratch->scale.vx >>= 6;
                scratch->scale.vy >>= 6;
                scratch->scale.vz >>= 6;
                D_800F2458.boneScales[i].vx = scratch->scale.vx;
                D_800F2458.boneScales[i].vy = scratch->scale.vy;
                D_800F2458.boneScales[i].vz = scratch->scale.vz;
            }
            *(D_800F2458.boneMatrices + i) = scratch->matrix;
            if (scratch->scale.vx != 64 || scratch->scale.vy != 64
                || scratch->scale.vz != 64) {
                scratch->scale.vx <<= 6;
                scratch->scale.vy <<= 6;
                scratch->scale.vz <<= 6;
                vs_core_mathScaleMatrixColumns(&scratch->matrix, &scratch->scale);
            }
            actor->bones[i] = scratch->matrix;
        }
    }

    if (!(((actorFlags*)actor)->flags & 2)) {
        return;
    }
    if (actor->unk0.position.vy >= 256) {
        if (actor->unk0.unkF == 0 || (short)vs_event_eventPlaybackState() != 4) {
            return;
        }
    }

    savedIdentity = *(actorIdentity*)actor;
    source = actor;
    if (arg2 < 2) {
        psx_set(scratch->model, actor->unk0.unk68);
        scratch->unk36 = actor->unk6E8;
        scratch->unk38 = actor->unk6EA;
        scratch->tpage = vs_render_legacyActorTextureSlots[actor->unk5BB].tpage;
    } else {
        *(actorIdentity*)actor = *(actorIdentity*)vs_actors_actorRecords[arg2];
        psx_set(scratch->model, vs_actors_actorRecords[arg2]->unk0.unk68);
        scratch->unk36 = vs_actors_actorRecords[arg2]->unk6E8;
        scratch->unk38 = vs_actors_actorRecords[arg2]->unk6EA;
        scratch->tpage = vs_render_legacyActorTextureSlots[vs_actors_actorRecords[arg2]->unk5BB].tpage;
        source = vs_actors_actorRecords[arg2];
    }

    if (vs_render_projectModelVertices(actor, actor->bones, -vs_main_nearClip << 2, psx_get(scratch->model)) == 0) {
        ((actorFlags*)actor)->flags |= 0x1000;
    } else {
        blend = 0;
        ((actorFlags*)actor)->flags &= ~0x1000;
        if (D_800E8FC0 != 0 || source->unk0.unk64.unk0 != 0) {
            blend = 1;
        }
        if (((((actorFlags*)source)->flags >> 8 & 0xF) - 4) < 5
            && (((actorFlags*)source)->flags & 0xF00) != 0x600) {
            blend = 1;
        }
        if ((((actorFlags*)source)->flags & 0xF00) == 0xF00) {
            blend = 1;
        }
        if (!(((actorFlags*)actor)->flags & 8)) {
            if (arg2 == 1) {
                vs_render_drawModelFlatPolygons(&D_800F2458, actor, &actor->unk6F8, psx_get(scratch->model));
            } else {
                if (!(VS_OBJECT_FLAGS5AC(actor) & 0x100)) {
                    if (VS_OBJECT_FLAGS648(actor) & 0x10000) {
                        value = actor->unk0.position.vz;
                        value <<= 16;
                        func_8008C458(value | actor->unk0.position.vx, actor->unk0.unk5D,
                            &actor->unk0.unk58, &scratch->offset);
                    } else {
                        actor->unk0.unk58.unk0 = 0x80;
                        actor->unk0.unk58.unk1 = 0x80;
                        actor->unk0.unk58.unk2 = 0x80;
                    }
                }
                if (((((actorFlags*)source)->flags >> 8) & 0xF) < 4) {
                    ((u_char*)&source->unk1864)[0] = value = actor->unk0.unk58.unk0;
                    value += (signed char)source->unk6F2 * 4;
                    if (value < 0) {
                        value = 0;
                    }
                    if (value >= 256) {
                        value = 255;
                    }
                    ((u_char*)&source->unk0.unk54)[0] = value;
                    ((u_char*)&source->unk1864)[1] = value = actor->unk0.unk58.unk1;
                    value += (signed char)source->unk6F1 * 4;
                    if (value < 0) {
                        value = 0;
                    }
                    if (value >= 256) {
                        value = 255;
                    }
                    ((u_char*)&source->unk0.unk54)[1] = value;
                    ((u_char*)&source->unk1864)[2] = value = actor->unk0.unk58.unk2;
                    value += (signed char)source->unk6F0 * 4;
                    if (value < 0) {
                        value = 0;
                    }
                    if (value >= 256) {
                        value = 255;
                    }
                    ((u_char*)&source->unk0.unk54)[2] = value;
                }
                if (blend) {
                    func_800B516C(
                        &D_800F2458, actor, &source->unk0.unk54, psx_get(scratch->model));
                } else {
                    vs_render_drawModelFlatPolygons(
                        &D_800F2458, actor, &source->unk0.unk54, psx_get(scratch->model));
                }
            }
        } else {
            if (((((actorFlags*)actor)->flags >> 8) & 0xF) < 4) {
                value = scratch->offset.vz;
                value <<= 16;
                func_8008C458(value | scratch->offset.vx, actor->unk0.unk5D,
                    &scratch->color, &actor->unk0.position);
                length = scratch->color.r;
                value = (length >> 1) + 64;
                ((u_char*)&actor->unk1864)[0] = value;
                length = scratch->color.g;
                value = (length >> 1) + 64;
                ((u_char*)&actor->unk1864)[1] = value;
                length = scratch->color.b;
                value = (length >> 1) + 64;
                ((u_char*)&actor->unk1864)[2] = value;
            }
            if (blend) {
                vs_render_drawModelGouraudPolygonsWithOverrides(&D_800F2458, actor, &actor->unk0.unk54, psx_get(scratch->model));
            } else {
                vs_render_drawModelGouraudPolygons(&D_800F2458, actor, &actor->unk0.unk54, psx_get(scratch->model));
            }
        }
    }
    if (arg2 == 0 && (((actorFlags*)actor)->flags & 0x8000)) {
        func_800B4B14(actor);
    }
    *(actorIdentity*)actor = savedIdentity;
}
