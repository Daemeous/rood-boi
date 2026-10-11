// Editor: Graphics > "2D / UI" (ui_upscale.h): how 2D screen furniture (HUD,
// menus, text, talk bubbles, icons) is sampled. Kept out of dev_overlay.cpp,
// which only calls pc_ui_upscale_panel().
#include "imgui.h"
#include "ui_upscale.h"

extern "C" int pc_hw_scale, pc_hw_active;

extern "C" void pc_ui_upscale_panel(void)
{
    if (!ImGui::CollapsingHeader("2D / UI"))
        return;
    if (!pc_hw_active) {
        ImGui::TextDisabled("Needs the hardware renderer");
        return;
    }
    static const char* modes[] = { "Nearest (original)", "MMPX (style-preserving)" };
    ImGui::Combo("2D textures", &pc_ui_upscale, modes, 2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How 2D textures (HUD, menus, text, talk bubbles, icons) are magnified to the internal\n"
                          "resolution. Nearest: the PS1's blocks. MMPX (McGuire & Gagiu 2021): curves, diagonals\n"
                          "and corners are reconstructed from the game's own pixels; the palette, transparency\n"
                          "and single-pixel details stay exact. 3D textures always keep nearest sampling.");
    if (pc_ui_upscale == PC_UI_MMPX) {
        int factor = pc_ui_upscale_factor(pc_hw_scale);
        pc_ui_upscale_stats s;
        pc_ui_upscale_get_stats(&s);
        if (!factor)
            ImGui::TextDisabled("Internal resolution 1x: nothing to magnify");
        else
            ImGui::TextDisabled("MMPX %dx (%s), %ld regions cached, %ld new last frame, %.2f ms", factor,
                s.gpu == 1 ? "Vulkan compute" : s.gpu == 2 ? "OpenGL compute" : "CPU", s.entries, s.frame_misses,
                s.frame_ms + s.miss_ms);
    }
    bool subpixel = pc_ui_subpixel != 0;
    if (ImGui::Checkbox("Sub-pixel text placement", &subpixel))
        pc_ui_subpixel = subpixel;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Text glyphs drawn where the game places them in 16.16 fixed point instead of whole\n"
                          "PS1 pixels: even spacing while talk bubbles zoom open and shut. Off: as the PS1.");
}
