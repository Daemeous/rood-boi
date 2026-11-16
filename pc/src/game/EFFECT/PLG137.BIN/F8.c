#include "common.h"

#define VS_MESH10_FUNCTION func_800F98F8
#define VS_MESH10_VERTICES vs_effects_plg137Mesh10Vertices
#define VS_MESH10_FACES vs_effects_plg137Mesh10Faces
#include "pc/src/game/EFFECT/meshes10.h"

#define VS_TRAIL_ORIGIN_FUNCTION func_800FA404
#define VS_TRAIL_TARGET_FUNCTION func_800FABA0
#define VS_TRAIL_CONTROL_FUNCTION func_800FADB8
#define VS_TRAIL_INIT_FUNCTION func_800FB27C
#define VS_TRAIL_UPDATE_FUNCTION func_800FB3F4
#define VS_TRAIL_RENDER_FUNCTION func_800FB580
#define VS_TRAIL_DISPATCH_FUNCTION func_800FBFDC
#define VS_TRAIL_CORNER_STATE vs_effects_plg137TrailEmissionCounter
#include "pc/src/game/EFFECT/trails5.h"

#define VS_GRID_FUNCTION func_800FC524
#include "pc/src/game/EFFECT/grids.h"

#define VS_TILED_RING_FUNCTION func_800FD074
#define VS_TILED_RING_COLORS vs_effects_plg137TiledRingColorFactors
#include "pc/src/game/EFFECT/tiledRings.h"
