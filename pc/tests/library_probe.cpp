// The library without the game: its entries and filters (pc/src/library_model.cpp)
// and its thumbnails (library_thumbs.cpp: our renderers' light model on a
// neutral floor and wall, the cache file).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "library_model.h"
#include "library_thumbs.h"
#include "light_shape.h"

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool near(float a, float b, float e = 1e-3f) { return fabsf(a - b) <= e; }

static rood_library::Entry entry(const char* item, const char* kind, const char* name, const char* category, bool mine = false)
{
    rood_library::Entry e;
    e.item = item, e.kind = kind, e.name = name, e.category = category, e.mine = mine;
    e.id = strchr(item, ':') ? strchr(item, ':') + 1 : item;
    return e;
}

static void model()
{
    using namespace rood_library;
    std::vector<Entry> all = { entry("fog:ankle_mist", "fog", "Ankle mist", "Fog presets"),
        entry("prefab:candle", "prefab", "Candle", "Fire"), entry("light:point", "light", "Point light", "Lights"),
        entry("emitter:sparks", "emitter", "Sparks", "Fire"), entry("prefab:my_lamp", "prefab", "My lamp", "My prefabs", true),
        entry("fixture:wall_torch", "fixture", "Wall torch", "Fire"), entry("thing:x", "thing", "X", "Other") };
    all[0].description = "A thin sheet of mist at the feet";
    sort_entries(all);
    std::string order;
    for (const Entry& e : all)
        order += e.kind + " ";
    check(order == "light prefab prefab fixture fog emitter thing ", "sorted by kind: lights, prefabs, fixtures, fog, emitters, then others");
    std::set<std::string> fav = { "prefab:candle" };
    Filter f;
    check(shown(all, f, fav).size() == all.size(), "no filter: everything");
    f.kind = "fog";
    check(shown(all, f, fav).size() == 1 && shown(all, f, fav)[0]->item == "fog:ankle_mist", "a kind chip");
    f = Filter();
    f.search = "MIST feet";
    check(shown(all, f, fav).size() == 1, "search: every word, any case, in the name or description");
    f.search = "fire";
    check(shown(all, f, fav).size() == 3, "search finds a category too");
    f = Filter();
    f.favourites = true;
    check(shown(all, f, fav).size() == 1 && shown(all, f, fav)[0]->item == "prefab:candle", "favourites only");
    f = Filter();
    f.mine = true;
    check(shown(all, f, fav).size() == 1 && shown(all, f, fav)[0]->item == "prefab:my_lamp", "Mine: the user's own");
    f = Filter();
    f.kind = "prefab", f.category = "Fire";
    check(shown(all, f, fav).size() == 1, "a kind and a category");
    std::vector<std::string> cats = categories(all, "prefab");
    check(cats.size() == 2 && cats[0] == "Fire" && cats[1] == "My prefabs", "a kind's categories in order");
    std::vector<std::string> kinds = kinds_present(all);
    check(kinds.size() == 6 && kinds.front() == "light" && kinds.back() == "thing", "the kinds present, in order");
    check(kind_label("fog") == "Fog volumes" && kind_label("emitter") == "Emitters" && kind_label("thing") == "thing",
        "kind labels");
    std::set<std::string> back = parse_favourites(favourites_line(fav).c_str() + strlen("editor_library_favourites "));
    check(back == fav && favourites_line({}).empty(), "favourites: a settings line and back");
}

static rood_thumbs::Scene point_scene(float intensity)
{
    rood_thumbs::Scene s;
    prefab::EvalLight l;
    l.type = prefab::LIGHT_POINT;
    l.position[0] = 0, l.position[1] = -48, l.position[2] = 0;
    l.intensity = intensity, l.radius = 320;
    s.lights.push_back(l);
    return s;
}
static float luma(const std::vector<uint8_t>& p, int size, float u, float v)
{
    int x = (int)(u * (size - 1)), y = (int)(v * (size - 1));
    const uint8_t* q = &p[((size_t)y * size + x) * 4];
    return 0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2];
}

static void thumbs(const char* dir)
{
    check(near(pc_light_falloff(0, 100), 1) && near(pc_light_falloff(50, 100), 0.5625f) && near(pc_light_falloff(120, 100), 0),
        "the renderers' falloff: (1 - (d/r)^2)^2");
    const int n = rood_thumbs::SIZE;
    std::vector<uint8_t> a, b, dark, fogged;
    rood_thumbs::render(point_scene(1.5f), n, a);
    rood_thumbs::render(point_scene(1.5f), n, b);
    check(a.size() == (size_t)n * n * 4 && a == b, "96 x 96 RGBA, the same bytes every time");
    check(luma(a, n, 0.5f, 0.75f) > luma(a, n, 0.05f, 0.95f) + 20, "lit under the light, darker toward the corner");
    rood_thumbs::render(rood_thumbs::Scene(), n, dark);
    check(luma(dark, n, 0.5f, 0.75f) < luma(a, n, 0.5f, 0.75f), "no light: only the ambient");
    check(rood_thumbs::hash(point_scene(1.5f), n) == rood_thumbs::hash(point_scene(1.5f), n)
            && rood_thumbs::hash(point_scene(1.5f), n) != rood_thumbs::hash(point_scene(2), n)
            && rood_thumbs::hash(point_scene(1.5f), n).size() == 16,
        "the hash follows what the picture shows");
    std::string before = rood_thumbs::hash(point_scene(1.5f), n);
    float saved = rood_thumbs::look().floor;
    rood_thumbs::look().floor = 0.3f;
    check(rood_thumbs::hash(point_scene(1.5f), n) != before, "and the scene's look");
    rood_thumbs::look().floor = saved;
    rood_thumbs::Scene spot;
    prefab::EvalLight l;
    l.type = prefab::LIGHT_SPOT;
    l.position[1] = -200, l.direction[0] = 0, l.direction[1] = 1, l.direction[2] = 0;
    l.cone = 30, l.radius = 600, l.intensity = 2;
    spot.lights.push_back(l);
    std::vector<uint8_t> sp;
    rood_thumbs::render(spot, n, sp);
    /* v = 0.64: the floor under the spot (z about 0) from this camera; u = 0.15 there is 46 degrees off its axis */
    check(luma(sp, n, 0.5f, 0.64f) > luma(sp, n, 0.15f, 0.64f) + 15, "a spot: its pool on the floor, dark outside its cone");
    rood_thumbs::Scene fog;
    fog.fog.on = true, fog.fog.density = 0.01f, fog.fog.fill[0] = fog.fog.fill[1] = fog.fog.fill[2] = 0.2f;
    rood_thumbs::render(fog, n, fogged);
    check(fogged != dark && luma(fogged, n, 0.5f, 0.7f) > luma(dark, n, 0.5f, 0.7f), "a fog volume shows over the floor");
    /* Every look field of a fog volume shows (presets that differ only there are told apart). */
    const char* names[] = { "noise", "taper", "glow_core", "phase", "opacity", "brightness", "ambient", "shadow" };
    for (int f = 0; f < 8; f++) {
        rood_thumbs::Scene a2 = fog;
        a2.fog.glow[0] = a2.fog.glow[1] = 0.02f;
        a2.lights.push_back(l);
        rood_thumbs::Scene b2 = a2;
        float* field[] = { &b2.fog.noise, &b2.fog.taper, &b2.fog.glow_core, &b2.fog.phase, &b2.fog.opacity,
            &b2.fog.brightness, &b2.fog.ambient, &b2.fog.shadow };
        *field[f] = f == 4 ? 0.3f : f == 3 ? -0.5f : 1.0f + (*field[f] > 0.9f ? 1.0f : 0.0f);
        std::vector<uint8_t> pa, pb;
        rood_thumbs::render(a2, n, pa);
        rood_thumbs::render(b2, n, pb);
        check(pa != pb && rood_thumbs::hash(a2, n) != rood_thumbs::hash(b2, n), (std::string("a fog volume's ") + names[f] + " shows").c_str());
    }
    std::string path = std::string(dir) + "/" + rood_thumbs::file_name("fog", "ankle mist/x", "0123456789abcdef");
    check(path.find("fog-ankle_mist_x-0123456789abcdef.rgba") != std::string::npos, "a file name safe for any id");
    std::string error;
    std::vector<uint8_t> back;
    check(rood_thumbs::save(path, n, fogged, &error) && rood_thumbs::load(path, n, back) && back == fogged,
        "the cache file round-trips");
    check(!rood_thumbs::load(path, n * 2, back), "a file of another size is not taken");
}

int main(int argc, char** argv)
{
    model();
    thumbs(argc > 1 ? argv[1] : "library_probe_out");
    printf("%s\n", failures ? "FAIL" : "PASS: library model and thumbnails");
    return failures ? 1 : 0;
}
