#!/usr/bin/env python3
"""Can a cheap model write the zig_diff specs? (pc/docs/porting/battle/BAKEOFF.md,
"Spec bake-off"). For each bake-off function a model is asked for the spec
file (pc/tests/zig_diff/<spec>.zig: prepare, classes, classify, stubs) and
its specs.toml entry with 3-6 mutations of the accepted port; each reply is
then scored in a scratch directory (the checkout's specs are never written):

  (a) compiles: the harness builds with the model's spec and the reference port;
  (b) self-check: the reference port passes it with 0 mismatches, every class
      it declares occurs, and every one of ITS mutations is caught as declared
      (zig_diff.py --self-check, seed 1);
  (c) strength: of the reference spec's mutations (specs.toml, written with
      the reference ports), how many the model's spec catches (a mismatch, a
      panic or a crash in the mutated port's run);
  (d) sanity: the reference port passes the model's spec with 0 mismatches on
      seeds 1, 2, 3 (a spec that rejects the correct port is wrong; (c) is
      only counted when (d) holds).

    python3 pc/tools/spec_bakeoff.py --out DIR [--model M] [--budget 2.0] [--only FUNCTION]...
    python3 pc/tools/spec_bakeoff.py --out DIR --prompts-only     # the prompts, no calls
    python3 pc/tools/spec_bakeoff.py --out DIR --rescore          # score DIR's saved replies again

One attempt per function; when (a) or (b) fails, one retry that is fed only
the checker's own error output. The model is called through OpenRouter
(OPENROUTER_API_KEY from the environment; never printed), temperature 0,
stopping when the budget is spent. DIR gets the prompts, the replies, the
scratch builds and results.json; keep it out of the repository (pc/build is
ignored). Needs a 64-bit build (python3 pc/tools/build_boot.py --arch 64).
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys
import threading
import time
import tomllib
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import bakeoff_context as bc  # noqa: E402
import ownership  # noqa: E402
import prompt_privacy  # noqa: E402
import zig_diff  # noqa: E402

API_URL = "https://openrouter.ai/api/v1/chat/completions"
# The worked examples: never one of the eleven bake-off functions.
EXAMPLES = ["_updateEnemyKills", "func_800B07DC"]
HARNESS_DOC = """How the harness (pc/tests/zig_diff/main.zig) uses a spec, per iteration:
the state regions (the arena `zd.zdiff_arena`, every global either side
reaches, the PS1 scratchpad at 0x1F800000) are filled with random bytes; the
unit's own initialised data is restored; `prepare(ctx)` builds the arguments
(and may write globals and arena objects to bias the state toward edges: a
pointer the C dereferences must point at valid memory, an arena object from
`ctx.alloc`, or be null where the C tests it); `classify(args)` (if
`classes` is declared) returns the index of the input class, counted; the
state is saved; the C body runs; the state is restored; the Zig port runs;
then all state bytes, the external call traces (each call's arguments and a
hash of the state at that call) and the return values are compared.
External functions are recording stubs: a void stub does nothing, an int
stub returns `ctx.value(T)` (the same in both runs), a pointer stub returns
null; a spec may give one a behaviour with
`pub const stubs = struct { pub fn NAME(ctx: *zd.Ctx, <the callee's parameters, `anytype` allowed>) R { ... } };`
(an allocator handing out arena blocks, a callee that must return a valid
pointer, a result whose bits the C tests). A static of the unit the C calls
is a trap (a crash). Exit codes: 0 no mismatch, 1 mismatches, 3 the port
panicked (a Zig safety check), 4 a crash (a signal: a spec bug, or a port
fault). Every iteration must leave both runs crash-free: the C runs first,
so an input on which the C itself faults is a spec bug.

The spec module may import "std", "zdiff", "common" (`@import("common").psx`),
"{views}", "{cmod}" and "port" (the accepted port's file: `@import("port").{fn}`).
It must declare `pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").{fn}));`
and `pub fn prepare(ctx: *zd.Ctx) Args`; with `classes` (a list of names)
also `pub fn classify(args: Args) usize`, called after prepare (globals are
as prepare left them).

A mutation is a wrong variant of the port: `from` must occur exactly once
in the port file below (copy it character for character, indentation and
line breaks included), `to` replaces it, and the result must still compile
(an unused local or parameter is a compile error in Zig). `expect` is
"mismatch" (default), "panic" or "crash". Choose mistakes a porter could
really make (a wrong constant, a signed/unsigned slip, a dropped call or
store, a moved store across a call, a wrong field, a wrong bound), each one
that your inputs must catch."""


def section_19():
    text = bc.GUIDE.read_text()
    start = text.index("## 19.")
    return text[start:text.index("\n## 20.", start)].strip()


def toml_entry(name):
    """specs.toml's raw text for one function: its table and its mutations."""
    out, inside = [], False
    for line in (zig_diff.SPECS / "specs.toml").read_text().split("\n"):
        m = re.match(r"\[\[?function\.([\w.]+?)(?:\.mutation)?\]\]?$", line)
        if m:
            inside = m.group(1) == name
        elif line.startswith("# --"):
            inside = False
        if inside:
            out.append(line)
    return "\n".join(out).strip()


def route(table, name):
    sub, zig_path = zig_diff.find_route(table, name)
    module, fn = zig_path.split(".")
    return sub, ROOT / "game" / sub.overlay / f"{module}.zig", fn


def prompt(entry, table, specs, build):
    name = entry["name"]
    sub, port_path, fn = route(table, name)
    stem = pathlib.PurePosixPath(sub.unit).stem
    unit_i = next(build.glob(f"*_{stem}.i"))
    i_body = bc.c_body(unit_i.read_text(errors="replace"), name)
    c_source = bc.c_body((ROOT / sub.unit).read_text(errors="replace"), name)
    port = port_path.read_text()
    tokens = set(bc.IDENT.findall(i_body)) | set(bc.IDENT.findall(c_source or "")) | set(bc.IDENT.findall(port))
    spec_file = specs[name]["spec"]
    parts = [f"""# Write the zig_diff spec of `{name}` ({sub.unit})

A differential harness checks a Zig port against the original C body: both
run on the inputs a spec builds and every touched byte, external call and
return value must agree. The Zig port of `{name}` below is accepted
(correct). Write its spec: the file `pc/tests/zig_diff/{spec_file}` and its
`specs.toml` entry.

The spec must:
- build the inputs through the zdiff API (`zdiff.zig`, below): the arguments
  and every object and global the C reads, so that the C never faults;
- declare input classes (`classes` + `classify`) covering every branch of
  the C and every value edge it has: overflow and wrapping, sign (negative
  values, signed vs unsigned reads), zero, null where the C tests for it,
  out-of-range indices the C allows (an unchecked index into an array, as
  far as it stays inside memory the harness owns); every class must occur
  in the iterations;
- come with 3-6 mutations of the port (TOML entries in specs.toml's format)
  that your inputs catch, each a mistake a porter could make.

Reply with exactly two fenced blocks: first ```zig (the whole spec file,
starting with a `//! zig_diff spec: {name} -> {port_path.stem}.{fn} ({pathlib.PurePosixPath(sub.unit).name}).`
line and a short `//!` paragraph saying what the inputs cover), then ```toml
(the `[function.{name}]` table with `spec = "{spec_file}"`, `iterations`, and
its `[[function.{name}.mutation]]` entries). Nothing else.""",
             HARNESS_DOC.replace("{views}", f"views_{stem}").replace("{cmod}", f"c_{stem}").replace("{fn}", fn),
             "## The porting guide's reviewer checklist (pc/docs/porting/PORTING.md §19)\n\n" + section_19(),
             "## The API (pc/tests/zig_diff/zdiff.zig)\n\n```zig\n" + zig_diff.API.read_text().strip() + "\n```"]
    for example in EXAMPLES:
        esub, eport, efn = route(table, example)
        parts.append(f"## Example: `{example}` ({esub.unit}) -> `{eport.stem}.{efn}`\n\nIts port "
                     f"(`game/{esub.overlay}/{eport.name}`):\n\n```zig\n{eport.read_text().strip()}\n```\n\n"
                     f"Its spec (`pc/tests/zig_diff/{specs[example]['spec']}`):\n\n```zig\n"
                     + (zig_diff.SPECS / specs[example]["spec"]).read_text().strip()
                     + "\n```\n\nIts specs.toml entry:\n\n```toml\n" + toml_entry(example) + "\n```")
    parts.append(f"## The C (as GCC compiled it: the routed `.i`)\n\n```c\n{i_body}\n```")
    if c_source and re.sub(r"\s+", " ", c_source) != re.sub(r"\s+", " ", i_body):
        parts.append(f"The source (`{sub.unit}`, for names and comments):\n\n```c\n{c_source}\n```")
    if entry.get("asm"):
        asm = bc.disassembly(unit_i.with_suffix(".o"), name)
        if asm:
            parts.append(f"The game's 64-bit object for it (`objdump -dr`):\n\n```\n{asm}\n```")
    parts.append(f"## The accepted port (`game/{sub.overlay}/{port_path.name}`, imported as \"port\"; "
                 f"the mutations edit this text)\n\n```zig\n{port.strip()}\n```")
    parts.append(f"## `views_{stem}` (the declarations it touches)\n\n```zig\n"
                 + bc.views_subset((build / "zig" / f"views_{stem}.zig").read_text(), tokens) + "\n```")
    parts.append(f"## `c_{stem}` (the declarations it touches)\n\n```zig\n"
                 + bc.c_subset((build / "zig" / f"c_{stem}.zig").read_text(), tokens, name) + "\n```")
    parts.append("## `common.psx` (game/common/psx.zig, public API)\n\n```zig\n" + bc.psx_api() + "\n```")
    return "\n\n".join(parts) + "\n"


# ---------------------------------------------------------------- the model
class Budget(Exception):
    pass


def call_model(messages, model, max_tokens):
    # Every message goes through the one sanitiser, and is refused if anything local is left.
    messages = [dict(m, content=prompt_privacy.check(prompt_privacy.sanitise(m["content"]))) for m in messages]
    key = os.environ.get("OPENROUTER_API_KEY")
    if not key:
        raise SystemExit("spec_bakeoff: OPENROUTER_API_KEY is not set")
    body = json.dumps({"model": model, "messages": messages, "temperature": 0, "max_tokens": max_tokens,
                       "usage": {"include": True}}).encode()
    request = urllib.request.Request(API_URL, data=body, headers={
        "Authorization": "Bearer " + key, "Content-Type": "application/json",
        "X-Title": "rood-boi spec bake-off"})
    start = time.monotonic()
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=600) as response:
                data = json.loads(response.read())
            break
        except urllib.error.HTTPError as error:
            detail = error.read().decode(errors="replace")[:500]
            if error.code in (429, 500, 502, 503) and attempt < 2:
                time.sleep(10 * (attempt + 1))
                continue
            raise SystemExit(f"spec_bakeoff: OpenRouter HTTP {error.code}: {detail}")
    if "choices" not in data:
        raise SystemExit(f"spec_bakeoff: OpenRouter: {json.dumps(data)[:500]}")
    usage = data.get("usage") or {}
    return {
        "text": data["choices"][0]["message"].get("content") or "",
        "finish_reason": data["choices"][0].get("finish_reason"),
        "model": data.get("model"),
        "prompt_tokens": usage.get("prompt_tokens"),
        "completion_tokens": usage.get("completion_tokens"),
        "reasoning_tokens": (usage.get("completion_tokens_details") or {}).get("reasoning_tokens"),
        "cost": usage.get("cost") or 0.0,
        "seconds": round(time.monotonic() - start, 1),
    }


def split_reply(text):
    """The reply's ```zig and ```toml blocks (None when missing)."""
    zig = re.search(r"```zig\s*\n(.*?)\n```", text, re.S)
    toml = re.search(r"```toml\s*\n(.*?)\n```", text, re.S)
    return (zig.group(1) + "\n" if zig else None), (toml.group(1) + "\n" if toml else None)


# ---------------------------------------------------------------- scoring
def run_exe(exe, iterations, seed, timeout):
    try:
        r = subprocess.run(["nice", "-n", "10", str(exe), str(iterations), str(seed)], capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, f"no result after {timeout} s (an endless loop?)"
    return r.returncode, r.stdout + r.stderr


def first_line(text):
    return next((l.removeprefix("zdiff: ") for l in text.splitlines()
                 if "MISMATCH" in l or "PANIC" in l or "CRASH" in l or "UNEXPECTED" in l), text.strip()[-300:])


def caught(code, text, expect):
    return {"mismatch": code == 1 and "MISMATCH" in text, "panic": code == 3 and "PANIC" in text,
            "crash": code == 4 and ("CRASH" in text or "UNEXPECTED" in text)}.get(expect, False)


def detected(code, text):
    """The mutated port's run was told apart in any way."""
    for kind in ("mismatch", "panic", "crash"):
        if caught(code, text, kind):
            return kind
    return None


def score(name, spec_path, mutations, ref_spec, table, build_dir, work, timeout, ref_mutations=True):
    """(a)-(d) for one spec file; `errors` is the checker output a retry sees."""
    iterations = ref_spec.get("iterations", 20000)
    out = {"compiles": False, "compile_error": None, "sanity": None, "self_check": None, "classes": {},
           "mutations": [], "strength": None, "errors": []}
    work.mkdir(parents=True, exist_ok=True)
    try:
        build = zig_diff.Build(name, {"spec": str(spec_path)}, build_dir, work, table)
        exe, _ = build.compile()
    except zig_diff.Failure as error:
        message = str(error).replace(str(work) + "/", "")
        out["compile_error"] = message[-4000:]
        out["errors"].append("FAIL: the harness does not build:\n" + message[-4000:])
        return out
    out["compiles"] = True
    # (d) the reference port on seeds 1-3; seed 1 is also the self-check's run.
    runs = []
    for seed in (1, 2, 3):
        code, text = run_exe(exe, iterations, seed, timeout)
        summary = re.search(r"(\d+) iterations, seed \d+, (\d+) mismatches", text or "")
        runs.append({"seed": seed, "exit": code, "mismatches": int(summary.group(2)) if summary else None,
                     "first": None if code == 0 else first_line(text)})
        if seed == 1:
            classes = re.search(r"classes:(.*)", text)
            out["classes"] = dict(c.split("=") for c in classes.group(1).split()) if classes else {}
            if code != 0:
                out["errors"].append(f"FAIL: the accepted port differs from the C under this spec (exit {code}):\n"
                                     + "\n".join(l for l in text.splitlines() if l.startswith("zdiff:"))[-1500:])
    out["sanity"] = {"passed": all(r["exit"] == 0 for r in runs), "runs": runs}
    empty = [c for c, n in out["classes"].items() if n == "0"]
    if empty:
        out["errors"].append("FAIL: input classes never generated: " + ", ".join(empty))
    # (b) the model's own mutations, as zig_diff.py --self-check judges them.
    for m in mutations:
        expect = m.get("expect", "mismatch")
        record = {"name": m.get("name"), "expect": expect, "caught": False, "first": None}
        try:
            if not isinstance(m.get("from"), str) or not isinstance(m.get("to"), str):
                raise zig_diff.Failure("needs string `from` and `to`")
            mexe, _ = build.compile(m)
            code, text = run_exe(mexe, iterations, 1, timeout)
            record["caught"] = code is not None and caught(code, text, expect)
            record["first"] = first_line(text)
            if not record["caught"]:
                out["errors"].append(f"FAIL: mutation {m.get('name')}: not caught as {expect} (exit {code}): "
                                     + text.strip()[-400:])
        except zig_diff.Failure as error:
            record["first"] = str(error).replace(str(work) + "/", "")[-1500:]
            out["errors"].append(f"FAIL: mutation {m.get('name')}: {record['first']}")
        out["mutations"].append(record)
    out["self_check"] = (out["sanity"]["runs"][0]["exit"] == 0 and not empty
                         and bool(mutations) and all(r["caught"] for r in out["mutations"]))
    # (c) the reference spec's mutations, only when the reference port passes.
    if ref_mutations:
        results = []
        for m in ref_spec.get("mutation", []):
            try:
                mexe, _ = build.compile(m)
                code, text = run_exe(mexe, iterations, 1, timeout)
                kind = detected(code, text) if code is not None else None
                results.append({"name": m["name"], "expect": m.get("expect", "mismatch"), "caught_as": kind,
                                "first": first_line(text) if kind else None})
            except zig_diff.Failure as error:
                results.append({"name": m["name"], "expect": m.get("expect", "mismatch"), "caught_as": None,
                                "first": "build: " + str(error)[-300:]})
        out["strength"] = {"caught": sum(r["caught_as"] is not None for r in results), "of": len(results),
                           "counted": out["sanity"]["passed"], "mutations": results}
    return out


def parse_toml(name, text):
    if text is None:
        return None, "FAIL: no ```toml block in the reply"
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        return None, f"FAIL: specs.toml entry: {error}"
    entry = data.get("function", {}).get(name)
    if entry is None:
        return None, f"FAIL: specs.toml entry: no [function.{name}] table"
    return entry, None


def evaluate(name, reply_text, ref_spec, table, args, work):
    zig_text, toml_text = split_reply(reply_text)
    entry, toml_error = parse_toml(name, toml_text)
    result = {"spec_present": zig_text is not None, "toml_error": toml_error,
              "model_iterations": entry.get("iterations") if entry else None}
    if zig_text is None:
        result.update(compiles=False, errors=["FAIL: no ```zig block in the reply"], self_check=False)
        return result
    spec_path = work / "model_spec.zig"
    work.mkdir(parents=True, exist_ok=True)
    spec_path.write_text(zig_text)
    mutations = entry.get("mutation", []) if entry else []
    result.update(score(name, spec_path, mutations, ref_spec, table, args.build, work / "build", args.timeout))
    if toml_error:
        result["errors"].insert(0, toml_error)
        result["self_check"] = False
    return result


def passed_checker(r):
    return r.get("compiles") and r.get("self_check") and not r.get("toml_error")


def function_run(entry, ref_spec, table, args, state):
    name = entry["name"]
    fdir = args.out / name
    fdir.mkdir(parents=True, exist_ok=True)
    out = {"function": name, "difficulty": entry["difficulty"], "attempts": []}
    text = (fdir / "prompt.md").read_text()
    messages = [{"role": "user", "content": text}]
    for attempt in (1, 2):
        reply_path = fdir / f"reply{attempt}.md"
        call = None
        if not reply_path.exists():
            if args.rescore:
                break
            with state["lock"]:
                if state["cost"] >= args.budget:
                    out["stopped"] = "budget"
                    break
            call = call_model(messages, args.model, args.max_tokens)
            reply_path.write_text(call["text"])
            (fdir / f"call{attempt}.json").write_text(json.dumps({k: v for k, v in call.items() if k != "text"}, indent=2))
            with state["lock"]:
                state["cost"] += call["cost"]
                print(f"spec_bakeoff: {name} attempt {attempt}: {call['prompt_tokens']} in, "
                      f"{call['completion_tokens']} out, ${call['cost']:.4f}, {call['seconds']} s "
                      f"(total ${state['cost']:.4f})", file=sys.stderr)
        elif (fdir / f"call{attempt}.json").exists():
            call = json.loads((fdir / f"call{attempt}.json").read_text())
        reply = reply_path.read_text()
        result = evaluate(name, reply, ref_spec, table, args, fdir / f"work{attempt}")
        result["call"] = call
        out["attempts"].append(result)
        if passed_checker(result) or attempt == 2:
            break
        feedback = "zig_diff.py --self-check output for your spec (fix the spec and the entry; reply with the two " \
                   "blocks again):\n\n" + "\n\n".join(result["errors"])
        feedback = prompt_privacy.sanitise(feedback)  # no local paths
        messages += [{"role": "assistant", "content": reply}, {"role": "user", "content": feedback}]
    return out


def summarize(results):
    rows, total = [], {"cost": 0.0, "seconds": 0.0, "prompt_tokens": 0, "completion_tokens": 0, "calls": 0}
    for f in results:
        for a in f["attempts"]:
            c = a.get("call") or {}
            total["cost"] += c.get("cost") or 0.0
            total["seconds"] += c.get("seconds") or 0.0
            total["prompt_tokens"] += c.get("prompt_tokens") or 0
            total["completion_tokens"] += c.get("completion_tokens") or 0
            total["calls"] += bool(c)
        final = f["attempts"][-1] if f["attempts"] else {}
        st = final.get("strength") or {}
        f["final"] = {
            "attempts": len(f["attempts"]),
            "compiles": bool(final.get("compiles")),
            "self_check": bool(final.get("self_check")),
            "own_mutations": f"{sum(m['caught'] for m in final.get('mutations', []))}/{len(final.get('mutations', []))}",
            "strength": f"{st.get('caught', 0)}/{st.get('of', 0)}" if st else "-",
            "strength_counted": bool(st.get("counted")),
            "reference_passes": bool((final.get("sanity") or {}).get("passed")),
            "first_try": bool(f["attempts"]) and passed_checker(f["attempts"][0]),
        }
        rows.append(f)
    counted = [f for f in rows if f["final"]["strength_counted"]]
    summary = {
        "functions": len(rows),
        "compiles": sum(f["final"]["compiles"] for f in rows),
        "self_check": sum(f["final"]["self_check"] for f in rows),
        "self_check_first_try": sum(f["final"]["first_try"] for f in rows),
        "reference_passes": sum(f["final"]["reference_passes"] for f in rows),
        "strength_caught": sum(int(f["final"]["strength"].split("/")[0]) for f in counted),
        "strength_of": sum(int(f["final"]["strength"].split("/")[1]) for f in rows if f["final"]["strength"] != "-"),
        **{k: round(v, 4) if isinstance(v, float) else v for k, v in total.items()},
    }
    return summary


def table_text(results, summary):
    lines = ["| Function | Tries | Compiles | Self-check (own mutations) | Strength (reference mutations) | Reference port passes |",
             "|---|---|---|---|---|---|"]
    for f in results:
        x = f["final"]
        strength = x["strength"] + ("" if x["strength_counted"] or x["strength"] == "-" else " (void)")
        lines.append(f"| `{f['function']}` | {x['attempts']} | {'yes' if x['compiles'] else 'no'} | "
                     f"{'pass' if x['self_check'] else 'FAIL'} ({x['own_mutations']}) | {strength} | "
                     f"{'yes' if x['reference_passes'] else 'no'} |")
    lines.append(f"\n{summary['compiles']}/{summary['functions']} compile, {summary['self_check']} pass the self-check "
                 f"({summary['self_check_first_try']} first try), {summary['reference_passes']} accept the reference "
                 f"port; strength {summary['strength_caught']}/{summary['strength_of']} reference mutations; "
                 f"{summary['calls']} calls, {summary['prompt_tokens']} + {summary['completion_tokens']} tokens, "
                 f"${summary['cost']:.4f}, {summary['seconds']:.0f} s of model time")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=pathlib.Path, required=True, help="prompts, replies, scratch builds, results.json")
    parser.add_argument("--model", default="anthropic/claude-haiku-5.5")
    parser.add_argument("--budget", type=float, default=2.0, help="US$: no new call once spent")
    parser.add_argument("--max-tokens", type=int, default=64000,
                        help="per call, reasoning included (16000 truncated the larger functions' replies)")
    parser.add_argument("--only", action="append")
    parser.add_argument("--prompts-only", action="store_true")
    parser.add_argument("--rescore", action="store_true", help="score the saved replies, make no calls")
    parser.add_argument("--timeout", type=int, default=120, help="seconds per harness run")
    parser.add_argument("-j", "--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument("--build", type=pathlib.Path, default=ROOT / "pc/build/boot64")
    args = parser.parse_args(argv)
    if not (args.build / "imports.ld").exists():
        print(f"spec_bakeoff: {args.build} has no 64-bit build (python3 pc/tools/build_boot.py --arch 64)", file=sys.stderr)
        return 77
    args.out = args.out.resolve()
    entries = bc.bakeoff_functions()
    if args.only:
        entries = [bc.find(entries, k) for k in args.only]
    specs = tomllib.loads((zig_diff.SPECS / "specs.toml").read_text())["function"]
    table = ownership.load()
    for e in entries:
        fdir = args.out / e["name"]
        fdir.mkdir(parents=True, exist_ok=True)
        text = prompt_privacy.sanitise(prompt(e, table, specs, args.build))
        (fdir / "prompt.md").write_text(text)
        print(f"spec_bakeoff: {e['name']:28} prompt {len(text):7} chars", file=sys.stderr)
    if args.prompts_only:
        return 0
    # The budget counts the calls already saved in DIR (a resumed run).
    spent = sum(json.loads(p.read_text()).get("cost") or 0.0 for p in args.out.glob("*/call*.json"))
    state = {"cost": spent, "lock": threading.Lock()}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda e: function_run(e, specs[e["name"]], table, args, state), entries))
    summary = summarize(results)
    summary.update(model=args.model, budget=args.budget)
    (args.out / "results.json").write_text(json.dumps({"summary": summary, "functions": results}, indent=2) + "\n")
    text = table_text(results, summary)
    (args.out / "results.md").write_text(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
