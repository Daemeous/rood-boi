#!/usr/bin/env python3
"""CPU null resource/stepping smoke; not default-native Vulkan parity proof."""
import os
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc


def main():
    rood_rpc.OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="null-backend-", dir=rood_rpc.OUT) as directory:
        work = Path(directory)
        settings = work / "settings.cfg"
        settings.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n")
        env = {"ROOD_RENDERER": "null", "ROOD_ORACLE": "1", "ROOD_SETTINGS": str(settings),
               "ROOD_CARD2": str(work / "absent2.mcd"), "ROOD_DETERMINISTIC": "1",
               "SDL_VIDEO_DRIVER": "invalid-null-smoke-driver"}
        # Neither probe nor display helper may be reached, even with an inherited
        # EGL/Xvfb default. Invalid SDL video driver also catches frontend init.
        with patch.object(rood_rpc, "egl_probe", side_effect=AssertionError("EGL probe")), \
             patch.object(rood_rpc, "start_xvfb", side_effect=AssertionError("Xvfb")):
            game = rood_rpc.Game.launch(extra_env=env, card=str(work / "absent.mcd"),
                                       log=work / "game.log", fast=True,
                                       socket_path=f"/tmp/rood-null-{os.getpid()}.sock")
        try:
            assert len(game.processes) == 1
            assert game.call("graphics")["hardware"] is False
            game.run(10)
            assert game.call("graphics")["hardware"] is False
            pid = game.processes[0].pid
            # Linux resource evidence, in addition to the fail-closed driver.
            fds = Path(f"/proc/{pid}/fd")
            if fds.exists():
                for fd in fds.iterdir():
                    try:
                        target = os.readlink(fd)
                    except FileNotFoundError:
                        continue
                    assert not target.startswith(("/dev/dri/", "/dev/nvidia")), target
                children = Path(f"/proc/{pid}/task/{pid}/children").read_text().strip()
                assert not children, children
            assert "null backend: CPU VRAM" in (work / "game.log").read_text()
            print("PASS: null launch, no graphics probe/display helper, hardware inactive, RPC stepping, no GPU device descriptors")
        finally:
            game.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
