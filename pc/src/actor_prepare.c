/* Host-only actor transform preparation, recovered from the retained decomp
 * candidate r12-d/b28/v12.c (see docs/decomp-blockers.md). The original matching
 * build keeps its assembly. This source is not claimed to match the PS1 binary;
 * validate rendered actors and less common animation branches as the port grows. */
#include "common.h"
#include <rand.h>
#include <abs.h>
#include <inline_c.h>
#include "vs_inline_c.h"
#include "src/BATTLE/BATTLE.PRG/146C.h"
#include "src/BATTLE/BATTLE.PRG/3A1A0.h"
#include "src/BATTLE/BATTLE.PRG/44F14.h"
#include "src/SLUS_010.40/main.h"
#include "src/BATTLE/BATTLE.PRG/30DB0.h"
#include "pc/src/psx_types.h"

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
extern MATRIX D_800F3C58[];

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
void func_800B17F0(SVECTOR*, MATRIX*);
void func_800B196C(MATRIX*, MATRIX*);
void func_800B396C(MATRIX*, VECTOR*);
int func_800B3A68(void*, MATRIX*, int, void*);
short func_800BEB00(void);
void func_800B516C(void*, void*, void*, void*);
void func_800B3CF0(void*, void*, void*, void*);
void func_800B4594(void*, void*, void*, void*);
void func_800B4B14(void*);
void func_800B5C30(void*, void*, void*, void*);
void func_8008C458(int, int, void*, void*);
int vs_gte_rsqrt(int);

typedef struct {
    char prefix[0x1870];
    MATRIX matrix;
} savedBoneMatrix;
typedef struct {
    char prefix[0x6C];
    MATRIX matrix;
} actorBoneMatrix;
typedef struct {
    char prefix[0x4C];
    SVECTOR translation;
} boneTransformScratch;
static inline void func_800B26B8(void* actor, MATRIX* parent, int bone, int saved)
{
    boneTransformScratch* scratch = (void*)0x1F80035C;
    MATRIX* matrix = (void*)0x1F8003C0;
    char* savedAddress;
    int boneOffset;
    actorBoneMatrix* destination;
    MATRIX* table;
    *matrix = ((savedBoneMatrix*)((char*)actor + (saved << 5)))->matrix;
    func_800B196C(parent, matrix);
    gte_SetRotMatrix(parent);
    gte_SetTransMatrix(parent);
    savedAddress = (char*)actor + ((saved - 1) << 5);
    scratch->translation.vx = *(u_short*)(savedAddress + 0x18A4);
    scratch->translation.vy = *(u_short*)(savedAddress + 0x18A8);
    scratch->translation.vz = *(u_short*)(savedAddress + 0x18AC);
    gte_ldv0((void*)0x1F8003A8);
    gte_rtv0tr2();
    gte_stlvnl((void*)0x1F8003D4);
    table = D_800F2458.boneMatrices;
    boneOffset = bone << 5;
    *(MATRIX*)((char*)table + boneOffset) = *matrix;
    destination = (void*)((char*)actor + boneOffset);
    destination->matrix = *matrix;
}

void func_800B28A8(void* actorData, MATRIX* parent, char arg2)
{
    D_800F4538_t* actor = actorData;
    actorTransformScratch* scratch = (void*)0x1F80035C;
    D_800F4538_t* source;
    int _[2] __attribute__((unused));
    actorIdentity savedIdentity;
    char* savedAddress;
    int i;
    int value;
    int parentBone;
    int dx;
    int dz;
    int angle;
    int length;
    vs_scratch_t* frame;
    int blend;
    u_int flags;

    psx_copy(scratch->model, actor->unk0.unk68);
    gte_SetRotMatrix(parent);
    gte_SetTransMatrix(parent);
    *(int*)&scratch->translation = *(int*)&scratch->offset;
    *(int*)&scratch->translation.vz = *(int*)&scratch->offset.vz;
    gte_ldv0((void*)0x1F8003A8);
    gte_rtv0tr2();
    scratch->rotation.vx = actor->unk704.unk0[0].vx >> 1;
    scratch->rotation.vy = actor->unk704.unk0[0].vy >> 1;
    scratch->rotation.vz = actor->unk704.unk0[0].vz >> 1;
    gte_stlvnl((void*)0x1F8003D4);
    scratch->rotation.vx += actor->unk0.unk24;
    scratch->rotation.vy += actor->unk0.facing;
    scratch->rotation.vz += actor->unk0.unk28;

    if (*(u_int*)((char*)actor + 0x5AC) & 0x200000) {
        if ((short)func_800BEB00() == 4 || (((actorFlags*)actor)->flags & 4)) {
            if (arg2 == 0) {
                flags = *(u_int*)((char*)actor + 0x5AC);
                if (!(flags & 0x400000)) {
                    value = actor->unk1802[0] + 1;
                    if (actor->unk1802[1] < value) {
                        *(u_int*)((char*)actor + 0x5AC) = flags | 0x400000;
                        value = actor->unk1802[1];
                    }
                } else {
                    value = actor->unk1802[0] - 1;
                    if (value < 0) {
                        *(u_int*)((char*)actor + 0x5AC) = flags & ~0x400000;
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
    func_800B17F0(&scratch->rotation, &scratch->matrix);
    func_800B196C(parent, &scratch->matrix);

    if (!(((actorFlags*)actor)->flags & 0x2000)) {
        scratch->scale.vx = actor->unk183C;
        scratch->scale.vy = actor->unk183C;
        scratch->scale.vz = actor->unk183C;
        func_800B396C(&scratch->matrix, &scratch->scale);
    }
    if (*(int*)&actor->unk0.unk2C != 0x10001000 || actor->unk0.unk30 != 0x1000) {
        scratch->scale.vx = actor->unk0.unk2E;
        scratch->scale.vy = actor->unk0.unk30;
        scratch->scale.vz = actor->unk0.unk2C;
        func_800B396C(&scratch->matrix, &scratch->scale);
    }
    *D_800F3C58 = scratch->matrix;

    D_800F2458.boneScales[0].vx = scratch->scale.vx = actor->unk704.unk150[0].vx;
    D_800F2458.boneScales[0].vy = scratch->scale.vy = actor->unk704.unk150[0].vy;
    D_800F2458.boneScales[0].vz = scratch->scale.vz = actor->unk704.unk150[0].vz;
    if (scratch->scale.vx != 64 || scratch->scale.vy != 64 || scratch->scale.vz != 64) {
        scratch->scale.vx <<= 6;
        scratch->scale.vy <<= 6;
        scratch->scale.vz <<= 6;
        func_800B396C(&scratch->matrix, &scratch->scale);
    }
    actor->bones[0] = scratch->matrix;
    scratch->translation.vx = scratch->translation.vy = 0;

    for (i = 1; i < actor->unk0.nBones; ++i) {
        value = psx_get(scratch->model)->armatures[i].unkC;
        if (value != 0) {
            func_800B26B8(actor, parent, i, value);
        } else {
            parentBone = psx_get(scratch->model)->armatures[i].unk4;
            psx_set(scratch->parent, &actor->bones[parentBone]);
            gte_SetRotMatrix(psx_get(scratch->parent));
            gte_SetTransMatrix(psx_get(scratch->parent));
            length = (u_short)psx_get(scratch->model)->armatures[parentBone].unk0;
            scratch->translation.vy = 0;
            scratch->translation.vz = 0;
            scratch->translation.vx = -length;
            gte_ldv0(&scratch->translation);
            gte_rtv0tr2();
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
                    boneSpin* spin = (boneSpin*)((char*)actor + 0x1874) + value;
                    scratch->rotation.vx = spin->angle += spin->step;
                }
                break;
            case 7:
                frame = (void*)0x1F800000;
                scratch->rotation.vx = frame->camera.angles.vx;
                scratch->rotation.vy = frame->camera.angles.vy - 0x400;
                scratch->rotation.vz = frame->camera.angles.vz;
            case 8:
            case 9:
                frame = (void*)0x1F800000;
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

            gte_stlvnl(&scratch->matrix.t);
            *(int*)&scratch->rotation &= 0x0FFF0FFF;
            *(int*)&scratch->rotation.vz &= 0xFFF;
            if (value >= 7) {
                RotMatrixYXZ_gte(&scratch->rotation, &scratch->matrix);
                func_800B196C(parent, &scratch->matrix);
                scratch->scale.vx = actor->unk183C;
                scratch->scale.vy = actor->unk183C;
                scratch->scale.vz = actor->unk183C;
                func_800B396C(&scratch->matrix, &scratch->scale);
                scratch->scale.vx = actor->unk0.unk2E;
                scratch->scale.vy = actor->unk0.unk30;
                scratch->scale.vz = actor->unk0.unk2C;
                func_800B396C(&scratch->matrix, &scratch->scale);
            } else {
                func_800B17F0(&scratch->rotation, &scratch->matrix);
                func_800B196C(&D_800F2458.boneMatrices[parentBone], &scratch->matrix);
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
                func_800B396C(&scratch->matrix, &scratch->scale);
            }
            actor->bones[i] = scratch->matrix;
        }
    }

    if (!(((actorFlags*)actor)->flags & 2)) {
        return;
    }
    if (actor->unk0.position.vy >= 256) {
        if (actor->unk0.unkF == 0 || (short)func_800BEB00() != 4) {
            return;
        }
    }

    savedIdentity = *(actorIdentity*)actor;
    source = actor;
    if (arg2 < 2) {
        psx_copy(scratch->model, actor->unk0.unk68);
        scratch->unk36 = actor->unk6E8;
        scratch->unk38 = actor->unk6EA;
        scratch->tpage = D_800F46A8[actor->unk5BB].tpage;
    } else {
        *(actorIdentity*)actor = *(actorIdentity*)D_800F4538[arg2];
        psx_copy(scratch->model, D_800F4538[arg2]->unk0.unk68);
        scratch->unk36 = D_800F4538[arg2]->unk6E8;
        scratch->unk38 = D_800F4538[arg2]->unk6EA;
        scratch->tpage = D_800F46A8[D_800F4538[arg2]->unk5BB].tpage;
        source = D_800F4538[arg2];
    }

    if (func_800B3A68(actor, actor->bones, -vs_main_nearClip << 2, psx_get(scratch->model)) == 0) {
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
                func_800B3CF0(&D_800F2458, actor, &actor->unk6F8, psx_get(scratch->model));
            } else {
                if (!(*(u_int*)((char*)actor + 0x5AC) & 0x100)) {
                    if (*(u_int*)((char*)actor + 0x648) & 0x10000) {
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
                    func_800B3CF0(
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
                func_800B5C30(&D_800F2458, actor, &actor->unk0.unk54, psx_get(scratch->model));
            } else {
                func_800B4594(&D_800F2458, actor, &actor->unk0.unk54, psx_get(scratch->model));
            }
        }
    }
    if (arg2 == 0 && (((actorFlags*)actor)->flags & 0x8000)) {
        func_800B4B14(actor);
    }
    *(actorIdentity*)actor = savedIdentity;
}
