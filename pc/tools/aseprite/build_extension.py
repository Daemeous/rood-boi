#!/usr/bin/env python3
"""Builds the Aseprite extension (an .aseprite-extension is a zip with package.json at its root): rood-live.aseprite-extension in
pc/build/aseprite (or --out DIR). Install: double-click it, or Aseprite > Edit > Preferences > Extensions > Add Extension."""
import argparse
import pathlib
import zipfile

HERE = pathlib.Path(__file__).resolve().parent


def build(out):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    target = out / "rood-live.aseprite-extension"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted((HERE / "rood-live").iterdir()):
            z.write(path, path.name)
    return target


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", default=str(HERE.parents[1] / "build/aseprite"))
    built = build(p.parse_args().out)
    print(built)
    print("Install: double-click it, or in Aseprite: Edit > Preferences > Extensions > Add Extension, then restart Aseprite. "
          "To update, remove the older Rood live on the same page first.")
