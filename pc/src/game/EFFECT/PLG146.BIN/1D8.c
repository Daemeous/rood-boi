#include "common.h"

#define VS_TILED_RING_FUNCTION func_800F99D8
#define VS_TILED_RING_COLORS vs_effects_plg146TiledRingColorFactors
#include "pc/src/game/EFFECT/tiledRings.h"

#define VS_MIRROR_SURFACE_FUNCTION func_800FA8D0
#define VS_MIRROR_SURFACE_VERTICES vs_effects_plg146MirrorSurfaceVertices
#include "pc/src/game/EFFECT/mirroredSurfaces.h"

#define VS_PARTICLE_ORIGIN_FUNCTION func_800FB544
#define VS_PARTICLE_TARGET_FUNCTION func_800FBCCC
#define VS_PARTICLE_CONTROL_FUNCTION func_800FBEE4
#define VS_PARTICLE_RENDER_FUNCTION func_800FC394
#define VS_PARTICLE_CORNER_STATE vs_effects_plg146ParticleEmissionCounter
#include "pc/src/game/EFFECT/particleSetup.h"
#undef VS_PARTICLE_ORIGIN_FUNCTION
#undef VS_PARTICLE_TARGET_FUNCTION
#undef VS_PARTICLE_CONTROL_FUNCTION
#undef VS_PARTICLE_RENDER_FUNCTION
#undef VS_PARTICLE_CORNER_STATE

#define VS_PARTICLE_RENDER_FUNCTION func_800FC394
#include "pc/src/game/EFFECT/particleLineRender.h"
#undef VS_PARTICLE_RENDER_FUNCTION

#define VS_PARTICLE_ORIGIN_FUNCTION func_800FB544
#define VS_PARTICLE_TARGET_FUNCTION func_800FBCCC
#define VS_PARTICLE_CONTROL_FUNCTION func_800FBEE4
#define VS_PARTICLE_RENDER_FUNCTION func_800FC394
#define VS_PARTICLE_DISPATCH_FUNCTION func_800FC8C4
#define VS_PARTICLE_UNTEXTURED
#include "pc/src/game/EFFECT/particleDispatch.h"

#define VS_RING_FUNCTION func_800FCB38
#define VS_RING_COLORS vs_effects_plg146RingColorFactors
#define VS_RING_TRANSPOSED
#include "pc/src/game/EFFECT/rings.h"

#define VS_TRAIL_ORIGIN_FUNCTION func_800FD75C
#define VS_TRAIL_TARGET_FUNCTION func_800FDEF8
#define VS_TRAIL_CONTROL_FUNCTION func_800FE110
#define VS_TRAIL_INIT_FUNCTION func_800FE5D4
#define VS_TRAIL_UPDATE_FUNCTION func_800FE74C
#define VS_TRAIL_RENDER_FUNCTION func_800FE8D8
#define VS_TRAIL_DISPATCH_FUNCTION func_800FF334
#define VS_TRAIL_CORNER_STATE vs_effects_plg146TrailEmissionCounter
#include "pc/src/game/EFFECT/trails5.h"
