#!/usr/bin/env python3
"""Modern menus, Ashley tab: ui_menu_opacity holds when the menu opens
straight onto the Ashley tab (pc/src/gl_present.c, vk_present.c menu
backdrop; pc/src/ui/ashley_bridge.c).

    python3 pc/tests/ui_ashley_backdrop_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The Ashley tab's model presents draw the world behind from a picture the
presenter keeps of the world while the menu is up (backdrop_keep), under
the menu's ground at its opacity; without a picture they draw the ground
alone, opaque. So the model's build waits (backdrop_ready, at most 20
ticks) for a kept picture taken with the field HUD slid out. The opaque
first open the user saw was not reproduced here (the build's other gates
usually let a present be kept first); what fails without the wait is the
picture back from an original screen (the HUD in it).

From a copy of the card inside card_fixtures.card_guard (skipped without
one; the player's cards fingerprinted before and after), in the save room, ui_menu_opacity 0.3, 4:3 in a 960x720
window: the menu's first open lands on the Ashley tab; once the model shows,
RPC ui_menu backdrop_kept is 1, backdrop_built (the keep count and the
count the wait asked for when the model was built) shows the build came
after a kept, HUD-free picture, and the model column's empty left strip shows
the world (not the flat ground colour: varied, off the ground), close to
the same strip on the Equipment tab visited afterwards (the world behind at
the same opacity). Back from the original Status (Square, Cross: MAINMENU
resident, the model built on the first tick back) a picture is kept and the
field HUD is not in it (the HUD's corner as on the first open; backdrop_built as on the first open).
Before: no backdrop_kept / backdrop_built fields; back from Status the
picture showed the HUD (it slides back in under the original screen).
--out keeps the crops (game imagery: keep it out of git)."""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from ui_ashley_stability_smoke import presented  # noqa: E402

OPACITY = 0.3


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="keep the crops here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_ashley_backdrop"
    out.mkdir(parents=True, exist_ok=True)
    # the player's cards fingerprinted before and after (card_guard), the game on a temp copy
    with card_guard(out / "cards", source=args.card) as cards:
        return run(args, out, cards.card1)


def run(args, out, card):
    import numpy as np
    from PIL import Image
    settings = out / "settings.cfg"
    settings.write_text(f"ui_menus 1\nui_menu_opacity {OPACITY}\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings)}
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    g = launch_at("save_room", card=card, fast=False, log=out / "game.log", extra_env=env)
    try:
        def ui():
            return g.call("ui_menu")

        def wait(pred, limit, what):
            for _ in range(limit):
                if pred():
                    return
                g.run(1)
            raise TimeoutError(f"{what}: {ui()}")

        g.call("ui_menu", modern=True)
        g.run(10)
        g.press("triangle", vsyncs=3, settle=2)
        wait(lambda: ui().get("visible"), 100, "the menu")
        check(ui().get("tab") == "ashley", f"the menu's first open lands on the Ashley tab ({ui().get('tab')})")
        wait(lambda: (ui().get("ashley") or {}).get("model") == 2, 300, "the Ashley model")
        g.run(8)
        kept = ui().get("backdrop_kept")
        built = ui().get("backdrop_built")
        rect = ui()["ashley"]["model_rect"]
        model = presented(g, out / "ashley")
        # the original Status and back (MAINMENU resident: the model is built on the first tick back)
        g.press("square", vsyncs=3, settle=120)
        g.press("cross", vsyncs=3, settle=2)
        wait(lambda: ui().get("visible") and (ui().get("ashley") or {}).get("model") == 2, 300,
             "back from Status: the Ashley model")
        g.run(8)
        kept_back = ui().get("backdrop_kept")
        built_back = ui().get("backdrop_built")
        back = presented(g, out / "ashley-back")
        g.press("r1", vsyncs=3, settle=2)
        wait(lambda: ui().get("tab") == "equipment", 60, "Equipment")
        g.run(20)
        other = presented(g, out / "equipment")
        g.press("triangle", vsyncs=3, settle=4)
        wait(lambda: not ui().get("visible"), 400, "the menu closes")
        x, y, w, h = (int(v) for v in rect)  # 960x720 window: layout units are pixels
        box = (slice(y + 8, y + int(h * 0.95)), slice(x + 4, x + 48))
        if args.out:
            for name, image in (("ashley", model), ("ashley-back", back), ("equipment", other)):
                Image.fromarray(image.astype(np.uint8)).save(out / f"{name}.png")
        m, e = model[box].astype(np.float32), other[box].astype(np.float32)
        spread = float(m.std(axis=(0, 1)).max())
        flat = float(np.abs(m - m.mean(axis=(0, 1))).max(axis=2).mean())
        to_other = float(np.abs(m - e).mean())
        check(kept == 1, f"RPC ui_menu backdrop_kept = 1 with the model shown ({kept})")
        # the first open's wait (backdrop_ready): the model built only once a picture was kept, and two
        # keeps after the last tick that drew the field HUD (need 0: no tick drew it during the wait)
        check(isinstance(built, list) and built[0] > 0 and built[0] >= built[1],
              f"the first open's model built after a kept, HUD-free picture (backdrop_built [kept, need] {built})")
        check(spread > 3 and flat > 2,
              f"the model column's left strip shows the world behind, not a flat ground (colour spread {spread:.1f}, "
              f"mean distance from its mean {flat:.1f})")
        check(to_other < 12,
              f"the strip matches the Equipment tab's (opacity {OPACITY}): mean difference {to_other:.1f}")
        # The field HUD's corner (HP, MP, RISK: PS1 10..110 x 8..36) behind the menu: back from Status the
        # picture behind is the first open's (the HUD slid out), not one with the HUD back in.
        hud = (slice(24, 108), slice(30, 330))
        moved = float((np.abs(back[hud] - model[hud]).max(axis=2) > 40).mean())
        check(isinstance(built_back, list) and built_back[0] > 0 and built_back[0] >= built_back[1],
              f"back from Status: the model built after a kept picture (backdrop_built {built_back})")
        check(kept_back == 1 and moved < 0.02,
              f"back from the original Status: a picture kept (backdrop_kept {kept_back}), without the field HUD "
              f"({moved:.1%} of the HUD's corner differs from the first open's by more than 40 levels)")
    finally:
        g.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
