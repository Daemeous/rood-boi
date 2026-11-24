#!/usr/bin/env python3
"""A comparison lane does not follow its worktree: build_compare's games run with the shaders as built (setting shader_reload 0) and a copy of
the shipped plugins (frozen_plugins), so a checkout, merge or edit while a lane runs cannot change what it draws (the first run of a
GI platforms pair differed for its whole length from the two after it; its log shows 'shader hot reload: 9 pipelines made again' and
'rood.options: files changed: reloading' during boot, the worktree having been switched under it).

    python3 pc/tests/lane_frozen_test.py

A scratch copy of the shaders and of the plugins is edited while a game runs (a comment appended to a stage, a line to a plugin):
  - the lane's setup (shader_reload 0, frozen_plugins): the game logs no shader reload and no plugin reload;
  - the same game without it (shader_reload 1, the live shipped folder): both fire (so the check can fail)."""
import pathlib
import shutil
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import Game  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def run(work, frozen):
    """A game with a scratch shader folder and plugin folder, both edited while it runs; its log."""
    work = pathlib.Path(work)
    shaders = work / "shaders"
    shutil.copytree(ROOT / "pc/shaders/vulkan", shaders)
    plugins = work / "plugins"
    if frozen:
        sys.path.insert(0, str(ROOT / "pc/tools"))
        import build_compare
        shutil.copytree(ROOT / "pc/plugins", work / "live-plugins")
        # (the lane's own helper, on the scratch tree: frozen_plugins copies pc/plugins into out/shipped-plugins)
        plugins = pathlib.Path(build_compare.frozen_plugins(work))
    else:
        shutil.copytree(ROOT / "pc/plugins", plugins)
    (work / "settings.cfg").write_text(f"shader_reload {0 if frozen else 1}\nshader_reload_poll_ms 100\nplugin_watch_vsyncs 1\n")
    env = {"ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_SHADERS": str(shaders), "ROOD_PLUGINS_SHIPPED": str(plugins),
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"), "ROOD_OUT": str(work)}
    g = Game.launch(log=work / "game.log", extra_env=env, fast=True)
    try:
        g.run(60)
        # the worktree changes under the running game: a stage, an included file's neighbour, a plugin
        stage = sorted(shaders.glob("*.frag"))[0]
        stage.write_text(stage.read_text() + "\n// edited while the lane ran\n")
        lua = (work / "live-plugins" / "rood.options" / "main.lua") if frozen else (plugins / "rood.options" / "main.lua")
        lua.write_text(lua.read_text() + "\n-- edited while the lane ran\n")
        for _ in range(40):
            g.run(15)
            time.sleep(0.15)
    finally:
        g.close()
    return (work / "game.log").read_text(errors="replace")


def main():
    with tempfile.TemporaryDirectory(prefix="lane-frozen-") as a:
        log = run(a, True)
    check("shader hot reload" not in log and "[shaders] reloaded" not in log, "the lane's setup: no shader reload while its shaders are edited")
    check("files changed: reloading" not in log, "the lane's setup: no plugin reload while the live plugin folder is edited")
    with tempfile.TemporaryDirectory(prefix="lane-live-") as b:
        log = run(b, False)
    check("[shaders] reloaded" in log or "shader hot reload" in log, "without it (shader_reload 1): the edited stage is reloaded (the check can fail)")
    check("files changed: reloading" in log, "without it: the edited plugin is reloaded")
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
