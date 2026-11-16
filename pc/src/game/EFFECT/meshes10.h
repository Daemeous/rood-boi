/* Ten-vertex textured effect with independently scaled vertex groups. */
#include "common.h"
#include "pc/src/gte_value.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "gpu.h"
#include "pc/src/game/EFFECT/effectItem.h"
#ifndef VS_MESH10_TYPES
#define VS_MESH10_TYPES
typedef struct {
    u_char lifetime, effect;
    short pad;
    int age;
    func_800D6CF_t texture;
    SVECTOR rotation;
} vs_mesh10State;
typedef struct {
    u_char u, v;
} vs_mesh10UV;
#endif
extern SVECTOR VS_MESH10_VERTICES[];
extern u_char VS_MESH10_FACES[][6];

int VS_MESH10_FUNCTION(func_800D4910_t* arg0, u_int arg1, int arg2)
{
    vs_gte_view view;
    vs_gte_result projected;
    vs_gte_result transformed;
    SVECTOR vertices[10];
    int screen[10];
    int depths[10];
    CVECTOR colors[2];
    vs_mesh10UV uv[6];
    func_800FB4C0_t child = { 0 }; /* (unk60_8, the draw order the children take: 0, their own) */
    vs_mesh10State* state;
    func_800FA098_arg1* scratch = &pc_effect_scratch;
    func_800FA098_arg0* effect;
    D_800F53B8_t* battle;
    int alive;
    MATRIX* actorMatrix;
    int color, index, depth;
    pFileBlock1Data* texture;
    u_char* face;
    pc_item* prim;
    alive = 1;
    state = arg0->unk8;
    battle = vs_effects_currentInstance;
    switch (arg1) {
    case 1:
        state = vs_main_allocHeapR(sizeof *state);
        arg0->unk8 = state;
        state->effect = arg2 >> 8;
        state->lifetime = arg2;
        *(int*)&state->rotation.vx = 0;
        *(int*)&state->rotation.vz = 0;
        state->age = 0;
        effect = &vs_effects_package->block5Data->definitions[state->effect];
        vs_effects_clearTextureAnimation((int*)&state->texture);
        vs_effects_restartTextureAnimation(&state->texture, effect->unk1, effect->unk0);
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
        vs_battle_lerpVector(effect->unk24,
            vs_battle_sampleCurve(effect->unk9, state->age), &scratch->unkA8);
        vs_battle_lerpSvector(effect->unk34[7],
            vs_battle_sampleCurve(effect->unk10, state->age), &scratch->attractionJitter);
        if (effect->rCurve) {
            index = vs_battle_sampleCurve(effect->rCurve, state->age) * 2;
            color = index;
            if (color >= 256)
                color = 255;
            colors[0].r = color;
        } else
            colors[0].r = 128;
        if (effect->gCurve) {
            index = vs_battle_sampleCurve(effect->gCurve, state->age) * 2;
            color = index;
            if (color >= 256)
                color = 255;
            colors[0].g = color;
        } else
            colors[0].g = 128;
        if (effect->bCurve) {
            index = vs_battle_sampleCurve(effect->bCurve, state->age) * 2;
            color = index;
            if (color >= 256)
                color = 255;
            colors[0].b = color;
        } else
            colors[0].b = 128;
        if (effect->unk34[2][0]) {
            index = vs_battle_sampleCurve(effect->unk34[2][0], state->age) * 2;
            color = index;
            if (index >= 256)
                color = 255;
            colors[1].r = color;
        } else
            colors[1].r = 128;
        if (effect->unk34[2][2]) {
            index = vs_battle_sampleCurve(effect->unk34[2][2], state->age) * 2;
            color = index;
            if (index >= 256)
                color = 255;
            colors[1].g = color;
        } else
            colors[1].g = 128;
        if (effect->unk34[2][4]) {
            index = vs_battle_sampleCurve(effect->unk34[2][4], state->age) * 2;
            color = index;
            if (index >= 256)
                color = 255;
            colors[1].b = color;
        } else
            colors[1].b = 128;
        do {
            for (index = 0; index < 4; ++index) {
                vertices[index].vx =
                    (VS_MESH10_VERTICES[index].vx * scratch->unkA8.vx) >> 12;
                vertices[index].vy =
                    (VS_MESH10_VERTICES[index].vy * scratch->unkA8.vy) >> 12;
                vertices[index].vz =
                    (VS_MESH10_VERTICES[index].vz * scratch->unkA8.vz) >> 12;
            }
        } while (0);
        for (index = 4; index < 10; ++index) {
            vertices[index].vx =
                (VS_MESH10_VERTICES[index].vx * scratch->attractionJitter.vx) >> 12;
            vertices[index].vy =
                (VS_MESH10_VERTICES[index].vy * scratch->attractionJitter.vy) >> 12;
            vertices[index].vz =
                (VS_MESH10_VERTICES[index].vz * scratch->attractionJitter.vz) >> 12;
        }
        vs_battle_lerpVector(effect->unk34[0],
            vs_battle_sampleCurve(effect->unkA, state->age), &scratch->unkE8);
        vs_battle_lerpVector(effect->unk34[1],
            vs_battle_sampleCurve(effect->unkB, state->age), &scratch->unkC8);
        scratch->unk2C.vx = (state->rotation.vx += scratch->unkC8.vx) + scratch->unkE8.vx;
        scratch->unk2C.vy = (state->rotation.vy += scratch->unkC8.vy) + scratch->unkE8.vy;
        scratch->unk2C.vz = (state->rotation.vz += scratch->unkC8.vz) + scratch->unkE8.vz;
        RotMatrix_gte(&scratch->unk2C, &scratch->unk64);
        scratch->unk64.t[0] = scratch->unk98.vx;
        scratch->unk64.t[1] = scratch->unk98.vy;
        scratch->unk64.t[2] = scratch->unk98.vz;
        view = vs_gte_view_of(&scratch->unk64, VS_GTE_NO_SCREEN);
        for (index = 0; index < 10; ++index) {
            transformed = vs_gte_transform(&view, &vertices[index]);
            vs_gte_ir_to_svector(&vertices[index], &transformed);
        }
        if (scratch->flags & 0x20000) {
            view = vs_gte_view_of(&battle->unk1C[effect->unkC8].unk58, vs_gte_screen_current());
        } else {
            view = vs_gte_view_of(&vs_view.viewMatrix, vs_gte_screen_current());
        }
        for (index = 0; index < 10; ++index) {
            projected = vs_gte_project(&view, &vertices[index]);
            screen[index] = projected.sxy;
            depths[index] = projected.otz;
        }
        vs_effects_advanceTextureAnimation(&state->texture);
        texture =
            &vs_effects_package->block1Data[state->texture.unk1C->dataIndex];
        uv[1].u = texture->u0 + texture->u1 / 4;
        uv[2].u = texture->u0 + texture->u1 * 3 / 4;
        uv[0].u = uv[4].u = texture->u0 + texture->u1 / 2;
        uv[3].u = texture->u0;
        uv[5].u = uv[3].u + texture->u1;
        uv[1].v = uv[2].v = texture->v0 + texture->v1 / 2;
        uv[0].v = texture->v0;
        uv[3].v = uv[4].v = uv[5].v = uv[0].v + texture->v1;
        for (index = 0; index < 16; ++index) {
            depth = (depths[VS_MESH10_FACES[index][0]] + depths[VS_MESH10_FACES[index][1]]
                        + depths[VS_MESH10_FACES[index][2]])
                  / 3;
            if (depth < 0x800 && vs_main_nearClip < depth) {
                prim = vs_effect_item(PC_ITEM_POLY, 3, VS_EFFECT_GT3, depth);
                vs_effect_xy(prim, 0, screen[VS_MESH10_FACES[index][0]]);
                vs_effect_xy(prim, 1, screen[VS_MESH10_FACES[index][1]]);
                vs_effect_xy(prim, 2, screen[VS_MESH10_FACES[index][2]]);
                vs_effect_rgb(prim, 0, *(int*)&colors[VS_MESH10_VERTICES[VS_MESH10_FACES[index][0]].pad]);
                vs_effect_rgb(prim, 1, *(int*)&colors[VS_MESH10_VERTICES[VS_MESH10_FACES[index][1]].pad]);
                vs_effect_rgb(prim, 2, *(int*)&colors[VS_MESH10_VERTICES[VS_MESH10_FACES[index][2]].pad]);
                vs_effect_uv(prim, 0, *(u_short*)((u_char*)uv + VS_MESH10_FACES[index][3]));
                vs_effect_uv(prim, 1, *(u_short*)((u_char*)uv + VS_MESH10_FACES[index][4]));
                vs_effect_uv(prim, 2, *(u_short*)((u_char*)uv + VS_MESH10_FACES[index][5]));
                prim->mode = texture->tpage;
                prim->clut = texture->clut;
            }
        }
        if (effect->unk3) {
            child.unk60 = 0;
            if (effect->flags & 0x20000) {
                view = vs_gte_view_of(actorMatrix, VS_GTE_NO_SCREEN);
                setVector(&scratch->unk2C, scratch->unk98.vx, scratch->unk98.vy,
                    scratch->unk98.vz);
                vs_gte_transform_to(&scratch->unk34, &view, &scratch->unk2C);
                setVector(&child.unkC, scratch->unk34.vx * ONE, scratch->unk34.vy * ONE,
                    scratch->unk34.vz * ONE);
            } else {
                setVector(&child.unkC, scratch->unk98.vx * ONE, scratch->unk98.vy * ONE,
                    scratch->unk98.vz * ONE);
            }
            func_800D2ADC(battle, effect->unk3 - 1, 0, 0, &child);
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

#undef VS_MESH10_FUNCTION
#undef VS_MESH10_VERTICES
#undef VS_MESH10_FACES
