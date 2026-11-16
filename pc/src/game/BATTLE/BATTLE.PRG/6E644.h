#pragma once
#include "146C.h"

typedef struct {
    u_short lba;
    u_short size;
} _loadFileContext;

typedef struct {
    u_short effectId;
    char unk2;
    char unk3;
    u_short unk4;
    short unk6;
    union {
        char u8[2];
        short s16;
    } unk8;
    union {
        char u8[2];
        short s16;
    } unkA;
    short unkC;
    short unkE;
    short unk10;
    short unk12;
    union {
        char u8[2];
        u_short u16;
    } unk14;
    union {
        char u8[2];
        u_short u16;
    } unk16;
    short unk18;
    short unk1A;
    int unk1C[45];
    char unkD0[16];
    SVECTOR unkE0;
} func_800CF0E8_t;

typedef struct func_800D4910_t func_800D4910_t;

typedef int (*effectExec)(func_800D4910_t*, u_int, int);

struct func_800D4910_t {
    struct func_800D4910_t* next;
    effectExec renderer;
    void* unk8;
};

void vs_effects_acquireParticlePool(int);
void vs_effects_setEffectTextureLoadState(int);
#ifdef ROOD_PC
/* The host passes the particle list (the PS1 wrapper forwards its $a0). */
void vs_effects_drawAnimatedSpriteParticles(void* particles);
#else
void vs_effects_drawAnimatedSpriteParticles(void);
#endif
void vs_effects_initializeEffectResources(void);
void vs_effects_updateParticleGravityAndDrag(void);
void vs_battle_configurePlg(int, int, int);
void vs_battle_setEffectExec(effectExec arg0, int arg1);
void vs_effects_setEffectTextureAssetCount(int arg0);
void vs_battle_setPlgLoadState(int);
void vs_battle_configureEffectFbLoad(
    int lba, int size, int suffix, int isClut, int index);
void vs_battle_aiUpdateRouteGateAvailability(void);
int vs_battle_aiRebuildActorActionPlans(void*);
void vs_battle_aiSetActorNavigationOverride(int, int, int);
void vs_battle_aiReleaseAiDestinationTile(int, int, int, int);
int vs_battle_aiActorNavigationOverrideStatus(void);
void func_800E5998(void);
void vs_battle_aiInitializeRoomNavigation(void);
struct vs_battle_actor;
int vs_battle_aiCreateActorAiState(struct vs_battle_actor*, int);
short vs_battle_aiPackActorAiPersistenceFlags(int);
void func_800E6700(int);
void vs_battle_aiMarkAiTileDynamicObstacle(int, int);
void func_800E6EAC(void);
void vs_battle_aiFreeRoomAiState(void);
void vs_battle_aiFreeActorAiState(vs_battle_actor*);
void vs_battle_aiClearActorAiLinksAndPlan(int);
int vs_core_elapsedFrameCounterTicks(void);
void func_800E511C(void);
void vs_battle_aiUpdateActorNavigationOverrides(void);
int vs_battle_getMainMenuLba(void);
int vs_battle_loadEffPurge(void);

extern int D_800F5684;
extern void* vs_battle_pfileBuf;
extern _loadFileContext vs_battle_pFileLoadContext;
extern func_800CF0E8_t vs_effects_actionRequest;
