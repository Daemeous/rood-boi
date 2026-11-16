#define TRAIL_SAMPLE_COUNT 5

#include "common.h"
#include "pc/src/gte_value.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "gpu.h"
#include <rand.h>
#include "pc/src/game/EFFECT/trails.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "pc/src/game/EFFECT/effectItem.h"

typedef struct {
    u_char lifetime, effect;
    short mode;
    int age;
    D_800F54D8_t startCamera;
    D_800F54D8_t endCamera;
    VECTOR unk88;
    VECTOR unk98;
    MATRIX unkA8;
    int nearClip;
    int padCC;
} func_800FD9A4_t;

void vs_effects_captureEffectCamera(D_800F54D8_t*);
void vs_effects_setEffectCameraTransitionMode(int);
extern short D_800F54D0;
extern short D_800F55A0;
extern D_800F54D8_t vs_effects_cameraMotionEnds[2];

void func_800FE3D0(
    func_800FA098_arg0*, func_800FA098_arg1*, D_800F53B8_t*, func_800FA76C_arg3*);
void func_800FEB6C(
    func_800FA098_arg0*, func_800FA098_arg1*, D_800F53B8_t*, func_800FA76C_arg3*);
void func_800FED84(
    func_800FA098_arg0*, func_800FA098_arg1*, D_800F53B8_t*, func_800FA76C_arg3*);
void func_800FF248(
    func_800FA098_arg0*, func_800FA098_arg1*, D_800F53B8_t*, func_800FA76C_arg3*);
void func_800FF3C0(
    func_800FA098_arg0*, func_800FA098_arg1*, D_800F53B8_t*, func_800FA76C_arg3*);
void func_800FF54C(
    func_800FA098_arg0*, func_800FA098_arg1*, D_800F53B8_t*, func_800FA76C_arg3*);

#define VS_SURFACE_FUNCTION func_800F98F8
#define VS_SURFACE_VERTICES vs_effects_plg128SurfaceVertices
#define VS_SURFACE_REPEAT
#include "pc/src/game/EFFECT/surfaces.h"

typedef struct {
    u_char lifetime;
    u_char effect;
    short pad;
    int radius;
    int radiusSpeed;
    int height;
    int heightSpeed;
    int angle;
    int age;
    func_800D6CF_t texture;
} vs_closedRingState;
typedef struct {
    u_char u;
    u_char v;
} vs_closedRingUV;
extern u_char vs_effects_plg128ClosedRingColorFactors[][4];

int func_800FA490(func_800D4910_t* arg0, u_int arg1, int arg2)
{
    vs_gte_view view;
    vs_gte_result projected;
    int radius[3];
    int height[3];
    SVECTOR vertices[3][16];
    int screen[3][16];
    int depths[3][16];
    int factors[3];
    CVECTOR colors[3];
    u_char u[5];
    u_char v[3];
    MATRIX unusedTransform;
    func_800FB4C0_t child = { 0 }; /* (unk60_8, the draw order the children take: 0, their own) */
    vs_closedRingState* state;
    func_800FA098_arg1* scratch = &pc_effect_scratch;
    func_800FA098_arg0* effect;
    D_800F53B8_t* battle;
    int alive;
    MATRIX* actorMatrix;
    int fraction;
    int radiusValue;
    int baseAngle;
    u_char* factorBytes;
    int cosAngle;
    int sinAngle;
    int speed;
    int maxSpeed;
    int row;
    int oldAngle;
    int column;
    int depth;
    int drawRow;
    int drawColumn;
    pc_item* prim;
    pFileBlock1Data* texture;
    alive = 1;
    state = arg0->unk8;
    battle = vs_effects_currentInstance;
    switch (arg1) {
    case 1:
        state = vs_main_allocHeapR(sizeof(*state));
        arg0->unk8 = state;
        state->lifetime = arg2;
        state->effect = arg2 >> 8;
        state->radiusSpeed = 0;
        state->radius = 0;
        state->heightSpeed = 0;
        state->height = 0;
        state->angle = 0;
        state->age = 0;
        effect = &vs_effects_package->block5Data->definitions[state->effect];
        vs_effects_clearTextureAnimation((int*)(&state->texture));
        vs_effects_restartTextureAnimation(&state->texture, effect->unkCA, effect->unkCB);
        break;

    case 2: {
        effect = &vs_effects_package->block5Data->definitions[state->effect];
        scratch->flags = effect->flags;
        actorMatrix = &battle->unk1C[effect->unkC8].unk38;
        effect = &vs_effects_package->block5Data->definitions[state->effect];
        column = vs_battle_sampleCurve(effect->unk17, state->age);
        fraction = func_800CFE1C(effect->unk30, column);
        column = ((u_char*)effect)[7];
        factorBytes = ((u_char*)vs_effects_plg128ClosedRingColorFactors) + (column & 0x1C);
        factors[0] = factorBytes[0];
        factors[1] = factorBytes[1];
        factors[2] = factorBytes[2];
        vs_battle_lerpVector(effect->unk18,
            vs_battle_sampleCurve(effect->unk8, state->age), &scratch->unk98);
        switch (scratch->flags & 7) {
        case 0:
            break;

        case 1:
            vs_battle_addVecToSvec(&scratch->unk98, vs_effects_effectOrigin, &scratch->unk98);
            break;

        case 2:
            scratch->unk98.vx += actorMatrix->t[0];
            scratch->unk98.vy += actorMatrix->t[1];
            scratch->unk98.vz += actorMatrix->t[2];
            break;
        }

        vs_battle_lerpVector(effect->unk24,
            vs_battle_sampleCurve(effect->unk9, state->age), &scratch->unkA8);
        column = vs_battle_sampleCurve(effect->unkD, state->age);
        vs_battle_lerpSvector(effect->unk34[2], column, &scratch->forceMin);
        vs_battle_lerpSvector(effect->unk34[3], column, &scratch->unk100);
        column = vs_battle_sampleCurve(effect->unkE, state->age);
        vs_battle_lerpSvector(effect->unk34[4], column, &scratch->accelerationMin);
        vs_battle_lerpSvector(effect->unk34[5], column, &scratch->accelerationMax);
        vs_battle_lerpVector(effect->unk34[0],
            vs_battle_sampleCurve(effect->unkA, state->age), &scratch->unkE8);
        vs_battle_lerp2DVector(effect->pairRanges[2],
            vs_battle_sampleCurve(effect->unkC, state->age), scratch->speedRange);
        state->radiusSpeed += scratch->accelerationMin.vx;

        radiusValue = (state->radius += scratch->forceMin.vx + state->radiusSpeed);
        height[0] = 0;
        radius[0] = radiusValue + scratch->unkA8.vx;
        state->heightSpeed += scratch->accelerationMin.vy;
        state->height += scratch->forceMin.vy + state->heightSpeed;
        radius[2] = state->height + scratch->unkA8.vy;

        column = rsin(scratch->unkE8.vx);
        height[2] = (radius[2] * column) >> 12;
        radius[1] = (radius[2] * fraction) >> 12;
        height[1] = (radius[1] * column) >> 12;
        row = rcos(scratch->unkE8.vx);
        radius[1] = ((radius[1] * row) >> 12) + radius[0];
        radius[2] = ((radius[2] * row) >> 12) + radius[0];
        oldAngle = state->angle;
        if (scratch->speedRange[0] != scratch->speedRange[1]) {
            if (scratch->speedRange[1] < scratch->speedRange[0]) {
                speed = (rand() % ((scratch->speedRange[0] - scratch->speedRange[1]) + 1))
                      + scratch->speedRange[1];
            } else {
                speed = (rand() % ((scratch->speedRange[1] - scratch->speedRange[0]) + 1))
                      + scratch->speedRange[0];
            }
        } else {
            speed = scratch->speedRange[0];
        }
        maxSpeed = oldAngle + speed;
        baseAngle = (state->angle = maxSpeed);
        for (column = 0; column < 4; ++column) {
            for (row = 0; row < 3; ++row) {
                cosAngle = (radius[row] * rcos(baseAngle + (column * 256))) >> 12;
                vertices[row][column + 12].vz = vertices[row][column + 8].vx =
                    -(vertices[row][column + 4].vz = vertices[row][column].vx = cosAngle);
                sinAngle = (radius[row] * rsin(baseAngle + (column * 256))) >> 12;
                vertices[row][column].vz = sinAngle;
                vertices[row][column + 12].vx = sinAngle;
                vertices[row][column + 8].vz = -sinAngle;
                vertices[row][column + 4].vx = -sinAngle;
                vertices[row][column].vy = height[row];
                vertices[row][column + 4].vy = height[row];
                vertices[row][column + 8].vy = height[row];
                vertices[row][column + 12].vy = height[row];
            }
        }

        for (drawRow = 0; drawRow < 3; ++drawRow) {
            for (drawColumn = 0; drawColumn < 16; ++drawColumn) {
                vertices[drawRow][drawColumn].vx += scratch->unk98.vx;
                vertices[drawRow][drawColumn].vy += scratch->unk98.vy;
                vertices[drawRow][drawColumn].vz += scratch->unk98.vz;
            }

            if (effect->rCurve) {
                colors[drawRow].r =
                    (factors[drawRow] * vs_battle_sampleCurve(effect->rCurve, state->age))
                    / 128;
            } else {
                colors[drawRow].r = factors[drawRow];
            }
            if (effect->gCurve) {
                colors[drawRow].g =
                    (factors[drawRow] * vs_battle_sampleCurve(effect->gCurve, state->age))
                    / 128;
            } else {
                colors[drawRow].g = factors[drawRow];
            }
            if (effect->bCurve) {
                colors[drawRow].b =
                    (factors[drawRow] * vs_battle_sampleCurve(effect->bCurve, state->age))
                    / 128;
            } else {
                colors[drawRow].b = factors[drawRow];
            }
        }

        if (scratch->flags & 0x20000) {
            view = vs_gte_view_of(&battle->unk1C[effect->unkC8].unk58, vs_gte_screen_current());
        } else {
            view = vs_gte_view_of(&vs_view.viewMatrix, vs_gte_screen_current());
        }
        for (drawRow = 0; drawRow < 3; ++drawRow) {
            for (drawColumn = 0; drawColumn < 16; ++drawColumn) {
                projected = vs_gte_project(&view, &vertices[drawRow][drawColumn]);
                screen[drawRow][drawColumn] = projected.sxy;
                depths[drawRow][drawColumn] = projected.otz;
            }
        }

        vs_effects_advanceTextureAnimation(&state->texture);
        texture =
            &vs_effects_package->block1Data[state->texture.unk1C->dataIndex];
        for (column = 0; column < 5; ++column)
            u[column] = texture->u0 + ((texture->u1 * column) / 4);

        v[0] = texture->v0;
        v[2] = v[0] + texture->v1;
        v[1] = texture->v0 + ((texture->v1 * fraction) >> 12);
        for (drawRow = 0; drawRow < 2; ++drawRow) {
            for (drawColumn = 0; drawColumn < 15; ++drawColumn) {
                depth = (((depths[drawRow][drawColumn] + depths[drawRow][drawColumn + 1])
                             + depths[drawRow + 1][drawColumn])
                            + depths[drawRow + 1][drawColumn + 1])
                      / 4;
                if ((depth < 0x800) && (vs_main_nearClip < depth)) {
                    prim = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, depth);
                    vs_effect_xy(prim, 0, screen[drawRow][drawColumn]);
                    vs_effect_xy(prim, 1, screen[drawRow][drawColumn + 1]);
                    vs_effect_xy(prim, 2, screen[drawRow + 1][drawColumn]);
                    vs_effect_xy(prim, 3, screen[drawRow + 1][drawColumn + 1]);
                    vs_effect_rgb(prim, 0, *(int*)&colors[drawRow]);
                    vs_effect_rgb(prim, 1, *(int*)&colors[drawRow]);
                    vs_effect_rgb(prim, 2, *(int*)&colors[drawRow + 1]);
                    vs_effect_rgb(prim, 3, *(int*)&colors[drawRow + 1]);
                    prim->u[0] = prim->u[2] = u[drawColumn & 3];
                    prim->u[1] = prim->u[3] = u[(drawColumn & 3) + 1];
                    prim->v[0] = prim->v[1] = v[drawRow];
                    prim->v[2] = prim->v[3] = v[drawRow + 1];
                    prim->mode = texture->tpage;
                    prim->clut = texture->clut;
                }
            }

            depth =
                (((depths[drawRow][15] + depths[drawRow][0]) + depths[drawRow + 1][15])
                    + depths[drawRow + 1][0])
                / 4;
            if ((depth < 0x800) && (vs_main_nearClip < depth)) {
                prim = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, depth);
                vs_effect_xy(prim, 0, screen[drawRow][15]);
                vs_effect_xy(prim, 1, screen[drawRow][0]);
                vs_effect_xy(prim, 2, screen[drawRow + 1][15]);
                vs_effect_xy(prim, 3, screen[drawRow + 1][0]);
                vs_effect_rgb(prim, 0, *(int*)&colors[drawRow]);
                vs_effect_rgb(prim, 1, *(int*)&colors[drawRow]);
                vs_effect_rgb(prim, 2, *(int*)&colors[drawRow + 1]);
                vs_effect_rgb(prim, 3, *(int*)&colors[drawRow + 1]);
                prim->u[0] = prim->u[2] = u[drawColumn & 3];
                prim->u[1] = prim->u[3] = u[(drawColumn & 3) + 1];
                prim->v[0] = prim->v[1] = v[drawRow];
                prim->v[2] = prim->v[3] = v[drawRow + 1];
                prim->mode = texture->tpage;
                prim->clut = texture->clut;
            }
        }

        if (effect->unk3) {
            child.unk60 = 0;
            if (effect->flags & 0x20000) {
                view = vs_gte_view_of(actorMatrix, VS_GTE_NO_SCREEN);
                (&scratch->unk2C)->vx = scratch->unk98.vx,
                (&scratch->unk2C)->vy = scratch->unk98.vy,
                (&scratch->unk2C)->vz = scratch->unk98.vz;
                vs_gte_transform_to(&scratch->unk34, &view, &scratch->unk2C);
                (&child.unkC)->vx = scratch->unk34.vx * 4096,
                (&child.unkC)->vy = scratch->unk34.vy * 4096,
                (&child.unkC)->vz = scratch->unk34.vz * 4096;
            } else {
                (&child.unkC)->vx = scratch->unk98.vx * 4096,
                (&child.unkC)->vy = scratch->unk98.vy * 4096,
                (&child.unkC)->vz = scratch->unk98.vz * 4096;
            }
            func_800D2ADC(battle, effect->unk3 - 1, 0, 0, &child);
        }
        ++state->age;
        if (state->lifetime) {
            --state->lifetime;
            if (!state->lifetime) {
                alive = 0;
            }
        }
        break;
    }

    case 3:
        state->lifetime = 1;
        break;

    case 4:
        alive = 0;
        break;
    }

    return alive;
}

#define VS_ROTATING_SURFACE_REPEAT
#define VS_ROTATING_SURFACE_FUNCTION func_800FB400
#include "pc/src/game/EFFECT/rotatingSurfaces.h"

#define VS_GRID_FUNCTION func_800FC2BC
#include "pc/src/game/EFFECT/grids.h"

#define VS_SURFACE_FUNCTION func_800FCE0C
#define VS_SURFACE_VERTICES vs_effects_plg128SurfaceVertices2
#include "pc/src/game/EFFECT/surfaces.h"

void func_800FD9A4(func_800FD9A4_t* arg0, VECTOR* arg1)
{
    vs_battle_lookAt(&arg0->unk98, &arg0->unk88, &arg0->unkA8);
    arg0->unkA8.t[0] = arg1->vx;
    arg0->unkA8.t[1] = arg1->vy;
    arg0->unkA8.t[2] = arg1->vz;
}

void func_800FDA04(func_800FD9A4_t* arg0, func_800FA098_arg1* arg1, D_800F53B8_t* arg2,
    func_800FA098_arg0* arg3)
{
    switch (arg1->flags & 7) {
    case 0:
        break;
    case 1:
        vs_battle_addVecToSvec(&arg1->unk98, vs_effects_effectOrigin, &arg1->unk98);
        break;
    case 2:
        arg1->unk98.vx += arg2->unk1C[arg3->unkC8].unk38.t[0];
        arg1->unk98.vy += arg2->unk1C[arg3->unkC8].unk38.t[1];
        arg1->unk98.vz += arg2->unk1C[arg3->unkC8].unk38.t[2];
        break;
    }
    arg0->unk88.vx = arg1->unk98.vx;
    arg0->unk88.vy = arg1->unk98.vy;
    arg0->unk88.vz = arg1->unk98.vz;
    switch (arg1->flags & 0x1C0) {
    case 0:
        break;
    case 0xC0:
        arg1->attractionOffset.vx += arg2->unk1C[arg3->unkC9].unk38.t[0];
        arg1->attractionOffset.vy += arg2->unk1C[arg3->unkC9].unk38.t[1];
        arg1->attractionOffset.vz += arg2->unk1C[arg3->unkC9].unk38.t[2];
        break;
    case 0x80:
        arg1->attractionOffset.vx += vs_effects_actionRequest.unkE0.vx;
        arg1->attractionOffset.vy += vs_effects_actionRequest.unkE0.vy;
        arg1->attractionOffset.vz += vs_effects_actionRequest.unkE0.vz;
        break;
    }
    arg0->unk98.vx = arg1->attractionOffset.vx;
    arg0->unk98.vy = arg1->attractionOffset.vy;
    arg0->unk98.vz = arg1->attractionOffset.vz;
}

void func_800FDC80(func_800FD9A4_t* arg0, func_800FA098_arg1* arg1, D_800F53B8_t* arg2,
    func_800FA098_arg0* arg3)
{
    switch (arg1->flags & 7) {
    case 0:
        break;
    case 1:
        vs_battle_addVecToSvec(&arg1->unk98, vs_effects_effectOrigin, &arg1->unk98);
        break;
    case 2:
        arg1->unk98.vx += arg2->unk1C[arg3->unkC8].unk38.t[0];
        arg1->unk98.vy += arg2->unk1C[arg3->unkC8].unk38.t[1];
        arg1->unk98.vz += arg2->unk1C[arg3->unkC8].unk38.t[2];
        break;
    }
    arg0->unk98.vx = arg1->unk98.vx;
    arg0->unk98.vy = arg1->unk98.vy;
    arg0->unk98.vz = arg1->unk98.vz;
    arg0->unk88.vx = arg1->unk98.vx
                   + ((-arg1->attractionJitter.vx * rsin(arg2->unk1C[arg3->unkC8].unkBE)) >> 12);
    arg0->unk88.vz = arg1->unk98.vz
                   + ((-arg1->attractionJitter.vx * rcos(arg2->unk1C[arg3->unkC8].unkBE)) >> 12);
    arg0->unk88.vy = arg1->unk98.vy;
}

void func_800FDE78(func_800FD9A4_t* arg0, func_800FA098_arg1* scratch)
{
    arg0->endCamera.position.vx = arg0->unk88.vx << 12;
    arg0->endCamera.position.vy = arg0->unk88.vy << 12;
    arg0->endCamera.position.vz = arg0->unk88.vz << 12;
    arg0->endCamera.lookAt.vx = arg0->unk98.vx << 12;
    arg0->endCamera.lookAt.vy = arg0->unk98.vy << 12;
    arg0->endCamera.lookAt.vz = arg0->unk98.vz << 12;
}

int func_800FDEC4(func_800D4910_t* arg0, u_int arg1, int arg2)
{
    vs_gte_view view;
    func_800FD9A4_t* state = arg0->unk8;
    D_800F53B8_t* battle = vs_effects_currentInstance;
    func_800FA098_arg1* scratch = &pc_effect_scratch;
    func_800FA098_arg0* effect;
    int alive = 1;
    int curve;
    switch (arg1) {
    case 1:
        state = arg0->unk8 = vs_main_allocHeapR(sizeof(*state));
        state->effect = arg2 >> 8;
        state->lifetime = arg2;
        state->mode = arg2 >> 16;
        state->age = 0;
        vs_effects_captureEffectCamera(&state->startCamera);
        vs_effects_captureEffectCamera(&state->endCamera);
        D_800F54D0 = 0;
        D_800F55A0 = arg2 & 0xFF;
        switch (state->mode) {
        case 0:
        case 1:
            vs_effects_setEffectCameraTransitionMode(2);
            break;
        }
        break;
    case 2:
        effect = &vs_effects_package->block5Data->definitions[state->effect];
        scratch->flags = effect->flags;
        curve = effect->unk8;
        vs_battle_lerpVector(
            effect->unk18, vs_battle_sampleCurve(curve, state->age), &scratch->unk98);
        vs_battle_lerpVector(
            effect->unk24, vs_battle_sampleCurve(curve, state->age), &scratch->unkA8);
        vs_battle_lerpVector(effect->unk34[0],
            vs_battle_sampleCurve(effect->unkA, state->age), &scratch->unkE8);
        vs_battle_lerp2DVector(effect->pairRanges[2],
            vs_battle_sampleCurve(effect->unkC, state->age), scratch->speedRange);
        vs_battle_lerpSvector(effect->unk34[6],
            vs_battle_sampleCurve(effect->unkF, state->age), &scratch->attractionOffset);
        vs_battle_lerpSvector(effect->unk34[7],
            vs_battle_sampleCurve(effect->unk10, state->age), &scratch->attractionJitter);
        state->nearClip = func_800CFE1C(
            effect->unk30, vs_battle_sampleCurve(effect->unk17, state->age));
        switch (state->mode) {
        case 0:
            func_800FDA04(state, scratch, battle, effect);
            break;
        case 1:
            func_800FDC80(state, scratch, battle, effect);
            break;
        }
        switch (scratch->flags & 0x38) {
        case 0:
            func_800FD9A4(state, &state->unk88);
            scratch->unk170 = rsin(scratch->unkE8.vx);
            if (scratch->unk170 > 0)
                scratch->unk170 = -scratch->unk170;
            scratch->unk178 = rcos(scratch->unkE8.vx);
            scratch->unk174 = rsin(scratch->unkE8.vy);
            scratch->unk17C = rcos(scratch->unkE8.vy);
            scratch->unk2C.vx =
                (((scratch->unkA8.vx * scratch->unk178) >> 12) * scratch->unk17C) >> 12;
            scratch->unk2C.vy =
                (((scratch->unkA8.vy * scratch->unk178) >> 12) * scratch->unk174) >> 12;
            scratch->unk2C.vz = (scratch->unkA8.vz * scratch->unk170) >> 12;
            break;
        case 0x20:
            func_800FD9A4(state, &state->unk88);
            scratch->unk170 = rsin(scratch->unkE8.vx);
            scratch->unk178 = rcos(scratch->unkE8.vx);
            scratch->unk2C.vx = (scratch->unkA8.vx * scratch->unk178) >> 12;
            scratch->unk2C.vz = scratch->unkA8.vz;
            scratch->unk2C.vy = (scratch->unkA8.vy * scratch->unk170) >> 12;
            break;
        }
        view = vs_gte_view_of(&state->unkA8, VS_GTE_NO_SCREEN);
        vs_gte_transform_to(&state->unk88, &view, &scratch->unk2C);
        switch (state->mode) {
        case 0:
        case 1:
            func_800FDE78(state, scratch);
            break;
        }
        state->endCamera.nearClip = state->nearClip;
        state->endCamera.farClip = scratch->speedRange[0];
        vs_effects_cameraMotionEnds[0] = state->startCamera;
        vs_effects_cameraMotionEnds[1] = state->endCamera;
        ++state->age;
        if (!--state->lifetime)
            alive = 0;
        break;
    case 3:
        state->lifetime = 1;
        break;
    case 4:
        alive = 0;
        break;
    }
    return alive;
}

#define VS_TRAIL_ORIGIN_FUNCTION func_800FE3D0
#define VS_TRAIL_TARGET_FUNCTION func_800FEB6C
#define VS_TRAIL_CONTROL_FUNCTION func_800FED84
#define VS_TRAIL_INIT_FUNCTION func_800FF248
#define VS_TRAIL_UPDATE_FUNCTION func_800FF3C0
#define VS_TRAIL_RENDER_FUNCTION func_800FF54C
#define VS_TRAIL_DISPATCH_FUNCTION func_800FFFA8
#define VS_TRAIL_CORNER_STATE vs_effects_plg128TrailEmissionCounter
#include "pc/src/game/EFFECT/trails5.h"
