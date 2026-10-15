"""The skip_intro setting (boot_runtime.c, project_settings.cpp): with it on,
a start without --skip-movies goes straight to the title (no publisher and
developer logos, no intro movie) and the title then looks as it does after
the logos; the setting covers only that first title. Without it, the start
is the original's: the logos take their 728 vsyncs before the title."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def frame(game, name):
    """The displayed frame (native resolution, VRAM) as bytes."""
    path = OUT / name
    game.call("screenshot", path=str(path))
    data = path.read_bytes()
    path.unlink()
    return data


def title(extra_env=None, movies=False, log="skip-intro.log"):
    """Launch, run to the title menu; (vsync at the title, the title frame
    60 vsyncs later, the game's log text)."""
    game = Game.launch(movies=movies, fast=True, extra_env=extra_env, log=OUT / log)
    try:
        game.wait_title(timeout=120)
        vsync = game.status()["vsync"]
        game.run(60)
        picture = frame(game, log + ".ppm")
        skip_movies = game.u32("pc_skip_movies")
        return vsync, picture, pathlib.Path(game.log).read_text(errors="replace"), skip_movies
    finally:
        game.close()


settings = OUT / "skip-intro-settings.cfg"
settings.write_text("skip_intro 1\n")
vsync, skipped, text, skip_movies = title({"ROOD_SETTINGS": str(settings)}, movies=True, log="skip-intro-on.log")
check("skip_intro: starting at the title" in text, "skip_intro 1: the start says it skips the intro")
check(vsync < 400, f"skip_intro 1: title menu at vsync {vsync} (logos and movie skipped; < 400)")
check("intro movie explicitly disabled" in text, "skip_intro 1: the intro movie is not played")
check(skip_movies == 0, "skip_intro 1: later titles (idle demo, after the ending) play the movie again")
check(settings.read_text() == "skip_intro 1\n", "the settings file is left as written")

vsync, original, text, _ = title(log="skip-intro-off.log")
check("skip_intro: starting" not in text, "no setting: no skip")
check(vsync > 700, f"no setting: title menu at vsync {vsync} (after the 728 vsyncs of logos)")
check(skipped == original, "the title looks the same with the logos skipped")

print(f"{len(failures)} failure(s)" if failures else "skip_intro_smoke: all checks passed")
sys.exit(1 if failures else 0)
