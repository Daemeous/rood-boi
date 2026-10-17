// The library's entries and filters (library_model.h).
#include "library_model.h"

#include <ctype.h>
#include <string.h>
#include <algorithm>

namespace rood_library {
namespace {
std::string lower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = (char)tolower((unsigned char)c);
    return out;
}
std::vector<std::string> words(const std::string& s)
{
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!w.empty())
                out.push_back(lower(w)), w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty())
        out.push_back(lower(w));
    return out;
}
} // namespace

const std::vector<std::string>& kind_order()
{
    static const std::vector<std::string> order = { "light", "prefab", "fixture", "fog", "emitter" };
    return order;
}
int kind_rank(const std::string& kind)
{
    const std::vector<std::string>& o = kind_order();
    auto it = std::find(o.begin(), o.end(), kind);
    return it == o.end() ? (int)o.size() : (int)(it - o.begin());
}
std::string kind_label(const std::string& kind)
{
    if (kind == "light")
        return "Lights";
    if (kind == "prefab")
        return "Light actors";
    if (kind == "fixture")
        return "Fixtures";
    if (kind == "fog")
        return "Fog volumes";
    if (kind == "emitter")
        return "Emitters";
    return kind;
}
void sort_entries(std::vector<Entry>& all)
{
    std::stable_sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        int ra = kind_rank(a.kind), rb = kind_rank(b.kind);
        return ra != rb ? ra < rb : (ra == (int)kind_order().size() && a.kind < b.kind);
    });
}
bool matches(const Entry& e, const Filter& f, const std::set<std::string>& favourites)
{
    if (!f.kind.empty() && e.kind != f.kind)
        return false;
    if (!f.category.empty() && e.category != f.category)
        return false;
    if (f.favourites && !favourites.count(e.item))
        return false;
    if (f.mine && !e.mine)
        return false;
    std::string text = lower(e.name + " " + e.category + " " + e.description + " " + kind_label(e.kind) + " " + e.item);
    for (const std::string& t : e.tags)
        text += " " + lower(t);
    for (const std::string& w : words(f.search))
        if (text.find(w) == std::string::npos)
            return false;
    return true;
}
std::vector<const Entry*> shown(const std::vector<Entry>& all, const Filter& f, const std::set<std::string>& favourites)
{
    std::vector<const Entry*> out;
    for (const Entry& e : all)
        if (matches(e, f, favourites))
            out.push_back(&e);
    return out;
}
std::vector<std::string> kinds_present(const std::vector<Entry>& all)
{
    std::vector<std::string> out;
    for (const Entry& e : all)
        if (std::find(out.begin(), out.end(), e.kind) == out.end())
            out.push_back(e.kind);
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        int ra = kind_rank(a), rb = kind_rank(b);
        return ra != rb ? ra < rb : a < b;
    });
    return out;
}
std::vector<std::string> categories(const std::vector<Entry>& all, const std::string& kind)
{
    std::vector<std::string> out;
    for (const Entry& e : all)
        if ((kind.empty() || e.kind == kind) && std::find(out.begin(), out.end(), e.category) == out.end())
            out.push_back(e.category);
    return out;
}
std::string favourites_line(const std::set<std::string>& favourites)
{
    if (favourites.empty())
        return std::string();
    std::string s = "editor_library_favourites";
    for (const std::string& f : favourites)
        s += " " + f;
    return s;
}
std::set<std::string> parse_favourites(const char* rest)
{
    std::set<std::string> out;
    for (const std::string& w : words(rest ? rest : "")) /* items have no spaces; lowercase is how they are written */
        out.insert(w);
    return out;
}
} // namespace rood_library
