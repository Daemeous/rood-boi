// Emitter assets (particle_assets.h).
#include "particle_assets.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "light_prefabs.h"

namespace rood_particles {
namespace {
std::vector<Asset> loaded;
bool loaded_once;
long long loaded_stamp; /* the directory's stamp as read (prefab::directory_stamp) */
std::string status;

/* Where each JSON value goes: group (a top-level object, or a module under "modules"), key, lighting field. */
struct Path {
    const char* group;
    const char* key;
    const char* field;
};
const Path PATHS[] = {
    { "spawn", "shape", "shape" }, { "spawn", "size", "size" }, { "spawn", "radius", "radius" },
    { "spawn", "count", "count" }, { "spawn", "lifetime", "lifetime" }, { "spawn", "lifetime_jitter", "lifetime_jitter" },
    { "drift", "velocity", "drift" }, { "curl", "amplitude", "curl" }, { "curl", "scale", "curl_scale" },
    { "curl", "speed", "curl_speed" }, { "gravity", "acceleration", "gravity" }, { "drag", "rate", "drag" },
    { "fade", "in", "fade_in" }, { "fade", "out", "fade_out" }, { "render", "type", "render" },
    { "render", "pixel_min", "pixel_min" }, { "render", "pixel_max", "pixel_max" }, { "render", "color", "color" },
    { "render", "brightness", "brightness" }, { "render", "variation", "variation" }, { "lighting", "mode", "lit" },
    { "lighting", "ambient", "ambient" },
};
const char* const MODULES[] = { "drift", "curl", "gravity", "drag", "fade" };
bool is_module(const char* group)
{
    for (const char* m : MODULES)
        if (!strcmp(m, group))
            return true;
    return false;
}
std::string text_of(const prefab::Json& j)
{
    if (j.type == prefab::Json::String)
        return j.string;
    if (j.type == prefab::Json::Bool)
        return j.boolean ? "1" : "0";
    if (j.type == prefab::Json::Number)
        return lighting::format_number(j.number, 6);
    std::string s;
    if (j.type == prefab::Json::Array)
        for (size_t i = 0; i < j.items.size(); i++)
            s += (i ? "," : "") + lighting::format_number(j.items[i].number, 6);
    return s;
}
bool read_text(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buffer[4096];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof buffer, f)) > 0)
        out.append(buffer, n);
    fclose(f);
    return true;
}
} // namespace

const std::string* Asset::get(const std::string& key) const
{
    for (const auto& f : fields)
        if (f.first == key)
            return &f.second;
    return nullptr;
}

std::string assets_dir()
{
    const char* e = getenv("ROOD_EMITTERS");
    return e && *e ? e : "pc/assets/emitters";
}

bool asset_from_json(const std::string& id, const prefab::Json& root, Asset& out, std::string* error)
{
    out = Asset();
    out.id = id;
    auto fail = [&](const std::string& why) {
        if (error)
            *error = id + ": " + why;
        return false;
    };
    const prefab::Json* kind = root.get("kind");
    if (root.type != prefab::Json::Object || !kind || kind->string != "emitter")
        return fail("not an emitter asset (kind \"emitter\")");
    for (const char* key : { "name", "category", "icon", "description" })
        if (const prefab::Json* j = root.get(key)) {
            std::string& to = !strcmp(key, "name") ? out.name : !strcmp(key, "category") ? out.category
                : !strcmp(key, "icon")                                                ? out.icon
                                                                                      : out.description;
            to = j->string;
        }
    if (out.name.empty())
        out.name = id;
    if (const prefab::Json* tags = root.get("tags"))
        for (const prefab::Json& t : tags->items)
            out.tags.push_back(t.string);
    const prefab::Json* modules = root.get("modules");
    if (modules)
        for (const auto& m : modules->members)
            if (!is_module(m.first.c_str()))
                return fail("modules: unknown module '" + m.first + "' (drift, curl, gravity, drag, fade)");
    if (const prefab::Json* q = root.get("quality"))
        out.fields.emplace_back("quality", q->string);
    for (const Path& p : PATHS) {
        const prefab::Json* group = is_module(p.group) ? (modules ? modules->get(p.group) : nullptr) : root.get(p.group);
        const prefab::Json* v = group ? group->get(p.key) : nullptr;
        if (!v)
            continue;
        std::string text = text_of(*v);
        if (!strcmp(p.field, "lit")) {
            if (text != "in_light" && text != "lit")
                return fail("lighting.mode is in_light or lit, not '" + text + "'");
            text = text == "lit" ? "1" : "0";
        }
        if (!strcmp(p.field, "shape") && text != "box" && text != "room" && text != "point" && text != "disc"
            && text != "surface")
            return fail("spawn.shape is box, room, point, disc or surface, not '" + text + "'");
        if (!strcmp(p.field, "render") && text != "pixel")
            return fail("render.type is pixel (soft sprites and streaks come later), not '" + text + "'");
        out.fields.emplace_back(p.field, lighting::canonical_value("emitter", p.field, text));
    }
    return true;
}

void reload_assets()
{
    loaded_once = true;
    loaded.clear();
    status.clear();
    std::string dir = assets_dir(), problems;
    loaded_stamp = prefab::directory_stamp(dir);
    DIR* d = opendir(dir.c_str());
    if (!d) {
        status = "no emitter assets (" + dir + ")";
        return;
    }
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json"))
            continue;
        std::string text, error;
        prefab::Json root;
        Asset a;
        if (!read_text(dir + "/" + name, text) || !prefab::json_parse(text.c_str(), root, &error)
            || !asset_from_json(name.substr(0, name.size() - 5), root, a, &error)) {
            problems += (problems.empty() ? "" : "; ") + name + ": " + error;
            continue;
        }
        loaded.push_back(a);
    }
    closedir(d);
    std::sort(loaded.begin(), loaded.end(), [](const Asset& a, const Asset& b) { return a.id < b.id; });
    status = std::to_string(loaded.size()) + (loaded.size() == 1 ? " asset (" : " assets (") + dir + ")"
        + (problems.empty() ? "" : "; " + problems);
    if (!problems.empty())
        fprintf(stderr, "[particles] %s\n", problems.c_str());
}
bool assets_changed() { return loaded_once && prefab::directory_stamp(assets_dir()) != loaded_stamp; }
const std::vector<Asset>& assets()
{
    if (!loaded_once)
        reload_assets();
    return loaded;
}
const Asset* find_asset(const std::string& id)
{
    for (const Asset& a : assets())
        if (a.id == id)
            return &a;
    return nullptr;
}
const std::string& assets_status()
{
    assets();
    return status;
}
} // namespace rood_particles
