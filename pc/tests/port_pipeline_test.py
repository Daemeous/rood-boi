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
    reused;
  - the model client (a fake OpenAI-compatible server on loopback): a local
    endpoint sends no Authorization header, the thinking switch as
    chat_template_kwargs, the temperature, max_tokens clamped to the
    server's context, a reasoning block stripped, cost 0, the key never in
    a record; a remote one sends the key; neither sends a local path; a
    conversation too long for the context stops the function as `context`;
  - best-of-N (--attempts 2): a failing first sample, a passing second one
    kept without a retry; two functions with --jobs 2;
  - the 7CD68 head-to-head's tooling fixes: a table sized from the object
    defining it, a pointer to rows typed in the views and the prompt, a
    routed function's own name in the c module, a self-call's note;
  - the 7CD68 review's: a global pointer to integers is many-item in the
    views, an unsized table's length in the spec prompt, a compile-fix
    round after the retry (--compile-fixes), a field of a null pointer
    handed to a stub stops the C as the game would.

CPU only; needs a 64-bit build (exit 77 without one)."""
import http.server
import json
import re
import os
import pathlib
import threading
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


class FakeServer(http.server.BaseHTTPRequestHandler):
    """An OpenAI-compatible server: /v1/models (a 4096-token context), /tokenize, /v1/chat/completions."""
    seen = []

    def log_message(self, *a):
        pass

    def reply(self, data):
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.reply({"data": [{"id": "fake", "max_model_len": 4096}]})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path == "/tokenize":
            return self.reply({"count": sum(len(m["content"]) for m in body["messages"]) // 4})
        FakeServer.seen.append({"headers": dict(self.headers), "body": body})
        self.reply({"model": body["model"], "choices": [{"message": {"content": "<think>hmm</think>\nanswer"},
                                                          "finish_reason": "stop"}],
                    "usage": {"prompt_tokens": 10, "completion_tokens": 3, "cost": 0.5}})


def client_checks(tmp):
    import model_client as mc
    import port_pipeline as pp
    failures = []
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeServer)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_port}/v1"
    try:
        os.environ["PIPELINE_TEST_KEY"] = "sk-test-secret"
        local = mc.Endpoint(base_url=url, thinking="off", temperature=0.6)
        state = {"spent": 0.0, "budget": 0.0, "reserve": 0.0, "model": "fake", "max_tokens": 64000,
                 "dry_run": False, "endpoint": local}
        path = tmp / "client" / "spec_reply1.s2.md"
        path.parent.mkdir(parents=True)
        text, call = pp.ask(state, [{"role": "user", "content": "x" * 400}], path, "test", sample=2)
        sent = FakeServer.seen[-1]
        if text != "answer" or call["endpoint"] != "local" or call["cost"] != 0.0 or call["sample"] != 2:
            failures.append(f"local call: {text!r} {call}")
        if "Authorization" in sent["headers"] or sent["body"].get("chat_template_kwargs") != {"enable_thinking": False} \
                or sent["body"]["temperature"] != 0.6 or sent["body"]["max_tokens"] != 4096 - 100 - 64:
            failures.append(f"local request: {sent['headers'].get('Authorization')} {sent['body'].get('chat_template_kwargs')} "
                            f"{sent['body']['temperature']} {sent['body']['max_tokens']}")
        # The same conversation is reused; sample 1 of it is another one.
        if pp.ask(state, [{"role": "user", "content": "x" * 400}], path, "test", sample=2)[1] is None \
                or len(FakeServer.seen) != 1:
            failures.append("a saved sample was not reused")
        if pp.conversation_key([], "m", {"sample": 2}) == pp.conversation_key([], "m") \
                or pp.conversation_key([], "m", mc.Endpoint().key_fields()) != pp.conversation_key([], "m"):
            failures.append("conversation keys: a sample or a non-default endpoint must change the key; OpenRouter's not")
        try:
            pp.ask(state, [{"role": "user", "content": "x" * 20000}], tmp / "client" / "long.md", "test")
            failures.append("a conversation past the context was sent")
        except pp.Stop as stop:
            if not str(stop).startswith("context:"):
                failures.append(f"context stop: {stop}")
        remote = mc.Endpoint(base_url=url, local=False, api_key_env="PIPELINE_TEST_KEY", thinking="on")
        call = mc.call([{"role": "user", "content": "hi"}], "fake", 100, remote)
        sent = FakeServer.seen[-1]
        if sent["headers"].get("Authorization") != "Bearer sk-test-secret" or call["cost"] != 0.5 \
                or sent["body"].get("reasoning") != {"enabled": True} or "chat_template_kwargs" in sent["body"]:
            failures.append(f"remote request: {sent['body']} cost {call['cost']}")
        if "sk-test-secret" in json.dumps(call) or any("sk-test-secret" in p.read_text() for p in (tmp / "client").iterdir()):
            failures.append("the key in a call record")
        for endpoint in (remote, local):
            mc.call([{"role": "user", "content": f"see {HOME}/x"}], "fake", 100, endpoint)
            if HOME in json.dumps(FakeServer.seen[-1]["body"]):
                failures.append(f"a {endpoint.kind} call sent a local path")
        if mc.Endpoint().api_key_env != "OPENROUTER_API_KEY" or mc.Endpoint(base_url=url).api_key_env != "" \
                or not mc.Endpoint(base_url="http://localhost:1/v1").local or mc.Endpoint().local:
            failures.append("endpoint defaults")
        if mc.strip_thinking("a</think>\nb") != "b":
            failures.append("strip_thinking: a block opened by the template")
    finally:
        os.environ.pop("PIPELINE_TEST_KEY", None)
        server.shutdown()
    return failures


def head_to_head_7CD68(tmp, table):
    """The tooling failures the 7CD68 head-to-head found (BAKEOFF.md, "Local Qwen vs Haiku")."""
    import bakeoff_context as bc
    import port_pipeline as pp
    import zig_c
    import zig_diff
    failures = []
    # A table defined in another unit (`extern T x[];` here): sized from the defining object.
    if zig_diff.sizes_from_objects(BUILD, ["D_800F1790"]).get("D_800F1790") != 80:
        failures.append("sizes_from_objects: D_800F1790 (battle_movement.o, 80 bytes)")
    # A pointer to rows (`u_short (*D_800F58B8)[32]`): typed, an accessor, shown as one.
    unit = pp.Unit("7CD68", "src/BATTLE/BATTLE.PRG/7CD68.c", BUILD, tmp / "unit-7CD68", table)
    views, _ = unit.views([], ["D_800F58B8"], tag="rows")
    want = 'pub inline fn D_800F58B8() *?[*][32]u16 { return psx.global(?[*][32]u16, "D_800F58B8"); }'
    if want not in views.read_text() or want not in bc.views_subset(views.read_text(), {"D_800F58B8"}):
        failures.append("views: D_800F58B8 as rows of 32 u16 (the views and the prompt's subset)")
    # A routed function without a prototype of its own: the c module declares the routed name too.
    text = zig_c._routed_names("pub extern fn f__orig(a: c_int) c_int;\npub extern fn g__orig() void;\npub extern fn g() void;\n")
    if text.count("pub extern fn f(a: c_int) c_int;") != 1 or text.count("pub extern fn g()") != 1:
        failures.append(f"zig_c routed names: {text}")
    # A self-recursive function's port prompt says the self-call goes through the routed symbol.
    plan = pp.Plan(name="func_800E5EC0", unit="src/BATTLE/BATTLE.PRG/7CD68.c", stem="7CD68", address=0x800E5EC0,
                   difficulty="S", zig_name="func_800E5EC0", name_source="test", module="func_800E5EC0",
                   subsystem="battle_7CD68_port", signature="pub fn func_800E5EC0(arg0: c_int, arg1: c_int, "
                   "arg2: c_int) callconv(.c) void")
    if "The C calls itself" not in pp.port_prompt(plan, unit, views, table, BUILD):
        failures.append("port prompt of a self-recursive function: no note on the routed self-call")
    return failures


def review_7CD68(tmp, table):
    """The tooling fixes of the 7CD68 review (BAKEOFF.md, "7CD68 review")."""
    import port_pipeline as pp
    failures = []
    # A global pointer to integers the C indexes (`u_short* D_800F58C8`): many-item in the views.
    unit = pp.Unit("7CD68", "src/BATTLE/BATTLE.PRG/7CD68.c", BUILD, tmp / "unit-7CD68-review", table)
    views, _ = unit.views([], ["D_800F58C8"], tag="int-pointer")
    if 'pub inline fn D_800F58C8() *?[*]u16 { return psx.global(?[*]u16, "D_800F58C8"); }' not in views.read_text():
        failures.append("views: D_800F58C8 as a many-item u16 pointer")
    # The functions these cases plan, unrouted (the review ported them since).
    import ownership
    text = "\n".join(l for l in (ROOT / "game/ownership.toml").read_text().split("\n")
                     if 'name = "func_800E5698"' not in l and 'name = "func_800E5A9C"' not in l)
    table_path = tmp / "ownership-7CD68.toml"
    table_path.write_text(text)
    unrouted = ownership.load(table_path)
    # An unsized table's length in the spec prompt (D_800F1790: 10 function pointers, sized from battle_movement.o).
    rows, names = pp.functions_tsv(), pp.meanings()
    plan, unit5a9c = pp.plan_function("func_800E5A9C", rows["func_800E5A9C"], unrouted, names, {}, BUILD,
                                      tmp / "plan-5A9C")
    if plan.skip:
        failures.append(f"func_800E5A9C skipped: {plan.skip}")
    else:
        views5a9c, _ = unit5a9c.views(plan.types, plan.globals, tag="test-5A9C")
        if "- `D_800F1790`: 10 entries (80 bytes" not in pp.spec_prompt(plan, unit5a9c, views5a9c, unrouted, BUILD):
            failures.append("spec prompt of func_800E5A9C: no length for D_800F1790")
    # A compile-fix round after the retry: two drafts that do not build, the third passes.
    work = tmp / "compile-fix"
    shutil.copytree(CANNED / "func_8006B214", work / "func_8006B214")
    good = (work / "func_8006B214/port_reply1.md").read_text()
    broken = good.replace("0, 0x100000, 1);", "0, 0x100000, 1)")
    assert broken != good
    for k in (1, 2):
        (work / f"func_8006B214/port_reply{k}.md").write_text(broken)
    (work / "func_8006B214/port_reply3.md").write_text(good)
    run = pipeline(["func_8006B214", "--no-write"], work)
    r = record(work, "func_8006B214")
    rounds = r and [(a["round"], a["passed"]) for a in r["port"]["attempts"]]
    if not r or r["status"] != "passed (not written)" or rounds != [(1, False), (2, False), (3, True)]:
        failures.append(f"compile-fix round: {r and r['status']} {rounds} {run.stderr[-800:]}")
    work = tmp / "no-compile-fix"
    shutil.copytree(tmp / "compile-fix/func_8006B214", work / "func_8006B214",
                    ignore=shutil.ignore_patterns("port1*", "port2*", "port3*", "spec1*", "*.json"))
    pipeline(["func_8006B214", "--no-write", "--compile-fixes", "0"], work)
    r = record(work, "func_8006B214")
    if not r or r["status"] != "port-failed" or len(r["port"]["attempts"]) != 2:
        failures.append(f"--compile-fixes 0: {r and r['status']} {r and len(r['port']['attempts'])}")
    # A field of a null pointer handed to a stub (`&psx_get(NULL)->unk38`): the
    # real callee reads through it, so the C "crashes" (Haiku's spec had a
    # null class the stub hid; the port's `.?` was then the one to fail).
    work = tmp / "null-field"
    pipeline(["func_800E5698", "--no-write"], work, CANNED / "func_800E5698", table=table_path)
    r = record(work, "func_800E5698", "7CD68")
    errors = " ".join(r["spec"]["attempts"][0]["errors"]) if r else ""
    if "the C itself crashed" not in errors or "NULL+0x38" not in errors or "of the call of func_800E5568" not in errors:
        failures.append(f"a null field handed to a stub: {r and r['status']}: {errors[:1200]}")
    return failures


def wave1(tmp, table):
    """The tooling failures wave 1 found (BAKEOFF.md, "Wave 1")."""
    import zig_c
    import zig_toolchain
    failures = []
    # A union with bitfields (translate-c: opaque) passed by value in a prototype
    # (func_800DA738's callee func_800E1388): the integer of its size.
    unit_i = tmp / "byvalue.i"
    unit_i.write_text("typedef union { unsigned int raw; struct { unsigned int x : 8, y : 8, z : 8, s : 8; } p; } pos;\n"
                      "typedef union { unsigned long long raw; struct { unsigned int a : 3; } p; } wide;\n"
                      "typedef union { unsigned int r; struct { unsigned int a : 3; } p; } byref;\n"
                      "void f(int, pos, unsigned int);\nwide g(void);\nvoid h(byref*);\n")
    text = zig_c.translate(zig_toolchain.ensure(), unit_i, tmp / "c_byvalue.zig", "x86_64-linux-none").read_text()
    for want in ("pub const pos = u32;", "pub const wide = u64;", "pub const byref = opaque {"):
        if want not in text:
            failures.append(f"zig_c: an opaque by value: {want!r} missing")
    import port_pipeline as pp
    import ownership
    # A header's `void f();` before the unit's `void f(void)` (58578's func_800C16DC): f() is the prototype.
    unit_i = tmp / "proto.i"
    unit_i.write_text("void f();\nvoid g();\nvoid f(void) { g(); }\n")
    text = zig_c.translate(zig_toolchain.ensure(), unit_i, tmp / "c_proto.zig", "x86_64-linux-none").read_text()
    if "pub extern fn f() void;" not in text or "pub extern fn g(...) void;" not in text:
        failures.append("zig_c: a header's unprototyped declaration before the unit's (void) definition")
    # `T f();` before the unit's definition with parameters (44F14's func_800B101C): the prototype wins.
    unit_i = tmp / "proto2.i"
    unit_i.write_text("int h();\nint h(int* p) { return *p; }\n")
    text = zig_c.translate(zig_toolchain.ensure(), unit_i, tmp / "c_proto2.zig", "x86_64-linux-none").read_text()
    if "pub extern fn h(p: [*c]c_int) c_int;" not in text:
        failures.append("zig_c: a header's unprototyped declaration before the unit's (void) definition")
    # A global named as its type (573B8's vs_main_buttonsPressed): the accessor names the type through the module.
    unit = pp.Unit("573B8", "src/BATTLE/BATTLE.PRG/573B8.c", BUILD, tmp / "unit-573B8", table)
    views, _ = unit.views([], ["vs_main_buttonsPressed"], tag="type-named-global")
    want = ('pub inline fn vs_main_buttonsPressed() *views.vs_main_buttonsPressed { return '
            'psx.global(views.vs_main_buttonsPressed, "vs_main_buttonsPressed"); }')
    if want not in views.read_text() or "const views = @This();" not in views.read_text():
        failures.append("views: a global named as its type (vs_main_buttonsPressed)")
    # zdiff's class counts, a spec's class names holding spaces and `=` (58578's func_800C2B0C).
    # A C mutation copied from the .c with NULL, where the preprocessed body has 0 (573B8's func_800C05EC).
    import types
    import zig_diff
    text = zig_diff.Build.standin_source(types.SimpleNamespace(function="f"),
                                         "int f(int* p)\n{\n    if (p != 0) {\n        return 1;\n    }\n    return 0;\n}\n",
                                         {"name": "m", "from": "if (p != NULL) {", "to": "if (p != NULL && *p) {"})
    if "if (p != 0 && *p) {" not in text:
        failures.append(f"a C mutation spelling NULL: {text}")
    # The unit's static helpers a function calls go into its port prompt (6FC00's func_800D87E8, func_800D8EF4).
    helpers = pp.static_helpers(types.SimpleNamespace(unit="src/BATTLE/BATTLE.PRG/6FC00.c", name="func_800D8EF4"))
    if [h.split("(")[0].split()[-1] for h in helpers] != ["historyStalled", "historyRepeats"]:
        failures.append(f"static helpers of func_800D8EF4: {helpers}")
    # A block-scope static (44F14's func_800AE47C, a ROOD_PC log-once counter): skipped at the plan.
    rows, names = pp.functions_tsv(), pp.meanings()
    plan, _ = pp.plan_function("func_800AE47C", rows["func_800AE47C"], table, names, {}, BUILD, tmp / "plan-AE47C")
    if "block-scope static" not in plan.skip:
        failures.append(f"func_800AE47C not skipped for its block-scope static: {plan.skip!r}")
    # The stand-in copy sits right after the original's body (44F14's func_800AD714 calls
    # func_800AE46C(x) through `void func_800AE46C();`; its later (void) definition rejected a copy at the end).
    sub = ownership.Subsystem("battle_44F14_test", 0, "battle", "src/BATTLE/BATTLE.PRG/44F14.c", "original", [],
                              [(0x800AD714, "func_800AD714")], {}, {"func_800AD714": "x.func_800AD714"})
    try:
        b = zig_diff.Build("func_800AD714", {"spec": "none"}, BUILD, tmp / "standin-AD714", None,
                           route=(sub, "x.func_800AD714"))
        b.reference(standin=True)
    except Exception as error:
        failures.append(f"stand-in of func_800AD714 (calls through an unprototyped declaration): {str(error)[-600:]}")
    # A unit static the function calls (6F014's _loadfile) is kept in the reference, not a trap.
    sub = ownership.Subsystem("battle_6F014_test", 0, "battle", "src/BATTLE/BATTLE.PRG/6F014.c", "original", [],
                              [(0x800D7BF8, "func_800D7BF8")], {}, {"func_800D7BF8": "x.func_800D7BF8"})
    try:
        b = zig_diff.Build("func_800D7BF8", {"spec": "none"}, BUILD, tmp / "static-7BF8", None,
                           route=(sub, "x.func_800D7BF8"), views=BUILD / "zig/views_146C.zig",
                           cmod=zig_c.translate(zig_toolchain.ensure(), next(BUILD.glob("*_6F014.i")),
                                                tmp / "c_6F014.zig", "x86_64-linux-none"))
        _, traps, _ = b.symbols(b.reference(standin=True))
        if "_loadfile" in traps:
            failures.append("func_800D7BF8: the unit static _loadfile is still a trap")
    except Exception as error:
        failures.append(f"reference of func_800D7BF8 with its static callee: {str(error)[-600:]}")
    # ... and what that static reads is planned into the views (D_800F568C, read only by _loadfile).
    # (on a table without the unit's port subsystem: its views would already hold the global)
    text = re.sub(r"\[subsystem\.battle_6F014_port\].*?(?=\n\[subsystem\.(?!battle_6F014_port))", "",
                  (ROOT / "game/ownership.toml").read_text(), flags=re.S)
    (tmp / "ownership-7BF8.toml").write_text(text)
    plan, _ = pp.plan_function("func_800D7BF8", rows["func_800D7BF8"], ownership.load(tmp / "ownership-7BF8.toml"), names,
                               {}, BUILD, tmp / "plan-7BF8")
    if "D_800F568C" not in plan.globals:
        failures.append(f"func_800D7BF8's plan misses its static callee's global: {plan.globals}")
    got = pp.parse_classes(" row>=9=12 row<9 position[0] negative=3 plain=0")
    if got != {"row>=9": "12", "row<9 position[0] negative": "3", "plain": "0"}:
        failures.append(f"parse_classes: {got}")
    return failures


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
    # 7CD68 review: `struct T*` (translate-c's struct_T, opaque there when T
    # holds a bitfield union) is the views type of the tag.
    if pp.zig_type("[*c]struct_actorInitializationActor", {"actorInitializationActor"}, set(),
                   {"actorInitializationActor": "struct_actorInitializationActor"},
                   {"struct_actorInitializationActor"}) != "?*v.actorInitializationActor":
        failures.append("(A) zig_type: a struct_T pointer is the views type T")
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
    # Both at once (--jobs 2).
    work = tmp / "pilot"
    for name in PILOT:
        shutil.copytree(CANNED / name, work / name)
    r = pipeline([*PILOT, "--no-write", "--jobs", "2"], work, table=table_path)
    for name in PILOT:
        rec = record(work, name, "38C1C")
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
        # Best-of-N: the weak spec as the first sample, the good one as the second.
        work = tmp / "best-of-2"
        shutil.copytree(CANNED / "func_8006B214", work / "func_8006B214")
        (work / "func_8006B214/spec_reply1.s2.md").write_text((CANNED / "func_8006B214/spec_reply1.md").read_text())
        weak = (work / "func_8006B214/spec_reply1.md")
        weak.write_text(weak.read_text().replace("        0 => 0x100000,\n        1 => ctx.int(i32) | 0x100000,\n",
                                                 "        0, 1 => ctx.int(i32) & ~@as(i32, 0x100000),\n")
                   .replace("else => ctx.pick(i32, &.{ 0, 0x80000, 0x200000, -1 }),", "else => 0x80000,"))
        best = pipeline(["func_8006B214", "--no-write", "--attempts", "2"], work)
        r = record(work, "func_8006B214")
        verdicts = r and [(a["round"], a["sample"], a["passed"]) for a in r["spec"]["attempts"]]
        if not r or r["status"] != "passed (not written)" or verdicts != [(1, 1, False), (1, 2, True)] \
                or r["spec"].get("kept") != {"round": 1, "sample": 2} or pp.gate_outcome(r["spec"]) != "best-of-N" \
                or pp.gate_outcome(r["port"]) != "first":
            failures.append(f"best-of-2: {r and r['status']} {verdicts} {r and r['spec'].get('kept')} {best.stderr[-800:]}")
        failures += pilot_38C1C(tmp, rows, names)
        failures += client_checks(tmp)
        failures += head_to_head_7CD68(tmp, table)
        failures += review_7CD68(tmp, table)
        failures += wave1(tmp, table)
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
