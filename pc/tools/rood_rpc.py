#!/usr/bin/env python3
"""Client for the game's developer JSON-RPC socket (pc/src/dev_rpc.c).

Python:
    from rood_rpc import Game
    game = Game.launch(card=copy_of_a_test_card)  # never a player's card: a temp copy (card_fixtures.py)
    game.wait_title()
    game.press("start"); game.run(120)              # run 120 vsyncs, then pause
    print(game.state()); game.screenshot("shot.png")
    game.value("randIndex")                         # state by name: the game resolves "vs_battle_actors[3]->stats.currentHP"
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

The socket of a client defaults to $ROOD_RPC or pc/build/boot/rpc.sock; a game LAUNCHED here without one (Game.launch)
gets a socket of its own (rood-rpc-PID-N.sock in the temp directory) so runs at once never share one, and one that
is alive is never taken away (on Windows tcp:47323, a
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
import atexit
import itertools
import json
import os
import pathlib
import secrets
import socket
import signal
import subprocess
import sys
import tempfile
import threading
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
    # The 64-bit game's own default renderer is Vulkan: named here too, so a suite started by
    # hand asks ROOD_RENDERER and gets the renderer it runs on (pc/tests/run.py always names it).
    if os.environ.get("ROOD_ARCH") == "64":
        os.environ.setdefault("ROOD_RENDERER", "vulkan")
BINARY = pathlib.Path(os.environ.get("ROOD_BINARY")  # ROOD_BINARY: e.g. a gdb wrapper
                      or BUILD / ("rood.exe" if WINDOWS else "rood_battle_probe"))
# A socket path, or tcp:PORT (a loopback TCP port; dev_rpc.c).
DEFAULT_SOCKET = os.environ.get("ROOD_RPC", "tcp:47323" if WINDOWS else str(ROOT / "pc/build/boot/rpc.sock"))
# Per-run output directory (logs, frames, scratch cards); the game reads it too.
OUT = pathlib.Path(os.environ.get("ROOD_OUT") or ROOT / "pc/build/boot")
_symbols = None
_headless = None
_launches = itertools.count()


def unique_socket():
    """A socket path of this run's own (the process and the launch's number, in the temp directory): a game
    launched without one shares nothing with another run's."""
    return os.path.join(tempfile.gettempdir(), f"rood-rpc-{os.getpid()}-{next(_launches)}.sock")
PROFILE = os.environ.get("ROOD_RPC_PROFILE")  # Game.call timing log (profile_report.py)


def symbols():
    """Game symbols -> addresses, from the layout table (the executable's own DWARF and symbol table, so no nm).
    Raw addresses are what tools move away from (memory lane S2: Game.field names state by path and the game
    resolves it); this stays for the tests that have not moved yet, and answers a build's decomp or new names alike.
    A Windows executable (a PE image, read by a MinGW nm under WSL: ROOD_SYMBOLS, ROOD_NM) still goes through nm."""
    global _symbols
    if _symbols is None:
        image = os.environ.get("ROOD_SYMBOLS") or str(BUILD / ("rood.exe" if WINDOWS else "rood_battle_probe"))
        with open(image, "rb") as f:
            elf = f.read(4) == b"\x7fELF"
        if elf:
            table = layout()
            _symbols = {n: s["address"] for n, s in table.syms.items()}
            for old, new in table.aliases.items():
                if old in _symbols and new not in _symbols:
                    _symbols[new] = _symbols[old]
            return _symbols
        _symbols = {}
        out = subprocess.run([os.environ.get("ROOD_NM", "nm"), image], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                _symbols[parts[2]] = int(parts[0], 16)
        import symbol_names
        for old, new in symbol_names.load().items():
            if old in _symbols and new not in _symbols:
                _symbols[new] = _symbols[old]
            elif new in _symbols and old not in _symbols:
                _symbols[old] = _symbols[new]
    return _symbols


_table = None


def layout():
    """The layout table of the executable that is running (pc/tools/layout_table.py): the sidecar the build wrote
    beside it, else generated from its DWARF. Only for builds older than the RPC `field` (Game.field answers
    them client side); a current game resolves names itself."""
    global _table
    if _table is None:
        import layout_table
        image = os.environ.get("ROOD_SYMBOLS") or str(BUILD / ("rood.exe" if WINDOWS else "rood_battle_probe"))
        _table = layout_table.Table.for_binary(image)
    return _table


class RpcError(RuntimeError):
    pass


class Field:
    """A named place in game memory, as the game's RPC `field` answers: address, size, kind (int, uint, float, bool,
    pointer, guest_pointer, struct, union, array), type name, value (a scalar's number) and data (its bytes)."""

    def __init__(self, path, address, size, kind, type_name, value, data, readable=True):
        self.path, self.address, self.size, self.kind, self.type, self.value, self.data = (
            path, address, size, kind, type_name, value, data)
        self.readable = readable  # false: named and sized, but in memory the RPC does not read (read-only data)

    def __repr__(self):
        return f"Field({self.path!r}, kind={self.kind}, size={self.size}, value={self.value})"


# ---- the game's own time: waits and hang guards that a paused game does not trip ----
# A test waits for conditions the game reports (a vsync, a reload counter, a
# flag), never for a guessed number of seconds or vsyncs. What bounds a wait
# is only a hang guard, and the guard counts the time the game could run:
# the time its process spends stopped (SIGSTOP: the thermal guard pauses test
# games while the CPU is hot; a debugger) is left out.
_stops = {"lock": threading.Lock(), "pids": set(), "total": 0.0, "since": None, "thread": None}


def _stopped(pid):
    """True while process `pid` is stopped (state T or t); None once it is gone."""
    try:
        with open(f"/proc/{pid}/stat", "rb") as f:
            state = f.read().rsplit(b")", 1)[1].split()[0]
    except (OSError, IndexError):
        return None
    return None if state in (b"Z", b"X") else state in (b"T", b"t")


def _watch_loop():
    while True:
        with _stops["lock"]:
            pids = list(_stops["pids"])
        states = {pid: _stopped(pid) for pid in pids}
        now = time.monotonic()
        with _stops["lock"]:
            for pid, state in states.items():
                if state is None:
                    _stops["pids"].discard(pid)
            stopped = any(states.values())
            if stopped and _stops["since"] is None:
                _stops["since"] = now
            elif not stopped and _stops["since"] is not None:
                _stops["total"] += now - _stops["since"]
                _stops["since"] = None
            if not _stops["pids"]:
                _stops["thread"] = None
                return
        time.sleep(0.01)


def watch_stops(pid):
    """Count the time game process `pid` spends stopped (sampled from /proc
    every 10 ms by a daemon thread) out of awake(). Game.launch and Game(...,
    processes) do it; tests that start the game themselves call it. Not on
    Windows (no SIGSTOP there): awake() is then time.monotonic()."""
    if WINDOWS or not os.path.exists(f"/proc/{pid}"):
        return
    with _stops["lock"]:
        _stops["pids"].add(pid)
        if _stops["thread"] is None:
            _stops["thread"] = threading.Thread(target=_watch_loop, name="rood-stop-watch", daemon=True)
            _stops["thread"].start()


def stopped_seconds():
    """Seconds the watched games have spent stopped so far (any of them)."""
    with _stops["lock"]:
        since = _stops["since"]
        return _stops["total"] + (time.monotonic() - since if since is not None else 0.0)


def awake():
    """time.monotonic() less the time the watched games spent stopped: the
    clock for hang guards and for measured latencies."""
    return time.monotonic() - stopped_seconds()


class Deadline:
    """A hang guard of `seconds` of awake() time (a stopped game's pause is not
    counted): Deadline(60).passed() / .elapsed()."""

    def __init__(self, seconds):
        self.seconds, self.start = seconds, awake()

    def elapsed(self):
        return awake() - self.start

    def passed(self):
        return self.elapsed() > self.seconds


def wait_for_file(path, timeout=60):
    """True once `path` exists and is not empty. The game writes its images
    (present_dump) beside the name and renames them, so an existing image is
    complete: no settling sleep needed. `timeout` is a hang guard (awake
    seconds: a stopped game's pause does not count)."""
    path = pathlib.Path(path)
    deadline = Deadline(timeout)
    while not (path.exists() and path.stat().st_size > 0):
        if deadline.passed():
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
    """("egl" | "xvfb" | "null", renderer or None); null never probes graphics."""
    global _headless
    if os.environ.get("ROOD_RENDERER") == "null" or os.environ.get("ROOD_HEADLESS") == "null":
        return ("null", "CPU VRAM (no graphics)")
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


def alive(path):
    """A game is accepting connections on this Unix socket (a file left behind by a killed game is not)."""
    if is_tcp(path):
        return listening(path)
    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        probe.connect(path)
        return True
    except OSError:
        return False
    finally:
        probe.close()


def listening(path):
    if not is_tcp(path):
        return os.path.exists(path)
    try:
        socket.create_connection(("127.0.0.1", tcp_parts(path)[0])).close()
        return True
    except OSError:
        return False


_direct_failures = []


def _exit_on_direct_hooks():
    if _direct_failures:
        sys.stdout.flush()
        os._exit(1)


atexit.register(_exit_on_direct_hooks)


def _die_with_parent():
    """In the game's child process (Popen preexec_fn): sent SIGTERM when the process that launched it ends, so a script that ends on an
    exception without closing its game (a timeout in a menu walk, a failed RPC) leaves no orphan holding the GPU and a window for hours (the
    game logs its own [exit]). Linux; elsewhere nothing. PDEATHSIG follows the launching THREAD, not the process: a game launched from a
    worker thread would be signalled when that thread ended, mid-run, so launch() sets this only from the main thread and the rest are
    reaped by _reap_launched at the end of the script."""
    try:
        import ctypes
        import signal
        ctypes.CDLL("libc.so.6", use_errno=True).prctl(1, signal.SIGTERM)  # PR_SET_PDEATHSIG
    except (OSError, AttributeError):
        pass


_launched = []  # the games launched here (Popen), for _reap_launched


def _reap_launched():
    """At the end of the script (atexit): a game still running is asked to quit (SIGTERM: its own exit is logged), then killed after 5 s."""
    live = [p for p in _launched if p.poll() is None]
    for p in live:
        p.terminate()
    for p in live:
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()


atexit.register(_reap_launched)


class Game:
    def __init__(self, path=DEFAULT_SOCKET, processes=()):
        self.path = path
        self.log = None
        self.display = os.environ.get("DISPLAY")
        self.processes = list(processes)
        self.next_id = 0
        self.has_wait = None  # the game answers "wait" (wait_paused)
        for p in self.processes[:1]:  # the game (the rest: its Xvfb)
            watch_stops(p.pid)
        self.sock = connect(path)
        self.file = self.sock.makefile("rwb")

    # ---- launching ----
    @classmethod
    def launch(cls, window=False, card=None, audio="none", socket_path=None, log=None, extra_env=None,
               movies=False, headless=None, fast=False, args=(), snapshot=None):
        """Start the game with RPC enabled; headless unless window=True
        (`headless`: "egl", "xvfb" or "null", default headless_mode()). The intro
        movie is skipped unless movies=True; fast=True drops the 59.94 Hz
        pacing (vsync-exact RPC stepping is unaffected). snapshot: a save
        state (RPC snapshot save; pc/src/snapshot.c) to resume instead of
        booting: the game starts paused where it was saved."""
        if socket_path is None and not os.environ.get("ROOD_RPC") and not WINDOWS:
            socket_path = unique_socket()  # runs at once (two build_compares, two worktrees) never share one
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
        null = (headless == "null" or env.get("ROOD_RENDERER") == "null"
                or env.get("ROOD_HEADLESS") == "null")
        if null:
            if window:
                raise RpcError("null renderer cannot open a desktop window")
            env["ROOD_RENDERER"] = "null"
            env["ROOD_HEADLESS"] = "null"
            env.pop("DISPLAY", None)
            env.pop("WAYLAND_DISPLAY", None)
        if not null and not window and not WINDOWS:
            mode = headless or headless_mode()[0]
            if mode == "egl":
                headless_env(mode, env)
            else:
                xvfb, env["DISPLAY"] = start_xvfb()
                env["SDL_VIDEO_DRIVER"] = "x11"  # the private display, not the desktop's Wayland
                env.pop("WAYLAND_DISPLAY", None)
                processes.append(xvfb)
        if not is_tcp(socket_path) and os.path.exists(socket_path):
            if alive(socket_path):  # another game's: never take its socket away
                raise RpcError(f"{socket_path} is a running game's RPC socket; launch with another socket_path")
            os.unlink(socket_path)
        OUT.mkdir(parents=True, exist_ok=True)
        log_file = open(log or OUT / "rpc-game.log", "w")
        game = subprocess.Popen(
            [str(BINARY), "--window"] + ([] if movies else ["--skip-movies"]) + (["--fast"] if fast else [])
            + list(args), cwd=ROOT, env=env, stdout=log_file, stderr=subprocess.STDOUT,
            preexec_fn=_die_with_parent if threading.current_thread() is threading.main_thread() else None)
        _launched.append(game)
        processes.insert(0, game)
        watch_stops(game.pid)
        start = time.monotonic()
        # A game that is still starting is waited for: under load (several suites sharing the
        # GPU, Vulkan pipelines compiled on a cold cache) that took more than 30 s.
        # ROOD_LAUNCH_TIMEOUT awake seconds (default 180); an exited game fails at once.
        deadline = Deadline(float(os.environ.get("ROOD_LAUNCH_TIMEOUT", "180")))
        while not listening(socket_path):
            if game.poll() is not None or deadline.passed():
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
        # The renderer thread in mode 2 (async): a renderer hook the game thread called directly is a race the mailbox does not cover (the goal
        # is none), so a run that ends with one fails: the message is printed and the process exits 1 when the test script ends (atexit).
        try:
            state = self.call("render_thread")
            if state.get("mode", 0) >= 2 and state.get("direct") and not os.environ.get("ROOD_RT_DIRECT_ALLOW"):
                names = ", ".join(f"{d['hook']} x{d['calls']}" for d in state["direct"])
                print(f"FAIL: the game thread called renderer hooks directly in render_thread {state['mode']}: {names}", flush=True)
                _direct_failures.append(names)
        except Exception:
            pass
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

    def exit_status(self):
        """How the game process ended, for the error of a closed connection: its exit code, or the signal that
        killed it (a SIGKILL leaves nothing in the game's log: something outside ended it; -9), or that it is
        still running (the game closed the connection itself)."""
        game = self.processes[0] if self.processes else None
        if not hasattr(game, "wait"):
            return "game exited?"
        try:
            code = game.wait(timeout=2)  # (a game that is going away is given a moment to be reaped)
        except subprocess.TimeoutExpired:
            return f"the game (pid {game.pid}) is still running"
        if code < 0:
            try:
                name = signal.Signals(-code).name
            except ValueError:
                name = f"signal {-code}"
            return f"the game was killed by {name}" + (", sent from outside: nothing in its log" if code == -9 else "")
        return f"the game exited with status {code}"

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
        try:
            self.file.write((json.dumps(request) + "\n").encode())
            self.file.flush()
            line = self.file.readline()
        except (BrokenPipeError, ConnectionResetError):
            line = b""
        if not line:
            raise RpcError(f"connection closed ({self.exit_status()})")
        response = json.loads(line)
        if "error" in response:
            message = response["error"]["message"]
            if "is for another build" in message and method in ("field", "symbol_info", "symbols") \
                    and not getattr(self, "_relayout", False):
                # the sidecar is older than the executable (a development build relinked): make it again, ask again
                self._relayout = True
                try:
                    import layout_table
                    image = os.environ.get("ROOD_SYMBOLS") or str(BUILD / ("rood.exe" if WINDOWS else "rood_battle_probe"))
                    layout_table.Table.for_binary(image)
                    return self._call(method, params)
                finally:
                    self._relayout = False
            raise RpcError(message)
        return response["result"]

    # ---- conveniences ----
    def status(self):
        return self.call("status")

    def state(self):
        return self.call("state")

    def pause(self, paused=True):
        return self.call("pause", paused=paused)

    STALL = 90  # awake seconds without a vsync before a step counts as hung

    def wait_paused(self, timeout=None, stall=None):
        """Wait for the step to finish. A step may take as long as the
        machine needs (path tracing under load): it fails only when the game
        stops advancing (no vsync for `stall` awake seconds, default STALL: a
        hang), or after `timeout` awake seconds in all when one is given
        (awake(): time the game spent stopped does not count)."""
        stall = stall or self.STALL
        start = moved = awake()
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
            now = awake()
            if s["vsync"] != last:
                last, moved = s["vsync"], now
            elif now - moved > stall:
                raise RpcError(f"the step stopped advancing: no vsync for {stall} s at vsync {last} "
                               f"({s['step_budget']} to go; hung?)")
            if timeout is not None and now - start > timeout:
                raise RpcError(f"timed out waiting for the step to finish ({timeout} s)")
            if not self.has_wait:
                time.sleep(0.01)

    def reload(self, target="engine"):
        """Swap the engine or Zig library at vsync; return the reload status."""
        if target not in ("engine", "zig"):
            raise ValueError("reload target must be engine or zig")
        return self.call("reload", target=target)

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

    def until(self, condition, step=2, guard=60, what=None):
        """Run the game `step` vsyncs at a time until condition() is true: a
        condition the game reports (a value, a reload counter, a flag), never a
        guessed number of vsyncs or seconds (menus glide and files are polled
        on real time). `guard` only bounds a hang, in awake seconds (a stopped
        game's pause does not count). Returns condition()'s last value, false
        when the guard ran out (or raises RpcError when `what` names the
        condition)."""
        deadline = Deadline(guard)
        while True:
            value = condition()
            if value or deadline.passed():
                break
            self.run(step)
        if not value and what:
            raise RpcError(f"{what}: not after {guard} s of the game running")
        return value

    def polls(self):
        """The game's file watchers' looks so far (RPC status polls): files
        (settings.cfg, the room's lighting files, emitter assets, fog presets;
        dev_overlay.cpp, twice a second), plugins (Lua hot reload), theme (the
        UI theme file, while a menu draws)."""
        polls = self.status().get("polls")
        if polls is None:
            raise RpcError("RPC status has no polls (an older game?)")
        return polls

    def wait_polled(self, watcher="files", step=2, guard=60):
        """After writing a file: run the game until its `watcher` (see polls)
        has looked at its files since, so what it loaded is in place (a look
        reads and applies a changed file at once). Then check exactly: no
        guessed sleep, no 'until it has the value' that hides a wrong one."""
        start = self.polls()[watcher]
        self.until(lambda: self.polls()[watcher] != start, step=step, guard=guard,
                   what=f"the game's {watcher} watcher looking at its files")

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

    def present_screenshot(self, path, timeout=60):
        """The next image the game presents (read back in the game; works
        where screen capture does not, e.g. Wayland). Converted to `path`.
        `timeout`: a hang guard in awake seconds."""
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

    def wait_presents(self, count=2, timeout=60):
        """Wait until the game has presented `count` more images (a paused
        game redraws about 60 times a second): after a graphics change,
        instead of sleeping. Returns at once on games without the counter.
        `timeout`: a hang guard in awake seconds."""
        start = self.status().get("presents")
        if start is None:
            return
        deadline = Deadline(timeout)
        while self.status()["presents"] < start + count:
            if deadline.passed():
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

    # ---- state by name: the game resolves the path ("vs_battle_actors[3]->unk3C.hp", pc/tools/layout_table.py
    # has the grammar), so a tool never knows an address or an offset; a build older than the RPC answers through
    # its own layout table. ----
    def field(self, path, length=None, offset=0):
        """The Field at `path`. `length`: how many bytes from its address come back as data (default: its size).
        `offset`: bytes past the named place (for a record laid out by a file format, the save card's, that no name
        is worth); the answer is then untyped."""
        params = {"path": path} if length is None else {"path": path, "length": length}
        if offset:
            params["offset"] = offset
        if getattr(self, "_named", True):
            try:
                r = self.call("field", **params)
                self._named = True
                return Field(path, r["address"], r["size"], r["kind"], r.get("type"), r.get("value"),
                             bytes.fromhex(r.get("hex", "")), r.get("readable", True))
            except RpcError as e:
                if "unknown method" not in str(e):
                    raise
                self._named = False
        table = layout()
        try:
            ref = table.resolve(path, read=self.read)
        except LookupError as e:
            raise RpcError(str(e))
        if offset:
            ref = {**ref, "address": ref["address"] + offset, "size": max(ref["size"] - offset, 0), "type": None,
                   "bitsize": 0, "bitoffset": 0, "limit": None if ref["limit"] is None else ref["limit"] - offset}
        size = ref["size"] if not length else length  # (length 0: no bytes back, but the scalar's value is decoded)
        try:
            data, readable = (self.read(ref["address"], size) if size else b""), True
        except RpcError:
            data, readable = b"", False
        t = table.types.get(ref["type"])
        return Field(path, ref["address"], ref["size"], table.kind_word(ref["type"]), t["name"] if t else None,
                     table.decode(ref, data) if readable else None, b"" if length == 0 else data, readable)

    def value(self, path):
        """A scalar's number at `path` (bit fields extracted, pointers as their address)."""
        f = self.field(path, length=0 if getattr(self, "_named", True) else None)
        if f.value is None:
            raise RpcError(f"{path} is not readable" if not f.readable else f"{path} is a {f.kind}, not a scalar")
        return f.value

    def data(self, path, length=None, offset=0):
        """The bytes at `path` (its whole size, or `length` bytes from its address; `offset`: see field)."""
        f = self.field(path, length, offset)
        if not f.readable:
            raise RpcError(f"{path} is not readable")
        return f.data

    def store(self, path, value):
        """Sets the scalar at `path` to an integer; bit fields keep their neighbours."""
        if getattr(self, "_named", True):
            try:
                return self.call("field", path=path, value=value)
            except RpcError as e:
                if "unknown method" not in str(e):
                    raise
                self._named = False
        table = layout()
        ref = table.resolve(path, read=self.read)
        t = table.types[ref["type"]]
        if ref["bitsize"]:
            width = (ref["bitoffset"] + ref["bitsize"] + 7) // 8
            old = int.from_bytes(self.read(ref["address"], width), "little")
            mask = ((1 << ref["bitsize"]) - 1) << ref["bitoffset"]
            raw = (old & ~mask) | ((value << ref["bitoffset"]) & mask)
        else:
            width = 4 if t["kind"] == "P" else t["size"]
            raw = value & ((1 << 8 * width) - 1)
        return self.write(ref["address"], raw.to_bytes(width, "little"))

    def store_data(self, path, data, offset=0):
        """Writes `data` from the address of the field at `path` (`offset` bytes past it: see field); it may cover the
        members that follow, up to the end of the symbol the path starts from."""
        if getattr(self, "_named", True):
            try:
                return self.call("field", path=path, hex=bytes(data).hex(), **({"offset": offset} if offset else {}))
            except RpcError as e:
                if "unknown method" not in str(e):
                    raise
                self._named = False
        ref = layout().resolve(path, read=self.read)
        room = (ref["limit"] - offset) if ref["limit"] is not None else 1 << 30  # (behind a pointer: an allocation's extent is not known)
        if not 0 < len(data) <= room:
            raise RpcError(f"{path}: {len(data)} bytes go past the end of its object")
        return self.write(ref["address"] + offset, data)

    def info(self, path):
        """Where `path` is and what it is: address, size, kind, type, count and element for arrays, and the
        fields of its struct (name, offset, size, kind, type, bits)."""
        if getattr(self, "_named", True):
            try:
                r = self.call("symbol_info", name=path)
                self._named = True
                return r
            except RpcError as e:
                if "unknown method" not in str(e):
                    raise
                self._named = False
        table = layout()
        try:
            return table.info(table.resolve(path, read=self.read))
        except LookupError as e:
            raise RpcError(str(e))

    def table(self, path, count, *names):
        """`count` consecutive records at `path` (an array's or a pointer's first element, "vs_battle_menuItems[0]")
        as dicts of the named members' values, from one read: the record size and the members' offsets are the
        game's own (symbol_info), not the caller's. A member that is an array comes as bytes."""
        info = self.info(path)
        stride = info["size"]
        members = {f["name"]: f for f in info.get("fields", [])}
        missing = [n for n in names if n not in members]
        if missing:
            raise RpcError(f"{path} has no member {missing[0]}")
        raw = self.data(path, stride * count)
        out = []
        for i in range(count):
            row = {}
            for n in names:
                f = members[n]
                chunk = raw[i * stride + f["offset"]:i * stride + f["offset"] + f["size"]]
                if f.get("bits"):
                    word = int.from_bytes(chunk, "little") >> (f["bit_offset"] - f["offset"] * 8)
                    row[n] = word & ((1 << f["bits"]) - 1)
                elif f["kind"] in ("int", "uint", "bool", "pointer", "guest_pointer") and 0 < f["size"] <= 8:
                    row[n] = int.from_bytes(chunk, "little", signed=f["kind"] == "int")
                else:
                    row[n] = chunk
            out.append(row)
        return out

    def symbols_like(self, prefix):
        """{name: address} of the symbols that start with `prefix`."""
        if getattr(self, "_named", True):
            try:
                r = self.call("symbols", prefix=prefix)
                self._named = True
                return {s["name"]: s["address"] for s in r["symbols"]}
            except RpcError as e:
                if "unknown method" not in str(e):
                    raise
                self._named = False
        return {n: s["address"] for n, s in layout().syms.items() if n.startswith(prefix)}

    def heap_free(self):
        """The game heap's free total in heap units: heapA's circular free list of vs_main_HeapHeader (prev, next,
        blockSz), walked by name (a node's path is its predecessor's .next)."""
        head, node, total = self.field("heapA", length=0).address, "heapA", 0
        for _ in range(300):  # (a path is at most 2048 characters: the list is a few dozen blocks)
            following = self.value(node + ".next")
            if following in (head, 0):
                break
            node += ".next"
            total += self.value(node + ".blockSz")
        return total

    def wait_title(self, timeout=120):
        """Run until the title menu is ready (log marker), then pause.
        `timeout`: a hang guard in awake seconds."""
        deadline = Deadline(timeout)
        while "TITLE MENU READY" not in pathlib.Path(self.log).read_text():
            if deadline.passed():
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
        deadline = Deadline(timeout)  # a hang guard (awake seconds)
        while not run["done"]:
            if deadline.passed():
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
                result = {"hex": game.data(params["name"], params.get("length", 4)).hex()}
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
    launch.add_argument("--headless", choices=["egl", "xvfb", "null"], help="default: $ROOD_HEADLESS, else egl if it works")
    sub.add_parser("headless")
    reload = sub.add_parser("reload", help="reload engine (default) or owned Zig code at vsync")
    reload.add_argument("target", nargs="?", default="engine", choices=["engine", "zig"])
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
    elif args.command == "reload":
        print(json.dumps(Game(args.socket).reload(args.target)))
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
