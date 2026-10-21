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
    root.zig import and export) produce a table ownership.py loads, on copies;
  - the pilot's (38C1C) tooling failures stay fixed: (A) a typedef'd pointer
    parameter (`u_short*`) is planned as a Zig type the stub, the spec
    harness and the port all see; (B) every global the C names gets a views
    accessor and the prompts show it (translate-c's `pub extern var`); (C) a
    spec's stub typed with views structs where the c prototype has c structs
    builds; a C crash is reported with its fault address, arguments and
    plain advice (no spurious class/mutation errors), the coverage gate's
    sign flag in plain words, and a saved reply to another prompt is not
    reused.

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


def pipeline(args, work, replies=None, edit=None, table=None):
    work.mkdir(parents=True, exist_ok=True)
    if replies:
        dest = work / replies.name
        shutil.copytree(replies, dest, dirs_exist_ok=True)
        for name, (old, new) in (edit or {}).items():
            text = (dest / name).read_text()
            assert text.count(old) == 1, (name, old)
            text = text.replace(old, new)
            if name == "spec_reply1.md" and replies.name == "func_8006B214":  # never sets the bit: no -1 edge either
                text = text.replace("else => ctx.pick(i32, &.{ 0, 0x80000, 0x200000, -1 }),", "else => 0x80000,")
            (dest / name).write_text(text)
    env = dict(os.environ, OPENROUTER_API_KEY="offline")
    r = subprocess.run([sys.executable, ROOT / "pc/tools/port_pipeline.py", *args, "--budget", "0", "--work", work,
                        "--records", work / "records", "--build", BUILD, *(["--table", table] if table else [])],
                       capture_output=True, text=True, env=env)
    return r


def record(work, function, unit="146C"):
    path = work / "records" / unit / f"{function}.json"
    return json.loads(path.read_text()) if path.exists() else None


PILOT = ["func_800A1AF8", "func_800A17BC"]
UNROUTED = PILOT + ["func_800A208C"]  # planned by the tests, ported since


def pilot_38C1C(tmp, rows, names):
    """Regressions from the 38C1C pilot (pc/docs/porting/battle/pipeline/38C1C)."""
    import ownership
    import port_pipeline as pp
    import zig_diff
    failures = []
    # A table where the canned functions are not routed (a later run may port them).
    text = "\n".join(l for l in (ROOT / "game/ownership.toml").read_text().split("\n")
                     if not any(f'name = "{f}"' in l for f in UNROUTED))
    table_path = tmp / "ownership-38C1C.toml"
    table_path.write_text(text)
    table = ownership.load(table_path)
    units = {}
    # (A) typedef'd pointer parameters as Zig types.
    for name, want in {"func_800A1AF8": "pub fn func_800A1AF8(arg0: c_int, arg1: c_int, arg2: [*c]c_ushort, arg3: c_int) "
                                        "callconv(.c) c_int",
                       "func_800A208C": "pub fn func_800A208C(actor: ?*v.D_800F4538_t, target: [*c]u8, speed: c_int, "
                                        "facing: c_short) callconv(.c) void"}.items():
        plan, _ = pp.plan_function(name, rows[name], table, names, units, BUILD, tmp / "plan-38C1C")
        if plan.skip or plan.signature != want:
            failures.append(f"(A) signature of {name}: {plan.skip or plan.signature}")
    if pp.zig_type("[*c]u_short", set(), set(), {"u_short": "c_ushort"}, {"u_short"}) != "[*c]c_ushort" \
            or pp.zig_type("[*c]psx_thing", set(), set(), {}, {"psx_thing"}) != "[*c]c.psx_thing":
        failures.append("(A) zig_type: aliases resolved, other unit names through c.")
    # (B) the globals the C names, with accessors, in the views and the prompts.
    if pp.c_decls("pub extern var D_1: [17]?*T;\npub const D_2: [*c]u8 = @extern([*c]u8, .{});\n")[1] != {"D_1", "D_2"}:
        failures.append("(B) c_decls: `pub extern var` and `@extern` globals")
    plan, unit = pp.plan_function("func_800A17BC", rows["func_800A17BC"], table, names, units, BUILD, tmp / "plan-38C1C")
    if plan.skip:
        failures.append(f"(B) func_800A17BC skipped: {plan.skip}")
    else:
        views_path, _ = unit.views(plan.types, plan.globals, tag="test-17BC")
        named = {"D_800F4538", "D_800F45E0", "D_800F49B8", "vs_battle_wepModels"}
        have = pp.views_globals(views_path.read_text())
        if not named <= have:
            failures.append(f"(B) views accessors of func_800A17BC: missing {sorted(named - have)}")
        for which, prompt in (("spec", pp.spec_prompt(plan, unit, views_path, table, BUILD)),
                              ("port", pp.port_prompt(plan, unit, views_path, table, BUILD))):
            if "pub inline fn D_800F4538() *[17]?*D_800F4538_t" not in prompt:
                failures.append(f"(B) the {which} prompt of func_800A17BC does not show v.g.D_800F4538")
    # Canned spec and port: (A) func_800A1AF8 (a [*c]c_ushort parameter, a stub
    # taking one); (B, C) func_800A17BC (the views accessors, a stub of
    # func_800A1DE8 declared with views MATRIX pointers, PS1 low RAM read for NULL).
    for name in PILOT:
        r = pipeline([name, "--no-write"], tmp / f"pilot-{name}", CANNED / name, table=table_path)
        rec = record(tmp / f"pilot-{name}", name, "38C1C")
        if not rec or rec["status"] != "passed (not written)":
            errors = rec and [e for k in ("spec", "port") for a in rec[k]["attempts"] for e in a["errors"]]
            failures.append(f"canned {name}: {rec and rec['status']}: {errors and errors[:3]} {r.stderr[-800:]}")
    # A spec whose C crashes: the advice, the fault address, the arguments;
    # no class/mutation/coverage noise (they cannot be judged on a crash).
    r = pipeline(["func_800A17BC", "--no-write"], tmp / "pilot-crash", CANNED / "func_800A17BC",
                 {"spec_reply1.md": ("    v.g.D_800F4538()[slot] = if (kind <= 1) null else actor;\n", "")},
                 table=table_path)
    rec = record(tmp / "pilot-crash", "func_800A17BC", "38C1C")
    errors = "\n".join(rec["spec"]["attempts"][0]["errors"]) if rec else ""
    if not rec or rec["status"] != "stopped" or "crashed on your inputs" not in errors \
            or not ("fault address 0x" in errors or "a wild pointer" in errors) or "crashed on arguments:" not in errors \
            or "no input classes" in errors or "mutation" in errors.replace("(the mutations and", ""):
        failures.append(f"(D) a crashing spec's feedback: {rec and rec['status']}: {errors[:1500]} {r.stderr[-500:]}")
    # A spec whose own prepare panics (an index past a table): said so, nothing else.
    r = pipeline(["func_800A17BC", "--no-write"], tmp / "pilot-panic", CANNED / "func_800A17BC",
                 {"spec_reply1.md": ("ctx.range(usize, 0, 9)", "ctx.range(usize, 0, 17)")}, table=table_path)
    rec = record(tmp / "pilot-panic", "func_800A17BC", "38C1C")
    errors = "\n".join(rec["spec"]["attempts"][0]["errors"]) if rec else ""
    if not rec or "your spec's own code" not in errors or "index out of bounds" not in errors \
            or "no input classes" in errors or "not deterministic" in errors:
        failures.append(f"a panicking prepare's feedback: {rec and rec['status']}: {errors[:1500]} {r.stderr[-500:]}")
    # (E) the sign flag in plain words.
    report = {"function": "f", "uncovered": [], "flags": [
        {"id": "+19 sign", "line": 19, "text": "} else if (bone == 255) {", "kind": "sign", "bits": 32, "constant": 255,
         "what": "..."}]}
    hints = "\n".join(zig_diff.coverage_hints(report))
    if "above 255 AND positive" not in hints or "256" not in hints:
        failures.append(f"(E) sign flag hint: {hints}")
    # Resume: a saved reply to another conversation is set aside, not reused.
    saved = tmp / "resume" / "spec_reply1.md"
    saved.parent.mkdir(parents=True)
    saved.write_text("old")
    saved.with_suffix(".json").write_text(json.dumps({"cost": 0.01}))
    state = {"spent": 0.0, "budget": 0.0, "reserve": 0.0, "model": "m", "max_tokens": 1, "dry_run": True}
    try:
        pp.ask(state, [{"role": "user", "content": "new"}], saved, "test")
        failures.append("resume: a reply to another prompt was reused")
    except pp.Stop:
        if saved.exists() or not (saved.parent / "spec_reply1.stale1.md").exists():
            failures.append("resume: the stale reply was not set aside")
    saved.write_text("same")
    key = pp.conversation_key([{"role": "user", "content": "same"}], "m")
    saved.with_suffix(".json").write_text(json.dumps({"conversation": key}))
    if pp.ask(state, [{"role": "user", "content": "same"}], saved, "test")[0] != "same":
        failures.append("resume: a reply to the same conversation was not reused")
    return failures


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
        failures += pilot_38C1C(tmp, rows, names)
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
