// Node material graphs (material_graph.h; pc/docs/plans/2026-10-30-node-materials-plan.md).
//
// A node definition is a GLSL function: `void node_<name>(in ..., out ...)` with the file's `glsl`
// as its body over the declared names. A graph is nodes (an instance of a definition each, with
// the knob values of its unlinked inputs) and links; the built-in sources (texel, planes, uv,
// normal, vertex_color) read the draw's context, the output node writes the surface inputs. The
// generated include holds every graph as `graph_<name>(GraphIn, <promoted parameters>, inout ...)`
// and `material_graph(slot, ...)`, a switch over the assigned instances' slots that calls each
// instance's master; knob and parameter values are reads of the values table (binding 7, filled per
// frame: a value edit needs no new shader), entry 0 the game time. Everything here is CPU work;
// the draw shaders (native_frag.glsl) call material_graph where a texel's cell carries a slot.
#include "material_graph.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>
#include <unordered_map>

#include <SDL3/SDL.h>

#include "light_prefabs.h" /* prefab::Json */
#include "materials.h"
#include "pc_paths.h"

using prefab::Json;
namespace fs = std::filesystem;
extern "C" void pc_shader_reload_request(void) __attribute__((weak));
extern "C" int pc_shader_reload_available(void) __attribute__((weak));
/* (below) */
extern "C" const float* pc_material_graph_values(int* count); /* stage sources on disk: graphs can show */

namespace material_graph {
namespace {

const char* type_name(Type t) { return t == T_FLOAT ? "float" : t == T_FLOAT2 ? "vec2" : t == T_FLOAT3 ? "vec3" : "vec4"; }
bool type_of(const std::string& s, Type* out)
{
    if (s == "float") *out = T_FLOAT;
    else if (s == "float2") *out = T_FLOAT2;
    else if (s == "float3") *out = T_FLOAT3;
    else if (s == "float4") *out = T_FLOAT4;
    else return false;
    return true;
}
const char* type_json(Type t) { return t == T_FLOAT ? "float" : t == T_FLOAT2 ? "float2" : t == T_FLOAT3 ? "float3" : "float4"; }
bool ident_ok(const std::string& s)
{
    if (s.empty() || isdigit((unsigned char)s[0])) return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_') return false;
    static const char* const KEYWORDS[] = { "in", "out", "inout", "float", "int", "uint", "bool", "vec2", "vec3", "vec4",
        "const", "void", "return", "if", "else", "for", "while", "do", "switch", "case", "default", "break",
        "continue", "discard", "struct", "uniform", "layout", "sampler2D", "main", "true", "false" };
    for (const char* k : KEYWORDS)
        if (s == k) return false;
    return true;
}
std::string lit(float v)
{
    char b[48];
    if (!std::isfinite(v)) v = 0;
    /* The shortest decimal that reads back as the same float. */
    for (int digits = 6; digits <= 9; digits++) {
        snprintf(b, sizeof b, "%.*g", digits, (double)v);
        if (strtof(b, nullptr) == v || digits == 9) break;
    }
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}
std::string lit_vec(const std::vector<float>& v, Type t)
{
    if (t == T_FLOAT) return lit(v.empty() ? 0.0f : v[0]);
    std::string s = std::string(type_name(t)) + "(";
    for (int i = 0; i < (int)t; i++) s += (i ? ", " : "") + lit(i < (int)v.size() ? v[i] : v.empty() ? 0.0f : v[0]);
    return s + ")";
}
std::string read_text(const std::string& path)
{
    std::string text;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f))) text.append(buf, n);
    fclose(f);
    return text;
}
bool write_text(const std::string& path, const std::string& text, std::string* error)
{
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f || fwrite(text.data(), 1, text.size(), f) != text.size() || fclose(f)) {
        if (f) fclose(f);
        if (error) *error = "cannot write " + path;
        return false;
    }
    if (rename(tmp.c_str(), path.c_str())) {
        if (error) *error = "cannot replace " + path;
        return false;
    }
    return true;
}
long long stamp(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st)) return 0;
    return (long long)st.st_mtime * 1000003 + st.st_size;
}
long long dir_stamp(const std::string& dir)
{
    long long s = stamp(dir);
    std::error_code ec;
    for (const auto& ent : fs::directory_iterator(dir, ec))
        if (ent.path().extension() == ".json") s = s * 1000003 + stamp(ent.path().string());
    return s;
}
std::vector<float> floats_of(const Json* v, int n)
{
    std::vector<float> out;
    if (!v) return out;
    if (v->type == Json::Number) out.assign((size_t)n, (float)v->number);
    else if (v->type == Json::Bool) out.assign((size_t)n, v->boolean ? 1.0f : 0.0f);
    else if (v->type == Json::Array)
        for (const Json& item : v->items) out.push_back(item.type == Json::Number ? (float)item.number : 0.0f);
    if (n <= 0) return out; /* (as many as written: a graph file's knob, sized by its port when used) */
    while ((int)out.size() < n) out.push_back(out.empty() ? 0.0f : out.back());
    out.resize((size_t)n);
    return out;
}
Json json_floats(const std::vector<float>& v)
{
    if (v.size() == 1) return Json::num(v[0]);
    Json a = Json::array();
    for (float f : v) a.items.push_back(Json::num(f));
    return a;
}

/* ---- the node library ---- */
Port port(const char* name, Type t, const char* label = nullptr)
{
    Port p;
    p.name = name, p.label = label ? label : name, p.type = t, p.knob = false;
    return p;
}
std::vector<NodeDef> builtins()
{
    std::vector<NodeDef> out;
    NodeDef texel;
    texel.name = "texel", texel.label = "Game texel", texel.category = "Input", texel.builtin = true;
    texel.outputs = { port("color", T_FLOAT3, "Colour") };
    out.push_back(texel);
    NodeDef planes;
    planes.name = "planes", planes.label = "Surface planes", planes.category = "Input", planes.builtin = true;
    planes.outputs = { port("roughness", T_FLOAT, "Roughness"), port("specular", T_FLOAT, "Specular"),
        port("metal", T_FLOAT, "Metal"), port("surface", T_FLOAT4, "Surface (r, s, m, has)"),
        port("relief", T_FLOAT4, "Relief") };
    out.push_back(planes);
    NodeDef uv;
    uv.name = "uv", uv.label = "UV", uv.category = "Input", uv.builtin = true;
    Port snap = port("snap", T_FLOAT, "Snap to texel");
    snap.def = { 1.0f }, snap.pin = true, snap.knob = true;
    uv.inputs = { snap };
    uv.outputs = { port("uv", T_FLOAT2, "UV (texels)") };
    out.push_back(uv);
    NodeDef normal;
    normal.name = "normal", normal.label = "Normal", normal.category = "Input", normal.builtin = true;
    normal.outputs = { port("normal", T_FLOAT3, "Normal (view)") };
    out.push_back(normal);
    NodeDef vcolor;
    vcolor.name = "vertex_color", vcolor.label = "Vertex colour", vcolor.category = "Input", vcolor.builtin = true;
    vcolor.outputs = { port("color", T_FLOAT3, "Colour") };
    out.push_back(vcolor);
    NodeDef time;
    time.name = "time", time.label = "Time", time.category = "Input", time.builtin = true;
    Port loop = port("loop", T_FLOAT, "Loop (seconds)");
    loop.def = { 60.0f }, loop.lo = 1, loop.hi = 3600, loop.pin = true, loop.knob = true;
    time.inputs = { loop };
    time.outputs = { port("seconds", T_FLOAT, "Seconds"), port("phase", T_FLOAT, "Phase (0..1)") };
    out.push_back(time);
    NodeDef output;
    output.name = "output", output.label = "Surface output", output.category = "Output", output.builtin = true;
    for (auto [n, t, l] : std::initializer_list<std::tuple<const char*, Type, const char*>> {
             { "base", T_FLOAT3, "Base colour" }, { "roughness", T_FLOAT, "Roughness" }, { "specular", T_FLOAT, "Specular" },
             { "metal", T_FLOAT, "Metal" }, { "glow", T_FLOAT3, "Glow (emissive)" }, { "surface", T_FLOAT4, "Surface (r, s, m, has)" },
             { "relief", T_FLOAT4, "Relief" } }) {
        Port p = port(n, t, l);
        p.optional = true;
        output.inputs.push_back(p);
    }
    out.push_back(output);
    return out;
}
bool read_node(const std::string& path, NodeDef& d, std::string* error)
{
    Json j;
    std::string text = read_text(path);
    if (text.empty() || !prefab::json_parse(text.c_str(), j, error) || j.type != Json::Object) {
        if (error && error->empty()) *error = "not a JSON object";
        return false;
    }
    const Json* name = j.get("name");
    if (!name || name->type != Json::String || !ident_ok(name->string)) {
        if (error) *error = "needs a name (letters, digits, _; not a GLSL keyword)";
        return false;
    }
    d = NodeDef();
    d.name = name->string, d.path = path;
    if (const Json* v = j.get("label"); v && v->type == Json::String) d.label = v->string;
    if (const Json* v = j.get("category"); v && v->type == Json::String) d.category = v->string;
    if (const Json* v = j.get("glsl"); v && v->type == Json::String) d.glsl = v->string;
    if (d.label.empty()) d.label = d.name;
    if (d.category.empty()) d.category = "Custom";
    for (int side = 0; side < 2; side++) {
        const Json* list = j.get(side ? "outputs" : "inputs");
        if (!list || list->type != Json::Array) continue;
        for (const Json& item : list->items) {
            if (item.type != Json::Object) continue;
            Port p;
            const Json* n = item.get("name");
            if (!n || n->type != Json::String || !ident_ok(n->string)) {
                if (error) *error = "a port needs a name (letters, digits, _; not a GLSL keyword)";
                return false;
            }
            p.name = n->string;
            const Json* t = item.get("type");
            if (!t || t->type != Json::String || !type_of(t->string, &p.type)) {
                if (error) *error = "port " + p.name + ": type float, float2, float3 or float4";
                return false;
            }
            if (const Json* v = item.get("label"); v && v->type == Json::String) p.label = v->string;
            if (p.label.empty()) p.label = p.name;
            if (!side) {
                p.def = floats_of(item.get("default"), (int)p.type);
                if (const Json* v = item.get("min"); v && v->type == Json::Number) p.lo = (float)v->number;
                if (const Json* v = item.get("max"); v && v->type == Json::Number) p.hi = (float)v->number;
                if (const Json* v = item.get("pin"); v && v->type == Json::Bool) p.pin = v->boolean;
                if (const Json* v = item.get("color"); v && v->type == Json::Bool) p.color = v->boolean;
                if (const Json* v = item.get("time"); v && v->type == Json::Bool) p.time = v->boolean;
                p.knob = !p.time; /* a time input: linked, or the game time; never a knob */
                d.inputs.push_back(p);
            } else {
                p.knob = false;
                d.outputs.push_back(p);
            }
        }
    }
    if (d.outputs.empty()) {
        if (error) *error = "needs an output";
        return false;
    }
    if (d.glsl.empty()) {
        if (error) *error = "needs glsl (the function's body)";
        return false;
    }
    return true;
}
std::vector<NodeDef> node_cache;
std::vector<std::string> node_problems;
long long nodes_stamp = -1;
const std::vector<NodeDef>& current_nodes()
{
    long long st = dir_stamp(nodes_dir()) * 7 + dir_stamp(user_nodes_dir());
    if (st == nodes_stamp) return node_cache;
    nodes_stamp = st;
    node_cache = builtins();
    node_problems.clear();
    for (int layer = 0; layer < 2; layer++) {
        std::string dir = layer ? user_nodes_dir() : nodes_dir();
        std::vector<std::string> files;
        std::error_code ec;
        for (const auto& ent : fs::directory_iterator(dir, ec))
            if (ent.path().extension() == ".json") files.push_back(ent.path().string());
        std::sort(files.begin(), files.end());
        for (const std::string& path : files) {
            NodeDef d;
            std::string error;
            if (!read_node(path, d, &error)) {
                node_problems.push_back(path + ": " + error);
                continue;
            }
            d.user = layer == 1;
            auto it = std::find_if(node_cache.begin(), node_cache.end(), [&](const NodeDef& n) { return n.name == d.name; });
            if (it != node_cache.end()) {
                if (it->builtin) {
                    node_problems.push_back(path + ": " + d.name + " is a built-in node");
                    continue;
                }
                *it = d;
            } else
                node_cache.push_back(d);
        }
    }
    return node_cache;
}

/* ---- graphs ---- */
std::map<std::string, Graph> graph_cache;
long long graphs_stamp = -1;
std::vector<std::string> graph_problems;
bool read_graph(const std::string& path, Graph& g, std::string* error)
{
    Json j;
    std::string text = read_text(path);
    if (text.empty() || !prefab::json_parse(text.c_str(), j, error) || j.type != Json::Object) return false;
    const Json* name = j.get("name");
    const Json* gj = j.get("graph");
    if (!name || name->type != Json::String || !gj || gj->type != Json::Object) return false;
    g = Graph();
    g.name = name->string, g.path = path;
    if (const Json* nodes = gj->get("nodes"); nodes && nodes->type == Json::Array)
        for (const Json& item : nodes->items) {
            if (item.type != Json::Object) continue;
            Node n;
            const Json* id = item.get("id");
            const Json* type = item.get("type");
            if (!id || id->type != Json::String || !type || type->type != Json::String) continue;
            n.id = id->string, n.type = type->string;
            if (const Json* pos = item.get("pos"); pos && pos->type == Json::Array && pos->items.size() == 2)
                n.x = (float)pos->items[0].number, n.y = (float)pos->items[1].number;
            if (const Json* knobs = item.get("knobs"); knobs && knobs->type == Json::Object)
                for (const auto& m : knobs->members) {
                    std::vector<float> v = floats_of(&m.second, 0);
                    if (m.second.type == Json::Number || m.second.type == Json::Bool) v = floats_of(&m.second, 1);
                    n.knobs[m.first] = v;
                }
            if (const Json* pinned = item.get("pinned"); pinned && pinned->type == Json::Array) {
                n.pinned_set = true;
                for (const Json& p : pinned->items)
                    if (p.type == Json::String) n.pinned.push_back(p.string);
            }
            g.nodes.push_back(n);
        }
    if (const Json* links = gj->get("links"); links && links->type == Json::Array)
        for (const Json& item : links->items) {
            if (item.type != Json::Object) continue;
            const Json* from = item.get("from");
            const Json* to = item.get("to");
            if (!from || from->type != Json::String || !to || to->type != Json::String) continue;
            size_t a = from->string.find('.'), b = to->string.find('.');
            if (a == std::string::npos || b == std::string::npos) continue;
            Link l;
            l.from_node = from->string.substr(0, a), l.from_port = from->string.substr(a + 1);
            l.to_node = to->string.substr(0, b), l.to_port = to->string.substr(b + 1);
            g.links.push_back(l);
        }
    if (const Json* promoted = gj->get("promoted"); promoted && promoted->type == Json::Object)
        for (const auto& m : promoted->members)
            if (m.second.type == Json::String) g.promoted[m.first] = m.second.string;
    return true;
}
std::map<std::string, Graph>& current_graphs()
{
    long long st = dir_stamp(materials::masters_dir()) * 7 + dir_stamp(materials::graphs_dir());
    if (st == graphs_stamp) return graph_cache;
    graphs_stamp = st;
    graph_problems.clear();
    std::map<std::string, Graph> fresh;
    for (int layer = 0; layer < 2; layer++) {
        std::string dir = layer ? materials::graphs_dir() : materials::masters_dir();
        std::vector<std::string> files;
        std::error_code ec;
        for (const auto& ent : fs::directory_iterator(dir, ec))
            if (ent.path().extension() == ".json") files.push_back(ent.path().string());
        std::sort(files.begin(), files.end());
        for (const std::string& path : files) {
            Graph g;
            std::string error;
            if (!read_graph(path, g, &error)) continue; /* (a plain master, or not one: materials.cpp reports) */
            g.user = layer == 1;
            fresh[g.name] = g;
        }
    }
    /* Unsaved edits survive the files changing under them (the editor's own graph). */
    for (auto& [name, g] : graph_cache)
        if (g.dirty) fresh[name] = g;
    graph_cache.swap(fresh);
    return graph_cache;
}
Json graph_json(const Graph& g)
{
    Json gj = Json::object();
    Json nodes = Json::array();
    for (const Node& n : g.nodes) {
        Json nj = Json::object();
        nj.set("id", Json::str(n.id));
        nj.set("type", Json::str(n.type));
        Json pos = Json::array();
        pos.items.push_back(Json::num(n.x)), pos.items.push_back(Json::num(n.y));
        nj.set("pos", pos);
        if (!n.knobs.empty()) {
            Json knobs = Json::object();
            for (const auto& k : n.knobs) knobs.set(k.first.c_str(), json_floats(k.second));
            nj.set("knobs", knobs);
        }
        if (n.pinned_set) {
            Json pinned = Json::array();
            for (const std::string& p : n.pinned) pinned.items.push_back(Json::str(p));
            nj.set("pinned", pinned);
        }
        nodes.items.push_back(nj);
    }
    gj.set("nodes", nodes);
    Json links = Json::array();
    for (const Link& l : g.links) {
        Json lj = Json::object();
        lj.set("from", Json::str(l.from_node + "." + l.from_port));
        lj.set("to", Json::str(l.to_node + "." + l.to_port));
        links.items.push_back(lj);
    }
    gj.set("links", links);
    Json promoted = Json::object();
    for (const auto& p : g.promoted) promoted.set(p.first.c_str(), Json::str(p.second));
    gj.set("promoted", promoted);
    return gj;
}
/* The master's parameters a graph exposes: its promoted knobs, in the order of
 * the promoted map (by "node.input"), as part 3's reader wants them. */
struct ParamInfo {
    std::string name, node, input, label;
    float def = 0, lo = 0, hi = 1;
    Type type = T_FLOAT;      /* T_FLOAT, or T_FLOAT3 (a colour knob: a colour parameter) */
    std::vector<float> def3;  /* a colour's default */
};
std::vector<ParamInfo> params_of(const Graph& g, std::string* error, std::string* error_node)
{
    std::vector<ParamInfo> out;
    std::set<std::string> names;
    for (const auto& [key, pname] : g.promoted) {
        size_t dot = key.find('.');
        ParamInfo p;
        p.name = pname, p.node = key.substr(0, dot), p.input = dot == std::string::npos ? "" : key.substr(dot + 1);
        const Node* n = g.node(p.node);
        const NodeDef* d = n ? node_def(n->type) : nullptr;
        const Port* port = d ? d->input(p.input) : nullptr;
        if (!n || !d || !port || !port->knob) {
            if (error) *error = "promoted " + key + ": no such knob";
            if (error_node) *error_node = p.node;
            return out;
        }
        if (port->type != T_FLOAT && !(port->type == T_FLOAT3 && port->color)) {
            if (error) *error = "promoted " + key + ": only number and colour knobs can be parameters";
            if (error_node) *error_node = p.node;
            return out;
        }
        if (!ident_ok(pname) || !names.insert(pname).second) {
            if (error) *error = "parameter " + pname + ": a name used twice, or not a name";
            if (error_node) *error_node = p.node;
            return out;
        }
        auto k = n->knobs.find(p.input);
        p.def = k != n->knobs.end() && !k->second.empty() ? k->second[0] : port->def.empty() ? 0.0f : port->def[0];
        p.lo = port->lo, p.hi = port->hi;
        p.type = port->type;
        if (p.type == T_FLOAT3) {
            p.def3 = k != n->knobs.end() ? k->second : port->def;
            p.def3.resize(3, p.def3.empty() ? 1.0f : p.def3.back());
            p.lo = 0, p.hi = 1;
        }
        p.label = pname;
        out.push_back(p);
    }
    return out;
}
Json master_json(const Graph& g)
{
    Json j = Json::object();
    j.set("format", Json::num(1));
    j.set("name", Json::str(g.name));
    j.set("about", Json::str("A node material graph (pc/docs/plans/2026-10-30-node-materials-plan.md): the parameters are its promoted knobs; edit it in Window > Material graph"));
    j.set("shader", Json::str("graph:" + g.name));
    Json params = Json::array();
    /* The standard five first: they feed the planes (the Surface planes node reads them), so an
     * instance of a graph master has them like any other; then the promoted knobs. */
    if (const materials::Master* std_master = materials::master("standard_surface"))
        for (const materials::MasterParam& p : std_master->params) {
            Json pj = Json::object();
            pj.set("name", Json::str(p.name));
            pj.set("type", Json::str(p.is_bool ? "bool" : p.is_color ? "color" : "number"));
            pj.set("default", p.is_bool ? Json::boolean_(p.def >= 0.5f)
                    : p.is_color ? json_floats(std::vector<float>(p.color, p.color + 3)) : Json::num(p.def));
            if (!p.is_bool) pj.set("min", Json::num(p.lo)), pj.set("max", Json::num(p.hi));
            pj.set("label", Json::str(p.label));
            pj.set("group", Json::str(p.group));
            if (!p.tip.empty()) pj.set("tip", Json::str(p.tip));
            params.items.push_back(pj);
        }
    for (const ParamInfo& p : params_of(g, nullptr, nullptr)) {
        Json pj = Json::object();
        pj.set("name", Json::str(p.name));
        pj.set("type", Json::str(p.type == T_FLOAT3 ? "color" : "number"));
        pj.set("default", p.type == T_FLOAT3 ? json_floats(p.def3) : Json::num(p.def));
        pj.set("min", Json::num(p.lo));
        pj.set("max", Json::num(p.hi));
        pj.set("label", Json::str(p.label));
        pj.set("group", Json::str("Graph"));
        params.items.push_back(pj);
    }
    j.set("parameters", params);
    Json slots = Json::array();
    Json texel = Json::object();
    texel.set("name", Json::str("texel")), texel.set("label", Json::str("Game texel"));
    slots.items.push_back(texel);
    j.set("slots", slots);
    j.set("graph", graph_json(g));
    return j;
}

/* ---- generation ---- */
std::string var(const std::string& node, const std::string& port) { return "n_" + node + "_" + port; }
/* The body of one graph as GLSL statements over `gi` and the inout results;
 * `params` the promoted knobs (read as p_<name>). */
/* `knob`: an unlinked, unpromoted knob's expression (the include: a values table read); none: its literal. */
using KnobExpr = std::function<std::string(const Node&, const Port&, const std::vector<float>& value)>;
bool emit_graph(const Graph& g, const std::vector<ParamInfo>& params, std::string& out, std::string* error, std::string* error_node,
    const KnobExpr* knob = nullptr)
{
    auto fail = [&](const std::string& node, const std::string& m) {
        if (error) *error = m;
        if (error_node) *error_node = node;
        return false;
    };
    std::set<std::string> ids;
    const Node* output = nullptr;
    for (const Node& n : g.nodes) {
        if (!ident_ok(n.id)) return fail(n.id, "node id " + n.id + ": letters, digits and _ only");
        if (!ids.insert(n.id).second) return fail(n.id, "node id " + n.id + " used twice");
        const NodeDef* d = node_def(n.type);
        if (!d) return fail(n.id, n.id + ": unknown node type " + n.type);
        if (n.type == "output") {
            if (output) return fail(n.id, "two output nodes");
            output = &n;
        }
    }
    /* Links: one per input, ports known, types matching (float broadcasts). */
    std::map<std::string, const Link*> into; /* "node.input" -> the link */
    std::map<std::string, std::vector<std::string>> deps;
    for (const Link& l : g.links) {
        const Node* a = g.node(l.from_node);
        const Node* b = g.node(l.to_node);
        if (!a) return fail(l.to_node, "link from unknown node " + l.from_node);
        if (!b) return fail(l.from_node, "link to unknown node " + l.to_node);
        const NodeDef* da = node_def(a->type);
        const NodeDef* db = node_def(b->type);
        const Port* pa = da->output(l.from_port);
        const Port* pb = db->input(l.to_port);
        if (!pa) return fail(l.from_node, l.from_node + " has no output " + l.from_port);
        if (!pb) return fail(l.to_node, l.to_node + " has no input " + l.to_port);
        if (pa->type != pb->type && pa->type != T_FLOAT)
            return fail(l.to_node, l.to_node + "." + l.to_port + " wants " + type_json(pb->type) + ", " + l.from_node + "."
                    + l.from_port + " is " + type_json(pa->type));
        std::string key = l.to_node + "." + l.to_port;
        if (into.count(key)) return fail(l.to_node, key + " has two links");
        into[key] = &l;
        deps[l.to_node].push_back(l.from_node);
    }
    /* Topological order (a cycle names the node). */
    std::map<std::string, int> state;
    std::vector<const Node*> order;
    std::function<bool(const Node*)> visit = [&](const Node* n) {
        int& s = state[n->id];
        if (s == 2) return true;
        if (s == 1) return fail(n->id, "a cycle through " + n->id);
        s = 1;
        for (const std::string& d : deps[n->id])
            if (!visit(g.node(d))) return false;
        s = 2;
        order.push_back(n);
        return true;
    };
    for (const Node& n : g.nodes)
        if (!visit(&n)) return false;
    auto source = [&](const Node& n, const Port& p) -> std::string {
        /* An input's expression: the link's variable (broadcast), a promoted parameter, the knob, the default. */
        std::string key = n.id + "." + p.name;
        if (auto it = into.find(key); it != into.end()) {
            const Node* a = g.node(it->second->from_node);
            const Port* pa = node_def(a->type)->output(it->second->from_port);
            std::string v = var(a->id, pa->name);
            return pa->type == p.type ? v : std::string(type_name(p.type)) + "(" + v + ")";
        }
        for (const ParamInfo& pi : params)
            if (pi.node == n.id && pi.input == p.name) return "p_" + pi.name;
        if (p.time) return "graph_values[0].x"; /* unlinked: the game time */
        auto k = n.knobs.find(p.name);
        const std::vector<float>& value = k != n.knobs.end() ? k->second : p.def;
        return knob ? (*knob)(n, p, value) : lit_vec(value, p.type);
    };
    for (const Node* n : order) {
        const NodeDef* d = node_def(n->type);
        if (n->type == "texel") out += "    vec3 " + var(n->id, "color") + " = gi.texel;\n";
        else if (n->type == "vertex_color") out += "    vec3 " + var(n->id, "color") + " = gi.vcolor;\n";
        else if (n->type == "normal") out += "    vec3 " + var(n->id, "normal") + " = gi.normal;\n";
        else if (n->type == "time") {
            /* The game time (the table's entry 0: vsyncs / 60, paused with the game, an hour's loop) in this node's loop. */
            std::string loop = "max(" + source(*n, d->inputs[0]) + ", 0.001)";
            out += "    float " + var(n->id, "seconds") + " = mod(graph_values[0].x, " + loop + ");\n";
            out += "    float " + var(n->id, "phase") + " = " + var(n->id, "seconds") + " / " + loop + ";\n";
        }
        else if (n->type == "uv")
            out += "    vec2 " + var(n->id, "uv") + " = " + source(*n, d->inputs[0]) + " >= 0.5 ? floor(gi.uv) + 0.5 : gi.uv;\n";
        else if (n->type == "planes") {
            out += "    float " + var(n->id, "roughness") + " = gi.planes.r, " + var(n->id, "specular") + " = gi.planes.g, "
                + var(n->id, "metal") + " = gi.planes.b;\n";
            out += "    vec4 " + var(n->id, "surface") + " = gi.planes, " + var(n->id, "relief") + " = gi.relief;\n";
        } else if (n->type == "output") {
            for (const Port& p : d->inputs) {
                if (!into.count(n->id + "." + p.name)) continue;
                std::string v = source(*n, p);
                if (p.name == "base") out += "    base = " + v + "; set |= 1;\n";
                else if (p.name == "glow") out += "    glow = " + v + "; set |= 2;\n";
                else if (p.name == "surface") out += "    material = " + v + ";\n";
                else if (p.name == "relief") out += "    relief = " + v + ";\n";
                else if (p.name == "roughness") out += "    material.r = " + v + "; material.a = 1.0;\n";
                else if (p.name == "specular") out += "    material.g = " + v + "; material.a = 1.0;\n";
                else if (p.name == "metal") out += "    material.b = " + v + "; material.a = 1.0;\n";
            }
        } else {
            std::string call = "    ";
            for (const Port& p : d->outputs) call += std::string(type_name(p.type)) + " " + var(n->id, p.name) + "; ";
            call += "node_" + d->name + "(";
            bool first = true;
            for (const Port& p : d->inputs) call += (first ? "" : ", ") + source(*n, p), first = false;
            for (const Port& p : d->outputs) call += (first ? "" : ", ") + var(n->id, p.name), first = false;
            out += call + ");\n";
        }
    }
    if (!output) return fail("", "no output node");
    return true;
}
std::string node_function(const NodeDef& d)
{
    std::string s = "void node_" + d.name + "(";
    bool first = true;
    for (const Port& p : d.inputs) s += (first ? "" : ", ") + std::string(type_name(p.type)) + " " + p.name, first = false;
    for (const Port& p : d.outputs) s += (first ? "" : ", out ") + std::string(first ? "out " : "") + type_name(p.type) + " " + p.name, first = false;
    s += ")\n{\n    " + d.glsl + "\n}\n";
    return s;
}
std::string graph_signature(const Graph& g, const std::vector<ParamInfo>& params)
{
    std::string s = "void graph_" + g.name + "(GraphIn gi";
    for (const ParamInfo& p : params) s += std::string(", ") + type_name(p.type) + " p_" + p.name;
    return s + ", inout vec4 material, inout vec4 relief, inout vec3 base, inout vec3 glow, inout int set)";
}
const char* HEADER = "#ifndef ROOD_MATERIAL_GRAPH_GLSL\n#define ROOD_MATERIAL_GRAPH_GLSL\n"
                     "// Generated by the game (pc/src/material_graph.cpp) from the node material graphs: do not edit.\n"
                     "// The shipped pc/shaders/vulkan/material_graph.glsl is the stub this file shadows.\n"
                     "struct GraphIn { vec3 texel; vec3 vcolor; vec2 uv; vec3 normal; vec4 planes; vec4 relief; };\n"
                     "// The values table (vk_render.inc binding 7): [0].x the game time in seconds, then the knobs and\n"
                     "// the instances' parameters at the offsets below; a value edit rewrites the table, not this file.\n"
                     "layout(std430, binding = 7) readonly buffer MaterialGraphValues { vec4 graph_values[]; };\n";
const char* SWITCH_HEAD = "void material_graph(uint slot, GraphIn gi, inout vec4 material, inout vec4 relief, inout vec3 base, "
                          "inout vec3 glow, inout int set)\n{\n    switch (slot) {\n";
const char* FOOTER = "    default: break;\n    }\n}\n#endif\n";

/* ---- the slots and the generated file ---- */
struct Slot {
    std::string instance, master;
};
std::vector<Slot> slots;                      /* index + 1 = the slot */
std::unordered_map<std::string, int> slot_of; /* instance id -> slot */
uint32_t slots_for = 0;                       /* the catalog generation the slots are of */
std::string last_written, last_error, last_error_node;
int generation_count = 0;
bool generated_dirty = true;
unsigned dirty_serial = 1; /* bumped by every edit: the values table refills when it moved */
void mark()
{
    generated_dirty = true;
    ++dirty_serial;
}

/* ---- the values table (binding 7 of the native draws: graph_values[]) ----
 * Entry 0: the game time (x: seconds, wrapped at TIME_WRAP); then one vec4 per value the include reads:
 * each used graph's unlinked, unpromoted knobs (by graph, node and input) and each slot's promoted
 * parameters (by instance and parameter). The include indexes it with literal offsets, so a value edit
 * is a table write on the next frame, no regeneration; only a structural change (a link, a node, a
 * promotion, an assignment) rewrites the include. Offsets are handed out once per key and never reused
 * while the table lives (a shader still running the previous include reads its own keys' current
 * values until the reload swaps the new one in); past TABLE_MAX entries it starts over. */
struct ValueRef {
    bool param = false;               /* else a knob */
    std::string graph, node, input;   /* a knob */
    std::string instance, name;       /* a parameter */
    Type type = T_FLOAT;
    std::vector<float> fallback;      /* the definition's default (the node or the parameter gone) */
};
constexpr int TABLE_MAX = 4096;
constexpr double TIME_WRAP = 3600.0; /* seconds: the game time loops each hour (float precision stays ~0.25 ms) */
std::vector<ValueRef> refs = { ValueRef() }; /* [0]: the time */
std::unordered_map<std::string, int> ref_of;
std::vector<float> table_values(4, 0.0f);
unsigned filled_serial = 0;
int ref_index(const std::string& key, const ValueRef& r)
{
    auto it = ref_of.find(key);
    if (it != ref_of.end()) {
        refs[it->second] = r; /* (the node's type may have changed under its id) */
        return it->second;
    }
    refs.push_back(r);
    ref_of[key] = (int)refs.size() - 1;
    table_values.resize(refs.size() * 4, 0.0f);
    filled_serial = 0;
    return (int)refs.size() - 1;
}
const char* swizzle(Type t) { return t == T_FLOAT ? ".x" : t == T_FLOAT2 ? ".xy" : t == T_FLOAT3 ? ".xyz" : ""; }
std::string table_read(int index, Type t) { return "graph_values[" + std::to_string(index) + "]" + swizzle(t); }
uint32_t values_for = 0; /* the catalog generation the values table was refilled for */
void refresh_slots()
{
    uint32_t gen = materials::catalog_structure_generation();
    if (gen == slots_for) {
        /* A value edit (the structure the same): the table refills, the include stays. */
        uint32_t values = materials::catalog_generation();
        if (values != values_for) values_for = values, ++dirty_serial;
        return;
    }
    slots_for = gen;
    values_for = materials::catalog_generation();
    std::vector<Slot> fresh;
    for (const materials::Instance& i : materials::instances()) {
        if (!i.assigned) continue;
        const materials::Master* m = materials::master(i.master);
        if (!m || m->shader.rfind("graph:", 0) != 0) continue;
        fresh.push_back({ i.id, m->shader.substr(6) });
    }
    std::sort(fresh.begin(), fresh.end(), [](const Slot& a, const Slot& b) { return a.instance < b.instance; });
    if (fresh.size() > 255) fresh.resize(255);
    bool same = fresh.size() == slots.size();
    for (size_t k = 0; same && k < fresh.size(); k++) same = fresh[k].instance == slots[k].instance && fresh[k].master == slots[k].master;
    slots.swap(fresh);
    slot_of.clear();
    for (size_t k = 0; k < slots.size(); k++) slot_of[slots[k].instance] = (int)k + 1;
    mark(); /* (values may have changed even when the slots did not) */
    (void)same;
}
} // namespace

const Port* NodeDef::input(const std::string& n) const
{
    for (const Port& p : inputs)
        if (p.name == n) return &p;
    return nullptr;
}
const Port* NodeDef::output(const std::string& n) const
{
    for (const Port& p : outputs)
        if (p.name == n) return &p;
    return nullptr;
}
Node* Graph::node(const std::string& id)
{
    for (Node& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}
const Node* Graph::node(const std::string& id) const
{
    for (const Node& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}
const std::vector<NodeDef>& nodes() { return current_nodes(); }
const NodeDef* node_def(const std::string& name)
{
    for (const NodeDef& d : current_nodes())
        if (d.name == name) return &d;
    return nullptr;
}
std::string nodes_dir()
{
    const char* e = getenv("ROOD_MATERIAL_NODES");
    if (e && *e) return e;
    return fs::path(materials::masters_dir()).parent_path().string() + "/nodes";
}
std::string user_nodes_dir() { return materials::user_dir() + "/nodes"; }
std::vector<std::string> graph_names()
{
    std::vector<std::string> out;
    for (const auto& p : current_graphs()) out.push_back(p.first);
    return out;
}
Graph* graph(const std::string& name)
{
    auto& all = current_graphs();
    auto it = all.find(name);
    return it == all.end() ? nullptr : &it->second;
}
Graph fresh_graph(const std::string& name)
{
    Graph g;
    g.name = name, g.user = true, g.dirty = true;
    Node texel, planes, output;
    texel.id = "texel", texel.type = "texel", texel.x = 40, texel.y = 60;
    planes.id = "planes", planes.type = "planes", planes.x = 40, planes.y = 180;
    output.id = "output", output.type = "output", output.x = 420, output.y = 100;
    g.nodes = { texel, planes, output };
    g.links = { { "planes", "surface", "output", "surface" }, { "planes", "relief", "output", "relief" } };
    return g;
}
namespace {
Generated generate_graph(const Graph& g, const KnobExpr* knob)
{
    Generated r;
    if (!ident_ok(g.name)) {
        r.error = "graph name " + g.name + ": letters, digits and _ only";
        return r;
    }
    std::vector<ParamInfo> params = params_of(g, &r.error, &r.error_node);
    if (!r.error.empty()) return r;
    std::string body;
    if (!emit_graph(g, params, body, &r.error, &r.error_node, knob)) return r;
    for (const ParamInfo& p : params) r.params.push_back(p.name);
    r.glsl = graph_signature(g, params) + "\n{\n" + body + "}\n";
    r.ok = true;
    return r;
}
} // namespace
Generated generate_one(const Graph& g) { return generate_graph(g, nullptr); }
std::string generate_all(std::string* first_error)
{
    refresh_slots();
    if ((int)refs.size() > TABLE_MAX) {
        /* Past the table's size: start over (a shader on the previous include may read stale values until
         * the reload, once, after thousands of structural edits). */
        refs.resize(1), ref_of.clear(), table_values.assign(4, 0.0f);
    }
    std::string text = HEADER;
    std::set<std::string> used_nodes, used_graphs;
    for (const Slot& s : slots) used_graphs.insert(s.master);
    /* Every node definition a used graph calls, then the graphs, then the switch. */
    std::map<std::string, Generated> gens;
    for (const std::string& name : used_graphs) {
        const Graph* g = graph(name);
        if (!g) {
            if (first_error && first_error->empty()) *first_error = "graph " + name + ": no such graph";
            continue;
        }
        /* The knobs as table reads: the graph's own entries (every instance of the master shares them). */
        KnobExpr knob = [&](const Node& n, const Port& p, const std::vector<float>&) {
            ValueRef ref;
            ref.graph = name, ref.node = n.id, ref.input = p.name, ref.type = p.type, ref.fallback = p.def;
            return table_read(ref_index("k:" + name + ":" + n.id + "." + p.name, ref), p.type);
        };
        Generated r = generate_graph(*g, &knob);
        if (!r.ok) {
            if (first_error && first_error->empty()) *first_error = name + ": " + r.error;
            continue;
        }
        for (const Node& n : g->nodes) used_nodes.insert(n.type);
        gens[name] = r;
    }
    for (const NodeDef& d : nodes())
        if (!d.builtin && used_nodes.count(d.name)) text += node_function(d);
    for (const auto& p : gens) text += p.second.glsl;
    text += SWITCH_HEAD;
    for (size_t k = 0; k < slots.size(); k++) {
        auto it = gens.find(slots[k].master);
        if (it == gens.end()) continue;
        const Graph* g = graph(slots[k].master);
        std::vector<ParamInfo> params = params_of(*g, nullptr, nullptr);
        std::string call = "    case " + std::to_string(k + 1) + "u: graph_" + slots[k].master + "(gi";
        for (const ParamInfo& p : params) {
            ValueRef ref;
            ref.param = true, ref.instance = slots[k].instance, ref.name = p.name, ref.type = p.type;
            ref.fallback = p.type == T_FLOAT3 ? p.def3 : std::vector<float> { p.def };
            call += ", " + table_read(ref_index("p:" + slots[k].instance + ":" + p.name, ref), p.type);
        }
        text += call + ", material, relief, base, glow, set); break;\n";
    }
    text += FOOTER;
    return text;
}
bool save_graph(const std::string& name, std::string* error)
{
    Graph* g = graph(name);
    if (!g) {
        if (error) *error = "no such graph: " + name;
        return false;
    }
    Generated r = generate_one(*g);
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    std::string path = materials::graphs_dir() + "/" + name + ".json";
    if (!write_text(path, prefab::json_write(master_json(*g), 2) + "\n", error)) return false;
    g->dirty = false, g->user = true, g->path = path;
    graphs_stamp = -1; /* (read back: the file is the truth) */
    materials::masters_changed();
    return true;
}
bool remove_graph(const std::string& name, std::string* error)
{
    Graph* g = graph(name);
    if (!g) {
        if (error) *error = "no such graph: " + name;
        return false;
    }
    if (!g->user) {
        if (error) *error = name + " is shipped: it cannot be removed";
        return false;
    }
    std::error_code ec;
    fs::remove(g->path, ec);
    g->dirty = false;
    graphs_stamp = -1;
    materials::masters_changed();
    return true;
}
void reload() { nodes_stamp = graphs_stamp = -1, slots_for = 0, mark(); }
std::string generated_dir()
{
    const char* e = getenv("ROOD_SHADERS_GENERATED");
    return e && *e ? e : "pc/build/shaders/generated";
}
std::string generated_path() { return generated_dir() + "/material_graph.glsl"; }

namespace {
/* Regenerates the include when anything it depends on changed; a graph that
 * does not generate keeps the file as it was (the last good shader). */
void regenerate()
{
    std::string error;
    std::string text = generate_all(&error);
    last_error = error;
    if (slots.empty()) {
        /* No graph instance: the stub's effect; the generated file goes (editor closed = game unchanged). */
        if (!last_written.empty() || fs::exists(generated_path())) {
            std::error_code ec;
            fs::remove(generated_path(), ec);
            last_written.clear();
            ++generation_count;
            if (pc_shader_reload_request) pc_shader_reload_request();
        }
        return;
    }
    if (text == last_written && fs::exists(generated_path())) return;
    if (!error.empty() && !last_written.empty()) return; /* keep the last good */
    std::string werr;
    if (write_text(generated_path(), text, &werr)) {
        last_written = text;
        ++generation_count;
        if (pc_shader_reload_request) pc_shader_reload_request();
    } else
        last_error = werr;
}
int fail(const std::string& m, char* out, int size)
{
    snprintf(out, size, "material_graph: %s", m.c_str());
    return -1;
}
int reply(const Json& j, char* out, int size)
{
    std::string text = prefab::json_write(j, 0);
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
    if ((int)text.size() >= size) return fail("reply too large", out, size);
    memcpy(out, text.c_str(), text.size() + 1);
    return 0;
}
std::string str(const Json& j, const char* name, const std::string& def = "")
{
    const Json* v = j.get(name);
    return v && v->type == Json::String ? v->string : def;
}
Json port_json(const Port& p)
{
    Json j = Json::object();
    j.set("name", Json::str(p.name));
    j.set("label", Json::str(p.label));
    j.set("type", Json::str(type_json(p.type)));
    if (p.knob) {
        j.set("default", json_floats(p.def.empty() ? std::vector<float>((size_t)p.type, 0.0f) : p.def));
        j.set("min", Json::num(p.lo)), j.set("max", Json::num(p.hi));
        j.set("pin", Json::boolean_(p.pin));
        j.set("color", Json::boolean_(p.color));
    }
    j.set("time", Json::boolean_(p.time));
    j.set("knob", Json::boolean_(p.knob));
    j.set("optional", Json::boolean_(p.optional));
    return j;
}
Json nodes_json()
{
    Json list = Json::array();
    for (const NodeDef& d : nodes()) {
        Json j = Json::object();
        j.set("name", Json::str(d.name)), j.set("label", Json::str(d.label)), j.set("category", Json::str(d.category));
        j.set("builtin", Json::boolean_(d.builtin)), j.set("user", Json::boolean_(d.user));
        Json ins = Json::array(), outs = Json::array();
        for (const Port& p : d.inputs) ins.items.push_back(port_json(p));
        for (const Port& p : d.outputs) outs.items.push_back(port_json(p));
        j.set("inputs", ins), j.set("outputs", outs);
        list.items.push_back(j);
    }
    return list;
}
Json graph_info(const Graph& g)
{
    Json j = Json::object();
    j.set("name", Json::str(g.name));
    j.set("path", Json::str(g.path));
    j.set("user", Json::boolean_(g.user));
    j.set("dirty", Json::boolean_(g.dirty));
    j.set("graph", graph_json(g));
    Generated r = generate_one(g);
    j.set("ok", Json::boolean_(r.ok));
    j.set("error", Json::str(r.error));
    j.set("error_node", Json::str(r.error_node));
    Json params = Json::array();
    for (const std::string& p : r.params) params.items.push_back(Json::str(p));
    j.set("params", params);
    /* The instances of this master, and their slots. */
    Json inst = Json::array();
    refresh_slots();
    for (const Slot& s : slots)
        if (s.master == g.name) {
            Json i = Json::object();
            i.set("id", Json::str(s.instance));
            i.set("slot", Json::num(slot_of[s.instance]));
            inst.items.push_back(i);
        }
    j.set("instances", inst);
    return j;
}
Json status_json()
{
    Json j = Json::object();
    refresh_slots();
    j.set("generation", Json::num(generation_count));
    j.set("path", Json::str(generated_path()));
    j.set("written", Json::boolean_(!last_written.empty()));
    j.set("error", Json::str(last_error));
    j.set("slots", Json::num((double)slots.size()));
    /* The values table (binding 7): its entries, and the game time it carries now. */
    j.set("values", Json::num((double)refs.size()));
    j.set("time", Json::num(pc_material_graph_values(nullptr)[0]));
    /* Without the shader sources (a shipped build) nothing applies the include: the planes' values show. */
    j.set("hot_reload", Json::boolean_(pc_shader_reload_available && pc_shader_reload_available()));
    Json problems = Json::array();
    for (const std::string& p : node_problems) problems.items.push_back(Json::str(p));
    j.set("node_problems", problems);
    j.set("nodes_dir", Json::str(nodes_dir()));
    j.set("graphs_dir", Json::str(materials::graphs_dir()));
    return j;
}
void touched(Graph& g)
{
    g.dirty = true;
    mark();
}
} // namespace
} // namespace material_graph

using namespace material_graph;

namespace material_graph {
namespace {
/* The table's values from the graphs' knobs and the instances' resolved parameters (when an edit moved
 * dirty_serial); an entry whose node or instance went keeps its last values. */
void fill_values()
{
    if (filled_serial == dirty_serial || refs.size() <= 1) return;
    filled_serial = dirty_serial;
    auto& all = current_graphs();
    std::unordered_map<std::string, std::map<std::string, float>> resolved;
    for (size_t i = 1; i < refs.size(); i++) {
        const ValueRef& r = refs[i];
        std::vector<float> v = r.fallback;
        if (!r.param) {
            auto g = all.find(r.graph);
            const Node* n = g == all.end() ? nullptr : g->second.node(r.node);
            if (!n) continue;
            auto k = n->knobs.find(r.input);
            if (k != n->knobs.end()) v = k->second;
        } else {
            auto it = resolved.find(r.instance);
            if (it == resolved.end()) {
                std::map<std::string, float> values;
                if (!materials::resolve(r.instance, values, nullptr, nullptr)) values.clear();
                it = resolved.emplace(r.instance, std::move(values)).first;
            }
            if (it->second.empty()) continue;
            if (r.type == T_FLOAT3) {
                static const char* const RGB[3] = { ".r", ".g", ".b" };
                v.resize(3, v.empty() ? 1.0f : v.back());
                for (int c = 0; c < 3; c++)
                    if (auto f = it->second.find(r.name + RGB[c]); f != it->second.end()) v[c] = f->second;
            } else if (auto f = it->second.find(r.name); f != it->second.end())
                v = { f->second };
        }
        for (int c = 0; c < 4; c++) table_values[i * 4 + c] = c < (int)v.size() ? v[c] : v.empty() ? 0.0f : v[0];
    }
}
} // namespace
} // namespace material_graph

extern "C" unsigned pc_vsync_count(void) __attribute__((weak)); /* boot_runtime.c: held while paused, M's in a photo render */
extern "C" const float* pc_material_graph_values(int* count)
{
    if (refs.size() > 1) {
        refresh_slots(); /* (an instance's value edited since the last poll: this frame shows it) */
        fill_values();
    }
    /* The game time: vsyncs at 60 a second, so pausing holds it and a photo render (rewound to its moment)
     * draws the same time every time; looped at TIME_WRAP to keep float precision. */
    unsigned v = pc_vsync_count ? pc_vsync_count() : 0;
    table_values[0] = (float)fmod((double)v / 60.0, TIME_WRAP);
    if (count) *count = (int)refs.size();
    return table_values.data();
}
extern "C" int pc_material_graph_slot(const char* instance_id)
{
    refresh_slots();
    auto it = slot_of.find(instance_id ? instance_id : "");
    return it == slot_of.end() ? 0 : it->second;
}
extern "C" void pc_material_graph_poll(void)
{
    refresh_slots();
    /* Files edited outside: the caches notice by stamp; regenerate then too (looked at twice a second:
     * four folders' files stat'ed). */
    static long long seen = -1;
    static Uint64 last_scan;
    Uint64 now = SDL_GetTicks();
    if (now - last_scan >= 500 || generated_dirty) {
        last_scan = now;
        long long st = dir_stamp(nodes_dir()) * 7 + dir_stamp(user_nodes_dir()) + dir_stamp(materials::masters_dir()) * 13
            + dir_stamp(materials::graphs_dir()) * 17;
        if (st != seen) seen = st, mark();
    }
    if (!generated_dirty) return;
    generated_dirty = false;
    regenerate();
}
/* RPC material_graph {action: list (default) | nodes | status | get {name} | new {name} | set {name, graph} |
 * save {name} | remove {name} | generate {name} | node_add {name, id, type, x, y} | node_remove {name, id} |
 * node_move {name, id, x, y} | link {name, from "node.port", to "node.port"} | unlink {name, to} |
 * knob {name, id, input, value} | pin {name, id, input, on} | promote {name, id, input, param (null: unpromote)} |
 * reload}. Edits are live (the include regenerates on the next frame); `save` writes the master file
 * (the user's graph folder); promote / unpromote save as well (they change the master's parameters). */
extern "C" int pc_material_graph_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)) return fail("params: " + error, out, size);
    std::string action = str(params, "action", "list");
    auto need_graph = [&](Graph** g) {
        std::string name = str(params, "name");
        *g = graph(name);
        return *g ? std::string() : "no such graph: " + name;
    };
    if (action == "list") {
        Json j = Json::object();
        Json graphs = Json::array();
        for (const std::string& n : graph_names()) {
            const Graph* g = graph(n);
            Json gj = Json::object();
            gj.set("name", Json::str(n)), gj.set("user", Json::boolean_(g->user)), gj.set("dirty", Json::boolean_(g->dirty));
            Generated r = generate_one(*g);
            gj.set("ok", Json::boolean_(r.ok)), gj.set("error", Json::str(r.error));
            graphs.items.push_back(gj);
        }
        j.set("graphs", graphs);
        j.set("status", status_json());
        return reply(j, out, size);
    }
    if (action == "nodes") {
        Json j = Json::object();
        j.set("nodes", nodes_json());
        return reply(j, out, size);
    }
    if (action == "status") return reply(status_json(), out, size);
    if (action == "values") {
        /* The values table as the next frame uploads it: each entry's key and its vec4. */
        pc_material_graph_values(nullptr);
        Json j = Json::object(), list = Json::array();
        std::vector<std::string> keys(refs.size());
        keys[0] = "time";
        for (const auto& [k, i] : ref_of) keys[(size_t)i] = k;
        for (size_t i = 0; i < refs.size(); i++) {
            Json e = Json::object();
            e.set("key", Json::str(keys[i]));
            e.set("value", json_floats(std::vector<float>(table_values.begin() + (long)i * 4, table_values.begin() + (long)i * 4 + 4)));
            list.items.push_back(e);
        }
        j.set("values", list);
        return reply(j, out, size);
    }
    if (action == "reload") {
        reload();
        materials::reload();
        return reply(status_json(), out, size);
    }
    if (action == "new") {
        std::string name = str(params, "name");
        if (!ident_ok(name)) return fail("new: name (letters, digits, _)", out, size);
        if (graph(name)) return fail("new: " + name + " exists", out, size);
        if (materials::master(name)) return fail("new: a master named " + name + " exists", out, size);
        current_graphs()[name] = fresh_graph(name);
        mark();
        return reply(graph_info(*graph(name)), out, size);
    }
    Graph* g = nullptr;
    if (std::string e = need_graph(&g); !e.empty()) return fail(action + ": " + e, out, size);
    if (action == "get") return reply(graph_info(*g), out, size);
    if (action == "generate") {
        Generated r = generate_one(*g);
        Json j = Json::object();
        j.set("ok", Json::boolean_(r.ok)), j.set("glsl", Json::str(r.glsl)), j.set("error", Json::str(r.error));
        j.set("error_node", Json::str(r.error_node));
        std::string first, functions;
        for (const NodeDef& d : nodes())
            if (!d.builtin && std::any_of(g->nodes.begin(), g->nodes.end(), [&](const Node& n) { return n.type == d.name; }))
                functions += node_function(d);
        j.set("functions", Json::str(functions));
        j.set("include", Json::str(generate_all(&first)));
        return reply(j, out, size);
    }
    if (action == "save") {
        if (!save_graph(g->name, &error)) return fail("save: " + error, out, size);
        mark();
        return reply(graph_info(*graph(str(params, "name"))), out, size);
    }
    if (action == "remove") {
        if (!remove_graph(g->name, &error)) return fail("remove: " + error, out, size);
        mark();
        return reply(status_json(), out, size);
    }
    if (action == "set") {
        const Json* gj = params.get("graph");
        if (!gj || gj->type != Json::Object) return fail("set: needs graph", out, size);
        Json wrapped = Json::object();
        wrapped.set("name", Json::str(g->name));
        wrapped.set("graph", *gj);
        std::string text = prefab::json_write(wrapped, 0);
        /* (read through the file reader: one parser for both) */
        Graph fresh;
        {
            Json j;
            if (!prefab::json_parse(text.c_str(), j, &error)) return fail("set: " + error, out, size);
            std::string tmp = generated_dir() + "/.set.json";
            if (!write_text(tmp, text, &error) || !read_graph(tmp, fresh, &error)) return fail("set: " + error, out, size);
            std::error_code ec;
            fs::remove(tmp, ec);
        }
        fresh.user = g->user, fresh.path = g->path;
        *g = fresh;
        touched(*g);
        return reply(graph_info(*g), out, size);
    }
    if (action == "node_add") {
        std::string id = str(params, "id"), type = str(params, "type");
        if (!ident_ok(id)) return fail("node_add: id (letters, digits, _)", out, size);
        if (g->node(id)) return fail("node_add: " + id + " exists", out, size);
        if (!node_def(type)) return fail("node_add: unknown node type " + type, out, size);
        Node n;
        n.id = id, n.type = type;
        if (const Json* v = params.get("x"); v && v->type == Json::Number) n.x = (float)v->number;
        if (const Json* v = params.get("y"); v && v->type == Json::Number) n.y = (float)v->number;
        g->nodes.push_back(n);
        touched(*g);
        return reply(graph_info(*g), out, size);
    }
    if (action == "node_remove") {
        std::string id = str(params, "id");
        if (!g->node(id)) return fail("node_remove: no node " + id, out, size);
        g->nodes.erase(std::remove_if(g->nodes.begin(), g->nodes.end(), [&](const Node& n) { return n.id == id; }), g->nodes.end());
        g->links.erase(std::remove_if(g->links.begin(), g->links.end(),
                           [&](const Link& l) { return l.from_node == id || l.to_node == id; }), g->links.end());
        for (auto it = g->promoted.begin(); it != g->promoted.end();)
            it = it->first.rfind(id + ".", 0) == 0 ? g->promoted.erase(it) : std::next(it);
        touched(*g);
        return reply(graph_info(*g), out, size);
    }
    if (action == "node_move") {
        Node* n = g->node(str(params, "id"));
        if (!n) return fail("node_move: no node " + str(params, "id"), out, size);
        if (const Json* v = params.get("x"); v && v->type == Json::Number) n->x = (float)v->number;
        if (const Json* v = params.get("y"); v && v->type == Json::Number) n->y = (float)v->number;
        g->dirty = true; /* (a move changes no shader) */
        return reply(graph_info(*g), out, size);
    }
    if (action == "link" || action == "unlink") {
        std::string from = str(params, "from"), to = str(params, "to");
        size_t a = from.find('.'), b = to.find('.');
        if (b == std::string::npos) return fail(action + ": to as node.input", out, size);
        g->links.erase(std::remove_if(g->links.begin(), g->links.end(),
                           [&](const Link& l) { return l.to_node + "." + l.to_port == to; }), g->links.end());
        if (action == "link") {
            if (a == std::string::npos) return fail("link: from as node.output", out, size);
            g->links.push_back({ from.substr(0, a), from.substr(a + 1), to.substr(0, b), to.substr(b + 1) });
        }
        touched(*g);
        return reply(graph_info(*g), out, size);
    }
    if (action == "knob") {
        Node* n = g->node(str(params, "id"));
        if (!n) return fail("knob: no node " + str(params, "id"), out, size);
        const NodeDef* d = node_def(n->type);
        const Port* p = d ? d->input(str(params, "input")) : nullptr;
        if (!p || !p->knob) return fail("knob: no knob " + str(params, "input") + " on " + n->id, out, size);
        const Json* v = params.get("value");
        if (!v || v->type == Json::Null) n->knobs.erase(p->name);
        else n->knobs[p->name] = floats_of(v, (int)p->type);
        touched(*g);
        return reply(graph_info(*g), out, size);
    }
    if (action == "pin") {
        Node* n = g->node(str(params, "id"));
        if (!n) return fail("pin: no node " + str(params, "id"), out, size);
        const NodeDef* d = node_def(n->type);
        const Port* p = d ? d->input(str(params, "input")) : nullptr;
        if (!p || !p->knob) return fail("pin: no knob " + str(params, "input") + " on " + n->id, out, size);
        if (!n->pinned_set) {
            n->pinned_set = true;
            for (const Port& q : d->inputs)
                if (q.pin) n->pinned.push_back(q.name);
        }
        const Json* on = params.get("on");
        bool want = !on || on->type != Json::Bool || on->boolean;
        n->pinned.erase(std::remove(n->pinned.begin(), n->pinned.end(), p->name), n->pinned.end());
        if (want) n->pinned.push_back(p->name);
        g->dirty = true;
        return reply(graph_info(*g), out, size);
    }
    if (action == "promote") {
        Node* n = g->node(str(params, "id"));
        if (!n) return fail("promote: no node " + str(params, "id"), out, size);
        const NodeDef* d = node_def(n->type);
        const Port* p = d ? d->input(str(params, "input")) : nullptr;
        if (!p || !p->knob) return fail("promote: no knob " + str(params, "input") + " on " + n->id, out, size);
        if (p->type != T_FLOAT && !(p->type == T_FLOAT3 && p->color))
            return fail("promote: only number and colour knobs can be parameters", out, size);
        std::string key = n->id + "." + p->name;
        const Json* pn = params.get("param");
        if (!pn || pn->type == Json::Null) g->promoted.erase(key);
        else {
            if (pn->type != Json::String || !ident_ok(pn->string)) return fail("promote: param (letters, digits, _)", out, size);
            for (const auto& q : g->promoted)
                if (q.second == pn->string && q.first != key) return fail("promote: parameter " + pn->string + " exists", out, size);
            g->promoted[key] = pn->string;
        }
        touched(*g);
        if (!save_graph(g->name, &error)) return fail("promote: " + error, out, size);
        return reply(graph_info(*graph(str(params, "name"))), out, size);
    }
    return fail("unknown action " + action, out, size);
}
