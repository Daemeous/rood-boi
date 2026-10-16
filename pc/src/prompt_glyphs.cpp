// Button prompts for native (Dear ImGui) menus (button_prompts.h, "Prompts
// for native menus"): the icons button_prompts.c draws for the game, as RGBA
// images packed into one ImTextureData registered as a Dear ImGui user
// texture, which the GL and Vulkan backends create and update themselves.
#include <string.h>
#include "imgui.h"
#include "imgui_internal.h" // RegisterUserTexture, ImTextureDataQueueUpload
#include "button_prompts.h"

namespace {
const int ATLAS = 512;
ImTextureData* texture;
ImGuiContext* owner;     // the context the texture is registered with
unsigned atlas_generation;
int shelf_x, shelf_y, shelf_h; // shelf packer
struct Entry {
    int button, size;
    int x, y, w, h; // w 0: no icon
};
ImVector<Entry> entries;

void clear_atlas()
{
    memset(texture->Pixels, 0, (size_t)texture->GetSizeInBytes());
    if (texture->Status != ImTextureStatus_WantCreate)
        ImTextureDataQueueUpload(texture, 0, 0, ATLAS, ATLAS);
    entries.clear();
    shelf_x = shelf_y = shelf_h = 0;
}
} // namespace

extern "C" int pc_prompt_device(void) { return pc_button_prompts_family(); }
extern "C" unsigned pc_prompt_generation(void) { return pc_button_prompts_generation(); }

/* button_roles.c (battle builds): the button whose binding presses PS1 button
 * `button` now (Circle and Cross exchanged where qol_confirm_cross or
 * qol_field_cross_attack cover the game's context). */
extern "C" int pc_button_roles_shown(int button) __attribute__((weak));

extern "C" pc_prompt_image pc_prompt_glyph(int button, float size_px)
{
    pc_prompt_image image;
    memset(&image, 0, sizeof image);
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context || button < 0 || button > PC_PROMPT_LEFT)
        return image;
    if (pc_button_roles_shown)
        button = pc_button_roles_shown(button);
    int size = (int)(size_px + 0.5f);
    size = size < 4 ? 4 : size > 128 ? 128 : size;
    if (!texture || owner != context) {
        texture = IM_NEW(ImTextureData)();
        texture->Create(ImTextureFormat_RGBA32, ATLAS, ATLAS);
        ImGui::RegisterUserTexture(texture);
        owner = context;
        atlas_generation = 0;
    }
    unsigned generation = pc_button_prompts_generation();
    if (generation != atlas_generation) {
        clear_atlas();
        atlas_generation = generation;
    }
    const Entry* found = NULL;
    for (const Entry& e : entries)
        if (e.button == button && e.size == size)
            found = &e;
    if (!found) {
        static uint32_t pixels[128 * 4 * 128];
        int w = 0, h = 0;
        Entry e = { button, size, 0, 0, 0, 0 };
        if (pc_button_prompts_icon_rgba(button, size, pixels, 128 * 4, &w, &h)) {
            if (shelf_x + w + 1 > ATLAS)
                shelf_x = 0, shelf_y += shelf_h + 1, shelf_h = 0;
            if (shelf_y + h > ATLAS) {
                clear_atlas(); // full: start again (images handed out this frame may change)
                atlas_generation = generation;
            }
            e.x = shelf_x, e.y = shelf_y, e.w = w, e.h = h;
            shelf_x += w + 1;
            shelf_h = h > shelf_h ? h : shelf_h;
            for (int y = 0; y < h; y++)
                memcpy(texture->GetPixelsAt(e.x, e.y + y), pixels + y * 128 * 4, (size_t)w * 4);
            if (texture->Status != ImTextureStatus_WantCreate)
                ImTextureDataQueueUpload(texture, e.x, e.y, w, h);
        }
        entries.push_back(e);
        found = &entries.back();
    }
    if (!found->w)
        return image;
    image.texture = texture;
    image.texture_id = (unsigned long long)texture->GetTexID();
    image.uv0[0] = (float)found->x / ATLAS, image.uv0[1] = (float)found->y / ATLAS;
    image.uv1[0] = (float)(found->x + found->w) / ATLAS, image.uv1[1] = (float)(found->y + found->h) / ATLAS;
    image.w = (float)found->w, image.h = (float)found->h;
    return image;
}
