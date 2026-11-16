/*
 * Byte-identical five-sample trail routines shared by several effect overlays.
 * Each including module supplies its original function and state symbols.
 */
#define TRAIL_SAMPLE_COUNT 5

#include "common.h"
#include "pc/src/game/EFFECT/trails.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "gpu.h"
#include <libgte.h>
#include "pc/src/gte_value.h"
#include <rand.h>
#include "pc/src/game/EFFECT/effectItem.h"


extern int VS_TRAIL_CORNER_STATE;

void VS_TRAIL_ORIGIN_FUNCTION(func_800FA098_arg0* arg0, func_800FA098_arg1* arg1,
    D_800F53B8_t* arg2, func_800FA76C_arg3* arg3)
{
    vs_gte_view view;
    int sp10;
    int sp14;
    MATRIX* matrix;
    SVECTOR* splinePoint;
    int temp_a1;
    int temp_lo;
    int temp_s0_2;
    int temp_s3;
    int temp_v0;
    int temp_v0_4;
    int i;
    u_short var_a0;
    u_short var_v1;
    u_short var_v1_2;

    vs_battle_lerpVector(arg0->unk18,
        vs_battle_sampleCurve(arg0->unk8, arg3->curveSampleDistance), &arg1->unk98);

    matrix = &arg2->unk1C[arg0->unkC8].unk38;

    // Determines trail origin:
    switch (arg1->flags & 7) {
    case 0:
        break;
    // Fixed point vs_effects_effectOrigin
    case 1:
        vs_battle_addVecToSvec(&arg1->unk98, vs_effects_effectOrigin, &arg1->unk98);
        break;
    // Caster
    case 2:
        applyVector(&arg1->unk98, matrix->t[0], matrix->t[1], matrix->t[2], +=);
        break;
    }

    vs_battle_lerpVector(arg0->unk24,
        vs_battle_sampleCurve(arg0->unk9, arg3->curveSampleDistance), &arg1->unkA8);
    view = vs_gte_view_rotation(matrix, VS_GTE_NO_SCREEN);

    switch (arg1->flags & 0x38) {
    case 0:
        for (i = 0; i < arg3->trailcount; ++i) {

            splinePoint = &arg3->trails[i].splinePoints[0];

            temp_v0 = rand();
            arg1->unk170 = rsin(temp_v0);
            arg1->unk178 = rcos(temp_v0);

            temp_v0 = rand();
            arg1->unk174 = rsin(temp_v0);
            arg1->unk17C = rcos(temp_v0);

            setVector(splinePoint,
                ((((arg1->unkA8.vx * arg1->unk178) >> 0xC) * arg1->unk17C) >> 0xC),
                ((((arg1->unkA8.vy * arg1->unk178) >> 0xC) * arg1->unk174) >> 0xC),
                ((arg1->unkA8.vz * arg1->unk170) >> 0xC));

            if (arg1->flags & 0x40000) {
                vs_gte_rotate_to(&arg1->unk34, &view, splinePoint);
                copyVector(splinePoint, &arg1->unk34);
            }

            addVector(splinePoint, &arg1->unk98);
        }
        return;

    case 8:
        for (i = 0; i < arg3->trailcount; ++i) {

            splinePoint = &arg3->trails[i].splinePoints[0];

            switch (arg1->flags & 0x1800) {
            case 0x0:
            case 0x800:
                switch (rand() % 6) {
                case 0:
                    splinePoint->vx = arg1->unkA8.vx;
                    if (0) {
                    case 1:
                        splinePoint->vx = -arg1->unkA8.vx;
                    }

                    splinePoint->vy =
                        vs_battle_randUniformInt(arg1->unkA8.vy, -arg1->unkA8.vy);
                    splinePoint->vz =
                        vs_battle_randUniformInt(arg1->unkA8.vz, -arg1->unkA8.vz);
                    break;

                case 2:
                    splinePoint->vy = arg1->unkA8.vy;
                    if (0) {
                    case 3:
                        splinePoint->vy = -arg1->unkA8.vy;
                    }

                    splinePoint->vx =
                        vs_battle_randUniformInt(arg1->unkA8.vx, -arg1->unkA8.vx);
                    splinePoint->vz =
                        vs_battle_randUniformInt(arg1->unkA8.vz, -arg1->unkA8.vz);
                    break;

                case 4:
                    splinePoint->vz = arg1->unkA8.vz;
                    if (0) {
                    case 5:
                        splinePoint->vz = -arg1->unkA8.vz;
                    }

                    splinePoint->vx =
                        vs_battle_randUniformInt(arg1->unkA8.vx, -arg1->unkA8.vx);
                    splinePoint->vy =
                        vs_battle_randUniformInt(arg1->unkA8.vy, -arg1->unkA8.vy);
                    break;
                }

                break;

            case 0x1000:
                temp_a1 = VS_TRAIL_CORNER_STATE + 1;
                VS_TRAIL_CORNER_STATE = temp_a1 & 7;

                var_a0 = arg1->unkA8.vx;
                if (!(temp_a1 & 1)) {
                    var_a0 = -var_a0;
                }

                splinePoint->vx = var_a0;

                var_v1 = arg1->unkA8.vy;
                if (!(temp_a1 & 2)) {
                    var_v1 = -var_v1;
                }
                splinePoint->vy = var_v1;

                var_v1_2 = arg1->unkA8.vz;
                if (!(temp_a1 & 4)) {
                    var_v1_2 = -var_v1_2;
                }
                splinePoint->vz = var_v1_2;

                break;
            }

            if (arg1->flags & 0x40000) {
                vs_gte_rotate_to(&arg1->unk34, &view, splinePoint);
                copyVector(splinePoint, &arg1->unk34);
            }

            addVector(splinePoint, &arg1->unk98);
        }

        return;

    case 16:
    case 24:
    case 32:
    case 40:
        arg1->ringSegments = 4;
        arg1->ringPositions = 4;
        break;

    case 48:
        arg1->ringSegments = arg0->unkCA;
        if (arg1->ringSegments == 0) {
            arg1->ringSegments = 1;
        }
        arg1->ringPositions = arg0->unkCB;
        if (arg1->ringPositions == 0) {
            arg1->ringPositions = arg1->ringSegments;
        }
        break;

    default:
        return;
    }

    temp_lo = ONE / arg1->ringSegments;

    if ((arg1->flags & 0x1800) != 0x1000) {
        VS_TRAIL_CORNER_STATE = VS_TRAIL_CORNER_STATE % arg1->ringPositions;
        temp_s0_2 = (VS_TRAIL_CORNER_STATE * temp_lo) + ONE / 8;
        sp10 = (rcos(temp_s0_2) * arg1->unkA8.vx) >> 0xC;
        sp14 = (rsin(temp_s0_2) * arg1->unkA8.vz) >> 0xC;
    }

    for (i = 0; i < arg3->trailcount; ++i) {
        splinePoint = &arg3->trails[i].splinePoints[0];
        VS_TRAIL_CORNER_STATE = (VS_TRAIL_CORNER_STATE + 1) % arg1->ringPositions;
        temp_s0_2 = (VS_TRAIL_CORNER_STATE * temp_lo) + ONE / 8;
        temp_s3 = (rcos(temp_s0_2) * arg1->unkA8.vx) >> 0xC;
        temp_s0_2 = (rsin(temp_s0_2) * arg1->unkA8.vz) >> 0xC;

        if ((arg1->flags & 0x1800) == 0x1000) {
            splinePoint->vx = temp_s3;
            splinePoint->vz = temp_s0_2;
        } else {
            temp_v0_4 = rand() >> 3;
            splinePoint->vx = (((temp_s3 - sp10) * temp_v0_4) >> 0xC) + sp10;
            splinePoint->vz = (((temp_s0_2 - sp14) * temp_v0_4) >> 0xC) + sp14;
        }

        splinePoint->vy = 0;

        if (arg1->flags & 0x40000) {
            vs_gte_rotate_to(&arg1->unk34, &view, splinePoint);
            copyVector(splinePoint, &arg1->unk34);
        }

        addVector(splinePoint, &arg1->unk98);
    }
}

void VS_TRAIL_TARGET_FUNCTION(func_800FA098_arg0* arg0, func_800FA098_arg1* arg1,
    D_800F53B8_t* arg2, func_800FA76C_arg3* arg3)
{
    int i;

    vs_battle_lerpSvector(arg0->unk34[6],
        vs_battle_sampleCurve(arg0->unkF, arg3->curveSampleDistance), &arg1->attractionOffset);
    vs_battle_lerpSvector(arg0->unk34[7],
        vs_battle_sampleCurve(arg0->unk10, arg3->curveSampleDistance), &arg1->attractionJitter);

    switch (arg1->flags & 0x1C0) {
    case 0:
        break;

    case 0xC0:
        applyVector(&arg1->attractionOffset, arg2->unk1C[arg0->unkC9].unk38.t[0],
            arg2->unk1C[arg0->unkC9].unk38.t[1], arg2->unk1C[arg0->unkC9].unk38.t[2], +=);
        break;

    case 0x80:
        applyVector(&arg1->attractionOffset, vs_effects_actionRequest.unkE0.vx, vs_effects_actionRequest.unkE0.vy,
            vs_effects_actionRequest.unkE0.vz, +=);
        break;
    }

    for (i = 0; i < arg3->trailcount; ++i) {
        SVECTOR* v = &arg3->trails[i].splinePoints[3];
        setVector(v,
            arg1->attractionOffset.vx + vs_battle_randUniformInt(arg1->attractionJitter.vx, -arg1->attractionJitter.vx),
            arg1->attractionOffset.vy + vs_battle_randUniformInt(arg1->attractionJitter.vy, -arg1->attractionJitter.vy),
            arg1->attractionOffset.vz
                + vs_battle_randUniformInt(arg1->attractionJitter.vz, -arg1->attractionJitter.vz));
    }
}

void VS_TRAIL_CONTROL_FUNCTION(func_800FA098_arg0* arg0, func_800FA098_arg1* arg1,
    D_800F53B8_t* arg2, func_800FA76C_arg3* arg3)
{
    vs_gte_view view;
    SVECTOR sp10;
    SVECTOR sp18;
    int i;
    int temp_s0;
    int var_s0;
    int var_s1;
    int var_s3;
    int var_s4;

    for (i = 0; i < arg3->trailcount; ++i) {

        SVECTOR* temp_s5 = arg3->trails[i].splinePoints;

        arg1->unk34.vx = temp_s5[3].vx - temp_s5[0].vx;
        arg1->unk34.vy = temp_s5[3].vy - temp_s5[0].vy;
        arg1->unk34.vz = temp_s5[3].vz - temp_s5[0].vz;

        temp_s0 = vs_gte_rsqrt((arg1->unk34.vx * arg1->unk34.vx)
                               + (arg1->unk34.vy * arg1->unk34.vy)
                               + (arg1->unk34.vz * arg1->unk34.vz));

        vs_battle_lerp2DVector(arg0->pairRanges[0],
            vs_battle_sampleCurve(arg0->unk12, arg3->curveSampleDistance), arg1->unk124);
        vs_battle_lerp2DVector(arg0->pairRanges[1],
            vs_battle_sampleCurve(arg0->unk13, arg3->curveSampleDistance), arg1->unk12C);
        vs_battle_lerp2DVector(arg0->pairRanges[2],
            vs_battle_sampleCurve(arg0->unkC, arg3->curveSampleDistance), arg1->speedRange);
        vs_battle_lerpVector(arg0->unk34[1],
            vs_battle_sampleCurve(arg0->unkB, arg3->curveSampleDistance), &arg1->unkC8);
        vs_battle_lerpVector(arg0->unk34[0],
            vs_battle_sampleCurve(arg0->unkA, arg3->curveSampleDistance), &arg1->unkE8);
        vs_battle_lerpSvector(arg0->unk34[2],
            vs_battle_sampleCurve(arg0->unkD, arg3->curveSampleDistance), &arg1->forceMin);

        if (arg1->forceMin.vx != 0) {
            int var_v0_2 =
                (arg1->speedRange[0] + (rand() % arg1->forceMin.vx)) - (arg1->forceMin.vx / 2);
            if (&arg1->unk34) {
                sp10.vz = (temp_s0 * var_v0_2) / 256;
            } else {
                sp10.vz = (temp_s0 * var_v0_2) / 256;
            }
        } else {
            sp10.vz = (temp_s0 * arg1->speedRange[0]) / 256;
        }

        if (arg1->forceMin.vy != 0) {
            int var_v0_3 =
                (arg1->speedRange[1] + (rand() % arg1->forceMin.vy)) - (arg1->forceMin.vy / 2);
            sp18.vz = temp_s0 + ((temp_s0 * var_v0_3) / 256);
        } else {
            sp18.vz = temp_s0 + ((temp_s0 * arg1->speedRange[1]) / 256);
        }

        var_s3 = arg1->unk124[0];

        if (arg1->unk124[1] != 0) {
            var_s3 += rand() % arg1->unk124[1];
        }

        var_s4 = arg1->unk12C[0];

        if (arg1->unk12C[1] != 0) {
            var_s4 += rand() % arg1->unk12C[1];
        }

        var_s0 = arg1->unkE8.vx;

        if (arg1->unkC8.vx != 0) {
            var_s0 += (rand() % (arg1->unkC8.vx * 2)) - arg1->unkC8.vx;
        }

        var_s1 = arg1->unkE8.vy;

        if (arg1->unkC8.vy != 0) {
            var_s1 += (rand() % (arg1->unkC8.vy * 2)) - arg1->unkC8.vy;
        }

        sp10.vx = (rcos(var_s0) * var_s3) >> 0xC;
        sp10.vy = (rsin(var_s0) * var_s3) >> 0xC;
        sp18.vx = (rcos(var_s1) * var_s4) >> 0xC;
        sp18.vy = (rsin(var_s1) * var_s4) >> 0xC;

        vs_battle_svecToVec(temp_s5, &arg1->unk34);
        vs_battle_svecToVec(temp_s5 + 3, &arg1->unk44);
        vs_battle_lookAt(&arg1->unk34, &arg1->unk44, &arg1->unk64);
        view = vs_gte_view_at(&arg1->unk64, &arg1->unk34, VS_GTE_NO_SCREEN);
        vs_gte_transform_to(&arg1->unk34, &view, &sp10);
        vs_battle_vecToSvec(&arg1->unk34, temp_s5 + 1);
        vs_gte_transform_to(&arg1->unk44, &view, &sp18);
        vs_battle_vecToSvec(&arg1->unk44, temp_s5 + 2);
    }
}

void VS_TRAIL_INIT_FUNCTION(func_800FA098_arg0* arg0, func_800FA098_arg1* arg1,
    D_800F53B8_t* arg2 __attribute__((unused)), func_800FA76C_arg3* arg3)
{
    int speed;
    int i;

    vs_battle_lerp2DVector(arg0->pairRanges[4], 0, arg1->unk134);

    speed = func_800CFE1C(arg0->unk30, 0) << 0xC;

    for (i = 0; i < arg3->trailcount; ++i) {
        int stepCount = vs_battle_randUniformInt(arg1->unk134[0], arg1->unk134[1]);
        if (stepCount == 0) {
            stepCount = 1;
        }
        arg3->trails[i].stepCount = stepCount;
        arg3->trails[i].splineSampleCount = 1;
        arg3->trails[i].trailCollapseCounter = 4;
        arg3->trails[i].currentSplineSample = 0;
        arg3->trails[i].splineSamples[0] = arg3->trails[i].splinePoints[0];
        arg3->trails[i].splineProgress = 0;
        arg3->trails[i].speed = speed;
        arg3->trails[i].unk =
            ((0x06000000 / (stepCount * stepCount)) - ((speed * 2) / stepCount));
    }
}

void VS_TRAIL_UPDATE_FUNCTION(func_800FA098_arg0* arg0 __attribute__((unused)),
    func_800FA098_arg1* arg1 __attribute__((unused)),
    D_800F53B8_t* arg2 __attribute__((unused)), func_800FA76C_arg3* arg3)
{
    int i;

    for (i = 0; i < arg3->trailcount; ++i) {

        _trail* temp_s0 = &arg3->trails[i];

        if (temp_s0->trailCollapseCounter == 0) {
            continue;
        }

        if (arg3->curveSampleDistance >= temp_s0->stepCount) {
            --temp_s0->trailCollapseCounter;
            if (temp_s0->trailCollapseCounter < temp_s0->stepCount) {
                --temp_s0->splineSampleCount;
            }
        } else {
            int currentStep = temp_s0->stepCount - arg3->curveSampleDistance;
            int speed = temp_s0->speed;

            speed += ((0x06000000 - (temp_s0->splineProgress * 2))
                         / (currentStep * currentStep))
                   - ((speed * 2) / currentStep);
            temp_s0->speed = speed;
            temp_s0->splineProgress += speed;
            temp_s0->currentSplineSample = (temp_s0->currentSplineSample + 1) % 5;

            vs_battle_splineInterpolate(&temp_s0->splinePoints[0],
                &temp_s0->splinePoints[1], &temp_s0->splinePoints[2],
                &temp_s0->splinePoints[3], (temp_s0->splineProgress >> 0xC) - ONE,
                &temp_s0->splineSamples[temp_s0->currentSplineSample]);

            if (temp_s0->splineSampleCount < 5) {
                ++temp_s0->splineSampleCount;
            }
        }
    }
}

void VS_TRAIL_RENDER_FUNCTION(func_800FA098_arg0* arg0, func_800FA098_arg1* arg1,
    D_800F53B8_t* arg2, func_800FA76C_arg3* arg3)
{
    vs_gte_view view;
    vs_gte_result projected;
    pc_item sp10;
    DVECTOR sp50[6];
    int sp78[6];
    int spA0[4];
    CVECTOR spC0;
    int _[6] __attribute__((unused));
    DVECTOR* spF0;
    int i;
    int spF8;
    int spFC;
    _trail* temp_s1;
    int temp_a1_2;
    int temp_s0;
    int var_s0_2;
    int j;
    pc_item* temp_a3;
    pFileBlock1Data* temp_s7;
    SVECTOR* temp;
    DVECTOR* temp2;
    int temp_s3;

    vs_battle_lerpSvector(arg0->unk34[4],
        vs_battle_sampleCurve(arg0->unkE, arg3->curveSampleDistance), &arg1->accelerationMin);

    if (arg0->rCurve != 0) {
        j = vs_battle_sampleCurve(arg0->rCurve, arg3->curveSampleDistance) * 2;
        spC0.r = j <= 255 ? j : 255;
    } else {
        spC0.r = 128;
    }

    if (arg0->gCurve != 0) {
        j = vs_battle_sampleCurve(arg0->gCurve, arg3->curveSampleDistance) * 2;
        spC0.g = j <= 255 ? j : 255;
    } else {
        spC0.g = 128;
    }

    if (arg0->bCurve != 0) {
        j = vs_battle_sampleCurve(arg0->bCurve, arg3->curveSampleDistance) * 2;
        spC0.b = j <= 255 ? j : 255;
    } else {
        spC0.b = 128;
    }


    if (arg1->flags & 0x20000) {
        view = vs_gte_view_of(&arg2->unk1C[arg0->unkC8].unk58, vs_gte_screen_current());
    } else {
        view = vs_gte_view_of(&vs_view.viewMatrix, vs_gte_screen_current());
    }

    vs_effects_advanceTextureAnimation(&arg3->unkC);

    temp_s7 = &vs_effects_package->block1Data[arg3->unkC.unk1C->dataIndex];

    for (i = 0; i < arg3->trailcount; ++i) {

        temp_s1 = &arg3->trails[i];

        temp = temp_s1->splineSamples;
        if (temp_s1->splineSampleCount >= 2) {
            temp_s3 = temp_s1->currentSplineSample;
            for (j = 0; j < temp_s1->splineSampleCount; ++j) {
                projected = vs_gte_project(&view, &temp[temp_s3]);
                sp50[j].vx = projected.sx;
                sp50[j].vy = projected.sy;
                sp78[j] = projected.otz;
                temp_s3 = (temp_s3 + 4) % 5;
            }

            for (j = 0; j < (temp_s1->splineSampleCount - 1); ++j) {
                DVECTOR* p = &sp50[j];
                spA0[j] = ratan2(p[1].vy - p[0].vy, p[1].vx - p[0].vx);
                spA0[j] -= 0x400;
            }

            temp_s3 = arg1->accelerationMin.vx / 2;
            temp_s0 = (rcos(spA0[0]) * temp_s3) >> 0xC;
            temp_a1_2 = (rsin(spA0[0]) * temp_s3) >> 0xC;
            spF0 = sp50;
            sp10.x[0] = spF0[0].vx + temp_s0;
            sp10.y[0] = spF0[0].vy + temp_a1_2;
            sp10.x[2] = spF0[0].vx - temp_s0;
            sp10.y[2] = spF0[0].vy - temp_a1_2;

            sp10.u[0] = sp10.u[2] =
                temp_s7->u0 + ((temp_s7->u1 * temp_s1->trailCollapseCounter) / 4);
            spF8 = temp_s7->v0;
            spFC = temp_s7->v0 + temp_s7->v1;
            sp10.v[0] = spF8;
            sp10.v[2] = spFC;
            sp10.rgb[0][0] =
                (spC0.r * arg3->sampledTransparencyCurve[temp_s1->trailCollapseCounter])
                >> 7;
            sp10.rgb[0][1] =
                (spC0.g * arg3->sampledTransparencyCurve[temp_s1->trailCollapseCounter])
                >> 7;
            sp10.rgb[0][2] =
                (spC0.b * arg3->sampledTransparencyCurve[temp_s1->trailCollapseCounter])
                >> 7;

            for (j = 0; j < (temp_s1->splineSampleCount - 2); ++j) {
                var_s0_2 = (spA0[j] + spA0[j + 1]) / 2;

                if (((spA0[j + 1] - spA0[j]) + 0x7FF) >= 0xFFFU) {
                    var_s0_2 += 0x800;
                }

                temp_s0 = (rcos(var_s0_2) * temp_s3) >> 0xC;
                temp_a1_2 = (rsin(var_s0_2) * temp_s3) >> 0xC;
                temp2 = &spF0[j];
                sp10.x[1] = temp2[1].vx + temp_s0;
                sp10.y[1] = temp2[1].vy + temp_a1_2;
                sp10.x[3] = temp2[1].vx - temp_s0;
                sp10.y[3] = temp2[1].vy - temp_a1_2;

                sp10.u[1] = sp10.u[3] =
                    temp_s7->u0
                    + ((temp_s7->u1 * ((temp_s1->trailCollapseCounter - j) - 1)) / 4);
                sp10.v[1] = spF8;
                sp10.v[3] = spFC;
                sp10.rgb[1][0] =
                    ((spC0.r
                         * arg3->sampledTransparencyCurve[temp_s1->trailCollapseCounter
                                                          - j - 1])
                        >> 7);
                sp10.rgb[1][1] =
                    ((spC0.g
                         * arg3->sampledTransparencyCurve[temp_s1->trailCollapseCounter
                                                          - j - 1])
                        >> 7);
                sp10.rgb[1][2] =
                    ((spC0.b
                         * arg3->sampledTransparencyCurve[temp_s1->trailCollapseCounter
                                                          - j - 1])
                        >> 7);

                temp_s0 = (sp78[j] + sp78[j + 1]) / 2;

                if ((temp_s0 < 0x800) && (vs_main_nearClip < temp_s0)) {
                    temp_a3 = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, temp_s0);
                    vs_effect_quad(temp_a3, &sp10, temp_s7->tpage, temp_s7->clut);
                    vs_effect_rgb_of(temp_a3, 0, &sp10, 0);
                    vs_effect_rgb_of(temp_a3, 1, &sp10, 1);
                    vs_effect_rgb_of(temp_a3, 2, &sp10, 0);
                    vs_effect_rgb_of(temp_a3, 3, &sp10, 1);
                }
                vs_effect_advance(&sp10, 0, 1);
                vs_effect_advance(&sp10, 2, 3);
                vs_effect_rgb_of(&sp10, 0, &sp10, 1);
            }

            temp_s0 = (rcos(spA0[j]) * temp_s3) >> 0xC;
            temp_a1_2 = (rsin(spA0[j]) * temp_s3) >> 0xC;
            sp10.x[1] = ((u_short*)spF0)[j * 2 + 2] + temp_s0;
            sp10.y[1] = ((u_short*)spF0)[j * 2 + 3] + temp_a1_2;
            sp10.x[3] = ((u_short*)spF0)[j * 2 + 2] - temp_s0;
            sp10.y[3] = ((u_short*)spF0)[j * 2 + 3] - temp_a1_2;
            sp10.u[1] = sp10.u[3] = temp_s7->u0;
            sp10.v[1] = spF8;
            sp10.v[3] = spFC;
            sp10.rgb[1][0] = (spC0.r * arg3->sampledTransparencyCurve[0]) >> 7;
            sp10.rgb[1][1] = (spC0.g * arg3->sampledTransparencyCurve[0]) >> 7;
            sp10.rgb[1][2] = (spC0.b * arg3->sampledTransparencyCurve[0]) >> 7;

            temp_s0 = (sp78[j] + sp78[j + 1]) / 2;

            if ((temp_s0 < 0x800) && (vs_main_nearClip < temp_s0)) {
                temp_a3 = vs_effect_item(PC_ITEM_POLY, 4, VS_EFFECT_GT4, temp_s0);
                vs_effect_quad(temp_a3, &sp10, temp_s7->tpage, temp_s7->clut);
                vs_effect_rgb_of(temp_a3, 0, &sp10, 0);
                vs_effect_rgb_of(temp_a3, 1, &sp10, 1);
                vs_effect_rgb_of(temp_a3, 2, &sp10, 0);
                vs_effect_rgb_of(temp_a3, 3, &sp10, 1);
            }
        }
    }
}

int VS_TRAIL_DISPATCH_FUNCTION(func_800D4910_t* arg0, u_int arg1, int arg2)
{
    vs_gte_view view;
    func_800FB4C0_t sp18 = { 0 }; /* (unk60_8, the draw order the children take: 0, their own) */
    func_800FA098_arg0* temp_s3;
    int trail;
    int j;
    func_800FA76C_arg3* temp_v0;

    func_800FA098_arg1* s4 = &pc_effect_scratch;
    func_800FA76C_arg3* temp_s0 = arg0->unk8;
    D_800F53B8_t* temp_s5 = vs_effects_currentInstance;
    int var_s6 = 1;

    switch (arg1) {
    case 1:
        temp_v0 = vs_main_allocHeapR(sizeof *temp_v0 + 8);
        arg0->unk8 = temp_v0;
        temp_v0->unk1 = arg2 >> 8;
        temp_v0->unk0 = arg2;
        temp_v0->curveSampleDistance = 0;
        temp_s3 = &vs_effects_package->block5Data->definitions[temp_v0->unk1];
        trail = temp_s3->unkC0[0];

        if (trail > 16) {
            trail = 16;
        }

        temp_v0->trails = vs_main_allocHeapR(trail * sizeof *temp_v0->trails);
        temp_v0->trailcount = trail;
        temp_v0->unk3 = 0;

        if (temp_s3->transparencyCurve != 0) {

            // bad var reuse, trail = length of curve
            trail =
                vs_effects_package->curveBlock->curveSizes[temp_s3->transparencyCurve];

            if (trail > 1) {
                --trail;
            }

            for (j = 0; j < 5; ++j) {
                temp_v0->sampledTransparencyCurve[j] = 
                    vs_effects_package->curves[temp_s3->transparencyCurve][(j * trail) / 4];
            }

        } else {
            for (j = 0; j < 5; ++j) {
                temp_v0->sampledTransparencyCurve[j] = 0x80;
            }
        }

        vs_effects_clearTextureAnimation((int*)&temp_v0->unkC);
        vs_effects_restartTextureAnimation(&temp_v0->unkC, temp_s3->unk1, temp_s3->unk0);
        break;

    case 2:
        temp_s3 = &vs_effects_package->block5Data->definitions[temp_s0->unk1];
        s4->flags = (temp_s3->flags);

        if (temp_s0->unk3 == 0) {
            VS_TRAIL_ORIGIN_FUNCTION(temp_s3, s4, temp_s5, temp_s0);
            VS_TRAIL_TARGET_FUNCTION(temp_s3, s4, temp_s5, temp_s0);
            VS_TRAIL_CONTROL_FUNCTION(temp_s3, s4, temp_s5, temp_s0);
            VS_TRAIL_INIT_FUNCTION(temp_s3, s4, temp_s5, temp_s0);
            temp_s0->unk3 = 1U;
        }

        VS_TRAIL_UPDATE_FUNCTION(temp_s3, s4, temp_s5, temp_s0);
        VS_TRAIL_RENDER_FUNCTION(temp_s3, s4, temp_s5, temp_s0);

        if (temp_s3->unk3 != 0) {
            for (trail = 0; trail < temp_s0->trailcount; ++trail) {

                sp18.unk60 = 0;

                if (temp_s3->flags & 0x20000) {
                    view = vs_gte_view_of(&temp_s5->unk1C[temp_s3->unkC8].unk38, VS_GTE_NO_SCREEN);
                    s4->unk2C.vx =
                        temp_s0->trails[trail]
                            .splineSamples[temp_s0->trails[trail].currentSplineSample]
                            .vx;
                    s4->unk2C.vy =
                        temp_s0->trails[trail]
                            .splineSamples[temp_s0->trails[trail].currentSplineSample]
                            .vy;
                    s4->unk2C.vz =
                        temp_s0->trails[trail]
                            .splineSamples[temp_s0->trails[trail].currentSplineSample]
                            .vz;

                    vs_gte_transform_to(&s4->unk34, &view, &s4->unk2C);

                    sp18.unkC.vx = s4->unk34.vx * ONE;
                    sp18.unkC.vy = s4->unk34.vy * ONE;
                    sp18.unkC.vz = s4->unk34.vz * ONE;
                } else {
                    sp18.unkC.vx =
                        temp_s0->trails[trail]
                            .splineSamples[temp_s0->trails[trail].currentSplineSample]
                            .vx
                        * ONE;
                    sp18.unkC.vy =
                        temp_s0->trails[trail]
                            .splineSamples[temp_s0->trails[trail].currentSplineSample]
                            .vy
                        * ONE;
                    sp18.unkC.vz =
                        temp_s0->trails[trail]
                            .splineSamples[temp_s0->trails[trail].currentSplineSample]
                            .vz
                        * ONE;
                }

                func_800D2ADC(temp_s5, temp_s3->unk3 - 1, 0, 0, &sp18);
            }
        }

        ++temp_s0->curveSampleDistance;

        if (temp_s0->unk0 != 0) {
            --temp_s0->unk0;
            if (!temp_s0->unk0) {
                var_s6 = 0;
                vs_main_freeHeapR(temp_s0->trails);
            }
        }
        break;

    case 3:
        temp_s0->unk0 = 1;
        break;

    case 4:
        var_s6 = 0;
        vs_main_freeHeapR(temp_s0->trails);
        break;
    }

    return var_s6;
}

#undef VS_TRAIL_ORIGIN_FUNCTION
#undef VS_TRAIL_TARGET_FUNCTION
#undef VS_TRAIL_CONTROL_FUNCTION
#undef VS_TRAIL_INIT_FUNCTION
#undef VS_TRAIL_UPDATE_FUNCTION
#undef VS_TRAIL_RENDER_FUNCTION
#undef VS_TRAIL_DISPATCH_FUNCTION
#undef VS_TRAIL_CORNER_STATE
#undef TRAIL_SAMPLE_COUNT
