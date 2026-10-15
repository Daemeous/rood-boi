/* Vulkan hardware rasterizer: the scaled VRAM target, its G-buffer and the
 * operations gpu_hw.c performs on them (vk_hooks.h). gpu_hw.c decides what
 * to draw; this file only draws it, in the order it is asked to. */
#define _POSIX_C_SOURCE 200809L
#include "vk_hw.h"
#include "emissive.h"
#include "vk_hooks.h"
#include "vk_shaders.h"
#include "ui_upscale.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

vk_hw_state vk_hw;
extern int pc_hw_perspective, pc_hw_texture_filter;

/* gpu_hw.c's vertex (the layout must match; checked by size at init). */
typedef struct {
    float x, y, z;
    uint8_t r, g, b, flags;
    uint16_t u, v;
    uint16_t tpage, clut;
    uint32_t window;
    int8_t nx, ny, nz, lit;
    uint8_t uv_min[2], uv_max[2];
    int16_t ui[2];
} hw_vertex;

static VkDescriptorSetLayout draw_set, upload_set;
static VkPipelineLayout draw_layout, upload_layout;
static VkPipeline opaque_pipeline, blend_pipelines[4], cover_pipeline, upload_pipeline;
/* Text from distance texels (gpu_hw.c BLEND_TEXT): its solid pixels' G-buffer
 * and depth (colour untouched), its colour added / subtracted by coverage
 * ("over": cover_pipeline). */
static VkPipeline text_solid_pipeline, text_add_pipeline, text_sub_pipeline;
static vk_attachments live_attachments, replay_attachments, object_attachments, object_replay_attachments;
/* MSAA: the twins' attachments, the packet pipelines at their sample count
 * ([1]: 2D, every sample at the pixel centre), and load/resolve. */
static vk_attachments ms_attachments, ms_object_attachments;
static VkPipeline ms_opaque[2], ms_blend[2][4], ms_cover; /* ms_cover: BLEND_COVER, at the pixel centre */
static VkPipeline ms_text_solid, ms_text_add, ms_text_sub; /* BLEND_TEXT, at the pixel centre */
/* The text overlay (pc_vk_hw_text_overlay): into the window image ([0]) or
 * the brightness input ([1]), over / added / subtracted by coverage. */
static VkDescriptorSetLayout overlay_set;
static VkPipelineLayout overlay_layout;
static VkPipeline overlay_pipelines[2][3];
typedef struct {
    float rect[4];
    int pass, ui_scale;
    float text_factor;
} overlay_push;
static VkDescriptorSetLayout ms_set;
static VkPipelineLayout ms_layout;
static VkPipeline ms_load_pipeline, ms_depth_pipeline, ms_resolve_pipeline, ms_sync_pipeline;
static int ms_pipeline_samples;

typedef struct {
    float occlusion_rect[4];
    int perspective, filtering, occlusion, pass;
    int ui_scale; /* the UI atlas's magnification (ui_upscale.c), 0: none */
    float text_factor; /* text's translucent pass: the PS1 mode's alpha factor */
    int emissive; /* emissive texels: the slot map (binding 3) is read */
} draw_push;
typedef struct {
    int rect[4];
    float scale;
} upload_push;

static void make_attachments(vk_attachments* a, const vk_target* t, const vk_image* depth)
{
    memset(a, 0, sizeof *a);
    a->colors[0] = t->color.view;
    a->colors[1] = t->normal.view;
    a->colors[2] = t->albedo.view;
    a->colors[3] = t->shade.view;
    a->color_count = 4;
    a->depth = depth->view;
    a->width = vk_hw.width;
    a->height = vk_hw.height;
}
static int make_target(vk_target* t)
{
    VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageUsageFlags depth = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return vk_image_create(&t->color, vk_hw.width, vk_hw.height, VK_TARGET_FORMAT, color)
        && vk_image_create(&t->normal, vk_hw.width, vk_hw.height, VK_TARGET_FORMAT, color)
        && vk_image_create(&t->albedo, vk_hw.width, vk_hw.height, VK_TARGET_FORMAT, color)
        && vk_image_create(&t->shade, vk_hw.width, vk_hw.height, VK_TARGET_FORMAT, color)
        && vk_image_create(&t->depth, vk_hw.width, vk_hw.height, vk.depth_format, depth);
}
static void destroy_target(vk_target* t)
{
    vk_image_destroy_later(&t->color);
    vk_image_destroy_later(&t->normal);
    vk_image_destroy_later(&t->albedo);
    vk_image_destroy_later(&t->shade);
    vk_image_destroy_later(&t->depth);
}
static int make_targets(int scale)
{
    vk_hw.scale = scale;
    vk_hw.width = 1024 * scale;
    vk_hw.height = 512 * scale;
    VkImageUsageFlags depth = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!make_target(&vk_hw.live) || !make_target(&vk_hw.replay)
        || !vk_image_create(&vk_hw.copy, vk_hw.width, vk_hw.height, VK_TARGET_FORMAT,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        || !vk_image_create(&vk_hw.room_depth, vk_hw.width, vk_hw.height, vk.depth_format, depth)
        || !vk_image_create(&vk_hw.before, vk_hw.width, vk_hw.height, vk.depth_format, depth))
        return 0;
    make_attachments(&live_attachments, &vk_hw.live, &vk_hw.live.depth);
    make_attachments(&replay_attachments, &vk_hw.replay, &vk_hw.replay.depth);
    make_attachments(&object_attachments, &vk_hw.live, &vk_hw.room_depth);
    make_attachments(&object_replay_attachments, &vk_hw.replay, &vk_hw.room_depth);
    return 1;
}

const vk_attachments* vk_hw_attachments(unsigned token)
{
    switch (token) {
    case VK_TOKEN_LIVE:
        return &live_attachments;
    case VK_TOKEN_REPLAY:
        return &replay_attachments;
    case VK_TOKEN_OBJECT:
        return &object_attachments;
    case VK_TOKEN_OBJECT_REPLAY:
        return &object_replay_attachments;
    case VK_TOKEN_MS:
        return vk_hw.ms_samples ? &ms_attachments : NULL;
    case VK_TOKEN_MS_OBJECT:
        return vk_hw.ms_samples ? &ms_object_attachments : NULL;
    }
    return NULL;
}
int vk_hw_token_samples(unsigned token)
{
    return token == VK_TOKEN_MS || token == VK_TOKEN_MS_OBJECT || token == VK_TOKEN_MS_ROOM_DEPTH
            || token == VK_TOKEN_MS_BEFORE
        ? vk_hw.ms_samples
        : 1;
}
const vk_image* vk_hw_depth(unsigned token)
{
    switch (token) {
    case VK_TOKEN_LIVE:
        return &vk_hw.live.depth;
    case VK_TOKEN_REPLAY:
        return &vk_hw.replay.depth;
    case VK_TOKEN_ROOM_DEPTH:
        return &vk_hw.room_depth;
    case VK_TOKEN_BEFORE:
        return &vk_hw.before;
    case VK_TOKEN_MS:
        return vk_hw.ms_samples ? &vk_hw.ms.depth : NULL;
    case VK_TOKEN_MS_ROOM_DEPTH:
        return vk_hw.ms_samples ? &vk_hw.ms_room : NULL;
    case VK_TOKEN_MS_BEFORE:
        return vk_hw.ms_samples ? &vk_hw.ms_before : NULL;
    }
    return NULL;
}
static const vk_target* target_of(unsigned token)
{
    return token == VK_TOKEN_REPLAY || token == VK_TOKEN_OBJECT_REPLAY ? &vk_hw.replay : &vk_hw.live;
}

int vk_hw_viewport_scissor(const int* clip)
{
    int s = vk_hw.scale;
    int x0 = clip[0] * s, y0 = clip[1] * s, x1 = (clip[0] + (clip[2] > 0 ? clip[2] : 0)) * s,
        y1 = (clip[1] + (clip[3] > 0 ? clip[3] : 0)) * s;
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > vk_hw.width ? vk_hw.width : x1, y1 = y1 > vk_hw.height ? vk_hw.height : y1;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    VkCommandBuffer cmd = vk_cmd();
    VkViewport viewport = { 0, 0, (float)vk_hw.width, (float)vk_hw.height, 0, 1 };
    VkRect2D scissor = { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    return 1;
}

static VkPipeline target_pipeline_ms(VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout,
    const VkVertexInputBindingDescription* binding, const VkVertexInputAttributeDescription* attributes, int count,
    VkPrimitiveTopology topology, int blend, unsigned masks, int samples, int centre)
{
    static const VkFormat formats[4] = { VK_TARGET_FORMAT, VK_TARGET_FORMAT, VK_TARGET_FORMAT, VK_TARGET_FORMAT };
    vk_pipeline_desc d = { .vertex = vs,
        .fragment = fs,
        .layout = layout,
        .topology = topology,
        .binding = binding,
        .attributes = attributes,
        .attribute_count = count,
        .color_count = 4,
        .color_formats = formats,
        .depth_format = vk.depth_format,
        .blend = blend,
        .samples = samples,
        .centre = centre };
    for (int i = 0; i < 4; i++)
        d.write_masks[i] = (masks >> (i * 4)) & 15;
    return vk_pipeline(&d);
}
static VkPipeline target_pipeline(VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout,
    const VkVertexInputBindingDescription* binding, const VkVertexInputAttributeDescription* attributes, int count,
    VkPrimitiveTopology topology, int blend, unsigned masks)
{
    return target_pipeline_ms(vs, fs, layout, binding, attributes, count, topology, blend, masks, 1, 0);
}

static const VkVertexInputBindingDescription binding = { 0, sizeof(hw_vertex), VK_VERTEX_INPUT_RATE_VERTEX };
static const VkVertexInputAttributeDescription attributes[8] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(hw_vertex, x) },
    { 1, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(hw_vertex, r) },
    { 2, 0, VK_FORMAT_R16G16_UINT, offsetof(hw_vertex, u) },
    { 3, 0, VK_FORMAT_R16G16_UINT, offsetof(hw_vertex, tpage) },
    { 4, 0, VK_FORMAT_R32_UINT, offsetof(hw_vertex, window) },
    { 5, 0, VK_FORMAT_R8G8B8A8_SNORM, offsetof(hw_vertex, nx) },
    { 6, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(hw_vertex, uv_min) },
    { 7, 0, VK_FORMAT_R16G16_SINT, offsetof(hw_vertex, ui) },
};
int pc_vk_hw_init(int scale, size_t vertex_size)
{
    if (vertex_size != sizeof(hw_vertex)) {
        fprintf(stderr, "[vk] gpu_hw.c's vertex is %zu bytes, vk_hw.c expects %zu\n", vertex_size, sizeof(hw_vertex));
        return 0;
    }
    if (!make_targets(scale)
        || !vk_image_create(&vk_hw.vram, 1024, 512, VK_FORMAT_R16_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        || !vk_image_create(&vk_hw.emissive, PC_EMISSIVE_MAP_W, PC_EMISSIVE_MAP_H, VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        || !vk_image_create(&vk_hw.dummy_depth, 1, 1, vk.depth_format,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        || !vk_image_create(&vk_hw.dummy_color, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return 0;
    VkDescriptorType draw_types[4] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
    draw_set = vk_push_layout(draw_types, 4);
    upload_set = vk_push_layout(draw_types, 1);
    VkPushConstantRange draw_range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw_push) };
    VkPipelineLayoutCreateInfo layout = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &draw_set,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &draw_range };
    if (!vk_check(vkCreatePipelineLayout(vk.device, &layout, NULL, &draw_layout), "vkCreatePipelineLayout"))
        return 0;
    VkPushConstantRange upload_range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(upload_push) };
    layout.pSetLayouts = &upload_set;
    layout.pPushConstantRanges = &upload_range;
    if (!vk_check(vkCreatePipelineLayout(vk.device, &layout, NULL, &upload_layout), "vkCreatePipelineLayout"))
        return 0;

    VkShaderModule vs = vk_shader(vk_spv_hw_vert, vk_spv_hw_vert_size),
                   fs = vk_shader(vk_spv_hw_frag, vk_spv_hw_frag_size);
    opaque_pipeline = target_pipeline(vs, fs, draw_layout, &binding, attributes, 8,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, -1, 0xffff);
    for (int m = 0; m < 4; m++)
        blend_pipelines[m] = target_pipeline(vs, fs, draw_layout, &binding, attributes, 8,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, m, 0x000f);
    /* gpu_hw.c's BLEND_COVER (anti-aliased 2D shapes): "over" by coverage,
     * colour only (the mask bit in alpha and the G-buffer kept). */
    cover_pipeline = target_pipeline(vs, fs, draw_layout, &binding, attributes, 8,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 4, 0x0007);
    text_solid_pipeline = target_pipeline(vs, fs, draw_layout, &binding, attributes, 8,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, -1, 0xfff0);
    text_add_pipeline = target_pipeline(vs, fs, draw_layout, &binding, attributes, 8,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 5, 0x0007);
    text_sub_pipeline = target_pipeline(vs, fs, draw_layout, &binding, attributes, 8,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 6, 0x0007);
    VkShaderModule uvs = vk_shader(vk_spv_upload_vert, vk_spv_upload_vert_size),
                   ufs = vk_shader(vk_spv_upload_frag, vk_spv_upload_frag_size);
    upload_pipeline = target_pipeline(uvs, ufs, upload_layout, NULL, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        -1, 0x000f);
    vkDestroyShaderModule(vk.device, vs, NULL);
    vkDestroyShaderModule(vk.device, fs, NULL);
    vkDestroyShaderModule(vk.device, uvs, NULL);
    vkDestroyShaderModule(vk.device, ufs, NULL);
    if (!opaque_pipeline || !upload_pipeline || !cover_pipeline || !text_solid_pipeline || !text_add_pipeline
        || !text_sub_pipeline)
        return 0;
    for (int m = 0; m < 4; m++)
        if (!blend_pipelines[m])
            return 0;
    return 1;
}

void pc_vk_hw_resize(int scale)
{
    destroy_target(&vk_hw.live);
    destroy_target(&vk_hw.replay);
    vk_image_destroy_later(&vk_hw.copy);
    vk_image_destroy_later(&vk_hw.room_depth);
    vk_image_destroy_later(&vk_hw.before);
    vk_end_rendering();
    if (!make_targets(scale))
        fprintf(stderr, "[vk] cannot make %dx targets\n", scale);
}

/* ---- native VRAM ---- */

void pc_vk_hw_upload_tiles(const uint16_t* vram, uint32_t tiles)
{
    if (!tiles)
        return;
    vk_end_rendering();
    VkCommandBuffer cmd = vk_cmd();
    VkBufferImageCopy regions[32];
    int count = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    for (int tile = 0; tile < 32; tile++) {
        if (!(tiles & (1u << tile)))
            continue;
        int x = (tile & 15) * 64, y = (tile >> 4) * 256;
        vk_slice slice = vk_arena(64 * 256 * 2, 16);
        if (!slice.data)
            return;
        for (int row = 0; row < 256; row++)
            memcpy((uint16_t*)slice.data + row * 64, vram + (y + row) * 1024 + x, 64 * 2);
        if (buffer && buffer != slice.buffer) {
            vkCmdCopyBufferToImage(cmd, buffer, vk_hw.vram.image, VK_IMAGE_LAYOUT_GENERAL, (uint32_t)count, regions);
            count = 0;
        }
        buffer = slice.buffer;
        regions[count++] = (VkBufferImageCopy){ .bufferOffset = slice.offset,
            .bufferRowLength = 64,
            .bufferImageHeight = 256,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { x, y, 0 },
            .imageExtent = { 64, 256, 1 } };
    }
    if (count)
        vkCmdCopyBufferToImage(cmd, buffer, vk_hw.vram.image, VK_IMAGE_LAYOUT_GENERAL, (uint32_t)count, regions);
    vk_barrier();
}

/* Emissive texels' slot map (emissive.h): rows [row0, row1) of the CPU copy. */
void pc_vk_hw_upload_emissive(const uint8_t* map, int row0, int row1)
{
    if (row1 <= row0 || !vk_hw.emissive.image)
        return;
    vk_end_rendering();
    VkDeviceSize bytes = (VkDeviceSize)(row1 - row0) * PC_EMISSIVE_MAP_W;
    vk_slice slice = vk_arena(bytes, 16);
    if (!slice.data)
        return;
    memcpy(slice.data, map + (size_t)row0 * PC_EMISSIVE_MAP_W, (size_t)bytes);
    VkBufferImageCopy region = { .bufferOffset = slice.offset,
        .bufferRowLength = PC_EMISSIVE_MAP_W,
        .bufferImageHeight = (uint32_t)(row1 - row0),
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { 0, row0, 0 },
        .imageExtent = { PC_EMISSIVE_MAP_W, (uint32_t)(row1 - row0), 1 } };
    vkCmdCopyBufferToImage(vk_cmd(), slice.buffer, vk_hw.emissive.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    vk_barrier();
}

/* ---- packet batches ---- */

int pc_hw_emissive(unsigned* texture); /* gpu_hw.c: the slot map is current; shaders read it */

void pc_vk_hw_draw_batch(unsigned fbo, const void* vertices, int count, int blend, const int* clip, int occlusion,
    const int* occlusion_rect)
{
    if (count <= 0)
        return;
    const vk_attachments* a = vk_hw_attachments(fbo);
    if (!a)
        return;
    vk_slice slice = vk_arena((VkDeviceSize)count * sizeof(hw_vertex), 16);
    if (!slice.data)
        return;
    memcpy(slice.data, vertices, (size_t)count * sizeof(hw_vertex));
    vk_begin_rendering(a);
    if (!vk_hw_viewport_scissor(clip))
        return;
    VkCommandBuffer cmd = vk_cmd();
    vk_descriptors d = { 0 };
    vk_descriptors_image(&d, 0, vk_hw.vram.view, vk.nearest);
    vk_descriptors_image(&d, 1, fbo == VK_TOKEN_MS ? vk_hw.ms_room.view : vk_hw.room_depth.view, vk.nearest);
    VkImageView atlas = (VkImageView)pc_ui_upscale_vk_view();
    vk_descriptors_image(&d, 2, atlas ? atlas : vk_hw.vram.view, vk.nearest);
    vk_descriptors_image(&d, 3, vk_hw.emissive.view, vk.nearest);
    draw_push push = { .perspective = pc_hw_perspective, .filtering = pc_hw_texture_filter, .occlusion = occlusion,
        .ui_scale = atlas ? pc_ui_upscale_draw_factor() : 0, .emissive = pc_hw_emissive(NULL) };
    if (occlusion) {
        int s = vk_hw.scale;
        push.occlusion_rect[0] = (float)(occlusion_rect[0] * s);
        push.occlusion_rect[1] = (float)(occlusion_rect[1] * s);
        push.occlusion_rect[2] = (float)((occlusion_rect[0] + occlusion_rect[2]) * s);
        push.occlusion_rect[3] = (float)((occlusion_rect[1] + occlusion_rect[3]) * s);
    }
    vkCmdBindVertexBuffers(cmd, 0, 1, &slice.buffer, &slice.offset);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE); /* ALWAYS: depth in draw order, never rejected */
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_ALWAYS);
    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
    /* Multisampled: 2D (any vertex without depth) takes every sample at the
     * pixel centre, exactly as single-sampled; 3D packets get coverage AA. */
    int ms = fbo == VK_TOKEN_MS, centre = 0;
    if (ms)
        for (int i = 0; i < count && !centre; i++)
            centre = ((const hw_vertex*)vertices)[i].z <= 0.0f;
    if (blend >= 16) { /* BLEND_TEXT (+ the PS1 mode, 4: opaque): text from distance texels */
        int mode = blend - 16;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ms ? ms_text_solid : text_solid_pipeline);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout);
        push.pass = 3;
        vkCmdPushConstants(cmd, draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
            &push);
        vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
        vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ms ? ms_cover : cover_pipeline);
        push.pass = 4;
        vkCmdPushConstants(cmd, draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
            &push);
        vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
        if (mode < 4) {
            VkPipeline p = mode == 0 ? (ms ? ms_cover : cover_pipeline)
                         : mode == 2 ? (ms ? ms_text_sub : text_sub_pipeline)
                                     : (ms ? ms_text_add : text_add_pipeline);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
            push.pass = 5;
            push.text_factor = mode == 0 ? 0.5f : mode == 3 ? 0.25f : 1.0f;
            vkCmdPushConstants(cmd, draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                sizeof push, &push);
            vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
        }
        return;
    }
    if (blend == 13) {
        /* BLEND_FADE (a screen fade): subtracted from what is not 3D under it
         * only (depth 0, and far: drawn twice, gpu_hw.c pc_hw_triangle) */
        vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_EQUAL);
        vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ms ? ms_blend[centre][2] : blend_pipelines[2]);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout);
        push.pass = 2;
        vkCmdPushConstants(cmd, draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
            &push);
        vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
        return;
    }
    if (blend == 12) { /* BLEND_COVER (2D shapes): one blended pass, depth "UI" */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ms ? ms_cover : cover_pipeline);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout);
        push.pass = 2;
        vkCmdPushConstants(cmd, draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
            &push);
        vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
        return;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ms ? ms_opaque[centre] : opaque_pipeline);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout);
    push.pass = blend < 0 ? 0 : 1;
    vkCmdPushConstants(cmd, draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
    vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
    if (blend >= 0) {
        /* Translucent texels keep the depth and the surface behind them;
         * 2D ones (blend + 4) replace the depth with "UI", except additive
         * ones (they only add light). */
        int additive = (blend & 3) == 1 || (blend & 3) == 3;
        vkCmdSetDepthWriteEnable(cmd, blend >= 4 && !additive ? VK_TRUE : VK_FALSE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ms ? ms_blend[centre][blend & 3] : blend_pipelines[blend & 3]);
        push.pass = 2;
        vkCmdPushConstants(cmd, draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
            &push);
        vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
    }
}

/* ---- the text overlay (gpu_hw.c pc_hw_text_overlay) ---- */

static int overlay_ready(void)
{
    static int state; /* 0 not yet, 1 ready, -1 failed */
    if (state)
        return state > 0;
    state = -1;
    VkDescriptorType types[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
    overlay_set = vk_push_layout(types, 2);
    VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(overlay_push) };
    VkPipelineLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &overlay_set,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &range };
    if (!vk_check(vkCreatePipelineLayout(vk.device, &info, NULL, &overlay_layout), "vkCreatePipelineLayout"))
        return 0;
    VkShaderModule vs = vk_shader(vk_spv_text_overlay_vert, vk_spv_text_overlay_vert_size),
                   fs = vk_shader(vk_spv_text_overlay_frag, vk_spv_text_overlay_frag_size);
    /* The presenters' formats (vk_present.c): the window image, the 16-bit brightness input. */
    static const VkFormat formats[2] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT };
    static const int blends[3] = { 4, 5, 6 };
    int ok = vs && fs;
    for (int f = 0; f < 2 && ok; f++)
        for (int b = 0; b < 3 && ok; b++) {
            vk_pipeline_desc d = { .vertex = vs,
                .fragment = fs,
                .layout = overlay_layout,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .binding = &binding,
                .attributes = attributes,
                .attribute_count = 8,
                .color_count = 1,
                .color_formats = &formats[f],
                .depth_format = VK_FORMAT_UNDEFINED,
                .write_masks = { 7 },
                .blend = blends[b] };
            overlay_pipelines[f][b] = vk_pipeline(&d);
            ok = overlay_pipelines[f][b] != VK_NULL_HANDLE;
        }
    if (vs)
        vkDestroyShaderModule(vk.device, vs, NULL);
    if (fs)
        vkDestroyShaderModule(vk.device, fs, NULL);
    if (!ok)
        fprintf(stderr, "[vk] text overlay unavailable (its pipelines did not build)\n");
    state = ok ? 1 : -1;
    return ok;
}

void pc_vk_hw_text_overlay(const void* vertices, int count, int mode, const float* rect, const float* clip, int view_x,
    int view_y, int to_view)
{
    VkImageView atlas = (VkImageView)pc_ui_upscale_vk_view();
    if (count <= 0 || !atlas || !overlay_ready())
        return;
    vk_slice slice = vk_arena((VkDeviceSize)count * sizeof(hw_vertex), 16);
    if (!slice.data)
        return;
    memcpy(slice.data, vertices, (size_t)count * sizeof(hw_vertex));
    VkCommandBuffer cmd = vk_cmd();
    int ox = to_view ? 0 : view_x, oy = to_view ? 0 : view_y;
    int x0 = (int)floorf(clip[0]), y0 = (int)floorf(clip[1]), x1 = (int)ceilf(clip[2]), y1 = (int)ceilf(clip[3]);
    VkRect2D scissor = { { ox + x0, oy + y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindVertexBuffers(cmd, 0, 1, &slice.buffer, &slice.offset);
    vk_descriptors d = { 0 };
    vk_descriptors_image(&d, 0, vk_hw.vram.view, vk.nearest);
    vk_descriptors_image(&d, 1, atlas, vk.nearest);
    overlay_push push = { { rect[0], rect[1], rect[2], rect[3] }, 4, pc_ui_upscale_draw_factor(), 1.0f };
    int f = to_view ? 1 : 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipelines[f][0]);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_layout);
    vkCmdPushConstants(cmd, overlay_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
        &push);
    vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
    if (mode < 4) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            overlay_pipelines[f][mode == 0 ? 0 : mode == 2 ? 2 : 1]);
        push.pass = 5;
        push.text_factor = mode == 0 ? 0.5f : mode == 3 ? 0.25f : 1.0f;
        vkCmdPushConstants(cmd, overlay_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            sizeof push, &push);
        vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
    }
}

/* ---- fills, uploads, copies ---- */

static int clear_rect(int x, int y, int w, int h, VkClearRect* rect)
{
    int s = vk_hw.scale;
    int x0 = x * s, y0 = y * s, x1 = (x + w) * s, y1 = (y + h) * s;
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > vk_hw.width ? vk_hw.width : x1, y1 = y1 > vk_hw.height ? vk_hw.height : y1;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    *rect = (VkClearRect){ { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } }, 0, 1 };
    return 1;
}
void pc_vk_hw_fill(unsigned fbo, int x, int y, int w, int h, int r, int g, int b)
{
    const vk_attachments* a = vk_hw_attachments(fbo);
    VkClearRect rect;
    if (!a || !clear_rect(x, y, w, h, &rect))
        return;
    vk_begin_rendering(a);
    /* Fills store 5-bit colours, clear the G-buffer (unlit) and reset depth. */
    VkClearAttachment clears[5];
    for (int i = 0; i < 4; i++)
        clears[i] = (VkClearAttachment){ VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)i, { .color = { { 0, 0, 0, 0 } } } };
    clears[0].clearValue.color.float32[0] = (r >> 3) / 31.0f;
    clears[0].clearValue.color.float32[1] = (g >> 3) / 31.0f;
    clears[0].clearValue.color.float32[2] = (b >> 3) / 31.0f;
    clears[4] = (VkClearAttachment){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } };
    vkCmdClearAttachments(vk_cmd(), 5, clears, 1, &rect);
}
void pc_vk_hw_upload_rect(int x, int y, int w, int h)
{
    VkClearRect rect;
    if (!clear_rect(x, y, w, h, &rect))
        return;
    vk_begin_rendering(&live_attachments);
    VkCommandBuffer cmd = vk_cmd();
    VkViewport viewport = { 0, 0, (float)vk_hw.width, (float)vk_hw.height, 0, 1 };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &rect.rect);
    vkCmdSetDepthTestEnable(cmd, VK_FALSE);
    vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_ALWAYS);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, upload_pipeline);
    vk_descriptors d = { 0 };
    vk_descriptors_image(&d, 0, vk_hw.vram.view, vk.nearest);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, upload_layout);
    upload_push push = { { x, y, w, h }, (float)vk_hw.scale };
    vkCmdPushConstants(cmd, upload_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
    vkCmdDraw(cmd, 4, 1, 0, 0);
    /* The image itself; then mark it unlit (G-buffer cleared, depth kept). */
    VkClearAttachment clears[3];
    for (int i = 0; i < 3; i++)
        clears[i] = (VkClearAttachment){ VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(i + 1), { .color = { { 0, 0, 0, 0 } } } };
    vkCmdClearAttachments(cmd, 3, clears, 1, &rect);
}
static void copy_image(const vk_image* from, const vk_image* to, int sx, int sy, int dx, int dy, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    VkImageCopy region = { .srcSubresource = { from->aspect, 0, 0, 1 },
        .srcOffset = { sx, sy, 0 },
        .dstSubresource = { to->aspect, 0, 0, 1 },
        .dstOffset = { dx, dy, 0 },
        .extent = { (uint32_t)w, (uint32_t)h, 1 } };
    vkCmdCopyImage(vk_cmd(), from->image, VK_IMAGE_LAYOUT_GENERAL, to->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
}
void pc_vk_hw_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    int s = vk_hw.scale;
    vk_end_rendering();
    copy_image(&vk_hw.live.color, &vk_hw.copy, sx * s, sy * s, 0, 0, w * s, h * s);
    vk_barrier();
    copy_image(&vk_hw.copy, &vk_hw.live.color, 0, 0, dx * s, dy * s, w * s, h * s);
    vk_barrier();
}
static int clamp_rect(int* x0, int* y0, int* x1, int* y1)
{
    *x0 = *x0 < 0 ? 0 : *x0, *y0 = *y0 < 0 ? 0 : *y0;
    *x1 = *x1 > vk_hw.width ? vk_hw.width : *x1, *y1 = *y1 > vk_hw.height ? vk_hw.height : *y1;
    return *x1 > *x0 && *y1 > *y0;
}
void pc_vk_hw_copy_depth(unsigned from, unsigned to, int x0, int y0, int x1, int y1)
{
    const vk_image *a = vk_hw_depth(from), *b = vk_hw_depth(to);
    if (!a || !b || !clamp_rect(&x0, &y0, &x1, &y1))
        return;
    int ms = vk_hw_token_samples(from) > 1;
    if (ms)
        vk_section("msaa copy");
    vk_end_rendering();
    copy_image(a, b, x0, y0, x0, y0, x1 - x0, y1 - y0);
    vk_barrier();
    if (ms)
        vk_section("draw");
}
void pc_vk_hw_clear_depth(unsigned token, int x0, int y0, int x1, int y1)
{
    const vk_image* depth = vk_hw_depth(token);
    if (!depth || !clamp_rect(&x0, &y0, &x1, &y1))
        return;
    vk_attachments a = { .depth = depth->view, .width = vk_hw.width, .height = vk_hw.height };
    vk_begin_rendering(&a);
    VkClearAttachment clear = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } };
    VkClearRect rect = { { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } }, 0, 1 };
    vkCmdClearAttachments(vk_cmd(), 1, &clear, 1, &rect);
    vk_end_rendering();
}
void pc_vk_hw_replay_copy(int x0, int y0, int x1, int y1)
{
    if (!clamp_rect(&x0, &y0, &x1, &y1))
        return;
    vk_end_rendering();
    const vk_image* from[5] = { &vk_hw.live.color, &vk_hw.live.normal, &vk_hw.live.albedo, &vk_hw.live.shade,
        &vk_hw.live.depth };
    const vk_image* to[5] = { &vk_hw.replay.color, &vk_hw.replay.normal, &vk_hw.replay.albedo, &vk_hw.replay.shade,
        &vk_hw.replay.depth };
    for (int i = 0; i < 5; i++)
        copy_image(from[i], to[i], x0, y0, x0, y0, x1 - x0, y1 - y0);
    vk_barrier();
}

/* ---- readbacks ---- */

int pc_vk_hw_dump(const char* path, int replay)
{
    int w = vk_hw.width, h = vk_hw.height;
    unsigned char* pixels = malloc((size_t)w * h * 4);
    if (!pixels || !vk_read_image(&target_of(replay ? VK_TOKEN_REPLAY : VK_TOKEN_LIVE)->color,
                       VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, pixels, 4)) {
        free(pixels);
        return -1;
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        free(pixels);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++)
        fwrite(pixels + i * 4, 1, 3, f);
    free(pixels);
    return fclose(f) ? -1 : 0;
}
int pc_vk_hw_pick(int x, int y, float* depth, unsigned char normal[4])
{
    if (x < 0 || y < 0 || x >= vk_hw.width || y >= vk_hw.height)
        return 0;
    uint32_t raw = 0;
    if (!vk_read_image(&vk_hw.live.normal, VK_IMAGE_ASPECT_COLOR_BIT, x, y, 1, 1, normal, 4)
        || !vk_read_image(&vk_hw.live.depth, VK_IMAGE_ASPECT_DEPTH_BIT, x, y, 1, 1, &raw, 4))
        return 0;
    if (vk.depth_format == VK_FORMAT_D32_SFLOAT)
        memcpy(depth, &raw, sizeof *depth);
    else
        *depth = (float)(raw & 0xffffff) / 16777215.0f;
    return 1;
}
/* VRAM readers of a stale rectangle (gpu_hw.c pc_hw_sync): the live target
 * (replay: the replay target, for a replayed batch that samples the frame's
 * own picture) scaled down to native pixels (nearest, as GL's blit) and read
 * back. */
static vk_image sync_image;
int pc_vk_hw_sync_read(int replay, int x0, int y0, int x1, int y1, unsigned char* pixels)
{
    if (x1 <= x0 || y1 <= y0)
        return 0;
    if (!sync_image.image
        && !vk_image_create(&sync_image, 1024, 512, VK_TARGET_FORMAT,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return 0;
    int s = vk_hw.scale;
    vk_end_rendering();
    VkImageBlit blit = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { { x0 * s, y0 * s, 0 }, { x1 * s, y1 * s, 1 } },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = { { x0, y0, 0 }, { x1, y1, 1 } } };
    vkCmdBlitImage(vk_cmd(), (replay ? &vk_hw.replay : &vk_hw.live)->color.image, VK_IMAGE_LAYOUT_GENERAL,
        sync_image.image, VK_IMAGE_LAYOUT_GENERAL,
        1, &blit, VK_FILTER_NEAREST);
    return vk_read_image(&sync_image, VK_IMAGE_ASPECT_COLOR_BIT, x0, y0, x1 - x0, y1 - y0, pixels, 4);
}

/* ---- MSAA (antialias.h): the multisampled twins ---- */

int pc_vk_hw_ms_max(void)
{
    for (int samples = 8; samples >= 2; samples /= 2)
        if (vk.msaa_counts & (VkSampleCountFlags)samples)
            return samples;
    return 0;
}
static void ms_destroy(void)
{
    vk_image_destroy_later(&vk_hw.ms.color);
    vk_image_destroy_later(&vk_hw.ms.normal);
    vk_image_destroy_later(&vk_hw.ms.albedo);
    vk_image_destroy_later(&vk_hw.ms.shade);
    vk_image_destroy_later(&vk_hw.ms.depth);
    vk_image_destroy_later(&vk_hw.ms_room);
    vk_image_destroy_later(&vk_hw.ms_before);
    vk_hw.ms_samples = vk_hw.ms_width = vk_hw.ms_height = 0;
}
/* The packet pipelines at `samples`, and load/resolve (made once per count). */
static int ms_make_pipelines(int samples)
{
    if (ms_pipeline_samples == samples)
        return 1;
    vkDeviceWaitIdle(vk.device);
    for (int c = 0; c < 2; c++) {
        if (ms_opaque[c])
            vkDestroyPipeline(vk.device, ms_opaque[c], NULL);
        for (int m = 0; m < 4; m++)
            if (ms_blend[c][m])
                vkDestroyPipeline(vk.device, ms_blend[c][m], NULL);
    }
    VkPipeline old[4] = { ms_load_pipeline, ms_depth_pipeline, ms_resolve_pipeline, ms_sync_pipeline };
    for (int i = 0; i < 4; i++)
        if (old[i])
            vkDestroyPipeline(vk.device, old[i], NULL);
    if (ms_cover)
        vkDestroyPipeline(vk.device, ms_cover, NULL);
    ms_cover = VK_NULL_HANDLE;
    VkPipeline text[3] = { ms_text_solid, ms_text_add, ms_text_sub };
    for (int i = 0; i < 3; i++)
        if (text[i])
            vkDestroyPipeline(vk.device, text[i], NULL);
    ms_text_solid = ms_text_add = ms_text_sub = VK_NULL_HANDLE;
    memset(ms_opaque, 0, sizeof ms_opaque);
    memset(ms_blend, 0, sizeof ms_blend);
    ms_load_pipeline = ms_depth_pipeline = ms_resolve_pipeline = ms_sync_pipeline = VK_NULL_HANDLE;
    ms_pipeline_samples = 0;
    if (!ms_set) {
        VkDescriptorType types[5];
        for (int i = 0; i < 5; i++)
            types[i] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ms_set = vk_push_layout(types, 5);
        VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
        VkPipelineLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &ms_set,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range };
        if (!vk_check(vkCreatePipelineLayout(vk.device, &info, NULL, &ms_layout), "vkCreatePipelineLayout"))
            return 0;
    }
    /* hw_ms.frag: the native surfaces' depth (occlusion) read from its twin. */
    VkShaderModule vs = vk_shader(vk_spv_hw_vert, vk_spv_hw_vert_size),
                   fs = vk_shader(vk_spv_hw_ms_frag, vk_spv_hw_ms_frag_size),
                   fullscreen = vk_shader(vk_spv_fullscreen_vert, vk_spv_fullscreen_vert_size),
                   load = vk_shader(vk_spv_ms_load_frag, vk_spv_ms_load_frag_size),
                   depth = vk_shader(vk_spv_ms_depth_frag, vk_spv_ms_depth_frag_size),
                   resolve = vk_shader(vk_spv_ms_resolve_frag, vk_spv_ms_resolve_frag_size),
                   sync = vk_shader(vk_spv_ms_sync_frag, vk_spv_ms_sync_frag_size);
    int ok = 1;
    for (int c = 0; c < 2; c++) {
        ms_opaque[c] = target_pipeline_ms(vs, fs, draw_layout, &binding, attributes, 8,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, -1, 0xffff, samples, c);
        ok = ok && ms_opaque[c];
        for (int m = 0; m < 4; m++) {
            ms_blend[c][m] = target_pipeline_ms(vs, fs, draw_layout, &binding, attributes, 8,
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, m, 0x000f, samples, c);
            ok = ok && ms_blend[c][m];
        }
    }
    ms_cover = target_pipeline_ms(vs, fs, draw_layout, &binding, attributes, 8, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 4,
        0x0007, samples, 1);
    ms_text_solid = target_pipeline_ms(vs, fs, draw_layout, &binding, attributes, 8,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, -1, 0xfff0, samples, 1);
    ms_text_add = target_pipeline_ms(vs, fs, draw_layout, &binding, attributes, 8, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        5, 0x0007, samples, 1);
    ms_text_sub = target_pipeline_ms(vs, fs, draw_layout, &binding, attributes, 8, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        6, 0x0007, samples, 1);
    ok = ok && ms_cover && ms_text_solid && ms_text_add && ms_text_sub;
    /* Load: every sample of the twins (the shader runs once per pixel). */
    ms_load_pipeline = target_pipeline_ms(fullscreen, load, ms_layout, NULL, NULL, 0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, -1, 0xffff, samples, 0);
    vk_pipeline_desc d = { .vertex = fullscreen,
        .fragment = depth,
        .layout = ms_layout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .color_count = 0,
        .depth_format = vk.depth_format,
        .blend = -1,
        .samples = samples };
    ms_depth_pipeline = vk_pipeline(&d);
    /* Sync: sample 0 of the surfaces' twin into the single-sampled surfaces. */
    d.fragment = sync;
    d.samples = 1;
    ms_sync_pipeline = vk_pipeline(&d);
    /* Resolve: into the single-sampled target. */
    ms_resolve_pipeline = target_pipeline_ms(fullscreen, resolve, ms_layout, NULL, NULL, 0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, -1, 0xffff, 1, 0);
    VkShaderModule modules[7] = { vs, fs, fullscreen, load, depth, resolve, sync };
    for (int i = 0; i < 7; i++)
        vkDestroyShaderModule(vk.device, modules[i], NULL);
    ok = ok && ms_load_pipeline && ms_depth_pipeline && ms_resolve_pipeline && ms_sync_pipeline;
    if (ok)
        ms_pipeline_samples = samples;
    return ok;
}
int pc_vk_hw_ms_make(int samples, int width, int height)
{
    if (samples < 2 || !(vk.msaa_counts & (VkSampleCountFlags)samples))
        return 0;
    if (vk_hw.ms_samples == samples && vk_hw.ms_width >= width && vk_hw.ms_height >= height)
        return 1;
    vk_end_rendering();
    ms_destroy();
    if (!ms_make_pipelines(samples))
        return 0;
    VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageUsageFlags depth = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    vk_target* t = &vk_hw.ms;
    if (!vk_image_create_ms(&t->color, width, height, VK_TARGET_FORMAT, color, samples)
        || !vk_image_create_ms(&t->normal, width, height, VK_TARGET_FORMAT, color, samples)
        || !vk_image_create_ms(&t->albedo, width, height, VK_TARGET_FORMAT, color, samples)
        || !vk_image_create_ms(&t->shade, width, height, VK_TARGET_FORMAT, color, samples)
        || !vk_image_create_ms(&t->depth, width, height, vk.depth_format, depth, samples)
        || !vk_image_create_ms(&vk_hw.ms_room, width, height, vk.depth_format, depth, samples)
        || !vk_image_create_ms(&vk_hw.ms_before, width, height, vk.depth_format, depth, samples)) {
        fprintf(stderr, "[vk] no %dx MSAA images (%d x %d)\n", samples, width, height);
        ms_destroy();
        return 0;
    }
    vk_hw.ms_samples = samples;
    vk_hw.ms_width = width;
    vk_hw.ms_height = height;
    make_attachments(&ms_attachments, &vk_hw.ms, &vk_hw.ms.depth);
    make_attachments(&ms_object_attachments, &vk_hw.ms, &vk_hw.ms_room);
    ms_attachments.width = ms_object_attachments.width = width;
    ms_attachments.height = ms_object_attachments.height = height;
    return 1;
}
/* A fullscreen pass over a rectangle (scaled pixels) of the open scope. */
static void ms_pass(VkPipeline pipeline, vk_descriptors* d, int x0, int y0, int x1, int y1, int samples)
{
    VkCommandBuffer cmd = vk_cmd();
    VkViewport viewport = { (float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0), 0, 1 };
    VkRect2D scissor = { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_ALWAYS);
    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_descriptors_push(d, VK_PIPELINE_BIND_POINT_GRAPHICS, ms_layout);
    int push[4] = { samples, 0, 0, 0 };
    vkCmdPushConstants(cmd, ms_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
static int ms_clamp(int* x0, int* y0, int* x1, int* y1)
{
    *x1 = *x1 > vk_hw.ms_width ? vk_hw.ms_width : *x1;
    *y1 = *y1 > vk_hw.ms_height ? vk_hw.ms_height : *y1;
    return vk_hw.ms_samples && clamp_rect(x0, y0, x1, y1);
}
void pc_vk_hw_ms_load(unsigned fbo, int x0, int y0, int x1, int y1, int room)
{
    if (!ms_clamp(&x0, &y0, &x1, &y1))
        return;
    vk_section("msaa load"); /* ROOD_PERF: a section of its own, then the drawing again */
    const vk_target* t = target_of(fbo);
    vk_begin_rendering(&ms_attachments);
    vk_descriptors d = { 0 };
    const vk_image* from[5] = { &t->color, &t->normal, &t->albedo, &t->shade, &t->depth };
    for (int i = 0; i < 5; i++)
        vk_descriptors_image(&d, i, from[i]->view, vk.nearest);
    ms_pass(ms_load_pipeline, &d, x0, y0, x1, y1, vk_hw.ms_samples);
    if (room) {
        vk_attachments a = { .depth = vk_hw.ms_room.view, .width = vk_hw.ms_width, .height = vk_hw.ms_height };
        vk_begin_rendering(&a);
        for (int i = 0; i < 5; i++)
            vk_descriptors_image(&d, i, vk_hw.room_depth.view, vk.nearest);
        ms_pass(ms_depth_pipeline, &d, x0, y0, x1, y1, vk_hw.ms_samples);
    }
    vk_section("draw");
}
void pc_vk_hw_ms_resolve(unsigned fbo, int x0, int y0, int x1, int y1)
{
    if (!ms_clamp(&x0, &y0, &x1, &y1))
        return;
    vk_section("msaa resolve");
    vk_begin_rendering(vk_hw_attachments(fbo == VK_TOKEN_REPLAY ? VK_TOKEN_REPLAY : VK_TOKEN_LIVE));
    vk_descriptors d = { 0 };
    const vk_image* from[5] = { &vk_hw.ms.color, &vk_hw.ms.normal, &vk_hw.ms.albedo, &vk_hw.ms.shade, &vk_hw.ms.depth };
    for (int i = 0; i < 5; i++)
        vk_descriptors_image(&d, i, from[i]->view, vk.nearest);
    ms_pass(ms_resolve_pipeline, &d, x0, y0, x1, y1, vk_hw.ms_samples);
    vk_end_rendering();
    vk_section("draw");
}
void pc_vk_hw_ms_sync_room(int x0, int y0, int x1, int y1)
{
    if (!ms_clamp(&x0, &y0, &x1, &y1))
        return;
    vk_section("msaa sync");
    vk_attachments a = { .depth = vk_hw.room_depth.view, .width = vk_hw.width, .height = vk_hw.height };
    vk_begin_rendering(&a);
    vk_descriptors d = { 0 };
    for (int i = 0; i < 5; i++)
        vk_descriptors_image(&d, i, vk_hw.ms_room.view, vk.nearest);
    ms_pass(ms_sync_pipeline, &d, x0, y0, x1, y1, 1);
    vk_section("draw");
}
