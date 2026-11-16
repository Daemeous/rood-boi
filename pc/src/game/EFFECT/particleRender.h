/* Shared segmented particle ribbon renderer. */
#include "pc/src/gte_value.h"
#include "gpu.h"
#include "pc/src/game/EFFECT/effectItem.h"

#ifndef VS_PARTICLE_PRIM_TYPE
#define VS_PARTICLE_PRIM_TYPE

#endif

void VS_PARTICLE_RENDER_FUNCTION(func_800FA098_arg0* arg0, func_800FA098_arg1* arg1,
    D_800F53B8_t* arg2, func_800FA098_arg3* arg3)
{
    vs_gte_view view;
    vs_gte_result projected;
    pc_item quad;
    DVECTOR screenPoints[10];
    int depths[10];
    int angles[8];
    CVECTOR color;
    int curveSamples[10];
    DVECTOR* screenStart;
    int particleIndex;
    int topV;
    int bottomV;
    SVECTOR* controlPoints;
    int offsetY;
    int offsetX;
    int angleOrDepth;
    int segment;
    pc_item* prim;
    pFileBlock1Data* texture;
    DVECTOR* point;
    int width;

    width = func_800CFE1C(arg0->unk30, vs_battle_sampleCurve(arg0->unk17, arg3->unkC));
    if (!width)
        width = 1;

    vs_battle_lerpSvector(
        arg0->unk34[4], vs_battle_sampleCurve(arg0->unkE, arg3->unkC), &arg1->unk108);

    if (arg0->rCurve != 0) {
        segment = vs_battle_sampleCurve(arg0->rCurve, arg3->unkC) * 2;
        color.r = segment <= 255 ? segment : 255;
    } else {
        color.r = 128;
    }

    if (arg0->gCurve != 0) {
        segment = vs_battle_sampleCurve(arg0->gCurve, arg3->unkC) * 2;
        color.g = segment <= 255 ? segment : 255;
    } else {
        color.g = 128;
    }

    if (arg0->bCurve != 0) {
        segment = vs_battle_sampleCurve(arg0->bCurve, arg3->unkC) * 2;
        color.b = segment <= 255 ? segment : 255;
    } else {
        color.b = 128;
    }


    if (arg1->flags & 0x20000) {
        view = vs_gte_view_of(&arg2->unk1C[arg0->unkC8].unk58, vs_gte_screen_current());
    } else {
        view = vs_gte_view_of(&vs_view.viewMatrix, vs_gte_screen_current());
    }

    vs_effects_advanceTextureAnimation(&arg3->unk10);

    texture = &vs_effects_package->block1Data[arg3->unk10.unk1C->dataIndex];

    for (particleIndex = 0; particleIndex < arg3->unk2; ++particleIndex) {

        controlPoints = &arg3->unk4[particleIndex * 4];
        projected = vs_gte_project(&view, controlPoints);
        screenPoints[0].vx = projected.sx;
        screenPoints[0].vy = projected.sy;
        depths[0] = projected.otz;
        curveSamples[0] = 0;
        for (segment = 1; segment < 8; ++segment) {
            curveSamples[segment] = segment * 0x600 + rand() % width - width / 2;
            vs_battle_splineInterpolate(controlPoints, controlPoints + 1,
                controlPoints + 2, controlPoints + 3, curveSamples[segment] - 0x1000,
                &arg1->unk2C);
            projected = vs_gte_project(&view, &arg1->unk2C);
            screenPoints[segment].vx = projected.sx;
            screenPoints[segment].vy = projected.sy;
            depths[segment] = projected.otz;
        }
        projected = vs_gte_project(&view, controlPoints + 3);
        screenPoints[segment].vx = projected.sx;
        screenPoints[segment].vy = projected.sy;
        depths[segment] = projected.otz;
        curveSamples[segment] = 0x3000;

        for (segment = 0; segment < 8; ++segment) {
            DVECTOR* p = &screenPoints[segment];
            angles[segment] = ratan2(p[1].vy - p[0].vy, p[1].vx - p[0].vx);
            angles[segment] -= 0x400;
        }

        width = arg1->unk108.vx / 2;
        screenStart = screenPoints;
        if (arg1->unk108.vy)
            width += (rand() % arg1->unk108.vy) / 2;
        offsetX = (rcos(angles[0]) * width) >> 0xC;
        offsetY = (rsin(angles[0]) * width) >> 0xC;
        quad.x[0] = screenStart[0].vx + offsetX;
        quad.y[0] = screenStart[0].vy + offsetY;
        quad.x[2] = screenStart[0].vx - offsetX;
        quad.y[2] = screenStart[0].vy - offsetY;

        quad.u[0] = quad.u[2] = texture->u0;
        topV = texture->v0;
        bottomV = topV + texture->v1;
        quad.v[0] = topV;
        quad.v[2] = bottomV;
        for (segment = 0; segment < 7; ++segment) {
            width = arg1->unk108.vx / 2;
            if (arg1->unk108.vy)
                width += (rand() % arg1->unk108.vy) / 2;
            angleOrDepth = (angles[segment] + angles[segment + 1]) / 2;
            offsetX = (rcos(angleOrDepth) * width) >> 0xC;
            offsetY = (rsin(angleOrDepth) * width) >> 0xC;
            point = &screenStart[segment];
            quad.x[1] = point[1].vx + offsetX;
            quad.y[1] = point[1].vy + offsetY;
            quad.x[3] = point[1].vx - offsetX;
            quad.y[3] = point[1].vy - offsetY;

            quad.u[1] = quad.u[3] =
                texture->u0 + ((texture->u1 * curveSamples[segment + 1]) / 0x3000);
            quad.v[1] = topV;
            quad.v[3] = bottomV;
            angleOrDepth = (depths[segment] + depths[segment + 1]) / 2;

            if ((angleOrDepth < 0x800) && (vs_main_nearClip < angleOrDepth)) {
                switch (arg0->unk34[5][0]) {
                case 0:
                    break;
                case 1:
                    angleOrDepth = 0x7FE;
                    break;
                case 2:
                    angleOrDepth = -15;
                    break;
                case 3:
                    angleOrDepth = -13;
                    break;
                case 4:
                    angleOrDepth = -11;
                    break;
                case 5:
                    if (vs_main_nearClip < angleOrDepth - 16)
                        angleOrDepth -= 16;
                    break;
                }
                prim = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, angleOrDepth);
                vs_effect_quad(prim, &quad, texture->tpage, texture->clut);
                vs_effect_rgb(prim, 0, *(int*)&color);
                vs_effect_rgb(prim, 1, *(int*)&color);
                vs_effect_rgb(prim, 2, *(int*)&color);
                vs_effect_rgb(prim, 3, *(int*)&color);
            }
            vs_effect_advance(&quad, 0, 1);
            vs_effect_advance(&quad, 2, 3);
        }

        width = arg1->unk108.vx / 2;
        if (arg1->unk108.vy)
            width += (rand() % arg1->unk108.vy) / 2;
        offsetX = (rcos(angles[segment]) * width) >> 0xC;
        offsetY = (rsin(angles[segment]) * width) >> 0xC;
        quad.x[1] = ((u_short*)screenStart)[segment * 2 + 2] + offsetX;
        quad.y[1] = ((u_short*)screenStart)[segment * 2 + 3] + offsetY;
        quad.x[3] = ((u_short*)screenStart)[segment * 2 + 2] - offsetX;
        quad.y[3] = ((u_short*)screenStart)[segment * 2 + 3] - offsetY;
        quad.u[1] = quad.u[3] = texture->u0 + texture->u1;
        quad.v[1] = topV;
        quad.v[3] = bottomV;
        angleOrDepth = (depths[segment] + depths[segment + 1]) / 2;

        if ((angleOrDepth < 0x800) && (vs_main_nearClip < angleOrDepth)) {
            switch (arg0->unk34[5][0]) {
            case 0:
                break;
            case 1:
                angleOrDepth = 0x7FE;
                break;
            case 2:
                angleOrDepth = -15;
                break;
            case 3:
                angleOrDepth = -13;
                break;
            case 4:
                angleOrDepth = -11;
                break;
            case 5:
                if (vs_main_nearClip < angleOrDepth - 16)
                    angleOrDepth -= 16;
                break;
            }
            prim = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, angleOrDepth);
            vs_effect_quad(prim, &quad, texture->tpage, texture->clut);
            vs_effect_rgb(prim, 0, *(int*)&color);
            vs_effect_rgb(prim, 1, *(int*)&color);
            vs_effect_rgb(prim, 2, *(int*)&color);
            vs_effect_rgb(prim, 3, *(int*)&color);
        }
    }
}
