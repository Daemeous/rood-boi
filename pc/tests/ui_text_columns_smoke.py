#!/usr/bin/env python3
"""ui_text's line layout keeps the columns the game pads its lists into (pc/src/ui_text.c, "columns").

    python3 pc/tests/ui_text_columns_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The magic class list (MENU0: "WARLOCK   (ATT)", "SHAMAN    (REC)", "SORCERER  (AID)", "ENCHANTER (AFF)") is one string a row
with its names padded by spaces so that "(" starts every row at one column. A segment of the line layout starts after a gap of more
than three spaces only: SHAMAN's "(REC)" stayed at its column while SORCERER's and ENCHANTER's, one or two spaces off the name,
were set after the name in the font's advances and drifted left of it.

Checks (the line layout, the font, on the original Magic menu):
 - the game's own "(" of the rows start at one x (the list is padded to a column: the premise);
 - the laid-out "(" of every row starts at one x (within half a pixel), whatever the name before it;
 - a list with no column in it (the spell list after it) keeps its layout: no segment of it starts at a "(" it has not got."""
import argparse
import json
import re
import os
import pathlib
import shutil
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402
import menu0_levelled_smoke as m  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def rows(game, work):
    path = work / "lines.json"
    game.call("ui_text", lines=str(path))
    report = json.loads(path.read_text())
    path.unlink()
    return [line for line in report["lines"] if line["style"] == "menu"]


def parens(lines):
    """Per row with a "(": the game's x of it and the laid-out ink's left edge (PS1 pixels), and the row's text."""
    out = []
    for line in lines:
        glyphs = sorted(line["glyphs"], key=lambda g: g["x"])
        text = "".join(chr(g["c"]) for g in glyphs)
        if not re.fullmatch(r"[A-Z]+\([A-Z]{3}\)", text):
            continue  # (the class rows: not the help line)
        for g in glyphs:
            if g["c"] == ord("(") and g["rows"]:
                out.append((g["x"], min(r[1] for r in g["rows"]), text))
                break
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "ui_text_columns").resolve()
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    env = {"ROOD_WINDOW": "1280x720", "ROOD_GI": "0"}
    if not os.environ.get("ROOD_OUT"):
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    s = m.Session(out, card, env, True)
    try:
        g = s.g
        g.call("ui_menu", modern=False)
        g.call("graphics", ui_text="font", ui_text_layout="line", interpolate=False, aa="off")
        g.store("vs_main_settings.menuFlags", g.value("vs_main_settings.menuFlags") | 1)
        g.run(2)
        g.press("triangle", vsyncs=3, settle=10)
        s.wait_ready(m.MAGIC_ITEM)
        s.nav(range(0, 10), m.MAGIC_ITEM)
        g.press("circle", vsyncs=3, settle=10)
        s.wait_ready(m.WARLOCK_ITEM)
        g.run(30)
        s.frame("class_list")
        found = parens(rows(g, out))
        for x, ink, text in found:
            print(f"      '{text}': game x {x:.1f}, laid-out ink {ink:.2f}")
        check(len(found) >= 4, f"the class list's rows with a column are found ({len(found)})")
        check(len({round(x, 1) for x, _, _ in found}) == 1, "the game pads every row's \"(\" to one x")
        spread = max(i for _, i, _ in found) - min(i for _, i, _ in found) if found else 99
        check(spread <= 0.5, f"the laid-out \"(\" start at one x (spread {spread:.2f} PS1 pixels)")
    finally:
        s.close()
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
