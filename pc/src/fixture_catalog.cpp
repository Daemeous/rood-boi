// The fixture catalog: types of room fixtures and how they are lit
// (fixture_catalog.h; pc/docs/Fixture_Catalog.md).
#include "fixture_catalog.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"

using prefab::Json;
using prefab::Value;

namespace fixtures {

namespace {
Catalog the_catalog;
bool loaded;

std::string catalog_path()
{
    const char* env = getenv("ROOD_FIXTURES");
    return env && *env ? env : "pc/assets/fixtures/catalog.json";
}
long long file_stamp(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        return 0;
    return PC_MTIME_NS(st) + st.st_size;
}
bool read_text(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buffer[8192];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof buffer, f)) > 0)
        out.append(buffer, n);
    fclose(f);
    return true;
}
bool value_of(const Json& j, Value& out)
{
    if (j.type == Json::Number)
        out = Value::scalar((float)j.number);
    else if (j.type == Json::Bool)
        out = Value::scalar(j.boolean ? 1.0f : 0.0f);
    else if (j.type == Json::Array && j.items.size() == 3 && j.items[0].type == Json::Number
        && j.items[1].type == Json::Number && j.items[2].type == Json::Number)
        out = Value::vec((float)j.items[0].number, (float)j.items[1].number, (float)j.items[2].number);
    else
        return false;
    return true;
}
Json json_of(const Value& v)
{
    if (v.n != 3)
        return Json::num(roundf(v.x() * 1e4f) / 1e4f);
    Json a = Json::array();
    for (int k = 0; k < 3; k++)
        a.items.push_back(Json::num(roundf(v.v[k] * 1e4f) / 1e4f));
    return a;
}
int glow_of(const Json* j, int fallback)
{
    if (!j || j->type != Json::String)
        return fallback;
    return j->string == "drive" ? GLOW_DRIVE : j->string == "suppress" ? GLOW_SUPPRESS : GLOW_LEAVE;
}
std::string string_of(const Json* j, const std::string& fallback = "")
{
    return j && j->type == Json::String ? j->string : fallback;
}
void set_param_value(std::vector<std::pair<std::string, Value>>& params, const std::string& key, const Value& v)
{
    for (auto& p : params)
        if (p.first == key) {
            p.second = v;
            return;
        }
    params.emplace_back(key, v);
}
/* A class's or a type's fields onto t (own: the type's). */
void apply_fields(Type& t, const Json& j, bool own)
{
    auto mark = [&](const std::string& field) {
        if (own)
            t.own.push_back(field);
    };
    if (const Json* p = j.get("prefab"); p && p->type == Json::String)
        t.prefab = p->string, mark("prefab");
    if (const Json* g = j.get("glow"))
        t.glow = glow_of(g, t.glow), mark("glow");
    if (const Json* g = j.get("glow_shape"); g && g->type == Json::String)
        t.glow_shape = g->string == "sphere" ? 1 : g->string == "column" ? 2 : 0, mark("glow_shape");
    if (const Json* e = j.get("enabled"); e && e->type == Json::Bool)
        t.enabled = e->boolean, mark("enabled");
    if (const Json* h = j.get("haze"); h && h->type == Json::Bool)
        t.haze = h->boolean;
    if (const Json* r = j.get("fixture_radius"); r && r->type == Json::Number)
        t.fixture_radius = (float)r->number, mark("fixture_radius");
    if (const Json* o = j.get("offset"); o && o->type == Json::Array && o->items.size() == 3) {
        for (int k = 0; k < 3; k++)
            t.offset[k] = (float)o->items[k].number;
        mark("offset");
    }
    if (const Json* ps = j.get("params"); ps && ps->type == Json::Object)
        for (const auto& m : ps->members) {
            Value v;
            if (value_of(m.second, v)) {
                set_param_value(t.params, m.first, v);
                mark("params." + m.first);
            }
        }
}
void resolve()
{
    Catalog& c = the_catalog;
    c.types.clear();
    const Json* classes = c.json.get("classes");
    const Json* types = c.json.get("types");
    if (!types || types->type != Json::Array) {
        if (c.error.empty())
            c.error = "no \"types\" list";
        return;
    }
    for (const Json& j : types->items) {
        Type t;
        t.id = string_of(j.get("id"));
        t.cls = string_of(j.get("class"));
        t.review = string_of(j.get("review"));
        t.confidence = string_of(j.get("confidence"));
        if (const Json* n = j.get("instances"); n && n->type == Json::Number)
            t.instances = (int)n->number;
        if (const Json* n = j.get("rooms"); n && n->type == Json::Number)
            t.rooms = (int)n->number;
        if (const Json* f = j.get("flames"); f && f->type == Json::Array)
            t.flames = (int)f->items.size();
        t.prefab = "none";
        if (const Json* cl = classes ? classes->get(t.cls.c_str()) : nullptr) {
            t.family = string_of(cl->get("family"));
            t.description = string_of(cl->get("description"));
            apply_fields(t, *cl, false);
        }
        apply_fields(t, j, true);
        c.types.push_back(t);
    }
}
void load()
{
    Catalog& c = the_catalog;
    c.path = catalog_path();
    c.error.clear();
    c.json = Json();
    c.stamp = file_stamp(c.path);
    std::string text, error;
    if (!read_text(c.path, text))
        c.error = "cannot read " + c.path;
    else if (!prefab::json_parse(text.c_str(), c.json, &error))
        c.error = c.path + ": " + error;
    resolve();
    c.dirty = false;
    ++c.serial;
    loaded = true;
    fprintf(stderr, "[fixtures] %s: %zu types%s%s\n", c.path.c_str(), c.types.size(), c.error.empty() ? "" : "; ",
        c.error.c_str());
}
Catalog& get()
{
    if (!loaded)
        load();
    return the_catalog;
}
Json* type_json(const std::string& id)
{
    Json* types = get().json.get("types");
    if (!types)
        return nullptr;
    for (Json& j : types->items)
        if (const Json* i = j.get("id"); i && i->type == Json::String && i->string == id)
            return &j;
    return nullptr;
}
bool edited()
{
    resolve();
    the_catalog.dirty = true;
    ++the_catalog.serial;
    return true;
}

/* ---- the prefab side's hooks ---- */
bool hook_param(const std::string& type, const std::string& key, Value& out)
{
    const Type* t = find(type);
    if (!t)
        return false;
    if (const Value* v = t->param(key)) {
        out = *v;
        return true;
    }
    if (key == "glow_mode") { /* the type's glow treatment (the prefab's enum: Leave, Drive, Suppress) */
        out = Value::scalar((float)t->glow);
        return true;
    }
    return false;
}
bool hook_offset(const std::string& type, int, float out[3])
{
    const Type* t = find(type);
    if (!t || (!t->offset[0] && !t->offset[1] && !t->offset[2]))
        return false;
    memcpy(out, t->offset, sizeof t->offset);
    return true;
}
bool hook_radius(const std::string& type, float* out)
{
    const Type* t = find(type);
    if (!t)
        return false;
    *out = t->fixture_radius;
    return true;
}
struct Install {
    Install()
    {
        prefab::type_hooks().param = hook_param;
        prefab::type_hooks().offset = hook_offset;
        prefab::type_hooks().fixture_radius = hook_radius;
    }
} install;
} // namespace

bool Type::owns(const std::string& field) const
{
    for (const std::string& f : own)
        if (f == field)
            return true;
    return false;
}
const Value* Type::param(const std::string& key) const
{
    for (const auto& p : params)
        if (p.first == key)
            return &p.second;
    return nullptr;
}

const Catalog& catalog() { return get(); }

const Type* find(const std::string& id)
{
    if (id.empty())
        return nullptr;
    for (const Type& t : get().types)
        if (t.id == id)
            return &t;
    return nullptr;
}

bool poll()
{
    Catalog& c = get();
    long long now = file_stamp(catalog_path());
    if (now == c.stamp)
        return false;
    if (c.dirty) { /* the file changed under unsaved edits: keep ours (Save or Revert) */
        c.stamp = now;
        fprintf(stderr, "[fixtures] %s changed on disk; unsaved type edits kept (save or revert them)\n", c.path.c_str());
        return false;
    }
    load();
    return true;
}

bool set_param(const std::string& type, const std::string& key, const Value* value)
{
    Json* j = type_json(type);
    if (!j)
        return false;
    Json* params = j->get("params");
    if (value) {
        if (!params)
            params = &j->set("params", Json::object());
        params->set(key.c_str(), json_of(*value));
    } else if (params) {
        params->remove(key.c_str());
        if (params->members.empty())
            j->remove("params");
    }
    return edited();
}
bool set_offset(const std::string& type, const float* offset)
{
    Json* j = type_json(type);
    if (!j)
        return false;
    if (offset)
        j->set("offset", json_of(Value::vec(offset[0], offset[1], offset[2])));
    else
        j->remove("offset");
    return edited();
}
bool set_fixture_radius(const std::string& type, const float* radius)
{
    Json* j = type_json(type);
    if (!j)
        return false;
    if (radius)
        j->set("fixture_radius", Json::num(roundf(*radius * 10) / 10));
    else
        j->remove("fixture_radius");
    return edited();
}
bool set_glow(const std::string& type, const int* mode)
{
    Json* j = type_json(type);
    if (!j)
        return false;
    if (mode)
        j->set("glow", Json::str(glow_name(*mode)));
    else
        j->remove("glow");
    return edited();
}
bool set_enabled(const std::string& type, const bool* enabled)
{
    Json* j = type_json(type);
    if (!j)
        return false;
    if (enabled)
        j->set("enabled", Json::boolean_(*enabled));
    else
        j->remove("enabled");
    return edited();
}
bool set_review(const std::string& type, const std::string& review)
{
    Json* j = type_json(type);
    if (!j)
        return false;
    j->set("review", Json::str(review));
    return edited();
}

bool save(std::string* message)
{
    Catalog& c = get();
    std::string temp = c.path + ".tmp", text = prefab::json_write(c.json) + "\n";
    FILE* f = fopen(temp.c_str(), "w");
    if (!f || fwrite(text.data(), 1, text.size(), f) != text.size()) {
        if (f)
            fclose(f);
        if (message)
            *message = "cannot write " + c.path + ": " + strerror(errno);
        return false;
    }
    fclose(f);
    if (pc_replace_file(temp.c_str(), c.path.c_str())) {
        if (message)
            *message = "cannot write " + c.path + ": " + strerror(errno);
        return false;
    }
    c.stamp = file_stamp(c.path);
    c.dirty = false;
    if (message)
        *message = "Saved " + c.path;
    return true;
}

void revert() { load(); }

const char* glow_name(int mode)
{
    return mode == GLOW_DRIVE ? "drive" : mode == GLOW_SUPPRESS ? "suppress" : "leave";
}

} // namespace fixtures

extern "C" int pc_fixture_glow_mode(const char* type)
{
    const fixtures::Type* t = type ? fixtures::find(type) : nullptr;
    return t ? t->glow : -1;
}
extern "C" int pc_fixture_glow_shape(const char* type)
{
    const fixtures::Type* t = type ? fixtures::find(type) : nullptr;
    return t ? t->glow_shape : 0;
}
