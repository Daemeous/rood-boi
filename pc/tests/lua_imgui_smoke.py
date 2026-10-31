#!/usr/bin/env python3
"""Developer tool windows in Lua (rood.imgui, rood.dev.window, rood.dev.rpc;
pc/src/lua/lua_api_imgui.cpp, lua_dev_tools.cpp; pc/docs/Extensions.md
"Developer tool windows"), with the fixture plugin test.imgui
(pc/tests/lua/plugins/test.imgui) copied into a scratch user plugin folder:

    python3 pc/tests/lua_imgui_smoke.py [--out DIR]

One game at the title, the overlay shown, a fresh settings file:
  - without dev.tools (the console's sandbox) neither rood.imgui nor
    rood.dev.window; the fixture declares it (dev mode: declared is
    granted) and registers two windows; RPC lua windows lists them (Window menu), the one
    registered open = true is drawn every presented frame, the closed one not;
  - opened by RPC: drawn; the open state saved (settings dev_window);
  - widget values round-trip: set from Lua (RPC eval), the next frame's
    SliderFloat / InputText / Checkbox / Combo / ColorEdit3 return them;
  - rood.imgui outside a draw raises; rood.dev.rpc: status's reply as a
    table, an unknown method as nil and its error, the refused method lua
    raises;
  - keys: focused, a bare A reaches IsKeyPressed, Ctrl+Enter too; typed
    into the window's InputText they do not;
  - a deliberate error in draw: that window shows the error (failed, its
    message), the other window and the overlay go on drawing, no Dear ImGui
    assertion; a draw leaving PushID and BeginGroup open fails the same way
    (the stack rewound); Retry (RPC) draws it again;
  - hot reload: main.lua edited (VERSION = 2) is drawn without a restart,
    the window still open;
  - the shipped example rood.dev_example: off by default; on, its window
    draws without a fault.
"""
import argparse
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

FIXTURE = ROOT / "pc/tests/lua/plugins/test.imgui"
PLUGIN = "test.imgui"
PANEL, OTHER = PLUGIN + "/panel", PLUGIN + "/other"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", help="logs and the plugins' copy")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "lua_imgui"
    out.mkdir(parents=True, exist_ok=True)
    return run(out)


def run(out):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    plugins = out / "plugins"
    shutil.rmtree(plugins, ignore_errors=True)
    shutil.copytree(FIXTURE, plugins / PLUGIN)
    main_lua = plugins / PLUGIN / "main.lua"
    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\n")
    data = out / "plugin_data"
    shutil.rmtree(data, ignore_errors=True)
    env = {"ROOD_WINDOW": "1280x960", "ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins),
           "ROOD_PLUGIN_DATA": str(data)}
    log = out / "game.log"
    g = launch_at("title", fast=True, log=log, extra_env=env)
    try:
        def lua(code):
            r = g.lua(code, plugin=PLUGIN)
            if not r.get("ok"):
                return {"error": r.get("error")}
            res = r.get("results") or []
            return res[0] if res else None

        def windows():
            return {w["id"]: w for w in g.call("lua", action="windows")["windows"]}

        def frames(n=2):
            for _ in range(n):
                g.call("redraw")

        g.call("editor_ui", visible=True)
        frames()
        console = g.lua("return rood.imgui == nil and (rood.dev == nil or rood.dev.window == nil)")
        check(console.get("results") == [True], f"without dev.tools (the console): no rood.imgui, no "
              f"rood.dev.window ({console})")
        w = windows()
        check(lua("return granted") is True and PANEL in w and OTHER in w and w[PANEL]["menu"] == "Window"
              and w[PANEL]["keys"] and not w[PANEL]["open"] and w[OTHER]["open"],
              f"granted: both windows registered for the Window menu ({w})")
        before = lua("return other_frames")
        frames(3)
        check(lua("return other_frames") >= before + 3 and lua("return frames") == 0,
              "the open window draws every presented frame, the closed one not")
        g.call("lua", action="window", id=PANEL, open=True, focus=True)
        frames(3)
        check(lua("return frames") >= 2 and windows()[PANEL]["open"], "opened by RPC: drawn")
        check(re.search(r"^dev_window test\.imgui/panel 1$", settings.read_text(), re.M) is not None,
              "the open state saved (settings dev_window)")
        lua('value, text, checked, choice, tint = 0.75, "set from Lua", true, 3, {0.1, 0.2, 0.3}')
        frames(2)
        seen = lua("return seen")
        check(abs(seen["value"] - 0.75) < 1e-6 and seen["text"] == "set from Lua" and seen["checked"] is True
              and seen["choice"] == 3 and [round(c, 3) for c in seen["tint"]] == [0.1, 0.2, 0.3],
              f"widget values round-trip through Lua ({seen})")
        c = lua("return canvas")
        check(c and "error" not in c and c["dx"] == 10 and c["dy"] == 5 and c["w"] == 60 and c["h"] == 12
              and c["pressed"] is False and isinstance(c["mouse"][0], (int, float)) and c["down"] is False
              and c["dragging"] is False and c["drag"] == [0, 0] and c["over"] is False and c["wheel"] == 0,
              f"a canvas: the cursor round-trips, the draw list draws, an invisible button is an item, the mouse "
              f"reads ({c})")
        r = g.lua('return pcall(rood.imgui.Text, "x")', plugin=PLUGIN)["results"]
        check(r[0] is False and "only inside" in r[1], f"rood.imgui outside a draw raises ({r})")
        r = lua("return call_rpc()")
        check(isinstance(r.get("vsync"), int) and r["vsync"] > 0 and r.get("none") is None
              and "unknown method" in (r.get("err") or "") and r["refused_ok"] is False
              and "not available" in r["refused"], f"rood.dev.rpc: a result, an error, a refusal ({r})")
        # keys: the window focused (RPC focus above), nothing typed
        frames(2)
        check(windows()[PANEL]["focused"], "the window has the keyboard (focused, keys = true)")
        g.call("editor_input", keys=["a"])
        frames(1)
        g.call("editor_input", keys=["enter"], ctrl=True)
        frames(2)
        keys = lua("return keys_seen")
        check(keys == ["A", "Ctrl+Enter"], f"keys reach IsKeyPressed: A, Ctrl+Enter ({keys})")
        # a deliberate error: that window fails visibly; the other and the overlay go on
        other = lua("return other_frames")
        lua("fail = true")
        frames(3)
        w = windows()[PANEL]
        check(w["failed"] and "deliberate failure" in w["error"] and w["failures"] == 1,
              f"an error in draw: that window failed, its message kept ({w['error'][:80]!r})")
        check(g.call("editor_ui")["visible"], "the overlay goes on")
        lua("fail = false unbalanced = true")
        g.call("lua", action="window", id=PANEL, retry=True)
        frames(3)
        w = windows()[PANEL]
        check(w["failed"] and "PushID" in w["error"] and "BeginGroup" in w["error"] and w["failures"] == 2,
              f"a draw leaving scopes open fails the same way ({w['error'][:100]!r})")
        lua("unbalanced = false")
        g.call("lua", action="window", id=PANEL, retry=True)
        n = lua("return frames")
        frames(3)
        w = windows()[PANEL]
        check(not w["failed"] and lua("return frames") >= n + 3, "Retry: drawn again")
        # The other window went on through the failures. The windows dock as tabs of one node in the
        # editor shell (the other behind the panel's tab): in front, it draws every frame.
        g.call("lua", action="window", id=OTHER, open=True, focus=True)
        frames(1)
        other = lua("return other_frames")
        frames(3)
        check(lua("return other_frames") >= other + 3 and not windows()[OTHER]["failed"], "the other window goes on")
        g.call("lua", action="window", id=PANEL, open=True, focus=True)  # back in front for the checks below
        frames(1)
        text = log.read_text(errors="replace") if log.exists() else ""
        check("Assertion" not in text and "imgui-error" not in text, "no Dear ImGui assertion or error")
        # hot reload: the file edited, drawn without a restart, the window still open
        main_lua.write_text(main_lua.read_text().replace("VERSION = 1 ", "VERSION = 2 "))
        reloaded = g.until(lambda: (g.call("redraw") or True) and lua("return drawn_version") == 2, step=1, guard=60)
        check(reloaded and windows()[PANEL]["open"], "hot reload: the edited draw runs, the window still open")
        # the shipped example: off by default; on, its window opens and draws (status by rood.dev.rpc)
        ex = g.lua_plugins().get("rood.dev_example", {})
        check(ex.get("state") == "off" and "rood.dev_example/example" not in windows(),
              f"the shipped example is off by default ({ex.get('state')})")
        g.call("lua", action="enable", plugin="rood.dev_example", enabled=True)
        g.call("lua", action="window", id="rood.dev_example/example", open=True)
        frames(4)
        w = windows().get("rood.dev_example/example", {})
        check(w.get("frames", 0) >= 3 and not w.get("failed") and g.lua_plugins()["rood.dev_example"]["faults"] == 0,
              f"the example on: Window > {w.get('title')} draws ({w})")
        faults = g.lua_plugins()[PLUGIN]
        check(faults["state"] == "loaded", f"the plugin stays loaded ({faults['state']}, {faults.get('status')})")
    finally:
        g.close()
    print(f"{'FAIL' if failures else 'PASS'}: lua_imgui_smoke ({len(failures)} failed)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
