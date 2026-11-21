#pragma once
#include "3A1A0.h"
#include <libgte.h>
#include "pc/src/psx_types.h"

typedef struct {
    u_int dataOffset;  /* from the block's start, as on disc (func_800AD494_t) */
    u_int indexOffset;
    char unk8[0x78F8];
} func_8009C378_t;
PSX_ASSERT_SIZE(func_8009C378_t, 0x7900);

typedef struct {
    int unk0;
    int unk4;
    int opaque8;
    int unkC;
    char unk10;
    char opaque11;
} vs_menu_FrameworkPreviewPoseState;

typedef struct {
    short tpage;
    short clut0;
    short clut1;
    u_char refCount;
} vs_battle_texInfo_t;

extern vs_menu_FrameworkPreviewPoseState* vs_battle_unusedGlobal1[];
extern int vs_battle_unusedGlobal2;
extern _wepModelOffsets* vs_battle_commonWepBuffs[2];
extern vs_battle_texInfo_t vs_render_legacyActorTextureSlots[];
extern vs_battle_texInfo_t vs_battle_wepTextures[20];
extern char D_800F49DC;

u_char vs_field_sampleCollisionHeightCode(u_int, int, int);
int vs_field_releaseGimmickInstance(int);
int vs_content_releaseWeaponModel(int);
void vs_content_copyWeaponSequence(func_8009C378_t*, func_8009C378_t*);
int vs_content_releaseSequenceEffectTexture(int);
void func_8009D7E8(int, int);
void vs_actors_resetActorWeaponVisibilityBlend(int);
void vs_actors_setActorWeaponDrawn(int, int, int);
void vs_actors_playActorWeaponDrawSound(int, int);
void vs_actors_stopActorMotion(int);
void vs_actors_clearActorMovementModes(int);
void vs_battle_playMeleeAttackAnimation(int, SVECTOR*, int);
void func_8009DDF4(int arg0, SVECTOR* arg1);
void func_8009E2E0(int, SVECTOR*, int);
void vs_field_refreshAshleyInteractionTile(D_800F4538_t*);
void vs_battle_playChainAbilityAnimation(int, SVECTOR*, int, int);
void vs_battle_playDefenseAbilityAnimation(int, SVECTOR*, int);
void func_8009EFEC(int, SVECTOR*, int);
void func_8009F298(int, SVECTOR*, int);
void vs_field_dropUnsupportedGimmickStack(int);
void vs_actors_setWeaponBrightnessOverride(int arg0, int arg1, int arg2);
int vs_actors_animationBattleWindowFrame(int);
void func_8009F940(int, int, D_800F4538_unk58_2*, D_800F4538_unk58_2*);
void vs_actors_setActorSoundOverrides(int, D_800F4538_unk64*);
void vs_actors_setRoomActorScale(VECTOR*);
int vs_actors_setActorTravelAnimation(int, int);
int vs_actors_startHeadTracking(int, int, SVECTOR*, int);
int vs_actors_disableModelPalettePulse(int arg0, int arg1);
void vs_actors_attachActorToParentBone(int, int, int, int);
int func_8009FFA8(int arg0, int arg1);
int vs_actors_queueActorAnimation(int, int, int, int);
int vs_actors_playActorAnimation(int, int, int, int);
int vs_actors_playActorAnimationAtFrame(int, int, int, int);
int vs_actors_setActorFadeParameters(int, D_800F4538_unk58_2*);
int vs_actors_setActorTint(int, D_800F4538_unk58_2*);
void func_800A0870(int);
void vs_actors_setActorDisplayFlags(int actorId, int);
void vs_actors_setActorAnimationLocked(int, int);
int vs_field_actorCollisionBypass(int);
void vs_actors_resetActorRunMomentum(int);
void vs_actors_setActorWalkRunSpeeds(int, int, int);
u_int vs_actors_actorStateFlags(int actorId);
int func_800A1178(int arg0, int arg1, MATRIX* arg2, u_long* arg3);
int vs_actors_targetPoint(int, int, SVECTOR*, int);
int func_800A13EC(int, int, int*, int);
void vs_actors_restoreActorNeutralPose(int arg0);
