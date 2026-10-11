/* Probe GI on Vulkan (included at the end of vk_rt.inc; probe_gi.inc has the
 * design and the shared parts). Two ways to trace the probes' rays:
 *
 * - Ray queries (probe_trace_rq.comp) against a TLAS of the room's opaque
 *   and cut-out BLASes (vk_rt.inc's, built when the room changes), without
 *   the movers: characters and props neither block nor bounce probe light
 *   (they would make the field flicker; their own contact and glow shadows
 *   stay with the per-pixel trace).
 * - Compute (probe_trace_bvh.comp) over ptgi.c's BVH of the room, built on
 *   the CPU once per room and uploaded: GPUs without ray tracing (and
 *   ROOD_PROBE_TRACE=bvh, to compare).
 *
 * Then, either way: state (relocation, classification, light scale) and
 * the blend into both atlases (and their tiles' borders). The present pass
 * binds the volume and atlases (pc_vk_probes_bind) and adds the field where
 * the path tracer's indirect light would go. */

static struct {
    int state; /* 0 untried, 1 ready, -1 unavailable */
    int bvh; /* trace with the compute BVH instead of ray queries */
    VkDescriptorSetLayout set;
    VkPipelineLayout layout;
    VkPipeline trace, blend, status, apply, surface;
    vk_image irradiance, depth, data, rays;
    vk_buffer state_buffer;
    int probes, columns, rows;
    /* Ray queries: the room-only TLAS. */
    VkAccelerationStructureKHR tlas;
    vk_buffer tlas_buffer;
    VkDeviceAddress tlas_room, tlas_cutout;
    /* Compute: the room's BVH (nodes; triangles in leaf order). */
    vk_buffer bvh_nodes, bvh_tris;
    int bvh_serial, bvh_root, bvh_triangles;
    unsigned seen_updates; /* vrt.updates when the lights were last gathered by the GI update */
    vk_slice volume; /* this frame's probe volume (the present samples with it) */
    /* Without a traced result: the field at GI resolution from the G-buffer
     * (probe_surface.comp), and a 1 x 1 neutral "extra" (no baked
     * visibility or glow light) for the present's GI lookup. */
    vk_image gi[3], extra;
    int gi_size[2];
} pvk = { .bvh_serial = -1 };

static int probe_vk_init(void)
{
    if (pvk.state)
        return pvk.state > 0;
    pvk.state = -1;
    const char* trace = getenv("ROOD_PROBE_TRACE");
    pvk.bvh = !vk.ray_query || (trace && !strcmp(trace, "bvh"));
    if (!pvk.bvh && !vrt_init())
        pvk.bvh = 1;
    const VkDescriptorType U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           T = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, B = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           A = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    /* 0 lights, 1 TLAS, 2 triangles, 3 VRAM, 4 rays, 5 irradiance, 6 depth, 7 data, 8 state, 9 movers,
     * 10 BVH nodes, 11 unused, 12 volume, 13-15 the atlases and data sampled (multi-bounce, lookups),
     * 16-17 the G-buffer's normals and depth (probe_surface.comp). */
    VkDescriptorType types[18] = { U, pvk.bvh ? B : A, B, T, S, S, S, S, B, B, B, B, U, T, T, T, T, T };
    pvk.set = vk_push_layout(types, 18);
    pvk.layout = vrt_layout(pvk.set, 0);
    pvk.trace = pvk.bvh ? vrt_compute(vk_spv_probe_trace_bvh_comp, vk_spv_probe_trace_bvh_comp_size, pvk.layout)
                        : vrt_compute(vk_spv_probe_trace_rq_comp, vk_spv_probe_trace_rq_comp_size, pvk.layout);
    pvk.blend = vrt_compute(vk_spv_probe_blend_comp, vk_spv_probe_blend_comp_size, pvk.layout);
    pvk.apply = vrt_compute(vk_spv_probe_apply_comp, vk_spv_probe_apply_comp_size, pvk.layout);
    pvk.surface = vrt_compute(vk_spv_probe_surface_comp, vk_spv_probe_surface_comp_size, pvk.layout);
    pvk.status = vrt_compute(vk_spv_probe_state_comp, vk_spv_probe_state_comp_size, pvk.layout);
    if (!pvk.trace || !pvk.blend || !pvk.status || !pvk.apply || !pvk.surface) {
        fprintf(stderr, "[gi] probe GI: Vulkan pipelines failed\n");
        return 0;
    }
    pvk.state = 1;
    fprintf(stderr, "[gi] probe GI ready (Vulkan %s)\n", pvk.bvh ? "compute over the room's BVH" : "ray queries");
    return 1;
}
static int probe_vk_image(vk_image* image, int w, int h, VkFormat format)
{
    vk_image_destroy_later(image);
    return vk_image_create(image, w, h, format,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
}
static int probe_vk_resources(void)
{
    if (pvk.probes != probe.probes || pvk.columns != probe.columns || pvk.rows != probe.rows) {
        pvk.probes = pvk.columns = pvk.rows = 0;
        if (!probe_vk_image(&pvk.irradiance, probe.columns * PROBE_IRRADIANCE_TILE, probe.rows * PROBE_IRRADIANCE_TILE,
                VK_FORMAT_R16G16B16A16_SFLOAT)
            || !probe_vk_image(&pvk.depth, probe.columns * PROBE_DEPTH_TILE, probe.rows * PROBE_DEPTH_TILE,
                VK_FORMAT_R16G16B16A16_SFLOAT)
            || !probe_vk_image(&pvk.data, probe.columns, probe.rows, VK_FORMAT_R32G32B32A32_SFLOAT)
            || !probe_vk_image(&pvk.rays, PROBE_MAX_RAYS, probe.probes, VK_FORMAT_R16G16B16A16_SFLOAT))
            return 0;
        vk_buffer_destroy_later(&pvk.state_buffer);
        if (!vk_buffer_create(&pvk.state_buffer, (VkDeviceSize)probe.probes * 16,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                0))
            return 0;
        pvk.probes = probe.probes, pvk.columns = probe.columns, pvk.rows = probe.rows;
        probe.reset = 1;
    }
    if (probe.reset) {
        /* A new room: clear the atlases, every probe in place and active. */
        vk_slice init = vk_arena((VkDeviceSize)probe.probes * 16, 16);
        if (!init.data)
            return 0;
        float* s = init.data;
        for (int i = 0; i < probe.probes; i++)
            s[i * 4] = s[i * 4 + 1] = s[i * 4 + 2] = 0, s[i * 4 + 3] = 1;
        vk_end_rendering();
        VkCommandBuffer cmd = vk_cmd();
        VkBufferCopy copy = { init.offset, 0, (VkDeviceSize)probe.probes * 16 };
        vkCmdCopyBuffer(cmd, init.buffer, pvk.state_buffer.buffer, 1, &copy);
        VkClearColorValue zero = { { 0, 0, 0, 0 } };
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vk_image* images[4] = { &pvk.irradiance, &pvk.depth, &pvk.data, &pvk.rays };
        for (int i = 0; i < 4; i++)
            vkCmdClearColorImage(cmd, images[i]->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        vk_barrier();
    }
    return 1;
}
/* Ray queries: a TLAS over the room's BLASes (vk_rt.inc vrt_room), rebuilt
 * when they change. */
static int probe_vk_tlas(void)
{
    if (!vrt_room())
        return 0;
    if (pvk.tlas && pvk.tlas_room == vrt.room_address && pvk.tlas_cutout == vrt.cutout_address)
        return 1;
    vk_slice instances = vk_arena(2 * sizeof(VkAccelerationStructureInstanceKHR), 16);
    if (!instances.data || !instances.address)
        return 0;
    VkAccelerationStructureInstanceKHR* in = instances.data;
    memset(in, 0, 2 * sizeof *in);
    uint32_t count = 0;
    for (int i = 0; i < 2; i++) {
        VkDeviceAddress address = i == 0 ? vrt.room_address : vrt.cutout_address;
        if (!address)
            continue;
        VkAccelerationStructureInstanceKHR* x = &in[count++];
        x->transform.matrix[0][0] = x->transform.matrix[1][1] = x->transform.matrix[2][2] = 1;
        x->instanceCustomIndex = i == 0 ? 0 : (uint32_t)vrt.room_opaque;
        x->mask = 1;
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
    uint32_t capacity = 2;
    vkGetAccelerationStructureBuildSizesKHR(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &capacity,
        &sizes);
    if (!vrt_structure(&pvk.tlas, &pvk.tlas_buffer, sizes.accelerationStructureSize,
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR))
        return 0;
    VkDeviceAddress scratch = vrt_scratch(sizes.buildScratchSize);
    if (!scratch)
        return 0;
    build.dstAccelerationStructure = pvk.tlas;
    build.scratchData.deviceAddress = scratch;
    VkAccelerationStructureBuildRangeInfoKHR range = { .primitiveCount = count };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vk_end_rendering();
    vkCmdBuildAccelerationStructuresKHR(vk_cmd(), 1, &build, &ranges);
    vk_barrier();
    pvk.tlas_room = vrt.room_address;
    pvk.tlas_cutout = vrt.cutout_address;
    return 1;
}
/* Compute: the room's triangles and BVH (ptgi.c's builder), uploaded once
 * per room capture. */
static int probe_vk_device_buffer(vk_buffer* b, const void* data, VkDeviceSize bytes)
{
    vk_buffer_destroy_later(b);
    if (!vk_buffer_create(b, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0))
        return 0;
    vk_slice staging = vk_arena(bytes, 16);
    if (!staging.data)
        return 0;
    memcpy(staging.data, data, (size_t)bytes);
    vk_end_rendering();
    VkBufferCopy copy = { staging.offset, 0, bytes };
    vkCmdCopyBuffer(vk_cmd(), staging.buffer, b->buffer, 1, &copy);
    vk_barrier();
    return 1;
}
static int probe_vk_bvh(void)
{
    int serial = pc_scene_serial(), room_count;
    if (serial == pvk.bvh_serial)
        return pvk.bvh_triangles > 0;
    const scene_triangle* room = pc_scene_triangle_data(&room_count);
    pvk.bvh_serial = serial;
    pvk.bvh_triangles = 0;
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
    if (!triangle_count)
        return 0;
    for (int i = 0; i < triangle_count; i++) {
        order[i] = i;
        for (int k = 0; k < 3; k++)
            centroids[i * 3 + k] = (triangles[i].a[k] + triangles[i].b[k] + triangles[i].c[k]) / 3.0f;
    }
    node_count = 0;
    int root = node_count++;
    build(root, 0, triangle_count);
    gpu_triangle* sorted = malloc((size_t)triangle_count * sizeof *sorted);
    if (!sorted)
        return 0;
    for (int i = 0; i < triangle_count; i++)
        sorted[i] = triangles[order[i]];
    int ok = probe_vk_device_buffer(&pvk.bvh_tris, sorted, (VkDeviceSize)triangle_count * sizeof *sorted)
          && probe_vk_device_buffer(&pvk.bvh_nodes, nodes, (VkDeviceSize)node_count * sizeof *nodes);
    free(sorted);
    if (!ok)
        return 0;
    pvk.bvh_root = root;
    pvk.bvh_triangles = triangle_count;
    fprintf(stderr, "[gi] probe BVH: %d room triangles, %d nodes\n", triangle_count, node_count);
    return 1;
}
static void probe_vk_dispatch(VkPipeline pipeline, vk_descriptors* d, uint32_t x, uint32_t y)
{
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk_descriptors_push(d, VK_PIPELINE_BIND_POINT_COMPUTE, pvk.layout);
    vkCmdDispatch(cmd, x, y, 1);
    vk_barrier();
}
/* Without a traced result: the GI-resolution images of probe_surface.comp. */
static int probe_vk_gi_images(int w, int h)
{
    if (!pvk.extra.image) {
        if (!vk_image_create(&pvk.extra, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            return 0;
        vk_end_rendering();
        VkClearColorValue neutral = { { 1, 0, 0, 0 } }; /* baked visibility 1, no glow light */
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(vk_cmd(), pvk.extra.image, VK_IMAGE_LAYOUT_GENERAL, &neutral, 1, &range);
        vk_barrier();
    }
    if (pvk.gi_size[0] == w && pvk.gi_size[1] == h)
        return 1;
    pvk.gi_size[0] = pvk.gi_size[1] = 0;
    VkFormat formats[3] = { VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
    for (int i = 0; i < 3; i++) {
        vk_image_destroy_later(&pvk.gi[i]);
        if (!vk_image_create(&pvk.gi[i], w, h, formats[i], VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
            return 0;
    }
    pvk.gi_size[0] = w, pvk.gi_size[1] = h;
    return 1;
}
/* The field at the display area's GI pixels from the G-buffer (no traced
 * result this frame). */
static int probe_vk_surface(vk_descriptors* d, int x, int y, int w, int h, int replay)
{
    float r[3][3], tr[3], hh, ox, oy, inverse[3][3];
    if (getenv("ROOD_PROBE_PER_PIXEL") || !pc_scene_camera_get(r, tr, &hh, &ox, &oy) || !pc_scene_camera_inverse(inverse))
        return 0;
    float res = pc_gi_resolution < 0.25f ? 0.25f : pc_gi_resolution > 4 ? 4 : pc_gi_resolution;
    int gw = (int)(w * res + 0.5f), gh = (int)(h * res + 0.5f);
    if (gw < 1 || gh < 1 || !probe_vk_gi_images(gw, gh))
        return 0;
    vk_slice ubo = vk_arena(sizeof(vrt_surface_ubo), vk.properties.limits.minUniformBufferOffsetAlignment);
    if (!ubo.data)
        return 0;
    vrt_surface_ubo* s = ubo.data;
    memset(s, 0, sizeof *s);
    s->rect[0] = x, s->rect[1] = y, s->rect[2] = w, s->rect[3] = h;
    s->scale[0] = pc_hw_scale;
    s->gi_scale[0] = (float)gw / w, s->gi_scale[1] = (float)gh / h;
    s->projection[0] = hh * (pc_gte_widescreen ? 0.75f : 1.0f), s->projection[1] = hh;
    s->screen_offset[0] = ox, s->screen_offset[1] = oy;
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) {
            s->inverse_rotation[col][row] = inverse[row][col];
            s->normal_to_world[col][row] = r[col][row];
        }
    s->camera_translation[0] = tr[0], s->camera_translation[1] = tr[1], s->camera_translation[2] = tr[2];
    for (int k = 0; k < 3; k++)
        s->eye[k] = -(inverse[k][0] * tr[0] + inverse[k][1] * tr[1] + inverse[k][2] * tr[2]);
    const vk_target* target = replay ? &vk_hw.replay : &vk_hw.live;
    vk_section("gi probe surface");
    vk_descriptors_buffer(d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ubo.buffer, ubo.offset, sizeof *s);
    for (int i = 0; i < 3; i++)
        vk_descriptors_storage_image(d, 4 + i, pvk.gi[i].view);
    vk_descriptors_buffer(d, 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, pvk.volume.buffer, pvk.volume.offset,
        sizeof(probe_volume_ubo));
    vk_descriptors_image(d, 16, target->normal.view, vk.nearest);
    vk_descriptors_image(d, 17, target->depth.view, vk.nearest);
    probe_vk_dispatch(pvk.surface, d, (uint32_t)(gw + 7) / 8, (uint32_t)(gh + 7) / 8);
    return 1;
}
/* The present (vk_present.c): update the field for this frame, before the
 * present pass, for the display area x, y, w, h of the live or replay
 * target. Returns 0 without a field this frame; 1 when the present should
 * sample it per display pixel (gi_on 64); 2 when it went into the
 * path-traced result's rgb (at GI resolution); 3 when it is at GI
 * resolution in its own images (pc_vk_probes_gi_bind; no traced result). */
int pc_vk_probes_update(int x, int y, int w, int h, int replay)
{
    probe.fresh = 0;
    pvk.volume.data = NULL;
    static int checked;
    const char* setting = getenv("ROOD_GI");
    if (!checked++ && setting && *setting == '0')
        pc_gi_enabled = pc_rt_shadows = pc_baked_shadows = 0; /* as ptgi.c's init() */
    if (!probe_wanted() || !probe_vk_init())
        return 0;
    int room_count;
    if (!pc_scene_triangle_data(&room_count) || !room_count)
        return 0;
    /* Lights and emitters: the path-traced update's when it ran this frame. */
    int traced = vrt.state > 0 && vrt.updates != pvk.seen_updates;
    if (!traced) {
        update_emitters();
        pc_scene_bounds(room_lo, room_hi);
        gather_lights();
    }
    pvk.seen_updates = vrt.updates;
    if (!probe_has_light())
        return 0;
    vk_section("gi probe scene");
    probe_fit();
    if (pvk.bvh ? !probe_vk_bvh() : !probe_vk_tlas())
        return 0;
    if (!probe_vk_resources())
        return 0;
    VkDeviceSize alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    vk_slice vs = vk_arena(sizeof(probe_volume_ubo), alignment), ts = vk_arena(sizeof(probe_trace_ubo), alignment);
    if (!vs.data || !ts.data)
        return 0;
    probe_begin(vs.data, ts.data, pvk.bvh ? pvk.bvh_root : -1);
    vk_end_rendering();
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ts.buffer, ts.offset, sizeof(probe_trace_ubo));
    if (pvk.bvh) {
        vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, pvk.bvh_tris.buffer, 0, VK_WHOLE_SIZE);
        vk_descriptors_buffer(&d, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, pvk.bvh_nodes.buffer, 0, VK_WHOLE_SIZE);
    } else {
        vk_descriptors_structure(&d, 1, pvk.tlas);
        vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0,
            (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof *triangles);
        vk_descriptors_buffer(&d, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0, sizeof(gpu_triangle));
    }
    vk_descriptors_image(&d, 3, vk_hw.vram.view, vk.nearest);
    vk_descriptors_storage_image(&d, 4, pvk.rays.view);
    vk_descriptors_storage_image(&d, 5, pvk.irradiance.view);
    vk_descriptors_storage_image(&d, 6, pvk.depth.view);
    vk_descriptors_storage_image(&d, 7, pvk.data.view);
    vk_descriptors_buffer(&d, 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, pvk.state_buffer.buffer, 0, VK_WHOLE_SIZE);
    vk_descriptors_buffer(&d, 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vs.buffer, vs.offset, sizeof(probe_volume_ubo));
    vk_descriptors_image(&d, 13, pvk.irradiance.view, vk.linear);
    vk_descriptors_image(&d, 14, pvk.depth.view, vk.linear);
    vk_descriptors_image(&d, 15, pvk.data.view, vk.nearest);
    vk_descriptors_image(&d, 16, vk_hw.dummy_color.view, vk.nearest);
    vk_descriptors_image(&d, 17, vk_hw.dummy_color.view, vk.nearest);
    const probe_volume_ubo* v = vs.data;
    vk_section("gi probe trace");
    probe_vk_dispatch(pvk.trace, &d, (uint32_t)(v->info[1] + 31) / 32, (uint32_t)probe.probes);
    vk_section("gi probe state");
    probe_vk_dispatch(pvk.status, &d, (uint32_t)(probe.probes + 63) / 64, 1);
    vk_section("gi probe blend");
    probe_vk_dispatch(pvk.blend, &d, (uint32_t)probe.probes, 1);
    /* The lookups use this volume, marked valid. */
    pvk.volume = vk_arena(sizeof(probe_volume_ubo), alignment);
    if (!pvk.volume.data)
        return 0;
    probe_volume_ubo* lookup = pvk.volume.data;
    memcpy(lookup, vs.data, sizeof *lookup);
    lookup->origin[3] = 1;
    memcpy(lookup->eye, vrt.eye, sizeof vrt.eye);
    probe_end();
    probe.backend = pvk.bvh ? 2 : 1;
    /* A path-traced result this frame (its shadows; the filter's output,
     * rebuilt every update): the field goes into its rgb at GI resolution
     * (far fewer lookups than per display pixel), else the present samples
     * it per pixel. */
    if (traced && (vrt.result == FILTER_A || vrt.result == FILTER_B) && vrt.size[0] > 0) {
        vk_section("gi probe apply");
        vk_descriptors_storage_image(&d, 4, vrt.images[POSITION].view);
        vk_descriptors_storage_image(&d, 5, vrt.images[NORMAL].view);
        vk_descriptors_storage_image(&d, 6, vrt.images[vrt.result].view);
        vk_descriptors_buffer(&d, 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, pvk.volume.buffer, pvk.volume.offset,
            sizeof(probe_volume_ubo));
        probe_vk_dispatch(pvk.apply, &d, (uint32_t)(vrt.size[0] + 7) / 8, (uint32_t)(vrt.size[1] + 7) / 8);
        probe.fresh = 2;
        return 2;
    }
    if (!traced && probe_vk_surface(&d, x, y, w, h, replay)) {
        probe.fresh = 3;
        return 3;
    }
    probe.fresh = 1;
    return 1;
}
/* After pc_vk_probes_update returned 3: the views for the present's GI
 * lookup (irradiance, positions, normals, a neutral extra) and their size. */
void pc_vk_probes_gi_bind(VkImageView views[4], int* w, int* h)
{
    views[0] = pvk.gi[2].view, views[1] = pvk.gi[0].view, views[2] = pvk.gi[1].view, views[3] = pvk.extra.view;
    *w = pvk.gi_size[0], *h = pvk.gi_size[1];
}
/* The present: bindings first..first + 3 (volume, irradiance, depth, data),
 * a zero volume and dummy images when the field is not sampled this frame. */
int pc_vk_probes_bind(vk_descriptors* d, int first)
{
    int on = probe.fresh == 1 && pvk.volume.data;
    probe.sampled = probe.fresh;
    probe.fresh = 0;
    vk_slice v = pvk.volume;
    if (!on) {
        v = vk_arena(sizeof(probe_volume_ubo), vk.properties.limits.minUniformBufferOffsetAlignment);
        if (v.data)
            memset(v.data, 0, sizeof(probe_volume_ubo));
    }
    vk_descriptors_buffer(d, first, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, v.buffer, v.offset, sizeof(probe_volume_ubo));
    VkImageView dummy = vk_hw.dummy_color.view;
    vk_descriptors_image(d, first + 1, on ? pvk.irradiance.view : dummy, vk.linear);
    vk_descriptors_image(d, first + 2, on ? pvk.depth.view : dummy, vk.linear);
    vk_descriptors_image(d, first + 3, on ? pvk.data.view : dummy, vk.nearest);
    return on;
}
/* Readback (probe_gi.inc probe_read): 0 irradiance, 1 depth, 2 data, 3 rays,
 * as RGBA float. */
static int probe_vk_read(int which, float* out)
{
    vk_image* images[4] = { &pvk.irradiance, &pvk.depth, &pvk.data, &pvk.rays };
    vk_image* im = images[which];
    if (!im->image)
        return 0;
    int w = im->width, h = im->height;
    if (which == 2)
        return vk_read_image(im, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, out, 16);
    uint16_t* half = malloc((size_t)w * h * 8);
    if (!half || !vk_read_image(im, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, half, 8)) {
        free(half);
        return 0;
    }
    for (size_t i = 0; i < (size_t)w * h * 4; i++)
        out[i] = vrt_half(half[i]);
    free(half);
    return 1;
}
