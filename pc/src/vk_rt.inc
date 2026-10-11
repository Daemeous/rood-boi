/* Path tracing with Vulkan ray queries (included at the end of ptgi.c when
 * built with ROOD_VULKAN, so it shares the scene gathering, emitters, glow
 * overrides, lights and settings above; the GL compute path stays the
 * reference). The CPU BVH becomes acceleration structures: one BLAS for the
 * captured room (built when the room changes, fast trace) and one for this
 * frame's characters and props (the casters, rebuilt every frame, fast
 * build) under a two-instance TLAS (mask 1 room, 2 moving). The passes are
 * ptgi.c's: surfaces from the G-buffer, trace (shadow visibility, indirect
 * light, baked-light visibility, haze, lighting signature), temporal
 * accumulation with the signature as strict history, a-trous filtering. */
#include "vk_hooks.h"
#include "vk_hw.h"
#include "vk_shaders.h"
#include "vk_nrd.h"

/* Vulkan-only images after ptgi.c's: the previous world position of each
 * GI pixel (motion), the temporal moments (two, ping-pong) and the variance
 * (temporal output, two for the filter iterations); the first-bounce hit
 * distance and NRD's inputs and output (gi_denoiser nrd). */
enum {
    VRT_MOTION = IMAGES, VRT_MOMENTS_A, VRT_MOMENTS_B, VRT_VARIANCE, VRT_VARIANCE_A, VRT_VARIANCE_B, VRT_HITDIST,
    VRT_NRD_NORMAL, VRT_NRD_VIEWZ, VRT_NRD_MOTION, VRT_NRD_RADIANCE, VRT_NRD_OUTPUT, VRT_IMAGES
};
/* NRD RELAX diffuse (vk_nrd.cpp) for the indirect light; with
 * pc_nrd_glow the glows' light goes through it too (else our filter). */
int pc_nrd_glow = 0;
int pc_nrd_max_frames = 30; /* RELAX's maximum accumulated frames */
/* Volumetric haze: a froxel volume of HAZE_CELL x HAZE_CELL display pixels
 * per cell and HAZE_SLICES logarithmic depth slices (rt_haze.comp,
 * rt_haze_integrate.comp), stored as slices side by side in 2D images. */
#define HAZE_CELL 4
#define HAZE_SLICES 64
#define HAZE_NEAR 16.0f
float pc_haze_history = 0.1f; /* weight of each new froxel sample (temporal blend) */
/* Frames of shadow history where a moving blocker touches the visibility. */
float pc_shadow_motion_history = 6.0f;
/* Custom index bit of mover instances (their triangles are in the mover buffer). */
#define VRT_MOVER_BIT (1u << 23)
#define VRT_MAX_MOVERS 256
#define VRT_REBUILD_FRAMES 32 /* a refit mover BLAS is rebuilt this often */
static struct {
    int state; /* 0 untried, 1 ready, -1 unavailable */
    VkDescriptorSetLayout trace_set, surface_set, temporal_set, atrous_set, skin_set, nrd_in_set, nrd_out_set;
    VkPipelineLayout trace_layout, surface_layout, temporal_layout, atrous_layout, skin_layout, nrd_in_layout,
        nrd_out_layout;
    VkPipeline trace, surface, temporal, atrous, skin, nrd_in, nrd_out;
    /* Room: opaque and cut-out BLASes over its shading triangles. */
    VkAccelerationStructureKHR room_blas, cutout_blas, tlas;
    vk_buffer room_buffer, cutout_buffer, tlas_buffer, scratch, room_tris, indices;
    VkDeviceAddress room_address, cutout_address;
    int room_serial, room_count, room_opaque, room_cutout, room_checks;
    unsigned room_frame;
    int index_capacity;
    /* Movers: two slots of skinned triangles, previous positions. */
    vk_buffer mover_tris[2], motion;
    int mover_capacity[2], motion_capacity, mover_slot, mover_triangles;
    struct {
        VkDeviceAddress address;
        int base;
    } movers[VRT_MAX_MOVERS];
    int mover_count;
    unsigned updates; /* scene updates (one per traced present) */
    /* The game view's GI images and history (ptgi.c's gi_context). */
    vk_image images[VRT_IMAGES];
    int size[2], history_current, history_valid;
    float previous_to_image[16];
    unsigned frame;
    int result, result_extra; /* image indices of the last result */
    /* Haze volume: in-scattering (two, ping-pong history) and integrated. */
    VkDescriptorSetLayout haze_set, integrate_set;
    VkPipelineLayout haze_layout, integrate_layout;
    VkPipeline haze, integrate;
    vk_image haze_scatter[2], haze_integrated;
    int haze_grid[3], haze_current, haze_valid, haze_on;
    float haze_range[2], haze_previous_rotation[3][4], haze_previous_projection[4], haze_previous_range[2];
    /* The camera of this update (for the haze pass). */
    float cam_r[3][3], cam_tr[3], cam_inverse[3][3], cam_projection[4];
    int display[2];
    /* Diagnostics (RPC gi_dump): this update's eye, whether it shaded a
     * replay (interpolated) frame; a burst of dumps of the next updates. */
    float eye[3];
    int replay, dump_left, dump_index;
    char dump_path[400];
    /* NRD: its camera (orthonormal view, projection) of the previous
     * update, and what resets its history. */
    struct {
        float view[16], projection[16], eye[3], forward[3];
        int valid, serial, size[2], denoised; /* denoised: NRD ran last update */
        unsigned lights, frame;
    } nrd;
} vrt = { .room_serial = -1 };

typedef struct {
    float eye[4];
    float light_pos[16][4], light_col[16][4], light_dir[16][4], light_right[16][4], light_up[16][4];
    int light_shadow[16];
    float emitter_pos[32][4], emitter_col[32][4];
    float room_lo[4], room_hi[4];
    int i0[4], i1[4], i2[4];
    float f0[4], f1[4];
    float f2[4]; /* lighting quality: traced AO radius (0 off), strength */
    int i3[4]; /* glows.c pc_glow_casts: emitters that may cast shadows */
} vrt_trace_ubo;
typedef struct {
    int rect[4], scale[4];
    float gi_scale[4], projection[4], screen_offset[4];
    float inverse_rotation[3][4], normal_to_world[3][4], camera_translation[4];
    float eye[4];
} vrt_surface_ubo;
typedef struct {
    float previous_to_image[16];
    float params[4];
} vrt_temporal_ubo;

static VkPipelineLayout vrt_layout(VkDescriptorSetLayout set, uint32_t push)
{
    VkPushConstantRange range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, push };
    VkPipelineLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set,
        .pushConstantRangeCount = push ? 1 : 0,
        .pPushConstantRanges = &range };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vk_check(vkCreatePipelineLayout(vk.device, &info, NULL, &layout), "vkCreatePipelineLayout");
    return layout;
}
static VkPipeline vrt_compute(const uint32_t* code, size_t bytes, VkPipelineLayout layout)
{
    VkShaderModule module = vk_shader(code, bytes);
    VkPipeline pipeline = module ? vk_compute_pipeline(module, layout) : VK_NULL_HANDLE;
    if (module)
        vkDestroyShaderModule(vk.device, module, NULL);
    return pipeline;
}
static int vrt_init(void)
{
    if (vrt.state)
        return vrt.state > 0;
    vrt.state = -1;
    if (!vk.ray_query) {
        fprintf(stderr, "[gi] Vulkan device without ray queries: no path tracing\n");
        return 0;
    }
    const VkDescriptorType U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           T = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, B = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           A = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    VkDescriptorType trace[11] = { U, A, B, T, S, S, S, S, S, B, S };
    VkDescriptorType surface[11] = { U, A, B, T, T, T, S, S, S, B, B };
    VkDescriptorType temporal[17] = { U, S, S, S, S, S, S, S, S, S, S, S, S, S, S, S, S };
    VkDescriptorType atrous[8] = { S, S, S, S, S, S, S, S };
    vrt.trace_set = vk_push_layout(trace, 11);
    vrt.surface_set = vk_push_layout(surface, 11);
    vrt.temporal_set = vk_push_layout(temporal, 17);
    vrt.atrous_set = vk_push_layout(atrous, 8);
    vrt.trace_layout = vrt_layout(vrt.trace_set, 0);
    vrt.surface_layout = vrt_layout(vrt.surface_set, 0);
    vrt.temporal_layout = vrt_layout(vrt.temporal_set, 0);
    vrt.atrous_layout = vrt_layout(vrt.atrous_set, 32);
    vrt.trace = vrt_compute(vk_spv_rt_trace_comp, vk_spv_rt_trace_comp_size, vrt.trace_layout);
    vrt.surface = vrt_compute(vk_spv_rt_surface_comp, vk_spv_rt_surface_comp_size, vrt.surface_layout);
    vrt.temporal = vrt_compute(vk_spv_rt_temporal_comp, vk_spv_rt_temporal_comp_size, vrt.temporal_layout);
    vrt.atrous = vrt_compute(vk_spv_rt_atrous_comp, vk_spv_rt_atrous_comp_size, vrt.atrous_layout);
    VkDescriptorType skin[5] = { B, B, B, B, B };
    vrt.skin_set = vk_push_layout(skin, 5);
    vrt.skin_layout = vrt_layout(vrt.skin_set, 32);
    vrt.skin = vrt_compute(vk_spv_rt_skin_comp, vk_spv_rt_skin_comp_size, vrt.skin_layout);
    VkDescriptorType haze[10] = { U, A, B, T, U, S, T, S, S, B };
    VkDescriptorType integrate[3] = { U, S, S };
    vrt.haze_set = vk_push_layout(haze, 10);
    vrt.integrate_set = vk_push_layout(integrate, 3);
    vrt.haze_layout = vrt_layout(vrt.haze_set, 0);
    vrt.integrate_layout = vrt_layout(vrt.integrate_set, 0);
    vrt.haze = vrt_compute(vk_spv_rt_haze_comp, vk_spv_rt_haze_comp_size, vrt.haze_layout);
    vrt.integrate = vrt_compute(vk_spv_rt_haze_integrate_comp, vk_spv_rt_haze_integrate_comp_size, vrt.integrate_layout);
    VkDescriptorType nrd_in[10] = { S, S, S, S, S, S, S, S, S, S }, nrd_out[4] = { S, S, S, S };
    vrt.nrd_in_set = vk_push_layout(nrd_in, 10);
    vrt.nrd_out_set = vk_push_layout(nrd_out, 4);
    vrt.nrd_in_layout = vrt_layout(vrt.nrd_in_set, 48);
    vrt.nrd_out_layout = vrt_layout(vrt.nrd_out_set, 16);
    vrt.nrd_in = vrt_compute(vk_spv_rt_nrd_in_comp, vk_spv_rt_nrd_in_comp_size, vrt.nrd_in_layout);
    vrt.nrd_out = vrt_compute(vk_spv_rt_nrd_out_comp, vk_spv_rt_nrd_out_comp_size, vrt.nrd_out_layout);
    if (!vrt.trace || !vrt.surface || !vrt.temporal || !vrt.atrous || !vrt.skin || !vrt.haze || !vrt.integrate
        || !vrt.nrd_in || !vrt.nrd_out)
        return 0;
    vrt.state = 1;
    fprintf(stderr, "[gi] path-traced global illumination ready (Vulkan ray queries)\n");
    /* Integrated GPUs (shared memory, few ray units): half the GI
     * resolution by default (a quarter of the pixels to trace and filter). */
    if (vk.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && pc_gi_resolution == 3.0f) {
        pc_gi_resolution = 1.5f;
        fprintf(stderr, "[gi] integrated GPU: GI resolution 1.5 per display pixel (Graphics > GI > Resolution)\n");
    }
    return 1;
}

/* ---- acceleration structures ----
 *
 * The room: its captured triangles (one native room mesh, static) in a
 * device buffer, split once per room into fully opaque triangles (no
 * texel can let a ray through: untextured, or every texel it covers is
 * solid) and cut-outs (clear or translucent texels: fences, grates,
 * glass). Two BLASes: the opaque one is a FORCE_OPAQUE instance, so rays
 * never run shader code for it; only cut-outs run the per-texel test.
 *
 * Movers (characters, weapons, props of this frame's object lists): each
 * instance is skinned on the GPU from its bone palette (rt_skin.comp) into
 * the frame's mover buffer and has its own BLAS, keyed by mesh and
 * occurrence, built once and refit while the topology stays (periodically
 * rebuilt). The mover buffer alternates between two slots so the skinning
 * can also hand each triangle its position in the previous traced frame
 * (motion vectors for reprojection).
 *
 * TLAS: room opaque (mask 1, custom index 0), room cut-outs (mask 1,
 * custom index = first cut-out triangle), one instance per mover (mask 2,
 * custom index = VRT_MOVER_BIT | its first triangle in the mover buffer). */


static void vrt_destroy_structure(void* context)
{
    VkAccelerationStructureKHR* as = context;
    vkDestroyAccelerationStructureKHR(vk.device, *as, NULL);
    free(as);
}
static void vrt_destroy_later(VkAccelerationStructureKHR* as)
{
    if (!*as)
        return;
    VkAccelerationStructureKHR* copy = malloc(sizeof *copy);
    if (copy) {
        *copy = *as;
        vk_on_frame_done(vrt_destroy_structure, copy);
    }
    *as = VK_NULL_HANDLE;
}
/* Room for `size` bytes of structure in `buffer`, (re)creating the
 * structure when the buffer had to grow. */
static int vrt_structure(VkAccelerationStructureKHR* as, vk_buffer* buffer, VkDeviceSize size,
    VkAccelerationStructureTypeKHR type)
{
    if (*as && buffer->size >= size)
        return 1;
    vrt_destroy_later(as);
    vk_buffer_destroy_later(buffer);
    VkDeviceSize bytes = size + size / 2;
    if (!vk_buffer_create(buffer, bytes,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 0))
        return 0;
    VkAccelerationStructureCreateInfoKHR info = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = buffer->buffer,
        .size = bytes,
        .type = type };
    return vk_check(vkCreateAccelerationStructureKHR(vk.device, &info, NULL, as), "vkCreateAccelerationStructureKHR");
}
/* Scratch memory for one batch of builds (`size` bytes in all, each part
 * aligned by the caller). */
static VkDeviceAddress vrt_scratch(VkDeviceSize size)
{
    VkDeviceSize need = size + vk.scratch_alignment;
    if (vrt.scratch.size < need) {
        vk_buffer_destroy_later(&vrt.scratch);
        if (!vk_buffer_create(&vrt.scratch, need + need / 2,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 0))
            return 0;
    }
    return (vrt.scratch.address + vk.scratch_alignment - 1) & ~(vk.scratch_alignment - 1);
}
static VkDeviceSize vrt_align_scratch(VkDeviceSize size)
{
    return (size + vk.scratch_alignment - 1) & ~(vk.scratch_alignment - 1);
}
static VkDeviceAddress vrt_address(VkAccelerationStructureKHR as)
{
    VkAccelerationStructureDeviceAddressInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .accelerationStructure = as };
    return vkGetAccelerationStructureDeviceAddressKHR(vk.device, &info);
}
/* Shading triangles are 64 bytes (a, b, c, info): their positions are the
 * first three vec4s, so a BLAS reads them in place with a stride of 16 and
 * indices 4t, 4t + 1, 4t + 2 (a shared index buffer). */
static int vrt_indices(int triangles)
{
    if (vrt.index_capacity >= triangles)
        return 1;
    int capacity = triangles + triangles / 2 + 1024;
    vk_buffer_destroy_later(&vrt.indices);
    if (!vk_buffer_create(&vrt.indices, (VkDeviceSize)capacity * 12,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            1)) {
        vrt.index_capacity = 0;
        return 0;
    }
    uint32_t* index = vrt.indices.mapped;
    for (int t = 0; t < capacity; t++)
        for (int k = 0; k < 3; k++)
            index[t * 3 + k] = (uint32_t)(t * 4 + k);
    vrt.index_capacity = capacity;
    return 1;
}
static VkAccelerationStructureGeometryKHR vrt_geometry(VkDeviceAddress triangles, int count, int opaque)
{
    VkAccelerationStructureGeometryKHR geometry = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0,
        .geometry.triangles = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData.deviceAddress = triangles,
            .vertexStride = 16,
            .maxVertex = (uint32_t)count * 4 - 1,
            .indexType = VK_INDEX_TYPE_UINT32,
            .indexData.deviceAddress = vrt.indices.address } };
    return geometry;
}

/* A room triangle no ray can pass: untextured, or no clear (0) or, when
 * semi-transparent, translucent (STP) texel among those its UVs cover (as
 * rt_common.glsl surface_texel samples them). */
extern const uint16_t* pc_hw_native_vram(void);
static int vrt_vram_texel(const uint16_t* vram, int u, int v, unsigned tpage, unsigned clut)
{
#define VRT_FETCH(x, y) vram[((y) & 511) * 1024 + ((x) & 1023)]
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
    int cx = (int)(clut & 63) * 16, cy = (int)((clut >> 6) & 511);
    unsigned depth = (tpage >> 7) & 3;
    if (depth == 0)
        return VRT_FETCH(cx + ((VRT_FETCH(tx + (u >> 2), ty + v) >> ((u & 3) * 4)) & 15), cy);
    if (depth == 1)
        return VRT_FETCH(cx + ((VRT_FETCH(tx + (u >> 1), ty + v) >> ((u & 1) * 8)) & 255), cy);
    return VRT_FETCH(tx + u, ty + v);
#undef VRT_FETCH
}
static int vrt_solid(const gpu_triangle* t, const uint16_t* vram)
{
    if (!(t->info[3] & 1u))
        return 1;
    if (!vram)
        return 0;
    float u[3] = { (float)(t->info[0] & 255), (float)(t->info[0] >> 16 & 255), (float)(t->info[1] & 255) };
    float v[3] = { (float)(t->info[0] >> 8 & 255), (float)(t->info[0] >> 24), (float)(t->info[1] >> 8 & 255) };
    unsigned tpage = t->info[1] >> 16, clut = t->info[2] & 0xffff;
    int semi = (t->info[3] & 4u) != 0;
    float area = (u[1] - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (v[1] - v[0]);
    int u0 = (int)fminf(u[0], fminf(u[1], u[2])), u1 = (int)fmaxf(u[0], fmaxf(u[1], u[2]));
    int v0 = (int)fminf(v[0], fminf(v[1], v[2])), v1 = (int)fmaxf(v[0], fmaxf(v[1], v[2]));
    for (int y = v0; y <= v1 && y < 256; y++)
        for (int x = u0; x <= u1 && x < 256; x++) {
            /* A texel counts when a point of its square (centre, corners) is
             * in the triangle (with a margin): samples floor() into it. */
            int covered = fabsf(area) < 1e-3f; /* degenerate in UV: its bounding box */
            for (int s = 0; s < 5 && !covered; s++) {
                float px = (float)x + (s == 0 ? 0.5f : (s & 1) ? 0.02f : 0.98f);
                float py = (float)y + (s == 0 ? 0.5f : (s & 2) ? 0.02f : 0.98f);
                float w0 = (u[1] - px) * (v[2] - py) - (u[2] - px) * (v[1] - py);
                float w1 = (u[2] - px) * (v[0] - py) - (u[0] - px) * (v[2] - py);
                float w2 = (u[0] - px) * (v[1] - py) - (u[1] - px) * (v[0] - py);
                float e = 0.02f * fabsf(area);
                covered = area > 0 ? (w0 >= -e && w1 >= -e && w2 >= -e) : (w0 <= e && w1 <= e && w2 <= e);
            }
            if (!covered)
                continue;
            int c = vrt_vram_texel(vram, x, y, tpage, clut);
            if (c == 0 || (semi && (c & 0x8000)))
                return 0;
        }
    return 1;
}

/* Build `count` BLASes in one call. */
typedef struct {
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR info;
    VkAccelerationStructureBuildRangeInfoKHR range;
} vrt_build;
static void vrt_build_all(vrt_build* builds, int count, VkDeviceSize scratch_bytes)
{
    if (!count)
        return;
    VkDeviceAddress scratch = vrt_scratch(scratch_bytes);
    if (!scratch)
        return;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[VRT_MAX_MOVERS + 2];
    VkAccelerationStructureBuildGeometryInfoKHR infos[VRT_MAX_MOVERS + 2];
    for (int i = 0; i < count; i++) {
        infos[i] = builds[i].info;
        infos[i].pGeometries = &builds[i].geometry;
        infos[i].scratchData.deviceAddress += scratch;
        ranges[i] = &builds[i].range;
    }
    vk_end_rendering();
    vkCmdBuildAccelerationStructuresKHR(vk_cmd(), (uint32_t)count, infos, ranges);
    vk_barrier();
}
/* Prepare a build (or an update, when `update`) of `as` over the geometry;
 * returns its scratch size, 0 on failure. */
static VkDeviceSize vrt_prepare(vrt_build* b, VkAccelerationStructureKHR* as, vk_buffer* buffer,
    VkAccelerationStructureGeometryKHR geometry, int primitives, VkBuildAccelerationStructureFlagsKHR flags,
    int update, VkDeviceSize scratch_offset)
{
    memset(b, 0, sizeof *b);
    b->geometry = geometry;
    b->info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    b->info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    b->info.flags = flags;
    b->info.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    b->info.geometryCount = 1;
    b->info.pGeometries = &b->geometry;
    uint32_t count = (uint32_t)primitives;
    VkAccelerationStructureBuildSizesInfoKHR sizes = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &b->info, &count,
        &sizes);
    if (!update && !vrt_structure(as, buffer, sizes.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR))
        return 0;
    b->info.srcAccelerationStructure = update ? *as : VK_NULL_HANDLE;
    b->info.dstAccelerationStructure = *as;
    b->info.scratchData.deviceAddress = scratch_offset; /* relative: vrt_build_all adds the base */
    b->range.primitiveCount = count;
    VkDeviceSize scratch = update ? sizes.updateScratchSize : sizes.buildScratchSize;
    return vrt_align_scratch(scratch ? scratch : 1);
}

/* Animated materials (room texture effects): each scene triangle's record in
 * room_tris (-1: none), and the scene's UV serial the records follow. The
 * UVs of the triangles the effects animate are rewritten in place, in
 * command order (vkCmdUpdateBuffer: frames in flight keep theirs), so hits
 * read the texels drawn this frame. */
static int* vrt_room_map;
static int vrt_room_map_count, vrt_uv_serial = -1;
static void vrt_room_uvs(const scene_triangle* room, int room_count)
{
    int serial = pc_scene_uv_serial();
    if (serial == vrt_uv_serial || !vrt_room_map || room_count != vrt_room_map_count || !vrt.room_tris.buffer)
        return;
    vrt_uv_serial = serial;
    int count;
    const int* animated = pc_scene_animated(&count);
    int open = 0;
    for (int n = 0; n < count; n++) {
        int i = animated[n], g = i < room_count ? vrt_room_map[i] : -1;
        if (g < 0 || g >= room_triangles)
            continue;
        const scene_triangle* t = &room[i];
        uint32_t info[2] = { (uint32_t)(t->vertex[0].u | t->vertex[0].v << 8 | t->vertex[1].u << 16 | t->vertex[1].v << 24),
            (uint32_t)(t->vertex[2].u | t->vertex[2].v << 8) | (uint32_t)t->tpage << 16 };
        if (!memcmp(triangles[g].info, info, sizeof info))
            continue;
        memcpy(triangles[g].info, info, sizeof info);
        ++pc_rt_uv_updates;
        if (!open) {
            vk_end_rendering();
            vk_barrier();
            open = 1;
        }
        vkCmdUpdateBuffer(vk_cmd(), vrt.room_tris.buffer, (VkDeviceSize)g * sizeof(gpu_triangle) + offsetof(gpu_triangle, info),
            sizeof info, info);
    }
    if (open)
        vk_barrier();
}
/* The room: shading triangles (opaque ones first), its buffer and BLASes;
 * rebuilt when the capture changes, and once more a second later (textures
 * may still be arriving when a room is first drawn). */
static int vrt_room(void)
{
    int room_count;
    const scene_triangle* room = pc_scene_triangle_data(&room_count);
    int serial = pc_scene_serial();
    int recheck = vrt.room_serial == serial && vrt.room_checks == 1 && vrt.frame - vrt.room_frame >= 60;
    if (serial == vrt.room_serial && room_count == vrt.room_count && !recheck) {
        vrt_room_uvs(room, room_count);
        return vrt.room_opaque + vrt.room_cutout > 0;
    }
    triangle_count = 0;
    for (int i = 0; i < room_count; i++) {
        const scene_triangle* t = &room[i];
        float p[3][3];
        uint8_t rgb[3][3], uv[3][2];
        for (int j = 0; j < 3; j++) {
            memcpy(p[j], t->vertex[j].position, sizeof p[j]);
            memcpy(rgb[j], t->vertex[j].rgb, 3);
            uv[j][0] = t->vertex[j].u, uv[j][1] = t->vertex[j].v;
        }
        add_triangle(p, rgb, uv, t->tpage, t->clut, t->code, 1u | (t->code & 1 ? 2u : 0u) | (t->code & 2 ? 4u : 0u));
    }
    room_triangles = triangle_count;
    /* Opaque first (stable within each part). */
    const uint16_t* vram = pc_hw_native_vram();
    gpu_triangle* sorted = malloc((size_t)(room_triangles ? room_triangles : 1) * sizeof *sorted);
    if (!sorted)
        return 0;
    int opaque = 0, cutout = 0;
    int* where = malloc((size_t)(room_triangles ? room_triangles : 1) * sizeof *where);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < room_triangles; i++)
            if (vrt_solid(&triangles[i], vram) == !pass) {
                if (where)
                    where[i] = opaque + cutout;
                sorted[opaque + cutout] = triangles[i];
                *(pass ? &cutout : &opaque) += 1;
            }
    memcpy(triangles, sorted, (size_t)room_triangles * sizeof *sorted); /* CPU copy in the same order */
    if (recheck && opaque == vrt.room_opaque) {
        free(sorted);
        free(where);
        vrt.room_checks = 2;
        return 1;
    }
    free(vrt_room_map);
    vrt_room_map = where; /* (room triangle i is scene triangle i) */
    vrt_room_map_count = where ? room_triangles : 0;
    vrt_uv_serial = pc_scene_uv_serial(); /* the records have this serial's UVs */
    VkDeviceSize bytes = (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof(gpu_triangle);
    vk_buffer_destroy_later(&vrt.room_tris);
    if (!vk_buffer_create(&vrt.room_tris, bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            1)) {
        free(sorted);
        return 0;
    }
    memcpy(vrt.room_tris.mapped, sorted, (size_t)room_triangles * sizeof *sorted);
    free(sorted);
    if (!vrt_indices(room_triangles))
        return 0;
    vrt_build builds[2];
    int count = 0;
    VkDeviceSize scratch = 0;
    VkBuildAccelerationStructureFlagsKHR trace = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (opaque) {
        VkDeviceSize s = vrt_prepare(&builds[count], &vrt.room_blas, &vrt.room_buffer,
            vrt_geometry(vrt.room_tris.address, opaque, 1), opaque, trace, 0, scratch);
        if (!s)
            return 0;
        scratch += s, ++count;
    }
    if (cutout) {
        VkDeviceSize s = vrt_prepare(&builds[count], &vrt.cutout_blas, &vrt.cutout_buffer,
            vrt_geometry(vrt.room_tris.address + (VkDeviceSize)opaque * sizeof(gpu_triangle), cutout, 0), cutout, trace,
            0, scratch);
        if (!s)
            return 0;
        scratch += s, ++count;
    }
    vrt_build_all(builds, count, scratch);
    vrt.room_address = opaque ? vrt_address(vrt.room_blas) : 0;
    vrt.cutout_address = cutout ? vrt_address(vrt.cutout_blas) : 0;
    vrt.room_opaque = opaque;
    vrt.room_cutout = cutout;
    if (!recheck) {
        fprintf(stderr, "[gi] room acceleration structures: %d opaque + %d cut-out triangles\n", opaque, cutout);
        vrt.room_frame = vrt.frame;
        vrt.room_checks = 1;
    } else
        vrt.room_checks = 2;
    vrt.room_serial = serial;
    vrt.room_count = room_count;
    return opaque + cutout > 0;
}

/* This present's movers: skinned, their BLASes built or refit. Fills
 * vrt.movers / vrt.mover_count (TLAS instances). */
typedef struct {
    uint32_t mesh;
    int occurrence, polygons;
    VkAccelerationStructureKHR blas;
    vk_buffer buffer;
    unsigned used; /* vrt.updates when last skinned */
    int base; /* first triangle in that update's mover slot */
    int slot;
    unsigned built; /* vrt.updates when last fully built */
} vrt_mover_entry;
static vrt_mover_entry vrt_entries[VRT_MAX_MOVERS];
static int vrt_entry_count;
extern int pc_hw_present_lists(uint32_t* lists, int max, float* t);
static vrt_mover_entry* vrt_entry(uint32_t mesh, int occurrence, int polygons)
{
    vrt_mover_entry* free_entry = NULL;
    for (int i = 0; i < vrt_entry_count; i++) {
        vrt_mover_entry* e = &vrt_entries[i];
        if (e->mesh == mesh && e->occurrence == occurrence && e->polygons == polygons)
            return e;
        if (!free_entry && (!e->mesh || vrt.updates - e->used > 120))
            free_entry = e;
    }
    if (!free_entry) {
        if (vrt_entry_count == VRT_MAX_MOVERS)
            return NULL;
        free_entry = &vrt_entries[vrt_entry_count++];
        memset(free_entry, 0, sizeof *free_entry);
    }
    vrt_destroy_later(&free_entry->blas);
    vk_buffer_destroy_later(&free_entry->buffer);
    memset(free_entry, 0, sizeof *free_entry);
    free_entry->mesh = mesh;
    free_entry->occurrence = occurrence;
    free_entry->polygons = polygons;
    return free_entry;
}
static int vrt_mover_buffers(int slot, int triangles)
{
    if (vrt.mover_capacity[slot] >= triangles && vrt.motion_capacity >= triangles)
        return 1;
    int capacity = triangles + triangles / 2 + 4096;
    if (vrt.mover_capacity[slot] < triangles) {
        vk_buffer_destroy_later(&vrt.mover_tris[slot]);
        if (!vk_buffer_create(&vrt.mover_tris[slot], (VkDeviceSize)capacity * sizeof(gpu_triangle),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                    | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                0)) {
            vrt.mover_capacity[slot] = 0;
            return 0;
        }
        vrt.mover_capacity[slot] = capacity;
    }
    if (vrt.motion_capacity < triangles) {
        vk_buffer_destroy_later(&vrt.motion);
        if (!vk_buffer_create(&vrt.motion, (VkDeviceSize)capacity * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0)) {
            vrt.motion_capacity = 0;
            return 0;
        }
        vrt.motion_capacity = capacity;
    }
    return 1;
}
typedef struct {
    uint32_t polygons, bones, has_previous, override_color, color, override_texture, tpage, clut;
} vrt_skin_push;
static void vrt_movers(void)
{
    vrt.mover_count = 0;
    vrt.mover_triangles = 0;
    uint32_t lists[64];
    float t;
    int list_count = pc_hw_present_lists(lists, 64, &t);
    static vk_mover movers[VRT_MAX_MOVERS];
    int count = 0;
    for (int i = 0; i < list_count && count < VRT_MAX_MOVERS; i++)
        count += pc_vk_movers(lists[i], t, movers + count, VRT_MAX_MOVERS - count);
    if (!count)
        return;
    int slot = (int)(vrt.updates & 1), other = slot ^ 1;
    int total = 0;
    for (int i = 0; i < count; i++)
        total += movers[i].polygons * 2;
    if (!vrt_mover_buffers(slot, total) || !vrt_indices(total))
        return;
    /* Skin each instance into the slot (and its previous pose into motion). */
    vk_end_rendering();
    vk_barrier();
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vrt.skin);
    vrt_mover_entry* entries[VRT_MAX_MOVERS];
    int bases[VRT_MAX_MOVERS];
    VkDeviceSize ssbo_alignment = vk.properties.limits.minStorageBufferOffsetAlignment;
    int base = 0;
    for (int i = 0; i < count; i++) {
        const vk_mover* m = &movers[i];
        int occurrence = 0;
        for (int k = 0; k < i; k++)
            occurrence += movers[k].mesh == m->mesh;
        vrt_mover_entry* e = vrt_entry(m->mesh, occurrence, m->polygons);
        entries[i] = e;
        bases[i] = base;
        if (!e)
            continue;
        int has_previous = e->used && e->used == vrt.updates - 1 && e->slot == other;
        vk_slice palette = vk_arena((VkDeviceSize)m->bones * 48, ssbo_alignment);
        if (!palette.data) {
            entries[i] = NULL;
            continue;
        }
        memcpy(palette.data, m->palette, (size_t)m->bones * 48);
        vk_descriptors d = { 0 };
        vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m->buffer, m->offset,
            (VkDeviceSize)m->polygons * 4 * VK_MESH_VERTEX_BYTES);
        vk_descriptors_buffer(&d, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, palette.buffer, palette.offset,
            (VkDeviceSize)m->bones * 48);
        VkDeviceSize bytes = (VkDeviceSize)m->polygons * 2 * sizeof(gpu_triangle);
        vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.mover_tris[slot].buffer,
            (VkDeviceSize)base * sizeof(gpu_triangle), bytes);
        if (has_previous)
            vk_descriptors_buffer(&d, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.mover_tris[other].buffer,
                (VkDeviceSize)e->base * sizeof(gpu_triangle), bytes);
        else
            vk_descriptors_buffer(&d, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.mover_tris[slot].buffer,
                (VkDeviceSize)base * sizeof(gpu_triangle), bytes);
        vk_descriptors_buffer(&d, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.motion.buffer, (VkDeviceSize)base * 64,
            (VkDeviceSize)m->polygons * 2 * 64);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_COMPUTE, vrt.skin_layout);
        vrt_skin_push push = { (uint32_t)m->polygons, (uint32_t)(m->bones > 0 ? m->bones : 1), (uint32_t)has_previous,
            (uint32_t)m->override_color, m->color, (uint32_t)m->override_texture, m->tpage, m->clut };
        vkCmdPushConstants(cmd, vrt.skin_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
        vkCmdDispatch(cmd, (uint32_t)(m->polygons + 63) / 64, 1, 1);
        e->used = vrt.updates;
        e->slot = slot;
        e->base = base;
        base += m->polygons * 2;
    }
    vk_barrier();
    /* Build new BLASes, refit the others (rebuilt every VRT_REBUILD_FRAMES). */
    static vrt_build builds[VRT_MAX_MOVERS];
    int build_count = 0;
    VkDeviceSize scratch = 0;
    VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                               | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    for (int i = 0; i < count; i++) {
        vrt_mover_entry* e = entries[i];
        if (!e)
            continue;
        int update = e->blas && vrt.updates - e->built < VRT_REBUILD_FRAMES;
        VkAccelerationStructureGeometryKHR geometry = vrt_geometry(
            vrt.mover_tris[slot].address + (VkDeviceSize)bases[i] * sizeof(gpu_triangle), e->polygons * 2, 0);
        VkDeviceSize s = vrt_prepare(&builds[build_count], &e->blas, &e->buffer, geometry, e->polygons * 2, flags,
            update, scratch);
        if (!s) {
            entries[i] = NULL;
            continue;
        }
        if (!update)
            e->built = vrt.updates;
        scratch += s;
        ++build_count;
    }
    vrt_build_all(builds, build_count, scratch);
    for (int i = 0; i < count; i++) {
        if (!entries[i])
            continue;
        vrt.movers[vrt.mover_count].address = vrt_address(entries[i]->blas);
        vrt.movers[vrt.mover_count].base = bases[i];
        ++vrt.mover_count;
    }
    vrt.mover_triangles = base;
    vrt.mover_slot = slot;
}
static int vrt_tlas(void)
{
    int max = 2 + vrt.mover_count;
    vk_slice instances = vk_arena((VkDeviceSize)max * sizeof(VkAccelerationStructureInstanceKHR), 16);
    if (!instances.data || !instances.address)
        return 0;
    VkAccelerationStructureInstanceKHR* in = instances.data;
    memset(in, 0, (size_t)max * sizeof *in);
    uint32_t count = 0;
    for (int i = 0; i < max; i++) {
        VkDeviceAddress address = i == 0 ? vrt.room_address : i == 1 ? vrt.cutout_address : vrt.movers[i - 2].address;
        if (!address)
            continue;
        VkAccelerationStructureInstanceKHR* x = &in[count++];
        x->transform.matrix[0][0] = x->transform.matrix[1][1] = x->transform.matrix[2][2] = 1;
        x->instanceCustomIndex = i == 0 ? 0 : i == 1 ? (uint32_t)vrt.room_opaque : VRT_MOVER_BIT | (uint32_t)vrt.movers[i - 2].base;
        x->mask = i < 2 ? 1 : 2;
        x->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
                 | (i == 0 ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR : 0);
        x->accelerationStructureReference = address;
    }
    VkAccelerationStructureGeometryKHR geometry = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .data.deviceAddress = instances.address } };
    VkAccelerationStructureBuildGeometryInfoKHR build = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry };
    VkAccelerationStructureBuildSizesInfoKHR sizes = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    uint32_t capacity = VRT_MAX_MOVERS + 2;
    vkGetAccelerationStructureBuildSizesKHR(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &capacity,
        &sizes);
    if (!vrt_structure(&vrt.tlas, &vrt.tlas_buffer, sizes.accelerationStructureSize,
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR))
        return 0;
    VkDeviceAddress scratch = vrt_scratch(sizes.buildScratchSize);
    if (!scratch)
        return 0;
    build.dstAccelerationStructure = vrt.tlas;
    build.scratchData.deviceAddress = scratch;
    VkAccelerationStructureBuildRangeInfoKHR range = { .primitiveCount = count };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vk_end_rendering();
    vkCmdBuildAccelerationStructuresKHR(vk_cmd(), 1, &build, &ranges);
    vk_barrier();
    return 1;
}
/* The room, this present's movers and the TLAS over them. */
static int vrt_scene(void)
{
    ++vrt.updates;
    if (!vrt_room())
        return 0;
    vrt_movers();
    triangle_count = room_triangles;
    return vrt_tlas();
}
/* Mover shading triangles and their previous positions for descriptors
 * (a dummy range when there are none). */
static void vrt_bind_movers(vk_descriptors* d, int binding, int motion_binding)
{
    const vk_buffer* b = vrt.mover_count ? &vrt.mover_tris[vrt.mover_slot] : &vrt.room_tris;
    VkDeviceSize bytes = vrt.mover_count ? (VkDeviceSize)vrt.mover_triangles * sizeof(gpu_triangle) : sizeof(gpu_triangle);
    vk_descriptors_buffer(d, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, b->buffer, 0, bytes);
    if (motion_binding >= 0) {
        const vk_buffer* m = vrt.mover_count ? &vrt.motion : &vrt.room_tris;
        vk_descriptors_buffer(d, motion_binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m->buffer, 0,
            vrt.mover_count ? (VkDeviceSize)vrt.mover_triangles * 64 : sizeof(gpu_triangle));
    }
}

/* ---- images and passes ---- */

static void vrt_images(int w, int h)
{
    if (vrt.size[0] == w && vrt.size[1] == h)
        return;
    for (int i = 0; i < VRT_IMAGES; i++) {
        vk_image_destroy_later(&vrt.images[i]);
        VkFormat format = i == POSITION || i == POSITION_OLD || i == VRT_MOTION ? VK_FORMAT_R32G32B32A32_SFLOAT
            : i == SIGNATURE || i == SIGNATURE_OLD         ? VK_FORMAT_R32_UINT
            : i == VRT_HITDIST                             ? VK_FORMAT_R16_SFLOAT
            : i == VRT_NRD_VIEWZ                           ? VK_FORMAT_R32_SFLOAT
                                                           : VK_FORMAT_R16G16B16A16_SFLOAT;
        vk_image_create(&vrt.images[i], w, h, format,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    }
    vrt.size[0] = w, vrt.size[1] = h;
    vrt.history_valid = 0;
}
static void vrt_dispatch(VkPipeline pipeline, VkPipelineLayout layout, vk_descriptors* d)
{
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk_descriptors_push(d, VK_PIPELINE_BIND_POINT_COMPUTE, layout);
    vkCmdDispatch(cmd, (uint32_t)(vrt.size[0] + 7) / 8, (uint32_t)(vrt.size[1] + 7) / 8, 1);
    vk_barrier();
}
static void vrt_copy(int from, int to)
{
    VkImageCopy region = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .extent = { (uint32_t)vrt.size[0], (uint32_t)vrt.size[1], 1 } };
    vkCmdCopyImage(vk_cmd(), vrt.images[from].image, VK_IMAGE_LAYOUT_GENERAL, vrt.images[to].image,
        VK_IMAGE_LAYOUT_GENERAL, 1, &region);
}
typedef struct {
    int grid[4];
    float depth_range[4], projection[4], inverse_rotation[3][4], camera_translation[4];
    float previous_rotation[3][4], previous_projection[4], previous_range[4];
} vrt_haze_ubo;
typedef struct {
    int grid[4];
    float depth_range[4], projection[4], inverse_rotation[3][4], camera_translation[4], room_lo[4], room_hi[4];
} vrt_integrate_ubo;
static void vrt_haze_dispatch(VkPipeline pipeline, VkPipelineLayout layout, vk_descriptors* d, int w, int h)
{
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk_descriptors_push(d, VK_PIPELINE_BIND_POINT_COMPUTE, layout);
    vkCmdDispatch(cmd, (uint32_t)(w + 7) / 8, (uint32_t)(h + 7) / 8, 1);
    vk_barrier();
}
/* The haze volume for this update's camera (after the trace: same UBO of
 * lights and the TLAS). */
static void vrt_haze(vk_slice trace_ubo)
{
    vrt.haze_on = 0;
    if (pc_lighting_traced_haze_density() <= 0) /* lighting quality: haze High */
        return;
    int gx = (vrt.display[0] + HAZE_CELL - 1) / HAZE_CELL, gy = (vrt.display[1] + HAZE_CELL - 1) / HAZE_CELL;
    if (gx < 1 || gy < 1)
        return;
    if (gx != vrt.haze_grid[0] || gy != vrt.haze_grid[1] || vrt.haze_grid[2] != HAZE_SLICES) {
        for (int i = 0; i < 3; i++) {
            vk_image* im = i < 2 ? &vrt.haze_scatter[i] : &vrt.haze_integrated;
            vk_image_destroy_later(im);
            if (!vk_image_create(im, gx * HAZE_SLICES, gy, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
                vrt.haze_grid[0] = 0;
                return;
            }
        }
        vrt.haze_grid[0] = gx, vrt.haze_grid[1] = gy, vrt.haze_grid[2] = HAZE_SLICES;
        vrt.haze_valid = 0;
    }
    /* Depth range: to the farthest corner of the room's bounds. */
    float zfar = HAZE_NEAR * 4;
    for (int c = 0; c < 8; c++) {
        float corner[3] = { c & 1 ? room_hi[0] : room_lo[0], c & 2 ? room_hi[1] : room_lo[1], c & 4 ? room_hi[2] : room_lo[2] };
        float z = vrt.cam_r[2][0] * corner[0] + vrt.cam_r[2][1] * corner[1] + vrt.cam_r[2][2] * corner[2] + vrt.cam_tr[2];
        zfar = z > zfar ? z : zfar;
    }
    float range[2] = { HAZE_NEAR, logf(zfar / HAZE_NEAR) };
    VkDeviceSize alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    vk_slice hs = vk_arena(sizeof(vrt_haze_ubo), alignment), is = vk_arena(sizeof(vrt_integrate_ubo), alignment);
    if (!hs.data || !is.data)
        return;
    vrt_haze_ubo* hu = hs.data;
    memset(hu, 0, sizeof *hu);
    hu->grid[0] = gx, hu->grid[1] = gy, hu->grid[2] = HAZE_SLICES, hu->grid[3] = vrt.haze_valid;
    hu->depth_range[0] = range[0], hu->depth_range[1] = range[1];
    hu->depth_range[2] = (float)vrt.display[0], hu->depth_range[3] = (float)vrt.display[1];
    memcpy(hu->projection, vrt.cam_projection, sizeof hu->projection);
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            hu->inverse_rotation[col][row] = vrt.cam_inverse[row][col];
    memcpy(hu->camera_translation, vrt.cam_tr, sizeof vrt.cam_tr);
    hu->camera_translation[3] = pc_haze_history;
    memcpy(hu->previous_rotation, vrt.haze_previous_rotation, sizeof hu->previous_rotation);
    memcpy(hu->previous_projection, vrt.haze_previous_projection, sizeof hu->previous_projection);
    hu->previous_range[0] = vrt.haze_previous_range[0], hu->previous_range[1] = vrt.haze_previous_range[1];
    int out = vrt.haze_current ^ 1;
    vk_section("haze");
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, trace_ubo.buffer, trace_ubo.offset,
        sizeof(vrt_trace_ubo));
    vk_descriptors_structure(&d, 1, vrt.tlas);
    vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0,
        (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof *triangles);
    vk_descriptors_image(&d, 3, vk_hw.vram.view, vk.nearest);
    vk_descriptors_buffer(&d, 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, hs.buffer, hs.offset, sizeof *hu);
    vk_descriptors_storage_image(&d, 5, vrt.haze_scatter[out].view);
    vk_descriptors_image(&d, 6, vrt.haze_scatter[vrt.haze_current].view, vk.linear);
    vrt_bind_movers(&d, 9, -1);
    vrt_haze_dispatch(vrt.haze, vrt.haze_layout, &d, gx * HAZE_SLICES, gy);
    vrt_integrate_ubo* iu = is.data;
    memset(iu, 0, sizeof *iu);
    memcpy(iu->grid, hu->grid, sizeof iu->grid);
    memcpy(iu->depth_range, hu->depth_range, sizeof iu->depth_range);
    memcpy(iu->projection, hu->projection, sizeof iu->projection);
    memcpy(iu->inverse_rotation, hu->inverse_rotation, sizeof iu->inverse_rotation);
    memcpy(iu->camera_translation, vrt.cam_tr, sizeof vrt.cam_tr);
    memcpy(iu->room_lo, room_lo, 3 * sizeof(float));
    memcpy(iu->room_hi, room_hi, 3 * sizeof(float));
    iu->room_lo[3] = pc_lighting_traced_haze_density();
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, is.buffer, is.offset, sizeof *iu);
    vk_descriptors_storage_image(&d, 1, vrt.haze_scatter[out].view);
    vk_descriptors_storage_image(&d, 2, vrt.haze_integrated.view);
    vrt_haze_dispatch(vrt.integrate, vrt.integrate_layout, &d, gx, gy);
    /* This camera becomes the history's. */
    for (int row = 0; row < 3; row++) {
        for (int k = 0; k < 3; k++)
            vrt.haze_previous_rotation[row][k] = vrt.cam_r[row][k];
        vrt.haze_previous_rotation[row][3] = vrt.cam_tr[row];
    }
    memcpy(vrt.haze_previous_projection, vrt.cam_projection, sizeof vrt.haze_previous_projection);
    vrt.haze_previous_range[0] = range[0], vrt.haze_previous_range[1] = range[1];
    vrt.haze_range[0] = range[0], vrt.haze_range[1] = range[1];
    vrt.haze_current = out;
    vrt.haze_valid = 1;
    vrt.haze_on = 1;
}
/* The haze volume for the present pass (0: none this frame). */
int pc_vk_haze_bind(VkImageView* view, int grid[4], float range[2])
{
    if (!vrt.haze_on)
        return 0;
    *view = vrt.haze_integrated.view;
    grid[0] = vrt.haze_grid[0], grid[1] = vrt.haze_grid[1], grid[2] = vrt.haze_grid[2], grid[3] = 1;
    range[0] = vrt.haze_range[0], range[1] = vrt.haze_range[1];
    return 1;
}
/* ---- NRD (gi_denoiser nrd) ---- */

int pc_gi_denoiser_active(void); /* ptgi.c */

/* Built with NRD, on a ray-tracing device, NRD not failed. */
static int vrt_nrd_usable(void) { return pc_vk_active && vk.ray_query && vk_nrd_available(); }
static unsigned vrt_hash(unsigned h, const void* data, size_t bytes)
{
    const unsigned char* b = data;
    for (size_t i = 0; i < bytes; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
/* NRD RELAX diffuse over this update's raw indirect light (plus the glows'
 * light with pc_nrd_glow), into the result's rgb (after our filter, which
 * keeps the visibility signals). Its camera: the GTE camera's rows carry
 * different scales (X stretched for the pixel aspect), so NRD gets an
 * orthonormal view (normalized rows) with the scales folded into its
 * projection; view depth is along the normalized forward axis. History is
 * reset on a resize, a new room capture, a light change and a camera cut. */
static int vrt_nrd(const float eye[3], int result, int result_extra)
{
    float e[3][3], s[3];
    for (int i = 0; i < 3; i++) {
        s[i] = sqrtf(vrt.cam_r[i][0] * vrt.cam_r[i][0] + vrt.cam_r[i][1] * vrt.cam_r[i][1]
                     + vrt.cam_r[i][2] * vrt.cam_r[i][2]);
        if (s[i] < 1e-9f)
            return 0;
    }
    /* Gram-Schmidt from the forward row: forward, right, down. */
    for (int k = 0; k < 3; k++)
        e[2][k] = vrt.cam_r[2][k] / s[2];
    for (int row = 0; row < 2; row++) { /* right (against forward), then down (against both) */
        float v[3] = { vrt.cam_r[row][0], vrt.cam_r[row][1], vrt.cam_r[row][2] };
        for (int q = 0; q < 2; q++) {
            int axis = q == 0 ? 2 : 0;
            if (q == 1 && row == 0)
                break;
            float dot = v[0] * e[axis][0] + v[1] * e[axis][1] + v[2] * e[axis][2];
            for (int k = 0; k < 3; k++)
                v[k] -= dot * e[axis][k];
        }
        float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l < 1e-9f)
            return 0;
        for (int k = 0; k < 3; k++)
            e[row][k] = v[k] / l;
    }
    float view[16] = { 0 }, projection[16] = { 0 };
    for (int row = 0; row < 3; row++) {
        for (int c = 0; c < 3; c++)
            view[c * 4 + row] = e[row][c];
        view[12 + row] = -(e[row][0] * eye[0] + e[row][1] * eye[1] + e[row][2] * eye[2]);
    }
    view[15] = 1;
    /* Display x = hx * vx / vz + ox with vx = s0 * x', vz = s2 * z' (x', z'
     * NRD's view); NRD's uv = clip.xy / w * (0.5, -0.5) + 0.5. */
    float w = (float)vrt.display[0], h = (float)vrt.display[1];
    float hx = vrt.cam_projection[0], hh = vrt.cam_projection[1], ox = vrt.cam_projection[2], oy = vrt.cam_projection[3];
    projection[0] = 2.0f * hx * s[0] / (s[2] * w);
    projection[8] = 2.0f * ox / w - 1.0f;
    projection[5] = -2.0f * hh * s[1] / (s[2] * h);
    projection[9] = 1.0f - 2.0f * oy / h;
    projection[10] = 1.0f; /* infinite far plane, near 1 */
    projection[11] = 1.0f;
    projection[14] = -1.0f;
    /* Resets. */
    unsigned lights = vrt_hash(2166136261u, &light_count, sizeof light_count);
    lights = vrt_hash(lights, lp, sizeof lp);
    lights = vrt_hash(lights, lc, sizeof lc);
    lights = vrt_hash(lights, ld, sizeof ld);
    lights = vrt_hash(lights, lr, sizeof lr);
    lights = vrt_hash(lights, lu, sizeof lu);
    lights = vrt_hash(lights, light_shadows, sizeof light_shadows);
    float amounts[4] = { pc_gi_baked_bounce, pc_emitter_light, (float)pc_gi_bounces, (float)pc_nrd_glow };
    lights = vrt_hash(lights, amounts, sizeof amounts);
    float moved = 0, facing = 0;
    for (int k = 0; k < 3; k++) {
        moved += (eye[k] - vrt.nrd.eye[k]) * (eye[k] - vrt.nrd.eye[k]);
        facing += e[2][k] * vrt.nrd.forward[k];
    }
    int serial = pc_scene_serial();
    int cut = moved > 256.0f * 256.0f || facing < 0.8f;
    int reset = !vrt.nrd.valid || !vrt.nrd.denoised || vrt.nrd.serial != serial || vrt.nrd.lights != lights || cut
        || vrt.nrd.size[0] != vrt.size[0] || vrt.nrd.size[1] != vrt.size[1];
    /* Inputs. */
    vk_image* im = vrt.images;
    vk_section("gi nrd");
    vk_descriptors d = { 0 };
    int inputs[10] = { POSITION, NORMAL, VRT_MOTION, RAW, RAW_EXTRA, VRT_HITDIST, VRT_NRD_NORMAL, VRT_NRD_VIEWZ,
        VRT_NRD_MOTION, VRT_NRD_RADIANCE };
    for (int i = 0; i < 10; i++)
        vk_descriptors_storage_image(&d, i, im[inputs[i]].view);
    const float range = 500000.0f;
    const float scale = 1.0f; /* radiance scale into NRD (1: RELAX was scale-invariant here, 1..256 tried) */
    struct {
        float eye_range[4], forward[4], scale[4];
    } push = { { eye[0], eye[1], eye[2], range }, { e[2][0], e[2][1], e[2][2], pc_nrd_glow ? 1.0f : 0.0f },
        { scale, 0, 0, 0 } };
    vkCmdPushConstants(vk_cmd(), vrt.nrd_in_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    vrt_dispatch(vrt.nrd_in, vrt.nrd_in_layout, &d);
    vk_nrd_frame f;
    memset(&f, 0, sizeof f);
    f.width = vrt.size[0], f.height = vrt.size[1];
    f.normal_roughness = im[VRT_NRD_NORMAL].view;
    f.view_z = im[VRT_NRD_VIEWZ].view;
    f.motion = im[VRT_NRD_MOTION].view;
    f.radiance = im[VRT_NRD_RADIANCE].view;
    f.output = im[VRT_NRD_OUTPUT].view;
    memcpy(f.world_to_view, view, sizeof view);
    memcpy(f.view_to_clip, projection, sizeof projection);
    memcpy(f.world_to_view_prev, vrt.nrd.view, sizeof view);
    memcpy(f.view_to_clip_prev, vrt.nrd.projection, sizeof projection);
    f.denoising_range = range;
    f.frame_index = reset ? 0 : vrt.nrd.frame + 1;
    f.reset = reset;
    f.max_frames = pc_nrd_max_frames;
    if (!vk_nrd_denoise(&f)) {
        vrt.nrd.denoised = 0;
        return 0;
    }
    /* Into the result. */
    memset(&d, 0, sizeof d);
    int outputs[4] = { VRT_NRD_OUTPUT, POSITION, result, result_extra };
    for (int i = 0; i < 4; i++)
        vk_descriptors_storage_image(&d, i, im[outputs[i]].view);
    struct {
        int with_glow;
        float inverse_scale;
        int unused[2];
    } out_push = { pc_nrd_glow != 0, 1.0f / scale, { 0, 0 } };
    vkCmdPushConstants(vk_cmd(), vrt.nrd_out_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof out_push, &out_push);
    vrt_dispatch(vrt.nrd_out, vrt.nrd_out_layout, &d);
    memcpy(vrt.nrd.view, view, sizeof view);
    memcpy(vrt.nrd.projection, projection, sizeof projection);
    memcpy(vrt.nrd.eye, eye, sizeof vrt.nrd.eye);
    memcpy(vrt.nrd.forward, e[2], sizeof vrt.nrd.forward);
    vrt.nrd.valid = 1;
    vrt.nrd.denoised = 1;
    vrt.nrd.serial = serial;
    vrt.nrd.lights = lights;
    vrt.nrd.size[0] = vrt.size[0], vrt.nrd.size[1] = vrt.size[1];
    vrt.nrd.frame = f.frame_index;
    return 1;
}

static void vrt_run(const float to_image[16], const float eye[3], float footprint)
{
    VkDeviceSize ubo_alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    vk_image* im = vrt.images;
    /* 1: trace. */
    vk_section("gi trace");
    vk_slice ubo = vk_arena(sizeof(vrt_trace_ubo), ubo_alignment);
    if (!ubo.data)
        return;
    vrt_trace_ubo* u = ubo.data;
    memset(u, 0, sizeof *u);
    memcpy(u->eye, eye, 3 * sizeof(float));
    memcpy(u->light_pos, lp, sizeof lp);
    memcpy(u->light_col, lc, sizeof lc);
    memcpy(u->light_dir, ld, sizeof ld);
    memcpy(u->light_right, lr, sizeof lr);
    memcpy(u->light_up, lu, sizeof lu);
    memcpy(u->light_shadow, light_shadows, sizeof light_shadows);
    memcpy(u->emitter_pos, emitter_pos, sizeof emitter_pos);
    memcpy(u->emitter_col, emitter_col, sizeof emitter_col);
    memcpy(u->room_lo, room_lo, sizeof room_lo);
    memcpy(u->room_hi, room_hi, sizeof room_hi);
    u->i0[0] = light_count;
    u->i0[1] = pc_gi_enabled; /* gi_method path only (probes: vk_probes.inc) */
    u->i0[2] = pc_rt_shadows;
    u->i0[3] = pc_gi_bounces < 1 ? 1 : pc_gi_bounces > 4 ? 4 : pc_gi_bounces;
    u->i1[0] = pc_gi_rays < 1 ? 1 : pc_gi_rays > 16 ? 16 : pc_gi_rays;
    u->i1[1] = emitter_count;
    u->i1[2] = pc_baked_shadows;
    u->i1[3] = pc_shadow_samples < 1 ? 1 : pc_shadow_samples > 8 ? 8 : pc_shadow_samples;
    u->i2[0] = pc_glow_shadows;
    u->i3[0] = (int)pc_glow_casts;
    u->i2[1] = pc_fog_steps < 2 ? 2 : pc_fog_steps > 64 ? 64 : pc_fog_steps;
    u->i2[2] = pc_gi_trace_debug;
    u->i2[3] = (int)++vrt.frame;
    u->f0[0] = pc_light_size;
    u->f0[1] = pc_gi_baked_bounce;
    u->f0[2] = pc_emitter_light;
    u->f0[3] = pc_contact_radius;
    u->f1[0] = pc_baked_ambient;
    u->f1[1] = 0; /* haze: the froxel volume (vrt_haze), not per pixel */
    u->f1[2] = pc_fog_anisotropy;
    u->f1[3] = pc_fog_glow;
    u->f2[0] = pc_lighting_ao_traced() ? pc_ao_radius : 0.0f;
    u->f2[1] = pc_ao_strength;
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ubo.buffer, ubo.offset, sizeof *u);
    vk_descriptors_structure(&d, 1, vrt.tlas);
    vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0,
        (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof *triangles);
    vrt_bind_movers(&d, 9, -1);
    vk_descriptors_image(&d, 3, vk_hw.vram.view, vk.nearest);
    vk_descriptors_storage_image(&d, 4, im[POSITION].view);
    vk_descriptors_storage_image(&d, 5, im[NORMAL].view);
    vk_descriptors_storage_image(&d, 6, im[RAW].view);
    vk_descriptors_storage_image(&d, 7, im[RAW_EXTRA].view);
    vk_descriptors_storage_image(&d, 8, im[SIGNATURE].view);
    vk_descriptors_storage_image(&d, 10, im[VRT_HITDIST].view);
    vrt_dispatch(vrt.trace, vrt.trace_layout, &d);
    vrt_haze(ubo);

    /* 2: temporal accumulation into the other history image. */
    vk_section("gi temporal");
    int old = HISTORY_A + vrt.history_current, out = HISTORY_A + (vrt.history_current ^ 1);
    int old_extra = HISTORY_EXTRA_A + vrt.history_current, out_extra = HISTORY_EXTRA_A + (vrt.history_current ^ 1);
    vk_slice tubo = vk_arena(sizeof(vrt_temporal_ubo), ubo_alignment);
    if (!tubo.data)
        return;
    vrt_temporal_ubo* t = tubo.data;
    memcpy(t->previous_to_image, vrt.previous_to_image, sizeof t->previous_to_image);
    t->params[0] = footprint;
    t->params[1] = pc_gi_denoise ? pc_shadow_history : 1.0f;
    t->params[2] = (float)vrt.history_valid;
    t->params[3] = pc_gi_denoise ? fminf(pc_shadow_motion_history, t->params[1]) : 1.0f;
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, tubo.buffer, tubo.offset, sizeof *t);
    int old_moments = VRT_MOMENTS_A + vrt.history_current, out_moments = VRT_MOMENTS_A + (vrt.history_current ^ 1);
    int temporal_images[16] = { RAW, POSITION, NORMAL, out, RAW_EXTRA, out_extra, SIGNATURE, old, POSITION_OLD,
        NORMAL_OLD, old_extra, SIGNATURE_OLD, VRT_MOTION, old_moments, out_moments, VRT_VARIANCE };
    for (int i = 0; i < 16; i++)
        vk_descriptors_storage_image(&d, 1 + i, im[temporal_images[i]].view);
    vrt_dispatch(vrt.temporal, vrt.temporal_layout, &d);
    /* The current surface becomes next frame's history surface. */
    vrt_copy(POSITION, POSITION_OLD);
    vrt_copy(NORMAL, NORMAL_OLD);
    vrt_copy(SIGNATURE, SIGNATURE_OLD);
    vk_barrier();
    memcpy(vrt.previous_to_image, to_image, sizeof vrt.previous_to_image);
    vrt.history_current ^= 1;
    vrt.history_valid = 1;

    /* 3: a-trous filtering of the accumulated light (display only). */
    vrt.result = out;
    vrt.result_extra = out_extra;
    if (!pc_gi_denoise) {
        vrt.nrd.denoised = 0;
        return;
    }
    vk_section("gi filter");
    int source = out, source_extra = out_extra, source_variance = VRT_VARIANCE;
    for (int i = 0; i < 4; i++) {
        int target = i & 1 ? FILTER_B : FILTER_A, target_extra = i & 1 ? FILTER_EXTRA_B : FILTER_EXTRA_A;
        int target_variance = i & 1 ? VRT_VARIANCE_B : VRT_VARIANCE_A;
        int filter_images[8] = { source, POSITION, NORMAL, target, source_extra, target_extra, source_variance,
            target_variance };
        for (int k = 0; k < 8; k++)
            vk_descriptors_storage_image(&d, k, im[filter_images[k]].view);
        struct {
            int step, filter_visibility, final_pass, unused;
            float eye_footprint[4];
        } push = { 1 << i, 1, i == 3, 0, { eye[0], eye[1], eye[2], footprint } };
        vkCmdPushConstants(vk_cmd(), vrt.atrous_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
        vrt_dispatch(vrt.atrous, vrt.atrous_layout, &d);
        source = target;
        source_extra = target_extra;
        source_variance = target_variance;
    }
    vrt.result = source;
    vrt.result_extra = source_extra;
    /* NRD for the indirect light (when there is any to denoise). */
    if (pc_gi_denoise >= 2 && pc_gi_enabled && vrt_nrd_usable())
        vrt_nrd(eye, vrt.result, vrt.result_extra);
    else
        vrt.nrd.denoised = 0;
}

int pc_vk_gi_dump(const char* path);
/* RPC gi_dump with a count: dump the next `count` updates to
 * path-000.bin, path-001.bin, ... (pairs with present_dump bursts). */
void pc_vk_gi_dump_burst(const char* path, int count)
{
    snprintf(vrt.dump_path, sizeof vrt.dump_path, "%s", path);
    vrt.dump_left = count;
    vrt.dump_index = 0;
}
/* Game view (vk_present.c): this frame's indirect light and shadow
 * visibility for the display area (x, y, w, h in VRAM) from the live or the
 * replay target. Returns 1 when there is a result (pc_vk_gi_bind). */
int pc_vk_gi_update(int x, int y, int w, int h, int replay)
{
    static int checked;
    const char* setting = getenv("ROOD_GI");
    if (!checked++ && setting && *setting == '0')
        pc_gi_enabled = pc_rt_shadows = pc_baked_shadows = 0; /* as ptgi.c's init() */
    pc_baked_shadows_active = 0; /* until this frame's trace */
    int ao = pc_lighting_ao_traced(); /* lighting quality: AO traced with the GI rays */
    if (w <= 0 || h <= 0 || !(pc_gi_enabled || pc_rt_shadows || pc_baked_shadows || ao) || !pc_lights_enabled
        || !vrt_init())
        return 0;
    float r[3][3], tr[3], hh, ox, oy, inverse[3][3];
    if (!pc_scene_camera_get(r, tr, &hh, &ox, &oy) || !pc_scene_camera_inverse(inverse))
        return 0;
    int room_count;
    if (!pc_scene_triangle_data(&room_count) || !room_count)
        return 0;
    /* As ptgi.c's ready(): emitters, lights; nothing to do without them. */
    update_emitters();
    pc_scene_bounds(room_lo, room_hi);
    int lights = gather_lights();
    pc_baked_shadows_active = pc_baked_shadows && (emitter_count || pc_contact_radius > 0);
    if (!(lights || pc_baked_shadows_active || (emitter_count && pc_emitter_light > 0) || ao))
        return 0;
    vk_section("gi scene");
    if (!vrt_scene())
        return 0;
    float res = pc_gi_resolution < 0.25f ? 0.25f : pc_gi_resolution > 4 ? 4 : pc_gi_resolution;
    int gw = (int)(w * res + 0.5f), gh = (int)(h * res + 0.5f);
    vrt_images(gw, gh);
    float hx = hh * (pc_gte_widescreen ? 0.75f : 1.0f);
    /* Surfaces from the G-buffer. */
    const vk_target* target = replay ? &vk_hw.replay : &vk_hw.live;
    vk_slice ubo = vk_arena(sizeof(vrt_surface_ubo), vk.properties.limits.minUniformBufferOffsetAlignment);
    if (!ubo.data)
        return 0;
    vrt_surface_ubo* s = ubo.data;
    memset(s, 0, sizeof *s);
    s->rect[0] = x, s->rect[1] = y, s->rect[2] = w, s->rect[3] = h;
    s->scale[0] = pc_hw_scale;
    s->gi_scale[0] = (float)gw / w, s->gi_scale[1] = (float)gh / h;
    s->projection[0] = hx, s->projection[1] = hh;
    s->screen_offset[0] = ox, s->screen_offset[1] = oy;
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) {
            s->inverse_rotation[col][row] = inverse[row][col];
            s->normal_to_world[col][row] = r[col][row];
        }
    s->camera_translation[0] = tr[0], s->camera_translation[1] = tr[1], s->camera_translation[2] = tr[2];
    float eye[3];
    for (int k = 0; k < 3; k++)
        eye[k] = -(inverse[k][0] * tr[0] + inverse[k][1] * tr[1] + inverse[k][2] * tr[2]);
    memcpy(s->eye, eye, sizeof eye);
    s->scale[1] = vrt.mover_count > 0;
    memcpy(vrt.cam_r, r, sizeof vrt.cam_r);
    memcpy(vrt.cam_tr, tr, sizeof vrt.cam_tr);
    memcpy(vrt.cam_inverse, inverse, sizeof vrt.cam_inverse);
    vrt.cam_projection[0] = hx, vrt.cam_projection[1] = hh, vrt.cam_projection[2] = ox, vrt.cam_projection[3] = oy;
    vrt.display[0] = w, vrt.display[1] = h;
    vk_end_rendering();
    vk_section("gi surface");
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ubo.buffer, ubo.offset, sizeof *s);
    vk_descriptors_structure(&d, 1, vrt.tlas);
    vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0,
        (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof *triangles);
    vk_descriptors_image(&d, 3, vk_hw.vram.view, vk.nearest);
    vk_descriptors_image(&d, 4, target->normal.view, vk.nearest);
    vk_descriptors_image(&d, 5, target->depth.view, vk.nearest);
    vk_descriptors_storage_image(&d, 6, vrt.images[POSITION].view);
    vk_descriptors_storage_image(&d, 7, vrt.images[NORMAL].view);
    vk_descriptors_storage_image(&d, 8, vrt.images[VRT_MOTION].view);
    vrt_bind_movers(&d, 9, 10);
    vrt_dispatch(vrt.surface, vrt.surface_layout, &d);
    /* World -> GI image (column-major, w = view depth), as ptgi.c. */
    float to_image[16];
    for (int k = 0; k < 4; k++) {
        float r0 = k < 3 ? r[0][k] : tr[0], r1 = k < 3 ? r[1][k] : tr[1], r2 = k < 3 ? r[2][k] : tr[2];
        to_image[k * 4 + 0] = (hx * r0 + ox * r2) / w;
        to_image[k * 4 + 1] = (hh * r1 + oy * r2) / h;
        to_image[k * 4 + 2] = 0;
        to_image[k * 4 + 3] = r2;
    }
    vrt_run(to_image, eye, 1.0f / (hh * (float)gh / (float)h));
    memcpy(vrt.eye, eye, sizeof vrt.eye);
    vrt.replay = replay;
    if (vrt.dump_left > 0) {
        char path[420];
        snprintf(path, sizeof path, "%s-%03d.bin", vrt.dump_path, vrt.dump_index++);
        vrt.dump_left--;
        pc_vk_gi_dump(path);
    }
    return 1;
}
/* The last result for the present pass: views of the indirect light (rgb,
 * a shadow visibility), the GI pixels' positions and normals, and the extra
 * term (r baked visibility, gba haze); the GI size; the room's bounds. */
void pc_vk_gi_bind(VkImageView views[4], int* w, int* h, float lo[3], float hi[3])
{
    views[0] = vrt.images[vrt.result].view;
    views[1] = vrt.images[POSITION].view;
    views[2] = vrt.images[NORMAL].view;
    views[3] = vrt.images[vrt.result_extra].view;
    *w = vrt.size[0], *h = vrt.size[1];
    memcpy(lo, room_lo, sizeof room_lo);
    memcpy(hi, room_hi, sizeof room_hi);
}

/* ---- editor inspector ---- */

static float vrt_half(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31, exponent = (h >> 10) & 31, mantissa = h & 1023;
    union {
        uint32_t u;
        float f;
    } v;
    if (exponent == 0)
        return (sign ? -1.0f : 1.0f) * ldexpf((float)mantissa, -24);
    v.u = sign | (exponent == 31 ? 0xff << 23 : (exponent + 112) << 23) | mantissa << 13;
    return v.f;
}
/* pc_gi_probe on Vulkan: the last filtered result at (u, v) of the display
 * area (indirect light, shadow visibility; baked visibility, glow light),
 * read back synchronously (an inspector, not a per-frame path). */
static int vrt_probe(float u, float v, float out[8])
{
    if (vrt.state <= 0 || !vrt.history_valid || !vrt.size[0] || u < 0 || v < 0 || u >= 1 || v >= 1)
        return 0;
    int x = (int)(u * vrt.size[0]), y = (int)(v * vrt.size[1]);
    uint16_t texel[2][4];
    if (!vk_read_image(&vrt.images[vrt.result], VK_IMAGE_ASPECT_COLOR_BIT, x, y, 1, 1, texel[0], 8)
        || !vk_read_image(&vrt.images[vrt.result_extra], VK_IMAGE_ASPECT_COLOR_BIT, x, y, 1, 1, texel[1], 8))
        return 0;
    for (int i = 0; i < 8; i++)
        out[i] = vrt_half(texel[i / 4][i % 4]);
    return 1;
}
/* RPC gi_dump: the last update's GI images to `path` (diagnostics and
 * tests: shadows mapped back to world space). Header line "rood-gi-dump W H
 * eye_x eye_y eye_z replay frame R(9, row-major) T(3) H_x H OFX OFY\n" (the
 * update's camera), then per GI pixel, row by row, 11 floats: world position
 * and kind (w 0 none, 1 surface, 2 moving), normal, raw traced shadow
 * visibility, filtered visibility, raw and filtered baked-light visibility
 * (contact and glow shadows of movers). */
int pc_vk_gi_dump(const char* path)
{
    if (vrt.state <= 0 || !vrt.history_valid || !vrt.size[0])
        return -1;
    int w = vrt.size[0], h = vrt.size[1];
    size_t n = (size_t)w * h;
    float* position = malloc(n * 16);
    uint16_t *normal = malloc(n * 8), *raw = malloc(n * 8), *result = malloc(n * 8);
    uint16_t *raw_extra = malloc(n * 8), *extra = malloc(n * 8);
    int ok = position && normal && raw && result && raw_extra && extra
        && vk_read_image(&vrt.images[POSITION], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, position, 16)
        && vk_read_image(&vrt.images[NORMAL], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, normal, 8)
        && vk_read_image(&vrt.images[RAW], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, raw, 8)
        && vk_read_image(&vrt.images[vrt.result], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, result, 8)
        && vk_read_image(&vrt.images[RAW_EXTRA], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, raw_extra, 8)
        && vk_read_image(&vrt.images[vrt.result_extra], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, extra, 8);
    FILE* f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        fprintf(f, "rood-gi-dump %d %d %.4f %.4f %.4f %d %u", w, h, vrt.eye[0], vrt.eye[1], vrt.eye[2], vrt.replay,
            vrt.frame);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                fprintf(f, " %.7g", vrt.cam_r[r][c]);
        fprintf(f, " %.7g %.7g %.7g %.7g %.7g %.7g %.7g\n", vrt.cam_tr[0], vrt.cam_tr[1], vrt.cam_tr[2],
            vrt.cam_projection[0], vrt.cam_projection[1], vrt.cam_projection[2], vrt.cam_projection[3]);
        for (size_t i = 0; i < n; i++) {
            float row[11] = { position[i * 4], position[i * 4 + 1], position[i * 4 + 2], position[i * 4 + 3],
                vrt_half(normal[i * 4]), vrt_half(normal[i * 4 + 1]), vrt_half(normal[i * 4 + 2]),
                vrt_half(raw[i * 4 + 3]), vrt_half(result[i * 4 + 3]), vrt_half(raw_extra[i * 4]),
                vrt_half(extra[i * 4]) };
            fwrite(row, sizeof row, 1, f);
        }
        ok = !fclose(f);
    } else
        ok = 0;
    free(position);
    free(normal);
    free(raw);
    free(result);
    free(raw_extra);
    free(extra);
    return ok ? 0 : -1;
}
/* RPC gi_dump {light: true}: the last update's indirect light to `path`
 * (denoiser comparisons). Header line "rood-gi-light W H denoiser frame
 * nrd nrd_frame\n" (denoiser 0 raw, 1 filter, 2 NRD; nrd 1 when NRD ran;
 * nrd_frame: NRD updates since its history was last reset), then per GI
 * pixel, row by row, 9 floats: kind (0 none, 1 surface, 2 moving), raw
 * indirect rgb, displayed indirect rgb (the result), NRD's history length
 * (frames; 0 without NRD), NRD's view depth input. */
int pc_vk_gi_dump_light(const char* path)
{
    if (vrt.state <= 0 || !vrt.history_valid || !vrt.size[0])
        return -1;
    int w = vrt.size[0], h = vrt.size[1], nrd = vrt.nrd.denoised;
    size_t n = (size_t)w * h;
    float *position = malloc(n * 16), *view_z = malloc(n * 4);
    uint16_t *raw = malloc(n * 8), *result = malloc(n * 8), *out = malloc(n * 8);
    int ok = position && view_z && raw && result && out
        && vk_read_image(&vrt.images[POSITION], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, position, 16)
        && vk_read_image(&vrt.images[RAW], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, raw, 8)
        && vk_read_image(&vrt.images[vrt.result], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, result, 8)
        && vk_read_image(&vrt.images[VRT_NRD_OUTPUT], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, out, 8)
        && vk_read_image(&vrt.images[VRT_NRD_VIEWZ], VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, view_z, 4);
    FILE* f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        fprintf(f, "rood-gi-light %d %d %d %u %d %u\n", w, h, pc_gi_denoiser_active(), vrt.frame, nrd, vrt.nrd.frame);
        for (size_t i = 0; i < n; i++) {
            float row[9] = { position[i * 4 + 3], vrt_half(raw[i * 4]), vrt_half(raw[i * 4 + 1]),
                vrt_half(raw[i * 4 + 2]), vrt_half(result[i * 4]), vrt_half(result[i * 4 + 1]),
                vrt_half(result[i * 4 + 2]), nrd ? vrt_half(out[i * 4 + 3]) : 0.0f, view_z[i] };
            fwrite(row, sizeof row, 1, f);
        }
        ok = !fclose(f);
    } else
        ok = 0;
    free(position);
    free(view_z);
    free(raw);
    free(result);
    free(out);
    return ok ? 0 : -1;
}
/* pc_gi_trace_cpu on Vulkan (no CPU BVH): every room triangle and this
 * frame's casters, brute force (the inspector traces a few rays). */
static int vrt_trace_triangle(const float o[3], const float d[3], const float* a, const float* b, const float* c,
    float best_t, float* t_out)
{
    float e1[3], e2[3], pv[3], tv[3], qv[3];
    for (int k = 0; k < 3; k++)
        e1[k] = b[k] - a[k], e2[k] = c[k] - a[k], tv[k] = o[k] - a[k];
    pv[0] = d[1] * e2[2] - d[2] * e2[1], pv[1] = d[2] * e2[0] - d[0] * e2[2], pv[2] = d[0] * e2[1] - d[1] * e2[0];
    float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
    if (fabsf(det) < 1e-8f)
        return 0;
    float inv = 1.0f / det, bu = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
    if (bu < 0 || bu > 1)
        return 0;
    qv[0] = tv[1] * e1[2] - tv[2] * e1[1], qv[1] = tv[2] * e1[0] - tv[0] * e1[2], qv[2] = tv[0] * e1[1] - tv[1] * e1[0];
    float bv = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) * inv;
    if (bv < 0 || bu + bv > 1)
        return 0;
    float t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
    if (t <= 1e-3f || t >= best_t)
        return 0;
    *t_out = t;
    return 1;
}
static int vrt_trace_cpu(const float o[3], const float d[3], float tmax, float* t_out, float centre[3])
{
    float best_t = tmax, t;
    int flags = -1;
    for (int i = 0; i < room_triangles; i++) {
        const gpu_triangle* tri = &triangles[i];
        if (vrt_trace_triangle(o, d, tri->a, tri->b, tri->c, best_t, &t)) {
            best_t = t;
            flags = (int)tri->info[3];
            for (int k = 0; k < 3; k++)
                centre[k] = (tri->a[k] + tri->b[k] + tri->c[k]) / 3.0f;
        }
    }
    int casters, serial;
    const float* p = pc_hw_casters(&casters, &serial);
    for (int i = 0; i < casters; i++) {
        const float* q = p + i * 9;
        if (vrt_trace_triangle(o, d, q, q + 3, q + 6, best_t, &t)) {
            best_t = t;
            flags = 8;
            for (int k = 0; k < 3; k++)
                centre[k] = (q[k] + q[3 + k] + q[6 + k]) / 3.0f;
        }
    }
    if (flags >= 0)
        *t_out = best_t;
    return flags;
}
#include "vk_probes.inc" /* gi_method probes on Vulkan */
