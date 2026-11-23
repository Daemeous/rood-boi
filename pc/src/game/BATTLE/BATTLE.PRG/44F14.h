#pragma once
#include "30DB0.h"

void func_8006CD60(u_int*, int, int);
void vs_audio_playActorModelSound(vs_actors_ModelInstance*, int);
void vs_actors_actorRootOffset(D_800F4538_t*);
void vs_render_prepareActorCameraMatrices(MATRIX*);
void vs_actors_updateActorAnimationBlend(D_800F4538_t*);
void func_800AFA28(D_800F4538_t*, D_800F4538_unkC54*, int);
void vs_actors_updateMotionAndLookAt(D_800F4538_t*, int);
void vs_actors_buildWeaponBoneChain(vs_battle_wepModels_t*, MATRIX*);
void vs_actors_drawActorModel(void*, MATRIX*, char);
void vs_actors_updateAndDrawRoomModels(MATRIX*);
