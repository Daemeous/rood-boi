/* Vulkan presentation (ROOD_RENDERER=vulkan): the window, its swapchain (or
 * none: headless runs render offscreen and only read frames back), and the
 * present passes of gl_present.c (lighting, haze, depth of field, FXAA, the
 * developer overlay) drawn into a window-sized image, then copied to the
 * swapchain. Settings are gl_present.c's (pc_gl_filter, pc_post_*, ...). */
#define _POSIX_C_SOURCE 200809L
#include "vk_hw.h"
#include "light_list.h"
#include "vk_hooks.h"
#include "vk_shaders.h"
#include "dev_state.h"
#include "lighting_quality.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int pc_display_masked __attribute__((weak)); /* boot_sdk.c SetDispMask */
extern int pc_display_rgb24, pc_hw_active, pc_hw_scale, pc_gte_widescreen, pc_hw_perspective;
extern int pc_gl_filter, pc_gl_aspect, pc_gl_aa, pc_gl_show_depth, pc_gl_view_mode, pc_post_flags,
    pc_post_focus_mode;
extern int pc_gl_view[4], pc_gl_display[4], pc_gl_inset[4];
extern float pc_post_aperture, pc_post_focus, pc_post_dof_radius, pc_post_tilt[3], pc_post_fog[4],
    pc_post_fog_range[2];
extern float pc_shadow_softness, pc_normal_smoothing, pc_lights_baked;
extern int pc_rt_shadows, pc_gi_enabled, pc_gi_debug, pc_baked_shadows_active;
extern float pc_gi_strength, pc_fog_density, pc_fog_anisotropy, pc_light_size;
extern void pc_scene_bounds(float lo[3], float hi[3]);
extern char pc_present_capture[400];
extern int pc_present_burst;
extern void pc_hw_init(const uint16_t* vram);
extern unsigned pc_hw_target(void);
extern void pc_hw_present_done(void);
extern void pc_hw_set_display(int x, int y, int w, int h);
extern void pc_hw_present_origin(int* x, int* y);
extern const uint16_t* pc_native_vram(void);
extern void pc_overlay_init(SDL_Window*, void*);
extern void pc_overlay_frame(int, int);
extern int pc_overlay_active(void) __attribute__((weak));
extern void pc_vk_overlay_render(VkCommandBuffer cmd);
extern int pc_lights_view(float (*)[4], float (*)[4], float (*)[4], float (*)[4], int*, float (*)[4], float (*)[4],
    int);
extern int pc_scene_camera_get(float (*)[3], float*, float*, float*, float*);
extern int pc_scene_camera_inverse(float inverse[3][3]);
extern int pc_scene_projection(float*, float*, float*);
/* Path-traced GI and shadows on Vulkan (vk_rt.c); 0 when unavailable. */
extern int pc_vk_gi_update(int x, int y, int w, int h, int replay) __attribute__((weak));
extern void pc_vk_gi_bind(VkImageView views[4], int* w, int* h, float lo[3], float hi[3]) __attribute__((weak));
extern int pc_vk_haze_bind(VkImageView* view, int grid[4], float range[2]) __attribute__((weak));
/* Probe GI (vk_probes.inc): update the field before the present pass (1: sample it per pixel, 2: it
 * went into the traced GI result); bind it (bindings 12-15). */
extern int pc_vk_probes_update(int x, int y, int w, int h, int replay) __attribute__((weak));
extern void pc_vk_probes_gi_bind(VkImageView views[4], int* w, int* h) __attribute__((weak));
extern int pc_vk_probes_bind(vk_descriptors* d, int first) __attribute__((weak));
extern int pc_vk_shadows_update(int count, const float world[][4], const int* wants, int* slot_out);
extern int pc_vk_shadows_slots(void);
extern VkImageView pc_vk_shadows_view(void);

static SDL_Window* window;
static VkSurfaceKHR surface;
static VkSwapchainKHR swapchain;
static VkFormat swapchain_format;
static VkImage swapchain_images[8];
static VkSemaphore acquired[VK_FRAMES], rendered[8];
static uint32_t swapchain_count;
static int swapchain_stale, window_w, window_h;
static vk_image window_image, present_vram;
typedef struct {
    vk_image image;
    int w, h;
} view_target;
static view_target blur_view, fxaa_view;

static VkDescriptorSetLayout scaled_set, single_set;
static VkPipelineLayout scaled_layout, blur_layout, fxaa_layout, vram_layout;
static VkPipeline scaled_to_window, scaled_to_view, blur_to_window, blur_to_view, fxaa_pipeline, vram_pipeline;

#define WINDOW_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#define VIEW_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

typedef struct {
    int rect[4];
    int i0[4]; /* scale, filtered, show_depth, post */
    int i1[4]; /* view_mode, light_count, gi_on */
    int i2[4]; /* gi_size */
    float view[4];
    float f0[4]; /* focus_sz, dof_radius, aperture, baked */
    float f1[4]; /* gi_strength, fog_density, shadow_softness, normal_smoothing */
    float tilt[4], fog[4], fog_range[4], room_lo[4], room_hi[4], projection[4], screen_offset[4];
    float inverse_rotation[3][4], normal_to_world[3][4], camera_translation[4];
    int haze_grid[4]; /* the froxel volume (vk_rt.inc): GX, GY, GZ, on */
    float haze_range[4]; /* znear, log(zfar / znear) */
    /* Lighting quality (lighting_quality.cpp): unshadowed haze density,
     * anisotropy, screen-space AO radius, strength; shadow map filter, SSAO
     * on; light size (PCSS). */
    float quality0[4];
    int quality1[4];
    float quality2[4];
} present_ubo;

/* ---- swapchain ---- */

static void destroy_swapchain(void)
{
    if (!swapchain)
        return;
    vkDeviceWaitIdle(vk.device);
    for (uint32_t i = 0; i < swapchain_count; i++)
        if (rendered[i])
            vkDestroySemaphore(vk.device, rendered[i], NULL), rendered[i] = VK_NULL_HANDLE;
    vkDestroySwapchainKHR(vk.device, swapchain, NULL);
    swapchain = VK_NULL_HANDLE;
}
static int make_swapchain(void)
{
    if (!surface)
        return 0;
    destroy_swapchain();
    VkSurfaceCapabilitiesKHR caps;
    if (!vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.gpu, surface, &caps), "surface capabilities"))
        return 0;
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.gpu, surface, &count, NULL);
    VkSurfaceFormatKHR formats[64];
    if (count > 64)
        count = 64;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.gpu, surface, &count, formats);
    VkSurfaceFormatKHR format = formats[0];
    for (uint32_t i = 0; i < count; i++)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            format = formats[i];
            break;
        }
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.gpu, surface, &mode_count, NULL);
    VkPresentModeKHR modes[16];
    if (mode_count > 16)
        mode_count = 16;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.gpu, surface, &mode_count, modes);
    /* The frame loop paces itself (as GL's swap interval 0): never block. */
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    static const VkPresentModeKHR preferred[2] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR };
    for (int p = 1; p >= 0; p--)
        for (uint32_t i = 0; i < mode_count; i++)
            if (modes[i] == preferred[p])
                mode = preferred[p];
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = (uint32_t)window_w;
        extent.height = (uint32_t)window_h;
    }
    if (!extent.width || !extent.height)
        return 0;
    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount && images > caps.maxImageCount)
        images = caps.maxImageCount;
    VkSwapchainCreateInfoKHR info = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = images,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = mode,
        .clipped = VK_TRUE };
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
        info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (!vk_check(vkCreateSwapchainKHR(vk.device, &info, NULL, &swapchain), "vkCreateSwapchainKHR"))
        return 0;
    swapchain_format = format.format;
    swapchain_count = 8;
    vkGetSwapchainImagesKHR(vk.device, swapchain, &swapchain_count, swapchain_images);
    VkSemaphoreCreateInfo semaphore = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < swapchain_count; i++)
        vkCreateSemaphore(vk.device, &semaphore, NULL, &rendered[i]);
    swapchain_stale = 0;
    return 1;
}

/* ---- pipelines ---- */

static VkPipelineLayout make_layout(VkDescriptorSetLayout set, uint32_t push_bytes)
{
    VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, push_bytes };
    VkPipelineLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set,
        .pushConstantRangeCount = push_bytes ? 1 : 0,
        .pPushConstantRanges = &range };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vk_check(vkCreatePipelineLayout(vk.device, &info, NULL, &layout), "vkCreatePipelineLayout");
    return layout;
}
static VkPipeline fullscreen(VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout, VkFormat format)
{
    vk_pipeline_desc d = { .vertex = vs,
        .fragment = fs,
        .layout = layout,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .color_count = 1,
        .color_formats = &format,
        .depth_format = VK_FORMAT_UNDEFINED,
        .write_masks = { 15 },
        .blend = -1 };
    return vk_pipeline(&d);
}
static int make_pipelines(void)
{
    VkDescriptorType scaled_types[17]; /* 12-15: the probe field (vk_probes.inc); 16: the lights */
    for (int i = 0; i < 17; i++)
        scaled_types[i] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    scaled_types[0] = scaled_types[12] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    scaled_types[16] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    scaled_set = vk_push_layout(scaled_types, 17);
    single_set = vk_push_layout(&scaled_types[1], 1);
    scaled_layout = make_layout(scaled_set, 0);
    blur_layout = make_layout(single_set, 16);
    fxaa_layout = make_layout(single_set, 8);
    vram_layout = make_layout(single_set, 32);
    VkShaderModule vs = vk_shader(vk_spv_fullscreen_vert, vk_spv_fullscreen_vert_size),
                   scaled = vk_shader(vk_spv_scaled_frag, vk_spv_scaled_frag_size),
                   blur = vk_shader(vk_spv_blur_frag, vk_spv_blur_frag_size),
                   fxaa = vk_shader(vk_spv_fxaa_frag, vk_spv_fxaa_frag_size),
                   vram = vk_shader(vk_spv_present_vram_frag, vk_spv_present_vram_frag_size);
    scaled_to_window = fullscreen(vs, scaled, scaled_layout, WINDOW_FORMAT);
    scaled_to_view = fullscreen(vs, scaled, scaled_layout, VIEW_FORMAT);
    blur_to_window = fullscreen(vs, blur, blur_layout, WINDOW_FORMAT);
    blur_to_view = fullscreen(vs, blur, blur_layout, VIEW_FORMAT);
    fxaa_pipeline = fullscreen(vs, fxaa, fxaa_layout, WINDOW_FORMAT);
    vram_pipeline = fullscreen(vs, vram, vram_layout, WINDOW_FORMAT);
    VkShaderModule modules[5] = { vs, scaled, blur, fxaa, vram };
    for (int i = 0; i < 5; i++)
        vkDestroyShaderModule(vk.device, modules[i], NULL);
    return scaled_to_window && scaled_to_view && blur_to_window && blur_to_view && fxaa_pipeline && vram_pipeline;
}

/* ---- window ---- */

static SDL_Window* abandon(SDL_Window* win)
{
    SDL_DestroyWindow(win);
    return NULL;
}
SDL_Window* pc_vk_create_window(const char* title, int width, int height)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, 40);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 40);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_Window* win = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!win) {
        /* The offscreen driver (headless runs) may lack a Vulkan surface:
         * a plain window still carries input and the overlay. */
        props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        win = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
        if (!win || !SDL_Vulkan_LoadLibrary(NULL)) {
            fprintf(stderr, "[vk] no Vulkan window: %s\n", SDL_GetError());
            return win ? abandon(win) : NULL;
        }
    }
    Uint32 extension_count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (!vk_core_init(extensions, extensions ? extension_count : 0))
        return abandon(win);
    window = win;
    SDL_GetWindowSizeInPixels(win, &window_w, &window_h);
    int headless = !strcmp(SDL_GetCurrentVideoDriver(), "offscreen") || getenv("ROOD_VK_HEADLESS");
    if (!headless && vkCreateSwapchainKHR && SDL_Vulkan_CreateSurface(win, vk.instance, NULL, &surface)) {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(vk.gpu, vk.queue_family, surface, &supported);
        if (!supported || !make_swapchain()) {
            fprintf(stderr, "[vk] cannot present to this window; rendering offscreen\n");
            swapchain = VK_NULL_HANDLE;
        }
    } else if (!headless)
        fprintf(stderr, "[vk] no Vulkan surface (%s); rendering offscreen\n", SDL_GetError());
    VkSemaphoreCreateInfo semaphore = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (int i = 0; i < VK_FRAMES; i++)
        vkCreateSemaphore(vk.device, &semaphore, NULL, &acquired[i]);
    if (!make_pipelines())
        return abandon(win);
    if (!vk_image_create(&present_vram, 1024, 512, VK_FORMAT_R16_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return abandon(win);
    pc_vk_active = 1;
    pc_hw_init(pc_native_vram());
    pc_overlay_init(win, NULL);
    const char* widescreen = getenv("ROOD_WIDESCREEN");
    pc_gte_widescreen = widescreen && *widescreen == '1';
    const char* perspective = getenv("ROOD_PERSPECTIVE");
    pc_hw_perspective = !(perspective && *perspective == '0');
    fprintf(stderr, "[vk] presenting %s\n", swapchain ? "to a swapchain" : "offscreen (headless)");
    return win;
}
void pc_vk_resize(int width, int height)
{
    window_w = width;
    window_h = height;
    swapchain_stale = 1;
}

/* ---- present passes ---- */

static int ensure_view(view_target* v, int w, int h)
{
    if (v->w == w && v->h == h && v->image.image)
        return 1;
    vk_image_destroy_later(&v->image);
    v->w = v->h = 0;
    if (!vk_image_create(&v->image, w, h, VIEW_FORMAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return 0;
    v->w = w, v->h = h;
    return 1;
}
static int ensure_window_image(void)
{
    if (window_image.image && window_image.width == window_w && window_image.height == window_h)
        return 1;
    vk_image_destroy_later(&window_image);
    return vk_image_create(&window_image, window_w, window_h, WINDOW_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT);
}
static void begin_single(const vk_image* image, int x, int y, int w, int h)
{
    vk_attachments a = { .colors = { image->view }, .color_count = 1, .width = image->width, .height = image->height };
    vk_begin_rendering(&a);
    VkViewport viewport = { (float)x, (float)y, (float)w, (float)h, 0, 1 };
    VkRect2D scissor = { { x, y }, { (uint32_t)w, (uint32_t)h } };
    VkCommandBuffer cmd = vk_cmd();
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetDepthTestEnable(cmd, VK_FALSE);
    vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_ALWAYS);
}

/* Focal depth (as gl_present.c's focus_depth). */
static float focus_depth(void)
{
    static float eased;
    if (pc_post_focus_mode == 2)
        return pc_post_focus;
    struct pc_dev_state s;
    float r[3][3], t[3], h, ox, oy;
    if (pc_post_focus_mode != 0 || !pc_dev_state || !pc_scene_camera_get(r, t, &h, &ox, &oy))
        return 0;
    pc_dev_state(&s);
    if (!s.has_player)
        return 0;
    float target = r[2][0] * s.x + r[2][1] * (s.y - 96) + r[2][2] * s.z + t[2];
    if (target <= 16)
        return 0;
    eased = eased > 0 ? eased + (target - eased) * 0.2f : target;
    return eased;
}
/* The game image's rectangle in the window (as gl_present.c's update_view). */
static void update_view(int* out_w, int* out_h)
{
    int area_x = pc_gl_inset[0], area_y = pc_gl_inset[1];
    int area_w = window_w - pc_gl_inset[0] - pc_gl_inset[2], area_h = window_h - pc_gl_inset[1] - pc_gl_inset[3];
    if (area_w < 64 || area_h < 48)
        area_x = area_y = 0, area_w = window_w, area_h = window_h;
    int view_w = area_w, view_h = area_h;
    if (pc_gl_aspect == 0 || pc_gl_aspect == 2) {
        /* 4:3, or 16:9 when the GTE projects widescreen (Hor+); movie frames
         * (24-bit display) are 4:3 pictures: pillarboxed in widescreen. */
        int wide = pc_gte_widescreen && !pc_display_rgb24;
        int aw = wide ? 16 : 4, ah = wide ? 9 : 3;
        view_h = area_w * ah / aw;
        if (view_h > area_h) {
            view_h = area_h;
            view_w = area_h * aw / ah;
        }
        int base_w = wide ? 427 : 320;
        if (pc_gl_aspect == 2 && view_w >= base_w && view_h >= 240) {
            int n = view_w / base_w < view_h / 240 ? view_w / base_w : view_h / 240;
            view_w = base_w * n;
            view_h = 240 * n;
        }
    }
    pc_gl_view[0] = area_x + (area_w - view_w) / 2;
    pc_gl_view[1] = area_y + (area_h - view_h) / 2;
    pc_gl_view[2] = view_w;
    pc_gl_view[3] = view_h;
    *out_w = view_w, *out_h = view_h;
}

static pc_light_gpu present_lights[PC_LIGHTS_MAX];
static int present_light_count;
static void fill_present_ubo(present_ubo* u, int x, int y, int w, int h, int view_w, int view_h, int gi,
    int gi_w, int gi_h, const float lo[3], const float hi[3])
{
    memset(u, 0, sizeof *u);
    u->rect[0] = x, u->rect[1] = y, u->rect[2] = w, u->rect[3] = h;
    u->i0[0] = pc_hw_scale, u->i0[1] = pc_gl_filter, u->i0[2] = pc_gl_show_depth, u->i0[3] = pc_post_flags;
    u->view[0] = (float)view_w, u->view[1] = (float)view_h;
    u->f0[0] = focus_depth();
    u->f0[1] = pc_post_dof_radius * view_h / 720.0f;
    u->f0[2] = pc_post_aperture;
    u->tilt[0] = pc_post_tilt[0], u->tilt[1] = pc_post_tilt[1], u->tilt[2] = pc_post_tilt[2] * view_h / 720.0f;
    memcpy(u->fog, pc_post_fog, sizeof u->fog);
    u->fog_range[0] = pc_post_fog_range[0], u->fog_range[1] = pc_post_fog_range[1];
    /* Lighting: world-space lights (every one: a storage buffer, binding
     * 16) and the projection to invert. */
    static float lp[PC_LIGHTS_MAX][4], lc[PC_LIGHTS_MAX][4], ld[PC_LIGHTS_MAX][4], lw[PC_LIGHTS_MAX][4],
        lr[PC_LIGHTS_MAX][4], lu[PC_LIGHTS_MAX][4];
    static int wants[PC_LIGHTS_MAX], slots[PC_LIGHTS_MAX];
    float hh, ox, oy;
    int count = 0;
    if (pc_scene_projection(&hh, &ox, &oy)) {
        count = pc_lights_view(lp, lc, ld, lw, wants, lr, lu, PC_LIGHTS_MAX);
        if (gi && pc_rt_shadows)
            for (int i = 0; i < count; i++)
                slots[i] = wants[i] ? -2 : -1;
        else { /* no traced visibility: shadow maps (vk_shadows.c) for the most important casters */
            pc_lights_shadow_pick(count, wants, pc_vk_shadows_slots());
            pc_vk_shadows_update(count, (const float(*)[4])lw, wants, slots);
        }
        float r[3][3], t[3], a, b, r_inverse[3][3];
        if (pc_scene_camera_get(r, t, &hh, &a, &b) && pc_scene_camera_inverse(r_inverse)) {
            for (int col = 0; col < 3; col++)
                for (int row = 0; row < 3; row++) {
                    u->inverse_rotation[col][row] = r_inverse[row][col];
                    u->normal_to_world[col][row] = r[col][row];
                }
            u->camera_translation[0] = t[0], u->camera_translation[1] = t[1], u->camera_translation[2] = t[2];
        }
        pc_scene_projection(&hh, &ox, &oy);
        u->projection[0] = hh * (pc_gte_widescreen ? 0.75f : 1.0f);
        u->projection[1] = hh;
        u->screen_offset[0] = ox, u->screen_offset[1] = oy;
    }
    for (int i = 0; i < count; i++) {
        pc_light_gpu* l = &present_lights[i];
        memcpy(l->pos, lp[i], sizeof lp[i]);
        memcpy(l->col, lc[i], sizeof lc[i]);
        memcpy(l->dir, ld[i], sizeof ld[i]);
        memcpy(l->right, lr[i], sizeof lr[i]);
        memcpy(l->up, lu[i], sizeof lu[i]);
        memcpy(l->world, lw[i], sizeof lw[i]);
        l->info[0] = slots[i], l->info[1] = l->info[2] = l->info[3] = 0;
    }
    present_light_count = count;
    u->i1[0] = pc_gl_view_mode;
    u->i1[1] = count;
    u->i1[2] = gi ? (pc_gi_enabled ? (pc_gi_debug == 1 ? 2 : 1) : 0) | (pc_rt_shadows ? 4 : 0)
                  | (pc_gi_debug == 2 ? 8 : 0) | (pc_baked_shadows_active || pc_lighting_ao_traced() ? 16 : 0)
                  | (pc_gi_debug == 3 ? 32 : 0)
                  : 0;
    u->i2[0] = gi_w, u->i2[1] = gi_h;
    u->f0[3] = pc_lighting_baked_scale(pc_lights_baked, count);
    u->f1[0] = pc_gi_strength;
    u->f1[1] = gi ? pc_fog_density : 0;
    u->f1[2] = pc_shadow_softness;
    u->f1[3] = pc_normal_smoothing;
    if (gi) {
        u->room_lo[0] = lo[0], u->room_lo[1] = lo[1], u->room_lo[2] = lo[2];
        u->room_hi[0] = hi[0], u->room_hi[1] = hi[1], u->room_hi[2] = hi[2];
    } else {
        pc_scene_bounds(u->room_lo, u->room_hi); /* the unshadowed haze fills the room too */
    }
    /* Lighting quality: haze Low (set after the froxel haze binds), AO, shadow map filter. */
    u->quality0[1] = pc_fog_anisotropy;
    u->quality0[2] = pc_ao_radius;
    u->quality0[3] = pc_ao_strength;
    u->quality1[0] = pc_shadow_map_filter;
    u->quality1[1] = pc_lighting_ssao(gi);
    u->quality2[0] = pc_light_size;
}

/* ---- capture (RPC present_dump) ---- */

typedef struct {
    vk_buffer buffer;
    char name[420];
    int w, h;
} capture;
static void write_capture(const char* name, const unsigned char* rgba, int w, int h)
{
    FILE* f = fopen(name, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    unsigned char* row = malloc((size_t)w * 3);
    for (int y = 0; y < h && row; y++) {
        for (int x = 0; x < w; x++)
            memcpy(row + x * 3, rgba + ((size_t)y * w + x) * 4, 3);
        fwrite(row, 1, (size_t)w * 3, f);
    }
    free(row);
    fclose(f);
}
static void capture_done(void* context)
{
    capture* c = context;
    write_capture(c->name, c->buffer.mapped, c->w, c->h);
    vk_buffer_destroy(&c->buffer);
    free(c);
}
static int burst_index;
static void capture_present(void)
{
    if (!pc_present_capture[0])
        return;
    int w = window_image.width, h = window_image.height;
    if (pc_present_burst <= 0) {
        unsigned char* pixels = malloc((size_t)w * h * 4);
        if (pixels && vk_read_image(&window_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, pixels, 4))
            write_capture(pc_present_capture, pixels, w, h);
        free(pixels);
        pc_present_capture[0] = 0;
        return;
    }
    /* A burst reads back asynchronously: written when the frame's slot comes
     * round, so frame timing (and interpolation) stays as in normal play. */
    capture* c = calloc(1, sizeof *c);
    if (c && vk_buffer_create(&c->buffer, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 2)) {
        vk_end_rendering();
        VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { (uint32_t)w, (uint32_t)h, 1 } };
        vkCmdCopyImageToBuffer(vk_cmd(), window_image.image, VK_IMAGE_LAYOUT_GENERAL, c->buffer.buffer, 1, &region);
        vk_barrier();
        snprintf(c->name, sizeof c->name, "%s.%04d.ppm", pc_present_capture, burst_index++);
        c->w = w, c->h = h;
        vk_on_frame_done(capture_done, c);
    } else
        free(c);
    if (--pc_present_burst <= 0) {
        burst_index = 0;
        pc_present_capture[0] = 0;
    }
}

/* ---- the frame ---- */

static void present_to_swapchain(void)
{
    if (!surface || swapchain_stale) {
        if (surface && swapchain_stale)
            make_swapchain();
    }
    if (!swapchain) {
        vk_frame_submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
        return;
    }
    uint32_t index = 0;
    VkSemaphore wait = acquired[vk.frame];
    VkResult result = vkAcquireNextImageKHR(vk.device, swapchain, UINT64_MAX, wait, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_stale = 1;
        vk_frame_submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        vk_frame_submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
        return;
    }
    VkCommandBuffer cmd = vk_cmd();
    vk_end_rendering();
    VkImageMemoryBarrier2 to_transfer = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain_images[index],
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkDependencyInfo dependency = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_transfer };
    vkCmdPipelineBarrier2(cmd, &dependency);
    VkImageBlit blit = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { { 0, 0, 0 }, { window_image.width, window_image.height, 1 } },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = { { 0, 0, 0 }, { window_image.width, window_image.height, 1 } } };
    vkCmdBlitImage(cmd, window_image.image, VK_IMAGE_LAYOUT_GENERAL, swapchain_images[index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    VkImageMemoryBarrier2 to_present = to_transfer;
    to_present.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dependency.pImageMemoryBarriers = &to_present;
    vkCmdPipelineBarrier2(cmd, &dependency);
    VkSemaphore done = rendered[index];
    vk_frame_submit(wait, VK_PIPELINE_STAGE_TRANSFER_BIT, done);
    VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &done,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &index };
    result = vkQueuePresentKHR(vk.queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        swapchain_stale = 1;
}

void pc_vk_present(const uint16_t* vram, int x, int y, int w, int h)
{
    pc_lighting_sync(); /* lighting quality: first-run preset, Custom on changes */
    if (!ensure_window_image()) {
        vk_frame_submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
        return;
    }
    if (pc_hw_active)
        pc_hw_set_display(x, y, w, h);
    unsigned target = pc_hw_active ? pc_hw_target() : 0;
    /* 24-bit display (movie frames): the scaled target only holds 15-bit
     * pixels, so present native VRAM. */
    if (pc_display_rgb24)
        target = 0;
    /* An interpolated frame may come from another area of the target (the
     * off-screen buffer the game shows next). */
    if (target)
        pc_hw_present_origin(&x, &y);
    vk_end_rendering();
    VkCommandBuffer cmd = vk_cmd();
    VkClearColorValue black = { { 0, 0, 0, 1 } };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, window_image.image, VK_IMAGE_LAYOUT_GENERAL, &black, 1, &range);
    vk_barrier();
    if (w > 0 && h > 0 && !(&pc_display_masked && pc_display_masked)) { /* SetDispMask(0): black */
        int view_w, view_h;
        update_view(&view_w, &view_h);
        pc_gl_display[0] = x, pc_gl_display[1] = y, pc_gl_display[2] = w, pc_gl_display[3] = h;
        int view_x = pc_gl_view[0], view_y = pc_gl_view[1];
        if (target) {
            const vk_target* t = target == VK_TOKEN_REPLAY ? &vk_hw.replay : &vk_hw.live;
            int replay = target == VK_TOKEN_REPLAY;
            int gi = 0, gi_w = 1, gi_h = 1;
            float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
            VkImageView gi_views[4] = { vk_hw.dummy_color.view, vk_hw.dummy_color.view, vk_hw.dummy_color.view,
                vk_hw.dummy_color.view };
            if (!pc_gl_show_depth && pc_vk_gi_update && pc_vk_gi_update(x, y, w, h, replay)) {
                gi = 1;
                pc_vk_gi_bind(gi_views, &gi_w, &gi_h, lo, hi);
            }
            int probes = !pc_gl_show_depth && pc_vk_probes_update ? pc_vk_probes_update(x, y, w, h, replay) : 0;
            if (probes == 3) /* the field at GI resolution, in place of a traced result */
                pc_vk_probes_gi_bind(gi_views, &gi_w, &gi_h);
            int blur = !pc_gl_show_depth && (pc_post_flags & 3) && ensure_view(&blur_view, view_w, view_h);
            int fxaa = pc_gl_aa && ensure_view(&fxaa_view, view_w, view_h);
            vk_slice ubo = vk_arena(sizeof(present_ubo), vk.properties.limits.minUniformBufferOffsetAlignment);
            if (!ubo.data)
                goto overlay;
            vk_section("present");
            fill_present_ubo(ubo.data, x, y, w, h, view_w, view_h, gi, gi_w, gi_h, lo, hi);
            VkDeviceSize light_bytes = (VkDeviceSize)(present_light_count ? present_light_count : 1) * sizeof(pc_light_gpu);
            vk_slice light_slice = vk_arena(light_bytes, vk.properties.limits.minStorageBufferOffsetAlignment);
            if (!light_slice.data)
                goto overlay;
            memcpy(light_slice.data, present_lights, (size_t)present_light_count * sizeof(pc_light_gpu));
            VkImageView haze_view = vk_hw.dummy_color.view;
            present_ubo* pu = ubo.data;
            if (gi && pc_vk_haze_bind && pc_vk_haze_bind(&haze_view, pu->haze_grid, pu->haze_range))
                pu->f1[1] = 0; /* no per-pixel haze */
            /* gi_on 128: a GI result is bound; 64: add the probe field per pixel; 256: the bound result's
             * rgb is the probe field (no traced result); 2 shows it alone. With probes a traced result's
             * rgb is the field (or zero, per pixel), plus the glows. */
            pu->i1[2] |= (gi || probes == 3 ? 128 : 0) | (probes == 1 ? 64 : 0) | (probes == 3 ? 256 : 0)
                       | (probes && pc_gi_debug == 1 ? 2 : 0) | (probes && gi && pc_gi_debug != 1 ? 1 : 0);
            pu->quality0[0] = pc_gl_show_depth ? 0 : pc_lighting_low_haze_density(pu->haze_grid[3]);
            /* Lit frame -> [depth-of-field blur] -> [FXAA] -> window. */
            const vk_image* first = blur ? &blur_view.image : fxaa ? &fxaa_view.image : &window_image;
            if (blur || fxaa)
                begin_single(first, 0, 0, view_w, view_h);
            else
                begin_single(first, view_x, view_y, view_w, view_h);
            cmd = vk_cmd();
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blur || fxaa ? scaled_to_view : scaled_to_window);
            vk_descriptors d = { 0 };
            vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ubo.buffer, ubo.offset, sizeof(present_ubo));
            vk_descriptors_image(&d, 1, t->color.view, vk.linear);
            vk_descriptors_image(&d, 2, t->depth.view, vk.nearest);
            vk_descriptors_image(&d, 3, t->normal.view, vk.nearest);
            vk_descriptors_image(&d, 4, t->albedo.view, vk.nearest);
            vk_descriptors_image(&d, 5, t->shade.view, vk.nearest);
            for (int i = 0; i < 4; i++)
                vk_descriptors_image(&d, 6 + i, gi_views[i], vk.nearest);
            vk_descriptors_image(&d, 10, haze_view, vk.linear);
            vk_descriptors_image(&d, 11, pc_vk_shadows_view(), vk.linear);
            if (pc_vk_probes_bind)
                pc_vk_probes_bind(&d, 12);
            vk_descriptors_buffer(&d, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, light_slice.buffer, light_slice.offset,
                light_bytes);
            vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, scaled_layout);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            if (blur) {
                if (fxaa)
                    begin_single(&fxaa_view.image, 0, 0, view_w, view_h);
                else
                    begin_single(&window_image, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaa ? blur_to_view : blur_to_window);
                vk_descriptors_image(&d, 0, blur_view.image.view, vk.linear);
                vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, blur_layout);
                float most = pc_post_dof_radius > pc_post_tilt[2] ? pc_post_dof_radius : pc_post_tilt[2];
                float push[4] = { (float)view_w, (float)view_h, most * view_h / 720.0f, 0 };
                vkCmdPushConstants(cmd, blur_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
            if (fxaa) {
                begin_single(&window_image, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaa_pipeline);
                vk_descriptors_image(&d, 0, fxaa_view.image.view, vk.linear);
                vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaa_layout);
                float push[2] = { 1.0f / view_w, 1.0f / view_h };
                vkCmdPushConstants(cmd, fxaa_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        } else {
            /* Native VRAM: upload it whole (as the GL path does each frame). */
            vk_slice staging = vk_arena(1024 * 512 * 2, 16);
            if (staging.data) {
                memcpy(staging.data, vram, 1024 * 512 * 2);
                vk_end_rendering();
                cmd = vk_cmd();
                VkBufferImageCopy region = { .bufferOffset = staging.offset,
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .imageExtent = { 1024, 512, 1 } };
                vkCmdCopyBufferToImage(cmd, staging.buffer, present_vram.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
                vk_barrier();
                begin_single(&window_image, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vram_pipeline);
                vk_descriptors d = { 0 };
                vk_descriptors_image(&d, 0, present_vram.view, vk.nearest);
                vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, vram_layout);
                int push[8] = { x, y, w, h, pc_display_rgb24, 0, 0, 0 };
                vkCmdPushConstants(cmd, vram_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        }
    }
overlay:
    pc_overlay_frame(window_w, window_h);
    {
        vk_attachments a = { .colors = { window_image.view }, .color_count = 1, .width = window_w, .height = window_h };
        vk_begin_rendering(&a);
        pc_vk_overlay_render(vk_cmd());
    }
    if (pc_hw_active)
        pc_hw_present_done();
    capture_present();
    present_to_swapchain();
}
