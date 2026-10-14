#!/usr/bin/env python3
"""The editor's undo history (pc/src/editor_undo.h) and its keyboard rules.

    python3 pc/tests/editor_undo_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room, with the editor open and a scratch ROOD_LIGHTS
directory. Through the RPC (each edit request is one transaction, as one
mouse action is):

- the grid snapping step defaults to 8 units;
- a light actor added, moved, its parameter changed, dragged (editor_undo
  begin/end around many moves: one transaction) and deleted with the Delete
  key, and a glow override set, each undone and redone with the state
  checked at every step (the history's names say what each did);
- saving keeps the history; a new edit after an undo drops the redo;
- Ctrl+Z / Ctrl+Y as keys undo and redo;
- while a text field has the keyboard (the Outliner's search), Delete, E
  and Ctrl+Z do nothing to the scene and Escape cancels the field without
  closing the editor;
- room group visibility undoes.

Keys go through SDL's event queue (RPC editor_input), the same path as the
keyboard. --out keeps the frames (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import golden_frames as gf  # noqa: E402


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def close(a, b, tolerance=0.05):
    return len(a) == len(b) and all(abs(x - y) <= tolerance for x, y in zip(a, b))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "editor_undo"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "1", "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-undo-{os.getpid()}.sock"  # sun_path is short
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    def history():
        return game.call("editor_undo", action="list")

    def undo(steps=1):
        return game.call("editor_undo", action="undo", steps=steps)

    def redo(steps=1):
        return game.call("editor_undo", action="redo", steps=steps)

    def actors():
        return game.call("actors")["actors"]

    def keys(names, **kw):
        game.call("editor_input", keys=names, **kw)
        game.run(4)  # the events reach the overlay over the next frames
        return game.call("editor_input")

    def glow(glow_id):
        return next((g for g in game.call("glows")["glows"] if g["id"] == glow_id), None)

    try:
        game.call("graphics", **{**gf.DEFAULTS, "scale": 2, "editor_cutaway": False, "gi": True})
        check(game.call("graphics")["editor_snap_move"] == 8, "grid snapping defaults to 8 units")
        state = game.state()
        player = state.get("player")
        if not player or not game.call("scene")["triangles"]:
            print(f"FAIL: no room with Ashley after Continue: {state}")
            return 1
        print(f"zone {state['zone']} room {state['room']}, Ashley at {player['x']} {player['y']} {player['z']}")
        game.call("editor_ui", visible=True)
        game.run(4)
        start = history()
        check(start["history"] == [] and start["undo"] is None, f"a fresh room visit has no history ({start})")

        # Add, move, edit a light actor: one transaction each.
        at = [player["x"], player["y"] - 120, player["z"]]
        game.call("actors", action="add", prefab="candle", position=at, seed=5)
        added = actors()
        h = history()
        check(len(added) == 1 and h["done"] == 1 and "Candle" in h["undo"], f"add: one transaction ({h['undo']})")
        moved_to = [at[0] + 64, at[1], at[2] - 32]
        game.call("actors", action="set", index=0, position=moved_to)
        h = history()
        check(h["done"] == 2 and h["undo"].startswith("Move"), f"move: one transaction ({h['undo']})")
        game.call("actors", action="set", index=0, params={"intensity": 2.5})
        h = history()
        check(h["done"] == 3 and h["undo"].startswith("Edit") and actors()[0]["overrides"] == {"intensity": 2.5},
              f"parameter edit (prefab override): one transaction ({h['undo']})")

        # A drag: many moves between begin and end are one transaction.
        game.call("editor_undo", action="begin", name="Drag Candle")
        for k in range(1, 9):
            game.call("actors", action="set", index=0, position=[moved_to[0] + 16 * k, moved_to[1], moved_to[2]])
            game.run(1)
        mid = history()
        game.call("editor_undo", action="end")
        h = history()
        dragged_to = [moved_to[0] + 128, moved_to[1], moved_to[2]]
        check(mid["open"] and mid["done"] == 3 and h["done"] == 4 and h["undo"] == "Drag Candle"
              and close(actors()[0]["position"], dragged_to), f"a drag of 8 moves is one transaction ({h['history']})")

        # A glow override.
        glows = [g for g in game.call("glows")["glows"] if g["category"] != "effect"]
        check(len(glows) > 0, f"the room has a glow to override ({len(glows)})")
        glow_id = glows[0]["id"] if glows else 0
        if glows:
            game.call("glows", action="set", id=glow_id, intensity=3.0)
            h = history()
            check(h["done"] == 5 and "glow" in h["undo"].lower() and glow(glow_id)["overridden"],
                  f"glow override: one transaction ({h['undo']})")

        # Delete with the key (nothing typed): the selected actor goes.
        game.call("actors", action="select", index=0)
        state_keys = keys(["delete"])
        h = history()
        check(not state_keys["want_text_input"] and actors() == [] and h["undo"].startswith("Delete"),
              f"Delete deletes the selected actor ({h['undo']})")
        done = h["done"]

        # Undo each step, checking the state.
        undo()
        check(len(actors()) == 1 and close(actors()[0]["position"], dragged_to), "undo delete: the actor is back")
        if glows:
            undo()
            check(not glow(glow_id)["overridden"], "undo glow override: the glow is as the game made it")
        undo()
        check(close(actors()[0]["position"], moved_to) and actors()[0]["overrides"] == {"intensity": 2.5},
              "undo drag: back where the drag began (one step for the whole drag)")
        undo()
        check(actors()[0]["overrides"] == {}, "undo parameter edit: the prefab's value again")
        undo()
        check(close(actors()[0]["position"], at), "undo move: back at the first position")
        undo()
        h = history()
        check(actors() == [] and h["done"] == 0 and h["redo"] is not None and h["undo"] is None,
              f"undo add: no actor; everything is redoable ({h['redo']})")
        # Redo everything.
        r = redo(steps=10)
        h = history()
        check(r["moved"] == done and h["done"] == done and actors() == [], f"redo all {done} steps: deleted again")
        undo()
        check(len(actors()) == 1 and actors()[0]["overrides"] == {"intensity": 2.5}
              and (not glows or glow(glow_id)["overridden"]), "redone state: the drag, the edit and the glow override")

        # Saving keeps the history; a new edit drops the redo.
        game.call("actors", action="save")
        h = history()
        check(h["done"] == done - 1 and h["redo"] is not None, "saving keeps the history (and the redo)")
        game.call("actors", action="set", index=0, enabled=False)
        h = history()
        check(h["done"] == done and h["redo"] is None, "a new edit after an undo drops what was undone")

        # Ctrl+Z / Ctrl+Y as keys.
        keys(["z"], ctrl=True)
        check(actors()[0]["enabled"] is True and history()["done"] == done - 1, "Ctrl+Z undoes")
        keys(["y"], ctrl=True)
        check(actors()[0]["enabled"] is False and history()["done"] == done, "Ctrl+Y redoes")
        keys(["z"], ctrl=True, shift=False)
        keys(["z"], ctrl=True, shift=True)
        check(actors()[0]["enabled"] is False and history()["done"] == done, "Ctrl+Shift+Z redoes")

        # Typing in a field: no shortcut fires; Escape only cancels the field.
        game.call("actors", action="select", index=0)
        gizmo = game.call("editor_input")["gizmo"]
        game.call("editor_input", focus="search")
        game.run(4)
        typing = keys(["delete", "e", "backspace"])
        typed = keys(["z"], ctrl=True)
        check(typing["want_text_input"] and len(actors()) == 1 and typing["gizmo"] == gizmo
              and history()["done"] == done and typed["want_text_input"],
              "typing in the Outliner search: Delete, Backspace, E and Ctrl+Z leave the scene alone")
        escaped = keys(["escape"])
        check(escaped["visible"] and not escaped["want_text_input"], "Escape cancels the field; the editor stays open")
        escaped = keys(["e"])
        check(escaped["gizmo"] == 1, "after it, E picks the rotate tool again")
        keys(["w"])
        game.call("editor_input", focus="none")
        game.run(2)

        # Room group visibility.
        groups = game.call("room_groups")["groups"]
        before = history()["done"]
        game.call("room_groups", hide=[0])
        hidden = game.call("room_groups")["groups"][0]["hidden"]
        undo()
        check(hidden == 1 and game.call("room_groups")["groups"][0]["hidden"] == 0 and history()["done"] == before,
              f"hiding a room group undoes ({len(groups)} groups)")

        if args.out:
            game.run(gf.SETTLE)
            path = out / "editor.png"
            game.present_screenshot(path)
            Image.open(path).convert("RGB").resize((640, 360)).save(path)
        print("history:", history()["history"])
    finally:
        game.close()
        shutil.rmtree(lights, ignore_errors=True)
    log = pathlib.Path(game.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", game.log)
        return 1
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: editor edits undo and redo as transactions; shortcuts stay out of text fields")
    return 0


if __name__ == "__main__":
    sys.exit(main())
