// The editor's selection and hidden objects (editor_selection.h).
#include "editor_selection.h"

#include <ctype.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <utility>

namespace rood_selection {
namespace {
std::vector<Ref> chosen;
unsigned changes = 1;
Source source = FROM_CODE;
Ref pinned;
std::map<std::pair<int, int>, std::vector<Ref>> hidden_by_room;
std::pair<int, int> current_room(-1, -1);

void changed(Source s)
{
    ++changes;
    source = s;
}
void drop(const Ref& r) { chosen.erase(std::remove(chosen.begin(), chosen.end(), r), chosen.end()); }
} // namespace

Ref ref_of(const std::string& id)
{
    static const struct {
        const char* prefix;
        Kind kind;
    } prefixes[] = { { "light:", KIND_LIGHT }, { "actor:", KIND_ACTOR }, { "glow:", KIND_GLOW }, { "fog:", KIND_VOLUME } };
    if (id == "env:sun") /* the room's sun or moon (its sky and fog are no objects) */
        return Ref { KIND_SUN, id };
    for (const auto& p : prefixes) {
        size_t n = strlen(p.prefix);
        if (id.size() > n && !id.compare(0, n, p.prefix))
            return Ref { p.kind, id };
    }
    return Ref();
}
const char* kind_name(Kind k)
{
    return k == KIND_LIGHT ? "light"
        : k == KIND_ACTOR  ? "actor"
        : k == KIND_GLOW   ? "glow"
        : k == KIND_VOLUME ? "fog"
        : k == KIND_SUN    ? "sun"
                           : "none";
}
const char* source_name(Source s)
{
    static const char* const names[] = { "viewport", "outliner", "details", "rpc", "undo", "code" };
    return names[s];
}

const std::vector<Ref>& items() { return chosen; }
Ref primary() { return chosen.empty() ? Ref() : chosen.back(); }
bool contains(const Ref& r) { return std::find(chosen.begin(), chosen.end(), r) != chosen.end(); }
bool empty() { return chosen.empty(); }
void set(const Ref& r, Source s)
{
    chosen.clear();
    if (r)
        chosen.push_back(r);
    changed(s);
}
void add(const Ref& r, Source s)
{
    if (!r)
        return;
    drop(r);
    chosen.push_back(r);
    changed(s);
}
void toggle(const Ref& r, Source s)
{
    if (!r)
        return;
    if (contains(r))
        drop(r);
    else
        chosen.push_back(r);
    changed(s);
}
void remove(const Ref& r, Source s)
{
    if (!contains(r))
        return;
    drop(r);
    changed(s);
}
void clear(Source s)
{
    if (chosen.empty())
        return;
    chosen.clear();
    changed(s);
}
void set_all(const std::vector<Ref>& refs, Source s)
{
    chosen.clear();
    for (const Ref& r : refs)
        if (r) {
            drop(r);
            chosen.push_back(r);
        }
    changed(s);
}
void click(const Ref& r, bool shift, bool ctrl, Source s)
{
    if (ctrl)
        toggle(r, s);
    else if (shift)
        add(r, s);
    else
        set(r, s);
}
void box(const std::vector<Ref>& refs, bool shift, bool ctrl, Source s)
{
    if (!shift && !ctrl)
        chosen.clear();
    for (const Ref& r : refs) {
        if (!r)
            continue;
        if (ctrl && contains(r))
            drop(r);
        else if (!contains(r))
            chosen.push_back(r);
    }
    changed(s);
}
bool retain(const std::function<bool(const Ref&)>& alive, Source s)
{
    size_t before = chosen.size();
    chosen.erase(std::remove_if(chosen.begin(), chosen.end(), [&](const Ref& r) { return !alive(r); }), chosen.end());
    if (chosen.size() == before)
        return false;
    changed(s);
    return true;
}
unsigned serial() { return changes; }
Source last_source() { return source; }

void lock(bool on) { pinned = on ? primary() : Ref(); }
Ref locked() { return pinned; }

void set_room(int zone, int room) { current_room = { zone, room }; }
bool hidden(const Ref& r)
{
    auto it = hidden_by_room.find(current_room);
    return it != hidden_by_room.end() && std::find(it->second.begin(), it->second.end(), r) != it->second.end();
}
bool any_hidden()
{
    auto it = hidden_by_room.find(current_room);
    return it != hidden_by_room.end() && !it->second.empty();
}
void hide(const std::vector<Ref>& refs)
{
    std::vector<Ref>& list = hidden_by_room[current_room];
    for (const Ref& r : refs)
        if (r && std::find(list.begin(), list.end(), r) == list.end())
            list.push_back(r);
    if (list.empty())
        hidden_by_room.erase(current_room);
}
void unhide_all() { hidden_by_room.erase(current_room); }
std::vector<Ref> hidden_items()
{
    auto it = hidden_by_room.find(current_room);
    return it == hidden_by_room.end() ? std::vector<Ref>() : it->second;
}
std::string hidden_line(int zone, int room)
{
    auto it = hidden_by_room.find({ zone, room });
    if (it == hidden_by_room.end() || it->second.empty())
        return "";
    std::string line = "editor_hidden_" + std::to_string(zone) + "_" + std::to_string(room);
    for (const Ref& r : it->second)
        line += " " + r.id;
    return line;
}
void write_hidden(FILE* f)
{
    for (const auto& kv : hidden_by_room) {
        std::string line = hidden_line(kv.first.first, kv.first.second);
        if (!line.empty())
            fprintf(f, "%s\n", line.c_str());
    }
}
bool parse_hidden(const char* key, const char* rest)
{
    int zone, room, used = 0;
    if (strncmp(key, "editor_hidden_", 14) || sscanf(key + 14, "%d_%d%n", &zone, &room, &used) != 2 || key[14 + used])
        return false;
    std::vector<Ref> refs;
    std::string word;
    for (const char* p = rest;; p++) {
        if (*p && !isspace((unsigned char)*p)) {
            word += *p;
            continue;
        }
        Ref r = ref_of(word);
        if (r && std::find(refs.begin(), refs.end(), r) == refs.end())
            refs.push_back(r);
        word.clear();
        if (!*p)
            break;
    }
    if (refs.empty())
        hidden_by_room.erase({ zone, room });
    else
        hidden_by_room[{ zone, room }] = refs;
    return true;
}
void clear_hidden() { hidden_by_room.clear(); }

} // namespace rood_selection
