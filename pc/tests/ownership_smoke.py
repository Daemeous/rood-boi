#!/usr/bin/env python3
"""In-process oracle: exact checkpoint rewind, input replay and per-tick digests."""
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import Game, OUT, BINARY, RpcError
from checkpoints import title
from snapshot_diff import Symbols


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    print(f"ok:   {message}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="oracle-", dir=OUT) as directory:
        out = pathlib.Path(directory)
        g = Game.launch(fast=True, log=out / "game.log", extra_env={
                "ROOD_DETERMINISTIC": "1", "ROOD_INTERPOLATE": "0",
                "ROOD_OWNER": "battle_action=owned",
                "ROOD_CARD1": str(out / "absent-card.mcd"),
                "ROOD_CARD2": str(out / "absent-card2.mcd")})
        try:
            title(g)
            owners = g.owner()["subsystems"]
            # battle_action first, then the trial port's one-route subsystems, original by default.
            check(owners[0]["name"] == "battle_action"
                  and owners[0]["routes"] == 9 and owners[0]["owner"] == "owned"
                  and all(o["owner"] == "original" for o in owners[1:]),
                  f"ROOD_OWNER and nine routed battle actions, the rest original: {owners}")
            for owner in ("original", "owned", "original"):
                check(g.owner("battle_action", owner)["subsystems"][0]["owner"] == owner,
                      f"owner round-trip: {owner}")
            for params in ({"subsystem": "missing", "owner": "owned"},
                           {"subsystem": "battle_action", "owner": "bad"},
                           {"subsystem": "battle_action"}, {"owner": "owned"},
                           {"subsystem": 1, "owner": "owned"},
                           {"action": "unknown"}, {"action": "report", "reset": "yes"},
                           {"action": "report", "owner": "owned"}):
                try:
                    g.call("owner", **params)
                    raise AssertionError(f"invalid owner request accepted: {params}")
                except RpcError:
                    pass
            report = g.call("owner", action="report", reset=True)
            check(report == {"calls": 0, "count": 0, "errors": 0, "truncated": False, "mismatches": [], "coverage": []},
                  f"shadow report is empty before shadow execution: {report}")
            g.call("graphics", interpolate=False)
            try:
                g.rewind()
                raise AssertionError("rewind without mark succeeded")
            except RpcError:
                pass
            marked = g.mark()["vsync"]
            g.run(300)
            first = g.call("snapshot", action="hash")
            g.rewind()
            check(g.status()["vsync"] == marked, "rewind returns to marked vsync")
            g.run(300)
            second = g.call("snapshot", action="hash")
            # snapshot hash includes host wall time in its monolithic state
            # hash; the page-digest proof below compares that region with only
            # the timestamp masked, rather than ignoring native state.
            check({k: v for k, v in first.items() if k != "state"} ==
                  {k: v for k, v in second.items() if k != "state"},
                  f"rewind guest/overlay/VRAM hashes are exact: {first} vs {second}")
            g.rewind()
            g.rewind()
            check(g.status()["vsync"] == marked, "mark survives repeated rewinds")
            rec = out / "pad.rec"
            # Only host monotonic time is excluded, just as snapshot_diff does.
            # Another unit has a 4-byte counter with this local name; only
            # boot_runtime's two-long timespec is host-clock noise.
            pointer_bytes = 8 if pathlib.Path(BINARY).read_bytes()[4] == 2 else 4
            masks = [[address, size] for address, size, name in Symbols(BINARY).rows
                     if name == "last_vsync" and size == 2 * pointer_bytes]
            check(len(masks) == 1 and masks[0][1] > 0, f"explicit host-clock mask: {masks}")
            for params in ({"action": "unknown"}, {"action": "record", "ticks": -1},
                           {"action": "compare"}, {"action": "record", "masks": [[1, -1]]},
                           {"action": "record", "masks": [[1, 1], "bad"]}):
                try:
                    g.call("digest", **params)
                    raise AssertionError(f"invalid digest request accepted: {params}")
                except RpcError:
                    pass
            g.digest("record", ticks=600, masks=masks)
            g.record(rec)
            g.hold(["up"], 90)
            g.press("down", vsyncs=3, settle=30)
            g.run(477)
            g.record(None)
            recorded = g.call("snapshot", action="hash")
            check(g.digest("stop")["recorded"] == 600, "exactly 600 digest samples recorded")
            g.rewind()
            g.replay(rec)
            g.digest("compare")
            g.run(600)
            replayed = g.call("snapshot", action="hash")
            check({k: v for k, v in replayed.items() if k != "state"} ==
                  {k: v for k, v in recorded.items() if k != "state"},
                  "600-vsync input replay reproduces guest/overlay/VRAM state")
            same = g.digest("result")
            check(same["error"] == 0 and same["first_tick"] == -1 and same["ticks"] == 600,
                  f"600-vsync digests bit-identical: {same}")
            g.replay(None)
            g.rewind()
            g.replay(rec)
            g.digest("compare")
            g.run(10)
            original = g.read(0x80001000, 1)
            g.write(0x80001000, bytes([original[0] ^ 0x5a]))
            g.run(590)
            poked = g.digest("result")
            check(poked["error"] == 0 and poked["first_tick"] == 10 and
                  any(page.startswith("ram+0x1000") for page in poked["first_pages"]),
                  f"perturbed byte detected with range and offset: {poked}")
            g.replay(None)
        finally:
            g.close()
    print("PASS oracle checkpoint, replay and digests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
