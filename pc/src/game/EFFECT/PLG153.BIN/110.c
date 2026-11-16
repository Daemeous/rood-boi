#include "common.h"

#define VS_MESH10_FUNCTION func_800F9910
#define VS_MESH10_VERTICES vs_effects_plg153Mesh10Vertices
#define VS_MESH10_FACES vs_effects_plg153Mesh10Faces
#include "pc/src/game/EFFECT/meshes10.h"

#define VS_RING_FUNCTION func_800FA41C
#define VS_RING_COLORS vs_effects_plg153RingColorFactors
#define VS_RING_TRANSPOSED
#include "pc/src/game/EFFECT/rings.h"

#define VS_PARTICLE_ORIGIN_FUNCTION func_800FB040
#define VS_PARTICLE_TARGET_FUNCTION func_800FB7C8
#define VS_PARTICLE_CONTROL_FUNCTION func_800FB9E0
#define VS_PARTICLE_RENDER_FUNCTION func_800FBE90
#define VS_PARTICLE_DISPATCH_FUNCTION func_800FCA50
#define VS_PARTICLE_CORNER_STATE vs_effects_plg153ParticleEmissionCounter
#include "pc/src/game/EFFECT/particleSetup.h"

#include "pc/src/game/EFFECT/particleRender.h"

#include "pc/src/game/EFFECT/particleDispatch.h"
