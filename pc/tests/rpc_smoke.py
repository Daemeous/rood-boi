"""Developer RPC: launch headless, reach the title, start New Game with injected
input and check status, stepping, state, memory read/write, screenshot, errors."""
import pathlib
import sys

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
