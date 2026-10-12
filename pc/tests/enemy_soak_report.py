#!/usr/bin/env python3
"""Summarise enemy_soak.py output directories: rooms covered (warped, with
actors, vsyncs played), rooms not reached, route continuations and findings.

    python3 pc/tests/enemy_soak_report.py pc/build/esoak1 pc/build/esoak2 ...
    python3 pc/tests/enemy_soak_report.py --missing DIR...   zone:room list of rooms never warped into
"""
import argparse
import json
import pathlib


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--missing", action="store_true")
    ap.add_argument("--rooms", help="rooms.json from RPC rooms (all rooms with doors) for the missing list")
    args = ap.parse_args()
    rooms, findings = {}, []
    for d in args.dirs:
        d = pathlib.Path(d)
        for line in (d / "rooms.jsonl").read_text().splitlines() if (d / "rooms.jsonl").exists() else []:
            r = json.loads(line)
            key = (r["zone"], r["room"])
            best = rooms.get(key)
            if not best or (r.get("warped") and not best.get("warped")) or r.get("vsyncs", 0) > best.get("vsyncs", 0):
                rooms[key] = r
        if (d / "findings.jsonl").exists():
            findings += [json.loads(l) for l in (d / "findings.jsonl").read_text().splitlines()]
    warped = [r for r in rooms.values() if r.get("warped")]
    if args.missing:
        print(",".join(f"{z}:{r}" for (z, r), v in sorted(rooms.items()) if not v.get("warped")))
        return 0
    actors = [r for r in warped if r.get("actors", 0) > 0]
    print(f"rooms attempted {len(rooms)}, warped into {len(warped)}, with actors {len(actors)}, "
          f"vsyncs of enemy play {sum(r.get('vsyncs', 0) for r in actors)}, "
          f"route continuations {sum(r.get('continuations') or 0 for r in warped)}")
    print("not reached:", " ".join(f"{z}/{r}" for (z, r), v in sorted(rooms.items()) if not v.get("warped")) or "none")
    for f in findings:
        print(f"finding {f['kind']} zone {f['zone']} room {f['room']} (MAP{f['location']:03d}): "
              f"{f['detail'].splitlines()[0]}")
    by_zone = {}
    for r in warped:
        by_zone.setdefault(r["zone"], []).append(r["room"])
    print("covered:", "; ".join(f"zone {z}: {' '.join(map(str, sorted(v)))}" for z, v in sorted(by_zone.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
