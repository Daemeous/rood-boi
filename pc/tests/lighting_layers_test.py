#!/usr/bin/env python3
"""Lighting packs without the game: the C++ reader (pc/build/lighting_probe,
pc/src/lighting_layers.cpp) and pc/tools/lighting_format.py agree on the
schema, number formatting and merges; layers merge by id with dangling
overrides reported; the user layer is the diff from the packs; old pc/lights
files migrate on save; Promote moves user entries into a pack's authored
file keeping its comments; lighting_lint.py catches what it should and
passes the repository's lighting/.

    python3 pc/tests/lighting_layers_test.py [--probe pc/build/lighting_probe]
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import lighting_format as lf  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


GENERATED = """rood-lighting 2
# generated: do not edit
actor gen.candle.aaaa prefab=candle pos=100,-40,200 seed=3
light gen.window.bbbb type=rect pos=0,-300,0 dir=0,0,1 color=0.6,0.7,1 intensity=2
light gen.window.cccc type=rect pos=50,-300,0 dir=0,0,1
"""
AUTHORED = """rood-lighting 2
# hand-authored: keep this comment
delete actor:gen.candle.aaaa
light 68ea4c1200a3f1 pos=10,-20,30 color=1,0.5,0.25 name="Hearth glow"
override light:gen.window.bbbb intensity=3.5 !color
override light:gen.window.gone intensity=1
set haze_density value=0.0006
"""
USER = """rood-lighting 2
override light:68ea4c1200a3f1 intensity=2
override light:gen.window.cccc enabled=0
light 68ea4c1300b2c4 type=spot pos=1,2,3 shadows=1
uv 3 speed=0.5
glow room:0000123@448,-40,576 intensity=0.5 pos=448,-40,576
"""
LEGACY = """# rood-boi lights for zone 9 room 17 (world space; see pc/src/lights.cpp)
light point 448.0 -160.0 576.0 1.000 0.750 0.450 1.000 600.0 0.000 1.000 0.000 45.0 1 1 128.0 128.0
light spot 0.0 -300.0 0.0 0.600 0.700 1.000 2.000 900.0 0.000 1.000 0.000 30.0 0 1 128.0 128.0
actor candle 100.0 -40.0 200.0 90.00 0.00 0.00 seed=7 intensity=1.5 tint=1,0.5,0.25
set baked_lighting 0.8
glow 448.0 -40.0 576.0 1 0.500 1.000 1.000 1.000 0.000 0.0 key=room:0000123@448,-40,576
uv_effect 3 0.5 0
"""


def probe(args, *words):
    r = subprocess.run([args.probe, *map(str, words)], capture_output=True, text=True)
    return r.returncode, r.stdout


def sections(text):
    out, name = {}, None
    for line in text.splitlines():
        if line.startswith("[") and line.endswith("]"):
            name = line[1:-1]
            out[name] = []
        elif name:
            out[name].append(line)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", default=str(ROOT / "pc/build/lighting_probe"))
    args = ap.parse_args()
    if not pathlib.Path(args.probe).exists():
        print(f"SKIP: {args.probe} not built (cmake --build pc/build)")
        return 77
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="lighting-test-"))
    try:
        run(args, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


def run(args, tmp):
    code, out = probe(args, "selftest")
    check(code == 0, "the C++ reader's self-test passes")
    # The same schema on both sides.
    code, out = probe(args, "schema")
    cxx = json.loads(out)
    py = {k: {"open": v[0], "other_decimals": v[1], "fields": [list(f) for f in v[2]]} for k, v in lf.SCHEMA.items()}
    check(cxx == py, "the C++ and Python schemas match")
    for kind, key, value in [("light", "pos", "1.25,-0.04,3.000"), ("light", "intensity", "0.12349"),
                             ("set", "value", "0.00055"), ("actor", "flicker", "0.123456"), ("actor", "rot", "90.005,0,0"),
                             ("light", "type", "spot"), ("glow", "size", "2.345")]:
        _, c = probe(args, "canon", kind, key, value)
        check(c.strip() == lf.canonical_value(kind, key, value), f"canonical {kind}.{key}={value}: {c.strip()}")
    check(lf.generated_id("actor", "candle", "room:0000123@1,2,3") == lf.generated_id("actor", "candle", "room:0000123@1,2,3")
          and lf.generated_id("actor", "candle", "x").startswith("actor:gen.candle."), "generated ids are deterministic")

    # Three layers, merged.
    root, user = tmp / "lighting", tmp / "user"
    write(root / "default/generated/zone09_room17.txt", GENERATED)
    write(root / "default/rooms/zone09_room17.txt", AUTHORED)
    write(user / "zone09_room17.txt", USER)
    code, out = probe(args, "merge", root, "default", user, 9, 17)
    s = sections(out)
    r = lf.load_room(9, 17, str(root), ["default"], str(user))
    py_effective = [repr(it) for _, it in sorted(r.effective.items())]
    check(s["effective"] == py_effective, "C++ and Python merge the layers alike")
    eff = r.effective
    check("actor:gen.candle.aaaa" not in eff, "delete removes a generated actor")
    check(eff["light:gen.window.bbbb"].get("intensity") == "3.5" and eff["light:gen.window.bbbb"].get("color") is None,
          "an override sets and unsets fields of a generated light")
    check(eff["light:gen.window.cccc"].get("enabled") == "0", "the user layer disables a generated light")
    check(eff["light:68ea4c1200a3f1"].get("intensity") == "2" and eff["light:68ea4c1200a3f1"].get("name") == "Hearth glow",
          "the user layer overrides an authored light")
    dangling = [line for line in s["problems"] if "light:gen.window.gone" in line]
    check(len(dangling) == 1 and "default/rooms:6" in dangling[0], f"a dangling override is reported with its place: {dangling}")
    check(any(p[2] == "light:gen.window.gone" for p in r.problems), "Python reports the same dangling override")
    badges = dict(line.split(" ", 1) for line in s["badges"])
    check(badges.get("light:gen.window.bbbb") == "G>P" and badges.get("light:gen.window.cccc") == "G>U"
          and badges.get("light:68ea4c1200a3f1") == "P>U" and badges.get("light:68ea4c1300b2c4") == "U",
          f"badges name the layers ({badges})")
    user_entries = [line for line in s["user"] if line and not line.startswith(("#", "rood-lighting"))]
    check(sorted(user_entries) == sorted(lf.format_entry(e) for e in lf.diff(r.base, r.effective)),
          "the user layer as saved is the diff from the packs (C++ = Python)")
    check(sorted(user_entries) == sorted(lf.format_entry(e) for e in lf.parse_text(USER).entries),
          "and equals the user file read (nothing gained, nothing lost)")

    # Old pc/lights files: read as the user layer, migrated on save.
    legacy = tmp / "legacy"
    write(legacy / "zone09_room17.txt", LEGACY)
    code, out = probe(args, "save", "-", "", legacy, 9, 17)
    text = (legacy / "zone09_room17.txt").read_text()
    f = lf.parse_text(text)
    kinds = sorted(e.id.split(":")[0] for e in f.entries)
    check(code == 0 and f.version == 2 and kinds == ["actor", "glow", "light", "light", "set", "uv"],
          f"an old file migrates to version 2 on save ({kinds})")
    actor = next(e.item for e in f.entries if e.id.startswith("actor:"))
    check(actor.get("intensity") == "1.5" and actor.get("tint") == "1,0.5,0.25" and actor.get("rot") == "90,0,0"
          and actor.get("seed") == "7", f"the actor keeps its parameters ({actor})")
    ids = [e.id for e in f.entries]
    check(ids == sorted(ids), "entries are sorted by id")
    code, again = probe(args, "save", "-", "", legacy, 9, 17)
    check((legacy / "zone09_room17.txt").read_text() == text, "saving again changes nothing (deterministic)")

    # Promote: every user entry into the authored layer; its comments stay.
    before = lf.load_room(9, 17, str(root), ["default"], str(user)).effective
    code, out = probe(args, "promote", root, "default", user, 9, 17)
    check(code == 0 and "Promoted" in out, f"promote: {out.strip()}")
    authored = (root / "default/rooms/zone09_room17.txt").read_text()
    check("# hand-authored: keep this comment" in authored, "the authored file keeps its comments")
    check("override light:gen.window.gone intensity=1" in authored, "and its dangling override (reported, not dropped)")
    after = lf.load_room(9, 17, str(root), ["default"], str(user))
    check({k: repr(v) for k, v in after.effective.items()} == {k: repr(v) for k, v in before.items()},
          "the room looks the same after promoting")
    check(not lf.diff(after.base, after.effective) and not (user / "zone09_room17.txt").exists(),
          "the user layer is empty afterwards (its file removed)")
    check('light 68ea4c1200a3f1 name="Hearth glow" pos=10,-20,30 color=1,0.5,0.25 intensity=2' in authored,
          f"an authored light takes the user's change into its definition")
    # Promote only one id.
    write(user / "zone09_room17.txt", "rood-lighting 2\nset baked_lighting value=0.7\nuv 4 freeze=1\n")
    code, out = probe(args, "promote", root, "default", user, 9, 17, "uv:4")
    left = lf.read_file(str(user / "zone09_room17.txt"))
    check([e.id for e in left.entries] == ["set:baked_lighting"] and "uv 4 freeze=1" in
          (root / "default/rooms/zone09_room17.txt").read_text(), "promoting a selection moves only it")

    # The lint.
    lint = [sys.executable, str(ROOT / "pc/tools/lighting_lint.py")]
    clean = subprocess.run(lint, capture_output=True, text=True, cwd=ROOT)
    check(clean.returncode == 0, f"the repository's lighting/ passes the lint: {clean.stdout.strip().splitlines()[-1:]}")
    bad = tmp / "bad"
    write(bad / "default/rooms/zone09_room17.txt", """rood-lighting 2
light 68ea4c1200a3f1 pos=1,2 colour=1,1,1
light 68ea4c1200a3f1 pos=1,2,3
actor 68ea4c1200a3f2 prefab=candle pos=1,2,3 flame_hieght=4
override light:nothing intensity=2
set no_such_setting value=1
light 68ea4c1200a3f3 type=sphere pos=1,2,3
light  68ea4c1200a3f4   pos=1.00,2,3
""")
    write(bad / "default/rooms/zone09_room99.txt", "rood-lighting 2\n")
    write(bad / "default/rooms/lights.txt", "rood-lighting 2\n")
    write(bad / "default/generated/zone09_room17.txt", "rood-lighting 2\noverride light:x intensity=1\nbogus line here\n")
    result = subprocess.run(lint + [str(bad)], capture_output=True, text=True, cwd=ROOT)
    report = result.stdout
    expect = ["pos takes 3 numbers", "unknown key 'colour'", "also on line 2", "'flame_hieght' is not a parameter",
              "dangling: override of light:nothing", "no such room setting", "type must be one of",
              "not canonical", "room files are named", "override in a generated layer", "expected key=value"]
    if (ROOT / "data/SLUS_010.40").exists():
        expect.append("zone 9 room 99 is not a room of the game")
    for e in expect:
        check(e in report, f"lint reports: {e}")
    check(result.returncode == 1, "the lint fails on errors")
    # The README's example parses as written.
    readme = (ROOT / "lighting/README.md").read_text()
    example = readme.split("```\n", 2)[1].split("```")[0]
    f = lf.parse_text(example)
    check(f.version == 2 and not f.errors and len(f.entries) == 7, f"the README example parses ({f.errors})")
    lines = [line for line in example.splitlines() if line and not line.startswith(("#", "rood-lighting"))]
    ids = [e.id for e in f.entries]
    check(lines == [lf.format_entry(e) for e in f.entries] and ids == sorted(ids), "and is canonical, sorted by id")
    fixed = subprocess.run(lint + [str(bad), "--fix"], capture_output=True, text=True, cwd=ROOT)
    check("light 68ea4c1200a3f4 pos=1,2,3" in (bad / "default/rooms/zone09_room17.txt").read_text(),
          "--fix rewrites entry lines in canonical form")


if __name__ == "__main__":
    sys.exit(main())
