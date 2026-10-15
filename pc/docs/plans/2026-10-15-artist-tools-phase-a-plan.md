# Artist tools phase A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Give the in-game editor one shared selection (lights, light actors, glows, fog volumes) that follows the artist everywhere (viewport, Outliner, Details, undo, RPC), with fog volumes pickable in the view, box select, Shift/Ctrl, hide/isolate, copy/paste/duplicate, and a Details panel drawn from a property schema with categories, search, a lock, per-value reset, tooltips and a Houdini value ladder.

**Architecture:** A pure selection model (`editor_selection.*`: typed references by lighting item id, click/box semantics, per-room hidden sets) is the single source of truth; `editor_scene.cpp` connects it to the room's objects (index resolution, publishing the per-kind state the older code reads, icons and ray picking, Outliner rows, hide/delete) and `editor_clipboard.cpp` copies through the lighting item format. A property schema (`editor_schema.*`) registers each kind's fields once (lights and light actors there, fog volumes from `atmosphere.cpp`), and `editor_details.cpp` draws Details from it; the RPC methods `editor_select` and `editor_schema` drive both for headless tests.

**Tech Stack:** C++17, Dear ImGui 1.93 + ImGuizmo (vendored), SDL3, the project's JSON (`prefab::Json`), dev RPC (`pc/src/dev_rpc.c`), Python 3 headless RPC tests (`pc/tests/run.py`, `rood_rpc.py`), CTest probes (`pc/CMakeLists.txt`).

---

> **Base update (2026-10-15):** the emissive branch has since merged into main (9364ea0f). Its "Emissive" Details tab (`select[6]` in `dev_overlay.cpp` `details()`) must become an entry in this plan's `TAB_*` enum and `details_tabs` name table, and its viewport click hook in `pc_lights_viewport` (just before `probe_mode`) must stay ahead of the new picking in Tasks 6–7. Branch off main at or after 9364ea0f.


## Ground rules for every task

- No changes under `src/` (the decompilation). Everything here is `pc/`.
- Dear ImGui and ImGuizmo stay; nothing new is vendored; no game-derived assets are committed (test frames stay in `pc/build/tests/`).
- Editor-only state (hidden objects) goes in the user settings (`project_settings.cpp`, keys `editor_hidden_<zone>_<room>`), never in lighting data.
- Every editor action that changes room data names its undo step with `pc_undo_hint`.
- Committed files never mention other game ports by name and never contain absolute home-directory paths.
- Heavy work (builds, game tests) always runs under `flock "$ROOD_HEAVY_LOCK"` (a lock file shared by all concurrent agents, e.g. in your temp directory), tests at `-j 2`.
- Tests use the pinned card `pc/build/test-card1.mcd` (run.py passes it as `--card`); they never read or write `pc/save`, `pc/user` or `pc/lights` (scratch `ROOD_LIGHTS`, `ROOD_SETTINGS` under the test's output directory).
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Windows: `windows.h` is force-included on MinGW (`pc/src/win_compat.h`), so no new identifier may be a Windows macro or typedef (`DELETE`, `FLOAT`, `INT`, `BOOL`, `near`, `far`, `small`, `interface`). Every enum below is prefixed (`KIND_*`, `FROM_*`, `TYPE_*`, `TAB_*`).

## File structure

Created:

| File | Responsibility |
|---|---|
| `pc/src/editor_selection.h`, `.cpp` | The selection model: `Ref` (kind + lighting item id), set/add/toggle/remove/click/box, primary, change serial and source, Details lock, per-room hidden sets and their settings lines. No ImGui, no editor objects. |
| `pc/src/editor_picking.h`, `.cpp` | Picking math: the world ray under a window pixel, ray entry into a yawed box or ellipsoid, rectangle test. No ImGui. |
| `pc/src/editor_scene.h`, `.cpp` | The room's objects as refs: index resolution, labels, frame positions, publishing the legacy per-kind selection, viewport icons and picking, box select, Outliner row sync, hide/isolate, delete, RPC `editor_select`. |
| `pc/src/editor_clipboard.cpp` | Copy / paste / duplicate the selection as lighting entries (`lighting_layers.h` format). |
| `pc/src/editor_schema.h`, `.cpp` | Property schema: `Field`/`Kind`, factories, value helpers (clamp, format, JSON, text), light kinds, light actor kinds (prefab parameters), lint, edits applied to the selection, RPC `editor_schema`. |
| `pc/src/editor_details.h`, `.cpp` | Details drawn from the schema: header with lock, search, categories, label/value/reset rows, tooltips, the value ladder, state for the RPC. |
| `pc/tests/selection_probe.cpp` | CTest probe of the selection model and picking math. |
| `pc/tests/editor_selection_smoke.py` | Headless RPC test: selection, picking, box, Outliner sync, Details follows, hide, clipboard, delete, F. |
| `pc/tests/editor_schema_smoke.py` | Headless RPC test: schema lint, get/set/reset per kind, Details UI, search, lock, value ladder. |

Modified:

| File | Change |
|---|---|
| `pc/src/lights.cpp` | `select_*` forward to the model; viewport records icons, picks through `editor_scene`, box select marquee, hidden objects skipped in drawing and in `pc_lights_world`; Outliner rows; light Details become `light_details_extra`. |
| `pc/src/light_actors.cpp` | Actor Outliner rows through the model; `actor_details` becomes `actor_details_extra` (fields move to the schema); RPC `from_lights`; delete forwards to `editor_scene`. |
| `pc/src/lights_internal.h` | Declares `light_details_extra`, `actor_details_extra`; drops `actor_details`. |
| `pc/src/atmosphere.cpp` | Editor accessors for volumes; selection through the model; volumes always drawn and recorded; fog schema kind; Outliner rows; volume Details extras; hidden volumes left out of rendering. |
| `pc/src/environment_internal.h` | Declares the new `rood_atmo` functions. |
| `pc/src/editor_undo.cpp` | Snapshots hold the selection as refs. |
| `pc/src/lighting_editor.cpp` | `apply_state` keeps the selection by id; Promote selection uses the model. |
| `pc/src/dev_overlay.cpp` | Details tab first (follows the selection), room-wide tabs kept; Outliner sections open/scroll to the selection; fog volumes in the Outliner; H / Shift+H / Ctrl+H, Ctrl+C/V/D; Escape cancels the ladder; Edit menu; help rows; Show > Fog volumes. |
| `pc/src/project_settings.cpp` | Reads and writes `editor_hidden_*` lines. |
| `pc/src/dev_rpc.c` | Routes `editor_select`, `editor_schema`; `editor_ui` replies the Details tab; both are undoable edits. |
| `pc/tools/build_boot.py` | Compiles the six new sources. |
| `pc/CMakeLists.txt` | `selection_probe`. |
| `pc/tests/run.py` | Registers the two smokes (checkpoint suites). |
| `lighting/README.md` | Fog volumes are placed and edited through the Outliner and Details. |

---

### Task 1: Worktree and baseline

**Files:** none changed.

- [ ] **Step 1: Branch in a worktree**

Use superpowers:using-git-worktrees. From the main checkout:

```bash
git worktree add .claude/worktrees/artist-tools-a -b claude/artist-tools-a main
cd .claude/worktrees/artist-tools-a
ln -s ../../../data data && ln -s ../../../disks disks
```

Expected: a new worktree on branch `claude/artist-tools-a`. (Keep RPC socket paths short: the tests already use `/tmp/rood-*.sock`.)

- [ ] **Step 2: Configure CTest if this tree has no build yet**

```bash
test -f pc/build/build.ninja || cmake -S pc -B pc/build -G Ninja
```

Expected: `pc/build/build.ninja` exists.

- [ ] **Step 3: Build the test binary**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
```

Expected: exit status 0; the output ends with a `Built …` line and has no `error:` line.

- [ ] **Step 4: Record the baseline of the suites this plan touches**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest editor light_actors atmosphere fog glow fixture lighting room_browser
```

Expected: a table ending in `N/N passed` (or a known failure list). Write down any suite that already fails; later tasks must not add failures.

---

### Task 2: The selection model

**Files:**
- Create: `pc/src/editor_selection.h`, `pc/src/editor_selection.cpp`, `pc/tests/selection_probe.cpp`
- Modify: `pc/CMakeLists.txt` (after the `fixture_catalog` block, line 45)

- [ ] **Step 1: Write the failing probe**

Create `pc/tests/selection_probe.cpp`:

```cpp
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
    check(!strcmp(kind_name(KIND_VOLUME), "fog") && !strcmp(source_name(FROM_OUTLINER), "outliner"), "kind and source names");
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

int main()
{
    references();
    selecting();
    hiding();
    if (failures) {
        printf("FAIL: %d checks\n", failures);
        return 1;
    }
    printf("PASS: selection model and hidden sets\n");
    return 0;
}
```

Add to `pc/CMakeLists.txt` after line 45 (`set_tests_properties(fixture_catalog ...)`):

```cmake
# The editor's selection model (pc/src/editor_selection.cpp) without the game.
add_executable(selection_probe tests/selection_probe.cpp src/editor_selection.cpp)
target_include_directories(selection_probe PRIVATE src)
target_compile_features(selection_probe PRIVATE cxx_std_17)
target_compile_options(selection_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME selection_probe COMMAND selection_probe)
```

- [ ] **Step 2: Run it and see it fail**

```bash
ninja -C pc/build selection_probe
```

Expected: FAIL: `fatal error: editor_selection.h: No such file or directory` (CMake re-configures first).

- [ ] **Step 3: Write the header**

Create `pc/src/editor_selection.h`:

```cpp
/* The editor's selection (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Selection model"): one set of typed references to the room's objects,
 * shared by the viewport, the Outliner, Details, undo and the RPC. A
 * reference is the object's lighting item id ("light:<id>", "actor:<id>",
 * "fog:<id>", "glow:<key>"; lighting_layers.h), so it survives edits, undo
 * and reloads where list indices would not. The last reference is the
 * primary: the gizmo's and Details' object.
 *
 * Also the objects hidden in the editor (H, Shift+H, Ctrl+H): editor state
 * per room, kept in the user settings (keys editor_hidden_<zone>_<room>),
 * never in the lighting data.
 *
 * No Dear ImGui and no editor objects here (pc/tests/selection_probe.cpp
 * tests it); editor_scene.cpp connects it to the room's objects. */
#ifndef ROOD_EDITOR_SELECTION_H
#define ROOD_EDITOR_SELECTION_H
#include <stdio.h>
#include <functional>
#include <string>
#include <vector>

namespace rood_selection {

enum Kind { KIND_NONE, KIND_LIGHT, KIND_ACTOR, KIND_GLOW, KIND_VOLUME };
struct Ref {
    Kind kind = KIND_NONE;
    std::string id; /* the lighting item id, its prefix included */
    bool operator==(const Ref& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const Ref& o) const { return !(*this == o); }
    explicit operator bool() const { return kind != KIND_NONE; }
};
/* A reference from an item id, by its prefix (KIND_NONE when unknown). */
Ref ref_of(const std::string& id);
const char* kind_name(Kind k); /* "light", "actor", "glow", "fog", "none" */

/* Where a change came from: the Outliner scrolls to the new primary unless
 * the change came from it; Details switches to it unless the change came
 * from Details itself or from undo. */
enum Source { FROM_VIEWPORT, FROM_OUTLINER, FROM_DETAILS, FROM_RPC, FROM_UNDO, FROM_CODE };
const char* source_name(Source s); /* "viewport", "outliner", "details", "rpc", "undo", "code" */

const std::vector<Ref>& items(); /* in the order chosen; the last is the primary */
Ref primary();
bool contains(const Ref& r);
bool empty();
void set(const Ref& r, Source s); /* only r (an empty ref clears) */
void add(const Ref& r, Source s); /* r too, as the primary */
void toggle(const Ref& r, Source s); /* r in or out */
void remove(const Ref& r, Source s);
void clear(Source s);
void set_all(const std::vector<Ref>& refs, Source s); /* the last is the primary */
/* A click on r: plain replaces, Shift adds, Ctrl toggles (Unreal). */
void click(const Ref& r, bool shift, bool ctrl, Source s);
/* A box around refs: plain replaces, Shift adds them, Ctrl toggles each. */
void box(const std::vector<Ref>& refs, bool shift, bool ctrl, Source s);
/* Keep the refs `alive` accepts (objects deleted or reloaded away); true
 * when any went (a change from `s`). */
bool retain(const std::function<bool(const Ref&)>& alive, Source s);
unsigned serial(); /* bumps on every change */
Source last_source();

/* Details' lock: pinned to the primary at the time (lock(false): off). */
void lock(bool on);
Ref locked();

/* ---- hidden in the editor ---- */
void set_room(int zone, int room); /* the room hiding applies to */
bool hidden(const Ref& r);
bool any_hidden(); /* in this room */
void hide(const std::vector<Ref>& refs);
void unhide_all(); /* this room's */
std::vector<Ref> hidden_items(); /* this room's */
/* Settings lines (project_settings.cpp): "editor_hidden_<zone>_<room> <id> ...",
 * one per room with hidden objects. */
std::string hidden_line(int zone, int room); /* "" when none */
void write_hidden(FILE* f);
/* A settings key and the rest of its line: true when it is a hidden set
 * (its room's set replaced). */
bool parse_hidden(const char* key, const char* rest);
void clear_hidden(); /* every room's (the settings file is read again) */

} // namespace rood_selection
#endif
```

- [ ] **Step 4: Write the implementation**

Create `pc/src/editor_selection.cpp`:

```cpp
// The editor's selection and hidden objects (editor_selection.h).
#include "editor_selection.h"

#include <ctype.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <utility>

namespace rood_selection {
namespace {
std::vector<Ref> chosen;
unsigned changes = 1;
Source source = FROM_CODE;
Ref pinned;
std::map<std::pair<int, int>, std::vector<Ref>> hidden_by_room;
std::pair<int, int> current_room(-1, -1);

void changed(Source s)
{
    ++changes;
    source = s;
}
void drop(const Ref& r) { chosen.erase(std::remove(chosen.begin(), chosen.end(), r), chosen.end()); }
} // namespace

Ref ref_of(const std::string& id)
{
    static const struct {
        const char* prefix;
        Kind kind;
    } prefixes[] = { { "light:", KIND_LIGHT }, { "actor:", KIND_ACTOR }, { "glow:", KIND_GLOW }, { "fog:", KIND_VOLUME } };
    for (const auto& p : prefixes) {
        size_t n = strlen(p.prefix);
        if (id.size() > n && !id.compare(0, n, p.prefix))
            return Ref { p.kind, id };
    }
    return Ref();
}
const char* kind_name(Kind k)
{
    return k == KIND_LIGHT ? "light" : k == KIND_ACTOR ? "actor" : k == KIND_GLOW ? "glow" : k == KIND_VOLUME ? "fog" : "none";
}
const char* source_name(Source s)
{
    static const char* const names[] = { "viewport", "outliner", "details", "rpc", "undo", "code" };
    return names[s];
}

const std::vector<Ref>& items() { return chosen; }
Ref primary() { return chosen.empty() ? Ref() : chosen.back(); }
bool contains(const Ref& r) { return std::find(chosen.begin(), chosen.end(), r) != chosen.end(); }
bool empty() { return chosen.empty(); }
void set(const Ref& r, Source s)
{
    chosen.clear();
    if (r)
        chosen.push_back(r);
    changed(s);
}
void add(const Ref& r, Source s)
{
    if (!r)
        return;
    drop(r);
    chosen.push_back(r);
    changed(s);
}
void toggle(const Ref& r, Source s)
{
    if (!r)
        return;
    if (contains(r))
        drop(r);
    else
        chosen.push_back(r);
    changed(s);
}
void remove(const Ref& r, Source s)
{
    if (!contains(r))
        return;
    drop(r);
    changed(s);
}
void clear(Source s)
{
    if (chosen.empty())
        return;
    chosen.clear();
    changed(s);
}
void set_all(const std::vector<Ref>& refs, Source s)
{
    chosen.clear();
    for (const Ref& r : refs)
        if (r) {
            drop(r);
            chosen.push_back(r);
        }
    changed(s);
}
void click(const Ref& r, bool shift, bool ctrl, Source s)
{
    if (ctrl)
        toggle(r, s);
    else if (shift)
        add(r, s);
    else
        set(r, s);
}
void box(const std::vector<Ref>& refs, bool shift, bool ctrl, Source s)
{
    if (!shift && !ctrl)
        chosen.clear();
    for (const Ref& r : refs) {
        if (!r)
            continue;
        if (ctrl && contains(r))
            drop(r);
        else if (!contains(r))
            chosen.push_back(r);
    }
    changed(s);
}
bool retain(const std::function<bool(const Ref&)>& alive, Source s)
{
    size_t before = chosen.size();
    chosen.erase(std::remove_if(chosen.begin(), chosen.end(), [&](const Ref& r) { return !alive(r); }), chosen.end());
    if (chosen.size() == before)
        return false;
    changed(s);
    return true;
}
unsigned serial() { return changes; }
Source last_source() { return source; }

void lock(bool on) { pinned = on ? primary() : Ref(); }
Ref locked() { return pinned; }

void set_room(int zone, int room) { current_room = { zone, room }; }
bool hidden(const Ref& r)
{
    auto it = hidden_by_room.find(current_room);
    return it != hidden_by_room.end() && std::find(it->second.begin(), it->second.end(), r) != it->second.end();
}
bool any_hidden()
{
    auto it = hidden_by_room.find(current_room);
    return it != hidden_by_room.end() && !it->second.empty();
}
void hide(const std::vector<Ref>& refs)
{
    std::vector<Ref>& list = hidden_by_room[current_room];
    for (const Ref& r : refs)
        if (r && std::find(list.begin(), list.end(), r) == list.end())
            list.push_back(r);
    if (list.empty())
        hidden_by_room.erase(current_room);
}
void unhide_all() { hidden_by_room.erase(current_room); }
std::vector<Ref> hidden_items()
{
    auto it = hidden_by_room.find(current_room);
    return it == hidden_by_room.end() ? std::vector<Ref>() : it->second;
}
std::string hidden_line(int zone, int room)
{
    auto it = hidden_by_room.find({ zone, room });
    if (it == hidden_by_room.end() || it->second.empty())
        return "";
    std::string line = "editor_hidden_" + std::to_string(zone) + "_" + std::to_string(room);
    for (const Ref& r : it->second)
        line += " " + r.id;
    return line;
}
void write_hidden(FILE* f)
{
    for (const auto& kv : hidden_by_room) {
        std::string line = hidden_line(kv.first.first, kv.first.second);
        if (!line.empty())
            fprintf(f, "%s\n", line.c_str());
    }
}
bool parse_hidden(const char* key, const char* rest)
{
    int zone, room, used = 0;
    if (strncmp(key, "editor_hidden_", 14) || sscanf(key + 14, "%d_%d%n", &zone, &room, &used) != 2 || key[14 + used])
        return false;
    std::vector<Ref> refs;
    std::string word;
    for (const char* p = rest;; p++) {
        if (*p && !isspace((unsigned char)*p)) {
            word += *p;
            continue;
        }
        Ref r = ref_of(word);
        if (r && std::find(refs.begin(), refs.end(), r) == refs.end())
            refs.push_back(r);
        word.clear();
        if (!*p)
            break;
    }
    if (refs.empty())
        hidden_by_room.erase({ zone, room });
    else
        hidden_by_room[{ zone, room }] = refs;
    return true;
}
void clear_hidden() { hidden_by_room.clear(); }

} // namespace rood_selection
```

- [ ] **Step 5: Run it and see it pass**

```bash
ninja -C pc/build selection_probe && pc/build/selection_probe
```

Expected: every line `ok:`, last line `PASS: selection model and hidden sets`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/editor_selection.h pc/src/editor_selection.cpp pc/tests/selection_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Editor selection model: typed references, click/Shift/Ctrl/box, lock, hidden sets (selection_probe)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Picking math

**Files:**
- Create: `pc/src/editor_picking.h`, `pc/src/editor_picking.cpp`
- Modify: `pc/tests/selection_probe.cpp`, `pc/CMakeLists.txt` (the `selection_probe` line)

- [ ] **Step 1: Add the failing checks**

In `pc/tests/selection_probe.cpp`, add `#include "editor_picking.h"` after `#include "editor_selection.h"`, add this function before `int main()`:

```cpp
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
```

In `main()` call `picking();` after `hiding();` and change the pass line to `printf("PASS: selection model, hidden sets and picking math\n");`. In `pc/CMakeLists.txt` change the probe line to:

```cmake
add_executable(selection_probe tests/selection_probe.cpp src/editor_selection.cpp src/editor_picking.cpp)
```

and its comment to `# The editor's selection model and picking math (pc/src/editor_selection.cpp, editor_picking.cpp) without the game.`

- [ ] **Step 2: Run it and see it fail**

```bash
ninja -C pc/build selection_probe
```

Expected: FAIL: `editor_picking.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `pc/src/editor_picking.h`:

```cpp
/* Picking in the editor's view (editor_scene.cpp): the world ray under a
 * window pixel, where a ray enters a fog volume (a box or an ellipsoid
 * turned about y, placed as atmosphere.cpp and medium.glsl place it), and
 * the window rectangle a box select covers. Plain math
 * (pc/tests/selection_probe.cpp). */
#ifndef ROOD_EDITOR_PICKING_H
#define ROOD_EDITOR_PICKING_H

namespace rood_pick {
/* The ray under window pixel (x, y) of a view (column-major view and
 * projection with clip w > 0 in front, as pc_overlay_view, and its window
 * rectangle x, y, w, h): its origin on the near plane and its unit
 * direction. False when the matrices do not invert. */
bool ray(const float view[16], const float projection[16], const float rect[4], float x, float y, float origin[3],
    float dir[3]);
/* The distance along a ray (unit `dir`) to where it enters a fog volume
 * (centre, full size, yaw in degrees; a box, else an ellipsoid inside the
 * same size), or -1 when it misses it or starts inside it. */
float volume_hit(const float origin[3], const float dir[3], const float centre[3], const float size[3], float yaw,
    bool sphere);
/* Whether (x, y) lies in the rectangle with corners (x0, y0) and (x1, y1), in either order. */
bool in_rect(float x, float y, float x0, float y0, float x1, float y1);
} // namespace rood_pick
#endif
```

- [ ] **Step 4: Write the implementation**

Create `pc/src/editor_picking.cpp`:

```cpp
// Picking math for the editor's view (editor_picking.h).
#include "editor_picking.h"

#include <math.h>

namespace rood_pick {
namespace {
/* The inverse of a column-major 4x4 matrix (cofactors); false when singular. */
bool invert(const double m[16], double out[16])
{
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11]
        - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11]
        + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11]
        - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10]
        + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11]
        + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11]
        - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11]
        + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10]
        - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]
        - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]
        + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]
        - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]
        + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7]
        + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7]
        - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7]
        + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6]
        - m[8] * m[2] * m[5];
    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabs(det) < 1e-30)
        return false;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] / det;
    return true;
}
} // namespace

bool ray(const float view[16], const float projection[16], const float rect[4], float x, float y, float origin[3],
    float dir[3])
{
    if (rect[2] <= 0 || rect[3] <= 0)
        return false;
    double m[16], inv[16]; /* projection * view, in double: the game's far plane is 65536 */
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            double s = 0;
            for (int k = 0; k < 4; k++)
                s += (double)projection[k * 4 + r] * view[c * 4 + k];
            m[c * 4 + r] = s;
        }
    if (!invert(m, inv))
        return false;
    /* Normalized device coordinates as lights.cpp view_project maps them back. */
    double nx = (x - rect[0]) / rect[2] * 2 - 1, ny = 1 - (y - rect[1]) / rect[3] * 2;
    double ends[2][3];
    for (int e = 0; e < 2; e++) {
        double ndc[4] = { nx, ny, e ? 1.0 : -1.0, 1 }, w[4];
        for (int r = 0; r < 4; r++)
            w[r] = inv[r] * ndc[0] + inv[4 + r] * ndc[1] + inv[8 + r] * ndc[2] + inv[12 + r] * ndc[3];
        if (fabs(w[3]) < 1e-12)
            return false;
        for (int k = 0; k < 3; k++)
            ends[e][k] = w[k] / w[3];
    }
    double d[3] = { ends[1][0] - ends[0][0], ends[1][1] - ends[0][1], ends[1][2] - ends[0][2] };
    double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length < 1e-9)
        return false;
    for (int k = 0; k < 3; k++) {
        origin[k] = (float)ends[0][k];
        dir[k] = (float)(d[k] / length);
    }
    return true;
}

float volume_hit(const float origin[3], const float dir[3], const float centre[3], const float size[3], float yaw,
    bool sphere)
{
    float half[3] = { size[0] * 0.5f, size[1] * 0.5f, size[2] * 0.5f };
    if (half[0] <= 0 || half[1] <= 0 || half[2] <= 0)
        return -1;
    float a = yaw * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
    float p[3] = { origin[0] - centre[0], origin[1] - centre[1], origin[2] - centre[2] };
    /* Into the volume's frame (the inverse of atmosphere.cpp's corner()), its half size to 1. */
    float o[3] = { (c * p[0] - s * p[2]) / half[0], p[1] / half[1], (s * p[0] + c * p[2]) / half[2] };
    float d[3] = { (c * dir[0] - s * dir[2]) / half[0], dir[1] / half[1], (s * dir[0] + c * dir[2]) / half[2] };
    if (sphere) {
        float qa = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        float qb = 2 * (o[0] * d[0] + o[1] * d[1] + o[2] * d[2]);
        float qc = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - 1;
        if (qc <= 0 || qa <= 0) /* inside it */
            return -1;
        float disc = qb * qb - 4 * qa * qc;
        if (disc < 0)
            return -1;
        float t = (-qb - sqrtf(disc)) / (2 * qa);
        return t >= 0 ? t : -1;
    }
    float t0 = -1e30f, t1 = 1e30f;
    for (int k = 0; k < 3; k++) {
        if (fabsf(d[k]) < 1e-12f) {
            if (o[k] < -1 || o[k] > 1)
                return -1;
            continue;
        }
        float a0 = (-1 - o[k]) / d[k], a1 = (1 - o[k]) / d[k];
        if (a0 > a1) {
            float swap = a0;
            a0 = a1, a1 = swap;
        }
        t0 = fmaxf(t0, a0);
        t1 = fminf(t1, a1);
        if (t0 > t1)
            return -1;
    }
    return t0 >= 0 ? t0 : -1; /* behind it, or inside it */
}

bool in_rect(float x, float y, float x0, float y0, float x1, float y1)
{
    return x >= fminf(x0, x1) && x <= fmaxf(x0, x1) && y >= fminf(y0, y1) && y <= fmaxf(y0, y1);
}
} // namespace rood_pick
```

- [ ] **Step 5: Run it and see it pass**

```bash
ninja -C pc/build selection_probe && pc/build/selection_probe
```

Expected: last line `PASS: selection model, hidden sets and picking math`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/editor_picking.h pc/src/editor_picking.cpp pc/tests/selection_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Editor picking math: the ray under a pixel, ray entry into yawed boxes and ellipsoids (selection_probe)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Fog volumes for the editor (accessors, no behaviour change)

**Files:**
- Modify: `pc/src/environment_internal.h` (the `rood_atmo` namespace, lines 26-32), `pc/src/atmosphere.cpp` (before `namespace rood_atmo {` at line 401; `add_items` lines 474-501; after `pc_atmosphere_viewport`, before line 1622 `/* ---- RPC environment`)
- Test: existing `atmosphere_smoke`, `fog_volume_smoke`

- [ ] **Step 1: Declare the accessors**

In `pc/src/environment_internal.h` replace the `rood_atmo` block with:

```cpp
namespace rood_atmo {
void reset();
bool apply_item(const lighting::Item& it); /* env fog, fog volumes */
void add_items(lighting::State& s);
bool rpc(const prefab::Json& params); /* RPC environment {fog, volumes, ...}: true when the room changed */
std::string json(); /* "fog":{...},"volumes":[...] for its reply */
/* The room's fog volumes for the editor (editor_scene.cpp, editor_schema.cpp,
 * editor_clipboard.cpp): by index, as lighting items ("fog:<id>"), picked by
 * a ray, copied. */
int volume_count();
std::string volume_id(int index); /* "fog:<id>" (an id assigned on the spot; "" out of range) */
int volume_index(const std::string& item_id); /* -1: none */
std::string volume_label(int index); /* "Box 2: Cellar mist" */
void set_selected(int index); /* the selection's volume: its gizmo (editor_scene.cpp publish) */
bool volume_centre(int index, float out[3], float* radius);
float volume_hit(int index, const float origin[3], const float dir[3]); /* rood_pick::volume_hit; -1 */
bool volumes_shown(); /* Show > Fog volumes */
lighting::Item volume_item(int index);
int add_volume_item(const lighting::Item& item, const float offset[3]); /* a copy with a new id: its index, or -1 */
void remove_volume(int index);
} // namespace rood_atmo
```

- [ ] **Step 2: One volume as an item**

In `pc/src/atmosphere.cpp` add `#include "editor_picking.h"` after `#include "editor_overlays.h"`. Directly before `namespace rood_atmo {` (line 401) add:

```cpp
namespace {
/* A volume as its lighting item: with a preset, only what differs from it
 * (a preset edit updates every volume using it). */
lighting::Item item_of(Volume& v)
{
    if (v.id.empty())
        v.id = lighting::new_name();
    lighting::Item it;
    it.kind = "fog", it.name = v.id;
    if (!v.name.empty())
        it.set("name", v.name);
    it.set("pos", vec3(v.pos, 1));
    it.set("yaw", num(v.yaw, 2));
    it.set("enabled", v.enabled ? "1" : "0");
    /* An unknown preset keeps every value. */
    const Preset* preset = v.preset.empty() ? nullptr : find_preset(v.preset);
    if (!v.preset.empty())
        it.set("preset", v.preset);
    for (const auto& f : volume_fields(v)) {
        if (preset) {
            const std::string* p = preset->get(f.first);
            std::string base = p ? *p : lighting::field_default("fog", f.first);
            if (lighting::canonical_value("fog", f.first, f.second) == lighting::canonical_value("fog", f.first, base))
                continue;
        }
        it.set(f.first.c_str(), f.second);
    }
    lighting::canonicalize(it);
    return it;
}
} // namespace
```

In `rood_atmo::add_items` replace the whole `for (Volume& v : volumes) { ... }` loop (lines 474-501) with:

```cpp
    for (Volume& v : volumes) {
        lighting::Item it = item_of(v);
        s[it.id()] = it;
    }
```

- [ ] **Step 3: The accessors**

In `pc/src/atmosphere.cpp`, after the closing brace of `pc_atmosphere_viewport` and before `/* ---- RPC environment {fog, volumes, fog_time}`, add:

```cpp
/* ---- the room's fog volumes for the editor (environment_internal.h) ---- */

int rood_atmo::volume_count() { return (int)volumes.size(); }
std::string rood_atmo::volume_id(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return "";
    if (volumes[index].id.empty())
        volumes[index].id = lighting::new_name();
    return "fog:" + volumes[index].id;
}
int rood_atmo::volume_index(const std::string& item_id)
{
    if (item_id.size() <= 4 || item_id.compare(0, 4, "fog:"))
        return -1;
    for (int i = 0; i < (int)volumes.size(); i++)
        if (!item_id.compare(4, std::string::npos, volumes[i].id))
            return i;
    return -1;
}
std::string rood_atmo::volume_label(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return "";
    const Volume& v = volumes[index];
    char label[128];
    snprintf(label, sizeof label, "%s %d%s%s", v.sphere ? "Sphere" : "Box", index + 1, v.name.empty() ? "" : ": ",
        v.name.c_str());
    return label;
}
void rood_atmo::set_selected(int index) { selected = index >= 0 && index < (int)volumes.size() ? index : -1; }
bool rood_atmo::volume_centre(int index, float out[3], float* radius)
{
    if (index < 0 || index >= (int)volumes.size())
        return false;
    const Volume& v = volumes[index];
    memcpy(out, v.pos, sizeof v.pos);
    *radius = 0.5f * sqrtf(v.size[0] * v.size[0] + v.size[1] * v.size[1] + v.size[2] * v.size[2]);
    return true;
}
float rood_atmo::volume_hit(int index, const float origin[3], const float dir[3])
{
    if (index < 0 || index >= (int)volumes.size() || !show_volumes)
        return -1;
    const Volume& v = volumes[index];
    return rood_pick::volume_hit(origin, dir, v.pos, v.size, v.yaw, v.sphere);
}
bool rood_atmo::volumes_shown() { return show_volumes; }
extern "C" bool* pc_atmosphere_show_volumes(void) { return &show_volumes; }
lighting::Item rood_atmo::volume_item(int index) { return item_of(volumes[index]); }
int rood_atmo::add_volume_item(const lighting::Item& item, const float offset[3])
{
    if (item.kind != "fog")
        return -1;
    lighting::Item copy = item;
    copy.name = lighting::new_name();
    if (!apply_item(copy))
        return -1;
    Volume& v = volumes.back();
    for (int k = 0; k < 3; k++)
        v.pos[k] += offset[k];
    return (int)volumes.size() - 1;
}
void rood_atmo::remove_volume(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    volumes.erase(volumes.begin() + index);
    if (selected == index)
        selected = -1;
    else if (selected > index)
        --selected;
}
```

- [ ] **Step 4: Build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
```

Expected: exit 0, a `Built …` line, no `error:`.

- [ ] **Step 5: The atmosphere suites stay green**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 atmosphere_smoke fog_volume_smoke
```

Expected: `PASS` for both (save/load in `atmosphere_smoke` checks the unchanged item format).

- [ ] **Step 6: Commit**

```bash
git add pc/src/environment_internal.h pc/src/atmosphere.cpp
git commit -m "$(cat <<'EOF'
Fog volumes for the editor: index/id/label/centre/ray-hit accessors, one volume as its item

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: The selection becomes the source of truth

**Files:**
- Create: `pc/src/editor_scene.h`, `pc/src/editor_scene.cpp`, `pc/tests/editor_selection_smoke.py`
- Modify: `pc/src/lights.cpp` (include block line 40; `select_*` lines 263-295; `pc_lights_selected_position` lines 1430-1452; Outliner rows lines 1774-1784; glow rows lines 2083-2088), `pc/src/light_actors.cpp` (include block line 21; `actors_outliner` lines 890-920; RPC `from_lights` lines 2081-2083), `pc/src/atmosphere.cpp` (includes; `add_volume` lines 1236-1237; volume list rows lines 1500-1509; Delete volume lines 1579-1583; RPC `select_volume` lines 1990-1997), `pc/src/editor_undo.cpp` (includes; `Snapshot` lines 68-71; `capture` lines 136-138; `apply` lines 373-386), `pc/src/lighting_editor.cpp` (`apply_state` lines 104-105 and 200-206; `pc_lighting_file_menu` lines 568-579), `pc/src/dev_overlay.cpp` (includes; `pc_overlay_frame` line 1635), `pc/src/dev_rpc.c` (doc comment near line 251; method chain near line 2247; `edits()` line 3060), `pc/tools/build_boot.py` (line 900), `pc/tests/run.py` (after `editor_undo_smoke`, line 152; `CHECKPOINT_SUITES` line 389)

- [ ] **Step 1: Write the failing test**

Create `pc/tests/editor_selection_smoke.py`:

```python
#!/usr/bin/env python3
"""The editor's selection (pc/src/editor_selection.h, editor_scene.cpp): one
set of typed references (light, light actor, glow, fog volume) shared by the
viewport, the Outliner, Details, undo and the RPC.

    python3 pc/tests/editor_selection_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room with the editor open, a scratch lights directory
(ROOD_LIGHTS) and a scratch settings file (ROOD_SETTINGS). A point light, a
candle and a fog volume are placed around Ashley; each section in SECTIONS
checks one part:

- rpc: the RPC's selection is what the lights, actors, glows and fog
  volumes report as selected; unknown ids are refused; undo restores it.
"""
import argparse
import math
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import golden_frames as gf  # noqa: E402

VOLUME = "seltest0000001"  # the fog volume's id (item fog:<id>)


def pinned_card():
    """The tests' pinned card (run.py default_save): pc/build/test-card1.mcd, here or in the main checkout."""
    roots = [ROOT]
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    if common:
        roots.append(pathlib.Path(common).parent)
    for root in roots:
        if (root / "pc/build/test-card1.mcd").exists():
            return root / "pc/build/test-card1.mcd"
    return ROOT / "pc/build/test-card1.mcd"


def centre(rect):
    return [rect[0] + rect[2] / 2, rect[1] + rect[3] / 2]


def distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


class Session:
    """A game in the save room with the editor open and three objects placed around Ashley."""

    def __init__(self, game, out):
        self.game, self.out, self.failures = game, out, []
        self.light = self.actor = self.volume = None
        self.player = None
        self.zone = self.room = None
        self.lights_dir = self.settings = None

    def check(self, ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            self.failures.append(message)
        return ok

    def select(self, **params):
        return self.game.call("editor_select", **params)

    def objects(self):
        return {o["id"]: o for o in self.select()["objects"]}

    def keys(self, names, **kw):
        self.game.call("editor_input", keys=names, **kw)
        self.game.run(4)  # the events reach the overlay over the next frames
        return self.game.call("editor_input")

    def mouse(self, xy, button=1, down=True):
        """Real SDL mouse events (RPC system mouse): the pointer to window pixel xy, then the button down or up."""
        self.game.call("system", mouse=[float(xy[0]), float(xy[1])], button=button, down=down)
        self.game.run(3)

    def click(self, xy, button=1):
        self.mouse(xy, button, True)
        self.mouse(xy, button, False)

    def index(self, method, ident):
        return [x["id"] for x in self.game.call(method)[method]].index(ident)

    def history(self):
        return self.game.call("editor_undo", action="list")

    def place(self):
        """A point light, a candle and a fog volume around Ashley."""
        g, p = self.game, self.player
        self.light = g.call("lights", action="add", position=[p["x"] - 160, p["y"] - 140, p["z"]])["lights"][-1]["id"]
        g.call("actors", action="add", prefab="candle", position=[p["x"] + 160, p["y"] - 120, p["z"]], seed=3)
        self.actor = g.call("actors")["actors"][-1]["id"]
        self.volume = "fog:" + VOLUME
        g.call("environment", volumes=[{"id": VOLUME, "pos": [p["x"], p["y"] - 64, p["z"] + 320],
                                        "size": [160, 128, 160]}])
        g.run(6)
        return True


def start(args, name):
    """Launch into the save room, editor open, objects placed: a Session, or None to skip."""
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return None
    out = pathlib.Path(args.out) if args.out else OUT / name
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    # A settings file with an aa line: no first-run detection (lighting keeps its built-in values).
    settings.write_text(f"# {name}: scratch settings\naa fxaa\n")
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_SETTINGS": str(settings), "ROOD_SCALE": "2"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-{name[7:12]}-{os.getpid()}.sock"  # sun_path is short
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    s = Session(game, out)
    s.lights_dir, s.settings = lights, settings
    game.call("graphics", **{**gf.DEFAULTS, "scale": 2, "editor_cutaway": False, "editor_snap_point": False,
                             "editor_snap_edge": False, "editor_snap_surface": False})
    state = game.state()
    s.player = state.get("player")
    if not s.player or not game.call("scene")["triangles"]:
        s.check(False, f"no room with Ashley after Continue: {state}")
        return s
    s.zone, s.room = state["zone"], state["room"]
    game.call("editor_ui", visible=True)
    game.run(6)
    s.check(s.place(), "the light, the candle and the fog volume each have their own spot on screen")
    game.run(6)
    return s


def finish(s, label):
    """Close the game; the exit status (a crash or shader error fails)."""
    s.game.close()
    shutil.rmtree(s.lights_dir, ignore_errors=True)
    log = pathlib.Path(s.game.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", s.game.log)
        return 1
    if s.failures:
        print(f"FAIL: {len(s.failures)} checks")
        return 1
    print("PASS: " + label)
    return 0


def check_rpc(s):
    """The RPC's selection is what every kind's own state reports; undo restores it."""
    g = s.game
    r = s.select(action="set", ids=[s.light])
    st = g.call("editor_place", phase="state")
    s.check(r["selection"] == [s.light] and r["primary"] == s.light and r["source"] == "rpc"
            and st["selected_light"] == s.index("lights", s.light) and st["selected_actor"] == -1,
            f"set: the light is the selection and the lights' own selected index ({r['selection']}, {st})")
    r = s.select(action="add", ids=[s.actor])
    st = g.call("editor_place", phase="state")
    s.check(r["selection"] == [s.light, s.actor] and st["selected_actor"] == s.index("actors", s.actor)
            and st["selected_light"] == -1, "add: the actor joins as the primary (the gizmo's object)")
    r = s.select(action="toggle", ids=[s.light])
    s.check(r["selection"] == [s.actor], f"toggle: the light goes ({r['selection']})")
    g.call("actors", action="select", index=s.index("actors", s.actor))
    s.check(s.select()["selection"] == [s.actor], "the actors RPC's select is the selection")
    g.call("environment", select_volume=VOLUME)
    r = s.select()
    s.check(r["selection"] == [s.volume], f"environment select_volume: the fog volume ({r['selection']})")
    glows = [x for x in g.call("glows")["glows"] if x["category"] != "effect"]
    if glows:
        g.call("glows", action="select", id=glows[0]["id"])
        g.run(2)
        r = s.select()
        s.check(len(r["selection"]) == 1 and r["primary"].startswith("glow:")
                and g.call("glows")["selected"] == glows[0]["id"],
                f"glows select: the glow, by its key ({r['selection']})")
    try:
        s.select(action="set", ids=["light:nosuchlight"])
        s.check(False, "an unknown id is refused")
    except RpcError:
        s.check(True, "an unknown id is refused")
    s.select(action="set", ids=[s.actor])
    s.keys(["delete"])
    gone = s.actor not in [a["id"] for a in g.call("actors")["actors"]]
    s.check(gone and s.select()["selection"] == [], "Delete: the actor goes, and with it the selection")
    g.call("editor_undo", action="undo")
    r = s.select()
    s.check(r["selection"] == [s.actor], f"undo: the actor and its selection are back ({r['selection']})")


SECTIONS = [check_rpc]  # in order: later sections start from where earlier ones leave the room


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (rpc, picking, ...)")
    args = parser.parse_args()
    s = start(args, "editor_selection")
    if s is None:
        return 77
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                section(s)
    finally:
        status = finish(s, "the selection follows the RPC, clicks, boxes and the Outliner; Details shows it; "
                           "hiding, copying and deleting act on it")
    return status


if __name__ == "__main__":
    sys.exit(main())
```

Register it in `pc/tests/run.py` right after the `editor_undo_smoke` Suite (line 152):

```python
            Suite("editor_selection_smoke", [PY, str(TESTS / "editor_selection_smoke.py"), "--card", str(args.save)],
                  420, note="editor selection: RPC, clicks (lights, actors, glows, fog volumes by ray), Shift/Ctrl, "
                            "box select, Outliner scroll, Details follows, hide/isolate, copy/paste/duplicate, "
                            "delete, F"),
```

and add `"editor_selection_smoke",` to `CHECKPOINT_SUITES` after `"editor_undo_smoke",`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)` for `editor_selection_smoke`; `pc/build/tests/editor_selection_smoke/output.log` ends in an `RpcError` naming `unknown method editor_select`.

- [ ] **Step 3: The scene header**

Create `pc/src/editor_scene.h`:

```cpp
/* The editor's objects for its selection (editor_selection.h): the room's
 * lights, light actors, the game's glows and fog volumes as references,
 * their list indices and labels; the per-kind state the editor's older code
 * reads (lights_internal.h selected, selected_actor, multi, glow_selected,
 * glow_id; atmosphere.cpp's selected volume) kept in step with the
 * selection; the viewport's icons and picking (an icon, else a ray against
 * the fog volumes) and box select; the Outliner's rows following the
 * selection; hiding, deleting, copying and pasting the selection
 * (editor_clipboard.cpp); the RPC editor_select. Editor internals. */
#ifndef ROOD_EDITOR_SCENE_H
#define ROOD_EDITOR_SCENE_H
#include <stdint.h>
#include <string>
#include <vector>

#include "editor_overlays.h"
#include "editor_selection.h"

namespace rood_scene {
using rood_selection::Ref;

/* ---- references (light and actor ids are assigned on the spot) ---- */
Ref light_ref(int index);
Ref actor_ref(int index);
Ref volume_ref(int index);
Ref glow_ref(uint32_t id); /* by its key; "glow:@x,y,z" when it has none */
Ref glow_ref_key(const char* key, const float position[3]);
/* A light's, actor's or volume's list index, a glow's id; -1 when it is gone. */
int index_of(const Ref& r);
bool exists(const Ref& r); /* glows always: they come and go with the game */
std::string label(const Ref& r); /* "Point light 3", "Candle 2", "Box 1: Cellar mist", a glow's name */
bool position(const Ref& r, float out[3], float* radius); /* where F frames it */

/* ---- in step ---- */
void frame(); /* each overlay frame: the room, refs to what is gone dropped, publish */
void refresh(); /* refs to what is gone dropped, then publish */
void publish(); /* the per-kind state from the selection */

/* ---- the Outliner ---- */
void row_clicked(const Ref& r); /* a row's click: Shift adds, Ctrl toggles, else selects */
void row_drawn(const Ref& r); /* after a row's item: the primary's row is scrolled to; its visibility kept (RPC) */
/* Whether the section holding kind a (or b) must open: the primary is one and a scroll to it is due. */
bool scroll_due(rood_selection::Kind a, rood_selection::Kind b = rood_selection::KIND_NONE);

/* ---- the viewport ---- */
void icons_begin(); /* lights.cpp, each frame before drawing */
/* An object's icon at window pixel (x, y): pickable when radius > 0; the
 * anchor is where box select and the RPC see it. */
void icon(const Ref& r, float x, float y, float radius, bool anchor);
Ref object_at(float x, float y); /* the icon under the pixel: lights first, then actors, then glows */
/* The fog volume a ray from the pixel enters first, unless a room surface is in front of it. */
Ref volume_at(const pc_overlay_view& v, pc_overlay_pick pick, float x, float y);
std::vector<Ref> in_box(float x0, float y0, float x1, float y1); /* lights, actors, volumes (not glows) */
/* A click in the view at (x, y) as the RPC makes it: what is under it is
 * selected (Shift adds, Ctrl toggles); a plain click on nothing clears. */
Ref click_at(float x, float y, bool shift, bool ctrl, rood_selection::Source s);

/* ---- hidden in the editor: no icon, not pickable, and (while the editor
 * shows) their light and fog left out ---- */
bool light_hidden(int index);
bool actor_hidden(int index);
bool volume_hidden_id(const std::string& id); /* by a volume's bare id */
bool glow_hidden(const char* key, const float position[3]);
void hide_selection(); /* H */
void isolate_selection(); /* Shift+H: everything else */
void unhide_all(); /* Ctrl+H */

/* ---- operations on the selection ---- */
int delete_selection(); /* lights, actors and volumes; how many went */
/* editor_clipboard.cpp: the selection as lighting entries (lighting_layers.h), and back as new objects. */
std::string copy_text();
int copy_selection(); /* Ctrl+C: the editor's clipboard and the system's */
const std::string& clipboard();
int paste_text(const std::string& text, const float offset[3], const char* verb);
int paste_clipboard(); /* Ctrl+V: where they were copied */
int duplicate_selection(); /* Ctrl+D: 64 units along +x */
} // namespace rood_scene

extern "C" int pc_editor_select_rpc(const char* params, char* out, int size);
#endif
```

(Functions declared here and not yet written are written in Tasks 6-17; nothing calls them before then.)

- [ ] **Step 4: The scene's core and the RPC**

Create `pc/src/editor_scene.cpp`:

```cpp
// The editor's objects for its selection (editor_scene.h).
#include "editor_scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_picking.h"
#include "environment_internal.h"
#include "glow_overrides.h"
#include "lights_internal.h"

extern "C" int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick); /* lights.cpp */

using rood_selection::Ref;
using namespace rood_lights;

namespace {
/* An object's icon in the view this frame (lights.cpp, atmosphere.cpp). */
struct Icon {
    Ref ref;
    float x, y, radius;
    bool anchor;
};
std::vector<Icon> icons;
/* The primary's Outliner row as last drawn (RPC). */
std::string row_target;
bool row_visible;
int row_frame = -1;

int frame_count() { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1; }
std::string glow_at_id(const float p[3])
{
    char text[64];
    snprintf(text, sizeof text, "glow:@%.0f,%.0f,%.0f", p[0], p[1], p[2]);
    return text;
}
/* A glow reference's glow now: by its key, or the one nearest its position. */
uint32_t glow_id_of(const Ref& r)
{
    if (r.kind != rood_selection::KIND_GLOW || r.id.size() <= 5)
        return 0;
    const char* key = r.id.c_str() + 5;
    float p[3];
    if (key[0] == '@')
        return sscanf(key + 1, "%f,%f,%f", &p[0], &p[1], &p[2]) == 3 ? pc_gi_glow_near(p, 96) : 0;
    return pc_gi_glow_by_key(key);
}
const Icon* anchor_of(const Ref& r)
{
    for (const Icon& c : icons)
        if (c.anchor && c.ref == r)
            return &c;
    return nullptr;
}
/* Every object of the room: lights, actors, fog volumes, the game's glows (not effects). */
std::vector<Ref> all_objects()
{
    std::vector<Ref> out;
    for (int i = 0; i < (int)lights.size(); i++)
        out.push_back(rood_scene::light_ref(i));
    for (int i = 0; i < (int)actors.size(); i++)
        out.push_back(rood_scene::actor_ref(i));
    for (int i = 0; i < rood_atmo::volume_count(); i++)
        out.push_back(rood_scene::volume_ref(i));
    static pc_glow_info glows[160];
    int n = pc_gi_glow_list(glows, 160);
    for (int i = 0; i < n; i++)
        if (glows[i].category != PC_GLOW_EFFECT)
            out.push_back(rood_scene::glow_ref_key(glows[i].key, glows[i].position));
    return out;
}
} // namespace

/* ---- references ---- */

Ref rood_scene::light_ref(int index)
{
    if (index < 0 || index >= (int)lights.size())
        return Ref();
    if (lights[index].id.empty())
        ensure_ids();
    return Ref { rood_selection::KIND_LIGHT, light_item_id(index) };
}
Ref rood_scene::actor_ref(int index)
{
    if (index < 0 || index >= (int)actors.size())
        return Ref();
    if (actors[index].id.empty())
        ensure_ids();
    return Ref { rood_selection::KIND_ACTOR, actor_item_id(index) };
}
Ref rood_scene::volume_ref(int index)
{
    std::string id = rood_atmo::volume_id(index);
    return id.empty() ? Ref() : Ref { rood_selection::KIND_VOLUME, id };
}
Ref rood_scene::glow_ref_key(const char* key, const float position[3])
{
    return Ref { rood_selection::KIND_GLOW, key && key[0] ? std::string("glow:") + key : glow_at_id(position) };
}
Ref rood_scene::glow_ref(uint32_t id)
{
    pc_glow_info g;
    return id && pc_gi_glow_get(id, &g) ? glow_ref_key(g.key, g.position) : Ref();
}
int rood_scene::index_of(const Ref& r)
{
    switch (r.kind) {
    case rood_selection::KIND_LIGHT:
        for (size_t i = 0; i < lights.size(); i++)
            if (r.id.size() > 6 && !r.id.compare(6, std::string::npos, lights[i].id))
                return (int)i;
        return -1;
    case rood_selection::KIND_ACTOR:
        for (size_t i = 0; i < actors.size(); i++)
            if (r.id.size() > 6 && !r.id.compare(6, std::string::npos, actors[i].id))
                return (int)i;
        return -1;
    case rood_selection::KIND_VOLUME:
        return rood_atmo::volume_index(r.id);
    case rood_selection::KIND_GLOW: {
        uint32_t id = glow_id_of(r);
        return id ? (int)id : -1;
    }
    default:
        return -1;
    }
}
bool rood_scene::exists(const Ref& r) { return r.kind == rood_selection::KIND_GLOW || index_of(r) >= 0; }
std::string rood_scene::label(const Ref& r)
{
    int i = index_of(r);
    char text[160];
    switch (r.kind) {
    case rood_selection::KIND_LIGHT:
        if (i < 0)
            break;
        snprintf(text, sizeof text, "%s %d", light_type_name(lights[i].type), i + 1);
        return text;
    case rood_selection::KIND_ACTOR: {
        if (i < 0)
            break;
        const prefab::Prefab* p = prefab::library().find(actors[i].prefab);
        snprintf(text, sizeof text, "%s %d", p ? p->name.c_str() : actors[i].prefab.c_str(), i + 1);
        return text;
    }
    case rood_selection::KIND_VOLUME:
        if (i >= 0)
            return rood_atmo::volume_label(i);
        break;
    case rood_selection::KIND_GLOW: {
        pc_glow_info g;
        if (i > 0 && pc_gi_glow_get((uint32_t)i, &g))
            return g.name;
        return "Glow";
    }
    default:
        break;
    }
    return r.id;
}
bool rood_scene::position(const Ref& r, float out[3], float* radius)
{
    int i = index_of(r);
    switch (r.kind) {
    case rood_selection::KIND_LIGHT:
        if (i < 0)
            return false;
        memcpy(out, lights[i].position, sizeof lights[i].position);
        *radius = lights[i].radius;
        return true;
    case rood_selection::KIND_ACTOR: {
        if (i < 0)
            return false;
        memcpy(out, actors[i].position, sizeof actors[i].position);
        std::vector<prefab::EvalLight> ls;
        actor_lights(actors[i], ls, false);
        *radius = 200;
        for (const prefab::EvalLight& e : ls)
            *radius = fmaxf(*radius, fminf(e.radius, 600));
        return true;
    }
    case rood_selection::KIND_VOLUME:
        return rood_atmo::volume_centre(i, out, radius);
    case rood_selection::KIND_GLOW: { /* a game glow: where it is now, a nominal radius */
        pc_glow_info g;
        if (i > 0 && pc_gi_glow_get((uint32_t)i, &g))
            memcpy(out, g.position, 3 * sizeof(float));
        else if (r.id.size() <= 6 || r.id[5] != '@' || sscanf(r.id.c_str() + 6, "%f,%f,%f", &out[0], &out[1], &out[2]) != 3)
            return false;
        *radius = 200;
        return true;
    }
    default:
        return false;
    }
}

/* ---- in step ---- */

void rood_scene::publish()
{
    Ref p = rood_selection::primary();
    selected = p.kind == rood_selection::KIND_LIGHT ? index_of(p) : -1;
    selected_actor = p.kind == rood_selection::KIND_ACTOR ? index_of(p) : -1;
    multi.clear(); /* the other selected lights (Create prefab) */
    for (const Ref& r : rood_selection::items())
        if (r.kind == rood_selection::KIND_LIGHT && r != p) {
            int i = index_of(r);
            if (i >= 0)
                multi.push_back(i);
        }
    glow_selected = p.kind == rood_selection::KIND_GLOW;
    glow_id = glow_selected ? glow_id_of(p) : 0;
    if (glow_selected) {
        pc_glow_info g;
        if (glow_id && pc_gi_glow_get(glow_id, &g))
            memcpy(glow_at, g.position, sizeof glow_at);
        else if (p.id.size() > 6 && p.id[5] == '@')
            sscanf(p.id.c_str() + 6, "%f,%f,%f", &glow_at[0], &glow_at[1], &glow_at[2]);
    }
    rood_atmo::set_selected(p.kind == rood_selection::KIND_VOLUME ? index_of(p) : -1);
}
void rood_scene::refresh()
{
    rood_selection::retain([](const Ref& r) { return exists(r); }, rood_selection::FROM_CODE);
    Ref pinned = rood_selection::locked();
    if (pinned && !exists(pinned))
        rood_selection::lock(false);
    publish();
}
void rood_scene::frame()
{
    track_room();
    rood_selection::set_room(zone, room);
    refresh();
}

/* ---- the Outliner ---- */

void rood_scene::row_clicked(const Ref& r)
{
    ImGuiIO& io = ImGui::GetIO();
    rood_selection::click(r, io.KeyShift, io.KeyCtrl, rood_selection::FROM_OUTLINER);
    publish();
}

/* ---- RPC editor_select ---- */

namespace {
using prefab::Json;
std::string quote(const std::string& s) { return prefab::json_quote(s); }
int rpc_fail(const std::string& message, char* out, int size)
{
    snprintf(out, size, "%s", message.c_str());
    return -1;
}
bool flag(const Json& params, const char* key)
{
    const Json* j = params.get(key);
    return j && (j->type == Json::Bool ? j->boolean : j->number != 0);
}
std::string refs_json(const std::vector<Ref>& refs)
{
    std::string s = "[";
    for (size_t i = 0; i < refs.size(); i++)
        s += (i ? "," : "") + quote(refs[i].id);
    return s + "]";
}
/* The selection, the Outliner's row, the hidden set, the view and every object (no closing brace). */
std::string state_json()
{
    Ref p = rood_selection::primary(), pinned = rood_selection::locked();
    std::string s = "{\"zone\":" + std::to_string(zone) + ",\"room\":" + std::to_string(room)
        + ",\"selection\":" + refs_json(rood_selection::items()) + ",\"primary\":" + (p ? quote(p.id) : "null")
        + ",\"serial\":" + std::to_string(rood_selection::serial()) + ",\"source\":\""
        + rood_selection::source_name(rood_selection::last_source()) + "\",\"locked\":" + (pinned ? quote(pinned.id) : "null")
        + ",\"outliner\":{\"target\":" + (row_frame >= 0 && row_frame == frame_count() ? quote(row_target) : "null")
        + ",\"visible\":" + (row_visible ? "true" : "false") + "},\"hidden\":" + refs_json(rood_selection::hidden_items())
        + ",\"hidden_setting\":" + quote(rood_selection::hidden_line(zone, room));
    pc_overlay_view v;
    if (pc_lights_tools_view(&v, nullptr)) {
        char rect[128];
        snprintf(rect, sizeof rect, ",\"view\":[%.1f,%.1f,%.1f,%.1f]", v.rect[0], v.rect[1], v.rect[2], v.rect[3]);
        s += rect;
    }
    s += ",\"objects\":[";
    bool first = true;
    for (const Ref& r : all_objects()) {
        const Icon* c = anchor_of(r);
        char screen[64] = "null";
        if (c)
            snprintf(screen, sizeof screen, "[%.1f,%.1f]", c->x, c->y);
        s += std::string(first ? "" : ",") + "{\"id\":" + quote(r.id) + ",\"kind\":\"" + rood_selection::kind_name(r.kind)
            + "\",\"label\":" + quote(rood_scene::label(r)) + ",\"screen\":" + screen
            + ",\"hidden\":" + (rood_selection::hidden(r) ? "true" : "false")
            + ",\"selected\":" + (rood_selection::contains(r) ? "true" : "false") + "}";
        first = false;
    }
    return s + "],\"status\":" + quote(status);
}
} // namespace

/* RPC editor_select (dev_rpc.c hands over the params object's text):
 * action list (default), set / add / toggle / remove {ids}, clear; source
 * "viewport" | "outliner" | "details" | "rpc" (default) says where the
 * change counts as coming from. Replies with the selection, its primary,
 * serial and source, the Details lock, the Outliner's row of the primary,
 * the room's hidden set (and its settings line), the view and every object
 * with its window pixel. Returns 0, or -1 with an error message. */
extern "C" int pc_editor_select_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return rpc_fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    rood_scene::frame();
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "list";
    rood_selection::Source source = rood_selection::FROM_RPC;
    if (const Json* s = params.get("source"))
        source = s->string == "viewport" ? rood_selection::FROM_VIEWPORT
            : s->string == "outliner"    ? rood_selection::FROM_OUTLINER
            : s->string == "details"     ? rood_selection::FROM_DETAILS
                                         : rood_selection::FROM_RPC;
    std::vector<Ref> refs;
    if (const Json* ids = params.get("ids"))
        for (const Json& j : ids->items) {
            Ref r = rood_selection::ref_of(j.string);
            if (!r || !rood_scene::exists(r))
                return rpc_fail("no object " + j.string + " in this room", out, size);
            refs.push_back(r);
        }
    std::string extra;
    if (action == "list") {
    } else if (action == "set") {
        rood_selection::set_all(refs, source);
    } else if (action == "add") {
        for (const Ref& r : refs)
            rood_selection::add(r, source);
    } else if (action == "toggle") {
        for (const Ref& r : refs)
            rood_selection::toggle(r, source);
    } else if (action == "remove") {
        for (const Ref& r : refs)
            rood_selection::remove(r, source);
    } else if (action == "clear") {
        rood_selection::clear(source);
    } else {
        return rpc_fail("unknown action " + action, out, size);
    }
    rood_scene::publish();
    std::string text = state_json() + extra + "}";
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
```

(`flag` is first used by Task 6; until then the compiler may note it unused, which is harmless in the game build.)

- [ ] **Step 5: Old selection calls go through the model (lights.cpp)**

In `pc/src/lights.cpp` add after `#include "editor_undo.h"` (line 40):

```cpp
#include "editor_scene.h"
#include "editor_selection.h"
```

Replace the five functions `rood_lights::select_none` … `rood_lights::select_glow_id` (lines 263-295) with:

```cpp
/* The selection lives in editor_selection.h (one set of typed references the
 * viewport, Outliner, Details, undo and the RPC share); these keep their old
 * meaning by replacing it, and editor_scene.cpp's publish keeps selected,
 * selected_actor, multi and the glow's state in step with it. */
void rood_lights::select_none()
{
    rood_selection::clear(rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_light(int index)
{
    rood_selection::set(rood_scene::light_ref(index), rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_actor(int index)
{
    rood_selection::set(rood_scene::actor_ref(index), rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_glow(const float position[3])
{
    uint32_t id = pc_gi_glow_near(position, 96);
    rood_selection::set(id ? rood_scene::glow_ref(id) : rood_scene::glow_ref_key("", position), rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_glow_id(uint32_t id)
{
    rood_selection::Ref ref = rood_scene::glow_ref(id);
    if (!ref)
        return;
    rood_selection::set(ref, rood_selection::FROM_CODE);
    rood_scene::publish();
}
```

Replace `pc_lights_selected_position` (lines 1430-1452) with:

```cpp
/* The selection's primary for the editor's focus key (F) and the cutaway's focus. */
extern "C" int pc_lights_selected_position(float out[3], float* radius)
{
    return rood_scene::position(rood_selection::primary(), out, radius) ? 1 : 0;
}
```

In `pc_lights_outliner`, replace the lines from `bool in_multi = ...` through the closing brace of `if (ImGui::Selectable(label, (int)i == selected || in_multi)) { ... }` (lines 1774-1784) with:

```cpp
        rood_selection::Ref ref = rood_scene::light_ref((int)i);
        if (ImGui::Selectable(label, rood_selection::contains(ref)))
            rood_scene::row_clicked(ref);
```

In `pc_lights_glows_outliner`, replace `bool is_selected = glow_selected && g.id == glow_id;` (line 2083) with

```cpp
        rood_selection::Ref ref = rood_scene::glow_ref_key(g.key, g.position);
        bool is_selected = rood_selection::contains(ref);
```

and `if (ImGui::Selectable(label, is_selected)) select_glow_id(g.id);` (lines 2087-2088) with

```cpp
        if (ImGui::Selectable(label, is_selected))
            rood_scene::row_clicked(ref);
```

- [ ] **Step 6: Actor rows and from_lights (light_actors.cpp)**

Add `#include "editor_scene.h"` after `#include "editor_undo.h"` (line 21). In `actors_outliner`, replace the `ImGuiTreeNodeFlags flags = …` statement and the following `if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) select_actor(i);` (lines 891-896) with:

```cpp
        rood_selection::Ref ref = rood_scene::actor_ref(i);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | (rood_selection::contains(ref) ? ImGuiTreeNodeFlags_Selected : 0) | (ls.empty() ? ImGuiTreeNodeFlags_Leaf : 0);
        bool open = ImGui::TreeNodeEx("##actor", flags, "%s%s%s", label.c_str(), a.overrides.empty() ? "" : " *",
            a.fixture ? "  [fixture]" : "");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            rood_scene::row_clicked(ref);
```

and in the two child rows replace `if (ImGui::IsItemClicked()) select_actor(i);` with `if (ImGui::IsItemClicked()) rood_scene::row_clicked(ref);`.

In `actors_method`'s `from_lights` branch replace

```cpp
        select_light((int)list->items[0].number);
        for (size_t k = 1; k < list->items.size(); k++)
            multi.push_back((int)list->items[k].number);
```

with

```cpp
        std::vector<rood_selection::Ref> refs;
        for (size_t k = 1; k < list->items.size(); k++)
            refs.push_back(rood_scene::light_ref((int)list->items[k].number));
        refs.push_back(rood_scene::light_ref((int)list->items[0].number)); /* the primary: the prefab's first light */
        rood_selection::set_all(refs, rood_selection::FROM_RPC);
        rood_scene::publish();
```

- [ ] **Step 7: Fog volumes select through the model (atmosphere.cpp)**

Add `#include "editor_scene.h"` and `#include "editor_selection.h"` after `#include "editor_picking.h"`. In `add_volume` replace

```cpp
    selected = (int)volumes.size() - 1;
    rood_lights::select_none();
```

with

```cpp
    rood_selection::set(rood_scene::volume_ref((int)volumes.size() - 1), rood_selection::FROM_CODE);
    rood_scene::publish();
```

In `pc_atmosphere_page` replace the row's `if (ImGui::Selectable(label, i == selected)) { … }` (lines 1504-1508) with:

```cpp
        rood_selection::Ref ref = rood_scene::volume_ref(i);
        if (ImGui::Selectable(label, rood_selection::contains(ref)))
            rood_scene::row_clicked(ref);
```

Replace the Delete volume button's body (lines 1580-1582)

```cpp
            volumes.erase(volumes.begin() + selected);
            selected = -1;
            changed = true;
```

with

```cpp
            rood_atmo::remove_volume(selected); /* pc_atmosphere_page is outside rood_atmo: qualified */
            rood_scene::refresh();
            changed = true;
```

In `rood_atmo::rpc` replace the `select_volume` block (lines 1990-1997) with:

```cpp
    if (const prefab::Json* sel = params.get("select_volume")) { /* the editor's selection (its gizmo) */
        int found = -1;
        for (int i = 0; i < (int)volumes.size(); i++)
            if (volumes[i].id == sel->string)
                found = i;
        rood_selection::set(rood_scene::volume_ref(found), rood_selection::FROM_RPC);
        rood_scene::publish();
    }
```

- [ ] **Step 8: Undo snapshots hold the selection as references (editor_undo.cpp)**

Add `#include "editor_scene.h"` and `#include "editor_selection.h"` after `#include "editor_undo.h"`. In `struct Snapshot` replace

```cpp
    int selected = -1, selected_actor = -1;
    bool glow_selected = false;
    uint32_t glow_id = 0;
    std::vector<int> multi;
```

with

```cpp
    std::vector<rood_selection::Ref> selection; /* the editor's selection (editor_selection.h) */
```

In `capture` replace the last three lines (`s.selected = …`, `s.glow_selected = …`, `s.multi = multi;`) with `s.selection = rood_selection::items();`. In `apply` replace the block from `/* The selection the state had (when it still names something). */` through the closing brace of the `else if (to.selected >= 0 …) { … }` (lines 373-386) with:

```cpp
    /* The selection the state had (references to what it no longer has drop out). */
    rood_selection::set_all(to.selection, rood_selection::FROM_UNDO);
    rood_scene::refresh();
```

- [ ] **Step 9: Reloads keep the selection by id (lighting_editor.cpp)**

Add `#include "editor_scene.h"` and `#include "editor_selection.h"` to the includes. In `apply_state` delete the two lines declaring `sel_light` and `sel_actor` (104-105) and replace

```cpp
    select_none();
    for (size_t i = 0; i < lights.size(); i++)
        if (!sel_light.empty() && lights[i].id == sel_light)
            select_light((int)i);
    for (size_t i = 0; i < actors.size(); i++)
        if (!sel_actor.empty() && actors[i].id == sel_actor)
            select_actor((int)i);
```

with

```cpp
    rood_scene::refresh(); /* the selection is by id: what is still here stays selected */
```

In `pc_lighting_file_menu` replace the building of `sel` (the `if (selected >= 0)` … glow block) with:

```cpp
    std::vector<std::string> sel;
    for (const rood_selection::Ref& r : rood_selection::items())
        sel.push_back(r.id);
```

- [ ] **Step 10: The overlay keeps it in step each frame (dev_overlay.cpp)**

Add `#include "editor_scene.h"` and `#include "editor_selection.h"` after `#include "editor_undo.h"`. In `pc_overlay_frame` replace

```cpp
    if (visible)
        pc_undo_frame_begin();
```

with

```cpp
    if (visible) {
        rood_scene::frame(); /* the selection follows the room's objects (editor_scene.cpp) */
        pc_undo_frame_begin();
    }
```

- [ ] **Step 11: Route the RPC (dev_rpc.c)**

In the method doc comment, after the `editor_ui {visible, help, tab}` entry, add:

```c
 *   editor_select {action, ids, source, x, y | u, v, shift, ctrl, ...}  the
 *                                editor's selection (editor_scene.cpp): list,
 *                                set / add / toggle / remove {ids}, clear;
 *                                replies the selection, its primary, the
 *                                Outliner's row, the hidden set and every
 *                                object with its window pixel
```

Before `} else if (!strcmp(method, "editor_overlay")) {` add:

```c
    } else if (!strcmp(method, "editor_select")) {
        /* The editor's selection (editor_scene.cpp parses the params object). */
        extern int pc_editor_select_rpc(const char*, char*, int) __attribute__((weak));
        if (!pc_editor_select_rpc) {
            fail(fd, id, -32000, "needs the editor");
            return;
        }
        static char args[LINE_MAX_BYTES];
        const char* end = params ? value_end(params) : NULL;
        if (params && end && (size_t)(end - params) < sizeof args) {
            memcpy(args, params, (size_t)(end - params));
            args[end - params] = 0;
        } else {
            strcpy(args, "{}");
        }
        if (pc_editor_select_rpc(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
```

In `edits()` extend the first condition with `|| !strcmp(method, "editor_select")` (paste, duplicate, delete in later tasks are undoable; selection alone never makes a transaction).

- [ ] **Step 12: Compile the new files**

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_undo.cpp",` (line 900) add:

```python
    root / "pc/src/editor_selection.cpp",
    root / "pc/src/editor_picking.cpp",
    root / "pc/src/editor_scene.cpp",
```

- [ ] **Step 13: Build and run the test**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: build exit 0; `PASS` for `editor_selection_smoke`, its log's checks all `ok:`.

- [ ] **Step 14: Neighbours stay green**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_undo light_actors glow_stability atmosphere_smoke fixture_lighting
```

Expected: all `PASS` (or the Task 1 baseline's failures only).

- [ ] **Step 15: Commit**

```bash
git add pc/src/editor_scene.h pc/src/editor_scene.cpp pc/src/lights.cpp pc/src/light_actors.cpp pc/src/atmosphere.cpp \
  pc/src/editor_undo.cpp pc/src/lighting_editor.cpp pc/src/dev_overlay.cpp pc/src/dev_rpc.c pc/tools/build_boot.py \
  pc/tests/run.py pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Editor selection is the source of truth: every select path, undo and reloads by item id; RPC editor_select

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Viewport icons, picking every kind (fog volumes by ray), Shift / Ctrl

**Files:**
- Modify: `pc/src/editor_scene.cpp` (before `/* ---- RPC editor_select ---- */`; RPC chain), `pc/src/lights.cpp` (`draw_actor` lines 1045-1087 and its callers; `pc_lights_viewport` lines 1185-1428), `pc/src/atmosphere.cpp` (`pc_atmosphere_viewport` lines 1620-1660), `pc/src/dev_overlay.cpp` (`show_popup` line 1149)
- Test: `pc/tests/editor_selection_smoke.py`

- [ ] **Step 1: Write the failing test sections**

In `pc/tests/editor_selection_smoke.py` replace `Session.place` with the screen-aware version:

```python
    def place(self):
        """A point light, a candle and a fog volume around Ashley, each with its own spot on screen."""
        g, p = self.game, self.player
        self.light = g.call("lights", action="add", position=[p["x"] - 160, p["y"] - 140, p["z"]])["lights"][-1]["id"]
        g.call("actors", action="add", prefab="candle", position=[p["x"] + 160, p["y"] - 120, p["z"]], seed=3)
        self.actor = g.call("actors")["actors"][-1]["id"]
        self.volume = "fog:" + VOLUME
        for dz in (320, -320, 480, -480, 640, -640):
            g.call("environment", volumes=[{"id": VOLUME, "pos": [p["x"], p["y"] - 64, p["z"] + dz],
                                            "size": [160, 128, 160]}])
            g.run(6)
            o = self.objects()
            mine = [o[i]["screen"] for i in (self.light, self.actor, self.volume)]
            others = [x["screen"] for i, x in o.items() if x["screen"] and i != self.volume]
            if all(mine) and all(distance(mine[2], xy) > 60 for xy in others):
                return True
        return False
```

Add after `check_rpc`:

```python
def check_picking(s):
    """A click picks what is under it: an icon first, else a fog volume (a ray against its box)."""
    o = s.objects()
    glows = [i for i, x in o.items() if x["kind"] == "glow" and x["screen"]][:1]
    for ident in [s.light, s.actor, s.volume] + glows:
        xy = o[ident]["screen"]
        if not s.check(xy is not None, f"{o[ident]['label']} is on screen"):
            continue
        r = s.select(action="click", x=xy[0], y=xy[1])
        s.check(r["under"] == ident and r["selection"] == [ident] and r["source"] == "rpc",
                f"a click on {o[ident]['label']} selects it ({r['selection']})")
    s.select(action="clear")
    s.click(o[s.volume]["screen"])
    r = s.select()
    s.check(r["selection"] == [s.volume] and r["source"] == "viewport",
            f"a mouse click inside the fog volume's outline picks it ({r['selection']})")
    view, empty = r["view"], None
    for fx, fy in ((0.06, 0.1), (0.94, 0.1), (0.06, 0.9), (0.94, 0.9), (0.5, 0.06)):
        xy = [view[0] + view[2] * fx, view[1] + view[3] * fy]
        if all(distance(xy, x["screen"]) > 80 for x in o.values() if x["screen"]) \
                and s.select(action="click", x=xy[0], y=xy[1], shift=True)["under"] is None:
            empty = xy
            break
    if s.check(empty is not None, "the view has an empty spot"):
        s.select(action="set", ids=[s.light])
        s.click(empty)
        s.check(s.select()["selection"] == [], "a mouse click on nothing selects nothing")


def check_modifiers(s):
    """Shift adds, Ctrl toggles (Unreal); the primary has the gizmo."""
    o = s.objects()

    def click(ident, **kw):
        xy = o[ident]["screen"]
        return s.select(action="click", x=xy[0], y=xy[1], **kw)["selection"]

    s.check(click(s.light) == [s.light], "click: the light alone")
    s.check(click(s.actor, shift=True) == [s.light, s.actor], "Shift+click adds the candle")
    s.check(click(s.volume, shift=True) == [s.light, s.actor, s.volume], "Shift+click adds the fog volume")
    s.check(click(s.light, ctrl=True) == [s.actor, s.volume], "Ctrl+click takes the light out")
    s.check(click(s.light, ctrl=True) == [s.actor, s.volume, s.light], "Ctrl+click puts it back, as the primary")
    st = s.game.call("editor_place", phase="state")
    s.check(st["selected_light"] == s.index("lights", s.light), "the primary has the gizmo")
```

and change `SECTIONS = [check_rpc]` to `SECTIONS = [check_rpc, check_picking, check_modifiers]`. Extend the module docstring's list:

```
- picking: a click selects a light, an actor (its pivot), a glow or a fog
  volume (inside its outline, by a ray), through the RPC and with real
  mouse events; a click on nothing selects nothing;
- modifiers: Shift adds, Ctrl toggles; the primary has the gizmo.
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)`; the log shows `FAIL: the light, the candle and the fog volume each have their own spot on screen` (no screen positions yet).

- [ ] **Step 3: Icons, picking and the RPC click (editor_scene.cpp)**

Before `/* ---- RPC editor_select ---- */` add:

```cpp
/* ---- the viewport ---- */

void rood_scene::icons_begin() { icons.clear(); }
void rood_scene::icon(const Ref& r, float x, float y, float radius, bool anchor) { icons.push_back({ r, x, y, radius, anchor }); }
Ref rood_scene::object_at(float x, float y)
{
    const Icon* best = nullptr;
    float best_d = 0;
    int best_rank = 9;
    for (const Icon& c : icons) {
        float dx = x - c.x, dy = y - c.y, d = dx * dx + dy * dy;
        if (c.radius <= 0 || d >= c.radius * c.radius)
            continue;
        int rank = c.ref.kind == rood_selection::KIND_LIGHT ? 0 : c.ref.kind == rood_selection::KIND_ACTOR ? 1 : 2;
        if (!best || rank < best_rank || (rank == best_rank && d < best_d))
            best = &c, best_rank = rank, best_d = d;
    }
    return best ? best->ref : Ref();
}
Ref rood_scene::volume_at(const pc_overlay_view& v, pc_overlay_pick pick, float x, float y)
{
    float o[3], d[3];
    if (!rood_pick::ray(v.view, v.projection, v.rect, x, y, o, d))
        return Ref();
    float best = 1e30f;
    int found = -1;
    for (int i = 0; i < rood_atmo::volume_count(); i++) {
        float t = rood_atmo::volume_hit(i, o, d);
        if (t >= 0 && t < best)
            best = t, found = i;
    }
    if (found < 0)
        return Ref();
    float world[3], normal[3];
    if (pick && pick(x, y, world, normal)) {
        float along = (world[0] - o[0]) * d[0] + (world[1] - o[1]) * d[1] + (world[2] - o[2]) * d[2];
        if (along + 1 < best)
            return Ref(); /* a room surface is in front of it */
    }
    return volume_ref(found);
}
std::vector<Ref> rood_scene::in_box(float x0, float y0, float x1, float y1)
{
    std::vector<Ref> out;
    for (const Icon& c : icons)
        if (c.anchor && c.ref.kind != rood_selection::KIND_GLOW && rood_pick::in_rect(c.x, c.y, x0, y0, x1, y1)
            && std::find(out.begin(), out.end(), c.ref) == out.end())
            out.push_back(c.ref);
    return out;
}
Ref rood_scene::click_at(float x, float y, bool shift, bool ctrl, rood_selection::Source s)
{
    Ref under = object_at(x, y);
    pc_overlay_view v;
    pc_overlay_pick pick = nullptr;
    if (!under && pc_lights_tools_view(&v, &pick))
        under = volume_at(v, pick, x, y);
    if (under)
        rood_selection::click(under, shift, ctrl, s);
    else if (!shift && !ctrl)
        rood_selection::clear(s);
    publish();
    return under;
}
```

In the RPC's anonymous namespace add after `flag`:

```cpp
/* The window pixel from x, y or u, v (of the editor's view). */
bool cursor_of(const Json& params, float* x, float* y)
{
    pc_overlay_view v;
    if (!pc_lights_tools_view(&v, nullptr))
        return false;
    *x = v.rect[0] + v.rect[2] * 0.5f, *y = v.rect[1] + v.rect[3] * 0.5f;
    if (const Json* u = params.get("u"))
        *x = v.rect[0] + v.rect[2] * (float)u->number;
    if (const Json* j = params.get("v"))
        *y = v.rect[1] + v.rect[3] * (float)j->number;
    if (const Json* j = params.get("x"))
        *x = (float)j->number;
    if (const Json* j = params.get("y"))
        *y = (float)j->number;
    return true;
}
```

In `pc_editor_select_rpc`, before the line `    } else {` that precedes `return rpc_fail("unknown action " + action, out, size);`, add:

```cpp
    } else if (action == "click") {
        float x, y;
        if (!cursor_of(params, &x, &y))
            return rpc_fail("click needs an editor view (hardware renderer and a room)", out, size);
        Ref under = rood_scene::click_at(x, y, flag(params, "shift"), flag(params, "ctrl"), source);
        extra += ",\"under\":" + (under ? quote(under.id) : std::string("null"));
```

and extend the RPC's doc comment with `click {x, y | u, v, shift, ctrl} (as a click in the view: "under" is what it hit)`.

- [ ] **Step 4: The viewport records icons and picks through the scene (lights.cpp)**

Change `draw_actor` to record its icons instead of returning a hover flag:

```cpp
/* An actor in the view: its prefab's icon at the pivot, lines to its
 * lights (small dots), the lights' shapes, and their radii when selected.
 * With a reference, its pivot and its lights are its icons for picking
 * (editor_scene.cpp; the pivot is its anchor). */
static void draw_actor(const View& v, ImDrawList* draw, const prefab::Actor& a, bool is_selected, float alpha,
    const char* label, const rood_selection::Ref* ref)
```

Inside it delete `bool hovered = false;`; replace the two lines `float dx = mouse.x - q.x, dy = mouse.y - q.y; hovered |= dx * dx + dy * dy < (r + 4) * (r + 4);` with `if (ref) rood_scene::icon(*ref, q.x, q.y, r + 4, false);`; replace the last three lines (`float dx = mouse.x - pivot.x …`, `hovered |= …`, `return hovered;`) with `if (ref) rood_scene::icon(*ref, pivot.x, pivot.y, icon + 3, true);`. In `draw_place_drag` and the ghosts loop change the calls' last argument `ImVec2(-1e9f, -1e9f)` to `nullptr`.

In `pc_lights_viewport`, add after `pc_overlay_view ov = overlay_view(v);`:

```cpp
    rood_scene::icons_begin();
```

Replace the light loop (`int hovered = -1;` through its closing brace, lines 1228-1247) with:

```cpp
    for (size_t i = 0; i < lights.size(); i++) {
        const Light& l = lights[i];
        ImVec2 p;
        if (!view_project(v, l.position, p))
            continue;
        rood_selection::Ref ref = rood_scene::light_ref((int)i);
        bool is_selected = rood_selection::contains(ref);
        draw_light_shape(v, draw, l, is_selected ? IM_COL32(120, 220, 255, 220) : IM_COL32(120, 220, 255, 90));
        draw_sphere(v, draw, l.position, l.radius, is_selected ? IM_COL32(255, 230, 120, 200) : IM_COL32(255, 255, 255, 70),
            is_selected ? 2.0f : 1.0f);
        float icon = 9 * ImGui::GetStyle().FontScaleMain;
        draw->AddCircleFilled(p, icon, light_colour(l.color, l.enabled ? 1.0f : 0.35f));
        draw->AddCircle(p, icon + 2, is_selected ? IM_COL32(255, 255, 0, 255) : IM_COL32(0, 0, 0, 200), 24, 2);
        char label[16];
        snprintf(label, sizeof label, "%zu", i + 1);
        draw->AddText(ImVec2(p.x + icon + 4, p.y - icon), IM_COL32(255, 255, 255, 230), label);
        rood_scene::icon(ref, p.x, p.y, icon + 4, true);
    }
```

Replace the actor loop (lines 1249-1256) with:

```cpp
    /* Actors, then suggestion ghosts (dimmed). */
    for (size_t i = 0; i < actors.size(); i++) {
        const prefab::Prefab* p = prefab::library().find(actors[i].prefab);
        char label[96];
        snprintf(label, sizeof label, "%s %zu", p ? p->name.c_str() : actors[i].prefab.c_str(), i + 1);
        rood_selection::Ref ref = rood_scene::actor_ref((int)i);
        draw_actor(v, draw, actors[i], rood_selection::contains(ref), 1.0f, label, &ref);
    }
```

In the glow loop delete `uint32_t hovered_glow = 0; float hovered_glow_d = 1e30f;`; replace `bool is_selected = glow_selected && g.id == glow_id;` with

```cpp
        rood_selection::Ref ref = rood_scene::glow_ref_key(g.key, g.position);
        bool is_selected = rood_selection::contains(ref);
```

and replace the last three lines of its body (`float dx = …`, `if (dx * dx … hovered_glow = g.id;`) with `rood_scene::icon(ref, p.x, p.y, r + 4, true);`.

Add before `pc_lights_viewport` (after `draw_cutaway_focus`):

```cpp
/* A click on nothing in the view: Ctrl adds the Place panel's item there,
 * Shift moves the selected light or actor there (the snapped point or edge,
 * else off the surface), a plain click selects nothing. */
static void empty_click(float x, float y, bool shift, bool ctrl)
{
    bool have_light = selected >= 0 && selected < (int)lights.size();
    bool have_actor = selected_actor >= 0 && selected_actor < (int)actors.size();
    Placement where;
    if (ctrl) {
        if (place_compute(place_item, x, y, 0, where))
            place_commit(place_item, where);
        return;
    }
    if (shift) {
        std::string item = have_actor ? "prefab:" + actors[selected_actor].prefab : "light:point";
        if (!(have_light || have_actor) || !place_compute(item, x, y, 0, where))
            return;
        pc_undo_hint(have_light ? "Move light to surface" : "Move actor to surface");
        if (have_light)
            memcpy(lights[selected].position, where.position, sizeof where.position);
        else
            memcpy(actors[selected_actor].position, where.position, sizeof where.position);
        dirty = true;
        return;
    }
    rood_selection::clear(rood_selection::FROM_VIEWPORT);
    rood_scene::publish();
}
```

Replace the end of `pc_lights_viewport`, from `if (fixture_mode && hovered < 0 && hovered_actor < 0) {` to the function's closing brace, with:

```cpp
    /* What is under the pointer: an icon (a light, an actor's pivot or one of
     * its lights, a glow), else the fog volume a ray from it enters first. */
    rood_selection::Ref under = rood_scene::object_at(io.MousePos.x, io.MousePos.y);
    if (!under)
        under = rood_scene::volume_at(ov, pick_for_snap, io.MousePos.x, io.MousePos.y);
    bool on_light = under.kind == rood_selection::KIND_LIGHT || under.kind == rood_selection::KIND_ACTOR;
    if (fixture_mode && !on_light) {
        /* The Light fixture tool: one click on a lantern, candle or glow. */
        light_fixture_at_pixel(io.MousePos.x, io.MousePos.y, "");
    } else if (under) {
        /* Unreal's: a click selects, Shift adds, Ctrl toggles. */
        rood_selection::click(under, io.KeyShift, io.KeyCtrl, rood_selection::FROM_VIEWPORT);
        rood_scene::publish();
    } else {
        empty_click(io.MousePos.x, io.MousePos.y, io.KeyShift, io.KeyCtrl);
    }
}
```

(`have_light`/`have_actor` stay above for the gizmo. The `world`, `normal` locals stay for the probe.)

- [ ] **Step 5: Fog volumes are always drawn and anchored (atmosphere.cpp)**

In `pc_atmosphere_viewport` delete the early return block

```cpp
    if (!page && selected < 0) {
        if (ghost_mode)
            rood_fog_sim::ghost(false, nullptr);
        return false;
    }
```

and replace the start of the drawing loop (from `if (show_volumes)` through `float thick = …;`) with:

```cpp
    if (show_volumes)
        for (int i = 0; i < (int)volumes.size(); i++) {
            const Volume& v = volumes[i];
            rood_selection::Ref ref = rood_scene::volume_ref(i);
            bool chosen = rood_selection::contains(ref);
            ImU32 col = i == selected ? IM_COL32(255, 230, 120, 230)
                : chosen             ? IM_COL32(255, 230, 120, 140)
                                     : IM_COL32(170, 200, 255, v.enabled ? 120 : 50);
            float thick = chosen ? 2.0f : 1.0f;
```

and in its label block add after `draw->AddText(…, label);`:

```cpp
                rood_scene::icon(ref, c.x, c.y, 0, true); /* box select and the RPC; a click picks it by a ray */
```

Update the function comment to: `/* The room's fog volumes in the editor view (Show > Fog volumes): outlines, the selected ones highlighted, each centre an anchor for box select and the RPC (a click picks a volume by a ray against its shape: editor_scene.cpp); the primary's gizmo when no light or actor is selected. Returns true while the gizmo has the mouse. */`

- [ ] **Step 6: Show > Fog volumes (dev_overlay.cpp)**

Add `extern "C" bool* pc_atmosphere_show_volumes(void);` beside the other `pc_lights_*` declarations, and in `show_popup` after `ImGui::MenuItem("Lights and glows", nullptr, pc_lights_show());` add:

```cpp
    ImGui::MenuItem("Fog volumes", nullptr, pc_atmosphere_show_volumes());
```

- [ ] **Step 7: Build and run the test**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `PASS`; the `-- picking` and `-- modifiers` checks are all `ok:`.

- [ ] **Step 8: Neighbours stay green**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 light_actors editor_snap fixture_lighting atmosphere_smoke fog_sim
```

Expected: all `PASS` (Place Actors drop, snapping, the fixture tool and the volume gizmo still work).

- [ ] **Step 9: Commit**

```bash
git add pc/src/editor_scene.cpp pc/src/lights.cpp pc/src/atmosphere.cpp pc/src/dev_overlay.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Editor picking: every kind by click (fog volumes by a ray against their shape), Shift adds, Ctrl toggles

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Box select

**Files:**
- Modify: `pc/src/lights.cpp` (anonymous `Drag` namespace near line 878; `pc_lights_viewport`'s click section), `pc/src/editor_scene.cpp` (RPC chain)
- Test: `pc/tests/editor_selection_smoke.py`

- [ ] **Step 1: Write the failing section**

Add after `check_modifiers`:

```python
def check_box(s):
    """A drag on empty space boxes what it encloses; Shift adds, Ctrl toggles."""
    o = s.objects()
    a, b, v = o[s.light]["screen"], o[s.actor]["screen"], o[s.volume]["screen"]
    others = [x["screen"] for i, x in o.items() if x["screen"] and i not in (s.light, s.actor)]
    lo = hi = None
    for margin in (30, 50, 70):
        lo = [min(a[0], b[0]) - margin, min(a[1], b[1]) - margin]
        hi = [max(a[0], b[0]) + margin, max(a[1], b[1]) + margin]
        enclosed = [xy for xy in others if lo[0] <= xy[0] <= hi[0] and lo[1] <= xy[1] <= hi[1]]
        if not enclosed and all(distance(lo, xy) > 30 for xy in others + [a, b]) \
                and s.select(action="click", x=lo[0], y=lo[1], shift=True)["under"] is None:
            break
    else:
        s.check(False, "test geometry: a box around the light and the candle alone, from an empty corner")
        return
    s.select(action="clear")
    s.mouse(lo, 1, True)
    s.mouse([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2], 1, True)
    s.mouse(hi, 1, True)
    s.mouse(hi, 1, False)
    r = s.select()
    s.check(sorted(r["selection"]) == sorted([s.light, s.actor]) and r["source"] == "viewport",
            f"a drag on empty space boxes the light and the candle ({r['selection']})")
    r = s.select(action="box", x0=v[0] - 20, y0=v[1] - 20, x1=v[0] + 20, y1=v[1] + 20, shift=True)
    s.check(sorted(r["selection"]) == sorted([s.light, s.actor, s.volume]), "Shift+box adds the fog volume")
    r = s.select(action="box", x0=a[0] - 10, y0=a[1] - 10, x1=a[0] + 10, y1=a[1] + 10, ctrl=True)
    s.check(sorted(r["selection"]) == sorted([s.actor, s.volume]), "Ctrl+box takes the light out")
    r = s.select(action="box", x0=a[0] - 10, y0=a[1] - 10, x1=a[0] + 10, y1=a[1] + 10)
    s.check(r["selection"] == [s.light], "a plain box replaces the selection")
```

Append `check_box` to `SECTIONS`; docstring: `- box: a drag on empty space selects the light and the candle (real mouse events); Shift+box adds, Ctrl+box toggles, a plain box replaces.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)`; the log shows `FAIL: a drag on empty space boxes the light and the candle ([])` (or an `RpcError: unknown action box`).

- [ ] **Step 3: The marquee (lights.cpp)**

In the anonymous namespace holding `struct Drag` (line 878) add after `std::vector<Ghost> drag_ghost;`:

```cpp
/* A box select begun on empty space: where it began, with which modifiers. */
struct Marquee {
    bool pending = false, dragging = false;
    float x = 0, y = 0;
    bool shift = false, ctrl = false;
} marquee;
```

In `pc_lights_viewport`, directly after `if (pc_atmosphere_viewport(v.view, v.projection, v.rect, draw)) gizmo_busy = true;`, add:

```cpp
    /* A box select: drawn while the button is held; on release it selects
     * what it encloses (Shift adds, Ctrl toggles), or, when the mouse hardly
     * moved, it was a click on empty space. */
    if (marquee.pending) {
        ImVec2 a(marquee.x, marquee.y), b = io.MousePos;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            marquee.dragging = true;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (marquee.dragging) {
                ImDrawList* front = ImGui::GetForegroundDrawList();
                ImVec2 lo(fminf(a.x, b.x), fminf(a.y, b.y)), hi(fmaxf(a.x, b.x), fmaxf(a.y, b.y));
                front->AddRectFilled(lo, hi, IM_COL32(120, 180, 255, 40));
                front->AddRect(lo, hi, IM_COL32(120, 180, 255, 220), 0, 0, 1.5f);
            }
            return;
        }
        Marquee m = marquee;
        marquee = Marquee();
        if (m.dragging) {
            rood_selection::box(rood_scene::in_box(m.x, m.y, b.x, b.y), m.shift, m.ctrl, rood_selection::FROM_VIEWPORT);
            rood_scene::publish();
        } else {
            empty_click(m.x, m.y, m.shift, m.ctrl);
        }
        return;
    }
```

At the end of the function replace

```cpp
    } else {
        empty_click(io.MousePos.x, io.MousePos.y, io.KeyShift, io.KeyCtrl);
    }
```

with

```cpp
    } else {
        /* Empty space: a box select, or on release without a drag a click there. */
        marquee.pending = true;
        marquee.dragging = false;
        marquee.x = io.MousePos.x, marquee.y = io.MousePos.y;
        marquee.shift = io.KeyShift, marquee.ctrl = io.KeyCtrl;
    }
```

- [ ] **Step 4: The RPC box (editor_scene.cpp)**

Before the RPC chain's final `} else {` add:

```cpp
    } else if (action == "box") {
        const Json *x0 = params.get("x0"), *y0 = params.get("y0"), *x1 = params.get("x1"), *y1 = params.get("y1");
        if (!x0 || !y0 || !x1 || !y1)
            return rpc_fail("box needs x0, y0, x1, y1 (window pixels)", out, size);
        rood_selection::box(rood_scene::in_box((float)x0->number, (float)y0->number, (float)x1->number, (float)y1->number),
            flag(params, "shift"), flag(params, "ctrl"), source);
```

and add `box {x0, y0, x1, y1, shift, ctrl}` to its doc comment.

- [ ] **Step 5: Build and run the test**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection light_actors editor_snap
```

Expected: all `PASS`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/lights.cpp pc/src/editor_scene.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Editor box select: a drag on empty space selects lights, actors and fog volumes (Shift adds, Ctrl toggles)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: The Outliner follows the selection (fog volumes listed)

**Files:**
- Modify: `pc/src/editor_scene.cpp` (anonymous namespace; `frame`; the Outliner section), `pc/src/environment_internal.h` (`rood_atmo`), `pc/src/atmosphere.cpp` (after `rood_atmo::remove_volume`), `pc/src/lights.cpp` (light and glow rows), `pc/src/light_actors.cpp` (actor rows), `pc/src/dev_overlay.cpp` (`outliner()` lines 1413-1427)
- Test: `pc/tests/editor_selection_smoke.py`

- [ ] **Step 1: Write the failing section**

```python
def check_outliner(s):
    """A selection made anywhere but the Outliner scrolls the Outliner to its row and opens its section."""
    g, p = s.game, s.player
    for k in range(30):
        g.call("lights", action="add", position=[p["x"] + 8 * k, p["y"] - 300, p["z"]])
    ids = [x["id"] for x in g.call("lights")["lights"]]
    g.call("editor_ui", tab="outliner")
    s.select(action="set", ids=[ids[0]])
    g.run(6)
    first = s.select()["outliner"]
    s.select(action="set", ids=[ids[-1]], source="outliner")
    g.run(6)
    unscrolled = s.select()["outliner"]
    s.select(action="set", ids=[ids[-1]])
    g.run(6)
    scrolled = s.select()["outliner"]
    s.check(first["target"] == ids[0] and first["visible"], f"the first light's row shows ({first})")
    s.check(unscrolled["target"] == ids[-1] and not unscrolled["visible"],
            f"a selection made in the Outliner itself does not scroll it (the 31st light is out of sight: {unscrolled})")
    s.check(scrolled["target"] == ids[-1] and scrolled["visible"],
            f"a selection made elsewhere scrolls the Outliner to its row ({scrolled})")
    s.select(action="set", ids=[s.volume])
    g.run(6)
    r = s.select()["outliner"]
    s.check(r["target"] == s.volume and r["visible"], f"fog volumes are in the Outliner too ({r})")
    g.call("editor_undo", action="undo", steps=30)
    s.check(len(g.call("lights")["lights"]) == len(ids) - 30, "the 30 extra lights are undone")
```

Append `check_outliner` to `SECTIONS`; docstring: `- outliner: a selection from elsewhere scrolls the Outliner to its row (not one from the Outliner itself); fog volumes have rows.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)`; `FAIL: the first light's row shows ({'target': None, 'visible': False})`.

- [ ] **Step 3: Row tracking and scroll (editor_scene.cpp)**

In the top anonymous namespace, after `int row_frame = -1;`, add:

```cpp
/* The selection serial last scrolled to, and a scroll waiting for its row. */
unsigned scrolled, waiting_serial;
int waiting_frames;
```

Append to `rood_scene::frame()` after `refresh();`:

```cpp
    /* A scroll to a row the Outliner never draws (filtered out, its tab
     * hidden) gives up after a few frames: its section may close again. */
    unsigned s = rood_selection::serial();
    if (s != scrolled) {
        if (s != waiting_serial)
            waiting_serial = s, waiting_frames = 0;
        else if (++waiting_frames > 3)
            scrolled = s;
    }
```

After `rood_scene::row_clicked` add:

```cpp
void rood_scene::row_drawn(const Ref& r)
{
    if (r != rood_selection::primary())
        return;
    if (rood_selection::serial() != scrolled) {
        if (rood_selection::last_source() != rood_selection::FROM_OUTLINER)
            ImGui::SetScrollHereY(0.5f); /* the row just drawn, centred */
        scrolled = rood_selection::serial();
    }
    row_target = r.id;
    row_visible = ImGui::IsItemVisible();
    row_frame = ImGui::GetFrameCount();
}
bool rood_scene::scroll_due(rood_selection::Kind a, rood_selection::Kind b)
{
    Ref p = rood_selection::primary();
    return p && rood_selection::serial() != scrolled && rood_selection::last_source() != rood_selection::FROM_OUTLINER
        && (p.kind == a || p.kind == b);
}
```

- [ ] **Step 4: Rows report themselves**

In `pc/src/lights.cpp` `pc_lights_outliner`, after `rood_scene::row_clicked(ref);` (the Selectable's body) add the line `rood_scene::row_drawn(ref);` (outside the `if`, right after it). Do the same in `pc_lights_glows_outliner` after its Selectable. In `pc/src/light_actors.cpp` `actors_outliner`, add `rood_scene::row_drawn(ref);` right after the `if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) rood_scene::row_clicked(ref);` statement.

- [ ] **Step 5: Fog volumes in the Outliner (atmosphere.cpp, environment_internal.h)**

Declare in `rood_atmo` (environment_internal.h): `void outliner(const char* filter); /* the Outliner's Fog volumes rows */`. Add after `rood_atmo::remove_volume`:

```cpp
/* Outliner rows for the room's fog volumes: the enabled box, click to
 * select (Shift adds, Ctrl toggles), the layer badge. */
void rood_atmo::outliner(const char* filter)
{
    if (volumes.empty()) {
        ImGui::TextDisabled("None in this room (Details > Atmosphere: Add volume)");
        return;
    }
    for (int i = 0; i < (int)volumes.size(); i++) {
        Volume& v = volumes[i];
        std::string label = volume_label(i);
        if (filter && filter[0] && !strcasestr(label.c_str(), filter))
            continue;
        ImGui::PushID(i);
        if (ImGui::Checkbox("##on", &v.enabled)) {
            pc_lights_mark_dirty();
            pc_undo_hint(v.enabled ? "Enable fog volume" : "Disable fog volume");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enabled");
        ImGui::SameLine();
        rood_selection::Ref ref = rood_scene::volume_ref(i);
        bool silent = !v.enabled;
        if (silent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(label.c_str(), rood_selection::contains(ref)))
            rood_scene::row_clicked(ref);
        rood_scene::row_drawn(ref);
        if (silent)
            ImGui::PopStyleColor();
        rood_lights::layer_row(ref.id); /* layer badge, revert / promote menu */
        ImGui::PopID();
    }
}
```

- [ ] **Step 6: Sections open for the selection (dev_overlay.cpp)**

Add `#include "environment_internal.h"` after `#include "editor_scene.h"`. In `outliner()`, replace the three tree nodes inside `ImGui::BeginChild("##outliner rows");` … `ImGui::EndChild();` with:

```cpp
            /* A selection made elsewhere opens its section (its row is then
             * scrolled to: editor_scene.cpp row_drawn). */
            if (rood_scene::scroll_due(rood_selection::KIND_LIGHT, rood_selection::KIND_ACTOR))
                ImGui::SetNextItemOpen(true);
            if (ImGui::TreeNodeEx("Lights and actors (this room)", ImGuiTreeNodeFlags_DefaultOpen)) {
                pc_lights_outliner(filter);
                ImGui::TreePop();
            }
            if (pc_gl_compute || (&pc_vk_active && pc_vk_active)) {
                if (rood_scene::scroll_due(rood_selection::KIND_GLOW))
                    ImGui::SetNextItemOpen(true);
                if (ImGui::TreeNodeEx("Glows (from the game)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    pc_lights_glows_outliner(filter);
                    ImGui::TreePop();
                }
            }
            if (rood_scene::scroll_due(rood_selection::KIND_VOLUME))
                ImGui::SetNextItemOpen(true);
            if (ImGui::TreeNodeEx("Fog volumes (this room)", ImGuiTreeNodeFlags_DefaultOpen)) {
                rood_atmo::outliner(filter);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Room groups")) {
                room_groups_outliner(filter);
                ImGui::TreePop();
            }
```

- [ ] **Step 7: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection editor_undo
```

Expected: both `PASS`.

- [ ] **Step 8: Commit**

```bash
git add pc/src/editor_scene.cpp pc/src/environment_internal.h pc/src/atmosphere.cpp pc/src/lights.cpp \
  pc/src/light_actors.cpp pc/src/dev_overlay.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Outliner follows the selection: its section opens and its row scrolls into view; fog volumes listed

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Details follows the selection; room-wide settings keep their own tabs

**Files:**
- Modify: `pc/src/dev_overlay.cpp` (statics lines 58-61; `pc_overlay_set_details_tab` lines 192-199; `details()` lines 1440-1531), `pc/src/dev_rpc.c` (`editor_ui` reply)
- Test: `pc/tests/editor_selection_smoke.py`

- [ ] **Step 1: Write the failing section**

```python
def check_details_follow(s):
    """Details shows the selection: a new one switches to the Details tab, unless it came from Details."""
    g = s.game
    g.call("editor_ui", details="graphics")
    g.run(3)
    s.select(action="set", ids=[s.light])
    g.run(3)
    s.check(g.call("editor_ui")["details"] == "details", "a selection switches Details to the Details tab")
    g.call("editor_ui", details="atmosphere")
    g.run(3)
    s.select(action="set", ids=[s.actor], source="details")
    g.run(3)
    s.check(g.call("editor_ui")["details"] == "atmosphere", "a change made from Details itself keeps the tab")
    for name in ("lighting", "inspector", "atmosphere", "post", "graphics", "diagnostics", "details"):
        g.call("editor_ui", details=name)
        g.run(2)
        s.check(g.call("editor_ui")["details"] == name, f"the {name} tab opens on its own")
```

Append to `SECTIONS`; docstring: `- details_follow: a selection switches to the Details tab (not one from Details); every room-wide tab still opens.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)`; a `KeyError: 'details'` traceback (the reply has no `details` yet).

- [ ] **Step 3: Named tabs (dev_overlay.cpp)**

Replace lines 60-61 (`static int details_tab_request …` and `static const char* const details_tabs[] …`) with:

```cpp
/* The Details tabs: the selection's Details first (it follows the
 * selection), then the room-wide settings on their own tabs (RPC editor_ui
 * {details}: by name). */
enum DetailsTab { TAB_DETAILS, TAB_LIGHTING, TAB_INSPECTOR, TAB_ATMOSPHERE, TAB_POST, TAB_GRAPHICS, TAB_DIAGNOSTICS, TAB_COUNT };
static const char* const details_tabs[TAB_COUNT] = { "details", "lighting", "inspector", "atmosphere", "post", "graphics",
    "diagnostics" };
static int details_tab_request = -1;
static int details_tab_shown = TAB_DETAILS;
```

In `pc_overlay_set_details_tab` change `for (int i = 0; i < 6; i++)` to `for (int i = 0; i < TAB_COUNT; i++)`, and add after it:

```cpp
/* RPC editor_ui: the Details tab shown in the last frame. */
extern "C" const char* pc_overlay_details_tab(void) { return details_tabs[details_tab_shown]; }
```

- [ ] **Step 4: The Details tab (dev_overlay.cpp details())**

Replace the start of `details()` from `ImGuiTabItemFlags select[6] = {};` through the end of the Lighting tab's `BeginTabItem` line with:

```cpp
    ImGuiTabItemFlags select[TAB_COUNT] = {};
    /* Selection follows you: a new selection shows in Details, unless Details
     * is locked or the change came from Details itself or from undo. */
    static unsigned seen_serial;
    if (rood_selection::serial() != seen_serial) {
        seen_serial = rood_selection::serial();
        rood_selection::Source s = rood_selection::last_source();
        if (!rood_selection::empty() && !rood_selection::locked() && s != rood_selection::FROM_DETAILS
            && s != rood_selection::FROM_UNDO)
            details_tab_request = TAB_DETAILS;
    }
    if (details_tab_request >= 0)
        select[details_tab_request] = ImGuiTabItemFlags_SetSelected;
    details_tab_request = -1;
    if (ImGui::BeginTabBar("details")) {
        if (ImGui::BeginTabItem("Details", nullptr, select[TAB_DETAILS])) {
            details_tab_shown = TAB_DETAILS;
            if (!pc_lights_details() && !pc_lights_glow_details())
                ImGui::TextDisabled("Nothing selected: click a light, light actor, glow or fog volume\n"
                                    "in the view or the Outliner.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lighting", nullptr, select[TAB_LIGHTING])) {
            details_tab_shown = TAB_LIGHTING;
```

and delete the old Lighting tab's first two lines (`if (!pc_lights_details())` / `pc_lights_glow_details();`). Change the other tab items to `select[TAB_INSPECTOR]`, `select[TAB_ATMOSPHERE]`, `select[TAB_POST]`, `select[TAB_GRAPHICS]`, `select[TAB_DIAGNOSTICS]`, each body starting with `details_tab_shown = TAB_<NAME>;`.

- [ ] **Step 5: The editor_ui reply (dev_rpc.c)**

In the `editor_ui` branch add `extern const char* pc_overlay_details_tab(void) __attribute__((weak));` to its declarations and replace its final `snprintf` with:

```c
        snprintf(result, sizeof result, "{\"visible\":%s,\"help\":%s,\"tab\":\"%s\",\"details\":\"%s\"}",
            open ? "true" : "false", pc_overlay_set_help && pc_overlay_set_help(help) ? "true" : "false",
            shown ? "place" : "outliner", pc_overlay_details_tab ? pc_overlay_details_tab() : "");
```

and update its doc comment line to `editor_ui {visible, help, tab, details}  … details: a Details tab by name (details, lighting, inspector, atmosphere, post, graphics, diagnostics); replies the states and the Details tab shown`.

- [ ] **Step 6: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection lighting_presets atmosphere_smoke fog_sim
```

Expected: all `PASS` (`lighting_presets_smoke` and the atmosphere tests open tabs by name).

- [ ] **Step 7: Commit**

```bash
git add pc/src/dev_overlay.cpp pc/src/dev_rpc.c pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Details follows the selection (its own first tab); Lighting, Atmosphere, Post-processing and Graphics stay room-wide

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: The property schema: lights and light actors, RPC editor_schema

**Files:**
- Create: `pc/src/editor_schema.h`, `pc/src/editor_schema.cpp`, `pc/tests/editor_schema_smoke.py`
- Modify: `pc/src/dev_rpc.c` (route, `edits()`, doc), `pc/tools/build_boot.py`, `pc/tests/run.py`

- [ ] **Step 1: Write the failing test**

Create `pc/tests/editor_schema_smoke.py`:

```python
#!/usr/bin/env python3
"""The editor's property schema (pc/src/editor_schema.h) and Details drawn
from it (editor_details.cpp).

    python3 pc/tests/editor_schema_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The same save room and objects as editor_selection_smoke.py (a point light,
a candle, a fog volume). Sections:

- lint: every field of every kind has a name, label, category, a range and
  a default inside it (the schema lint, and the kinds as listed);
- lights: RPC get / set round-trips each light field (clamped to its range),
  the type switches the kind, reset (the ↺) restores the default, and each
  edit is one undo step named after its field;
- actors: a light actor's fields are its transform and its prefab's
  parameters; a parameter set is an override, reset drops it.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import RpcError  # noqa: E402
from editor_selection_smoke import VOLUME, centre, finish, pinned_card, start  # noqa: E402

EXPECTED_KINDS = {"point", "spot", "rect", "actor:candle", "actor:torch"}


def schema(s, **params):
    return s.game.call("editor_schema", **params)


def close(a, b, tolerance=1e-3):
    if isinstance(a, list) or isinstance(b, list):
        return isinstance(a, list) and isinstance(b, list) and len(a) == len(b) \
            and all(abs(x - y) <= tolerance * max(1, abs(y)) for x, y in zip(a, b))
    if isinstance(a, (bool, str)) or isinstance(b, (bool, str)):
        return a == b
    return abs(a - b) <= tolerance * max(1, abs(b))


def value(s, ident, field):
    return schema(s, action="get", id=ident)["fields"][field]["value"]


def check_lint(s):
    lint = schema(s, action="lint")
    s.check(lint["problems"] == [] and lint["fields"] > 0,
            f"the schema lint is clean ({lint['kinds']} kinds, {lint['fields']} fields; {lint['problems'][:5]})")
    kinds = {k["name"]: k for k in schema(s, action="kinds")["kinds"]}
    s.check(EXPECTED_KINDS <= set(kinds), f"the kinds ({sorted(kinds)})")
    bad = [f"{k['name']}.{f['name']}" for k in kinds.values() for f in k["fields"]
           if not f["min"] < f["max"] or f["default"] is None]
    s.check(not bad, f"every field has a range and a default, as listed ({bad[:5]})")


def check_lights(s):
    g = s.game
    got = schema(s, action="get", id=s.light)
    s.check(got["kind"] == "point" and close(got["fields"]["intensity"]["value"], 1)
            and got["fields"]["radius"]["source"] == "default", f"a new point light: its kind and defaults ({got['kind']})")
    before = s.history()["done"]
    values = {"intensity": 2.5, "color": [0.2, 0.4, 0.6], "radius": 900, "shadows": True}
    for name, v in values.items():
        schema(s, action="set", id=s.light, values={name: v})
        s.check(close(value(s, s.light, name), v), f"set {name} = {v}: get reads it back")
    light = next(x for x in g.call("lights")["lights"] if x["id"] == s.light)
    s.check(close(light["intensity"], 2.5) and close(light["color"], [0.2, 0.4, 0.6]) and close(light["radius"], 900),
            "the lights RPC sees the same values")
    h = s.history()
    s.check(h["done"] == before + len(values) and h["undo"] == "Edit Casts shadows",
            f"one undo step per edit, named after the field ({h['undo']})")
    schema(s, action="set", id=s.light, values={"intensity": 99})
    s.check(close(value(s, s.light, "intensity"), 8), "a value past the range is clamped to it (8)")
    schema(s, action="reset", id=s.light, fields=["radius", "intensity"])
    f = schema(s, action="get", id=s.light)["fields"]
    s.check(close(f["radius"]["value"], 600) and close(f["intensity"]["value"], 1) and not f["radius"]["overridden"],
            "reset restores the defaults")
    s.check(s.history()["undo"].startswith("Reset"), f"and is named Reset ({s.history()['undo']})")
    schema(s, action="set", id=s.light, values={"type": "spot"})
    got = schema(s, action="get", id=s.light)
    s.check(got["kind"] == "spot" and "cone" in got["fields"], "the type switches the light's kind (a spot has a cone)")
    schema(s, action="set", id=s.light, values={"cone": 60, "direction": [1, 0, 0]})
    s.check(close(value(s, s.light, "cone"), 60) and close(value(s, s.light, "direction"), [1, 0, 0]),
            "spot fields round-trip")
    schema(s, action="set", id=s.light, values={"type": "rect"})
    schema(s, action="set", id=s.light, values={"width": 200, "height": 96})
    got = schema(s, action="get", id=s.light)
    s.check(got["kind"] == "rect" and close(got["fields"]["width"]["value"], 200)
            and close(got["fields"]["height"]["value"], 96), "rect fields round-trip")
    schema(s, action="set", id=s.light, values={"type": "point"})
    try:
        schema(s, action="set", id=s.light, values={"nonsense": 1})
        s.check(False, "an unknown field is refused")
    except RpcError:
        s.check(True, "an unknown field is refused")


def check_actors(s):
    got = schema(s, action="get", id=s.actor)
    f = got["fields"]
    s.check(got["kind"] == "actor:candle" and {"position", "rotation", "intensity", "tint"} <= set(f)
            and f["intensity"]["source"] == "prefab" and not f["intensity"]["overridden"],
            f"a candle: its transform and its prefab's parameters ({sorted(f)})")
    schema(s, action="set", id=s.actor, values={"intensity": 2.5, "tint": [1, 0.5, 0.25], "rotation": [90, 0, 0]})
    actor = next(a for a in s.game.call("actors")["actors"] if a["id"] == s.actor)
    s.check(close(actor["overrides"].get("intensity", 0), 2.5) and close(actor["overrides"].get("tint", [0, 0, 0]), [1, 0.5, 0.25])
            and close(actor["rotation"], [90, 0, 0]), f"parameters become overrides, rotation its own ({actor['overrides']})")
    s.check(s.history()["undo"] == "Edit 3 fields", f"one undo step for the request ({s.history()['undo']})")
    candle = next(k for k in schema(s, action="kinds")["kinds"] if k["name"] == "actor:candle")
    glow = next(x for x in candle["fields"] if x["name"] == "glow_mode")
    schema(s, action="set", id=s.actor, values={"glow_mode": glow["options"][0]})
    s.check(value(s, s.actor, "glow_mode") == glow["options"][0], "an enum parameter by its option's name")
    schema(s, action="reset", id=s.actor, fields=["intensity"])
    actor = next(a for a in s.game.call("actors")["actors"] if a["id"] == s.actor)
    s.check("intensity" not in actor["overrides"] and not schema(s, action="get", id=s.actor)["fields"]["intensity"]["overridden"],
            "reset drops the override: the prefab's value again")


SECTIONS = [check_lint, check_lights, check_actors]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (lint, lights, ...)")
    args = parser.parse_args()
    s = start(args, "editor_schema")
    if s is None:
        return 77
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                section(s)
    finally:
        status = finish(s, "every kind's fields lint clean, round-trip, reset; Details draws them; the value ladder")
    return status


if __name__ == "__main__":
    sys.exit(main())
```

(`VOLUME` and `centre` are imported for later sections.) Register in `pc/tests/run.py` after the `editor_selection_smoke` Suite:

```python
            Suite("editor_schema_smoke", [PY, str(TESTS / "editor_schema_smoke.py"), "--card", str(args.save)], 420,
                  note="property schema: lint, RPC get/set/reset per kind (lights, actors, fog presets), Details drawn "
                       "from it (categories, search, lock, reset arrow), the value ladder (middle-drag, Esc)"),
```

and `"editor_schema_smoke",` in `CHECKPOINT_SUITES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema
```

Expected: `FAIL (1)`; `RpcError` with `unknown method editor_schema`.

- [ ] **Step 3: The schema header**

Create `pc/src/editor_schema.h`:

```cpp
/* The editor's property schema (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Property schema"): each kind of object the editor edits registers its
 * fields once (name, label, category, type, range, unit, log scale, step,
 * default, accessors by the object's index, and what the per-value reset
 * restores), and Details (editor_details.cpp), undo names and the RPC
 * editor_schema use them. Kinds: the point, spot and rect light; a light
 * actor per prefab (its transform and the prefab's exposed parameters); the
 * fog volume (atmosphere.cpp registers it). Editor internals. */
#ifndef ROOD_EDITOR_SCHEMA_H
#define ROOD_EDITOR_SCHEMA_H
#include <functional>
#include <string>
#include <vector>

#include "editor_selection.h"
#include "light_prefabs.h"

namespace rood_schema {
using prefab::Value;

enum Type { TYPE_FLOAT, TYPE_INT, TYPE_BOOL, TYPE_COLOUR, TYPE_ENUM, TYPE_VEC3, TYPE_ANGLE };
const char* type_name(Type t); /* "float", "int", "bool", "colour", "enum", "vec3", "angle" */

struct Field {
    std::string name, label, category, unit, tooltip;
    Type type = TYPE_FLOAT;
    float min = 0, max = 1; /* every component's (bool 0..1, enum 0..options-1) */
    float step = 0.01f; /* drag speed per pixel; the value ladder opens on its nearest step */
    bool logarithmic = false;
    bool resettable = true; /* has a reset (a position has none) */
    bool shared = true; /* an edit goes to every selected object of the kind (a position: the edited one only) */
    std::vector<std::string> options; /* TYPE_ENUM */
    Value def; /* the schema's default */
    std::function<Value(int)> get; /* by the object's index */
    std::function<void(int, const Value&)> set;
    /* What reset restores when it is not `def` (a preset's or a prefab's
     * value), and where it comes from ("default", "preset", "prefab", "fixture type"). */
    std::function<Value(int)> base;
    std::function<const char*(int)> base_source;
    /* Whether reset has something to undo (an actor: it overrides the parameter); null: get != base. */
    std::function<bool(int)> overridden;
    std::function<void(int)> reset; /* null: set(base) */
};

struct Kind {
    std::string name, label; /* "point" / "Point light"; an actor's "actor:<prefab id>" / the prefab's name */
    rood_selection::Kind object = rood_selection::KIND_NONE;
    std::vector<Field> fields;
    std::vector<std::string> closed; /* categories Details shows closed at first */
    std::function<void(int)> extras; /* Details: the kind's own tools under its fields */
    const Field* field(const std::string& name) const;
};

/* Field factories (each kind then sets get / set). */
Field number_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* unit = "", bool logarithmic = false, const char* tooltip = "");
Field angle_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* tooltip = "");
Field bool_field(const char* name, const char* label, const char* category, bool def, const char* tooltip = "");
Field colour_field(const char* name, const char* label, const char* category, float max, Value def, const char* tooltip = "");
Field vec3_field(const char* name, const char* label, const char* category, float min, float max, float step, Value def,
    const char* unit = "", const char* tooltip = "");
Field enum_field(const char* name, const char* label, const char* category, const std::vector<std::string>& options,
    int def, const char* tooltip = "");

/* The kind of an object and its index (null for glows and what is gone). */
const Kind* kind_of(const rood_selection::Ref& r, int* index);
const Kind* find_kind(const std::string& name);
std::vector<const Kind*> all_kinds(); /* the lights', the fog volume's, one per prefab in the library */
std::vector<std::string> lint(); /* problems; none: every field is complete */

Value base_of(const Field& f, int index);
const char* base_source_of(const Field& f, int index);
bool is_overridden(const Field& f, int index);
void reset_field(const Field& f, int index);
Value clamp(const Field& f, const Value& v);
bool same(const Field& f, const Value& a, const Value& b);
std::string format(const Field& f, const Value& v); /* "1.25 u", "Spot", "on" */
/* A lighting value as text ("1.5", "1,0.5,0", "sphere"). */
Value from_text(Type type, const std::vector<std::string>& options, const std::string& text);
std::string to_json(const Field& f, const Value& v);
bool from_json(const Field& f, const prefab::Json& j, Value& out, std::string* error);

/* The objects an edit of `target`'s field goes to: it, and with a shared
 * field every other selected object of the same kind (when it is selected). */
std::vector<rood_selection::Ref> targets(const Kind& k, const Field& f, const rood_selection::Ref& target);
/* Edits (Details, the value ladder, the RPC): the value clamped to its
 * range, to every target; the room is unsaved; the undo step is named
 * "Edit <label>" / "Reset <label>". */
void apply(const Kind& k, const Field& f, const rood_selection::Ref& target, const Value& v);
void apply_reset(const Kind& k, const Field& f, const rood_selection::Ref& target);
} // namespace rood_schema

extern "C" int pc_editor_schema_rpc(const char* params, char* out, int size);
#endif
```

- [ ] **Step 4: The schema**

Create `pc/src/editor_schema.cpp`:

```cpp
// The editor's property schema (editor_schema.h): the value helpers, the
// lights' and light actors' kinds, the lint and the RPC editor_schema.
#include "editor_schema.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "editor_scene.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lights_internal.h"

using rood_selection::Ref;

namespace rood_schema {

const char* type_name(Type t)
{
    static const char* const names[] = { "float", "int", "bool", "colour", "enum", "vec3", "angle" };
    return names[t];
}
const Field* Kind::field(const std::string& name) const
{
    for (const Field& f : fields)
        if (f.name == name)
            return &f;
    return nullptr;
}

/* ---- factories ---- */

Field number_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* unit, bool logarithmic, const char* tooltip)
{
    Field f;
    f.name = name, f.label = label, f.category = category, f.unit = unit, f.tooltip = tooltip;
    f.type = TYPE_FLOAT, f.min = min, f.max = max, f.step = step, f.logarithmic = logarithmic;
    f.def = Value::scalar(def);
    return f;
}
Field angle_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* tooltip)
{
    Field f = number_field(name, label, category, min, max, step, def, "deg", false, tooltip);
    f.type = TYPE_ANGLE;
    return f;
}
Field bool_field(const char* name, const char* label, const char* category, bool def, const char* tooltip)
{
    Field f = number_field(name, label, category, 0, 1, 1, def ? 1.0f : 0.0f, "", false, tooltip);
    f.type = TYPE_BOOL;
    return f;
}
Field colour_field(const char* name, const char* label, const char* category, float max, Value def, const char* tooltip)
{
    Field f = number_field(name, label, category, 0, max, 0.01f, 0, "", false, tooltip);
    f.type = TYPE_COLOUR;
    f.def = def;
    return f;
}
Field vec3_field(const char* name, const char* label, const char* category, float min, float max, float step, Value def,
    const char* unit, const char* tooltip)
{
    Field f = number_field(name, label, category, min, max, step, 0, unit, false, tooltip);
    f.type = TYPE_VEC3;
    f.def = def;
    return f;
}
Field enum_field(const char* name, const char* label, const char* category, const std::vector<std::string>& options,
    int def, const char* tooltip)
{
    Field f = number_field(name, label, category, 0, options.empty() ? 0.0f : (float)(options.size() - 1), 1, (float)def,
        "", false, tooltip);
    f.type = TYPE_ENUM;
    f.options = options;
    return f;
}

/* ---- values ---- */

namespace {
int components(const Field& f) { return f.type == TYPE_COLOUR || f.type == TYPE_VEC3 ? 3 : 1; }
int decimals(float step) { return step >= 1 ? 0 : step >= 0.1f ? 1 : step >= 0.01f ? 2 : step >= 0.001f ? 3 : 5; }
bool same_word(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}
} // namespace

Value base_of(const Field& f, int index) { return f.base ? f.base(index) : f.def; }
const char* base_source_of(const Field& f, int index) { return f.base_source ? f.base_source(index) : "default"; }
bool is_overridden(const Field& f, int index)
{
    return f.overridden ? f.overridden(index) : !same(f, f.get(index), base_of(f, index));
}
void reset_field(const Field& f, int index)
{
    if (f.reset)
        f.reset(index);
    else
        f.set(index, base_of(f, index));
}
Value clamp(const Field& f, const Value& in)
{
    int n = components(f);
    Value v = n == 3 ? Value::vec(in.at(0), in.at(1), in.at(2)) : Value::scalar(in.x());
    for (int c = 0; c < n; c++) {
        float x = v.v[c];
        if (!std::isfinite(x))
            x = f.def.at(c);
        x = fminf(fmaxf(x, f.min), f.max);
        if (f.type == TYPE_INT || f.type == TYPE_ENUM)
            x = roundf(x);
        if (f.type == TYPE_BOOL)
            x = x > 0.5f ? 1.0f : 0.0f;
        v.v[c] = x;
    }
    return v;
}
bool same(const Field& f, const Value& a, const Value& b)
{
    for (int c = 0; c < components(f); c++)
        if (fabsf(a.at(c) - b.at(c)) > 1e-5f * fmaxf(1.0f, fabsf(b.at(c))))
            return false;
    return true;
}
std::string format(const Field& f, const Value& v)
{
    char text[128];
    int d = decimals(f.step);
    switch (f.type) {
    case TYPE_BOOL:
        return v.x() > 0.5f ? "on" : "off";
    case TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        return i >= 0 && i < (int)f.options.size() ? f.options[i] : std::to_string(i);
    }
    case TYPE_COLOUR:
        snprintf(text, sizeof text, "%.3f, %.3f, %.3f", v.at(0), v.at(1), v.at(2));
        return text;
    case TYPE_VEC3:
        snprintf(text, sizeof text, "%.*f, %.*f, %.*f", d, v.at(0), d, v.at(1), d, v.at(2));
        break;
    default:
        snprintf(text, sizeof text, "%.*f", d, v.x());
        break;
    }
    return f.unit.empty() ? std::string(text) : std::string(text) + " " + f.unit;
}
Value from_text(Type type, const std::vector<std::string>& options, const std::string& text)
{
    if (type == TYPE_ENUM) {
        for (size_t i = 0; i < options.size(); i++)
            if (same_word(options[i], text))
                return Value::scalar((float)i);
        return Value::scalar((float)atof(text.c_str()));
    }
    if (type == TYPE_COLOUR || type == TYPE_VEC3) {
        float x[3];
        if (sscanf(text.c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]) == 3)
            return Value::vec(x[0], x[1], x[2]);
        float s = (float)atof(text.c_str());
        return Value::vec(s, s, s);
    }
    return Value::scalar((float)atof(text.c_str()));
}
std::string to_json(const Field& f, const Value& v)
{
    char text[128];
    switch (f.type) {
    case TYPE_BOOL:
        return v.x() > 0.5f ? "true" : "false";
    case TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        return i >= 0 && i < (int)f.options.size() ? prefab::json_quote(f.options[i]) : std::to_string(i);
    }
    case TYPE_COLOUR:
    case TYPE_VEC3:
        snprintf(text, sizeof text, "[%.6g,%.6g,%.6g]", v.at(0), v.at(1), v.at(2));
        return text;
    default:
        snprintf(text, sizeof text, "%.6g", v.x());
        return text;
    }
}
bool from_json(const Field& f, const prefab::Json& j, Value& out, std::string* error)
{
    using prefab::Json;
    if (f.type == TYPE_BOOL && (j.type == Json::Bool || j.type == Json::Number)) {
        out = Value::scalar((j.type == Json::Bool ? j.boolean : j.number != 0) ? 1.0f : 0.0f);
        return true;
    }
    if (f.type == TYPE_ENUM && j.type == Json::String) {
        for (size_t i = 0; i < f.options.size(); i++)
            if (same_word(f.options[i], j.string)) {
                out = Value::scalar((float)i);
                return true;
            }
        if (error)
            *error = "no option " + j.string + " for " + f.name;
        return false;
    }
    if ((f.type == TYPE_COLOUR || f.type == TYPE_VEC3) && j.type == Json::Array && j.items.size() == 3
        && j.items[0].type == Json::Number && j.items[1].type == Json::Number && j.items[2].type == Json::Number) {
        out = Value::vec((float)j.items[0].number, (float)j.items[1].number, (float)j.items[2].number);
        return true;
    }
    if (f.type == TYPE_COLOUR && j.type == Json::Number) { /* a grey */
        out = Value::vec((float)j.number, (float)j.number, (float)j.number);
        return true;
    }
    if (f.type != TYPE_BOOL && f.type != TYPE_COLOUR && f.type != TYPE_VEC3 && j.type == Json::Number) {
        out = Value::scalar((float)j.number);
        return true;
    }
    if (error)
        *error = "bad value for " + f.name + " (a " + type_name(f.type) + ")";
    return false;
}

/* ---- kinds ---- */

namespace {
using rood_lights::actors;
using rood_lights::lights;

Value vec_of(const float* v) { return Value::vec(v[0], v[1], v[2]); }
void put(float* out, const Value& v)
{
    for (int k = 0; k < 3; k++)
        out[k] = v.at(k);
}

/* A point (0), spot (1) or rect (2) light; a new light's values are the defaults. */
Kind light_kind(int type)
{
    const rood_lights::Light d = rood_lights::Light();
    Kind k;
    k.object = rood_selection::KIND_LIGHT;
    k.name = type == 1 ? "spot" : type == 2 ? "rect" : "point";
    k.label = rood_lights::light_type_name(type);
    Field f = enum_field("type", "Type", "Light", { "Point", "Spot", "Rect" }, type, "Point, spot or rect (its fields follow)");
    f.resettable = false;
    f.get = [](int i) { return Value::scalar((float)lights[i].type); };
    f.set = [](int i, const Value& v) { lights[i].type = (int)lroundf(v.x()); };
    k.fields.push_back(f);
    f = bool_field("enabled", "Enabled", "Light", d.enabled, "Off: it lights nothing (the Outliner's box)");
    f.get = [](int i) { return Value::scalar(lights[i].enabled ? 1.0f : 0.0f); };
    f.set = [](int i, const Value& v) { lights[i].enabled = v.x() > 0.5f; };
    k.fields.push_back(f);
    f = number_field("intensity", "Intensity", "Light", 0, 8, 0.01f, d.intensity, "", false, "Scale on its colour (linear light)");
    f.get = [](int i) { return Value::scalar(lights[i].intensity); };
    f.set = [](int i, const Value& v) { lights[i].intensity = v.x(); };
    k.fields.push_back(f);
    f = colour_field("color", "Colour", "Light", 1, vec_of(d.color), "Linear colour");
    f.get = [](int i) { return vec_of(lights[i].color); };
    f.set = [](int i, const Value& v) { put(lights[i].color, v); };
    k.fields.push_back(f);
    f = number_field("radius", "Radius", "Light", 32, 4096, 1, d.radius, "u", true, "Its reach: the light falls to nothing here");
    f.get = [](int i) { return Value::scalar(lights[i].radius); };
    f.set = [](int i, const Value& v) { lights[i].radius = v.x(); };
    k.fields.push_back(f);
    f = bool_field("shadows", "Casts shadows", "Light", d.shadows);
    f.get = [](int i) { return Value::scalar(lights[i].shadows ? 1.0f : 0.0f); };
    f.set = [](int i, const Value& v) { lights[i].shadows = v.x() > 0.5f; };
    k.fields.push_back(f);
    f = vec3_field("position", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "World units (y is down)");
    f.resettable = false, f.shared = false;
    f.get = [](int i) { return vec_of(lights[i].position); };
    f.set = [](int i, const Value& v) { put(lights[i].position, v); };
    k.fields.push_back(f);
    if (type != 0) {
        f = vec3_field("direction", "Direction", "Transform", -1, 1, 0.01f, vec_of(d.direction), "",
            "Where it points (world, y down; the rotate tool, E)");
        f.get = [](int i) { return vec_of(lights[i].direction); };
        f.set = [](int i, const Value& v) { put(lights[i].direction, v); };
        k.fields.push_back(f);
    }
    if (type == 1) {
        f = angle_field("cone", "Cone", "Shape", 5, 170, 0.5f, d.cone, "Its full opening angle");
        f.get = [](int i) { return Value::scalar(lights[i].cone); };
        f.set = [](int i, const Value& v) { lights[i].cone = v.x(); };
        k.fields.push_back(f);
    }
    if (type == 2) {
        f = number_field("width", "Width", "Shape", 8, 4096, 1, d.size[0], "u");
        f.get = [](int i) { return Value::scalar(lights[i].size[0]); };
        f.set = [](int i, const Value& v) { lights[i].size[0] = v.x(); };
        k.fields.push_back(f);
        f = number_field("height", "Height", "Shape", 8, 4096, 1, d.size[1], "u");
        f.get = [](int i) { return Value::scalar(lights[i].size[1]); };
        f.set = [](int i, const Value& v) { lights[i].size[1] = v.x(); };
        k.fields.push_back(f);
    }
    return k;
}

/* A light actor of a prefab: its own transform and switch, then the prefab's
 * exposed parameters (a change is the actor's override; reset drops it). */
Kind actor_kind(const std::string& prefab_id)
{
    const prefab::Prefab* p = prefab::library().find(prefab_id);
    Kind k;
    k.object = rood_selection::KIND_ACTOR;
    k.name = "actor:" + prefab_id;
    k.label = p ? p->name : prefab_id + " (missing prefab)";
    Field f = bool_field("enabled", "Enabled", "Actor", true, "Off: none of its lights shine");
    f.get = [](int i) { return Value::scalar(actors[i].enabled ? 1.0f : 0.0f); };
    f.set = [](int i, const Value& v) { actors[i].enabled = v.x() > 0.5f; };
    k.fields.push_back(f);
    f = vec3_field("position", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "Its pivot (world units, y down)");
    f.resettable = false, f.shared = false;
    f.get = [](int i) { return vec_of(actors[i].position); };
    f.set = [](int i, const Value& v) { put(actors[i].position, v); };
    k.fields.push_back(f);
    f = vec3_field("rotation", "Rotation", "Transform", -360, 360, 0.5f, Value::vec(0, 0, 0), "deg",
        "Yaw, pitch, roll (the rotate tool, E)");
    f.get = [](int i) { return vec_of(actors[i].rotation); };
    f.set = [](int i, const Value& v) { put(actors[i].rotation, v); };
    k.fields.push_back(f);
    if (!p)
        return k;
    for (const prefab::Param& q : p->params) {
        Field pf;
        pf.name = q.key;
        pf.label = q.label.empty() ? q.key : q.label;
        pf.category = q.group.empty() ? "Parameters" : q.group;
        pf.unit = q.unit, pf.tooltip = q.tooltip;
        pf.type = q.type == prefab::PARAM_COLOR ? TYPE_COLOUR
            : q.type == prefab::PARAM_BOOL      ? TYPE_BOOL
            : q.type == prefab::PARAM_ENUM      ? TYPE_ENUM
                                                : TYPE_FLOAT;
        pf.options = q.options;
        pf.min = q.min, pf.max = q.max, pf.logarithmic = q.logarithmic;
        float span = q.max - q.min;
        pf.step = pf.type != TYPE_FLOAT ? (pf.type == TYPE_COLOUR ? 0.01f : 1.0f) : span >= 200 ? 1.0f : span >= 20 ? 0.1f : 0.01f;
        pf.def = q.def;
        std::string key = q.key, id = prefab_id;
        pf.get = [key, id](int i) {
            const prefab::Prefab* pp = prefab::library().find(id);
            return pp ? prefab::actor_param(*pp, actors[i], key) : Value();
        };
        pf.set = [key](int i, const Value& v) { actors[i].set_override(key, v); };
        pf.base = [key, id](int i) {
            const prefab::Prefab* pp = prefab::library().find(id);
            return pp ? prefab::inherited_param(*pp, actors[i], key) : Value();
        };
        pf.base_source = [key, id](int i) -> const char* {
            const prefab::Prefab* pp = prefab::library().find(id);
            bool from_type = false;
            if (pp)
                prefab::inherited_param(*pp, actors[i], key, &from_type);
            return from_type ? "fixture type" : "prefab";
        };
        pf.overridden = [key](int i) { return actors[i].override_of(key) != nullptr; };
        pf.reset = [key](int i) { actors[i].clear_override(key); };
        k.fields.push_back(pf);
    }
    return k;
}

std::vector<Kind>& fixed_kinds()
{
    static std::vector<Kind> kinds;
    if (kinds.empty()) {
        for (int type = 0; type < 3; type++)
            kinds.push_back(light_kind(type));
    }
    return kinds;
}
/* One kind per prefab, rebuilt when the library reloads (its parameters may change). */
std::map<std::string, Kind>& actor_kinds()
{
    static std::map<std::string, Kind> kinds;
    static int serial = -1;
    if (serial != prefab::library().serial) {
        kinds.clear();
        serial = prefab::library().serial;
    }
    return kinds;
}
const Kind* actor_kind_for(const std::string& prefab_id)
{
    std::map<std::string, Kind>& kinds = actor_kinds();
    auto it = kinds.find(prefab_id);
    if (it == kinds.end())
        it = kinds.emplace(prefab_id, actor_kind(prefab_id)).first;
    return &it->second;
}
} // namespace

const Kind* find_kind(const std::string& name)
{
    for (const Kind& k : fixed_kinds())
        if (k.name == name)
            return &k;
    if (name.compare(0, 6, "actor:") == 0 && prefab::library().find(name.substr(6)))
        return actor_kind_for(name.substr(6));
    return nullptr;
}
const Kind* kind_of(const Ref& r, int* index)
{
    int i = r.kind == rood_selection::KIND_GLOW ? -1 : rood_scene::index_of(r);
    if (index)
        *index = i;
    if (i < 0)
        return nullptr;
    switch (r.kind) {
    case rood_selection::KIND_LIGHT: {
        int type = lights[i].type;
        return &fixed_kinds()[type == 1 ? 1 : type == 2 ? 2 : 0];
    }
    case rood_selection::KIND_ACTOR:
        return actor_kind_for(actors[i].prefab);
    default:
        return find_kind("fog"); /* atmosphere.cpp registers it */
    }
}
std::vector<const Kind*> all_kinds()
{
    std::vector<const Kind*> out;
    for (const Kind& k : fixed_kinds())
        out.push_back(&k);
    for (const prefab::Prefab& p : prefab::library().prefabs)
        out.push_back(actor_kind_for(p.id));
    return out;
}
std::vector<std::string> lint()
{
    std::vector<std::string> problems;
    for (const Kind* k : all_kinds()) {
        std::vector<std::string> names;
        if (k->fields.empty())
            problems.push_back(k->name + ": no fields");
        for (const Field& f : k->fields) {
            std::string where = k->name + "." + f.name;
            if (f.name.empty() || f.label.empty() || f.category.empty())
                problems.push_back(where + ": needs a name, a label and a category");
            if (std::find(names.begin(), names.end(), f.name) != names.end())
                problems.push_back(where + ": the name twice");
            names.push_back(f.name);
            if (!(f.min < f.max))
                problems.push_back(where + ": no range (" + std::to_string(f.min) + " to " + std::to_string(f.max) + ")");
            if (!(f.step > 0))
                problems.push_back(where + ": no step");
            if (f.def.n != components(f))
                problems.push_back(where + ": no default with " + std::to_string(components(f)) + " components");
            else
                for (int c = 0; c < components(f); c++)
                    if (!std::isfinite(f.def.at(c)) || f.def.at(c) < f.min || f.def.at(c) > f.max)
                        problems.push_back(where + ": default " + format(f, f.def) + " outside its range");
            if (f.type == TYPE_ENUM && (f.options.size() < 2 || (int)f.max != (int)f.options.size() - 1))
                problems.push_back(where + ": an enum needs two options or more, its range their indices");
            if (!f.get || !f.set)
                problems.push_back(where + ": no accessors");
        }
    }
    return problems;
}

std::vector<Ref> targets(const Kind& k, const Field& f, const Ref& target)
{
    std::vector<Ref> out = { target };
    if (!f.shared || !rood_selection::contains(target))
        return out;
    for (const Ref& r : rood_selection::items()) {
        int i;
        if (r != target && kind_of(r, &i) == &k)
            out.push_back(r);
    }
    return out;
}
void apply(const Kind& k, const Field& f, const Ref& target, const Value& value)
{
    Value v = clamp(f, value);
    for (const Ref& r : targets(k, f, target)) {
        int i = rood_scene::index_of(r);
        if (i >= 0)
            f.set(i, v);
    }
    rood_lights::dirty = true;
    pc_undo_hint(("Edit " + f.label).c_str());
}
void apply_reset(const Kind& k, const Field& f, const Ref& target)
{
    for (const Ref& r : targets(k, f, target)) {
        int i = rood_scene::index_of(r);
        if (i >= 0)
            reset_field(f, i);
    }
    rood_lights::dirty = true;
    pc_undo_hint(("Reset " + f.label).c_str());
}
} // namespace rood_schema

/* ---- RPC editor_schema ---- */

namespace {
using prefab::Json;
using rood_schema::Field;
using rood_schema::Kind;
std::string quote(const std::string& s) { return prefab::json_quote(s); }
std::string number(double v)
{
    char text[32];
    snprintf(text, sizeof text, "%.6g", v);
    return text;
}
int failure(const std::string& message, char* out, int size)
{
    snprintf(out, size, "%s", message.c_str());
    return -1;
}
std::string kind_json(const Kind& k)
{
    std::string s = "{\"name\":" + quote(k.name) + ",\"label\":" + quote(k.label) + ",\"object\":\""
        + rood_selection::kind_name(k.object) + "\",\"closed\":[";
    for (size_t i = 0; i < k.closed.size(); i++)
        s += (i ? "," : "") + quote(k.closed[i]);
    s += "],\"fields\":[";
    for (size_t i = 0; i < k.fields.size(); i++) {
        const Field& f = k.fields[i];
        s += std::string(i ? "," : "") + "{\"name\":" + quote(f.name) + ",\"label\":" + quote(f.label) + ",\"category\":"
            + quote(f.category) + ",\"type\":\"" + rood_schema::type_name(f.type) + "\",\"min\":" + number(f.min)
            + ",\"max\":" + number(f.max) + ",\"step\":" + number(f.step) + ",\"log\":" + (f.logarithmic ? "true" : "false")
            + ",\"unit\":" + quote(f.unit) + ",\"default\":" + rood_schema::to_json(f, f.def) + ",\"resettable\":"
            + (f.resettable ? "true" : "false") + ",\"shared\":" + (f.shared ? "true" : "false") + ",\"options\":[";
        for (size_t o = 0; o < f.options.size(); o++)
            s += (o ? "," : "") + quote(f.options[o]);
        s += "]}";
    }
    return s + "]}";
}
std::string object_json(const Kind& k, const rood_selection::Ref& r, int index)
{
    std::string s = "{\"id\":" + quote(r.id) + ",\"kind\":" + quote(k.name) + ",\"label\":" + quote(rood_scene::label(r))
        + ",\"index\":" + std::to_string(index) + ",\"fields\":{";
    for (size_t i = 0; i < k.fields.size(); i++) {
        const Field& f = k.fields[i];
        s += std::string(i ? "," : "") + quote(f.name) + ":{\"value\":" + rood_schema::to_json(f, f.get(index))
            + ",\"base\":" + rood_schema::to_json(f, rood_schema::base_of(f, index)) + ",\"source\":\""
            + rood_schema::base_source_of(f, index) + "\",\"overridden\":"
            + (f.resettable && rood_schema::is_overridden(f, index) ? "true" : "false") + "}";
    }
    return s + "}}";
}
} // namespace

/* RPC editor_schema (dev_rpc.c hands over the params object's text):
 * action kinds (default; {kind}: one), lint, get {id}, set {id, values
 * {field: value}}, reset {id, fields [names] (default every resettable
 * one)}; without an id, the object Details shows (its lock, else the
 * selection's primary). Values are numbers, [r, g, b] / [x, y, z],
 * true / false and enum option names; set clamps them to their range. */
extern "C" int pc_editor_schema_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return failure("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    rood_scene::frame();
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "kinds";
    std::string text;
    if (action == "lint") {
        std::vector<std::string> problems = rood_schema::lint();
        size_t fields = 0;
        std::vector<const Kind*> kinds = rood_schema::all_kinds();
        for (const Kind* k : kinds)
            fields += k->fields.size();
        text = "{\"kinds\":" + std::to_string(kinds.size()) + ",\"fields\":" + std::to_string(fields) + ",\"problems\":[";
        for (size_t i = 0; i < problems.size(); i++)
            text += (i ? "," : "") + quote(problems[i]);
        text += "]}";
    } else if (action == "kinds") {
        const Json* only = params.get("kind");
        text = "{\"kinds\":[";
        bool first = true;
        for (const Kind* k : rood_schema::all_kinds())
            if (!only || only->string == k->name) {
                text += (first ? "" : ",") + kind_json(*k);
                first = false;
            }
        text += "]}";
    } else if (action == "get" || action == "set" || action == "reset") {
        rood_selection::Ref target = rood_selection::locked() ? rood_selection::locked() : rood_selection::primary();
        if (const Json* id = params.get("id"))
            target = rood_selection::ref_of(id->string);
        int index;
        const Kind* k = rood_schema::kind_of(target, &index);
        if (!k)
            return failure("no object with fields: " + (target ? target.id : std::string("nothing selected"))
                    + " (glows have none yet)",
                out, size);
        if (action == "set") {
            const Json* values = params.get("values");
            if (!values || values->type != Json::Object)
                return failure("set needs values {field: value}", out, size);
            std::vector<std::pair<const Field*, rood_schema::Value>> edits;
            for (const auto& m : values->members) {
                const Field* f = k->field(m.first);
                if (!f)
                    return failure("no field " + m.first + " on " + k->name, out, size);
                rood_schema::Value v;
                if (!rood_schema::from_json(*f, m.second, v, &error))
                    return failure(error, out, size);
                edits.push_back({ f, v });
            }
            for (const auto& e : edits)
                rood_schema::apply(*k, *e.first, target, e.second);
            if (edits.size() > 1)
                pc_undo_hint(("Edit " + std::to_string(edits.size()) + " fields").c_str());
        } else if (action == "reset") {
            const Json* names = params.get("fields");
            std::vector<const Field*> fields;
            if (names)
                for (const Json& n : names->items) {
                    const Field* f = k->field(n.string);
                    if (!f || !f->resettable)
                        return failure("no resettable field " + n.string + " on " + k->name, out, size);
                    fields.push_back(f);
                }
            else
                for (const Field& f : k->fields)
                    if (f.resettable)
                        fields.push_back(&f);
            for (const Field* f : fields)
                rood_schema::apply_reset(*k, *f, target);
            if (fields.size() > 1)
                pc_undo_hint(("Reset " + std::to_string(fields.size()) + " fields").c_str());
        }
        k = rood_schema::kind_of(target, &index); /* a light's type may have changed it */
        text = object_json(*k, target, index);
    } else {
        return failure("action: kinds, lint, get, set or reset", out, size);
    }
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
```

- [ ] **Step 5: Route it (dev_rpc.c), compile it (build_boot.py)**

In `dev_rpc.c` change the `editor_select` branch's condition and handler to serve both methods:

```c
    } else if (!strcmp(method, "editor_select") || !strcmp(method, "editor_schema")) {
        /* The editor's selection (editor_scene.cpp) and property schema
         * (editor_schema.cpp); they parse the params object themselves. */
        extern int pc_editor_select_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_schema_rpc(const char*, char*, int) __attribute__((weak));
        int (*handler)(const char*, char*, int) = !strcmp(method, "editor_select") ? pc_editor_select_rpc : pc_editor_schema_rpc;
        if (!handler) {
            fail(fd, id, -32000, "needs the editor");
            return;
        }
```

and use `handler(args, result, sizeof result)` in place of `pc_editor_select_rpc(...)` below. Add `|| !strcmp(method, "editor_schema")` to `edits()`. Doc comment entry:

```c
 *   editor_schema {action, id, values, fields}  the editor's property schema
 *                                (editor_schema.cpp): kinds, lint, get {id},
 *                                set {id, values}, reset {id, fields}; ui
 *                                (Details' state: its object, fields, their
 *                                window rectangles, the value ladder)
```

In `build_boot.py` add `root / "pc/src/editor_schema.cpp",` after `editor_scene.cpp`.

- [ ] **Step 6: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema
```

Expected: `PASS`; `-- lint`, `-- lights`, `-- actors` checks all `ok:`.

- [ ] **Step 7: Commit**

```bash
git add pc/src/editor_schema.h pc/src/editor_schema.cpp pc/src/dev_rpc.c pc/tools/build_boot.py pc/tests/run.py \
  pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Editor property schema: point/spot/rect lights and light actors (prefab parameters), lint, RPC editor_schema

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: The fog volume's schema (presets as the reset value)

**Files:**
- Modify: `pc/src/atmosphere.cpp` (includes; after `rood_atmo::outliner`), `pc/src/environment_internal.h`, `pc/src/editor_schema.cpp` (`fixed_kinds`)
- Test: `pc/tests/editor_schema_smoke.py`

- [ ] **Step 1: Write the failing section**

Change `EXPECTED_KINDS` to `{"point", "spot", "rect", "fog", "actor:candle", "actor:torch"}` and add:

```python
def check_fog(s):
    g = s.game
    got = schema(s, action="get", id=s.volume)
    f = got["fields"]
    s.check(got["kind"] == "fog" and close(f["density"]["value"], 0.004) and f["density"]["source"] == "default",
            f"a fog volume: its kind and the lighting schema's defaults ({got['kind']})")
    schema(s, action="set", id=s.volume, values={"density": 0.01, "shape": "sphere", "size": [200, 100, 300], "yaw": 30,
                                                 "disturb_player": 0.5})
    v = next(x for x in g.call("environment")["volumes"] if x["id"] == VOLUME)
    s.check(close(v["density"], 0.01) and v["shape"] == "sphere" and close(v["size"], [200, 100, 300])
            and close(v["yaw"], 30) and close(v["disturb_player"], 0.5), "fog fields reach the volume (environment RPC)")
    schema(s, action="reset", id=s.volume, fields=["density"])
    s.check(close(value(s, s.volume, "density"), 0.004), "reset without a preset: the default")
    g.call("environment", volumes=[{"id": VOLUME, "preset": "ankle_mist"}])
    f = schema(s, action="get", id=s.volume)["fields"]
    s.check(close(f["density"]["value"], 0.012) and f["density"]["source"] == "preset" and not f["density"]["overridden"],
            f"with a preset: its value, from the preset ({f['density']})")
    schema(s, action="set", id=s.volume, values={"density": 0.02})
    s.check(schema(s, action="get", id=s.volume)["fields"]["density"]["overridden"], "a changed preset field is overridden")
    schema(s, action="reset", id=s.volume, fields=["density"])
    s.check(close(value(s, s.volume, "density"), 0.012), "reset with a preset: the preset's value")
    g.call("environment", volumes=[{"id": VOLUME, "preset": "", "size": [160, 128, 160], "shape": "box", "yaw": 0}])
```

Append `check_fog` to `SECTIONS`; docstring: `- fog: a fog volume's fields round-trip; reset restores the default, or the preset's value when it has one.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema
```

Expected: `FAIL (1)`; `FAIL: the kinds (…)` (no `fog`) and an `RpcError` `no object with fields: fog:seltest0000001`.

- [ ] **Step 3: Register the fog kind (atmosphere.cpp)**

Declare in `environment_internal.h` before `namespace rood_atmo {`: `namespace rood_schema { struct Kind; }`, and inside it: `void schema_kind(rood_schema::Kind& out); /* the fog volume's fields (editor_schema.h) */`. In `atmosphere.cpp` add `#include "editor_schema.h"`. After `rood_atmo::outliner` add:

```cpp
/* The fog volume's fields (editor_schema.h): defaults from the lighting
 * schema (lighting_layers.cpp), so the two agree; what reset restores is the
 * volume's preset's value where its preset has the field. */
void rood_atmo::schema_kind(rood_schema::Kind& k)
{
    namespace rs = rood_schema;
    using rs::Field;
    using rs::Value;
    k = rs::Kind();
    k.name = "fog";
    k.label = "Fog volume";
    k.object = rood_selection::KIND_VOLUME;
    k.closed = { "Look", "Interaction" };
    auto add = [&k](Field f) {
        const char* text = lighting::field_default("fog", f.name);
        if (*text)
            f.def = rs::from_text(f.type, f.options, text);
        if (lighting::preset_field("fog", f.name)) {
            std::string key = f.name;
            rs::Type type = f.type;
            std::vector<std::string> options = f.options;
            f.base = [key, type, options](int i) {
                const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
                const std::string* x = p ? p->get(key) : nullptr;
                return rs::from_text(type, options, x ? *x : std::string(lighting::field_default("fog", key)));
            };
            f.base_source = [key](int i) -> const char* {
                const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
                return p && p->get(key) ? "preset" : "default";
            };
        }
        k.fields.push_back(f);
    };
    auto scalar = [&add](Field f, float Volume::*m) {
        f.get = [m](int i) { return Value::scalar(volumes[i].*m); };
        f.set = [m](int i, const Value& v) { volumes[i].*m = v.x(); };
        add(f);
    };
    auto triple = [&add](Field f, float (Volume::*m)[3]) {
        f.get = [m](int i) {
            const float* p = volumes[i].*m;
            return Value::vec(p[0], p[1], p[2]);
        };
        f.set = [m](int i, const Value& v) {
            float* p = volumes[i].*m;
            for (int c = 0; c < 3; c++)
                p[c] = v.at(c);
        };
        add(f);
    };
    auto flag = [&add](Field f, bool Volume::*m) {
        f.get = [m](int i) { return Value::scalar(volumes[i].*m ? 1.0f : 0.0f); };
        f.set = [m](int i, const Value& v) { volumes[i].*m = v.x() > 0.5f; };
        add(f);
    };
    flag(rs::bool_field("enabled", "Enabled", "Basics", true), &Volume::enabled);
    {
        Field f = rs::enum_field("shape", "Shape", "Basics", { "Box", "Sphere" }, 0, "A box, or an ellipsoid of the same size");
        f.get = [](int i) { return Value::scalar(volumes[i].sphere ? 1.0f : 0.0f); };
        f.set = [](int i, const Value& v) { volumes[i].sphere = v.x() > 0.5f; };
        add(f);
    }
    scalar(rs::number_field("density", "Density", "Basics", 0, 0.05f, 0.0001f, 0.004f, "/u", true,
               "Extinction per world unit in its middle"),
        &Volume::density);
    scalar(rs::number_field("edge", "Soft edge", "Basics", 0.01f, 1, 0.01f, 0.5f, "", false,
               "The part of its half size over which it fades out"),
        &Volume::edge);
    scalar(rs::number_field("noise", "Wisps", "Basics", 0, 1, 0.01f, 0.5f), &Volume::noise);
    triple(rs::colour_field("color", "Colour", "Basics", 1, Value::vec(1, 1, 1), "Albedo: the share of each colour it scatters"),
        &Volume::color);
    triple(rs::colour_field("fill", "Fill light", "Basics", 4, Value::vec(0, 0, 0),
               "Light it scatters from everywhere (radiance): keeps it visible where no light reaches"),
        &Volume::fill);
    {
        Field f = rs::vec3_field("pos", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "Its centre");
        f.resettable = false, f.shared = false;
        triple(f, &Volume::pos);
    }
    triple(rs::vec3_field("size", "Size", "Transform", 2, 8192, 1, Value::vec(256, 128, 256), "u", "Full extents"),
        &Volume::size);
    scalar(rs::angle_field("yaw", "Yaw", "Transform", -180, 180, 0.5f, 0), &Volume::yaw);
    scalar(rs::number_field("opacity", "Opacity", "Look", 0, 1, 0.01f, 1, "", false,
               "The most of the scene it may hide along a ray (1: solid)"),
        &Volume::opacity);
    scalar(rs::number_field("brightness", "Brightness", "Look", 0, 4, 0.01f, 1, "", false, "Scale on the light it scatters and emits"),
        &Volume::brightness);
    scalar(rs::number_field("phase", "Forward scattering", "Look", -0.9f, 0.9f, 0.01f, 0.3f, "", false,
               "How much brighter it is looking toward a light through it"),
        &Volume::phase);
    scalar(rs::number_field("ambient", "Ambient", "Look", 0, 4, 0.01f, 1, "", false,
               "Scale on its ambient light (the room's baked light around it, the sky)"),
        &Volume::ambient);
    triple(rs::colour_field("glow", "Glow", "Look", 16, Value::vec(0, 0, 0), "Light it emits per unit of density"), &Volume::glow);
    scalar(rs::number_field("glow_core", "Glow at its base", "Look", 0, 1, 0.01f, 0.5f), &Volume::glow_core);
    scalar(rs::number_field("taper", "Taper", "Look", 0, 1, 0.01f, 0, "", false, "0 an even density, 1 thinning toward its top"),
        &Volume::taper);
    scalar(rs::number_field("detail_size", "Billow size", "Look", 0, 1024, 1, 0, "u", false,
               "Its billows' size (0: from the room's wisp size)"),
        &Volume::detail_size);
    scalar(rs::number_field("detail", "Detail", "Look", 0, 1, 0.01f, 0.5f), &Volume::detail);
    scalar(rs::number_field("shadow", "Self-shadow", "Look", 0, 2, 0.01f, 0), &Volume::shadow);
    flag(rs::bool_field("sim", "Characters part it", "Interaction", false,
             "Interactive fog: characters, attacks and spells clear it and leave a wake"),
        &Volume::sim);
    scalar(rs::number_field("sim_strength", "Strength", "Interaction", 0, 2, 0.01f, 1), &Volume::sim_strength);
    scalar(rs::number_field("sim_recover", "Recovery time", "Interaction", 0.3f, 12, 0.1f, 3, "s"), &Volume::sim_recover);
    scalar(rs::number_field("sim_footprint", "Footprint", "Interaction", 0.25f, 3, 0.01f, 1), &Volume::sim_footprint);
    scalar(rs::number_field("sim_trail", "Trail", "Interaction", 0, 3, 0.01f, 1), &Volume::sim_trail);
    scalar(rs::number_field("sim_wind", "Wind", "Interaction", 0, 4, 0.01f, 1), &Volume::sim_wind);
    scalar(rs::number_field("sim_swirl", "Swirl", "Interaction", 0, 4, 0.01f, 0), &Volume::sim_swirl);
    for (int s = 0; s < 4; s++) {
        Field f = rs::number_field(DISTURB_KEYS[s], SOURCE_LABELS[s], "Interaction", -1, 2, 0.01f, -1, "", false, SOURCE_TIPS[s]);
        f.tooltip += "\n-1: the global weight (Atmosphere > Interactive fog: all rooms)";
        f.get = [s](int i) { return Value::scalar(volumes[i].disturb[s]); };
        f.set = [s](int i, const Value& v) { volumes[i].disturb[s] = v.x() < 0 ? -1 : v.x(); };
        add(f);
    }
}
```

In `editor_schema.cpp` `fixed_kinds()`, after the light loop inside the `if`, add:

```cpp
        Kind fog;
        rood_atmo::schema_kind(fog);
        kinds.push_back(fog);
```

- [ ] **Step 4: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema atmosphere_smoke
```

Expected: both `PASS`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/atmosphere.cpp pc/src/environment_internal.h pc/src/editor_schema.cpp pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Fog volume schema: Basics / Transform / Look / Interaction fields; reset restores the preset's value

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: Details drawn from the schema

**Files:**
- Create: `pc/src/editor_details.h`, `pc/src/editor_details.cpp`
- Modify: `pc/src/lights.cpp` (`pc_lights_details` lines 1804-1877 → `light_details_extra`), `pc/src/light_actors.cpp` (`param_row` lines 930-1011, `actor_details` lines 1016-1236, `overridden_colour` line 41), `pc/src/lights_internal.h` (line 100), `pc/src/atmosphere.cpp` (`weight_row`/`interaction_ui` lines 1321-1423; `pc_atmosphere_page` lines 1489-1586; `pc_atmosphere_viewport` ghost; new `details_extra`), `pc/src/environment_internal.h`, `pc/src/editor_schema.cpp` (extras, RPC `ui`), `pc/src/dev_overlay.cpp` (Details tab; declarations), `pc/tools/build_boot.py`
- Test: `pc/tests/editor_schema_smoke.py`

- [ ] **Step 1: Write the failing section**

```python
def check_details(s):
    """Details draws the selection's fields by category, with a reset arrow beside each changed value."""
    g = s.game
    s.select(action="set", ids=[s.light])
    g.call("editor_ui", details="details")
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.light and ui["kind"] == "point" and {"intensity", "radius", "color"} <= set(ui["fields"])
            and "position.x" in ui["rects"], f"Details shows the light's fields ({ui['fields']})")
    s.check(all(r[2] > 0 and r[3] > 0 for r in ui["rects"].values()), "each value has its place in the panel")
    s.select(action="set", ids=[s.volume])
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.volume and "density" in ui["fields"] and "opacity" not in ui["fields"],
            f"a fog volume: Basics open, Look closed at first ({ui['fields']})")
    s.select(action="set", ids=[s.actor])
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.actor and "intensity" in ui["fields"], "a light actor: its prefab's parameters")
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"radius": 900})
    g.run(4)
    ui = schema(s, action="ui")
    rect = ui["reset"].get("radius")
    if s.check(rect is not None and "intensity" not in ui["reset"], f"a changed value (only) has a reset arrow ({ui['reset']})"):
        s.click(centre(rect))
        s.check(close(value(s, s.light, "radius"), 600) and s.history()["undo"] == "Reset Radius",
                f"clicking it restores the default, one undo step ({s.history()['undo']})")
```

Append `check_details`; docstring: `- details: Details draws the selection's fields by category (a fog volume's Look closed at first); a changed value's reset arrow, clicked with the mouse, restores it.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema
```

Expected: `FAIL (1)`; `RpcError` `action: kinds, lint, get, set or reset` (no `ui` yet).

- [ ] **Step 3: Details' header and implementation**

Create `pc/src/editor_details.h`:

```cpp
/* Details (dev_overlay.cpp's Details tab; editor_details.cpp): the
 * selection's properties drawn from the editor's schema (editor_schema.h),
 * and Houdini's value ladder on its numbers. Editor internals. */
#ifndef ROOD_EDITOR_DETAILS_H
#define ROOD_EDITOR_DETAILS_H
#include <string>

namespace rood_details {
void draw(); /* the Details tab's body */
void ladder_frame(); /* a value ladder being dragged follows the mouse (every overlay frame) */
bool ladder_cancel(); /* Escape: the values back; true when a ladder was open */
std::string state_json(); /* RPC editor_schema {action "ui"} */
void set_search(const std::string& text);
} // namespace rood_details
#endif
```

Create `pc/src/editor_details.cpp`:

```cpp
// Details (editor_details.h), as Unreal's Details panel: the object's name
// and a lock, a search box, collapsible categories of label / value / reset
// rows (the reset arrow restores the default, or the preset's or prefab's
// value; tooltips give the range and unit), and the kind's own tools under
// them. A middle-drag on a number scrubs it on Houdini's value ladder: rows
// of steps 10 / 1 / 0.1 / 0.01 (up and down picks the step, left and right
// adds steps); Escape cancels; the release is one undo step.
#include "editor_details.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "editor_scene.h"
#include "editor_schema.h"
#include "editor_selection.h"
#include "editor_undo.h"

extern "C" bool pc_lights_glow_details(void); /* lights.cpp: a glow's overrides */

using rood_schema::Field;
using rood_schema::Kind;
using rood_schema::Value;
using rood_selection::Ref;

namespace {
const ImVec4 changed_colour(1.0f, 0.78f, 0.3f, 1); /* a value off its default, as the room settings' overrides */
const float STEPS[4] = { 10, 1, 0.1f, 0.01f }; /* the value ladder's rows, top to bottom */
char search[64];
/* As last drawn (RPC editor_schema {action "ui"}). */
Ref shown;
std::string shown_kind;
int shown_frame = -1;
std::vector<std::string> shown_fields;
std::map<std::string, ImVec4> value_rects, reset_rects; /* window pixels: x, y, w, h */
float row_height, notch;

struct Ladder {
    bool active = false;
    Ref target;
    std::string field;
    int component = 0; /* a vec3's x, y or z */
    int row = 0, first_row = 0; /* STEPS indices */
    float press_x = 0, press_y = 0, origin_x = 0;
    float base = 0, value = 0; /* the value when the row was chosen; now */
    std::vector<std::pair<Ref, Value>> before; /* every object it changes, as they were (Escape) */
} ladder;

bool numeric(const Field& f)
{
    return f.type == rood_schema::TYPE_FLOAT || f.type == rood_schema::TYPE_INT || f.type == rood_schema::TYPE_ANGLE
        || f.type == rood_schema::TYPE_VEC3;
}
bool matches(const Field& f) { return !search[0] || strcasestr(f.label.c_str(), search) || strcasestr(f.name.c_str(), search); }
void keep_rect(std::map<std::string, ImVec4>& rects, const std::string& name)
{
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    rects[name] = ImVec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
}
std::string number_format(const Field& f)
{
    int d = f.step >= 1 ? 0 : f.step >= 0.1f ? 1 : f.step >= 0.01f ? 2 : f.step >= 0.001f ? 3 : 5;
    std::string s = "%." + std::to_string(d) + "f";
    return f.unit.empty() ? s : s + " " + f.unit;
}
void tooltip(const Field& f, int index)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;
    std::string tip = f.tooltip.empty() ? f.label : f.tooltip;
    if (numeric(f)) {
        Field one = f;
        one.type = rood_schema::TYPE_FLOAT;
        tip += "\nRange " + rood_schema::format(one, Value::scalar(f.min)) + " to " + rood_schema::format(one, Value::scalar(f.max));
    }
    tip += "\nDefault " + rood_schema::format(f, f.def);
    const char* source = rood_schema::base_source_of(f, index);
    if (strcmp(source, "default"))
        tip += std::string("\nThe ") + source + "'s value " + rood_schema::format(f, rood_schema::base_of(f, index));
    if (numeric(f))
        tip += "\nMiddle-drag: the value ladder (steps of 10, 1, 0.1, 0.01)";
    ImGui::SetTooltip("%s", tip.c_str());
}

void ladder_begin(const Kind& k, const Field& f, const Ref& target, int index, int component)
{
    ladder = Ladder();
    ladder.active = true;
    ladder.target = target;
    ladder.field = f.name;
    ladder.component = component;
    int first = 3;
    float best = 1e30f;
    for (int r = 0; r < 4; r++) {
        float d = fabsf(logf(STEPS[r]) - logf(f.step > 0 ? f.step : 0.01f));
        if (d < best)
            best = d, first = r;
    }
    if (f.type == rood_schema::TYPE_INT && first > 1)
        first = 1;
    ladder.row = ladder.first_row = first;
    ImVec2 m = ImGui::GetIO().MousePos;
    ladder.press_x = ladder.origin_x = m.x;
    ladder.press_y = m.y;
    ladder.base = ladder.value = f.get(index).at(component);
    for (const Ref& r : rood_schema::targets(k, f, target)) {
        int i = rood_scene::index_of(r);
        if (i >= 0)
            ladder.before.push_back({ r, f.get(i) });
    }
    pc_undo_begin(("Edit " + f.label).c_str()); /* the whole drag is one undo step */
}
void ladder_end(bool keep)
{
    if (!keep)
        for (const auto& b : ladder.before) {
            int i;
            const Kind* k = rood_schema::kind_of(b.first, &i);
            const Field* f = k ? k->field(ladder.field) : nullptr;
            if (f)
                f->set(i, b.second);
        }
    ladder.active = false;
    ladder.before.clear();
    pc_undo_end();
}
void ladder_hover(const Kind& k, const Field& f, const Ref& target, int index, int component)
{
    if (!ladder.active && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        ladder_begin(k, f, target, index, component);
}

/* The field's widget; true when the user changed `v`. */
bool widget(const Kind& k, const Field& f, const Ref& target, int index, Value& v)
{
    bool edited = false;
    ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | (f.logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
    std::string fmt = number_format(f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    switch (f.type) {
    case rood_schema::TYPE_FLOAT:
    case rood_schema::TYPE_ANGLE: {
        float x = v.x();
        edited = ImGui::DragFloat("##value", &x, f.step, f.min, f.max, fmt.c_str(), flags);
        if (edited)
            v = Value::scalar(x);
        keep_rect(value_rects, f.name);
        ladder_hover(k, f, target, index, 0);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_INT: {
        int x = (int)lroundf(v.x());
        edited = ImGui::DragInt("##value", &x, f.step < 1 ? 1.0f : f.step, (int)f.min, (int)f.max, "%d", flags);
        if (edited)
            v = Value::scalar((float)x);
        keep_rect(value_rects, f.name);
        ladder_hover(k, f, target, index, 0);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_BOOL: {
        bool b = v.x() > 0.5f;
        edited = ImGui::Checkbox("##value", &b);
        if (edited)
            v = Value::scalar(b ? 1.0f : 0.0f);
        keep_rect(value_rects, f.name);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        bool open = ImGui::BeginCombo("##value", i >= 0 && i < (int)f.options.size() ? f.options[i].c_str() : "?");
        keep_rect(value_rects, f.name);
        if (!open)
            tooltip(f, index);
        if (open) {
            for (int o = 0; o < (int)f.options.size(); o++)
                if (ImGui::Selectable(f.options[o].c_str(), o == i)) {
                    v = Value::scalar((float)o);
                    edited = true;
                }
            ImGui::EndCombo();
        }
        break;
    }
    case rood_schema::TYPE_COLOUR: {
        float c[3] = { v.at(0), v.at(1), v.at(2) };
        edited = ImGui::ColorEdit3("##value", c, ImGuiColorEditFlags_Float | (f.max > 1 ? ImGuiColorEditFlags_HDR : 0));
        if (edited)
            v = Value::vec(c[0], c[1], c[2]);
        keep_rect(value_rects, f.name);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_VEC3: {
        /* Three fields, so the value ladder knows which one it scrubs. */
        float c[3] = { v.at(0), v.at(1), v.at(2) };
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        float w = (ImGui::GetContentRegionAvail().x - spacing * 2) / 3;
        static const char* const ids[3] = { "##x", "##y", "##z" };
        for (int a = 0; a < 3; a++) {
            if (a)
                ImGui::SameLine(0, spacing);
            ImGui::SetNextItemWidth(w);
            edited |= ImGui::DragFloat(ids[a], &c[a], f.step, f.min, f.max, fmt.c_str(), flags);
            keep_rect(value_rects, f.name + "." + std::string(1, "xyz"[a]));
            ladder_hover(k, f, target, index, a);
            tooltip(f, index);
        }
        if (edited)
            v = Value::vec(c[0], c[1], c[2]);
        break;
    }
    }
    return edited;
}

void row(const Kind& k, const Field& f, const Ref& target, int index)
{
    shown_fields.push_back(f.name);
    ImGui::PushID(f.name.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    bool off_base = f.resettable && rood_schema::is_overridden(f, index);
    ImGui::AlignTextToFramePadding();
    if (off_base)
        ImGui::TextColored(changed_colour, "%s", f.label.c_str());
    else
        ImGui::TextUnformatted(f.label.c_str());
    tooltip(f, index);
    ImGui::TableNextColumn();
    Value v = f.get(index);
    if (widget(k, f, target, index, v))
        rood_schema::apply(k, f, target, v);
    ImGui::TableNextColumn();
    if (off_base) {
        if (ImGui::ArrowButton("##reset", ImGuiDir_Left)) /* Unreal's yellow reset arrow */
            rood_schema::apply_reset(k, f, target);
        keep_rect(reset_rects, f.name);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            const char* source = rood_schema::base_source_of(f, index);
            std::string to = rood_schema::format(f, rood_schema::base_of(f, index));
            if (strcmp(source, "default"))
                ImGui::SetTooltip("Reset to the %s's value: %s", source, to.c_str());
            else
                ImGui::SetTooltip("Reset to the default: %s", to.c_str());
        }
    }
    ImGui::PopID();
}

/* A padlock: closed while locked, its shackle swung open when not. */
bool lock_button(bool locked)
{
    float side = ImGui::GetFrameHeight();
    if (locked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    bool clicked = ImGui::Button("##lock", ImVec2(side, side));
    if (locked)
        ImGui::PopStyleColor(2);
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    float cx = (lo.x + hi.x) * 0.5f, w = side * 0.42f, body = lo.y + side * 0.48f;
    d->AddRectFilled(ImVec2(cx - w * 0.5f, body), ImVec2(cx + w * 0.5f, hi.y - side * 0.18f), col, side * 0.06f);
    float r = w * 0.32f, sx = locked ? cx : cx + w * 0.3f;
    d->PathArcTo(ImVec2(sx, body - side * 0.02f), r, 3.14159265f, 2 * 3.14159265f, 10);
    d->PathStroke(col, 0, side * 0.09f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(locked ? "Locked: Details keeps this object while you select others (click to follow the selection)"
                                 : "Lock Details to this object while you select others");
    return clicked;
}
} // namespace

void rood_details::draw()
{
    shown_fields.clear();
    value_rects.clear();
    reset_rects.clear();
    shown_frame = ImGui::GetFrameCount();
    row_height = ImGui::GetFrameHeight();
    notch = ImGui::GetFontSize() * 0.75f;
    Ref locked = rood_selection::locked();
    Ref subject = locked ? locked : rood_selection::primary();
    shown = subject;
    shown_kind.clear();
    ImGui::PushID("details");
    if (!subject) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::TextWrapped("Click a light, light actor, glow or fog volume in the view or the Outliner: its properties "
                           "show here. Room-wide settings are on the Lighting, Atmosphere, Post-processing and Graphics tabs.");
        ImGui::PopID();
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rood_scene::label(subject).c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    if (lock_button((bool)locked))
        rood_selection::lock(!locked);
    if (subject.kind == rood_selection::KIND_GLOW) {
        pc_lights_glow_details();
        ImGui::PopID();
        return;
    }
    int index;
    const Kind* k = rood_schema::kind_of(subject, &index);
    if (!k) {
        ImGui::TextDisabled("It is gone (deleted or reloaded).");
        ImGui::PopID();
        return;
    }
    shown_kind = k->name;
    size_t n = rood_selection::items().size();
    if (locked && !rood_selection::contains(subject))
        ImGui::TextDisabled("%s (locked; the selection is elsewhere)", k->label.c_str());
    else if (n > 1)
        ImGui::TextDisabled("%s; %zu selected: edits go to every selected %s", k->label.c_str(), n, k->label.c_str());
    else
        ImGui::TextDisabled("%s", k->label.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "Search details", search, sizeof search);
    std::vector<std::string> categories;
    for (const Field& f : k->fields)
        if (std::find(categories.begin(), categories.end(), f.category) == categories.end())
            categories.push_back(f.category);
    for (const std::string& c : categories) {
        bool any = false;
        for (const Field& f : k->fields)
            any |= f.category == c && matches(f);
        if (!any)
            continue;
        if (search[0])
            ImGui::SetNextItemOpen(true); /* a search shows its matches in every category */
        bool closed = std::find(k->closed.begin(), k->closed.end(), c) != k->closed.end();
        if (!ImGui::CollapsingHeader((c + "##category").c_str(), closed ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        if (ImGui::BeginTable(c.c_str(), 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            for (const Field& f : k->fields)
                if (f.category == c && matches(f))
                    row(*k, f, subject, index);
            ImGui::EndTable();
        }
    }
    if (k->extras) {
        ImGui::Spacing();
        k->extras(index);
    }
    ImGui::PopID();
}

void rood_details::ladder_frame()
{
    if (!ladder.active)
        return;
    int index;
    const Kind* k = rood_schema::kind_of(ladder.target, &index);
    const Field* f = k ? k->field(ladder.field) : nullptr;
    if (!f) {
        ladder_end(false);
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    row_height = ImGui::GetFrameHeight();
    notch = ImGui::GetFontSize() * 0.75f;
    int last = f->type == rood_schema::TYPE_INT ? 1 : 3;
    int row = ladder.first_row + (int)floorf((io.MousePos.y - ladder.press_y) / row_height + 0.5f);
    row = row < 0 ? 0 : row > last ? last : row;
    if (row != ladder.row) { /* a new step: scrub on from the value now */
        ladder.row = row;
        ladder.base = ladder.value;
        ladder.origin_x = io.MousePos.x;
    }
    int notches = (int)((io.MousePos.x - ladder.origin_x) / notch); /* toward zero */
    Value v = f->get(index);
    if (f->type == rood_schema::TYPE_VEC3)
        v = Value::vec(v.at(0), v.at(1), v.at(2));
    v.v[ladder.component] = ladder.base + STEPS[ladder.row] * notches;
    v = rood_schema::clamp(*f, v);
    ladder.value = v.at(ladder.component);
    rood_schema::apply(*k, *f, ladder.target, v);
    /* The ladder: a column of steps with the chosen one lit, the value beside it. */
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float w = ImGui::GetFontSize() * 4;
    for (int r = 0; r <= last; r++) {
        float cy = ladder.press_y + (r - ladder.first_row) * row_height;
        ImVec2 lo(ladder.press_x - w * 0.5f, cy - row_height * 0.5f), hi(ladder.press_x + w * 0.5f, cy + row_height * 0.5f);
        d->AddRectFilled(lo, hi, r == ladder.row ? IM_COL32(70, 110, 170, 235) : IM_COL32(30, 30, 36, 220));
        d->AddRect(lo, hi, IM_COL32(0, 0, 0, 200));
        char text[16];
        snprintf(text, sizeof text, "%g", STEPS[r]);
        ImVec2 ts = ImGui::CalcTextSize(text);
        d->AddText(ImVec2(ladder.press_x - ts.x * 0.5f, cy - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), text);
    }
    Field one = *f;
    one.type = f->type == rood_schema::TYPE_VEC3 ? rood_schema::TYPE_FLOAT : f->type;
    std::string shown_value = rood_schema::format(one, Value::scalar(ladder.value));
    float cy = ladder.press_y + (ladder.row - ladder.first_row) * row_height;
    d->AddText(ImVec2(ladder.press_x + w * 0.5f + 6, cy - ImGui::GetFontSize() * 0.5f), IM_COL32(255, 230, 120, 255),
        shown_value.c_str());
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        ladder_end(true);
}

bool rood_details::ladder_cancel()
{
    if (!ladder.active)
        return false;
    ladder_end(false);
    return true;
}

void rood_details::set_search(const std::string& text) { snprintf(search, sizeof search, "%s", text.c_str()); }

std::string rood_details::state_json()
{
    auto q = [](const std::string& s) { return prefab::json_quote(s); };
    bool current = ImGui::GetCurrentContext() && shown_frame == ImGui::GetFrameCount();
    auto rects_json = [&](const std::map<std::string, ImVec4>& rects) {
        std::string s = "{";
        bool first = true;
        for (const auto& kv : rects) {
            if (!current)
                break;
            char r[96];
            snprintf(r, sizeof r, "[%.1f,%.1f,%.1f,%.1f]", kv.second.x, kv.second.y, kv.second.z, kv.second.w);
            s += (first ? "" : ",") + q(kv.first) + ":" + r;
            first = false;
        }
        return s + "}";
    };
    Ref pinned = rood_selection::locked();
    std::string s = "{\"subject\":" + (current && shown ? q(shown.id) : std::string("null"))
        + ",\"kind\":" + (current && !shown_kind.empty() ? q(shown_kind) : std::string("null"))
        + ",\"locked\":" + (pinned ? q(pinned.id) : std::string("null")) + ",\"search\":" + q(search) + ",\"fields\":[";
    for (size_t i = 0; current && i < shown_fields.size(); i++)
        s += (i ? "," : "") + q(shown_fields[i]);
    s += "],\"rects\":" + rects_json(value_rects) + ",\"reset\":" + rects_json(reset_rects);
    char lad[400];
    snprintf(lad, sizeof lad,
        ",\"ladder\":{\"active\":%s,\"field\":%s,\"component\":%d,\"row\":%d,\"first_row\":%d,\"step\":%g,"
        "\"row_height\":%.2f,\"notch\":%.2f,\"value\":%.6g}}",
        ladder.active ? "true" : "false", q(ladder.field).c_str(), ladder.component, ladder.row, ladder.first_row,
        STEPS[ladder.row], row_height, notch, ladder.value);
    return s + lad;
}
```

- [ ] **Step 4: The lights' and actors' own tools**

In `pc/src/lights_internal.h` replace `bool actor_details();` with:

```cpp
/* Details: a kind's own tools under its schema fields (editor_schema.cpp). */
void light_details_extra(int index); /* lights.cpp: Duplicate, Delete, Create prefab */
void actor_details_extra(int index); /* the prefab line, Reset all, fixture, glow, its lights, actions */
```

In `pc/src/lights.cpp` replace `pc_lights_details` (lines 1804-1877) with:

```cpp
/* Details > a light's own actions under its fields (editor_schema.cpp
 * draws those): Duplicate, Delete, and Create prefab from the selected
 * lights (they are replaced by an actor of it). */
void rood_lights::light_details_extra(int index)
{
    if (index < 0 || index >= (int)lights.size())
        return;
    Light& l = lights[index];
    ImGui::PushID(index);
    if (ImGui::Button("Duplicate")) {
        pc_undo_hint((std::string("Duplicate ") + light_type_name(l.type) + " " + std::to_string(index + 1)).c_str());
        Light copy = l;
        copy.position[0] += 64;
        lights.push_back(copy);
        select_light((int)lights.size() - 1);
        dirty = true;
        ImGui::PopID();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        pc_undo_hint((std::string("Delete ") + light_type_name(l.type) + " " + std::to_string(index + 1)).c_str());
        lights.erase(lights.begin() + index);
        if (solo == index)
            solo = -1;
        else if (solo > index)
            --solo;
        select_none();
        dirty = true;
        ImGui::PopID();
        return;
    }
    /* Create prefab from selection: this light (and the other selected
     * lights) become a prefab, replaced here by an actor of it. */
    ImGui::SameLine();
    static char name[64];
    if (ImGui::Button("Create prefab..."))
        ImGui::OpenPopup("create prefab");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Make a prefab of the selected light%s (Shift or Ctrl+click adds more);\n"
                          "they are replaced by an actor of it",
            multi.empty() ? "" : "s");
    if (ImGui::BeginPopup("create prefab")) {
        ImGui::Text("Prefab from %zu light%s", multi.size() + 1, multi.empty() ? "" : "s");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputTextWithHint("##name", "Name (e.g. Chandelier)", name, sizeof name,
            ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Create") || enter) && name[0]) {
            if (create_prefab_from_selection(name))
                name[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
```

In `pc/src/light_actors.cpp`: delete `const ImVec4 overridden_colour(...)` (line 41) and the whole `param_row` function (its anonymous namespace keeps `std::string editing_prefab;`). Replace the head of `rood_lights::actor_details()` — from its signature through the line `ImGui::TextDisabled("Highlighted values override the prefab for this actor.");` and the brace after it — with:

```cpp
/* Details > a light actor's own tools under its fields (editor_schema.cpp
 * draws its transform and its prefab's parameters): the prefab, Reset all,
 * the fixture, the glow link, its lights now, Duplicate / Break / Delete,
 * the prefab's actions. */
void rood_lights::actor_details_extra(int index)
{
    if (index < 0 || index >= (int)actors.size())
        return;
    Actor& a = actors[index];
    const Prefab* p = prefab::library().find(a.prefab);
    ImGui::PushID(20000 + index);
    if (!p) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Prefab '%s' not found (pc/assets/prefabs, %s)", a.prefab.c_str(),
            prefab::library().user_dir.c_str());
    } else {
        ImGui::TextDisabled("Prefab %s: %s%s", p->id.c_str(),
            p->user ? (p->overrides_builtin ? "yours, over the built-in, " : "yours, ") : "built-in, ", p->path.c_str());
        if (!p->error.empty())
            ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", p->error.c_str());
        if (!a.overrides.empty() && ImGui::SmallButton("Reset all to the prefab")) {
            pc_undo_hint("Reset all to the prefab");
            a.overrides.clear();
            dirty = true;
        }
    }
```

In the rest of that function (from `ImGui::SeparatorText("Fixture");` to its end) change every `return true;` to `return;`, and delete the final `return true;` after the last `ImGui::PopID();`.

- [ ] **Step 5: The fog volume's own tools (atmosphere.cpp)**

Declare in `environment_internal.h` (`rood_atmo`): `void details_extra(int index); /* Details: a fog volume's tools under its fields */`.

In `atmosphere.cpp` delete `weight_row` (lines 1321-1347). Replace `interaction_ui` (lines 1349-1423) with:

```cpp
int extras_frame = -100; /* the selected volume's Details tools last drawn (the ghost preview) */
/* The interactive fog's preview tools (its sliders are Details fields:
 * editor_schema.cpp): its field in the view, the ghost at the mouse,
 * stamps at Ashley, its state. */
void preview_tools(Volume& v)
{
    ImGui::SeparatorText("Interactive fog preview");
    ImGui::BeginDisabled(!v.sim);
    ImGui::Checkbox("Show the field", &show_field);
    tooltip("Its density on its floor in the view: red cleared, blue piled.");
    ImGui::SameLine();
    ImGui::Checkbox("Ghost at the mouse", &ghost_mode);
    tooltip("The surface point under the mouse parts the fog like a character (a preview without\n"
            "walking). The game must be running: the fog steps on game time.");
    struct pc_dev_state st;
    memset(&st, 0, sizeof st);
    if (pc_dev_state)
        pc_dev_state(&st);
    ImGui::BeginDisabled(!st.has_player);
    static const char* names[rood_fog_sim::SHAPES] = { "Burst", "Ring", "Swing", "Vortex" };
    for (int shape = 0; shape < rood_fog_sim::SHAPES; shape++) {
        if (shape)
            ImGui::SameLine();
        if (ImGui::SmallButton(names[shape])) {
            rood_fog_sim::Event e;
            e.source = rood_fog_sim::GHOST, e.shape = shape;
            e.pos[0] = (float)st.x, e.pos[1] = (float)st.y, e.pos[2] = (float)st.z;
            float a = st.facing * (6.2831853f / 4096);
            e.dir[0] = -sinf(a), e.dir[1] = -cosf(a);
            e.radius = shape == rood_fog_sim::BURST ? 130 : shape == rood_fog_sim::SWING ? 95 : 110;
            e.frames = shape == rood_fog_sim::SWING ? 12 : 18;
            rood_fog_sim::emit(e);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("at Ashley (stamps, whatever the weights)");
    ImGui::EndDisabled();
    rood_fog_sim::Stats s;
    if (rood_fog_sim::stats(v.id.c_str(), &s))
        ImGui::TextDisabled("field: least %.2f, mean %.3f, %s, %d source%s, %u steps, %.3f ms", s.least, s.mean,
            s.awake ? "ticking" : "at rest", s.actors, s.actors == 1 ? "" : "s", s.steps, s.ms);
    else if (v.sim)
        ImGui::TextDisabled("%s", v.layer < 0 ? "not simulated (over the room's budget, the fog off, or no source yet)"
                                             : "no field yet");
    if (ImGui::SmallButton("Settle now"))
        rood_fog_sim::reset();
    ImGui::EndDisabled();
}
```

In `pc_atmosphere_page` replace everything from `ImGui::SeparatorText("Fog volumes");` through the closing brace of `if (selected >= 0 && selected < (int)volumes.size()) { … }` (lines 1489-1586) with:

```cpp
    ImGui::SeparatorText("Fog volumes");
    if (ImGui::Button("Add volume")) {
        if (add_volume()) {
            changed = true;
            pc_undo_hint("Add fog volume");
        }
    }
    tooltip("A box of mist on the surface under the view's centre, selected: its fields are in Details.");
    ImGui::SameLine();
    ImGui::Checkbox("Show in the view", &show_volumes);
    ImGui::TextDisabled("%d in this room: in the Outliner (Fog volumes); click one there or in the view\n"
                        "to edit it in Details.",
        (int)volumes.size());
```

After `rood_atmo::schema_kind` add:

```cpp
/* Details > a fog volume's own tools under its fields (editor_schema.cpp
 * draws those): the gizmo's mode, Drop to floor and Fit to floor area, its
 * name, its preset, the interactive fog's preview, Delete. */
void rood_atmo::details_extra(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    extras_frame = ImGui::GetFrameCount();
    Volume& v = volumes[index];
    bool changed = false;
    ImGui::PushID(v.id.c_str());
    ImGui::SeparatorText("Tools");
    ImGui::RadioButton("Move", &operation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Size", &operation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Turn", &operation, 2);
    bool end = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_End, false);
    if (ImGui::Button("Drop to floor (End)") || end) {
        pc_undo_hint("Drop fog volume to floor");
        changed |= drop_to_floor(v);
    }
    tooltip("Moves it down (or up) so its bottom face rests on the floor under it: the highest floor under\n"
            "its centre and its footprint's corners. While moving it with the gizmo, the editor's snapping\n"
            "(point, edge, midpoint, surface) puts its bottom face on the target.");
    ImGui::SameLine();
    if (ImGui::Button("Fit to floor area")) {
        pc_undo_hint("Fit fog volume to floor area");
        changed |= fit_to_floor(v);
    }
    tooltip("Its footprint becomes the level floor around its centre (the room's floor grid: the cells\n"
            "connected to the one under it, within 12 units of its height), square to the room, on the floor.");
    char name[96];
    snprintf(name, sizeof name, "%s", v.name.c_str());
    if (ImGui::InputText("Name", name, sizeof name)) {
        v.name = name;
        changed = true;
    }
    changed |= preset_ui(v);
    preview_tools(v);
    if (ImGui::SmallButton("Delete volume")) {
        pc_undo_hint("Delete fog volume");
        remove_volume(index);
        rood_scene::refresh();
        changed = true;
    }
    ImGui::PopID();
    if (changed)
        pc_lights_mark_dirty();
}
```

In `pc_atmosphere_viewport`, replace `bool page = ImGui::GetFrameCount() - page_frame <= 1;` with `bool tools = ImGui::GetFrameCount() - extras_frame <= 1; /* the selected volume's Details tools show */` and in the ghost block replace `ghost_mode && page` with `ghost_mode && tools`.

- [ ] **Step 6: Each kind's tools, and the RPC ui (editor_schema.cpp)**

Add `#include "editor_details.h"`. In `light_kind` add after `k.label = …;`: `k.extras = [](int i) { rood_lights::light_details_extra(i); };`. In `actor_kind` after `k.label = …;`: `k.extras = [](int i) { rood_lights::actor_details_extra(i); };`. In `atmosphere.cpp` `schema_kind` after `k.closed = …;`: `k.extras = [](int i) { rood_atmo::details_extra(i); };`. In `pc_editor_schema_rpc`, before the final `} else {` add:

```cpp
    } else if (action == "ui") {
        if (const Json* s = params.get("search"))
            rood_details::set_search(s->string);
        text = rood_details::state_json();
```

and change the failure text to `"action: kinds, lint, get, set, reset or ui"`.

- [ ] **Step 7: The Details tab draws it (dev_overlay.cpp)**

Add `#include "editor_details.h"`. Delete the declaration `bool pc_lights_details(void);` (line 642). Replace the Details tab's body (`if (!pc_lights_details() && !pc_lights_glow_details()) …`) with `rood_details::draw();`. In `pc_overlay_frame`, after `viewport_keys();` add:

```cpp
        rood_details::ladder_frame(); /* a value ladder being dragged (editor_details.cpp) */
```

In `pc_overlay_event`, before `/* Escape during a drag from Place Actors cancels the drag only. */` add:

```cpp
        /* Escape during a value ladder drag cancels it (editor_details.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_details::ladder_cancel())
            return 1;
```

In `build_boot.py` add `root / "pc/src/editor_details.cpp",` after `editor_schema.cpp`.

- [ ] **Step 8: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema editor_selection light_actors fog_sim atmosphere_smoke fixture_lighting
```

Expected: all `PASS`.

- [ ] **Step 9: Commit**

```bash
git add pc/src/editor_details.h pc/src/editor_details.cpp pc/src/lights.cpp pc/src/light_actors.cpp \
  pc/src/lights_internal.h pc/src/atmosphere.cpp pc/src/environment_internal.h pc/src/editor_schema.cpp \
  pc/src/dev_overlay.cpp pc/tools/build_boot.py pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Details drawn from the schema: categories, tooltips with range and unit, per-value reset; kinds' own tools beneath

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 13: Search and the lock

**Files:**
- Modify: `pc/src/editor_scene.cpp` (RPC chain)
- Test: `pc/tests/editor_schema_smoke.py`

(The search box and the padlock are drawn since Task 12; this task adds their RPC and proves them.)

- [ ] **Step 1: Write the failing section**

```python
def check_search_lock(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    g.call("editor_ui", details="details")
    g.run(4)
    schema(s, action="ui", search="rad")
    g.run(3)
    ui = schema(s, action="ui")
    s.check(ui["fields"] == ["radius"] and ui["search"] == "rad", f"search: only the matching field shows ({ui['fields']})")
    schema(s, action="ui", search="")
    g.run(3)
    s.check(len(schema(s, action="ui")["fields"]) > 4, "an empty search shows every field again")
    s.select(action="lock", on=True)
    s.select(action="set", ids=[s.actor])
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.light and ui["locked"] == s.light, "locked: Details keeps the light while the candle is selected")
    g.call("editor_ui", details="graphics")
    g.run(2)
    s.select(action="set", ids=[s.volume])
    g.run(3)
    s.check(g.call("editor_ui")["details"] == "graphics", "locked: a new selection does not switch tabs")
    schema(s, action="set", values={"intensity": 3})  # no id: the object Details shows
    s.check(close(value(s, s.light, "intensity"), 3), "an edit without an id goes to Details' (locked) object")
    s.select(action="lock", on=False)
    g.call("editor_ui", details="details")
    g.run(4)
    s.check(schema(s, action="ui")["subject"] == s.volume, "unlocked: Details follows the selection again")
    schema(s, action="reset", id=s.light, fields=["intensity"])
```

Append `check_search_lock`; docstring: `- search_lock: search shows only matching fields; the lock keeps Details on its object (no tab switch), RPC edits without an id go to it; unlocking follows the selection again.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema
```

Expected: `FAIL (1)`; `RpcError` `unknown action lock`.

- [ ] **Step 3: The RPC lock (editor_scene.cpp)**

Before the chain's final `} else {`:

```cpp
    } else if (action == "lock") {
        rood_selection::lock(flag(params, "on")); /* Details' lock, on the primary */
```

and add `lock {on}` to the doc comment.

- [ ] **Step 4: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema
```

Expected: `PASS`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_scene.cpp pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Details search and lock: matching fields only; the lock keeps its object and tab while the selection moves

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: The value ladder

**Files:**
- Modify: none in C++ (drawn and wired in Task 12); this task proves it and fixes what the test finds.
- Test: `pc/tests/editor_schema_smoke.py`

- [ ] **Step 1: Write the section**

```python
def check_ladder(s):
    """Middle-drag on a number: up/down picks steps of 10, 1, 0.1, 0.01, left/right adds them; Esc cancels."""
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="reset", id=s.light, fields=["intensity", "radius"])
    g.call("editor_ui", details="details")
    g.run(4)
    c = centre(schema(s, action="ui")["rects"]["intensity"])
    done = s.history()["done"]
    s.mouse(c, button=2, down=True)
    lad = schema(s, action="ui")["ladder"]
    s.check(lad["active"] and lad["field"] == "intensity" and close(lad["step"], 0.01),
            f"a middle press on Intensity opens the ladder on its own step ({lad})")
    rh, notch = lad["row_height"], lad["notch"]
    s.mouse([c[0], c[1] - rh], button=2, down=True)  # one row up: steps of 0.1
    s.mouse([c[0] + notch * 5.5, c[1] - rh], button=2, down=True)  # five notches right
    lad = schema(s, action="ui")["ladder"]
    s.check(close(lad["step"], 0.1) and close(lad["value"], 1.5), f"one row up, five notches right: +0.5 ({lad})")
    s.mouse([c[0] + notch * 5.5, c[1] - rh], button=2, down=False)
    h = s.history()
    s.check(close(value(s, s.light, "intensity"), 1.5) and not schema(s, action="ui")["ladder"]["active"]
            and h["done"] == done + 1 and h["undo"] == "Edit Intensity",
            f"released: the value stays, one undo step ({h['undo']})")
    c = centre(schema(s, action="ui")["rects"]["radius"])
    s.mouse(c, button=2, down=True)
    lad = schema(s, action="ui")["ladder"]
    s.check(close(lad["step"], 1), f"Radius opens on steps of 1 ({lad})")
    s.mouse([c[0], c[1] - lad["row_height"]], button=2, down=True)  # up: steps of 10
    s.mouse([c[0] - notch * 3.5, c[1] - lad["row_height"]], button=2, down=True)  # three notches left
    s.mouse([c[0] - notch * 3.5, c[1] - lad["row_height"]], button=2, down=False)
    s.check(close(value(s, s.light, "radius"), 570), f"Radius: three notches left in steps of 10 ({value(s, s.light, 'radius')})")
    c = centre(schema(s, action="ui")["rects"]["intensity"])
    done = s.history()["done"]
    s.mouse(c, button=2, down=True)
    s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=True)  # steps of 0.01: +0.03
    mid = value(s, s.light, "intensity")
    state = s.keys(["escape"])
    s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=False)
    s.check(close(mid, 1.53) and close(value(s, s.light, "intensity"), 1.5) and state["visible"]
            and s.history()["done"] == done, f"Escape cancels: the value back, the editor open, no undo step ({mid})")
```

Append `check_ladder`; docstring: `- ladder: a middle press opens the value ladder on the field's step; one row up and five notches right adds 0.5 (Intensity), three notches left in steps of 10 takes 30 (Radius); the release is one undo step; Escape puts the value back and leaves the editor open.`

- [ ] **Step 2: Run it**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema
```

Expected: `PASS`. If a check fails, use superpowers:systematic-debugging: read `pc/build/tests/editor_schema_smoke/output.log` (the ladder state is printed in each message), then fix `ladder_begin` / `ladder_frame` in `pc/src/editor_details.cpp` (the row and notch arithmetic) and rerun.

- [ ] **Step 3: Commit**

```bash
git add pc/tests/editor_schema_smoke.py pc/src/editor_details.cpp
git commit -m "$(cat <<'EOF'
Value ladder test: middle-drag scrubs in steps of 10/1/0.1/0.01, one undo step; Esc cancels

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 15: Hide, isolate, show all (editor state in the user settings)

**Files:**
- Modify: `pc/src/editor_scene.cpp`, `pc/src/lights.cpp` (light/actor/glow draw loops; `pc_lights_world` lines 1526-1546; Outliner rows), `pc/src/light_actors.cpp` (actor rows), `pc/src/atmosphere.cpp` (`volumes_on` line 190, sim loop line 683, `pc_medium_gpu_get` line 798, `pc_fogvol_gpu_get` line 955, viewport loop, Outliner rows), `pc/src/project_settings.cpp`, `pc/src/dev_overlay.cpp` (`viewport_keys`)
- Test: `pc/tests/editor_selection_smoke.py`

- [ ] **Step 1: Write the failing section**

```python
def check_hide(s):
    """H hides the selection in the editor (its icon, picking, its light while the editor shows), Ctrl+H shows
    everything, Shift+H hides everything else; kept in the user settings, never in the room's lighting."""
    g = s.game
    g.run(2)
    before = g.call("actors")["lights_wanted"]
    xy = s.objects()[s.light]["screen"]
    s.select(action="set", ids=[s.light])
    s.keys(["h"])
    r = s.select()
    o = {x["id"]: x for x in r["objects"]}
    s.check(s.light in r["hidden"] and r["selection"] == [] and o[s.light]["hidden"] and o[s.light]["screen"] is None,
            f"H: the light is hidden and deselected ({r['hidden']})")
    s.check(r["hidden_setting"] == f"editor_hidden_{s.zone}_{s.room} {s.light}", f"its settings line ({r['hidden_setting']})")
    g.run(2)
    s.check(g.call("actors")["lights_wanted"] == before - 1, "while the editor shows, a hidden light lights nothing")
    s.check(s.light not in s.select(action="click", x=xy[0], y=xy[1])["selection"], "a hidden light cannot be picked")
    saved = g.call("lighting", action="save")
    text = (s.lights_dir / f"zone{s.zone:02d}_room{s.room:02d}.txt").read_text()
    s.check(saved.get("ok") and s.light.split(":", 1)[1] in text and "hidden" not in text,
            "saving the room writes the light; nothing about hiding goes into the lighting data")
    g.call("settings", action="save")
    s.check(f"editor_hidden_{s.zone}_{s.room} {s.light}" in s.settings.read_text(), "File > Save settings keeps the hidden set")
    s.keys(["h"], ctrl=True)
    g.run(2)
    s.check(s.select()["hidden"] == [] and g.call("actors")["lights_wanted"] == before, "Ctrl+H shows everything again")
    g.call("settings", action="load")
    g.run(2)
    s.check(s.select()["hidden"] == [s.light], "loading the settings brings the hidden set back")
    s.keys(["h"], ctrl=True)
    s.select(action="set", ids=[s.actor])
    s.keys(["h"], shift=True)
    r = s.select()
    s.check(s.light in r["hidden"] and s.volume in r["hidden"] and s.actor not in r["hidden"] and r["selection"] == [s.actor],
            f"Shift+H hides everything but the selection ({r['hidden']})")
    s.keys(["h"], ctrl=True)
    s.check(s.select()["hidden"] == [], "and Ctrl+H shows it all")
```

Append `check_hide`; docstring: `- hide: H hides the selection (no icon, no pick, no light while the editor shows), Ctrl+H shows all, Shift+H hides the rest; the set is a user setting (saved and loaded), never lighting data.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)`; `FAIL: H: the light is hidden and deselected ([])`.

- [ ] **Step 3: Hidden predicates and operations (editor_scene.cpp)**

Add after the `pc_lights_tools_view` declaration: `extern "C" int pc_overlay_visible(void); /* dev_overlay.cpp */`. Before `/* ---- RPC editor_select ---- */` add:

```cpp
/* ---- hidden in the editor ---- */

namespace {
bool hiding() { return rood_selection::any_hidden() && pc_overlay_visible(); }
} // namespace
bool rood_scene::light_hidden(int index)
{
    return hiding() && index >= 0 && index < (int)lights.size()
        && rood_selection::hidden(Ref { rood_selection::KIND_LIGHT, "light:" + lights[index].id });
}
bool rood_scene::actor_hidden(int index)
{
    return hiding() && index >= 0 && index < (int)actors.size()
        && rood_selection::hidden(Ref { rood_selection::KIND_ACTOR, "actor:" + actors[index].id });
}
bool rood_scene::volume_hidden_id(const std::string& id)
{
    return hiding() && rood_selection::hidden(Ref { rood_selection::KIND_VOLUME, "fog:" + id });
}
bool rood_scene::glow_hidden(const char* key, const float position[3])
{
    return hiding() && rood_selection::hidden(glow_ref_key(key, position));
}
void rood_scene::hide_selection()
{
    std::vector<Ref> refs = rood_selection::items();
    if (refs.empty())
        return;
    rood_selection::hide(refs);
    rood_selection::clear(rood_selection::FROM_CODE);
    publish();
    snprintf(status, sizeof status, "Hid %zu object%s in the editor (Ctrl+H shows everything)", refs.size(),
        refs.size() == 1 ? "" : "s");
}
void rood_scene::isolate_selection()
{
    if (rood_selection::empty())
        return;
    std::vector<Ref> others;
    for (const Ref& r : all_objects())
        if (!rood_selection::contains(r))
            others.push_back(r);
    rood_selection::hide(others);
    snprintf(status, sizeof status, "Hid %zu other object%s in the editor (Ctrl+H shows everything)", others.size(),
        others.size() == 1 ? "" : "s");
}
void rood_scene::unhide_all()
{
    rood_selection::unhide_all();
    snprintf(status, sizeof status, "Everything shows again");
}
```

RPC chain, before the final `} else {`:

```cpp
    } else if (action == "hide") {
        if (refs.empty())
            rood_scene::hide_selection();
        else {
            rood_selection::hide(refs);
            for (const Ref& r : refs)
                rood_selection::remove(r, source);
        }
    } else if (action == "unhide_all") {
        rood_scene::unhide_all();
    } else if (action == "isolate") {
        rood_scene::isolate_selection();
```

(doc: `hide {ids} (default the selection), isolate, unhide_all`).

- [ ] **Step 4: Hidden objects are left out of the view and the light (lights.cpp)**

In the light loop of `pc_lights_viewport` change `if (!view_project(v, l.position, p)) continue;` to `if (rood_scene::light_hidden((int)i) || !view_project(v, l.position, p)) continue;`. At the top of the actor loop body add `if (rood_scene::actor_hidden((int)i)) continue;`. In the glow loop change the first `continue` condition to `if (g.color[3] < 0 || g.category == PC_GLOW_EFFECT || rood_scene::glow_hidden(g.key, g.position) || !view_project(v, g.position, p))`. In `pc_lights_world` change

```cpp
        if (lights[i].enabled && (!soloing || (int)i == solo))
```

to

```cpp
        if (lights[i].enabled && (!soloing || (int)i == solo) && !rood_scene::light_hidden((int)i))
```

and `if (!actors[i].enabled || (soloing && (int)i != solo_actor))` to `if (!actors[i].enabled || (soloing && (int)i != solo_actor) || rood_scene::actor_hidden((int)i))`.

In `pc_lights_outliner` make a hidden row dim and say so: change `bool silent = …;` to also `|| rood_selection::hidden(ref)` (move the `ref` line above `silent`), and draw the Selectable label as `rood_selection::hidden(ref) ? (std::string(label) + "  [hidden]").c_str() : label`. Do the same in `actors_outliner` (`silent` gains `|| rood_selection::hidden(ref)`; the TreeNodeEx format gets `"%s%s%s%s"` with `rood_selection::hidden(ref) ? "  [hidden]" : ""` appended) and in `rood_atmo::outliner` (`silent = !v.enabled || rood_selection::hidden(ref)`; label gains `"  [hidden]"`).

- [ ] **Step 5: Hidden fog volumes are left out (atmosphere.cpp)**

After `bool fog_on() …` add:

```cpp
/* A volume the renderers draw: on, with density, and not hidden in the editor (editor_scene.cpp). */
bool drawn(const Volume& v) { return v.enabled && v.density > 0 && !rood_scene::volume_hidden_id(v.id); }
```

Use it in `volumes_on` (`n += drawn(v);`), the sim loop (`if (!drawn(v) || shown >= PC_MEDIUM_VOLUMES) continue;`), `pc_medium_gpu_get` (`if (!drawn(v) || n >= PC_MEDIUM_VOLUMES) continue;`) and `pc_fogvol_gpu_get` (`if (drawn(v) && n < count) drawn_list[n++] = &v;` — rename that function's local array `drawn` to `drawn_list` throughout its body so the name does not shadow the predicate). In `pc_atmosphere_viewport`'s drawing loop add first: `if (rood_scene::volume_hidden_id(v.id)) continue;`.

- [ ] **Step 6: The settings lines (project_settings.cpp)**

Add `#include "editor_selection.h" /* editor_hidden_<zone>_<room>: objects hidden in the editor */` after `#include "post.h"`. In `apply_line`, before `int applied = 0;`, add:

```cpp
    if (!strncmp(key, "editor_hidden_", 14)) /* editor state: objects hidden in a room (editor_selection.h) */
        return rood_selection::parse_hidden(key, rest) ? 1 : 0;
```

In `pc_settings_write`, after the `for (const Entry& e : entries) { … }` loop, add `rood_selection::write_hidden(f);`. In `pc_settings_load`, right after `char line[512];` add `rood_selection::clear_hidden(); /* the file's sets replace them */`.

- [ ] **Step 7: The keys (dev_overlay.cpp viewport_keys)**

After the Delete/Backspace block add:

```cpp
    /* Hiding, wherever the pointer is (editor state, never saved in the room):
     * H the selection, Shift+H everything else, Ctrl+H shows everything. */
    if (!io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        if (io.KeyCtrl)
            rood_scene::unhide_all();
        else if (io.KeyShift)
            rood_scene::isolate_selection();
        else
            rood_scene::hide_selection();
    }
```

- [ ] **Step 8: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection editor_undo fog_volume atmosphere_smoke system_settings
```

Expected: all `PASS` (`editor_undo_smoke`: the settings lines are preferences, never transactions).

- [ ] **Step 9: Commit**

```bash
git add pc/src/editor_scene.cpp pc/src/lights.cpp pc/src/light_actors.cpp pc/src/atmosphere.cpp \
  pc/src/project_settings.cpp pc/src/dev_overlay.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Editor hide / isolate / show all (H, Shift+H, Ctrl+H): editor state per room in the user settings

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 16: Copy, paste, duplicate

**Files:**
- Create: `pc/src/editor_clipboard.cpp`
- Modify: `pc/src/editor_scene.cpp` (RPC chain), `pc/src/dev_overlay.cpp` (`viewport_keys`), `pc/tools/build_boot.py`
- Test: `pc/tests/editor_selection_smoke.py`

- [ ] **Step 1: Write the failing section**

```python
def check_clipboard(s):
    """Ctrl+C / Ctrl+V / Ctrl+D through the lighting item format: new ids, selected, one undo step each."""
    g = s.game

    def counts():
        return len(g.call("lights")["lights"]), len(g.call("actors")["actors"]), len(g.call("environment")["volumes"])

    def position(method, ident):
        return next(x for x in g.call(method)[method] if x["id"] == ident)["position"]

    start = counts()
    s.select(action="set", ids=[s.light, s.actor])
    copied = s.select(action="copy")
    text = copied["clipboard"]
    s.check(copied["count"] == 2 and text.startswith("rood-lighting 2") and f"light {s.light[6:]} " in text
            and f"actor {s.actor[6:]} " in text, "copy: the light and the candle as lighting entries")
    s.keys(["c"], ctrl=True)
    s.keys(["v"], ctrl=True)
    sel = s.select()["selection"]
    new_light = next((i for i in sel if i.startswith("light:")), None)
    new_actor = next((i for i in sel if i.startswith("actor:")), None)
    s.check(counts() == (start[0] + 1, start[1] + 1, start[2]) and new_light and new_actor
            and s.light not in sel and s.actor not in sel, f"Ctrl+V: a new light and candle, selected ({sel})")
    s.check(position("lights", new_light) == position("lights", s.light), "pasted where they were copied")
    s.check(s.history()["undo"] == "Paste 2 objects", f"one undo step ({s.history()['undo']})")
    s.keys(["d"], ctrl=True)
    sel = s.select()["selection"]
    dup = next(i for i in sel if i.startswith("light:"))
    a, b = position("lights", dup), position("lights", new_light)
    s.check(counts() == (start[0] + 2, start[1] + 2, start[2]) and abs(a[0] - b[0] - 64) < 0.01 and a[1:] == b[1:],
            "Ctrl+D: copies 64 units along x, selected")
    s.check(s.history()["undo"] == "Duplicate 2 objects", f"one undo step ({s.history()['undo']})")
    g.call("editor_undo", action="undo", steps=2)
    s.check(counts() == start, "undo takes both away")
    s.select(action="set", ids=[s.volume])
    r = s.select(action="duplicate")
    volumes = g.call("environment")["volumes"]
    s.check(r["count"] == 1 and len(volumes) == start[2] + 1 and r["primary"].startswith("fog:") and r["primary"] != s.volume,
            "a fog volume duplicates (through its item: preset and fields)")
    g.call("editor_undo", action="undo")
    r = s.select(action="paste", text=text)
    s.check(r["count"] == 2 and counts() == (start[0] + 1, start[1] + 1, start[2]), "pasting text from the RPC")
    g.call("editor_undo", action="undo")
```

Append `check_clipboard`; docstring: `- clipboard: Ctrl+C / Ctrl+V paste the light and candle in place with new ids, Ctrl+D 64 units along x; a fog volume duplicates; each is one undo step.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)`; `RpcError` `unknown action copy`.

- [ ] **Step 3: The clipboard**

Create `pc/src/editor_clipboard.cpp`:

```cpp
// Copy, paste and duplicate in the editor (editor_scene.h): the selection
// as lighting entries (lighting_layers.h, the room files' own format: text
// that can be kept, sent or pasted into a room file), and entries back as
// new objects with new ids, selected, one undo step.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_scene.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lighting_layers.h"
#include "lights_internal.h"

using rood_selection::Ref;
using namespace rood_lights;

namespace {
std::string held; /* the editor's own clipboard (the system's may hold anything) */
int copied_count;
} // namespace

std::string rood_scene::copy_text()
{
    std::string text = "rood-lighting " + std::to_string(lighting::FORMAT_VERSION) + "\n# copied in the editor\n";
    copied_count = 0;
    for (const Ref& r : rood_selection::items()) {
        int i = index_of(r);
        if (i < 0 || r.kind == rood_selection::KIND_GLOW)
            continue;
        lighting::Entry e;
        e.item = r.kind == rood_selection::KIND_LIGHT ? lighting::item_from_light(lights[i])
            : r.kind == rood_selection::KIND_ACTOR    ? lighting::item_from_actor(actors[i])
                                                       : rood_atmo::volume_item(i);
        e.id = e.item.id();
        text += lighting::format_entry(e) + "\n";
        ++copied_count;
    }
    return copied_count ? text : std::string();
}
int rood_scene::copy_selection()
{
    std::string text = copy_text();
    if (text.empty()) {
        snprintf(status, sizeof status, "Copy: select lights, light actors or fog volumes (glows come from the game)");
        return 0;
    }
    held = text;
    if (ImGui::GetCurrentContext())
        ImGui::SetClipboardText(text.c_str());
    snprintf(status, sizeof status, "Copied %d object%s (Ctrl+V pastes them where they were)", copied_count,
        copied_count == 1 ? "" : "s");
    return copied_count;
}
const std::string& rood_scene::clipboard() { return held; }
int rood_scene::paste_text(const std::string& text, const float offset[3], const char* verb)
{
    lighting::File file;
    if (!lighting::parse_text(text, file) || file.version == 0) {
        snprintf(status, sizeof status, "%s: no lighting items in it", verb);
        return 0;
    }
    track_room();
    std::vector<Ref> added;
    for (const lighting::Entry& e : file.entries) {
        if (e.verb != lighting::DEFINE)
            continue;
        if (e.item.kind == "light") {
            Light l;
            if (!lighting::light_from_item(e.item, l))
                continue;
            l.id.clear(); /* a new one (ensure_ids) */
            for (int k = 0; k < 3; k++)
                l.position[k] += offset[k];
            lights.push_back(l);
            added.push_back(light_ref((int)lights.size() - 1));
        } else if (e.item.kind == "actor") {
            prefab::Actor a;
            if (!lighting::actor_from_item(e.item, a))
                continue;
            a.id.clear();
            a.glow_linked = false; /* one actor drives a glow */
            a.glow_key.clear();
            a.seed = prefab::hash32(a.seed + (uint32_t)actors.size() * 977u) % 1000000u + 1;
            for (int k = 0; k < 3; k++)
                a.position[k] += offset[k];
            actors.push_back(a);
            added.push_back(actor_ref((int)actors.size() - 1));
        } else if (e.item.kind == "fog") {
            int i = rood_atmo::add_volume_item(e.item, offset);
            if (i >= 0)
                added.push_back(volume_ref(i));
        }
    }
    if (added.empty()) {
        snprintf(status, sizeof status, "%s: no lights, light actors or fog volumes in it", verb);
        return 0;
    }
    rood_selection::set_all(added, rood_selection::FROM_CODE);
    publish();
    dirty = true;
    int n = (int)added.size();
    std::string name = std::string(verb) + " " + (n == 1 ? label(added[0]) : std::to_string(n) + " objects");
    pc_undo_hint(name.c_str());
    snprintf(status, sizeof status, "%s (unsaved; Ctrl+Z undoes)", name.c_str());
    return n;
}
int rood_scene::paste_clipboard()
{
    const char* system = ImGui::GetCurrentContext() ? ImGui::GetClipboardText() : nullptr;
    std::string text = system && !strncmp(system, "rood-lighting ", 14) ? std::string(system) : held;
    const float here[3] = { 0, 0, 0 };
    return paste_text(text, here, "Paste");
}
int rood_scene::duplicate_selection()
{
    std::string text = copy_text();
    const float along_x[3] = { 64, 0, 0 };
    return text.empty() ? 0 : paste_text(text, along_x, "Duplicate");
}
```

- [ ] **Step 4: RPC and keys**

RPC chain in `editor_scene.cpp`, before the final `} else {`:

```cpp
    } else if (action == "copy") {
        int n = rood_scene::copy_selection();
        extra += ",\"count\":" + std::to_string(n) + ",\"clipboard\":" + quote(rood_scene::clipboard());
    } else if (action == "paste") {
        const float here[3] = { 0, 0, 0 };
        const Json* t = params.get("text");
        int n = t ? rood_scene::paste_text(t->string, here, "Paste") : rood_scene::paste_clipboard();
        extra += ",\"count\":" + std::to_string(n);
    } else if (action == "duplicate") {
        extra += ",\"count\":" + std::to_string(rood_scene::duplicate_selection());
```

(doc: `copy (replies clipboard), paste {text}, duplicate`). In `viewport_keys` (dev_overlay.cpp) after the hiding block:

```cpp
    /* Copy, paste (where it was copied), duplicate (64 units along x). */
    if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            rood_scene::copy_selection();
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            rood_scene::paste_clipboard();
        if (ImGui::IsKeyPressed(ImGuiKey_D, false))
            rood_scene::duplicate_selection();
    }
```

In `build_boot.py` add `root / "pc/src/editor_clipboard.cpp",` after `editor_scene.cpp`.

- [ ] **Step 5: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection editor_undo
```

Expected: both `PASS`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/editor_clipboard.cpp pc/src/editor_scene.cpp pc/src/dev_overlay.cpp pc/tools/build_boot.py \
  pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Editor copy / paste / duplicate (Ctrl+C/V/D) through the lighting item format: new ids, selected, one undo step

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 17: Delete the whole selection, the Edit menu, help, F frames any kind

**Files:**
- Modify: `pc/src/editor_scene.cpp`, `pc/src/light_actors.cpp` (`pc_lights_delete_selection`, `pc_lights_has_selection` lines 616-659), `pc/src/dev_overlay.cpp` (`edit_menu` lines 884-886; `help_window` table lines 1313-1351; Delete key)
- Test: `pc/tests/editor_selection_smoke.py`

- [ ] **Step 1: Write the failing sections**

```python
def check_delete(s):
    """Delete takes the whole selection (lights, actors, fog volumes) as one undo step; undo brings it back selected."""
    g = s.game
    s.select(action="set", ids=[s.light, s.actor, s.volume])
    s.keys(["delete"])
    ids = [x["id"] for x in s.select()["objects"]]
    s.check(s.light not in ids and s.actor not in ids and s.volume not in ids, "Delete: the light, the candle and the fog volume go")
    s.check(s.history()["undo"] == "Delete 3 objects", f"one undo step ({s.history()['undo']})")
    g.call("editor_undo", action="undo")
    r = s.select()
    ids = [x["id"] for x in r["objects"]]
    s.check({s.light, s.actor, s.volume} <= set(ids) and r["selection"] == [s.light, s.actor, s.volume],
            f"undo: all three back, and selected ({r['selection']})")


def check_frame(s):
    """F frames the selection's primary, a fog volume too (the free camera)."""
    g, p = s.game, s.player
    s.select(action="set", ids=[s.volume])
    centre_of = next(v for v in g.call("environment")["volumes"] if v["id"] == VOLUME)["pos"]
    g.call("editor_camera", x=p["x"] + 1500, y=p["y"] - 800, z=p["z"] + 1500, yaw=0, pitch=0)
    g.run(8)
    s.keys(["f"])
    cam = g.call("editor_camera")
    d = math.dist([cam["x"], cam["y"], cam["z"]], centre_of)
    s.check(abs(d - 300) < 5, f"F frames the fog volume ({d:.1f} units from its centre, 300 expected)")
    g.call("editor_camera", off=1)
    g.run(4)
```

Append `check_delete` and `check_frame` (last) to `SECTIONS`; docstring: `- delete: Delete takes lights, actors and fog volumes in one undo step; undo restores them selected; - frame: F frames a fog volume.`

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `FAIL (1)`; `FAIL: Delete: the light, the candle and the fog volume go` (the old Delete takes the actor only).

- [ ] **Step 3: delete_selection (editor_scene.cpp)**

Add `#include "editor_undo.h"` to the includes; before `/* ---- RPC editor_select ---- */`:

```cpp
/* ---- operations ---- */

int rood_scene::delete_selection()
{
    track_room();
    std::vector<int> gone_lights, gone_actors, gone_volumes;
    int glows = 0;
    std::string single;
    for (const Ref& r : rood_selection::items()) {
        if (r.kind == rood_selection::KIND_GLOW) {
            ++glows;
            continue;
        }
        int i = index_of(r);
        if (i < 0)
            continue;
        (r.kind == rood_selection::KIND_LIGHT ? gone_lights : r.kind == rood_selection::KIND_ACTOR ? gone_actors : gone_volumes)
            .push_back(i);
        single = label(r);
    }
    int count = (int)(gone_lights.size() + gone_actors.size() + gone_volumes.size());
    if (!count) {
        if (glows)
            snprintf(status, sizeof status, "Glows come from the game: untick one in the Outliner to mute it");
        return 0;
    }
    pc_undo_hint((count == 1 ? "Delete " + single : "Delete " + std::to_string(count) + " objects").c_str());
    auto descending = [](std::vector<int>& v) {
        std::sort(v.rbegin(), v.rend());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    descending(gone_lights);
    descending(gone_actors);
    descending(gone_volumes);
    for (int i : gone_lights) {
        lights.erase(lights.begin() + i);
        if (solo == i)
            solo = -1;
        else if (solo > i)
            --solo;
    }
    for (int i : gone_actors) {
        actors.erase(actors.begin() + i);
        if (solo_actor == i)
            solo_actor = -1;
        else if (solo_actor > i)
            --solo_actor;
    }
    for (int i : gone_volumes)
        rood_atmo::remove_volume(i);
    rood_selection::clear(rood_selection::FROM_CODE);
    publish();
    dirty = true;
    snprintf(status, sizeof status, "Deleted %d object%s (unsaved; Ctrl+Z undoes)", count, count == 1 ? "" : "s");
    return count;
}
```

RPC chain: `} else if (action == "delete") { extra += ",\"count\":" + std::to_string(rood_scene::delete_selection());` (doc: `delete`).

- [ ] **Step 4: The old entry points forward (light_actors.cpp)**

Replace the bodies of `pc_lights_delete_selection` and `pc_lights_has_selection` (lines 616-659) with:

```cpp
/* Delete (the key, Edit > Delete): the whole selection (editor_scene.cpp);
 * glows come from the game and are only muted. Returns how many went. */
extern "C" int pc_lights_delete_selection(void) { return rood_scene::delete_selection(); }
extern "C" int pc_lights_has_selection(void) { return !rood_selection::empty(); }
```

- [ ] **Step 5: Edit menu and help (dev_overlay.cpp)**

In `edit_menu` replace the Delete item (lines 885-886) with:

```cpp
    bool any = !rood_selection::empty();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, any))
        rood_scene::copy_selection();
    if (ImGui::MenuItem("Paste", "Ctrl+V"))
        rood_scene::paste_clipboard();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, any))
        rood_scene::duplicate_selection();
    if (ImGui::MenuItem("Delete", "Delete", false, any))
        rood_scene::delete_selection();
    ImGui::Separator();
    if (ImGui::MenuItem("Hide selection", "H", false, any))
        rood_scene::hide_selection();
    if (ImGui::MenuItem("Hide unselected", "Shift+H", false, any))
        rood_scene::isolate_selection();
    if (ImGui::MenuItem("Show all", "Ctrl+H", false, rood_selection::any_hidden()))
        rood_scene::unhide_all();
```

In `viewport_keys` change `pc_lights_delete_selection();` to `rood_scene::delete_selection();`. In `help_window`'s `shortcuts` table: replace the Delete row with `{ "Editor", "Delete / Backspace", "Delete the selection (lights, light actors, fog volumes)" },`; after it add

```cpp
        { "Editor", "Ctrl+C / Ctrl+V", "Copy the selection / paste it where it was copied (lighting items as text)" },
        { "Editor", "Ctrl+D", "Duplicate the selection (64 units along x; the copies are selected)" },
        { "Editor", "H / Shift+H / Ctrl+H", "Hide the selection / hide everything else / show everything (editor only)" },
        { "Details", "Middle-drag (number)", "Value ladder: up/down picks steps of 10, 1, 0.1, 0.01; left/right adds them; Esc cancels" },
        { "Details", "Reset arrow", "Back to the default, or the preset's or prefab's value" },
        { "Details", "Lock", "Keep Details on its object while you select others" },
```

replace `{ "Camera", "F", "Frame the selected light or glow" },` with `{ "Camera", "F", "Frame the selection (a light, actor, glow or fog volume)" },`; replace the `Lights / Click` row with

```cpp
        { "Select", "Click", "Select a light, light actor (its icon or any of its lights), glow or fog volume (inside its outline)" },
        { "Select", "Shift+click / Ctrl+click", "Add to the selection / add or take out (the view and the Outliner)" },
        { "Select", "Drag on empty space", "Box select lights, light actors and fog volumes (Shift adds, Ctrl toggles)" },
```

change the `Lights / Shift+click` and `Lights / Ctrl+click` rows' keys to `"Shift+click (empty)"` and `"Ctrl+click (empty)"`, and delete the `{ "Lights", "Ctrl+click (Outliner)", … }` row.

- [ ] **Step 6: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection editor_undo editor_cutaway light_actors
```

Expected: all `PASS` (`editor_undo_smoke`'s Delete still names its step `Delete …`).

- [ ] **Step 7: Commit**

```bash
git add pc/src/editor_scene.cpp pc/src/light_actors.cpp pc/src/dev_overlay.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Delete takes the whole selection (fog volumes too); Edit menu copy/paste/duplicate/hide; F frames any kind; help

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 18: Docs and full verification

**Files:**
- Modify: `lighting/README.md` (line 163)

- [ ] **Step 1: README**

Replace the sentence `placed in the editor (Details > Atmosphere > Fog volumes: Add volume, then the Move / Size / Turn gizmo) like lights.` with:

```
placed in the editor (Details > Atmosphere: Add volume) and then edited like
lights: click one in the view or the Outliner (Fog volumes) and its fields
are in Details (Basics, Transform, Look, Interaction; the reset arrow goes
back to its preset's value), with the Move / Size / Turn gizmo.
```

- [ ] **Step 2: Every build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows
```

Expected: each exits 0 with a `Built …` line; the Windows build reports no failed units (a failure names the unit; the usual cause is a Windows macro used as an identifier).

- [ ] **Step 3: 32-bit suites**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest editor light_actors atmosphere fog glow fixture lighting room_browser system_settings
```

Expected: `N/N passed`, or only the failures recorded in Task 1.

- [ ] **Step 4: 64-bit Vulkan suites**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan editor light_actors atmosphere fog glow fixture lighting
```

Expected: `N/N passed` (baseline failures excepted).

- [ ] **Step 5: Leak scan of the branch's committed text**

```bash
pc/tools/publish.sh --dry-run "leak check"
```

Expected: `clean`.

- [ ] **Step 6: Commit**

```bash
git add lighting/README.md
git commit -m "$(cat <<'EOF'
lighting/README: fog volumes are selected in the view or the Outliner and edited in Details

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-review

### Spec coverage (phase A)

| Requirement | Task |
|---|---|
| Selection model `pc/src/editor_selection.*` (typed refs, shared set) | 2, 5 |
| Shared by viewport, Outliner, Details, RPC (and undo) | 5 (publish, undo, reloads), 6, 8, 9, 12 |
| Schema `pc/src/editor_schema.*`: point / spot / rect lights | 10 |
| Schema: prefab instances (exposed params) | 10 |
| Schema: fog volumes (preset value for reset) | 11 |
| Field: name, label, category, type (float/int/bool/colour/enum/vec3/angle), range, unit, log, step, default, preset value, accessors | 10 (`Field`), 11 (`base` from presets) |
| Details from the schema: collapsible categories | 12 |
| Details: search | 12 (box), 13 (RPC + test) |
| Details: lock | 12 (padlock), 13 (RPC + test) |
| Details: per-value reset to default / preset | 10 (RPC reset), 11 (preset), 12 (arrow, mouse test) |
| Details: tooltips with unit and range | 12 (`tooltip`) |
| Undo names from the field label | 10 (`apply`: "Edit <label>" / "Reset <label>"), 14 (ladder "Edit Intensity") |
| Outliner sync: scroll to and highlight the selection, open its section | 8 |
| Details switches to the selection (not from Details/undo; not while locked) | 9, 13 |
| Room-wide settings (Atmosphere, Post-processing, Graphics) on their own tabs | 9 (tabs), 12 (volume fields leave the Atmosphere tab) |
| Fog volume picking in the viewport (ray vs box / ellipsoid, behind-surface check) | 3, 6 |
| Box select, Shift-add, Ctrl-toggle | 2, 6, 7 |
| Hide / unhide (H, Ctrl+H; Shift+H isolate) as editor-only state in user settings | 2, 15 |
| Copy / paste / duplicate (Ctrl+C/V/D) through the layer item format | 4 (`volume_item`), 16 |
| Value ladder (middle-drag, steps 0.01/0.1/1/10, Esc, one undo step) | 12, 14 |
| RPC to list/get/set schema fields and drive selection | 5, 6, 7, 10, 12, 13, 15, 16, 17 |
| "Frame selection" (F) for every kind | 5 (`position`), 17 (test) |
| Tests: selection sync, fog picking, hide, clipboard, lint, get/set, reset, ladder, existing suites green | 2, 3, 5-17, 18 |
| No src/ changes; ImGui + ImGuizmo; editor state in settings; `pc_undo_hint`; no names/paths/assets | Ground rules; 18 (scan) |
| Not in phase A: scale, handles, colour popover, library | not planned |

### Placeholder scan

No step says "TBD", "add error handling", "similar to Task N" or "write tests for the above"; every code step shows the code. Two steps modify existing code by description plus anchors where the change is a mechanical rename (`return true;` → `return;` in `actor_details_extra`; `drawn` → `drawn_list` in `pc_fogvol_gpu_get`); both name the exact lines. Task 14 has no new C++ by design (the ladder is written in Task 12); it says what to do if its test fails.

### Type and name consistency

- `rood_selection::{Kind KIND_*, Ref, Source FROM_*, ref_of, kind_name, source_name, items, primary, contains, empty, set, add, toggle, remove, clear, set_all, click, box, retain, serial, last_source, lock, locked, set_room, hidden, any_hidden, hide, unhide_all, hidden_items, hidden_line, write_hidden, parse_hidden, clear_hidden}`: declared in Task 2, used with these signatures in Tasks 5-17.
- `rood_pick::{ray, volume_hit, in_rect}`: Task 3; used in Task 4 (`rood_atmo::volume_hit`) and Task 6 (`volume_at`, `in_box`).
- `rood_scene::*`: the full header is written in Task 5; each function is defined in exactly one task (refs/index/label/position/publish/refresh/frame/row_clicked: 5; icons/object_at/volume_at/in_box/click_at: 6; row_drawn/scroll_due: 8; hidden predicates/hide/isolate/unhide_all: 15; copy/paste/duplicate/clipboard: 16; delete_selection: 17) and called only from that task on.
- `rood_atmo::{volume_count, volume_id, volume_index, volume_label, set_selected, volume_centre, volume_hit, volumes_shown, volume_item, add_volume_item, remove_volume}` (Task 4), `outliner` (8), `schema_kind` (11), `details_extra` (12): declared in `environment_internal.h` before use.
- `rood_schema::{Type TYPE_*, Field, Kind, *_field factories, kind_of, find_kind, all_kinds, lint, base_of, base_source_of, is_overridden, reset_field, clamp, same, format, from_text(Type, options, text), to_json, from_json, targets, apply, apply_reset}`: Task 10; `from_text` is called with `(type, options, text)` in Task 11; `targets` by the ladder in Task 12.
- `rood_details::{draw, ladder_frame, ladder_cancel, state_json, set_search}`: Task 12; RPC `ui` uses `state_json`/`set_search`; `dev_overlay.cpp` uses `draw`, `ladder_frame`, `ladder_cancel`.
- `rood_lights::{light_details_extra, actor_details_extra}` replace `pc_lights_details`/`actor_details` in Task 12; the declaration of `pc_lights_details` in `dev_overlay.cpp` is removed in the same task, and the temporary Details tab body of Task 9 is replaced there.
- RPC field names used by the tests (`selection`, `primary`, `source`, `locked`, `outliner.target/visible`, `hidden`, `hidden_setting`, `view`, `objects[].screen/hidden/selected`, `under`, `count`, `clipboard`; schema `kinds`, `fields{value, base, source, overridden}`, `problems`, `ui{subject, kind, locked, search, fields, rects, reset, ladder{active, field, step, row_height, notch, value}}`) match the C++ that produces them.
