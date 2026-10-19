#!/usr/bin/env python3
"""Client for the game's developer JSON-RPC socket (pc/src/dev_rpc.c).

Python:
    from rood_rpc import Game
    game = Game.launch(card=copy_of_a_test_card)  # never a player's card: a temp copy (card_fixtures.py)
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
    rood_rpc.py lua 'return rood.version' [--plugin ID]   evaluate Lua in the running game
    rood_rpc.py lua-test [--plugin ID] [--filter TEXT]     run the plugins' Lua tests in it
    rood_rpc.py repl eval CODE [--session S] [--plugin ID]   the REPL bridge (RPC lua {action "repl"}):
    rood_rpc.py repl describe EXPR | inspect WHAT [ARG] | sessions | close   one JSON reply each
                                              (pc/tools/rood_repl.py: the interactive REPL)
    rood_rpc.py hud-show [KIND] [--timing slow] [--value N] [--heal] [--pool mp] [--stat S] [--slot S] [--no-hold]
                                              summon a HUD element (RPC hud show): status, chain, buff, damage, travel, name_window
    rood_rpc.py hud [list|clear|stats]        the HUD layer's elements, claims and timings

The socket defaults to $ROOD_RPC or pc/build/boot/rpc.sock (on Windows tcp:47323, a
loopback TCP port; ROOD_RPC=tcp:PORT works on Linux too). Methods run at vsync
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
import secrets
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Windows: the build_boot.py --target windows executable, reached over TCP
# (Windows' Python has no AF_UNIX); always windowed (no EGL/Xvfb there).
WINDOWS = os.name == "nt"
# ROOD_ARCH=64 selects the LP64 build (build_boot.py --arch 64).
if WINDOWS:
    BUILD = ROOT / "pc/build/win64"
else:
    BUILD = ROOT / ("pc/build/boot64" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot")
BINARY = pathlib.Path(os.environ.get("ROOD_BINARY")  # ROOD_BINARY: e.g. a gdb wrapper
                      or BUILD / ("rood.exe" if WINDOWS else "rood_battle_probe"))
# A socket path, or tcp:PORT (a loopback TCP port; dev_rpc.c).
DEFAULT_SOCKET = os.environ.get("ROOD_RPC", "tcp:47323" if WINDOWS else str(ROOT / "pc/build/boot/rpc.sock"))
# Per-run output directory (logs, frames, scratch cards); the game reads it too.
OUT = pathlib.Path(os.environ.get("ROOD_OUT") or ROOT / "pc/build/boot")
_symbols = None
_headless = None
PROFILE = os.environ.get("ROOD_RPC_PROFILE")  # Game.call timing log (profile_report.py)


def symbols():
    """Game symbols -> addresses (native build is non-PIE, so nm is exact)."""
    global _symbols
    if _symbols is None:
        _symbols = {}
        # The executable that is running (or ROOD_SYMBOLS, e.g. rood.exe
        # read by a MinGW nm under WSL); ROOD_NM picks the nm.
        image = os.environ.get("ROOD_SYMBOLS") or str(BUILD / ("rood.exe" if WINDOWS else "rood_battle_probe"))
        out = subprocess.run([os.environ.get("ROOD_NM", "nm"), image], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                _symbols[parts[2]] = int(parts[0], 16)
    return _symbols


class RpcError(RuntimeError):
    pass


def wait_for_file(path, timeout=10):
    """True once `path` exists and is not empty. The game writes its images
    (present_dump) beside the name and renames them, so an existing image is
    complete: no settling sleep needed."""
    path = pathlib.Path(path)
    deadline = time.monotonic() + timeout
    while not (path.exists() and path.stat().st_size > 0):
        if time.monotonic() > deadline:
            return False
        time.sleep(0.003)
    return True


def egl_probe(device=None):
    """Renderer string if SDL's offscreen driver gets an OpenGL context on EGL
    `device` (index; None = the first that works), else None."""
    source = ROOT / "pc/tests/egl_probe.c"
    # Built against whichever SDL the builds made: the game's own word size
    # first (ROOD_ARCH), so a 64-bit-only checkout still gets EGL.
    # The SDL each build linked (its third-party.json: the shared build
    # cache, pc/tools/shared_cache.py), else a pre-cache build's own copy.
    def library(build, legacy):
        try:
            return pathlib.Path(json.loads((ROOT / build / "third-party.json").read_text())["sdl"])
        except (OSError, ValueError, KeyError):
            return ROOT / legacy / "libSDL3.a"

    builds = [("32", "pc/build/boot/egl_probe", library("pc/build/boot", "pc/build/sdl3")),
              ("64", "pc/build/boot64/egl_probe", library("pc/build/boot64", "pc/build/sdl3_64"))]
    if os.environ.get("ROOD_ARCH") == "64":
        builds.reverse()
    found = [(bits, ROOT / p, s) for bits, p, s in builds if s.exists()]
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


def is_tcp(path):
    return str(path).startswith("tcp:")


def tcp_parts(path):
    """(port, token or None) of tcp:PORT or tcp:PORT:TOKEN."""
    port, _, token = str(path)[4:].partition(":")
    return int(port), token or None


def with_token(path):
    """tcp:PORT with a fresh random token (tcp:PORT:TOKEN), so that only this client gets in;
    other paths unchanged."""
    if is_tcp(path) and tcp_parts(path)[1] is None:
        return f"{path}:{secrets.token_hex(16)}"
    return path


def connect(path):
    """A connected socket: a Unix socket path, or tcp:PORT[:TOKEN] on 127.0.0.1 (the token is
    sent as the first request, auth {token})."""
    if is_tcp(path):
        port, token = tcp_parts(path)
        if token is None:
            token_file = os.environ.get("ROOD_RPC_TOKEN_FILE") or str(OUT / "rpc.token")
            try:
                token = pathlib.Path(token_file).read_text().strip()
            except OSError as e:
                raise RpcError(f"cannot read TCP RPC token file {token_file}: {e}") from e
            if not token or len(token) > 128:
                raise RpcError(f"invalid TCP RPC token file {token_file}")
        sock = socket.create_connection(("127.0.0.1", port))
        if token:
            request = {"jsonrpc": "2.0", "id": 0, "method": "auth", "params": {"token": token}}
            sock.sendall((json.dumps(request) + "\n").encode())
            reply = b""
            while not reply.endswith(b"\n"):
                chunk = sock.recv(4096)
                if not chunk:
                    sock.close()
                    raise RpcError("the game refused the RPC token")
                reply += chunk
            if "error" in json.loads(reply):
                sock.close()
                raise RpcError("the game refused the RPC token")
        return sock
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(path)
    return sock


def listening(path):
    if not is_tcp(path):
        return os.path.exists(path)
    try:
        socket.create_connection(("127.0.0.1", tcp_parts(path)[0])).close()
        return True
    except OSError:
        return False


class Game:
    def __init__(self, path=DEFAULT_SOCKET, processes=()):
        self.path = path
        self.log = None
        self.display = os.environ.get("DISPLAY")
        self.processes = list(processes)
        self.next_id = 0
        self.has_wait = None  # the game answers "wait" (wait_paused)
        self.sock = connect(path)
        self.file = self.sock.makefile("rwb")

    # ---- launching ----
    @classmethod
    def launch(cls, window=False, card=None, audio="none", socket_path=None, log=None, extra_env=None,
               movies=False, headless=None, fast=False, args=(), snapshot=None):
        """Start the game with RPC enabled; headless unless window=True
        (`headless`: "egl" or "xvfb", default headless_mode()). The intro
        movie is skipped unless movies=True; fast=True drops the 59.94 Hz
        pacing (vsync-exact RPC stepping is unaffected). snapshot: a save
        state (RPC snapshot save; pc/src/snapshot.c) to resume instead of
        booting: the game starts paused where it was saved."""
        socket_path = with_token(socket_path or DEFAULT_SOCKET)
        env = dict(os.environ, ROOD_RPC=socket_path, ROOD_CONTROLLER="none", ROOD_AUDIO=audio)
        env.setdefault("ROOD_GI", "0")  # path tracing is slow on software GL; tests opt in
        # The PS1 glyphs, whatever device the machine has (button_prompts_smoke opts in).
        env.setdefault("ROOD_BUTTON_PROMPTS", "original")
        env.setdefault("ROOD_CARD1", card or str(OUT / "test-card1.mcd"))
        if card:
            env["ROOD_CARD1"] = card
        env.setdefault("ROOD_CARD2", str(OUT / "test-card2-absent.mcd"))
        env.update(extra_env or {})
        if snapshot:
            env["ROOD_SNAPSHOT"] = str(snapshot)
        processes = []
        if not window and not WINDOWS:
            mode = headless or headless_mode()[0]
            if mode == "egl":
                headless_env(mode, env)
            else:
                xvfb, env["DISPLAY"] = start_xvfb()
                env["SDL_VIDEO_DRIVER"] = "x11"  # the private display, not the desktop's Wayland
                env.pop("WAYLAND_DISPLAY", None)
                processes.append(xvfb)
        if not is_tcp(socket_path) and os.path.exists(socket_path):
            os.unlink(socket_path)
        OUT.mkdir(parents=True, exist_ok=True)
        log_file = open(log or OUT / "rpc-game.log", "w")
        game = subprocess.Popen(
            [str(BINARY), "--window"] + ([] if movies else ["--skip-movies"]) + (["--fast"] if fast else [])
            + list(args), cwd=ROOT, env=env, stdout=log_file, stderr=subprocess.STDOUT)
        processes.insert(0, game)
        start = time.monotonic()
        # A game that is still starting is waited for: under load (several suites sharing the
        # GPU, Vulkan pipelines compiled on a cold cache) that took more than 30 s.
        # ROOD_LAUNCH_TIMEOUT seconds (default 180); an exited game fails at once.
        deadline = start + float(os.environ.get("ROOD_LAUNCH_TIMEOUT", "180"))
        while not listening(socket_path):
            if game.poll() is not None or time.monotonic() > deadline:
                for p in processes:
                    p.kill()
                why = (f"exited with status {game.returncode}" if game.poll() is not None
                       else f"no socket after {time.monotonic() - start:.0f} s")
                raise RpcError(f"game did not open the RPC socket ({why}); see " + log_file.name)
            time.sleep(0.05)
        if PROFILE:
            with open(PROFILE, "a") as f:
                f.write(f"{start:.4f} {time.monotonic() - start:.4f} launch - {os.getpid()}\n")
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
        if self.processes and not is_tcp(self.path) and os.path.exists(self.path):
            os.unlink(self.path)  # launched here: the game leaves its socket behind

    # ---- protocol ----
    def call(self, method, **params):
        if PROFILE:
            return self._profiled_call(method, params)
        return self._call(method, params)

    def _profiled_call(self, method, params):
        """ROOD_RPC_PROFILE=path: one line per call (start, seconds, method,
        the test's file:line), for pc/tests/profile_report.py."""
        frame = sys._getframe(2)
        while frame and frame.f_code.co_filename == __file__:
            frame = frame.f_back
        site = f"{pathlib.Path(frame.f_code.co_filename).name}:{frame.f_lineno}" if frame else "?"
        start = time.monotonic()
        try:
            return self._call(method, params)
        finally:
            with open(PROFILE, "a") as f:
                f.write(f"{start:.4f} {time.monotonic() - start:.4f} {method} {site} {os.getpid()}\n")

    def _call(self, method, params):
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

    STALL = 90  # seconds without a vsync before a step counts as hung

    def wait_paused(self, timeout=None, stall=None):
        """Wait for the step to finish. A step may take as long as the
        machine needs (path tracing under load): it fails only when the game
        stops advancing (no vsync for `stall` seconds, default STALL: a
        hang), or after `timeout` seconds in all when one is given."""
        stall = stall or self.STALL
        start = moved = time.monotonic()
        last = None
        while True:
            # "wait" replies as soon as the step is over (or after a second):
            # no status polling. Older games without it: status every 10 ms.
            if self.has_wait is not False:
                try:
                    s = self.call("wait", timeout_ms=1000)
                    self.has_wait = True
                except RpcError as e:
                    if self.has_wait or "method" not in str(e):
                        raise
                    self.has_wait = False
                    continue
            else:
                s = self.status()
            if s["paused"] and s["step_budget"] == 0:
                return s
            now = time.monotonic()
            if s["vsync"] != last:
                last, moved = s["vsync"], now
            elif now - moved > stall:
                raise RpcError(f"the step stopped advancing: no vsync for {stall} s at vsync {last} "
                               f"({s['step_budget']} to go; hung?)")
            if timeout is not None and now - start > timeout:
                raise RpcError(f"timed out waiting for the step to finish ({timeout} s)")
            if not self.has_wait:
                time.sleep(0.01)

    def digest(self, action, **params):
        """Record/compare per-vsync state pages; see pc/docs/Oracle.md."""
        return self.call("digest", action=action, **params)

    def record(self, path):
        """Record buttons/stick by vsync; None stops and closes the file."""
        return self.call("record", path=str(path) if path else "")

    def replay(self, path):
        """Replay by recorded vsync (rewind seeks again); None stops."""
        return self.call("replay", path=str(path) if path else "")

    def mark(self):
        """Keep one in-process checkpoint (requires an idle paused game)."""
        return self.call("snapshot", action="mark")

    def rewind(self):
        """Restore the checkpoint and stay paused at its vsync."""
        return self.call("snapshot", action="rewind")

    def run(self, vsyncs):
        """Run exactly `vsyncs` vsyncs, then stay paused (no wall-clock
        limit while the game advances; see wait_paused)."""
        self.call("step", vsyncs=vsyncs)
        return self.wait_paused()

    def reload_lights(self):
        """Read the current room's lighting files from disk now (RPC lights
        {action: reload}): call it right after writing a room file instead
        of waiting for the editor's twice-a-second poll. Returns the lights
        reply (its "loads" counts the room's layer loads)."""
        return self.call("lights", action="reload")

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
        if not wait_for_file(ppm, timeout):
            raise RpcError("no frame presented")
        subprocess.run(["convert", str(ppm), str(path)], check=True)
        ppm.unlink()
        return path

    def wait_presents(self, count=2, timeout=10):
        """Wait until the game has presented `count` more images (a paused
        game redraws about 60 times a second): after a graphics change,
        instead of sleeping. Returns at once on games without the counter."""
        start = self.status().get("presents")
        if start is None:
            return
        deadline = time.monotonic() + timeout
        while self.status()["presents"] < start + count:
            if time.monotonic() > deadline:
                raise RpcError(f"no present for {timeout} s")
            time.sleep(0.002)

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

    # ---- Lua (pc/src/lua, pc/docs/Extensions.md) ----
    def lua(self, code, plugin=None):
        """Evaluate Lua in a plugin (default: the console's own sandbox) under the
        test budget: {"ok", "results", "output", "error"} (a Lua error is a result,
        not an RpcError)."""
        params = {"action": "eval", "code": code}
        if plugin:
            params["plugin"] = plugin
        return self.call("lua", **params)

    def repl(self, op="eval", session=None, plugin=None, **params):
        """The REPL bridge (RPC lua {action "repl"}, pc/src/lua/lua_repl.cpp; dev mode): op eval
        {code}, describe {code}, inspect {what, arg}, sessions, close. A session (any name of
        letters, digits, _ - .; made on first use, else one is named in the reply) keeps its
        bindings between evals; plugin picks the sandbox (default "@console")."""
        params = {k: v for k, v in params.items() if v is not None}
        if session:
            params["session"] = session
        if plugin:
            params["plugin"] = plugin
        return self.call("lua", action="repl", op=op, **params)

    def lua_plugins(self):
        """Every plugin by id (RPC lua {action "plugins"})."""
        return {p["id"]: p for p in self.call("lua", action="plugins")["plugins"]}

    def lua_test(self, plugin=None, filter=None, step=2, timeout=120):
        """Run the plugins' Lua tests (rood.test) in this game, without a relaunch:
        start a run, then step the game `step` vsyncs at a time while cases wait for
        ticks. Returns the finished run ({passed, failed, skipped, results})."""
        params = {"action": "test"}
        if plugin:
            params["plugin"] = plugin
        if filter:
            params["filter"] = filter
        run = self.call("lua", **params)
        deadline = time.monotonic() + timeout
        while not run["done"]:
            if time.monotonic() > deadline:
                raise RpcError(f"Lua tests still waiting after {timeout} s: {run['waiting']} case(s)")
            self.run(step)
            run = self.call("lua", action="test_status", run=run["run"])
        return run


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
    lua = sub.add_parser("lua", help="evaluate Lua in the running game (RPC lua eval)")
    lua.add_argument("code")
    lua.add_argument("--plugin")
    lua_test = sub.add_parser("lua-test", help="run the plugins' Lua tests in the running game")
    lua_test.add_argument("--plugin")
    lua_test.add_argument("--filter")
    repl = sub.add_parser("repl", help="the REPL bridge in the running game (RPC lua repl; rood_repl.py)")
    repl.add_argument("op", choices=["eval", "describe", "inspect", "sessions", "close"])
    repl.add_argument("text", nargs="?", help="eval: code; describe: an expression; inspect: plugins, claims, "
                                              "exports, queries, globals, upvalues, complete")
    repl.add_argument("arg", nargs="?", help="inspect upvalues: an expression; complete: the text")
    repl.add_argument("--session", "-s")
    repl.add_argument("--plugin", "-p")
    hud_show = sub.add_parser("hud-show", help="summon a HUD element in the running game, held (RPC hud show)")
    hud_show.add_argument("kind", nargs="?", default="status")
    hud_show.add_argument("--timing", default="slow", help="too_fast, fast, success, slow, too_slow")
    hud_show.add_argument("--variant", type=int, default=0)
    hud_show.add_argument("--value", type=int, help="chain: the count; buff, damage: the amount")
    hud_show.add_argument("--heal", action="store_true", help="damage: healing (buff: a gain is the default)")
    hud_show.add_argument("--loss", action="store_true", help="buff: a loss")
    hud_show.add_argument("--pool", help="damage: hp, mp or stat")
    hud_show.add_argument("--stat", help="buff: human .. darkness; damage with --pool stat: 0..5")
    hud_show.add_argument("--slot", help="buff: weapon, shield, r.arm, l.arm, head, body, legs")
    hud_show.add_argument("--index", type=int, help="name_window: the action (vs_main_actions' index; 184)")
    hud_show.add_argument("--message", type=int, help="name_window: the scene message (11 a technique's name)")
    hud_show.add_argument("--text", help="name_window away from the field: its text")
    hud_show.add_argument("--no-hold", action="store_true")
    hud = sub.add_parser("hud", help="the HUD layer (RPC hud): list, clear, stats")
    hud.add_argument("action", nargs="?", default="list", choices=["list", "clear", "stats"])
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
    elif args.command == "lua":
        print(json.dumps(Game(args.socket).lua(args.code, plugin=args.plugin), indent=1))
    elif args.command == "repl":
        extra = {"eval": {"code": args.text}, "describe": {"code": args.text},
                 "inspect": {"what": args.text, "arg": args.arg}}.get(args.op, {})
        print(json.dumps(Game(args.socket).repl(args.op, session=args.session, plugin=args.plugin, **extra),
                         indent=1))
    elif args.command == "hud-show":
        params = dict(action="show", kind=args.kind, timing=args.timing, variant=args.variant, hold=not args.no_hold)
        params.update({k: v for k, v in [("value", args.value), ("pool", args.pool), ("slot", args.slot),
                                         ("index", args.index), ("message", args.message), ("text", args.text)]
                       if v is not None})
        if args.stat is not None:
            params["stat"] = int(args.stat) if args.stat.isdigit() else args.stat
        if args.heal or args.loss:
            params["sign"] = 1 if args.heal else -1
        print(json.dumps(Game(args.socket).call("hud", **params)))
    elif args.command == "hud":
        print(json.dumps(Game(args.socket).call("hud", action=args.action), indent=1))
    elif args.command == "lua-test":
        run = Game(args.socket).lua_test(plugin=args.plugin, filter=args.filter)
        for r in run["results"]:
            print(f"{r['status'].upper():<5} {r['plugin']} {r['file']}: {r['name']}")
            if r["status"] == "fail":
                print("      " + r["message"].replace("\n", "\n      "))
        print(f"{run['passed']} passed, {run['failed']} failed, {run['skipped']} skipped")
        sys.exit(1 if run["failed"] else 0)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
