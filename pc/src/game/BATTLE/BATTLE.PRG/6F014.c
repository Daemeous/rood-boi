#include "common.h"
#include "3A1A0.h"
#include "4A0A8.h"
#include "5BF94.h"
#include "6E644.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "pc/src/game/SLUS_010.40/overlay.h"
#include "build/src/include/lbas.h"
#include <abs.h>
#include <stdlib.h>
#include "pc/src/psx_types.h"
#include "ai_state.h"

struct func_800D4910_t;

typedef struct {
    int assetCount;
    _loadFileContext loadContexts[8];
    u_short slots[8];
    u_short isClut[8];
} vs_effects_TextureLoadList;

typedef struct {
    short tableCount;
    u_short dataOffset;
    int tableOffsets[0];
} pFileBlock1;

typedef struct {
    int animationCount;
    u_char data[0];
} pFileBlock2;

typedef struct {
    int meta;
    u_char data[0];
} p_file_t;

void vs_effects_setParticleGravity(VECTOR*);
void vs_effects_setParticleDrag(int*);
void vs_effects_selectEffectResourceContext(int arg0);
void _parsePfileBlock1(pFileBlock1* arg0);
void _parsePfileBlock12(void*);
void _parsePfileBlock13(void*);
void vs_effects_setEffectTextureLoadState(int);
void vs_battle_setPlgLoadState(int);
void vs_effects_bindEffectPackageBlocks(p_file_t* arg0);
void vs_battle_aiDisengageActor(vs_battle_aiState*);
void vs_battle_aiTryEngageActor(vs_battle_aiState*);
int vs_battle_previewActionAgainstLimb(int, int, int, int, void*);
int vs_battle_previewActionAgainstActor(int, int, int, int, SVECTOR*, SVECTOR*, void*);
int vs_field_collectMovingPlatformPositions(func_8008D2C0_t*);
short vs_field_floorHeightAt(int, int);
int vs_battle_aiIsActionCooldownReady(vs_battle_aiState*, func_800E78F4_t2*);
void vs_battle_aiApplyActionRelationshipChanges(func_800DEEA4_t*);
struct actionStatSnapshot;
void vs_battle_aiApplySimulatedActionResults(struct actionStatSnapshot*);
void vs_battle_aiIssueActorCommand(vs_battle_aiState*, int);
struct vs_battle_aiState;
int vs_battle_aiCanFlyInDirection(struct vs_battle_aiState*, int);
struct vs_battle_aiState;
int func_800E0918(struct vs_battle_aiState*, int, int);
void vs_battle_aiRecoverMovement(vs_battle_aiState*);
void vs_battle_aiResolveDirectionalMovement(vs_battle_aiState*, int, int);
int vs_field_groundHeightAtTile(int, int);
int func_800E4764(vs_battle_aiState*, int*, int*);
void func_800E4B18(vs_battle_aiState*);
void vs_battle_aiRememberAiActionTarget(vs_battle_aiState*);
void vs_battle_aiForgetAiActionTarget(vs_battle_aiState* arg0);
void vs_battle_aiFloodFillAiTerrainHeights(int, int, int);
void vs_battle_aiReleaseActorAiReservations(vs_battle_aiState*);
int func_800E7698(vs_battle_aiState*);
void vs_battle_aiUpdateBossOrbitTracking(vs_battle_aiState*);

extern p_file_t vs_effects_emptyPackage;
extern D_800F16EC_t D_800F16EC[8];
extern int vs_effects_curveSamples[];
extern char vs_effects_secondaryRequest[];
extern int vs_effects_textureLoadIndex;
extern vs_effects_TextureLoadList vs_effects_textureLoads;
extern u_int D_800F5680;
extern vs_main_CdQueueSlot* vs_effects_resourceReadSlot;
extern u_int _plgLoadState;
extern _loadFileContext _plgLoadContext;
extern u_long* vs_effects_textureLoadBuffer;
extern int _plgEntryPointCount;
extern effectExec vs_effects_rendererTableAlias[];
#ifndef ROOD_PC
extern func_800CF0E8_t* vs_effects_actionRequestPointer;
#endif
/* Defined here with D_800F56C8: the two contexts mix words with pointers, so they cannot be LP64 imports. */
D_800F569C_t D_800F57A0;
D_800F569C_t D_800F56C8;
extern int D_800F5874;
extern vs_battle_aiState* vs_battle_aiActorAiStates[];
extern u_short (*D_800F58B8)[32];
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern u_short (*D_800F58D0)[32];
extern func_800DEEA4_t* vs_battle_aiPlanningWorkspace;
extern D_800F5910_t* D_800F5910;
extern int D_800F5918;
extern int D_800F591C;
extern D_800F5920_t* D_800F5920;

void vs_effects_initializeEffectResources(void);

void vs_effects_selectEffectResourceContext(int arg0);

extern D_800F569C_t* vs_effects_savedPackage;

void vs_effects_saveEffectResourceContext(void);

void vs_effects_restoreEffectResourceContext(void);

void _parsePfileBlock5(pFileBlock5* block);

void vs_effects_updateParticleGravityAndDrag(void);

void _parsePfileBlock3(void* block);

void _parsePfileCurveBlock(pFileCurveBlock* block);

void _parsePfileBlock8(void* block);

void _parsePfileBlock9(pFileBlock9* arg0);

void _parsePfileBlock10(void* arg0);

void _parsePfileBlock1(pFileBlock1* block);

void _parsePfileBlock2(pFileBlock2* block);

void _parsePfileBlock11(u_char* arg0);

void _parsePfileBlock12(void* arg0);

void _parsePfileBlock13(void* arg0);

int vs_battle_loadEffPurge(void);

int vs_effects_loadEffectPackage(void);

void vs_effects_releaseEffectPackageResources(void);

int vs_effects_updateEffectTextureLoading(void);

int vs_effects_updateEffectPluginLoading(void);

static void vs_effects_configureEffectPackageLoad(int arg0, int arg1) __attribute__((unused));
static void vs_effects_configureEffectPackageLoad(int lba, int size)
{
    vs_battle_pFileLoadContext.lba = lba;
    vs_battle_pFileLoadContext.size = size;
}

void vs_effects_setEffectTextureLoadState(int arg0);

void vs_battle_setPlgLoadState(int arg0);

void vs_effects_bindEffectPackageBlocks(p_file_t* arg0);

int vs_battle_getMainMenuLba(void);

