#!/usr/bin/env python3
"""Emissive entries in the editor (pc/src/emissive_editor.cpp): Duplicate and
Delete, by RPC and with the panel's buttons, each one undo step.

    python3 pc/tests/emissive_entries_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The save room with the editor open (editor_selection_smoke.py's start; the
emissive user layer is the scratch lights directory's emissive.json).

- rpc: an entry of yours duplicates under a new id with " copy" after its
  name (one undo step "Duplicate emissive entry"; undo takes the copy away,
  redo brings it back); remove deletes it (one undo step "Delete emissive
  entry"; undo brings it back);
- panel: on the Emissive tab, Delete... asks first (the entry stays while the
  confirm shows); Yes deletes it, one undo step; Cancel keeps it; a curated
  entry has Duplicate but no Delete, and its copy is yours;
- disk: undo back to the saved layer leaves nothing unsaved; the layer read
  again from its file (a hand edit, reload) is no undo step.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import centre, finish, pinned_card, start  # noqa: E402

TEST = "U950"
DISK = "U951"


def entries(s):
    return {e["id"]: e for e in s.game.call("emissive")["entries"]}


def ui(s):
    s.game.run(3)
    return s.game.call("emissive", action="ui")


def check_rpc(s, curated):
    g = s.game
    g.call("emissive", action="set", entry={"id": TEST, "name": "entries test", "match": curated["match"], "mask": {},
                                            "color": [1, 0.5, 0.25], "strength": 2, "light": 0})
    copy = g.call("emissive", action="duplicate", id=TEST)
    e, h = entries(s), s.history()
    s.check(copy["id"] != TEST and copy["id"] in e and e[copy["id"]]["name"] == "entries test copy"
            and e[copy["id"]]["strength"] == 2 and e[copy["id"]]["origin"] == "user"
            and h["undo"] == "Duplicate emissive entry",
            f"duplicate: a copy of yours under {copy['id']}, one undo step ({h['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(copy["id"] not in entries(s), "undo takes the copy away")
    g.call("editor_undo", action="redo")
    s.check(copy["id"] in entries(s), "redo brings it back")
    g.call("emissive", action="remove", id=copy["id"])
    s.check(copy["id"] not in entries(s) and s.history()["undo"] == "Delete emissive entry",
            f"remove: gone, one undo step ({s.history()['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(copy["id"] in entries(s), "undo brings the deleted entry back")
    g.call("emissive", action="remove", id=copy["id"])


def check_disk(s, curated):
    """The layer read again from its file is no undo step; undo back to what is saved is not unsaved."""
    g = s.game
    g.call("emissive", action="set", entry={"id": DISK, "name": "disk test", "match": curated["match"], "mask": {},
                                            "color": [1, 1, 1], "strength": 1, "light": 0})
    g.call("emissive", action="save")
    path = pathlib.Path(g.call("emissive")["user"])
    s.check(not g.call("emissive")["unsaved"], "saved: nothing unsaved")
    g.call("emissive", action="set", id=DISK, name="disk test changed")
    s.check(g.call("emissive")["unsaved"], "an edit: unsaved")
    g.call("editor_undo", action="undo")
    g.run(2)
    s.check(not g.call("emissive")["unsaved"] and entries(s)[DISK]["name"] == "disk test",
            "undone back to the saved layer: nothing unsaved (hand edits reload again)")
    done = s.history()["done"]
    text = path.read_text().replace('"disk test"', '"disk test edited"')
    path.write_text(text)
    g.call("emissive", action="reload")
    g.run(3)
    h = s.history()
    s.check(entries(s)[DISK]["name"] == "disk test edited" and h["done"] == done,
            f"the file read again: no undo step ({h['done']} vs {done})")
    g.call("emissive", action="remove", id=DISK)
    g.call("emissive", action="save")


def check_panel(s, curated):
    g = s.game
    g.call("editor_ui", details="emissive")
    g.call("emissive", action="select", id=TEST)
    state = ui(s)
    if not s.check("delete" in state["rects"] and "duplicate" in state["rects"] and state["selected"] == TEST,
                   f"the panel shows the entry's Duplicate and Delete... ({state})"):
        return
    s.click(centre(state["rects"]["delete"]))
    state = ui(s)
    s.check(state["confirm"] and TEST in entries(s), "Delete... asks first; the entry stays")
    s.click(centre(state["rects"]["confirm_cancel"]))
    state = ui(s)
    s.check(not state["confirm"] and TEST in entries(s), "Cancel keeps it")
    s.click(centre(state["rects"]["delete"]))
    state = ui(s)
    done = s.history()["done"]
    s.click(centre(state["rects"]["confirm_yes"]))
    ui(s)
    h = s.history()
    s.check(TEST not in entries(s) and h["done"] == done + 1 and h["undo"] == "Delete emissive entry",
            f"Yes deletes it, one undo step ({h['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(TEST in entries(s), "undo brings it back")
    g.call("emissive", action="select", id=curated["id"])
    state = ui(s)
    s.check("duplicate" in state["rects"] and "delete" not in state["rects"],
            f"a curated entry: Duplicate, no Delete ({sorted(state['rects'])})")
    before = set(entries(s))
    s.click(centre(state["rects"]["duplicate"]))
    ui(s)
    new = sorted(set(entries(s)) - before)
    s.check(len(new) == 1 and entries(s)[new[0]]["origin"] == "user"
            and entries(s)[new[0]]["name"] == (curated["name"] or curated["id"]) + " copy",
            f"its copy is yours ({new})")
    for ident in new + [TEST]:
        g.call("emissive", action="remove", id=ident)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    args = parser.parse_args()
    s = start(args, "emissive_entries")
    if s is None:
        return 77
    try:
        curated = next((e for e in s.game.call("emissive")["entries"] if e["origin"] == "curated"), None)
        if s.check(curated is not None, "a curated entry to copy the match from"):
            print("-- rpc", flush=True)
            check_rpc(s, curated)
            print("-- disk", flush=True)
            check_disk(s, curated)
            print("-- panel", flush=True)
            check_panel(s, curated)
    finally:
        status = finish(s, "emissive entries duplicate and delete (with a confirm), each one undo step")
    return status


if __name__ == "__main__":
    sys.exit(main())
