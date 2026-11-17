/* Shared untextured spline-particle line renderer. */
#include "pc/src/gte_value.h"
#include "gpu.h"
#include "pc/src/game/EFFECT/effectItem.h"

void VS_PARTICLE_RENDER_FUNCTION(vs_effects_ParticleDefinition* arg0, vs_effects_ParticleEmitterScratch* arg1,
    vs_effects_EffectInstance* arg2, func_800FA098_arg3* arg3)
{
    vs_gte_view view;
    vs_gte_result projected;
    int screenPoints[10];
    int depths[10];
    CVECTOR color;
    int blendMode = (((u_int*)arg0)[1] >> 26) & 3;
    int particleIndex;
    int segment;
    int renderSegment;
    int width;
    int depth;
    int screenOffset;
    int* endpoint;
    u_int endpointStep;
    SVECTOR* controlPoints;
    pc_item* prim;
    width = func_800CFE1C(arg0->unk30, vs_battle_sampleCurve(arg0->unk17, arg3->unkC));
    if (!width)
        width = 1;

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

    for (particleIndex = 0; particleIndex < arg3->unk2; ++particleIndex) {
        controlPoints = &arg3->unk4[particleIndex * 4];
        projected = vs_gte_project(&view, controlPoints);
        screenPoints[0] = projected.sxy;
        depths[0] = projected.otz;
        for (segment = 1; segment < 8; ++segment) {
            vs_battle_splineInterpolate(controlPoints, controlPoints + 1,
                controlPoints + 2, controlPoints + 3,
                (segment * 0x600) - 0x1000 + rand() % width - width / 2, &arg1->unk2C);
            projected = vs_gte_project(&view, &arg1->unk2C);
            screenPoints[segment] = projected.sxy;
            depths[segment] = projected.otz;
        }
        projected = vs_gte_project(&view, controlPoints + 3);
        screenPoints[segment] = projected.sxy;
        depths[segment] = projected.otz;
        for (renderSegment = 0; renderSegment < 8; ++renderSegment) {
            depth = (depths[renderSegment] + depths[renderSegment + 1]) / 2;
            if (depth < 0x800 && vs_main_nearClip < depth) {
                switch (arg0->unk34[5][0]) {
                case 0:
                    break;
                case 1:
                    depth = 0x7FE;
                    break;
                case 2:
                    depth = -15;
                    break;
                case 3:
                    depth = -13;
                    break;
                case 4:
                    depth = -11;
                    break;
                case 5:
                    if (vs_main_nearClip < depth - 16)
                        depth -= 16;
                    break;
                }
                prim = vs_effect_item(PC_ITEM_LINE, 2, VS_EFFECT_LINE, depth);
                prim->mode = blendMode << 5;
                vs_effect_rgb(prim, 0, *(int*)&color);
                screenOffset = renderSegment * sizeof(screenPoints[0]);
                vs_effect_xy(prim, 0, *(int*)((char*)screenPoints + screenOffset));
                endpointStep = 1;
                endpoint = &screenPoints[renderSegment + endpointStep];
                vs_effect_xy(prim, 1, *endpoint);
                vs_effect_shorten_line(prim);
            }
        }
    }
}
