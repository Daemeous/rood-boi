#!/usr/bin/env python3
"""The editor shell (pc/src/editor_shell.cpp, editor_commands.cpp; design
pc/docs/plans/2026-10-30-editor-shell-design.md): Dear ImGui docking, the game
picture as the Game panel, the default layout, the command registry and
palette, plugin windows docking.

    python3 pc/tests/editor_shell_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

Continues from a copy of the card into its save room with the editor open
(editor_selection_smoke's session: a point light, a candle and a fog volume
placed around Ashley, a test plugin with a tool window and a command). Each
section checks one part:

- dock: docking is on, the dock space exists, the Game panel is docked in the
  centre node with the Outliner left and Details right; the picture (the view
  rectangle) is inside the Game panel at the game's aspect (4:3);
- pick: a click on the light in the Game panel selects it, through the RPC
  and with real mouse events (the panel is a hit-test hole: no window is
  hovered over the picture), and a click on nothing clears;
- commands: the registry lists the panels', the tools' and the menus'
  commands; running window.room_browser docks the Room Browser into the
  bottom node and the picture makes room; the view mode command applies;
- palette: Ctrl+Shift+P opens it over the Game panel; typing ranks the Room
  Browser first for "room b"; Enter runs it (the window closes again);
  Escape closes the palette;
- plugin: the test plugin's rood.dev.window docks into the bottom node and
  its rood.dev.command is listed (plugin) and runs; its window command exists;
- reset: Reset layout rebuilds the default (the Game rectangle as at the
  start) and re-docks the tool windows; the layout's proportions are settings
  (RPC graphics editor_shell_*; a change and a reset move the panels);
- closed: F1 (editor_ui visible false) gives the picture the window back (the
  view rectangle as before the editor opened); reopening keeps the layout;
- ini: the layout is saved to the test's imgui.ini ([ShellLayout]).
"""
import argparse
import os
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
import editor_selection_smoke as es  # noqa: E402

FIXTURE = ROOT / "pc/tests/lua/plugins/test.shell"
PLUGIN = "test.shell"
TOOL = "Shell test tool###lua:test.shell/tool"


def inside(inner, outer, slack=1):
    return (inner[0] >= outer[0] - slack and inner[1] >= outer[1] - slack
            and inner[0] + inner[2] <= outer[0] + outer[2] + slack and inner[1] + inner[3] <= outer[1] + outer[3] + slack)


def shell(s, **params):
    return s.game.call("editor_shell", **params)


def check_dock(s):
    g = s.game
    st = shell(s)
    s.check(st["docking"] and st["dockspace"], f"docking on, the dock space exists ({st['docking']}, {st['dockspace']})")
    nodes = st["nodes"]
    s.check(all(nodes[k] != "00000000" for k in ("left", "right", "bottom", "centre")), f"four layout nodes ({nodes})")
    game = st["game"]
    s.check(game["shown"] and game["visible"] and game["docked"] and game["node"] == nodes["centre"],
            f"the Game panel is docked in the centre node ({game})")
    w = st["windows"]
    s.check(w["Outliner"]["docked"] and w["Outliner"]["node"] == nodes["left"], f"the Outliner is docked left ({w['Outliner']})")
    s.check(w["Details"]["docked"] and w["Details"]["node"] == nodes["right"], f"Details is docked right ({w['Details']})")
    s.check(w["Outliner"]["rect"][0] < game["rect"][0] < w["Details"]["rect"][0],
            "left to right: Outliner, Game, Details")
    view = g.call("editor_select")["view"]
    s.check(inside(view, game["rect"]), f"the picture {view} is inside the Game panel {game['rect']}")
    s.check(view == [float(v) for v in st["view"]], "editor_shell reports the same view rectangle")
    s.check(abs(view[2] / view[3] - 4 / 3) < 0.01, f"the picture keeps the game's aspect ({view[2]}x{view[3]})")
    s.check(view[2] >= game["rect"][2] - 2 or view[3] >= game["rect"][3] - 2, "the picture fills the panel's width or height")
    s.first_rect = game["rect"]


def check_pick(s):
    g = s.game
    o = s.objects()
    xy = o[s.light]["screen"]
    r = s.select(action="click", x=xy[0], y=xy[1])
    s.check(r["under"] == s.light and r["selection"] == [s.light], f"RPC click on the light selects it ({r['selection']})")
    s.select(action="clear")
    s.mouse(xy, 1, False)  # the pointer there first: what Dear ImGui hovers
    st = shell(s)
    s.check(st["hovered"] is None and not st["capture_mouse"],
            f"over the picture no window is hovered (the Game panel is a hit-test hole): {st['hovered']}, {st['capture_mouse']}")
    s.click(xy)
    g.run(3)
    r = s.select()
    s.check(r["selection"] == [s.light], f"a real mouse click in the Game panel selects the light ({r['selection']})")
    xy = o[s.actor]["screen"]
    under = s.select(action="click", x=xy[0], y=xy[1])["under"]  # the candle, or its own light's icon over its pivot
    s.select(action="clear")
    s.click(xy)
    g.run(3)
    s.check(under in (s.actor, None) or under.startswith("light:"), f"the RPC click at the candle finds it or its light ({under})")
    s.check(s.select()["selection"] == ([under] if under else []), f"a real click there selects the same ({s.select()['selection']})")
    s.mouse([o[s.light]["screen"][0], 40], 1, False)  # the toolbar: a window
    st = shell(s)
    s.check(st["hovered"] == "##toolbar" and st["capture_mouse"], f"over the toolbar Dear ImGui hovers it ({st['hovered']})")
    view = r["view"]
    empty = None
    for fx, fy in ((0.5, 0.92), (0.08, 0.92), (0.92, 0.92), (0.5, 0.08), (0.08, 0.5)):
        p = [view[0] + view[2] * fx, view[1] + view[3] * fy]
        if all(es.distance(p, x["screen"]) > 80 for x in o.values() if x["screen"]) \
                and s.select(action="click", x=p[0], y=p[1], shift=True).get("under") is None:
            empty = p
            break
    if empty is None:
        s.check(False, "no empty spot in the view to click")
        return
    s.click(empty)
    g.run(3)
    # Click-to-inspect (editor_inspect.cpp): a plain click on a room surface selects that surface in
    # place of the lights; one on nothing at all clears.
    sel = s.select()["selection"]
    s.check(all(x.startswith(("surface:", "model:")) for x in sel) and len(sel) <= 1,
            f"a real click on no object clears the selection or selects the surface there ({sel})")


def check_commands(s):
    g = s.game
    r = g.call("editor_command")
    ids = {c["id"]: c for c in r["commands"]}
    wanted = ["window.room_browser", "window.outliner", "window.details", "window.cutscene", "shell.reset_layout",
              "shell.palette", "tool.move", "tool.scale", "edit.undo", "file.save_area", "view.mode.3", "window.details.graphics"]
    missing = [w for w in wanted if w not in ids]
    s.check(not missing and r["count"] >= 50, f"the registry lists the panels, tools and menu items ({r['count']}; missing {missing})")
    s.check(ids["window.room_browser"]["shortcut"] == "Ctrl+R" and ids["window.room_browser"]["checked"] is False,
            f"a command has its shortcut and toggle state ({ids['window.room_browser']})")
    before = shell(s)["game"]["rect"]
    r = g.call("editor_command", action="run", id="window.room_browser")
    g.run(6)
    st = shell(s)
    rb = shell(s, action="window", name="Room Browser")
    s.check(r["ran"] and rb["exists"] and rb["docked"] and rb["node"] == st["nodes"]["bottom"],
            f"window.room_browser opens the Room Browser docked in the bottom node ({rb})")
    s.check(st["game"]["rect"][3] < before[3] - 40 and inside(g.call("editor_select")["view"], st["game"]["rect"]),
            f"the Game panel made room for it ({before} -> {st['game']['rect']}) and the picture stays inside")
    s.check(g.call("editor_command", action="query", query="room brow")["commands"][0]["id"] == "window.room_browser",
            "query ranks the Room Browser first for 'room brow'")
    g.call("editor_command", action="run", id="view.mode.3")
    g.run(2)
    ids = {c["id"]: c for c in g.call("editor_command")["commands"]}
    s.check(ids["view.mode.3"]["checked"] is True and ids["view.mode.0"]["checked"] is False,
            "view.mode.3 sets the base colour view mode (its toggle state)")
    g.call("editor_command", action="run", id="view.mode.0")
    try:
        g.call("editor_command", action="run", id="no.such.command")
        s.check(False, "an unknown command is refused")
    except RpcError as e:
        s.check("no command" in str(e), f"an unknown command is refused ({e})")
    g.call("editor_command", action="run", id="window.room_browser")  # closes it again
    g.run(3)


def palette_key(s):
    """Ctrl+Shift+P as held modifiers, released after (a key event's mod field alone would leave them held in ImGui)."""
    s.game.call("editor_input", keys=["p"], hold=["ctrl", "shift"])
    s.game.run(3)
    s.game.call("editor_input", release=True)
    s.game.run(3)


def check_palette(s):
    g = s.game
    palette_key(s)
    st = shell(s)
    s.check(st["palette"]["open"], "Ctrl+Shift+P opens the command palette")
    pal = shell(s, action="window", name="Command palette##palette")
    s.check(pal["exists"] and inside([pal["rect"][0], pal["rect"][1], pal["rect"][2], 1], st["game"]["rect"], 4),
            f"the palette floats over the Game panel ({pal['rect']} in {st['game']['rect']})")
    g.call("editor_input", text="room b")
    g.run(4)
    st = shell(s)
    s.check(st["palette"]["query"] == "room b" and st["palette"]["first"] == "window.room_browser",
            f"typing filters: 'room b' ranks the Room Browser first ({st['palette']})")
    s.keys(["enter"])
    g.run(4)
    st = shell(s)
    rb = shell(s, action="window", name="Room Browser")
    s.check(not st["palette"]["open"] and rb["active"] and not rb["hidden"], f"Enter runs it and closes the palette ({rb})")
    g.call("editor_command", action="run", id="window.room_browser")  # close it again
    palette_key(s)
    s.check(shell(s)["palette"]["open"], "opened again")
    s.keys(["escape"])
    g.run(2)
    st = shell(s)
    s.check(not st["palette"]["open"] and g.call("editor_ui")["visible"], "Escape closes the palette, not the editor")


def check_plugin(s):
    g = s.game
    r = g.lua("return granted, ran, command_id", plugin=PLUGIN)
    s.check(r.get("results", [None])[0] is True and r["results"][2] == "test.shell/hello", f"the plugin is granted ({r})")
    g.call("lua", action="window", id="test.shell/tool", open=True)
    g.run(4)
    w = shell(s, action="window", name=TOOL)
    st = shell(s)
    s.check(w["exists"] and w["docked"] and w["node"] == st["nodes"]["bottom"],
            f"the plugin's rood.dev.window docks into the bottom node ({w})")
    ids = {c["id"]: c for c in g.call("editor_command")["commands"]}
    s.check("test.shell/hello" in ids and ids["test.shell/hello"]["plugin"] and ids["test.shell/hello"]["category"] == "Test",
            f"the plugin's command is listed ({ids.get('test.shell/hello')})")
    s.check("window.test.shell/tool" in ids, "its window has a command")
    g.call("editor_command", action="run", id="test.shell/hello")
    g.run(2)
    s.check(g.lua("return ran", plugin=PLUGIN)["results"] == [1], "running it calls the plugin's function")
    g.call("editor_shell", action="palette", open=True, query="hello")
    g.run(2)
    s.check(shell(s)["palette"]["first"] == "test.shell/hello", "the palette finds it")
    g.call("editor_shell", action="palette", open=False)


def check_reset(s):
    g = s.game
    g.call("lua", action="window", id="test.shell/tool", open=False)
    g.run(3)
    rect = shell(s)["game"]["rect"]
    s.check(rect == s.first_rect, f"with every tool window closed the Game panel is back to its first size ({rect} vs {s.first_rect})")
    g.call("graphics", editor_shell_left=0.3, editor_shell_right=0.2)
    gr = g.call("graphics")
    s.check(abs(gr["editor_shell_left"] - 0.3) < 1e-4, f"the layout's proportions are graphics keys ({gr.get('editor_shell_left')})")
    shell(s, action="reset")
    g.run(6)
    st = shell(s)
    w = st["windows"]
    s.check(st["reset_count"] == 1 and abs(w["Outliner"]["rect"][2] - 1280 * 0.3) < 24 and abs(w["Details"]["rect"][2] - 1280 * 0.2) < 24,
            f"Reset layout rebuilds with the new proportions ({w['Outliner']['rect']}, {w['Details']['rect']})")
    g.call("graphics", editor_shell_left=0.22, editor_shell_right=0.26)
    shell(s, action="reset")
    g.run(6)
    st = shell(s)
    s.check(st["game"]["rect"] == s.first_rect, f"and back to the default ({st['game']['rect']} vs {s.first_rect})")
    g.call("lua", action="window", id="test.shell/tool", open=True)
    g.call("editor_command", action="run", id="window.cutscene")
    g.run(6)
    st = shell(s)
    tool = shell(s, action="window", name=TOOL)
    cut = shell(s, action="window", name="Cutscene")
    s.check(tool["docked"] and tool["node"] == st["nodes"]["bottom"] and cut["docked"] and cut["node"] == st["nodes"]["bottom"],
            f"after the reset the tool windows dock below again ({tool}, {cut})")
    line = g.call("settings", get="editor_shell_left").get("line") or ""
    s.check(line.startswith("editor_shell_left 0.22"), f"editor_shell_left is a settings key ({line!r})")
    g.call("editor_command", action="run", id="window.cutscene")
    g.call("lua", action="window", id="test.shell/tool", open=False)
    g.run(3)


def check_closed(s):
    g = s.game
    layout = shell(s)["game"]["rect"]
    g.call("editor_ui", visible=False)
    g.run(6)
    st = shell(s)
    view = st["view"]
    s.check(st["inset"] == [0, 0, 0, 0] and not st["game"]["shown"], f"editor closed: no inset ({st['inset']})")
    s.check(view == s.closed_view, f"the picture is back where it was before the editor opened ({view} vs {s.closed_view})")
    g.call("editor_ui", visible=True)
    g.run(6)
    st = shell(s)
    s.check(st["game"]["rect"] == layout and st["reset_count"] == 2, f"reopened: the layout as left ({st['game']['rect']})")


def check_ini(s):
    ini = pathlib.Path(os.environ.get("ROOD_IMGUI_INI") or (ROOT / "pc/build/boot/imgui.ini"))
    s.game.call("editor_ui", visible=True)
    s.game.run(2)
    r = shell(s, action="save_ini")
    s.check(r["saved"] and ini.exists(), f"save_ini writes the layout ({ini})")
    s.ini = ini


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(es.pinned_card()))
    ap.add_argument("--out")
    ap.add_argument("--only", help="comma-separated section names")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "editor_shell"
    plugins = out / "plugins"
    shutil.rmtree(plugins, ignore_errors=True)
    shutil.copytree(FIXTURE, plugins / PLUGIN)
    ini = out / "imgui.ini"
    if ini.exists():
        ini.unlink()
    os.environ["ROOD_IMGUI_INI"] = str(ini)
    s = es.start(args, "editor_shell", extra_env={"ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(out / "plugin_data"),
                                                   "ROOD_IMGUI_INI": str(ini)})
    if s is None:
        return 77
    if s.failures:
        return es.finish(s, "editor shell")
    g = s.game
    # The picture before the editor opened (start opens it): close, read, reopen.
    g.call("editor_ui", visible=False)
    g.run(6)
    s.closed_view = shell(s)["view"]
    g.call("editor_ui", visible=True)
    g.call("lua", action="window", id="test.shell/tool", open=False)  # (open at first: check_plugin opens it)
    g.run(6)
    sections = [check_dock, check_pick, check_commands, check_palette, check_plugin, check_reset, check_closed, check_ini]
    only = set(args.only.split(",")) if args.only else None
    for section in sections:
        name = section.__name__[6:]
        if only and name not in only:
            continue
        print(f"--- {name}", flush=True)
        try:
            section(s)
        except Exception as e:  # noqa: BLE001
            s.check(False, f"{name}: {type(e).__name__}: {e}")
    status = es.finish(s, "editor shell: docking, the Game panel, the layout, the palette, plugins")
    if hasattr(s, "ini"):
        text = s.ini.read_text() if s.ini.exists() else ""
        ok = "[ShellLayout][Data]" in text and re.search(r"^Version=1$", text, re.M) and "[Window][Game]" in text
        print(("ok:   " if ok else "FAIL: ") + f"the layout is saved in the test's imgui.ini ({s.ini})")
        if not ok:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
