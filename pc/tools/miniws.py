#!/usr/bin/env python3
"""A minimal WebSocket (RFC 6455) over a blocking socket, standard library only: the server side of one connection (handshake,
masked client frames, fragmented messages, ping/pong, close) for paint_roundtrip.py live, and a small client (the tests', and a way
to try the bridge by hand)."""
import base64
import hashlib
import os
import socket
import struct

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CONT, TEXT, BINARY, CLOSE, PING, PONG = 0, 1, 2, 8, 9, 10
MAX_MESSAGE = 64 << 20


class Closed(Exception):
    pass


def accept_key(key):
    return base64.b64encode(hashlib.sha1(key.strip().encode() + GUID).digest()).decode()


def frame(opcode, payload, mask=False, fin=True):
    """One frame's bytes (a client's are masked)."""
    head = bytes([(0x80 if fin else 0) | opcode])
    n = len(payload)
    flag = 0x80 if mask else 0
    head += bytes([flag | n]) if n < 126 else bytes([flag | 126]) + struct.pack(">H", n) if n < 65536 else bytes([flag | 127]) + struct.pack(">Q", n)
    if mask:
        key = os.urandom(4)
        payload = _xor(payload, key)
        head += key
    return head + payload


def _xor(payload, key):
    n = len(payload)
    big = int.from_bytes(payload, "little") ^ int.from_bytes((key * (n // 4 + 1))[:n], "little")
    return big.to_bytes(n, "little")


class WebSocket:
    def __init__(self, sock, client=False):
        self.sock, self.client, self.buffer, self.closed = sock, client, b"", False

    def _read(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(max(65536, n - len(self.buffer)))
            if not chunk:
                raise Closed()
            self.buffer += chunk
        out, self.buffer = self.buffer[:n], self.buffer[n:]
        return out

    def _head(self):
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk or len(self.buffer) > 65536:
                raise Closed()
            self.buffer += chunk
        head, _, self.buffer = self.buffer.partition(b"\r\n\r\n")
        return head.decode("latin-1")

    def accept(self):
        """The server's handshake: reads the HTTP upgrade request, answers it; returns the request path. Anything else is a 400."""
        request = self._head()
        lines = request.split("\r\n")
        headers = {k.strip().lower(): v.strip() for k, _, v in (line.partition(":") for line in lines[1:])}
        parts = lines[0].split(" ")
        if (len(parts) < 3 or parts[0] != "GET" or "websocket" not in headers.get("upgrade", "").lower()
                or "upgrade" not in headers.get("connection", "").lower() or "sec-websocket-key" not in headers
                or headers.get("sec-websocket-version") != "13"):
            self.sock.sendall(b"HTTP/1.1 400 Bad Request\r\nSec-WebSocket-Version: 13\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            raise Closed()
        self.sock.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           f"Sec-WebSocket-Accept: {accept_key(headers['sec-websocket-key'])}\r\n\r\n").encode())
        return parts[1]

    def connect(self, host, path="/"):
        """The client's handshake."""
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        reply = self._head()
        if not reply.startswith("HTTP/1.1 101") or accept_key(key).lower() not in reply.lower():
            raise Closed(f"handshake refused: {reply.splitlines()[0]}")

    def _frame(self):
        b0, b1 = self._read(2)
        opcode, fin, masked, n = b0 & 15, bool(b0 & 0x80), bool(b1 & 0x80), b1 & 127
        if b0 & 0x70:
            raise Closed("reserved bits set")
        if n == 126:
            (n,) = struct.unpack(">H", self._read(2))
        elif n == 127:
            (n,) = struct.unpack(">Q", self._read(8))
        if n > MAX_MESSAGE:
            raise Closed("message too big")
        key = self._read(4) if masked else None
        payload = self._read(n)
        if key:
            payload = _xor(payload, key)
        return opcode, fin, payload

    def recv(self):
        """The next message as (opcode TEXT or BINARY, payload), fragments joined; None once the peer closed. Pings are answered."""
        message, kind = b"", None
        try:
            while True:
                opcode, fin, payload = self._frame()
                if opcode == PING:
                    self._send(PONG, payload)
                elif opcode == PONG:
                    pass
                elif opcode == CLOSE:
                    if not self.closed:
                        self.close(1000, payload[:2])
                    return None
                elif opcode in (TEXT, BINARY):
                    if kind is not None:
                        raise Closed("a new message inside a fragmented one")
                    kind, message = opcode, payload
                    if fin:
                        return kind, message
                elif opcode == CONT:
                    if kind is None:
                        raise Closed("continuation without a message")
                    message += payload
                    if fin:
                        return kind, message
                else:
                    raise Closed(f"opcode {opcode}")
        except (Closed, ConnectionError, OSError):
            return None

    def _send(self, opcode, payload):
        self.sock.sendall(frame(opcode, payload, mask=self.client))

    def send(self, data):
        self._send(TEXT, data.encode()) if isinstance(data, str) else self._send(BINARY, data)

    def close(self, code=1000, _echo=None):
        if self.closed:
            return
        self.closed = True
        try:
            self._send(CLOSE, struct.pack(">H", code))
            self.sock.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def connect(host, port, path="/", timeout=5):
    sock = socket.create_connection((host, port), timeout=timeout)
    ws = WebSocket(sock, client=True)
    ws.connect(f"{host}:{port}", path)
    return ws
