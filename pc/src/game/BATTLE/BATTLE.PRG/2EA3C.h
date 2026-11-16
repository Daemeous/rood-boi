#pragma once
#include "146C.h"
#include "pc/src/gte_value_core.h"

typedef struct {
    int x;
    int y;
} vs_battle_geomOffset;

vs_battle_roomName* vs_battle_initSceneAndGetRoomNames(void*);
void vs_battle_getGeomOffset(vs_battle_geomOffset*);
void vs_battle_setGeomOffset(vs_battle_geomOffset*);

void vs_render_drawSkyDome(void*, const vs_gte_view*);
void vs_render_buildRoomGeometryViewMatrix(VECTOR*, VECTOR*, MATRIX*);
void vs_render_buildScaleCompensatedRoomView(VECTOR*, VECTOR*, MATRIX*);
