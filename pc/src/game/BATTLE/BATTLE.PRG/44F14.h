#pragma once
#include "30DB0.h"

void func_8006CD60(u_int*, int, int);
void vs_audio_playActorModelSound(D_800F4538_unk0*, int);
void vs_actors_actorRootOffset(D_800F4538_t*);
void vs_render_prepareActorCameraMatrices(MATRIX*);
void vs_actors_updateActorAnimationBlend(D_800F4538_t*);
void func_800AFA28(D_800F4538_t*, D_800F4538_unkC54*, int);
void func_800B002C(D_800F4538_t*, int);
void vs_actors_buildWeaponBoneChain(vs_battle_wepModels_t*, MATRIX*);
void func_800B28A8(void*, MATRIX*, char);
void vs_actors_updateAndDrawRoomModels(MATRIX*);
