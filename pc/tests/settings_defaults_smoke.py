#!/usr/bin/env python3
"""The settings file keeps the keys that differ from the defaults (pc/src/project_settings.cpp pc_settings_save).

    python3 pc/tests/settings_defaults_smoke.py [--out DIR]

pc_settings_save wrote every key, so a value the user never chose (a temporary default: ui_text_layout glyph) was frozen into
the file and stayed after the default moved. Now a one-line key whose line equals the default (the settings as the game starts,
before the file) is left out; a file written by an older build, with every key, still loads, and what differs from the default
is kept. With the title menu's game (no card needed):
 - a file with a default-valued key (ui_text_layout line) and a changed one (ui_text_bubble_align left) loads both;
 - after RPC settings save the file has the changed key and not the default one (a positive control: it had both before);
 - a key the user sets back to its default (ui_text_bubble_align centre) leaves the file at the next save, and a key moved off the
   default (ui_text_layout glyph) is written;
 - the keys the file must carry (aa, lighting_preset) are still written."""
import argparse
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import Game, OUT  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def keys(path):
    return {line.split()[0]: line for line in path.read_text().splitlines() if line.strip() and not line.startswith("#")}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out")
    args = parser.parse_args()
    out = (pathlib.Path(args.out) if args.out else OUT / "settings_defaults").resolve()
    out.mkdir(parents=True, exist_ok=True)
    cfg = out / "settings.cfg"
    cfg.write_text("ui_text_layout line\nui_text_bubble_align left\naa_downsample lanczos\n")
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(log=out / "game.log", extra_env={"ROOD_SETTINGS": str(cfg), "ROOD_WINDOW": "640x480"})
    try:
        game.run(60)
        check(game.call("graphics")["ui_text_bubble_align"] == "left", "the file's changed key loaded (ui_text_bubble_align left)")
        check(game.call("graphics")["ui_text_layout"] == "line", "the file's default-valued key loaded (ui_text_layout line)")
        game.call("settings", action="save")
        saved = keys(cfg)
        check(saved.get("ui_text_bubble_align") == "ui_text_bubble_align left", "the key that differs is kept")
        check("ui_text_layout" not in saved, "a key at its default is not written (ui_text_layout line)")
        check("aa" in saved and "lighting_preset" in saved, "aa and lighting_preset are still written")
        check(len(saved) < 40, f"the file is short ({len(saved)} keys; every key was written before)")
        game.call("graphics", ui_text_bubble_align="centre", ui_text_layout="glyph")
        game.call("settings", action="save")
        saved = keys(cfg)
        check("ui_text_bubble_align" not in saved, "a key set back to its default leaves the file")
        check(saved.get("ui_text_layout") == "ui_text_layout glyph", "a key moved off the default is written")
        game.call("graphics", ui_text_layout="line")
        game.call("settings", action="save")
        check("ui_text_layout" not in keys(cfg), "and leaves it again at the default")
    finally:
        game.close()
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
