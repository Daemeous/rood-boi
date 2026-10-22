// rood.ui.hide's and the format's host side (photo.h; pc/docs/Extensions.md "Photo mode foundation"):
// every owner's hidden layers together (the HUD: the classic hooks and the modern pass; the game's 2D:
// boot_runtime.c's draw_packet; the developer overlay: dev_overlay.cpp), and the newest owner's format
// (an aspect ratio: bars over the live view, a capture cropped to it). A change while paused asks for a
// render (photo.cpp).
#include "photo.h"
#include "photo_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

extern "C" {
int pc_photo_hide_game2d;
int pc_photo_hide_actors;
int pc_photo_game2d_slots = 35; /* the field's ordering table: entries 0..34, vs_scratch.unk4 (entry 18) +- 16 */
extern int pc_gl_view[4] __attribute__((weak));
}

namespace {
std::vector<std::pair<std::string, int>> hidden;
int hidden_flags;
unsigned hide_frames; /* the game frame the hidden layers last changed at (a capture while running waits) */
void hidden_update()
{
    int flags = 0;
    for (const auto& h : hidden)
        flags |= h.second;
    if (flags != hidden_flags) {
        hidden_flags = flags;
        hide_frames = photo::game_frames();
        pc_photo_redraw();
    }
    pc_photo_hide_game2d = (flags & PC_PHOTO_HIDE_GAME2D) != 0;
    pc_photo_hide_actors = (flags & PC_PHOTO_HIDE_ASHLEY ? 1 : 0) | (flags & PC_PHOTO_HIDE_OTHERS ? 2 : 0);
}

std::vector<std::pair<std::string, float>> formats;
std::vector<std::pair<std::string, unsigned>> format_colours; /* the bars' colour (0xRRGGBB), newest owner's */
} // namespace

/* The picture's rectangle in the window cropped to the format: x, y, w, h (top-left origin). */
void photo::format_rect(const int view[4], int out[4])
{
    memcpy(out, view, 4 * sizeof(int));
    float a = pc_photo_format();
    if (a <= 0 || view[2] <= 0 || view[3] <= 0)
        return;
    float va = (float)view[2] / view[3];
    if (a > va) { /* wider: bars above and below */
        int h = (int)lroundf(view[2] / a);
        out[1] = view[1] + (view[3] - h) / 2, out[3] = h;
    } else {
        int w = (int)lroundf(view[3] * a);
        out[0] = view[0] + (view[2] - w) / 2, out[2] = w;
    }
}
unsigned photo::hide_changed_frame() { return hide_frames; }

/* ------------------------------------------------------------ ui */

extern "C" void pc_photo_ui_hide(const char* owner, int flags)
{
    auto it = std::find_if(hidden.begin(), hidden.end(), [&](const auto& h) { return h.first == owner; });
    if (flags) {
        if (it == hidden.end())
            hidden.emplace_back(owner, flags);
        else
            it->second = flags;
    } else if (it != hidden.end())
        hidden.erase(it);
    hidden_update();
}
extern "C" int pc_photo_ui_hidden(void) { return hidden_flags; }
extern "C" int pc_photo_hide_hud(void) { return (hidden_flags & PC_PHOTO_HIDE_HUD) != 0; }
extern "C" int pc_photo_hide_overlay(void) { return (hidden_flags & PC_PHOTO_HIDE_OVERLAY) != 0; }

/* ------------------------------------------------------------ format */

extern "C" void pc_photo_format_set(const char* owner, float aspect)
{
    float before = pc_photo_format();
    formats.erase(std::remove_if(formats.begin(), formats.end(), [&](const auto& f) { return f.first == owner; }),
        formats.end());
    if (aspect > 0)
        formats.emplace_back(owner, fmaxf(0.2f, fminf(5, aspect)));
    (void)before;
}
extern "C" float pc_photo_format(void) { return formats.empty() ? 0.0f : formats.back().second; }
/* The bars' colour over the view (the photo is cropped to the format: never in it); black by default. */
extern "C" void pc_photo_format_colour_set(const char* owner, int rgb)
{
    format_colours.erase(std::remove_if(format_colours.begin(), format_colours.end(),
                             [&](const auto& f) { return f.first == owner; }),
        format_colours.end());
    if (rgb > 0)
        format_colours.emplace_back(owner, (unsigned)rgb & 0xFFFFFFu);
}
extern "C" int pc_photo_format_colour(void) { return format_colours.empty() ? 0 : (int)format_colours.back().second; }
extern "C" void pc_photo_format_draw(void)
{
    if (formats.empty() || !&pc_gl_view || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0)
        return;
    int r[4];
    photo::format_rect(pc_gl_view, r);
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    unsigned rgb = (unsigned)pc_photo_format_colour();
    const ImU32 black = IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
    float x0 = (float)pc_gl_view[0], y0 = (float)pc_gl_view[1];
    float x1 = x0 + pc_gl_view[2], y1 = y0 + pc_gl_view[3];
    if (r[1] > pc_gl_view[1]) {
        d->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, (float)r[1]), black);
        d->AddRectFilled(ImVec2(x0, (float)(r[1] + r[3])), ImVec2(x1, y1), black);
    }
    if (r[0] > pc_gl_view[0]) {
        d->AddRectFilled(ImVec2(x0, y0), ImVec2((float)r[0], y1), black);
        d->AddRectFilled(ImVec2((float)(r[0] + r[2]), y0), ImVec2(x1, y1), black);
    }
}

namespace {
void release(const std::string& owner)
{
    pc_photo_format_set(owner.c_str(), 0);
    pc_photo_format_colour_set(owner.c_str(), 0);
    pc_photo_ui_hide(owner.c_str(), 0);
}
void owners(std::vector<std::string>& out)
{
    for (const auto& h : hidden)
        out.push_back(h.first);
    for (const auto& f : formats)
        out.push_back(f.first);
    for (const auto& f : format_colours)
        out.push_back(f.first);
}
void reset()
{
    hidden.clear();
    formats.clear();
    format_colours.clear();
    hidden_flags = 0;
    pc_photo_hide_actors = 0;
    pc_photo_hide_game2d = 0; /* (crosses an engine reload: a host-owned block) */
}
void json(std::string& out)
{
    char text[96];
    snprintf(text, sizeof text, ",\"hidden\":%d,\"game2d_slots\":%d,\"format\":%.4f", hidden_flags,
        pc_photo_game2d_slots, pc_photo_format());
    out += text;
}
void params(const char* json)
{
    int n;
    if (photo::json_int(json, "game2d_slots", &n))
        pc_photo_game2d_slots = std::max(0, std::min(0x822, n));
}
photo::Register part({ "ui", release, owners, reset, json, params });
} // namespace
