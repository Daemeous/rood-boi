// Lighting packs: layered per-room lighting files (lighting_layers.h;
// format and workflow: lighting/README.md).
#include "lighting_layers.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <set>
#include "platform.h"

namespace lighting {

/* ---- schema ---- */
namespace {
struct FieldSpec {
    const char* key;
    int decimals; /* -1: text */
    const char* def; /* nullptr: no default (always written) */
    bool vol; /* follows the game (compared only for presence) */
};
struct KindSpec {
    const char* kind;
    std::vector<FieldSpec> fields;
    bool open; /* other keys allowed (an actor's prefab parameters) */
    int other_decimals;
};
const std::vector<KindSpec>& kinds()
{
    static const std::vector<KindSpec> k = {
        { "actor",
            { { "name", -1, nullptr, false }, { "prefab", -1, nullptr, false }, { "pos", 1, nullptr, false },
                { "rot", 2, "0,0,0", false }, { "seed", 0, "1", false }, { "enabled", 0, "1", false },
                { "glow", 1, nullptr, true }, { "glowkey", -1, nullptr, false }, { "fixture", 1, nullptr, false },
                { "fixture_radius", 1, "16", false }, { "fixture_shadows", 0, "0", false },
                { "fixture_type", -1, nullptr, false }, { "flame", 0, "0", false } },
            true, 4 },
        /* The room's sun ("env sun") and sky ("env sky"): environment.cpp. */
        { "env",
            { { "enabled", 0, "1", false }, { "azimuth", 2, "180", false }, { "elevation", 2, "45", false },
                { "color", 3, "1,1,1", false }, { "intensity", 3, "1", false }, { "angle", 2, "0.53", false },
                { "shadows", 0, "1", false }, { "zenith", 3, "0.1,0.12,0.2", false },
                { "horizon", 3, "0.25,0.25,0.3", false }, { "ground", 3, "0.05,0.05,0.05", false },
                { "sun_tint", 3, "0", false }, { "ambient", 3, "0.5", false },
                /* The ground fog ("env fog"): atmosphere.cpp. */
                { "density", 5, "0.003", false }, { "height", 1, "48", false }, { "offset", 1, "0", false },
                { "noise", 2, "0.6", false }, { "scale", 1, "160", false }, { "wind", 1, "4,0,1.5", false },
                { "fill", 3, "0,0,0", false }, { "sky", 3, "1", false } },
            false, 4 },
        /* Fog volumes ("fog <id>"): atmosphere.cpp. */
        { "fog",
            { { "name", -1, nullptr, false }, { "shape", -1, "box", false }, { "pos", 1, nullptr, false },
                { "size", 1, "256,128,256", false }, { "yaw", 2, "0", false }, { "density", 5, "0.004", false },
                { "edge", 2, "0.5", false }, { "color", 3, "1,1,1", false }, { "fill", 3, "0,0,0", false },
                { "noise", 2, "0.5", false }, { "enabled", 0, "1", false },
                /* the interactive fog (fog_sim.h): the characters, attacks and spells part it */
                { "sim", 0, "0", false }, { "sim_strength", 2, "1", false }, { "sim_recover", 2, "3", false },
                { "sim_footprint", 2, "1", false }, { "sim_trail", 2, "1", false }, { "sim_wind", 2, "1", false },
                { "detail", 2, "0.5", false }, { "sim_swirl", 2, "0", false }, { "shadow", 2, "0", false },
                /* its own renderer (atmosphere.h pc_fogvol_gpu): its look, exempt from the room's haze limits */
                { "opacity", 2, "1", false }, { "brightness", 2, "1", false }, { "phase", 2, "0.3", false },
                { "ambient", 2, "1", false }, { "glow", 3, "0,0,0", false }, { "glow_core", 2, "0.5", false },
                { "taper", 2, "0", false }, { "detail_size", 1, "0", false },
                /* each source's weight here (-1: the global setting fog_disturb_*) */
                { "disturb_player", 2, "-1", false }, { "disturb_actors", 2, "-1", false },
                { "disturb_attacks", 2, "-1", false }, { "disturb_spells", 2, "-1", false },
                /* a named preset (pc/assets/environment/fog_presets.json) for the fields not written */
                { "preset", -1, nullptr, false } },
            false, 4 },
        { "glow",
            { { "pos", 1, nullptr, true }, { "muted", 0, "0", false }, { "intensity", 3, "1", false },
                { "tint", 3, "1,1,1", false }, { "size", 2, "0", false }, { "reach", 1, "0", false },
                { "fixture_type", -1, nullptr, false }, { "shape", -1, "auto", false } },
            false, 4 },
        { "light",
            { { "name", -1, nullptr, false }, { "type", -1, "point", false }, { "pos", 1, nullptr, false },
                { "dir", 4, "0,1,0", false }, { "color", 3, "1,0.75,0.45", false }, { "intensity", 3, "1", false },
                { "radius", 1, "600", false }, { "cone", 2, "45", false }, { "size", 1, "128,128", false },
                { "shadows", 0, "0", false }, { "enabled", 0, "1", false } },
            false, 4 },
        /* Post-processing profiles ("post room", "post cutscene", "post event.<id>"): post_profile.cpp.
         * Open: any post field (its value checked there), `preset` a named profile under them. */
        { "post", { { "preset", -1, nullptr, false } }, true, 4 },
        { "set", { { "value", 6, nullptr, false } }, false, 6 },
        { "uv", { { "speed", 3, "1", false }, { "freeze", 0, "0", false } }, false, 4 },
    };
    return k;
}
const KindSpec* kind_spec(const std::string& kind)
{
    for (const KindSpec& k : kinds())
        if (kind == k.kind)
            return &k;
    return nullptr;
}
const FieldSpec* field_spec(const std::string& kind, const std::string& key)
{
    if (const KindSpec* k = kind_spec(kind))
        for (const FieldSpec& f : k->fields)
            if (key == f.key)
                return &f;
    return nullptr;
}
int field_rank(const std::string& kind, const std::string& key)
{
    if (const KindSpec* k = kind_spec(kind))
        for (size_t i = 0; i < k->fields.size(); i++)
            if (key == k->fields[i].key)
                return (int)i;
    return 1000;
}
bool volatile_field(const std::string& kind, const std::string& name, const std::string& key)
{
    const FieldSpec* f = field_spec(kind, key);
    if (!f || !f->vol)
        return false;
    /* A glow override without a key follows its position: that is its identity. */
    return !(kind == "glow" && !name.empty() && name[0] == '@');
}
bool parse_numbers(const std::string& text, std::vector<double>& out)
{
    out.clear();
    if (text.empty())
        return false;
    const char* p = text.c_str();
    for (;;) {
        char* end;
        errno = 0;
        double v = strtod(p, &end);
        if (end == p || errno == ERANGE || !isfinite(v))
            return false;
        out.push_back(v);
        if (!*end)
            return true;
        if (*end != ',')
            return false;
        p = end + 1;
    }
}
std::string format_numbers(const std::vector<double>& v, int decimals)
{
    std::string s;
    for (size_t i = 0; i < v.size(); i++)
        s += (i ? "," : "") + format_number(v[i], decimals);
    return s;
}
bool id_char(char c) { return c > ' ' && c != '#' && c != '"' && c != '='; }
} // namespace

int field_decimals(const std::string& kind, const std::string& key)
{
    if (const FieldSpec* f = field_spec(kind, key))
        return f->decimals;
    const KindSpec* k = kind_spec(kind);
    return k ? k->other_decimals : 4;
}
std::string format_number(double v, int decimals)
{
    char text[64];
    snprintf(text, sizeof text, "%.*f", decimals < 0 ? 4 : decimals, v);
    std::string s = text;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    if (s == "-0")
        s = "0";
    return s;
}
std::string canonical_value(const std::string& kind, const std::string& key, const std::string& value)
{
    int d = field_decimals(kind, key);
    if (d < 0)
        return value;
    std::vector<double> v;
    if (!parse_numbers(value, v))
        return value;
    return format_numbers(v, d);
}
const char* field_default(const std::string& kind, const std::string& key)
{
    const FieldSpec* f = field_spec(kind, key);
    return f && f->def ? f->def : "";
}
bool known_field(const std::string& kind, const std::string& key)
{
    if (field_spec(kind, key))
        return true;
    const KindSpec* k = kind_spec(kind);
    return k && k->open;
}
bool known_kind(const std::string& kind) { return kind_spec(kind) != nullptr; }

const std::string* Item::get(const std::string& key) const
{
    for (const auto& f : fields)
        if (f.first == key)
            return &f.second;
    return nullptr;
}
void Item::set(const std::string& key, const std::string& value)
{
    for (auto& f : fields)
        if (f.first == key) {
            f.second = value;
            return;
        }
    fields.emplace_back(key, value);
}
void Item::erase(const std::string& key)
{
    fields.erase(std::remove_if(fields.begin(), fields.end(), [&](const std::pair<std::string, std::string>& f) {
        return f.first == key;
    }),
        fields.end());
}

bool preset_field(const std::string& kind, const std::string& key)
{
    if (kind != "fog")
        return false;
    for (const char* k : { "shape", "size", "density", "edge", "color", "fill", "noise", "sim", "sim_strength",
             "sim_recover", "sim_footprint", "sim_trail", "sim_wind", "detail", "sim_swirl", "shadow", "opacity", "brightness",
             "phase", "ambient", "glow", "glow_core", "taper", "detail_size", "disturb_player", "disturb_actors",
             "disturb_attacks", "disturb_spells" })
        if (key == k)
            return true;
    return false;
}

void canonicalize(Item& item)
{
    Fields out;
    /* A fog volume with a preset keeps the values it writes even when they
     * are the schema's defaults: they differ from its preset's. */
    bool preset = item.kind == "fog" && item.get("preset");
    for (const auto& f : item.fields) {
        std::string v = canonical_value(item.kind, f.first, f.second);
        const FieldSpec* spec = field_spec(item.kind, f.first);
        if (spec && spec->def && v == canonical_value(item.kind, f.first, spec->def)
            && !(preset && preset_field(item.kind, f.first)))
            continue;
        out.emplace_back(f.first, v);
    }
    std::stable_sort(out.begin(), out.end(), [&](const std::pair<std::string, std::string>& a,
                                                const std::pair<std::string, std::string>& b) {
        int ra = field_rank(item.kind, a.first), rb = field_rank(item.kind, b.first);
        if (ra != rb)
            return ra < rb;
        return a.first < b.first;
    });
    /* One value per key (the last one written wins). */
    Fields unique;
    for (const auto& f : out) {
        bool replaced = false;
        for (auto& u : unique)
            if (u.first == f.first)
                u.second = f.second, replaced = true;
        if (!replaced)
            unique.push_back(f);
    }
    item.fields = unique;
}

std::string new_name()
{
    /* Seconds (hex), a counter within the second, then random digits: ids
     * sort in the order they were made (the Outliner keeps that order). */
    static long long last_second = -1;
    static unsigned counter;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (ts.tv_sec != last_second)
        last_second = ts.tv_sec, counter = 0;
    uint16_t random = 0;
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f || fread(&random, sizeof random, 1, f) != 1)
        random = (uint16_t)((uint32_t)ts.tv_nsec * 2654435761u >> 16 ^ (uint32_t)getpid());
    if (f)
        fclose(f);
    char text[32];
    snprintf(text, sizeof text, "%08llx%02x%04x", (unsigned long long)ts.tv_sec & 0xffffffffULL, counter++ & 0xff,
        random);
    return text;
}

/* ---- files ---- */
namespace {
/* The words of a line: bare tokens, key=value (value may be "quoted"),
 * !key; a # outside quotes ends the line. */
struct Token {
    std::string key, value;
    bool has_value = false, unset = false;
};
bool tokenize(const std::string& line, std::vector<Token>& out, std::string* error)
{
    out.clear();
    size_t i = 0, n = line.size();
    while (i < n) {
        while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n'))
            ++i;
        if (i >= n || line[i] == '#')
            break;
        Token t;
        if (line[i] == '!') {
            t.unset = true;
            ++i;
        }
        size_t start = i;
        while (i < n && line[i] != '=' && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n')
            ++i;
        t.key = line.substr(start, i - start);
        if (i < n && line[i] == '=') {
            ++i;
            t.has_value = true;
            if (i < n && line[i] == '"') {
                ++i;
                bool closed = false;
                while (i < n) {
                    char c = line[i++];
                    if (c == '\\' && i < n) {
                        t.value += line[i++];
                        continue;
                    }
                    if (c == '"') {
                        closed = true;
                        break;
                    }
                    t.value += c;
                }
                if (!closed) {
                    if (error)
                        *error = "unterminated quote";
                    return false;
                }
            } else {
                size_t vs = i;
                while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n')
                    ++i;
                t.value = line.substr(vs, i - vs);
            }
        }
        if (t.key.empty()) {
            if (error)
                *error = "empty key";
            return false;
        }
        out.push_back(t);
    }
    return true;
}
std::string quote(const std::string& v)
{
    bool plain = !v.empty() && v[0] != '"' && v[0] != '!';
    for (char c : v)
        if (c <= ' ' || c == '"' || c == '#' || c == '\\')
            plain = false;
    if (plain)
        return v;
    std::string q = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\')
            q += '\\';
        q += c;
    }
    return q + "\"";
}
bool valid_id(const std::string& id)
{
    size_t colon = id.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= id.size())
        return false;
    for (size_t i = 0; i < colon; i++)
        if (!(islower((unsigned char)id[i]) || id[i] == '_'))
            return false;
    for (char c : id)
        if (!id_char(c))
            return false;
    return true;
}
/* One line of a version 2 file: 1 an entry, 0 nothing (blank, comment,
 * header), -1 an error. */
int parse_line(const std::string& line, Entry& e, int* header, std::string* error)
{
    std::vector<Token> tokens;
    if (!tokenize(line, tokens, error))
        return -1;
    if (tokens.empty())
        return 0;
    const std::string& verb = tokens[0].key;
    if (tokens[0].has_value || tokens[0].unset) {
        *error = "expected a kind or verb, not '" + verb + "='";
        return -1;
    }
    if (verb == "rood-lighting") {
        if (tokens.size() < 2 || tokens[1].has_value) {
            *error = "header: rood-lighting <version>";
            return -1;
        }
        *header = atoi(tokens[1].key.c_str());
        return 0;
    }
    if (tokens.size() < 2 || tokens[1].has_value || tokens[1].unset) {
        *error = "'" + verb + "' needs an id";
        return -1;
    }
    e = Entry();
    if (verb == "override" || verb == "delete") {
        e.verb = verb == "override" ? OVERRIDE : REMOVE;
        e.id = tokens[1].key;
        if (!valid_id(e.id)) {
            *error = "bad id '" + e.id + "' (kind:name)";
            return -1;
        }
        std::string kind = e.id.substr(0, e.id.find(':'));
        for (size_t i = 2; i < tokens.size(); i++) {
            const Token& t = tokens[i];
            if (e.verb == REMOVE) {
                *error = "delete takes only an id";
                return -1;
            }
            if (t.unset) {
                if (t.has_value) {
                    *error = "!" + t.key + " takes no value";
                    return -1;
                }
                e.unset.push_back(t.key);
            } else if (!t.has_value) {
                *error = "expected key=value, got '" + t.key + "'";
                return -1;
            } else
                e.set.emplace_back(t.key, canonical_value(kind, t.key, t.value));
        }
        return 1;
    }
    for (char c : verb)
        if (!(islower((unsigned char)c) || c == '_')) {
            *error = "bad kind '" + verb + "'";
            return -1;
        }
    e.verb = DEFINE;
    e.item.kind = verb;
    e.item.name = tokens[1].key;
    e.id = e.item.id();
    if (!valid_id(e.id)) {
        *error = "bad id '" + e.id + "'";
        return -1;
    }
    for (size_t i = 2; i < tokens.size(); i++) {
        const Token& t = tokens[i];
        if (t.unset || !t.has_value) {
            *error = "expected key=value, got '" + std::string(t.unset ? "!" : "") + t.key + "'";
            return -1;
        }
        e.item.fields.emplace_back(t.key, t.value);
    }
    canonicalize(e.item);
    return 1;
}
bool is_v2(const std::string& text)
{
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        size_t s = line.find_first_not_of(" \t\r");
        if (s == std::string::npos || line[s] == '#')
            continue;
        return line.compare(s, 13, "rood-lighting") == 0;
    }
    return false;
}
std::string read_all(const std::string& path, bool* ok)
{
    *ok = false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return std::string();
    std::string text;
    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof buffer, f)) > 0)
        text.append(buffer, n);
    fclose(f);
    *ok = true;
    return text;
}
void mkdirs(const std::string& path)
{
    for (size_t i = 1; i < path.size(); i++)
        if (path[i] == '/')
            pc_mkdir(path.substr(0, i).c_str());
}
} // namespace

bool parse_text(const std::string& text, File& out)
{
    out.entries.clear();
    out.errors.clear();
    if (!is_v2(text)) {
        out.version = 0;
        std::vector<Item> items;
        legacy_items(text, items);
        for (Item& it : items) {
            Entry e;
            e.item = it;
            e.id = it.id();
            out.entries.push_back(e);
        }
        return true;
    }
    out.version = 0;
    size_t at = 0;
    int number = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        ++number;
        Entry e;
        std::string error;
        int header = 0;
        int r = parse_line(line, e, &header, &error);
        if (header)
            out.version = header;
        if (r < 0)
            out.errors.push_back("line " + std::to_string(number) + ": " + error);
        else if (r > 0) {
            e.line = number;
            out.entries.push_back(e);
        }
    }
    if (out.version > FORMAT_VERSION)
        out.errors.push_back("format version " + std::to_string(out.version) + " is newer than this build reads ("
            + std::to_string(FORMAT_VERSION) + ")");
    return out.errors.empty();
}

bool read_file(const std::string& path, File& out)
{
    out = File();
    out.path = path;
    bool ok;
    std::string text = read_all(path, &ok);
    out.exists = ok;
    out.mtime = file_mtime(path);
    if (!ok)
        return false;
    parse_text(text, out);
    return true;
}

std::string format_entry(const Entry& e)
{
    std::string s;
    if (e.verb == DEFINE) {
        s = e.item.kind + " " + e.item.name;
        for (const auto& f : e.item.fields)
            s += " " + f.first + "=" + quote(f.second);
    } else if (e.verb == OVERRIDE) {
        s = "override " + e.id;
        for (const auto& f : e.set)
            s += " " + f.first + "=" + quote(f.second);
        for (const std::string& k : e.unset)
            s += " !" + k;
    } else
        s = "delete " + e.id;
    return s;
}

std::string format_file(const std::vector<Entry>& entries, const std::string& comment)
{
    std::vector<const Entry*> sorted;
    for (const Entry& e : entries)
        sorted.push_back(&e);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Entry* a, const Entry* b) { return a->id < b->id; });
    std::string s = "rood-lighting " + std::to_string(FORMAT_VERSION) + "\n";
    if (!comment.empty())
        s += "# " + comment + "\n";
    for (const Entry* e : sorted)
        s += format_entry(*e) + "\n";
    return s;
}

bool write_file(const std::string& path, const std::string& text, std::string* error)
{
    mkdirs(path);
    std::string temp = path + ".tmp";
    FILE* f = fopen(temp.c_str(), "wb");
    if (!f) {
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        unlink(temp.c_str());
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    return true;
}

std::string patch_text(const std::string& old_text, const std::vector<std::string>& ids,
    const std::vector<Entry>& entries, const std::string& comment)
{
    if (!is_v2(old_text)) {
        /* A new (or old-format) file: written whole. */
        File f;
        parse_text(old_text, f);
        std::vector<Entry> all;
        std::set<std::string> touched(ids.begin(), ids.end());
        for (const Entry& e : f.entries)
            if (!touched.count(e.id))
                all.push_back(e);
        for (const Entry& e : entries)
            all.push_back(e);
        return format_file(all, comment);
    }
    std::set<std::string> touched(ids.begin(), ids.end());
    std::map<std::string, const Entry*> replacement;
    for (const Entry& e : entries)
        replacement[e.id] = &e;
    std::vector<std::string> lines;
    std::vector<std::string> line_ids; /* "" for comments, blanks, the header */
    size_t at = 0;
    while (at < old_text.size()) {
        size_t end = old_text.find('\n', at);
        if (end == std::string::npos)
            end = old_text.size();
        std::string line = old_text.substr(at, end - at);
        at = end + 1;
        Entry e;
        std::string error;
        int header = 0;
        int r = parse_line(line, e, &header, &error);
        lines.push_back(line);
        line_ids.push_back(r > 0 ? e.id : "");
    }
    std::set<std::string> written;
    std::vector<std::string> out;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& id = line_ids[i];
        if (id.empty() || !touched.count(id)) {
            out.push_back(lines[i]);
            continue;
        }
        auto it = replacement.find(id);
        if (it != replacement.end() && !written.count(id)) {
            out.push_back(format_entry(*it->second));
            written.insert(id);
        }
    }
    /* New entries: before the first entry line with a greater id. */
    for (const Entry& e : entries) {
        if (written.count(e.id))
            continue;
        size_t place = out.size();
        for (size_t i = 0; i < out.size(); i++) {
            Entry x;
            std::string error;
            int header = 0;
            if (parse_line(out[i], x, &header, &error) > 0 && x.id > e.id) {
                place = i;
                break;
            }
        }
        out.insert(out.begin() + (long)place, format_entry(e));
        written.insert(e.id);
    }
    std::string s;
    for (const std::string& l : out)
        s += l + "\n";
    return s;
}

long long file_mtime(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) ? 0 : PC_MTIME_NS(st);
}

/* ---- merging ---- */
bool apply_entry(State& state, const Entry& e, std::string* problem)
{
    if (e.verb == DEFINE) {
        state[e.id] = e.item;
        return true;
    }
    auto it = state.find(e.id);
    if (it == state.end()) {
        if (problem)
            *problem = std::string(e.verb == OVERRIDE ? "override" : "delete") + " of " + e.id
                + ": no layer below defines it";
        return false;
    }
    if (e.verb == REMOVE) {
        state.erase(it);
        return true;
    }
    Item& item = it->second;
    for (const auto& f : e.set)
        item.set(f.first, f.second);
    for (const std::string& k : e.unset)
        item.erase(k);
    canonicalize(item);
    return true;
}

namespace {
bool same_item(const Item& a, const Item& b)
{
    if (a.kind != b.kind)
        return false;
    auto stable = [&](const Item& x) {
        Fields f;
        for (const auto& kv : x.fields)
            if (volatile_field(x.kind, x.name, kv.first))
                f.emplace_back(kv.first, std::string()); /* presence only */
            else
                f.push_back(kv);
        return f;
    };
    return stable(a) == stable(b);
}
} // namespace

bool diff_one(const State& below, const State& desired, const std::string& id, Entry& out)
{
    out = Entry();
    out.id = id;
    auto b = below.find(id);
    auto d = desired.find(id);
    if (d == desired.end()) {
        if (b == below.end())
            return false;
        out.verb = REMOVE;
        return true;
    }
    if (b == below.end()) {
        out.verb = DEFINE;
        out.item = d->second;
        return true;
    }
    if (same_item(b->second, d->second))
        return false;
    const Item &x = b->second, &y = d->second;
    if (x.kind != "light" && x.kind != "actor" && x.kind != "env" && x.kind != "fog" && x.kind != "post") {
        out.verb = DEFINE;
        out.item = y;
        return true;
    }
    out.verb = OVERRIDE;
    for (const auto& f : y.fields) {
        const std::string* old = x.get(f.first);
        if (old && (*old == f.second || volatile_field(y.kind, y.name, f.first)))
            continue;
        out.set.push_back(f);
    }
    for (const auto& f : x.fields)
        if (!y.get(f.first))
            out.unset.push_back(f.first);
    return !out.set.empty() || !out.unset.empty();
}

std::vector<Entry> diff(const State& below, const State& desired)
{
    std::set<std::string> ids;
    for (const auto& kv : below)
        ids.insert(kv.first);
    for (const auto& kv : desired)
        ids.insert(kv.first);
    std::vector<Entry> out;
    for (const std::string& id : ids) {
        Entry e;
        if (diff_one(below, desired, id, e))
            out.push_back(e);
    }
    return out;
}

/* ---- layers and paths ---- */
namespace {
std::string packs_value = "default";
std::string active_value;
int serial = 1;
std::string env(const char* name)
{
    const char* v = getenv(name);
    return v ? v : "";
}
} // namespace

std::string packs_root()
{
    std::string root = env("ROOD_LIGHTING");
    if (!root.empty())
        return root == "none" ? std::string() : root;
    if (!env("ROOD_LIGHTS").empty())
        return std::string(); /* tests: an isolated user layer, no packs */
    return "lighting";
}
std::string user_dir()
{
    std::string d = env("ROOD_LIGHTS");
    return d.empty() ? "pc/user/lighting" : d;
}
std::string legacy_dir() { return env("ROOD_LIGHTS").empty() ? "pc/lights" : std::string(); }
std::vector<std::string> packs()
{
    std::vector<std::string> out;
    if (packs_root().empty())
        return out;
    std::string s = packs_value;
    size_t at = 0;
    while (at <= s.size()) {
        size_t end = s.find(',', at);
        if (end == std::string::npos)
            end = s.size();
        std::string name = s.substr(at, end - at);
        at = end + 1;
        size_t a = name.find_first_not_of(" \t"), b = name.find_last_not_of(" \t");
        if (a == std::string::npos)
            continue;
        name = name.substr(a, b - a + 1);
        bool safe = name != "." && name != "..";
        for (char c : name)
            if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                safe = false;
        if (safe && std::find(out.begin(), out.end(), name) == out.end())
            out.push_back(name);
    }
    return out;
}
void set_packs(const std::string& comma_list)
{
    if (comma_list != packs_value) {
        packs_value = comma_list;
        ++serial;
    }
}
std::string packs_setting() { return packs_value; }
std::string active_pack_setting() { return active_value; }
void set_active_pack(const std::string& name)
{
    if (name != active_value) {
        active_value = name;
        ++serial;
    }
}
int config_serial() { return serial; }
std::string room_file_name(int zone, int room)
{
    char name[64];
    snprintf(name, sizeof name, "zone%02d_room%02d.txt", zone, room);
    return name;
}

std::string Layer::label() const
{
    if (kind == USER)
        return "user";
    return pack + (kind == GENERATED ? "/generated" : "/rooms");
}
char Layer::badge() const { return kind == GENERATED ? 'G' : kind == AUTHORED ? 'P' : 'U'; }

State merge(const Room& r, int upto, std::vector<Problem>* problems, std::map<std::string, Provenance>* provenance,
    std::vector<Entry>* dangling_of_last, std::vector<std::string>* deleted)
{
    State s;
    for (int i = 0; i < upto && i < (int)r.layers.size(); i++) {
        const Layer& layer = r.layers[i];
        for (const Entry& e : layer.file.entries) {
            bool existed = s.count(e.id) != 0;
            std::string problem;
            if (!apply_entry(s, e, &problem)) {
                if (problems)
                    problems->push_back(Problem { i, e.line, e.id, problem });
                if (dangling_of_last && i == upto - 1)
                    dangling_of_last->push_back(e);
                continue;
            }
            if (provenance) {
                if (e.verb == REMOVE)
                    provenance->erase(e.id);
                else if (e.verb == DEFINE && !existed)
                    (*provenance)[e.id] = Provenance { i, {} };
                else
                    (*provenance)[e.id].changed.push_back(i);
            }
            if (deleted) {
                if (e.verb == REMOVE)
                    deleted->push_back(e.id);
                else
                    deleted->erase(std::remove(deleted->begin(), deleted->end(), e.id), deleted->end());
            }
        }
    }
    return s;
}

void load_room(Room& r, int zone, int room)
{
    r = Room();
    r.zone = zone, r.room = room;
    std::string name = room_file_name(zone, room);
    std::string root = packs_root();
    std::vector<std::string> list = packs();
    std::string active = active_value;
    if (active.empty() || std::find(list.begin(), list.end(), active) == list.end())
        active = list.empty() ? std::string() : list.back();
    for (const std::string& pack : list) {
        for (int k = 0; k < 2; k++) {
            Layer layer;
            layer.kind = k ? AUTHORED : GENERATED;
            layer.pack = pack;
            layer.path = root + "/" + pack + (k ? "/rooms/" : "/generated/") + name;
            read_file(layer.path, layer.file);
            if (k && pack == active)
                r.active = (int)r.layers.size();
            r.layers.push_back(layer);
        }
    }
    Layer user;
    user.kind = USER;
    user.path = user_dir() + "/" + name;
    if (!legacy_dir().empty())
        user.legacy = legacy_dir() + "/" + name;
    if (!read_file(user.path, user.file) && !user.legacy.empty()) {
        File old;
        if (read_file(user.legacy, old))
            user.file = old; /* (its path names the old file: saving migrates it) */
    }
    r.layers.push_back(user);
    for (int i = 0; i < (int)r.layers.size(); i++)
        for (const std::string& e : r.layers[i].file.errors)
            r.problems.push_back(Problem { i, atoi(e.c_str() + 5), "", e });
    int n = (int)r.layers.size();
    r.base = merge(r, n - 1, &r.problems, &r.provenance, nullptr, &r.deleted);
    std::vector<Problem> top;
    r.loaded = merge(r, n, &top, nullptr, &r.user_dangling, nullptr);
    for (const Problem& p : top)
        if (p.layer == n - 1)
            r.problems.push_back(p);
}

bool changed_on_disk(const Room& r)
{
    for (const Layer& l : r.layers) {
        if (l.kind == USER && !l.legacy.empty() && l.file.exists && l.file.path == l.legacy) {
            /* Reading the old file: a new one appearing, or the old one changing. */
            if (file_mtime(l.path) || file_mtime(l.legacy) != l.file.mtime)
                return true;
            continue;
        }
        if (file_mtime(l.path) != l.file.mtime)
            return true;
    }
    return false;
}

std::string user_layer_text(const Room& r, const State& current)
{
    std::vector<Entry> entries = diff(r.base, current);
    for (const Entry& e : r.user_dangling)
        entries.push_back(e);
    if (entries.empty())
        return std::string();
    char comment[128];
    snprintf(comment, sizeof comment, "zone %d room %d: your own lighting (untracked; see lighting/README.md)", r.zone,
        r.room);
    return format_file(entries, comment);
}

bool save_user(Room& r, const State& current, std::string* message)
{
    Layer& user = r.layers.back();
    std::string text = user_layer_text(r, current);
    std::string error;
    bool legacy_exists = !user.legacy.empty() && file_mtime(user.legacy);
    if (text.empty() && !legacy_exists) {
        unlink(user.path.c_str());
        if (message)
            *message = "Nothing differs from the packs: " + user.path + " removed";
    } else {
        if (text.empty())
            text = "rood-lighting 2\n# no changes (an old pc/lights file exists for this room and is no longer read)\n";
        if (!write_file(user.path, text, &error)) {
            if (message)
                *message = error;
            return false;
        }
        if (message)
            *message = "Saved to " + user.path;
    }
    read_file(user.path, user.file);
    r.loaded = current;
    return true;
}

bool promote(Room& r, const State& current, const std::vector<std::string>& ids, std::string* message)
{
    if (r.active < 0) {
        if (message)
            *message = "No lighting pack to promote to (settings key lighting_packs; ROOD_LIGHTING)";
        return false;
    }
    std::vector<Entry> user = diff(r.base, current);
    std::set<std::string> wanted(ids.begin(), ids.end());
    std::vector<std::string> chosen;
    for (const Entry& e : user)
        if (wanted.empty() || wanted.count(e.id))
            chosen.push_back(e.id);
    if (chosen.empty()) {
        if (message)
            *message = "Nothing to promote: the selection has no changes in the user layer";
        return false;
    }
    State below = merge(r, r.active);
    State at = merge(r, r.active + 1);
    for (const std::string& id : chosen) {
        auto c = current.find(id);
        if (c != current.end())
            at[id] = c->second;
        else
            at.erase(id);
    }
    std::vector<Entry> entries;
    for (const std::string& id : chosen) {
        Entry e;
        if (diff_one(below, at, id, e))
            entries.push_back(e);
    }
    Layer& layer = r.layers[r.active];
    bool ok;
    std::string old_text = read_all(layer.path, &ok);
    char comment[160];
    snprintf(comment, sizeof comment, "zone %d room %d: lighting pack '%s' (hand-authored; see lighting/README.md)",
        r.zone, r.room, layer.pack.c_str());
    std::string text = patch_text(ok ? old_text : std::string(), chosen, entries, comment);
    std::string error;
    if (!write_file(layer.path, text, &error)) {
        if (message)
            *message = error;
        return false;
    }
    /* The layers again (the base changed), then the user layer without them. */
    Room again;
    load_room(again, r.zone, r.room);
    again.user_dangling = r.user_dangling;
    std::string saved;
    r = again;
    save_user(r, current, &saved);
    int still = 0;
    for (const std::string& id : chosen) {
        auto b = r.base.find(id);
        auto c = current.find(id);
        bool same = (b == r.base.end()) == (c == current.end()) && (b == r.base.end() || same_item(b->second, c->second));
        still += !same;
    }
    if (message) {
        char text_out[512];
        snprintf(text_out, sizeof text_out, "Promoted %zu change%s to %s%s", chosen.size(), chosen.size() == 1 ? "" : "s",
            layer.path.c_str(),
            still ? " (a higher pack still changes some of them: they stay in the user layer)" : "");
        *message = text_out;
    }
    return true;
}

std::string badge(const Room& r, const State& current, const std::string& id)
{
    std::string s;
    auto p = r.provenance.find(id);
    auto b = r.base.find(id);
    if (p != r.provenance.end() && p->second.defined >= 0) {
        s += r.layers[p->second.defined].badge();
        for (int i : p->second.changed)
            if (s.back() != r.layers[i].badge())
                s += std::string(">") + r.layers[i].badge();
    }
    auto c = current.find(id);
    if (c == current.end())
        return s;
    if (b == r.base.end())
        return "U";
    if (!same_item(b->second, c->second))
        s += ">U";
    return s;
}

/* ---- conversions ---- */
namespace {
std::string vec(const float* v, int n, int decimals)
{
    std::vector<double> d(v, v + n);
    return format_numbers(d, decimals);
}
bool get_vec(const Item& it, const char* key, float* out, int n)
{
    const std::string* v = it.get(key);
    std::string text = v ? *v : field_default(it.kind, key);
    std::vector<double> d;
    if (!parse_numbers(text, d) || (int)d.size() != n)
        return false;
    for (int i = 0; i < n; i++)
        out[i] = (float)d[i];
    return true;
}
double get_num(const Item& it, const char* key, double fallback)
{
    float v;
    return get_vec(it, key, &v, 1) ? v : fallback;
}
} // namespace

Item item_from_light(const rood_lights::Light& l)
{
    Item it;
    it.kind = "light";
    it.name = l.id;
    if (!l.name.empty())
        it.set("name", l.name);
    it.set("type", l.type == 1 ? "spot" : l.type == 2 ? "rect" : "point");
    it.set("pos", vec(l.position, 3, 1));
    it.set("dir", vec(l.direction, 3, 4));
    it.set("color", vec(l.color, 3, 3));
    it.set("intensity", format_number(l.intensity, 3));
    it.set("radius", format_number(l.radius, 1));
    it.set("cone", format_number(l.cone, 2));
    it.set("size", vec(l.size, 2, 1));
    it.set("shadows", l.shadows ? "1" : "0");
    it.set("enabled", l.enabled ? "1" : "0");
    canonicalize(it);
    return it;
}

bool light_from_item(const Item& it, rood_lights::Light& l)
{
    if (it.kind != "light")
        return false;
    l = rood_lights::Light();
    l.id = it.name;
    if (const std::string* n = it.get("name"))
        l.name = *n;
    const std::string* type = it.get("type");
    l.type = !type || *type == "point" ? 0 : *type == "spot" ? 1 : *type == "rect" ? 2 : 0;
    bool ok = get_vec(it, "pos", l.position, 3);
    get_vec(it, "dir", l.direction, 3);
    get_vec(it, "color", l.color, 3);
    l.intensity = (float)get_num(it, "intensity", 1);
    l.radius = (float)get_num(it, "radius", 600);
    l.cone = (float)get_num(it, "cone", 45);
    get_vec(it, "size", l.size, 2);
    l.shadows = get_num(it, "shadows", 0) != 0;
    l.enabled = get_num(it, "enabled", 1) != 0;
    return ok;
}

Item item_from_actor(const prefab::Actor& a)
{
    Item it;
    it.kind = "actor";
    it.name = a.id;
    if (!a.name.empty())
        it.set("name", a.name);
    it.set("prefab", a.prefab);
    it.set("pos", vec(a.position, 3, 1));
    it.set("rot", vec(a.rotation, 3, 2));
    it.set("seed", std::to_string(a.seed));
    it.set("enabled", a.enabled ? "1" : "0");
    if (a.glow_linked) {
        it.set("glow", vec(a.glow, 3, 1));
        if (!a.glow_key.empty())
            it.set("glowkey", a.glow_key);
    }
    if (a.fixture) {
        it.set("fixture", vec(a.fixture_at, 3, 1));
        if (a.fixture_radius_set || a.fixture_type.empty()) /* else its fixture type's */
            it.set("fixture_radius", format_number(a.fixture_radius, 1));
        it.set("fixture_shadows", a.fixture_shadows ? "1" : "0");
    }
    if (!a.fixture_type.empty()) {
        it.set("fixture_type", a.fixture_type);
        it.set("flame", std::to_string(a.flame));
    }
    for (const auto& o : a.overrides) {
        if (field_spec("actor", o.first))
            continue; /* (a parameter named like an actor field cannot be stored) */
        it.set(o.first, vec(o.second.v, o.second.n == 3 ? 3 : 1, 4));
    }
    canonicalize(it);
    return it;
}

bool actor_from_item(const Item& it, prefab::Actor& a)
{
    if (it.kind != "actor")
        return false;
    a = prefab::Actor();
    a.id = it.name;
    const std::string* p = it.get("prefab");
    if (!p || p->empty())
        return false;
    a.prefab = *p;
    if (const std::string* n = it.get("name"))
        a.name = *n;
    bool ok = get_vec(it, "pos", a.position, 3);
    get_vec(it, "rot", a.rotation, 3);
    a.seed = (uint32_t)get_num(it, "seed", 1);
    a.enabled = get_num(it, "enabled", 1) != 0;
    if (const std::string* k = it.get("glowkey"))
        a.glow_key = *k, a.glow_linked = true;
    if (get_vec(it, "glow", a.glow, 3))
        a.glow_linked = true;
    if (get_vec(it, "fixture", a.fixture_at, 3)) {
        a.fixture = true;
        a.fixture_radius = (float)get_num(it, "fixture_radius", 16);
        a.fixture_radius_set = it.get("fixture_radius") != nullptr;
        a.fixture_shadows = get_num(it, "fixture_shadows", 0) != 0;
    }
    if (const std::string* t = it.get("fixture_type"))
        a.fixture_type = *t;
    a.flame = (int)get_num(it, "flame", 0);
    for (const auto& f : it.fields) {
        if (field_spec("actor", f.first))
            continue;
        std::vector<double> d;
        if (!parse_numbers(f.second, d) || (d.size() != 1 && d.size() != 3))
            continue;
        prefab::Value v = d.size() == 3 ? prefab::Value::vec((float)d[0], (float)d[1], (float)d[2])
                                        : prefab::Value::scalar((float)d[0]);
        a.overrides.emplace_back(f.first, v);
    }
    return ok;
}

void legacy_items(const std::string& text, std::vector<Item>& out)
{
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        const char* c = line.c_str();
        char key[64];
        float v;
        prefab::Actor a;
        if (sscanf(c, "set %63s %f", key, &v) == 2) {
            Item it;
            it.kind = "set";
            it.name = key;
            it.set("value", format_number(v, 6));
            canonicalize(it);
            out.push_back(it);
            continue;
        }
        int index, freeze;
        float speed;
        if (sscanf(c, "uv_effect %d %f %d", &index, &speed, &freeze) == 3) {
            Item it;
            it.kind = "uv";
            it.name = std::to_string(index);
            it.set("speed", format_number(speed, 3));
            it.set("freeze", std::to_string(freeze));
            canonicalize(it);
            out.push_back(it);
            continue;
        }
        float g[9];
        int muted;
        if (sscanf(c, "glow %f %f %f %d %f %f %f %f %f %f", &g[0], &g[1], &g[2], &muted, &g[3], &g[4], &g[5], &g[6],
                &g[7], &g[8])
            == 10) {
            Item it;
            it.kind = "glow";
            char k[64] = "";
            if (const char* kp = strstr(c, " key="))
                sscanf(kp + 5, "%63s", k);
            it.name = k[0] ? std::string(k) : "@" + vec(g, 3, 1);
            it.set("pos", vec(g, 3, 1));
            it.set("muted", std::to_string(muted));
            it.set("intensity", format_number(g[3], 3));
            it.set("tint", vec(g + 4, 3, 3));
            it.set("size", format_number(g[7], 2));
            it.set("reach", format_number(g[8], 1));
            char t[16] = "";
            if (const char* tp = strstr(c, " fixture_type="))
                sscanf(tp + 14, "%15s", t);
            if (t[0])
                it.set("fixture_type", t);
            char shape[16] = "";
            if (const char* sp = strstr(c, " shape="))
                sscanf(sp + 7, "%15s", shape);
            if (shape[0])
                it.set("shape", shape); /* glows.c: sphere, column (auto: as found) */
            canonicalize(it);
            out.push_back(it);
            continue;
        }
        if (prefab::actor_parse(c, a)) {
            a.id = new_name();
            out.push_back(item_from_actor(a));
            continue;
        }
        rood_lights::Light l;
        char type[16];
        int shadows = 0, enabled = 1;
        if (sscanf(c, "light %15s %f %f %f %f %f %f %f %f %f %f %f %f %d %d %f %f", type, &l.position[0],
                &l.position[1], &l.position[2], &l.color[0], &l.color[1], &l.color[2], &l.intensity, &l.radius,
                &l.direction[0], &l.direction[1], &l.direction[2], &l.cone, &shadows, &enabled, &l.size[0], &l.size[1])
            >= 9) {
            l.type = !strcmp(type, "spot") ? 1 : !strcmp(type, "rect") ? 2 : 0;
            l.shadows = shadows != 0;
            l.enabled = enabled != 0;
            l.id = new_name();
            out.push_back(item_from_light(l));
        }
    }
}

std::string schema_json()
{
    std::string s = "{";
    bool first_kind = true;
    for (const KindSpec& k : kinds()) {
        s += std::string(first_kind ? "" : ",") + "\"" + k.kind + "\":{\"open\":" + (k.open ? "true" : "false")
            + ",\"other_decimals\":" + std::to_string(k.other_decimals) + ",\"fields\":[";
        first_kind = false;
        for (size_t i = 0; i < k.fields.size(); i++) {
            const FieldSpec& f = k.fields[i];
            s += std::string(i ? "," : "") + "[\"" + f.key + "\"," + std::to_string(f.decimals) + ","
                + (f.def ? "\"" + std::string(f.def) + "\"" : std::string("null")) + "," + (f.vol ? "true" : "false")
                + "]";
        }
        s += "]}";
    }
    return s + "}";
}

} // namespace lighting
