#!/usr/bin/env python3
"""build_compare draws each probe with the shaders it was built with: the game reloads shader sources from
$ROOD_SHADERS (default pc/shaders/vulkan of the working directory) over its built-in SPIR-V, so two probes from two
trees would both draw with one tree's sources (the land-27 GI lane's false difference). own_shaders() points
$ROOD_SHADERS and $ROOD_SHADERS_GENERATED at an empty folder (the service is off); every lane that launches a game
calls it before the game starts."""
import inspect
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import build_compare  # noqa: E402

os.environ.pop("ROOD_SHADERS", None)
os.environ.pop("ROOD_SHADERS_GENERATED", None)
build_compare.own_shaders()
for name in ("ROOD_SHADERS", "ROOD_SHADERS_GENERATED"):
    folder = pathlib.Path(os.environ[name])
    assert folder.is_dir() and not any(folder.iterdir()), f"{name} must be an existing empty folder: {folder}"

launchers = [build_compare.play_hashes if hasattr(build_compare, "play_hashes") else None, build_compare.play_casts,
             build_compare.play_menus, build_compare.play_audio]
for fn in (f for f in launchers if f):
    assert "own_shaders()" in inspect.getsource(fn), f"{fn.__name__} launches the game without own_shaders()"
sources = [n for n, f in inspect.getmembers(build_compare, inspect.isfunction)
           if "os.environ.update(ROOD_BINARY=" in inspect.getsource(f) and "own_shaders()" not in inspect.getsource(f)
           and n != "cover"]
assert not sources, f"these set up a game without own_shaders(): {sources}"
print("build_compare_shaders_test: ok")
