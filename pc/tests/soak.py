#!/usr/bin/env python3
"""Soak test: play the native port for a long time with scripted and seeded
random input and report what goes wrong.

Starts New Game (or Continue from a copy of --card; the card itself is never
written), advances the opening, then plays: walking in every direction,
opening and closing the menu (Triangle), battle mode and attacks, crate
pick-up/drop, dialogue, camera turns and long walks that change rooms.

Ashley's death is not a finding: the Game Over screen is recorded as an
event (events.log, summary "game_overs") and play continues with Continue
(the prologue restarts its fight; later, the file menu loads the card's first
save) or, when that leads to the title, New Game / Continue from the card.

Play starts at a fixed vsync (--start-vsync), so a seed replays the same input
on the same frames: rerun with --stop-on-finding to reach a finding again.

Findings, each with the game log, a resolved stack and a presented frame:
  crash          the game exited ([crash] signal or other exit status)
  unimplemented  a pc_unimplemented diagnostic ([UNIMPLEMENTED])
  stall          a step did not finish (the game thread is stuck), or vsyncs
                 advance while the battle loop does not
  soft-freeze    the battle loop advances but Ashley stops responding to
                 input, through menu/dialogue recovery presses (story
                 cutscenes, which change rooms, extend the recovery budget)

After a finding the game is relaunched (unless --stop-on-finding) and the run
continues until --frames vsyncs of play have elapsed.

    python3 pc/tests/soak.py --frames 20000 --seed 1
    python3 pc/tests/soak.py --card pc/save/card1.mcd --frames 50000

Exit status: 0 with no findings, 1 otherwise. Output: --out (pc/build/soak).
"""
import argparse
import ctypes
import json
import os
import pathlib
import random
import re
import shutil
import signal
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import BINARY, OUT, Game, RpcError  # noqa: E402

DIRECTIONS = ["up", "down", "left", "right"]
DIAGONALS = [["up", "left"], ["up", "right"], ["down", "left"], ["down", "right"]]


class BackAtTitle(Exception):
    pass


class GameOver(Exception):
    pass


class Finding(Exception):
    def __init__(self, kind, detail):
        super().__init__(f"{kind}: {detail}")
        self.kind, self.detail = kind, detail


def resolve_stacks(text):
    """[stack] lines -> function (file:line) chains, innermost first."""
    chains = []
    for line in re.findall(r"\[stack\]([^\n]*)", text):
        addrs = line.split()
        if not addrs:
            continue
        out = subprocess.run(["addr2line", "-f", "-s", "-e", str(BINARY), *addrs],
                             capture_output=True, text=True).stdout.splitlines()
        chains.append(" <- ".join(f"{f} ({loc})" for f, loc in zip(out[0::2], out[1::2])))
    return chains


def tgkill(pid, sig):
    """Signal the game's main thread (its game loop), not any thread."""
    libc = ctypes.CDLL(None, use_errno=True)
    libc.syscall(234 if ctypes.sizeof(ctypes.c_void_p) == 8 else 270, pid, pid, sig)


class Soak:
    def __init__(self, args):
        self.args = args
        self.rng = random.Random(args.seed)
        self.out = pathlib.Path(args.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.findings = []
        self.played = 0  # vsyncs of play (after the game became controllable)
        self.launches = 0
        self.game = None
        self.events = []
        self.rooms = set()
        self.visited = {}
        self.heading = {}
        self.game_overs = []

    # ---- process ----
    def launch(self):
        self.launches += 1
        run = self.out / f"run{self.launches:02d}"
        if run.exists():
            shutil.rmtree(run)
        run.mkdir()
        self.run_dir = run
        card = run / "card1.mcd"
        if self.args.card:
            shutil.copyfile(self.args.card, card)  # never write the provided save
        env = {"ROOD_SCALE": str(self.args.scale), "ROOD_GI": "0"}
        self.log = run / "game.log"
        sock = f"/tmp/rood-soak-{os.getpid()}.sock"  # sun_path is 108 bytes
        # Headless (EGL on a GPU, else a private Xvfb: rood_rpc.headless_mode)
        # and unpaced: play is driven by vsync-exact RPC steps.
        self.game = Game.launch(window=self.args.window, card=str(card), socket_path=sock,
                                log=str(self.log), extra_env=env, fast=not self.args.window)
        self.game.sock.settimeout(self.args.rpc_timeout)
        self.pid = self.game.processes[0].pid
        self.note(f"launch {self.launches} (pid {self.pid})")
        self.game.wait_title(timeout=90)
        # The title becomes ready on the same vsync every launch, but the pause
        # lands a little later (wall clock): start play on a fixed vsync so the
        # seed's input hits the same frames and a run replays exactly.
        v = self.game.status()["vsync"]
        target = self.args.start_vsync if v < self.args.start_vsync else (v + 7) // 8 * 8
        if target > v:
            self.game.run(target - v)
        self.last_moved = None
        self.last_battle = self.game.status()["battle_frame"]
        self.last_battle_vsync = self.game.status()["vsync"]
        self.title_count = self.log.read_text().count("TITLE MENU READY")

    def close(self):
        if self.game:
            try:
                self.game.close()
            except Exception:
                pass
            for p in self.game.processes:
                if p.poll() is None:
                    p.kill()
            self.game = None

    def note(self, text):
        line = f"[soak] {text}"
        self.events.append(line)
        print(line, flush=True)

    # ---- RPC wrappers that turn failures into findings ----
    def call(self, method, **params):
        try:
            return self.game.call(method, **params)
        except (socket.timeout, TimeoutError):
            raise Finding("stall", f"RPC {method} unanswered for {self.args.rpc_timeout}s")
        except (RpcError, OSError, json.JSONDecodeError) as e:
            raise self.exited(f"RPC {method}: {e}")

    def exited(self, why):
        p = self.game.processes[0]
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            return Finding("stall", why)
        text = self.log.read_text(errors="replace")
        m = re.search(r"\[UNIMPLEMENTED\] ([^\n]*)", text)
        if m:
            return Finding("unimplemented", m.group(1))
        m = re.search(r"\[crash\] ([^\n]*)", text)
        if m:
            return Finding("crash", m.group(1))
        return Finding("crash", f"game exited with status {p.returncode} ({why})")

    def step(self, vsyncs):
        """Run exactly `vsyncs`, checking the process and the battle loop."""
        self.call("step", vsyncs=vsyncs)
        limit = max(self.args.rpc_timeout, vsyncs / 10)
        deadline = time.monotonic() + limit
        while True:
            s = self.call("status")
            if s["paused"] and s["step_budget"] == 0:
                break
            if time.monotonic() > deadline:
                raise Finding("stall", f"{vsyncs}-vsync step unfinished after {limit:.0f}s "
                              f"(vsync {s['vsync']}, step budget {s['step_budget']})")
            time.sleep(0.01)
        if s["battle_frame"] != self.last_battle:
            self.last_battle, self.last_battle_vsync = s["battle_frame"], s["vsync"]
        elif s["vsync"] - self.last_battle_vsync > self.args.stall_vsyncs:
            titles = self.log.read_text(errors="replace").count("TITLE MENU READY")
            if titles > self.title_count:  # Game Over or quit to title: start again
                self.title_count = titles
                self.last_battle_vsync = s["vsync"]
                raise BackAtTitle()
            else:
                raise Finding("stall", f"battle loop stopped at frame {s['battle_frame']} for "
                              f"{s['vsync'] - self.last_battle_vsync} vsyncs")
        return s

    def check_screens(self, game_over_ok=False):
        """Before every input: raise BackAtTitle when the title menu came up
        again, and GameOver while the Game Over screen is up, so random
        presses never pick a title or Game Over option."""
        titles = self.log.read_text(errors="replace").count("TITLE MENU READY")
        if titles > self.title_count:
            self.title_count = titles
            raise BackAtTitle()
        if not game_over_ok and self.call("state").get("game_over"):
            raise GameOver()

    def press(self, buttons, vsyncs=3, settle=6, game_over_ok=False):
        self.check_screens(game_over_ok)
        self.call("input", buttons=buttons if isinstance(buttons, list) else [buttons],
                  vsyncs=vsyncs)
        return self.step(vsyncs + settle)

    def hold(self, buttons, vsyncs):
        self.check_screens()
        self.call("input", buttons=buttons if isinstance(buttons, list) else [buttons],
                  vsyncs=vsyncs)
        return self.step(vsyncs)

    def state(self):
        st = self.call("state")
        room = (st["zone"], st["room"])
        if room not in self.rooms:
            self.rooms.add(room)
            self.note(f"room zone {room[0]} room {room[1]} (vsync {self.call('status')['vsync']})")
        return st

    @staticmethod
    def pos(st):
        p = st.get("player")
        return None if p is None else (p["x"], p["y"], p["z"], p["facing"])

    # ---- reaching gameplay ----
    def title_item(self):
        """The title cursor (0 New Game, 1 Continue, 2/3 settings): the game
        logs it when the menu is ready (Continue when a card holds a save) and
        on every move ("[title] selected menu item N")."""
        text = self.log.read_text(errors="replace")
        moves = re.findall(r"\[title\] selected menu item (\d)", text[text.rfind("TITLE MENU READY"):])
        return int(moves[-1]) if moves else 0

    def new_game(self):
        """From the title: Continue from the card's first save (--card), else
        New Game."""
        want = 1 if self.args.card else 0
        for _ in range((self.title_item() - want) % 4):  # up: one item back (wraps)
            self.press("up", 3, 20)
        if self.args.card:
            # Continue: Load, slot 1, the first file.
            self.press("start", 4, 90)
            for _ in range(3):
                self.press("circle", 3, 90)
            self.step(200)
        else:
            self.press("start", 4, 60)

    def controllable(self, budget):
        """Advance dialogue/cutscenes until a walk moves Ashley. Returns True
        when control was reached within `budget` vsyncs."""
        start = self.call("status")["vsync"]
        while self.call("status")["vsync"] - start < budget:
            if self.responds() and self.responds():
                return True
            self.press("circle", 2, 100)
        return False

    def responds(self):
        """Does Ashley answer a walk? She must hold still while idle, take a
        walking-sized step (or turn) while a direction is held, and stop again
        when it is released: scripted cutscene motion and teleports fail."""
        for d in self.rng.sample(DIRECTIONS, 4):
            self.step(10)
            a = self.pos(self.state())
            self.step(10)
            b = self.pos(self.state())
            self.hold(d, 14)
            c = self.pos(self.state())
            self.step(12)
            e = self.pos(self.state())
            self.step(10)
            f = self.pos(self.state())
            if None in (a, b, c, e, f) or a != b or e != f:
                continue  # moving on her own (cutscene, knockback): try again
            if c != b and abs(c[0] - b[0]) + abs(c[2] - b[2]) < 300:
                self.last_moved = self.call("status")["vsync"]
                return True
        return False

    # ---- play ----
    def action(self):
        r = self.rng.random()
        if r < 0.40:  # walk, sometimes diagonally, sometimes far (room changes)
            far = self.rng.random() < 0.3
            d = self.explore_direction() if far else self.rng.choice(DIRECTIONS + DIAGONALS)
            n = self.rng.randint(120, 360) if far else self.rng.randint(8, 70)
            def walk():
                if far:  # doors only open outside battle mode: Cross leaves it
                    self.press("cross", 2, 40)
                self.walk(d, n)
            return f"walk {d}", walk
        if r < 0.52:  # menu open, browse, close
            def menu():
                self.press("triangle", 3, self.rng.randint(10, 40))
                for _ in range(self.rng.randint(0, 6)):
                    self.press(self.rng.choice(DIRECTIONS + ["circle", "cross", "l1", "r1"]),
                               3, self.rng.randint(4, 20))
                for _ in range(self.rng.randint(1, 4)):
                    self.press(self.rng.choice(["triangle", "cross"]), 3, self.rng.randint(8, 30))
            return "menu", menu
        if r < 0.64:  # battle mode, attack, leave battle mode
            def attack():
                self.press("circle", 3, 12)
                for _ in range(self.rng.randint(0, 3)):
                    self.press("circle", 3, self.rng.randint(6, 40))
                if self.rng.random() < 0.7:
                    self.press("cross", 3, 20)
            return "attack", attack
        if r < 0.74:  # pick up / drop / throw a crate, or jump
            def crate():
                self.press(self.rng.choice(["circle", "square"]), 3, 20)
                self.hold(self.rng.choice(DIRECTIONS), self.rng.randint(10, 60))
                self.press(self.rng.choice(["circle", "square", "cross"]), 3, 30)
            return "crate", crate
        if r < 0.84:  # dialogue / confirm prompts
            n = self.rng.randint(1, 6)
            return "advance", lambda: [self.press("circle", 2, self.rng.randint(8, 30)) for _ in range(n)]
        if r < 0.92:  # camera and shoulder buttons
            b = self.rng.choice(["l1", "r1", "l2", "r2", ["l1", "r1"]])
            return f"camera {b}", lambda: self.hold(b, self.rng.randint(4, 60))
        if r < 0.97:  # idle
            n = self.rng.randint(10, 90)
            return "idle", lambda: self.step(n)
        b = self.rng.choice(["select", "start", "square", "cross"])  # anything else
        return f"press {b}", lambda: self.press(b, 3, self.rng.randint(10, 60))

    def walk(self, d, vsyncs):
        before = self.pos(self.state())
        self.hold(d, vsyncs)
        st = self.state()
        after = self.pos(st)
        if before and after:
            room = (st["zone"], st["room"])
            self.visit(room, after)
            dx, dz = after[0] - before[0], after[2] - before[2]
            if abs(dx) + abs(dz) > 64:  # camera-relative input -> world motion
                self.heading[str(d)] = (dx / (abs(dx) + abs(dz)), dz / (abs(dx) + abs(dz)))

    def visit(self, room, p):
        cell = (room, p[0] // 256, p[2] // 256)
        self.visited[cell] = self.visited.get(cell, 0) + 1

    def explore_direction(self):
        """Prefer the input whose last observed motion leads to the least
        visited ground of this room (novelty search: reaches exits)."""
        st = self.state()
        p = self.pos(st)
        options = DIRECTIONS + DIAGONALS
        if p is None or self.rng.random() < 0.25:
            return self.rng.choice(options)
        room = (st["zone"], st["room"])
        def score(d):
            h = self.heading.get(str(d))
            if h is None:
                return -1  # unknown: try it
            cell = (room, int(p[0] + h[0] * 768) // 256, int(p[2] + h[1] * 768) // 256)
            return self.visited.get(cell, 0) + self.rng.random()
        return min(options, key=score)

    def keep_frame(self):
        """The latest presented frame, kept for findings where the game has
        already exited."""
        try:
            self.game.present_screenshot(self.run_dir / "last-frame.png", timeout=5)
        except Exception:
            pass

    def recover(self):
        """Ashley stopped responding: try what a player would (close menus,
        dismiss prompts, advance dialogue) for --freeze-vsyncs. A room change
        means the story is moving on (the end of the prologue plays the credits
        in zone 4, then zones 8 and 18, for several thousand vsyncs before
        Ashley walks in zone 9), so after one the budget is --cutscene-vsyncs."""
        first = self.call("status")["vsync"]
        st = self.state()
        room = (st["zone"], st["room"])
        budget = self.args.freeze_vsyncs
        moves = [["cross"], ["triangle"], ["circle"], ["circle"], ["start"], ["cross"]]
        while self.call("status")["vsync"] - first < budget:
            self.press(self.rng.choice(moves), 3, 30)
            if self.responds():
                return True
            st = self.state()
            if (st["zone"], st["room"]) != room:
                room = (st["zone"], st["room"])
                budget = max(budget, self.args.cutscene_vsyncs)
        return False

    def start(self):
        self.new_game()
        if not self.controllable(self.args.opening_vsyncs):
            raise Finding("soft-freeze", f"Ashley never became controllable within "
                          f"{self.args.opening_vsyncs} vsyncs of the start")
        self.note(f"controllable at vsync {self.call('status')['vsync']}")

    def play(self):
        started, game_over = False, False
        while self.played < self.args.frames:
            try:
                if game_over:
                    game_over = False
                    self.game_over()
                elif not started:
                    self.start()
                    started = True
                self.play_until_title()
            except BackAtTitle:
                self.note("back at the title screen; starting again")
                started = False
            except GameOver:
                game_over = started = True

    def game_over(self):
        """Ashley died: record it (an event, not a finding) and take Continue,
        the screen's default option."""
        st = self.call("state")
        s = self.call("status")
        self.game_overs.append({"vsync": s["vsync"], "played_vsyncs": self.played, "zone": st["zone"],
                                "room": st["room"], "intro_state": st.get("intro_state"),
                                "recent_actions": getattr(self, "recent", [])[-6:]})
        self.note(f"game over at vsync {s['vsync']} (zone {st['zone']} room {st['room']}); continuing")
        self.step(90)  # the menu fades in before it takes input
        # Continue: the prologue reloads its fight; later the file menu opens
        # on the card: Circle takes the slot and first file, and Cross backs
        # out of an empty card to the title (BackAtTitle).
        v0 = self.call("status")["vsync"]
        presses = 0
        while self.call("state").get("game_over"):
            if self.call("status")["vsync"] - v0 > self.args.freeze_vsyncs:
                raise Finding("soft-freeze", f"the Game Over screen did not close within "
                              f"{self.args.freeze_vsyncs} vsyncs of Continue input")
            self.press("circle" if presses < 4 else "cross", 3, 90, game_over_ok=True)
            presses += 1
        if not self.controllable(self.args.opening_vsyncs):
            raise Finding("soft-freeze", f"Ashley never became controllable within "
                          f"{self.args.opening_vsyncs} vsyncs of Continue after a game over")
        self.note(f"controllable again at vsync {self.call('status')['vsync']}")

    def play_until_title(self):
        since_check = 0
        while self.played < self.args.frames:
            name, act = self.action()
            v0 = self.call("status")["vsync"]
            act()
            v1 = self.call("status")["vsync"]
            self.played += v1 - v0
            since_check += v1 - v0
            self.recent = (self.recent + [name])[-20:]
            if since_check >= self.args.check_vsyncs:
                since_check = 0
                self.keep_frame()
                st = self.state()
                if not self.responds():
                    self.note(f"no response at {name!r} (zone {st['zone']} room {st['room']}); recovering")
                    if not self.recover():
                        raise Finding("soft-freeze", f"Ashley did not respond for "
                                      f"{self.args.freeze_vsyncs} vsyncs of recovery input "
                                      f"(zone {st['zone']} room {st['room']}, last actions {self.recent[-6:]})")
                self.note(f"played {self.played}/{self.args.frames} vsyncs, zone {st['zone']} "
                              f"room {st['room']} hp {st['hp']}/{st['max_hp']}")

    # ---- findings ----
    def record(self, finding):
        index = len(self.findings) + 1
        where = self.out / f"finding{index:02d}-{finding.kind}"
        if where.exists():
            shutil.rmtree(where)
        where.mkdir()
        alive = self.game and self.game.processes[0].poll() is None
        frame = None
        if not alive and (self.run_dir / "last-frame.png").exists():
            shutil.copyfile(self.run_dir / "last-frame.png", where / "frame.png")
            frame = "last kept"
        if alive:
            if finding.kind in ("stall", "soft-freeze"):
                tgkill(self.pid, signal.SIGUSR1)  # [stack] of the game thread
                time.sleep(0.5)
            try:
                frame = self.game.present_screenshot(where / "frame.png", timeout=5)
            except Exception:
                try:
                    frame = self.game.window_screenshot(str(where / "frame.png"))
                except Exception:
                    frame = None
        state = None
        if alive:
            try:
                state = self.game.call("state")
            except Exception:
                pass
        self.close()
        shutil.copyfile(self.log, where / "game.log")
        text = self.log.read_text(errors="replace")
        report = {
            "kind": finding.kind,
            "detail": finding.detail,
            "seed": self.args.seed,
            "launch": self.launches,
            "played_vsyncs": self.played,
            "recent_actions": getattr(self, "recent", []),
            "state": state,
            "frame": str(where / "frame.png") if frame else None,
            "stacks": resolve_stacks(text),
            "log_tail": text.splitlines()[-25:],
        }
        (where / "finding.json").write_text(json.dumps(report, indent=2))
        self.findings.append(report)
        self.note(f"FINDING {finding.kind}: {finding.detail} -> {where}")
        for chain in report["stacks"][-1:]:
            self.note(f"  stack: {chain[:600]}")

    def run(self):
        while self.played < self.args.frames:
            self.recent = []
            try:
                self.launch()
                self.play()
            except Finding as f:
                self.record(f)
                if self.args.stop_on_finding or self.launches >= self.args.max_launches:
                    break
                self.rng.seed(f"{self.args.seed}-{self.launches}")
                continue
            finally:
                if self.game and self.played >= self.args.frames:
                    self.close()
        self.close()
        summary = {"seed": self.args.seed, "played_vsyncs": self.played,
                   "launches": self.launches, "rooms": sorted(self.rooms),
                   "game_overs": self.game_overs,
                   "findings": [{k: f[k] for k in ("kind", "detail", "frame")} for f in self.findings]}
        (self.out / "summary.json").write_text(json.dumps(summary, indent=2))
        (self.out / "events.log").write_text("\n".join(self.events) + "\n")
        print(json.dumps(summary, indent=2))
        return 1 if self.findings else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--frames", type=int, default=20000, help="vsyncs of play (60 per second)")
    p.add_argument("--seed", default="1")
    p.add_argument("--card", help="memory card to Continue from (copied, never written)")
    p.add_argument("--out", default=str(OUT / "soak" if os.environ.get("ROOD_OUT") else ROOT / "pc/build/soak"))
    p.add_argument("--scale", type=int, default=1, help="ROOD_SCALE (1 keeps software GL fast)")
    p.add_argument("--window", action="store_true", help="on the current display, not headless")
    p.add_argument("--opening-vsyncs", type=int, default=30000,
                   help="budget for advancing the opening until Ashley moves")
    p.add_argument("--check-vsyncs", type=int, default=600, help="vsyncs between response checks")
    p.add_argument("--freeze-vsyncs", type=int, default=3600,
                   help="recovery input before a soft freeze is reported")
    p.add_argument("--cutscene-vsyncs", type=int, default=30000,
                   help="recovery input in all while the room keeps changing (story cutscenes)")
    p.add_argument("--start-vsync", type=int, default=1024,
                   help="vsync to start New Game/Continue on (fixed: a seed replays exactly)")
    p.add_argument("--stall-vsyncs", type=int, default=1800,
                   help="vsyncs without a battle-loop frame before a stall is reported")
    p.add_argument("--rpc-timeout", type=float, default=60)
    p.add_argument("--max-launches", type=int, default=8)
    p.add_argument("--stop-on-finding", action="store_true")
    args = p.parse_args()
    sys.exit(Soak(args).run())


if __name__ == "__main__":
    main()
