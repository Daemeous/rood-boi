#!/usr/bin/env python3
"""Test checkpoints: game states many tests start from, reached by booting
with vsync-exact input or, much faster, by resuming a save state of them
(pc/src/snapshot.c) that is validated against booting first.

    from checkpoints import launch_at
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    game.call("graphics", ...)   # host settings after the checkpoint, either way

Checkpoints (each from a fresh boot, the title menu taken at TITLE_VSYNC so
New Game / Continue are pressed on a vsync multiple of 8):
  title      the title menu, paused at TITLE_VSYNC
  opening    New Game, run to vsync 3300: zone 1 room 0 after the opening pan
  first_bubble  New Game, Circle once, 2200 more vsyncs: the opening's first
             talk bubble is about to zoom in
  save_room  Continue from the card (Start, then Circle three times: Load,
             slot 1, its first save), 400 more vsyncs: the save's room
             (needs card=; the card's contents are part of the key)

launch_at returns a Game paused at the checkpoint. With save states on, it
resumes the checkpoint's snapshot WARMUP vsyncs before it and runs those
vsyncs (so the renderer's frame history is the game's again), else it boots
and plays the checkpoint's input. Either way the game state at the
checkpoint is the same: snapshot_equivalence.py (validate() below; run.py's
first job) boots it, saves the snapshot, plays on, then resumes the snapshot
in another process and requires identical guest RAM, scratchpad, I/O page,
VRAM and overlay statics at the checkpoint and over the next FRAMES vsyncs,
the same presented frames (interpolation off, deterministic scanlines:
ROOD_DETERMINISTIC=1 in both), and identical end states (native game
statics and SPU too) after a field menu open and close. A snapshot that
fails stays unused (<name>.bad holds why): its tests boot, as do tests with
another card (a snapshot is made for run.py's --save card only).

Save states are used when ROOD_TEST_SNAPSHOT_DIR names the cache (run.py
sets pc/build/test-snapshots/<binary, data and config hash>) and
ROOD_TEST_SNAPSHOTS is not 0 (run.py --no-snapshots). Run directly, a test
boots unless those are set.
"""
import fcntl
import hashlib
import json
import os
import pathlib
import shutil
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc  # noqa: E402
from rood_rpc import Game, RpcError  # noqa: E402

TITLE_VSYNC = 960  # the title menu is ready at ~870
WARMUP = 16  # vsyncs a resumed snapshot runs before the checkpoint
FRAMES = 40  # vsyncs compared after it
OPENING_VSYNC = 3300


def _title(game, target=TITLE_VSYNC):
    """Boot to the title menu and pause at vsync `target`: pause at once
    (just after launch), then step there exactly. The title menu is ready at
    about vsync 870 on every launch (its log line is required)."""
    game.pause()
    v = game.status()["vsync"]
    if v > target:
        raise RpcError(f"the game was at vsync {v} when first paused (> {target})")
    if v < target:
        game.run(target - v)
    if "TITLE MENU READY" not in pathlib.Path(game.log).read_text(errors="replace"):
        raise RpcError(f"the title menu is not ready at vsync {target}")


def _opening(game, end):
    _title(game)
    game.press("start", vsyncs=4, settle=60)
    game.run(end - game.status()["vsync"])


def _first_bubble(game, end):
    _title(game)
    game.press("start", vsyncs=4, settle=90)
    game.press("circle", vsyncs=3, settle=60)
    game.run(end - game.status()["vsync"])


def _save_room(game, end):
    _title(game)
    game.press("start", vsyncs=4, settle=90)
    for _ in range(3):  # Continue, the card, its first save
        game.press("circle", vsyncs=3, settle=90)
    game.run(end - game.status()["vsync"])


# name: (needs a card, the checkpoint's vsync, play from boot to vsync `end`)
CHECKPOINTS = {
    "title": (False, TITLE_VSYNC, _title),
    "opening": (False, OPENING_VSYNC, _opening),
    # 960 + Start (4 + 90) + Circle (3 + 60) + 2200: the opening's first
    # talk bubble is about to zoom in
    "first_bubble": (False, TITLE_VSYNC + 94 + 63 + 2200, _first_bubble),
    # 960 + Start (4 + 90) + 3 x Circle (3 + 90) + 400
    "save_room": (True, TITLE_VSYNC + 94 + 3 * 93 + 400, _save_room),
}


def enabled():
    return bool(os.environ.get("ROOD_TEST_SNAPSHOT_DIR")) and os.environ.get("ROOD_TEST_SNAPSHOTS", "1") != "0"


def _file_hash(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()[:16]


def _paths(name, card):
    directory = pathlib.Path(os.environ["ROOD_TEST_SNAPSHOT_DIR"])
    stem = name + (f"-card{_file_hash(card)}" if card else "")
    return directory, directory / f"{stem}.snap", directory / f"{stem}.ok", directory / f"{stem}.bad"


def _drive(game, name, end):
    CHECKPOINTS[name][2](game, end)
    status = game.status()
    if status["vsync"] != end:
        raise RpcError(f"checkpoint {name}: at vsync {status['vsync']}, expected {end}")


def _base_env(work):
    """The environment snapshots are made and checked in: run.py's suite
    environment with private paths, deterministic scanlines."""
    return {"ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"),
            "ROOD_DETERMINISTIC": "1", "ROOD_OUT": str(work)}


def _record(game, work, label, frames):
    """Per vsync: the snapshot hashes (RPC snapshot hash) and the presented
    image's hash (present_dump, interpolation off)."""
    rows = []
    for i in range(frames):
        game.run(1)
        ppm = work / f"{label}-{i:03d}.ppm"
        ppm.unlink(missing_ok=True)
        game.call("present_dump", path=str(ppm))
        if not rood_rpc.wait_for_file(ppm, 20):
            raise RpcError("no frame presented")
        image = hashlib.sha256(ppm.read_bytes()).hexdigest()[:16]
        ppm.unlink()
        h = game.call("snapshot", action="hash")
        if os.environ.get("ROOD_CHECKPOINT_DEBUG") == str(i):  # both states at that vsync, for snapshot_diff.py
            game.call("snapshot", action="save", path=str(work / f"{label}-{i:03d}.snap"))
        rows.append({"vsync": h["vsync"], "image": image,
                     **{k: h[k] for k in ("ram", "scratch", "io", "vram", "overlays")}})
    return rows


def _menu_probe(game):
    """After the recorded vsyncs, input that reads VRAM back (the field menu
    captures the screen behind it): the end states are compared after it."""
    game.press("triangle", vsyncs=3, settle=60)
    game.press("cross", vsyncs=3, settle=60)
    game.run(30)


def validate(name, card=None, work=None, log=print):
    """Boot to checkpoint `name`, save its snapshot WARMUP vsyncs before,
    resume it in a second process and compare the two over the warm-up and
    FRAMES vsyncs. Returns (snapshot path or None, report lines). Writes the
    snapshot and <name>.ok / .bad into the cache."""
    import snapshot_diff

    needs_card, at, _ = CHECKPOINTS[name]
    if needs_card and not card:
        raise ValueError(f"checkpoint {name} needs a memory card")
    directory, snap, ok, bad = _paths(name, card)
    directory.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(work or directory / f"work-{name}-{os.getpid()}")
    work.mkdir(parents=True, exist_ok=True)
    env = _base_env(work)
    socket = f"/tmp/rood-cp-{os.getpid()}.sock"
    cards = []
    for side in ("a", "b"):  # each run its own copy of the card (Continue only reads it)
        if card:
            cards.append(work / f"card-{side}.mcd")
            shutil.copyfile(card, cards[-1])
        else:
            cards.append(work / f"absent-{side}.mcd")
    report = []
    start = time.monotonic()
    part = snap.with_suffix(".snap.part")
    game = Game.launch(fast=True, card=str(cards[0]), socket_path=socket, log=work / "boot.log", extra_env=env)
    try:
        game.call("graphics", interpolate=False)
        _drive(game, name, at - WARMUP)
        game.call("snapshot", action="save", path=str(part))
        booted = _record(game, work, "a", WARMUP + FRAMES)
        _menu_probe(game)
        game.call("snapshot", action="save", path=str(work / "a-end.snap"))
    finally:
        game.close()
    boot_time = time.monotonic() - start
    start = time.monotonic()
    game = Game.launch(fast=True, card=str(cards[1]), socket_path=socket, log=work / "resume.log", extra_env=env,
                       snapshot=part)
    try:
        game.call("graphics", interpolate=False)
        resumed = _record(game, work, "b", WARMUP + FRAMES)
        _menu_probe(game)
        game.call("snapshot", action="save", path=str(work / "b-end.snap"))
    finally:
        game.close()
    resume_time = time.monotonic() - start
    first_image = next((i for i in range(len(booted)) if all(booted[j]["image"] == resumed[j]["image"]
                                                             for j in range(i, len(booted)))), None)
    state_keys = ("ram", "scratch", "io", "vram", "overlays")
    # Row i is the state after i + 1 vsyncs; row WARMUP - 1 is the checkpoint.
    differs = [(i, r["vsync"], [k for k in state_keys if a[k] != r[k]]) for i, (a, r) in enumerate(zip(booted, resumed))
               if any(a[k] != r[k] for k in state_keys)]
    in_warmup = [d[1:] for d in differs if d[0] < WARMUP - 1]
    after = [d[1:] for d in differs if d[0] >= WARMUP - 1]
    report.append(f"{name}: booted to vsync {at - WARMUP} in {boot_time:.1f}s (+{WARMUP + FRAMES} recorded), "
                  f"resumed and recorded in {resume_time:.1f}s")
    if in_warmup:
        report.append(f"  during the warm-up only (allowed): differs at {in_warmup[:6]}")
    if after:
        report.append(f"  game state differs at or after the checkpoint: {after[:8]}{'...' if len(after) > 8 else ''}")
    lines = snapshot_diff.diff(snapshot_diff.read(work / "a-end.snap"), snapshot_diff.read(work / "b-end.snap"),
                               snapshot_diff.Symbols(rood_rpc.BINARY))
    report += ["  end state: " + line for line in lines]
    if first_image is None or first_image > WARMUP - 1:
        report.append(f"  presented frames differ at or after the checkpoint "
                      f"(identical from row {first_image if first_image is not None else 'never'}; warm-up {WARMUP})")
    else:
        report.append(f"  frames identical from vsync +{first_image + 1} of the {WARMUP}-vsync warm-up on; "
                      f"game state identical at the checkpoint and the {FRAMES} vsyncs after it")
    good = not after and not lines and first_image is not None and first_image <= WARMUP - 1
    if good:
        os.replace(part, snap)
        ok.write_text(json.dumps({"vsync": at, "warmup": WARMUP, "frames": FRAMES,
                                  "first_identical_frame": first_image}) + "\n")
        bad.unlink(missing_ok=True)
        shutil.rmtree(work, ignore_errors=True)
    else:
        part.unlink(missing_ok=True)
        bad.write_text("\n".join(report) + "\n")
        ok.unlink(missing_ok=True)
    for line in report:
        log(line)
    return (snap if good else None), report


BUILDING = "building"  # run.py: snapshot_equivalence.py is (re)making the cache


def prepare(name, card=None, log=print):
    """The checkpoint's validated snapshot, or None (boot instead): when save
    states are off, when snapshot_equivalence.py has not made it (another
    card, a test run on its own) or when it did not resume exactly. Waits
    while snapshot_equivalence.py is making the cache."""
    if not enabled():
        return None
    directory, snap, ok, bad = _paths(name, card)
    deadline = time.monotonic() + 900
    while (directory / BUILDING).exists() and time.monotonic() < deadline:
        time.sleep(0.2)
    if not directory.exists():
        return None
    with open(directory / f"{snap.stem}.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_SH)  # not while one is being made
        if ok.exists() and snap.exists() and not bad.exists():
            return snap
    if bad.exists():
        log(f"[checkpoint] {name}: its snapshot did not resume exactly ({bad.name}); booting")
    return None


def launch_at(name, card=None, **launch):
    """A game paused at checkpoint `name` (see the module docstring); takes
    Game.launch's arguments. game.checkpoint_resumed says how it got there."""
    needs_card, at, _ = CHECKPOINTS[name]
    if needs_card and not card:
        raise ValueError(f"checkpoint {name} needs a memory card")
    snap = prepare(name, card)
    if card:
        launch["card"] = str(card)
    if snap:
        game = Game.launch(snapshot=snap, **launch)
        game.run(WARMUP)
        print(f"[checkpoint] {name}: resumed {snap.name} and ran the {WARMUP}-vsync warm-up (vsync {at})")
    else:
        game = Game.launch(**launch)
        _drive(game, name, at)
        print(f"[checkpoint] {name}: booted to vsync {at}")
    game.checkpoint_resumed = bool(snap)
    return game


if __name__ == "__main__":
    print(__doc__)
