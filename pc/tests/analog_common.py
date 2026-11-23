"""What the analog movement suites share: a game in the save room with Ashley in the field (weapon sheathed), the
stick's vector and the card copy (pc/tests/analog_*_smoke.py)."""
import math
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402


def wrap(d, turn=4096):
    return (d + turn // 2) % turn - turn // 2


def stick_at(degrees, travel=1.0):
    """The stick pushed `travel` toward `degrees` from forward (up), turning right."""
    r = math.radians(degrees)
    return [travel * math.sin(r), -travel * math.cos(r)]


class Checks:
    def __init__(self):
        self.failures = []

    def __call__(self, ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            self.failures.append(message)

    def finish(self, name):
        if self.failures:
            print(f"{len(self.failures)} failed")
            return 1
        print(f"{name}: all ok")
        return 0


def prepare(card, out_arg, name):
    """(out dir, card copy) or None to skip when the card is missing."""
    if not pathlib.Path(card).exists():
        print("SKIP: no memory card", card)
        return None
    out = pathlib.Path(out_arg) if out_arg else OUT / name
    out.mkdir(parents=True, exist_ok=True)
    copy = out / "card1.mcd"
    shutil.copyfile(card, copy)
    return out, copy


def field_game(out, card, checkpoint="save_room"):
    """A game at the checkpoint with Ashley in the field and the weapon sheathed."""
    game = launch_at(checkpoint, card=card, fast=True, log=out / "game.log",
                     extra_env={"ROOD_WINDOW": "960x720", "ROOD_GI": "0"})
    game.run(300)
    for _ in range(2):
        game.press("cross", vsyncs=3, settle=30)
    return game


def warp(game, zone, room):
    """The game in zone/room (the Room Browser's warp, no enemies or scripts) with Ashley standing and the pose settled."""
    game.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
    for _ in range(120):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.run(80)
    st = game.state()
    return (st["zone"], st["room"]) == (zone, room)
