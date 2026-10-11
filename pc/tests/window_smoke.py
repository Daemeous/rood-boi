"""SDL window on X11 (always a private Xvfb: it sends real X11 key and close
events): reach the title, Up moves the menu, Escape does not quit, closing
the window exits cleanly."""
import os, subprocess, sys, time, pathlib

root = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "pc/tools"))
from rood_rpc import OUT, start_xvfb  # noqa: E402

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
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                if "TITLE MENU READY" in log.read_text():
                    break
                if p.poll() is not None:
                    raise RuntimeError(log.read_text())
                time.sleep(0.1)
            else:
                raise RuntimeError("title timeout")
            subprocess.run(
                [send_key, "Up"], cwd=root, env=env, check=True
            )
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if "[title] selected menu item 3" in log.read_text():
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("menu did not respond: " + log.read_text())
            # Escape belongs to the game (and the overlay); it must not quit.
            subprocess.run(
                [send_key, "Escape"], cwd=root, env=env, check=True
            )
            time.sleep(1)
            assert p.poll() is None, "Escape quit the game"
            subprocess.run(
                [send_key, "--close"], cwd=root, env=env, check=True
            )
            p.wait(timeout=5)
            assert p.returncode == 0, p.returncode
            print(
                "PASS: SDL window (X11 on Xvfb) reached title, Up selected Sound (item 3), "
                "Escape ignored, closing the window exited cleanly"
            )
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait(timeout=5)
finally:
    x.terminate()
    x.wait(timeout=5)
