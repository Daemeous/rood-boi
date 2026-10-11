/* The Vulkan hardware rasterizer's targets (vk_hw.c), shared with the native
 * list drawing (vk_render.inc) and the presenter (vk_present.c). */
#ifndef ROOD_VK_HW_H
#define ROOD_VK_HW_H
#include "vk_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A colour target with its G-buffer and depth (the GL target_fbo set). */
typedef struct {
    vk_image color, normal, albedo, shade, depth;
} vk_target;
typedef struct {
    int scale, width, height; /* 1024 x 512 times scale */
    vk_target live, replay;
    vk_image copy; /* scratch for VRAM-to-VRAM copies */
    vk_image room_depth, before; /* native surfaces' depth, and before an object list */
    vk_image vram; /* native VRAM, R16_UINT 1024 x 512 */
    vk_image dummy_depth; /* 1 x 1 depth for unused depth bindings */
    vk_image dummy_color; /* 1 x 1 colour for unused colour bindings */
} vk_hw_state;
extern vk_hw_state vk_hw;
#define VK_TARGET_FORMAT VK_FORMAT_R8G8B8A8_UNORM

/* The attachments a token names (live, replay, object, object replay). */
const vk_attachments* vk_hw_attachments(unsigned token);
/* The depth image a token names (live, replay, room depth, before); NULL
 * for 0. */
const vk_image* vk_hw_depth(unsigned token);
/* Viewport and scissor (native VRAM clip rectangle) for target draws;
 * returns 0 when the scissor is empty. */
int vk_hw_viewport_scissor(const int* clip);

/* A moving instance (character, weapon, prop) of an object list for the
 * path tracer (vk_render.inc -> vk_rt.inc): its mesh's vertices (render.c's
 * mesh_vertex, four per polygon) and its bone palette at a blend, as the
 * native draw used it; skinned into world-space triangles on the GPU. */
#define VK_MOVER_BONES 64
typedef struct {
    uint32_t mesh; /* render_mesh handle (identity across frames) */
    VkBuffer buffer;
    VkDeviceSize offset; /* of the mesh's first vertex */
    int polygons;
    int bones;
    float palette[VK_MOVER_BONES][12]; /* model -> world rows: r00 r01 r02 t0, r10 .. t1, r20 .. t2 */
    int override_color, override_texture;
    uint32_t color; /* r | g << 8 | b << 16 */
    uint32_t tpage, clut;
} vk_mover;
/* The opaque instances of object list `list` at palette blend t (blended
 * instances are translucent: not casters); returns how many were written. */
int pc_vk_movers(uint32_t list, float t, vk_mover* out, int max);
/* render.c's mesh_vertex size (bytes). */
#define VK_MESH_VERTEX_BYTES 40

#ifdef __cplusplus
}
#endif
#endif
