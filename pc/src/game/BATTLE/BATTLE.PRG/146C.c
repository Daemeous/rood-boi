#include "pc/src/gte_value_core.h"
#include "common.h"
#include "146C.h"
#include "2842C.h"
#include "2EA3C.h"
#include "30DB0.h"
#include "38C1C.h"
#include "3A1A0.h"
#include "40564.h"
#include "44F14.h"
#include "4A0A8.h"
#include "573B8.h"
#include "58578.h"
#include "5BF94.h"
#include "6E644.h"
#include "pc/src/game/SLUS_010.40/31724.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "pc/src/game/BATTLE/INITBTL.PRG/18.h"
#include <abs.h>
#include "insertTPage.h"
#include <libetc.h>
#include <libgpu.h>
#include <libgte.h>
#include <limits.h>
#include <memory.h>
#include <rand.h>
#include "pc/src/psx_types.h"
#ifdef ROOD_PC
#include "pc/src/widescreen.h"
#include "pc/src/classic/classic_hook.h"
/* environment.cpp: whether solid room geometry hides a rain drop (its item is left out of the hardware picture;
 * native VRAM draws it) */
int pc_weather_drop_hidden(int x, int y, int z);
int pc_charge_skip(int phase); /* pc/src/charge_skip.inc: battle charging skip */
int pc_charge_skip_hold(void);
void pc_charge_skip_name(int now);
#endif

/* The block-scope static of the original function: the Zig reaches this storage through the views
 * (battle_statics.py, STATE.tsv). */
/* Passed as a 32-bit int: needs a guest-range address. */
static char func_80074860__sp20[4] __attribute__((used));

typedef struct {
    u_char targetActor;
    signed char targetLimb;
    char hitThreshold;
    char unk3;
    short hp;
    short mp;
} func_8008631C_t2;

typedef struct {
    short value0;
    short value1;
} limbEffects_t;

typedef struct {
    func_8008631C_t2 target;
    short risk;
    short strength;
    short intelligence;
    short agility;
    short maxHp;
    short maxMp;
    int removeStatuses;
    int inflictStatuses;
    u_int hpEffect : 2;
    u_int mpEffect : 2;
    u_int riskEffect : 2;
    u_int strengthEffect : 2;
    u_int intelligenceEffect : 2;
    u_int agilityEffect : 2;
    u_int maxHpEffect : 2;
    u_int maxMpEffect : 2;
    u_int unk1C_16 : 2;
    u_int weaponDpEffect : 2;
    u_int weaponPpEffect : 2;
    u_int armorDpEffect : 2;
    u_int shieldPpEffect : 2;
    u_int unk1C_26 : 2;
    u_int unk1C_28 : 4;
    union {
        limbEffects_t effects;
        u_int hp;
    } limbs[6];
    short weaponDp;
    short weaponPp;
    short armorDp;
    short shieldPp;
    int unk40;
} _hitEntity_t;

typedef struct {
    u_short actionIndex;
    char unk2;
    char unk3;
    _hitEntity_t primaryTarget;
    u_short unk48;
    u_short targetCount;
    _hitEntity_t targets[30];
    SVECTOR unk844;
} vs_battle_ActionRequest;

typedef struct {
    SVECTOR unk0;
    char unk8;
    char unk9;
    char unkA;
    char unkB;
    char unkC;
    char unkD;
    short unkE;
    int unk10;
    int unk14;
} D_800F19CC_t5;

typedef struct {
    u_int unk0;
    u_int unk4;
    vs_battle_ActionRequest unk8;
    vs_battle_ActionRequest queue[4];
    int unk2984;
    int unk2988;
    u_char unk298C;
    u_char unk298D;
    char unk298E;
    char unk298F;
    SVECTOR unk2990;
    SVECTOR unk2998;
    char unk29A0;
    char unk29A1;
    short unk29A2;
    char rangeX;
    char rangeY;
    char rangeZ;
    char rangeAngle;
    SVECTOR rangeOrigin;
    char unk29B0;
    char unk29B1;
    short unk29B2;
    char areaX;
    char areaY;
    char areaZ;
    char unk29B7;
    SVECTOR areaOrigin;
    D_800F19CC_t5 previews[23];
    int unk2BE8;
    int unk2BEC;
    int unk2BF0;
    int unk2BF4;
    int unk2BF8;
    int unk2BFC;
    u_short previewCount;
    char unk2C02;
    char chainAbilitySlot;
    char previousChainInput;
    char battleAbilityInputSuccessful;
    char battleAbilityInputAttempted;
    char unk2C07;
    u_short battleAbilityTimer;
    u_short battleAbilityTargetWindow;
    short unk2C0C;
    short unk2C0E;
} vs_battle_ActionState;

typedef struct {
    char spawnSlot;
    char unk1;
    short aiFlags;
    short currentHP;
    short maxHP;
    short currentMP;
    short maxMP;
    short totalStrength;
    short totalIntelligence;
    short totalAgility;
    short limbHp[6];
    int statuses;
    int placement;
} vs_actors_RoomActorRecordWrite;

typedef struct {
    _mpdRoomSectionA* unk0;
    func_8006EBF8_t unk4;
} vs_field_InteractionFocus;

typedef struct {
    signed char unk0;
    signed char unk1;
    signed char unk2;
    char unk3;
    short unk4;
    short unk6;
} vs_field_RoomTint;

typedef struct {
    int unk0;
    int unk4;
    int unk8;
} func_8006EBF8_t2;

typedef struct {
    short unk0;
    short unk2;
    short unk4;
    short unk6;
    short unk8;
    short unkA;
    char unkC;
    char unkD;
    char unkE;
    char unkF;
} D_800F1DD4_t;

typedef struct {
    short unk0;
    short unk2;
    short unk4;
    short unk6;
    int unk8;
    char unkC[3][4];
    int unk18;
    int unk1C;
    int unk20;
    char unk24[4];
} D_800E8594_t;

typedef struct {
    int unk0[4];
} func_80077DF0_t2;

typedef struct {
    int unk0[17];
    func_80077DF0_t2 unk44;
} func_80077DF0_t3;

typedef struct {
    VECTOR position;
    VECTOR lookAt;
} func_80077DF0_t;

typedef struct {
    char unk0;
    char unk1;
    char unk2;
    char unk3;
    int unk4;
    int unk8;
    int unkC;
    int unk10;
    int unk14;
    int unk18;
    int unk1C;
    int unk20[8];
    int unk40;
} func_80085A34_t;

typedef struct {
    short unk0;
    char unk2;
    char unk3;
    short unk4;
    short unk6;
    char targetActor;
    char unk9;
    char unkA;
    char unkB;
    int unkC;
    short unk10;
    short unk12;
    func_8008631C_t2 unk14;
    int unk18[50];
#ifdef ROOD_PC
    /* vs_effects_beginSecondaryEffects copies 0xE8 bytes (func_800CF0E8_t) from this request; the PS1 local is 0xE4 and the last four
     * bytes (unkE0.vz and pad) are whatever its stack held. Here they were dead host stack, which differs
     * between owned and original routes (pc/docs/Oracle.md, known hazards): make the local the size it is read. */
    int unkE4;
#endif
} func_8006C4C4_t;

typedef struct {
    int unk0;
    _mpdRoomSectionA* unk4;
} vs_field_GimmickAnimationJob;

typedef struct {
    short unk0;
    short unk1;
    short unk2;
    short unk3;
} func_8008D2C0_t;

typedef struct {
    int unk0;
    int unk4;
    short unk8;
    char unkA;
    char effect;
    short unkC;
    short unkE;
    int unk10;
    char unk14;
    char unk15;
    char unk16;
    char enemyClass;
} func_8006F630_t1;

typedef struct {
    u_short actionId;
    char unk2;
    char unk3;
} func_8006F630_t2;

typedef struct {
    char bytes[8];
} actorSpawnSettings;

typedef struct {
    u_char type;
    u_char index;
    u_char hidden;
    u_char unk3;
    u_char id;
    u_char slot;
    u_char flagTest;
    u_char statTest;
    u_short flagValue;
    u_short statValue;
    u_char x;
    u_char direction;
    u_char z;
    u_char height;
    actorSpawnSettings settings;
    u_char unk18[2];
    u_short itemId;
    u_short item2Id;
    u_short item3Id;
    u_char itemCount;
    u_char item2Count;
    u_char item3Count;
    u_char facing;
    u_char unk24;
    u_char variant;
    u_char unk26;
    u_char sceneTest;
} mpdEnemySpawn;

typedef struct {
    u_char unk0;
    u_char hidden;
    short aiFlags;
    short currentHP;
    short maxHP;
    short currentMP;
    short maxMP;
    short strength;
    short intelligence;
    short agility;
    short limbHP[6];
    short unk1E;
    int statuses;
    u_char x;
    u_char direction;
    u_char z;
    u_char height;
} savedEnemyState;

typedef struct {
    int characterIds;
    int shieldIds;
    int model;
    int unkC;
    int blade;
    int unk14;
    int shield;
    int unk1C;
    int effect;
    int effectSize;
    int weaponEffect;
    int weaponEffectSize;
} zudHeader;

typedef struct {
    u_char type;
    u_char unk1;
    u_char unk2;
    u_char unk3;
    u_short flag;
    u_char flagMin;
    u_char flagMax;
    u_char slot;
    u_char unk9;
    u_char x;
    u_char z;
    u_char y;
    u_char direction;
    u_short model;
    u_char unk10[0x18];
} mpdObjectSpawn;

typedef struct {
    u_char actorId : 4;
    char unk0_4 : 4;
    char unk1;
    char unk2;
    char unk3;
    short unk4;
    short unk6;
    int unk8;
    int unkC;
    int unk10;
    int unk14;
} func_8006F630_t3;

typedef struct {
    int count;
    _mpdRoomDoorSection_t* values[32]; /* the PS1's 0x1D08..0x1D28 held a count and 7 words; a room with more matching doors overran it */
} vs_field_DoorMatchList;

typedef struct {
    psx_long mpdOffset;
    int mpdLen;
    psx_long enemiesOffset;
    int enemiesLen;
    psx_long timOffset;
    int timLen;
    int musicId;
} _zndHeader;
PSX_ASSERT_SIZE(_zndHeader, 0x1C);

typedef union {
    char u8[4];
    int s32;
} func_800765B0_t;

typedef struct {
    int roomSectionOffset;
    u_short roomSectionLen;
    int clearedSectionOffset;
    u_short clearedSectionLen;
    int scriptSectionOffset;
    u_short scriptSectionLen;
    int doorSectionOffset;
    u_short doorSectionLen;
    int enemySectionOffset;
    int enemySectionLen;
    int lootSectionOffset;
    int lootSectionLen;
} _mpdHeader;

typedef struct {
    int unk0;
    int unk4;
    int unk8;
    int unkC;
    int unk10;
    int unk14;
    int unk18;
    int unk1C;
    int unk20;
} func_800A1108_t;

int _loadMpdRoomSection(int, void* data);
int _dropMisc(vs_battle_loot*, vs_battle_uiMisc*);
int _dropMiscRand(vs_battle_loot*, vs_battle_uiMiscRand*);
void _snapLookAtToPosition(VECTOR*);
void func_8006C350(void);
void vs_field_pauseFieldForMenu(void);
void vs_field_resumeFieldAfterMenu(void);
int vs_battle_playWeaponHitSounds(int);
void func_80069DEC(int, int);
int _removeActorAtIndex(u_int, int);
void _applyAccessoryStats(vs_battle_uiAccessory*, _accessoryIntermediate*);
void _applyArmorStats(vs_battle_uiArmor* arg0, _armorIntermediate* arg1);
void vs_battle_copyUiEquipmentStats(
    vs_main_inventoryArmor*, vs_battle_uiEquipment* equipment);
void vs_items_removeEquipmentBoostOnEquip(void);
void vs_items_removeArmorEnchantmentOnEquip(void);
void _updateEnemyKills(vs_battle_actor*);
void _drawScreenTransition(void*);
void _updateWeaponKillStreak(int);
void _clampPositionToZoneBounds(VECTOR*, VECTOR*);
int _computeStepAndDisplacement(SVECTOR* arg0, VECTOR*, VECTOR*, int, int);
int _stepTowardTarget(SVECTOR*, int, int);
void vs_field_updateCameraOrbitInput(void);
void _computeCameraTransition(VECTOR*);
int _stepCameraTransition(void);
void _setRoomSeen(void);
void func_8006F5CC(void);
void vs_battle_beginPendingAbilityReward(void);
void vs_battle_grantPendingAbilityUnlock(void);
void vs_battle_advanceBattleAbilitySequence(void);
void vs_battle_startActionAnimation(void);
void vs_battle_filterAreaActionTargets(void);
void vs_battle_collectEnvironmentActionTargets(void);
void vs_battle_commitSelectedAction(int);
void vs_battle_startActionPresentation(void);
void vs_battle_initializeActionTargeting(int);
void vs_battle_beginActorActionSelection(int, u_short);
void _endBattleCameraTransition(void);
void vs_event_prepareCutsceneActors(void);
void vs_field_triggerActorTrap(_mpdRoomSectionA*, func_8006EBF8_t*, int);
void vs_field_tryOpenChest(_mpdRoomSectionA*, int);
int vs_field_tileInFrontOfActor(func_8006EBF8_t*, char*);
void vs_field_updateCameraTileConstraint(int arg0, func_8006EBF8_t_fields* arg1);
void vs_actors_initializeActorDefaults(u_int, int, int, int, int);
void vs_actors_queueEquipmentModelLoad(vs_battle_actor*, int, int, int, int);
vs_battle_actor_dat* vs_actors_allocateActorWithEquipment(
    int, int, int, int, int, int, vs_battle_objectData_flags*, int);
void vs_actors_registerLoadedActor(vs_battle_actor*, int, int, int, int, int);
int vs_battle_actionTargetDistance(void);
void vs_battle_actionCameraMidpoint(SVECTOR*);
int vs_battle_canTriggerBattleAbility(int);
int vs_battle_resolveActorDefeat(int arg0, int arg1, int arg2);
void _computeSphericalOffset(VECTOR*, VECTOR*, _sphericalValues*);
void _setCameraPositionFromAngles(_sphericalValues* arg0);
void _setCameraLookAtFromAngles(_sphericalValues* arg0);
void vs_render_transformRoomPalettes(int, int, int, short, short);
void vs_render_transformSceneColors(int, int, short, short, short);
void vs_render_transformActorColors(int arg0, int arg1, int arg2, short arg3, short arg4, short arg5);
void vs_battle_beginRewardPause(void);
void vs_battle_showPendingRewardUnlock(void);
void vs_battle_endRewardPause(void);
void func_8007B508(void);
void vs_battle_openPendingLootRewards(void);
void func_8007B7DC(void);
void vs_state_resetSceneTimer(void);
int func_8007BD8C(int arg0);
void vs_actors_enableActorModel(int);
void vs_actors_disableActorModel(int);
void vs_battle_setActorBattleMode(int, int);
int vs_field_beginRoomTransition(_mpdRoomDoorSection_t*, int, int);
int vs_field_beginZoneTransition(_mpdRoomDoorSection_t*, int, int);
int vs_field_teleportToLocation(int, int, int, int, int);
void vs_battle_initialiseCameraFromSpherical(
    VECTOR* arg0, VECTOR* arg1, int arg2, int arg3);
int _getLocationId(int, int);
void func_8007D360(void);
void _calculateWeaponClassAffinity(vs_battle_actor2*);
int vs_battle_itemIdIsInInventory(int);
short _getDefenseGemBuff(vs_action_t*, vs_battle_actor2*, int);
int _doesAttackHit(
    vs_action_t* arg0, _hitEntity_t* arg1, _hitEntity_t* arg2, int arg3, int arg4);
int vs_field_rollRiskLockBreak(void);
int _hitPrerequisiteCanApplyEffect(int arg0, u_char* arg1);
int _getEnemyClass(u_char*);
void vs_battle_distributeHitLimbDamage(vs_action_t*, _hitEntity_t*, short);
void vs_battle_distributeLimbRecovery(vs_action_t*, _hitEntity_t*, short);
void _adjustDpPp(vs_action_t*, vs_battle_actor2*, vs_battle_actor2*, int);
void vs_items_accumulateHitEquipmentPp(vs_action_t*, vs_battle_actor2*, vs_battle_actor2*, int);
void _triggerArmorAffinityChange(vs_action_t*, vs_battle_uiEquipment*, int, int, int);
void _triggerClassChange(vs_action_t* arg0, vs_battle_uiEquipment*, int, int, int);
int _setApplicableStatuses(int, _hitEntity_t*);
int _setInflictedStatuses(int, _hitEntity_t*);
static short _getPhysicalAttackDamage(
    vs_action_t*, _hitEntity_t*, _hitEntity_t*, int, int, int);
short _calculateStatChange(vs_action_t*, _hitEntity_t*, _hitEntity_t*, int, int);
int _getActionCost(u_int, vs_battle_actor2*, int);
void vs_battle_deriveLimbInjuryStatuses(_hitEntity_t*);
void vs_battle_applyActionHitEffect(
    vs_action_t* arg0, _hitEntity_t* arg1, _hitEntity_t* arg2, int arg3, int arg4);
void vs_battle_resolveActionTargets(int, vs_battle_ActionRequest*, vs_battle_ActionRequest*, int);
void vs_battle_removeActorStatuses(int, vs_battle_actor2*);
void _applyBattleAbilityEffect(_hitEntity_t*);
void vs_battle_updateActorStatusTimers(vs_battle_actor2*);
int vs_state_advanceSceneTimer(void);
int vs_battle_updateActorVitals(void);
void func_80088CA0(void);
void vs_field_spawnRoomActors(void);
void func_80089CE4(void);
void func_80089D04(void);
void vs_items_clearRoomLootList(void);
int vs_field_releaseOwnedRoomSections(int arg0);
void vs_field_uploadRoomPalettes(void);
void vs_field_offsetRoomTextureIds(int arg0, int* arg1);
int vs_field_setMinimapVisible(char arg0);
void vs_field_clearGeometryOcclusion(void);
static void _loadZnd(int id);
void _loadMpd(int arg0);
void _loadZndTims(void*);
int vs_field_enableOwnedRoomRendering(int);
int vs_field_disableOwnedRoomRendering(int);
void vs_field_copyZonePaletteRow(void* arg0, int arg1, int arg2, int arg3);
void vs_field_setRoomAnimationsPaused(int arg0);
void _vsBattleNop2(int arg0);
int _getCollisionMapDimensions(int arg0);
_zoneContextBounds* _getMapBounds(int arg0);
void vs_field_setEntranceDoorRecord(char (*arg0)[12]);
void vs_field_findEntranceDoorNearTile(int, int, int);
_mpdRoomSectionA* vs_field_interactiveDoorAtTile(int, int, int);
void vs_field_toggleDoorCollision(_mpdRoomSectionA*);
int vs_field_beginDoorToggle(_mpdRoomSectionA* arg0);
int vs_field_startDoorAnimation(_mpdRoomSectionA*);
void vs_field_queueGimmickAnimation(int arg0, _mpdRoomSectionA* arg1);
int vs_field_finishDoorAnimation(_mpdRoomSectionA* arg0);
int func_8008C2C0(int arg0, int arg1, int arg2, int arg3);
int vs_field_setMovingPlatformActor(int, int);
void vs_field_initializeMovingPlatforms(void);
void vs_field_resetMovingPlatformWaypoint(int, int);
_mpdRoomSection9* vs_field_findRoomTriggerAtTile(int, int, int);
_mpdRoomSection13* vs_items_treasureChestAtTile(int arg0, int arg1, int arg2);
void vs_field_freezeMovingPlatforms(int);
void vs_field_setMovingPlatformsPaused(int arg0);
void func_8008D5FC(_mpdRoomSectionA* arg0);
void vs_field_toggleRoomSwitch(_mpdRoomSectionA*);
void vs_items_restoreRoomChestStates(void);
int _getDoorId(int);
void vs_battle_aiDisengageActorById(int);
short vs_field_floorHeightAt(int arg0, int arg1);
void vs_field_setRoomTriggerEnabled(_mpdRoomSection9* arg0, int arg1);
void vs_field_drawRoomTriggerMarker(int arg0, int arg1, short arg2, u_int arg3);
_mpdRoomSection13* vs_items_roomTreasureChests(int* arg0);
_mpdRoomSectionA* func_8008E3B8(int* arg0);
void vs_audio_setRoomAmbienceDucked(int arg0);
void func_800E4C64(int);
void vs_field_setPlatformFreezeAppearance(int);
void _loadMpdLootSection(void* arg0);
void vs_field_markSigilDoorUnlocked(int);
void func_8008E8F8(void);
void vs_field_initializeRoomBackgroundColors(void);
int func_8008EA90(int arg0);
int vs_field_releaseTiledBackground(void);
void func_8008FA14(D_800F1DD4_t*);
void func_80090A1C(D_800F1DD4_t*);
void vs_field_releaseWeatherParticles(void);
int* vs_field_findRoomGeometryGroup(int);
void func_80091AC0(int);
int vs_field_startRoomEffect(int);
void vs_field_releaseRoomEffects(void);
int vs_field_isRoomEffectActive(int);
void vs_field_finishRoomTiltRotation(int);
void vs_field_restoreRoomEffectSoundVolume(void);
void vs_battle_queueWorldLockedLabel(void*);
void func_80093B68(int arg0, int arg1, int arg2, int arg3);
int vs_battle_renderBattleAbilityTimingResult(int);
void func_80093FEC(int, int, int, int);
void vs_field_swapRoomGeometryRedGreen(int);
void vs_battle_setActorMarker(int, int);
void func_80096768(int, int, int);
void func_80096FF0(int);
void func_800971D4(void);
void vs_render_drawRoomGeometry(void*, int);
void vs_render_setRoomTextureLodDepth(int);
void vs_render_updateRoomGroupTransforms(void*);
void vs_render_tintRoomGeometryPrimitives(void*, void*, vs_field_RoomTint*, int);
void vs_field_setGimmickPaletteTable(u_short*);
void func_8009D458(void);
void vs_actors_resetAllActorMovement(void);
void func_8009D7E8(int, int);
void func_8009DF3C(int, int);
void func_8009E070(int, short*, int);
int func_8009E480(void);
u_int vs_actors_isActorActionAnimationBusy(int);
void func_8009EA14(int, SVECTOR*);

void vs_battle_aiSetActorVerticalMovementSteps(int, int, int);

extern u_char vs_battle_statusParams5Duration;
extern u_char vs_battle_statusParams11Duration;
extern u_char vs_battle_statusParams13Duration;
extern u_char vs_battle_statusParams14TickInterval;
extern u_char vs_battle_statusParams17TickInterval;
extern u_char vs_battle_statusParams19Duration;
extern char D_8005FFAF;
extern D_800F18EC_t* D_800F18EC;
extern int vs_field_legacyBackgroundGradientPackets[];
extern char D_800E8184[];
extern char _wepIdCategories[];
extern u_char D_800E8200[];
extern int D_800E8204[];
extern u_short vs_battle_bladePpDamageBonuses[];
extern int (*_hitFunctions[])(vs_action_t*, _hitEntity_t*, _hitEntity_t*, int, int);
extern int (*_attackPrerequisiteFunctions[])(vs_action_t*, char*);
extern void (*_actionEffectMediators[])(
    vs_action_t*, _hitEntity_t*, _hitEntity_t*, int, int);
extern short (*_statCalculators[])(vs_action_t*, _hitEntity_t*, _hitEntity_t*, int, int);
extern int vs_battle_vitalsTickCounter;
extern int vs_battle_regeneratingLimb;
extern int vs_battle_targetingRiskTimer;
extern short vs_field_lockedDoorIndexRanges[];
extern char vs_field_lockedDoorRoomPairs[][4];
extern MATRIX vs_field_triggerMarkerMatrix;
extern D_800E8594_t vs_field_triggerMarkerVertex;
extern char vs_field_triggerMarkerRecordTail[];
extern RECT vs_field_triggerMarkerTextureSource;
extern RECT D_800E85C4;
extern int vs_field_isMenuRequestQueued;
extern int vs_effects_tintedActor;
extern int D_800F1860;
extern int D_800F1864;
extern int D_800F1868;
extern void* vs_render_legacyCurrentOrderingTable;
extern int _firstPersonViewEnabled;
extern void* D_800F18E4;
extern u_int _cameraMode;
extern char vs_content_overlayWorkspaceOwner;
extern int D_800F18FC;
extern int D_800F1908;
extern int vs_field_isFieldRenderingEnabled;
vs_field_InteractionFocus vs_field_interactionFocus;
extern vs_battle_actor* D_800F192C;
extern int D_800F1968;
extern u_int D_800F196C;
extern int vs_field_isRoomVisitPending;
extern char vs_render_legacyTexturePageReservations[];
extern int D_800F19C8;
extern vs_battle_ActionState* vs_battle_actionState;
extern u_int D_800F1A00;
extern u_int D_800F1A04;
extern u_int _lastValue;
extern int vs_battle_criticalHitOccurred;
extern short _armorDpAdjustmentAmounts[];
extern int D_800F1A20;
extern short vs_render_framebufferEffectRotation[];
extern short vs_render_framebufferEffectRoll;
extern int vs_render_framebufferEffectScale[];
extern int vs_render_framebufferEffectUsesPreviousFrame;
extern u_int vs_render_framebufferEffectDepth;
extern int D_800F1A50[];
extern vs_render_FramebufferEffectTranslation vs_render_framebufferEffectOffset;
extern P_CODE vs_render_framebufferEffectTint;
extern char D_800F1A9C[];
extern int vs_render_framebufferEffectState;
extern int vs_render_framebufferEffectBlendMode;
extern u_short vs_field_areRoomUpdatesPaused;
extern vs_field_RoomTint D_800F1BB0;
extern short vs_field_isRoomTintActive;
extern void* D_800F1BB8;
extern char D_800F1BBC;
extern short vs_battle_doorEntered;
/* Defined here: the room and zone records hold pointers among their words, so they cannot be imports (LP64). */
_mpdRoomSection vs_battle_roomData;
vs_view_t vs_view;
/* The frame's view for the GTE maths (game/common/gte.zig): the camera's rotation and translation, set where the frame set
 * the GTE's registers. */
vs_gte_view vs_frame_view;
vs_scratch_t vs_scratch;
unsigned char vs_pad[0x408] __attribute__((aligned(16)));
vs_nav_t vs_nav;
_zoneContext_t _zoneContext;
extern u_short vs_field_entranceDoorPlatformFlags;
extern void* D_800F1CC0;
extern char vs_field_isMinimapVisible;
extern u_short vs_field_roomAnimationTicks;
vs_field_DoorMatchList vs_field_matchingDoorRecords;
vs_field_GimmickAnimationJob vs_field_gimmickAnimationQueue[8];
extern short vs_field_pendingGimmickAnimationCount;
extern u_short vs_field_triggerMarkerFrameTicks;
extern int vs_field_backgroundGradientColors[5];
extern char D_800F1DC7;
extern short vs_field_platformFreezeFrames;
extern signed char vs_field_areMovingPlatformsPaused;
extern char vs_field_isPlatformFreezeTintActive;
extern u_char vs_field_platformBounceFrames[];
extern short D_800F1D98[];
extern vs_main_CdQueueSlot* vs_field_backgroundTextureDiscRequest;
extern vs_main_CdQueueSlot* vs_field_backgroundLayoutDiscRequest;
extern short vs_field_backgroundOffsetX;
extern short vs_field_backgroundOffsetY;
extern short vs_field_backgroundParallaxScaleXFixed12;
extern short vs_field_backgroundParallaxScaleYFixed12;
extern short vs_field_weatherAmbientSoundId;
extern short vs_field_legacyBackgroundTextureReservationId;
extern short vs_field_weatherParticleCount;
extern short vs_field_weatherParticleCapacity;
extern short vs_field_weatherIntensity;
extern char vs_field_isSolidBackground;
extern char vs_field_legacyBackgroundTexturePage;
extern u_char vs_field_isTiledBackgroundEnabled;
extern char vs_field_roomWeatherKind;
extern char vs_field_isWeatherVariationEnabled;
extern vs_battle_backgroundLayout* vs_field_loadedBackgroundLayout;
extern u_long* vs_field_legacyBackgroundTimBuffer;
extern D_800F1DD4_t* vs_field_weatherParticles;
extern short D_800F5160;
extern short D_8005FFB0;

void vs_battle_armDefenseAbilityTimingWindow(int arg0);

void func_80069D14(void);

void vs_field_updateCameraFacingState(void);

void vs_actors_clearRoomEnemyMemory(void);

void func_80069DEC(int mapId, int zndId);

void* vs_actors_findRememberedEnemy(int arg0);

void* vs_actors_acquireRememberedEnemy(int arg0);

void vs_actors_rememberEnemy(int arg0, int arg1);

int _insertActor(vs_battle_actor* actor);

int _removeActor(vs_battle_actor* actor);

int _removeActorAtIndex(u_int index, int arg1);

void vs_battle_applyWeaponStats( vs_battle_uiWeapon* target, vs_battle_weaponIntermediate* source);

void vs_battle_applyShieldStats( vs_battle_uiShield* target, vs_battle_shieldIntermediate* source);

void _applyAccessoryStats(vs_battle_uiAccessory* target, _accessoryIntermediate* source);

void _applyArmorStats(vs_battle_uiArmor* target, _armorIntermediate* source);

void vs_battle_copyInventoryBladeStats( vs_battle_uiEquipment* target, vs_main_inventoryBlade* source);

void vs_battle_copyInventoryGripStats( vs_battle_uiEquipment* target, vs_main_inventoryGrip* source);

void vs_battle_copyInventoryArmorStats( vs_battle_uiEquipment* target, vs_main_inventoryArmor* source);

void vs_battle_copyInventoryGemStats( vs_battle_uiEquipment* target, vs_main_inventoryGem* source);

void vs_battle_applyWeapon(vs_battle_uiWeapon* target, vs_main_inventoryWeapon* source);

void vs_battle_applyShield(vs_battle_uiShield* target, vs_main_inventoryShield* source);

void vs_battle_applyArmor(vs_battle_uiArmor* target, vs_main_inventoryArmor* source);

void vs_battle_applyAccessory( vs_battle_uiAccessory* target, vs_main_inventoryArmor* source);

void vs_battle_nop0(vs_battle_actor2* actor __attribute__((unused)));

void vs_items_removeEquipmentBoostOnEquip(void);

void vs_items_removeWeaponEnchantmentOnEquip(void);

void vs_items_removeArmorEnchantmentOnEquip(void);

void vs_battle_copyUiBladeStats( vs_main_inventoryBlade* target, vs_battle_uiEquipment* source);

void vs_battle_copyUiGripStats( vs_main_inventoryGrip* target, vs_battle_uiEquipment* source);

void vs_battle_copyUiGemStats(vs_main_inventoryGem* target, vs_battle_uiEquipment* source);

void vs_battle_copyUiEquipmentStats( vs_main_inventoryArmor* target, vs_battle_uiEquipment* source);

int _dropWeaponRand(vs_battle_lootedWeapon* looted, vs_battle_uiWeapon* base);

int _dropShieldRand(vs_battle_lootedShield* looted, vs_battle_uiShield* base);

int _dropAccessoryRand(vs_battle_lootedAccessory* looted, vs_battle_uiAccessory* base);

int _dropArmorRand(vs_battle_lootedArmor* looted, vs_battle_uiArmor* base);

int _dropMisc(vs_battle_loot* looted, vs_battle_uiMisc* base);

int _dropMiscRand(vs_battle_loot* looted, vs_battle_uiMiscRand* base);

void _dropLoot(vs_battle_actor* enemy);

void _updateEnemyKills(vs_battle_actor* enemy);

void _updateWeaponKillStreak(int arg0 __attribute__((unused)));

void _enemyDefeated(int arg0, int actorId, int arg2);

void vs_actors_freezeAllActorAnimations(void);

void vs_actors_unfreezeAllActorAnimations(void);

void func_8006C2FC(void);

void func_8006C350(void);

void vs_field_pauseFieldForMenu(void);

void vs_field_resumeFieldAfterMenu(void);

void func_8006C480(int arg0, int arg1);

void func_8006C4A4(int arg0, int arg1);

void func_8006C4C4(int arg0, _hitEntity_t* arg1, int targetLimb);

int vs_battle_playWeaponHitSounds(int arg0);

int func_8006C84C(int arg0);

void vs_battle_applyActionSourceEffects(int arg0, func_8006CE70_t* arg1);

/* PC only: the decomp's exact-matching C for vs_battle_applyActionTargetEffects (rood-reverse
 * .work/ultracode/r12-e/cb9.c, score 0). The decomp rejected it upstream only
 * for its conditional dead store, which is harmless here. */
extern u_char D_800E8194[];
void vs_battle_applyActionTargetEffects(int arg0, func_8006CE70_t* arg1);

void func_8006CD60(u_int* arg0, int arg1, int arg2);

void func_8006CDB8(int arg0);

void vs_field_disableTrapAtPosition(func_8006CE70_t* arg0);

void func_8006CE50(void);

void vs_field_applyUnlockEffectToRoomObject(func_8006CE70_t* arg0);

void func_8006D028(int arg0);

void func_8006D064(void);

void _drawScreenTransition(void* ot);

int _addMinDelta(int base, int a, int b);

int _add_min(int base, int a, int b);

void _clampPositionToZoneBounds(VECTOR* inPos, VECTOR* outPos);

int _computeStepAndDisplacement(SVECTOR* targetPos, VECTOR* outStep, VECTOR* outDisplacement, int clampToZone, int maxSpeed);

int _stepTowardTarget(SVECTOR* targetPos, int transformFlag, int maxSpeed);

void _snapLookAtToPosition(VECTOR* arg0);

#define CAMERA_INPUT_LOCKED()                                                           \
    ((D_800F196C == 3) || ((D_800F196C == 2) && ((_cameraMode == 4) || (_cameraMode == 11))))

void vs_field_updateCameraOrbitInput(void);

void vs_field_trackActionCameraTarget(int arg0);

int vs_actors_updateAshleyMovementInput(void);

int vs_field_updateAshleyFollowCamera(void);

/* First-person up/down, inverted by the PC port's setting qol_fp_invert_y
 * (project_settings.cpp); the original's directions in the PS1 build. */
#ifdef ROOD_PC
extern int pc_qol_fp_invert_y;
#define FP_INVERT_Y pc_qol_fp_invert_y
#else
#define FP_INVERT_Y 0
#endif

void vs_field_updateFirstPersonCamera(void);

void vs_field_storePreferredActionCameraDistance(void);

void _computeCameraTransition(VECTOR* targetPosition);

int _stepCameraTransition(void);

void func_8006F450(func_800CF0E8_t* arg0);

void func_8006F4B0(vs_actors_ActorLocation* arg0);

void _setRoomSeen(void);

void func_8006F5CC(void);

void func_8006F5FC(void);

void vs_battle_populateActionTargetPreview(func_8006F630_t1* arg0, func_8006F630_t2* arg1, func_8006F630_t3* arg2);

int _isArtOrAbilityUnlocked(void);

void func_8006F848(void);

void vs_battle_beginPendingAbilityReward(void);

void vs_battle_grantPendingAbilityUnlock(void);

void vs_items_openPendingEnemyLoot(void);

void vs_battle_beginPostActionRecovery(int arg0);

void vs_battle_beginActionTrailFade(void);

void vs_battle_finishBattleAbilitySequence(void);

void vs_battle_aiNotifyActionRelationshipChanges(vs_battle_ActionRequest*);
void func_80093F24(u_int);

void vs_battle_advanceBattleAbilitySequence(void);

void vs_battle_startActionAnimation(void);

void func_800704B0(void);

void vs_battle_returnToBattleActionMenu(void);

typedef struct {
    char reserved[0x29C0];
    struct {
        SVECTOR position;
        char kind, actor;
        signed char limb;
        char reservedB[13];
    } targets[23];
} actionTargetContextView;
extern unsigned char vs_battle_categoryTargetsBodyParts[];
void func_800C0700(void*);
void vs_battle_setSphereTargets(int, void*, int);
int vs_battle_previewActionAgainstLimb(int, int, int, int, void*);

void vs_battle_filterAreaActionTargets(void);

void vs_battle_aimActionBehindSource(vs_battle_ActionState* arg0);

void vs_battle_setSingleActionTarget(void);

void vs_battle_setAllActionTargets(void);

/* Environment targets overlay the actor-effect records with coordinates. */
typedef struct {
    short x, y, z, radius;
    char reserved8[56];
    int kind;
} environmentTarget;
typedef struct {
    unsigned short action;
    char reserved2[72];
    unsigned short count;
    environmentTarget targets[30];
} environmentTargetList;
_mpdRoomSection9* func_8008E400(int*);
int vs_field_collectMovingPlatformPositions(func_8008D2C0_t*);
void vs_battle_collectEnvironmentActionTargets(void);

void vs_battle_commitSelectedAction(int arg0);

typedef struct {
    u_short type;
    short reserved2;
    union {
        struct {
            u_char actor;
            u_char reserved;
            signed char part;
            char reserved3;
        } actor;
        SVECTOR position;
    } value;
} actionEffectTarget;

typedef struct {
    u_short effect;
    signed char count;
    u_char otherCount;
    actionEffectTarget source;
    actionEffectTarget targets[16];
    char others[16];
    SVECTOR position;
} actionEffectRequest;

void vs_battle_startActionPresentation(void);

void func_800719DC(int arg0 __attribute__((unused)));

extern unsigned char D_800E81B4[], D_800E81D4[];
void vs_actors_anchorPoint(int, int, void*, int);
void vs_battle_collectSourceAreaActionTargets(int arg0 __attribute__((unused)));

/* 573B8.c stores the target list's guest address (arg3) in an int field. */
void func_800C05EC(void*, void*, int, void*);

void vs_battle_collectDirectActionTargets(int arg0);

void vs_battle_collectGroundAreaActionTargets(int arg0);

typedef struct {
    u_char id;
    u_char unk1;
    u_char flags;
} actionHeaderView;

extern unsigned char D_800E8194[];

void vs_battle_initializeActionTargeting(int action);

void vs_battle_beginActorActionSelection(int arg0, u_short arg1);
/* The start of an actor's action (the base attack's, a cast's, the enemies' abilities'), named for the dev RPC action (pc/src/action_driver.c). */
void pc_action_start_actor(int actor, int action)
{
    /* An enemy's ability starts after its AI's targeting commands chose who it is for (unk10: the target actor, the limb in the high half):
     * vs_battle_collectDirectActionTargets then picks the target list entry that choice names, and with none (unk298C stays 0xFF) the AI does not carry the ability
     * out. The driver has no such command: Ashley (actor 0) is the choice, as it is for an enemy with no other to choose. */
    if (actor != 0 && vs_battle_actors[actor])
        vs_battle_actors[actor]->unk10.s32 = 0;
    vs_battle_beginActorActionSelection(actor, (u_short)action);
}
/* The player's action committed (what the menus' target confirmation calls), likewise. */
void pc_action_start_player(int kind) { vs_battle_commitSelectedAction(kind); }

void _initBattleCameraTransition(void);

void _endBattleCameraTransition(void);

void func_80073484(void);

void vs_battle_beginAshleyWeaponToggle(void);

void func_8007350C(void);

void vs_field_beginShortcutMenu(void);

void vs_field_endShortcutMenu(void);

void vs_field_beginBattleActionMenu(int arg0 __attribute__((unused)), int arg1 __attribute__((unused)));

void vs_field_closeBattleActionMenu(D_800F18EC_t* arg0);

void vs_field_dispatchBattleActionMenuResult(D_800F18EC_t* arg0);

void vs_event_prepareCutsceneActors(void);

void vs_event_startPreparedCutscene(void);

void func_80073898(void);

void vs_event_endFieldCutscene(void);

void vs_field_beginObjectInteractionWait(void);

void vs_field_endObjectInteractionWait(void);

int vs_field_interactWithLockedRoomObject(_mpdRoomSectionA* arg0);

void vs_field_triggerActorTrap(_mpdRoomSectionA* arg0, func_8006EBF8_t* arg1, int targetActor);

void vs_field_tryOpenChest(_mpdRoomSectionA* arg0, int arg1);

void vs_field_beginTrapInteraction(_mpdRoomSectionA* arg0, vs_actors_ActorLocation* arg1, int arg2);

void vs_field_beginChestInteraction(_mpdRoomSectionA* arg0, int arg1);

int vs_field_waitForChestCameraFocus(void);

int vs_field_stepTrapCameraFocus(void);

void vs_field_dispatchTrapOrChestInteraction(void);

int func_8007424C(void);

int func_80074294(void);

void func_800742A4(void);

void vs_field_finishTrapInteraction(void);

int vs_field_trackInteractionCameraToAshley(void);

void vs_field_openChestContents();

int vs_field_updateChestContentsSelection(void);

void vs_field_updateFirstPersonViewEntry(void);

void _checkFirstPersonViewExit(void);

void vs_field_updateFirstPersonViewExit(void);

int vs_field_tileInFrontOfActor(func_8006EBF8_t* arg0, char* arg1);

int vs_field_tryTriggerFacingTileEvent(int arg0);

_mpdRoomSectionA* vs_field_findFacingRoomObject(int arg0);

_mpdRoomSection13* vs_field_findFacingUnlootedChest(int arg0);

int vs_battle_beginActorDefeatAnimation(int arg0);

void vs_field_updateCameraTileConstraint(int arg0, func_8006EBF8_t_fields* arg1);

int vs_field_checkAshleyTileTriggers(int, u_char*);
/* PC only: the decomp's closest non-matching C for vs_field_checkAshleyTileTriggers
 * (rood-reverse .work/ultracode/r7-near-b/74bac/p40.full.c, score 40: a
 * register-allocation swap, same behaviour). */
vs_field_DoorMatchList* vs_field_transitionDoorsAtTile(int, int, int);
int vs_field_checkAshleyTileTriggers(int actorId, u_char* cell);

int vs_field_checkActorTrapAndFallTriggers(int arg0, vs_actors_ActorLocation* arg1);

void vs_actors_consumeActorMovementOffset(vs_battle_actor* arg0, SVECTOR* arg1);

int vs_actors_executeQueuedActorCommands(void);
#include "pc/src/actor_commands.inc"

void vs_render_configureFieldDisplay(int arg0, int arg1, int arg2, int arg3, int arg4, int arg5);

int vs_render_presentFieldFrame(u_long* otag);

void func_8007647C(int arg0, int arg1);

vs_battle_actor* vs_actors_createAuxiliaryActor( int index, int actorId, vs_battle_objectData_flags* arg2, int material, int arg4);

void vs_actors_applyActorInitialStats( int arg0, vs_battle_actor2* actor, vs_battle_charInitData* init, int arg3);

void vs_actors_initializeActorDefaults(u_int id, int arg1, int arg2, int arg3, int arg4);

void vs_actors_initializeActorFromTemplate( int id, vs_battle_charInitData* arg1, int arg2, int arg3, int arg4, int arg5);

int vs_actors_queueActorModelLoad(vs_battle_actor* arg0, int index, int wepId, vs_battle_objectData_flags* arg3, int arg4);

void vs_actors_queueActorBaseSequenceLoad(vs_battle_actor* arg0 __attribute__((unused)), int arg1);

void vs_actors_queueEquipmentModelLoad( vs_battle_actor* actor, int actorId, int wepId, int isShield, int material);

void vs_actors_queueActorWeaponSequenceLoad(char* arg0, int arg1, int arg2, int arg3);

vs_battle_actor_dat* vs_actors_allocateActorWithEquipment(int arg0, int arg1, int bladeWepId, int bladeMaterial, int shieldWepId, int shieldMaterial, vs_battle_objectData_flags* arg6, int arg7);

void vs_actors_registerLoadedActor( vs_battle_actor* arg0, int arg1, int arg2, int arg3, int arg4, int arg5);

vs_battle_actor* vs_actors_createActorWithEquipment(int arg0, int arg1, int bladeWepId, int bladeMaterial, int shieldWepId, int shieldMaterial, vs_battle_objectData_flags* arg6, int arg7);

extern u_char vs_actors_usesSecondaryBodyModel[];
extern vs_battle_charInitData* vs_actors_zoneCharacterInitData;

vs_battle_actor* vs_actors_spawnRoomEnemy( int id, mpdEnemySpawn* spawn, vs_battle_charInitData* init, int flags);

int _isLookAtAtDestination(void);

void vs_battle_actionTargetLimbPosition(int arg0, int arg1, SVECTOR* arg2);

int vs_battle_actionTargetDistance(void);

void vs_battle_actionCameraMidpoint(SVECTOR* arg0);

void vs_battle_stepCameraTowardActionMidpoint(int arg0);

void func_80078248(void);

void func_800782E4(void);

void func_80078364(void);

void _handleCombatCameraZoom(void);

void func_80078748(void);

void vs_battle_startReadyActionAnimation(void);

int vs_battle_canTriggerBattleAbility(int arg0);

void vs_battle_updateBattleAbilityTiming(void);

void func_80079030(void);

void func_80079050(void);

void func_800790BC(void);

int vs_battle_resolveActorDefeat(int arg0, int arg1, int arg2);

void vs_battle_finishActionSequence(void);
extern int D_800F19D8;
extern int D_800F19EC;

void vs_battle_finishActionSequence(void);

void vs_battle_updateLootResults(void);

void vs_battle_updateAbilityUnlockDelay(void);

void vs_battle_updateAbilityUnlockResults(void);

void _finishLoadZnd(int id);
void vs_field_finishRoomLoad(int arg0);
int vs_field_updateMovingPlatforms(void);
int vs_battle_updateRoomActorVitals(void);
void func_80088B8C(void);
void vs_field_drawRoomBackground(VECTOR* arg0);
void vs_field_drawRoomScene(void);
void vs_field_advanceRoomAnimationClock(void);
void vs_render_drawFramebufferEffect(void*);
void vs_render_advanceTexturePageRetirements(void);
void vs_field_unloadCurrentRoom(void);
void vs_field_releaseZoneData(void);

void vs_battle_exec(void);
void vs_boot_initializeGameplayOverlay(int);

void vs_battle_exec(void);

void vs_render_appendOverlayPrimitive(int* arg0);

int vs_battle_syncCameraAnglesFromPosition(_sphericalCamera* arg0);

void _computeSphericalOffset( VECTOR* outPosition, VECTOR* basePosition, _sphericalValues* angles);

void _setCameraPositionFromAngles(_sphericalValues* spherical);

void _setCameraLookAtFromAngles(_sphericalValues* spherical);

void vs_battle_getCameraPosition(VECTOR* outPosition);

void vs_battle_setCameraPosition(VECTOR* inPosition);

void vs_battle_getCameraLookAt(VECTOR* outLookAt);

void vs_battle_setCameraLookAt(VECTOR* inLookAt);

void vs_battle_setCameraRoll(int arg0);

int vs_battle_getCameraRoll(void);

extern MATRIX vs_render_cameraBasisMatrix;

void vs_render_buildCameraViewMatrix(void);

void vs_battle_lookAt(VECTOR* from, VECTOR* to, MATRIX* mat);

void func_8007B0FC(void);

void func_8007B104(void);

void vs_render_transformRoomPalettes(int arg0, int arg1, int arg2, short arg3, short arg4);

void vs_render_transformSceneColors(int arg0, int arg1, short arg2, short arg3, short arg4);

void vs_render_transformActorColors(int arg0, int arg1, int arg2, short arg3, short arg4, short arg5);

void vs_render_transformBackgroundColors(int arg0, int arg1, short arg2, short arg3, short arg4);

void vs_render_startRoomPaletteTransition(int arg0, u_short* arg1, int arg2, int arg3) __attribute__((unused));
void vs_render_startRoomPaletteTransition(int arg0, u_short* arg1, int arg2, int arg3);

void func_8007B3D0(int arg0, int arg1, u_short* arg2, int arg3) __attribute__((unused));
void func_8007B3D0(int arg0, int arg1, u_short* arg2, int arg3);

void vs_battle_beginRewardPause(void);

void vs_battle_endRewardPause(void);

void vs_battle_resumeAfterRewards(void);

void func_8007B508(void);

void vs_battle_showPendingRewardUnlock(void);

void vs_battle_openPendingLootRewards(void);

void func_8007B7DC();

void vs_battle_updateRewardResultsDialog(void);

void vs_battle_updatePendingLootDialog(void);

void vs_battle_waitForRewardUnlockEffects(void);

void vs_battle_updateRewardUnlockDialog(void);

void vs_battle_beginEventRewardResults(void);

void vs_battle_beginPostActionRewards(void);

int vs_battle_updateRewardSequence(void);

void vs_items_presentScriptedLoot(int id0, int count0, int id1, int count1);

void func_8007BBB8(int arg0, int arg1);

void vs_state_startSceneTimer(int arg0, int arg1, int arg2);

void vs_state_resetSceneTimer(void);

void vs_state_setSceneTimerPaused(int arg0);

int func_8007BD8C(int arg0);

int vs_field_setFirstPersonViewDisabled(int arg0);

int vs_audio_setZoneMusicSwitchEnabled(int arg0);

void func_8007BE20(int arg0, int arg1);

void func_8007BEC4(int arg0);

int func_8007BEE4(void);

void func_8007BF04(void);

int func_8007BF0C(void);

void func_8007BF2C(void);

void func_8007BF4C(void);

void vs_actors_enableActorModel(int arg0);

void vs_actors_enableAllActorModels(void);

void vs_actors_disableActorModel(int arg0);

void vs_actors_disableAllActorModels(void);

void vs_battle_setActorBattleMode(int arg0, int arg1);

int vs_actors_actorHpPercentage(int arg0);

void vs_actors_restoreActorVitals(int arg0);

void vs_battle_preserveAshleyStatuses(void);

void func_8007C348(int arg0, int arg1);

int vs_state_selectTickSpeed(int arg0);

int vs_field_screenTransitionPhase(void);

void func_8007C404(void);

void func_8007C424(void);

void vs_field_configureScreenTransition(int arg0, int arg1, int arg2);

void vs_field_setTransitionWipeAngle(int arg0);

void vs_field_beginScreenFadeOut(int effect, int speed, int angle);

void vs_field_beginScreenFadeIn(int effect, int arg1, int arg2);

void vs_field_cancelPendingRoomVisit(void);

int vs_field_beginRoomTransition(_mpdRoomDoorSection_t* arg0, int effect, int speed);

void vs_field_beginScriptedRoomTransition(_mpdRoomDoorSection_t* arg0, int arg1, int arg2);

int vs_field_beginZoneTransition(_mpdRoomDoorSection_t* arg0, int effect, int speed);

void vs_field_beginScriptedZoneTransition(_mpdRoomDoorSection_t* arg0, int arg1, int arg2);

int vs_field_teleportToLocation(int arg0, int arg1, int arg2, int effect, int speed);

int _getLocationId(int zoneId, int mapId);

int vs_battle_getCurrentRoomId(void);

void vs_audio_playActorScriptSound(int arg0, int arg1);

int func_8007C8F8(int arg0);

int vs_actors_queueScriptedActorSpawn(u_int arg0, int arg1, vs_battle_objectData_flags* arg2);

void vs_actors_loadActorEquipmentModels(int actorId, int bladeWepId, int shieldWepId);

void vs_actors_refreshActorEquipmentModels(int arg0);

void vs_actors_queueSharedSequenceLoad(int index, int wepId);

void vs_actors_unloadSharedSequence(int arg0);

void vs_actors_queueSharedSequenceWithTextures(int index, int wepId, int arg2);

void vs_battle_loadEtm(int index, int wepId, int arg2);

void vs_actors_releaseSequenceEffectTextures(int arg0);

void vs_actors_setSharedSequenceBank(int arg0);

int func_8007CCAC(void);

void vs_battle_setNearClip(int arg0);

void vs_battle_setProjectionDistance(int projectionDistance);

void vs_battle_setFarClip(int farClip, int arg1);

void vs_battle_initialiseCameraFromSpherical( VECTOR* outputPos, VECTOR* outputLookAt, int facing, int mode);

void vs_field_setCameraPitchPreset(int arg0);

void func_8007CEA0(int arg0);

void vs_actors_returnAllActorsToIdle(void);

int vs_actors_findActorByModelId(int arg0);

vs_battle_actor* vs_actors_isBattleActorSlot(int actorId);

int vs_actors_countActorsByDisposition(int arg0);

char vs_actors_isActorRetainedAfterDefeat(int arg0);

void func_8007CFF8(int arg0, SVECTOR* arg1);

int vs_actors_isScriptedActorPathFinished(int arg0);

int vs_actors_setActorLogicEnabled(int arg0, int arg1);

int vs_actors_setOtherActorLogicEnabled(int arg0);

void vs_actors_setActorDetached(int arg0, int arg1);

void func_8007D260(int arg0);

int vs_battle_isActionUnlocked(u_int arg0);

void vs_battle_setActionUnlocked(u_int arg0);

void vs_actors_removeActorAtSlot(int arg0);

void func_8007D360(void);

void vs_render_drawFramebufferEffect(void* arg0);

void vs_render_setFramebufferEffectEnabled(int arg0);

void vs_render_setFramebufferEffectRoll(int arg0);

void vs_render_setFramebufferEffectScale(vs_render_FramebufferEffectTranslation* arg0);

void vs_render_setFramebufferEffectTint(P_CODE* arg0);

void vs_render_setFramebufferEffectOffset(vs_render_FramebufferEffectTranslation* arg0);

void vs_render_setFramebufferEffectBlendMode(int arg0);

void vs_render_setFramebufferEffectDepth(u_int arg0);

void vs_render_setFramebufferEffectPreviousFrame(int arg0);

void vs_render_readFramebufferEffectScale(int* arg0);

void vs_render_readFramebufferEffectTint(P_CODE* arg0);

void vs_render_readFramebufferEffectOffset(vs_render_FramebufferEffectTranslation* arg0);

void func_8007DF40(void);

void vs_render_advanceTexturePageRetirements(void);

int vs_render_reserveTexturePages(u_int arg0, u_int arg1, int arg2);

int vs_render_retireTexturePages(u_int arg0, u_int arg1, int arg2);

int func_8007E180(int arg0);

int func_8007E1C0(int arg0);

void _calculateArmorClassAffinity(vs_battle_actor2* arg0, int arg1);

void _calculateShieldClassAffinity(vs_battle_actor2* arg0);

void _calculateWeaponClassAffinity(vs_battle_actor2* arg0);

extern u_int _itemCategorySizes[];

int vs_battle_itemIdIsInInventory(int id);

short _getAttackGemBuff(vs_action_t* action, vs_battle_actor2* actor);

short _getDefenseGemBuff(vs_action_t* action, vs_battle_actor2* actor, int hitNumber);

int _doesAttackHit(vs_action_t* action, _hitEntity_t* source, _hitEntity_t* target, int hitNumber, int addVariance);

int vs_field_rollRiskLockBreak(void);

int _hitPrerequisiteCanApplyEffect(int debuff, u_char* arg1);

int _getEnemyClass(u_char* arg0);

int _canPerformAttack(vs_action_t* action, char* arg1, int hitNumber);

short vs_battle_applyDamageGemMultiplier( vs_action_t* action, short arg1, vs_battle_actor2* arg2, vs_battle_actor2* arg3);

void vs_battle_distributeHitLimbDamage(vs_action_t* action, _hitEntity_t* entity, short baseValue);

void vs_battle_distributeLimbRecovery(vs_action_t* action, _hitEntity_t* entity, short amount);

void _adjustDpPp( vs_action_t* action, vs_battle_actor2* source, vs_battle_actor2* target, int amount);

void vs_items_accumulateHitEquipmentPp( vs_action_t* action, vs_battle_actor2* arg1, vs_battle_actor2* arg2, int arg3);

void _triggerArmorAffinityChange(vs_action_t* action, vs_battle_uiEquipment* equipment, int affinity, int arg3, int actorId);

void _triggerWeaponAffinityChange(vs_action_t* action, vs_battle_uiEquipment* equipment, int affinity, int arg3, int suppressNotification);

void _triggerClassChange(vs_action_t* action __attribute__((unused)), vs_battle_uiEquipment* equipment, int class, int arg3, int actorId);

void vs_items_growDefenderEquipmentRatings( vs_action_t* arg0, vs_battle_actor2* arg1, int arg2, int arg3, int arg4);

void vs_items_growAttackerWeaponRatings(vs_action_t* action, vs_battle_actor2* arg1, int arg2, int arg3);

int _setApplicableStatuses(int arg0, _hitEntity_t* entity);

int _setInflictedStatuses(int arg0, _hitEntity_t* entity);

typedef struct {
    /* 0x00 */ u_char type;
    /* 0x01 */ u_char sourceAffinity;
    /* 0x02 */ u_char sourceClass;
    /* 0x03 */ u_char unk3;
    /* 0x04 */ short typeValue;
    /* 0x06 */ short weaponAffinity;
    /* 0x08 */ short weaponClass;
    /* 0x0A */ short value;
    /* 0x0C */ short weaponDamage;
    /* 0x0E */ short unkE;
    /* 0x10 */ u_char unk10;
    /* 0x11 */ u_char targetAffinity;
    /* 0x12 */ u_char targetClass;
    /* 0x13 */ u_char unk13;
    /* 0x14 */ short unk14;
    /* 0x16 */ short tempTargetAffinity;
    /* 0x18 */ short unk18;
    /* 0x1A */ short mediatingStat;
    /* 0x1C */ short shieldStat;
    /* 0x1E */ short armorStat;
    /* 0x20 */ short statFactor;
    /* 0x22 */ short shieldStatFactor;
    /* 0x24 */ short armorStatFactor;
} _hitIntermediate;

static short _getPhysicalAttackDamage(vs_action_t* action, _hitEntity_t* source,
    _hitEntity_t* target, int nHit, int withRandMod, int applyStatuses)
{
    _hitIntermediate sp18;
    int attack;
    short maxAffinity;
    short value;
    int i;
    int affinity;
    int defense;

    vs_battle_actor2* sourceActor =
        vs_battle_actors[source->target.targetActor]->unk3C;
    vs_battle_actor2* targetActor =
        vs_battle_actors[target->target.targetActor]->unk3C;
    int limb = target->target.targetLimb;

    sp18.sourceClass = sourceActor->enemyClass;
    sp18.targetClass = targetActor->enemyClass;

    if (action->hitParams[nHit].type == 0) {
        sp18.type = sourceActor->weapon.damageType;
        if (!sp18.type) {
            sp18.type = 1;
        }
    } else {
        sp18.type = action->hitParams[nHit].type;
    }

    if (action->hitParams[nHit].affinity == 0) {
        if (!(vs_battle_actors[sourceActor->actorId]->weaponDrawn & 1)
            || sourceActor->weapon.blade.id == 0) {
            affinity = 0;
        } else {
            affinity = 0;
            maxAffinity = 0;
            for (i = 0; i < 7; ++i) {
                if (maxAffinity
                    < sourceActor->weapon.classAffinityCurrent.affinity[0][i]) {
                    maxAffinity = sourceActor->weapon.classAffinityCurrent.affinity[0][i];
                    affinity = i;
                }
            }
        }
        sp18.sourceAffinity = affinity;
    } else {
        sp18.sourceAffinity = action->hitParams[nHit].affinity - 1;
    }

    affinity = 0;
    maxAffinity = 0;

    for (i = 0; i < 7; ++i) {

        sp18.tempTargetAffinity =
            targetActor->limbs[limb].affinities[i]
            + targetActor->limbs[limb].armor.classAffinityCurrent.affinity[0][i];

        if (vs_battle_actors[targetActor->actorId]->weaponDrawn & 1) {
            sp18.tempTargetAffinity +=
                targetActor->shield.classAffinityCurrent.affinity[0][i];
        }

        if (maxAffinity < sp18.tempTargetAffinity) {
            maxAffinity = sp18.tempTargetAffinity;
            affinity = i;
        }
    }

    sp18.targetAffinity = affinity;
    sp18.value = sourceActor->strength;

    if (vs_battle_actors[sourceActor->actorId]->weaponDrawn & 1) {

        sp18.value = sp18.value + sourceActor->weapon.currentStr;
        sp18.typeValue = sourceActor->weapon.damageTypeValue;
        sp18.weaponAffinity =
            sourceActor->weapon.classAffinityCurrent.affinity[0][sp18.sourceAffinity];
        sp18.weaponClass =
            sourceActor->weapon.classAffinityCurrent.class[0][targetActor->enemyClass];
        sp18.weaponDamage = sp18.weaponClass + (sp18.typeValue + sp18.weaponAffinity);

        if (sourceActor->weapon.maxDp != 0) {
            sp18.weaponDamage =
                sp18.weaponDamage
                - (((sourceActor->weapon.maxDp - sourceActor->weapon.currentDp) * 100)
                    / sourceActor->weapon.maxDp);
        }

        if (sourceActor->weapon.maxPp != 0) {
            sp18.weaponDamage +=
                (sourceActor->weapon.currentPp
                    * *((sourceActor->weapon.blade.category) + vs_battle_bladePpDamageBonuses))
                / sourceActor->weapon.maxPp;
        }

    } else {
        sp18.weaponClass = 0;
        sp18.weaponAffinity = 0;
        sp18.typeValue = 0;
        sp18.weaponDamage = 0;
    }

    sp18.value += sourceActor->accessory.currentStr;
    sp18.weaponDamage += sourceActor->accessory.types[sp18.type]
                       + sourceActor->accessory.classes[sp18.targetClass]
                       + sourceActor->accessory.affinities[sp18.sourceAffinity];
    sp18.weaponDamage = sp18.weaponDamage / 4 + 100;
    sp18.mediatingStat = targetActor->strength;
    sp18.armorStat = targetActor->limbs[limb].armor.currentStr;
    sp18.shieldStat = 0;
    sp18.armorStatFactor = 100;
    sp18.shieldStatFactor = 100;
    sp18.statFactor = 100;
    sp18.statFactor = targetActor->limbs[limb].types[sp18.type]
                    + targetActor->limbs[limb].affinities[sp18.sourceAffinity] + 100;
    sp18.armorStatFactor =
        targetActor->limbs[limb].armor.classAffinityCurrent.class[0][sp18.sourceClass]
        + (targetActor->limbs[limb].armor.types[sp18.type]
            + targetActor->limbs[limb]
                .armor.classAffinityCurrent.affinity[0][sp18.sourceAffinity])
        + 100;

    if (targetActor->limbs[limb].armor.maxDp != 0) {
        sp18.armorStatFactor -= (((targetActor->limbs[limb].armor.maxDp
                                      - targetActor->limbs[limb].armor.currentDp)
                                     * 100)
                                    / targetActor->limbs[limb].armor.maxDp)
                              / 4;
    }

    if (vs_battle_actors[targetActor->actorId]->weaponDrawn & 1) {

        sp18.shieldStat += targetActor->shield.currentStr;
        sp18.shieldStatFactor =
            sp18.shieldStatFactor
            + ((targetActor->shield.types[sp18.type]
                   + targetActor->shield.classAffinityCurrent
                       .affinity[0][sp18.sourceAffinity])
                + targetActor->shield.classAffinityCurrent.class[0][sp18.sourceClass]);

        if (targetActor->shield.maxDp != 0) {
            sp18.shieldStatFactor -=
                (((targetActor->shield.maxDp - targetActor->shield.currentDp) * 100)
                    / targetActor->shield.maxDp)
                / 4;
        }
        if (targetActor->shield.maxPp != 0) {
            sp18.shieldStatFactor +=
                (targetActor->shield.currentPp * 100) / targetActor->shield.maxPp / 4;
        }
    }

    sp18.mediatingStat = sp18.mediatingStat + targetActor->accessory.currentStr;
    sp18.statFactor = sp18.statFactor
                    + ((targetActor->accessory.types[sp18.type]
                           + targetActor->accessory.classes[sp18.sourceClass])
                        + targetActor->accessory.affinities[sp18.sourceAffinity]);
    attack = (sp18.value * sp18.weaponDamage) / 100;
    attack = (attack * ((action->hitParams[nHit].statFactor * 10) + 100)) / 100;
    defense = (sp18.mediatingStat - 20) * sp18.statFactor;
    defense += sp18.shieldStat * sp18.shieldStatFactor;
    defense += sp18.armorStat * sp18.armorStatFactor;
    defense /= (targetActor->risk / 2) + 100;
    value = vs_battle_applyDamageGemMultiplier(action, attack - defense, sourceActor, targetActor);

    if (value < 0) {
        value = 0;
    }

    if (withRandMod != 0) {
        value += 1 + vs_main_getRand(5);
    }

    if ((applyStatuses != 0) && (sourceActor->statuses & 2)) {
        value /= 2;
    }

    if (withRandMod != 0) {

        if ((action->id < 6) && (vs_main_getRand(100) < (sourceActor->risk / 10) + 10)) {
            value = (short)((u_int)(value << 0x10) >> 0xF);
            vs_battle_criticalHitOccurred = 1;
        }

        if (nHit == 0) {
            if (target->unk40 == 0) {
                vs_items_growAttackerWeaponRatings(action, sourceActor, (targetActor->enemyClass) + 1,
                    sp18.targetAffinity + 1);
                vs_items_growDefenderEquipmentRatings(action, targetActor, (sourceActor->enemyClass) + 1,
                    sp18.sourceAffinity + 1, limb);
            }
        }
    }

    return value;
}

static int _getRoomTrapLevel(void);

short _calculateStatChange( vs_action_t* action, _hitEntity_t* arg1, _hitEntity_t* arg2, int hit, int arg4);

void vs_battle_deriveLimbInjuryStatuses(_hitEntity_t* arg0);

void vs_battle_applyActionHitEffect( vs_action_t* action, _hitEntity_t* arg1, _hitEntity_t* arg2, int arg3, int hit);

void vs_battle_clearActionRequest(void* arg0);

void vs_battle_initializeActionHit(_hitEntity_t* arg0);

/**
 * Retrieves a compound value of the action type, cost, and whether
 * the actor can pay the cost.
 * @return Packed integer:
 *         - bits 31-24 = bool requirements met
 *         - bits 23-16 = enum action type
 *         - bits 15-0  = int cost
 */
int _getActionCost(u_int actionId, vs_battle_actor2* actor, int updateStat);

void vs_battle_addActionRecoveryDelay(u_int arg0, vs_battle_actor2* arg1, int arg2);

void vs_battle_finalizeActionHit(func_80085A34_t* arg0);

int vs_field_traceActorAndTerrainCollision(SVECTOR*, SVECTOR*, SVECTOR*, int);

void vs_battle_resolveActionTargets(int action, vs_battle_ActionRequest* result, vs_battle_ActionRequest* source, int arg3);

int vs_battle_previewActionAgainstLimb(int arg0, int arg1, int targetActor, int targetLimb, void* arg4);

int vs_battle_previewActionAgainstActor(int arg0, int arg1, int targetActor, int arg3, SVECTOR* arg4, SVECTOR* arg5, vs_battle_ActionRequest* arg6);

#ifdef ROOD_PC
#undef sp860
#undef sp870
#endif

void vs_battle_removeActorStatuses(int arg0, vs_battle_actor2* arg1);

void vs_battle_applyWeaponElementEnchantment(vs_battle_actor2* arg0, int arg1, int arg2, int arg3);

void vs_battle_applyArmorElementEnchantment(vs_battle_actor2* arg0, int arg1, int arg2, int arg3);

void vs_battle_inflictActorStatuses(int arg0, vs_battle_actor2* arg1);

void vs_battle_queueActorStatChangeNumber(int arg0, int arg1, int arg2, int arg3);

void _applyBattleAbilityEffect(_hitEntity_t* arg0);

void vs_battle_updateActorStatusTimers(vs_battle_actor2* actor);

int vs_state_advanceSceneTimer(void);

int vs_battle_updateActorVitals(void);

int vs_battle_updateRoomActorVitals(void);

void func_80088B8C(void);

void func_80088CA0(void);

void func_80088CAC(void);

void _loadMpdLbas(void* arg0, int size);

void _loadZndEnemies(psx_long* data, int arg1 __attribute__((unused)));

static void _loadZnd(int id)
{
    vs_main_CdFile cdFile;

    cdFile.lba = vs_main_zndFiles[id].lba;
    cdFile.size = vs_main_zndFiles[id].size;
    if (_zoneContext.zndCdFile != 0) {
        vs_main_nop9(0x87, 0);
    }
    _zoneContext.zndCdFile = vs_main_allocateCdQueueSlot(&cdFile);
    if (_zoneContext.zndData != NULL) {
        vs_main_nop9(0x88, 0);
    }
    _zoneContext.zndData = vs_main_allocHeapR(cdFile.size);
    vs_main_cdEnqueuePriority(
        _zoneContext.zndCdFile, _zoneContext.zndData);
    if (_zoneContext.unk30 != 0) {
        int temp_s0 = vs_field_zoneMusicIds[id];
        if (vs_main_getCurrentMusicId() != temp_s0) {
            vs_audio_fadeMusicSlotVolume(vs_main_soundData.currentMusicSlot, 0, 0x78);
        }
    }
}

void _finishLoadZnd(int id);

void vs_field_releaseZoneData(void);

int func_80089104(void);


void vs_field_spawnRoomActors(void);

void vs_field_placeAshleyAtRoomEntrance(void);

void vs_field_restoreRoomEnemyCache(void);

void _loadMpdEnemySection(void* data, u_int dataLen);

void func_80089CE4(void);

void func_80089D04(void);

void _loadMpd(int id);

void vs_field_finishRoomLoad(int arg0);

void vs_field_removeRoomActors(void);

void vs_field_unloadCurrentRoom(void);

void vs_items_clearRoomLootList(void);

void _loadZndTims(void* data);

int _loadMpdRoomSection(int arg0, void* data);

void vs_battle_setDoorEntered(void);

int vs_field_enableOwnedRoomRendering(int arg0);

int vs_field_disableOwnedRoomRendering(int arg0);

int vs_field_releaseOwnedRoomSections(int arg0);

#include "pc/src/scene_submit.inc"
#include "pc/src/charge_skip.inc"

void vs_field_drawRoomGeometry(void* arg0);

void vs_field_advanceRoomAnimationClock(void);

void vs_field_copyZonePaletteRow(void* arg0, int arg1, int arg2, int arg3);

void vs_field_uploadRoomPalettes(void);

void vs_field_offsetRoomTextureIds(int arg0, int* arg1);

void vs_field_setRoomAnimationsPaused(int arg0);

int vs_field_setMinimapVisible(char arg0);

void vs_battle_setRoomsUnk0(vs_battle_scene* scene);

void _vsBattleNop2(int arg0 __attribute__((unused)));

void vs_field_buildMinimapRotation(MATRIX* arg0, int arg1);

void vs_field_resetGimmickAnimationQueue(void);

void* func_8008B6FC(void);

int _getCollisionMapDimensions(int arg0 __attribute__((unused)));

_zoneContextBounds* _getMapBounds(int arg0 __attribute__((unused)));

_mpdRoomSection3* vs_field_collisionCellAt(u_int width, u_int height, int arg2);

int func_8008B808(int arg0, int arg1, int arg2, int arg3);

void vs_field_setEntranceDoorRecord(char (*arg0)[12]);

void vs_field_findEntranceDoorNearTile(int arg0, int arg1, int arg2);

vs_field_DoorMatchList* vs_field_transitionDoorsAtTile(int arg0, int arg1, int arg2);

_mpdRoomSectionA* vs_field_interactiveDoorAtTile(int arg0, int arg1, int arg2);

void vs_field_toggleDoorCollision(_mpdRoomSectionA* arg0);

int vs_field_beginDoorToggle(_mpdRoomSectionA* arg0);

int vs_field_startDoorAnimation(_mpdRoomSectionA* arg0);

void vs_field_queueGimmickAnimation(int arg0, _mpdRoomSectionA* arg1);

void vs_field_updateGimmickAnimationQueue(void);

int vs_field_finishDoorAnimation(_mpdRoomSectionA* arg0);

int func_8008C2C0(int arg0, int arg1, int arg2, int arg3);

void vs_field_clearGeometryOcclusion(void);

#ifndef ROOD_PC
void func_8008C458(int arg0) { vs_render_sampleRoomLighting(arg0, 0); }
#endif

int vs_field_setMovingPlatformActor(int arg0, int arg1);

void vs_field_initializeMovingPlatforms(void);

void vs_field_resetMovingPlatformWaypoint(int arg0, int arg1);

static short const vs_field_platformBounceOffsets[] __attribute__((used)) = { 8, 14, 16, 11, 7, 3, 1 };

typedef struct {
    unsigned int flags[2];
    VECTOR positions[4];
    VECTOR velocities[6];
} RoomMotionVectors;
int vs_field_checkPlatformActorObstruction(int, SVECTOR*);
int vs_field_updateMovingPlatforms(void);

#include "pc/src/room_shadows.inc"

int vs_field_collectMovingPlatformPositions(func_8008D2C0_t arg0[]);

int vs_battle_getCurrentSceneId(void);

int vs_field_roomFallDestination(void);

_mpdRoomSection9* vs_field_findRoomTriggerAtTile(int arg0, int arg1, int arg2);

_mpdRoomSection13* vs_items_treasureChestAtTile(int arg0, int arg1, int arg2);

void vs_field_setMovingPlatformsPaused(int arg0);

void vs_field_freezeMovingPlatforms(int arg0);

void func_8008D5FC(_mpdRoomSectionA* arg0);

void vs_field_toggleRoomSwitch(_mpdRoomSectionA* arg0);

void vs_items_restoreRoomChestStates(void);

void vs_battle_parseTim(u_long* arg0, TIM_IMAGE* tim);

int _getDoorId(int door);

void vs_battle_setDoorEntered2(int arg0);

short vs_field_interpolatedFloorHeight(int arg0, int arg1);

short vs_field_floorHeightAt(int arg0, int arg1);

short vs_field_ceilingHeightAt(int arg0, int arg1);

int vs_field_floorAndCeilingHeightsAt(int arg0, int arg1);

void vs_field_setRoomTriggerEnabled(_mpdRoomSection9* arg0, int arg1);

void vs_field_drawRoomTriggerMarkers(void);

void vs_field_drawRoomTriggerMarker(int arg0, int arg1, short arg2, u_int arg3);

void vs_field_setMovingPlatformVisibility(int arg0, int arg1);

void* vs_field_movingPlatformTargetWaypoint(int arg0);

u_int vs_field_movingPlatformDwellTicks(int arg0);

_mpdRoomSection13* vs_items_roomTreasureChests(int* arg0);

_mpdRoomSectionA* func_8008E3B8(int* arg0);

_mpdRoomSection9* func_8008E400(int* arg0);

int vs_field_isRoomAudioSequenceAbsent(void);

void vs_audio_setRoomAmbienceDucked(int arg0);

short func_8008E4AC(void);

static int _getRoomTrapLevel(void)
{
    return vs_battle_roomData.sectionB->trapLevel;
}

void vs_field_setPlatformFreezeAppearance(int arg0);

void _loadMpdLootSection(void* data);

void vs_field_markSigilDoorUnlocked(int arg0);

void func_8008E8F8(void);

void vs_field_initializeRoomBackgroundColors(void);

void vs_field_setRoomWeather(int);

int func_8008EA90(int arg0);

void vs_field_setBackgroundParallaxScale(int arg0, int arg1);

void vs_field_setTiledBackgroundEnabled(int arg0);

void func_8008EB04(int* arg0, int* arg1);

void vs_field_setRoomBackgroundColors(int* arg0);

void vs_field_drawRoomBackground(VECTOR* arg0);

int vs_field_queueTiledBackgroundLoad(int arg0);

int vs_field_finishTiledBackgroundLoad(void);

int vs_field_releaseTiledBackground(void);

void func_8008F29C(int arg0, int arg1);

void func_8008F30C(int arg0, int arg1);

void func_8008F440(void);

void func_8008F9A4(int arg0, int arg1);

/* The screen's X range for the weather particles drawn as 2D at projected
 * points: in widescreen (port) the 16:9 view's (pc/src/widescreen.h). */
#ifdef ROOD_PC
#define WEATHER_LEFT (-PC_WIDESCREEN_MARGIN)
#define WEATHER_RIGHT (320 + PC_WIDESCREEN_MARGIN)
#else
#define WEATHER_LEFT 0
#define WEATHER_RIGHT 320
#endif

void func_8008FA14(D_800F1DD4_t* arg0);

void func_8008FAC8(void);

void func_8008FDC4(void);

void func_80090434(void);

void func_80090A1C(D_800F1DD4_t* arg0);

void func_80090B28(void);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/146C", vs_field_legacyBackgroundGradientPackets);

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
extern void pc_story_frame(void);
extern void pc_frame_begin(void);
extern void pc_scene_room(const void*);
extern void pc_room_warp_room_loaded(void);
extern int pc_fast_travel_begin(int packed);
extern void pc_battle_frame_complete(void);

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
extern int pc_weapon_slots_process_pad(void);
extern void pc_scene_camera(const void*);
extern void pc_room_warp_room_spawned(void);

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
extern int pc_room_warp_poll(void);
extern int pc_action_poll(void); /* pc/src/action_driver.c: the dev RPC action */
extern void pc_action_capture(const void* state, int kind, int mode, int menu);
