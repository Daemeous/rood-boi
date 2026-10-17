/* The post stack on Vulkan (vk_present.c includes this; post.h,
 * pc/docs/Post_Processing.md): the grade pass (post_grade.frag), the
 * Gaussian bloom pyramid and the meter (bloom.frag), and the convolution
 * bloom: the second pyramid level padded into an n x n domain
 * (bloom.frag mode 3), its 2D FFT, multiplied by the kernel's spectrum,
 * transformed back (bloom_fft_*.comp). The kernel (post_profile.cpp
 * pc_post_kernel_build) is uploaded and transformed once per change. Made
 * on first use: with every effect neutral nothing here runs. */

#define FFT_FORMAT VK_FORMAT_R32G32B32A32_SFLOAT
static VkDescriptorSetLayout grade_set, storage2_set;
static VkPipelineLayout grade_layout, bloom_layout, fft_layout;
static VkPipeline grade_to_view, grade_to_window, bloom_pipeline, bloom_domain_pipeline, fft_rows_pipeline,
    fft_cols_pipeline, fft_inverse_pipeline;
static vk_image post_black, post_lut_image;
static unsigned post_lut_serial = ~0u;
static int post_lut_size;
#define BLOOM_LEVELS 8
static vk_image bloom_down[BLOOM_LEVELS], bloom_up[BLOOM_LEVELS], meter_image;
static vk_image fft_domain, fft_data, fft_kernel, fft_kernel_spectrum, fft_result;
static unsigned fft_kernel_hash;
typedef struct {
    float p0[4], p1[4], p2[4];
} bloom_push;

static int post_vk_ready(void)
{
    static int state;
    if (state)
        return state > 0;
    state = -1;
    VkDescriptorType grade_types[4] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
    VkDescriptorType storage[2] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
    grade_set = vk_push_layout(grade_types, 4);
    storage2_set = vk_push_layout(storage, 2);
    grade_layout = make_layout(grade_set, 16);
    bloom_layout = make_layout(triple_set, sizeof(bloom_push));
    {
        VkPushConstantRange range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
        VkPipelineLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &storage2_set,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range };
        vk_check(vkCreatePipelineLayout(vk.device, &info, NULL, &fft_layout), "vkCreatePipelineLayout");
    }
    VkShaderModule vs = vk_shader(vk_spv_fullscreen_vert, vk_spv_fullscreen_vert_size),
                   grade = vk_shader(vk_spv_post_grade_frag, vk_spv_post_grade_frag_size),
                   bloom = vk_shader(vk_spv_bloom_frag, vk_spv_bloom_frag_size),
                   rows = vk_shader(vk_spv_bloom_fft_rows_comp, vk_spv_bloom_fft_rows_comp_size),
                   cols = vk_shader(vk_spv_bloom_fft_cols_comp, vk_spv_bloom_fft_cols_comp_size),
                   inverse = vk_shader(vk_spv_bloom_fft_rows_inverse_comp, vk_spv_bloom_fft_rows_inverse_comp_size);
    grade_to_view = fullscreen(vs, grade, grade_layout, VIEW_FORMAT);
    grade_to_window = fullscreen(vs, grade, grade_layout, WINDOW_FORMAT);
    bloom_pipeline = fullscreen(vs, bloom, bloom_layout, VIEW_FORMAT);
    bloom_domain_pipeline = fullscreen(vs, bloom, bloom_layout, FFT_FORMAT);
    fft_rows_pipeline = vk_compute_pipeline(rows, fft_layout);
    fft_cols_pipeline = vk_compute_pipeline(cols, fft_layout);
    fft_inverse_pipeline = vk_compute_pipeline(inverse, fft_layout);
    VkShaderModule modules[6] = { vs, grade, bloom, rows, cols, inverse };
    for (int i = 0; i < 6; i++)
        if (modules[i])
            vkDestroyShaderModule(vk.device, modules[i], NULL);
    static const unsigned char black[4] = { 0, 0, 0, 255 };
    if (!grade_to_view || !grade_to_window || !bloom_pipeline || !smaa_upload(&post_black, VK_FORMAT_R8G8B8A8_UNORM, black, 1, 1, 4)
        || !smaa_upload(&post_lut_image, VK_FORMAT_R8G8B8A8_UNORM, black, 1, 1, 4)) {
        fprintf(stderr, "[vk] post stack unavailable (its pipelines did not build)\n");
        return 0;
    }
    /* The convolution bloom: compute and a 32-bit float attachment. */
    pc_post_fft_available = bloom_domain_pipeline && fft_rows_pipeline && fft_cols_pipeline && fft_inverse_pipeline;
    state = 1;
    return 1;
}

static int ensure_post_image(vk_image* image, int w, int h, VkFormat format, VkImageUsageFlags usage)
{
    if (image->image && image->width == w && image->height == h)
        return 1;
    vk_image_destroy_later(image);
    return vk_image_create(image, w, h, format, usage);
}
#define POST_USAGE (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)

static void bloom_pass(const vk_image* out, VkPipeline pipeline, const vk_image* source, const vk_image* level,
    const bloom_push* push)
{
    begin_single(out, 0, 0, out->width, out->height);
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_descriptors d = { 0 };
    vk_descriptors_image(&d, 0, source->view, vk.linear);
    vk_descriptors_image(&d, 1, (level ? level : &post_black)->view, vk.linear);
    vk_descriptors_image(&d, 2, post_black.view, vk.linear);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_layout);
    vkCmdPushConstants(cmd, bloom_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof *push,
        push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

/* The pyramid down `levels` levels from `source` (w x h); returns how many were made. */
static int bloom_pyramid_down(const pc_post_frame* pf, const vk_image* source, int w, int h, int levels)
{
    for (int i = 0; i < levels; i++) {
        int lw = w >> (i + 1), lh = h >> (i + 1);
        if (lw < 2 || lh < 2)
            return i;
        if (!ensure_post_image(&bloom_down[i], lw, lh, VIEW_FORMAT, POST_USAGE)
            || !ensure_post_image(&bloom_up[i], lw, lh, VIEW_FORMAT, POST_USAGE))
            return i;
        const vk_image* from = i ? &bloom_down[i - 1] : source;
        int fw = i ? w >> i : w, fh = i ? h >> i : h;
        bloom_push p = { { 1.0f / fw, 1.0f / fh, i ? 1.0f : 0.0f, 1 },
            { pf->exposure_scale, pf->bloom_threshold, pf->bloom_knee, 1 }, { 0, 0, 1, 1 } };
        bloom_pass(&bloom_down[i], bloom_pipeline, from, NULL, &p);
    }
    return levels;
}

/* The Gaussian bloom: the pyramid down and back up; the result (half size) or NULL. */
static const vk_image* vk_bloom_gaussian(const pc_post_frame* pf, const vk_image* source, int w, int h)
{
    int levels = 5 + (int)lrintf(log2f(pf->bloom_size > 0.05f ? pf->bloom_size : 0.05f) * 1.5f);
    levels = levels < 2 ? 2 : levels > BLOOM_LEVELS ? BLOOM_LEVELS : levels;
    levels = bloom_pyramid_down(pf, source, w, h, levels);
    if (levels < 1)
        return NULL;
    const vk_image* below = &bloom_down[levels - 1];
    for (int i = levels - 2; i >= 0; i--) {
        bloom_push p = { { 1.0f / below->width, 1.0f / below->height, 2, i == 0 ? 1.0f / levels : 1.0f },
            { 0, 0, 0, 1 }, { 0, 0, 1, 1 } };
        bloom_pass(&bloom_up[i], bloom_pipeline, below, &bloom_down[i], &p);
        below = &bloom_up[i];
    }
    return levels > 1 ? &bloom_up[0] : &bloom_down[0];
}

static void fft_dispatch(VkPipeline pipeline, const vk_image* a, const vk_image* b, int n, int mode, float scale)
{
    vk_end_rendering();
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk_descriptors d = { 0 };
    vk_descriptors_storage_image(&d, 0, a->view);
    vk_descriptors_storage_image(&d, 1, b->view);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_COMPUTE, fft_layout);
    struct {
        int n, log2n, mode;
        float scale;
    } push = { n, n == 512 ? 9 : 8, mode, scale };
    vkCmdPushConstants(cmd, fft_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    vkCmdDispatch(cmd, (uint32_t)n, 1, 1);
    vk_barrier();
}

/* The convolution bloom; `uv` gets the result's mapping from the view's uv. NULL when unavailable. */
static const vk_image* vk_bloom_convolution(const pc_post_frame* pf, const vk_image* source, int w, int h, float uv[4])
{
    int n = pc_post_fft_size >= 512 ? 512 : 256;
    VkImageUsageFlags storage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (bloom_pyramid_down(pf, source, w, h, 2) < 2
        || !ensure_post_image(&fft_domain, n, n, FFT_FORMAT, storage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        || !ensure_post_image(&fft_data, n, n, FFT_FORMAT, storage)
        || !ensure_post_image(&fft_kernel, n, n, FFT_FORMAT, storage | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        || !ensure_post_image(&fft_kernel_spectrum, n, n, FFT_FORMAT, storage)
        || !ensure_post_image(&fft_result, n, n, VK_FORMAT_R16G16B16A16_SFLOAT, storage))
        return NULL;
    /* The picture fills half the domain's side (its long side), centred: the kernel's reach (at most the
     * padding) never wraps light from one edge onto the other. */
    int picture = n / 2;
    float fw = (float)picture, fh = (float)picture;
    if (w >= h)
        fh = picture * (float)h / w;
    else
        fw = picture * (float)w / h;
    float off_x = (n - fw) * 0.5f, off_y = (n - fh) * 0.5f;
    uv[0] = fw / n, uv[1] = fh / n, uv[2] = off_x / n, uv[3] = off_y / n;
    unsigned hash = pc_post_kernel_hash(n, picture);
    if (hash != fft_kernel_hash) { /* the kernel and its spectrum, once per change */
        float* k = malloc((size_t)n * n * sizeof(float));
        vk_slice staging = k ? vk_arena((VkDeviceSize)n * n * 16, 16) : (vk_slice){ 0 };
        if (!staging.data) {
            free(k);
            return NULL;
        }
        pc_post_kernel_build(k, n, picture);
        float* texels = staging.data;
        for (size_t i = 0; i < (size_t)n * n; i++)
            texels[i * 4] = k[i], texels[i * 4 + 1] = texels[i * 4 + 2] = texels[i * 4 + 3] = 0;
        free(k);
        vk_end_rendering();
        VkBufferImageCopy region = { .bufferOffset = staging.offset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { (uint32_t)n, (uint32_t)n, 1 } };
        vkCmdCopyBufferToImage(vk_cmd(), staging.buffer, fft_kernel.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        vk_barrier();
        fft_dispatch(fft_rows_pipeline, &fft_kernel, &fft_data, n, 0, 1);
        fft_dispatch(fft_cols_pipeline, &fft_data, &fft_kernel_spectrum, n, 0, 1);
        fft_kernel_hash = hash;
    }
    vk_section("bloom fft");
    bloom_push p = { { 0, 0, 3, 1 }, { 0, 0, 0, 0 }, { off_x / n, off_y / n, fw / n, fh / n } };
    bloom_pass(&fft_domain, bloom_domain_pipeline, &bloom_down[1], NULL, &p);
    fft_dispatch(fft_rows_pipeline, &fft_domain, &fft_data, n, 0, 1);
    fft_dispatch(fft_cols_pipeline, &fft_data, &fft_kernel_spectrum, n, 1, 1);
    fft_dispatch(fft_inverse_pipeline, &fft_data, &fft_result, n, 0, 1.0f / ((float)n * n));
    return &fft_result;
}

/* Auto exposure: meter into a small image, copied to a host buffer read when the frame is done. */
typedef struct {
    vk_buffer buffer;
} meter_readback;
static void meter_done(void* context)
{
    meter_readback* m = context;
    unsigned short* h = m->buffer.mapped; /* RGBA16F */
    static float texels[PC_POST_METER_W * PC_POST_METER_H * 2];
    for (int i = 0; i < PC_POST_METER_W * PC_POST_METER_H; i++)
        for (int k = 0; k < 2; k++) {
            unsigned short x = h[i * 4 + k];
            int e = (x >> 10) & 31, f = x & 1023;
            float v = e == 0 ? ldexpf((float)f, -24) : e == 31 ? 0.0f : ldexpf((float)(f | 1024), e - 25);
            texels[i * 2 + k] = x & 0x8000 ? -v : v;
        }
    pc_post_meter_reduce(texels, PC_POST_METER_W, PC_POST_METER_H, 2);
    vk_buffer_destroy(&m->buffer);
    free(m);
}
static void vk_meter(const vk_image* source)
{
    if (!ensure_post_image(&meter_image, PC_POST_METER_W, PC_POST_METER_H, VIEW_FORMAT,
            POST_USAGE | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        return;
    bloom_push p = { { 1.0f / PC_POST_METER_W, 1.0f / PC_POST_METER_H, 4, 1 }, { 0 }, { 0 } };
    bloom_pass(&meter_image, bloom_pipeline, source, NULL, &p);
    meter_readback* m = calloc(1, sizeof *m);
    if (!m || !vk_buffer_create(&m->buffer, PC_POST_METER_W * PC_POST_METER_H * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 2)) {
        free(m);
        return;
    }
    vk_end_rendering();
    VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { PC_POST_METER_W, PC_POST_METER_H, 1 } };
    vkCmdCopyImageToBuffer(vk_cmd(), meter_image.image, VK_IMAGE_LAYOUT_GENERAL, m->buffer.buffer, 1, &region);
    vk_barrier();
    vk_on_frame_done(meter_done, m);
}

static void vk_lut_update(void)
{
    int size;
    unsigned serial;
    const unsigned char* strip = pc_post_lut(&size, &serial);
    if (serial == post_lut_serial)
        return;
    post_lut_serial = serial;
    if (!strip)
        return;
    vk_image_destroy_later(&post_lut_image);
    if (!smaa_upload(&post_lut_image, VK_FORMAT_R8G8B8A8_UNORM, strip, size * size, size, 4))
        fprintf(stderr, "[vk] LUT upload failed\n");
    post_lut_size = size;
}

/* The grade pass from `source` (the lit image, w x h) into the stage stage_begin opened: bloom and meter
 * first (their own passes), then the grade (origin: the output's corner; last: the window). */
static void vk_grade(const pc_post_frame* pf, const vk_image* source, int w, int h, int view_w, int view_h,
    const vk_image* out, int origin_x, int origin_y, int last)
{
    const vk_image* bloom = NULL;
    float bloom_uv[4] = { 1, 1, 0, 0 };
    vk_section("post");
    if (pf->bloom == PC_BLOOM_CONVOLUTION)
        bloom = vk_bloom_convolution(pf, source, w, h, bloom_uv);
    if (pf->bloom && !bloom) {
        bloom_uv[0] = bloom_uv[1] = 1, bloom_uv[2] = bloom_uv[3] = 0;
        bloom = vk_bloom_gaussian(pf, source, w, h);
        pc_post_active_bloom = bloom ? PC_BLOOM_GAUSSIAN : 0;
    } else
        pc_post_active_bloom = bloom ? PC_BLOOM_CONVOLUTION : 0;
    if (pf->meter)
        vk_meter(source);
    if (pf->lut)
        vk_lut_update();
    vk_slice ubo = vk_arena(sizeof(pc_post_gpu), vk.properties.limits.minUniformBufferOffsetAlignment);
    if (!ubo.data)
        return;
    pc_post_gpu_pack(ubo.data, w, h, view_w, view_h, bloom_uv);
    if (!bloom)
        ((pc_post_gpu*)ubo.data)->v[0][1] = 0;
    vk_section("grade");
    begin_single(out, origin_x, origin_y, w, h);
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, last ? grade_to_window : grade_to_view);
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ubo.buffer, ubo.offset, sizeof(pc_post_gpu));
    vk_descriptors_image(&d, 1, source->view, vk.linear);
    vk_descriptors_image(&d, 2, (bloom ? bloom : &post_black)->view, vk.linear);
    vk_descriptors_image(&d, 3, (pf->lut ? &post_lut_image : &post_black)->view, vk.linear);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, grade_layout);
    int push[4] = { origin_x, origin_y, last, 0 };
    vkCmdPushConstants(cmd, grade_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

/* ---- depth of field (dof.frag, dof.glsl): half-size layers, then the composite into the chain ---- */
static VkPipelineLayout dof_layout;
static VkPipeline dof_pipeline, dof_to_window;
static vk_image dof_half, dof_background, dof_foreground, dof_tiles, dof_dilated, dof_probe_image;
static pc_post_dof_gpu dof_params;
static int dof_vk_ready(void)
{
    static int state;
    if (state)
        return state > 0;
    state = -1;
    dof_layout = make_layout(triple_set, sizeof(pc_post_dof_gpu));
    VkShaderModule vs = vk_shader(vk_spv_fullscreen_vert, vk_spv_fullscreen_vert_size),
                   fs = vk_shader(vk_spv_dof_frag, vk_spv_dof_frag_size);
    dof_pipeline = fullscreen(vs, fs, dof_layout, VIEW_FORMAT);
    dof_to_window = fullscreen(vs, fs, dof_layout, WINDOW_FORMAT);
    if (vs)
        vkDestroyShaderModule(vk.device, vs, NULL);
    if (fs)
        vkDestroyShaderModule(vk.device, fs, NULL);
    if (!dof_pipeline || !dof_to_window) {
        fprintf(stderr, "[vk] depth of field unavailable (its pipelines did not build)\n");
        return 0;
    }
    state = 1;
    return 1;
}

/* One pass into the rendering begun (w x h): the pass, its main source's size, its sources. */
static void dof_draw(VkPipeline pipeline, pc_post_dof_gpu* g, int pass, int w, int h, int src_w, int src_h,
    VkImageView a, const vk_image* b, const vk_image* c)
{
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_descriptors d = { 0 };
    vk_descriptors_image(&d, 0, a, vk.nearest);
    vk_descriptors_image(&d, 1, (b ? b : &post_black)->view, vk.linear);
    vk_descriptors_image(&d, 2, (c ? c : &post_black)->view, vk.linear);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, dof_layout);
    g->v[0][0] = (float)w, g->v[0][1] = (float)h, g->v[0][2] = (float)src_w, g->v[0][3] = (float)src_h;
    g->v[1][0] = (float)pass;
    vkCmdPushConstants(cmd, dof_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof *g, g);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
static void dof_pass(const vk_image* out, int pass, int src_w, int src_h, const vk_image* a, const vk_image* b)
{
    begin_single(out, 0, 0, out->width, out->height);
    dof_draw(dof_pipeline, &dof_params, pass, out->width, out->height, src_w, src_h, a->view, b, NULL);
}

/* The half-size layers from the lit image `source` (w x h; alpha the signed blur radius). Returns 0 when
 * they could not be made (the composite then passes the picture through), 1, or 2 with a foreground. */
static int vk_dof_layers(const pc_post_frame* pf, const vk_image* source, int w, int h)
{
    pc_post_dof_pack(&dof_params, w, h);
    if (!post_vk_ready() || !dof_vk_ready())
        return 0;
    int hw = (w + 1) / 2, hh = (h + 1) / 2;
    int tw = (hw + PC_DOF_TILE - 1) / PC_DOF_TILE, th = (hh + PC_DOF_TILE - 1) / PC_DOF_TILE;
    if (!ensure_post_image(&dof_half, hw, hh, VIEW_FORMAT, POST_USAGE)
        || !ensure_post_image(&dof_background, hw, hh, VIEW_FORMAT, POST_USAGE))
        return 0;
    int fg = pf->dof_near && ensure_post_image(&dof_foreground, hw, hh, VIEW_FORMAT, POST_USAGE)
        && ensure_post_image(&dof_tiles, tw, th, VIEW_FORMAT, POST_USAGE)
        && ensure_post_image(&dof_dilated, tw, th, VIEW_FORMAT, POST_USAGE);
    dof_pass(&dof_half, PC_DOF_PREFILTER, w, h, source, NULL);
    if (fg) {
        dof_pass(&dof_tiles, PC_DOF_TILES, hw, hh, &dof_half, NULL);
        dof_pass(&dof_dilated, PC_DOF_DILATE, tw, th, &dof_tiles, NULL);
    }
    dof_pass(&dof_background, PC_DOF_FAR, hw, hh, &dof_half, NULL);
    if (fg)
        dof_pass(&dof_foreground, PC_DOF_NEAR, hw, hh, &dof_half, &dof_dilated);
    return fg ? 2 : 1;
}
/* The composite into the stage begun: the view (to_view) or the window's view. Without layers the
 * picture passes through (2D/UI marked for a grade pass). */
static void vk_dof_composite(const vk_image* source, int w, int h, int to_view, int keep_mask, int layers)
{
    pc_post_dof_gpu* g = &dof_params;
    if (!dof_vk_ready())
        return;
    g->v[5][0] = keep_mask ? 1.0f : 0.0f;
    g->v[5][1] = layers == 2 ? 1.0f : 0.0f;
    g->v[5][2] = layers ? 0.0f : 1.0f;
    dof_draw(to_view ? dof_pipeline : dof_to_window, g, PC_DOF_COMPOSITE, w, h, w, h, source->view,
        layers ? &dof_background : NULL, layers == 2 ? &dof_foreground : NULL);
}

/* The focus probe: the view depth around the focus point (the depth target `depth`, the display area
 * x, y, w, h in VRAM), read back when the frame is done. */
typedef struct {
    vk_buffer buffer;
} probe_readback;
static float half_float(unsigned short x)
{
    int e = (x >> 10) & 31, f = x & 1023;
    float v = e == 0 ? ldexpf((float)f, -24) : e == 31 ? 0.0f : ldexpf((float)(f | 1024), e - 25);
    return x & 0x8000 ? -v : v;
}
static void probe_done(void* context)
{
    probe_readback* m = context;
    unsigned short* h = m->buffer.mapped; /* RGBA16F */
    pc_post_focus_result(half_float(h[0]), half_float(h[1]));
    vk_buffer_destroy(&m->buffer);
    free(m);
}
static void vk_dof_probe(const pc_post_frame* pf, VkImageView depth, int x, int y, int w, int h)
{
    if (!post_vk_ready() || !dof_vk_ready()
        || !ensure_post_image(&dof_probe_image, 1, 1, VIEW_FORMAT, POST_USAGE | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        return;
    probe_readback* m = calloc(1, sizeof *m);
    if (!m || !vk_buffer_create(&m->buffer, 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 2)) {
        free(m);
        return;
    }
    pc_post_dof_gpu g;
    memset(&g, 0, sizeof g);
    g.v[6][0] = pf->focus_uv[0], g.v[6][1] = pf->focus_uv[1], g.v[6][2] = pf->focus_area;
    g.v[6][3] = (float)pc_hw_scale;
    g.v[7][0] = (float)x, g.v[7][1] = (float)y, g.v[7][2] = (float)w, g.v[7][3] = (float)h;
    begin_single(&dof_probe_image, 0, 0, 1, 1);
    dof_draw(dof_pipeline, &g, PC_DOF_PROBE, 1, 1, 1, 1, depth, NULL, NULL);
    vk_end_rendering();
    VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { 1, 1, 1 } };
    vkCmdCopyImageToBuffer(vk_cmd(), dof_probe_image.image, VK_IMAGE_LAYOUT_GENERAL, m->buffer.buffer, 1, &region);
    vk_barrier();
    vk_on_frame_done(probe_done, m);
}
