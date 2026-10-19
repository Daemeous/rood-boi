#!/usr/bin/env python3
"""A Lua REPL into the running game (the REPL bridge: RPC lua {action "repl"},
pc/src/lua/lua_repl.cpp; pc/docs/Extensions.md "The REPL"). Dev mode only: the
game runs with ROOD_RPC (the socket this connects to) or ROOD_DEV=1.

    rood_repl.py                              interactive, in the console's sandbox
    rood_repl.py -p rood.hud                  in a plugin's sandbox (its globals; M: its module)
    rood_repl.py -e 'x = 1' -e 'x + 1'        one-shot: each form in turn, one session
    rood_repl.py -s work -e 'x'               a named session: its bindings stay between calls
    rood_repl.py --json -e 'rood.game.mode()' one JSON reply per form (for agents)
    rood_repl.py -e - < form.lua              a form from stdin
    rood_repl.py --describe 'rood.hud'        type, keys, a function's source and upvalues
    rood_repl.py --inspect claims             plugins, claims, exports, queries, globals,
                                              upvalues EXPR, complete TEXT
    rood_repl.py --sessions | --close S

A form is an expression (its values printed) or statements, as the Lua console
takes them. Interactive input that ends early (an open `function`, `do`, `{`)
asks for more lines (">>"). Names assigned in a session stay in it (a name the
sandbox already has is the sandbox's: redefining a plugin's global function is
live); `local` lasts one form. `_` is the last form's first value, `_e` the last
error; `repl` has describe, claims, claim(kind), exports, queries, plugins,
globals, upvalues(f), getupvalue/setupvalue(f, name[, v]), where(f), reload().
A reload of the plugin (a file saved, repl.reload()) starts the session over.

Interactive commands: :help  :plugin ID  :session NAME  :describe EXPR
:inspect WHAT [ARG]  :sessions  :reload  :json  :quit (or end of input).
Exit status (one-shot): 1 when a form failed, 2 when the game refused the request.
"""
import argparse
import json
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import rood_rpc  # noqa: E402

INSPECTIONS = ("plugins", "claims", "exports", "queries", "globals", "upvalues", "complete")


class Repl:
    def __init__(self, game, plugin=None, session=None, as_json=False, out=sys.stdout, err=sys.stderr):
        self.game, self.plugin, self.session, self.as_json = game, plugin, session, as_json
        self.out, self.err = out, err

    def request(self, op, **params):
        reply = self.game.repl(op, session=self.session, plugin=self.plugin, **params)
        if reply.get("session"):
            self.session = reply["session"]
        return reply

    def show(self, reply):
        """Prints a reply; returns whether it was ok."""
        if self.as_json:
            print(json.dumps(reply), file=self.out, flush=True)
            return reply.get("ok", True)
        if reply.get("reset"):
            print(f"-- {reply['reset']}", file=self.err)
        for line in reply.get("output", []):
            print(line, file=self.out)
        if "values" in reply and reply.get("ok"):
            for value in reply["values"]:
                print(value, file=self.out)
        elif "result" in reply and reply.get("ok"):
            print(json.dumps(reply["result"], indent=1), file=self.out)
        elif "sessions" in reply:
            for s in reply["sessions"]:
                print(f"{s['session']:<12} {s['plugin']:<20} load {s['generation']}"
                      f"{'' if s['current'] else ' (stale)'}  {s['evals']} evals, idle {s['idle_s']:.0f} s",
                      file=self.out)
        if not reply.get("ok", True):
            print(f"error: {reply.get('error', '')}", file=self.err)
            if reply.get("traceback"):
                print(reply["traceback"], file=self.err)
        self.out.flush()
        return reply.get("ok", True)

    def eval(self, code):
        return self.request("eval", code=code)

    def describe(self, expression):
        return self.request("describe", code=expression)

    def inspect(self, what, arg=None):
        return self.request("inspect", what=what, arg=arg)

    def complete(self, text):
        try:
            reply = self.inspect("complete", text)
        except rood_rpc.RpcError:
            return []
        return reply.get("result") or [] if reply.get("ok") else []


def command(repl, line):
    """A ':' command; False to quit."""
    name, _, rest = line[1:].strip().partition(" ")
    rest = rest.strip()
    if name in ("q", "quit", "exit"):
        return False
    if name in ("h", "help"):
        print(__doc__.split("Interactive commands:")[1].split("Exit status")[0].strip(), file=repl.out)
    elif name == "plugin":
        repl.plugin, repl.session = rest or None, None
        print(f"-- now in {repl.plugin or '@console'} (a new session)", file=repl.err)
    elif name == "session":
        repl.session = rest or None
    elif name == "describe":
        repl.show(repl.describe(rest))
    elif name == "inspect":
        what, _, arg = rest.partition(" ")
        repl.show(repl.inspect(what or "plugins", arg.strip() or None))
    elif name == "sessions":
        repl.show(repl.game.repl("sessions"))
    elif name == "reload":
        repl.show(repl.eval("repl.reload()"))
    elif name == "json":
        repl.as_json = not repl.as_json
    else:
        print(f"unknown command :{name} (:help)", file=repl.err)
    return True


def interactive(repl, stream=sys.stdin):
    tty = stream.isatty()
    if tty:
        try:
            import readline
            history = rood_rpc.OUT / "repl_history"
            try:
                readline.read_history_file(history)
            except OSError:
                pass
            matches = []

            def completer(text, state):
                if state == 0:
                    matches[:] = repl.complete(readline.get_line_buffer()[:readline.get_endidx()])
                    cut = len(readline.get_line_buffer()[:readline.get_endidx()]) - len(text)
                    matches[:] = [m[cut:] for m in matches]
                return matches[state] if state < len(matches) else None

            readline.set_completer(completer)
            readline.set_completer_delims(" \t\n()[]{},;=+-*/%^#<>~\"'")
            readline.parse_and_bind("tab: complete")
            import atexit
            atexit.register(lambda: readline.write_history_file(history))
        except ImportError:
            pass
    buffer = []
    while True:
        prompt = (">> " if buffer else f"{repl.plugin or '@console'}> ") if tty else ""
        try:
            line = input(prompt) if tty else stream.readline()
        except EOFError:
            line = ""
        except KeyboardInterrupt:
            print(file=repl.err)
            buffer = []
            continue
        if not tty and line == "":
            break
        if tty and line == "" and not buffer:
            continue
        line = line.rstrip("\n")
        if not buffer and line.startswith(":"):
            if not command(repl, line):
                break
            continue
        buffer.append(line)
        try:
            reply = repl.eval("\n".join(buffer))
        except rood_rpc.RpcError as e:
            print(f"refused: {e}", file=repl.err)
            buffer = []
            continue
        if reply.get("incomplete"):
            continue
        buffer = []
        repl.show(reply)
    if buffer:  # the input ended inside a form: its error
        repl.show(repl.eval("\n".join(buffer)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--socket", default=rood_rpc.DEFAULT_SOCKET, help="default: $ROOD_RPC")
    parser.add_argument("-p", "--plugin", help="the sandbox: a plugin's id, or @console (default) / @NAME")
    parser.add_argument("-s", "--session", help="a session name (made on first use; kept between calls)")
    parser.add_argument("-e", "--eval", action="append", metavar="CODE", help="a form ('-': stdin); repeatable")
    parser.add_argument("--describe", metavar="EXPR")
    parser.add_argument("--inspect", metavar="WHAT", help=", ".join(INSPECTIONS))
    parser.add_argument("--arg", help="--inspect upvalues: an expression; complete: the text")
    parser.add_argument("--sessions", action="store_true")
    parser.add_argument("--close", metavar="SESSION")
    parser.add_argument("--json", action="store_true", help="one JSON reply per line")
    args = parser.parse_args()
    try:
        game = rood_rpc.Game(args.socket)
    except OSError as e:
        print(f"rood_repl: cannot connect to {args.socket}: {e} (is the game running with ROOD_RPC?)",
              file=sys.stderr)
        return 2
    repl = Repl(game, plugin=args.plugin, session=args.session, as_json=args.json)
    try:
        if args.sessions or args.close:
            reply = game.repl("close", session=args.close) if args.close else game.repl("sessions")
            repl.show(reply)
            return 0
        if args.eval or args.describe or args.inspect:
            ok = True
            for code in args.eval or []:
                ok &= repl.show(repl.eval(sys.stdin.read() if code == "-" else code))
            if args.describe:
                ok &= repl.show(repl.describe(args.describe))
            if args.inspect:
                ok &= repl.show(repl.inspect(args.inspect, args.arg))
            if not args.json and not args.session and repl.session:
                print(f"-- session {repl.session} (-s {repl.session} to go on)", file=sys.stderr)
            return 0 if ok else 1
        interactive(repl)
        return 0
    except rood_rpc.RpcError as e:
        print(f"rood_repl: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
