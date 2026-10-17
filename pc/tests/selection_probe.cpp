// The editor's selection model (pc/src/editor_selection.cpp) without the
// game: references from lighting item ids, click / Shift / Ctrl / box
// semantics, the primary, retain, the lock, the change serial and source,
// and the hidden sets with their settings lines.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "editor_selection.h"
#include "editor_picking.h"

using namespace rood_selection;

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool is(const std::vector<std::string>& want)
{
    std::vector<std::string> got;
    for (const Ref& r : items())
        got.push_back(r.id);
    return got == want;
}

static void references()
{
    check(ref_of("light:a1").kind == KIND_LIGHT && ref_of("light:a1").id == "light:a1", "light ids are lights");
    check(ref_of("actor:b2").kind == KIND_ACTOR && ref_of("fog:c3").kind == KIND_VOLUME
            && ref_of("glow:room_12_0,0,0").kind == KIND_GLOW,
        "actor, fog and glow ids by their prefix");
    check(!ref_of("light:") && !ref_of("set:haze_density") && !ref_of(""), "an empty name or another kind is no reference");
    check(ref_of("env:sun").kind == KIND_SUN && !ref_of("env:sky") && !strcmp(kind_name(KIND_SUN), "sun"),
        "the sun is env:sun (the sky is no reference)");
    check(!strcmp(kind_name(KIND_VOLUME), "fog") && !strcmp(source_name(FROM_OUTLINER), "outliner"), "kind and source names");
    check(ref_of("emitter:d4").kind == KIND_EMITTER && ref_of("emitter:d4").id == "emitter:d4" && !ref_of("emitter:")
            && !strcmp(kind_name(KIND_EMITTER), "emitter"),
        "emitter ids are particle emitters (particles.cpp)");
}

static void selecting()
{
    Ref a = ref_of("light:a"), b = ref_of("actor:b"), c = ref_of("fog:c");
    clear(FROM_CODE);
    unsigned s0 = serial();
    set(a, FROM_VIEWPORT);
    check(is({ "light:a" }) && primary() == a && serial() != s0 && last_source() == FROM_VIEWPORT,
        "set: only it, the primary; the serial and the source change");
    click(b, true, false, FROM_VIEWPORT);
    check(is({ "light:a", "actor:b" }) && primary() == b, "Shift+click adds, as the primary");
    click(a, true, false, FROM_VIEWPORT);
    check(is({ "actor:b", "light:a" }) && primary() == a, "Shift+click on a selected object makes it the primary");
    click(b, false, true, FROM_OUTLINER);
    check(is({ "light:a" }) && last_source() == FROM_OUTLINER, "Ctrl+click toggles off");
    click(c, false, true, FROM_VIEWPORT);
    check(is({ "light:a", "fog:c" }) && primary() == c, "Ctrl+click toggles on");
    click(b, false, false, FROM_VIEWPORT);
    check(is({ "actor:b" }), "a plain click replaces");
    box({ a, c }, false, false, FROM_VIEWPORT);
    check(is({ "light:a", "fog:c" }), "a box replaces");
    box({ b }, true, false, FROM_VIEWPORT);
    check(is({ "light:a", "fog:c", "actor:b" }), "Shift+box adds");
    box({ a, b }, false, true, FROM_VIEWPORT);
    check(is({ "fog:c" }), "Ctrl+box toggles each");
    set_all({ a, b, a }, FROM_RPC);
    check(is({ "actor:b", "light:a" }), "set_all: no duplicates, the last named is the primary");
    clear(FROM_CODE);
    unsigned s1 = serial();
    clear(FROM_CODE);
    check(empty() && serial() == s1, "clearing an empty selection is no change");
    set_all({ a, b, c }, FROM_CODE);
    bool went = retain([](const Ref& r) { return r.kind != KIND_ACTOR; }, FROM_CODE);
    check(went && is({ "light:a", "fog:c" }), "retain drops what is gone");
    check(!retain([](const Ref&) { return true; }, FROM_CODE), "retain with everything alive is no change");
    lock(true);
    set(b, FROM_VIEWPORT);
    check(locked() == c && primary() == b, "the lock keeps the primary it was taken on");
    lock(false);
    check(!locked(), "unlocked");
    set(Ref(), FROM_CODE);
    check(empty(), "set with no reference clears");
}

static void hiding()
{
    clear_hidden();
    set_room(9, 5);
    Ref a = ref_of("light:a"), c = ref_of("fog:c");
    hide({ a, c, a });
    check(hidden(a) && hidden(c) && any_hidden() && hidden_items().size() == 2, "hide: each once");
    check(hidden_line(9, 5) == "editor_hidden_9_5 light:a fog:c", "its settings line");
    set_room(9, 6);
    check(!hidden(a) && !any_hidden() && hidden_line(9, 6).empty(), "per room");
    check(parse_hidden("editor_hidden_9_6", " glow:room_1_2,3,4  actor:x\n"), "a settings line reads back");
    check(hidden(ref_of("glow:room_1_2,3,4")) && hidden(ref_of("actor:x")), "with its references");
    check(!parse_hidden("editor_hiddenX", "light:a") && !parse_hidden("editor_hidden_9", "light:a")
            && !parse_hidden("editor_snap_grid", "1"),
        "other keys are not hidden sets");
    FILE* f = tmpfile();
    write_hidden(f);
    rewind(f);
    char text[512];
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    fclose(f);
    check(!strcmp(text, "editor_hidden_9_5 light:a fog:c\neditor_hidden_9_6 glow:room_1_2,3,4 actor:x\n"),
        "every room's line, in room order");
    unhide_all();
    check(!any_hidden() && hidden_line(9, 6).empty() && !hidden_line(9, 5).empty(), "unhide_all: this room only");
    check(parse_hidden("editor_hidden_9_5", "") && hidden_line(9, 5).empty(), "an empty line clears its room");
    clear_hidden();
}

static bool near_value(float a, float b, float eps = 1e-3f) { return fabsf(a - b) <= eps; }

static void picking()
{
    /* A camera at the origin looking down +z, as the game's projection is
     * built (lights.cpp gizmo_matrices): x and y over z, clip w = z, depth
     * 16..65536; a 200 x 200 view at window pixel (100, 50). */
    float view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float zn = 16, zf = 65536, projection[16] = { 0 };
    projection[0] = 1, projection[5] = 1, projection[10] = (zf + zn) / (zf - zn), projection[11] = 1;
    projection[14] = -2 * zf * zn / (zf - zn);
    float rect[4] = { 100, 50, 200, 200 };
    float o[3], d[3];
    check(rood_pick::ray(view, projection, rect, 200, 150, o, d) && near_value(o[0], 0) && near_value(o[1], 0)
            && near_value(o[2], 16, 0.01f) && near_value(d[2], 1),
        "the view's centre: straight ahead, from the near plane");
    rood_pick::ray(view, projection, rect, 250, 150, o, d);
    check(near_value(d[0] / d[2], 0.5f) && near_value(d[1], 0), "half way to the right edge: x / z = 0.5");
    rood_pick::ray(view, projection, rect, 200, 100, o, d);
    check(near_value(d[1] / d[2], 0.5f), "half way up: y / z = 0.5");

    float centre[3] = { 0, 0, 100 }, size[3] = { 20, 20, 20 }, origin[3] = { 0, 0, 16 }, ahead[3] = { 0, 0, 1 };
    check(near_value(rood_pick::volume_hit(origin, ahead, centre, size, 0, false), 74), "a box: entered at its near face");
    check(near_value(rood_pick::volume_hit(origin, ahead, centre, size, 0, true), 74), "a sphere: at its near pole");
    check(near_value(rood_pick::volume_hit(origin, ahead, centre, size, 45, false), 100 - 10 * sqrtf(2) - 16),
        "a box turned 45 degrees: at its edge");
    float tall[3] = { 20, 20, 60 };
    check(near_value(rood_pick::volume_hit(origin, ahead, centre, tall, 0, true), 54), "an ellipsoid: its half size along the ray");
    check(near_value(rood_pick::volume_hit(origin, ahead, centre, tall, 90, true), 74),
        "turned 90 degrees, its long axis lies across the ray");
    float sideways[3] = { 1, 0, 0 }, back[3] = { 0, 0, -1 }, inside[3] = { 0, 0, 100 };
    check(rood_pick::volume_hit(origin, sideways, centre, size, 0, false) < 0, "a ray beside it misses");
    check(rood_pick::volume_hit(origin, back, centre, size, 0, true) < 0, "a ray away from it misses");
    check(rood_pick::volume_hit(inside, ahead, centre, size, 0, false) < 0
            && rood_pick::volume_hit(inside, ahead, centre, size, 0, true) < 0,
        "from inside it: no hit (a camera in a volume does not pick it)");
    float far_centre[3] = { 0, 0, 200 }, big[3] = { 40, 40, 40 };
    rood_pick::ray(view, projection, rect, 200, 150, o, d);
    check(near_value(rood_pick::volume_hit(o, d, far_centre, big, 0, false), 200 - 20 - 16, 0.05f),
        "the ray under the view's centre enters a box ahead of it");
    check(rood_pick::in_rect(5, 5, 0, 0, 10, 10) && rood_pick::in_rect(5, 5, 10, 10, 0, 0)
            && !rood_pick::in_rect(11, 5, 0, 0, 10, 10),
        "box select: inside the rectangle, its corners in either order");
}

int main()
{
    references();
    selecting();
    hiding();
    picking();
    if (failures) {
        printf("FAIL: %d checks\n", failures);
        return 1;
    }
    printf("PASS: selection model, hidden sets and picking math\n");
    return 0;
}
