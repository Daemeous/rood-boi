#ifndef ROOD_MATERIAL_GRAPH_GLSL
#define ROOD_MATERIAL_GRAPH_GLSL
// Node material graphs (pc/src/material_graph.cpp; pc/docs/plans/2026-10-30-node-materials-plan.md).
// This is the stub: no graph. The game writes a file of the same name into its generated shader
// folder (pc/build/shaders/generated, $ROOD_SHADERS_GENERATED) holding every graph master in use and
// a switch over the material planes' graph slots; the shader compiler reads that one first and the
// hot reload swaps it in. Without it (the embedded shaders, no graph instance) nothing here runs: the
// slot plane is 0 everywhere and native_frag.glsl never calls material_graph.
// The values table the generated file reads (knobs, instances' parameters, [0].x the game time;
// pc_material_graph_values, vk_render.inc binding 7): declared here too, so the built-in stage's
// interface (the hot reload's contract) has the binding a generated file uses.
layout(std430, binding = 7) readonly buffer MaterialGraphValues { vec4 graph_values[]; };
struct GraphIn { vec3 texel; vec3 vcolor; vec2 uv; vec3 normal; vec4 planes; vec4 relief; };
void material_graph(uint slot, GraphIn gi, inout vec4 material, inout vec4 relief, inout vec3 base, inout vec3 glow, inout int set)
{
}
#endif
