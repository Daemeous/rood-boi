#!/usr/bin/env python3
"""Find the event scripts a Room Browser warp starts, for the cutscene
player's catalog (pc/assets/cutscenes/catalog.json, pc/docs/Cutscene_Player.md).

From the save room of a memory card (the tests' pinned card by default), for
every room with a door in: back to the save room (an in-memory rewind), warp
there with the room's scripts and enemies on (RPC room_goto, as the cutscene
player's warp recipe does), and watch the event runner (D_800F4C34) through
RPC cutscene's status: whether an event starts within WATCH vsyncs of the
arrival, its id, and how long it runs (dialogue advanced with Circle / Cross
when it waits; capped at CAP vsyncs). A crash restarts the game and is
recorded. Rooms with an event are warped to a second time: a scene is
`reliable` when both runs saw the same event run to its end.

    python3 pc/tools/cutscene_scan.py [--card CARD] [--zones 9,13] [--limit N]
        [--out DIR] [--apply]

--apply merges the result into the catalog (its warp entries replaced; the
other recipes kept). Without it the result is only written to
DIR/scan.json. Run under nice; a full scan is a few hundred warps.
"""
import argparse
import json
import os
import pathlib
import shutil
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402

WATCH = 180  # vsyncs after the arrival an entry event may take to start
CAP = 7200  # vsyncs an event is followed at most
CATALOG = ROOT / "pc/assets/cutscenes/catalog.json"


def default_card():
    sys.path.insert(0, str(ROOT / "pc/tests"))
    import run  # noqa: E402
    return run.default_save()


class Scanner:
    def __init__(self, card, out):
        self.card, self.out = card, out
        self.game = None
        self.launches = 0

    def launch(self):
        if self.game:
            try:
                self.game.close()
            except Exception:
                pass
        self.launches += 1
        work = self.out / f"run{self.launches}"
        work.mkdir(parents=True, exist_ok=True)
        card = work / "card1.mcd"
        shutil.copyfile(self.card, card)
        socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-cscan-{os.getpid()}.sock"
        self.game = launch_at("save_room", card=card, fast=True, log=work / "game.log", socket_path=socket,
                              extra_env={"ROOD_CUTSCENES": str(work / "cutscenes"), "ROOD_PHOTOS": str(work / "photos"),
                                         "ROOD_CARD2": str(work / "absent-card2.mcd")})
        self.game.call("graphics", interpolate=False)
        self.game.mark()
        self.home = self.game.state()

    def alive(self):
        return bool(self.game and self.game.processes and self.game.processes[0].poll() is None)

    def event(self):
        e = self.game.call("cutscene")["event"]
        return e["id"] if e["running"] else -1

    def visit(self, zone, room):
        """One warp from the save room; {event, frames, presses, ...}."""
        g = self.game
        g.rewind()
        g.call("room_goto", action="reset")  # (the warp machinery is host state: not rewound)
        r = g.call("room_goto", zone=zone, room=room, scripts=True, enemies=True)
        if not r["ok"]:
            return {"result": "refused", "why": r["message"]}
        # Arrived: phase 3 (the room loaded, fading in: an entry event holds it there until it ends),
        # or back to idle / done after loading.
        loading = False
        for _ in range(300):
            g.run(10)
            phase = g.call("room_goto", action="status")["phase"]
            loading |= phase >= 2
            if phase >= 3 or (loading and phase == 0):
                break
        st = g.state()
        if (st["zone"], st["room"]) != (zone, room):
            return {"result": "not reached", "at": [st["zone"], st["room"]]}
        seen = self.event()
        waited = 0
        while seen < 0 and waited < WATCH:
            g.run(10)
            waited += 10
            seen = self.event()
        if seen < 0:
            return {"result": "no event"}
        frames = presses = quiet = 0
        while frames < CAP:
            g.run(20)
            frames += 20
            if self.event() < 0:
                quiet += 20
                if quiet >= 60:
                    break
                continue
            quiet = 0
            if frames % 120 == 0:  # dialogue waiting for a button: advance it
                g.press("circle" if presses % 2 == 0 else "cross", vsyncs=4, settle=1)
                presses += 1
        return {"result": "event", "event": seen, "frames": frames - quiet, "presses": presses,
                "ended": frames < CAP}

    def safe_visit(self, zone, room):
        try:
            return self.visit(zone, room)
        except (RpcError, OSError, ConnectionError, TimeoutError) as e:
            crashed = not self.alive()
            text = ""
            try:
                log = pathlib.Path(self.game.log).read_text(errors="replace")
                text = "\n".join(l for l in log.splitlines() if "[crash]" in l or "UNIMPLEMENTED" in l)[-400:]
            except OSError:
                pass
            self.launch()
            return {"result": "crashed" if crashed else "error", "why": str(e)[:200], "log": text}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card")
    ap.add_argument("--zones", help="comma-separated zone numbers (default: all)")
    ap.add_argument("--limit", type=int, default=0, help="at most N rooms")
    ap.add_argument("--out")
    ap.add_argument("--from-scan", help="no scan: apply an earlier scan.json (with --apply)")
    ap.add_argument("--apply", action="store_true", help="merge into " + str(CATALOG.relative_to(ROOT)))
    ap.add_argument("--min-frames", type=int, default=90,
                    help="catalogue events that run at least this long (shorter entry scripts set flags, "
                         "place actors: no scene to watch); default 90")
    args = ap.parse_args()
    card = pathlib.Path(args.card) if args.card else default_card()
    if not card.exists():
        print(f"no memory card {card}")
        return 2
    out = pathlib.Path(args.out) if args.out else OUT / "cutscene_scan"
    if args.from_scan:
        results = json.loads(pathlib.Path(args.from_scan).read_text())["rooms"]
        return apply(args, results)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    s = Scanner(card, out)
    s.launch()
    rooms = s.game.call("rooms")["rooms"]
    zones = {int(z) for z in args.zones.split(",")} if args.zones else None
    todo = [r for r in rooms if r["entries"] and (zones is None or r["zone"] in zones)]
    if args.limit:
        todo = todo[:args.limit]
    print(f"{len(todo)} rooms with a door in (of {len(rooms)}); home {s.home['zone']}/{s.home['room']}", flush=True)
    results = []
    start = time.monotonic()
    for i, r in enumerate(todo):
        v = s.safe_visit(r["zone"], r["room"])
        if v["result"] == "event":
            again = s.safe_visit(r["zone"], r["room"])
            v["again"] = again
            v["reliable"] = (again.get("result") == "event" and again.get("event") == v["event"] and v["ended"]
                             and again.get("ended") and abs(again["frames"] - v["frames"]) <= max(60, v["frames"] // 10))
        entry = {"zone": r["zone"], "room": r["room"], "name": r["name"], "area": r.get("area", ""), **v}
        results.append(entry)
        print(f"[{i + 1}/{len(todo)}] {r['zone']:3d}/{r['room']:2d} {r['name'][:28]:28s} {v['result']}"
              + (f" 0x{v['event']:04x} {v['frames']} frames, {v['presses']} presses"
                 f"{'' if v.get('reliable') else ' (unconfirmed)'}" if v["result"] == "event" else
                 f" {v.get('why', '')[:60]}" if "why" in v else ""), flush=True)
        (out / "scan.json").write_text(json.dumps({"card": str(card), "rooms": results}, indent=1) + "\n")
    s.game.close()
    print(f"{time.monotonic() - start:.0f} s; {out / 'scan.json'}")
    return apply(args, results)


def apply(args, results):
    events = [e for e in results if e["result"] == "event" and e["frames"] >= args.min_frames]
    print(f"{len(events)} rooms start an event of {args.min_frames}+ frames ({sum(1 for e in events if e.get('reliable'))} reliable), "
          f"{sum(1 for e in results if e['result'] == 'crashed')} crashed")
    if args.apply:
        catalog = json.loads(CATALOG.read_text())
        keep = [e for e in catalog["scenes"] if e.get("recipe", {}).get("kind") != "warp"]
        for e in events:
            source = ("zone EVT", "room entry", "door")[(e["event"] >> 12) & 3] if (e["event"] >> 12) < 3 else "?"
            keep.append({
                "id": f"z{e['zone']:03d}r{e['room']:02d}",
                "name": f"{e['name']} ({e['zone']}/{e['room']}, event 0x{e['event']:04x})",
                "recipe": {"kind": "warp", "zone": e["zone"], "room": e["room"], "entry": 0},
                "from": "any field room: a warp with the room's scripts and enemies on (out of story order)",
                "notes": f"{source} script {e['event'] & 0xFFF}; about {e['frames']} frames"
                         + (f", dialogue advanced {e['presses']} times (Circle / Cross)" if e["presses"] else "")
                         + ("" if e["ended"] else "; did not end within the scan's cap"),
                "frames": e["frames"],
                "event": e["event"],
                "reliable": bool(e.get("reliable")),
            })
        catalog["scenes"] = keep
        CATALOG.write_text(json.dumps(catalog, indent=2) + "\n")
        print(f"{CATALOG}: {len(keep)} scenes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
