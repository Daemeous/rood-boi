#!/usr/bin/env python3
"""pc/tools/port_pipeline.py without a model: canned replies
(pc/tests/port_pipeline/<FUNCTION>/) stand in for the model's, and a zero
budget makes any call a stop.

  - the plan: readable names (the decompilation's, MEANINGS.tsv's) or the
    original symbol kept, snake_case modules, the signature from the unit's
    prototype with views pointer types, already-ported functions skipped;
  - a correct spec and port pass both gates (the C as the stand-in port,
    every class, every C mutation caught, full coverage; 0 mismatches on 3
    seeds) and nothing is written with --no-write;
  - a wrong port fails its gate, a weak spec (a class never generated, a
    mutation not caught) fails its gate, each recorded with the checker's
    errors and stopped before the retry (budget 0); no record holds a local path;
  - the writers (specs.toml entry, ownership.toml subsystem and route,
    root.zig import and export) produce a table ownership.py loads, on copies.

CPU only; needs a 64-bit build (exit 77 without one)."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
BUILD = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "pc/build/boot64"
CANNED = ROOT / "pc/tests/port_pipeline"
HOME = str(pathlib.Path.home())


def pipeline(args, work, replies=None, edit=None):
    work.mkdir(parents=True, exist_ok=True)
    if replies:
        dest = work / replies.name
        shutil.copytree(replies, dest, dirs_exist_ok=True)
        for name, (old, new) in (edit or {}).items():
            text = (dest / name).read_text()
            assert text.count(old) == 1, (name, old)
            text = text.replace(old, new)
            if name == "spec_reply1.md":  # a spec that never sets the bit: no -1 edge either
                text = text.replace("else => ctx.pick(i32, &.{ 0, 0x80000, 0x200000, -1 }),", "else => 0x80000,")
            (dest / name).write_text(text)
    env = dict(os.environ, OPENROUTER_API_KEY="offline")
    r = subprocess.run([sys.executable, ROOT / "pc/tools/port_pipeline.py", *args, "--budget", "0", "--work", work,
                        "--records", work / "records", "--build", BUILD], capture_output=True, text=True, env=env)
    return r


def record(work, function, unit="146C"):
    path = work / "records" / unit / f"{function}.json"
    return json.loads(path.read_text()) if path.exists() else None


def main():
    if not (BUILD / "imports.ld").exists():
        print(f"port_pipeline_test: no 64-bit build in {BUILD}: skipped")
        return 77
    import port_pipeline as pp
    import ownership
    failures = []
    rows, names = pp.functions_tsv(), pp.meanings()
    # Names.
    for name, want in {"func_8006B214": ("func_8006B214", "func_8006B214"),
                       "vs_battle_equipShield": ("equipShield", "equip_shield"),
                       "_insertActor": ("insertActor", "insert_actor"),
                       "vs_battle_copyUiGripStats": ("copyUiGripStats", "copy_ui_grip_stats")}.items():
        zig, _ = pp.readable_name(name, names)
        if (zig, pp.snake(zig)) != want:
            failures.append(f"names of {name}: {(zig, pp.snake(zig))}, expected {want}")
    with tempfile.TemporaryDirectory(prefix="port-pipeline-test-") as tmp:
        tmp = pathlib.Path(tmp)
        table = ownership.load()
        units = {}
        plan, _ = pp.plan_function("_removeActor", rows["_removeActor"], table, names, units, BUILD, tmp / "plan")
        if plan.signature != "pub fn removeActor(actor: ?*v.vs_battle_actor) callconv(.c) c_int":
            failures.append(f"signature of _removeActor: {plan.signature}")
        if plan.subsystem != "battle_146C_port" or plan.module != "remove_actor":
            failures.append(f"plan of _removeActor: {plan.subsystem} {plan.module}")
        skipped, _ = pp.plan_function("func_8007C088", rows["func_8007C088"], table, names, units, BUILD, tmp / "plan")
        if "already ported" not in skipped.skip:
            failures.append(f"func_8007C088 not skipped as ported: {skipped.skip!r}")

        # The gates, with canned replies.
        good = pipeline(["func_8006B214", "--no-write"], tmp / "good", CANNED / "func_8006B214")
        r = record(tmp / "good", "func_8006B214")
        if not r or r["status"] != "passed (not written)":
            failures.append(f"canned func_8006B214: {r and r['status']}: {good.stderr[-1500:]}")
        elif not r["spec"]["coverage"]["passed"] or len(r["port"]["attempts"][0]["runs"]) != 3:
            failures.append(f"canned func_8006B214: gates incomplete: {json.dumps(r)[:800]}")
        bad_port = pipeline(["func_8006B214", "--no-write"], tmp / "bad-port", CANNED / "func_8006B214",
                            {"port_reply1.md": ("0, 0x100000, 1);", "0, 0x100000, 0);")})
        r = record(tmp / "bad-port", "func_8006B214")
        if not r or r["status"] != "stopped" or r["port"]["attempts"][0]["passed"] \
                or "differs" not in " ".join(r["port"]["attempts"][0]["errors"]):
            failures.append(f"wrong port not stopped at its gate: {r and r['status']}: {bad_port.stderr[-800:]}")
        bad_spec = pipeline(["func_8006B214", "--no-write"], tmp / "bad-spec", CANNED / "func_8006B214",
                            {"spec_reply1.md": ("        0 => 0x100000,\n        1 => ctx.int(i32) | 0x100000,\n",
                                                "        0, 1 => ctx.int(i32) & ~@as(i32, 0x100000),\n"),
                             "port_reply1.md": ("0x100000 != 0", "0x100000 != 0")})
        r = record(tmp / "bad-spec", "func_8006B214")
        errors = " ".join(r["spec"]["attempts"][0]["errors"]) if r else ""
        if not r or r["status"] != "stopped" or "never generated: status-set" not in errors \
                or "not caught" not in errors or "UNCOVERED" not in errors:
            failures.append(f"weak spec not stopped at its gate: {r and r['status']}: {errors[:1500]} {bad_spec.stderr[-500:]}")
        for p in tmp.rglob("records/**/*.json"):
            if HOME in p.read_text() or os.path.join(tempfile.gettempdir(), "claude") in p.read_text():
                failures.append(f"{p.name}: a local path in the record")

        # The writers, on copies of the table, root.zig and specs.toml.
        game = tmp / "game"
        shutil.copytree(ROOT / "game/battle", game / "battle")
        shutil.copy(ROOT / "game/ownership.toml", game / "ownership.toml")
        pp.GAME, pp.OWNERSHIP, pp.ROOT_ZIG = game, game / "ownership.toml", game / "battle/root.zig"
        plan.types, plan.globals = ["vs_battle_actor"], ["vs_battle_actors"]
        pp.write_ownership(plan)
        pp.write_root(plan)
        second, _ = pp.plan_function("vs_battle_equipShield", rows["vs_battle_equipShield"], table, names, units, BUILD,
                                     tmp / "plan")
        pp.write_ownership(second)
        pp.write_root(second)
        new = ownership.load(game / "ownership.toml")
        sub = next((s for s in new.subsystems if s.name == "battle_146C_port"), None)
        if sub is None or sub.zig.get("_removeActor") != "remove_actor.removeActor" \
                or sub.zig.get("vs_battle_equipShield") != "equip_shield.equipShield" \
                or "vs_battle_actor" not in sub.views["types"]:
            failures.append(f"ownership.toml writer: {sub}")
        root = (game / "battle/root.zig").read_text()
        for needle in ('pub const remove_actor = @import("remove_actor.zig");',
                       'pub const equip_shield = @import("equip_shield.zig");',
                       "ownership.routesOf(.battle_146C_port), @This());"):
            if root.count(needle) != 1:
                failures.append(f"root.zig writer: {needle!r} x{root.count(needle)}")
        entry = pp.toml_entry(plan, {"iterations": 99, "mutation": [
            {"name": 'a "quoted"\nname', "from": "a\\b", "to": "c"}],
            "coverage_waiver": [{"id": "+1 b0", "reason": "why"}]}, "m")
        parsed = tomllib.loads(entry)["function"]["_removeActor"]
        if parsed["iterations"] != 2000 or parsed["mutation"][0]["target"] != "c" \
                or parsed["mutation"][0]["from"] != "a\\b" or parsed["coverage_waiver"][0]["reason"] != "why":
            failures.append(f"specs.toml entry: {parsed}")
    for f in failures:
        print("FAIL:", f)
    print("port_pipeline_test: " + ("FAILED" if failures else "passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
