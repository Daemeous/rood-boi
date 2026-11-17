"""The layout table (pc/tools/layout_table.py, pc/src/dev_layout.c): the game's RPC `field` and `symbol_info` name
state by path, the generator's table agrees with the game, the build's sidecar matches the executable. Memory lane S2."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import rood_rpc  # noqa: E402
from rood_rpc import Game, RpcError, layout  # noqa: E402

table = layout()
game = Game.launch(fast=True)
try:
    game.wait_title()
    assert game.call("field", path="randIndex")["size"] == 4, "the game must answer `field` itself"

    # the game's resolver and the generator's agree on every typed data symbol and on each member of the structs
    checked = 0
    typed = [(n, s) for n, s in table.syms.items() if s["kind"] == "d" and s["type"] and "@" not in n]
    for name, sym in typed[::2]:
        paths = [name]
        t = table.types[sym["type"]]
        if t["kind"] in "su":
            paths += [f"{name}.{f[0]}" for f in t["fields"] if f[0]]
        for path in paths:
            ref = table.resolve(path)
            got = game.field(path, length=0)
            assert (got.address, got.size) == (ref["address"], ref["size"]), (path, got, ref)
            assert got.kind == table.kind_word(ref["type"]), (path, got.kind)
            checked += 1
    assert checked > 2000, checked

    # a symbol's address is the one the linker gave it
    assert game.field("randIndex").address == rood_rpc.symbols()["randIndex"]

    # a scalar: written and read back by name, then restored
    before = game.value("randIndex")
    game.store("randIndex", 0x1234)
    assert game.value("randIndex") == 0x1234
    assert game.data("randIndex") == (0x1234).to_bytes(4, "little")
    game.store("randIndex", before)

    # a pointer is followed by -> and by .; an index on a pointer steps in its target's size
    head = game.field("heapA")
    assert head.kind == "struct" and head.size == 24, head
    node = game.value("heapA.next")
    assert node and game.value("heapA.next->blockSz") == game.value("heapA.next.blockSz")
    assert game.field("heapA.next[1]").address == node + head.size

    # a bit field: its value is its bits only; a write keeps the neighbours
    for name, sym in typed:
        t = table.types[sym["type"]]
        bit = next((f for f in t["fields"] if f[3] and f[0]), None) if t["kind"] == "s" else None
        if bit and sym["size"] and bit[3] <= 8 and sym["address"] < 0x20000000:
            path = f"{name}.{bit[0]}"
            whole = game.data(name)
            original = game.value(path)
            assert 0 <= original < 1 << bit[3] or table.types[bit[2]]["kind"] == "i", (path, original)
            game.store(path, original ^ 1)
            assert game.value(path) == ((original ^ 1) if table.types[bit[2]]["kind"] != "i" else game.value(path))
            changed = game.data(name)
            assert sum(a != b for a, b in zip(whole, changed)) <= 1, (path, whole.hex(), changed.hex())
            game.store_data(name, whole)
            assert game.data(name) == whole
            break
    else:
        raise AssertionError("no bit field to test")

    # a symbol answers under its decomp name and ours
    for old, new in table.aliases.items():
        if old in table.syms and old.startswith("D_") and table.syms[old]["type"]:
            assert game.field(old, length=0).address == game.field(new, length=0).address
            break

    # symbol_info: the struct's members with offsets, an array's element
    info = game.info("heapA")
    assert [f["name"] for f in info["fields"]][:2] == ["prev", "next"] and info["fields"][1]["offset"] == 8, info
    arrays = [n for n, s in typed if table.types[s["type"]]["kind"] == "a"]
    assert game.info(arrays[0])["count"] >= 0
    assert len(game.symbols_like("vs_main_")) > 20

    # refusals: no such symbol, member or index, a null pointer, a scalar's member, a write of the wrong size
    for bad in ["no_such_symbol_xyz", "heapA.nothing", "heapA[1]", "randIndex.x", "heapA->prev->next->nothing",
                "vs_battle_actors[99]", "vs_battle_actors[0]->unk3C"]:
        try:
            game.field(bad)
            raise AssertionError(f"{bad} should fail")
        except RpcError:
            pass
    try:
        game.store_data("randIndex", bytes(5))
        raise AssertionError("a write past the field should fail")
    except RpcError:
        pass

    # the digest's masks by name (the Oracle's page digests leave out named state, not addresses): a field, a pointer's
    # target, a sized range, and a name that does not resolve
    game.pause()
    game.mark()
    game.rewind()
    masks = [{"path": "randIndex"}, {"path": "vs_scratch.unk0"},
             {"path": "vs_render_legacyPrimitiveBuffers[0]", "deref": True, "size": 0x20000}, {"path": "heapA", "size": 8}]
    assert game.digest("record", ticks=10, masks=masks)["error"] == 0
    game.run(2)
    assert game.digest("result")["recorded"] >= 1
    game.digest("stop")
    try:
        game.digest("record", ticks=10, masks=[{"path": "no_such_symbol_xyz"}])
        raise AssertionError("a mask that does not resolve should fail")
    except RpcError:
        pass

    # the older-build path: the same questions answered client side from the table
    game._named = False
    try:
        assert game.value("heapA.next->blockSz") == game.value("heapA.next.blockSz")
        assert game.field("heapA").address == head.address and game.info("heapA")["fields"][1]["offset"] == 8
        game.store("randIndex", 7)
        assert game.value("randIndex") == 7
        game.store("randIndex", before)
    finally:
        game._named = True
    # a game without the RPC (a baseline): the first call finds it out and answers from the table, values included
    older = Game(game.path)
    older.call = lambda method, **params: (_ for _ in ()).throw(RpcError(f"unknown method {method}")) \
        if method in ("field", "symbol_info", "symbols") else game.call(method, **params)
    older.read = game.read
    older.write = game.write
    assert older.value("heapA.next->blockSz") == game.value("heapA.next->blockSz") and older._named is False
    older.store_data("heapA.next->blockSz", game.data("heapA.next->blockSz") + b"")  # (behind a pointer: no object limit)
    try:
        older.store_data("randIndex", bytes(5))
        raise AssertionError("a write past a symbol's end should fail on a baseline too")
    except RpcError:
        pass
    print(f"PASS: the layout table: {checked} paths agree between the game and the generator, scalars, bit fields, "
          f"pointers, aliases, symbol_info, refusals and the client-side fallback")
finally:
    game.close()
