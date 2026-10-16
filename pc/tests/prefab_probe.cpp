// Light actor prefabs (pc/src/light_prefabs.cpp) without the game: every
// built-in prefab parses with no problems, expressions evaluate, an actor's
// overrides survive its room-file line, animation is deterministic, and a
// user prefab overrides a built-in of the same id.
//
//     prefab_probe [pc/assets/prefabs]   (CTest passes the directory)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "light_prefabs.h"

using namespace prefab;

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool near(float a, float b, float eps = 1e-4f) { return fabsf(a - b) <= eps; }

int main(int argc, char** argv)
{
    const char* assets = argc > 1 ? argv[1] : "pc/assets/prefabs";
    char user[] = "/tmp/prefab_probe_XXXXXX";
    if (!mkdtemp(user)) {
        perror("mkdtemp");
        return 1;
    }
    setenv("ROOD_PREFAB_ASSETS", assets, 1);
    setenv("ROOD_PREFABS", user, 1);

    /* Expressions. */
    auto lookup = [](const std::string& name, Value& v) {
        if (name == "a")
            v = Value::scalar(2);
        else if (name == "tint")
            v = Value::vec(1, 0.5f, 0.25f);
        else
            return false;
        return true;
    };
    Value v = expr_eval(expr_parse("-a * 3 + 1"), lookup);
    check(v.n == 1 && near(v.x(), -5), "scalar arithmetic and unary minus");
    v = expr_eval(expr_parse("tint * a"), lookup);
    check(v.n == 3 && near(v.v[1], 1) && near(v.v[2], 0.5f), "vectors broadcast with scalars");
    v = expr_eval(expr_parse("clamp(a * 10, 0, 4) + min(1, 0.5) + select(a, 100, 200)"), lookup);
    check(near(v.x(), 104.5f), "functions (clamp, min, select)");
    float k[3];
    kelvin(6500, k);
    check(k[0] > 0.97f && k[1] > 0.95f && k[2] > 0.95f, "kelvin(6500) is near white");
    kelvin(1900, k);
    check(k[0] == 1 && k[1] < 0.6f && k[2] < 0.15f, "kelvin(1900) is orange");
    std::string error;
    check(!expr_parse("a +", &error) && !error.empty(), "a broken expression reports an error");
    check(!expr_parse("nope(1)", &error), "unknown functions are errors");

    /* JSON round trip. */
    Json j;
    check(json_parse("{\"a\": [1, 2.5, \"x\"], // comment\n \"b\": {\"c\": true,},}", j, &error), "JSON with comments and trailing commas");
    Json back;
    check(json_parse(json_write(j).c_str(), back) && back.get("b")->get("c")->boolean && back.get("a")->items[1].number == 2.5,
        "JSON written back parses the same");

    /* The library: every built-in prefab is clean. */
    Library& lib = library();
    check(lib.prefabs.size() >= 6, "built-in prefabs found");
    for (const Prefab& p : lib.prefabs) {
        char what[256];
        snprintf(what, sizeof what, "%s (%s) loads without problems%s%s", p.id.c_str(), p.name.c_str(),
            p.error.empty() ? "" : ": ", p.error.c_str());
        check(p.error.empty() && !p.lights.empty() && !p.user, what);
    }
    const Prefab* candle = lib.find("candle");
    check(candle != nullptr, "a candle prefab");
    if (!candle)
        return 1;

    /* Actor line round trip with overrides. */
    Actor a;
    a.prefab = "candle";
    a.position[0] = 100, a.position[1] = -50, a.position[2] = 300;
    a.rotation[0] = 90;
    a.seed = 4242;
    a.set_override("intensity", Value::scalar(2.5f));
    a.set_override("tint", Value::vec(1, 0.5f, 0.25f));
    a.glow_linked = true;
    a.glow[0] = 100, a.glow[1] = -60, a.glow[2] = 300;
    a.fixture = true;
    a.fixture_at[0] = 101, a.fixture_at[1] = -61, a.fixture_at[2] = 299;
    a.fixture_radius = 22;
    std::string line = actor_format(a);
    Actor b;
    check(actor_parse(line.c_str(), b), "actor line parses");
    check(b.prefab == "candle" && near(b.position[1], -50) && near(b.rotation[0], 90) && b.seed == 4242 && b.glow_linked
            && near(b.glow[1], -60),
        "actor transform, seed and glow link survive the line");
    check(b.fixture && near(b.fixture_at[1], -61) && near(b.fixture_radius, 22) && !b.fixture_shadows,
        "the fixture record survives the line");
    const Value* o = b.override_of("tint");
    check(b.overrides.size() == 2 && near(b.override_of("intensity")->x(), 2.5f) && o && o->n == 3 && near(o->v[2], 0.25f),
        "only the overridden parameters are stored, values intact");
    check(near(actor_param(*candle, b, "radius").x(), candle->param("radius")->def.x()),
        "parameters not overridden come from the prefab");

    /* Evaluation: the flame sits flame_height above the pivot; the
     * override drives the intensity; animation is deterministic. */
    std::vector<EvalLight> still, l1, l2, l3;
    evaluate(*candle, b, 0, false, still);
    check(still.size() == 1 && near(still[0].position[1], -50 - candle->param("flame_height")->def.x(), 1e-3f)
            && near(still[0].intensity, 2.5f),
        "the flame is above the pivot with the overridden intensity");
    check(near(still[0].shadow_skip, 22), "a fixture's light skips its fixture's shadow (shadow_skip = fixture radius)");
    /* Scale (R): the light's offset from the pivot and its radius grow with
     * the actor; its intensity does not. */
    Actor big = b;
    big.scale = 2;
    std::vector<EvalLight> scaled;
    evaluate(*candle, big, 0, false, scaled);
    check(scaled.size() == 1
            && near(scaled[0].position[1] - big.position[1], 2 * (still[0].position[1] - b.position[1]), 1e-3f)
            && near(scaled[0].radius, 2 * still[0].radius, 1e-3f) && near(scaled[0].intensity, still[0].intensity),
        "a scaled actor: its light twice as far from the pivot, twice the radius, the same intensity");
    /* A prefab's lights may have an inner cone and barn doors (the doors'
     * length scales with the actor, the angles do not). */
    Json shaped_json;
    check(json_parse("{\"name\": \"Shaped\", \"lights\": [{\"type\": \"rect\", \"barn_angle\": 30, \"barn_length\": 40},"
                     " {\"type\": \"spot\", \"inner\": 25}]}",
              shaped_json),
        "a prefab with barn doors and an inner cone parses");
    Prefab shaped = prefab_from_json("shaped", shaped_json);
    Actor sa;
    sa.prefab = "shaped";
    sa.scale = 2;
    std::vector<EvalLight> se;
    evaluate(shaped, sa, 0, false, se);
    check(shaped.error.empty() && se.size() == 2 && near(se[0].barn_angle, 30) && near(se[0].barn_length, 80)
            && near(se[1].inner, 25),
        "its lights carry them");
    Actor shadowed = b;
    shadowed.fixture_shadows = true;
    evaluate(*candle, shadowed, 0, false, l3);
    check(l3[0].shadow_skip == 0, "unless the fixture casts shadow");
    evaluate(*candle, b, 12.345, true, l1);
    evaluate(*candle, b, 12.345, true, l2);
    evaluate(*candle, b, 12.4, true, l3);
    check(!memcmp(l1[0].position, l2[0].position, sizeof l1[0].position) && l1[0].intensity == l2[0].intensity,
        "flicker is deterministic for a time");
    check(l1[0].intensity != l3[0].intensity, "flicker changes over time");
    float lo = 1e9f, hi = 0, sum = 0;
    const int samples = 4000;
    for (int i = 0; i < samples; i++) {
        evaluate(*candle, b, i * 0.013, true, l3);
        lo = fminf(lo, l3[0].factor), hi = fmaxf(hi, l3[0].factor), sum += l3[0].factor;
    }
    float amount = candle->param("flicker")->def.x();
    printf("      flicker factor %.3f..%.3f, mean %.3f (amount %.2f)\n", lo, hi, sum / samples, amount);
    check(lo >= 1 - amount - 1e-3f && hi <= 1 + amount + 1e-3f && fabsf(sum / samples - 1) < 0.05f,
        "flicker stays within its amount and keeps the mean intensity");
    Actor c = b;
    c.seed = 99;
    evaluate(*candle, c, 12.345, true, l3);
    check(l3[0].intensity != l1[0].intensity, "another seed flickers differently");
    float scale = 0;
    check(glow_drive(*candle, b, 12.345, true, &scale) == 1 && near(scale, l1[0].factor), "the linked glow follows the flicker");

    /* Rotation: facing a wall normal, and back. */
    float n[3] = { 1, 0, 0 }, rot[3], r[3], d[3], f[3];
    rotation_facing(n, rot);
    rotation_basis(rot, r, d, f);
    check(near(f[0], 1, 1e-4f) && near(d[1], 1, 1e-4f), "rotation_facing: forward along the normal, upright");
    float again[3];
    rotation_from_basis(r, d, f, again);
    check(near(again[0], rot[0], 1e-3f) && near(again[1], rot[1], 1e-3f) && near(again[2], rot[2], 1e-3f),
        "rotation round trip");

    /* A user prefab overrides the built-in by id, and live reload sees it. */
    Json user_candle = candle->source;
    user_candle.set("name", Json::str("My candle"));
    check(lib.save_user("candle", user_candle, &error), "user prefab saved");
    const Prefab* mine = lib.find("candle");
    check(mine && mine->user && mine->overrides_builtin && mine->name == "My candle", "user prefab overrides the built-in");
    check(!lib.poll(), "nothing to reload after saving");
    check(lib.revert_user("candle", &error) && !lib.find("candle")->user, "reverting brings the built-in back");

    std::string cleanup = std::string("rm -rf '") + user + "'";
    if (system(cleanup.c_str())) {
    }
    printf("%s\n", failures ? "FAIL" : "PASS: prefabs parse, evaluate deterministically and round-trip");
    return failures ? 1 : 0;
}
