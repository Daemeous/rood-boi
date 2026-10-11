/* Vulkan backend core (ROOD_RENDERER=vulkan; pc/docs/Vulkan_Renderer.md).
 *
 * The device, the frame's command buffer, per-frame upload arenas, images,
 * pipelines and readbacks. Vulkan is loaded at run time through SDL (no link
 * against libvulkan), so every entry point is a function pointer here, named
 * as the API names it. Everything runs on the game thread.
 *
 * Recording model: one command buffer is open between presents; the
 * hardware rasterizer (vk_hw.c), the native lists (vk_render.inc) and the
 * presenter (vk_present.c) record into it in the order the GL path issues
 * its calls. A rendering scope (dynamic rendering) stays open across draws
 * to the same attachments and closes when something else (a copy, an
 * upload, other attachments) comes; scopes and transfers are separated by
 * full memory barriers, so the order of the GL path is the order here. */
#ifndef ROOD_VK_CORE_H
#define ROOD_VK_CORE_H
#define VK_NO_PROTOTYPES
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_GLOBAL_FUNCTIONS(X)                                                                                         \
    X(vkCreateInstance)                                                                                                \
    X(vkEnumerateInstanceExtensionProperties)                                                                          \
    X(vkEnumerateInstanceLayerProperties)                                                                              \
    X(vkEnumerateInstanceVersion)
#define VK_INSTANCE_FUNCTIONS(X)                                                                                       \
    X(vkDestroyInstance)                                                                                               \
    X(vkEnumeratePhysicalDevices)                                                                                      \
    X(vkGetPhysicalDeviceProperties)                                                                                   \
    X(vkGetPhysicalDeviceProperties2)                                                                                  \
    X(vkGetPhysicalDeviceFeatures2)                                                                                    \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                        \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                             \
    X(vkGetPhysicalDeviceFormatProperties)                                                                             \
    X(vkEnumerateDeviceExtensionProperties)                                                                            \
    X(vkCreateDevice)                                                                                                  \
    X(vkGetDeviceProcAddr)                                                                                             \
    X(vkDestroySurfaceKHR)                                                                                             \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                                       \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)
#define VK_DEVICE_FUNCTIONS(X)                                                                                         \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkQueueSubmit)                                                                                                   \
    X(vkQueueWaitIdle)                                                                                                 \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkResetCommandPool)                                                                                              \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkCreateFence)                                                                                                   \
    X(vkDestroyFence)                                                                                                  \
    X(vkWaitForFences)                                                                                                 \
    X(vkResetFences)                                                                                                   \
    X(vkCreateSemaphore)                                                                                               \
    X(vkDestroySemaphore)                                                                                              \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkMapMemory)                                                                                                     \
    X(vkUnmapMemory)                                                                                                   \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkBindImageMemory)                                                                                               \
    X(vkCreateImageView)                                                                                               \
    X(vkDestroyImageView)                                                                                              \
    X(vkCreateSampler)                                                                                                 \
    X(vkDestroySampler)                                                                                                \
    X(vkCreateShaderModule)                                                                                            \
    X(vkDestroyShaderModule)                                                                                           \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkCreateGraphicsPipelines)                                                                                       \
    X(vkCreateComputePipelines)                                                                                        \
    X(vkDestroyPipeline)                                                                                               \
    X(vkCreateDescriptorSetLayout)                                                                                     \
    X(vkDestroyDescriptorSetLayout)                                                                                    \
    X(vkCreateDescriptorPool)                                                                                          \
    X(vkDestroyDescriptorPool)                                                                                         \
    X(vkAllocateDescriptorSets)                                                                                        \
    X(vkResetDescriptorPool)                                                                                           \
    X(vkUpdateDescriptorSets)                                                                                          \
    X(vkCreateQueryPool)                                                                                               \
    X(vkDestroyQueryPool)                                                                                              \
    X(vkGetQueryPoolResults)                                                                                           \
    X(vkCmdBeginRendering)                                                                                             \
    X(vkCmdEndRendering)                                                                                               \
    X(vkCmdPipelineBarrier2)                                                                                           \
    X(vkCmdBindPipeline)                                                                                               \
    X(vkCmdBindVertexBuffers)                                                                                          \
    X(vkCmdBindIndexBuffer)                                                                                            \
    X(vkCmdBindDescriptorSets)                                                                                         \
    X(vkCmdDraw)                                                                                                       \
    X(vkCmdDrawIndexed)                                                                                                \
    X(vkCmdDispatch)                                                                                                   \
    X(vkCmdSetViewport)                                                                                                \
    X(vkCmdSetScissor)                                                                                                 \
    X(vkCmdSetDepthTestEnable)                                                                                         \
    X(vkCmdSetDepthWriteEnable)                                                                                        \
    X(vkCmdSetDepthCompareOp)                                                                                          \
    X(vkCmdPushConstants)                                                                                              \
    X(vkCmdClearAttachments)                                                                                           \
    X(vkCmdClearColorImage)                                                                                            \
    X(vkCmdClearDepthStencilImage)                                                                                     \
    X(vkCmdCopyImage)                                                                                                  \
    X(vkCmdBlitImage)                                                                                                  \
    X(vkCmdCopyBuffer)                                                                                                 \
    X(vkCmdCopyBufferToImage)                                                                                          \
    X(vkCmdCopyImageToBuffer)                                                                                          \
    X(vkCmdFillBuffer)                                                                                                 \
    X(vkCmdUpdateBuffer)                                                                                               \
    X(vkCmdResetQueryPool)                                                                                             \
    X(vkCmdWriteTimestamp)
/* Loaded when the device has them (NULL otherwise). */
#define VK_OPTIONAL_DEVICE_FUNCTIONS(X)                                                                                \
    X(vkCmdPushDescriptorSetKHR)                                                                                       \
    X(vkCreateSwapchainKHR)                                                                                            \
    X(vkDestroySwapchainKHR)                                                                                           \
    X(vkGetSwapchainImagesKHR)                                                                                         \
    X(vkAcquireNextImageKHR)                                                                                           \
    X(vkQueuePresentKHR)                                                                                               \
    X(vkGetBufferDeviceAddress)                                                                                        \
    X(vkCreateAccelerationStructureKHR)                                                                                \
    X(vkDestroyAccelerationStructureKHR)                                                                               \
    X(vkGetAccelerationStructureBuildSizesKHR)                                                                         \
    X(vkCmdBuildAccelerationStructuresKHR)                                                                             \
    X(vkGetAccelerationStructureDeviceAddressKHR)

#define VK_DECLARE(name) extern PFN_##name name;
VK_GLOBAL_FUNCTIONS(VK_DECLARE)
VK_INSTANCE_FUNCTIONS(VK_DECLARE)
VK_DEVICE_FUNCTIONS(VK_DECLARE)
VK_OPTIONAL_DEVICE_FUNCTIONS(VK_DECLARE)
#undef VK_DECLARE
extern PFN_vkGetInstanceProcAddr vk_get_instance_proc_addr;

/* ---- the device ---- */

#define VK_FRAMES 2 /* frames in flight */
typedef struct {
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    uint32_t api_version;
    int ray_query; /* VK_KHR_ray_query + acceleration structures enabled */
    VkFormat depth_format; /* X8_D24 when it can be sampled and attached, else D32_SFLOAT */
    VkSampler nearest, linear; /* clamp to edge */
    int frame; /* slot of the frame being recorded (0..VK_FRAMES-1) */
    uint64_t serial; /* frames submitted so far */
    float timestamp_period;
    VkDeviceSize scratch_alignment; /* acceleration structure build scratch */
} vk_context;
extern vk_context vk;
extern int pc_vk_active;

/* Instance and device (no surface yet); 0 on failure (the caller falls back
 * to OpenGL). */
int vk_core_init(const char* const* instance_extensions, uint32_t extension_count);
void vk_core_shutdown(void);
/* Report a failed call and return the condition. */
int vk_check(VkResult result, const char* what);

/* ---- memory ---- */

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    void* mapped; /* host-visible buffers stay mapped */
    VkDeviceAddress address; /* with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT */
} vk_buffer;
/* host: host-visible (and coherent), device-local when such memory exists. */
/* host: 0 device-local, 1 CPU-written (upload), 2 CPU-read (readback). */
int vk_buffer_create(vk_buffer* b, VkDeviceSize size, VkBufferUsageFlags usage, int host);
void vk_buffer_destroy(vk_buffer* b);
/* Destroy after the frames that may still use it have finished. */
void vk_buffer_destroy_later(vk_buffer* b);

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
    VkImageAspectFlags aspect;
    int width, height;
} vk_image;
int vk_image_create(vk_image* image, int width, int height, VkFormat format, VkImageUsageFlags usage);
void vk_image_destroy(vk_image* image);
void vk_image_destroy_later(vk_image* image);

/* ---- per-frame upload arena ----
 * Host-visible memory for data the frame's commands read: vertices,
 * uniforms, palettes, staging for texture uploads. Reused when the frame
 * slot comes round again (its fence has signalled). */
typedef struct {
    VkBuffer buffer;
    VkDeviceSize offset;
    void* data;
    VkDeviceAddress address; /* with ray tracing (buffer device addresses) */
} vk_slice;
vk_slice vk_arena(VkDeviceSize size, VkDeviceSize alignment);

/* ---- command recording ---- */

VkCommandBuffer vk_cmd(void);
/* A full memory barrier (all commands, all writes before -> all access after). */
void vk_barrier(void);
/* Rendering scopes: color attachments (views, count) and an optional depth
 * attachment, over a render area. A scope with the same attachments stays
 * open; otherwise the open one ends, a barrier follows and a new one begins. */
typedef struct {
    VkImageView colors[4];
    int color_count;
    VkImageView depth;
    int width, height;
} vk_attachments;
void vk_begin_rendering(const vk_attachments* attachments);
void vk_end_rendering(void); /* ends an open scope (and barriers) */
int vk_rendering_open(void);
/* ROOD_PERF: a named GPU section starts here and lasts until the next
 * section or the end of the frame (`name` must stay valid: a literal). */
void vk_section(const char* name);
/* Submit what has been recorded and wait for it (readbacks). */
void vk_flush_wait(void);
/* End the frame: submit (waiting on `wait`, signalling `signal` when given)
 * and move to the next frame slot, waiting for its previous use. */
void vk_frame_submit(VkSemaphore wait, VkPipelineStageFlags wait_stage, VkSemaphore signal);
/* Called when a frame slot is reused (its commands finished): readbacks
 * queued with vk_on_frame_done run then. */
typedef void (*vk_frame_callback)(void* context);
void vk_on_frame_done(vk_frame_callback callback, void* context);

/* ---- pipelines ---- */

VkShaderModule vk_shader(const uint32_t* code, size_t bytes);
/* A descriptor set layout of push descriptors: one type per binding (stage
 * flags ALL). */
VkDescriptorSetLayout vk_push_layout(const VkDescriptorType* types, int count);
typedef struct {
    VkShaderModule vertex, geometry, fragment;
    VkPipelineLayout layout;
    VkPrimitiveTopology topology;
    const VkVertexInputBindingDescription* binding; /* NULL: no vertex input */
    const VkVertexInputAttributeDescription* attributes;
    int attribute_count;
    int color_count;
    const VkFormat* color_formats;
    VkFormat depth_format; /* VK_FORMAT_UNDEFINED: none */
    /* Per colour attachment write masks (RGBA bits 1,2,4,8), and the blend
     * state shared by all attachments that write (-1: none, else the PS1
     * mode 0-3; 4: alpha "over" for the overlay-style passes). */
    unsigned write_masks[4];
    int blend;
    /* Depth test/write/compare are dynamic state. */
} vk_pipeline_desc;
VkPipeline vk_pipeline(const vk_pipeline_desc* desc);
VkPipeline vk_compute_pipeline(VkShaderModule module, VkPipelineLayout layout);

/* Push descriptor writes helpers. */
typedef struct {
    VkWriteDescriptorSet writes[24];
    VkDescriptorImageInfo images[24];
    VkDescriptorBufferInfo buffers[24];
    VkWriteDescriptorSetAccelerationStructureKHR structures[2];
    VkAccelerationStructureKHR structure_handles[2];
    int count, structure_count;
} vk_descriptors;
void vk_descriptors_image(vk_descriptors* d, int binding, VkImageView view, VkSampler sampler);
void vk_descriptors_storage_image(vk_descriptors* d, int binding, VkImageView view);
void vk_descriptors_buffer(vk_descriptors* d, int binding, VkDescriptorType type, VkBuffer buffer, VkDeviceSize offset,
    VkDeviceSize range);
void vk_descriptors_structure(vk_descriptors* d, int binding, VkAccelerationStructureKHR structure);
void vk_descriptors_push(vk_descriptors* d, VkPipelineBindPoint point, VkPipelineLayout layout);

/* ---- readback ---- */

/* Copy a rectangle of an image's aspect to host memory, synchronously. */
int vk_read_image(const vk_image* image, VkImageAspectFlags aspect, int x, int y, int w, int h, void* out,
    size_t texel_bytes);

#ifdef __cplusplus
}
#endif
#endif
