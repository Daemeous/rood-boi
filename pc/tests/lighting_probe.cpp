// The game's lighting pack reader (pc/src/lighting_layers.cpp) from the
// command line, for pc/tests/lighting_layers_test.py:
//   lighting_probe selftest
//   lighting_probe schema
//   lighting_probe merge ROOT PACKS USER ZONE ROOM      effective items, problems, user layer
//   lighting_probe save ROOT PACKS USER ZONE ROOM       rewrite the user layer (migrates old files)
//   lighting_probe promote ROOT PACKS USER ZONE ROOM [ID...]
//   lighting_probe canon KIND KEY VALUE
// ROOT "-" means no packs; PACKS is the comma list.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "lighting_layers.h"

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}

static void setup(const char* root, const char* packs, const char* user)
{
    setenv("ROOD_LIGHTING", strcmp(root, "-") ? root : "none", 1);
    setenv("ROOD_LIGHTS", user, 1);
    lighting::set_packs(packs);
}

static void print_state(const char* title, const lighting::State& s)
{
    printf("[%s]\n", title);
    for (const auto& kv : s) {
        lighting::Entry e;
        e.item = kv.second;
        e.id = kv.first;
        printf("%s\n", lighting::format_entry(e).c_str());
    }
}

static int selftest()
{
    using namespace lighting;
    check(format_number(1.25, 1) == "1.2" && format_number(-0.04, 1) == "0" && format_number(600, 1) == "600"
            && format_number(0.1 + 0.2, 3) == "0.3",
        "fixed number formatting (rounded, no trailing zeros, no -0)");
    check(canonical_value("light", "pos", "1.04,-2.00,3") == "1,-2,3", "vectors are canonical");
    rood_lights::Light l;
    l.id = "0123456789abcd";
    l.type = 1;
    l.position[0] = 100.04f, l.position[1] = -320.26f, l.position[2] = 77;
    l.color[0] = 0.5f;
    l.intensity = 2.5f;
    l.shadows = true;
    l.name = "Window \"left\"";
    Item it = item_from_light(l);
    rood_lights::Light back;
    check(light_from_item(it, back) && back.type == 1 && fabsf(back.position[1] + 320.3f) < 1e-3f && back.shadows
            && back.name == l.name && back.id == l.id,
        "a light round-trips through an item");
    check(!it.get("radius") && !it.get("enabled") && it.get("type") && *it.get("type") == "spot",
        "defaults are left out of items");
    Entry e;
    e.item = it;
    e.id = it.id();
    File f;
    parse_text(format_file({ e }, "test"), f);
    check(f.version == 2 && f.entries.size() == 1 && f.entries[0].item == it, "a file round-trips (quotes too)");
    prefab::Actor a;
    a.id = "a1";
    a.prefab = "candle";
    a.position[0] = 5;
    a.overrides.emplace_back("intensity", prefab::Value::scalar(2.5f));
    a.overrides.emplace_back("tint", prefab::Value::vec(1, 0.5f, 0.25f));
    a.glow_linked = true;
    a.glow_key = "room:0000001@1,2,3";
    Item ai = item_from_actor(a);
    prefab::Actor ab;
    check(actor_from_item(ai, ab) && ab.overrides.size() == 2 && ab.glow_linked && ab.glow_key == a.glow_key
            && ab.prefab == "candle",
        "an actor round-trips (parameters, glow link)");
    /* Diff and merge. */
    State below, want;
    below[it.id()] = it;
    Item moved = it;
    moved.set("pos", "1,2,3");
    moved.erase("shadows");
    canonicalize(moved);
    want[it.id()] = moved;
    want[ai.id()] = ai;
    std::vector<Entry> d = diff(below, want);
    check(d.size() == 2 && d[0].verb == DEFINE && d[1].verb == OVERRIDE && d[1].set.size() == 1 && d[1].unset.size() == 1,
        "diff: a new item defined, a changed one overridden field by field (set and unset)");
    State merged = below;
    for (const Entry& x : d)
        apply_entry(merged, x, nullptr);
    check(merged.size() == 2 && merged[it.id()] == moved, "merge(below, diff(below, want)) == want");
    Entry dangling;
    dangling.verb = OVERRIDE;
    dangling.id = "light:nothing";
    std::string problem;
    check(!apply_entry(merged, dangling, &problem) && problem.find("light:nothing") != std::string::npos,
        "an override of a missing id is a problem");
    std::string patched = patch_text("rood-lighting 2\n# keep me\nlight b pos=1,1,1 # note\nlight d pos=2,2,2\n", { "light:c", "light:d" },
        { [] {
            Entry x;
            x.verb = REMOVE;
            x.id = "light:c";
            return x;
        }() },
        "");
    check(patched == "rood-lighting 2\n# keep me\nlight b pos=1,1,1 # note\ndelete light:c\n",
        "patch keeps comments, removes and inserts in id order");
    std::string a1 = new_name(), a2 = new_name();
    check(a1.size() == 14 && a2 > a1, "new ids are time-ordered");
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    std::string cmd = argv[1];
    if (cmd == "selftest")
        return selftest();
    if (cmd == "schema") {
        printf("%s\n", lighting::schema_json().c_str());
        return 0;
    }
    if (cmd == "canon" && argc == 5) {
        printf("%s\n", lighting::canonical_value(argv[2], argv[3], argv[4]).c_str());
        return 0;
    }
    if ((cmd == "merge" || cmd == "save" || cmd == "promote") && argc >= 7) {
        setup(argv[2], argv[3], argv[4]);
        lighting::Room r;
        lighting::load_room(r, atoi(argv[5]), atoi(argv[6]));
        if (cmd == "merge") {
            print_state("base", r.base);
            print_state("effective", r.loaded);
            printf("[problems]\n");
            for (const lighting::Problem& p : r.problems)
                printf("%s:%d: %s\n", p.layer >= 0 ? r.layers[p.layer].label().c_str() : "", p.line, p.message.c_str());
            printf("[badges]\n");
            for (const auto& kv : r.loaded)
                printf("%s %s\n", kv.first.c_str(), lighting::badge(r, r.loaded, kv.first).c_str());
            printf("[user]\n%s", lighting::user_layer_text(r, r.loaded).c_str());
            return 0;
        }
        std::string message;
        bool ok;
        if (cmd == "save")
            ok = lighting::save_user(r, r.loaded, &message);
        else {
            std::vector<std::string> ids(argv + 7, argv + argc);
            ok = lighting::promote(r, r.loaded, ids, &message);
        }
        printf("%s: %s\n", ok ? "ok" : "failed", message.c_str());
        return ok ? 0 : 1;
    }
    fprintf(stderr, "usage: see the top of pc/tests/lighting_probe.cpp\n");
    return 2;
}
