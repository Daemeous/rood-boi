// The game's reader of the fixture catalog (pc/src/fixture_catalog.cpp)
// without the game: fixture_probe CATALOG [WRITTEN]
// loads the catalog (as ROOD_FIXTURES), prints every resolved type as JSON
// ({id: {prefab, glow, enabled, fixture_radius, offset, params}}), checks
// that an actor naming a type takes its parameters, offset and radius
// (prefab hooks), and writes the catalog back to WRITTEN (the editor's
// save) for pc/tests/fixture_catalog_test.py to compare byte for byte.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "fixture_catalog.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: fixture_probe CATALOG [WRITTEN]\n");
        return 2;
    }
    setenv("ROOD_FIXTURES", argv[1], 1);
    const fixtures::Catalog& c = fixtures::catalog();
    if (!c.error.empty() || c.types.empty()) {
        fprintf(stderr, "catalog: %s (%zu types)\n", c.error.c_str(), c.types.size());
        return 1;
    }
    std::string out = "{";
    for (size_t i = 0; i < c.types.size(); i++) {
        const fixtures::Type& t = c.types[i];
        char head[256];
        snprintf(head, sizeof head, "%s\"%s\":{\"prefab\":\"%s\",\"glow\":%d,\"enabled\":%d,\"fixture_radius\":%g,"
            "\"offset\":[%g,%g,%g],\"params\":{", i ? "," : "", t.id.c_str(), t.prefab.c_str(), t.glow, t.enabled ? 1 : 0,
            t.fixture_radius, t.offset[0], t.offset[1], t.offset[2]);
        out += head;
        for (size_t k = 0; k < t.params.size(); k++) {
            const prefab::Value& v = t.params[k].second;
            char item[160];
            if (v.n == 3)
                snprintf(item, sizeof item, "%s\"%s\":[%g,%g,%g]", k ? "," : "", t.params[k].first.c_str(), v.v[0], v.v[1], v.v[2]);
            else
                snprintf(item, sizeof item, "%s\"%s\":[%g]", k ? "," : "", t.params[k].first.c_str(), v.v[0]);
            out += item;
        }
        out += "}}";
    }
    out += "}";
    printf("%s\n", out.c_str());

    /* An actor of the first type with a light offset: its parameters and
     * radius come from the type, its own override wins, the offset moves
     * the light in the actor's frame. */
    const fixtures::Type* t = nullptr;
    for (const fixtures::Type& x : c.types)
        if (x.prefab == "candle" && !t)
            t = &x;
    if (t) {
        std::string id = t->id;
        prefab::Actor a;
        a.prefab = "candle";
        a.fixture = true;
        a.fixture_type = id;
        a.rotation[0] = 90; /* right = -z */
        prefab::Prefab p;
        prefab::Json json;
        std::string error;
        const char* text = "{\"params\":[{\"key\":\"intensity\",\"default\":0.5},{\"key\":\"flame_height\",\"default\":10}],"
                           "\"lights\":[{\"name\":\"Flame\",\"offset\":[0,\"-flame_height\",0],\"intensity\":\"intensity\"}]}";
        if (!prefab::json_parse(text, json, &error)) {
            fprintf(stderr, "probe prefab: %s\n", error.c_str());
            return 1;
        }
        p = prefab::prefab_from_json("candle", json);
        float off[3] = { 4, -2, 0 };
        prefab::Value v = prefab::Value::scalar(2.5f);
        fixtures::set_param(id, "intensity", &v);
        fixtures::set_offset(id, off);
        float radius = 21;
        fixtures::set_fixture_radius(id, &radius);
        std::vector<prefab::EvalLight> ls;
        prefab::evaluate(p, a, 0, false, ls);
        const fixtures::Type* edited = fixtures::find(id);
        bool ok = ls.size() == 1 && fabsf(ls[0].intensity - 2.5f) < 1e-4f && fabsf(ls[0].shadow_skip - 21) < 1e-4f
            && fabsf(ls[0].position[2] + 4) < 1e-3f && fabsf(ls[0].position[1] + 2 + edited->param("flame_height")->x()) < 1e-3f
            && fabsf(ls[0].position[0]) < 1e-3f;
        a.set_override("intensity", prefab::Value::scalar(0.25f));
        prefab::evaluate(p, a, 0, false, ls);
        ok = ok && fabsf(ls[0].intensity - 0.25f) < 1e-4f;
        if (!ok) {
            fprintf(stderr, "type hooks: light at %g %g %g, intensity %g, skip %g\n", ls[0].position[0], ls[0].position[1],
                ls[0].position[2], ls[0].intensity, ls[0].shadow_skip);
            return 1;
        }
        fixtures::revert(); /* the file as it was */
    }
    if (argc > 2) {
        setenv("ROOD_FIXTURES", argv[1], 1);
        std::string message;
        /* save() writes the loaded path: copy through a temporary catalog path */
        const fixtures::Catalog& again = fixtures::catalog();
        std::string text = prefab::json_write(again.json) + "\n";
        FILE* f = fopen(argv[2], "w");
        if (!f || fwrite(text.data(), 1, text.size(), f) != text.size())
            return 1;
        fclose(f);
    }
    fprintf(stderr, "fixture_probe: %zu types\n", c.types.size());
    return 0;
}
