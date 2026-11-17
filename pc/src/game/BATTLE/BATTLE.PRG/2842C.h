#pragma once
#include <stddef.h>
#include <libgte.h>
#include <libgpu.h>
#include "pc/src/psx_types.h"

typedef struct {
    RECT uv;
    CVECTOR colors[4];
} vs_battle_backgroundTile;

typedef struct {
    int width;
    int height;
    int unk8;
    vs_battle_backgroundTile tiles[0];
} vs_battle_backgroundLayout;

typedef struct {
    u_int unk0_0 : 8;
    u_int unk0_8 : 8;
    u_int unk0_16 : 8;
    u_int unk0_24 : 8;
} vs_battle_objectData_flags;

typedef struct {
    u_char dataType;
    signed char index;
    short modelId;
    void* destination;
    __UINTPTR_TYPE__ dataAddr; /* the file's data (an address in the heap), or the slot of another loaded object that has it (below 20) */
    vs_battle_objectData_flags placement;
    u_char actorId;
    char unk11;
    char material;
    char variant;
} vs_battle_objectData;

int vs_battle_clamp(short arg0, int arg1, int arg2);
void vs_field_setWeatherVariationEnabled(int);
void vs_field_setWeatherIntensity(int);
void vs_field_setWeatherWindAngles(int, int);
int vs_field_setWeatherAmbientSound(int);
void vs_field_setRoomBackgroundOffset(int, int);
void vs_field_drawRoomBackgroundTiles(vs_battle_backgroundLayout*, int, int, int);
void vs_field_duckWeatherAmbientSound(int);
int vs_field_showRoomGeometryGroup(int);
int vs_field_hideRoomGeometryGroup(int);
int vs_field_swapRoomGeometryGroupFlags(int, int);
int vs_field_addRoomGeometryDepthBias(int, int);
void vs_field_initializeRoomEffects(void);
int vs_field_startRoomEffect(int);
int vs_field_toggleRoomEffectUpdates(int);
int vs_field_isRoomEffectActive(int);
void vs_field_finishRoomDoorRotation(int);
void vs_field_duckRoomEffectSound(int);
void vs_field_restartRoomEffectSound(void);
void vs_battle_resetFloatingLabels(void);
void vs_battle_setActorMarkersHidden(int);
void vs_battle_suppressAshleyStatusLabels(void);
void vs_render_setRoomPolygonNearClip(int);
void vs_render_setRoomTextureLodDepth(int);
int vs_battle_getEmptyObjectDataSlot(void);
void vs_render_sampleRoomLighting(int, int);
int vs_battle_populateDataSlot(vs_battle_objectData*);
int vs_battle_processObjectDataQueue(void);
int vs_content_releaseActorSequence(int, int);
int vs_actors_selectSharedSequenceBank(u_int arg0);
int vs_actors_sharedSequenceBankId(void);
int vs_content_releaseActorModel(int);
// Unprototyped: _parseShp calls it with only the actor index (the original passes one
// argument).
void vs_actors_resetActorMotionState();
void func_8009D854(int);
void func_8009E5C4(int);
void vs_actors_beginActorDeath(int, void*, int);
int vs_actors_enableModelPalettePulse(int, int, int);
void vs_field_setActorCollisionBypass(int, int);
void func_800941FC(void);

extern vs_battle_objectData vs_battle_objectDataSlots[16];
