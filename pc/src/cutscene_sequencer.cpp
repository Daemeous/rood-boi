// The timeline records interpreter places observed at vsync, not instruction dispatches.
// Looks are a transient renderer layer; only explicit Save writes the user's scene file.
#include "cutscene_sequencer.h"
#include "cutscene_internal.h"
#include "light_prefabs.h"
#include "lighting_layers.h"
#include "platform.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <climits>
#include <cerrno>
#include <map>
#include <sys/stat.h>

extern "C" int pc_battle_event_arguments(int thread, int* args, int* line, int* actor) __attribute__((weak));
namespace cutscene {
namespace {
using prefab::Json;
std::vector<SequencerKey> samples;
std::vector<LookKey> keys;
std::string scene, load_error;
int next_id = 1, frame_now = 0;
constexpr size_t sample_limit = 131072;
bool safe_id(const std::string& s)
{
    if (s.empty() || s.size() > 160) return false;
    for (unsigned char c : s)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '_' && c != '-') return false;
    return true;
}
bool fail(std::string* why, const std::string& text) { if (why) *why = text; return false; }
std::string defaults_path() { return "pc/assets/cutscenes/overrides/" + scene + ".json"; }
std::string user_path() { return lighting::user_dir() + "/cutscenes/" + scene + ".json"; }
std::string str(const Json& j, const char* k, const char* fallback = "")
{
    const Json* v = j.get(k); return v && v->type == Json::String ? v->string : fallback;
}
bool integer(const Json& j, const char* k, int& n)
{
    const Json* v = j.get(k);
    if (!v || v->type != Json::Number || !std::isfinite(v->number) ||
        v->number < INT_MIN || v->number > INT_MAX || std::floor(v->number) != v->number) return false;
    n = (int)v->number; return true;
}
void sort_keys()
{
    std::sort(keys.begin(), keys.end(), [](const LookKey& a, const LookKey& b) {
        return a.target == b.target ? a.frame < b.frame : a.target < b.target;
    });
}
bool valid_key(const LookKey& k, std::string* why)
{
    if (k.frame < 0 || k.frame > 100000000 || !std::isfinite(k.value)) return fail(why, "invalid frame or value");
    if (k.ease != "linear" && k.ease != "step") return fail(why, "ease must be linear or step");
    for (const auto& t : look_targets()) if (t.name == k.target) {
        if (k.value < t.minimum || k.value > t.maximum) return fail(why, "value outside target range");
        return true;
    }
    return fail(why, "unknown lighting target");
}
Json key_json(const LookKey& k)
{
    Json j = Json::object(); j.set("id", Json::num(k.id)); j.set("frame", Json::num(k.frame));
    j.set("target", Json::str(k.target)); j.set("value", Json::num(k.value)); j.set("ease", Json::str(k.ease)); return j;
}
Json document()
{
    Json j = Json::object(); j.set("version", Json::num(1)); j.set("scene", Json::str(scene));
    Json a = Json::array(); for (const auto& k : keys) a.items.push_back(key_json(k)); j.set("keys", a); return j;
}
bool read_document(const std::string& path, std::vector<LookKey>& result, bool& exists, std::string* why)
{
    struct stat st; exists = !stat(path.c_str(), &st);
    if (!exists) return errno == ENOENT ? true : fail(why, "cannot inspect " + path);
    if (st.st_size > 1024 * 1024) return fail(why, "scene look exceeds 1 MiB");
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return fail(why, "cannot read " + path);
    std::string text; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f))) text.append(buf, n);
    bool bad = ferror(f); fclose(f); if (bad) return fail(why, "failed reading " + path);
    Json j; if (!prefab::json_parse(text.c_str(), j, why)) return false;
    const Json* a = j.get("keys"); int version = 0;
    if (j.type != Json::Object || !integer(j, "version", version) || version != 1 ||
        str(j, "scene") != scene || !a || a->type != Json::Array || a->items.size() > 512)
        return fail(why, "invalid scene look document");
    result.clear();
    for (const auto& v : a->items) {
        LookKey k; const Json* value = v.get("value");
        if (!integer(v, "id", k.id) || k.id <= 0 || k.id >= INT_MAX || !integer(v, "frame", k.frame) ||
            !value || value->type != Json::Number) return fail(why, "invalid lighting key");
        k.target = str(v, "target"); k.ease = str(v, "ease", "linear"); k.value = (float)value->number;
        if (!valid_key(k, why)) return false;
        for (const auto& old : result)
            if (old.id == k.id || (old.target == k.target && old.frame == k.frame)) return fail(why, "duplicate lighting key");
        result.push_back(k);
    }
    return true;
}
std::string group(const std::string& n)
{
    std::string s = n; for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (s.find("camera") != s.npos) return "camera";
    if (s.find("text") != s.npos || s.find("string") != s.npos) return "dialogue";
    if (s.find("actor") != s.npos || s.find("character") != s.npos || s.find("animation") != s.npos) return "actors";
    if (s.find("sound") != s.npos || s.find("sfx") != s.npos || s.find("effect") != s.npos || s.find("music") != s.npos) return "effects";
    return "flow";
}
Json tracks_json(int offset, int limit)
{
    Json result = Json::object(), tracks = Json::array();
    result.set("sampling", Json::str("vsync interpreter places; not exhaustive executed instructions"));
    for (int t = 0; t < 4; ++t) {
        Json track = Json::object(), a = Json::array(); track.set("thread", Json::num(t));
        for (size_t i = (size_t)offset; i < samples.size() && i < (size_t)offset + limit; ++i) {
            const auto& k = samples[i]; if (k.thread != t) continue;
            Json j = Json::object(); j.set("frame", Json::num(k.frame)); j.set("event", Json::num(k.event));
            j.set("offset", Json::num(k.offset)); j.set("opcode", Json::num(k.opcode)); j.set("name", Json::str(k.name));
            j.set("group", Json::str(k.group)); j.set("actor", Json::num(k.actor)); j.set("line", Json::num(k.line));
            Json args = Json::array(); for (int v : k.arguments) args.items.push_back(Json::num(v)); j.set("arguments", args); a.items.push_back(j);
        }
        track.set("keys", a); tracks.items.push_back(track);
    }
    result.set("tracks", tracks); result.set("capacity", Json::num(sample_limit));
    result.set("total", Json::num(samples.size())); result.set("offset", Json::num(offset));
    result.set("next_offset", Json::num((size_t)offset + limit < samples.size() ? offset + limit : -1));
    return result;
}
}
const std::vector<SequencerKey>& sequencer_tracks() { return samples; }
const std::vector<LookKey>& look_keys() { return keys; }
std::vector<LookTarget> look_targets() { return pc_room_look_targets(); }
void sequencer_evaluate(int frame)
{
    frame_now = frame; pc_room_look_clear();
    // Sorted by target/frame: no value before the first key; final key holds.
    for (size_t i = 0; i < keys.size(); ++i) {
        const LookKey& k = keys[i]; if (k.frame > frame) continue;
        const LookKey* right = i + 1 < keys.size() && keys[i+1].target == k.target ? &keys[i+1] : nullptr;
        if (right && right->frame <= frame) continue;
        float value = k.value;
        if (right && k.ease == "linear") value += (right->value - value) * (float)(frame - k.frame) / (right->frame - k.frame);
        pc_room_look_set(k.target.c_str(), value);
    }
}
void sequencer_end() { pc_room_look_clear(); samples.clear(); keys.clear(); scene.clear(); load_error.clear(); next_id = 1; frame_now = 0; }
const std::string& look_error() { return load_error; }
void sequencer_begin(const std::string& id) { sequencer_end(); scene = id; look_reload(nullptr); }
void sequencer_sample(int frame, const EventInfo& e)
{
    // Loading states can retain old interpreter pointers; they are not executed keys.
    if (!e.running || e.state != 4) return;
    for (int t = 0; t < 4; ++t) {
        const Thread& thread = e.threads[t]; if (thread.offset < 0) continue;
        // One key on arrival at a place, rather than hundreds for an opcode waiting for input.
        const SequencerKey* last = nullptr;
        for (auto i = samples.rbegin(); i != samples.rend(); ++i) if (i->thread == t) { last = &*i; break; }
        if (last && last->frame <= frame && last->event == e.id && last->offset == thread.offset && last->opcode == thread.opcode) continue;
        if (samples.size() >= sample_limit) return;
        SequencerKey k; k.frame = frame; k.event = e.id; k.thread = t; k.offset = thread.offset;
        k.opcode = thread.opcode; k.name = thread.name; k.group = group(k.name);
        if (k.name == "func_800BC2E8") k.group = "camera";
        if (pc_battle_event_arguments) { int args[16]; int n = pc_battle_event_arguments(t, args, &k.line, &k.actor);
            if (n > 0 && n <= 16) k.arguments.assign(args, args+n); }
        if (k.actor >= 0) k.group = "actors";
        samples.push_back(k);
    }
}
void sequencer_truncate(int frame)
{
    samples.erase(std::remove_if(samples.begin(), samples.end(), [=](const SequencerKey& k) { return k.frame >= frame; }), samples.end());
}
void sequencer_prune(int first)
{
    samples.erase(std::remove_if(samples.begin(), samples.end(), [=](const SequencerKey& k) { return k.frame < first; }), samples.end());
}
bool look_set(int id, int frame, const std::string& target, float value, const std::string& ease, std::string* why)
{
    if (scene.empty()) return fail(why, "no active scene");
    LookKey k; k.id = id; k.frame = frame; k.target = target; k.value = value; k.ease = ease;
    if (!valid_key(k, why)) return false;
    auto found = std::find_if(keys.begin(), keys.end(), [=](const LookKey& old) { return old.id == id; });
    if (id >= 0 && found == keys.end()) return fail(why, "lighting key not found");
    for (const auto& old : keys) if (old.id != id && old.target == target && old.frame == frame) return fail(why, "target already has a key at this frame");
    if (found != keys.end()) *found = k;
    else { if (keys.size() >= 512 || next_id == INT_MAX) return fail(why, "too many lighting keys"); k.id = next_id++; keys.push_back(k); }
    sort_keys(); sequencer_evaluate(frame_now); return true;
}
bool look_delete(int id)
{
    auto i = std::find_if(keys.begin(), keys.end(), [=](const LookKey& k) { return k.id == id; });
    if (i == keys.end()) return false;
    keys.erase(i); sequencer_evaluate(frame_now); return true;
}
bool look_reload(std::string* why)
{
    // Parse into a local error so even a caller passing &load_error cannot
    // alias an output that gets cleared or replaced during this operation.
    std::string error;
    std::vector<LookKey> loaded, user; bool exists = false;
    if (!safe_id(scene)) error = "scene id is not a safe file name";
    else if (read_document(defaults_path(), loaded, exists, &error) &&
             read_document(user_path(), user, exists, &error)) error.clear();
    if (!error.empty()) {
        load_error = error;
        return fail(why, error);
    }
    if (why) why->clear();
    // User document replaces the default document, including empty keys (explicit deletion).
    if (exists) loaded = std::move(user);
    keys = std::move(loaded); next_id = 1; for (const auto& k : keys) next_id = std::max(next_id, k.id + 1);
    sort_keys(); sequencer_evaluate(frame_now); load_error.clear(); return true;
}
bool look_save(std::string* why)
{
    if (!safe_id(scene)) return fail(why, "scene id is not a safe file name");
    std::string path = user_path(), temp = path + ".tmp";
    for (size_t i = 1; i < path.size(); ++i) if (path[i] == '/') pc_mkdir(path.substr(0, i).c_str());
    FILE* f = fopen(temp.c_str(), "wb"); if (!f) return fail(why, "cannot write " + temp);
    std::string text = prefab::json_write(document()) + "\n";
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size(); if (fclose(f)) ok = false;
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) { remove(temp.c_str()); return fail(why, "cannot save " + path); }
    return true;
}
int sequencer_rpc(const Json& p, char* out, int size)
{
    std::string why; Json result;
    if (str(p, "action") == "tracks") {
        int offset = 0, limit = 256;
        if ((p.get("offset") && !integer(p, "offset", offset)) ||
            (p.get("limit") && !integer(p, "limit", limit)) || offset < 0 || limit < 1 || limit > 256) {
            snprintf(out, size, "tracks requires offset >= 0 and limit 1..256"); return -1;
        }
        result = tracks_json(offset, limit);
    }
    else {
        std::string op = str(p, "op", "list"); bool ok = true;
        if (op == "set" || op == "add" || op == "update") {
            int id = -1, frame = 0; const Json* value = p.get("value");
            if ((p.get("id") && !integer(p, "id", id)) || !integer(p, "frame", frame) || !value || value->type != Json::Number)
                ok = fail(&why, "set requires integer frame, numeric value and optional integer id");
            else ok = look_set(id, frame, str(p, "target"), (float)value->number, str(p, "ease", "linear"), &why);
        } else if (op == "delete") { int id; ok = integer(p, "id", id) && look_delete(id); if (!ok) why = "lighting key not found"; }
        else if (op == "save") ok = look_save(&why);
        else if (op == "reload") ok = look_reload(&why);
        else if (op != "list") ok = fail(&why, "unknown look operation");
        if (!ok) { snprintf(out, size, "%s", why.c_str()); return -1; }
        result = document(); result.set("load_error", Json::str(load_error)); result.set("default_path", Json::str(defaults_path())); result.set("user_path", Json::str(user_path()));
        Json targets = Json::array(); for (const auto& t : look_targets()) { Json v = Json::object();
            v.set("name", Json::str(t.name)); v.set("value", Json::num(t.value)); v.set("minimum", Json::num(t.minimum)); v.set("maximum", Json::num(t.maximum)); targets.items.push_back(v); }
        result.set("targets", targets);
    }
    // RPC uses newline-delimited JSON. Keep the file writer's pretty output
    // for saved looks, but remove whitespace outside strings on the wire.
    std::string text;
    bool quoted = false, escaped = false;
    for (char c : prefab::json_write(result)) {
        if (!quoted && (c == ' ' || c == '\n' || c == '\r' || c == '\t')) continue;
        text += c;
        if (escaped) escaped = false;
        else if (quoted && c == '\\') escaped = true;
        else if (c == '"') quoted = !quoted;
    }
    if ((int)text.size() >= size) { snprintf(out, size, "sequencer response exceeds buffer"); return -1; }
    snprintf(out, size, "%s", text.c_str()); return 0;
}
} // namespace cutscene
