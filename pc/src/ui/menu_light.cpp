// The menu model's studio light as data (menu_light.h): the fields of the
// theme file's "menu_model_light" block, their ranges, the live rig, saving
// it into the theme file, and RPC graphics menu_light.
#include "ui/menu_light.h"
#include "ui/menu_light_fields.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "light_prefabs.h"
#include "platform.h"
#include "ui/ui_theme.h"

namespace ui {

namespace {
#define F(member) offsetof(pc_menu_light, member)
const MenuLightField FIELDS[] = {
    { "key_direction", "key", "direction", MenuLightField::DIRECTION, F(key_dir), -1, 1 },
    { "key_color", "key", "color", MenuLightField::COLOUR, F(key_color), 0, 4 },
    { "key_intensity", "key", "intensity", MenuLightField::SCALAR, F(key_intensity), 0, 4 },
    { "key_wrap", "key", "wrap", MenuLightField::SCALAR, F(key_wrap), 0, 1 },
    { "fill_direction", "fill", "direction", MenuLightField::DIRECTION, F(fill_dir), -1, 1 },
    { "fill_color", "fill", "color", MenuLightField::COLOUR, F(fill_color), 0, 4 },
    { "fill_intensity", "fill", "intensity", MenuLightField::SCALAR, F(fill_intensity), 0, 4 },
    { "fill_wrap", "fill", "wrap", MenuLightField::SCALAR, F(fill_wrap), 0, 1 },
    { "rim_direction", "rim", "direction", MenuLightField::DIRECTION, F(rim_dir), -1, 1 },
    { "rim_color", "rim", "color", MenuLightField::COLOUR, F(rim_color), 0, 4 },
    { "rim_intensity", "rim", "intensity", MenuLightField::SCALAR, F(rim_intensity), 0, 4 },
    { "rim_wrap", "rim", "wrap", MenuLightField::SCALAR, F(rim_wrap), 0, 1 },
    { "rim_power", "rim", "power", MenuLightField::SCALAR, F(rim_power), 0.25f, 16 },
    { "rim_lift", "rim", "lift", MenuLightField::SCALAR, F(rim_lift), 0, 1 },
    { "ambient_sky", "ambient", "sky", MenuLightField::COLOUR, F(ambient_sky), 0, 4 },
    { "ambient_ground", "ambient", "ground", MenuLightField::COLOUR, F(ambient_ground), 0, 4 },
    { "ambient_intensity", "ambient", "intensity", MenuLightField::SCALAR, F(ambient_intensity), 0, 4 },
    { "exposure", "", "exposure", MenuLightField::SCALAR, F(exposure), 0, 8 },
    { "saturation", "", "saturation", MenuLightField::SCALAR, F(saturation), 0, 2 },
};
#undef F
const char* const LIGHTS[3] = { "key", "fill", "rim" };

float clampf(float x, float lo, float hi) { return std::fmin(std::fmax(x, lo), hi); }
bool finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
float length3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// A JSON number, or an array of three, as floats; false when it is not.
bool numbers(const prefab::Json& j, int n, float* out)
{
    if (n == 1) {
        if (j.type != prefab::Json::Number || !std::isfinite(j.number))
            return false;
        out[0] = (float)j.number;
        return true;
    }
    if (j.type != prefab::Json::Array || (int)j.items.size() != n)
        return false;
    for (int i = 0; i < n; i++) {
        if (j.items[i].type != prefab::Json::Number || !std::isfinite(j.items[i].number))
            return false;
        out[i] = (float)j.items[i].number;
    }
    return true;
}

// A value for field `f` from JSON, clamped to its range; false (and `error`) when the JSON is wrong.
bool take(const MenuLightField& f, const prefab::Json& j, pc_menu_light& l, const std::string& where, std::string* error)
{
    float v[3];
    int n = f.components();
    if (!numbers(j, n, v)) {
        if (error)
            *error = where + ": expected " + (n == 1 ? "a number" : "[x, y, z]");
        return false;
    }
    if (f.type == MenuLightField::DIRECTION && length3(v) < 1e-6f) {
        if (error)
            *error = where + ": a direction needs a length";
        return false;
    }
    float* dst = f.at(l);
    for (int i = 0; i < n; i++)
        dst[i] = clampf(v[i], f.min, f.max);
    return true;
}

// The shortest text a float reads back from exactly.
std::string number_text(float x)
{
    char text[32];
    for (int digits = 6; digits <= 9; digits++) {
        std::snprintf(text, sizeof text, "%.*g", digits, (double)x);
        if ((float)std::strtod(text, nullptr) == x)
            break;
    }
    return text;
}
std::string value_text(const MenuLightField& f, const pc_menu_light& l)
{
    const float* v = f.at(l);
    if (f.components() == 1)
        return number_text(v[0]);
    return "[" + number_text(v[0]) + ", " + number_text(v[1]) + ", " + number_text(v[2]) + "]";
}

// The live rig: the theme file's until edited, the file's again when it changes on disk.
pc_menu_light live;
int live_serial = -1;
const pc_menu_light& current(const pc_menu_light** file = nullptr)
{
    int serial;
    const Theme& t = theme_file(&serial);
    if (serial != live_serial) {
        live = t.menu_light;
        live_serial = serial;
    }
    if (file)
        *file = &t.menu_light;
    return live;
}
} // namespace

int MenuLightField::components() const { return type == SCALAR ? 1 : 3; }
float* MenuLightField::at(pc_menu_light& l) const { return (float*)((char*)&l + offset); }
const float* MenuLightField::at(const pc_menu_light& l) const { return (const float*)((const char*)&l + offset); }

const MenuLightField* menu_light_fields(int* count)
{
    *count = (int)(sizeof FIELDS / sizeof FIELDS[0]);
    return FIELDS;
}
const MenuLightField* menu_light_field(const char* name)
{
    for (const MenuLightField& f : FIELDS)
        if (!std::strcmp(f.name, name))
            return &f;
    return nullptr;
}

void menu_light_angles(const float dir[3], float* azimuth, float* elevation)
{
    const float deg = 57.29577951f;
    *azimuth = std::atan2(dir[0], -dir[2]) * deg;
    *elevation = std::atan2(-dir[1], std::sqrt(dir[0] * dir[0] + dir[2] * dir[2])) * deg;
}
void menu_light_direction(float azimuth, float elevation, float dir[3])
{
    const float rad = 0.01745329252f;
    float a = azimuth * rad, e = clampf(elevation, -90, 90) * rad;
    dir[0] = std::cos(e) * std::sin(a);
    dir[1] = -std::sin(e);
    dir[2] = -std::cos(e) * std::cos(a);
}

void menu_light_clamp(pc_menu_light& l)
{
    pc_menu_light d;
    pc_ui_menu_model_light_defaults(&d);
    for (const MenuLightField& f : FIELDS) {
        float* v = f.at(l);
        const float* fallback = f.at(d);
        int n = f.components();
        if (f.type == MenuLightField::DIRECTION && (!finite3(v) || length3(v) < 1e-6f)) {
            std::memcpy(v, fallback, sizeof(float) * 3);
            continue;
        }
        for (int i = 0; i < n; i++)
            v[i] = std::isfinite(v[i]) ? clampf(v[i], f.min, f.max) : fallback[i];
    }
}

bool menu_light_parse(const prefab::Json& block, pc_menu_light& l, std::string* error)
{
    const std::string root = "menu_model_light";
    if (block.type != prefab::Json::Object) {
        if (error)
            *error = root + ": expected an object";
        return false;
    }
    for (const char* group : { "key", "fill", "rim", "ambient" })
        if (const prefab::Json* g = block.get(group))
            if (g->type != prefab::Json::Object) {
                if (error)
                    *error = root + "." + group + ": expected an object";
                return false;
            }
    for (const MenuLightField& f : FIELDS) {
        const prefab::Json* g = f.group[0] ? block.get(f.group) : &block;
        const prefab::Json* v = g ? g->get(f.key) : nullptr;
        std::string where = root + "." + (f.group[0] ? std::string(f.group) + "." : std::string()) + f.key;
        if (v) {
            if (!take(f, *v, l, where, error))
                return false;
        } else if (f.type == MenuLightField::DIRECTION && g) {
            // or azimuth / elevation in degrees (either missing: the default direction's)
            const prefab::Json* az = g->get("azimuth");
            const prefab::Json* el = g->get("elevation");
            if (az || el) {
                float a, e, dir[3];
                menu_light_angles(f.at(l), &a, &e);
                if ((az && !numbers(*az, 1, &a)) || (el && !numbers(*el, 1, &e))) {
                    if (error)
                        *error = root + "." + f.group + ": azimuth and elevation are numbers (degrees)";
                    return false;
                }
                menu_light_direction(a, clampf(e, -90, 90), dir);
                std::memcpy(f.at(l), dir, sizeof dir);
            }
        }
    }
    return true;
}

std::string menu_light_block(const pc_menu_light& l, const char* indent)
{
    std::string in = indent, s = "{\n";
    for (const char* group : { "key", "fill", "rim", "ambient" }) {
        s += in + "  \"" + group + "\": { ";
        bool first = true;
        for (const MenuLightField& f : FIELDS)
            if (!std::strcmp(f.group, group)) {
                s += std::string(first ? "" : ", ") + "\"" + f.key + "\": " + value_text(f, l);
                first = false;
            }
        s += " },\n";
    }
    s += in + "  \"exposure\": " + value_text(*menu_light_field("exposure"), l) + ", \"saturation\": "
        + value_text(*menu_light_field("saturation"), l) + "\n" + in + "}";
    return s;
}

namespace {
// Where a JSON value starting at text[i] ends (just past it); npos when the text ends first.
size_t value_end(const std::string& text, size_t i)
{
    int depth = 0;
    bool in_string = false;
    for (; i < text.size(); i++) {
        char c = text[i];
        if (in_string) {
            if (c == '\\')
                i++;
            else if (c == '"') {
                in_string = false;
                if (!depth)
                    return i + 1;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (--depth <= 0)
                return depth < 0 ? i : i + 1;
        } else if (!depth && (c == ',' || std::isspace((unsigned char)c))) {
            return i;
        }
    }
    return std::string::npos;
}
size_t skip_space(const std::string& text, size_t i)
{
    while (i < text.size() && std::isspace((unsigned char)text[i]))
        i++;
    return i;
}
} // namespace

bool menu_light_replace_block(const std::string& text, const pc_menu_light& l, std::string& out, std::string* error)
{
    prefab::Json root;
    if (!prefab::json_parse(text.c_str(), root, error))
        return false;
    if (root.type != prefab::Json::Object) {
        if (error)
            *error = "the theme is not a JSON object";
        return false;
    }
    // the root's members, in order: find "menu_model_light"'s value
    size_t i = skip_space(text, 0) + 1, last_end = std::string::npos;
    while (true) {
        i = skip_space(text, i);
        if (i >= text.size() || text[i] == '}')
            break;
        size_t key_end = value_end(text, i);
        std::string key = text.substr(i + 1, key_end - i - 2);
        i = skip_space(text, key_end);
        i = skip_space(text, i + 1); /* past ':' */
        size_t end = value_end(text, i);
        if (end == std::string::npos)
            break;
        if (key == "menu_model_light") {
            out = text.substr(0, i) + menu_light_block(l, "  ") + text.substr(end);
            return true;
        }
        last_end = end;
        i = skip_space(text, end);
        if (i < text.size() && text[i] == ',')
            i++;
    }
    // not there: a new last member
    size_t close = text.rfind('}');
    std::string block = "\"menu_model_light\": " + menu_light_block(l, "  ");
    if (last_end == std::string::npos)
        out = text.substr(0, close) + "\n  " + block + "\n" + text.substr(close);
    else
        out = text.substr(0, last_end) + ",\n  " + block + text.substr(last_end);
    return true;
}

} // namespace ui

extern "C" int pc_ui_menu_model_light(pc_menu_light* out)
{
    *out = ui::current();
    return 1;
}

extern "C" void pc_ui_menu_model_light_set(const pc_menu_light* in)
{
    ui::current();
    ui::live = *in;
    ui::menu_light_clamp(ui::live);
}

// Whether the live rig differs from the theme file's (unsaved edits).
bool ui::menu_light_edited()
{
    const pc_menu_light* file;
    const pc_menu_light& l = current(&file);
    return std::memcmp(&l, file, sizeof l) != 0;
}

extern "C" int pc_ui_menu_model_light_save(char* error, int size)
{
    auto fail = [&](const std::string& message) {
        if (error && size > 0)
            std::snprintf(error, size, "%s", message.c_str());
        return -1;
    };
    const char* path = ui::theme_path();
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return fail(std::string(path) + ": cannot read it");
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    std::fclose(f);
    std::string out, why;
    pc_menu_light l = ui::current();
    if (!ui::menu_light_replace_block(text, l, out, &why))
        return fail(std::string(path) + ": " + why);
    std::string temp = std::string(path) + ".tmp";
    FILE* w = std::fopen(temp.c_str(), "wb");
    bool ok = w && std::fwrite(out.data(), 1, out.size(), w) == out.size();
    if (w)
        ok = std::fclose(w) == 0 && ok;
    if (!ok || pc_replace_file(temp.c_str(), path)) {
        std::remove(temp.c_str());
        return fail(std::string(path) + ": cannot write it");
    }
    return 0;
}

extern "C" int pc_ui_menu_light_rpc(const char* value, char* out, int size)
{
    auto fail = [&](const std::string& message) {
        std::snprintf(out, size, "%s", message.c_str());
        return -1;
    };
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(value && *value ? value : "true", params, &error))
        return fail("menu_light: " + error);
    std::string saved;
    if (params.type == prefab::Json::String && params.string == "reset") {
        pc_menu_light d;
        pc_ui_menu_model_light_defaults(&d);
        pc_ui_menu_model_light_set(&d);
    } else if (params.type == prefab::Json::String && params.string == "save") {
        char why[256];
        if (pc_ui_menu_model_light_save(why, sizeof why))
            return fail(std::string("menu_light save: ") + why);
        saved = ui::theme_path();
    } else if (params.type == prefab::Json::Object) {
        pc_menu_light l = ui::current();
        for (const auto& m : params.members) {
            const ui::MenuLightField* f = ui::menu_light_field(m.first.c_str());
            if (f) {
                if (!ui::take(*f, m.second, l, "menu_light." + m.first, &error))
                    return fail(error);
                continue;
            }
            // <light>_azimuth / <light>_elevation (degrees)
            bool angle = false;
            for (int i = 0; i < 3 && !angle; i++) {
                std::string light = ui::LIGHTS[i];
                bool az = m.first == light + "_azimuth", el = m.first == light + "_elevation";
                if (!az && !el)
                    continue;
                angle = true;
                float x;
                if (!ui::numbers(m.second, 1, &x))
                    return fail("menu_light." + m.first + ": expected degrees");
                float* dir = ui::menu_light_field((light + "_direction").c_str())->at(l);
                float a, e;
                ui::menu_light_angles(dir, &a, &e);
                ui::menu_light_direction(az ? x : a, el ? x : e, dir);
            }
            if (!angle)
                return fail("menu_light: no field " + m.first);
        }
        pc_ui_menu_model_light_set(&l);
    }
    const pc_menu_light& l = ui::current();
    std::string s = "{";
    int count;
    const ui::MenuLightField* fields = ui::menu_light_fields(&count);
    for (int i = 0; i < count; i++) {
        std::string v = ui::value_text(fields[i], l);
        s += std::string(i ? "," : "") + "\"" + fields[i].name + "\":" + v;
    }
    for (int i = 0; i < 3; i++) {
        float a, e;
        ui::menu_light_angles(ui::menu_light_field((std::string(ui::LIGHTS[i]) + "_direction").c_str())->at(l), &a, &e);
        s += ",\"" + std::string(ui::LIGHTS[i]) + "_azimuth\":" + ui::number_text(a) + ",\"" + ui::LIGHTS[i]
            + "_elevation\":" + ui::number_text(e);
    }
    s += std::string(",\"edited\":") + (ui::menu_light_edited() ? "true" : "false") + ",\"theme\":"
        + prefab::json_quote(ui::theme_path());
    if (!saved.empty())
        s += ",\"saved\":true";
    s += "}";
    if ((int)s.size() >= size)
        return fail("menu_light: the reply does not fit");
    std::snprintf(out, size, "%s", s.c_str());
    return 0;
}
