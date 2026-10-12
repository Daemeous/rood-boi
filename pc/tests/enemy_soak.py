#!/usr/bin/env python3
"""Enemy AI coverage soak: warp into rooms WITH their enemies and let them
path and fight for a while, logging every [UNIMPLEMENTED] or crash.

Continues the save on a copy of --card (the card itself is never written),
then for each selected room: Room Browser warp (RPC room_goto {zone, room,
enemies: 1, scripts: 0}), count the room's actors, and play a seeded script
for --vsyncs: wait for the enemies to come (they route around obstacles and
through room links), enter battle mode and attack, walk in every direction,
leave battle mode. A finding (the game stopped: [UNIMPLEMENTED], [crash],
other exit, or a stall) is recorded with the room and the resolved stack;
the game is relaunched and the run continues with the next room. Ashley's
death (Game Over) only relaunches.

    python3 pc/tests/enemy_soak.py --card pc/save/card1.mcd               every room
    python3 pc/tests/enemy_soak.py --rooms links --vsyncs 3000            rooms with room links first
    python3 pc/tests/enemy_soak.py --shard 1/3 --out pc/build/esoak1      a third of the rooms

Output (--out, default pc/build/enemy_soak): rooms.jsonl (one line per room:
zone, room, MAP location, name, actors, vsyncs, route continuations
(L800DF8A4 runs), outcome), findings.jsonl, run directories with game logs,
and summary.json. Exit status 0 with no findings.
"""
import argparse
import json
import os
import pathlib
import random
import re
import shutil
import socket
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, RpcError, symbols  # noqa: E402
from soak import resolve_stacks  # noqa: E402

DIRECTIONS = ["up", "down", "left", "right"]
# Rooms whose navigation table has room links (MAPnnn.MPD section 6 entries
# with bit 10 or 26): enemies there can take a link and continue their route
# (L800DF8A4). Found by scanning every MPD on the disc.
LINK_MAPS = {14, 55, 77, 84, 150, 429}


class Stop(Exception):
    def __init__(self, kind, detail):
        super().__init__(f"{kind}: {detail}")
        self.kind, self.detail = kind, detail


class Soak:
    def __init__(self, args):
        self.args = args
        self.out = pathlib.Path(args.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.game = None
        self.launches = 0
        self.rng = random.Random(args.seed)
        self.rooms_log = open(self.out / "rooms.jsonl", "a")
        self.findings_log = open(self.out / "findings.jsonl", "a")
        self.findings = []
        self.covered = []

    # ---- process ----
    def launch(self):
        self.close()
        self.launches += 1
        run = self.out / f"run{self.launches:03d}"
        shutil.rmtree(run, ignore_errors=True)
        run.mkdir()
        self.run_dir = run
        card = run / "card1.mcd"
        shutil.copyfile(self.args.card, card)
        self.log = run / "game.log"
        sock = f"/tmp/rood-esoak-{os.getpid()}.sock"
        env = {"ROOD_GI": "0", "ROOD_LIGHTS": str(run / "lights"), "ROOD_PREFABS": str(run / "prefabs"),
               "ROOD_LIGHTING": str(run / "lighting")}
        for d in ("lights", "prefabs", "lighting"):
            (run / d).mkdir()
        os.environ.pop("ROOD_SETTINGS", None)
        self.game = Game.launch(card=str(card), socket_path=sock, log=str(self.log), extra_env=env, fast=True)
        self.game.sock.settimeout(self.args.rpc_timeout)
        self.game.wait_title(timeout=120)
        self.game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            self.game.press("circle", vsyncs=3, settle=90)
        self.game.run(400)
        st = self.game.state()
        if st.get("player") is None:
            raise Stop("setup", "Continue did not reach gameplay")
        self.game.call("room_goto", action="status")

    def close(self):
        if self.game:
            try:
                self.game.close()
            except Exception:
                pass
            for p in self.game.processes:
                if p.poll() is None:
                    p.kill()
            self.game = None

    def stopped(self, why):
        """The game no longer answers: classify from its log."""
        p = self.game.processes[0]
        try:
            p.wait(timeout=10)
        except Exception:
            return Stop("stall", why)
        text = self.log.read_text(errors="replace")
        stacks = resolve_stacks(text)
        m = re.search(r"\[UNIMPLEMENTED\] ([^\n]*)", text)
        if m:
            return Stop("unimplemented", m.group(1) + ("\n  " + stacks[-1] if stacks else ""))
        m = re.search(r"\[crash\] ([^\n]*)", text)
        if m:
            return Stop("crash", m.group(1) + ("\n  " + stacks[-1] if stacks else ""))
        return Stop("exit", f"status {p.returncode} ({why})")

    def call(self, method, **params):
        try:
            return self.game.call(method, **params)
        except (socket.timeout, TimeoutError):
            raise Stop("stall", f"RPC {method} unanswered for {self.args.rpc_timeout}s")
        except (RpcError, OSError, json.JSONDecodeError) as e:
            if isinstance(e, RpcError) and "timed out" in str(e):
                raise Stop("stall", str(e))
            raise self.stopped(f"RPC {method}: {e}")

    def run(self, vsyncs):
        try:
            return self.game.run(vsyncs)
        except (socket.timeout, TimeoutError):
            raise Stop("stall", f"{vsyncs}-vsync step unanswered")
        except (RpcError, OSError, json.JSONDecodeError) as e:
            if isinstance(e, RpcError) and "timed out" in str(e):
                raise Stop("stall", str(e))
            raise self.stopped(f"step: {e}")

    def press(self, buttons, vsyncs=3, settle=8):
        self.call("input", buttons=buttons if isinstance(buttons, list) else [buttons], vsyncs=vsyncs)
        return self.run(vsyncs + settle)

    def counter(self):
        try:
            return int.from_bytes(self.game.read_symbol("pc_route_continuations", 4), "little")
        except Exception:
            return -1

    def actors(self):
        try:
            words = self.game.read_symbol("vs_battle_actors", 64)
        except Exception:
            return -1
        return sum(1 for i in range(1, 16) if int.from_bytes(words[4 * i:4 * i + 4], "little"))

    # ---- one room ----
    def warp(self, zone, room):
        """Warp (the request is taken in the field loop). When the field
        does not come back (a menu, targeting or a dialogue is up), back out
        with Cross/Triangle and try again; the caller relaunches after a
        second failure."""
        for attempt in range(3):
            for _ in range(2):
                self.press("cross", 3, 20)  # leave battle mode / menus (they block doors)
            reply = self.call("room_goto", zone=zone, room=room, enemies=True, scripts=False)
            if not reply["ok"]:
                return False, reply.get("message", "")
            status = reply
            for _ in range(40):
                self.run(20)
                status = self.call("room_goto", action="status")
                if status["phase"] in (0, 4):
                    break
            if status["phase"] in (0, 4):
                self.run(30)
                st = self.call("state")
                return (st["zone"], st["room"]) == (zone, room), status.get("message", "")
            self.call("room_goto", action="cancel")
            if attempt == 0:
                try:
                    self.game.present_screenshot(self.run_dir / f"stuck-{zone}-{room}.png", timeout=5)
                except Exception:
                    pass
            for b in ("triangle", "cross", "cross", "circle", "cross"):
                self.press(b, 3, 30)
            self.run(120)
        return False, status.get("message", "")

    def play(self, vsyncs):
        """Seeded play: mostly waiting/attacking where the enemies come to
        Ashley, with walks to make them re-route."""
        start = self.call("status")["vsync"]
        while self.call("status")["vsync"] - start < vsyncs:
            if self.call("state").get("game_over"):
                raise Stop("game_over", "Ashley died")
            r = self.rng.random()
            if r < 0.30:
                self.run(self.rng.randint(40, 150))  # let them come
            elif r < 0.60:  # battle mode, attack (targeting, confirm)
                self.press("circle", 3, 12)
                for _ in range(self.rng.randint(1, 3)):
                    self.press("circle", 3, self.rng.randint(10, 50))
                if self.rng.random() < 0.3:
                    self.press("cross", 3, 20)
            elif r < 0.85:
                d = self.rng.choice(DIRECTIONS)
                self.call("input", buttons=[d], vsyncs=self.rng.randint(15, 80))
                self.run(self.rng.randint(15, 80))
            elif r < 0.93:
                self.press(self.rng.choice(["l1", "r1"]), 3, 10)
            else:
                self.press("cross", 3, 20)  # back out of menus/targeting

    def record(self, entry):
        self.rooms_log.write(json.dumps(entry) + "\n")
        self.rooms_log.flush()
        self.covered.append(entry)

    def finding(self, room, stop):
        f = {"zone": room["zone"], "room": room["room"], "location": room["location"], "name": room["name"],
             "kind": stop.kind, "detail": stop.detail, "run": str(self.run_dir)}
        self.findings.append(f)
        self.findings_log.write(json.dumps(f) + "\n")
        self.findings_log.flush()
        print(f"FINDING {stop.kind} in zone {room['zone']} room {room['room']} (MAP{room['location']:03d}):"
              f" {stop.detail}", flush=True)

    def visit(self, room, retry=False):
        entry = {"zone": room["zone"], "room": room["room"], "location": room["location"], "name": room["name"]}
        before = self.counter()
        ok, message = self.warp(room["zone"], room["room"])
        entry["warped"] = ok
        if not ok and not retry:
            raise Stop("warp", message)  # relaunch and try this room once more
        if not ok:
            entry["outcome"] = f"warp failed: {message}"
            self.record(entry)
            return
        entry["actors"] = self.actors()
        vsyncs = self.args.vsyncs if entry["actors"] > 0 else self.args.empty_vsyncs
        try:
            self.play(vsyncs)
            entry["outcome"] = "ok"
        finally:
            entry["vsyncs"] = vsyncs
            entry["continuations"] = self.counter() - before if before >= 0 else None
            if "outcome" not in entry:
                entry["outcome"] = "stopped"
            self.record(entry)

    def main(self, rooms):
        deadline = time.monotonic() + self.args.budget * 60
        i, retried = 0, -1
        while i < len(rooms):
            if time.monotonic() > deadline:
                print(f"budget reached after {i} of {len(rooms)} rooms", flush=True)
                break
            room = rooms[i]
            try:
                if self.game is None:
                    self.launch()
                print(f"[{i + 1}/{len(rooms)}] zone {room['zone']} room {room['room']} MAP{room['location']:03d}"
                      f" {room['name'] or room['area']}", flush=True)
                self.visit(room, retry=retried == i)
                i += 1
            except Stop as stop:
                if stop.kind == "setup":
                    print("setup failed; retrying", flush=True)
                    self.close()
                    continue
                if stop.kind == "warp":
                    print(f"warp stuck ({stop.detail}); relaunching", flush=True)
                    retried = i
                    self.close()
                    continue
                if stop.kind != "game_over":
                    self.finding(room, stop)
                self.close()
                i += 1
        self.close()
        summary = {"rooms": len(self.covered), "with_actors": sum(1 for r in self.covered if r.get("actors", 0) > 0),
                   "continuations": sum(r.get("continuations") or 0 for r in self.covered),
                   "findings": len(self.findings), "launches": self.launches}
        (self.out / "summary.json").write_text(json.dumps(summary, indent=1))
        print(json.dumps(summary), flush=True)
        return 1 if self.findings else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--out", default=str(OUT.parent / "enemy_soak"))
    ap.add_argument("--rooms", default="all", help="all, links, or zone:room,zone:room...")
    ap.add_argument("--vsyncs", type=int, default=1500, help="play per room with actors")
    ap.add_argument("--empty-vsyncs", type=int, default=60, help="play per room without actors")
    ap.add_argument("--shard", default="1/1", help="k/n: every n-th room starting at k")
    ap.add_argument("--budget", type=float, default=60, help="minutes")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--skip-done", nargs="*", default=[], metavar="DIR",
                    help="skip rooms already warped into by earlier runs (their --out directories)")
    ap.add_argument("--rpc-timeout", type=float, default=120)
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    soak = Soak(args)
    # The room list comes from the game itself (read from the disc).
    soak.launch()
    listed = [r for r in soak.call("rooms")["rooms"] if r["entries"]]
    if args.rooms == "all":
        rooms = sorted(listed, key=lambda r: (r["location"] not in LINK_MAPS, r["zone"], r["room"]))
    elif args.rooms == "links":
        rooms = [r for r in listed if r["location"] in LINK_MAPS]
    else:
        want = [tuple(int(v) for v in w.split(":")) for w in args.rooms.split(",")]
        rooms = [r for w in want for r in listed if (r["zone"], r["room"]) == w]
    done = set()
    for d in args.skip_done:
        path = pathlib.Path(d) / "rooms.jsonl"
        for line in path.read_text().splitlines() if path.exists() else []:
            r = json.loads(line)
            if r.get("warped") and r.get("outcome") == "ok":
                done.add((r["zone"], r["room"]))
    rooms = [r for r in rooms if (r["zone"], r["room"]) not in done]
    k, n = (int(v) for v in args.shard.split("/"))
    rooms = rooms[k - 1::n]
    print(f"{len(rooms)} rooms", flush=True)
    return soak.main(rooms)


if __name__ == "__main__":
    sys.exit(main())
