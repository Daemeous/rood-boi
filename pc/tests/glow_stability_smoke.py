#!/usr/bin/env python3
"""The game's glows are stable, selectable, controllable light sources
(pc/src/glows.c, pc/docs/Glows.md).

    python3 pc/tests/glow_stability_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room (zone 9 room 17), with a private lights directory
(ROOD_LIGHTS). The user's report: the save point's glow was 2-4 emitters that
appeared and moved every frame, so its light jumped and it could not be
selected. Checks:

- over 90 vsyncs the save point is one glow ("Save point glow"), always the
  same id, at the same place (its fixed centre), traced every frame;
- its light energy (RPC glows `energy`) varies little (scrolling texture,
  smoothed): coefficient of variation under 0.1;
- selecting it by id (RPC glows select) keeps it selected across frames;
- an override by id (intensity 0.5) halves its energy; saved with the room
  (the line carries the glow's key), it applies again after the game is
  relaunched and the room loaded from the file (another process);
- an old-format glow line (no key, by position) still binds to it (muted),
  and takes the key on the next save;
- glows have categories: the save point is "magic", category settings
  (RPC graphics glow_magic_*) switch and scale it;
- the prologue fight (New Game, the knight in the courtyard, vsync ~12100;
  --no-fight skips it): hit effects make transient "effect" glows, which
  never cast shadows (RPC glows `casts`: their emitter bits stay clear with
  glow shadows on) and whose light rises and decays smoothly (largest
  change per vsync under a quarter of the peak), never listed for overrides.

--out keeps the logs and the room files."""
import argparse
import os
import pathlib
import shutil
import statistics
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402

FRAMES = 90


def fight(out, socket, check):
    """The prologue fight: transient effect glows (hit sparks)."""
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-fight-", dir=out))
    env = {"ROOD_LIGHTS": str(lights), "ROOD_GI": "1", "ROOD_SCALE": "2", "ROOD_CARD1": str(out / "absent.mcd")}
    # The title at an exact vsync (checkpoints.py): the fight's arrival, and with it where the
    # knight is when the attacks start, must not depend on how busy the machine was at boot.
    game = launch_at("title", fast=True, log=out / "fight.log", extra_env=env, socket_path=socket)
    try:
        # Path tracing only for the fight: it starts 1000 vsyncs before (its
        # history and the glows' smoothing settle in far fewer), not for the
        # 12000-vsync opening on the way (the path tracer's own tests cover it).
        game.call("graphics", gi=False, glow_shadows=True)
        game.press("start")
        traced = False
        while game.status()["vsync"] < 12000:  # New Game; dialogue and the walk to the fight
            if not traced and game.status()["vsync"] >= 11000:
                game.call("graphics", gi=True, glow_shadows=True)
                traced = True
            game.press("circle", 2, 100)
        if not traced:
            game.call("graphics", gi=True, glow_shadows=True)
        state = game.state()
        print(f"fight: zone {state['zone']} room {state['room']} at vsync {game.status()['vsync']}")
        rows = []
        for i in range(500):
            if i % 100 == 0:
                game.press("circle", 2, 1)  # attack when in range
            else:
                game.run(1)
            rows.append(game.call("glows"))
            if len(rows) > 150 and not any(g["category"] == "effect" for g in rows[-1]["glows"]) \
                    and any(r["categories"]["effects"]["energy"] > 0.5 for r in rows):
                break  # one hit seen and over
        effect_rows = [r for r in rows if any(g["category"] == "effect" and g["traced"] for g in r["glows"])]
        check(len(effect_rows) >= 10, f"hits make effect glows ({len(effect_rows)} vsyncs with traced effect glows)")
        if not effect_rows:
            return
        leaks = 0
        for r in effect_rows:
            traced = [g for g in r["glows"] if g["traced"]]
            for i, g in enumerate(traced):
                if g["category"] == "effect" and (g["casts_shadows"] or (r["casts"] >> i) & 1):
                    leaks += 1
        check(leaks == 0, f"effect glows never cast shadows, glow shadows on ({leaks} shadowed emitters)")
        listed = [g for r in effect_rows for g in r["glows"] if g["category"] == "effect"]
        try:
            game.call("glows", action="set", id=listed[-1]["id"], intensity=2)
            refused = False
        except Exception:
            refused = True
        check(refused, "effect glows take no per-glow overrides (only their category)")
        energy = [r["categories"]["effects"]["energy"] for r in rows]
        peak = max(energy)
        step = max(abs(a - b) for a, b in zip(energy, energy[1:]))
        flips = sum(1 for a, b, c in zip(energy, energy[1:], energy[2:]) if (b - a) * (c - b) < 0 and abs(b - a) > 0.05 * peak)
        print(f"effect light: peak {peak:.2f}, largest change per vsync {step:.3f}, direction changes {flips}")
        check(peak > 0 and step < 0.25 * peak, f"effect light changes smoothly ({step / peak if peak else 1:.2f} of the peak per vsync)")
        check(flips <= 6, f"effect light does not strobe ({flips} reversals)")
    finally:
        game.close()
        shutil.rmtree(lights, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the logs and room files here")
    parser.add_argument("--no-fight", action="store_true", help="skip the prologue fight (effect glows)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "glow_stability"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_LIGHTS": str(lights), "ROOD_GI": "1", "ROOD_SCALE": "2"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-glows-{os.getpid()}.sock"  # sun_path is short
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    def launch(name):
        game = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env, socket_path=socket)
        game.call("graphics", gi=True)
        return game

    def save_point(reply):
        return [g for g in reply["glows"] if g["name"].startswith("Save point glow")]

    def settle_reload(game):
        """Take the room file as written now (the editor's own poll runs
        twice a second of wall-clock time), then let a few frames draw."""
        game.reload_lights()
        game.run(4)

    def energy(game, frames=30):
        values = []
        for _ in range(frames):
            game.run(1)
            values.append(game.call("glows")["energy"])
        return statistics.mean(values)

    game = launch("game")
    try:
        state = game.state()
        zone, room = state["zone"], state["room"]
        print(f"zone {zone} room {room}")
        game.run(120)  # the Continue effect's sparkles fade out
        # Stability: one glow for the save point, the same id, steady.
        rows = []
        for _ in range(FRAMES):
            game.run(1)
            rows.append(game.call("glows"))
        first = save_point(rows[0])
        check(len(first) == 1, f"the save point is one glow ({[g['name'] for g in rows[0]['glows']]})")
        if not first:
            return 1
        glow = first[0]
        ids = [[g["id"] for g in save_point(r)] for r in rows]
        traced = [[g["id"] for g in r["glows"] if g["traced"]] for r in rows]
        check(all(i == [glow["id"]] for i in ids), f"the save point keeps id {glow['id']} for {FRAMES} vsyncs")
        check(all(t == traced[0] for t in traced) and glow["id"] in traced[0],
              f"the traced glows stay the same ({len(traced[0])}: {traced[0]})")
        moves = [max(abs(a - b) for a, b in zip(save_point(r)[0]["position"], glow["position"])) for r in rows]
        check(max(moves) < 1.0, f"its position holds still (largest move {max(moves):.2f} units)")
        values = [r["energy"] for r in rows]
        mean = statistics.mean(values)
        cv = statistics.pstdev(values) / mean if mean else 1
        raw = [r["raw_energy"] for r in rows]
        raw_cv = statistics.pstdev(raw) / statistics.mean(raw) if statistics.mean(raw) else 0
        print(f"energy mean {mean:.3f}, variation {cv:.3f} (unsmoothed {raw_cv:.3f})")
        check(mean > 0 and cv < 0.1, f"its light is steady (energy variation {cv:.3f} < 0.1)")
        check(glow["category"] == "magic" and glow["casts_shadows"], f"the save point is a magic glow ({glow['category']})")
        # Category settings: magic off mutes it, intensity 2 doubles it.
        game.call("graphics", glow_magic_enabled=False)
        off = energy(game, 10)
        game.call("graphics", glow_magic_enabled=True, glow_magic_intensity=2.0)
        doubled = energy(game, 20)
        game.call("graphics", glow_magic_intensity=1.0)
        check(off < 0.05 * mean and abs(doubled / mean - 2) < 0.3,
              f"the magic category switches and scales it (off {off:.3f}, x2 {doubled / mean:.2f})")
        # Selection by id holds across frames.
        game.call("glows", action="select", id=glow["id"])
        selected = []
        for _ in range(30):
            game.run(1)
            selected.append(game.call("glows")["selected"])
        check(all(s == glow["id"] for s in selected), f"glow {glow['id']} stays selected for 30 vsyncs")
        # An override by id, saved with the room by key.
        game.call("glows", action="set", id=glow["id"], intensity=0.5)
        halved = energy(game)
        print(f"energy {mean:.3f} -> {halved:.3f} at intensity 0.5")
        check(abs(halved / mean - 0.5) < 0.15, f"intensity 0.5 halves its light ({halved / mean:.2f})")
        game.call("actors", action="save")
        room_file = lights / f"zone{zone:02d}_room{room:02d}.txt"
        text = room_file.read_text() if room_file.exists() else ""
        line = [x for x in text.splitlines() if x.startswith("glow ")]
        check(len(line) == 1 and line[0].split()[1] == glow['key'], f"saved with its key: {line}")
    finally:
        game.close()

    # A new process loads the room file: the override finds its glow by key.
    game = launch("reload")
    try:
        game.run(120)
        reply = game.call("glows")
        again = save_point(reply)
        check(len(again) == 1 and again[0]["key"] == glow["key"] and again[0]["overridden"],
              f"after a relaunch the save point glow has its key and override ({[(g['key'], g['overridden']) for g in again]})")
        reloaded = energy(game)
        check(abs(reloaded / mean - 0.5) < 0.15, f"the saved override applies after the reload ({reloaded / mean:.2f})")
        # An old line (no key): by position, muted; the next save adds the key.
        p = glow["position"]
        room_file.write_text(f"glow {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} 1 1.000 1.000 1.000 1.000 0.0 0.0\n")
        game.call("editor_ui", visible=True)
        settle_reload(game)
        reply = game.call("glows")
        old = save_point(reply)
        check(len(old) == 1 and old[0]["muted"] == 1 and not old[0]["traced"] and reply["energy"] < 0.05 * mean,
              f"an old-format line (by position) mutes it ({[(g['muted'], g['traced']) for g in old]}, "
              f"energy {reply['energy']:.3f})")
        game.call("actors", action="save")
        line = [x for x in room_file.read_text().splitlines() if x.startswith("glow ")]
        check(len(line) == 1 and line[0].split()[1] == glow['key'], f"the old line takes the key when saved: {line}")
    finally:
        game.close()
    if not args.no_fight:
        fight(out, socket, check)
    if not args.out:
        shutil.rmtree(lights, ignore_errors=True)
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
