/* Native lists drawn with Vulkan (included at the end of render.c when built
 * with ROOD_VULKAN, so it shares the lists' internals; the GL drawing above
 * stays the reference). The control flow follows render_draw_list and
 * render_draw_translucent step by step; the GL state becomes pipelines,
 * dynamic depth state and a uniform block per draw. */
#include "vk_hooks.h"
#include "vk_hw.h"
#include "vk_shaders.h"

/* ---- mesh memory: vertex buffers suballocated from large blocks ---- */

#define VK_MESH_BLOCK (16u << 20)
typedef struct {
    vk_buffer buffer;
    struct {
        VkDeviceSize offset, size;
    } * free_list;
    int free_count, free_capacity;
} vk_mesh_block;
typedef struct {
    int block; /* -1: free slot */
    VkDeviceSize offset, size;
} vk_mesh_entry;
static vk_mesh_block* vk_blocks;
static int vk_block_count;
static vk_mesh_entry* vk_meshes;
static int vk_mesh_count;

static void vk_block_free(vk_mesh_block* b, VkDeviceSize offset, VkDeviceSize size)
{
    /* Insert sorted, merging with neighbours. */
    int i = 0;
    while (i < b->free_count && b->free_list[i].offset < offset)
        ++i;
    if (i > 0 && b->free_list[i - 1].offset + b->free_list[i - 1].size == offset) {
        b->free_list[i - 1].size += size;
        if (i < b->free_count && offset + size == b->free_list[i].offset) {
            b->free_list[i - 1].size += b->free_list[i].size;
            memmove(&b->free_list[i], &b->free_list[i + 1], (size_t)(b->free_count - i - 1) * sizeof *b->free_list);
            --b->free_count;
        }
        return;
    }
    if (i < b->free_count && offset + size == b->free_list[i].offset) {
        b->free_list[i].offset = offset;
        b->free_list[i].size += size;
        return;
    }
    if (b->free_count == b->free_capacity) {
        int capacity = b->free_capacity ? b->free_capacity * 2 : 16;
        void* grown = realloc(b->free_list, (size_t)capacity * sizeof *b->free_list);
        if (!grown)
            return; /* leaks the range */
        b->free_list = grown;
        b->free_capacity = capacity;
    }
    memmove(&b->free_list[i + 1], &b->free_list[i], (size_t)(b->free_count - i) * sizeof *b->free_list);
    b->free_list[i].offset = offset;
    b->free_list[i].size = size;
    ++b->free_count;
}
static int vk_block_alloc(VkDeviceSize size, int* block, VkDeviceSize* offset)
{
    size = (size + 255) & ~(VkDeviceSize)255;
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < vk_block_count; k++) {
            vk_mesh_block* b = &vk_blocks[k];
            for (int i = 0; i < b->free_count; i++)
                if (b->free_list[i].size >= size) {
                    *block = k;
                    *offset = b->free_list[i].offset;
                    b->free_list[i].offset += size;
                    b->free_list[i].size -= size;
                    if (!b->free_list[i].size) {
                        memmove(&b->free_list[i], &b->free_list[i + 1],
                            (size_t)(b->free_count - i - 1) * sizeof *b->free_list);
                        --b->free_count;
                    }
                    return 1;
                }
        }
        if (pass)
            break;
        vk_mesh_block* grown = realloc(vk_blocks, (size_t)(vk_block_count + 1) * sizeof *grown);
        if (!grown)
            return 0;
        vk_blocks = grown;
        vk_mesh_block* b = &vk_blocks[vk_block_count];
        memset(b, 0, sizeof *b);
        VkDeviceSize bytes = size > VK_MESH_BLOCK ? size : VK_MESH_BLOCK;
        if (!vk_buffer_create(&b->buffer, bytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                    | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                1))
            return 0;
        vk_block_free(b, 0, bytes);
        ++vk_block_count;
    }
    return 0;
}
uint32_t pc_vk_mesh_create(const void* vertices, size_t bytes)
{
    int slot = 0;
    while (slot < vk_mesh_count && vk_meshes[slot].block >= 0)
        ++slot;
    if (slot == vk_mesh_count) {
        vk_mesh_entry* grown = realloc(vk_meshes, (size_t)(vk_mesh_count + 64) * sizeof *grown);
        if (!grown)
            return 0;
        vk_meshes = grown;
        for (int i = vk_mesh_count; i < vk_mesh_count + 64; i++)
            vk_meshes[i].block = -1;
        vk_mesh_count += 64;
    }
    vk_mesh_entry* e = &vk_meshes[slot];
    if (!vk_block_alloc(bytes, &e->block, &e->offset)) {
        e->block = -1;
        return 0;
    }
    e->size = bytes;
    memcpy((char*)vk_blocks[e->block].buffer.mapped + e->offset, vertices, bytes);
    return (uint32_t)slot + 1;
}
typedef struct {
    int block;
    VkDeviceSize offset, size;
} vk_pending_free;
static void vk_free_range(void* context)
{
    vk_pending_free* p = context;
    vk_block_free(&vk_blocks[p->block], p->offset, (p->size + 255) & ~(VkDeviceSize)255);
    free(p);
}
void pc_vk_mesh_destroy(uint32_t mesh)
{
    if (!mesh || (int)mesh > vk_mesh_count || vk_meshes[mesh - 1].block < 0)
        return;
    vk_mesh_entry* e = &vk_meshes[mesh - 1];
    /* The range is reused once the frames that may draw it have finished. */
    vk_pending_free* p = malloc(sizeof *p);
    if (p) {
        p->block = e->block, p->offset = e->offset, p->size = e->size;
        vk_on_frame_done(vk_free_range, p);
    }
    e->block = -1;
}
static int vk_mesh_bind(uint32_t mesh)
{
    if (!mesh || (int)mesh > vk_mesh_count || vk_meshes[mesh - 1].block < 0)
        return 0;
    const vk_mesh_entry* e = &vk_meshes[mesh - 1];
    vkCmdBindVertexBuffers(vk_cmd(), 0, 1, &vk_blocks[e->block].buffer.buffer, &e->offset);
    return 1;
}

/* ---- pipelines ---- */

typedef struct {
    float normal_matrix[3][4];
    float projection[4], tint[4], offset[2], clip_z[2];
    int rules[4], override_color[4], override_texture[4];
    int split[2], instance_blend[2], forced[2];
    int objects, zsf3, translucent, perspective, filtering;
    uint32_t window;
    int mask_set;
    float surface;
    int depth_pass, painter, semi_pass, surface_test;
    int uv_count; /* animated materials' table entries (binding 5) */
    int pad;
    float cut_focus[4], cut_scale[4]; /* the editor's cutaway (cutaway_params) */
    float instance_cut[4]; /* render_instance.cut at this draw's t, ghost edges */
} native_ubo;
_Static_assert(offsetof(native_ubo, projection) == 48 && offsetof(native_ubo, rules) == 96
        && offsetof(native_ubo, split) == 144 && offsetof(native_ubo, objects) == 168
        && offsetof(native_ubo, window) == 188 && offsetof(native_ubo, surface_test) == 212
        && offsetof(native_ubo, uv_count) == 216 && offsetof(native_ubo, cut_focus) == 224 && offsetof(native_ubo, cut_scale) == 240
        && offsetof(native_ubo, instance_cut) == 256,
    "native_ubo must match native_common.glsl's std140 layout");
static VkDescriptorSetLayout vk_native_set;
static VkPipelineLayout vk_native_layout;
/* The list pipelines: [0] single-sampled, [1] multisampled at
 * vk_native_ms_samples (MSAA, antialias.h; made when first needed). */
static VkPipeline vk_native_opaque[2], vk_native_depth_only[2], vk_native_blend[2][4];
static int vk_native_ready = -1, vk_native_ms_samples;
static int vk_native_pipelines(int set, int samples)
{
    VkShaderModule vs = vk_shader(vk_spv_native_vert, vk_spv_native_vert_size),
                   gs = vk_shader(vk_spv_native_geom, vk_spv_native_geom_size),
                   fs = set ? vk_shader(vk_spv_native_ms_frag, vk_spv_native_ms_frag_size) /* surfaces multisampled */
                            : vk_shader(vk_spv_native_frag, vk_spv_native_frag_size);
    static const VkVertexInputBindingDescription binding = { 0, sizeof(mesh_vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    static const VkVertexInputAttributeDescription attributes[7] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh_vertex, position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh_vertex, normal) },
        { 2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(mesh_vertex, r) },
        { 3, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(mesh_vertex, u) },
        { 4, 0, VK_FORMAT_R16G16_UINT, offsetof(mesh_vertex, tpage) },
        { 5, 0, VK_FORMAT_R8_UINT, offsetof(mesh_vertex, bias) },
        { 6, 0, VK_FORMAT_R8_UINT, offsetof(mesh_vertex, uv_effect) },
    };
    static const VkFormat formats[4] = { VK_TARGET_FORMAT, VK_TARGET_FORMAT, VK_TARGET_FORMAT, VK_TARGET_FORMAT };
    vk_pipeline_desc d = { .vertex = vs,
        .geometry = gs,
        .fragment = fs,
        .layout = vk_native_layout,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
        .binding = &binding,
        .attributes = attributes,
        .attribute_count = 7,
        .color_count = 4,
        .color_formats = formats,
        .depth_format = vk.depth_format,
        .write_masks = { 15, 15, 15, 15 },
        .blend = -1,
        .samples = samples };
    vk_native_opaque[set] = vk_pipeline(&d);
    memset(d.write_masks, 0, sizeof d.write_masks);
    vk_native_depth_only[set] = vk_pipeline(&d);
    d.write_masks[0] = 15;
    for (int m = 0; m < 4; m++) {
        d.blend = m;
        vk_native_blend[set][m] = vk_pipeline(&d);
    }
    vkDestroyShaderModule(vk.device, vs, NULL);
    vkDestroyShaderModule(vk.device, gs, NULL);
    vkDestroyShaderModule(vk.device, fs, NULL);
    return vk_native_opaque[set] && vk_native_depth_only[set] && vk_native_blend[set][0] && vk_native_blend[set][1]
        && vk_native_blend[set][2] && vk_native_blend[set][3];
}
static int vk_native_init(void)
{
    if (vk_native_ready >= 0)
        return vk_native_ready;
    vk_native_ready = 0;
    VkDescriptorType types[6] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
    vk_native_set = vk_push_layout(types, 6);
    VkPipelineLayoutCreateInfo layout = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk_native_set };
    if (!vk_check(vkCreatePipelineLayout(vk.device, &layout, NULL, &vk_native_layout), "vkCreatePipelineLayout"))
        return 0;
    vk_native_ready = vk_native_pipelines(0, 1);
    if (!vk_native_ready)
        fprintf(stderr, "[vk] native list pipelines unavailable\n");
    return vk_native_ready;
}
/* The pipeline set for attachments with `samples` per pixel: 0 single, 1
 * multisampled, -1 unavailable. */
static int vk_native_set_for(int samples)
{
    if (samples <= 1)
        return 0;
    if (vk_native_ms_samples != samples) {
        vkDeviceWaitIdle(vk.device);
        VkPipeline* all[6] = { &vk_native_opaque[1], &vk_native_depth_only[1], &vk_native_blend[1][0],
            &vk_native_blend[1][1], &vk_native_blend[1][2], &vk_native_blend[1][3] };
        for (int i = 0; i < 6; i++)
            if (*all[i])
                vkDestroyPipeline(vk.device, *all[i], NULL), *all[i] = VK_NULL_HANDLE;
        vk_native_ms_samples = vk_native_pipelines(1, samples) ? samples : 0;
    }
    return vk_native_ms_samples == samples ? 1 : -1;
}

/* ---- drawing ---- */

typedef struct {
    native_ubo ubo; /* the list's and the pass's state; instances fill the rest */
    VkImageView surface_depth, surface_before;
    VkDeviceSize ubo_alignment, ssbo_alignment;
    vk_slice uv_effects; /* the list's animated materials (uv_effect_table) */
} vk_draw_state;

/* Model -> view for each palette entry (camera * world) into a storage
 * slice; the room's normal matrix from entry 0 (as upload_palette). */
static int vk_palette(vk_draw_state* s, const render_camera* cam, const render_transform* palette, int bones, int room,
    vk_slice* out)
{
    *out = vk_arena((VkDeviceSize)bones * 4 * 16, s->ssbo_alignment);
    if (!out->data)
        return 0;
    float* dst = out->data;
    double r0[3][3];
    for (int b = 0; b < bones; b++) {
        const render_transform* w = &palette[b];
        double r[3][3], t[3];
        for (int a = 0; a < 3; a++) {
            for (int c = 0; c < 3; c++) {
                r[a][c] = 0;
                for (int k = 0; k < 3; k++)
                    r[a][c] += (double)cam->rotation[a][k] * w->rotation[k][c];
            }
            t[a] = cam->translation[a];
            for (int k = 0; k < 3; k++)
                t[a] += (double)cam->rotation[a][k] * w->translation[k];
        }
        for (int c = 0; c < 3; c++) {
            for (int a = 0; a < 3; a++)
                dst[b * 16 + c * 4 + a] = (float)r[a][c]; /* column c */
            dst[b * 16 + c * 4 + 3] = 0;
        }
        for (int a = 0; a < 3; a++)
            dst[b * 16 + 12 + a] = (float)t[a];
        dst[b * 16 + 15] = 0;
        if (!b)
            memcpy(r0, r, sizeof r0);
    }
    if (room) {
        /* Normals: the inverse transpose (the cofactor matrix). */
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) {
                int a1 = (a + 1) % 3, a2 = (a + 2) % 3, b1 = (b + 1) % 3, b2 = (b + 2) % 3;
                s->ubo.normal_matrix[b][a] = (float)(r0[a1][b1] * r0[a2][b2] - r0[a1][b2] * r0[a2][b1]);
            }
    }
    return 1;
}
/* Uniforms, palette and vertex buffer for one instance, pushed (as
 * use_instance); returns its mesh. */
static mesh_slot* vk_use_instance(vk_draw_state* s, const draw_list* list, const list_instance* in,
    const render_target* target, const render_camera* cam, int pass, int* interpolated)
{
    mesh_slot* mesh = lookup(in->mesh);
    if (!mesh)
        return NULL;
    render_transform palette[RENDER_MAX_BONES];
    if (instance_palette(list, in, target->t, palette) && pass == 0)
        ++*interpolated;
    vk_slice bones;
    if (!vk_palette(s, cam, palette, in->bones, !list->rules.objects, &bones))
        return NULL;
    native_ubo* u = &s->ubo;
    u->rules[0] = list->rules.near_bucket, u->rules[1] = list->rules.far_bucket;
    u->rules[2] = list->rules.lod_bucket, u->rules[3] = in->bias;
    u->split[0] = list->rules.subdivision, u->split[1] = in->subdivision_level;
    u->override_color[0] = in->color[0], u->override_color[1] = in->color[1], u->override_color[2] = in->color[2];
    u->override_color[3] = in->override & RENDER_OVERRIDE_COLOR ? 1 : 0;
    u->override_texture[0] = in->tpage, u->override_texture[1] = in->clut;
    u->override_texture[2] = in->override & RENDER_OVERRIDE_TEXTURE ? 1 : 0, u->override_texture[3] = 0;
    u->instance_blend[0] = in->blend != 0;
    u->instance_blend[1] = in->blend ? ((in->blend - 1) & 3) << 5 : 0;
    u->instance_cut[0] = instance_cut(in, target->t);
    u->instance_cut[1] = in->cut_ghost ? 1.0f : 0.0f;
    if (!vk_mesh_bind(mesh->vbo))
        return NULL;
    vk_slice ubo = vk_arena(sizeof *u, s->ubo_alignment);
    if (!ubo.data)
        return NULL;
    memcpy(ubo.data, u, sizeof *u);
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ubo.buffer, ubo.offset, sizeof *u);
    vk_descriptors_buffer(&d, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bones.buffer, bones.offset,
        (VkDeviceSize)in->bones * 64);
    vk_descriptors_image(&d, 2, vk_hw.vram.view, vk.nearest);
    vk_descriptors_image(&d, 3, s->surface_depth, vk.nearest);
    vk_descriptors_image(&d, 4, s->surface_before, vk.nearest);
    vk_descriptors_buffer(&d, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, s->uv_effects.buffer, s->uv_effects.offset,
        (VkDeviceSize)(s->ubo.uv_count > 0 ? s->ubo.uv_count : 1) * sizeof(uv_effect_gpu));
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_native_layout);
    return mesh;
}
/* The uniforms shared by list and translucent draws (as begin_draw). */
static const render_camera* vk_begin_draw(vk_draw_state* s, const draw_list* list, const render_target* target)
{
    const render_camera* cam = target->camera ? target->camera : &list->camera;
    memset(s, 0, sizeof *s);
    s->ubo_alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    s->ssbo_alignment = vk.properties.limits.minStorageBufferOffsetAlignment;
    native_ubo* u = &s->ubo;
    u->normal_matrix[0][0] = u->normal_matrix[1][1] = u->normal_matrix[2][2] = 1;
    u->perspective = target->perspective;
    u->filtering = target->filtering;
    u->window = target->window;
    u->mask_set = target->mask_set;
    float hx = cam->h * (list->camera.widescreen ? 0.75f : 1.0f);
    u->projection[0] = hx, u->projection[1] = cam->h;
    u->projection[2] = cam->ofx + (float)target->ox, u->projection[3] = cam->ofy + (float)target->oy;
    u->offset[0] = cam->ofx, u->offset[1] = cam->ofy;
    for (int i = 0; i < 4; i++)
        u->tint[i] = (float)list->tint[i];
    u->clip_z[0] = 1.0f, u->clip_z[1] = 65536.0f;
    u->objects = list->rules.objects != 0;
    u->zsf3 = list->rules.zsf3;
    u->surface = list->rules.objects ? 0.75f : 1.0f;
    cutaway_params(list, cam, u->cut_focus, u->cut_scale);
    /* Animated materials: the table for this draw's blend t. */
    uv_effect_gpu table[RENDER_MAX_UV_EFFECTS + 1];
    int entries = uv_effect_table(list, target->t, table);
    s->uv_effects = vk_arena((VkDeviceSize)entries * sizeof *table, s->ssbo_alignment);
    if (s->uv_effects.data)
        memcpy(s->uv_effects.data, table, (size_t)entries * sizeof *table);
    u->uv_count = s->uv_effects.data ? entries : 0;
    s->surface_depth = vk_hw.dummy_depth.view;
    s->surface_before = vk_hw.dummy_depth.view;
    return cam;
}
static void vk_depth_state(int test, VkCompareOp op, int write)
{
    VkCommandBuffer cmd = vk_cmd();
    vkCmdSetDepthTestEnable(cmd, test ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, op);
    vkCmdSetDepthWriteEnable(cmd, write ? VK_TRUE : VK_FALSE);
}
static void vk_draw_instances(vk_draw_state* s, const draw_list* list, const render_target* target,
    const render_camera* cam, int pass, int* interpolated)
{
    VkCommandBuffer cmd = vk_cmd();
    int painter = list->order_used > 0 && list->rules.objects && target->surface_before;
    s->ubo.painter = painter;
    if (!painter) {
        for (int i = 0; i < list->count; i++) {
            if (list->instances[i].blend)
                continue; /* wholly semi-transparent: drawn at translucent markers */
            mesh_slot* mesh = vk_use_instance(s, list, &list->instances[i], target, cam, pass, interpolated);
            if (mesh)
                vkCmdDraw(cmd, (uint32_t)mesh->polygons * 4, 1, 0, 0);
        }
    } else {
        /* PS1 order: each polygon paints over the ones before it; only the
         * surfaces from before the list hide it. */
        vk_slice indices = vk_arena((VkDeviceSize)list->order_used * 4 * sizeof(uint32_t), 16);
        if (!indices.data)
            return;
        uint32_t* index = indices.data;
        for (int k = 0; k < list->order_used; k++)
            for (int c = 0; c < 4; c++)
                index[k * 4 + c] = (list->order[k] & 0xffff) * 4 + (unsigned)c;
        vkCmdBindIndexBuffer(cmd, indices.buffer, indices.offset, VK_INDEX_TYPE_UINT32);
        vk_depth_state(1, VK_COMPARE_OP_ALWAYS, 1);
        int current = -1;
        mesh_slot* mesh = NULL;
        for (int k = 0; k < list->order_used;) {
            int instance = (int)(list->order[k] >> 16), run = k;
            while (run < list->order_used && (int)(list->order[run] >> 16) == instance)
                ++run;
            if (instance < list->count && !list->instances[instance].blend) {
                if (instance != current) {
                    mesh = vk_use_instance(s, list, &list->instances[instance], target, cam, pass, interpolated);
                    current = instance;
                }
                int valid = mesh != NULL;
                for (int q = k; valid && q < run; q++)
                    valid = (int)(list->order[q] & 0xffff) < mesh->polygons;
                if (valid)
                    vkCmdDrawIndexed(cmd, (uint32_t)(run - k) * 4, 1, (uint32_t)k * 4, 0, 0);
            }
            k = run;
        }
        vk_depth_state(1, pass == 0 ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_ALWAYS, 1);
    }
    if (target->count_stats && pass == 0)
        for (int i = 0; i < list->count; i++) {
            mesh_slot* mesh = lookup(list->instances[i].mesh);
            if (!mesh)
                continue;
            if (list->rules.objects) {
                counting.object_instances++;
                counting.object_triangles += mesh->triangles;
            } else if (list->rules.layered) {
                counting.layered_instances++;
            } else {
                counting.instances++;
                counting.native_triangles += mesh->triangles;
            }
        }
}
static int vk_draw_list(uint32_t id, const render_target* target)
{
    draw_list* list = find_list(id);
    if (!list || !vk_native_init())
        return -1;
    int layered = list->rules.objects || list->rules.layered;
    if (layered && (!target->object_fbo || !target->surface_depth))
        return -1;
    vk_draw_state s;
    const render_camera* cam = vk_begin_draw(&s, list, target);
    uv_trace_record(list, target->t);
    const vk_attachments* a = vk_hw_attachments(layered ? target->object_fbo : target->fbo);
    if (!a)
        return -1;
    vk_begin_rendering(a);
    if (!vk_hw_viewport_scissor(target->clip))
        return list->count;
    VkCommandBuffer cmd = vk_cmd();
    if (!layered) {
        /* A room list resets depth over its drawing area first. */
        int s_ = target->scale, x0 = target->clip[0] * s_, y0 = target->clip[1] * s_;
        int x1 = (target->clip[0] + target->clip[2]) * s_, y1 = (target->clip[1] + target->clip[3]) * s_;
        x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
        x1 = x1 > vk_hw.width ? vk_hw.width : x1, y1 = y1 > vk_hw.height ? vk_hw.height : y1;
        if (x1 > x0 && y1 > y0) {
            VkClearAttachment clear = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } };
            VkClearRect rect = { { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } }, 0, 1 };
            vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
        }
    }
    if (list->rules.objects && target->surface_before) {
        const vk_image* before = vk_hw_depth(target->surface_before);
        if (before)
            s.surface_before = before->view;
    }
    int set = vk_native_set_for(vk_hw_token_samples(layered ? target->object_fbo : target->fbo));
    if (set < 0)
        return -1;
    if (set) /* multisampled surfaces: unused bindings take a multisampled image too */
        s.surface_depth = s.surface_before = vk_hw.ms_room.view;
    if (set && list->rules.objects && target->surface_before) {
        const vk_image* before = vk_hw_depth(target->surface_before);
        if (before)
            s.surface_before = before->view;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_native_opaque[set]);
    vk_depth_state(1, VK_COMPARE_OP_LESS, 1);
    int interpolated = 0;
    vk_draw_instances(&s, list, target, cam, 0, &interpolated);
    if (layered) {
        /* Pass 2: the visible fragments' depth into the frame's depth. */
        if (target->surface_sync) /* MSAA: pass 1's depth into the surfaces pass 2 reads */
            target->surface_sync();
        const vk_attachments* frame = vk_hw_attachments(target->fbo);
        const vk_image* surface = vk_hw_depth(target->surface_depth);
        if (frame && surface) {
            vk_begin_rendering(frame);
            vk_hw_viewport_scissor(target->clip);
            cmd = vk_cmd();
            s.surface_depth = surface->view;
            s.ubo.depth_pass = 1;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_native_depth_only[set]);
            vk_depth_state(1, VK_COMPARE_OP_ALWAYS, 1);
            vk_draw_instances(&s, list, target, cam, 1, &interpolated);
            s.ubo.depth_pass = 0;
        }
    }
    if (target->count_stats) {
        if (list->rules.objects)
            counting.object_lists++;
        else if (list->rules.layered)
            counting.layered_lists++;
        else
            counting.lists++;
    } else
        counting.interpolated_instances += interpolated;
    return list->count;
}
static int vk_draw_translucent(uint32_t id, int first, int count, const render_target* target)
{
    draw_list* list = find_list(id);
    if (!list || !target->surface_depth || !vk_native_init())
        return -1;
    if (first < 0)
        first = 0;
    if (first + count > list->translucent_used)
        count = list->translucent_used - first;
    if (count <= 0)
        return 0;
    const vk_attachments* a = vk_hw_attachments(target->fbo);
    const vk_image* surface = vk_hw_depth(target->surface_depth);
    if (!a || !surface)
        return -1;
    vk_slice indices = vk_arena((VkDeviceSize)count * 4 * sizeof(uint32_t), 16);
    if (!indices.data)
        return -1;
    const uint32_t* entries = list->translucent + first;
    const uint8_t* flags = list->translucent_flags + first;
    uint32_t* index = indices.data;
    for (int k = 0; k < count; k++)
        for (int c = 0; c < 4; c++)
            index[k * 4 + c] = (entries[k] & 0xffff) * 4 + (unsigned)c;
    vk_draw_state s;
    const render_camera* cam = vk_begin_draw(&s, list, target);
    int set = vk_native_set_for(vk_hw_token_samples(target->fbo));
    if (set < 0)
        return -1;
    if (set)
        s.surface_before = vk_hw.ms_room.view; /* unused, but of the multisampled kind */
    vk_begin_rendering(a);
    if (!vk_hw_viewport_scissor(target->clip))
        return 0;
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindIndexBuffer(cmd, indices.buffer, indices.offset, VK_INDEX_TYPE_UINT32);
    s.surface_depth = surface->view;
    int effects = list->rules.effects;
    s.ubo.translucent = effects ? 2 : 1;
    s.ubo.surface_test = list->rules.objects ? 1 : effects ? 3 : 2;
    if (effects)
        s.ubo.surface = 0.0f; /* unlit: effects light nothing up themselves */
    int interpolated = 0, drawn = 0, current = -1;
    mesh_slot* mesh = NULL;
    for (int k = 0; k < count;) {
        int instance = (int)(entries[k] >> 16);
        if (instance >= list->count) {
            ++k;
            continue;
        }
        const list_instance* in = &list->instances[instance];
        if (instance != current) {
            mesh = lookup(in->mesh);
            current = instance;
        }
        int mode = -1, run = k;
        for (; run < count && (int)(entries[run] >> 16) == instance && flags[run] == flags[k]; run++) {
            int polygon = (int)(entries[run] & 0xffff);
            if (!mesh || polygon >= mesh->polygons)
                break;
            int m = (int)(effective_tpage(in, &mesh->cpu[polygon]) >> 5) & 3;
            if (mode >= 0 && m != mode)
                break;
            mode = m;
        }
        if (run == k) { /* a stale entry */
            ++k;
            continue;
        }
        s.ubo.forced[0] = (flags[k] & RENDER_TRANSLUCENT_DECIDED) != 0;
        s.ubo.forced[1] = (flags[k] & RENDER_TRANSLUCENT_HALVE) != 0;
        /* Opaque texels (no STP bit) as opaque surfaces... */
        s.ubo.semi_pass = 1;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_native_opaque[set]);
        vk_depth_state(1, VK_COMPARE_OP_ALWAYS, 1);
        if (vk_use_instance(&s, list, in, target, cam, 0, &interpolated))
            vkCmdDrawIndexed(cmd, (uint32_t)(run - k) * 4, 1, (uint32_t)k * 4, 0, 0);
        /* ...then the semi-transparent ones blended, keeping the depth and
         * the surface behind them. */
        s.ubo.semi_pass = 2;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_native_blend[set][mode & 3]);
        vk_depth_state(1, VK_COMPARE_OP_ALWAYS, 0);
        if (vk_use_instance(&s, list, in, target, cam, 1, &interpolated))
            vkCmdDrawIndexed(cmd, (uint32_t)(run - k) * 4, 1, (uint32_t)k * 4, 0, 0);
        drawn += run - k;
        k = run;
    }
    if (target->count_stats) {
        if (effects == RENDER_EFFECTS_SHADOWS) {
            counting.shadow_markers++;
            counting.shadow_polygons += drawn;
        } else if (effects) {
            counting.effect_markers++;
            counting.effect_polygons += drawn;
        } else {
            counting.translucent_markers++;
            counting.translucent_polygons += drawn;
        }
    }
    return drawn;
}

/* ---- moving geometry for the path tracer (vk_rt.inc) ---- */

_Static_assert(sizeof(mesh_vertex) == VK_MESH_VERTEX_BYTES, "vk_hw.h VK_MESH_VERTEX_BYTES");
int pc_vk_movers(uint32_t id, float t, vk_mover* out, int max)
{
    draw_list* list = find_list(id);
    if (!list || !list->rules.objects)
        return 0;
    int n = 0;
    render_transform palette[RENDER_MAX_BONES];
    for (int i = 0; i < list->count && n < max; i++) {
        const list_instance* in = &list->instances[i];
        mesh_slot* mesh = lookup(in->mesh);
        if (!mesh || in->blend || !mesh->vbo || (int)mesh->vbo > vk_mesh_count || vk_meshes[mesh->vbo - 1].block < 0)
            continue;
        const vk_mesh_entry* e = &vk_meshes[mesh->vbo - 1];
        vk_mover* m = &out[n++];
        m->mesh = in->mesh;
        m->buffer = vk_blocks[e->block].buffer.buffer;
        m->offset = e->offset;
        m->polygons = mesh->polygons;
        m->bones = in->bones;
        instance_palette(list, in, t, palette);
        for (int b = 0; b < in->bones; b++)
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++)
                    m->palette[b][r * 4 + c] = palette[b].rotation[r][c];
                m->palette[b][r * 4 + 3] = palette[b].translation[r];
            }
        m->override_color = (in->override & RENDER_OVERRIDE_COLOR) != 0;
        m->override_texture = (in->override & RENDER_OVERRIDE_TEXTURE) != 0;
        m->color = in->color[0] | (uint32_t)in->color[1] << 8 | (uint32_t)in->color[2] << 16;
        m->tpage = in->tpage;
        m->clut = in->clut;
    }
    return n;
}
