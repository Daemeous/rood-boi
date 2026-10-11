// Developer overlay on Vulkan: Dear ImGui's upstream Vulkan backend
// (imgui_impl_vulkan, MIT) drawing into the presenter's window image with
// dynamic rendering. dev_overlay.cpp builds the UI as on OpenGL.
#ifndef IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES /* as build_boot.py compiles imgui_impl_vulkan.cpp */
#endif
#include "vk_core.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include <stdio.h>
#include <string.h>

static bool ready;

static PFN_vkVoidFunction load(const char* name, void*)
{
    /* Instance-level entry points (physical device, surface) come from the
     * instance: asking the device for them is invalid usage. */
    bool instance_level = !strncmp(name, "vkGetPhysicalDevice", 19) || !strncmp(name, "vkEnumeratePhysicalDevices", 26)
                       || !strcmp(name, "vkDestroySurfaceKHR");
    PFN_vkVoidFunction f = !instance_level && vkGetDeviceProcAddr ? vkGetDeviceProcAddr(vk.device, name) : nullptr;
    return f ? f : vk_get_instance_proc_addr(vk.instance, name);
}

extern "C" int pc_vk_overlay_init(void)
{
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, load)) {
        fprintf(stderr, "[overlay] Vulkan backend functions unavailable\n");
        return 0;
    }
    static const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = vk.instance;
    info.PhysicalDevice = vk.gpu;
    info.Device = vk.device;
    info.QueueFamily = vk.queue_family;
    info.Queue = vk.queue;
    info.DescriptorPoolSize = 64;
    info.MinImageCount = 2;
    info.ImageCount = VK_FRAMES + 1;
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {};
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
    if (!ImGui_ImplVulkan_Init(&info)) {
        fprintf(stderr, "[overlay] ImGui Vulkan backend failed\n");
        return 0;
    }
    ready = true;
    return 1;
}

extern "C" void pc_vk_overlay_new_frame(void)
{
    if (ready)
        ImGui_ImplVulkan_NewFrame();
}

/* The UI built this frame (ImGui::Render already ran), inside a rendering
 * scope on the window image. */
extern "C" void pc_vk_overlay_render(VkCommandBuffer cmd)
{
    if (!ready || !ImGui::GetCurrentContext())
        return;
    ImDrawData* data = ImGui::GetDrawData();
    if (!data || !data->Valid)
        return;
    VkViewport viewport = { 0, 0, data->DisplaySize.x, data->DisplaySize.y, 0, 1 };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    ImGui_ImplVulkan_RenderDrawData(data, cmd);
}
