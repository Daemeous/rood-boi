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
#include <stdint.h>
#include <unordered_map>
#include <vector>

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
    info.PipelineCache = vk.pipeline_cache; /* persisted between runs (vk_core.c) */
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

/* The UI's textures (T1 R4e-4). ImGui's own list (the font atlas, the glyph and button textures the UI registers) belongs to the game
 * thread: the context writes its pixels and statuses while it builds the UI. The backend's textures are the renderer's: at the snapshot
 * the game thread turns every pending create, update and destroy into an operation that carries its pixels, and answers the context as the
 * backend would (the status becomes OK, the texture id the stable handle below), and the draw commands name their texture by that handle
 * instead of pointing at the context's object. The renderer applies the operations in order before it draws, on texture objects of its
 * own (the shadows), which the backend creates, uploads and destroys by its own protocol; each draw command's handle becomes the
 * shadow's descriptor set as it is drawn. */
struct TextureOp {
    enum Kind { Create, Update, Destroy } kind;
    int uid, width, height, bytes_per_pixel;
    ImTextureFormat format;
    ImTextureRect rect; /* an update's rows (a create carries the whole texture) */
    std::vector<unsigned char> pixels;
};
static std::vector<TextureOp> texture_ops; /* made at snapshots, applied when an overlay is drawn */
static std::unordered_map<int, ImTextureData*> shadows;
static ImVector<ImTextureData*> shadow_list;
/* The texture a draw command named, by handle, in each snapshot list (parallel to its CmdBuffer). */
static std::vector<std::vector<int>> command_textures;

/* The handle each context texture goes by. ImTextureData::UniqueID is the font atlas's own count ("unique per atlas"): every texture
 * the application registers (the library's thumbnails, the prompt glyphs, the UI text) has 0 there, so keying by it made them
 * one texture, the last created. A handle is made at the create and kept to the destroy. */
static std::unordered_map<const ImTextureData*, int> texture_handles;
static int texture_handle_next = 1;
static int texture_handle(const ImTextureData* tex)
{
    auto found = texture_handles.find(tex);
    return found != texture_handles.end() ? found->second : 0;
}

static void texture_ops_collect(ImDrawData* data)
{
    if (!data->Textures)
        return;
    for (ImTextureData* tex : *data->Textures) {
        if (tex->Status == ImTextureStatus_OK || tex->Status == ImTextureStatus_Destroyed)
            continue;
        TextureOp op;
        op.uid = tex->Status == ImTextureStatus_WantCreate ? (texture_handles[tex] = texture_handle_next++) : texture_handle(tex);
        if (!op.uid)
            continue; /* (an update or destroy of a texture never created here) */
        op.width = tex->Width, op.height = tex->Height, op.format = tex->Format;
        op.bytes_per_pixel = tex->BytesPerPixel, op.rect = tex->UpdateRect;
        if (tex->Status == ImTextureStatus_WantCreate) {
            op.kind = TextureOp::Create;
            op.pixels.assign((const unsigned char*)tex->Pixels, (const unsigned char*)tex->Pixels + tex->GetSizeInBytes());
            tex->SetTexID((ImTextureID)(uintptr_t)op.uid);
            tex->SetStatus(ImTextureStatus_OK);
        } else if (tex->Status == ImTextureStatus_WantUpdates) {
            op.kind = TextureOp::Update;
            for (int y = 0; y < op.rect.h; y++) {
                const unsigned char* row = (const unsigned char*)tex->GetPixelsAt(op.rect.x, op.rect.y + y);
                op.pixels.insert(op.pixels.end(), row, row + (size_t)op.rect.w * op.bytes_per_pixel);
            }
            tex->SetStatus(ImTextureStatus_OK);
        } else { /* WantDestroy */
            op.kind = TextureOp::Destroy;
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
            texture_handles.erase(tex);
        }
        texture_ops.push_back(std::move(op));
    }
}

/* The renderer's, before the draw: the operations queued since the last one, on the shadows. */
static void texture_ops_apply(void)
{
    for (TextureOp& op : texture_ops) {
        if (op.kind == TextureOp::Create) {
            ImTextureData* shadow = IM_NEW(ImTextureData)();
            shadow->Create(op.format, op.width, op.height);
            memcpy(shadow->Pixels, op.pixels.data(), op.pixels.size());
            shadow->SetStatus(ImTextureStatus_WantCreate);
            shadows[op.uid] = shadow;
            shadow_list.push_back(shadow);
            continue;
        }
        auto found = shadows.find(op.uid);
        if (found == shadows.end())
            continue;
        ImTextureData* shadow = found->second;
        if (op.kind == TextureOp::Update) {
            for (int y = 0; y < op.rect.h; y++)
                memcpy(shadow->GetPixelsAt(op.rect.x, op.rect.y + y), op.pixels.data() + (size_t)y * op.rect.w * op.bytes_per_pixel,
                    (size_t)op.rect.w * op.bytes_per_pixel);
            shadow->UpdateRect = op.rect;
            if (shadow->Status != ImTextureStatus_WantCreate) /* (a create not yet uploaded takes the pixels whole) */
                shadow->SetStatus(ImTextureStatus_WantUpdates);
        } else {
            shadow->SetStatus(ImTextureStatus_WantDestroy); /* (the backend destroys it once no frame in flight can use it) */
            shadow->UnusedFrames = 0;
        }
    }
    texture_ops.clear();
    for (ImTextureData* shadow : shadow_list) {
        if (shadow->Status == ImTextureStatus_WantDestroy)
            ++shadow->UnusedFrames;
        ImGui_ImplVulkan_UpdateTexture(shadow); /* (what RenderDrawData does for its list, now: the descriptor sets are needed below) */
    }
}
static void texture_shadows_reap(void)
{
    for (int i = shadow_list.Size - 1; i >= 0; i--) {
        ImTextureData* shadow = shadow_list[i];
        if (shadow->Status != ImTextureStatus_Destroyed)
            continue;
        for (auto it = shadows.begin(); it != shadows.end(); ++it)
            if (it->second == shadow) {
                shadows.erase(it);
                break;
            }
        shadow_list.erase(shadow_list.Data + i);
        IM_DELETE(shadow);
    }
}
static void texture_shadows_destroy(void)
{
    for (ImTextureData* shadow : shadow_list) {
        shadow->SetStatus(ImTextureStatus_WantDestroy);
        shadow->UnusedFrames = 1 << 20;
        ImGui_ImplVulkan_UpdateTexture(shadow);
        IM_DELETE(shadow);
    }
    shadow_list.clear();
    shadows.clear();
    texture_ops.clear();
}

/* Before an engine reload (dev_overlay.cpp): the backend's own objects, while the device lives. */
extern "C" void pc_vk_overlay_shutdown(void)
{
    if (!ready)
        return;
    vkDeviceWaitIdle(vk.device);
    texture_shadows_destroy();
    ImGui_ImplVulkan_Shutdown();
    ready = false;
}

extern "C" void pc_vk_overlay_new_frame(void)
{
    if (ready)
        ImGui_ImplVulkan_NewFrame();
}

/* The UI built for this present (T1 R4c): the game thread builds it (pc_overlay_build, from the present's header fill: the panels read
 * and edit live game state) and hands the renderer a copy of the draw data (ImDrawList::CloneOutput; the texture update list stays
 * the context's), so the presenter reads nothing of the UI's own state. */
static ImDrawData snapshot;
static bool snapshot_ready;
extern "C" void pc_vk_overlay_snapshot(void)
{
    for (ImDrawList* list : snapshot.CmdLists)
        IM_DELETE(list);
    snapshot.Clear();
    snapshot_ready = false;
    if (!ready || !ImGui::GetCurrentContext())
        return;
    ImDrawData* data = ImGui::GetDrawData();
    if (!data || !data->Valid)
        return;
    snapshot.Valid = true;
    snapshot.FrameCount = data->FrameCount;
    snapshot.TotalIdxCount = data->TotalIdxCount, snapshot.TotalVtxCount = data->TotalVtxCount;
    snapshot.DisplayPos = data->DisplayPos, snapshot.DisplaySize = data->DisplaySize, snapshot.FramebufferScale = data->FramebufferScale;
    snapshot.OwnerViewport = data->OwnerViewport;
    texture_ops_collect(data);
    snapshot.Textures = &shadow_list; /* (the renderer's textures, not the context's) */
    command_textures.clear();
    for (const ImDrawList* list : data->CmdLists) {
        ImDrawList* clone = list->CloneOutput(); /* (not AddDrawList: it checks the writer's pointers, a clone has none) */
        command_textures.emplace_back();
        for (ImDrawCmd& cmd : clone->CmdBuffer) {
            int uid = cmd.TexRef._TexData ? texture_handle(cmd.TexRef._TexData) : 0;
            command_textures.back().push_back(uid);
            cmd.TexRef._TexData = nullptr; /* (the draw reads the renderer's texture, never the context's object) */
            cmd.TexRef._TexID = ImTextureID_Invalid;
        }
        snapshot.CmdLists.push_back(clone);
    }
    snapshot_ready = true;
}

/* The UI built for this present (pc_vk_overlay_snapshot), inside a rendering scope on the window image. */
extern "C" void pc_vk_overlay_render(VkCommandBuffer cmd)
{
    if (!ready || !snapshot_ready)
        return;
    ImDrawData* data = &snapshot;
    texture_ops_apply();
    for (int l = 0; l < data->CmdLists.Size; l++)
        for (int c = 0; c < data->CmdLists[l]->CmdBuffer.Size; c++) {
            int uid = command_textures[l][c];
            auto found = uid ? shadows.find(uid) : shadows.end();
            data->CmdLists[l]->CmdBuffer[c].TexRef._TexID = found != shadows.end() ? found->second->TexID : ImTextureID_Invalid;
        }
    VkViewport viewport = { 0, 0, data->DisplaySize.x, data->DisplaySize.y, 0, 1 };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    ImGui_ImplVulkan_RenderDrawData(data, cmd);
    texture_shadows_reap();
}
