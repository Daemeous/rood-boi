// The material preview (pc/docs/plans/2026-10-30-master-materials-plan.md, decision 6): a sphere
// shaded on the CPU with the shared GGX model (pc/docs/Materials.md "Shading": GGX D,
// height-correlated Smith, Schlick; F0 = 0.04 x specular mixed to the albedo by metal), textured
// nearest with the instance's texture where it is drawn now (else a swatch), lit by a key light and
// an ambient whose direction, colour and strength are settings keys (material_preview_*). Drawn as
// rectangles, so it is the same picture on GL and Vulkan; reshaded only when something changed.
// Window > Material preview; RPC material_preview.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_shell.h"
#include "emissive_internal.h"
#include "light_prefabs.h" /* prefab::Json */
#include "materials.h"

using prefab::Json;

extern "C" {
extern float pc_specular_strength, pc_roughness_bias;
/* The look (settings keys, lighting_quality.cpp's table; RPC graphics): the sphere's pixels across,
 * the key light's strength and direction (degrees: yaw around the vertical, pitch above the
 * horizon, as seen from the viewer), the ambient's strength, the swatch's grey. */
float pc_material_preview_size = 96, pc_material_preview_light = 1.6f, pc_material_preview_yaw = -35,
      pc_material_preview_pitch = 35, pc_material_preview_ambient = 0.22f, pc_material_preview_swatch = 0.6f;
}
bool pc_material_preview_open = false;

namespace {
std::string shown; /* the instance previewed (the Materials panel's selection follows it) */
std::vector<uint8_t> pixels; /* size x size RGB */
int pixels_size = 0;
std::string cache_key;
uint32_t last_generation = 0;
std::string texture_key_used;

std::string key_of(const materials::Instance* i, const std::map<std::string, float>& values)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s|%d|%.3f|%.1f|%.1f|%.3f|%.3f|%.3f|%.3f", i ? i->id.c_str() : "", (int)pc_material_preview_size,
        pc_material_preview_light, pc_material_preview_yaw, pc_material_preview_pitch, pc_material_preview_ambient,
        pc_material_preview_swatch, pc_specular_strength, pc_roughness_bias);
    std::string k = buf;
    for (const auto& p : values) k += "|" + p.first + "=" + std::to_string(p.second);
    if (i) k += "|" + i->key + "|" + i->clut;
    return k;
}
float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* The sphere, reshaded when its inputs changed. */
void shade()
{
    const materials::Instance* i = shown.empty() ? nullptr : materials::find_instance(shown);
    std::map<std::string, float> values;
    if (i) materials::resolve(i->id, values, nullptr, nullptr);
    uint32_t gen = materials::catalog_generation();
    std::string k = key_of(i, values) + "|" + std::to_string(gen);
    if (k == cache_key) return;
    cache_key = k;
    materials::Surface s = materials::surface_of(values);
    int n = (int)pc_material_preview_size;
    if (n < 8) n = 8;
    if (n > 256) n = 256;
    pixels_size = n;
    pixels.assign((size_t)n * n * 3, 0);
    /* The texture (nearest): the instance's own key, drawn now. */
    emissive::TexturePick t;
    std::vector<uint8_t> rgba;
    int tw = 0, th = 0;
    texture_key_used.clear();
    if (i && !i->key.empty() && emissive::texture_where(i->key, t)) {
        rgba = emissive::texture_rgba_box(t.tpage, t.clut, t.box);
        tw = t.w(), th = t.h();
        texture_key_used = i->key;
    }
    float yaw = pc_material_preview_yaw * (float)M_PI / 180, pitch = pc_material_preview_pitch * (float)M_PI / 180;
    float L[3] = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
    float V[3] = { 0, 0, 1 };
    float H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
    float hl = sqrtf(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
    H[0] /= hl, H[1] /= hl, H[2] /= hl;
    float rough = clamp01(s.roughness + pc_roughness_bias);
    float a = rough * rough;
    if (a < 0.03f) a = 0.03f;
    float a2 = a * a;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float px = ((x + 0.5f) / n) * 2 - 1, py = 1 - ((y + 0.5f) / n) * 2;
            float r2 = px * px + py * py;
            uint8_t* o = &pixels[((size_t)y * n + x) * 3];
            if (r2 > 1) {
                o[0] = 28, o[1] = 28, o[2] = 32; /* the surround */
                continue;
            }
            float N[3] = { px, py, sqrtf(1 - r2) };
            float albedo[3] = { pc_material_preview_swatch, pc_material_preview_swatch, pc_material_preview_swatch };
            if (tw > 0) {
                float u = 0.5f + atan2f(N[0], N[2]) / (2 * (float)M_PI), v = 0.5f - asinf(N[1]) / (float)M_PI;
                int tx = (int)(u * tw * 2) % tw, ty = (int)(v * th * 2) % th; /* the texture twice around */
                const uint8_t* p = &rgba[((size_t)ty * tw + tx) * 4];
                if (p[3]) albedo[0] = p[0] / 255.0f, albedo[1] = p[1] / 255.0f, albedo[2] = p[2] / 255.0f;
            }
            float NoL = N[0] * L[0] + N[1] * L[1] + N[2] * L[2];
            float NoV = N[2], NoH = N[0] * H[0] + N[1] * H[1] + N[2] * H[2], VoH = H[2];
            if (NoL < 0) NoL = 0;
            float d = NoH * NoH * (a2 - 1) + 1;
            float D = a2 / ((float)M_PI * d * d);
            float gv = NoL * sqrtf(NoV * NoV * (1 - a2) + a2), gl = NoV * sqrtf(NoL * NoL * (1 - a2) + a2);
            float Vis = gv + gl > 0 ? 0.5f / (gv + gl) : 0;
            float fres = powf(1 - VoH, 5);
            float light = pc_material_preview_light, amb = pc_material_preview_ambient;
            for (int c = 0; c < 3; c++) {
                float f0 = 0.04f * s.specular * (1 - s.metal) + albedo[c] * s.metal;
                float f90 = s.specular * (1 - s.metal) + s.metal;
                float F = f0 + (f90 - f0) * fres;
                float diffuse = albedo[c] * (1 - s.metal) * (amb + NoL * light);
                float spec = (float)M_PI * D * Vis * F * NoL * light * pc_specular_strength + albedo[c] * s.metal * amb * F;
                float v = diffuse + spec;
                v = v / (1 + v * 0.35f); /* a soft shoulder */
                o[c] = (uint8_t)(clamp01(v) * 255 + 0.5f);
            }
        }
}
int fail(const std::string& m, char* out, int size)
{
    snprintf(out, size, "%s", m.c_str());
    return -1;
}
Json state_json()
{
    shade();
    Json j = Json::object();
    j.set("id", Json::str(shown));
    j.set("size", Json::num(pixels_size));
    j.set("texture", Json::str(texture_key_used));
    uint32_t sum = 0;
    double mean = 0;
    for (uint8_t v : pixels) sum = sum * 31 + v, mean += v;
    j.set("digest", Json::num(sum));
    j.set("mean", Json::num(pixels.empty() ? 0 : mean / (double)pixels.size()));
    Json centre = Json::array(), edge = Json::array();
    if (pixels_size) {
        const uint8_t* c = &pixels[((size_t)(pixels_size / 2) * pixels_size + pixels_size / 2) * 3];
        const uint8_t* e = &pixels[((size_t)(pixels_size / 2) * pixels_size + pixels_size / 8) * 3];
        for (int k = 0; k < 3; k++) centre.items.push_back(Json::num(c[k])), edge.items.push_back(Json::num(e[k]));
    }
    j.set("centre", centre), j.set("edge", edge);
    Json settings = Json::object();
    settings.set("material_preview_size", Json::num(pc_material_preview_size));
    settings.set("material_preview_light", Json::num(pc_material_preview_light));
    settings.set("material_preview_yaw", Json::num(pc_material_preview_yaw));
    settings.set("material_preview_pitch", Json::num(pc_material_preview_pitch));
    settings.set("material_preview_ambient", Json::num(pc_material_preview_ambient));
    settings.set("material_preview_swatch", Json::num(pc_material_preview_swatch));
    j.set("settings", settings);
    j.set("open", Json::boolean_(pc_material_preview_open));
    return j;
}
} // namespace

extern "C" void pc_material_preview_select(const char* id) { shown = id ? id : ""; }

extern "C" void pc_material_preview_window(void)
{
    if (!pc_material_preview_open) return;
    rood_shell::dock_tool_window("Material preview");
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material preview", &pc_material_preview_open)) {
        ImGui::End();
        return;
    }
    std::vector<materials::Instance> all = materials::instances();
    if (ImGui::BeginCombo("Instance", shown.empty() ? "(none)" : shown.c_str())) {
        for (const materials::Instance& i : all) {
            if (i.preview) continue;
            if (ImGui::Selectable(i.id.c_str(), shown == i.id)) shown = i.id;
        }
        ImGui::EndCombo();
    }
    shade();
    float px = ImGui::GetContentRegionAvail().x;
    if (px > 400) px = 400;
    if (pixels_size > 0) {
        float cell = px / pixels_size;
        ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int y = 0; y < pixels_size; y++)
            for (int x = 0; x < pixels_size; x++) {
                const uint8_t* p = &pixels[((size_t)y * pixels_size + x) * 3];
                ImVec2 a(o.x + x * cell, o.y + y * cell);
                dl->AddRectFilled(a, ImVec2(a.x + cell + 0.5f, a.y + cell + 0.5f), IM_COL32(p[0], p[1], p[2], 255));
            }
        ImGui::Dummy(ImVec2(px, px));
    }
    if (!shown.empty()) {
        std::map<std::string, float> values;
        if (materials::resolve(shown, values, nullptr, nullptr)) {
            materials::Surface s = materials::surface_of(values);
            ImGui::Text("rough %.2f  spec %.2f  metal %.2f  relief x%.2f%s", s.roughness, s.specular, s.metal, s.bump,
                s.auto_height ? " (estimated)" : "");
        }
        ImGui::TextDisabled("%s", texture_key_used.empty() ? "swatch (the texture is not drawn here)" : ("texture " + texture_key_used).c_str());
    }
    if (ImGui::TreeNode("Light")) {
        ImGui::SliderFloat("Key light", &pc_material_preview_light, 0, 4, "%.2f");
        ImGui::SliderFloat("Yaw", &pc_material_preview_yaw, -180, 180, "%.0f deg");
        ImGui::SliderFloat("Pitch", &pc_material_preview_pitch, -89, 89, "%.0f deg");
        ImGui::SliderFloat("Ambient", &pc_material_preview_ambient, 0, 1, "%.2f");
        ImGui::SliderFloat("Swatch grey", &pc_material_preview_swatch, 0, 1, "%.2f");
        ImGui::SliderFloat("Pixels", &pc_material_preview_size, 16, 256, "%.0f");
        ImGui::TextDisabled("Settings keys material_preview_* (RPC graphics)");
        ImGui::TreePop();
    }
    ImGui::End();
}

/* RPC material_preview: {action: state (default) | select {id} | open {open}}; the look is set by RPC
 * graphics (material_preview_size, _light, _yaw, _pitch, _ambient, _swatch). */
extern "C" int pc_material_preview_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "state";
    if (action == "select") {
        const Json* id = params.get("id");
        std::string s = id && id->type == Json::String ? id->string : "";
        if (!s.empty() && !materials::find_instance(s)) return fail("unknown instance " + s, out, size);
        shown = s;
    } else if (action == "open") {
        const Json* o = params.get("open");
        pc_material_preview_open = !o || o->type != Json::Bool || o->boolean;
    } else if (action != "state")
        return fail("unknown action " + action, out, size);
    std::string text = prefab::json_write(state_json(), 0);
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end()); /* one line: the RPC is line-based */
    if ((int)text.size() >= size) return fail("reply too large", out, size);
    memcpy(out, text.c_str(), text.size() + 1);
    return 0;
}
