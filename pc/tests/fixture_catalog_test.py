#!/usr/bin/env python3
"""The fixture catalog and the generated lighting (pc/docs/Fixture_Catalog.md).

    python3 pc/tests/fixture_catalog_test.py [--probe pc/build/fixture_probe] [--zone 9]

Without the game's disc:
- catalog schema: format, classes (family, prefab of pc/assets/prefabs or
  "none", known parameters, glow leave/drive/suppress), types (ids T001...
  unique and in order, a known class, match rules well formed, flames,
  overrides of known prefab parameters), hashes and numbers only;
- the file is in canonical form (fixture_lib.json_write of it gives the same
  bytes, the layout the editor's save writes), and the game's reader
  (fixture_probe, pc/src/fixture_catalog.cpp) resolves every type as
  fixture_lib.type_fields does and writes the file back byte for byte;
- the generated layer files lint clean, name only catalogued types, and
  their actors carry what the game needs (prefab, pos, fixture, fixture_type).
With the disc (ROOD_DISC or disks/SLUS-01040.bin):
- the scan matches every fixture of the zones (--zone, default those with
  generated files) to exactly one type, by its exact signature;
- the generator is deterministic (two runs into scratch directories give
  identical files) and the committed generated files are up to date.
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import fixture_lib as fl  # noqa: E402
import lighting_format as lf  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def prefab_params():
    out = {}
    for path in sorted((ROOT / "pc/assets/prefabs").glob("*.json")):
        out[path.stem] = {p["key"] for p in json.loads(path.read_text()).get("params", [])}
    return out


def schema(cat, prefabs):
    text = (fl.CATALOG).read_text()
    check(cat.get("format") == 1, "format 1")
    problems = []
    for name, c in cat["classes"].items():
        if c.get("family") not in ("fire", "lamp", "magic", "window", "shaft", "decal", "non", "other"):
            problems.append(f"class {name}: family {c.get('family')}")
        if c.get("prefab") != "none" and c.get("prefab") not in prefabs:
            problems.append(f"class {name}: prefab {c.get('prefab')} not in pc/assets/prefabs")
        if c.get("glow") not in ("leave", "drive", "suppress"):
            problems.append(f"class {name}: glow {c.get('glow')}")
        for k in c.get("params", {}):
            if k not in prefabs.get(c.get("prefab"), set()):
                problems.append(f"class {name}: {k} is not a parameter of {c.get('prefab')}")
    ids = [t["id"] for t in cat["types"]]
    if ids != ["T%03d" % (i + 1) for i in range(len(ids))]:
        problems.append("type ids are not T001, T002, ... in order")
    for t in cat["types"]:
        if t["class"] not in cat["classes"]:
            problems.append(f"{t['id']}: unknown class {t['class']}")
            continue
        m = t.get("match", {})
        if not (isinstance(m.get("emitters"), list) and m["emitters"] and isinstance(m.get("holders"), list)
                and len(m.get("polygons", [])) == 2 and len(m.get("extent", {}).get("min", [])) == 3
                and len(m.get("extent", {}).get("max", [])) == 3):
            problems.append(f"{t['id']}: malformed match rule")
        for e in m.get("emitters", []):
            if not re.fullmatch(r"(opaque:)?[0-9a-f]{10}(\*\d+)?(\+[0-9a-f]{10}(\*\d+)?)*", e):
                problems.append(f"{t['id']}: emitter signature {e!r}")
        for h in m.get("holders", []):
            if not re.fullmatch(r"([0-9a-f]{10})?", h):
                problems.append(f"{t['id']}: holder signature {h!r}")
        f = fl.type_fields(cat, t)
        for k in t.get("params", {}):
            if k not in prefabs.get(f["prefab"], set()):
                problems.append(f"{t['id']}: {k} is not a parameter of {f['prefab']}")
        if not isinstance(t.get("flames"), list) or any(len(p) != 3 for p in t["flames"]):
            problems.append(f"{t['id']}: flames")
    check(not problems, f"catalog schema ({len(cat['types'])} types, {len(cat['classes'])} classes)"
          + "".join("\n      " + p for p in problems[:20]))
    # hashes and numbers only: every string is a known word, a hash, a signature or prose of ours
    words = set()

    def walk(v, key=""):
        if isinstance(v, dict):
            for k, x in v.items():
                walk(x, k)
        elif isinstance(v, list):
            for x in v:
                walk(x, key)
        elif isinstance(v, str) and key not in ("comment", "description", "emitters", "holders", "zones"):
            words.add(v)
    walk(cat)
    allowed = set(cat["classes"]) | set(prefabs) | {"none", "leave", "drive", "suppress", "high", "medium", "low",
                                                    "scan", "new", "checked", "fire", "lamp", "magic", "window",
                                                    "shaft", "decal", "non", "other"}
    odd = sorted(w for w in words if w not in allowed and not re.fullmatch(r"T\d{3}", w))
    check(not odd, f"only our own words, ids and hashes in the catalog ({odd[:8]})")
    check(fl.json_write(cat) + "\n" == text, "the catalog is in canonical form (as fixture_lib and the editor write it)")


def probe(path, cat):
    if not path or not pathlib.Path(path).exists():
        print(f"note: no fixture_probe ({path}): the game's reader is not checked")
        return
    with tempfile.TemporaryDirectory() as tmp:
        written = pathlib.Path(tmp) / "catalog.json"
        r = subprocess.run([str(path), str(fl.CATALOG), str(written)], capture_output=True, text=True)
        check(r.returncode == 0, f"fixture_probe runs ({r.stderr.strip()[:200]})")
        if r.returncode:
            return
        got = json.loads(r.stdout)
        check(written.read_bytes() == fl.CATALOG.read_bytes(), "the game writes the catalog back byte for byte")
    bad = []
    for t in cat["types"]:
        f = fl.type_fields(cat, t)
        g = got.get(t["id"])
        if not g:
            bad.append(f"{t['id']} missing")
            continue
        want_glow = {"leave": 0, "drive": 1, "suppress": 2}[f["glow"]]
        if g["prefab"] != f["prefab"] or g["glow"] != want_glow or bool(g["enabled"]) != bool(f["enabled"]) \
                or abs(g["fixture_radius"] - f["fixture_radius"]) > 1e-4:
            bad.append(f"{t['id']}: {g} vs {f}")
        for k, v in f["params"].items():
            gv = g["params"].get(k)
            v = v if isinstance(v, list) else [float(v)]
            if gv is None or any(abs(a - float(b)) > 1e-4 for a, b in zip(gv, v)):
                bad.append(f"{t['id']}: param {k} {gv} vs {v}")
    check(not bad, "the game resolves every type as the tools do" + "".join("\n      " + b for b in bad[:10]))


def generated_files(cat):
    ids = {t["id"] for t in cat["types"]}
    r = subprocess.run([sys.executable, str(ROOT / "pc/tools/lighting_lint.py")], capture_output=True, text=True)
    check(r.returncode == 0, "lighting_lint: " + r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "lint")
    zones, bad, actors = set(), [], 0
    for path in sorted((ROOT / "lighting").glob("*/generated/zone*_room*.txt")):
        zones.add(int(path.name[4:6]))
        f = lf.read_file(str(path))
        for e in f.entries:
            if e.verb != "define":
                bad.append(f"{path.name}: {e.verb} in a generated file")
                continue
            t = e.item.get("fixture_type")
            if t and t not in ids:
                bad.append(f"{path.name}: {e.id} names {t}, not in the catalog")
            if e.item.kind == "actor":
                actors += 1
                if not (e.id.startswith("actor:gen.") and e.item.get("prefab") and e.item.get("pos")
                        and e.item.get("fixture") and t):
                    bad.append(f"{path.name}: {e.id} lacks prefab/pos/fixture/fixture_type")
        text = path.read_text()
        lines = text.splitlines()
        comment = lines[1][2:] if len(lines) > 1 and lines[1].startswith("# ") else ""
        if text != lf.format_file(f.entries, comment):
            bad.append(f"{path.name}: not in canonical form")
    check(not bad, f"generated files: {actors} actors in zones {sorted(zones)}" + "".join("\n      " + b for b in bad[:10]))
    return sorted(zones)


def with_disc(zones):
    try:
        fl.Disc()
    except OSError as error:
        print(f"note: no disc image ({error}): scan and generator not checked")
        return
    args = [a for z in zones for a in ("--zone", str(z))]
    r = subprocess.run([sys.executable, str(ROOT / "pc/tools/fixture_scan.py"), "--check", *args],
                       capture_output=True, text=True)
    last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:]
    check(r.returncode == 0 and "unmatched 0" in last and "by rule" in last and ", 0 by rule" in last,
          f"scan: every fixture matches one type exactly ({last})")
    with tempfile.TemporaryDirectory() as tmp:
        outs = []
        for n in range(2):
            root = pathlib.Path(tmp) / f"run{n}"
            r = subprocess.run([sys.executable, str(ROOT / "pc/tools/generate_lighting.py"), "--quiet", "--root", str(root),
                                *args], capture_output=True, text=True)
            check(r.returncode == 0, f"generator run {n + 1}: {r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:]}")
            outs.append({p.name: p.read_bytes() for p in sorted((root / "default/generated").glob("*.txt"))})
        check(outs[0] == outs[1] and outs[0], f"the generator is deterministic ({len(outs[0])} files, identical twice)")
        committed = {p.name: p.read_bytes() for p in sorted((ROOT / "lighting/default/generated").glob("*.txt"))
                     if int(p.name[4:6]) in zones}
        check(committed == outs[0], "the committed generated files are up to date (regenerate: "
              "pc/tools/generate_lighting.py --zone N)")
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--probe", default=str(ROOT / "pc/build/fixture_probe"))
    ap.add_argument("--zone", type=int, action="append")
    args = ap.parse_args()
    cat = fl.load_catalog()
    schema(cat, prefab_params())
    probe(args.probe, cat)
    zones = generated_files(cat)
    with_disc(args.zone or zones)
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
