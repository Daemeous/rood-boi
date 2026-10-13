/* Point-light shadows on Vulkan devices without ray tracing (or with the
 * traced shadows off): shadows.c's cube maps of the captured room and this
 * frame's casters, distance to the light over its radius, as an atlas of
 * SLOTS rows of six 512 x 512 faces (GL cube-face orientations, so the
 * present pass picks the face and texel exactly as a GL cube lookup).
 * A slot is redrawn only when the room, the casters or the light changed. */
#include "vk_hw.h"
#include "vk_shaders.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLOTS 4
#define SIZE 512
#define SUN_SIZE 1024 /* the sun's tile, below the cube rows */
#define ATLAS_W (SIZE * 6)
#define ATLAS_H (SIZE * SLOTS + SUN_SIZE)

extern const float* pc_scene_positions(int* count);
extern int pc_scene_serial(void);
extern const float* pc_hw_casters(int* count, int* serial);

static int ready; /* 0 untried, 1 ready, -1 unavailable */
static vk_image atlas, depth;
static VkPipelineLayout layout;
static VkPipeline pipeline;
static vk_buffer room;
static int room_serial = -1, room_vertices;
static float cached[SLOTS][4];
static int cached_serial[SLOTS], cached_casters[SLOTS];

static void init(void)
{
    ready = -1;
    if (!vk_image_create(&atlas, ATLAS_W, ATLAS_H, VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        || !vk_image_create(&depth, ATLAS_W, ATLAS_H, vk.depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        return;
    VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80 };
    VkPipelineLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &range };
    if (!vk_check(vkCreatePipelineLayout(vk.device, &info, NULL, &layout), "vkCreatePipelineLayout"))
        return;
    VkShaderModule vs = vk_shader(vk_spv_shadow_vert, vk_spv_shadow_vert_size),
                   fs = vk_shader(vk_spv_shadow_frag, vk_spv_shadow_frag_size);
    static const VkVertexInputBindingDescription binding = { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX };
    static const VkVertexInputAttributeDescription attribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    static const VkFormat format = VK_FORMAT_R32_SFLOAT;
    vk_pipeline_desc d = { .vertex = vs,
        .fragment = fs,
        .layout = layout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .binding = &binding,
        .attributes = &attribute,
        .attribute_count = 1,
        .color_count = 1,
        .color_formats = &format,
        .depth_format = vk.depth_format,
        .write_masks = { 15 },
        .blend = -1 };
    pipeline = vs && fs ? vk_pipeline(&d) : VK_NULL_HANDLE;
    if (vs)
        vkDestroyShaderModule(vk.device, vs, NULL);
    if (fs)
        vkDestroyShaderModule(vk.device, fs, NULL);
    for (int i = 0; i < SLOTS; i++)
        cached_serial[i] = -1;
    if (pipeline) {
        ready = 1;
        fprintf(stderr, "[vk] shadow maps ready (lights without traced shadows)\n");
    }
}

/* Column-major 4x4: projection(90 deg, near 1, far radius) * lookAt(face),
 * as shadows.c face_matrix. */
static void face_matrix(int face, const float light[4], float out[16])
{
    static const float target[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const float up[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
    const float* f = target[face];
    const float* u = up[face];
    float s[3] = { f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0] };
    float v[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    float view[3][4];
    for (int k = 0; k < 3; k++)
        view[0][k] = s[k], view[1][k] = v[k], view[2][k] = -f[k];
    for (int r = 0; r < 3; r++)
        view[r][3] = -(view[r][0] * light[0] + view[r][1] * light[1] + view[r][2] * light[2]);
    float n = 1, far = light[3] * 1.05f;
    float p22 = -(far + n) / (far - n), p23 = -2 * far * n / (far - n);
    memset(out, 0, 16 * sizeof(float));
    for (int c = 0; c < 4; c++) {
        out[c * 4 + 0] = view[0][c];
        out[c * 4 + 1] = view[1][c];
        out[c * 4 + 2] = p22 * view[2][c] + (c == 3 ? p23 : 0);
        out[c * 4 + 3] = -view[2][c];
    }
}

static void render(int slot, const float light[4])
{
    int serial = pc_scene_serial(), casters, caster_serial, count;
    const float* caster_positions = pc_hw_casters(&casters, &caster_serial);
    if (cached_serial[slot] == serial && cached_casters[slot] == caster_serial
        && !memcmp(cached[slot], light, sizeof cached[slot]))
        return;
    const float* positions = pc_scene_positions(&count);
    if (!positions || !count)
        return;
    if (room_serial != serial) {
        vk_buffer_destroy_later(&room);
        if (!vk_buffer_create(&room, (VkDeviceSize)count * 36, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 1))
            return;
        memcpy(room.mapped, positions, (size_t)count * 36);
        room_serial = serial;
        room_vertices = count * 3;
    }
    vk_slice moving = { 0 };
    if (casters) {
        moving = vk_arena((VkDeviceSize)casters * 36, 16);
        if (moving.data)
            memcpy(moving.data, caster_positions, (size_t)casters * 36);
    }
    vk_attachments a = { .colors = { atlas.view }, .color_count = 1, .depth = depth.view, .width = ATLAS_W,
        .height = ATLAS_H };
    vk_begin_rendering(&a);
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS);
    for (int face = 0; face < 6; face++) {
        VkRect2D rect = { { face * SIZE, slot * SIZE }, { SIZE, SIZE } };
        VkViewport viewport = { (float)(face * SIZE), (float)(slot * SIZE), SIZE, SIZE, 0, 1 };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &rect);
        VkClearAttachment clears[2] = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, { .color = { .float32 = { 2, 2, 2, 2 } } } },
            { VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } } };
        VkClearRect clear_rect = { rect, 0, 1 };
        vkCmdClearAttachments(cmd, 2, clears, 1, &clear_rect);
        struct {
            float face[16], light[4];
        } push;
        face_matrix(face, light, push.face);
        memcpy(push.light, light, sizeof push.light);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &room.buffer, &zero);
        vkCmdDraw(cmd, (uint32_t)room_vertices, 1, 0, 0);
        if (moving.data) {
            vkCmdBindVertexBuffers(cmd, 0, 1, &moving.buffer, &moving.offset);
            vkCmdDraw(cmd, (uint32_t)casters * 3, 1, 0, 0);
        }
    }
    vk_end_rendering();
    memcpy(cached[slot], light, sizeof cached[slot]);
    cached_serial[slot] = serial;
    cached_casters[slot] = caster_serial;
}

/* As shadows.c pc_shadows_update: slots for the lights that want shadows
 * (world xyz, radius), their maps brought up to date; slot_out[i] = slot
 * or -1. Returns how many. */
int pc_vk_shadows_update(int count, const float world[][4], const int* wants, int* slot_out)
{
    if (!ready)
        init();
    int used = 0;
    for (int i = 0; i < count; i++) {
        slot_out[i] = -1;
        if (ready > 0 && wants[i] && used < SLOTS) {
            render(used, world[i]);
            slot_out[i] = used++;
        }
    }
    return used;
}
/* The sun's shadow map (shadows.c the same on OpenGL): one orthographic
 * SUN_SIZE tile below the cube rows, drawn with the cube pipeline (the
 * "light" a far point along the sun). Fills its lookup (matrix, far point
 * and radius, tile x y size); 0 when unavailable. */
void pc_shadow_sun_matrix(const float dir[3], float out[16], float light[4]); /* shadows.c */
static float sun_cached[4];
static int sun_serial = -1, sun_casters = -1;
int pc_vk_shadows_sun(const float dir[3], float matrix[16], float light[4], float tile[4])
{
    if (!ready)
        init();
    if (ready <= 0)
        return 0;
    pc_shadow_sun_matrix(dir, matrix, light);
    tile[0] = 0, tile[1] = (float)(SIZE * SLOTS), tile[2] = SUN_SIZE, tile[3] = 0;
    int serial = pc_scene_serial(), casters, caster_serial, count;
    const float* caster_positions = pc_hw_casters(&casters, &caster_serial);
    float key[4] = { dir[0], dir[1], dir[2], light[3] };
    if (sun_serial == serial && sun_casters == caster_serial && !memcmp(sun_cached, key, sizeof key))
        return 1;
    const float* positions = pc_scene_positions(&count);
    if (!positions || !count)
        return 0;
    if (room_serial != serial) {
        vk_buffer_destroy_later(&room);
        if (!vk_buffer_create(&room, (VkDeviceSize)count * 36, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 1))
            return 0;
        memcpy(room.mapped, positions, (size_t)count * 36);
        room_serial = serial;
        room_vertices = count * 3;
    }
    vk_slice moving = { 0 };
    if (casters) {
        moving = vk_arena((VkDeviceSize)casters * 36, 16);
        if (moving.data)
            memcpy(moving.data, caster_positions, (size_t)casters * 36);
    }
    vk_attachments a = { .colors = { atlas.view }, .color_count = 1, .depth = depth.view, .width = ATLAS_W,
        .height = ATLAS_H };
    vk_begin_rendering(&a);
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS);
    VkRect2D rect = { { 0, SIZE * SLOTS }, { SUN_SIZE, SUN_SIZE } };
    VkViewport viewport = { 0, (float)(SIZE * SLOTS), SUN_SIZE, SUN_SIZE, 0, 1 };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &rect);
    VkClearAttachment clears[2] = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, { .color = { .float32 = { 2, 2, 2, 2 } } } },
        { VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } } };
    VkClearRect clear_rect = { rect, 0, 1 };
    vkCmdClearAttachments(cmd, 2, clears, 1, &clear_rect);
    struct {
        float face[16], light[4];
    } push;
    memcpy(push.face, matrix, sizeof push.face);
    memcpy(push.light, light, sizeof push.light);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &room.buffer, &zero);
    vkCmdDraw(cmd, (uint32_t)room_vertices, 1, 0, 0);
    if (moving.data) {
        vkCmdBindVertexBuffers(cmd, 0, 1, &moving.buffer, &moving.offset);
        vkCmdDraw(cmd, (uint32_t)casters * 3, 1, 0, 0);
    }
    vk_end_rendering();
    memcpy(sun_cached, key, sizeof key);
    sun_serial = serial;
    sun_casters = caster_serial;
    return 1;
}
/* How many lights get shadow maps (the most important: light_list.h). */
int pc_vk_shadows_slots(void) { return SLOTS; }
/* The atlas for the present pass (a 1 x 1 dummy before the first use). */
VkImageView pc_vk_shadows_view(void) { return ready > 0 ? atlas.view : vk_hw.dummy_color.view; }
