#!/usr/bin/env python3
"""The port pipeline in rounds (port_pipeline.py --mode rounds): every
function's next model request goes out together, as one batch, and the
replies come back late (OpenRouter's Batch API: half the price, minutes to
hours), so a function is a state machine kept on disk instead of a loop of
calls:

    needs-spec -> spec-submitted -> spec-gate -> (needs-spec-retry -> ...) ->
    needs-port -> port-submitted -> port-gate -> (needs-port-retry -> ...) ->
    write -> done (status: ported, passed (not written), spec-failed,
    port-failed, views-conflict, stopped)

    python3 pc/tools/port_pipeline.py ACTION --mode rounds --units 7CD68 38C1C [options]

ACTION:
    submit   every function's pending request (needs-*), in one batch
    collect  the finished batches' replies; the local gates (zig_diff, in
             parallel, nice 10); each function advanced (its next request
             queued for the next submit); the tree written for what passed
    status   a summary (stages, batches, spend)
    run      collect/submit/poll until every function is done, the budget is
             spent or --until HH:MM (then it parks: submitted batches keep
             running at the provider and are collected by the next run)

The run's state lives in --work/rounds/<run>/ (default run name: the units
joined by '+'): run.json (settings, batches) and functions/<NAME>.json (stage,
round, the conversation so far, the pending request, the record in the
synchronous pipeline's shape). Every change is written atomically before
the next step, so a run can be killed at any point and resumed: a reply is
applied once (a function only takes the result of the request it is waiting
for), a gate that was cut short runs again, spending is the sum of the
recorded calls. The settings (model, endpoint, reasoning budget, max
tokens, transport, seeds, --no-write, --compile-fixes) are fixed when the run
is created; --budget, --reserve, --until, --poll, --check-jobs and --max-calls
apply per invocation.

Replies are saved where the synchronous pipeline saves them (--work/<NAME>/
spec_reply1.md and its .json, with the conversation's key), so either mode can
pick up the other's replies.

Transports (--transport): `batch` (default for a remote endpoint) posts the
requests to POST /batches and polls GET /batches/{id} (results only when the
whole batch is final; items can fail one by one); `sync` (the default for a
local endpoint) asks every request of the round at once through the normal
chat endpoint, at most --max-calls in flight. A failed item is asked again
next round; its second failure stops the function.
"""
import concurrent.futures
import contextlib
import dataclasses
import datetime
import fcntl
import json
import os
import pathlib
import re
import sys
import threading
import time

import model_client
import port_pipeline as pp
import prompt_privacy

ACTIONS = ("submit", "collect", "status", "run")
TERMINAL = "done"
ITEM_RETRIES = 2  # a request whose result failed this many times stops its function
MAX_BATCH = 5000  # OpenRouter's limit per batch
# Fixed when the run is created (the conversation keys and the gates depend on them).
SETTINGS = ("model", "base_url", "api_key_env", "thinking", "temperature", "reasoning_budget", "reasoning_effort",
            "max_tokens",
            "transport", "seeds", "no_write", "compile_fixes", "units", "functions", "table", "build")


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text)
    os.replace(tmp, path)


def die_for_test(point):
    """The tests' kill switch: PORT_ROUNDS_DIE=<point>[:<n>] ends the process
    abruptly the n-th time (default 1st) it reaches `point`."""
    spec = os.environ.get("PORT_ROUNDS_DIE", "")
    if not spec or spec.split(":")[0] != point:
        return
    count = die_for_test.counts.get(point, 0) + 1
    die_for_test.counts[point] = count
    if count >= int(spec.split(":")[1] if ":" in spec else 1):
        print(f"port_rounds: killed at {point} (test)", file=sys.stderr, flush=True)
        os._exit(137)


die_for_test.counts = {}


# ---------------------------------------------------------------- the run on disk
class Run:
    def __init__(self, directory):
        self.dir = directory
        self.functions_dir = directory / "functions"
        self.batches_dir = directory / "batches"
        self.meta = json.loads((directory / "run.json").read_text()) if (directory / "run.json").exists() else None
        self.fns = {}
        if self.functions_dir.exists():
            for p in sorted(self.functions_dir.glob("*.json")):
                f = json.loads(p.read_text())
                self.fns[f["name"]] = f
        self.lock = threading.Lock()
        self._lockfile = None

    def acquire(self):
        """One process per run at a time."""
        self.dir.mkdir(parents=True, exist_ok=True)
        self._lockfile = open(self.dir / "lock", "w")
        try:
            fcntl.flock(self._lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit(f"port_rounds: another process is working on {self.dir.name}") from None

    def save(self):
        with self.lock:
            atomic_write(self.dir / "run.json", json.dumps(self.meta, indent=1) + "\n")

    def save_fn(self, f):
        with self.lock:
            atomic_write(self.functions_dir / f"{f['name']}.json", json.dumps(f, indent=1) + "\n")

    def spent(self):
        """The recorded calls' cost: the judged ones and the replies waiting for their gate."""
        judged = sum(a["call"].get("cost") or 0.0 for f in self.fns.values() for k in ("spec", "port")
                     for a in f["record"][k]["attempts"] if a.get("call"))
        return judged + sum(f["reply"]["call"].get("cost") or 0.0 for f in self.fns.values() if f.get("reply"))

    def outstanding(self):
        return [f for f in self.fns.values() if f["stage"].endswith("-submitted")]


def stage_needs(kind, round_):
    return f"needs-{kind}" if round_ == 1 else f"needs-{kind}-retry"


def custom_id(f):
    """The id of a function's next request: unique per function, step, round and try (a failed item's next)."""
    return f"{f['name']}-{f['kind']}{f['round']}-{f.get('tries', 0) + 1}"


def run_name(args):
    if args.run_name:
        return args.run_name
    if args.units:
        return "+".join(args.units)
    return "+".join(args.functions[:3]) + (f"+{len(args.functions) - 3}" if len(args.functions) > 3 else "")


def endpoint_of(meta):
    return model_client.Endpoint(base_url=meta["base_url"], api_key_env=meta["api_key_env"], thinking=meta["thinking"],
                                 temperature=meta["temperature"], reasoning_budget=meta["reasoning_budget"],
                                 reasoning_effort=meta.get("reasoning_effort"))


def apply_settings(args, meta):
    """The run's fixed settings onto args (the gates and writers read args)."""
    for key in SETTINGS:
        setattr(args, key, meta[key])
    args.build = pathlib.Path(meta["build"])
    args.table = pathlib.Path(meta["table"]) if meta["table"] else None
    args.attempts = 1


# ---------------------------------------------------------------- creating a run
def create(run, args, endpoint):
    rows, names = pp.functions_tsv(), pp.meanings()
    table = pp.ownership.load(args.table or pp.OWNERSHIP)
    if args.units:
        selected = []
        for unit in args.units:
            args.unit = unit
            selected += [n for n in pp.select(args, rows) if n not in selected]
    else:
        selected = list(args.functions)
        missing = [f for f in selected if f not in rows]
        if missing:
            raise SystemExit(f"port_rounds: not in FUNCTIONS.tsv: {', '.join(missing)}")
    transport = args.transport or ("sync" if endpoint.local else "batch")
    run.meta = {"created": time.time(), "model": args.model, "base_url": endpoint.base_url,
                "api_key_env": endpoint.api_key_env, "thinking": endpoint.thinking,
                "temperature": endpoint.temperature, "reasoning_budget": endpoint.reasoning_budget,
                "reasoning_effort": endpoint.reasoning_effort,
                "max_tokens": args.max_tokens, "transport": transport, "seeds": args.seeds, "no_write": args.no_write,
                "compile_fixes": args.compile_fixes, "units": args.units or [], "functions": args.functions or [],
                "table": str(args.table) if args.table else "", "build": str(args.build), "batches": {}}
    units = {}
    args.attempts = 1
    for name in selected:
        stem = pathlib.PurePosixPath(rows[name]["file"]).stem
        record_path = args.records / stem / f"{name}.json"
        if record_path.exists() and not args.force and json.loads(record_path.read_text()).get("status") == "ported":
            print(f"port_rounds: {name}: already ported (record): skipped", file=sys.stderr)
            continue
        plan, unit = pp.plan_function(name, rows[name], table, names, units, args.build, args.work)
        if plan.skip:
            print(f"port_rounds: {name}: skipped: {plan.skip}", file=sys.stderr)
            continue
        record = {}
        _, spec_text, _ = pp.begin(plan, unit, table, args, endpoint, record)
        record.update({"mode": "rounds", "transport": transport})
        for key in ("reasoning_budget", "reasoning_effort"):
            if getattr(endpoint, key) is not None:
                record[key] = getattr(endpoint, key)
        f = {"name": name, "stem": stem, "plan": dataclasses.asdict(plan), "stage": "needs-spec", "kind": "spec",
             "round": 1, "messages": [{"role": "user", "content": spec_text}], "pending": None, "reply": None,
             "submissions": 0, "item_failures": 0, "entry": None, "spec_path": None, "port_path": None,
             "record": record, "events": []}
        run.fns[name] = f
        run.save_fn(f)
    run.save()
    print(f"port_rounds: run {run.dir.name}: {len(run.fns)} function(s), transport {transport}", file=sys.stderr)


def reconcile(run):
    """A process killed between recording a batch and its functions: the
    functions still at needs-* whose next request is in an uncollected batch
    are waiting for it."""
    for bid, b in run.meta["batches"].items():
        if b.get("collected"):
            continue
        for cid, name in b["ids"].items():
            f = run.fns.get(name)
            if f and f["stage"].startswith("needs-") and cid == custom_id(f):
                f["pending"] = {"custom_id": cid, "batch": bid, "max_tokens": b.get("max_tokens")}
                f["submissions"] += 1
                f["tries"] = f.get("tries", 0) + 1
                f["stage"] = f"{f['kind']}-submitted"
                run.save_fn(f)


# ---------------------------------------------------------------- replies
def reply_files(args, f):
    path = pp.reply_path(args.work / f["name"], f["kind"], f["round"], 1)
    return path, path.with_suffix(".json")


def conversation(f, meta, endpoint):
    return pp.conversation_key(f["messages"], meta["model"], dict(endpoint.key_fields()))


def saved_reply(args, f, meta, endpoint):
    """A reply already saved for this very conversation (a crash after it was
    written, or the synchronous pipeline's): its call record, else None."""
    path, meta_path = reply_files(args, f)
    if path.exists() and meta_path.exists():
        saved = json.loads(meta_path.read_text())
        if saved.get("conversation") == conversation(f, meta, endpoint):
            return saved
    return None


def store_reply(args, f, meta, endpoint, text, call):
    path, meta_path = reply_files(args, f)
    path.parent.mkdir(parents=True, exist_ok=True)
    key = conversation(f, meta, endpoint)
    if path.exists() and meta_path.exists() and json.loads(meta_path.read_text()).get("conversation") != key:
        n = 1
        while path.with_name(f"{path.stem}.stale{n}.md").exists():
            n += 1
        path.rename(path.with_name(f"{path.stem}.stale{n}.md"))
        meta_path.rename(path.with_name(f"{path.stem}.stale{n}.json"))
    record = dict(call, sample=1, conversation=key)
    path.write_text(text)
    atomic_write(meta_path, json.dumps(record, indent=1))
    return record


def take_reply(run, f, call):
    """The function has its reply: to the gate."""
    f["reply"] = {"call": {k: v for k, v in call.items() if k != "conversation"}}
    f["pending"] = None
    f["stage"] = f"{f['kind']}-gate"
    run.save_fn(f)


def item_failed(run, f, why):
    print(f"port_rounds: {f['name']}: {f['kind']} {f['round']}: request failed: {why[:200]}", file=sys.stderr)
    f["pending"] = None
    f["item_failures"] += 1
    f["events"].append({"time": time.time(), "failed": prompt_privacy.sanitise(why)[:500]})
    if f.get("tries", 0) >= ITEM_RETRIES:
        finish(run, f, "stopped", f"the {f['kind']} {f['round']} request failed {f['tries']} times: {why}"[:600])
    else:
        f["stage"] = stage_needs(f["kind"], f["round"])
        run.save_fn(f)


# ---------------------------------------------------------------- submit
def submit(run, args, endpoint):
    """Every function's next request, in one batch (or, sync, all at once); the number sent."""
    meta = run.meta
    reconcile(run)
    todo = [f for f in run.fns.values() if f["stage"].startswith("needs-")]
    for f in list(todo):
        saved = saved_reply(args, f, meta, endpoint)
        if saved:  # answered already (a crash after the reply was saved, or the other mode)
            take_reply(run, f, saved)
            todo.remove(f)
    if not todo:
        return 0
    spent, outstanding = run.spent(), len(run.outstanding())
    room = int((args.budget - spent) / args.reserve) - outstanding if args.reserve > 0 else len(todo)
    if room < len(todo):
        print(f"port_rounds: budget: ${spent:.4f} spent of ${args.budget:.2f}, {outstanding} request(s) out: "
              f"{max(room, 0)} of {len(todo)} sent", file=sys.stderr)
        todo = todo[:max(room, 0)]
    requests = []
    for f in todo:
        try:
            body = model_client.request_body(f["messages"], meta["model"], meta["max_tokens"], endpoint)
        except prompt_privacy.Leak as error:
            finish(run, f, "stopped", str(error))
            continue
        except model_client.ContextError as error:
            finish(run, f, "context", f"context: {error}")
            continue
        requests.append((custom_id(f), f, body))
    sent = 0
    for start in range(0, len(requests), MAX_BATCH):
        chunk = requests[start:start + MAX_BATCH]
        if meta["transport"] == "batch":
            try:
                obj = model_client.submit_batch([(cid, body) for cid, _, body in chunk], meta["model"], endpoint)
            except model_client.ModelError as error:
                print(f"port_rounds: submit: {error}", file=sys.stderr)
                return sent
            bid = obj["id"]
            status = obj.get("status")
        else:
            bid = f"sync-{int(time.time() * 1000)}"
            status = "in_progress"
        die_for_test("after-post")
        meta["batches"][bid] = {"transport": meta["transport"], "submitted": time.time(), "status": status,
                                "ids": {cid: f["name"] for cid, f, _ in chunk},
                                "max_tokens": chunk[0][2]["max_tokens"], "collected": False}
        run.save()
        die_for_test("after-batch-saved")
        for cid, f, body in chunk:
            f["pending"] = {"custom_id": cid, "batch": bid, "max_tokens": body["max_tokens"]}
            f["submissions"] += 1
            f["tries"] = f.get("tries", 0) + 1
            f["stage"] = f"{f['kind']}-submitted"
            run.save_fn(f)
        sent += len(chunk)
        print(f"port_rounds: submitted {len(chunk)} request(s) as {bid} ({meta['transport']})", file=sys.stderr)
        if meta["transport"] == "sync":
            ask_sync(run, args, endpoint, bid, chunk)
    return sent


def ask_sync(run, args, endpoint, bid, chunk):
    """The sync transport: each request through the chat endpoint, at most
    --max-calls at once, each result saved as it arrives (a kill loses only
    the calls in flight: they fail as items and are asked again)."""
    path = run.batches_dir / f"{bid}.json"
    results, lock = {}, threading.Lock()

    def one(item):
        cid, f, _ = item
        try:
            call = model_client.call(f["messages"], run.meta["model"], run.meta["max_tokens"], endpoint)
            result = {"custom_id": cid, "record": call}
        except model_client.ModelError as error:
            result = {"custom_id": cid, "error": {"message": str(error)}}
        with lock:
            results[cid] = result
            atomic_write(path, json.dumps({"id": bid, "status": "in_progress", "results": list(results.values())}))
        die_for_test("sync-result")

    with concurrent.futures.ThreadPoolExecutor(max(1, args.max_calls)) as pool:
        list(pool.map(one, chunk))
    atomic_write(path, json.dumps({"id": bid, "status": "completed", "results": list(results.values())}))
    run.meta["batches"][bid]["status"] = "completed"
    run.save()


# ---------------------------------------------------------------- collect
def fetch(run, bid, b, endpoint):
    """A batch's final object (saved once final), or None while it runs."""
    path = run.batches_dir / f"{bid}.json"
    if b["transport"] == "sync":
        if not path.exists():
            return {"id": bid, "status": "failed", "results": [], "error": {"message": "no results saved"}}
        obj = json.loads(path.read_text())
        if obj.get("status") != "completed":  # its process ended mid-round: the missing items failed
            obj["status"] = "completed"
        return obj
    if path.exists():
        return json.loads(path.read_text())
    try:
        obj = model_client.get_batch(bid, endpoint)
    except model_client.ModelError as error:
        print(f"port_rounds: {error}", file=sys.stderr)
        return None
    b["status"] = obj.get("status")
    b["counts"] = obj.get("request_counts")
    if obj.get("status") not in model_client.BATCH_FINAL:
        return None
    if obj.get("status") == "completed" and obj.get("results") is None:
        return None  # final, results not attached yet
    b["usage"] = obj.get("usage")
    atomic_write(path, json.dumps(obj))
    return obj


def apply_result(run, args, endpoint, f, result, batch_status, prices):
    meta = run.meta
    if result is None:
        return item_failed(run, f, f"no result in the batch ({batch_status})")
    if result.get("error"):
        e = result["error"]
        return item_failed(run, f, f"{e.get('type') or 'error'}: {e.get('message')}")
    if "record" in result:  # the sync transport: already a call record
        call = result["record"]
    else:
        response = result.get("response") or {}
        if response.get("status_code", 200) != 200:
            return item_failed(run, f, f"HTTP {response.get('status_code')}: {json.dumps(response.get('body'))[:300]}")
        try:
            call = model_client.parse_reply(response.get("body"), meta["model"], {"max_tokens": f["pending"].get(
                "max_tokens") or meta["max_tokens"]}, endpoint, None, prices)
        except model_client.ModelError as error:
            return item_failed(run, f, str(error))
        call["batch"] = f["pending"]["batch"]
    text = call.pop("text")
    record = store_reply(args, f, meta, endpoint, text, call)
    die_for_test("after-reply-saved")
    take_reply(run, f, record)
    print(f"port_rounds: {f['name']}: {f['kind']} {f['round']}: {call.get('prompt_tokens')} in, "
          f"{call.get('completion_tokens')} out ({call.get('reasoning_tokens')} reasoning), "
          f"${call.get('cost') or 0:.4f}, {call.get('finish_reason')}", file=sys.stderr)


def collect_batches(run, args, endpoint):
    """Apply every final batch's results to the functions waiting for them; the number applied."""
    applied, prices = 0, None
    reconcile(run)
    for bid, b in run.meta["batches"].items():
        if b.get("collected"):
            continue
        obj = fetch(run, bid, b, endpoint)
        if obj is None:
            continue
        if b["transport"] == "batch" and prices is None:
            prices = model_client.batch_prices(run.meta["model"], endpoint) or (0.0, 0.0)
        results = {r.get("custom_id"): r for r in obj.get("results") or []}
        for cid, name in b["ids"].items():
            f = run.fns.get(name)
            if not f or not f.get("pending") or f["pending"].get("custom_id") != cid:
                continue  # applied already (a kill after it), or never sent
            apply_result(run, args, endpoint, f, results.get(cid), obj.get("status"), prices)
            applied += 1
        b["collected"] = True
        b["status"] = obj.get("status")
        b["collected_at"] = time.time()
        run.save()
        print(f"port_rounds: batch {bid}: {obj.get('status')}, {len(results)} result(s)"
              + (f", ${(obj.get('usage') or {}).get('cost') or 0:.4f} by the provider" if obj.get("usage") else ""),
              file=sys.stderr)
    return applied


class Context:
    """What the gates need, made once per process: the table, each unit's
    generated views/c modules, the write lock."""

    def __init__(self, args):
        self.args = args
        self.table = pp.ownership.load(args.table or pp.OWNERSHIP)
        self.units = {}
        self.lock = threading.Lock()
        self.state = {"write_lock": threading.Lock()}

    def unit(self, f):
        with self.lock:
            if f["stem"] not in self.units:
                self.units[f["stem"]] = pp.Unit(f["stem"], f["plan"]["unit"], self.args.build, self.args.work,
                                                self.table)
            return self.units[f["stem"]]


def plan_of(f):
    return pp.Plan(**f["plan"])


def gate(run, ctx, f):
    """Judge a function's reply and advance it."""
    args, plan, kind, round_ = ctx.args, plan_of(f), f["kind"], f["round"]
    unit = ctx.unit(f)
    views_path, _ = unit.views(plan.types, plan.globals, tag=f"fn-{plan.name}")
    fdir = args.work / plan.name
    reply_path, _ = reply_files(args, f)
    reply = reply_path.read_text()
    started = time.monotonic()
    if kind == "spec":
        ok, errors, details = pp.evaluate_spec(plan, unit, views_path, ctx.table, args, fdir, reply, round_)
    else:
        ok, errors, details = pp.evaluate_port(plan, unit, views_path, ctx.table, args, fdir, f["entry"],
                                               pathlib.Path(f["spec_path"]), reply, round_)
    record = f["record"]
    record["check_seconds"] = record.get("check_seconds", 0.0) + time.monotonic() - started
    record[kind]["attempts"].append({"round": round_, "sample": 1, "call": f["reply"]["call"], "passed": ok,
                                     "errors": [prompt_privacy.sanitise(e)[:3000] for e in errors]}
                                    | ({"runs": details.get("runs", [])} if kind == "port" else {}))
    f["reply"] = None
    print(f"port_rounds: {plan.name}: {kind} {round_}: {'passed' if ok else 'failed'} its gate", file=sys.stderr)
    if ok and kind == "spec":
        f["entry"], f["spec_path"] = details["entry"], str(details["path"])
        record["spec"]["kept"] = {"round": round_, "sample": 1}
        record["spec"].update({k: v for k, v in details.items() if k not in ("entry", "path")})
        record["spec"]["waivers"] = f["entry"].get("coverage_waiver", [])
        port_text = (fdir / "port_prompt.md").read_text()
        f.update(kind="port", round=1, tries=0, messages=[{"role": "user", "content": port_text}], stage="needs-port")
    elif ok:
        f["port_path"] = str(details["path"])
        record["port"]["kept"] = {"round": round_, "sample": 1}
        f["stage"] = "write"
    else:
        nxt = round_ + 1
        if nxt <= 2 or (nxt <= 2 + args.compile_fixes and pp.progress(errors) == 0):
            f["messages"] = f["messages"] + [{"role": "assistant", "content": reply},
                                             {"role": "user", "content": pp.feedback(errors, kind)}]
            f.update(round=nxt, tries=0, stage=stage_needs(kind, nxt))
        else:
            record[kind].update({k: v for k, v in details.items() if k not in ("entry", "path", "runs")})
            return finish(run, f, f"{kind}-failed")
    run.save_fn(f)


def write(run, ctx, f):
    plan, args = plan_of(f), ctx.args
    unit = ctx.unit(f)
    views_path, _ = unit.views(plan.types, plan.globals, tag=f"fn-{plan.name}")
    record = f["record"]
    pp.write_function(plan, unit, views_path, f["entry"], pathlib.Path(f["spec_path"]), pathlib.Path(f["port_path"]),
                      args, ctx.state, record)
    finish(run, f, record["status"])


def finish(run, f, status, why=None):
    """A function is done: its record (the synchronous pipeline's shape) beside the others."""
    record = f["record"]
    record["status"] = status
    if why:
        record["stopped"] = prompt_privacy.sanitise(why)
    record["wall_seconds"] = time.time() - run.meta["created"]
    record["totals"] = pp.totals_of(record)
    f["stage"] = TERMINAL
    run.save_fn(f)
    args = run.args
    path = args.records / f["stem"] / f"{f['name']}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    atomic_write(path, prompt_privacy.sanitise(json.dumps(record, indent=1)) + "\n")
    run.finished.add(f["stem"])
    print(f"port_rounds: {f['name']}: {status} (${record['totals']['cost']:.4f})", file=sys.stderr)


def collect(run, args, endpoint, deadline=None):
    """Apply the finished batches, run the gates (in parallel) and the writes (one at a time)."""
    applied = collect_batches(run, args, endpoint)
    ctx = Context(args)
    gates = [f for f in run.fns.values() if f["stage"].endswith("-gate")]
    if gates:
        def one(f):
            if deadline and time.time() >= deadline:
                return  # parked: gated next time
            try:
                gate(run, ctx, f)
            except Exception as error:  # a checker crash must not lose the other functions' progress
                finish(run, f, "stopped", f"gate error: {type(error).__name__}: {error}")
            die_for_test("after-gate")
        with concurrent.futures.ThreadPoolExecutor(max(1, args.check_jobs)) as pool:
            list(pool.map(one, gates))
    for f in [f for f in run.fns.values() if f["stage"] == "write"]:
        if deadline and time.time() >= deadline:
            break
        write(run, ctx, f)
    summarise(run, args)
    return applied, len(gates)


def summarise(run, args):
    for stem in sorted(run.finished):
        pp.write_summary(args.records, stem)
    if run.finished:
        pp.write_review(args.records)
    run.finished.clear()


# ---------------------------------------------------------------- status and run
def status(run, out=sys.stdout):
    stages = {}
    for f in run.fns.values():
        key = f["stage"] if f["stage"] != TERMINAL else f"done: {f['record']['status']}"
        stages[key] = stages.get(key, 0) + 1
    meta = run.meta
    print(f"run {run.dir.name}: {len(run.fns)} function(s), model {meta['model']}, transport {meta['transport']}"
          + (f", reasoning budget {meta['reasoning_budget']}" if meta.get("reasoning_budget") else "")
          + (f", reasoning effort {meta['reasoning_effort']}" if meta.get("reasoning_effort") else ""), file=out)
    for key in sorted(stages):
        print(f"  {key}: {stages[key]}", file=out)
    open_ = {bid: b for bid, b in meta["batches"].items() if not b.get("collected")}
    provider = sum((b.get("usage") or {}).get("cost") or 0.0 for b in meta["batches"].values())
    print(f"  spent ${run.spent():.4f} (recorded calls)" + (f", ${provider:.4f} reported by the provider's batches"
                                                         if provider else "")
          + f"; {len(meta['batches'])} batch(es), {len(open_)} open", file=out)
    for bid, b in open_.items():
        age = (time.time() - b["submitted"]) / 60
        print(f"  open: {bid} ({b['transport']}): {b.get('status')}, {len(b['ids'])} request(s), "
              f"{b.get('counts') or ''} {age:.0f} min", file=out)
    return stages


def deadline_of(until):
    """--until HH:MM (local time) as a timestamp: today, or tomorrow when already past."""
    if not until:
        return None
    hh, mm = (int(x) for x in until.split(":"))
    now = datetime.datetime.now()
    when = now.replace(hour=hh, minute=mm, second=0, microsecond=0)
    if when <= now:
        when += datetime.timedelta(days=1)
    return when.timestamp()


def run_loop(run, args, endpoint):
    deadline = deadline_of(args.until)
    while True:
        collect(run, args, endpoint, deadline)
        if deadline and time.time() >= deadline:
            print(f"port_rounds: {args.until}: parked ({len(run.outstanding())} request(s) still out; "
                  f"collected next time)", file=sys.stderr)
            return 3
        live = [f for f in run.fns.values() if f["stage"] != TERMINAL]
        if not live:
            print("port_rounds: every function is done", file=sys.stderr)
            return 0
        sent = submit(run, args, endpoint)
        if sent == 0 and not run.outstanding() and not any(f["stage"].endswith("-gate") or f["stage"] == "write"
                                                           for f in run.fns.values()):
            print("port_rounds: nothing can be sent (budget) and nothing is out: stopped", file=sys.stderr)
            return 2
        if any(b["transport"] == "batch" and not b.get("collected") for b in run.meta["batches"].values()):
            wait = args.poll if not deadline else max(0, min(args.poll, deadline - time.time()))
            time.sleep(wait)


def main(args):
    """port_pipeline.py --mode rounds (its arguments, parsed)."""
    action = args.functions[0] if args.functions and args.functions[0] in ACTIONS else None
    if action:
        args.functions = args.functions[1:]
    elif args.units and args.units[-1] in ACTIONS:
        action = args.units.pop()
    if action is None:
        raise SystemExit(f"port_rounds: give an action: {', '.join(ACTIONS)}")
    run = Run(args.work / "rounds" / run_name(args))
    run.args, run.finished = args, set()
    if action == "status":
        if run.meta is None:
            raise SystemExit(f"port_rounds: no run {run.dir.name}")
        status(run)
        return 0
    run.acquire()
    endpoint = model_client.from_args(args)
    if run.meta is None:
        if not (args.units or args.functions):
            raise SystemExit("port_rounds: give --units (or FUNCTION...) to create a run")
        if args.transport is None:
            args.transport = "sync" if endpoint.local else "batch"
        create(run, args, endpoint)
    apply_settings(args, run.meta)
    endpoint = endpoint_of(run.meta)
    if endpoint.missing_key():
        print(f"port_rounds: {endpoint.missing_key()} is not set (run through `bash -ic` so your profile sets it)",
              file=sys.stderr)
        return 2
    os.nice(10)  # the checker's builds and runs are this process's children
    if action == "submit":
        submit(run, args, endpoint)
    elif action == "collect":
        collect(run, args, endpoint, deadline_of(args.until))
    else:
        code = run_loop(run, args, endpoint)
        status(run, sys.stderr)
        return code
    status(run, sys.stderr)
    return 0
