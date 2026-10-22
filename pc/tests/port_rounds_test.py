#!/usr/bin/env python3
"""pc/tools/port_rounds.py (port_pipeline.py --mode rounds) offline: a fake
OpenRouter batch server on loopback (POST /v1/batches, GET /v1/batches/{id},
the batch price list) and, for the state machine, gates judged by the reply's
text (GOOD passes, NOBUILD does not compile, BAD differs):

  - the rounds: a spec that fails goes back with the checker's feedback as
    a retry, a draft that does not build gets the --compile-fixes round, a
    spec that passes queues the port prompt, a port that passes is written;
    each function's record in the synchronous pipeline's shape; the cost
    from the batch prices (the result bodies carry none), counted once;
  - partial collection: of two open batches only the finished one is
    applied, the other function keeps waiting;
  - a failed batch item is asked again next round (a new custom_id); a
    second failure stops the function; a failed batch fails its items;
  - a kill (the process ending at a test kill point) after a batch was
    recorded but before its functions were, after a reply was saved but
    before the function took it, and after the first gate of a collect: the
    resumed run sends nothing twice, applies every reply once and ends as
    an uninterrupted run does;
  - the sync transport (concurrent chat calls) through the same machine;
  - --reasoning-budget: OpenRouter's `reasoning.max_tokens`, max_tokens kept
    above it, the budget in the conversation key; --reasoning-effort:
    `reasoning.effort`, in the key too.

Runs on its own (python3 pc/tests/port_rounds_test.py) or from
port_pipeline_test.py, which also runs one function through the real gates
in rounds."""
import argparse
import dataclasses
import http.server
import json
import pathlib
import re
import sys
import tempfile
import threading

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
PRICES = (1e-6, 2e-6)  # per token: the fake batch price list


class FakeBatches(http.server.BaseHTTPRequestHandler):
    """OpenRouter's Batch API, as far as port_rounds uses it. The test sets
    `respond(custom_id, body) -> text` (raising KeyError: the item fails),
    and `hold` (batch ids that stay in progress) / `fail` (batch ids that fail)."""
    batches = {}
    hold, fail = set(), set()
    respond = None
    posts = []
    chats = []

    def log_message(self, *a):
        pass

    def send(self, data, code=200):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def obj(self, bid):
        b = FakeBatches.batches[bid]
        out = {"id": bid, "object": "batch", "model": b["model"], "status": "in_progress", "results": None,
               "usage": None, "request_counts": {"total": len(b["requests"]), "completed": 0, "failed": 0}}
        if bid in FakeBatches.fail:
            out["status"] = "failed"
            out["error"] = {"message": "provider error"}
            return out
        if bid in FakeBatches.hold:
            return out
        results, cost = [], 0.0
        for r in b["requests"]:
            try:
                text = FakeBatches.respond(r["custom_id"], r["body"])
                results.append({"custom_id": r["custom_id"], "id": "x", "error": None, "response": {
                    "status_code": 200, "body": {"model": b["model"], "choices": [
                        {"message": {"content": text}, "finish_reason": "stop"}],
                        "usage": {"prompt_tokens": 1000, "completion_tokens": 500}}}})
                cost += 1000 * PRICES[0] + 500 * PRICES[1]
            except KeyError:
                results.append({"custom_id": r["custom_id"], "id": "x", "response": None,
                                "error": {"type": "invalid_request", "message": "item failed"}})
        out.update(status="completed", results=results, usage={"cost": cost})
        return out

    def do_GET(self):
        if self.path.endswith(":batch/endpoints"):
            return self.send({"data": {"endpoints": [{"pricing": {"prompt": str(PRICES[0]),
                                                                  "completion": str(PRICES[1])}}]}})
        m = re.fullmatch(r"/v1/batches/([\w-]+)", self.path)
        if m and m.group(1) in FakeBatches.batches:
            return self.send(self.obj(m.group(1)))
        self.send({"error": "not found"}, 404)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path == "/v1/chat/completions":
            FakeBatches.chats.append(body)
            text = FakeBatches.respond("chat", body)
            return self.send({"model": body["model"], "choices": [{"message": {"content": text}, "finish_reason": "stop"}],
                              "usage": {"prompt_tokens": 10, "completion_tokens": 5, "cost": 0.001}})
        if self.path != "/v1/batches":
            return self.send({"error": "not found"}, 404)
        if list(body)[-1] != "requests":
            return self.send({"error": "requests before the metadata"}, 400)
        bid = f"batch-{len(FakeBatches.batches) + 1}"
        FakeBatches.batches[bid] = body
        FakeBatches.posts.append(bid)
        self.send({"id": bid, "object": "batch", "status": "validating", "results": None}, 202)


def serve():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeBatches)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_port}/v1"


class Killed(BaseException):
    pass


def fake_gates(pr, pp):
    """Gates judged by the reply's text; the writer only sets the status."""
    def verdict(reply):
        if "NOBUILD" in reply:
            return False, ["FAIL: does not compile:\nerror: x"]
        if "BAD" in reply:
            return False, ["FAIL: seed 1: the port differs from the C (exit 1)"]
        return True, []

    def evaluate_spec(plan, unit, views_path, table, args, fdir, reply, round_, k=1):
        ok, errors = verdict(reply)
        return ok, errors, ({"entry": {"mutation": [], "iterations": 2000}, "path": fdir / "spec.zig",
                             "classes": {"a": "1"}} if ok else {"classes": {}})

    def evaluate_port(plan, unit, views_path, table, args, fdir, entry, spec_path, reply, round_, k=1):
        ok, errors = verdict(reply)
        return ok, errors, {"path": fdir / "port.zig", "runs": [{"seed": 1, "exit": 0 if ok else 1}]}

    def write_function(plan, unit, views_path, entry, spec_path, port_path, args, state, record):
        record["status"] = "passed (not written)"
        return record

    class FakeUnit:
        def views(self, *a, **k):
            return pathlib.Path("views.zig"), None

    pp.evaluate_spec, pp.evaluate_port, pp.write_function = evaluate_spec, evaluate_port, write_function
    pr.Context.unit = lambda self, f: FakeUnit()


def make_run(pr, pp, work, url, names, transport="batch"):
    args = argparse.Namespace(work=work, records=work / "records", budget=1.0, reserve=0.01, check_jobs=2,
                              max_calls=4, compile_fixes=1, build=work, table=None, seeds=[1], no_write=True,
                              model="fake/model", max_tokens=1000, until=None, poll=0.0, force=False)
    run = pr.Run(work / "rounds" / "test")
    run.args, run.finished = args, set()
    run.meta = {"created": 0.0, "model": "fake/model", "base_url": url, "api_key_env": "", "thinking": "default",
                "temperature": 0.0, "reasoning_budget": None, "max_tokens": 1000, "transport": transport,
                "seeds": [1], "no_write": True, "compile_fixes": 1, "units": [], "functions": names, "table": "",
                "build": str(work), "batches": {}}
    for name in names:
        plan = pp.Plan(name=name, unit="src/BATTLE/T.c", stem="T", address=0x80000000, difficulty="S",
                       zig_name=name, name_source="test", module=name, signature=f"pub fn {name}() void")
        fdir = work / name
        fdir.mkdir(parents=True, exist_ok=True)
        (fdir / "port_prompt.md").write_text(f"PORT {name}")
        record = {"function": name, "unit": plan.unit, "difficulty": "S", "model": "fake/model", "status": "started",
                  "reviewed": False, "plan": dict(dataclasses.asdict(plan), address="0x80000000"),
                  "spec": {"attempts": []}, "port": {"attempts": []}, "written": [], "check_seconds": 0.0}
        f = {"name": name, "stem": "T", "plan": dataclasses.asdict(plan), "stage": "needs-spec", "kind": "spec",
             "round": 1, "messages": [{"role": "user", "content": f"SPEC {name}"}], "pending": None, "reply": None,
             "submissions": 0, "item_failures": 0, "entry": None, "spec_path": None, "port_path": None,
             "record": record, "events": []}
        run.fns[name] = f
        run.save_fn(f)
    run.save()
    return run, args


def reload(pr, run, args):
    """The run as a new process would find it on disk."""
    again = pr.Run(run.dir)
    again.args, again.finished = args, set()
    return again


def drive(pr, run, args, endpoint, limit=20):
    """submit/collect until done (the run loop without its sleeps)."""
    for _ in range(limit):
        pr.collect(run, args, endpoint)
        if all(f["stage"] == "done" for f in run.fns.values()):
            return
        pr.submit(run, args, endpoint)
    raise AssertionError("the rounds did not finish")


def statuses(run):
    return {n: (f["stage"], f["record"]["status"]) for n, f in run.fns.items()}


def checks(tmp):
    import model_client as mc
    import port_pipeline as pp
    import port_rounds as pr
    failures = []
    saved = (pp.evaluate_spec, pp.evaluate_port, pp.write_function, pr.Context.unit, pr.die_for_test)
    server, url = serve()
    endpoint = mc.Endpoint(base_url=url, local=False, api_key_env="")  # remote-like: the batch prices apply
    item_cost = 1000 * PRICES[0] + 500 * PRICES[1]
    try:
        fake_gates(pr, pp)
        # 1. The rounds: A passes first time; B's spec fails, then passes on the retry;
        # C's port does not build twice (the retry and the compile-fix round), then passes.
        script = {"B-spec1": "BAD", "C-port1": "NOBUILD", "C-port2": "NOBUILD"}
        FakeBatches.respond = lambda cid, body: script.get(cid.rsplit("-", 1)[0], "GOOD")
        run, args = make_run(pr, pp, tmp / "rounds", url, ["A", "B", "C"])
        drive(pr, run, args, endpoint)
        want = {n: ("done", "passed (not written)") for n in "ABC"}
        if statuses(run) != want:
            failures.append(f"rounds: {statuses(run)}")
        b = run.fns["B"]
        if [(a["round"], a["passed"]) for a in b["record"]["spec"]["attempts"]] != [(1, False), (2, True)]:
            failures.append(f"rounds: B's spec attempts {b['record']['spec']['attempts']}")
        c = run.fns["C"]["record"]["port"]["attempts"]
        if [(a["round"], a["passed"]) for a in c] != [(1, False), (2, False), (3, True)]:
            failures.append(f"rounds: C's port attempts (the compile-fix round) {[(a['round'], a['passed']) for a in c]}")
        retry = next(body for bid in FakeBatches.posts for r in FakeBatches.batches[bid]["requests"]
                     if r["custom_id"] == "B-spec2-1" for body in [r["body"]])
        if [m["role"] for m in retry["messages"]] != ["user", "assistant", "user"] \
                or "the port differs" not in retry["messages"][2]["content"] or retry["messages"][1]["content"] != "BAD":
            failures.append(f"rounds: B's retry conversation {retry['messages']}")
        port1 = next(r["body"] for bid in FakeBatches.posts for r in FakeBatches.batches[bid]["requests"]
                     if r["custom_id"] == "A-port1-1")
        if port1["messages"] != [{"role": "user", "content": "PORT A"}] or "model" in port1:
            failures.append(f"rounds: A's port request {port1}")
        calls = sum(len(f["record"][k]["attempts"]) for f in run.fns.values() for k in ("spec", "port"))
        if abs(run.spent() - calls * item_cost) > 1e-9 or calls != 9:
            failures.append(f"rounds: spent {run.spent()} for {calls} calls (want {calls * item_cost})")
        rec = json.loads((args.records / "T" / "B.json").read_text())
        if rec["status"] != "passed (not written)" or rec["totals"]["calls"] != 3 \
                or not (args.records / "REVIEW.md").exists() or not (args.records / "T/summary.json").exists():
            failures.append(f"rounds: B's record {rec.get('status')} {rec.get('totals')}")
        meta = json.loads((args.work / "B" / "spec_reply2.json").read_text())
        if meta.get("conversation") != pp.conversation_key(retry["messages"], "fake/model", endpoint.key_fields()):
            failures.append("rounds: the saved reply's key is not the synchronous pipeline's")

        # 2. Partial collection: two open batches, only one finished.
        FakeBatches.respond = lambda cid, body: "GOOD"
        run, args = make_run(pr, pp, tmp / "partial", url, ["P", "Q"])
        args.budget = 0.01  # room for one request
        pr.submit(run, args, endpoint)
        args.budget = 1.0
        pr.submit(run, args, endpoint)
        first, second = FakeBatches.posts[-2], FakeBatches.posts[-1]
        FakeBatches.hold.add(second)
        pr.collect(run, args, endpoint)
        if (run.fns["P"]["stage"], run.fns["Q"]["stage"]) != ("needs-port", "spec-submitted") \
                or not run.meta["batches"][first]["collected"] or run.meta["batches"][second]["collected"]:
            failures.append(f"partial: {statuses(run)} {run.meta['batches']}")
        pr.submit(run, args, endpoint)  # P's port goes out while Q's spec is still out
        if len(run.outstanding()) != 2:
            failures.append(f"partial: P's port not sent beside Q's open spec: {statuses(run)}")
        FakeBatches.hold.discard(second)
        drive(pr, run, args, endpoint)
        if statuses(run) != {n: ("done", "passed (not written)") for n in "PQ"}:
            failures.append(f"partial: {statuses(run)}")

        # 3. A failed item is asked again; a second failure stops the function; a failed batch.
        failing = set()

        def respond(cid, body):
            if cid in failing:
                raise KeyError(cid)
            return "GOOD"
        FakeBatches.respond = respond
        run, args = make_run(pr, pp, tmp / "items", url, ["F", "G", "H"])
        pr.submit(run, args, endpoint)
        FakeBatches.fail.add(FakeBatches.posts[-1])  # the whole first batch fails: every item asked again
        pr.collect(run, args, endpoint)
        if any(f["stage"] != "needs-spec" or f["item_failures"] != 1 for f in run.fns.values()):
            failures.append(f"items: a failed batch: {statuses(run)}")
        failing = {"F-port1-1", "G-spec1-2"}  # G's spec: its second failure (the batch's was its first)
        drive(pr, run, args, endpoint)
        st = statuses(run)
        sent = [r["custom_id"] for bid in FakeBatches.posts[-6:] for r in FakeBatches.batches[bid]["requests"]]
        if st["F"] != ("done", "passed (not written)") or "F-port1-2" not in sent or st["G"] != ("done", "stopped") \
                or "failed 2 times" not in run.fns["G"]["record"].get("stopped", "") or st["H"][1] != "passed (not written)":
            failures.append(f"items: {st} {sent}")

        # 4. Kills: after a batch was recorded (its functions not yet), after a
        # reply was saved (the function not yet told), after the first gate.
        FakeBatches.respond = lambda cid, body: script.get(cid.rsplit("-", 1)[0], "GOOD")
        for point in ("after-batch-saved", "after-reply-saved", "after-gate"):
            run, args = make_run(pr, pp, tmp / f"kill-{point}", url, ["A", "B", "C"])
            posts = len(FakeBatches.posts)
            fired = []

            def die(p, point=point, fired=fired):
                if p == point and not fired:
                    fired.append(p)
                    raise Killed(p)
            pr.die_for_test = die
            for _ in range(20):
                try:
                    pr.collect(run, args, endpoint)
                    if all(f["stage"] == "done" for f in run.fns.values()):
                        break
                    pr.submit(run, args, endpoint)
                except Killed:
                    run = reload(pr, run, args)
            if not fired:
                failures.append(f"kill {point}: the kill point was never reached")
            if statuses(run) != {n: ("done", "passed (not written)") for n in "ABC"}:
                failures.append(f"kill {point}: {statuses(run)}")
            ids = [r["custom_id"] for bid in FakeBatches.posts[posts:] for r in FakeBatches.batches[bid]["requests"]]
            if len(ids) != len(set(ids)) or len(ids) != 9:
                failures.append(f"kill {point}: requests sent {ids}")
            calls = sum(len(f["record"][k]["attempts"]) for f in run.fns.values() for k in ("spec", "port"))
            if calls != 9 or abs(run.spent() - 9 * item_cost) > 1e-9:
                failures.append(f"kill {point}: {calls} attempts, spent {run.spent()}")
        pr.die_for_test = saved[4]

        # 5. The sync transport: the same machine, the calls at once.
        FakeBatches.respond = lambda cid, body: "BAD" if body["messages"][0]["content"] == "SPEC S2" \
            and len(body["messages"]) == 1 else "GOOD"
        run, args = make_run(pr, pp, tmp / "sync", url, ["S1", "S2"], transport="sync")
        chats = len(FakeBatches.chats)
        drive(pr, run, args, endpoint)
        if statuses(run) != {n: ("done", "passed (not written)") for n in ("S1", "S2")} \
                or len(FakeBatches.chats) - chats != 5 or abs(run.spent() - 0.005) > 1e-9:
            failures.append(f"sync transport: {statuses(run)} {len(FakeBatches.chats) - chats} calls {run.spent()}")

        # 6. The reasoning budget.
        budget = mc.Endpoint(reasoning_budget=4000)
        body = mc.request_body([{"role": "user", "content": "hi"}], "m", 2000, budget)
        if body.get("reasoning") != {"max_tokens": 4000} or body["max_tokens"] <= 4000 \
                or mc.request_body([{"role": "user", "content": "hi"}], "m", 64000, budget)["max_tokens"] != 64000:
            failures.append(f"reasoning budget: {body}")
        if pp.conversation_key([], "m", budget.key_fields()) == pp.conversation_key([], "m") \
                or "reasoning" in mc.request_body([{"role": "user", "content": "hi"}], "m", 100, mc.Endpoint()):
            failures.append("reasoning budget: the key, or a default request with a reasoning field")
        effort = mc.Endpoint(reasoning_effort="low")
        if mc.request_body([{"role": "user", "content": "hi"}], "m", 100, effort).get("reasoning") != {"effort": "low"} \
                or pp.conversation_key([], "m", effort.key_fields()) == pp.conversation_key([], "m"):
            failures.append("reasoning effort: the request or the key")
        try:
            mc.Endpoint(reasoning_budget=500)
            failures.append("reasoning budget below Anthropic's minimum accepted")
        except ValueError:
            pass
    finally:
        pp.evaluate_spec, pp.evaluate_port, pp.write_function, pr.Context.unit, pr.die_for_test = saved
        server.shutdown()
    return failures


def main():
    with tempfile.TemporaryDirectory(prefix="port-rounds-test-") as tmp:
        failures = checks(pathlib.Path(tmp))
    for f in failures:
        print("FAIL:", f)
    print("port_rounds_test: " + ("FAILED" if failures else "passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
