// Light actors (prefabs): JSON, expressions, the prefab library, actor
// instances and their per-frame evaluation. See light_prefabs.h.
#include "light_prefabs.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <set>
#include "platform.h"

namespace prefab {

/* ---------------------------------------------------------------- JSON */

const Json* Json::get(const char* key) const
{
    if (type != Object)
        return nullptr;
    for (const auto& m : members)
        if (m.first == key)
            return &m.second;
    return nullptr;
}
Json* Json::get(const char* key)
{
    return const_cast<Json*>(static_cast<const Json*>(this)->get(key));
}
Json& Json::set(const char* key, const Json& value)
{
    if (type != Object) {
        *this = object();
    }
    for (auto& m : members)
        if (m.first == key) {
            m.second = value;
            return m.second;
        }
    members.emplace_back(key, value);
    return members.back().second;
}
void Json::remove(const char* key)
{
    for (size_t i = 0; i < members.size(); i++)
        if (members[i].first == key) {
            members.erase(members.begin() + (long)i);
            return;
        }
}
Json Json::num(double v)
{
    Json j;
    j.type = Number;
    j.number = v;
    return j;
}
Json Json::str(const std::string& s)
{
    Json j;
    j.type = String;
    j.string = s;
    return j;
}
Json Json::boolean_(bool b)
{
    Json j;
    j.type = Bool;
    j.boolean = b;
    return j;
}
Json Json::array()
{
    Json j;
    j.type = Array;
    return j;
}
Json Json::object()
{
    Json j;
    j.type = Object;
    return j;
}

namespace {
struct Parser {
    const char* p;
    const char* start;
    std::string error;
    void fail(const char* what)
    {
        if (error.empty()) {
            int line = 1;
            for (const char* q = start; q < p; q++)
                line += *q == '\n';
            char text[160];
            snprintf(text, sizeof text, "line %d: %s", line, what);
            error = text;
        }
    }
    void space()
    {
        for (;;) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
                ++p;
            if (p[0] == '/' && p[1] == '/') { /* comments: an extension for hand-edited files */
                while (*p && *p != '\n')
                    ++p;
                continue;
            }
            return;
        }
    }
    bool string(std::string& out)
    {
        if (*p != '"') {
            fail("expected a string");
            return false;
        }
        ++p;
        out.clear();
        while (*p && *p != '"') {
            if (*p == '\\') {
                ++p;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned c = 0;
                    for (int i = 1; i <= 4; i++) {
                        char h = p[i];
                        c = c * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                    : h >= 'A' && h <= 'F'              ? h - 'A' + 10
                                                                        : 0);
                    }
                    p += 4;
                    if (c < 0x80)
                        out += (char)c;
                    else if (c < 0x800)
                        out += (char)(0xC0 | (c >> 6)), out += (char)(0x80 | (c & 63));
                    else
                        out += (char)(0xE0 | (c >> 12)), out += (char)(0x80 | ((c >> 6) & 63)),
                            out += (char)(0x80 | (c & 63));
                    break;
                }
                case 0: fail("unterminated string"); return false;
                default: out += *p; break;
                }
                ++p;
            } else {
                out += *p++;
            }
        }
        if (*p != '"') {
            fail("unterminated string");
            return false;
        }
        ++p;
        return true;
    }
    bool value(Json& out, int depth)
    {
        if (depth > 64) {
            fail("nested too deeply");
            return false;
        }
        space();
        if (*p == '{') {
            ++p;
            out = Json::object();
            space();
            if (*p == '}') {
                ++p;
                return true;
            }
            for (;;) {
                space();
                std::string key;
                if (!string(key))
                    return false;
                space();
                if (*p != ':') {
                    fail("expected ':'");
                    return false;
                }
                ++p;
                Json v;
                if (!value(v, depth + 1))
                    return false;
                out.members.emplace_back(key, v);
                space();
                if (*p == ',') {
                    ++p;
                    space();
                    if (*p == '}') { /* trailing comma (hand-edited files) */
                        ++p;
                        return true;
                    }
                    continue;
                }
                if (*p == '}') {
                    ++p;
                    return true;
                }
                fail("expected ',' or '}'");
                return false;
            }
        }
        if (*p == '[') {
            ++p;
            out = Json::array();
            space();
            if (*p == ']') {
                ++p;
                return true;
            }
            for (;;) {
                Json v;
                if (!value(v, depth + 1))
                    return false;
                out.items.push_back(v);
                space();
                if (*p == ',') {
                    ++p;
                    space();
                    if (*p == ']') {
                        ++p;
                        return true;
                    }
                    continue;
                }
                if (*p == ']') {
                    ++p;
                    return true;
                }
                fail("expected ',' or ']'");
                return false;
            }
        }
        if (*p == '"') {
            out.type = Json::String;
            return string(out.string);
        }
        if (!strncmp(p, "true", 4)) {
            p += 4;
            out = Json::boolean_(true);
            return true;
        }
        if (!strncmp(p, "false", 5)) {
            p += 5;
            out = Json::boolean_(false);
            return true;
        }
        if (!strncmp(p, "null", 4)) {
            p += 4;
            out = Json();
            return true;
        }
        char* end;
        double v = strtod(p, &end);
        if (end == p) {
            fail("unexpected character");
            return false;
        }
        p = end;
        out = Json::num(v);
        return true;
    }
};
} // namespace

bool json_parse(const char* text, Json& out, std::string* error)
{
    Parser parser { text, text, {} };
    bool ok = parser.value(out, 0);
    if (ok) {
        parser.space();
        if (*parser.p) {
            parser.fail("text after the value");
            ok = false;
        }
    }
    if (!ok && error)
        *error = parser.error;
    return ok;
}

std::string json_quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\', out += c;
        else if (c == '\n')
            out += "\\n";
        else if (c == '\t')
            out += "\\t";
        else if ((unsigned char)c < 0x20) {
            char u[8];
            snprintf(u, sizeof u, "\\u%04x", c);
            out += u;
        } else
            out += c;
    }
    return out + "\"";
}

static std::string number_text(double v)
{
    char text[32];
    if (v == floor(v) && fabs(v) < 1e9)
        snprintf(text, sizeof text, "%.0f", v);
    else
        snprintf(text, sizeof text, "%.6g", v);
    return text;
}

static bool inline_value(const Json& v)
{
    if (v.type == Json::Array) {
        if (v.items.size() > 4)
            return false;
        for (const Json& i : v.items)
            if (i.type == Json::Array || i.type == Json::Object || (i.type == Json::String && i.string.size() > 24))
                return false;
        return true;
    }
    return v.type != Json::Object;
}

std::string json_write(const Json& value, int indent)
{
    std::string pad(indent * 2, ' '), inner((indent + 1) * 2, ' ');
    switch (value.type) {
    case Json::Null: return "null";
    case Json::Bool: return value.boolean ? "true" : "false";
    case Json::Number: return number_text(value.number);
    case Json::String: return json_quote(value.string);
    case Json::Array: {
        if (value.items.empty())
            return "[]";
        if (inline_value(value)) {
            std::string out = "[";
            for (size_t i = 0; i < value.items.size(); i++)
                out += (i ? ", " : "") + json_write(value.items[i], 0);
            return out + "]";
        }
        std::string out = "[\n";
        for (size_t i = 0; i < value.items.size(); i++)
            out += inner + json_write(value.items[i], indent + 1) + (i + 1 < value.items.size() ? ",\n" : "\n");
        return out + pad + "]";
    }
    case Json::Object: {
        if (value.members.empty())
            return "{}";
        /* Small objects of plain values on one line (params, placement). */
        bool flat = value.members.size() <= 8;
        size_t width = 0;
        for (const auto& m : value.members) {
            flat &= inline_value(m.second);
            width += m.first.size() + 8 + (m.second.type == Json::String ? m.second.string.size() : 8);
        }
        if (flat && width < 150 && indent > 0) {
            std::string out = "{ ";
            for (size_t i = 0; i < value.members.size(); i++)
                out += (i ? ", " : "") + json_quote(value.members[i].first) + ": " + json_write(value.members[i].second, 0);
            return out + " }";
        }
        std::string out = "{\n";
        for (size_t i = 0; i < value.members.size(); i++)
            out += inner + json_quote(value.members[i].first) + ": " + json_write(value.members[i].second, indent + 1)
                + (i + 1 < value.members.size() ? ",\n" : "\n");
        return out + pad + "}";
    }
    }
    return "null";
}

/* ---------------------------------------------------------- expressions */

Value Value::scalar(float x)
{
    Value v;
    v.v[0] = v.v[1] = v.v[2] = x;
    v.n = 1;
    return v;
}
Value Value::vec(float x, float y, float z)
{
    Value v;
    v.v[0] = x, v.v[1] = y, v.v[2] = z;
    v.n = 3;
    return v;
}

struct Expr {
    enum Kind { Const, Var, Neg, Add, Sub, Mul, Div, Call } kind = Const;
    Value value;
    std::string name;
    std::vector<ExprPtr> args;
};

namespace {
struct ExprParser {
    const char* p;
    std::string error;
    void space()
    {
        while (*p == ' ' || *p == '\t')
            ++p;
    }
    ExprPtr fail(const char* what)
    {
        if (error.empty())
            error = what;
        return nullptr;
    }
    ExprPtr make(Expr::Kind kind, ExprPtr a, ExprPtr b = nullptr)
    {
        auto e = std::make_shared<Expr>();
        e->kind = kind;
        e->args.push_back(a);
        if (b)
            e->args.push_back(b);
        return e;
    }
    ExprPtr primary()
    {
        space();
        if (*p == '(') {
            ++p;
            ExprPtr e = sum();
            space();
            if (*p != ')')
                return fail("missing ')'");
            ++p;
            return e;
        }
        if (*p == '-') {
            ++p;
            ExprPtr e = primary();
            return e ? make(Expr::Neg, e) : nullptr;
        }
        if (*p == '+') {
            ++p;
            return primary();
        }
        if ((*p >= '0' && *p <= '9') || *p == '.') {
            char* end;
            double v = strtod(p, &end);
            if (end == p)
                return fail("bad number");
            p = end;
            return expr_constant(Value::scalar((float)v));
        }
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            const char* s = p;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
                ++p;
            auto e = std::make_shared<Expr>();
            e->name.assign(s, p);
            space();
            if (*p != '(') {
                e->kind = Expr::Var;
                return e;
            }
            ++p;
            e->kind = Expr::Call;
            space();
            if (*p != ')')
                for (;;) {
                    ExprPtr a = sum();
                    if (!a)
                        return nullptr;
                    e->args.push_back(a);
                    space();
                    if (*p == ',') {
                        ++p;
                        continue;
                    }
                    break;
                }
            space();
            if (*p != ')')
                return fail("missing ')' after arguments");
            ++p;
            static const struct {
                const char* name;
                int args;
            } known[] = { { "vec", 3 }, { "rgb", 3 }, { "kelvin", 1 }, { "min", 2 }, { "max", 2 }, { "clamp", 3 },
                { "mix", 3 }, { "select", 3 }, { "pow", 2 }, { "sqrt", 1 }, { "abs", 1 } };
            for (const auto& k : known)
                if (e->name == k.name) {
                    if ((int)e->args.size() != k.args) {
                        error = e->name + "() takes " + std::to_string(k.args) + " arguments";
                        return nullptr;
                    }
                    return e;
                }
            error = "unknown function " + e->name + "()";
            return nullptr;
        }
        return fail(*p ? "unexpected character" : "unexpected end");
    }
    ExprPtr product()
    {
        ExprPtr a = primary();
        while (a) {
            space();
            if (*p != '*' && *p != '/')
                break;
            Expr::Kind k = *p == '*' ? Expr::Mul : Expr::Div;
            ++p;
            ExprPtr b = primary();
            if (!b)
                return nullptr;
            a = make(k, a, b);
        }
        return a;
    }
    ExprPtr sum()
    {
        ExprPtr a = product();
        while (a) {
            space();
            if (*p != '+' && *p != '-')
                break;
            Expr::Kind k = *p == '+' ? Expr::Add : Expr::Sub;
            ++p;
            ExprPtr b = product();
            if (!b)
                return nullptr;
            a = make(k, a, b);
        }
        return a;
    }
};

void collect_names(const ExprPtr& e, std::set<std::string>& out)
{
    if (!e)
        return;
    if (e->kind == Expr::Var)
        out.insert(e->name);
    for (const ExprPtr& a : e->args)
        collect_names(a, out);
}
} // namespace

ExprPtr expr_parse(const std::string& text, std::string* error)
{
    ExprParser parser { text.c_str(), {} };
    ExprPtr e = parser.sum();
    if (e) {
        parser.space();
        if (*parser.p) {
            e = nullptr;
            parser.error = std::string("unexpected '") + *parser.p + "'";
        }
    }
    if (!e && error)
        *error = parser.error + " in \"" + text + "\"";
    return e;
}

ExprPtr expr_constant(Value v)
{
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Const;
    e->value = v;
    return e;
}

ExprPtr expr_from_json(const Json* field, std::string* error)
{
    if (!field || field->type == Json::Null)
        return nullptr;
    if (field->type == Json::Number)
        return expr_constant(Value::scalar((float)field->number));
    if (field->type == Json::Bool)
        return expr_constant(Value::scalar(field->boolean ? 1.0f : 0.0f));
    if (field->type == Json::String)
        return expr_parse(field->string, error);
    if (field->type == Json::Array && (field->items.size() == 3 || field->items.size() == 2)) {
        auto e = std::make_shared<Expr>();
        e->kind = Expr::Call;
        e->name = "vec";
        for (const Json& item : field->items) {
            ExprPtr a = expr_from_json(&item, error);
            if (!a)
                return nullptr;
            e->args.push_back(a);
        }
        if (e->args.size() == 2)
            e->args.push_back(expr_constant(Value::scalar(0)));
        return e;
    }
    if (error)
        *error = "expected a number, an expression or a list of three";
    return nullptr;
}

void kelvin(float temperature, float rgb[3])
{
    /* Blackbody colours (CIE 1931 through linear sRGB, brightest channel 1,
     * then sRGB-encoded: the renderer adds light in display space), every
     * 500 K from 1000 K to 12000 K. */
    static const float table[][3] = { { 1.000f, 0.185f, 0.000f }, { 1.000f, 0.417f, 0.000f }, { 1.000f, 0.553f, 0.084f },
        { 1.000f, 0.650f, 0.289f }, { 1.000f, 0.725f, 0.429f }, { 1.000f, 0.784f, 0.547f }, { 1.000f, 0.832f, 0.649f },
        { 1.000f, 0.871f, 0.737f }, { 1.000f, 0.904f, 0.815f }, { 1.000f, 0.932f, 0.883f }, { 1.000f, 0.955f, 0.943f },
        { 1.000f, 0.975f, 0.997f }, { 0.958f, 0.951f, 1.000f }, { 0.921f, 0.927f, 1.000f }, { 0.889f, 0.907f, 1.000f },
        { 0.863f, 0.891f, 1.000f }, { 0.840f, 0.876f, 1.000f }, { 0.821f, 0.863f, 1.000f }, { 0.804f, 0.852f, 1.000f },
        { 0.789f, 0.842f, 1.000f }, { 0.776f, 0.833f, 1.000f }, { 0.765f, 0.825f, 1.000f }, { 0.754f, 0.818f, 1.000f } };
    const int n = sizeof table / sizeof table[0];
    float x = (temperature - 1000.0f) / 500.0f;
    if (!(x > 0))
        x = 0;
    if (x > n - 1)
        x = (float)(n - 1);
    int i = (int)x;
    if (i >= n - 1)
        i = n - 2;
    float f = x - (float)i;
    for (int k = 0; k < 3; k++)
        rgb[k] = table[i][k] + (table[i + 1][k] - table[i][k]) * f;
}

namespace {
Value unary(const Value& a, float (*f)(float))
{
    Value r = a;
    for (int i = 0; i < 3; i++)
        r.v[i] = f(a.v[i]);
    return r;
}
Value binary(const Value& a, const Value& b, int op)
{
    Value r;
    r.n = a.n > b.n ? a.n : b.n;
    for (int i = 0; i < 3; i++) {
        float x = a.at(i), y = b.at(i);
        r.v[i] = op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : op == 3 ? (y != 0 ? x / y : 0)
            : op == 4                                                    ? fminf(x, y)
            : op == 5                                                    ? fmaxf(x, y)
                                                                         : powf(fmaxf(x, 0), y);
    }
    if (r.n == 1)
        r.v[1] = r.v[2] = r.v[0];
    return r;
}
Value eval(const Expr* e, const Lookup& lookup, int depth)
{
    if (!e || depth > 64)
        return Value();
    switch (e->kind) {
    case Expr::Const: return e->value;
    case Expr::Var: {
        Value v;
        if (!lookup || !lookup(e->name, v))
            return Value();
        return v;
    }
    case Expr::Neg: return binary(Value::scalar(0), eval(e->args[0].get(), lookup, depth + 1), 1);
    case Expr::Add:
    case Expr::Sub:
    case Expr::Mul:
    case Expr::Div:
        return binary(eval(e->args[0].get(), lookup, depth + 1), eval(e->args[1].get(), lookup, depth + 1),
            e->kind - Expr::Add);
    case Expr::Call: break;
    }
    Value a[3];
    for (size_t i = 0; i < e->args.size() && i < 3; i++)
        a[i] = eval(e->args[i].get(), lookup, depth + 1);
    const std::string& f = e->name;
    if (f == "vec")
        return Value::vec(a[0].x(), a[1].x(), a[2].x());
    if (f == "rgb")
        return Value::vec(a[0].x() / 255.0f, a[1].x() / 255.0f, a[2].x() / 255.0f);
    if (f == "kelvin") {
        Value r;
        r.n = 3;
        kelvin(a[0].x(), r.v);
        return r;
    }
    if (f == "min")
        return binary(a[0], a[1], 4);
    if (f == "max")
        return binary(a[0], a[1], 5);
    if (f == "pow")
        return binary(a[0], a[1], 6);
    if (f == "clamp")
        return binary(binary(a[0], a[1], 5), a[2], 4);
    if (f == "mix")
        return binary(a[0], binary(binary(a[1], a[0], 1), a[2], 2), 0);
    if (f == "select")
        return a[0].x() > 0.5f ? a[1] : a[2];
    if (f == "sqrt")
        return unary(a[0], [](float x) { return sqrtf(fmaxf(x, 0)); });
    if (f == "abs")
        return unary(a[0], [](float x) { return fabsf(x); });
    return Value();
}
} // namespace

Value expr_eval(const ExprPtr& e, const Lookup& lookup, Value fallback)
{
    return e ? eval(e.get(), lookup, 0) : fallback;
}

/* --------------------------------------------------------------- noise */

uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float noise1(uint32_t seed, double t)
{
    double cell = floor(t);
    float f = (float)(t - cell);
    uint32_t i = (uint32_t)(int64_t)cell;
    float a = (float)(hash32(seed ^ hash32(i)) >> 8) / 16777216.0f;
    float b = (float)(hash32(seed ^ hash32(i + 1)) >> 8) / 16777216.0f;
    float s = f * f * (3 - 2 * f);
    return a + (b - a) * s;
}

float flicker_noise(uint32_t seed, double t)
{
    return 0.55f * noise1(seed, t) + 0.3f * noise1(seed * 31 + 101, t * 2.37) + 0.15f * noise1(seed * 17 + 202, t * 5.11);
}

/* -------------------------------------------------------------- prefabs */

const Param* Prefab::param(const std::string& key) const
{
    for (const Param& p : params)
        if (p.key == key)
            return &p;
    return nullptr;
}

static float number_of(const Json* j, float fallback)
{
    if (!j)
        return fallback;
    if (j->type == Json::Number)
        return (float)j->number;
    if (j->type == Json::Bool)
        return j->boolean ? 1.0f : 0.0f;
    return fallback;
}
static std::string string_of(const Json* j, const std::string& fallback = "")
{
    return j && j->type == Json::String ? j->string : fallback;
}

Prefab prefab_from_json(const std::string& id, const Json& json)
{
    Prefab p;
    p.id = id;
    p.source = json;
    auto problem = [&](const std::string& what) {
        if (!p.error.empty())
            p.error += "; ";
        p.error += what;
    };
    if (json.type != Json::Object) {
        problem("not a JSON object");
        return p;
    }
    p.name = string_of(json.get("name"), id);
    p.category = string_of(json.get("category"), "Other");
    p.description = string_of(json.get("description"));
    p.icon = string_of(json.get("icon"));
    if (const Json* placement = json.get("placement")) {
        p.align = string_of(placement->get("align"), "up");
        p.surface_offset = number_of(placement->get("surface_offset"), 0);
        p.fixture_radius = number_of(placement->get("fixture_radius"), 16);
    }
    if (const Json* params = json.get("params")) {
        for (const Json& j : params->items) {
            Param q;
            q.key = string_of(j.get("key"));
            if (q.key.empty()) {
                problem("a parameter without a key");
                continue;
            }
            q.label = string_of(j.get("label"), q.key);
            q.group = string_of(j.get("group"));
            q.unit = string_of(j.get("unit"));
            q.tooltip = string_of(j.get("tooltip"));
            std::string type = string_of(j.get("type"), "float");
            q.type = type == "color" ? PARAM_COLOR : type == "bool" ? PARAM_BOOL : type == "enum" ? PARAM_ENUM : PARAM_FLOAT;
            if (const Json* options = j.get("options"))
                for (const Json& o : options->items)
                    q.options.push_back(o.string);
            const Json* d = j.get("default");
            if (q.type == PARAM_COLOR) {
                q.def = Value::vec(1, 1, 1);
                if (d && d->type == Json::Array && d->items.size() == 3)
                    q.def = Value::vec(number_of(&d->items[0], 1), number_of(&d->items[1], 1), number_of(&d->items[2], 1));
                q.min = 0, q.max = 1;
            } else if (q.type == PARAM_ENUM && d && d->type == Json::String) {
                int index = 0;
                for (size_t i = 0; i < q.options.size(); i++)
                    if (q.options[i] == d->string)
                        index = (int)i;
                q.def = Value::scalar((float)index);
            } else {
                q.def = Value::scalar(number_of(d, 0));
            }
            if (q.type == PARAM_FLOAT) {
                q.min = number_of(j.get("min"), 0);
                q.max = number_of(j.get("max"), q.def.x() > 0 ? q.def.x() * 4 : 1);
                q.logarithmic = number_of(j.get("log"), 0) != 0;
            } else if (q.type == PARAM_BOOL) {
                q.min = 0, q.max = 1;
            } else if (q.type == PARAM_ENUM) {
                q.min = 0, q.max = q.options.empty() ? 0 : (float)(q.options.size() - 1);
            }
            if (p.param(q.key))
                problem("parameter " + q.key + " twice");
            p.params.push_back(q);
        }
    }
    std::set<std::string> names;
    auto field = [&](const Json& j, const char* key, const std::string& where) -> ExprPtr {
        std::string error;
        const Json* f = j.get(key);
        ExprPtr e = expr_from_json(f, &error);
        if (f && f->type != Json::Null && !e)
            problem(where + " " + key + ": " + error);
        collect_names(e, names);
        return e;
    };
    if (const Json* lights = json.get("lights")) {
        int index = 0;
        for (const Json& j : lights->items) {
            ++index;
            LightTemplate l;
            std::string where = "light " + std::to_string(index);
            l.name = string_of(j.get("name"), "Light " + std::to_string(index));
            std::string type = string_of(j.get("type"), "point");
            l.type = type == "spot" ? LIGHT_SPOT : type == "rect" ? LIGHT_RECT : LIGHT_POINT;
            if (type != "point" && type != "spot" && type != "rect")
                problem(where + ": unknown type " + type);
            l.offset = field(j, "offset", where);
            l.direction = field(j, "direction", where);
            l.color = field(j, "color", where);
            l.intensity = field(j, "intensity", where);
            l.radius = field(j, "radius", where);
            l.cone = field(j, "cone", where);
            l.inner = field(j, "inner", where);
            l.size = field(j, "size", where);
            l.barn_angle = field(j, "barn_angle", where);
            l.barn_length = field(j, "barn_length", where);
            l.shadows = field(j, "shadows", where);
            l.light_size = field(j, "light_size", where);
            l.enabled = field(j, "enabled", where);
            l.flicker = field(j, "flicker", where);
            l.flicker_speed = field(j, "flicker_speed", where);
            l.jitter = field(j, "jitter", where);
            l.pulse = field(j, "pulse", where);
            l.pulse_speed = field(j, "pulse_speed", where);
            l.phase = number_of(j.get("phase"), 0);
            p.lights.push_back(l);
        }
    }
    if (p.lights.empty())
        problem("no lights");
    if (const Json* glow = json.get("glow")) {
        p.glow_mode = field(*glow, "mode", "glow");
        p.glow_scale = field(*glow, "scale", "glow");
        if (const Json* s = glow->get("suggest")) {
            p.suggest.enabled = true;
            p.suggest.hue = string_of(s->get("hue"), "any");
            p.suggest.min_size = number_of(s->get("min_size"), 0);
            p.suggest.max_size = number_of(s->get("max_size"), 1e9f);
            p.suggest.priority = (int)number_of(s->get("priority"), 0);
            p.suggest.min_brightness = number_of(s->get("min_brightness"), 0.2f);
            p.suggest.merge = number_of(s->get("merge"), 0);
            p.suggest.flames = number_of(s->get("flames"), 0) != 0;
            p.suggest.flame_min = number_of(s->get("flame_min"), 0);
            p.suggest.flame_max = number_of(s->get("flame_max"), 1e9f);
        }
    }
    for (const std::string& n : names)
        if (!p.param(n))
            problem("unknown parameter '" + n + "'");
    return p;
}

const Prefab* Library::find(const std::string& id) const
{
    for (const Prefab& p : prefabs)
        if (p.id == id)
            return &p;
    return nullptr;
}

std::vector<std::string> json_files(const std::string& dir)
{
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d)
        return out;
    while (struct dirent* e = readdir(d)) {
        size_t n = strlen(e->d_name);
        if (n > 5 && !strcmp(e->d_name + n - 5, ".json") && e->d_name[0] != '.')
            out.push_back(e->d_name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}
long long directory_stamp(const std::string& dir)
{
    long long stamp = 1469598103934665603LL;
    for (const std::string& name : json_files(dir)) {
        struct stat st;
        std::string path = dir + "/" + name;
        long long m = stat(path.c_str(), &st) ? 0 : PC_MTIME_NS(st) + st.st_size;
        for (char c : name)
            stamp = (stamp ^ (unsigned char)c) * 1099511628211LL;
        stamp = (stamp ^ m) * 1099511628211LL;
    }
    return stamp;
}
namespace {
bool read_file(const std::string& path, std::string& out)
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
void configure(Library& lib)
{
    if (!lib.builtin_dir.empty())
        return;
    const char* builtin = getenv("ROOD_PREFAB_ASSETS");
    lib.builtin_dir = builtin && *builtin ? builtin : "pc/assets/prefabs";
    const char* user = getenv("ROOD_PREFABS");
    lib.user_dir = user && *user ? user : "pc/user/prefabs";
}
} // namespace

void Library::load()
{
    configure(*this);
    prefabs.clear();
    for (int pass = 0; pass < 2; pass++) {
        const std::string& dir = pass ? user_dir : builtin_dir;
        for (const std::string& name : json_files(dir)) {
            std::string id = name.substr(0, name.size() - 5), text, error;
            std::string path = dir + "/" + name;
            Json json;
            Prefab p;
            if (!read_file(path, text))
                continue;
            if (!json_parse(text.c_str(), json, &error)) {
                p.id = id;
                p.name = id;
                p.category = "Other";
                p.error = "JSON " + error;
            } else {
                p = prefab_from_json(id, json);
            }
            p.path = path;
            p.user = pass == 1;
            bool replaced = false;
            for (Prefab& existing : prefabs)
                if (existing.id == id) {
                    p.overrides_builtin = !existing.user;
                    existing = p;
                    replaced = true;
                }
            if (!replaced)
                prefabs.push_back(p);
        }
    }
    std::stable_sort(prefabs.begin(), prefabs.end(), [](const Prefab& a, const Prefab& b) {
        return a.category != b.category ? a.category < b.category : a.name < b.name;
    });
    stamp = directory_stamp(builtin_dir) * 31 + directory_stamp(user_dir);
    ++serial;
}

bool Library::poll()
{
    configure(*this);
    long long now = directory_stamp(builtin_dir) * 31 + directory_stamp(user_dir);
    if (now == stamp)
        return false;
    load();
    return true;
}

static void make_dirs(const std::string& dir)
{
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/')
            pc_mkdir(dir.substr(0, i).c_str());
}

bool Library::save_user(const std::string& id, const Json& json, std::string* error)
{
    configure(*this);
    make_dirs(user_dir);
    std::string path = user_dir + "/" + id + ".json", temp = path + ".tmp";
    FILE* f = fopen(temp.c_str(), "w");
    if (!f) {
        if (error)
            *error = "cannot write " + path + ": " + strerror(errno);
        return false;
    }
    std::string text = json_write(json) + "\n";
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    if (pc_replace_file(temp.c_str(), path.c_str())) {
        if (error)
            *error = "cannot write " + path + ": " + strerror(errno);
        return false;
    }
    load();
    return true;
}

bool Library::revert_user(const std::string& id, std::string* error)
{
    configure(*this);
    std::string path = user_dir + "/" + id + ".json";
    if (pc_replace_file(path.c_str(), (path + ".bak").c_str())) {
        if (error)
            *error = "cannot move " + path + " aside: " + strerror(errno);
        return false;
    }
    load();
    return true;
}

Library& library()
{
    static Library lib;
    if (lib.stamp == -1)
        lib.load();
    return lib;
}

std::string make_id(const std::string& name)
{
    std::string id;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            id += c;
        else if (c >= 'A' && c <= 'Z')
            id += (char)(c - 'A' + 'a');
        else if (!id.empty() && id.back() != '_')
            id += '_';
    }
    while (!id.empty() && id.back() == '_')
        id.pop_back();
    return id.empty() ? "prefab" : id;
}

/* --------------------------------------------------------------- actors */

const Value* Actor::override_of(const std::string& key) const
{
    for (const auto& o : overrides)
        if (o.first == key)
            return &o.second;
    return nullptr;
}
void Actor::set_override(const std::string& key, const Value& v)
{
    for (auto& o : overrides)
        if (o.first == key) {
            o.second = v;
            return;
        }
    overrides.emplace_back(key, v);
}
void Actor::clear_override(const std::string& key)
{
    for (size_t i = 0; i < overrides.size(); i++)
        if (overrides[i].first == key) {
            overrides.erase(overrides.begin() + (long)i);
            return;
        }
}

static bool parse_value(const char* text, Value& out)
{
    float v[3];
    int n = 0;
    const char* p = text;
    while (n < 3) {
        char* end;
        double x = strtod(p, &end);
        if (end == p)
            break;
        v[n++] = (float)x;
        p = end;
        if (*p != ',')
            break;
        ++p;
    }
    if (n == 1)
        out = Value::scalar(v[0]);
    else if (n == 3)
        out = Value::vec(v[0], v[1], v[2]);
    else
        return false;
    return true;
}

bool actor_parse(const char* line, Actor& out)
{
    char id[128];
    int used = 0;
    Actor a;
    if (sscanf(line, "actor %127s %f %f %f %f %f %f%n", id, &a.position[0], &a.position[1], &a.position[2],
            &a.rotation[0], &a.rotation[1], &a.rotation[2], &used)
        < 7)
        return false;
    a.prefab = id;
    const char* p = line + used;
    for (;;) {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p || *p == '\n' || *p == '\r' || *p == '#')
            break;
        const char* key = p;
        while (*p && *p != '=' && *p != ' ' && *p != '\n')
            ++p;
        if (*p != '=') {
            while (*p && *p != ' ' && *p != '\n')
                ++p;
            continue;
        }
        std::string k(key, p);
        ++p;
        const char* value = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            ++p;
        std::string text(value, p);
        if (k == "glowkey") {
            a.glow_key = text;
            continue;
        }
        if (k == "fixture_type") {
            a.fixture_type = text;
            continue;
        }
        Value v;
        if (!parse_value(text.c_str(), v))
            continue;
        if (k == "enabled")
            a.enabled = v.x() != 0;
        else if (k == "seed")
            a.seed = (uint32_t)strtoul(text.c_str(), nullptr, 10);
        else if (k == "glow" && v.n == 3) {
            a.glow_linked = true;
            memcpy(a.glow, v.v, sizeof a.glow);
        } else if (k == "fixture" && v.n == 3) {
            a.fixture = true;
            memcpy(a.fixture_at, v.v, sizeof a.fixture_at);
        } else if (k == "fixture_radius") {
            a.fixture_radius = v.x();
            a.fixture_radius_set = true;
        } else if (k == "flame")
            a.flame = (int)v.x();
        else if (k == "fixture_shadows")
            a.fixture_shadows = v.x() != 0;
        else
            a.overrides.emplace_back(k, v);
    }
    out = a;
    return true;
}

std::string actor_format(const Actor& a)
{
    char text[256];
    snprintf(text, sizeof text, "actor %s %.1f %.1f %.1f %.2f %.2f %.2f seed=%u", a.prefab.c_str(), a.position[0],
        a.position[1], a.position[2], a.rotation[0], a.rotation[1], a.rotation[2], a.seed);
    std::string out = text;
    if (!a.enabled)
        out += " enabled=0";
    if (a.glow_linked) {
        snprintf(text, sizeof text, " glow=%.1f,%.1f,%.1f", a.glow[0], a.glow[1], a.glow[2]);
        out += text;
        if (!a.glow_key.empty())
            out += " glowkey=" + a.glow_key;
    }
    if (a.fixture) {
        snprintf(text, sizeof text, " fixture=%.1f,%.1f,%.1f", a.fixture_at[0], a.fixture_at[1], a.fixture_at[2]);
        out += text;
        if (a.fixture_radius_set || a.fixture_type.empty()) {
            snprintf(text, sizeof text, " fixture_radius=%g", a.fixture_radius);
            out += text;
        }
        if (a.fixture_shadows)
            out += " fixture_shadows=1";
    }
    if (!a.fixture_type.empty()) {
        out += " fixture_type=" + a.fixture_type;
        if (a.flame)
            out += " flame=" + std::to_string(a.flame);
    }
    for (const auto& o : a.overrides) {
        if (o.second.n == 3)
            snprintf(text, sizeof text, " %s=%g,%g,%g", o.first.c_str(), o.second.v[0], o.second.v[1], o.second.v[2]);
        else
            snprintf(text, sizeof text, " %s=%g", o.first.c_str(), o.second.v[0]);
        out += text;
    }
    return out;
}

TypeHooks& type_hooks()
{
    static TypeHooks hooks;
    return hooks;
}
static bool type_param(const Actor& a, const std::string& key, Value& out)
{
    return !a.fixture_type.empty() && type_hooks().param && type_hooks().param(a.fixture_type, key, out);
}
Value inherited_param(const Prefab& p, const Actor& a, const std::string& key, bool* from_type)
{
    Value v;
    bool typed = type_param(a, key, v);
    if (from_type)
        *from_type = typed;
    if (typed)
        return v;
    if (const Param* q = p.param(key))
        return q->def;
    return Value();
}
float actor_fixture_radius(const Actor& a)
{
    float r;
    if (!a.fixture_radius_set && !a.fixture_type.empty() && type_hooks().fixture_radius
        && type_hooks().fixture_radius(a.fixture_type, &r))
        return r;
    return a.fixture_radius;
}

Value actor_param(const Prefab& p, const Actor& a, const std::string& key)
{
    if (const Value* v = a.override_of(key))
        return *v;
    Value t;
    if (type_param(a, key, t))
        return t;
    if (const Param* q = p.param(key))
        return q->def;
    return Value();
}

void rotation_basis(const float rotation[3], float right[3], float down[3], float forward[3])
{
    const float d = 3.14159265f / 180.0f;
    float ca = cosf(rotation[0] * d), sa = sinf(rotation[0] * d);
    float cb = cosf(rotation[1] * d), sb = sinf(rotation[1] * d);
    float cc = cosf(rotation[2] * d), sc = sinf(rotation[2] * d);
    /* R = Ry(yaw) Rx(pitch) Rz(roll); columns. */
    right[0] = ca * cc + sa * sb * sc, right[1] = cb * sc, right[2] = -sa * cc + ca * sb * sc;
    down[0] = -ca * sc + sa * sb * cc, down[1] = cb * cc, down[2] = sa * sc + ca * sb * cc;
    forward[0] = sa * cb, forward[1] = -sb, forward[2] = ca * cb;
}

void rotation_from_basis(const float right[3], const float down[3], const float forward[3], float rotation[3])
{
    const float r = 180.0f / 3.14159265f;
    float sb = -forward[1];
    sb = sb > 1 ? 1 : sb < -1 ? -1 : sb;
    float pitch = asinf(sb), yaw, roll;
    if (cosf(pitch) > 1e-4f) {
        yaw = atan2f(forward[0], forward[2]);
        roll = atan2f(right[1], down[1]);
    } else {
        roll = 0;
        yaw = atan2f(-right[2], right[0]);
    }
    rotation[0] = yaw * r, rotation[1] = pitch * r, rotation[2] = roll * r;
    for (int k = 0; k < 3; k++)
        if (fabsf(rotation[k]) < 1e-4f)
            rotation[k] = 0;
}

void rotation_facing(const float normal[3], float rotation[3])
{
    float f[3] = { normal[0], normal[1], normal[2] };
    float len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 1e-6f) {
        rotation[0] = rotation[1] = rotation[2] = 0;
        return;
    }
    for (int k = 0; k < 3; k++)
        f[k] /= len;
    float ref[3] = { 0, 1, 0 }; /* world down */
    if (fabsf(f[1]) > 0.95f)
        ref[1] = 0, ref[2] = f[1] > 0 ? -1 : 1;
    float r[3] = { ref[1] * f[2] - ref[2] * f[1], ref[2] * f[0] - ref[0] * f[2], ref[0] * f[1] - ref[1] * f[0] };
    float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int k = 0; k < 3; k++)
        r[k] /= rl;
    float d[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
    rotation_from_basis(r, d, f, rotation);
}

static void to_world(const float right[3], const float down[3], const float forward[3], const float local[3], float out[3])
{
    for (int k = 0; k < 3; k++)
        out[k] = right[k] * local[0] + down[k] * local[1] + forward[k] * local[2];
}

void evaluate(const Prefab& p, const Actor& a, double time, bool animate, std::vector<EvalLight>& out)
{
    out.clear();
    Lookup lookup = [&](const std::string& name, Value& v) {
        if (const Value* o = a.override_of(name)) {
            v = *o;
            return true;
        }
        if (type_param(a, name, v))
            return true;
        if (const Param* q = p.param(name)) {
            v = q->def;
            return true;
        }
        return false;
    };
    float right[3], down[3], forward[3];
    rotation_basis(a.rotation, right, down, forward);
    /* A fixture type's light offset (from the flame, in the actor's frame). */
    float pivot[3] = { a.position[0], a.position[1], a.position[2] }, type_offset[3];
    if (!a.fixture_type.empty() && type_hooks().offset && type_hooks().offset(a.fixture_type, a.flame, type_offset)) {
        float w[3];
        to_world(right, down, forward, type_offset, w);
        for (int k = 0; k < 3; k++)
            pivot[k] += w[k];
    }
    float skip = a.fixture && !a.fixture_shadows ? fmaxf(actor_fixture_radius(a), 0) : 0;
    float s = a.scale > 0 ? a.scale : 1; /* its size (R): lengths scale, intensities do not */
    int index = 0;
    for (const LightTemplate& t : p.lights) {
        EvalLight l;
        l.name = t.name;
        l.type = t.type;
        l.enabled = expr_eval(t.enabled, lookup, Value::scalar(1)).x() > 0.5f;
        Value offset = expr_eval(t.offset, lookup, Value::scalar(0));
        float local[3] = { offset.at(0) * s, offset.at(1) * s, offset.at(2) * s }, world[3];
        to_world(right, down, forward, local, world);
        for (int k = 0; k < 3; k++)
            l.position[k] = pivot[k] + world[k];
        Value dir = expr_eval(t.direction, lookup, Value::vec(0, 0, 1));
        float ld[3] = { dir.at(0), dir.at(1), dir.at(2) };
        to_world(right, down, forward, ld, l.direction);
        float dl = sqrtf(l.direction[0] * l.direction[0] + l.direction[1] * l.direction[1] + l.direction[2] * l.direction[2]);
        for (int k = 0; k < 3; k++)
            l.direction[k] = dl > 1e-6f ? l.direction[k] / dl : (k == 2);
        Value color = expr_eval(t.color, lookup, Value::scalar(1));
        for (int k = 0; k < 3; k++)
            l.color[k] = fmaxf(color.at(k), 0);
        l.intensity = fmaxf(expr_eval(t.intensity, lookup, Value::scalar(1)).x(), 0);
        l.radius = fmaxf(expr_eval(t.radius, lookup, Value::scalar(600)).x() * s, 1);
        l.cone = expr_eval(t.cone, lookup, Value::scalar(45)).x();
        l.inner = expr_eval(t.inner, lookup, Value::scalar(0)).x();
        Value size = expr_eval(t.size, lookup, Value::scalar(128));
        l.size[0] = fmaxf(size.at(0) * s, 1), l.size[1] = fmaxf((size.n == 3 ? size.v[1] : size.v[0]) * s, 1);
        l.barn_angle = expr_eval(t.barn_angle, lookup, Value::scalar(45)).x();
        l.barn_length = fmaxf(expr_eval(t.barn_length, lookup, Value::scalar(0)).x() * s, 0);
        l.shadows = expr_eval(t.shadows, lookup, Value::scalar(0)).x() > 0.5f;
        l.light_size = fmaxf(expr_eval(t.light_size, lookup, Value::scalar(0)).x() * s, 0);
        l.shadow_skip = skip;
        float flicker = expr_eval(t.flicker, lookup, Value::scalar(0)).x();
        float speed = expr_eval(t.flicker_speed, lookup, Value::scalar(8)).x();
        float jitter = expr_eval(t.jitter, lookup, Value::scalar(0)).x() * s;
        float pulse = expr_eval(t.pulse, lookup, Value::scalar(0)).x();
        float pulse_speed = expr_eval(t.pulse_speed, lookup, Value::scalar(0.5f)).x();
        l.flicker = flicker, l.flicker_speed = speed, l.jitter = jitter, l.pulse = pulse, l.pulse_speed = pulse_speed;
        if (animate) {
            uint32_t seed = hash32(a.seed * 2654435761u + (uint32_t)index * 7919u + (uint32_t)(t.phase * 1000.0f));
            float factor = 1;
            if (flicker > 0)
                factor *= 1 + flicker * (1 - 2 * flicker_noise(seed, time * speed));
            if (pulse > 0)
                factor *= 1 + pulse * sinf((float)(6.283185307 * fmod(time * pulse_speed + t.phase, 1.0)));
            if (jitter > 0) {
                double s = time * speed * 0.5;
                l.position[0] += jitter * (2 * noise1(seed + 11, s) - 1);
                l.position[1] += jitter * 0.6f * (2 * noise1(seed + 12, s) - 1);
                l.position[2] += jitter * (2 * noise1(seed + 13, s) - 1);
            }
            l.factor = fmaxf(factor, 0);
            l.intensity *= l.factor;
        }
        out.push_back(l);
        ++index;
    }
}

int glow_drive(const Prefab& p, const Actor& a, double time, bool animate, float* scale)
{
    if (!a.glow_linked || !a.enabled)
        return 0;
    Lookup lookup = [&](const std::string& name, Value& v) {
        if (const Value* o = a.override_of(name)) {
            v = *o;
            return true;
        }
        if (type_param(a, name, v))
            return true;
        if (const Param* q = p.param(name)) {
            v = q->def;
            return true;
        }
        return false;
    };
    /* A prefab without a glow mode still takes its fixture type's. */
    Value typed;
    int mode = p.glow_mode || !lookup("glow_mode", typed)
        ? (int)lroundf(expr_eval(p.glow_mode, lookup, Value::scalar(1)).x())
        : (int)lroundf(typed.x());
    if (mode == 1) {
        float s = expr_eval(p.glow_scale, lookup, Value::scalar(1)).x();
        if (animate && !p.lights.empty()) {
            std::vector<EvalLight> lights;
            evaluate(p, a, time, true, lights);
            s *= lights[0].factor;
        }
        *scale = fmaxf(s, 0);
    }
    return mode < 0 ? 0 : mode > 2 ? 2 : mode;
}

} // namespace prefab
