#include "common.h"
#include "146C.h"
#include "30DB0.h"
#include "38C1C.h"
#include "3A1A0.h"
#include "44F14.h"
#include "6E644.h"
#include "../SLUS_010.40/main.h"
#include "../SLUS_010.40/31724.h"
#include "../SLUS_010.40/32154.h"
#include "build/src/include/lbas.h"
#include "gpu.h"
#include <libgte.h>
#include <libetc.h>
#include <memory.h>
#include "pc/src/psx_types.h"
#ifdef ROOD_PC
#include "pc/src/widescreen.h"
#endif

typedef struct {
    u_char unk0[0x1FE0];
    u_short unk1FE0;
} D_800F2458_t;

typedef struct {
    u_char unk0;
    u_char unk1;
    u_char unk2;
    u_char unk3;
    int unk4;
    int unk8;
} func_800A0024_t;

typedef struct {
    int unk0;
    int unk4;
    int unk8;
    int unkC;
} D_800E8FB4_t;

typedef struct {
    u_char unk0[0x84];
    SVECTOR unk84;
} D_1F80035C_t;

void vs_field_setMovingPlatformActor(int, int);
int vs_field_createGimmickInstance(vs_battle_objectData*);
void vs_content_offsetModelTextureCoordinates(void*, int, int, int, int);
void* vs_content_copyModelGeometry(int* arg0, void* arg1, int arg2);
int _loadWep(vs_battle_objectData*);
int _parseWep(vs_battle_objectData*);
int _loadShp(vs_battle_objectData*);
int _parseShp(vs_battle_objectData*);
int vs_content_updateActorSequenceLoad(vs_battle_objectData*);
int _loadSeq(vs_battle_objectData*);
int _loadEtm(vs_battle_objectData*);
void _loadMaterialPalette(int index, int material);
int vs_actors_actorFacingDeltaToPoint(D_800F4538_t*, SVECTOR* arg1);
int vs_actors_pitchToPoint(D_800F4538_t* arg0, SVECTOR* arg1);
void vs_actors_restoreActorBasePose(int, int);
int vs_field_findGimmickBelow(D_800F45E0_t* arg0);
void vs_actors_applyActorSoundOverrides(int, D_800F4538_unk64*);
void vs_actors_headTrackingSpeed(D_800F4538_t* arg0);
int vs_field_setGimmickOrientation(int, int);
void vs_actors_beginActorShadowBlend(D_800F4538_t*, int);
void func_800A11D8(int, int, MATRIX*, u_long*);
/* 38C1C.c takes the output vector's address as an int. */
int func_800A141C(int arg0, int arg1, int arg2, int arg3);
int func_800A152C(int, int, int);
void func_800A2C48(D_800F4538_t*);
void vs_actors_clearActorMovementRequest(D_800F4538_t*);
int vs_field_sampleActorSupportHeight(short*, int, int, int);
int vs_field_findTopGimmickAtTile(int, int, int);
int vs_actors_setTextureAnimation(int, int, int, int);
void vs_actors_restartsTextureFrames(int);
void vs_actors_clearActorBonePoseLocks(D_800F4538_t*);
void vs_actors_initializeActorAnimationPose(void*, void*);
int vs_actors_findActorAnimationHeader(D_800F4538_t*, int, u_short**);
void vs_audio_actorSoundPanAndVolume(int, int*, int*, int);
int vs_actors_runAnimationEvents(D_800F4538_t*, D_800F4538_unkC54*, int);
void vs_actors_restartActorAnimationEvents(D_800F4538_t*);
void vs_audio_playActorSoundPair(D_800F4538_t*, int, int);
short vs_event_eventPlaybackState();
void vs_battle_aiAddAiTileOccupant(void*);
void vs_battle_aiRemoveAiTileOccupant(D_800F45E0_t*);

extern u_short _shpLbaOffsets[];
extern u_char _shpFileSectorSizes[];
extern u_char D_800E8960[];
extern u_short _etmLbaOffsets[];
extern u_char _etmFileSectorSizes[];
extern u_char _wepFileSectorOffsets[];
extern u_short vs_content_legacyWepDiscOffsets[];
extern u_char vs_battle_chainAbilityAnimationGroups[];
extern u_char _loadShpState;
extern u_char D_800E8F29;
extern u_char vs_actors_sharedSequenceBankIndex;
extern u_char _loadEtmState;
extern u_char D_800E8FA8[];
extern void* vs_field_gimmickModelTemplates[];
extern char D_800E8FC0;
extern int D_800E8FC4;
extern u_char _weaponMaterialPaletteMap[];
extern u_char vs_field_gimmickOrientationRotations[];
extern u_char D_800E9090[];
extern u_char D_800E909C[];
extern u_char vs_field_gimmickTemplateIndices[];
extern void* _wepFileData;
extern int _wepFileLoaded;
extern vs_main_CdFile _wepFile;
extern vs_main_CdQueueSlot* _wepCdSlot;
extern void* _shpData;
extern int _shpAllocated;
extern vs_main_CdFile _shpFile;
extern vs_main_CdQueueSlot* _shpCdSlot;
extern u_short* _etmData;
extern vs_main_CdFile _etmFile;
extern vs_main_CdQueueSlot* _etmFileCdSlot;
extern void* _wepFileEpilog;
extern u_char vs_actors_sequenceTexturePagesOneBased[];
extern u_char D_800F2450[];
extern D_800F2458_t D_800F2458;
extern VECTOR D_800F4438;
extern char vs_field_gimmickPaletteColors[];
extern int D_800F457C;
extern int D_800F4580;
extern int D_800F45D8;
extern short vs_actors_sharedSequenceAnimationIds[4][16];
extern char* vs_actors_sharedSequenceAnimationData[4][16];
extern short D_800F4938[4][16];
extern int vs_battle_chainAbilityAnimationSide;
extern u_int D_800F49E0;

int vs_battle_getEmptyObjectDataSlot(void);

int vs_battle_populateDataSlot(vs_battle_objectData* objectData);

int vs_battle_processObjectDataQueue(void);

void vs_field_setGimmickPaletteTable(void* arg0);

int vs_field_createGimmickInstance(vs_battle_objectData* arg0);

int vs_field_releaseGimmickInstance(int arg0);

void vs_content_offsetModelTextureCoordinates(void* arg0, int arg1, int arg2, int arg3, int arg4);

void* vs_content_copyModelGeometry(int* arg0, void* arg1, int arg2);

int _loadWep(vs_battle_objectData* arg0);

int _parseWep(vs_battle_objectData* objectData);

void _loadMaterialPalette(int index, int material);

int vs_content_releaseWeaponModel(int index);

inline int vs_content_findSharedActorModel(vs_battle_objectData* arg0);

int _loadShp(vs_battle_objectData* arg0);

extern u_char vs_actors_textureSlotOrigins[];
extern u_char vs_actors_textureSlotRowOffsets[];
extern u_char* D_800F22B4;
extern u_char D_800F22B8;
extern int D_800F22BC;
extern int D_800F22C0;
extern int D_800F22C4;
extern void* vs_actors_sharedSequenceData[];

void vs_content_bindSharedActorSequence(D_800F4538_t* arg0, int arg1);

int _parseShp(vs_battle_objectData* object);

extern u_short vs_content_legacySequenceDiscOffsets[], vs_content_legacyMaterialSequenceDiscOffsets[], vs_content_legacyMaterialModelSequenceDiscOffsets[][4];
extern u_char vs_content_legacySequenceSectorCounts[], vs_content_legacyMaterialSequenceSectorCounts[], vs_content_legacyMaterialModelSequenceSectorCounts[][4];
extern void* vs_content_legacySequenceFileBuffer;
extern int vs_content_legacySequenceBufferOwned;
extern vs_main_CdFile vs_content_legacySequenceFileLocation;
extern vs_main_CdQueueSlot* vs_content_legacySequenceDiscRequest;
int vs_content_updateActorSequenceLoad(vs_battle_objectData* object);

inline void vs_content_copyWeaponSequence(func_8009C378_t* arg0, func_8009C378_t* arg1);

extern u_char D_800E8F2B;
extern u_char* vs_content_legacySequenceParseData;
extern void* D_800F4768;

int _loadSeq(vs_battle_objectData* object);

void vs_content_bindSharedActorSequence(D_800F4538_t* arg0, int arg1);

int vs_content_releaseActorSequence(int arg0, int arg1);

void vs_actors_textureFrameMove(D_800F4538_t*, int, int);

int vs_actors_selectSharedSequenceBank(u_int arg0);

int vs_actors_sharedSequenceBankId(void);

int vs_content_releaseActorModel(int arg0);

int vs_content_releaseSequenceEffectTexture(int arg0);

int _loadEtm(vs_battle_objectData* objdata);

void func_8009D458(void);

void vs_actors_resetActorMotionState(int arg0, int arg1);

void vs_actors_resetAllActorMovement(void);

void func_8009D7E8(int arg0, int arg1);

void func_8009D854(int arg0);

void vs_actors_resetActorWeaponVisibilityBlend(int arg0);

void vs_actors_setActorWeaponDrawn(int arg0, int arg1, int arg2);

void vs_actors_playActorWeaponDrawSound(int arg0, int arg1);

void vs_actors_stopActorMotion(int arg0);

void vs_actors_clearActorMovementModes(int arg0);

void vs_battle_playMeleeAttackAnimation(int arg0, SVECTOR* arg1, int arg2);

void func_8009DDF4(int arg0, SVECTOR* arg1);

void func_8009DF3C(int arg0, int arg1);

void func_8009E070(int arg0, SVECTOR* arg1, int arg2);

int vs_actors_actorFacingDeltaToPoint(D_800F4538_t* arg0, SVECTOR* arg1);

int vs_actors_pitchToPoint(D_800F4538_t* arg0, SVECTOR* arg1);

void func_8009E2E0(int arg0, SVECTOR* arg1, int arg2);

int func_8009E480(void);

int vs_actors_isActorActionAnimationBusy(int arg0);

void func_8009E5C4(int arg0);

void vs_actors_restoreActorNeutralPose(int arg0);

void vs_actors_restoreActorBasePose(int actorId, int animation);

void func_8009EA14(int arg0, SVECTOR* arg1);

void vs_battle_playChainAbilityAnimation(int arg0, SVECTOR* arg1, int arg2, int arg3);

void vs_battle_playDefenseAbilityAnimation(int arg0, SVECTOR* arg1, int arg2);

extern char vs_battle_specialActionPitch;

void func_8009EFEC(int actorId, SVECTOR* target, int mode);

void func_8009F298(int arg0, SVECTOR* arg1, int arg2);

void vs_audio_playGimmickSoundPair(void*, int, int);

void vs_actors_beginActorDeath(int actorId, void* context, int animate);

void vs_field_dropUnsupportedGimmickStack(int arg0);

int vs_field_findGimmickBelow(D_800F45E0_t* arg0);

int vs_field_gimmickSubtype(int arg0);

void vs_actors_setWeaponBrightnessOverride(int arg0, int arg1, int arg2);

int vs_actors_animationBattleWindowFrame(int arg0);

void func_8009F940(int arg0, int arg1, D_800F4538_unk58_2* arg2, D_800F4538_unk58_2* arg3);

void vs_actors_setActorSoundOverrides(int arg0, D_800F4538_unk64* arg1);

void vs_actors_applyActorSoundOverrides(int arg0, D_800F4538_unk64* arg1);

int vs_actors_setActorTravelAnimation(int arg0, int arg1);

int vs_actors_startHeadTracking(int arg0, int arg1, SVECTOR* arg2, int arg3);

void vs_actors_headTrackingSpeed(D_800F4538_t* arg0);

int vs_actors_enableModelPalettePulse(int arg0, int arg1, int arg2);

int vs_actors_disableModelPalettePulse(int arg0, int arg1);

void vs_actors_attachActorToParentBone(int arg0, int arg1, int arg2, int arg3);

int func_8009FFA8(int arg0, int arg1);

int vs_field_gimmickPlacement(int arg0, func_800A0024_t* arg1);

int vs_field_setGimmickOrientation(int index, int material);

int vs_actors_queueActorAnimation(int arg0, int arg1, int arg2, int arg3);

int vs_actors_playActorAnimation(int actorId, int animation, int arg2, int blendFrames);

int vs_actors_playActorAnimationAtFrame(int arg0, int arg1, int arg2, int arg3);

void vs_actors_beginActorShadowBlend(D_800F4538_t* arg0, int arg1);

int vs_actors_setActorTint(int arg0, D_800F4538_unk58_2* arg1);

void func_800A0870(int arg0);

void vs_actors_setActorDisplayFlags(int actorId, int arg1);

void vs_actors_setActorAnimationLocked(int arg0, int arg1);

void vs_field_setActorCollisionBypass(int arg0, int arg1);

void vs_actors_resetActorRunMomentum(int arg0);

void vs_actors_setActorWalkRunSpeeds(int arg0, int arg1, int arg2);

u_int vs_actors_actorStateFlags(int actorId);

int vs_actors_actorPlacement(int arg0, void* arg1);

int func_800A1178(int arg0, int arg1, MATRIX* arg2, u_long* arg3);

void func_800A11D8(int arg0, int arg1, MATRIX* arg2, u_long* arg3);

int vs_actors_targetPoint(int arg0, int arg1, SVECTOR* arg2, int arg3);

