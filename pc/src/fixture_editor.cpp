// The editor's side of the fixture catalog (fixture_catalog.h): a generated
// actor's fixture type in Details, "Edit type T00x" (the type's prefab
// parameters, light offset, fixture radius, glow treatment: every instance
// follows at once, live), saving the catalog, regenerating the room's or
// zone's generated layer (pc/tools/generate_lighting.py), and RPC fixtures.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "fixture_catalog.h"
#include "glow_overrides.h"
#include "lighting_layers.h"
#include "lights_internal.h"

using namespace rood_lights;
using prefab::Json;
using prefab::Prefab;
using prefab::Value;

namespace rood_lights {
bool fixture_type_details(int index);
}
extern "C" void pc_lights_poll(void);

namespace {
const ImVec4 own_colour(1.0f, 0.78f, 0.3f, 1);

/* ---- regenerating (a background run of the generator) ---- */
std::mutex regen_lock;
std::string regen_message;
std::atomic<int> regen_running { 0 };
int regen_runs;

std::string shell_quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}
std::string regenerate_command(int z, int r)
{
    std::string root = lighting::packs_root();
    std::vector<std::string> packs = lighting::packs();
    if (root.empty() || packs.empty())
        return std::string();
    std::string cmd = "python3 pc/tools/generate_lighting.py --quiet --zone " + std::to_string(z);
    if (r >= 0)
        cmd += " --room " + std::to_string(r);
    cmd += " --root " + shell_quote(root) + " --pack " + shell_quote(packs.front()) + " --catalog "
        + shell_quote(fixtures::catalog().path) + " 2>&1";
    return cmd;
}
std::string run(const std::string& cmd)
{
    FILE* p = popen(cmd.c_str(), "r");
    if (!p)
        return "cannot run the generator";
    std::string out, last;
    char line[512];
    while (fgets(line, sizeof line, p)) {
        out += line;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n')
            line[n - 1] = 0;
        if (line[0])
            last = line;
    }
    int status = pclose(p);
    fprintf(stderr, "[fixtures] %s\n%s", cmd.c_str(), out.c_str());
    return status ? "Generator failed: " + last : last;
}
/* Regenerate zone z (room r, or every room with r < 0); in the background
 * unless wait. The layers reload live when the files change. */
bool regenerate(int z, int r, bool wait, std::string* message)
{
    std::string cmd = regenerate_command(z, r);
    if (cmd.empty()) {
        if (message)
            *message = "No lighting packs (ROOD_LIGHTING): nothing to regenerate";
        return false;
    }
    if (fixtures::catalog().dirty) {
        std::string m;
        if (!fixtures::save(&m)) { /* the generator reads the file */
            if (message)
                *message = m;
            return false;
        }
    }
    if (regen_running.exchange(1)) {
        if (message)
            *message = "The generator is already running";
        return false;
    }
    ++regen_runs;
    if (wait) {
        std::string m = run(cmd);
        regen_running = 0;
        std::lock_guard<std::mutex> g(regen_lock);
        regen_message = m;
        if (message)
            *message = m;
        return m.rfind("Generator failed", 0) != 0;
    }
    std::thread([cmd]() {
        std::string m = run(cmd);
        {
            std::lock_guard<std::mutex> g(regen_lock);
            regen_message = m;
        }
        regen_running = 0;
    }).detach();
    if (message)
        *message = "Regenerating...";
    return true;
}
std::string last_regen()
{
    std::lock_guard<std::mutex> g(regen_lock);
    return regen_running ? std::string("Regenerating...") : regen_message;
}

int instances_in_room(const std::string& type)
{
    int n = 0;
    for (const prefab::Actor& a : actors)
        n += a.fixture_type == type;
    return n;
}
std::string format_value(const prefab::Param& q, const Value& v)
{
    char text[64];
    if (q.type == prefab::PARAM_COLOR)
        snprintf(text, sizeof text, "%.2f, %.2f, %.2f", v.at(0), v.at(1), v.at(2));
    else if (q.type == prefab::PARAM_BOOL)
        snprintf(text, sizeof text, "%s", v.x() > 0.5f ? "on" : "off");
    else if (q.type == prefab::PARAM_ENUM) {
        int i = (int)lroundf(v.x());
        snprintf(text, sizeof text, "%s", i >= 0 && i < (int)q.options.size() ? q.options[i].c_str() : "?");
    } else
        snprintf(text, sizeof text, "%.3g", v.x());
    return text;
}
/* One prefab parameter of the type: the class's value or the type's own
 * (highlighted, reset arrow back to the class's). */
void type_param_row(const fixtures::Type& t, const prefab::Param& q)
{
    const Value* typed = t.param(q.key);
    Value v = typed ? *typed : q.def;
    if (q.key == "glow_mode")
        return; /* the type's Glow below */
    bool own = t.owns("params." + q.key), changed = false;
    ImGui::PushID(q.key.c_str());
    if (own)
        ImGui::PushStyleColor(ImGuiCol_Text, own_colour);
    float width = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 1.6f;
    ImGui::SetNextItemWidth(width * 0.55f);
    if (q.type == prefab::PARAM_FLOAT) {
        float x = v.x();
        float span = q.max - q.min;
        const char* format = span >= 200 ? "%.0f" : span >= 20 ? "%.1f" : "%.2f";
        if (ImGui::SliderFloat(q.label.c_str(), &x, q.min, q.max, format,
                q.logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None))
            v = Value::scalar(x), changed = true;
    } else if (q.type == prefab::PARAM_COLOR) {
        float c[3] = { v.at(0), v.at(1), v.at(2) };
        if (ImGui::ColorEdit3(q.label.c_str(), c))
            v = Value::vec(c[0], c[1], c[2]), changed = true;
    } else if (q.type == prefab::PARAM_BOOL) {
        bool b = v.x() > 0.5f;
        if (ImGui::Checkbox(q.label.c_str(), &b))
            v = Value::scalar(b ? 1.0f : 0.0f), changed = true;
    } else {
        int i = (int)lroundf(v.x());
        std::string items;
        for (const std::string& o : q.options)
            items += o + '\0';
        items += '\0';
        if (ImGui::Combo(q.label.c_str(), &i, items.c_str()))
            v = Value::scalar((float)i), changed = true;
    }
    if (own)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s for every instance of %s%s", own ? "This type's value" : "From its class (or the prefab)",
            t.id.c_str(), own ? "" : "\nChange it to set it for this type");
    if (changed)
        fixtures::set_param(t.id, q.key, &v);
    if (own) {
        ImGui::SameLine();
        if (ImGui::ArrowButton("##reset", ImGuiDir_Left))
            fixtures::set_param(t.id, q.key, nullptr);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Back to the class's value");
    }
    ImGui::PopID();
}
} // namespace

/* Details > Fixture type, for an actor that names one. */
bool rood_lights::fixture_type_details(int index)
{
    if (index < 0 || index >= (int)actors.size() || actors[index].fixture_type.empty())
        return false;
    const prefab::Actor& a = actors[index];
    ImGui::SeparatorText("Fixture type");
    const fixtures::Type* t = fixtures::find(a.fixture_type);
    if (!t) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Type %s is not in the catalog (%s)", a.fixture_type.c_str(),
            fixtures::catalog().path.c_str());
        return true;
    }
    ImGui::Text("%s: %s", t->id.c_str(), t->description.c_str());
    ImGui::TextDisabled("Class %s; flame %d of %d; %d instance%s in this room, %d on the disc (%d rooms); review: %s",
        t->cls.c_str(), a.flame + 1, t->flames ? t->flames : 1, instances_in_room(t->id),
        instances_in_room(t->id) == 1 ? "" : "s", t->instances, t->rooms, t->review.empty() ? "-" : t->review.c_str());
    std::string label = "Edit type " + t->id;
    if (!ImGui::CollapsingHeader(label.c_str()))
        return true;
    ImGui::PushID(t->id.c_str());
    ImGui::TextWrapped("Changes apply to every %s at once, live (this actor's own overrides above still win). "
                       "Save writes %s; prefab and on/off take effect when the room is regenerated.",
        t->id.c_str(), fixtures::catalog().path.c_str());
    const Prefab* p = prefab::library().find(t->prefab);
    if (p)
        for (const prefab::Param& q : p->params)
            type_param_row(*t, q);
    float off[3] = { t->offset[0], t->offset[1], t->offset[2] };
    bool own_off = t->owns("offset");
    if (own_off)
        ImGui::PushStyleColor(ImGuiCol_Text, own_colour);
    if (ImGui::DragFloat3("Light offset", off, 0.25f, -256, 256, "%.1f"))
        fixtures::set_offset(t->id, off);
    if (own_off)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("From the flame's centre, in the fixture's frame: x right, y down (negative: up), z forward");
    if (own_off) {
        ImGui::SameLine();
        if (ImGui::ArrowButton("##offreset", ImGuiDir_Left))
            fixtures::set_offset(t->id, nullptr);
    }
    float radius = t->fixture_radius;
    if (ImGui::SliderFloat("Fixture radius (no self-shadow)", &radius, 0, 128, "%.0f u"))
        fixtures::set_fixture_radius(t->id, &radius);
    int glow = t->glow;
    if (ImGui::Combo("Game glow", &glow, "Leave\0Drive (flickers with the light)\0Suppress (not a light)\0\0"))
        fixtures::set_glow(t->id, &glow);
    bool enabled = t->enabled;
    if (ImGui::Checkbox("Lit by default", &enabled))
        fixtures::set_enabled(t->id, &enabled);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Generated actors of this type start on or off (inferred fill lights are off); regenerate to apply");
    static const char* reviews[] = { "scan", "new", "checked" };
    int review = 0;
    for (int i = 0; i < 3; i++)
        if (t->review == reviews[i])
            review = i;
    if (ImGui::Combo("Review", &review, "scan (classified by the scan)\0new (found since)\0checked (by a person)\0\0"))
        fixtures::set_review(t->id, reviews[review]);
    ImGui::TextDisabled("Prefab %s (regenerate after changing it in the catalog)", t->prefab.c_str());
    bool dirty_catalog = fixtures::catalog().dirty;
    if (!dirty_catalog)
        ImGui::BeginDisabled();
    if (ImGui::Button("Save catalog")) {
        std::string m;
        fixtures::save(&m);
        snprintf(status, sizeof status, "%s", m.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
        fixtures::revert();
    if (!dirty_catalog)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Regenerate room")) {
        std::string m;
        regenerate(zone, room, false, &m);
        snprintf(status, sizeof status, "%s", m.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Regenerate zone")) {
        std::string m;
        regenerate(zone, -1, false, &m);
        snprintf(status, sizeof status, "%s", m.c_str());
    }
    std::string last = last_regen();
    if (!last.empty())
        ImGui::TextDisabled("%s", last.c_str());
    if (dirty_catalog)
        ImGui::TextColored(own_colour, "Unsaved type edits (they already apply here)");
    ImGui::PopID();
    return true;
}

/* ---- RPC fixtures ---- */
namespace {
std::string num(double v)
{
    char text[32];
    snprintf(text, sizeof text, "%.6g", v);
    return text;
}
std::string value_json(const Value& v)
{
    return v.n == 3 ? "[" + num(v.v[0]) + "," + num(v.v[1]) + "," + num(v.v[2]) + "]" : num(v.x());
}
std::string type_json(const fixtures::Type& t)
{
    std::string out = "{\"id\":" + prefab::json_quote(t.id) + ",\"class\":" + prefab::json_quote(t.cls)
        + ",\"family\":" + prefab::json_quote(t.family) + ",\"prefab\":" + prefab::json_quote(t.prefab)
        + ",\"glow\":\"" + fixtures::glow_name(t.glow) + "\",\"enabled\":" + (t.enabled ? "true" : "false")
        + ",\"fixture_radius\":" + num(t.fixture_radius) + ",\"offset\":[" + num(t.offset[0]) + "," + num(t.offset[1])
        + "," + num(t.offset[2]) + "],\"review\":" + prefab::json_quote(t.review)
        + ",\"instances\":" + std::to_string(t.instances) + ",\"in_room\":" + std::to_string(instances_in_room(t.id))
        + ",\"params\":{";
    for (size_t k = 0; k < t.params.size(); k++)
        out += (k ? "," : "") + prefab::json_quote(t.params[k].first) + ":" + value_json(t.params[k].second);
    out += "},\"own\":[";
    for (size_t k = 0; k < t.own.size(); k++)
        out += (k ? "," : "") + prefab::json_quote(t.own[k]);
    return out + "]}";
}
int reply(const std::string& text, char* out, int size)
{
    if ((int)text.size() >= size) {
        snprintf(out, size, "reply too large (%zu bytes)", text.size());
        return -1;
    }
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size();
}
int fail(const std::string& m, char* out, int size)
{
    snprintf(out, size, "%s", m.c_str());
    return -1;
}
} // namespace

/* RPC fixtures: action list (default; type: one type; all: every type, else
 * the types in the room), set {type, params {key: value | null}, offset
 * (null: the class's), fixture_radius, glow (leave/drive/suppress), enabled,
 * review}, save, revert, regenerate {scope: room | zone} (waits). */
extern "C" int pc_fixtures_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != Json::Object)
        return fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    const Json* action_j = params.get("action");
    std::string action = action_j && action_j->type == Json::String ? action_j->string : "list";
    const Json* type_j = params.get("type");
    std::string type = type_j && type_j->type == Json::String ? type_j->string : "";
    std::string message;
    if (action == "set") {
        const fixtures::Type* t = fixtures::find(type);
        if (!t)
            return fail("unknown type " + type, out, size);
        const Prefab* p = prefab::library().find(t->prefab);
        if (const Json* ps = params.get("params")) {
            for (const auto& m : ps->members) {
                if (m.second.type == Json::Null) {
                    fixtures::set_param(type, m.first, nullptr);
                    continue;
                }
                const prefab::Param* q = p ? p->param(m.first) : nullptr;
                if (!q)
                    return fail("unknown parameter " + m.first + " of prefab " + t->prefab, out, size);
                Value v;
                if (m.second.type == Json::Number)
                    v = Value::scalar((float)m.second.number);
                else if (m.second.type == Json::Bool)
                    v = Value::scalar(m.second.boolean ? 1.0f : 0.0f);
                else if (m.second.type == Json::Array && m.second.items.size() == 3)
                    v = Value::vec((float)m.second.items[0].number, (float)m.second.items[1].number,
                        (float)m.second.items[2].number);
                else
                    return fail("bad value for " + m.first, out, size);
                fixtures::set_param(type, m.first, &v);
            }
        }
        if (const Json* o = params.get("offset")) {
            if (o->type == Json::Null)
                fixtures::set_offset(type, nullptr);
            else if (o->type == Json::Array && o->items.size() == 3) {
                float f[3] = { (float)o->items[0].number, (float)o->items[1].number, (float)o->items[2].number };
                fixtures::set_offset(type, f);
            }
        }
        if (const Json* r = params.get("fixture_radius")) {
            float f = (float)r->number;
            fixtures::set_fixture_radius(type, r->type == Json::Null ? nullptr : &f);
        }
        if (const Json* g = params.get("glow")) {
            int mode = g->type == Json::String
                ? (g->string == "drive" ? fixtures::GLOW_DRIVE : g->string == "suppress" ? fixtures::GLOW_SUPPRESS : fixtures::GLOW_LEAVE)
                : 0;
            fixtures::set_glow(type, g->type == Json::Null ? nullptr : &mode);
        }
        if (const Json* e = params.get("enabled")) {
            bool b = e->boolean;
            fixtures::set_enabled(type, e->type == Json::Null ? nullptr : &b);
        }
        if (const Json* r = params.get("review"); r && r->type == Json::String)
            fixtures::set_review(type, r->string);
    } else if (action == "save") {
        if (!fixtures::save(&message))
            return fail(message, out, size);
    } else if (action == "revert") {
        fixtures::revert();
    } else if (action == "regenerate") {
        const Json* scope = params.get("scope");
        bool whole = scope && scope->type == Json::String && scope->string == "zone";
        if (zone < 0)
            return fail("no room loaded", out, size);
        if (!regenerate(zone, whole ? -1 : room, true, &message))
            return fail(message, out, size);
        pc_lights_poll(); /* take the new files now (not half a second later) */
    } else if (action != "list") {
        return fail("unknown action " + action, out, size);
    }
    const fixtures::Catalog& c = fixtures::catalog();
    const Json* all = params.get("all");
    std::string text = "{\"path\":" + prefab::json_quote(c.path) + ",\"error\":" + prefab::json_quote(c.error)
        + ",\"dirty\":" + (c.dirty ? "true" : "false") + ",\"count\":" + std::to_string(c.types.size())
        + ",\"message\":" + prefab::json_quote(message) + ",\"regenerated\":" + std::to_string(regen_runs)
        + ",\"types\":[";
    int n = 0;
    for (const fixtures::Type& t : c.types) {
        bool want = !type.empty() ? t.id == type : (all && all->boolean) || instances_in_room(t.id) > 0;
        if (want)
            text += (n++ ? "," : "") + type_json(t);
    }
    text += "],\"actors\":[";
    n = 0;
    for (size_t i = 0; i < actors.size(); i++) {
        const prefab::Actor& a = actors[i];
        if (a.fixture_type.empty())
            continue;
        text += std::string(n++ ? "," : "") + "{\"index\":" + std::to_string(i) + ",\"id\":"
            + prefab::json_quote("actor:" + a.id) + ",\"type\":" + prefab::json_quote(a.fixture_type) + ",\"flame\":"
            + std::to_string(a.flame) + ",\"enabled\":" + (a.enabled ? "true" : "false") + "}";
    }
    text += "],\"glows\":[";
    static pc_glow_info glows[160];
    int count = pc_gi_glow_list(glows, 160);
    n = 0;
    for (int i = 0; i < count; i++)
        if (glows[i].fixture_type[0])
            text += std::string(n++ ? "," : "") + "{\"id\":" + std::to_string(glows[i].id) + ",\"key\":"
                + prefab::json_quote(glows[i].key) + ",\"type\":" + prefab::json_quote(glows[i].fixture_type)
                + ",\"muted\":" + std::to_string(glows[i].muted) + ",\"traced\":" + (glows[i].traced ? "true" : "false")
                + "}";
    return reply(text + "]}", out, size);
}
