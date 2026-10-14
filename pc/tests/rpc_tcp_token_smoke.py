"""Developer RPC over TCP with a token (ROOD_RPC=tcp:PORT:TOKEN, pc/src/dev_rpc.c):
rood_rpc.py authenticates and works; a connection whose first request is not
auth {token} with the right token gets an error and is closed; an HTTP request
line is closed without a reply; an over-long line without a newline from an
unauthenticated client is closed; the game's log never shows the token."""
import json
import pathlib
import secrets
import socket
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game  # noqa: E402


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def exchange(port, lines, timeout=10):
    """Send `lines` on a fresh connection; the reply lines until the game closes it (or the timeout)."""
    sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    data = b""
    closed = False
    try:
        for line in lines:
            sock.sendall(line if isinstance(line, bytes) else (line + "\n").encode())
    except (BrokenPipeError, ConnectionResetError):
        sock.close()
        return [], True
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                closed = True
                break
            data += chunk
            if data.count(b"\n") >= len(lines) and len(lines) > 1:
                break
    except socket.timeout:
        pass
    except ConnectionResetError:
        closed = True
    sock.close()
    return [json.loads(x) for x in data.splitlines() if x.strip()], closed


def request(method, **params):
    return json.dumps({"jsonrpc": "2.0", "id": 7, "method": method, "params": params})


token = secrets.token_hex(16)
port = free_port()
log = OUT / "rpc-tcp-token.log"
game = Game.launch(fast=True, socket_path=f"tcp:{port}:{token}", log=log)
try:
    game.wait_title()
    assert game.status()["paused"], "rood_rpc.py authenticated and runs requests"

    replies, closed = exchange(port, [request("status")])
    assert closed and len(replies) == 1 and replies[0]["error"]["code"] == -32001, (replies, closed)

    replies, closed = exchange(port, [request("auth", token="wrong" + token)])
    assert closed and replies and "error" in replies[0], (replies, closed)

    replies, closed = exchange(port, [request("auth", token=token[:-1])])
    assert closed and replies and "error" in replies[0], ("a prefix of the token", replies, closed)

    replies, closed = exchange(port, ["GET / HTTP/1.1"])
    assert closed and not replies, ("an HTTP request line", replies, closed)

    replies, closed = exchange(port, [b"{" + b" " * 300000], timeout=10)
    assert closed, "an unauthenticated over-long line closes the connection"

    replies, closed = exchange(port, [request("auth", token=token), request("status")])
    assert len(replies) == 2 and replies[0].get("result") == {"ok": True}, replies
    assert "vsync" in replies[1]["result"], replies

    assert game.status()["paused"], "the first client still works"
finally:
    game.close()
text = pathlib.Path(log).read_text(errors="replace")
assert "token required" in text, "the log says a token is required"
assert token not in text, "the token is not logged"
print("PASS: TCP RPC with a token: rood_rpc.py authenticates; missing, wrong and partial tokens, HTTP lines and "
      "over-long unauthenticated lines are refused and closed; the token is never logged")
