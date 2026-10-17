// The Travel prompt (fast travel, pc/docs/Fast_Travel.md): while Ashley stands
// on a Circle of Magic and the Travel button would open the destinations
// (fast_travel.c pc_fast_travel_prompt), the Travel binding's icon for the
// device used last (pc_prompt_glyph) and "Travel" on a small plate over the
// game picture, centred at qol_fast_travel_prompt_x / _y (fractions of the
// window), qol_fast_travel_prompt_scale its size. Nothing when that device has
// nothing bound to Travel. Called from dev_overlay.cpp's frame.
// Modern UI only: with ui_menus off the classic look keeps the game's own
// button sprites and layout (a classic prompt comes with the action bar).
#include "button_prompts.h"
#include "fast_travel.h"
#include "imgui.h"
#include <cfloat>

extern "C" int pc_fast_travel_prompt(void) __attribute__((weak)); // fast_travel.c (battle builds)
extern "C" unsigned pc_fast_travel_prompt_drawn __attribute__((weak));
extern "C" int pc_ui_menus; // project_settings.cpp

extern "C" void pc_fast_travel_prompt_draw(void)
{
    if (!pc_ui_menus || !pc_fast_travel_prompt || !pc_fast_travel_prompt())
        return;
    ImGuiIO& io = ImGui::GetIO();
    float size = io.DisplaySize.y * 0.045f * pc_qol_fast_travel_prompt_scale;
    pc_prompt_image g = pc_prompt_glyph(PC_PROMPT_TRAVEL, size);
    if (!g.texture)
        return;
    const char* label = "Travel";
    ImFont* font = ImGui::GetFont();
    float fs = size * 0.7f, pad = size * 0.25f;
    ImVec2 text = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
    float w = pad + g.w + pad + text.x + pad * 2, h = (g.h > text.y ? g.h : text.y) + pad * 2;
    float x = io.DisplaySize.x * pc_qol_fast_travel_prompt_x - w * 0.5f;
    float y = io.DisplaySize.y * pc_qol_fast_travel_prompt_y - h * 0.5f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(12, 10, 9, 200), h * 0.5f);
    // ImTextureRef has no ImTextureData* constructor: GetTexRef (prompt_glyphs.cpp)
    dl->AddImage(((ImTextureData*)g.texture)->GetTexRef(), ImVec2(x + pad, y + (h - g.h) * 0.5f),
        ImVec2(x + pad + g.w, y + (h + g.h) * 0.5f), ImVec2(g.uv0[0], g.uv0[1]), ImVec2(g.uv1[0], g.uv1[1]));
    dl->AddText(font, fs, ImVec2(x + pad * 2 + g.w, y + (h - text.y) * 0.5f), IM_COL32(232, 224, 208, 255), label);
    if (&pc_fast_travel_prompt_drawn)
        ++pc_fast_travel_prompt_drawn;
}
