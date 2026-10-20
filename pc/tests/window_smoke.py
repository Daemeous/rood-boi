"""SDL window on X11 (always a private Xvfb: it sends real X11 key and close
events): reach the title, Up moves the menu, Escape does not quit, closing
the window exits cleanly."""
import os, subprocess, sys, time, pathlib

root = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "pc/tools"))
from rood_rpc import OUT, Deadline, Game, start_xvfb, watch_stops  # noqa: E402

# Keep tests off the player's saves (a save makes the title default to
# Continue): an empty slot 1 and no slot 2.
os.environ.setdefault("ROOD_CARD1", str(OUT / "test-card1.mcd"))
os.environ.setdefault("ROOD_CARD2", str(OUT / "test-card2-absent.mcd"))
# Software GL under Xvfb is slow at 4x; these tests cover game logic at 1x.
os.environ.setdefault("ROOD_SCALE", "1")

send_key = str(OUT / "send_key")
subprocess.run(["gcc", "pc/tests/send_key.c", "-lX11", "-o", send_key], cwd=root, check=True)
x, display = start_xvfb("800x600x24")
# SDL uses X11 on the private display (not the desktop's Wayland), so the
# X11 events send_key delivers reach the game.
env = dict(os.environ, DISPLAY=display, SDL_VIDEO_DRIVER="x11", ROOD_CONTROLLER="none",
           ROOD_GI=os.environ.get("ROOD_GI", "0"))
env.pop("WAYLAND_DISPLAY", None)
# RPC only to count what the game did (its presented images), never to drive it.
env.setdefault("ROOD_RPC", f"/tmp/rood-window-{os.getpid()}.sock")  # sun_path is 108 bytes
log = OUT / "window-test.log"
try:
    with log.open("w") as output:
        p = subprocess.Popen(
            [("pc/build/boot64/rood_battle_probe" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot/rood_boot"), "--window", "--skip-movies", "--fast"],
            cwd=root,
            env=env,
            stdout=output,
            stderr=output,
        )
        watch_stops(p.pid)  # the hang guards below leave out time the game spends stopped (SIGSTOP)
        try:
            deadline = Deadline(120)
            while not deadline.passed():
                if "TITLE MENU READY" in log.read_text():
                    break
                if p.poll() is not None:
                    raise RuntimeError(log.read_text())
                time.sleep(0.1)
            else:
                raise RuntimeError("title timeout")
            rpc = Game(env["ROOD_RPC"])

            def tap(name, counter="vsync", frames=6):
                """Key down, held until the game has run `frames` more (its own count), then up: a fixed
                100 ms hold reached a stopped game as a press and a release in one frame (no press)."""
                subprocess.run([send_key, name, "down"], cwd=root, env=env, check=True)
                try:
                    start, deadline = rpc.status()[counter], Deadline(60)  # (a hang guard)
                    while rpc.status()[counter] < start + frames and not deadline.passed():
                        time.sleep(0.01)
                except (OSError, RuntimeError):
                    pass  # the game quit (asserted after)
                subprocess.run([send_key, name, "up"], cwd=root, env=env, check=True)

            tap("Up")
            deadline = Deadline(60)  # a hang guard: the game's answer is what is waited for
            while not deadline.passed():
                if "[title] selected menu item 3" in log.read_text():
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("menu did not respond: " + log.read_text())
            # Escape belongs to the game (and the overlay); it must not quit.
            presents = rpc.status()["presents"]
            tap("Escape", "presents")  # (Escape may open the editor, which holds the game's vsyncs)
            # the game handled it: it went on presenting (events are read every frame), not a guessed second
            deadline, now = Deadline(60), presents
            while now < presents + 30 and not deadline.passed():
                try:
                    now = rpc.status()["presents"]
                except (OSError, RuntimeError):
                    break  # it quit (asserted below)
                time.sleep(0.02)
            assert p.poll() is None, "Escape quit the game"
            assert now >= presents + 30, f"the game stopped presenting after Escape ({presents} -> {now})"
            rpc.file.close()
            rpc.sock.close()
            subprocess.run(
                [send_key, "--close"], cwd=root, env=env, check=True
            )
            deadline = Deadline(60)  # a cold Mesa shader cache (after a shader change) slows the exit
            while p.poll() is None and not deadline.passed():
                time.sleep(0.05)
            assert p.returncode == 0, p.returncode
            print(
                "PASS: SDL window (X11 on Xvfb) reached title, Up selected Sound (item 3), "
                "Escape ignored, closing the window exited cleanly"
            )
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait(timeout=15)  # a cold Mesa shader cache (after a shader change) slows the exit
finally:
    x.terminate()
    x.wait(timeout=5)
