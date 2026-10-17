"""Developer RPC: launch headless, reach the title, start New Game with injected
input and check status, stepping, state, memory read/write, screenshot, errors."""
import json
import os
import pathlib
import secrets
import socket
import stat
import sys
import tempfile
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game, RpcError, symbols  # noqa: E402

game = Game.launch(fast=True)
try:
    game.wait_title()
    before = game.status()
    assert before["paused"] and before["battle_frame"] == 0, before
    after = game.run(30)
    assert after["vsync"] == before["vsync"] + 30, (before, after)
    game.press("start")
    game.run(600)
    status = game.status()
    assert status["battle_frame"] > 0, status
    state = game.state()
    assert state["zone"] == 1 and state["start_state"] == 0, state
    address = symbols()["D_80061068"]
    assert game.read(address, 1)[0] == state["zone"]
    scratch = 0x1F8003F0
    original = game.read(scratch, 4)
    game.write(scratch, b"\x12\x34\x56\x78")
    assert game.read(scratch, 4) == b"\x12\x34\x56\x78"
    game.write(scratch, original)
    shot = game.screenshot(OUT / "rpc-smoke.ppm")
    assert (shot["width"], shot["height"]) == (320, 224), shot
    graphics = game.call("graphics")
    if graphics["hardware"]:
        assert graphics["scale"] == 4, graphics
        assert game.call("graphics", scale=2, filter=1, aspect=2)["scale"] == 2
        game.run(4)
        dump = OUT / "rpc-gpu.ppm"
        game.call("gpu_dump", path=str(dump))
        assert dump.read_bytes().startswith(b"P6\n2048 1024\n"), dump
        game.call("graphics", scale=4, filter=0, aspect=0)
    for method, params in [("nope", {}), ("read", {"address": 0x1000, "length": 4}),
                           ("write", {"address": symbols()["main"], "hex": "00"}),
                           ("input", {"buttons": ["turbo"]})]:
        try:
            game.call(method, **params)
            raise AssertionError(f"{method} should fail")
        except RpcError:
            pass
    print("PASS: RPC title, exact stepping, input, state, read/write, screenshot, graphics/GPU dump and error handling")
finally:
    game.close()


def tcp_security(directory, explicit=None, print_token=False):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    address = f"tcp:{port}" + (f":{explicit}" if explicit else "")
    token_file = directory / "rpc.token"
    log = directory / "game.log"
    env = {"ROOD_RPC_TOKEN_FILE": str(token_file),
           "ROOD_RPC_PRINT_TOKEN": "1" if print_token else "0"}
    # Normally Game.launch supplies its own token; bypass only that convenience
    # to exercise the server's generation path, and the client's file lookup.
    with mock.patch.dict(os.environ, env), mock.patch("rood_rpc.with_token", side_effect=lambda p: p):
        game = Game.launch(fast=True, socket_path=address, log=log)
        try:
            token = explicit or token_file.read_text().strip()
            if not explicit:
                assert len(token) == 64 and all(c in "0123456789abcdef" for c in token), token
                if os.name != "nt":
                    assert stat.S_IMODE(token_file.stat().st_mode) == 0o600
            else:
                assert not token_file.exists(), "explicit tokens need no token file"
            assert "vsync" in game.status(), "authenticated client can run RPC"
            requests = [("status", {}), ("lua", {"action": "eval", "code": "return 1"}),
                        ("lua", {"action": "grant", "plugin": "rpc-test", "cap": "fs.write"}),
                        ("auth", {"token": "wrong"}), ("auth", {"token": token[:-1]}),
                        ("auth", {"token": token + "extra"})]
            for method, params in requests:
                with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
                    request = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
                    sock.sendall((json.dumps(request) + "\n").encode())
                    with sock.makefile("rb") as stream:
                        response = json.loads(stream.readline())
                        assert response["error"]["code"] == -32001, response
                        assert stream.read(1) == b"", "unauthenticated connection must close"
            # Authentication belongs to each connection, not the listener.
            second = Game(f"tcp:{port}:{token}")
            try:
                assert "vsync" in second.status()
            finally:
                second.sock.close()  # Game.close would quit the shared process
            assert "vsync" in game.status()
        finally:
            game.close()
    text = log.read_text(errors="replace")
    assert "token required" in text
    assert (token in text) == print_token, "secret logging requires explicit opt-in"
    return token


with tempfile.TemporaryDirectory(prefix="rpc-security-", dir=OUT) as temporary:
    directory = pathlib.Path(temporary)
    first = tcp_security(directory)
    if os.name == "nt":
        (directory / "rpc.token").unlink()  # Windows refuses to replace existing files
    second = tcp_security(directory, print_token=True)
    assert first != second, "each startup must generate a fresh secret"
    (directory / "rpc.token").unlink()
    tcp_security(directory, explicit=secrets.token_hex(64))
print("PASS: TCP generated/private/fresh tokens, opt-in printing, unauthenticated Lua eval/grant rejection, "
      "per-connection authentication and explicit tokens")
