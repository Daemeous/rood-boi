#include "common.h"

#define VS_TILED_RING_FUNCTION func_800F98F8
#define VS_TILED_RING_COLORS vs_effects_plg073TiledRingColorFactors
#include "pc/src/game/EFFECT/tiledRings.h"

#define VS_RING_FUNCTION func_800FA7F0
#define VS_RING_COLORS vs_effects_plg073RingColorFactors
#include "pc/src/game/EFFECT/rings.h"

#define VS_TRAIL_ORIGIN_FUNCTION func_800FB414
#define VS_TRAIL_TARGET_FUNCTION func_800FBBB0
#define VS_TRAIL_CONTROL_FUNCTION func_800FBDC8
#define VS_TRAIL_INIT_FUNCTION func_800FC28C
#define VS_TRAIL_UPDATE_FUNCTION func_800FC404
#define VS_TRAIL_RENDER_FUNCTION func_800FC590
#define VS_TRAIL_DISPATCH_FUNCTION func_800FCFEC
#define VS_TRAIL_CORNER_STATE vs_effects_plg073TrailEmissionCounter
#include "pc/src/game/EFFECT/trails5.h"

#define VS_ROTATING_SURFACE_FUNCTION func_800FD534
#include "pc/src/game/EFFECT/rotatingSurfaces.h"

#define VS_GRID_FUNCTION func_800FE3EC
#include "pc/src/game/EFFECT/grids.h"
