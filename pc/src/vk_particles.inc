/* The port's particles on Vulkan (included at the end of vk_rt.inc;
 * particles.h, pc/docs/Particles.md): one compute pass a present
 * (particles.comp, or particles_rt.comp with this frame's acceleration
 * structure, the path tracer's or built for it: shadow rays) evaluates every mote, lights it, writes its colour
 * (RGBA32F, 256 x 64) and splats it into the key image (R32_UINT, a texel
 * per game pixel of the display, cleared to none first); scaled.frag
 * particles_at composites them (bindings 21, 22). Perf section "particles". */
#include "particles.h"

static struct {
    int state; /* 0 untried, 1 ready, -1 unavailable */
    VkDescriptorSetLayout set, rt_set;
    VkPipelineLayout layout, rt_layout;
    VkPipeline plain, rt;
    vk_image keys, colours;
    int size[2], on, count;
} vpt;

static int vpt_init(void)
{
    if (vpt.state)
        return vpt.state > 0;
    vpt.state = -1;
    const VkDescriptorType U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           T = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, B = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           A = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    VkDescriptorType plain[7] = { U, U, U, U, B, S, S }; /* 1-3 unused without ray queries */
    VkDescriptorType rt[10] = { U, A, B, T, B, S, S, U, U, B }; /* rt_common.glsl: 1 tlas, 2 tris, 3 vram, 9 movers */
    vpt.set = vk_push_layout(plain, 7);
    vpt.layout = vrt_layout(vpt.set, 0);
    vpt.plain = vrt_compute(vk_spv_particles_comp, vk_spv_particles_comp_size, vpt.layout);
    if (vk.ray_query) {
        vpt.rt_set = vk_push_layout(rt, 10);
        vpt.rt_layout = vrt_layout(vpt.rt_set, 0);
        vpt.rt = vrt_compute(vk_spv_particles_rt_comp, vk_spv_particles_rt_comp_size, vpt.rt_layout);
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!vpt.plain
        || !vk_image_create(&vpt.colours, PC_PARTICLE_COLOUR_W, PC_PARTICLES_MAX / PC_PARTICLE_COLOUR_W,
            VK_FORMAT_R32G32B32A32_SFLOAT, usage)) {
        fprintf(stderr, "[particles] the Vulkan particle pass could not be created\n");
        return 0;
    }
    vpt.state = 1;
    return 1;
}

/* This present's motes for the display area w x h (game pixels); `traced`:
 * the path tracer built this frame's acceleration structure (else the pass
 * builds it: vrt_scene). Returns 1 when there is something to composite
 * (pc_vk_particles_bind). */
int pc_vk_particles_update(int x, int y, int w, int h, int traced)
{
    (void)x, (void)y;
    vpt.on = 0;
    if (w <= 0 || h <= 0)
        return 0;
    /* Dust Off or no emitter: nothing gathered beyond the room's emitters, nothing allocated or bound. */
    static pc_particles_gpu frame;
    static pc_light_gpu lights[PC_LIGHTS_MAX];
    int light_count = 0;
    int motes = pc_particles_gpu_frame(w, h, &frame, lights, PC_LIGHTS_MAX, &light_count);
    if (motes <= 0 || !vpt_init())
        return 0;
    VkDeviceSize alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    vk_slice us = vk_arena(sizeof(pc_particles_gpu), alignment);
    if (!us.data)
        return 0;
    pc_particles_gpu* pu = us.data;
    *pu = frame;
    if (vpt.size[0] != w || vpt.size[1] != h || !vpt.keys.image) {
        vk_image_destroy_later(&vpt.keys);
        if (!vk_image_create(&vpt.keys, w, h, VK_FORMAT_R32_UINT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            vpt.size[0] = vpt.size[1] = 0;
            return 0;
        }
        vpt.size[0] = w, vpt.size[1] = h;
    }
    VkDeviceSize light_bytes = (VkDeviceSize)(light_count ? light_count : 1) * sizeof(pc_light_gpu);
    vk_slice ls = vk_arena(light_bytes, vk.properties.limits.minStorageBufferOffsetAlignment);
    if (!ls.data)
        return 0;
    memcpy(ls.data, lights, (size_t)light_count * sizeof(pc_light_gpu));
    /* Shadow rays: against the path tracer's scene of this frame, else one built for the
     * motes (GI and the traced shadows off); without ray queries none, and a mote in light
     * only takes no light from a source that casts shadows (particles_main.glsl). */
    int rt = 0, room = 0;
    if (vpt.rt && !pc_particles_untraced) {
        if (traced && vrt.tlas)
            rt = 1;
        else if (pc_scene_triangle_data(&room) && room && vrt_init() && vrt_scene() && vrt.tlas)
            rt = 2;
    }
    pu->info2[3] = rt != 0;
    pc_particles_gpu_traced(rt);
    vk_end_rendering();
    vk_section("particles");
    VkClearColorValue none = { .uint32 = { PC_PARTICLE_NONE, 0, 0, 0 } };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(vk_cmd(), vpt.keys.image, VK_IMAGE_LAYOUT_GENERAL, &none, 1, &range);
    vk_barrier();
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, us.buffer, us.offset, sizeof(pc_particles_gpu));
    vk_descriptors_buffer(&d, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ls.buffer, ls.offset, light_bytes);
    vk_descriptors_storage_image(&d, 5, vpt.keys.view);
    vk_descriptors_storage_image(&d, 6, vpt.colours.view);
    VkCommandBuffer cmd = vk_cmd();
    if (rt) {
        vk_descriptors_structure(&d, 1, vrt.tlas);
        vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0,
            (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof *triangles);
        vk_descriptors_image(&d, 3, vk_hw.vram.view, vk.nearest);
        for (int b = 7; b <= 8; b++) /* declared, unused */
            vk_descriptors_buffer(&d, b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, us.buffer, us.offset, sizeof(pc_particles_gpu));
        vrt_bind_movers(&d, 9, -1);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.rt);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.rt_layout);
    } else {
        for (int b = 1; b <= 3; b++) /* declared, unused */
            vk_descriptors_buffer(&d, b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, us.buffer, us.offset, sizeof(pc_particles_gpu));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.plain);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.layout);
    }
    vkCmdDispatch(cmd, (uint32_t)(motes + 63) / 64, 1, 1);
    vk_barrier();
    vpt.on = 1, vpt.count = motes;
    return 1;
}
/* The present's images (scaled.frag bindings 21, 22). */
int pc_vk_particles_bind(VkImageView* keys, VkImageView* colours)
{
    if (!vpt.on)
        return 0;
    *keys = vpt.keys.view, *colours = vpt.colours.view;
    return 1;
}
/* RPC particles {motes}: the last pass's key image (w x h) and its motes' colours. */
int pc_vk_particles_read(uint32_t* keys, int w, int h, float* colours, int count)
{
    if (!vpt.on || vpt.size[0] != w || vpt.size[1] != h || count > vpt.count || count <= 0)
        return 0;
    int rows = (count + PC_PARTICLE_COLOUR_W - 1) / PC_PARTICLE_COLOUR_W;
    float* all = malloc((size_t)rows * PC_PARTICLE_COLOUR_W * 4 * sizeof(float));
    int ok = all && vk_read_image(&vpt.keys, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, keys, sizeof(uint32_t))
        && vk_read_image(&vpt.colours, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, PC_PARTICLE_COLOUR_W, rows, all, 4 * sizeof(float));
    if (ok)
        memcpy(colours, all, (size_t)count * 4 * sizeof(float));
    free(all);
    return ok;
}
