#!/usr/bin/env python3
"""Check lighting packs (lighting/README.md) before they are committed.

    python3 pc/tools/lighting_lint.py                  every pack under lighting/
    python3 pc/tools/lighting_lint.py --packs default,extra   stack these, in this order
    python3 pc/tools/lighting_lint.py --user pc/user/lighting also the user layer on top
    python3 pc/tools/lighting_lint.py --fix            rewrite entry lines in canonical form

Checks every room file of every layer: syntax and the format header; known
kinds and keys (prefab parameters against pc/assets/prefabs, room settings
against pc/src/room_settings.cpp, fixture types against the fixture catalog
pc/assets/fixtures/catalog.json); required fields and value shapes;
duplicate ids in a file; overrides and deletes that name an id no layer
below defines (dangling: an item was removed or renamed under them);
overrides in generated layers; canonical formatting and id order; file
names and rooms that do not exist in the game. The game's rooms are read
from the user's own data (data/SLUS_010.40's location table) when it is
there; nothing about the game is stored here. Exit status 1 on errors.
"""
import argparse
import json
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import lighting_format as lf  # noqa: E402

NAME = re.compile(r"^zone(\d\d)_room(\d\d)\.txt$")


def game_rooms(data):
    """{(zone, room)} from the executable's location table, or None."""
    exe = pathlib.Path(data) / "SLUS_010.40"
    try:
        blob = exe.read_bytes()
    except OSError:
        return None
    at = 0x8004F0CC - 0x80010000 + 0x800
    table = blob[at:at + 1024]
    if len(table) != 1024:
        return None
    return {(table[2 * i], table[2 * i + 1]) for i in range(1, 512) if 0 < table[2 * i] < 100}


def setting_keys():
    text = (ROOT / "pc/src/room_settings.cpp").read_text()
    return set(re.findall(r'\{\s*"([a-z0-9_]+)",\s*"', text))


def prefab_params(dirs):
    params = {}
    for d in dirs:
        for path in sorted(pathlib.Path(d).glob("*.json")) if pathlib.Path(d).is_dir() else []:
            try:
                data = json.loads(path.read_text())
            except (OSError, ValueError):
                continue
            keys = set()
            for p in data.get("params", []):
                if isinstance(p, dict) and "key" in p:
                    keys.add(p["key"])
            params[path.stem] = keys
    return params


class Report:
    def __init__(self):
        self.errors = self.warnings = 0

    def __call__(self, level, path, line, message):
        where = f"{os.path.relpath(path, ROOT) if os.path.isabs(str(path)) else path}"
        where += f":{line}" if line else ""
        print(f"{level}: {where}: {message}")
        if level == "error":
            self.errors += 1
        else:
            self.warnings += 1


def fixture_types():
    """The catalog's type ids (pc/assets/fixtures/catalog.json), or None."""
    try:
        data = json.loads((ROOT / "pc/assets/fixtures/catalog.json").read_text())
    except (OSError, ValueError):
        return None
    return {t.get("id") for t in data.get("types", [])}


FIXTURE_TYPES = None


_FOG_PRESETS = None


def fog_presets():
    """The fog volume presets' names (pc/assets/environment/fog_presets.json)."""
    global _FOG_PRESETS
    if _FOG_PRESETS is None:
        try:
            _FOG_PRESETS = set(json.loads((ROOT / "pc/assets/environment/fog_presets.json").read_text())["presets"])
        except (OSError, ValueError, KeyError):
            _FOG_PRESETS = set()
    return _FOG_PRESETS


TONEMAP_KEYS = ("tonemapper", "tonemap_white")


def post_preset_tonemap(name):
    """The tonemapper keys a post preset (pc/assets/post/<name>.json "values") sets."""
    try:
        values = json.loads((ROOT / "pc/assets/post" / f"{name}.json").read_text()).get("values", {})
    except (OSError, ValueError):
        return []
    return [k for k in TONEMAP_KEYS if k in values]


_EMITTER_ASSETS = None


def emitter_assets():
    """The emitter assets' ids (pc/assets/emitters/*.json)."""
    global _EMITTER_ASSETS
    if _EMITTER_ASSETS is None:
        _EMITTER_ASSETS = {p.stem for p in (ROOT / "pc/assets/emitters").glob("*.json")}
    return _EMITTER_ASSETS


def check_values(report, path, e, prefabs, settings):
    global FIXTURE_TYPES
    kind = e.id.split(":", 1)[0]
    fields = e.item.fields if e.verb == "define" else e.set
    ftype = dict(fields).get("fixture_type")
    if ftype is not None:
        if FIXTURE_TYPES is None:
            FIXTURE_TYPES = fixture_types() or set()
        if FIXTURE_TYPES and ftype not in FIXTURE_TYPES:
            report("error", path, e.line, f"{e.id}: fixture_type {ftype} is not in pc/assets/fixtures/catalog.json")
    if kind not in lf.SCHEMA:
        report("error", path, e.line, f"unknown kind '{kind}' (the game ignores it)")
        return
    if e.verb == "define":
        for key in lf.REQUIRED.get(kind, []):
            if e.item.get(key) is None:
                report("error", path, e.line, f"{e.id}: needs {key}=")
        if kind == "set" and e.item.name not in settings:
            report("error", path, e.line, f"{e.id}: no such room setting (pc/src/room_settings.cpp)")
        if kind == "uv" and not (e.item.name.isdigit() and 1 <= int(e.item.name) <= 64):
            report("error", path, e.line, f"{e.id}: texture effects are numbered 1-64")
    if kind == "env":
        name = e.id.split(":", 1)[1]
        if name not in lf.ENV_FIELDS:
            report("error", path, e.line, f"{e.id}: the environment items are env:sun, env:sky and env:fog")
        else:
            for key, _ in fields:
                if lf.known_field(kind, key) and key not in lf.ENV_FIELDS[name]:
                    report("error", path, e.line, f"{e.id}: '{key}' is not a field of the {name}")
    if kind == "post":
        name = e.id.split(":", 1)[1]
        if not (name in ("room", "cutscene") or (name.startswith("event.") and name[6:].isdigit())):
            report("error", path, e.line, f"{e.id}: post profiles are post:room, post:cutscene and post:event.<id>")
        # The tonemapper is the player's choice (Graphics > Post-processing, settings post_preset): a room's
        # profile that sets it overrides theirs. A warning: the game still applies it.
        for key, _ in fields:
            if key in TONEMAP_KEYS:
                report("warning", path, e.line, f"{e.id}: sets {key}: the tonemapper is the player's choice; "
                                                f"leave {key} out of room profiles")
        preset = dict(fields).get("preset")
        if preset:
            for key in post_preset_tonemap(preset):
                report("warning", path, e.line, f"{e.id}: its preset {preset} sets {key}: the tonemapper is the "
                                                f"player's choice; name a preset without it, or set the fields here")
        return  # the fields: pc/src/post_profile.cpp checks them (and logs unknown ones)
    for key, value in fields:
        if not lf.known_field(kind, key):
            report("error", path, e.line, f"{e.id}: unknown key '{key}'")
            continue
        decimals = lf.field_decimals(kind, key)
        spec = lf._spec(kind, key)
        if kind == "light" and key == "type":
            if value not in lf.LIGHT_TYPES:
                report("error", path, e.line, f"{e.id}: type must be one of {', '.join(lf.LIGHT_TYPES)}")
            continue
        if kind == "fog" and key == "shape":
            if value not in lf.FOG_SHAPES:
                report("error", path, e.line, f"{e.id}: shape must be one of {', '.join(lf.FOG_SHAPES)}")
            continue
        if kind == "fog" and key == "preset":
            if value not in fog_presets():
                report("warning", path, e.line,
                       f"{e.id}: preset '{value}' is not in pc/assets/environment/fog_presets.json (its fields as written)")
            continue
        if kind == "emitter" and key == "shape":
            if value not in lf.EMITTER_SHAPES:
                report("error", path, e.line, f"{e.id}: shape must be one of {', '.join(lf.EMITTER_SHAPES)}")
            continue
        if kind == "emitter" and key == "render":
            if value not in lf.EMITTER_RENDERS:
                report("error", path, e.line, f"{e.id}: render must be {' or '.join(lf.EMITTER_RENDERS)}")
            continue
        if kind == "emitter" and key == "quality":
            if value not in lf.EMITTER_QUALITIES:
                report("error", path, e.line, f"{e.id}: quality must be one of {', '.join(lf.EMITTER_QUALITIES)}")
            continue
        if kind == "emitter" and key == "asset":
            if value not in emitter_assets():
                report("warning", path, e.line, f"{e.id}: asset '{value}' is not in pc/assets/emitters (its fields as written)")
            continue
        if decimals < 0:
            continue
        nums = lf.parse_numbers(value)
        if nums is None:
            report("error", path, e.line, f"{e.id}: {key}={value} is not a number or a comma vector")
            continue
        count = lf.COUNTS.get(key)
        if key == "size" and kind == "light":
            count = 2
        elif key == "size" and kind in ("fog", "emitter"):
            count = 3
        elif key == "size":
            count = 1
        if spec is None:
            count = None  # a prefab parameter: a number or a vector of 3
            if len(nums) not in (1, 3):
                report("error", path, e.line, f"{e.id}: {key} takes 1 or 3 numbers")
        if count is None and spec is not None:
            count = 1
        if count and len(nums) != count:
            report("error", path, e.line, f"{e.id}: {key} takes {count} number{'s' if count > 1 else ''}")
    if kind == "actor":
        prefab = e.item.get("prefab") if e.verb == "define" else dict(e.set).get("prefab")
        if prefab is not None and prefabs and prefab not in prefabs:
            report("warning", path, e.line, f"{e.id}: prefab '{prefab}' is not a built-in (pc/assets/prefabs)")
        if prefab in prefabs:
            for key, _ in fields:
                if lf._spec("actor", key) is None and key not in prefabs[prefab]:
                    report("error", path, e.line, f"{e.id}: '{key}' is not a parameter of prefab '{prefab}'")


def lint_file(report, path, kind, rooms, prefabs, settings, fix):
    m = NAME.match(path.name)
    if not m:
        if path.suffix == ".txt" and path.name != "defaults.txt":
            report("error", path, 0, "room files are named zoneZZ_roomRR.txt")
        return None
    zone, room = int(m.group(1)), int(m.group(2))
    if rooms is not None and (zone, room) not in rooms:
        report("error", path, 0, f"zone {zone} room {room} is not a room of the game")
    text = path.read_text(encoding="utf-8")
    f = lf.parse_text(text, str(path))
    for line, message in f.errors:
        report("error", path, line, message)
    if f.version == 0:
        return f
    seen = {}
    for e in f.entries:
        if e.id in seen:
            report("error", path, e.line, f"{e.id}: also on line {seen[e.id]} (one entry per id per file)")
        seen.setdefault(e.id, e.line)
        if kind == "generated" and e.verb != "define":
            report("warning", path, e.line, f"{e.verb} in a generated layer (generators define items; overrides "
                                            "belong in rooms/)")
        check_values(report, path, e, prefabs, settings)
    ids = [e.id for e in f.entries]
    if ids != sorted(ids):
        report("warning", path, 0, "entries are not sorted by id")
    lines = text.splitlines()
    canonical = list(lines)
    for e in f.entries:
        want = lf.format_entry(e)
        if re.sub(r"\s+#.*$", "", lines[e.line - 1]).strip() != want:
            canonical[e.line - 1] = want
            if not fix:
                report("warning", path, e.line, f"not canonical; expected: {want}")
    if fix and canonical != lines:
        path.write_text("\n".join(canonical) + "\n", encoding="utf-8")
        print(f"fixed: {os.path.relpath(path, ROOT)}")
    return f


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", nargs="?", default=str(ROOT / "lighting"), help="the packs' directory")
    ap.add_argument("--packs", help="comma-separated stack, lowest first (default: every pack, by name; 'default' first)")
    ap.add_argument("--user", help="also check this user layer on top (e.g. pc/user/lighting)")
    ap.add_argument("--data", default=str(ROOT / "data"), help="the game's extracted files (room ids)")
    ap.add_argument("--fix", action="store_true", help="rewrite entry lines in canonical form")
    args = ap.parse_args()
    root = pathlib.Path(args.root)
    report = Report()
    if not root.is_dir():
        print(f"no packs at {root}")
        return 0
    if args.packs:
        packs = [p.strip() for p in args.packs.split(",") if p.strip()]
    else:
        packs = sorted((p.name for p in root.iterdir() if p.is_dir()), key=lambda n: (n != "default", n))
    rooms = game_rooms(args.data)
    if rooms is None:
        print(f"note: {args.data}/SLUS_010.40 not found: room ids are not checked against the game")
    settings = setting_keys()
    prefabs = prefab_params([ROOT / "pc/assets/prefabs"])
    names = set()
    count = 0
    for pack in packs:
        for kind in ("generated", "rooms"):
            d = root / pack / kind
            if not d.is_dir():
                continue
            for path in sorted(d.iterdir()):
                if path.is_file() and path.suffix == ".txt":
                    count += 1
                    if lint_file(report, path, kind, rooms, prefabs, settings, args.fix):
                        names.add(path.name)
        for extra in sorted((root / pack).iterdir()) if (root / pack).is_dir() else []:
            if extra.is_dir() and extra.name not in ("generated", "rooms"):
                report("warning", extra, 0, "unexpected directory (a pack has generated/ and rooms/)")
    user = pathlib.Path(args.user) if args.user else None
    if user and user.is_dir():
        for path in sorted(user.glob("zone*_room*.txt")):
            count += 1
            if lint_file(report, path, "user", rooms, prefabs, settings, args.fix):
                names.add(path.name)
    # Merged per room: dangling overrides and deletes.
    for name in sorted(names):
        m = NAME.match(name)
        r = lf.load_room(int(m.group(1)), int(m.group(2)), str(root), packs, str(user) if user else None)
        for layer, line, id, message in r.problems:
            if id:
                report("error", r.layers[layer].path, line, f"dangling: {message}")
    print(f"{count} room file{'s' if count != 1 else ''} in {len(packs)} pack{'s' if len(packs) != 1 else ''}: "
          f"{report.errors} error{'s' if report.errors != 1 else ''}, {report.warnings} warning"
          f"{'s' if report.warnings != 1 else ''}")
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
