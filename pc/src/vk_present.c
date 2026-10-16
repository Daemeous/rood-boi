/* Vulkan presentation (ROOD_RENDERER=vulkan): the window, its swapchain (or
 * none: headless runs render offscreen and only read frames back), and the
 * present passes of gl_present.c (lighting, haze, depth of field, FXAA, the
 * developer overlay) drawn into a window-sized image, then copied to the
 * swapchain. Settings are gl_present.c's (pc_gl_filter, pc_post_*, ...). */
#define _POSIX_C_SOURCE 200809L
#include "vk_hw.h"
#include "light_list.h"
#include "environment.h"
#include "atmosphere.h"
int pc_vk_shadows_sun(const float dir[3], float matrix[16], float light[4], float tile[4]); /* vk_shadows.c */
#include "vk_hooks.h"
#include "vk_shaders.h"
#include "dev_state.h"
#include "lighting_quality.h"
#include "antialias.h"
#include "post.h"
#include "emissive.h"
#include "ui/menu_light.h" /* the model view's studio light */
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int pc_display_masked __attribute__((weak)); /* boot_sdk.c SetDispMask */
extern int pc_display_rgb24, pc_hw_active, pc_hw_scale, pc_gte_widescreen, pc_hw_perspective;
extern int pc_gl_filter, pc_gl_aspect, pc_gl_show_depth, pc_gl_view_mode;
extern int pc_gl_view[4], pc_gl_display[4], pc_gl_inset[4];
static pc_post_frame post_frame; /* this present's resolved post profile (post.h) */
static int post_hdr; /* the present shader's post bit 8: HDR out, the grade pass tone maps */
static int post_grade_on; /* the grade pass runs this present (emissive texels may exceed 1: post bit 32) */
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
extern void pc_hw_supersample(int view_h, int display_h);
extern void pc_hw_present_origin(int* x, int* y);
extern const uint16_t* pc_native_vram(void);
extern void pc_overlay_init(SDL_Window*, void*);
extern void pc_settings_env_override(const char* key) __attribute__((weak)); /* project_settings.cpp */
extern void pc_overlay_frame(int, int);
extern int pc_overlay_active(void) __attribute__((weak));
extern void pc_vk_overlay_render(VkCommandBuffer cmd);
extern void pc_hw_text_overlay(int view_x, int view_y, int view_w, int view_h, int to_view);
extern int pc_lights_view(float (*)[4], float (*)[4], float (*)[4], float (*)[4], int*, float (*)[4], float (*)[4],
    int);
extern int pc_scene_camera_get(float (*)[3], float*, float*, float*, float*);
extern int pc_scene_camera_inverse(float inverse[3][3]);
extern int pc_scene_projection(float*, float*, float*);
/* Path-traced GI and shadows on Vulkan (vk_rt.c); 0 when unavailable. */
extern int pc_vk_gi_update(int x, int y, int w, int h, int replay) __attribute__((weak));
extern void pc_vk_gi_bind(VkImageView views[4], int* w, int* h, float lo[3], float hi[3]) __attribute__((weak));
extern int pc_vk_haze_bind(VkImageView* view, int grid[4], float range[2]) __attribute__((weak));
/* The fog volumes' own renderer (vk_fogvol.inc): its passes for this frame, then its result. */
extern int pc_vk_fogvol_update(int x, int y, int w, int h, int replay, int traced) __attribute__((weak));
extern int pc_vk_fogvol_bind(VkImageView* light, VkImageView* aux, int size[2], float scale[2]) __attribute__((weak));
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
static VkExtent2D swapchain_extent; /* its images' size: the window image's while there is a swapchain */
/* stale: recreate the swapchain at the next frame's start (a size change,
 * OUT_OF_DATE or SUBOPTIMAL). window_w/h: the window image's size. */
static int swapchain_stale, window_w, window_h;
extern unsigned pc_window_presents; /* gl_present.c: images that reached the window (RPC status) */
static vk_image window_image, present_vram;
typedef struct {
    vk_image image;
    int w, h;
} view_target;
/* The post chain's lit-sized images, used in turn (pass n writes chain[n & 1]). */
static view_target chain[2];
/* SMAA (antialias.h): its edges and blending weights at the lit image's
 * size, and the reference lookup textures. */
static vk_image smaa_edges, smaa_weights, smaa_area, smaa_search;

static VkDescriptorSetLayout scaled_set, single_set, triple_set;
static VkPipelineLayout scaled_layout, blur_layout, fxaa_layout, vram_layout, smaa_layout, downsample_layout;
static VkPipeline scaled_to_window, scaled_to_view, blur_to_window, blur_to_view, fxaa_pipeline, fxaa_to_view,
    vram_pipeline, smaa_edges_pipeline, smaa_weights_pipeline, smaa_blend_to_window, smaa_blend_to_view,
    downsample_pipeline;
typedef struct {
    float metrics[4]; /* 1 / width, 1 / height, width, height */
    int rect[4]; /* the display area (VRAM pixels) */
    int info[4]; /* scale, geometric edges only */
} smaa_push;
typedef struct {
    float src_size[2], dst_size[2];
    int filter;
} downsample_push;

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
    pc_env_gpu env; /* the room's sun and sky (environment.h) */
    int env_mode[4]; /* the sun's shadows (-2 traced, 0 its shadow map, -1 none), the sky's ambient term, any on */
    float sun_matrix[16], sun_light[4], sun_tile[4]; /* its shadow map (vk_shadows.c) */
    pc_medium_gpu medium; /* the unshadowed haze (haze Low): the room's medium (medium.glsl) */
    int fogvol[4]; /* the fog volumes' renderer (vk_fogvol.inc): its image's size, on, the debug view */
    float fogvol_scale[4]; /* its pixels per display pixel */
    /* The modern menu's model view (gl_present.c "The modern menu's backdrop"): the ground colour and
     * opacity; mode (0 off, 1 the ground alone, 2 the kept picture at binding 20 under it); [1] the
     * model's studio light (menu_studio.glsl) on. */
    float backdrop_color[4];
    int backdrop[4];
    /* The studio light's rig (ui/menu_light.h pc_menu_light_pack; scaled.frag studio_). */
    float studio[PC_MENU_LIGHT_VEC4S][4];
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
    swapchain_extent = extent;
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
#define EDGES_FORMAT VK_FORMAT_R8G8_UNORM
#define WEIGHTS_FORMAT VK_FORMAT_R8G8B8A8_UNORM
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
    /* 12-15: the probe field (vk_probes.inc); 16: the lights; 17: the medium's texture; 18, 19: the fog volumes;
     * 20: the menu's backdrop (backdrop_keep) */
    VkDescriptorType scaled_types[21];
    for (int i = 0; i < 21; i++)
        scaled_types[i] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    scaled_types[0] = scaled_types[12] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    scaled_types[16] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    scaled_set = vk_push_layout(scaled_types, 21);
    single_set = vk_push_layout(&scaled_types[1], 1);
    triple_set = vk_push_layout(&scaled_types[1], 3);
    scaled_layout = make_layout(scaled_set, 0);
    blur_layout = make_layout(single_set, 16);
    fxaa_layout = make_layout(single_set, 8);
    vram_layout = make_layout(single_set, 32);
    smaa_layout = make_layout(triple_set, sizeof(smaa_push));
    downsample_layout = make_layout(single_set, sizeof(downsample_push));
    VkShaderModule vs = vk_shader(vk_spv_fullscreen_vert, vk_spv_fullscreen_vert_size),
                   scaled = vk_shader(vk_spv_scaled_frag, vk_spv_scaled_frag_size),
                   blur = vk_shader(vk_spv_blur_frag, vk_spv_blur_frag_size),
                   fxaa = vk_shader(vk_spv_fxaa_frag, vk_spv_fxaa_frag_size),
                   vram = vk_shader(vk_spv_present_vram_frag, vk_spv_present_vram_frag_size),
                   edges = vk_shader(vk_spv_smaa_edges_frag, vk_spv_smaa_edges_frag_size),
                   weights = vk_shader(vk_spv_smaa_weights_frag, vk_spv_smaa_weights_frag_size),
                   blend = vk_shader(vk_spv_smaa_blend_frag, vk_spv_smaa_blend_frag_size),
                   down = vk_shader(vk_spv_downsample_frag, vk_spv_downsample_frag_size);
    scaled_to_window = fullscreen(vs, scaled, scaled_layout, WINDOW_FORMAT);
    scaled_to_view = fullscreen(vs, scaled, scaled_layout, VIEW_FORMAT);
    blur_to_window = fullscreen(vs, blur, blur_layout, WINDOW_FORMAT);
    blur_to_view = fullscreen(vs, blur, blur_layout, VIEW_FORMAT);
    fxaa_pipeline = fullscreen(vs, fxaa, fxaa_layout, WINDOW_FORMAT);
    fxaa_to_view = fullscreen(vs, fxaa, fxaa_layout, VIEW_FORMAT);
    vram_pipeline = fullscreen(vs, vram, vram_layout, WINDOW_FORMAT);
    smaa_edges_pipeline = fullscreen(vs, edges, smaa_layout, EDGES_FORMAT);
    smaa_weights_pipeline = fullscreen(vs, weights, smaa_layout, WEIGHTS_FORMAT);
    smaa_blend_to_window = fullscreen(vs, blend, smaa_layout, WINDOW_FORMAT);
    smaa_blend_to_view = fullscreen(vs, blend, smaa_layout, VIEW_FORMAT);
    downsample_pipeline = fullscreen(vs, down, downsample_layout, WINDOW_FORMAT);
    VkShaderModule modules[9] = { vs, scaled, blur, fxaa, vram, edges, weights, blend, down };
    for (int i = 0; i < 9; i++)
        vkDestroyShaderModule(vk.device, modules[i], NULL);
    return scaled_to_window && scaled_to_view && blur_to_window && blur_to_view && fxaa_pipeline && fxaa_to_view
        && vram_pipeline && smaa_edges_pipeline && smaa_weights_pipeline && smaa_blend_to_window && smaa_blend_to_view
        && downsample_pipeline;
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
            vkDestroySurfaceKHR(vk.instance, surface, NULL); /* offscreen from now on (sizes from the events) */
            surface = VK_NULL_HANDLE;
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
    /* As gl_present.c: the variables apply only when set (the settings keep theirs). */
    const char* widescreen = getenv("ROOD_WIDESCREEN");
    if (widescreen && *widescreen) {
        pc_gte_widescreen = *widescreen == '1';
        if (pc_settings_env_override)
            pc_settings_env_override("widescreen"); /* not saved as the user's (project_settings.cpp) */
    }
    const char* perspective = getenv("ROOD_PERSPECTIVE");
    if (perspective && *perspective) {
        pc_hw_perspective = *perspective != '0';
        if (pc_settings_env_override)
            pc_settings_env_override("perspective_textures");
    }
    fprintf(stderr, "[vk] presenting %s\n", swapchain ? "to a swapchain" : "offscreen (headless)");
    return win;
}
/* SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED (boot_window.c, between frames). With
 * a swapchain only marks it stale: the next frame recreates it at the
 * surface's extent, which is the window image's size from then on (the
 * event can lag the surface, or carry a passing size, while the window
 * switches to or from fullscreen). */
void pc_vk_resize(int width, int height)
{
    if (!swapchain) {
        window_w = width;
        window_h = height;
    }
    if (surface && (!swapchain || (int)swapchain_extent.width != width || (int)swapchain_extent.height != height))
        swapchain_stale = 1;
}
/* At the frame's start (nothing drawn into the window image yet): a stale
 * swapchain is recreated (make_swapchain waits for the device to go idle
 * first, so no frame in flight still uses the old one) and the window image
 * takes its size. Every pass of the frame then draws at that size, and the
 * blit to the swapchain image matches it exactly: a window image larger
 * than the swapchain (the window image kept a fullscreen size while the
 * surface was already back to windowed) wrote outside the swapchain image
 * and lost the device. */
static void sync_swapchain(void)
{
    if (!surface || vk.lost)
        return;
    if (swapchain_stale)
        make_swapchain(); /* fails while the window has no area (minimised): stays stale, tried next frame */
    if (swapchain) {
        window_w = (int)swapchain_extent.width;
        window_h = (int)swapchain_extent.height;
    }
}

/* ---- present passes ---- */

static int ensure_view(view_target* v, int w, int h)
{
    if (v->w == w && v->h == h && v->image.image)
        return 1;
    vk_image_destroy_later(&v->image);
    v->w = v->h = 0;
    if (!vk_image_create(&v->image, w, h, VIEW_FORMAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT /* (backdrop_keep reads the brightness input) */))
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
static int ensure_image(vk_image* image, int w, int h, VkFormat format)
{
    if (image->image && image->width == w && image->height == h)
        return 1;
    vk_image_destroy_later(image);
    return vk_image_create(image, w, h, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
}
/* The medium's texture (atmosphere.h pc_medium_texture: the detail noise,
 * the interactive fog's fields), for the present pass's Low haze and the
 * traced haze (vk_rt.inc): uploaded whole once, then its field slices when
 * they change (once a vsync at most). Call outside a render pass. */
VkImageView pc_vk_medium_texture(void)
{
    /* One image per frame in flight: the slot's last frame has finished
     * when it records again, so an upload needs no wait for earlier reads,
     * and only the passes after it wait for the copy (not a full barrier:
     * the fields change every vsync while the fog moves). */
    static vk_image images[VK_FRAMES];
    static unsigned uploaded[VK_FRAMES];
    static int whole[VK_FRAMES];
    vk_image* image = &images[vk.frame];
    unsigned serial;
    const unsigned char* texels = pc_medium_texture(&serial);
    if (!image->image) {
        if (!vk_image_create_3d(image, PC_MEDIUM_TEX_SIZE, PC_MEDIUM_TEX_SIZE, PC_MEDIUM_TEX_DEPTH, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            return VK_NULL_HANDLE;
        whole[vk.frame] = 1;
    }
    if (whole[vk.frame] || serial != uploaded[vk.frame]) {
        int z0 = whole[vk.frame] ? 0 : PC_MEDIUM_TEX_NOISE, depth = PC_MEDIUM_TEX_DEPTH - z0;
        size_t slice = (size_t)PC_MEDIUM_TEX_SIZE * PC_MEDIUM_TEX_SIZE * 4, bytes = slice * depth; /* RGBA8 */
        vk_slice staging = vk_arena(bytes, 16);
        if (staging.data) {
            memcpy(staging.data, texels + slice * z0, bytes);
            vk_end_rendering();
            VkCommandBuffer cmd = vk_cmd();
            VkBufferImageCopy region = { .bufferOffset = staging.offset,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageOffset = { 0, 0, z0 },
                .imageExtent = { PC_MEDIUM_TEX_SIZE, PC_MEDIUM_TEX_SIZE, (uint32_t)depth } };
            vkCmdCopyBufferToImage(cmd, staging.buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
            VkMemoryBarrier2 copied = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };
            VkDependencyInfo dependency = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &copied };
            vkCmdPipelineBarrier2(cmd, &dependency);
            whole[vk.frame] = 0, uploaded[vk.frame] = serial;
        }
    }
    return image->view;
}
/* SMAA's lookup textures, uploaded once (antialias.c). */
static int smaa_upload(vk_image* image, VkFormat format, const unsigned char* data, int w, int h, int texel_bytes)
{
    if (!vk_image_create(image, w, h, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return 0;
    size_t bytes = (size_t)w * h * texel_bytes;
    vk_slice staging = vk_arena(bytes, 16);
    if (!staging.data)
        return 0;
    memcpy(staging.data, data, bytes);
    vk_end_rendering();
    VkBufferImageCopy region = { .bufferOffset = staging.offset,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { (uint32_t)w, (uint32_t)h, 1 } };
    vkCmdCopyBufferToImage(vk_cmd(), staging.buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    vk_barrier();
    return 1;
}
static int smaa_ready(void)
{
    static int state; /* 0 not yet, 1 ready, -1 failed */
    if (!state) {
        int aw, ah, sw, sh;
        const unsigned char* area = pc_smaa_area_texture(&aw, &ah);
        const unsigned char* search = pc_smaa_search_texture(&sw, &sh);
        state = smaa_upload(&smaa_area, VK_FORMAT_R8G8_UNORM, area, aw, ah, 2)
                && smaa_upload(&smaa_search, VK_FORMAT_R8_UNORM, search, sw, sh, 1)
            ? 1
            : -1;
    }
    return state > 0;
}
static void begin_single(const vk_image* image, int x, int y, int w, int h);
/* Pass `stage` of the post chain renders into the window's view rectangle
 * when it is the last of `stages`, else into the next lit-sized image;
 * returns that image (the input of the pass after it). */
static int brightness_stage; /* this frame's chain ends with the brightness curve (brightness_on) */
static view_target brightness_input; /* its input: the chain's last image, view-sized 16-bit float */
static const vk_image* stage_begin(int stage, int stages, int lit_w, int lit_h, int view_x, int view_y, int view_w,
    int view_h)
{
    if (stage + 1 >= stages) {
        begin_single(&window_image, view_x, view_y, view_w, view_h);
        return &window_image;
    }
    if (brightness_stage && stage + 2 >= stages) { /* settings brightness: its 16-bit input, view-sized */
        begin_single(&brightness_input.image, 0, 0, view_w, view_h);
        return &brightness_input.image;
    }
    const vk_image* out = &chain[stage & 1].image;
    begin_single(out, 0, 0, lit_w, lit_h);
    return out;
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

/* Brightness (settings brightness; gl_present.c's brightness block, the
 * same rule): at 50 nothing runs; otherwise the post chain gets one more
 * stage: what would have gone to the window goes to a view-sized 16-bit
 * float image (brightness_input), drawn into the window's view through
 * brightness.frag. The pipelines are made on first use: the curve's, and
 * the downsample and native-VRAM passes into the 16-bit format (the other
 * passes have theirs already). */
extern int pc_present_brightness;
extern float pc_present_brightness_factor(void);
static VkPipelineLayout brightness_layout;
static VkPipeline brightness_pipeline, downsample_to_view, vram_to_view;
static int brightness_on(int view_w, int view_h)
{
    static int state; /* 0 not yet, 1 ready, -1 failed */
    if (pc_present_brightness == 50 || state < 0 || view_w <= 0 || view_h <= 0)
        return 0;
    if (!state) {
        VkShaderModule vs = vk_shader(vk_spv_fullscreen_vert, vk_spv_fullscreen_vert_size),
                       fs = vk_shader(vk_spv_brightness_frag, vk_spv_brightness_frag_size),
                       down = vk_shader(vk_spv_downsample_frag, vk_spv_downsample_frag_size),
                       vram = vk_shader(vk_spv_present_vram_frag, vk_spv_present_vram_frag_size);
        brightness_layout = make_layout(single_set, 16);
        if (vs && fs && down && vram && brightness_layout) {
            brightness_pipeline = fullscreen(vs, fs, brightness_layout, WINDOW_FORMAT);
            downsample_to_view = fullscreen(vs, down, downsample_layout, VIEW_FORMAT);
            vram_to_view = fullscreen(vs, vram, vram_layout, VIEW_FORMAT);
        }
        VkShaderModule modules[4] = { vs, fs, down, vram };
        for (int i = 0; i < 4; i++)
            if (modules[i])
                vkDestroyShaderModule(vk.device, modules[i], NULL);
        state = brightness_pipeline && downsample_to_view && vram_to_view ? 1 : -1;
        if (state < 0) {
            fprintf(stderr, "[vk] brightness unavailable (its pipelines did not build)\n");
            return 0;
        }
    }
    return ensure_view(&brightness_input, view_w, view_h);
}
/* The modern menu's backdrop (Ashley tab; gl_present.c "The modern menu's backdrop" says why and how):
 * every present while the modern menu is up and shows the world keeps the chain's finished picture
 * (before game text and the brightness curve); a present of the menu's model view shows it, under the
 * menu's ground colour and opacity, where the frame has 2D (the present shader, binding 20). */
extern int pc_ui_menu_active(void) __attribute__((weak));            /* ui/menu_bridge.c */
extern int pc_ui_menu_backdrop(float rgba[4]) __attribute__((weak)); /* ui/ui_theme.cpp: ground, opacity */
extern int pc_hw_present_menu_model(void) __attribute__((weak));     /* gpu_hw.c */
extern int pc_hw_present_menu_studio(void) __attribute__((weak));    /* gpu_hw.c: its studio light */
extern int pc_ui_menu_model_light(pc_menu_light* out) __attribute__((weak)); /* ui/menu_light.cpp: its rig */
static view_target backdrop;
static int backdrop_valid;
static unsigned backdrop_keeps; /* pictures kept so far */
/* gl_present.c pc_gl_menu_backdrop_kept */
int pc_vk_menu_backdrop_kept(void) { return backdrop_valid ? (int)(backdrop_keeps & 0x3FFFFFFF) + 1 : 0; }
/* The present shader's backdrop fields and its image (binding 20). */
static VkImageView backdrop_fill(present_ubo* u)
{
    float rgba[4] = { 0, 0, 0, 1 };
    int mode = 0;
    if (pc_hw_present_menu_model && pc_hw_present_menu_model() && pc_ui_menu_backdrop && pc_ui_menu_backdrop(rgba))
        mode = backdrop_valid ? 2 : 1;
    if (mode == 1)
        rgba[3] = 1;
    memcpy(u->backdrop_color, rgba, sizeof rgba);
    u->backdrop[0] = mode;
    u->backdrop[1] = pc_hw_present_menu_studio && pc_hw_present_menu_studio(); /* scaled.frag menu_studio */
    pc_menu_light light; /* the rig as data (theme.json "menu_model_light"; RPC graphics menu_light) */
    if (!pc_ui_menu_model_light || !pc_ui_menu_model_light(&light))
        pc_ui_menu_model_light_defaults(&light);
    pc_menu_light_pack(&light, u->studio);
    return mode == 2 ? backdrop.image.view : vk_hw.dummy_color.view;
}
static void backdrop_keep(int view_x, int view_y, int view_w, int view_h)
{
    if (!pc_ui_menu_active || !pc_ui_menu_active()) {
        backdrop_valid = 0;
        if (backdrop.image.image) { /* the menu closed: give the picture's memory back */
            vk_image_destroy_later(&backdrop.image);
            backdrop.w = backdrop.h = 0;
        }
        return;
    }
    if ((pc_hw_present_menu_model && pc_hw_present_menu_model()) || !ensure_view(&backdrop, view_w, view_h))
        return;
    const vk_image* from = brightness_stage ? &brightness_input.image : &window_image;
    int x = brightness_stage ? 0 : view_x, y = brightness_stage ? 0 : view_y;
    vk_end_rendering();
    VkImageBlit region = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { { x, y, 0 }, { x + view_w, y + view_h, 1 } },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = { { 0, 0, 0 }, { view_w, view_h, 1 } } };
    vkCmdBlitImage(vk_cmd(), from->image, VK_IMAGE_LAYOUT_GENERAL, backdrop.image.image, VK_IMAGE_LAYOUT_GENERAL, 1,
        &region, VK_FILTER_NEAREST);
    vk_barrier();
    backdrop_valid = 1;
    ++backdrop_keeps;
}
/* The last stage: brightness_input through the curve into the window's view. */
static void brightness_draw(int view_x, int view_y, int view_w, int view_h)
{
    vk_section("brightness");
    begin_single(&window_image, view_x, view_y, view_w, view_h);
    VkCommandBuffer cmd = vk_cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, brightness_pipeline);
    vk_descriptors d = { 0 };
    vk_descriptors_image(&d, 0, brightness_input.image.view, vk.nearest);
    vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, brightness_layout);
    struct {
        int origin[2];
        float factor, unused;
    } push = { { view_x, view_y }, pc_present_brightness_factor(), 0 };
    vkCmdPushConstants(cmd, brightness_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof push, &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

#include "post_vk.inc" /* the post stack: grade pass, Gaussian and convolution bloom, meter */

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
    const pc_post_frame* pf = &post_frame;
    u->i0[0] = pc_hw_scale, u->i0[1] = pc_gl_filter, u->i0[2] = pc_gl_show_depth;
    /* 16: emissive texels in the shade target (emissive.h); 32: the grade pass runs (they may exceed 1 for bloom). */
    u->i0[3] = pf->flags | post_hdr | (pc_emissive_active() ? 16 : 0) | (post_grade_on ? 32 : 0);
    u->view[0] = (float)view_w, u->view[1] = (float)view_h;
    u->f0[0] = pf->focus_sz;
    u->f0[1] = pf->dof_radius * view_h / 720.0f;
    u->f0[2] = pf->aperture;
    u->tilt[0] = pf->tilt[0], u->tilt[1] = pf->tilt[1], u->tilt[2] = pf->tilt[2] * view_h / 720.0f;
    u->tilt[3] = pf->near_blur; /* scaled.frag: the share of the blur in front of the focal plane */
    memcpy(u->fog, pf->fog, sizeof u->fog);
    u->fog_range[0] = pf->fog_range[0], u->fog_range[1] = pf->fog_range[1];
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
    u->quality1[2] = pc_haze_debug; /* the haze alone (atmosphere.h) */
    u->quality2[0] = pc_light_size;
    /* The sun and sky: the trace weighs a shadow-casting sun into the visibility. */
    u->env_mode[2] = pc_env_gpu_get(&u->env);
    u->env_mode[0] = gi && pc_rt_shadows && u->env.sun_col[3] > 0.5f ? -2 : -1;
    if (u->env_mode[0] == -1 && u->env.sun_col[3] > 0.5f
        && pc_vk_shadows_sun(u->env.sun_dir, u->sun_matrix, u->sun_light, u->sun_tile))
        u->env_mode[0] = 0; /* no traced visibility: the sun's shadow map */
    if (!u->sun_tile[2])
        u->sun_tile[2] = 1;
    u->env_mode[1] = 0; /* the sky's ambient: set once the probe field is known */
}

/* ---- capture (RPC present_dump) ---- */

typedef struct {
    vk_buffer buffer;
    char name[420];
    int w, h;
} capture;
/* gl_present.c: written beside the name, then renamed (complete when it exists). */
extern void pc_write_ppm_atomic(const char* name, const unsigned char* pixels, int w, int h, int stride, int flip);
extern unsigned pc_presents;
extern int pc_present_sample_pending(int xy[2]); /* gl_present.c: the eyedropper's pixel */
extern void pc_present_sample_store(const unsigned char* pixels, int stride);
static void write_capture(const char* name, const unsigned char* rgba, int w, int h)
{
    pc_write_ppm_atomic(name, rgba, w, h, 4, 0);
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
    ++pc_presents;
    int sample_at[2];
    if (pc_present_sample_pending(sample_at)) { /* the eyedropper's pixel */
        unsigned char px[36];
        int w = window_image.width, h = window_image.height;
        int x = sample_at[0] - 1, y = sample_at[1] - 1;
        x = x < 0 ? 0 : x > w - 3 ? w - 3 : x;
        y = y < 0 ? 0 : y > h - 3 ? h - 3 : y;
        if (vk_read_image(&window_image, VK_IMAGE_ASPECT_COLOR_BIT, x, y, 3, 3, px, 4))
            pc_present_sample_store(px, 4);
    }
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

/* The window image to the swapchain. The swapchain is only ever recreated
 * at a frame's start (sync_swapchain): here a frame whose window image does
 * not match it (cannot happen, but a blit outside the swapchain image loses
 * the device) or whose image cannot be acquired (OUT_OF_DATE: the window
 * changed) is submitted without presenting, and the next frame recreates
 * the swapchain. */
static void present_to_swapchain(void)
{
    if (!swapchain || vk.lost || window_image.width != (int)swapchain_extent.width
        || window_image.height != (int)swapchain_extent.height) {
        if (swapchain && !vk.lost)
            swapchain_stale = 1;
        vk_frame_submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
        return;
    }
    uint32_t index = 0;
    VkSemaphore wait = acquired[vk.frame];
    /* Bounded: a window being reconfigured may hold its images for a while. */
    VkResult result
        = vkAcquireNextImageKHR(vk.device, swapchain, 1000ull * 1000 * 1000, wait, VK_NULL_HANDLE, &index);
    if (result == VK_SUBOPTIMAL_KHR)
        swapchain_stale = 1; /* still presentable: present this one, recreate for the next frame */
    else if (result != VK_SUCCESS) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
            swapchain_stale = 1;
        else if (result == VK_ERROR_DEVICE_LOST)
            vk_device_lost("vkAcquireNextImageKHR");
        else if (result != VK_TIMEOUT && result != VK_NOT_READY) {
            fprintf(stderr, "[vk] vkAcquireNextImageKHR failed (%d): recreating the swapchain\n", (int)result);
            swapchain_stale = 1;
        }
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
    if (vk.lost)
        return; /* not submitted: nothing signals `done` */
    VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &done,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &index };
    result = vkQueuePresentKHR(vk.queue, &present);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
        ++pc_window_presents;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        swapchain_stale = 1;
    else if (result == VK_ERROR_DEVICE_LOST)
        vk_device_lost("vkQueuePresentKHR");
    else if (result != VK_SUCCESS) {
        fprintf(stderr, "[vk] vkQueuePresentKHR failed (%d): recreating the swapchain\n", (int)result);
        swapchain_stale = 1;
    }
}

void pc_vk_present(const uint16_t* vram, int x, int y, int w, int h)
{
    pc_lighting_sync(); /* lighting quality: first-run preset, Custom on changes */
    sync_swapchain(); /* a resize or window mode switch: the frame boundary */
    if (!ensure_window_image()) {
        vk_frame_submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
        return;
    }
    if (pc_hw_active && w > 0 && h > 0) { /* supersampling: the internal resolution the view needs */
        int view_w, view_h;
        update_view(&view_w, &view_h);
        pc_hw_supersample(view_h, h);
    }
    if (pc_hw_active)
        pc_hw_set_display(x, y, w, h);
    unsigned target = pc_hw_active ? pc_hw_target() : 0;
    pc_post_frame_get(&post_frame); /* the post profile for this present */
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
        brightness_stage = brightness_on(view_w, view_h); /* settings brightness: one more stage (above) */
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
            int fogvol = !pc_gl_show_depth && pc_vk_fogvol_update ? pc_vk_fogvol_update(x, y, w, h, replay, gi) : 0;
            /* The post chain (antialias.h): the lit image -> [FXAA | SMAA] -> [depth of field blur]
             * -> [supersampling's downsample] -> the window. The lit image is view-sized, or with
             * supersampling as large as the display area at the internal resolution. */
            int lit_w = view_w, lit_h = view_h, ssaa = 0;
            if (pc_aa_ssaa > 1.0f && w * pc_hw_scale > view_w && h * pc_hw_scale > view_h)
                ssaa = 1, lit_w = w * pc_hw_scale, lit_h = h * pc_hw_scale;
            int post = pc_gl_aa == PC_AA_FXAA || (pc_gl_aa == PC_AA_SMAA && smaa_ready()) ? pc_gl_aa : PC_AA_OFF;
            int blur = !pc_gl_show_depth && (post_frame.flags & 3);
            /* The post stack's grade pass (post.h): only when an effect is on; never on the depth view. */
            int grade = !pc_gl_show_depth && post_frame.grade && post_vk_ready();
            pc_post_active_grade = grade;
            if (!grade)
                pc_post_active_bloom = 0;
            post_hdr = grade && post_frame.hdr ? 8 : 0;
            post_grade_on = grade;
            int stages = 1 + (post != PC_AA_OFF) + blur + grade + ssaa;
            if ((stages > 1 && !ensure_view(&chain[0], lit_w, lit_h))
                || (stages > 2 && !ensure_view(&chain[1], lit_w, lit_h)))
                stages = 1, post = PC_AA_OFF, blur = ssaa = grade = post_hdr = post_grade_on = 0, lit_w = view_w,
                lit_h = view_h;
            if (post == PC_AA_SMAA
                && (!ensure_image(&smaa_edges, lit_w, lit_h, EDGES_FORMAT)
                    || !ensure_image(&smaa_weights, lit_w, lit_h, WEIGHTS_FORMAT))) {
                post = PC_AA_OFF;
                stages = 1 + blur + grade + ssaa;
            }
            stages += brightness_stage;
            vk_slice ubo = vk_arena(sizeof(present_ubo), vk.properties.limits.minUniformBufferOffsetAlignment);
            if (!ubo.data)
                goto overlay;
            vk_section("present");
            fill_present_ubo(ubo.data, x, y, w, h, lit_w, lit_h, gi, gi_w, gi_h, lo, hi);
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
            VkImageView medium_view = pc_vk_medium_texture(); /* before the pass begins: may upload */
            /* Haze Low (or High where nothing traced it this frame): the room's
             * medium marched per pixel, unshadowed but for the sun's map. */
            pu->quality0[0] = !pc_gl_show_depth && !pu->haze_grid[3]
                && pc_medium_gpu_get(&pu->medium, pc_lighting_low_haze_density(0), pu->room_lo, pu->room_hi)
                /* the uniform haze alone with nothing to light it would only darken: as before, none */
                && (present_light_count > 0 || pu->env_mode[2] || pc_medium_has_fog());
            /* The sky's ambient term where neither the path tracer nor the probes trace the sky. */
            pu->env_mode[1] = pu->env.zenith[3] > 0.5f && !(gi && pc_gi_enabled) && !probes;
            int stage = 0;
            const vk_image* input = stage_begin(stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
            cmd = vk_cmd();
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stages > 1 ? scaled_to_view : scaled_to_window);
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
            vk_descriptors_image(&d, 17, medium_view, vk.linear);
            VkImageView fogvol_light = vk_hw.dummy_color.view, fogvol_aux = vk_hw.dummy_color.view;
            if (fogvol && pc_vk_fogvol_bind(&fogvol_light, &fogvol_aux, pu->fogvol, pu->fogvol_scale))
                pu->fogvol[2] = 1;
            pu->fogvol[3] = pc_gl_show_depth ? 0 : pc_fog_volume_debug; /* (nothing drawn: black, clear) */
            vk_descriptors_image(&d, 18, fogvol_light, vk.nearest);
            vk_descriptors_image(&d, 19, fogvol_aux, vk.nearest);
            vk_descriptors_image(&d, 20, backdrop_fill(pu), vk.linear); /* the menu's model view (above) */
            vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, scaled_layout);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            if (post != PC_AA_OFF)
                vk_section("aa");
            if (post == PC_AA_FXAA) {
                const vk_image* source = input;
                input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stage + 1 < stages ? fxaa_to_view : fxaa_pipeline);
                vk_descriptors_image(&d, 0, source->view, vk.linear);
                vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaa_layout);
                float push[2] = { 1.0f / lit_w, 1.0f / lit_h };
                vkCmdPushConstants(cmd, fxaa_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            } else if (post == PC_AA_SMAA) {
                const vk_image* source = input;
                smaa_push push = { { 1.0f / lit_w, 1.0f / lit_h, (float)lit_w, (float)lit_h }, { x, y, w, h },
                    { pc_hw_scale, pc_aa_geometry_edges && !pc_gl_show_depth, 0, 0 } };
                /* 1: edges (the reference discards pixels without any: cleared first). */
                begin_single(&smaa_edges, 0, 0, lit_w, lit_h);
                cmd = vk_cmd();
                VkClearAttachment none = { VK_IMAGE_ASPECT_COLOR_BIT, 0, { .color = { { 0, 0, 0, 0 } } } };
                VkClearRect all = { { { 0, 0 }, { (uint32_t)lit_w, (uint32_t)lit_h } }, 0, 1 };
                vkCmdClearAttachments(cmd, 1, &none, 1, &all);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, smaa_edges_pipeline);
                vk_descriptors e = { 0 };
                vk_descriptors_image(&e, 0, source->view, vk.linear);
                vk_descriptors_image(&e, 1, t->depth.view, vk.nearest);
                vk_descriptors_image(&e, 2, t->normal.view, vk.nearest);
                vk_descriptors_push(&e, VK_PIPELINE_BIND_POINT_GRAPHICS, smaa_layout);
                vkCmdPushConstants(cmd, smaa_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, &push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                /* 2: blending weights. */
                begin_single(&smaa_weights, 0, 0, lit_w, lit_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, smaa_weights_pipeline);
                vk_descriptors wd = { 0 };
                vk_descriptors_image(&wd, 0, smaa_edges.view, vk.linear);
                vk_descriptors_image(&wd, 1, smaa_area.view, vk.linear);
                vk_descriptors_image(&wd, 2, smaa_search.view, vk.nearest);
                vk_descriptors_push(&wd, VK_PIPELINE_BIND_POINT_GRAPHICS, smaa_layout);
                vkCmdPushConstants(cmd, smaa_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, &push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                /* 3: neighbourhood blending. */
                input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stage + 1 < stages ? smaa_blend_to_view : smaa_blend_to_window);
                vk_descriptors b = { 0 };
                vk_descriptors_image(&b, 0, source->view, vk.linear);
                vk_descriptors_image(&b, 1, smaa_weights.view, vk.linear);
                vk_descriptors_image(&b, 2, smaa_weights.view, vk.linear); /* unused binding */
                vk_descriptors_push(&b, VK_PIPELINE_BIND_POINT_GRAPHICS, smaa_layout);
                vkCmdPushConstants(cmd, smaa_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, &push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
            if (blur) {
                vk_section("blur");
                const vk_image* source = input;
                input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stage + 1 < stages ? blur_to_view : blur_to_window);
                vk_descriptors_image(&d, 0, source->view, vk.linear);
                vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, blur_layout);
                float most = post_frame.dof_radius > post_frame.tilt[2] ? post_frame.dof_radius : post_frame.tilt[2];
                float push[4] = { (float)lit_w, (float)lit_h, most * lit_h / 720.0f, grade ? 1.0f : 0.0f };
                vkCmdPushConstants(cmd, blur_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
            if (grade) {
                const vk_image* source = input;
                input = stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
                int last = stage + 1 >= stages;
                vk_grade(&post_frame, source, lit_w, lit_h, view_w, view_h, input, last ? view_x : 0, last ? view_y : 0,
                    last);
            }
            if (ssaa) {
                vk_section("downsample");
                const vk_image* source = input;
                stage_begin(++stage, stages, lit_w, lit_h, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stage + 1 < stages ? downsample_to_view : downsample_pipeline);
                vk_descriptors_image(&d, 0, source->view, vk.linear);
                vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, downsample_layout);
                downsample_push push = { { (float)lit_w, (float)lit_h }, { (float)view_w, (float)view_h },
                    pc_aa_downsample };
                vkCmdPushConstants(cmd, downsample_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, &push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
            /* The modern menu's backdrop: keep this picture of the world (backdrop_keep, above). */
            if (!pc_gl_show_depth)
                backdrop_keep(view_x, view_y, view_w, view_h);
            /* Text held back for the window's resolution (gpu_hw.c ui_text_overlay), over the picture, before
             * the brightness curve (into the chain's last image, its view as the viewport). */
            if (brightness_stage)
                begin_single(&brightness_input.image, 0, 0, view_w, view_h);
            else
                begin_single(&window_image, view_x, view_y, view_w, view_h);
            pc_hw_text_overlay(view_x, view_y, view_w, view_h, brightness_stage);
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
                if (brightness_stage) /* settings brightness: into its 16-bit input */
                    begin_single(&brightness_input.image, 0, 0, view_w, view_h);
                else
                    begin_single(&window_image, view_x, view_y, view_w, view_h);
                cmd = vk_cmd();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, brightness_stage ? vram_to_view : vram_pipeline);
                vk_descriptors d = { 0 };
                vk_descriptors_image(&d, 0, present_vram.view, vk.nearest);
                vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_GRAPHICS, vram_layout);
                int push[8] = { x, y, w, h, pc_display_rgb24, 0, 0, 0 };
                vkCmdPushConstants(cmd, vram_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    sizeof push, push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        }
        if (brightness_stage) /* settings brightness (above) */
            brightness_draw(view_x, view_y, view_w, view_h);
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
