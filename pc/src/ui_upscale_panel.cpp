// Editor: Graphics > "2D / UI" (ui_upscale.h): how 2D screen furniture (HUD,
// menus, text, talk bubbles, icons) is sampled. Kept out of dev_overlay.cpp,
// which only calls pc_ui_upscale_panel().
#include <string.h>
#include "imgui.h"
#include "ui_upscale.h"
#include "ui_text.h"
#include "ui_bubbles.h"
#include "button_prompts.h"
#include "ui/ui_shell.h"

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
    pc_ui_menus_editor_row(); /* modern menus (pc/src/ui) */
    static const char* text_modes[] = { "Original glyphs", "Modern fonts" };
    ImGui::Combo("Text", &pc_ui_text, text_modes, 2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Original: the PS1's glyphs. Modern fonts: the game's own text (same characters, places,\n"
                          "colours and timing) drawn at the internal resolution from open-licensed fonts matched to\n"
                          "each style (pc/tools/font_match.py): talk bubbles, menus, the information box, HUD\n"
                          "numbers and labels. Needs internal resolution 2x or more.");
    if (pc_ui_text == PC_UI_TEXT_FONT) {
        pc_ui_text_stats t;
        pc_ui_text_get_stats(&t);
        if (pc_hw_scale < 2)
            ImGui::TextDisabled("Internal resolution 1x: original glyphs");
        else
            ImGui::TextDisabled("%d fonts, %d glyphs, %ld regions drawn", t.fonts, t.glyphs, t.regions);
        for (int i = 0; i < PC_UI_TEXT_STYLES; i++) {
            const char* file = strrchr(pc_ui_text_styles[i].font, '/');
            ImGui::TextDisabled("  %-8s %s", pc_ui_text_styles[i].name, file ? file + 1 : pc_ui_text_styles[i].font);
        }
    }
    static const char* line_modes[] = { "Legacy (gaps when upscaled)", "Joined (default)" };
    ImGui::Combo("Lines", &pc_ui_lines, line_modes, 2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("2D lines (menu rules, the map, targeting, talk bubble outlines) at the internal\n"
                          "resolution. Joined: one PS1 pixel thick at any scale, end pixels whole so lines meet\n"
                          "without gaps, covering the edges of shapes drawn along them (no light dots along\n"
                          "talk bubble outlines). Legacy: the earlier one-pixel quads, for comparison.");
    ImGui::SliderInt("Outline boost", &pc_ui_outline_boost, 0, 3, pc_ui_outline_boost ? "+%d px" : "off");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("2D lines and vector bubble outlines this many screen pixels thicker: a bolder,\n"
                          "inked look. Off: as the game.");
    static const char* bubble_modes[] = { "Original", "Vector" };
    ImGui::Combo("Talk bubbles", &pc_ui_bubbles, bubble_modes, 2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Original: the game's polygons and one-pixel lines. Vector: each bubble the game builds\n"
                          "this frame (its corners, tail, size and zoom) drawn as one smooth shape: the same\n"
                          "paper fill, drop shadow and an anti-aliased outline. Text is unchanged.");
    if (pc_ui_bubbles == PC_UI_BUBBLES_VECTOR)
        ImGui::SliderFloat("Bubble outline", &pc_ui_bubble_outline, 0.5f, 4.0f, "%.2f PS1 px");
    static const char* prompt_modes[] = { "Auto (last device used)", "PlayStation", "Xbox", "Nintendo",
        "Keyboard", "Original glyphs" };
    ImGui::Combo("Button prompts", &pc_button_prompts, prompt_modes, PC_PROMPTS_MODES);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The PS1 buttons the game shows (in text, menus, the quick manual) drawn as the key or\n"
                          "gamepad button bound to them (settings bind_key_* / bind_pad_*), from Kenney's Input\n"
                          "Prompts (CC0). Auto follows the device used last: key caps after a key, the gamepad's\n"
                          "own labels after a gamepad button. Original: the PS1's glyphs.");
    if (pc_button_prompts != PC_PROMPTS_ORIGINAL) {
        int family = pc_button_prompts_family();
        ImGui::TextDisabled("Showing %s", family ? pc_button_prompts_names[family] : "original");
    }
    bool subpixel = pc_ui_subpixel != 0;
    if (ImGui::Checkbox("Sub-pixel text placement", &subpixel))
        pc_ui_subpixel = subpixel;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Text glyphs drawn where the game places them in 16.16 fixed point instead of whole\n"
                          "PS1 pixels: even spacing while talk bubbles zoom open and shut. Off: as the PS1.");
}
