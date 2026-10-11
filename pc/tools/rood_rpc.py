#!/usr/bin/env python3
"""Client for the game's developer JSON-RPC socket (pc/src/dev_rpc.c).

Python:
    from rood_rpc import Game
    game = Game.launch(card="pc/save/card1.mcd")   # headless (EGL/Xvfb) unless window=True
    game.wait_title()
    game.press("start"); game.run(120)              # run 120 vsyncs, then pause
    print(game.state()); game.screenshot("shot.png")
    game.read_symbol("D_800F18B0", 4)
    game.close()

CLI:
    rood_rpc.py launch [--window] [--card PATH] [--audio wav:PATH|none] [--headless egl|xvfb]
                                              (keeps running)
    rood_rpc.py headless                      which headless mode and GPU this machine gets
    rood_rpc.py call METHOD [JSON_PARAMS]     e.g. call input '{"buttons":["circle"],"vsyncs":4}'
    rood_rpc.py symbol NAME                   address of a game symbol
    rood_rpc.py --rpc                         JSON-RPC lines on stdin/stdout; adds the
                                              client-side methods "symbol" and "read_symbol"

The socket defaults to $ROOD_RPC or pc/build/boot/rpc.sock. Methods run at vsync
boundaries; "step" pauses after N vsyncs, so step + wait_paused gives exact timing.

Headless runs (ROOD_HEADLESS, or launch(headless=...)):
    egl   SDL's offscreen video driver with an EGL context on a GPU device
          (EGL_EXT_platform_device; ROOD_EGL_DEVICE=N picks one, else the first
          that works: NVIDIA, Mesa GPUs, then llvmpipe). No X server, so no
          X11 input injection: drive input through the RPC "input" method.
    xvfb  a private Xvfb display (software GL), for tests that send X11 events.
The default is egl when pc/tests/egl_probe.c can create a context, else xvfb.
ROOD_OUT (default pc/build/boot) is where the game and tests write files, so
parallel runs (pc/tests/run.py) each get their own.
"""
import argparse
import json
import os
import pathlib
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
# ROOD_ARCH=64 selects the LP64 build (build_boot.py --arch 64).
BUILD = ROOT / ("pc/build/boot64" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot")
BINARY = pathlib.Path(os.environ.get("ROOD_BINARY") or BUILD / "rood_battle_probe")  # ROOD_BINARY: e.g. a gdb wrapper
DEFAULT_SOCKET = os.environ.get("ROOD_RPC", str(ROOT / "pc/build/boot/rpc.sock"))
# Per-run output directory (logs, frames, scratch cards); the game reads it too.
OUT = pathlib.Path(os.environ.get("ROOD_OUT") or ROOT / "pc/build/boot")
_symbols = None
_headless = None


def symbols():
    """Game symbols -> addresses (native build is non-PIE, so nm is exact)."""
    global _symbols
    if _symbols is None:
        _symbols = {}
        out = subprocess.run(["nm", str(BUILD / "rood_battle_probe")], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                _symbols[parts[2]] = int(parts[0], 16)
    return _symbols


class RpcError(RuntimeError):
    pass


def egl_probe(device=None):
    """Renderer string if SDL's offscreen driver gets an OpenGL context on EGL
    `device` (index; None = the first that works), else None."""
    source = ROOT / "pc/tests/egl_probe.c"
    # Built against whichever SDL the builds made: the game's own word size
    # first (ROOD_ARCH), so a 64-bit-only checkout still gets EGL.
    builds = [("32", "pc/build/boot/egl_probe", "pc/build/sdl3"),
              ("64", "pc/build/boot64/egl_probe", "pc/build/sdl3_64")]
    if os.environ.get("ROOD_ARCH") == "64":
        builds.reverse()
    found = [(bits, ROOT / p, ROOT / s / "libSDL3.a") for bits, p, s in builds if (ROOT / s / "libSDL3.a").exists()]
    if not found:
        return None
    bits, probe, library = found[0]
    if not probe.exists() or probe.stat().st_mtime < source.stat().st_mtime:
        probe.parent.mkdir(parents=True, exist_ok=True)
        tmp = probe.with_name(f"egl_probe.{os.getpid()}")
        built = subprocess.run(["gcc", f"-m{bits}", "-O1", str(source), "-I", str(ROOT / "pc/third_party/SDL3/include"),
                                str(library), "-ldl", "-lm", "-lpthread", "-o", str(tmp)],
                               capture_output=True)
        if built.returncode:
            return None
        os.replace(tmp, probe)  # atomic: parallel callers never run a half-written probe
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
    try:
        result = subprocess.run([str(probe)] + ([str(device)] if device is not None else []),
                                capture_output=True, text=True, timeout=30, env=env)
    except subprocess.TimeoutExpired:
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def headless_mode():
    """("egl" | "xvfb", renderer or None): ROOD_HEADLESS, else egl when it works."""
    global _headless
    if _headless is None:
        mode = os.environ.get("ROOD_HEADLESS", "")
        renderer = None
        if mode in ("", "egl"):
            renderer = egl_probe(os.environ.get("ROOD_EGL_DEVICE"))
            if renderer is None and mode == "egl":
                raise RpcError("ROOD_HEADLESS=egl but no EGL device creates an OpenGL context")
            mode = "egl" if renderer else "xvfb"
        elif mode != "xvfb":
            raise RpcError(f"ROOD_HEADLESS={mode}: expected egl or xvfb")
        _headless = (mode, renderer)
    return _headless


def headless_env(mode, env):
    """Point `env` at the egl headless mode: no X server or compositor."""
    assert mode == "egl", mode
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    env["SDL_VIDEO_DRIVER"] = "offscreen"
    if os.environ.get("ROOD_EGL_DEVICE"):
        env["SDL_HINT_EGL_DEVICE"] = os.environ["ROOD_EGL_DEVICE"]
    return env


def start_xvfb(size="1280x1024x24"):
    """A private Xvfb: (process, ":N"). Tries explicit display numbers so
    concurrent callers (other tests, other sessions) cannot pick the same
    one: Xvfb takes /tmp/.XN-lock atomically and exits when it is held, and
    -displayfd reports the number only once the server accepts clients."""
    start = 200 + (os.getpid() * 7) % 600
    for attempt in range(64):
        n = start + attempt
        if os.path.exists(f"/tmp/.X{n}-lock"):
            continue
        r, w = os.pipe()
        x = subprocess.Popen(["Xvfb", f":{n}", "-displayfd", str(w), "-nolisten", "tcp", "-screen", "0", size],
                             pass_fds=(w,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(w)
        with os.fdopen(r) as ready:
            number = ready.read(16).strip()  # empty when Xvfb exited (display taken)
        if number == str(n) and x.poll() is None:
            return x, f":{n}"
        x.kill()
        x.wait()
    raise RpcError("no free X display for Xvfb")


class Game:
    def __init__(self, path=DEFAULT_SOCKET, processes=()):
        self.path = path
        self.log = None
        self.display = os.environ.get("DISPLAY")
        self.processes = list(processes)
        self.next_id = 0
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.file = self.sock.makefile("rwb")

    # ---- launching ----
    @classmethod
    def launch(cls, window=False, card=None, audio="none", socket_path=None, log=None, extra_env=None,
               movies=False, headless=None, fast=False, args=()):
        """Start the game with RPC enabled; headless unless window=True
        (`headless`: "egl" or "xvfb", default headless_mode()). The intro
        movie is skipped unless movies=True; fast=True drops the 59.94 Hz
        pacing (vsync-exact RPC stepping is unaffected)."""
        socket_path = socket_path or DEFAULT_SOCKET
        env = dict(os.environ, ROOD_RPC=socket_path, ROOD_CONTROLLER="none", ROOD_AUDIO=audio)
        env.setdefault("ROOD_GI", "0")  # path tracing is slow on software GL; tests opt in
        env.setdefault("ROOD_CARD1", card or str(OUT / "test-card1.mcd"))
        if card:
            env["ROOD_CARD1"] = card
        env.setdefault("ROOD_CARD2", str(OUT / "test-card2-absent.mcd"))
        env.update(extra_env or {})
        processes = []
        if not window:
            mode = headless or headless_mode()[0]
            if mode == "egl":
                headless_env(mode, env)
            else:
                xvfb, env["DISPLAY"] = start_xvfb()
                env["SDL_VIDEO_DRIVER"] = "x11"  # the private display, not the desktop's Wayland
                env.pop("WAYLAND_DISPLAY", None)
                processes.append(xvfb)
        if os.path.exists(socket_path):
            os.unlink(socket_path)
        log_file = open(log or OUT / "rpc-game.log", "w")
        game = subprocess.Popen(
            [str(BINARY), "--window"] + ([] if movies else ["--skip-movies"]) + (["--fast"] if fast else [])
            + list(args), cwd=ROOT, env=env, stdout=log_file, stderr=subprocess.STDOUT)
        processes.insert(0, game)
        deadline = time.monotonic() + 30
        while not os.path.exists(socket_path):
            if game.poll() is not None or time.monotonic() > deadline:
                for p in processes:
                    p.kill()
                raise RpcError("game did not open the RPC socket; see " + log_file.name)
            time.sleep(0.05)
        instance = cls(socket_path, processes)
        instance.log = log_file.name
        instance.display = env.get("DISPLAY")
        return instance

    def close(self):
        try:
            self.call("quit")
        except Exception:
            pass
        for p in self.processes:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
        for p in self.processes[1:]:
            p.terminate()
        if self.processes and os.path.exists(self.path):
            os.unlink(self.path)  # launched here: the game leaves its socket behind

    # ---- protocol ----
    def call(self, method, **params):
        self.next_id += 1
        request = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params:
            request["params"] = params
        self.file.write((json.dumps(request) + "\n").encode())
        self.file.flush()
        line = self.file.readline()
        if not line:
            raise RpcError("connection closed (game exited?)")
        response = json.loads(line)
        if "error" in response:
            raise RpcError(response["error"]["message"])
        return response["result"]

    # ---- conveniences ----
    def status(self):
        return self.call("status")

    def state(self):
        return self.call("state")

    def pause(self, paused=True):
        return self.call("pause", paused=paused)

    def wait_paused(self, timeout=120):
        deadline = time.monotonic() + timeout
        while True:
            s = self.status()
            if s["paused"] and s["step_budget"] == 0:
                return s
            if time.monotonic() > deadline:
                raise RpcError("timed out waiting for the step to finish")
            time.sleep(0.01)

    def run(self, vsyncs):
        """Run exactly `vsyncs` vsyncs, then stay paused."""
        self.call("step", vsyncs=vsyncs)
        return self.wait_paused(timeout=max(30, vsyncs / 30))

    def press(self, buttons, vsyncs=4, settle=8):
        """Hold buttons for `vsyncs`, then run `settle` more vsyncs released."""
        if isinstance(buttons, str):
            buttons = [buttons]
        self.call("input", buttons=buttons, vsyncs=vsyncs)
        return self.run(vsyncs + settle)

    def hold(self, buttons, vsyncs):
        if isinstance(buttons, str):
            buttons = [buttons]
        self.call("input", buttons=buttons, vsyncs=vsyncs)
        return self.run(vsyncs)

    def screenshot(self, path=None):
        path = pathlib.Path(path or OUT / "rpc-screenshot.png")
        ppm = path.with_suffix(".ppm")
        info = self.call("screenshot", path=str(ppm.resolve()))
        if path.suffix != ".ppm":
            try:
                from PIL import Image
                Image.open(ppm).save(path)
                ppm.unlink()
                info["path"] = str(path)
            except ImportError:
                pass
        return info

    def present_screenshot(self, path, timeout=10):
        """The next image the game presents (read back in the game; works
        where screen capture does not, e.g. Wayland). Converted to `path`."""
        path = pathlib.Path(path)
        ppm = path.with_suffix(".capture.ppm")
        if ppm.exists():
            ppm.unlink()
        self.call("present_dump", path=str(ppm.resolve()))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if ppm.exists() and ppm.stat().st_size > 0:
                time.sleep(0.05)
                break
            time.sleep(0.02)
        else:
            raise RpcError("no frame presented")
        subprocess.run(["convert", str(ppm), str(path)], check=True)
        ppm.unlink()
        return path

    def window_screenshot(self, path):
        """Grab the presented window (after scaling/post-processing) from the X server
        (Xvfb or a real window; under egl use present_screenshot)."""
        subprocess.run(["import", "-display", self.display, "-window", "root", str(path)], check=True)
        return {"path": str(path)}

    def read(self, address, length=4):
        return bytes.fromhex(self.call("read", address=address, length=length)["hex"])

    def write(self, address, data):
        return self.call("write", address=address, hex=bytes(data).hex())

    def read_symbol(self, name, length=4):
        return self.read(symbols()[name], length)

    def u32(self, name_or_address):
        a = symbols()[name_or_address] if isinstance(name_or_address, str) else name_or_address
        return int.from_bytes(self.read(a, 4), "little")

    def wait_title(self, timeout=60):
        """Run until the title menu is ready (log marker), then pause."""
        deadline = time.monotonic() + timeout
        while "TITLE MENU READY" not in pathlib.Path(self.log).read_text():
            if time.monotonic() > deadline:
                raise RpcError("title menu not reached")
            time.sleep(0.05)
        return self.pause()


def bridge(path):
    """stdin/stdout JSON-RPC pass-through with client-side symbol methods."""
    game = Game(path)
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            print(json.dumps({"jsonrpc": "2.0", "id": None,
                              "error": {"code": -32700, "message": "parse error"}}), flush=True)
            continue
        method, params = request.get("method"), request.get("params") or {}
        try:
            if method == "symbol":
                result = {"address": symbols()[params["name"]]}
            elif method == "read_symbol":
                result = {"hex": game.read_symbol(params["name"], params.get("length", 4)).hex()}
            else:
                result = game.call(method, **params)
            response = {"jsonrpc": "2.0", "id": request.get("id"), "result": result}
        except (RpcError, KeyError) as e:
            response = {"jsonrpc": "2.0", "id": request.get("id"),
                        "error": {"code": -32000, "message": str(e)}}
        print(json.dumps(response), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--socket", default=DEFAULT_SOCKET)
    parser.add_argument("--rpc", action="store_true", help="stdin/stdout JSON-RPC bridge")
    sub = parser.add_subparsers(dest="command")
    launch = sub.add_parser("launch")
    launch.add_argument("--window", action="store_true")
    launch.add_argument("--card")
    launch.add_argument("--audio", default="none")
    launch.add_argument("--movies", action="store_true", help="play the intro movie")
    launch.add_argument("--headless", choices=["egl", "xvfb"], help="default: $ROOD_HEADLESS, else egl if it works")
    sub.add_parser("headless")
    call = sub.add_parser("call")
    call.add_argument("method")
    call.add_argument("params", nargs="?", default="{}")
    sym = sub.add_parser("symbol")
    sym.add_argument("name")
    args = parser.parse_args()
    if args.rpc:
        bridge(args.socket)
    elif args.command == "launch":
        game = Game.launch(window=args.window, card=args.card, audio=args.audio, socket_path=args.socket,
                           movies=args.movies, headless=args.headless)
        print(json.dumps({"socket": args.socket, "pid": game.processes[0].pid, "log": game.log,
                          "display": game.display}))
    elif args.command == "headless":
        mode, renderer = headless_mode()
        print(json.dumps({"mode": mode, "renderer": renderer}))
    elif args.command == "call":
        print(json.dumps(Game(args.socket).call(args.method, **json.loads(args.params))))
    elif args.command == "symbol":
        print(hex(symbols()[args.name]))
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
