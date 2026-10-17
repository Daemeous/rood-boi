#!/usr/bin/env python3
"""The extension system in one running game (pc/src/lua, pc/docs/Extensions.md).

1. Every fixture plugin's Lua tests (pc/tests/lua/plugins, rood.test) and the
   shipped plugins', in this one game.
2. What Lua cannot test from inside: a runaway eval (also a pcall loop and a
   pattern) is stopped within its budget and the game runs on; memory
   exhaustion is refused; a runaway tick hook and a failing frame hook are
   caught, logged with a traceback and their plugins disabled; capabilities
   exist only when granted (and go with a revoke or a code change), fs stays
   in its folders, the others refuse, elevated calls are audited; hot reload
   swaps a plugin's registrations; settings changed from Lua reach
   settings.cfg; a disabled plugin's settings kind is gone.

    python3 pc/tests/lua_smoke.py
    python3 pc/tests/lua_smoke.py --tests-only --plugin test.settings   one plugin's Lua tests (the fast loop)
"""
import argparse
import json
import pathlib
import shutil
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game, RpcError  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "pc/tests/lua/plugins"

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--plugin", action="append", default=[], help="only this plugin's Lua tests (repeatable)")
parser.add_argument("--tests-only", action="store_true", help="only the Lua tests (part 1)")
args = parser.parse_args()

plugins = OUT / "lua-plugins"
data = OUT / "lua-plugin-data"
settings = OUT / "lua-settings.cfg"
shutil.rmtree(plugins, ignore_errors=True)
shutil.rmtree(data, ignore_errors=True)
settings.unlink(missing_ok=True)
shutil.copytree(FIXTURES, plugins)


def write_plugin(name, main):
    folder = plugins / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "plugin.lua").write_text(f'return {{ id = "{name}", version = "1.0.0", api_version = 1 }}\n')
    (folder / "main.lua").write_text(main)


def lua_tests(game):
    listed = game.lua_plugins()
    for folder in sorted(p.name for p in FIXTURES.iterdir() if p.is_dir()):
        assert listed[folder]["state"] == "loaded", listed[folder]
    if "rood.example" in listed:
        game.call("lua", action="enable", plugin="rood.example", enabled=True)
    results = []
    for plugin in args.plugin or [None]:
        results += game.lua_test(plugin=plugin)["results"]
    for r in results:
        print(f"{r['status'].upper():<5} {r['plugin']} {r['file']}: {r['name']} ({r['ms']} ms)")
    failed = [r for r in results if r["status"] == "fail"]
    assert not failed, "Lua tests failed:\n" + "\n\n".join(f"{r['plugin']} {r['name']}:\n{r['message']}" for r in failed)
    return results


def runaway(game):
    vsync = game.status()["vsync"]
    for code, why in [("while true do end", "budget exceeded"),
                      ("while true do pcall(function() while true do end end) end", "budget exceeded"),
                      ("return string.find(string.rep('a', 4000), '.-.-.-.-.-.-.-b')", "string pattern"),
                      ("table.move({}, 1, 1 << 40, 2)", "budget exceeded"),  # a C loop: it hung the game
                      ("setmetatable({}, { __gc = function() while true do end end }) collectgarbage()",
                       "__gc is not available")]:  # finalizers run with the hook off: it hung the game
        start = time.monotonic()
        result = game.lua(code)
        assert not result["ok"] and why in result["error"], (code, result)
        assert time.monotonic() - start < 2.0, (code, "the eval budget is 250 ms")
    assert game.lua("return #string.rep('', math.maxinteger)")["results"] == [0]
    assert game.run(5)["vsync"] == vsync + 5
    game.call("lua", action="budget", memory_mb=8, load_budget_ms=5000, load_budget_instructions=10**10)
    result = game.lua("local t = {} for i = 1, 1e8 do t[i] = ('x'):rep(100) .. i end")
    assert not result["ok"] and "not enough memory" in result["error"], result
    kb = game.lua("collectgarbage() return collectgarbage('count')")["results"][0]
    assert kb < 8 * 1024, kb
    game.call("lua", action="budget", memory_mb=32, load_budget_ms=250, load_budget_instructions=50000000)
    assert game.run(5)["vsync"] == vsync + 10


def failing_hooks(game):
    write_plugin("hard.loop", 'rood.on("tick", function() while true do end end)\n')
    write_plugin("hard.frame_error", 'rood.on("frame", function() error("boom from a frame hook") end)\n')
    game.call("lua", action="rescan")
    listed = game.lua_plugins()
    assert listed["hard.loop"]["state"] == "loaded" and listed["hard.frame_error"]["state"] == "loaded", listed
    vsync = game.status()["vsync"]
    start = time.monotonic()
    assert game.run(30)["vsync"] == vsync + 30
    assert time.monotonic() - start < 15, "five call budgets, then plain vsyncs"
    listed = game.lua_plugins()
    loop, frame = listed["hard.loop"], listed["hard.frame_error"]
    assert loop["state"] == "disabled" and loop["kills"] >= 5, loop
    assert frame["state"] == "disabled" and frame["faults"] >= 5, frame
    lines = game.call("lua", action="log", since=0)["lines"]
    assert any("boom from a frame hook" in line["text"] and "main.lua:1" in line["text"] for line in lines), lines[-5:]


def capabilities(game):
    caps = "test.caps"
    assert game.lua("return rood.fs == nil and rood.net == nil and rood.exec == nil", plugin=caps)["results"] == [True]
    assert game.lua_plugins()[caps]["declared"] == ["fs.read", "fs.write", "net:example.com", "exec"]
    for capability in ("fs.read", "fs.write", "net:example.com"):
        game.call("lua", action="grant", plugin=caps, capability=capability, elevated=True)
    p = game.lua_plugins()[caps]
    assert p["granted"] == ["fs.read", "fs.write", "net:example.com"] and p["elevated"], p
    result = game.lua('rood.fs.write("out/note.txt", "hi") return rood.fs.read("out/note.txt")', plugin=caps)
    assert result["ok"] and result["results"] == ["hi"], result
    assert (plugins / caps / "out/note.txt").read_text() == "hi"
    result = game.lua('return pcall(rood.fs.write, "../escape.txt", "x")', plugin=caps)
    assert result["results"][0] is False and "bad path" in result["results"][1], result
    result = game.lua('return pcall(rood.net.request, "https://example.com")', plugin=caps)
    assert result["results"][0] is False and "not available in this version" in result["results"][1], result
    assert game.lua("return rood.exec == nil", plugin=caps)["results"] == [True]
    audit = game.call("lua", action="audit")["lines"]
    assert any(" test.caps fs.write " in line for line in audit), audit
    assert "test.caps fs.write" in (data / "audit.log").read_text()
    game.call("lua", action="grant", plugin=caps, capability="fs.write", revoke=True)
    assert game.lua("return rood.fs.write == nil and rood.fs.read ~= nil", plugin=caps)["results"] == [True]
    main = plugins / caps / "main.lua"
    main.write_text(main.read_text() + "-- changed: grants are asked again\n")
    game.run(31)
    p = game.lua_plugins()[caps]
    assert p["granted"] == [] and "fs.read" in p["pending"], p
    assert game.lua("return rood.fs == nil", plugin=caps)["results"] == [True]
    assert f"plugin_grant {caps} " in settings.read_text()


def filesystem_grants(game):
    name = "hard.grants"
    write_plugin(name, "-- filesystem grant regression\n")
    # Include non-existent descendants: realpath alone cannot classify these.
    own = f"fs.write:{plugins / name / 'new'}"
    own_data = f"fs.write:{data / name / 'new'}"
    external = f"fs.write:{OUT / 'lua-exports' / 'new'}"
    read_external = f"fs.read:{OUT}"
    alias = plugins / "grant-root-link"
    alias.symlink_to(plugins, target_is_directory=True)
    broad = [f"fs.write:{p}" for p in ("/", OUT, plugins, data, settings, alias)]
    allowed = [own, own_data, external, read_external]
    manifest = ("return { id = " + json.dumps(name)
                + ', version = "1.0.0", api_version = 1, capabilities = {'
                + ",".join(json.dumps(c) for c in broad + allowed) + "} }\n")
    (plugins / name / "plugin.lua").write_text(manifest)
    game.call("lua", action="rescan")
    assert game.lua_plugins()[name]["state"] == "loaded"
    for cap in broad:
        try:
            game.call("lua", action="grant", plugin=name, capability=cap, elevated=True)
        except RpcError as error:
            assert "contains settings or a plugin/plugin_data root" in str(error), (cap, error)
        else:
            raise AssertionError(f"accepted broad filesystem grant: {cap}")
    p = game.lua_plugins()[name]
    assert p["granted"] == [] and p["external_filesystem_grants"] == [], p
    for cap in allowed:
        game.call("lua", action="grant", plugin=name, capability=cap, elevated=True)
    p = game.lua_plugins()[name]
    assert p["granted"] == allowed, p
    assert p["external_filesystem_grants"] == [external, read_external], p
    game.call("lua", action="grant", plugin=name, capability=external, revoke=True)
    assert game.lua_plugins()[name]["external_filesystem_grants"] == [read_external]
    game.call("lua", action="grant", plugin=name, capability=external, elevated=True)
    return name, p["hash"], broad, allowed, [external, read_external]


def hot_reload(game):
    def entries():
        return [(e["id"], e["data"]["version"]) for e in game.call("lua", action="registry", kind="test.reload")["entries"]]
    assert entries() == [("test.reload/entry", 1)]
    before = game.lua_plugins()["test.reload"]
    main = plugins / "test.reload/main.lua"
    main.write_text(main.read_text().replace("VERSION = 1", "VERSION = 2")
                    .replace('"test.reload/entry"', '"test.reload/entry2"'))
    game.run(31)
    after = game.lua_plugins()["test.reload"]
    assert after["generation"] > before["generation"] and after["state"] == "loaded", (before, after)
    assert entries() == [("test.reload/entry2", 2)], entries()
    assert (after["handlers"], after["timers"]) == (before["handlers"], before["timers"]) == (1, 1), (before, after)


def settings_file(game):
    text = settings.read_text()
    assert "skip_intro 1" in text, text
    assert (data / "test.settings/settings.json").exists()
    game.call("lua", action="enable", plugin="test.settings", enabled=False)
    assert game.lua('return #rood.settings.list({ page = "plugin:test.settings" })')["results"] == [0]
    lint = game.call("editor_schema", action="lint")
    assert not lint["problems"], lint
    assert "plugin_enabled test.settings 0" in settings.read_text()


game = Game.launch(fast=True, extra_env={"ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(data),
                                         "ROOD_SETTINGS": str(settings)})
try:
    game.pause()
    results = lua_tests(game)
    if not args.tests_only:
        runaway(game)
        failing_hooks(game)
        capabilities(game)
        grant_reload = filesystem_grants(game)
        hot_reload(game)
        game.run(40)  # settings.cfg: the Lua tests' changes are saved within 30 polls
        settings_file(game)
        assert game.call("lua", action="console", input="return 6 * 7")["results"] == [42]
        vsync = game.status()["vsync"]
        assert game.run(5)["vsync"] == vsync + 5
    print(f"PASS: {len(results)} Lua tests in one game"
          + ("" if args.tests_only else "; runaway scripts, memory, failing hooks, capabilities, hot reload and "
                                        "settings checked in the same game"))
finally:
    game.close()

if not args.tests_only:
    # A real settings reload must reject old/hand-edited broad grants too,
    # while preserving valid grants and the RPC's external-directory warning.
    name, content_hash, broad, allowed, external = grant_reload
    with settings.open("a") as saved:
        for cap in broad:
            saved.write(f"\nplugin_grant {name} {content_hash} {cap}\n")
    game = Game.launch(fast=True, extra_env={"ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(data),
                                           "ROOD_SETTINGS": str(settings)})
    try:
        game.pause()
        p = game.lua_plugins()[name]
        assert p["state"] == "loaded" and p["hash"] == content_hash, p
        assert p["granted"] == allowed and p["external_filesystem_grants"] == external, p
        print("PASS: filesystem grants survive settings reload; broad saved grants rejected")
    finally:
        game.close()
