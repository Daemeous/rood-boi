#!/usr/bin/env python3
"""Battle charging skip soak (pc/src/charge_skip.c, charge_skip.inc): every
spell of the four schools, every usable misc item and the equipped weapon's
four Break Arts, cast one after the other with Skip on (who: all), unpaced.

    python3 pc/tests/charge_skip_soak.py [--card pc/build/test-card1.mcd] [--out DIR] [--items N]

Needs a card whose first save is in the save room (skipped otherwise). Warps
with enemies to zone 9 room 18 (as skip_charging_smoke.py), learns every
spell (vs_main_actions[id].unlocked; the Magick command on), fills the misc
bag with two of each of the first --items misc ids from Cure Root (323) on
(potions, grimoires...), and learns the equipped category's four Break Arts.
Then, through the modern menus, each spell (its first level), each usable
item and each art: the row, Circle, then Circle on the target until the
action is under way. A spell or item the game refuses here (no valid target
in the room, greyed) is counted and passed over. Each action that runs:
  - its hold skipped (_cameraMode 5 and 6): at most 32 vsyncs, none
    presented; the camera's move before it (4) presented;
  - its name window (the Information option on) opened with its name;
  - a watchdog: BATTLE's action state (D_800F196C 2) for more than 2000
    vsyncs fails (stuck in the wind-up or after it);
  - afterwards clean: _cameraMode 0, no effects in either list, no name
    window, D_800F522C 0, the object data slots empty;
  - no crash, no [UNIMPLEMENTED].
Ashley's HP and MP are refilled before each, and the enemies' HP (a kill's
loot window would wait for input). --out keeps the logs."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import skip_charging_smoke as smoke  # noqa: E402

SCHOOLS = (("warlock", 18), ("shaman", 6), ("sorcerer", 18), ("enchanter", 8))
MENU_FLAGS = 2  # vs_main_settings.menuFlags (bit 0: the Magick command)
FIRST_MISC = 323
WATCHDOG = 2000


class Soak(smoke.Run):
    def refill(self):
        g = self.g
        for slot in range(16):
            a2 = self.actor2(slot)
            if a2:
                g.write(a2 + smoke.HP, g.read(a2 + smoke.HP + 2, 2))
                g.write(a2 + smoke.MP, g.read(a2 + smoke.MP + 2, 2))
        g.write(self.actor2(0) + smoke.MP, struct.pack("<hh", 999, 999))

    def run_action(self, what):
        """From the focused row: Circle, then Circle on the target. Returns None when the game did not take
        it (no targeting), else the wind-up's counters, the vsyncs the action took and what was left."""
        g = self.g
        g.call("skip_charging", reset=True)
        g.press("circle", vsyncs=3, settle=1)
        try:
            self.wait(lambda: self.mode_now() == (2, smoke.CLOSED), 120, "")
        except TimeoutError:
            return None
        g.run(10)
        for _ in range(6):
            g.call("input", buttons=["circle"], vsyncs=3)
            g.run(4)
            if self.camera_mode() not in (1, 2, 3):
                break
            g.run(30)
        if self.camera_mode() in (1, 2, 3):  # no target taken: back out
            for _ in range(4):
                g.press("cross", vsyncs=3, settle=10)
                if self.mode_now()[0] != 2:
                    break
            return {"refused": True}
        n = self.wait(lambda: self.mode_now()[0] != 2, WATCHDOG, f"{what}: the action state (watchdog)", step=4)
        g.run(4)
        self.wait(lambda: self.mode_now()[0] == 1, 600, f"{what}: back in the field", step=4)
        return {"stats": g.call("skip_charging"), "vsyncs": n, "clean": self.clean()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(smoke.card_default()))
    parser.add_argument("--out", help="keep the logs here")
    parser.add_argument("--items", type=int, default=40, help="misc ids from 323 to try (default 40)")
    parser.add_argument("--only", default="spells,items,arts", help="what to cast (default spells,items,arts)")
    parser.add_argument("--first", type=int, default=FIRST_MISC, help="the first misc id (default 323)")
    parser.add_argument("--mode", default="skip", help="the option: skip (default), or off for a baseline")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "charge_skip_soak").resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_PREFABS": str(out / "prefabs"),
           "ROOD_LIGHTING": str(out / "lighting"), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1"}
    (out / "prefabs").mkdir()
    (out / "lighting").mkdir()
    if not os.environ.get("ROOD_OUT"):
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-csoak-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    snap = out / "room.snap"
    g = launch_at("save_room", card=card, fast=True, log=out / "boot.log", extra_env=env, socket_path=socket)
    try:
        reply = g.call("room_goto", zone=smoke.ROOM[0], room=smoke.ROOM[1], enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(smoke.APPROACH)
        st = g.state()
        if not check(reply["ok"] and (st["zone"], st["room"]) == smoke.ROOM, f"warped to zone 9 room 18"):
            return 1
        g.call("snapshot", action="save", path=str(snap))
    finally:
        g.close()

    r = Soak("soak", snap, card, env, out, socket, fast=True)
    g, sym = r.g, r.sym
    ran, refused, results = [], [], []
    try:
        g.call("ui_menu", modern=True)
        g.call("skip_charging", mode=args.mode, who="all", name=smoke.NAME_SECONDS)
        g.write(sym["vs_main_settings"] + smoke.INFORMATION, b"\x01")
        flags = g.read(sym["vs_main_settings"] + MENU_FLAGS, 1)[0]
        g.write(sym["vs_main_settings"] + MENU_FLAGS, bytes([flags | 1]))
        for name, size in SCHOOLS:
            for i in g.read(sym[f"vs_battle_{name}SpellIds"], size):
                a = sym["vs_main_actions"] + i * smoke.ACTION + smoke.ACTION_UNLOCKED
                g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
        misc = b"".join(struct.pack("<HBB", args.first + k, 2, k + 1) for k in range(min(args.items, 64)))
        misc += b"".join(struct.pack("<HBB", 0, 0, k + 1) for k in range(min(args.items, 64), 64))
        g.write(sym["vs_main_inventory"] + smoke.MISC, misc)
        g.run(4)

        def record(kind, what, res):
            if res is None or res.get("refused"):
                refused.append(f"{kind} {what}")
                return
            s = res["stats"]
            ran.append(f"{kind} {what}")
            hold, shown = s["phases"][1] + s["phases"][2], sum(s["phases_presented"][1:])
            results.append((kind, what, s["vsyncs"], hold, shown, res["vsyncs"], res["clean"]))
            idle = (res["clean"]["camera"], res["clean"]["effects"], res["clean"]["background"],
                    res["clean"]["name window"], res["clean"]["D_800F522C"], res["clean"]["object slots"])
            skipping = args.mode == "skip"
            if not ((not skipping or (hold <= smoke.HOLD_MAX and shown == 0))
                    and s["phases_presented"][0] == s["phases"][0] and idle == (0, 0, 0, 0, 0, [])):
                check(False, f"{kind} {what}: wind-up {s['vsyncs']} vsyncs ({s['phases']}, presented "
                      f"{s['phases_presented']}), clean {res['clean']}")
            names = [e.split(":") for e in s["name"]["names"].split()]
            if not (len(names) == 1 and names[0][0] == str(s["action"])
                    and r.action_name(s["action"]) in names[0][1]):
                check(False, f"{kind} {what}: the name window {s['name']} (action {s['action']})")

        # The spells.
        parts = set(args.only.split(","))
        for k, (name, size) in enumerate(SCHOOLS if "spells" in parts else ()):
            r.refill()
            r.shell("magick")
            for _ in range(6):
                if r.ui()["magick"].get("sub") == name:
                    break
                g.press("r2", vsyncs=3, settle=4)
            spells = next(s["spells"] for s in r.ui()["magick"]["schools"] if s.get("id") == name)
            for row, spell in enumerate(spells):
                r.refill()
                r.shell("magick")
                for _ in range(6):
                    if r.ui()["magick"].get("sub") == name:
                        break
                    g.press("r2", vsyncs=3, settle=4)
                for _ in range(24):
                    c = r.ui()["magick"]["cursor"]
                    if c == row:
                        break
                    g.press("down" if c < row else "up", vsyncs=3, settle=4)
                record("spell", f"{name} {spell['id']}", r.run_action(f"spell {spell['id']}"))
        # The items.
        for row in range(min(args.items, 64) if "items" in parts else 0):
            r.refill()
            r.shell("items")
            for _ in range(8):
                if r.ui().get("items", {}).get("category") == "misc":
                    break
                g.press("r2", vsyncs=3, settle=4)
            def listed():
                return r.ui()["items"]["list"]
            lst = listed()
            if row < len(lst) and lst[row]["use_reason"].startswith("Loading"):
                try:  # (MAINMENU is loaded again after an action)
                    r.wait(lambda: not listed()[row]["use_reason"].startswith("Loading"), 240, "", step=4)
                except TimeoutError:
                    pass
                lst = listed()
            # (Misc id 357 lists as usable with action 0, and using it crashes the game, option off too:
            # not this test's matter, passed over.)
            if row >= len(lst) or not lst[row]["usable"] or not lst[row]["action"]:
                continue
            for _ in range(70):
                c = r.ui()["items"]["cursor"]
                if c == row:
                    break
                g.press("down" if c < row else "up", vsyncs=3, settle=2)
            g.press("circle", vsyncs=3, settle=4)
            for _ in range(10):
                m = r.ui()["items"]
                if m.get("mode") == "actions" and m["actions"][m["action"]]["id"] == "use":
                    break
                g.press("down", vsyncs=3, settle=4)
            record("item", f"{lst[row]['id']} (action {lst[row]['action']})", r.run_action(f"item {lst[row]['id']}"))
        # The Break Arts: the equipped category's four.
        if "arts" in parts:
            art0, _ = r.learn_art()
            for k in range(4):
                a = sym["vs_main_actions"] + (art0 + k) * smoke.ACTION + smoke.ACTION_UNLOCKED
                g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
            r.shell("abilities")
            cat = r.ab()["equipped_category"]
            g.write(sym["vs_main_artsStatus"] + smoke.ARTS_LEARNED + cat % 10, bytes([4]))
            r.shell_close()
            for k in range(4):
                r.refill()
                r.art_menu(art0)
                for _ in range(6):
                    if r.ab()["art"] == k:
                        break
                    g.press("down", vsyncs=3, settle=4)
                record("art", str(art0 + k), r.run_action(f"art {art0 + k}"))
        log = pathlib.Path(g.log).read_text(errors="replace")
        check("[UNIMPLEMENTED]" not in log and "[crash]" not in log, "no crash, nothing unimplemented")
    except (TimeoutError, RuntimeError) as e:
        check(False, str(e))
    finally:
        r.close()
    for kind, what, wind, hold, shown, n, clean in results:
        print(f"  {kind:5} {what:28}: wind-up {wind:3} vsyncs, hold {hold:2} ({shown} presented), action {n} vsyncs")
    kinds = {k: sum(1 for x in results if x[0] == k) for k in ("spell", "item", "art")}
    want = {"spell": 10 * ("spells" in args.only), "item": 3 * ("items" in args.only), "art": "arts" in args.only}
    check(all(kinds[k] >= n for k, n in want.items()),
          f"ran {kinds} ({len(refused)} refused here: {refused})")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
