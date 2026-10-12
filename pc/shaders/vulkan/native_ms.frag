#version 450
#extension GL_GOOGLE_include_directive : require
// Native lists into multisampled twins (MSAA, vk_render.inc): the native surfaces' depth is multisampled too.
#define SURFACE_MS 1
#include "native_frag.glsl"
