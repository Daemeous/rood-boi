/* The hand-placed fog volumes' own renderer on Vulkan (included at the end
 * of vk_rt.inc; atmosphere.h pc_fogvol_gpu; pc/shaders/vulkan/
 * fog_volume.glsl; pc/docs/plans/2026-10-14-fog-volume-renderer.md):
 *
 * 1. light grid (fogvol_light[_rt].comp): per volume a FV_GRID^3 grid of
 *    the transmittance toward its lights, the sun and the ambient term (its
 *    own shadow; with ray queries, when the path tracer built this frame's
 *    acceleration structure, the scene's too), half its cells a frame;
 * 2. march (fogvol_march.comp): per pixel of a lower-resolution image
 *    (fog_volume_resolution of the internal resolution) the volumes' light
 *    and transmittance up to the scene's depth;
 * 3. temporal (fogvol_temporal.comp): reprojected, depth-checked, clamped
 *    accumulation (two images, ping-pong) with a 3 x 3 depth-aware filter.
 *
 * The present pass (scaled.frag fogvol_at) upsamples the result with a
 * joint bilateral filter and composites it in linear light before the haze
 * and the post effects. */
#define FV_GRID 32
static struct {
    int state; /* 0 untried, 1 ready, -1 unavailable */
    VkDescriptorSetLayout light_set, light_rt_set, march_set, temporal_set;
    VkPipelineLayout light_layout, light_rt_layout, march_layout, temporal_layout;
    VkPipeline light, light_rt, march, temporal;
    vk_image grid[2]; /* a, b: FV_GRID x FV_GRID x (FV_GRID x PC_FOGVOL_MAX) */
    vk_image march_image, march_aux, history[2], history_aux[2];
    int size[2], current, valid, on, grids_new;
    unsigned frame;
    float scale[2]; /* march pixels per display pixel */
    float previous_rotation[3][4], previous_projection[4];
    unsigned keys[PC_FOGVOL_MAX][8], shape[PC_FOGVOL_MAX];
} vfv;
typedef struct {
    int info[4];
    int reset[8];
    float params[4];
    pc_medium_gpu medium;
    pc_fogvol_gpu fogvol;
} vfv_light_ubo;
typedef struct {
    int size[4], rect[4];
    float scale[4], projection[4], inverse_rotation[3][4], camera_translation[4];
    pc_medium_gpu medium;
    pc_fogvol_gpu fogvol;
} vfv_march_ubo;
typedef struct {
    int size[4];
    float params[4], projection[4], inverse_rotation[3][4], camera_translation[4];
    float previous_rotation[3][4], previous_projection[4];
} vfv_temporal_ubo;

static int vfv_init(void)
{
    if (vfv.state)
        return vfv.state > 0;
    vfv.state = -1;
    const VkDescriptorType U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           T = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, B = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           A = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    VkDescriptorType light[7] = { U, U, U, U, T, S, S }; /* 1-3 unused without ray queries */
    VkDescriptorType light_rt[10] = { U, A, B, T, T, S, S, U, U, B }; /* rt_common.glsl: 1 tlas, 2 tris, 3 vram, 9 movers */
    VkDescriptorType march[7] = { U, T, T, T, T, S, S };
    VkDescriptorType temporal[7] = { U, S, S, T, T, S, S };
    vfv.light_set = vk_push_layout(light, 7);
    vfv.march_set = vk_push_layout(march, 7);
    vfv.temporal_set = vk_push_layout(temporal, 7);
    vfv.light_layout = vrt_layout(vfv.light_set, 0);
    vfv.march_layout = vrt_layout(vfv.march_set, 0);
    vfv.temporal_layout = vrt_layout(vfv.temporal_set, 0);
    vfv.light = vrt_compute(vk_spv_fogvol_light_comp, vk_spv_fogvol_light_comp_size, vfv.light_layout);
    vfv.march = vrt_compute(vk_spv_fogvol_march_comp, vk_spv_fogvol_march_comp_size, vfv.march_layout);
    vfv.temporal = vrt_compute(vk_spv_fogvol_temporal_comp, vk_spv_fogvol_temporal_comp_size, vfv.temporal_layout);
    if (vk.ray_query) {
        vfv.light_rt_set = vk_push_layout(light_rt, 10);
        vfv.light_rt_layout = vrt_layout(vfv.light_rt_set, 0);
        vfv.light_rt = vrt_compute(vk_spv_fogvol_light_rt_comp, vk_spv_fogvol_light_rt_comp_size, vfv.light_rt_layout);
    }
    if (!vfv.light || !vfv.march || !vfv.temporal) {
        fprintf(stderr, "[fog] the fog volumes' renderer could not be created\n");
        return 0;
    }
    vfv.state = 1;
    return 1;
}
static void vfv_dispatch(VkPipeline pipeline, VkPipelineLayout layout, vk_descriptors* d, int x, int y, int z)
{
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk_descriptors_push(d, VK_PIPELINE_BIND_POINT_COMPUTE, layout);
    vkCmdDispatch(cmd, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    vk_barrier();
}
static int vfv_images(int w, int h)
{
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!vfv.grid[0].image) {
        for (int i = 0; i < 2; i++)
            if (!vk_image_create_3d(&vfv.grid[i], FV_GRID, FV_GRID, FV_GRID * PC_FOGVOL_MAX, VK_FORMAT_R16G16B16A16_SFLOAT,
                    usage))
                return 0;
        vfv.grids_new = 1;
    }
    if (vfv.size[0] == w && vfv.size[1] == h && vfv.march_image.image)
        return 1;
    vk_image* rgba[4] = { &vfv.march_image, &vfv.history[0], &vfv.history[1], NULL };
    vk_image* aux[4] = { &vfv.march_aux, &vfv.history_aux[0], &vfv.history_aux[1], NULL };
    for (int i = 0; rgba[i]; i++) {
        vk_image_destroy_later(rgba[i]);
        vk_image_destroy_later(aux[i]);
        if (!vk_image_create(rgba[i], w, h, VK_FORMAT_R16G16B16A16_SFLOAT, usage)
            || !vk_image_create(aux[i], w, h, VK_FORMAT_R32G32B32A32_SFLOAT, usage)) {
            vfv.size[0] = vfv.size[1] = 0;
            return 0;
        }
    }
    vfv.size[0] = w, vfv.size[1] = h;
    vfv.valid = 0;
    return 1;
}

/* This frame's volumes for the display area (x, y, w, h in VRAM) of the
 * live or replay target; `traced`: the path tracer built this frame's
 * acceleration structure (the scene shadows the volumes' lights). Returns 1
 * when there is a result to composite (pc_vk_fogvol_bind). */
int pc_vk_fogvol_update(int x, int y, int w, int h, int replay, int traced)
{
    vfv.on = 0;
    if (!pc_fog_volume_render || w <= 0 || h <= 0)
        return 0;
    static pc_medium_gpu medium;
    static pc_fogvol_gpu record;
    pc_fogvol_info info;
    float lo[3], hi[3];
    pc_scene_bounds(lo, hi);
    int count = pc_fogvol_gpu_get(&medium, &record, &info, lo, hi);
    if (count <= 0) {
        vfv.valid = 0, vfv.grids_new = 1; /* volumes coming back start fresh */
        return 0;
    }
    if (!vfv_init())
        return 0;
    float r[3][3], tr[3], hh, ox, oy, inverse[3][3];
    if (!pc_scene_camera_get(r, tr, &hh, &ox, &oy) || !pc_scene_camera_inverse(inverse))
        return 0;
    float hx = hh * (pc_gte_widescreen ? 0.75f : 1.0f);
    float res = pc_fog_volume_resolution < 0.125f ? 0.125f : pc_fog_volume_resolution > 1 ? 1 : pc_fog_volume_resolution;
    int mw = (int)(w * pc_hw_scale * res + 0.5f), mh = (int)(h * pc_hw_scale * res + 0.5f);
    mw = mw < 1 ? 1 : mw, mh = mh < 1 ? 1 : mh;
    if (!vfv_images(mw, mh))
        return 0;
    vfv.scale[0] = (float)mw / w, vfv.scale[1] = (float)mh / h;
    /* A new room (capture): no history from the last one. */
    static int serial = -1;
    if (pc_scene_serial() != serial) {
        serial = pc_scene_serial();
        vfv.valid = 0, vfv.grids_new = 1;
    }
    /* What restarts the light grid: a new grid, a volume's change, a light changing slot. */
    int reset_all = vfv.grids_new, resets[8] = { 0 };
    for (int v = 0; v < count; v++) {
        if (info.shape[v] != vfv.shape[v])
            resets[v] = 0xff, vfv.valid = 0; /* an edited volume shows at once (no history) */
        else
            for (int s = 0; s < 8; s++)
                if (info.keys[v][s] != vfv.keys[v][s])
                    resets[v] |= 1 << s;
    }
    memcpy(vfv.keys, info.keys, sizeof vfv.keys);
    memcpy(vfv.shape, info.shape, sizeof vfv.shape);
    for (int v = count; v < PC_FOGVOL_MAX; v++)
        vfv.shape[v] = 0; /* a volume appearing there later starts fresh */
    vfv.grids_new = 0;
    ++vfv.frame;
    record.v[0][2] = (float)(vfv.frame & 0xffff);
    VkDeviceSize alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    vk_slice ls = vk_arena(sizeof(vfv_light_ubo), alignment), ms = vk_arena(sizeof(vfv_march_ubo), alignment),
             ts = vk_arena(sizeof(vfv_temporal_ubo), alignment);
    if (!ls.data || !ms.data || !ts.data)
        return 0;
    VkImageView medium_view = pc_vk_medium_texture(); /* before any pass: may upload */
    vk_end_rendering();
    vk_section("fog grid");
    /* 1: the light grid */
    vfv_light_ubo* lu = ls.data;
    memset(lu, 0, sizeof *lu);
    lu->info[0] = FV_GRID, lu->info[1] = count, lu->info[2] = (int)vfv.frame, lu->info[3] = reset_all;
    memcpy(lu->reset, resets, sizeof lu->reset);
    lu->params[0] = 0.35f;
    int rt = traced && vfv.light_rt && pc_fog_volume_shadows && vrt.tlas;
    lu->params[1] = rt ? 1.0f : 0.0f;
    lu->medium = medium;
    lu->fogvol = record;
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ls.buffer, ls.offset, sizeof *lu);
    vk_descriptors_image(&d, 4, medium_view, vk.linear);
    vk_descriptors_storage_image(&d, 5, vfv.grid[0].view);
    vk_descriptors_storage_image(&d, 6, vfv.grid[1].view);
    int groups = FV_GRID / 4;
    if (rt) {
        vk_descriptors_structure(&d, 1, vrt.tlas);
        vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0,
            (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof *triangles);
        vk_descriptors_image(&d, 3, vk_hw.vram.view, vk.nearest);
        vrt_bind_movers(&d, 9, -1);
        for (int b = 7; b <= 8; b++) /* declared, unused */
            vk_descriptors_buffer(&d, b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ls.buffer, ls.offset, sizeof *lu);
        vfv_dispatch(vfv.light_rt, vfv.light_rt_layout, &d, groups, groups, groups * count);
    } else {
        /* bindings 1-3 are declared but unused: bind the light grid's UBO there */
        for (int b = 1; b <= 3; b++)
            vk_descriptors_buffer(&d, b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ls.buffer, ls.offset, sizeof *lu);
        vfv_dispatch(vfv.light, vfv.light_layout, &d, groups, groups, groups * count);
    }
    /* 2: the march */
    vk_section("fog march");
    const vk_target* target = replay ? &vk_hw.replay : &vk_hw.live;
    vfv_march_ubo* mu = ms.data;
    memset(mu, 0, sizeof *mu);
    mu->size[0] = mw, mu->size[1] = mh, mu->size[2] = (int)vfv.frame;
    mu->size[3] = pc_fog_volume_steps < 4 ? 4 : pc_fog_volume_steps > 256 ? 256 : pc_fog_volume_steps;
    mu->rect[0] = x, mu->rect[1] = y, mu->rect[2] = w, mu->rect[3] = h;
    mu->scale[0] = (float)pc_hw_scale, mu->scale[1] = vfv.scale[0], mu->scale[2] = vfv.scale[1], mu->scale[3] = FV_GRID;
    mu->projection[0] = hx, mu->projection[1] = hh, mu->projection[2] = ox, mu->projection[3] = oy;
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            mu->inverse_rotation[col][row] = inverse[row][col];
    memcpy(mu->camera_translation, tr, sizeof tr);
    mu->medium = medium;
    mu->fogvol = record;
    memset(&d, 0, sizeof d);
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ms.buffer, ms.offset, sizeof *mu);
    vk_descriptors_image(&d, 1, target->depth.view, vk.nearest);
    vk_descriptors_image(&d, 2, medium_view, vk.linear);
    vk_descriptors_image(&d, 3, vfv.grid[0].view, vk.linear);
    vk_descriptors_image(&d, 4, vfv.grid[1].view, vk.linear);
    vk_descriptors_storage_image(&d, 5, vfv.march_image.view);
    vk_descriptors_storage_image(&d, 6, vfv.march_aux.view);
    vfv_dispatch(vfv.march, vfv.march_layout, &d, (mw + 7) / 8, (mh + 7) / 8, 1);
    /* 3: temporal */
    vk_section("fog temporal");
    vfv_temporal_ubo* tu = ts.data;
    memset(tu, 0, sizeof *tu);
    tu->size[0] = mw, tu->size[1] = mh, tu->size[2] = vfv.valid;
    tu->params[0] = pc_fog_volume_history < 0.01f ? 0.01f : pc_fog_volume_history > 1 ? 1 : pc_fog_volume_history;
    tu->params[1] = vfv.scale[0], tu->params[2] = vfv.scale[1], tu->params[3] = 64;
    memcpy(tu->projection, mu->projection, sizeof tu->projection);
    memcpy(tu->inverse_rotation, mu->inverse_rotation, sizeof tu->inverse_rotation);
    memcpy(tu->camera_translation, tr, sizeof tr);
    memcpy(tu->previous_rotation, vfv.previous_rotation, sizeof tu->previous_rotation);
    memcpy(tu->previous_projection, vfv.previous_projection, sizeof tu->previous_projection);
    int out = vfv.current ^ 1;
    memset(&d, 0, sizeof d);
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ts.buffer, ts.offset, sizeof *tu);
    vk_descriptors_storage_image(&d, 1, vfv.march_image.view);
    vk_descriptors_storage_image(&d, 2, vfv.march_aux.view);
    vk_descriptors_image(&d, 3, vfv.history[vfv.current].view, vk.linear);
    vk_descriptors_image(&d, 4, vfv.history_aux[vfv.current].view, vk.nearest);
    vk_descriptors_storage_image(&d, 5, vfv.history[out].view);
    vk_descriptors_storage_image(&d, 6, vfv.history_aux[out].view);
    vfv_dispatch(vfv.temporal, vfv.temporal_layout, &d, (mw + 7) / 8, (mh + 7) / 8, 1);
    for (int row = 0; row < 3; row++) {
        for (int k = 0; k < 3; k++)
            vfv.previous_rotation[row][k] = r[row][k];
        vfv.previous_rotation[row][3] = tr[row];
    }
    memcpy(vfv.previous_projection, mu->projection, sizeof vfv.previous_projection);
    vfv.current = out;
    vfv.valid = 1;
    vfv.on = 1;
    return 1;
}
/* The result for the present pass: its light (rgb, a transmittance) and
 * depth images, their size, march pixels per display pixel. */
int pc_vk_fogvol_bind(VkImageView* light, VkImageView* aux, int size[2], float scale[2])
{
    if (!vfv.on)
        return 0;
    *light = vfv.history[vfv.current].view;
    *aux = vfv.history_aux[vfv.current].view;
    size[0] = vfv.size[0], size[1] = vfv.size[1];
    scale[0] = vfv.scale[0], scale[1] = vfv.scale[1];
    return 1;
}
