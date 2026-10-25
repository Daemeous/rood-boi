/* Vulkan backend core: device, frames, memory, recording, pipelines
 * (vk_core.h; pc/docs/Vulkan_Renderer.md). */
#define _POSIX_C_SOURCE 200809L
#include "vk_core.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lighting_quality.h"
#include "platform.h"
#include "vk_shaders_digest.h"
#include "frame_cost.h" /* ROOD_PERF: fence waits */

#define VK_DEFINE(name) PFN_##name name;
VK_GLOBAL_FUNCTIONS(VK_DEFINE)
VK_INSTANCE_FUNCTIONS(VK_DEFINE)
VK_DEVICE_FUNCTIONS(VK_DEFINE)
VK_OPTIONAL_DEVICE_FUNCTIONS(VK_DEFINE)
#undef VK_DEFINE
PFN_vkGetInstanceProcAddr vk_get_instance_proc_addr;
vk_context vk;
int pc_vk_active;

int vk_check(VkResult result, const char* what)
{
    if (result != VK_SUCCESS)
        fprintf(stderr, "[vk] %s failed (%d)\n", what, (int)result);
    return result == VK_SUCCESS;
}

/* ---- object tracking ----
 * An engine reload (build_boot.py --engine-library; pc/docs/plans/
 * 2026-10-19-engine-reload.md) tears the device down and the new library
 * builds its own. Most objects have no teardown of their own, so the create
 * and destroy entry points vk_core hands out are wrapped: every object made
 * through them is recorded until destroyed, and vk_core_teardown destroys
 * what is still alive, newest first, before the device. Dear ImGui's
 * backend loads its own entry points and destroys its objects itself. */
#define VK_TRACKED(X)                                                                                                  \
    X(Buffer, VkBuffer, VkBufferCreateInfo)                                                                            \
    X(Image, VkImage, VkImageCreateInfo)                                                                               \
    X(ImageView, VkImageView, VkImageViewCreateInfo)                                                                   \
    X(Sampler, VkSampler, VkSamplerCreateInfo)                                                                         \
    X(ShaderModule, VkShaderModule, VkShaderModuleCreateInfo)                                                          \
    X(PipelineLayout, VkPipelineLayout, VkPipelineLayoutCreateInfo)                                                    \
    X(PipelineCache, VkPipelineCache, VkPipelineCacheCreateInfo)                                                       \
    X(DescriptorSetLayout, VkDescriptorSetLayout, VkDescriptorSetLayoutCreateInfo)                                     \
    X(DescriptorPool, VkDescriptorPool, VkDescriptorPoolCreateInfo)                                                    \
    X(QueryPool, VkQueryPool, VkQueryPoolCreateInfo)                                                                   \
    X(CommandPool, VkCommandPool, VkCommandPoolCreateInfo)                                                             \
    X(Fence, VkFence, VkFenceCreateInfo)                                                                               \
    X(Semaphore, VkSemaphore, VkSemaphoreCreateInfo)                                                                   \
    X(SwapchainKHR, VkSwapchainKHR, VkSwapchainCreateInfoKHR)                                                          \
    X(AccelerationStructureKHR, VkAccelerationStructureKHR, VkAccelerationStructureCreateInfoKHR)
enum {
#define KIND(name, handle, info) TRACK_##name,
    VK_TRACKED(KIND)
#undef KIND
    TRACK_MEMORY,
    TRACK_PIPELINE,
    TRACK_DEAD
};
static struct tracked {
    uint64_t handle;
    int kind;
} * tracked;
static size_t tracked_count, tracked_capacity, tracked_dead;
static void track_add(int kind, uint64_t handle)
{
    if (tracked_count == tracked_capacity) {
        size_t capacity = tracked_capacity ? tracked_capacity * 2 : 1024;
        struct tracked* grown = realloc(tracked, capacity * sizeof *grown);
        if (!grown)
            return; /* not destroyed at a reload: a leak, not a fault */
        tracked = grown, tracked_capacity = capacity;
    }
    tracked[tracked_count++] = (struct tracked) { handle, kind };
}
static void track_remove(int kind, uint64_t handle)
{
    /* Newest first: short-lived objects (shader modules, staging) are near the end. */
    for (size_t i = tracked_count; i-- > 0;)
        if (tracked[i].handle == handle && tracked[i].kind == kind) {
            if (i + 1 == tracked_count)
                --tracked_count;
            else
                tracked[i].kind = TRACK_DEAD, ++tracked_dead;
            break;
        }
    if (tracked_dead > 256 && tracked_dead > tracked_count / 2) {
        size_t kept = 0;
        for (size_t i = 0; i < tracked_count; i++)
            if (tracked[i].kind != TRACK_DEAD)
                tracked[kept++] = tracked[i];
        tracked_count = kept, tracked_dead = 0;
    }
}
#define WRAP(name, handle, info)                                                                                       \
    static PFN_vkCreate##name real_create_##name;                                                                      \
    static PFN_vkDestroy##name real_destroy_##name;                                                                    \
    static VKAPI_ATTR VkResult VKAPI_CALL track_create_##name(VkDevice device, const info* create,                    \
        const VkAllocationCallbacks* allocator, handle* out)                                                           \
    {                                                                                                                  \
        VkResult result = real_create_##name(device, create, allocator, out);                                          \
        if (result == VK_SUCCESS && *out)                                                                              \
            track_add(TRACK_##name, (uint64_t)(uintptr_t)*out);                                                       \
        return result;                                                                                                 \
    }                                                                                                                  \
    static VKAPI_ATTR void VKAPI_CALL track_destroy_##name(VkDevice device, handle object,                            \
        const VkAllocationCallbacks* allocator)                                                                        \
    {                                                                                                                  \
        if (object)                                                                                                    \
            track_remove(TRACK_##name, (uint64_t)(uintptr_t)object);                                                  \
        real_destroy_##name(device, object, allocator);                                                                \
    }
VK_TRACKED(WRAP)
#undef WRAP
static PFN_vkAllocateMemory real_allocate_memory;
static PFN_vkFreeMemory real_free_memory;
static PFN_vkCreateGraphicsPipelines real_graphics_pipelines;
static PFN_vkCreateComputePipelines real_compute_pipelines;
static PFN_vkDestroyPipeline real_destroy_pipeline;
static PFN_vkDestroyDevice real_destroy_device;
static VKAPI_ATTR VkResult VKAPI_CALL track_allocate_memory(VkDevice device, const VkMemoryAllocateInfo* info,
    const VkAllocationCallbacks* allocator, VkDeviceMemory* out)
{
    VkResult result = real_allocate_memory(device, info, allocator, out);
    if (result == VK_SUCCESS && *out)
        track_add(TRACK_MEMORY, (uint64_t)(uintptr_t)*out);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL track_free_memory(VkDevice device, VkDeviceMemory memory,
    const VkAllocationCallbacks* allocator)
{
    if (memory)
        track_remove(TRACK_MEMORY, (uint64_t)(uintptr_t)memory);
    real_free_memory(device, memory, allocator);
}
static void track_pipelines(VkResult result, uint32_t count, const VkPipeline* pipelines)
{
    /* Pipelines that were made are valid even when another of the batch failed. */
    (void)result;
    for (uint32_t i = 0; i < count; i++)
        if (pipelines[i])
            track_add(TRACK_PIPELINE, (uint64_t)(uintptr_t)pipelines[i]);
}
static VKAPI_ATTR VkResult VKAPI_CALL track_graphics_pipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* infos, const VkAllocationCallbacks* allocator, VkPipeline* out)
{
    VkResult result = real_graphics_pipelines(device, cache, count, infos, allocator, out);
    track_pipelines(result, count, out);
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL track_compute_pipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo* infos, const VkAllocationCallbacks* allocator, VkPipeline* out)
{
    VkResult result = real_compute_pipelines(device, cache, count, infos, allocator, out);
    track_pipelines(result, count, out);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL track_destroy_pipeline(VkDevice device, VkPipeline pipeline,
    const VkAllocationCallbacks* allocator)
{
    if (pipeline)
        track_remove(TRACK_PIPELINE, (uint64_t)(uintptr_t)pipeline);
    real_destroy_pipeline(device, pipeline, allocator);
}
extern int pc_engine_reload_request(void) __attribute__((weak)); /* engine_host.c (--engine-library) */
/* After the device's entry points are loaded: hand out the tracking ones. */
static void track_install(void)
{
#define INSTALL(name, handle, info)                                                                                    \
    real_create_##name = vkCreate##name, real_destroy_##name = vkDestroy##name;                                         \
    if (vkCreate##name && vkDestroy##name)                                                                             \
        vkCreate##name = track_create_##name, vkDestroy##name = track_destroy_##name;
    VK_TRACKED(INSTALL)
#undef INSTALL
    real_allocate_memory = vkAllocateMemory, vkAllocateMemory = track_allocate_memory;
    real_free_memory = vkFreeMemory, vkFreeMemory = track_free_memory;
    real_graphics_pipelines = vkCreateGraphicsPipelines, vkCreateGraphicsPipelines = track_graphics_pipelines;
    real_compute_pipelines = vkCreateComputePipelines, vkCreateComputePipelines = track_compute_pipelines;
    real_destroy_pipeline = vkDestroyPipeline, vkDestroyPipeline = track_destroy_pipeline;
    real_destroy_device = vkDestroyDevice;
}
static void destroy_tracked(const struct tracked* t)
{
    switch (t->kind) {
#define DESTROY(name, type, info)                                                                                      \
    case TRACK_##name:                                                                                                 \
        real_destroy_##name(vk.device, (type)(uintptr_t)t->handle, NULL);                                              \
        break;
        VK_TRACKED(DESTROY)
#undef DESTROY
    case TRACK_MEMORY:
        real_free_memory(vk.device, (VkDeviceMemory)(uintptr_t)t->handle, NULL);
        break;
    case TRACK_PIPELINE:
        real_destroy_pipeline(vk.device, (VkPipeline)(uintptr_t)t->handle, NULL);
        break;
    default:
        break;
    }
}

/* ---- frames ---- */

typedef struct deferred {
    int kind; /* 0 buffer, 1 image */
    vk_buffer buffer;
    vk_image image;
} deferred;
typedef struct {
    vk_frame_callback callback;
    void* context;
} frame_callback;
#define ARENA_CHUNK (16u << 20)
typedef struct {
    vk_buffer buffer;
    VkDeviceSize used;
} arena_chunk;
static struct {
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    int submitted; /* the fence signals when this slot's commands finish (0: never submitted, or failed) */
    arena_chunk* chunks;
    int chunk_count, chunk_current;
    deferred* destroy;
    int destroy_count, destroy_capacity;
    frame_callback* callbacks;
    int callback_count, callback_capacity;
} frames[VK_FRAMES];
static int recording;
static int rendering;
/* ROOD_PERF: GPU time per frame from timestamps at the start and the end of
 * each frame's commands (read when the frame's slot comes round). */
static VkQueryPool timestamps;
static int perf = -1, frame_started[VK_FRAMES], frame_timed[VK_FRAMES];
static double gpu_sum, gpu_max;
static int gpu_count;
/* Named sections inside a frame (vk_section): a timestamp at each mark and
 * one at the end of the frame; averaged per section over the same 300
 * frames as the frame time. */
#define VK_SECTIONS 127
static VkQueryPool section_pool;
static const char* section_names[VK_FRAMES][VK_SECTIONS];
static int section_count[VK_FRAMES];
static struct {
    const char* name;
    double sum;
} section_totals[VK_SECTIONS];
static int section_total_count;
/* The same sections accumulated for RPC perf until its {reset: true}. */
static struct {
    const char* name;
    double sum;
} section_rpc[VK_SECTIONS];
static int section_rpc_count, section_rpc_frames;
static vk_attachments current_attachments;
static VkDebugUtilsMessengerEXT messenger;

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    (void)type, (void)user;
    static int reported;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && reported++ < 200)
        fprintf(stderr, "[vk] validation: %s\n", data->pMessage);
    return VK_FALSE;
}

static int has_extension(const VkExtensionProperties* list, uint32_t count, const char* name)
{
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(list[i].extensionName, name))
            return 1;
    return 0;
}

static int format_ok(VkFormat format, VkFormatFeatureFlags needed)
{
    VkFormatProperties p;
    vkGetPhysicalDeviceFormatProperties(vk.gpu, format, &p);
    return (p.optimalTilingFeatures & needed) == needed;
}

/* ---- the pipeline cache ----
 * Pipelines compile once per driver, device and shader set: the driver's
 * pipeline cache data is kept between runs in the user's cache directory
 * ($XDG_CACHE_HOME or ~/.cache, %LOCALAPPDATA% on Windows: rood-boi/
 * vulkan-pipelines-<vendor>-<device>.bin; ROOD_VK_PIPELINE_CACHE=path
 * chooses the file, =0 none). The data's own header (vendor, device, the
 * driver's cache UUID) is checked before use, and a different driver
 * version or shader set (VK_SHADERS_DIGEST) starts the cache afresh. Saved
 * once pipeline creation has settled for 120 frames (vk_frame_submit), to a
 * temporary file renamed over the old one. Only start-up time changes. */
#define PIPELINE_CACHE_MAGIC "ROODVKPC"
typedef struct {
    char magic[8];
    uint32_t version; /* 1 */
    uint32_t driver_version;
    char shaders[64]; /* VK_SHADERS_DIGEST */
    uint64_t size; /* of the driver's data that follows */
} pipeline_cache_prefix;
static struct {
    char path[1024];
    int dirty, created;
    uint64_t dirty_serial, ns, loaded; /* loaded: bytes read at start */
} pipeline_cache;

static int pipeline_cache_path(char* out, size_t size)
{
    const char* env = getenv("ROOD_VK_PIPELINE_CACHE");
    if (env) {
        if (!*env || !strcmp(env, "0"))
            return 0;
        snprintf(out, size, "%s", env);
        return 1;
    }
    char dir[900];
#ifdef _WIN32
    const char* base = getenv("LOCALAPPDATA");
    if (!base || !*base)
        return 0;
    snprintf(dir, sizeof dir, "%s/rood-boi", base);
#else
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (xdg && *xdg == '/') {
        pc_mkdir(xdg);
        snprintf(dir, sizeof dir, "%s/rood-boi", xdg);
    } else if (home && *home) {
        snprintf(dir, sizeof dir, "%s/.cache", home);
        pc_mkdir(dir);
        snprintf(dir, sizeof dir, "%s/.cache/rood-boi", home);
    } else
        return 0;
#endif
    pc_mkdir(dir);
    snprintf(out, size, "%s/vulkan-pipelines-%04x-%04x.bin", dir, vk.properties.vendorID, vk.properties.deviceID);
    return 1;
}

/* The driver's data is for this device and driver build. */
static int pipeline_cache_data_ok(const unsigned char* data, uint64_t size)
{
    uint32_t header[4];
    if (size < 16 + VK_UUID_SIZE)
        return 0;
    memcpy(header, data, sizeof header);
    return header[0] >= 16 + VK_UUID_SIZE && header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
        && header[2] == vk.properties.vendorID && header[3] == vk.properties.deviceID
        && !memcmp(data + 16, vk.properties.pipelineCacheUUID, VK_UUID_SIZE);
}

static void pipeline_cache_init(void)
{
    unsigned char* data = NULL;
    uint64_t size = 0;
    const char* why = "disabled (ROOD_VK_PIPELINE_CACHE=0)";
    if (pipeline_cache_path(pipeline_cache.path, sizeof pipeline_cache.path)) {
        FILE* f = fopen(pipeline_cache.path, "rb");
        pipeline_cache_prefix p;
        why = "none saved yet";
        if (f && fread(&p, sizeof p, 1, f) == 1) {
            why = "unrecognised file";
            if (memcmp(p.magic, PIPELINE_CACHE_MAGIC, 8) || p.version != 1)
                ;
            else if (p.driver_version != vk.properties.driverVersion)
                why = "the driver changed";
            else if (memcmp(p.shaders, VK_SHADERS_DIGEST, sizeof p.shaders))
                why = "the shaders changed";
            else if (p.size && p.size < ((uint64_t)1 << 30) && (data = malloc((size_t)p.size))
                     && fread(data, 1, (size_t)p.size, f) == p.size && pipeline_cache_data_ok(data, p.size))
                size = p.size;
            else
                why = "not for this device";
        }
        if (f)
            fclose(f);
    } else
        pipeline_cache.path[0] = 0;
    VkPipelineCacheCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = (size_t)size,
        .pInitialData = size ? data : NULL };
    if (vkCreatePipelineCache(vk.device, &info, NULL, &vk.pipeline_cache) != VK_SUCCESS) {
        info.initialDataSize = 0, info.pInitialData = NULL, size = 0, why = "the driver refused the data";
        if (vkCreatePipelineCache(vk.device, &info, NULL, &vk.pipeline_cache) != VK_SUCCESS)
            vk.pipeline_cache = VK_NULL_HANDLE;
    }
    free(data);
    pipeline_cache.loaded = size;
    if (size)
        fprintf(stderr, "[vk] pipeline cache: %llu bytes from %s\n", (unsigned long long)size, pipeline_cache.path);
    else
        fprintf(stderr, "[vk] pipeline cache: starting empty (%s)\n", why);
}

static void pipeline_cache_save(void)
{
    pipeline_cache.dirty = 0;
    if (!vk.pipeline_cache || !pipeline_cache.path[0])
        return;
    size_t size = 0;
    /* Entries are only added: the same size is the same data (every
     * pipeline came from the cache), so it is not rewritten. */
    if (vkGetPipelineCacheData(vk.device, vk.pipeline_cache, &size, NULL) != VK_SUCCESS || !size
        || size == pipeline_cache.loaded)
        return;
    void* data = malloc(size);
    if (!data || vkGetPipelineCacheData(vk.device, vk.pipeline_cache, &size, data) != VK_SUCCESS) {
        free(data);
        return;
    }
    pipeline_cache_prefix p = { .version = 1, .driver_version = vk.properties.driverVersion, .size = size };
    memcpy(p.magic, PIPELINE_CACHE_MAGIC, 8);
    memcpy(p.shaders, VK_SHADERS_DIGEST, sizeof p.shaders);
    char temp[1100];
    snprintf(temp, sizeof temp, "%s.%llx.tmp", pipeline_cache.path, (unsigned long long)SDL_GetTicksNS());
    FILE* f = fopen(temp, "wb");
    int ok = f && fwrite(&p, sizeof p, 1, f) == 1 && fwrite(data, 1, size, f) == size;
    if (f && fclose(f))
        ok = 0;
    free(data);
    if (ok && !pc_replace_file(temp, pipeline_cache.path)) {
        pipeline_cache.loaded = size;
        fprintf(stderr, "[vk] pipeline cache: saved %zu bytes (%d pipelines created in %.1f ms this run)\n", size,
            pipeline_cache.created, pipeline_cache.ns * 1e-6);
    } else
        remove(temp);
}

void vk_pipeline_cache_note(uint64_t ns)
{
    pipeline_cache.created++;
    pipeline_cache.ns += ns;
    pipeline_cache.dirty = 1;
    pipeline_cache.dirty_serial = vk.serial;
    if (vk.serial && ns > 5000000) /* a hitch once frames are being presented */
        fprintf(stderr, "[vk] pipeline created after frame %llu in %.1f ms\n", (unsigned long long)vk.serial,
            ns * 1e-6);
}

int vk_core_init(const char* const* instance_extensions, uint32_t extension_count)
{
    vk_get_instance_proc_addr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (!vk_get_instance_proc_addr) {
        fprintf(stderr, "[vk] no Vulkan loader: %s\n", SDL_GetError());
        return 0;
    }
#define LOAD_GLOBAL(name) name = (PFN_##name)vk_get_instance_proc_addr(NULL, #name);
    VK_GLOBAL_FUNCTIONS(LOAD_GLOBAL)
#undef LOAD_GLOBAL
    if (!vkCreateInstance)
        return 0;
    uint32_t loader = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion)
        vkEnumerateInstanceVersion(&loader);
    if (loader < VK_API_VERSION_1_3) {
        fprintf(stderr, "[vk] the Vulkan loader is older than 1.3\n");
        return 0;
    }
    /* Extensions: the window system's (when all exist), debug utils for
     * validation (ROOD_VK_VALIDATE=1). */
    uint32_t available_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &available_count, NULL);
    VkExtensionProperties* available = calloc(available_count ? available_count : 1, sizeof *available);
    vkEnumerateInstanceExtensionProperties(NULL, &available_count, available);
    const char* extensions[32];
    uint32_t count = 0;
    int surfaces = 1;
    for (uint32_t i = 0; i < extension_count; i++)
        surfaces &= has_extension(available, available_count, instance_extensions[i]);
    if (surfaces)
        for (uint32_t i = 0; i < extension_count && count < 24; i++)
            extensions[count++] = instance_extensions[i];
    const char* validate = getenv("ROOD_VK_VALIDATE");
    int validation = validate && *validate == '1';
    const char* layers[1] = { "VK_LAYER_KHRONOS_validation" };
    if (validation && has_extension(available, available_count, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        extensions[count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    free(available);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "rood-boi",
        .pEngineName = "rood-boi",
        .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo create = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledExtensionCount = count,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount = validation ? 1 : 0,
        .ppEnabledLayerNames = layers };
    VkResult result = vkCreateInstance(&create, NULL, &vk.instance);
    if (result != VK_SUCCESS && validation) {
        fprintf(stderr, "[vk] no validation layer; continuing without\n");
        validation = 0;
        create.enabledLayerCount = 0;
        if (count && !strcmp(extensions[count - 1], VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            create.enabledExtensionCount = --count;
        result = vkCreateInstance(&create, NULL, &vk.instance);
    }
    if (!vk_check(result, "vkCreateInstance"))
        return 0;
#define LOAD_INSTANCE(name) name = (PFN_##name)vk_get_instance_proc_addr(vk.instance, #name);
    VK_INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    if (validation) {
        PFN_vkCreateDebugUtilsMessengerEXT create_messenger
            = (PFN_vkCreateDebugUtilsMessengerEXT)vk_get_instance_proc_addr(vk.instance, "vkCreateDebugUtilsMessengerEXT");
        VkDebugUtilsMessengerCreateInfoEXT info = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_message };
        if (create_messenger)
            create_messenger(vk.instance, &info, NULL, &messenger);
        fprintf(stderr, "[vk] validation on\n");
    }

    /* The GPU: ROOD_VK_DEVICE=N, else the first discrete one with 1.3. */
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(vk.instance, &gpu_count, NULL);
    VkPhysicalDevice gpus[16];
    if (gpu_count > 16)
        gpu_count = 16;
    vkEnumeratePhysicalDevices(vk.instance, &gpu_count, gpus);
    const char* wanted = getenv("ROOD_VK_DEVICE");
    int pick = -1;
    if (wanted)
        for (uint32_t i = 0; i < gpu_count; i++) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(gpus[i], &p);
            fprintf(stderr, "[vk] device %u: %s\n", i, p.deviceName);
        }
    for (int pass = 0; pass < 2 && pick < 0; pass++)
        for (uint32_t i = 0; i < gpu_count && pick < 0; i++) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(gpus[i], &p);
            if (p.apiVersion < VK_API_VERSION_1_3)
                continue;
            if (wanted) {
                if ((int)i == atoi(wanted))
                    pick = (int)i;
                continue;
            }
            if (pass == 1 || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                pick = (int)i;
        }
    if (pick < 0) {
        fprintf(stderr, "[vk] no Vulkan 1.3 device\n");
        return 0;
    }
    vk.gpu = gpus[pick];
    vkGetPhysicalDeviceProperties(vk.gpu, &vk.properties);
    vkGetPhysicalDeviceMemoryProperties(vk.gpu, &vk.memory);
    vk.timestamp_period = vk.properties.limits.timestampPeriod;

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.gpu, &family_count, NULL);
    VkQueueFamilyProperties families[16];
    if (family_count > 16)
        family_count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.gpu, &family_count, families);
    vk.queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < family_count; i++)
        if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            vk.queue_family = i;
            break;
        }
    if (vk.queue_family == UINT32_MAX)
        return 0;

    uint32_t device_extension_count = 0;
    vkEnumerateDeviceExtensionProperties(vk.gpu, NULL, &device_extension_count, NULL);
    VkExtensionProperties* device_extensions = calloc(device_extension_count + 1, sizeof *device_extensions);
    vkEnumerateDeviceExtensionProperties(vk.gpu, NULL, &device_extension_count, device_extensions);
    const char* enable[16];
    uint32_t enable_count = 0;
    if (!has_extension(device_extensions, device_extension_count, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
        fprintf(stderr, "[vk] %s lacks VK_KHR_push_descriptor\n", vk.properties.deviceName);
        free(device_extensions);
        return 0;
    }
    enable[enable_count++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    if (has_extension(device_extensions, device_extension_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        enable[enable_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    const char* rt = getenv("ROOD_VK_RT");
    int want_rt = !(rt && *rt == '0');
    int ray_query = want_rt && has_extension(device_extensions, device_extension_count, VK_KHR_RAY_QUERY_EXTENSION_NAME)
                 && has_extension(device_extensions, device_extension_count, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                 && has_extension(device_extensions, device_extension_count, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    /* MSAA (antialias.h): 2D drawn into multisampled targets takes every
     * sample at the pixel centre, as if single-sampled. */
    if (has_extension(device_extensions, device_extension_count, VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME)) {
        enable[enable_count++] = VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME;
        vk.sample_locations = 1;
    }
    /* ROOD_GPU_PRIORITY=low (the test runner, pc/tests/run.py): a low
     * priority queue, so a game being played beside the tests keeps the GPU
     * first. Ignored where the driver has no global priority. */
    const char* gpu_priority = getenv("ROOD_GPU_PRIORITY");
    int low_priority = 0;
    if (gpu_priority && !strcmp(gpu_priority, "low")) {
        const char* names[] = { VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME, VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME };
        for (int i = 0; i < 2 && !low_priority; i++)
            if (has_extension(device_extensions, device_extension_count, names[i])) {
                enable[enable_count++] = names[i];
                low_priority = 1;
            }
    }
    free(device_extensions);

    /* Features. */
    VkPhysicalDeviceRayQueryFeaturesKHR rq = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, .pNext = &rq };
    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = ray_query ? &as : NULL };
    VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13 };
    VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f12 };
    vkGetPhysicalDeviceFeatures2(vk.gpu, &features);
    if (!f13.dynamicRendering || !f13.synchronization2 || !features.features.geometryShader
        || !features.features.independentBlend) {
        fprintf(stderr, "[vk] %s lacks dynamic rendering, synchronization2, geometry shaders or independent blend\n",
            vk.properties.deviceName);
        return 0;
    }
    ray_query = ray_query && rq.rayQuery && as.accelerationStructure && f12.bufferDeviceAddress;
    VkPhysicalDeviceRayQueryFeaturesKHR rq_on = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .rayQuery = VK_TRUE };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as_on = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &rq_on,
        .accelerationStructure = VK_TRUE };
    VkPhysicalDeviceVulkan13Features on13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = ray_query ? &as_on : NULL,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
        /* glslang turns `discard` into OpDemoteToHelperInvocation for 1.3 */
        .shaderDemoteToHelperInvocation = f13.shaderDemoteToHelperInvocation };
    VkPhysicalDeviceVulkan12Features on12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &on13,
        .bufferDeviceAddress = ray_query ? VK_TRUE : VK_FALSE,
        .scalarBlockLayout = f12.scalarBlockLayout,
        .hostQueryReset = f12.hostQueryReset };
    VkPhysicalDeviceFeatures2 on = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &on12 };
    on.features.geometryShader = VK_TRUE;
    on.features.independentBlend = VK_TRUE;
    on.features.fragmentStoresAndAtomics = features.features.fragmentStoresAndAtomics;
    on.features.shaderInt64 = features.features.shaderInt64;
    /* NRD's shaders (HLSL through DXC) declare storage images without formats. */
    on.features.shaderStorageImageReadWithoutFormat = features.features.shaderStorageImageReadWithoutFormat;
    on.features.shaderStorageImageWriteWithoutFormat = features.features.shaderStorageImageWriteWithoutFormat;
    on.features.shaderStorageImageExtendedFormats = features.features.shaderStorageImageExtendedFormats;
    if (ray_query) {
        enable[enable_count++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        enable[enable_count++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        enable[enable_count++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
    }
    float priority = 1.0f;
    VkDeviceQueueGlobalPriorityCreateInfoKHR queue_priority = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR,
        .globalPriority = VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR };
    VkDeviceQueueCreateInfo queue = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = low_priority ? &queue_priority : NULL,
        .queueFamilyIndex = vk.queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority };
    VkDeviceCreateInfo device = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &on,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
        .enabledExtensionCount = enable_count,
        .ppEnabledExtensionNames = enable };
    if (low_priority && vkCreateDevice(vk.gpu, &device, NULL, &vk.device) == VK_SUCCESS)
        fprintf(stderr, "[vk] low GPU priority queue (ROOD_GPU_PRIORITY=low)\n");
    else {
        if (low_priority) { /* refused: the normal queue (the extension stays harmlessly enabled) */
            vk.device = VK_NULL_HANDLE;
            queue.pNext = NULL;
        }
        if (!vk_check(vkCreateDevice(vk.gpu, &device, NULL, &vk.device), "vkCreateDevice"))
            return 0;
    }
    vk.ray_query = ray_query;
#define LOAD_DEVICE(name)                                                                                              \
    name = (PFN_##name)vkGetDeviceProcAddr(vk.device, #name);                                                          \
    if (!name) {                                                                                                       \
        fprintf(stderr, "[vk] missing %s\n", #name);                                                                   \
        return 0;                                                                                                      \
    }
    VK_DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
#define LOAD_OPTIONAL(name) name = (PFN_##name)vkGetDeviceProcAddr(vk.device, #name);
    VK_OPTIONAL_DEVICE_FUNCTIONS(LOAD_OPTIONAL)
#undef LOAD_OPTIONAL
    if (pc_engine_reload_request) /* only where the engine can be reloaded (engine_host.c) */
        track_install();
    if (!vkCmdPushDescriptorSetKHR)
        return 0;
    vkGetDeviceQueue(vk.device, vk.queue_family, 0, &vk.queue);
    pipeline_cache_init();
    if (ray_query) {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR as_properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
        VkPhysicalDeviceProperties2 properties = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &as_properties };
        vkGetPhysicalDeviceProperties2(vk.gpu, &properties);
        vk.scratch_alignment = as_properties.minAccelerationStructureScratchOffsetAlignment;
        if (vk.scratch_alignment < 1)
            vk.scratch_alignment = 256;
    }

    vk.depth_format = VK_FORMAT_X8_D24_UNORM_PACK32;
    VkFormatFeatureFlags depth_needs = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                                     | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
                                     | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (!format_ok(vk.depth_format, depth_needs))
        vk.depth_format = VK_FORMAT_D32_SFLOAT;
    /* Sample counts the multisampled 3D can use: drawn into (colour and
     * depth), sampled by the resolve, at the target's format. */
    {
        const VkPhysicalDeviceLimits* l = &vk.properties.limits;
        vk.msaa_counts = l->framebufferColorSampleCounts & l->framebufferDepthSampleCounts
                       & l->sampledImageColorSampleCounts & l->sampledImageDepthSampleCounts;
        if (vk.sample_locations) {
            VkPhysicalDeviceSampleLocationsPropertiesEXT locations = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT };
            VkPhysicalDeviceProperties2 properties = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &locations };
            vkGetPhysicalDeviceProperties2(vk.gpu, &properties);
            vk.sample_location_counts = locations.sampleLocationSampleCounts;
            /* One location for every sample of a pixel (no grid): needs variable locations. */
            if (!locations.variableSampleLocations)
                vk.sample_location_counts = 0;
        }
    }

    for (int i = 0; i < VK_FRAMES; i++) {
        VkCommandPoolCreateInfo pool = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = vk.queue_family };
        if (!vk_check(vkCreateCommandPool(vk.device, &pool, NULL, &frames[i].pool), "vkCreateCommandPool"))
            return 0;
        VkCommandBufferAllocateInfo alloc = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frames[i].pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1 };
        if (!vk_check(vkAllocateCommandBuffers(vk.device, &alloc, &frames[i].cmd), "vkAllocateCommandBuffers"))
            return 0;
        VkFenceCreateInfo fence = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        if (!vk_check(vkCreateFence(vk.device, &fence, NULL, &frames[i].fence), "vkCreateFence"))
            return 0;
    }
    VkSamplerCreateInfo sampler = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0 };
    vkCreateSampler(vk.device, &sampler, NULL, &vk.nearest);
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(vk.device, &sampler, NULL, &vk.linear);
    fprintf(stderr, "[vk] %s, Vulkan %u.%u.%u%s, depth %s\n", vk.properties.deviceName,
        VK_API_VERSION_MAJOR(vk.properties.apiVersion), VK_API_VERSION_MINOR(vk.properties.apiVersion),
        VK_API_VERSION_PATCH(vk.properties.apiVersion), ray_query ? ", ray query" : "",
        vk.depth_format == VK_FORMAT_D32_SFLOAT ? "D32F" : "D24");
    return 1;
}

static void run_frame_done(int slot);
void vk_core_shutdown(void)
{
    if (!vk.device)
        return;
    vkDeviceWaitIdle(vk.device);
    for (int i = 0; i < VK_FRAMES; i++)
        run_frame_done(i);
    if (pipeline_cache.dirty)
        pipeline_cache_save();
}

/* Destroy everything: the objects still alive (newest first), the device,
 * `surface` (the presenter's), the debug messenger and the instance. The
 * window stays (an engine reload keeps it). vk is zeroed: vk_core_init can
 * run again. */
void vk_core_teardown(VkSurfaceKHR surface)
{
    if (!vk.instance)
        return;
    if (vk.device) {
        vkDeviceWaitIdle(vk.device);
        for (int i = 0; i < VK_FRAMES; i++)
            run_frame_done(i);
        if (pipeline_cache.dirty)
            pipeline_cache_save();
        size_t destroyed = 0;
        for (size_t i = tracked_count; i-- > 0;)
            if (tracked[i].kind != TRACK_DEAD) {
                destroy_tracked(&tracked[i]);
                ++destroyed;
            }
        tracked_count = tracked_dead = 0;
        (real_destroy_device ? real_destroy_device : vkDestroyDevice)(vk.device, NULL);
        fprintf(stderr, "[vk] device torn down (%zu objects destroyed)\n", destroyed);
    }
    if (surface && vkDestroySurfaceKHR)
        vkDestroySurfaceKHR(vk.instance, surface, NULL);
    if (messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger
            = (PFN_vkDestroyDebugUtilsMessengerEXT)vk_get_instance_proc_addr(vk.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger)
            destroy_messenger(vk.instance, messenger, NULL);
        messenger = VK_NULL_HANDLE;
    }
    vkDestroyInstance(vk.instance, NULL);
    memset(&vk, 0, sizeof vk);
    pc_vk_active = 0;
}

/* ---- memory ---- */

static int memory_type(uint32_t bits, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags required)
{
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < vk.memory.memoryTypeCount; i++) {
            VkMemoryPropertyFlags f = vk.memory.memoryTypes[i].propertyFlags;
            if (!(bits & (1u << i)) || (f & required) != required)
                continue;
            if (pass == 0 && (f & wanted) != wanted)
                continue;
            return (int)i;
        }
    return -1;
}

int vk_buffer_create(vk_buffer* b, VkDeviceSize size, VkBufferUsageFlags usage, int host)
{
    memset(b, 0, sizeof *b);
    if (!vk.ray_query)
        usage &= ~(VkBufferUsageFlags)(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                       | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                       | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
    VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (!vk_check(vkCreateBuffer(vk.device, &info, NULL, &b->buffer), "vkCreateBuffer"))
        return 0;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk.device, b->buffer, &req);
    VkMemoryPropertyFlags required = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                          : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    /* host 2 = the CPU reads it back: cached system memory. Host-visible
     * device memory (ReBAR) is fast to write but uncached to read, which made
     * a 1280x960 readback take over a second. */
    VkMemoryPropertyFlags wanted = host == 2 ? required | VK_MEMORY_PROPERTY_HOST_CACHED_BIT
                                             : required | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    int type = memory_type(req.memoryTypeBits, wanted, required);
    if (type < 0) {
        vkDestroyBuffer(vk.device, b->buffer, NULL);
        b->buffer = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryAllocateFlagsInfo flags = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT };
    VkMemoryAllocateInfo alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &flags : NULL,
        .allocationSize = req.size,
        .memoryTypeIndex = (uint32_t)type };
    if (!vk_check(vkAllocateMemory(vk.device, &alloc, NULL, &b->memory), "vkAllocateMemory")) {
        vkDestroyBuffer(vk.device, b->buffer, NULL);
        b->buffer = VK_NULL_HANDLE;
        return 0;
    }
    vkBindBufferMemory(vk.device, b->buffer, b->memory, 0);
    if (host)
        vkMapMemory(vk.device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped);
    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) && vkGetBufferDeviceAddress) {
        VkBufferDeviceAddressInfo address = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = b->buffer };
        b->address = vkGetBufferDeviceAddress(vk.device, &address);
    }
    b->size = size;
    return 1;
}
void vk_buffer_destroy(vk_buffer* b)
{
    if (b->buffer)
        vkDestroyBuffer(vk.device, b->buffer, NULL);
    if (b->memory)
        vkFreeMemory(vk.device, b->memory, NULL);
    memset(b, 0, sizeof *b);
}
static deferred* push_deferred(void)
{
    typeof(frames[0])* f = &frames[vk.frame];
    if (f->destroy_count == f->destroy_capacity) {
        int capacity = f->destroy_capacity ? f->destroy_capacity * 2 : 64;
        deferred* grown = realloc(f->destroy, (size_t)capacity * sizeof *grown);
        if (!grown)
            return NULL;
        f->destroy = grown;
        f->destroy_capacity = capacity;
    }
    return &f->destroy[f->destroy_count++];
}
void vk_buffer_destroy_later(vk_buffer* b)
{
    if (!b->buffer)
        return;
    deferred* d = push_deferred();
    if (!d) {
        vkDeviceWaitIdle(vk.device);
        vk_buffer_destroy(b);
        return;
    }
    d->kind = 0;
    d->buffer = *b;
    memset(b, 0, sizeof *b);
}

int vk_image_create(vk_image* image, int width, int height, VkFormat format, VkImageUsageFlags usage)
{
    return vk_image_create_ms(image, width, height, format, usage, 1);
}
int vk_image_create_ms(vk_image* image, int width, int height, VkFormat format, VkImageUsageFlags usage, int samples)
{
    memset(image, 0, sizeof *image);
    int depth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_X8_D24_UNORM_PACK32
             || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D16_UNORM;
    VkImageCreateInfo info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = depth && samples > 1 && vk.sample_location_counts
            ? VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT
            : 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = samples > 1 ? (VkSampleCountFlagBits)samples : VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    if (!vk_check(vkCreateImage(vk.device, &info, NULL, &image->image), "vkCreateImage"))
        return 0;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, image->image, &req);
    int type = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VkMemoryAllocateInfo alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = (uint32_t)type };
    if (type < 0 || !vk_check(vkAllocateMemory(vk.device, &alloc, NULL, &image->memory), "vkAllocateMemory")) {
        vkDestroyImage(vk.device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
        return 0;
    }
    vkBindImageMemory(vk.device, image->image, image->memory, 0);
    image->aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (!(usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                   | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
        goto viewless;
    VkImageViewCreateInfo view = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = { image->aspect, 0, 1, 0, 1 } };
    if (!vk_check(vkCreateImageView(vk.device, &view, NULL, &image->view), "vkCreateImageView"))
        return 0;
viewless:
    image->format = format;
    image->width = width;
    image->height = height;
    /* Every image lives in GENERAL; start it cleared (colour 0, depth far). */
    vk_end_rendering();
    VkCommandBuffer cmd = vk_cmd();
    VkImageMemoryBarrier2 barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->image,
        .subresourceRange = { image->aspect, 0, 1, 0, 1 } };
    VkDependencyInfo dependency = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier };
    vkCmdPipelineBarrier2(cmd, &dependency);
    if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        VkImageSubresourceRange range = { image->aspect, 0, 1, 0, 1 };
        if (depth) {
            VkClearDepthStencilValue far_depth = { 1.0f, 0 };
            vkCmdClearDepthStencilImage(cmd, image->image, VK_IMAGE_LAYOUT_GENERAL, &far_depth, 1, &range);
        } else {
            VkClearColorValue zero = { { 0, 0, 0, 0 } };
            vkCmdClearColorImage(cmd, image->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        }
        vk_barrier();
    }
    return 1;
}
int vk_image_create_3d(vk_image* image, int width, int height, int depth, VkFormat format, VkImageUsageFlags usage)
{
    memset(image, 0, sizeof *image);
    VkImageCreateInfo info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_3D,
        .format = format,
        .extent = { (uint32_t)width, (uint32_t)height, (uint32_t)depth },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    if (!vk_check(vkCreateImage(vk.device, &info, NULL, &image->image), "vkCreateImage 3D"))
        return 0;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, image->image, &req);
    int type = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VkMemoryAllocateInfo alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = (uint32_t)type };
    if (type < 0 || !vk_check(vkAllocateMemory(vk.device, &alloc, NULL, &image->memory), "vkAllocateMemory")) {
        vkDestroyImage(vk.device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
        return 0;
    }
    vkBindImageMemory(vk.device, image->image, image->memory, 0);
    image->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewCreateInfo view = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_3D,
        .format = format,
        .subresourceRange = { image->aspect, 0, 1, 0, 1 } };
    if (!vk_check(vkCreateImageView(vk.device, &view, NULL, &image->view), "vkCreateImageView 3D"))
        return 0;
    image->format = format;
    image->width = width;
    image->height = height;
    vk_end_rendering();
    VkImageMemoryBarrier2 barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->image,
        .subresourceRange = { image->aspect, 0, 1, 0, 1 } };
    VkDependencyInfo dependency = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier };
    vkCmdPipelineBarrier2(vk_cmd(), &dependency);
    return 1;
}
void vk_image_destroy(vk_image* image)
{
    if (image->view)
        vkDestroyImageView(vk.device, image->view, NULL);
    if (image->image)
        vkDestroyImage(vk.device, image->image, NULL);
    if (image->memory)
        vkFreeMemory(vk.device, image->memory, NULL);
    memset(image, 0, sizeof *image);
}
void vk_image_destroy_later(vk_image* image)
{
    if (!image->image)
        return;
    deferred* d = push_deferred();
    if (!d) {
        vkDeviceWaitIdle(vk.device);
        vk_image_destroy(image);
        return;
    }
    d->kind = 1;
    d->image = *image;
    memset(image, 0, sizeof *image);
}

/* ---- arena ---- */

vk_slice vk_arena(VkDeviceSize size, VkDeviceSize alignment)
{
    typeof(frames[0])* f = &frames[vk.frame];
    vk_slice slice = { VK_NULL_HANDLE, 0, NULL };
    if (alignment < 16)
        alignment = 16;
    /* Chunks after the current one are unused in this frame (all are reset
     * when the slot comes round): try them in order, else add one. */
    for (;;) {
        if (f->chunk_current < f->chunk_count) {
            arena_chunk* c = &f->chunks[f->chunk_current];
            VkDeviceSize start = (c->used + alignment - 1) & ~(alignment - 1);
            if (start + size <= c->buffer.size) {
                c->used = start + size;
                slice.buffer = c->buffer.buffer;
                slice.offset = start;
                slice.data = (char*)c->buffer.mapped + start;
                slice.address = c->buffer.address ? c->buffer.address + start : 0;
                return slice;
            }
            ++f->chunk_current;
            continue;
        }
        arena_chunk* grown = realloc(f->chunks, (size_t)(f->chunk_count + 1) * sizeof *grown);
        if (!grown)
            return slice;
        f->chunks = grown;
        arena_chunk* c = &f->chunks[f->chunk_count];
        VkDeviceSize chunk = size + alignment > ARENA_CHUNK ? size + alignment : ARENA_CHUNK;
        if (!vk_buffer_create(&c->buffer, chunk,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                    | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                    | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                1)) {
            fprintf(stderr, "[vk] out of arena memory (%llu bytes)\n", (unsigned long long)size);
            return slice;
        }
        c->used = 0;
        f->chunk_current = f->chunk_count++;
    }
}

/* ---- recording ---- */

VkCommandBuffer vk_cmd(void)
{
    typeof(frames[0])* f = &frames[vk.frame];
    if (!recording) {
        VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(f->cmd, &begin);
        recording = 1;
        if (perf < 0) {
            perf = getenv("ROOD_PERF") != NULL;
            VkQueryPoolCreateInfo info = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = 2 * VK_FRAMES };
            if (perf && !vk_check(vkCreateQueryPool(vk.device, &info, NULL, &timestamps), "vkCreateQueryPool"))
                perf = 0;
            info.queryCount = (VK_SECTIONS + 1) * VK_FRAMES;
            if (perf && !vk_check(vkCreateQueryPool(vk.device, &info, NULL, &section_pool), "vkCreateQueryPool"))
                section_pool = VK_NULL_HANDLE;
        }
        if (perf && !frame_started[vk.frame]) {
            vkCmdResetQueryPool(f->cmd, timestamps, (uint32_t)vk.frame * 2, 2);
            if (section_pool)
                vkCmdResetQueryPool(f->cmd, section_pool, (uint32_t)vk.frame * (VK_SECTIONS + 1), VK_SECTIONS + 1);
            section_count[vk.frame] = 0;
            vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps, (uint32_t)vk.frame * 2);
            frame_started[vk.frame] = 1;
            if (section_pool) { /* the frame's drawing (VRAM target, native lists) until the first section */
                section_names[vk.frame][0] = "draw";
                section_count[vk.frame] = 1;
                vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, section_pool,
                    (uint32_t)(vk.frame * (VK_SECTIONS + 1)));
            }
        }
        /* Earlier submissions wrote what this one reads. */
        VkMemoryBarrier2 barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT };
        VkDependencyInfo dependency = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier };
        vkCmdPipelineBarrier2(f->cmd, &dependency);
    }
    return f->cmd;
}
void vk_barrier(void)
{
    pc_cost_count(PC_COUNT_BARRIERS, 1);
    VkMemoryBarrier2 barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT };
    VkDependencyInfo dependency = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier };
    vkCmdPipelineBarrier2(vk_cmd(), &dependency);
}
int vk_rendering_open(void) { return rendering; }
void vk_end_rendering(void)
{
    if (!rendering)
        return;
    vkCmdEndRendering(vk_cmd());
    rendering = 0;
    vk_barrier();
}
void vk_begin_rendering(const vk_attachments* a)
{
    if (rendering && !memcmp(a, &current_attachments, sizeof *a))
        return;
    vk_end_rendering();
    VkRenderingAttachmentInfo colors[5], depth;
    for (int i = 0; i < a->color_count; i++)
        colors[i] = (VkRenderingAttachmentInfo){ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = a->colors[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
    depth = (VkRenderingAttachmentInfo){ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = a->depth,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
    VkRenderingInfo info = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { { 0, 0 }, { (uint32_t)a->width, (uint32_t)a->height } },
        .layerCount = 1,
        .colorAttachmentCount = (uint32_t)a->color_count,
        .pColorAttachments = colors,
        .pDepthAttachment = a->depth ? &depth : NULL };
    vkCmdBeginRendering(vk_cmd(), &info);
    pc_cost_count(PC_COUNT_PASSES, 1);
    current_attachments = *a;
    rendering = 1;
}

static void run_frame_done(int slot)
{
    typeof(frames[0])* f = &frames[slot];
    for (int i = 0; i < f->callback_count; i++)
        f->callbacks[i].callback(f->callbacks[i].context);
    f->callback_count = 0;
    for (int i = 0; i < f->destroy_count; i++) {
        if (f->destroy[i].kind == 0)
            vk_buffer_destroy(&f->destroy[i].buffer);
        else
            vk_image_destroy(&f->destroy[i].image);
    }
    f->destroy_count = 0;
}
void vk_on_frame_done(vk_frame_callback callback, void* context)
{
    typeof(frames[0])* f = &frames[vk.frame];
    if (f->callback_count == f->callback_capacity) {
        int capacity = f->callback_capacity ? f->callback_capacity * 2 : 8;
        frame_callback* grown = realloc(f->callbacks, (size_t)capacity * sizeof *grown);
        if (!grown) {
            callback(context);
            return;
        }
        f->callbacks = grown;
        f->callback_capacity = capacity;
    }
    f->callbacks[f->callback_count].callback = callback;
    f->callbacks[f->callback_count++].context = context;
}

static void submit(VkSemaphore wait, VkPipelineStageFlags wait_stage, VkSemaphore signal)
{
    typeof(frames[0])* f = &frames[vk.frame];
    vk_cmd(); /* an empty frame still submits (its fence is waited for) */
    vk_end_rendering();
    vkEndCommandBuffer(f->cmd);
    recording = 0;
    VkSubmitInfo info = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = wait ? 1 : 0,
        .pWaitSemaphores = &wait,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &f->cmd,
        .signalSemaphoreCount = signal ? 1 : 0,
        .pSignalSemaphores = &signal };
    f->submitted = 0;
    if (!vk.lost) { /* a lost device takes no more work: the frame is dropped */
        vkResetFences(vk.device, 1, &f->fence);
        VkResult result = vkQueueSubmit(vk.queue, 1, &info, f->fence);
        if (result == VK_SUCCESS)
            f->submitted = 1;
        else if (result == VK_ERROR_DEVICE_LOST)
            vk_device_lost("vkQueueSubmit");
        else
            fprintf(stderr, "[vk] vkQueueSubmit failed (%d): frame dropped\n", (int)result);
    }
    ++vk.serial;
}
int pc_vk_device_lost(void) { return vk.lost; } /* RPC status */
void vk_device_lost(const char* where)
{
    if (vk.lost)
        return;
    vk.lost = 1;
    fprintf(stderr,
        "[vk] device lost (%s): rendering stops, the game goes on (restart it to render again)\n", where);
}
/* The slot's last frame is finished (or will never be): its command pool,
 * arena and deferred objects can be reused. Never fatal: a lost device (or a
 * frame still not done after 30 s, a hung GPU) is noted (vk.lost) and the
 * frames go on unsubmitted. */
static void wait_slot(int slot)
{
    typeof(frames[0])* f = &frames[slot];
    uint64_t cost = pc_cost_begin();
    if (f->submitted && !vk.lost) {
        VkResult result;
        int waited = 0;
        while ((result = vkWaitForFences(vk.device, 1, &f->fence, VK_TRUE, 10ull * 1000 * 1000 * 1000)) == VK_TIMEOUT
            && (waited += 10) < 30)
            fprintf(stderr, "[vk] frame fence: still waiting after %d s\n", waited);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[vk] frame fence wait failed (%d)\n", (int)result);
            vk_device_lost(result == VK_TIMEOUT ? "a frame never finished" : "vkWaitForFences");
        }
    }
    pc_cost_end(PC_COST_FENCE, cost);
    f->submitted = 0;
    vkResetCommandPool(vk.device, f->pool, 0);
}
void vk_flush_wait(void)
{
    submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
    wait_slot(vk.frame);
    /* Arena data of this frame stays (recorded slices may still be in use by
     * the caller); deferred objects are no longer referenced by anything. */
    typeof(frames[0])* f = &frames[vk.frame];
    for (int i = 0; i < f->destroy_count; i++) {
        if (f->destroy[i].kind == 0)
            vk_buffer_destroy(&f->destroy[i].buffer);
        else
            vk_image_destroy(&f->destroy[i].image);
    }
    f->destroy_count = 0;
}
void vk_section(const char* name)
{
    if (perf <= 0 || !section_pool || !frame_started[vk.frame] || section_count[vk.frame] >= VK_SECTIONS)
        return;
    vk_end_rendering();
    int i = section_count[vk.frame]++;
    section_names[vk.frame][i] = name;
    vkCmdWriteTimestamp(vk_cmd(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, section_pool,
        (uint32_t)(vk.frame * (VK_SECTIONS + 1) + i));
}
static void read_sections(int slot)
{
    int count = section_count[slot];
    if (!count || !section_pool)
        return;
    uint64_t t[VK_SECTIONS + 1];
    if (vkGetQueryPoolResults(vk.device, section_pool, (uint32_t)(slot * (VK_SECTIONS + 1)), (uint32_t)count + 1,
            sizeof t, t, sizeof t[0], VK_QUERY_RESULT_64_BIT)
        != VK_SUCCESS)
        return;
    for (int i = 0; i < count; i++) {
        const char* name = section_names[slot][i];
        int k = 0;
        while (k < section_total_count && strcmp(section_totals[k].name, name))
            ++k;
        if (k == section_total_count) {
            if (k == VK_SECTIONS)
                continue;
            section_totals[section_total_count].name = name;
            section_totals[section_total_count++].sum = 0;
        }
        double ms = (double)(t[i + 1] - t[i]) * vk.timestamp_period * 1e-6;
        section_totals[k].sum += ms;
        k = 0;
        while (k < section_rpc_count && strcmp(section_rpc[k].name, name))
            ++k;
        if (k == section_rpc_count) {
            if (k == VK_SECTIONS)
                continue;
            section_rpc[section_rpc_count].name = name;
            section_rpc[section_rpc_count++].sum = 0;
        }
        section_rpc[k].sum += ms;
    }
    ++section_rpc_frames;
}
/* RPC perf: the sections' GPU time per frame since the last reset, as
 * `,"sections":{"name":ms,...}` (lighting_quality.cpp pc_perf_json). */
int pc_vk_perf_sections_json(char* out, int size, int reset)
{
    int n = 0;
    if (section_rpc_frames && size > 16) {
        n += snprintf(out, (size_t)size, ",\"sections\":{");
        for (int k = 0; k < section_rpc_count && n < size - 64; k++)
            n += snprintf(out + n, (size_t)(size - n), "%s\"%s\":%.4f", k ? "," : "", section_rpc[k].name,
                section_rpc[k].sum / section_rpc_frames);
        n += snprintf(out + n, (size_t)(size - n), "}");
    }
    if (reset)
        section_rpc_count = section_rpc_frames = 0;
    return n < size ? n : size - 1;
}
void vk_frame_submit(VkSemaphore wait, VkPipelineStageFlags wait_stage, VkSemaphore signal)
{
    if (perf > 0 && frame_started[vk.frame]) {
        vk_end_rendering();
        if (section_pool && section_count[vk.frame])
            vkCmdWriteTimestamp(vk_cmd(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, section_pool,
                (uint32_t)(vk.frame * (VK_SECTIONS + 1) + section_count[vk.frame]));
        vkCmdWriteTimestamp(vk_cmd(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps, (uint32_t)vk.frame * 2 + 1);
        frame_timed[vk.frame] = 1;
    }
    frame_started[vk.frame] = 0;
    submit(wait, wait_stage, signal);
    vk.frame = (vk.frame + 1) % VK_FRAMES;
    wait_slot(vk.frame);
    if (perf > 0 && frame_timed[vk.frame]) {
        uint64_t t[2];
        frame_timed[vk.frame] = 0;
        if (vkGetQueryPoolResults(vk.device, timestamps, (uint32_t)vk.frame * 2, 2, sizeof t, t, sizeof t[0],
                VK_QUERY_RESULT_64_BIT)
            == VK_SUCCESS) {
            double ms = (double)(t[1] - t[0]) * vk.timestamp_period * 1e-6;
            gpu_sum += ms;
            pc_perf_gpu_add(ms); /* RPC perf (lighting_quality.cpp) */
            gpu_max = ms > gpu_max ? ms : gpu_max;
            read_sections(vk.frame);
            if (++gpu_count == 300) {
                fprintf(stderr, "[perf] Vulkan GPU time per presented frame avg %.2f ms, max %.2f ms\n",
                    gpu_sum / 300, gpu_max);
                if (section_total_count) {
                    char line[1024];
                    int used = snprintf(line, sizeof line, "[perf] GPU sections per frame:");
                    for (int k = 0; k < section_total_count && used < (int)sizeof line - 64; k++)
                        used += snprintf(line + used, sizeof line - (size_t)used, " %s %.3f ms%s", section_totals[k].name,
                            section_totals[k].sum / 300, k + 1 < section_total_count ? "," : "");
                    fprintf(stderr, "%s\n", line);
                    section_total_count = 0;
                }
                gpu_sum = gpu_max = 0;
                gpu_count = 0;
            }
        }
    }
    run_frame_done(vk.frame);
    if (pipeline_cache.dirty && vk.serial - pipeline_cache.dirty_serial >= 120)
        pipeline_cache_save();
    typeof(frames[0])* f = &frames[vk.frame];
    for (int i = 0; i < f->chunk_count; i++)
        f->chunks[i].used = 0;
    f->chunk_current = 0;
}

/* ---- pipelines ---- */

VkShaderModule vk_shader(const uint32_t* code, size_t bytes)
{
    VkShaderModuleCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes,
        .pCode = code };
    VkShaderModule module = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(vk.device, &info, NULL, &module), "vkCreateShaderModule");
    return module;
}
VkDescriptorSetLayout vk_push_layout(const VkDescriptorType* types, int count)
{
    VkDescriptorSetLayoutBinding bindings[32]; /* (vk_descriptors holds 32 writes) */
    if (count > 32) {
        fprintf(stderr, "[vk] a push descriptor layout of %d bindings (32 at most)\n", count);
        return VK_NULL_HANDLE;
    }
    for (int i = 0; i < count; i++)
        bindings[i] = (VkDescriptorSetLayoutBinding){ .binding = (uint32_t)i,
            .descriptorType = types[i],
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL };
    VkDescriptorSetLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
        .bindingCount = (uint32_t)count,
        .pBindings = bindings };
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    vk_check(vkCreateDescriptorSetLayout(vk.device, &info, NULL, &layout), "vkCreateDescriptorSetLayout");
    return layout;
}
static VkPipelineColorBlendAttachmentState blend_state(int blend, unsigned mask)
{
    VkPipelineColorBlendAttachmentState s = { .colorWriteMask = mask };
    if (blend < 0 || !mask)
        return s;
    s.blendEnable = VK_TRUE;
    s.colorBlendOp = s.alphaBlendOp = blend == 2 || blend == 6 ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;
    switch (blend) {
    case 0: /* B/2 + F/2 */
        s.srcColorBlendFactor = s.srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
        s.dstColorBlendFactor = s.dstAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
        break;
    case 3: /* B + F/4 */
        s.srcColorBlendFactor = s.srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
        s.dstColorBlendFactor = s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case 4: /* over */
        s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case 5: /* B + F * alpha */
    case 6: /* B - F * alpha */
        s.srcColorBlendFactor = s.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        s.dstColorBlendFactor = s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    default: /* B + F, B - F */
        s.srcColorBlendFactor = s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        s.dstColorBlendFactor = s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }
    return s;
}
VkPipeline vk_pipeline(const vk_pipeline_desc* d)
{
    VkPipelineShaderStageCreateInfo stages[3];
    int stage_count = 0;
    VkShaderModule modules[3] = { d->vertex, d->geometry, d->fragment };
    VkShaderStageFlagBits kinds[3] = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT };
    for (int i = 0; i < 3; i++)
        if (modules[i])
            stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = kinds[i],
                .module = modules[i],
                .pName = "main" };
    VkPipelineVertexInputStateCreateInfo input = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = d->binding ? 1 : 0,
        .pVertexBindingDescriptions = d->binding,
        .vertexAttributeDescriptionCount = d->binding ? (uint32_t)d->attribute_count : 0,
        .pVertexAttributeDescriptions = d->attributes };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = d->topology };
    VkPipelineViewportStateCreateInfo viewport = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = d->samples > 1 ? (VkSampleCountFlagBits)d->samples : VK_SAMPLE_COUNT_1_BIT };
    /* Every sample at the pixel centre: coverage and depth as single-sampled. */
    VkSampleLocationEXT centres[16];
    for (int i = 0; i < 16; i++)
        centres[i] = (VkSampleLocationEXT){ 0.5f, 0.5f };
    VkPipelineSampleLocationsStateCreateInfoEXT locations = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT,
        .sampleLocationsEnable = VK_TRUE,
        .sampleLocationsInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT,
            .sampleLocationsPerPixel = multisample.rasterizationSamples,
            .sampleLocationGridSize = { 1, 1 },
            .sampleLocationsCount = (uint32_t)(d->samples > 1 ? d->samples : 1),
            .pSampleLocations = centres } };
    if (d->samples > 1 && d->centre && (vk.sample_location_counts & (VkSampleCountFlags)d->samples))
        multisample.pNext = &locations;
    VkPipelineDepthStencilStateCreateInfo depth = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS };
    VkPipelineColorBlendAttachmentState attachments[5];
    for (int i = 0; i < d->color_count; i++)
        attachments[i] = blend_state(d->blend, d->write_masks[i]);
    VkPipelineColorBlendStateCreateInfo blend = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = (uint32_t)d->color_count,
        .pAttachments = attachments,
        .blendConstants = { 0, 0, 0, d->blend == 3 ? 0.25f : 0.5f } };
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP };
    VkPipelineDynamicStateCreateInfo dynamic = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 5,
        .pDynamicStates = dynamic_states };
    VkPipelineRenderingCreateInfo rendering = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = (uint32_t)d->color_count,
        .pColorAttachmentFormats = d->color_formats,
        .depthAttachmentFormat = d->depth_format };
    VkGraphicsPipelineCreateInfo info = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = (uint32_t)stage_count,
        .pStages = stages,
        .pVertexInputState = &input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = d->layout };
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint64_t start = SDL_GetTicksNS();
    vk_check(vkCreateGraphicsPipelines(vk.device, vk.pipeline_cache, 1, &info, NULL, &pipeline),
        "vkCreateGraphicsPipelines");
    vk_pipeline_cache_note(SDL_GetTicksNS() - start);
    return pipeline;
}
VkPipeline vk_compute_pipeline(VkShaderModule module, VkPipelineLayout layout)
{
    VkComputePipelineCreateInfo info = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module,
            .pName = "main" },
        .layout = layout };
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint64_t start = SDL_GetTicksNS();
    vk_check(vkCreateComputePipelines(vk.device, vk.pipeline_cache, 1, &info, NULL, &pipeline),
        "vkCreateComputePipelines");
    vk_pipeline_cache_note(SDL_GetTicksNS() - start);
    return pipeline;
}

/* ---- descriptors ---- */

static VkWriteDescriptorSet* next_write(vk_descriptors* d, int binding, VkDescriptorType type)
{
    if (d->count >= (int)(sizeof d->writes / sizeof d->writes[0])) {
        fprintf(stderr, "[vk] more than %d descriptor writes\n", d->count);
        abort();
    }
    VkWriteDescriptorSet* w = &d->writes[d->count];
    *w = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding = (uint32_t)binding,
        .descriptorCount = 1,
        .descriptorType = type };
    return w;
}
void vk_descriptors_image(vk_descriptors* d, int binding, VkImageView view, VkSampler sampler)
{
    VkWriteDescriptorSet* w = next_write(d, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    d->images[d->count] = (VkDescriptorImageInfo){ sampler, view, VK_IMAGE_LAYOUT_GENERAL };
    w->pImageInfo = &d->images[d->count++];
}
void vk_descriptors_storage_image(vk_descriptors* d, int binding, VkImageView view)
{
    VkWriteDescriptorSet* w = next_write(d, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    d->images[d->count] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL };
    w->pImageInfo = &d->images[d->count++];
}
void vk_descriptors_buffer(vk_descriptors* d, int binding, VkDescriptorType type, VkBuffer buffer, VkDeviceSize offset,
    VkDeviceSize range)
{
    VkWriteDescriptorSet* w = next_write(d, binding, type);
    d->buffers[d->count] = (VkDescriptorBufferInfo){ buffer, offset, range };
    w->pBufferInfo = &d->buffers[d->count++];
}
void vk_descriptors_structure(vk_descriptors* d, int binding, VkAccelerationStructureKHR structure)
{
    VkWriteDescriptorSet* w = next_write(d, binding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
    int s = d->structure_count++;
    d->structure_handles[s] = structure;
    d->structures[s] = (VkWriteDescriptorSetAccelerationStructureKHR){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &d->structure_handles[s] };
    w->pNext = &d->structures[s];
    d->count++;
}
void vk_descriptors_push(vk_descriptors* d, VkPipelineBindPoint point, VkPipelineLayout layout)
{
    if (d->count > 0) /* nothing to write: a no-op (zero writes is invalid usage) */
        vkCmdPushDescriptorSetKHR(vk_cmd(), point, layout, 0, (uint32_t)d->count, d->writes);
    d->count = 0;
    d->structure_count = 0;
}

/* ---- readback ---- */

int vk_read_image(const vk_image* image, VkImageAspectFlags aspect, int x, int y, int w, int h, void* out,
    size_t texel_bytes)
{
    if (!image->image || w <= 0 || h <= 0)
        return 0;
    vk_buffer staging;
    if (!vk_buffer_create(&staging, (VkDeviceSize)w * h * texel_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 2))
        return 0;
    vk_end_rendering();
    vk_barrier();
    VkBufferImageCopy region = { .imageSubresource = { aspect, 0, 0, 1 },
        .imageOffset = { x, y, 0 },
        .imageExtent = { (uint32_t)w, (uint32_t)h, 1 } };
    vkCmdCopyImageToBuffer(vk_cmd(), image->image, VK_IMAGE_LAYOUT_GENERAL, staging.buffer, 1, &region);
    vk_barrier();
    vk_flush_wait();
    memcpy(out, staging.mapped, (size_t)w * h * texel_bytes);
    vk_buffer_destroy(&staging);
    return 1;
}

/* For the lighting quality presets (lighting_quality.cpp): ray queries,
 * integrated GPU, the GPU's name. */
int pc_vk_caps(int* ray_query, int* integrated, char* name, int size)
{
    if (!vk.device)
        return 0;
    *ray_query = vk.ray_query;
    *integrated = vk.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
        || vk.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    snprintf(name, (size_t)size, "%s", vk.properties.deviceName);
    return 1;
}
