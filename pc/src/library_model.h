/* The library's entries and filters (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Library"; editor_library.h fills and draws them): an entry per thing to
 * place (a basic light, a light prefab, a fixture, a fog preset, an emitter),
 * its kind (its source, and its tile's type icon), name, category,
 * description and tags; filters by kind, category, search words, favourites
 * and the user's own ("Mine"); the favourites' settings line. No Dear ImGui
 * (pc/tests/library_probe.cpp). */
#ifndef ROOD_LIBRARY_MODEL_H
#define ROOD_LIBRARY_MODEL_H
#include <set>
#include <string>
#include <vector>

namespace rood_library {
struct Entry {
    std::string item; /* what placing it names: "light:point", "prefab:candle", "fixture:wall_torch", "fog:ankle_mist", "emitter:<id>" */
    std::string kind; /* "light", "prefab", "fixture", "fog", "emitter", or a registered source's */
    std::string id; /* the item after its kind ("candle"): its thumbnail file's name */
    std::string name, category, description;
    std::vector<std::string> tags;
    bool mine = false; /* the user's own (pc/user/prefabs and its fog/) */
    std::string error; /* a load problem (shown on its tile) */
};
struct Filter {
    std::string kind; /* "": every kind */
    std::string category; /* "": every category */
    std::string search; /* words, all of which must appear (any case) */
    bool favourites = false, mine = false;
};
/* The kinds in the library's order; any other kind ranks after them, by name. */
const std::vector<std::string>& kind_order();
int kind_rank(const std::string& kind);
std::string kind_label(const std::string& kind); /* "Lights", "Light actors", "Fixtures", "Fog volumes", "Emitters"; else the kind */
void sort_entries(std::vector<Entry>& all); /* by kind, then as listed (stable) */
bool matches(const Entry& e, const Filter& f, const std::set<std::string>& favourites);
std::vector<const Entry*> shown(const std::vector<Entry>& all, const Filter& f, const std::set<std::string>& favourites);
std::vector<std::string> kinds_present(const std::vector<Entry>& all); /* in order */
std::vector<std::string> categories(const std::vector<Entry>& all, const std::string& kind); /* first seen first; "" kind: all */
/* "editor_library_favourites <item> <item> ..." ("" when none), and its rest back. */
std::string favourites_line(const std::set<std::string>& favourites);
std::set<std::string> parse_favourites(const char* rest);
} // namespace rood_library
#endif
