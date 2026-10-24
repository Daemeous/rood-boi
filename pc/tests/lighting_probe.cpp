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
    rood_lights::Light shaped;
    shaped.id = "fedcba98765432";
    shaped.type = 2;
    shaped.inner = 20, shaped.barn_angle = 30, shaped.barn_length = 48;
    Item shaped_item = item_from_light(shaped);
    rood_lights::Light shaped_back;
    check(light_from_item(shaped_item, shaped_back) && shaped_back.inner == 20 && shaped_back.barn_angle == 30
            && shaped_back.barn_length == 48 && shaped_item.get("barn_length")
            && !item_from_light(rood_lights::Light()).get("barn_angle"),
        "the inner cone and barn doors round-trip; at their defaults they are left out");
    shaped.pattern = shaped.pattern_mode = 1;
    shaped.pattern_grid = 4;
    shaped.pattern_elevation = -15;
    shaped.pattern_spread = 3.5f;
    shaped.pattern_entry = "E008";
    shaped.pattern_faces = "0123456789,0,0,0,0,1,0,1,0,0,1,0,1";
    Item patterned = item_from_light(shaped);
    rood_lights::Light pattern_back;
    check(light_from_item(patterned, pattern_back) && pattern_back.pattern == 1 && pattern_back.pattern_mode == 1
            && pattern_back.pattern_grid == 4 && pattern_back.pattern_elevation == -15
            && pattern_back.pattern_spread == 3.5f && pattern_back.pattern_entry == "E008"
            && pattern_back.pattern_faces == shaped.pattern_faces,
        "window aperture controls and texture-key/UV mapping round-trip without mask pixels");
    patterned.set("pattern_grid", "999");
    patterned.set("pattern_elevation", "-999");
    patterned.set("pattern_spread", "0");
    light_from_item(patterned, pattern_back);
    check(pattern_back.pattern_grid == 16 && pattern_back.pattern_elevation == -80
            && pattern_back.pattern_spread == .01f,
        "window aperture controls clamp to supported bounds");
    Entry e;
    e.item = it;
    e.id = it.id();
    File f;
    parse_text(format_file({ e }, "test"), f);
    check(f.version == 2 && f.entries.size() == 1 && f.entries[0].item == it, "a file round-trips (quotes too)");
    /* An emitter (pc/src/particles.cpp; lighting/README.md "Emitters"): with an
     * asset it keeps the asset fields it writes even at the schema's
     * defaults (they differ from its asset's), without one they go. */
    File ef;
    check(parse_text("rood-lighting 2\nemitter 68ea4c1200a3f2 asset=dust_motes pos=10,-20,30 count=400 drift=1,0,0.5\n", ef)
            && ef.entries.size() == 1 && ef.entries[0].item.kind == "emitter" && ef.entries[0].item.get("count")
            && *ef.entries[0].item.get("drift") == "1,0,0.5",
        "an emitter parses; with its asset, a field at the default stays (count=400)");
    Entry ee;
    ee.item = ef.entries[0].item;
    ee.id = ee.item.id();
    check(format_entry(ee) == "emitter 68ea4c1200a3f2 asset=dust_motes pos=10,-20,30 count=400 drift=1,0,0.5",
        "and formats back the same (the schema's field order)");
    Item bare;
    bare.kind = "emitter", bare.name = "68ea4c1200a3f3";
    bare.set("pos", "1,2,3"), bare.set("count", "400"), bare.set("yaw", "0"), bare.set("lit", "1");
    canonicalize(bare);
    check(!bare.get("count") && !bare.get("yaw") && bare.get("pos") && bare.get("lit") && preset_field("emitter", "curl")
            && !preset_field("emitter", "pos") && known_kind("emitter"),
        "without an asset its defaults are left out; asset fields are preset fields, its own are not");
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
    prefab::Actor sized = a;
    sized.scale = 1.75f;
    Item si = item_from_actor(sized);
    prefab::Actor sb;
    check(actor_from_item(si, sb) && fabsf(sb.scale - 1.75f) < 1e-4f && si.get("scale") && !ai.get("scale"),
        "an actor's scale round-trips; at 1 it is left out");
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
