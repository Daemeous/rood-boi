/* Photo's presentation checkpoint: GPU-to-GPU copies, including G-buffer and
 * depth. Never reconstruct the last presented picture from guest VRAM. */
static vk_target photo_targets[2];
static vk_image photo_depth[2];
static int photo_width, photo_height;
void pc_vk_hw_photo_drop(void)
{
    destroy_target(&photo_targets[0]);
    destroy_target(&photo_targets[1]);
    vk_image_destroy_later(&photo_depth[0]);
    vk_image_destroy_later(&photo_depth[1]);
    photo_width = photo_height = 0;
}
static void photo_copy_target(vk_target* from, vk_target* to)
{
    vk_image* a[] = { &from->color, &from->normal, &from->albedo, &from->shade, &from->depth };
    vk_image* b[] = { &to->color, &to->normal, &to->albedo, &to->shade, &to->depth };
    for (int i = 0; i < 5; ++i)
        copy_image(a[i], b[i], 0, 0, 0, 0, photo_width, photo_height);
}
int pc_vk_hw_photo_mark(void)
{
    pc_vk_hw_photo_drop();
    VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!make_target(&photo_targets[0]) || !make_target(&photo_targets[1])
        || !vk_image_create(&photo_depth[0], vk_hw.width, vk_hw.height, vk.depth_format, usage)
        || !vk_image_create(&photo_depth[1], vk_hw.width, vk_hw.height, vk.depth_format, usage)) {
        pc_vk_hw_photo_drop();
        return 0;
    }
    photo_width = vk_hw.width;
    photo_height = vk_hw.height;
    vk_end_rendering();
    vk_barrier();
    photo_copy_target(&vk_hw.live, &photo_targets[0]);
    photo_copy_target(&vk_hw.replay, &photo_targets[1]);
    copy_image(&vk_hw.room_depth, &photo_depth[0], 0, 0, 0, 0, photo_width, photo_height);
    copy_image(&vk_hw.before, &photo_depth[1], 0, 0, 0, 0, photo_width, photo_height);
    vk_barrier();
    return 1;
}
int pc_vk_hw_photo_restore(void)
{
    if (!photo_width || photo_width != vk_hw.width || photo_height != vk_hw.height)
        return 0;
    vk_end_rendering();
    vk_barrier();
    photo_copy_target(&photo_targets[0], &vk_hw.live);
    photo_copy_target(&photo_targets[1], &vk_hw.replay);
    copy_image(&photo_depth[0], &vk_hw.room_depth, 0, 0, 0, 0, photo_width, photo_height);
    copy_image(&photo_depth[1], &vk_hw.before, 0, 0, 0, 0, photo_width, photo_height);
    vk_barrier();
    return 1;
}
