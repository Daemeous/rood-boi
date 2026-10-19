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
                          "colours and timing) drawn anti-aliased from open-licensed fonts matched to each style\n"
                          "(pc/tools/font_match.py): talk bubbles, menus, the information box, HUD numbers and\n"
                          "labels.");
    if (pc_ui_text == PC_UI_TEXT_FONT) {
        static const char* layout_modes[] = { "Per glyph (original cells)", "Lines (font spacing)" };
        ImGui::Combo("Text layout", &pc_ui_text_layout, layout_modes, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Lines: each line of dialogue and menu text set with the font's own spacing and\n"
                              "kerning from where the game starts it, descenders below the line, never running\n"
                              "past the game's own letters (tightened where the font is wider). Per glyph:\n"
                              "each letter drawn inside the original's 12-pixel cell at the game's advance.");
        if (pc_ui_text_layout == PC_UI_TEXT_LAYOUT_LINE) {
            static const char* align_modes[] = { "Left (where the game starts them)", "Centred" };
            ImGui::Combo("Bubble lines", &pc_ui_text_bubble_align, align_modes, 2);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Talk bubbles' lines (menus and the information box stay left). Left: each line\n"
                                  "starts where the game starts it (set with the font's spacing it usually ends a\n"
                                  "little short of the original). Centred: each line centred within the original\n"
                                  "line's extent, letters still appearing where they stay as the bubble types out.");
        }
        static const char* overlay_modes[] = { "Off (internal resolution)", "Auto (window resolution)" };
        ImGui::Combo("Text sharpness", &pc_ui_text_overlay, overlay_modes, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Auto: while the window is finer than the internal resolution, text and button icons\n"
                              "are drawn at the window's own resolution over the picture (anti-aliased edges one\n"
                              "window pixel wide), unless something the game draws later covers them. Off: they\n"
                              "are drawn into the picture at the internal resolution and scaled with it.");
        pc_ui_text_stats t;
        pc_ui_text_get_stats(&t);
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
        static const char* styles[] = { "Solid", "Outline", "Colour solid", "Colour outline" };
        ImGui::Combo("Prompt style", &pc_button_prompts_style, styles, PC_PROMPTS_STYLES);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Solid: a filled cap with the symbol knocked out. Outline: ring and symbol, no fill.\n"
                              "Colour: the PlayStation and Xbox face buttons in their own colours (others neutral).");
        ImGui::SliderFloat("Prompt size", &pc_button_prompts_scale, 0.4f, 1.0f, "%.2f of the cell");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The 16 x 16 button sprites' icons fit in a circle this fraction of the cell across,\n"
                              "centred where the original's is: 0.7 keeps the face-button diamond's neighbours apart.");
        static const char* dpads[] = { "Whole D-pad", "Dim arms without an action", "Fill arms with an action" };
        ImGui::Combo("Shortcut D-pad", &pc_button_prompts_dpad, dpads, PC_PROMPTS_DPADS);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The L2 shortcut cross's D-pad. Whole D-pad: the family's D-pad in the prompt style.\n"
                              "Dim: the arms with no usable action beside them dimmed. Fill: the arms with one\n"
                              "filled (outline styles; solid styles dim the others' insides).");
        if (ImGui::TreeNode("Prompt colours")) {
            static const char* const labels[PC_PROMPTS_COLORS] = { "Neutral", "PlayStation triangle",
                "PlayStation circle", "PlayStation cross", "PlayStation square", "Xbox A", "Xbox B", "Xbox X", "Xbox Y",
                "Dimmed toward" };
            for (int k = 0; k < PC_PROMPTS_COLORS; k++) {
                unsigned v = pc_button_prompts_colors[k];
                float c[3] = { (v >> 16 & 255) / 255.0f, (v >> 8 & 255) / 255.0f, (v & 255) / 255.0f };
                if (ImGui::ColorEdit3(labels[k], c, ImGuiColorEditFlags_NoInputs)) {
                    unsigned r = (unsigned)(c[0] * 255 + 0.5f), g = (unsigned)(c[1] * 255 + 0.5f),
                             b = (unsigned)(c[2] * 255 + 0.5f);
                    pc_button_prompts_colors[k] = r << 16 | g << 8 | b;
                    pc_button_prompts_colors_changed();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Settings button_prompts_color_%s (#rrggbb).%s", pc_button_prompts_color_names[k],
                        k == PC_PROMPTS_COLOR_DIM ? "\nWhat a dimmed D-pad arm's colour mixes toward." : "");
                if (k % 2 == 0 && k + 1 < PC_PROMPTS_COLORS)
                    ImGui::SameLine(220);
            }
            if (ImGui::SliderFloat("Dim mix", &pc_button_prompts_dim_mix, 0.0f, 1.0f, "%.2f"))
                pc_button_prompts_colors_changed();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Settings button_prompts_dim_mix: how far a dimmed D-pad arm goes toward that colour.");
            if (ImGui::SmallButton("Default colours")) {
                for (int k = 0; k < PC_PROMPTS_COLORS; k++)
                    pc_button_prompts_colors[k] = pc_button_prompts_color_defaults[k];
                pc_button_prompts_dim_mix = 0.62f;
                pc_button_prompts_colors_changed();
            }
            ImGui::TreePop();
        }
    }
    bool subpixel = pc_ui_subpixel != 0;
    if (ImGui::Checkbox("Sub-pixel text placement", &subpixel))
        pc_ui_subpixel = subpixel;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Text glyphs drawn where the game places them in 16.16 fixed point instead of whole\n"
                          "PS1 pixels: even spacing while talk bubbles zoom open and shut. Off: as the PS1.");
}
