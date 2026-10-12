#version 450
#extension GL_GOOGLE_include_directive : require
// Packets into multisampled twins (MSAA, vk_hw.c): the native surfaces' depth is multisampled too.
#define SURFACE_MS 1
#include "hw_frag.glsl"
