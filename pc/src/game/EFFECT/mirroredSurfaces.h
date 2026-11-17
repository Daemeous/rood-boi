/* Animated mirrored nine-row radial surface with per-row color curves. */
#include "common.h"
#include "pc/src/gte_value.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "gpu.h"
#include <rand.h>
#include "pc/src/game/EFFECT/effectItem.h"
#ifndef VS_MIRROR_SURFACE_TYPES
#define VS_MIRROR_SURFACE_TYPES
typedef struct {
    u_char lifetime, effect;
    short pad;
    int radius, radiusSpeed, height, heightSpeed, age;
    int factors[9];
    func_800D6CF_t texture;
} vs_mirrorSurfaceState;
#endif
extern SVECTOR VS_MIRROR_SURFACE_VERTICES[5][16];

#ifdef VS_MIRROR_SURFACE_REPEAT
#define VS_MIRROR_SURFACE_U_SEGMENTS 4
#define VS_MIRROR_SURFACE_U_INDEX(column) ((column) & 3)
#else
#define VS_MIRROR_SURFACE_U_SEGMENTS 16
#define VS_MIRROR_SURFACE_U_INDEX(column) (column)
#endif

int VS_MIRROR_SURFACE_FUNCTION(func_800D4910_t* arg0, u_int arg1, int arg2)
{
    vs_gte_view view;
    vs_gte_result projected;
    SVECTOR vertices[9][16];
    int screen[9][16];
    int depths[9][16];
    CVECTOR colors[9];
    int maxFactor;
    u_char u[VS_MIRROR_SURFACE_U_SEGMENTS + 1];
    u_char v[9];
    vs_effects_EffectInstance* battle;
    int alive;
    int heightStep;
    int row, column;
    int radiusStep;
    int radiusValue, heightValue;
    int sample;
    int depth;
    int curveIndex;
    int initIndex, tempHeight;
    vs_mirrorSurfaceState* state;
    vs_effects_ParticleEmitterScratch* scratch = &pc_effect_scratch;
    vs_effects_ParticleDefinition* effect;
    MATRIX* actorMatrix;
    pc_item* prim;
    pFileBlock1Data* texture;

    alive = 1;
    state = arg0->unk8;
    battle = vs_effects_currentInstance;
    switch (arg1) {
    case 1:
        state = vs_main_allocHeapR(sizeof *state);
        arg0->unk8 = state;
        state->effect = arg2 >> 8;
        state->lifetime = arg2;
        state->radiusSpeed = 0;
        state->radius = 0;
        state->heightSpeed = 0;
        state->height = 0;
        state->age = 0;
        effect = &vs_effects_package->block5Data->definitions[state->effect];
        curveIndex = effect->transparencyCurve;
        if (curveIndex) {
            sample = vs_effects_package->curveBlock->curveSizes[curveIndex];
            if (sample >= 2)
                --sample;
            for (initIndex = 0; initIndex < 9; ++initIndex) {
                state->factors[initIndex] =
                    vs_effects_package
                            ->curves[effect->transparencyCurve][sample * initIndex / 8]
                    * 2;
                if (state->factors[initIndex] == 256)
                    state->factors[initIndex] = 255;
            }
        } else {
            maxFactor = 255;
            for (initIndex = 8; initIndex >= 0; --initIndex)
                state->factors[initIndex] = maxFactor;
        }
        vs_effects_clearTextureAnimation((int*)&state->texture);
        vs_effects_restartTextureAnimation(&state->texture, effect->unkCA, effect->unkCB);
        break;
    case 2: {
        effect = &vs_effects_package->block5Data->definitions[state->effect];
        scratch->flags = effect->flags;
        actorMatrix = &battle->unk1C[effect->unkC8].unk38;
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
        heightStep = 0;
        vs_battle_lerpVector(effect->unk24,
            vs_battle_sampleCurve(effect->unk9, state->age), &scratch->unkA8);
        radiusStep = 0;
        sample = vs_battle_sampleCurve(effect->unkD, state->age);
        vs_battle_lerpSvector(effect->unk34[2], sample, &scratch->forceMin);
        vs_battle_lerpSvector(effect->unk34[3], sample, &scratch->unk100);
        row = 0;
        sample = vs_battle_sampleCurve(effect->unkE, state->age);
        vs_battle_lerpSvector(effect->unk34[4], sample, &scratch->accelerationMin);
        vs_battle_lerpSvector(effect->unk34[5], sample, &scratch->accelerationMax);
        vs_battle_lerpVector(effect->unk34[1],
            vs_battle_sampleCurve(effect->unkB, state->age), &scratch->unkC8);
        vs_battle_lerp2DVector(effect->pairRanges[2],
            vs_battle_sampleCurve(effect->unkC, state->age), scratch->speedRange);
        state->heightSpeed += scratch->accelerationMin.vy;
        tempHeight = state->height += scratch->forceMin.vy + state->heightSpeed;
        heightValue = tempHeight + scratch->unkA8.vy;
        state->radiusSpeed += scratch->accelerationMin.vx;
        state->radius += scratch->forceMin.vx + state->radiusSpeed;
        radiusValue = state->radius + scratch->unkA8.vx;
        for (row = 0; row < 9; ++row) {
            if (row < 5) {
                for (column = 0; column < 16; ++column) {
                    vertices[row][column].vx =
                        scratch->unk98.vx
                        + ((VS_MIRROR_SURFACE_VERTICES[4 - row][column].vx * radiusValue)
                            >> 12);
                    vertices[row][column].vz =
                        scratch->unk98.vz
                        + ((VS_MIRROR_SURFACE_VERTICES[4 - row][column].vz * radiusValue)
                            >> 12);
                    vertices[row][column].vy =
                        scratch->unk98.vy
                        + ((VS_MIRROR_SURFACE_VERTICES[4 - row][column].vy * heightValue)
                            >> 12);
                }
            } else {
                for (column = 0; column < 16; ++column) {
                    vertices[row][column].vx =
                        scratch->unk98.vx
                        + ((VS_MIRROR_SURFACE_VERTICES[row - 4][column].vx * radiusValue)
                            >> 12);
                    vertices[row][column].vz =
                        scratch->unk98.vz
                        + ((VS_MIRROR_SURFACE_VERTICES[row - 4][column].vz * radiusValue)
                            >> 12);
                    vertices[row][column].vy =
                        scratch->unk98.vy
                        + ((-VS_MIRROR_SURFACE_VERTICES[row - 4][column].vy * heightValue)
                            >> 12);
                }
            }
            radiusStep += scratch->accelerationMax.vx;
            radiusValue += radiusStep + scratch->unk100.vx;
            heightStep += scratch->accelerationMax.vy;
            heightValue += heightStep + scratch->unk100.vy;
            if (effect->rCurve)
                colors[row].r = state->factors[row]
                              * vs_battle_sampleCurve(effect->rCurve, state->age) / 128;
            else
                colors[row].r = state->factors[row];
            if (effect->gCurve)
                colors[row].g = state->factors[row]
                              * vs_battle_sampleCurve(effect->gCurve, state->age) / 128;
            else
                colors[row].g = state->factors[row];
            if (effect->bCurve)
                colors[row].b = state->factors[row]
                              * vs_battle_sampleCurve(effect->bCurve, state->age) / 128;
            else
                colors[row].b = state->factors[row];
        }
        if (scratch->flags & 0x20000) {
            view = vs_gte_view_of(&battle->unk1C[effect->unkC8].unk58, vs_gte_screen_current());
        } else {
            view = vs_gte_view_of(&vs_view.viewMatrix, vs_gte_screen_current());
        }
        for (row = 0; row < 9; ++row) {
            for (column = 0; column < 16; ++column) {
                projected = vs_gte_project(&view, &vertices[row][column]);
                screen[row][column] = projected.sxy;
                depths[row][column] = projected.otz;
            }
        }
        vs_effects_advanceTextureAnimation(&state->texture);
        texture =
            &vs_effects_package->block1Data[state->texture.unk1C->dataIndex];
        for (sample = 0; sample < VS_MIRROR_SURFACE_U_SEGMENTS + 1; ++sample)
            u[sample] = texture->u0 + texture->u1 * sample / VS_MIRROR_SURFACE_U_SEGMENTS;
        for (sample = 0; sample < 9; ++sample)
            v[sample] = texture->v0 + texture->v1 * sample / 8;
        for (row = 0; row < 8; ++row) {
            for (column = 0; column < 15; ++column) {
                depth = (depths[row][column] + depths[row][column + 1]
                            + depths[row + 1][column] + depths[row + 1][column + 1])
                      / 4;
                if (depth < 0x800 && vs_main_nearClip < depth) {
                    prim = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, depth);
                    vs_effect_xy(prim, 0, screen[row][column]);
                    vs_effect_xy(prim, 1, screen[row][column + 1]);
                    vs_effect_xy(prim, 2, screen[row + 1][column]);
                    vs_effect_xy(prim, 3, screen[row + 1][column + 1]);
                    vs_effect_rgb(prim, 0, *(int*)&colors[row]);
                    vs_effect_rgb(prim, 1, *(int*)&colors[row]);
                    vs_effect_rgb(prim, 2, *(int*)&colors[row + 1]);
                    vs_effect_rgb(prim, 3, *(int*)&colors[row + 1]);
                    prim->u[0] = prim->u[2] = u[VS_MIRROR_SURFACE_U_INDEX(column)];
                    prim->u[1] = prim->u[3] = u[VS_MIRROR_SURFACE_U_INDEX(column) + 1];
                    prim->v[0] = prim->v[1] = v[row];
                    prim->v[2] = prim->v[3] = v[row + 1];
                    prim->mode = texture->tpage;
                    prim->clut = texture->clut;
                }
            }
            depth = (depths[row][15] + depths[row][0] + depths[row + 1][15]
                        + depths[row + 1][0])
                  / 4;
            if (depth < 0x800 && vs_main_nearClip < depth) {
                prim = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, depth);
                vs_effect_xy(prim, 0, screen[row][15]);
                vs_effect_xy(prim, 1, screen[row][0]);
                vs_effect_xy(prim, 2, screen[row + 1][15]);
                vs_effect_xy(prim, 3, screen[row + 1][0]);
                vs_effect_rgb(prim, 0, *(int*)&colors[row]);
                vs_effect_rgb(prim, 1, *(int*)&colors[row]);
                vs_effect_rgb(prim, 2, *(int*)&colors[row + 1]);
                vs_effect_rgb(prim, 3, *(int*)&colors[row + 1]);
                prim->u[0] = prim->u[2] = u[VS_MIRROR_SURFACE_U_INDEX(column)];
                prim->u[1] = prim->u[3] = u[VS_MIRROR_SURFACE_U_INDEX(column) + 1];
                prim->v[0] = prim->v[1] = v[row];
                prim->v[2] = prim->v[3] = v[row + 1];
                prim->mode = texture->tpage;
                prim->clut = texture->clut;
            }
        }
        ++state->age;
        if (state->lifetime) {
            --state->lifetime;
            if (!state->lifetime)
                alive = 0;
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
#undef VS_MIRROR_SURFACE_FUNCTION
#undef VS_MIRROR_SURFACE_VERTICES

#undef VS_MIRROR_SURFACE_U_SEGMENTS
#undef VS_MIRROR_SURFACE_U_INDEX
#undef VS_MIRROR_SURFACE_REPEAT
