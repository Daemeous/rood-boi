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
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

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
        if c.get("glow_shape", "auto") not in ("auto", "sphere", "column"):
            problems.append(f"class {name}: glow_shape {c.get('glow_shape')}")
        if not 0 <= c.get("glow_shadow_core", 0.25) <= 1:
            problems.append(f"class {name}: glow_shadow_core {c.get('glow_shadow_core')}")
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
                                                    "shaft", "decal", "non", "other",
                                                    "auto", "sphere", "column"}  # glow_shape
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


# The scan and the whole-disc generation are pure functions of their inputs
# (the tools, catalog, prefabs, lighting packs and the disc image): a passing
# result is kept under pc/build/fixture-check/<hash of the inputs>.json and
# stands for identical inputs (ROOD_FIXTURE_CHECK_CACHE=0 recomputes). The
# determinism check always runs: a second generation, in another process
# with other parallelism, against the full run's (kept) file hashes.
CACHE = ROOT / "pc/build/fixture-check"


def inputs_key(zones, disc):
    h = hashlib.sha256()
    files = [pathlib.Path(__file__).resolve()]
    for d, pattern in (("pc/tools", "*.py"), ("pc/assets/fixtures", "**/*"), ("pc/assets/prefabs", "**/*"),
                       ("lighting", "**/*")):
        files += sorted(p for p in (ROOT / d).glob(pattern) if p.is_file())
    for p in files:
        h.update(str(p.relative_to(ROOT)).encode() + b"\0" + p.read_bytes() + b"\0")
    st = os.stat(disc)
    h.update(f"{os.path.abspath(disc)} {st.st_size} {st.st_mtime_ns} {zones} {sys.version}".encode())
    return h.hexdigest()


def generate(root, zones, jobs):
    r = subprocess.run([sys.executable, str(ROOT / "pc/tools/generate_lighting.py"), "--quiet", "--root", str(root),
                        "--jobs", jobs, *[a for z in zones for a in ("--zone", str(z))]], capture_output=True, text=True)
    last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:]
    return r.returncode, last, {p.name: p.read_bytes() for p in sorted((root / "default/generated").glob("*.txt"))}


def with_disc(zones):
    try:
        disc = fl.Disc().path
    except OSError as error:
        print(f"note: no disc image ({error}): scan and generator not checked")
        return
    # Every zone once (zones in parallel), then a second run of a few zones
    # with other parallelism (a candle room, torches, the 67-candle hall, the
    # town's street lanterns) must give the same bytes.
    again = [z for z in (9, 13, 26, 48) if z in zones] or zones[:1]
    key = inputs_key(zones, disc)
    cached = CACHE / f"{key[:32]}.json"
    use_cache = os.environ.get("ROOD_FIXTURE_CHECK_CACHE", "1") != "0"
    record = None
    if use_cache and cached.exists():
        try:
            record = json.loads(cached.read_text())
        except ValueError:
            record = None
    with tempfile.TemporaryDirectory() as tmp:
        if record and record.get("key") == key:
            print(f"note: scan and full generation: the passing result of {record['when']} for these exact inputs "
                  f"(tools, catalog, prefabs, lighting packs, disc; {cached.relative_to(ROOT)})")
            for message in record["passed"]:
                check(True, message + " [same inputs as a passing run]")
            full = record["hashes"]
        else:
            failed_before = len(failures)
            passed = []

            def keep(ok, message):
                check(ok, message)
                if ok:
                    passed.append(message)

            args = [a for z in zones for a in ("--zone", str(z))]
            r = subprocess.run([sys.executable, str(ROOT / "pc/tools/fixture_scan.py"), "--check", *args],
                               capture_output=True, text=True)
            last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:]
            keep(r.returncode == 0 and "unmatched 0" in last and "by rule" in last and ", 0 by rule" in last,
                 f"scan: every fixture matches one type exactly ({last})")
            code, last, files = generate(pathlib.Path(tmp) / "run0", zones, "8")
            keep(code == 0, f"generator run 1: {last}")
            committed = {p.name: p.read_bytes() for p in sorted((ROOT / "lighting/default/generated").glob("*.txt"))
                         if int(p.name[4:6]) in zones}
            keep(committed == files, f"the committed generated files are up to date ({len(committed)} files in "
                 f"{len(zones)} zones; regenerate: pc/tools/generate_lighting.py)")
            full = {k: hashlib.sha256(v).hexdigest() for k, v in files.items() if int(k[4:6]) in again}
            if len(failures) == failed_before and use_cache:
                CACHE.mkdir(parents=True, exist_ok=True)
                cached.write_text(json.dumps({"key": key, "when": time.strftime("%Y-%m-%d %H:%M"),
                                              "passed": passed, "hashes": full}, indent=1) + "\n")
        code, last, files = generate(pathlib.Path(tmp) / "run1", again, "1")
        check(code == 0, f"generator run 2: {last}")
        second = {k: hashlib.sha256(v).hexdigest() for k, v in files.items()}
        check(second == full and second, f"the generator is deterministic (zones {again}: {len(second)} files "
                                         "identical, in parallel and not)")
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
