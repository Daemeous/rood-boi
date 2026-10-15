#!/usr/bin/env python3
"""Memory card safety and fixtures for tests that can save (pc/src/memory_card.c).

card_guard(): a test that can write a card runs inside it.

    with card_guard(out / "cards") as cards:
        game = launch_at("save_room", card=cards.card1, ...)

  - before: the sha256 and mtime of every player card (pc/save/card1.mcd and
    card2.mcd here and in the main checkout, found through git's common dir)
  - ROOD_CARD1 is a copy of the tests' pinned card (run.py default_save(),
    never the live card) in the guard's directory, ROOD_CARD2 a missing temp
    path (slot 2 empty) or a copy, set in os.environ before any launch, so a
    launch that forgets card= still cannot reach a player card; neither may
    resolve to a player card; ROOD_OUT is set
  - after (finally): every player card's hash and mtime as before, or
    AssertionError. This checkout's cards fail hard. Another checkout's
    (the main one: the user plays there) may change under a concurrent play
    session; the error then names the games (rood*) running in that checkout
    outside this test's own tree (pids from /proc), so a save made while the
    test ran is told apart from a leak (the test's games always run with
    ROOD_CARD1 / ROOD_CARD2 set to temp paths)

Fixtures mirror memory_card.c's raw 128 KiB image (frame 0 "MC", directory
frames 1..15 with a next link at +8 and the name at +10, each frame's last
byte the XOR of the others, 8 KiB blocks) and MENU7's save files
(src/MENU/MENU7.PRG/260.c: 3 blocks, BASLUS-01040VAG1..5, temps VAGA..E; the
header's key at 0x180 and the keystream from 0x184, checksums per 256-byte
sector with sector 1 XOR-ing to 0). Payloads are copied from saves the game
made; only a generation is rewritten (decoded, re-sealed, re-encoded with the
file's own key)."""
import contextlib
import hashlib
import os
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

CARD_SIZE = 0x20000
BLOCK = 0x2000
FRAME = 0x80
SAVE_BLOCKS = 3
SAVE_SIZE = 0x5C00  # sizeof(savedata_t)
TEMPLATE = "BASLUS-01040VAG"


# ---- the player's cards ----
def checkouts():
    roots = [ROOT]
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    if common and pathlib.Path(common).parent.resolve() != ROOT.resolve():
        roots.append(pathlib.Path(common).parent)
    return roots


def player_cards():
    """Every player card path (existing or not: a card made during the test is a change too)."""
    return [root / "pc/save" / name for root in checkouts() for name in ("card1.mcd", "card2.mcd")]


def _descendants(pid):
    """pid and every process under it (from /proc's parent links)."""
    parent = {}
    for d in pathlib.Path("/proc").iterdir():
        if d.name.isdigit():
            try:
                stat = (d / "stat").read_text()
                parent[int(d.name)] = int(stat[stat.rindex(")") + 2:].split()[1])
            except (OSError, ValueError):
                pass
    tree, grew = {pid}, True
    while grew:
        grew = False
        for p, pp in parent.items():
            if pp in tree and p not in tree:
                tree.add(p)
                grew = True
    return tree


def _players_in(root):
    """Games (rood*) outside this test's tree whose working directory is in checkout `root`."""
    ours = _descendants(os.getpid())
    found = []
    root = root.resolve()
    for d in pathlib.Path("/proc").iterdir():
        if not d.name.isdigit() or int(d.name) in ours:
            continue
        try:
            cwd = pathlib.Path(os.readlink(d / "cwd"))
            name = (d / "comm").read_text().strip()
        except OSError:
            continue
        if name.startswith("rood") and (cwd == root or root in cwd.parents):
            found.append(f"{name}[{d.name}]")
    return found


def _fingerprint(path):
    if not path.exists():
        return None
    return hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_mtime_ns


def default_save():
    """run.py's pinned test card ($ROOD_TEST_CARD, else pc/build/test-card1.mcd)."""
    sys.path.insert(0, str(ROOT / "pc/tests"))
    import run  # noqa: E402
    return run.default_save()


class Cards:
    def __init__(self, directory, card1, card2):
        self.dir, self.card1, self.card2 = directory, card1, card2


@contextlib.contextmanager
def card_guard(directory, source=None, card2=None):
    """Player cards untouched; ROOD_CARD1 / ROOD_CARD2 are temp copies.
    source: the slot 1 card to copy (default: run.py default_save()); card2: a
    slot 2 card to copy (default: none, a missing path)."""
    directory = pathlib.Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    before = {p: _fingerprint(p) for p in player_cards()}
    source = pathlib.Path(source) if source else default_save()
    real = {p.resolve() for p in player_cards()}
    if source.resolve() in real:
        raise AssertionError(f"card_guard: the source {source} is a player card")
    card1 = directory / "card1.mcd"
    shutil.copyfile(source, card1)
    second = directory / "card2.mcd"
    second.unlink(missing_ok=True)
    pathlib.Path(str(second) + ".tmp").unlink(missing_ok=True)
    if card2:
        shutil.copyfile(card2, second)
    saved = {k: os.environ.get(k) for k in ("ROOD_CARD1", "ROOD_CARD2", "ROOD_OUT")}
    os.environ["ROOD_CARD1"] = str(card1)
    os.environ["ROOD_CARD2"] = str(second)
    os.environ.setdefault("ROOD_OUT", str(directory))
    for path in (card1, second):
        if path.resolve() in real:
            raise AssertionError(f"card_guard: {path} resolves to a player card")
    try:
        yield Cards(directory, card1, second)
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        after = {p: _fingerprint(p) for p in player_cards()}
        changed = [p for p in before if before[p] != after[p]]
        here = [str(p) for p in changed if ROOT.resolve() in p.resolve().parents]
        elsewhere = [p for p in changed if str(p) not in here]
        if here:
            raise AssertionError(f"card_guard: a player card changed: {here}")
        if elsewhere:
            notes = []
            for p in elsewhere:
                root = p.parents[2]  # <checkout>/pc/save/cardN.mcd
                players = _players_in(root)
                notes.append(f"{p} (" + (f"running in that checkout outside this test: {', '.join(players)}; "
                                         "a save made there while the test ran changes it too"
                                         if players else "nothing else runs in that checkout now; if a game "
                                         "there saved and quit while the test ran, that changes it too") + ")")
            raise AssertionError("card_guard: a player card in another checkout changed: " + "; ".join(notes))


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


# ---- the raw image (memory_card.c) ----
def seal(image, index):
    f = index * FRAME
    x = 0
    for b in image[f:f + FRAME - 1]:
        x ^= b
    image[f + FRAME - 1] = x


def blank():
    image = bytearray(CARD_SIZE)
    image[0:2] = b"MC"
    seal(image, 0)
    for block in range(15):
        free_entry(image, block)
    for i in range(16, 36):
        f = i * FRAME
        image[f:f + 4] = struct.pack("<I", 0xFFFFFFFF)
        image[f + 8:f + 10] = struct.pack("<H", 0xFFFF)
        seal(image, i)
    image[63 * FRAME:64 * FRAME] = image[0:FRAME]
    return image


def free_entry(image, block):
    f = (block + 1) * FRAME
    image[f:f + FRAME] = bytes(FRAME)
    image[f:f + 4] = struct.pack("<I", 0xA0)
    image[f + 8:f + 10] = struct.pack("<H", 0xFFFF)
    seal(image, block + 1)


def entry(image, block):
    f = (block + 1) * FRAME
    state, size, nxt = struct.unpack_from("<IIH", image, f)
    name = bytes(image[f + 10:f + 31]).split(b"\0")[0].decode("ascii", "replace")
    return state, size, nxt, name


def files(image):
    """{name: [blocks]} of the card's files."""
    out = {}
    for block in range(15):
        state, size, nxt, name = entry(image, block)
        if state != 0x51:
            continue
        chain, b = [block], nxt
        while b < 15 and len(chain) < 15:
            chain.append(b)
            b = entry(image, b)[2]
        out[name] = chain
    return out


def free_blocks(image):
    return [b for b in range(15) if entry(image, b)[0] & 0xF0 == 0xA0]


def create(image, name, blocks):
    chosen = free_blocks(image)[:blocks]
    if len(chosen) < blocks or name in files(image):
        raise ValueError(f"cannot create {name} ({blocks} blocks)")
    for i, block in enumerate(chosen):
        f = (block + 1) * FRAME
        image[f:f + FRAME] = bytes(FRAME)
        image[f:f + 4] = struct.pack("<I", 0x51 if i == 0 else 0x53 if i == blocks - 1 else 0x52)
        image[f + 8:f + 10] = struct.pack("<H", 0xFFFF if i == blocks - 1 else chosen[i + 1])
        if i == 0:
            image[f + 4:f + 8] = struct.pack("<I", blocks * BLOCK)
            image[f + 10:f + 10 + len(name)] = name.encode("ascii")
        seal(image, block + 1)
        image[(block + 1) * BLOCK:(block + 2) * BLOCK] = bytes(BLOCK)
    return chosen


def read_file(image, name):
    return b"".join(bytes(image[(b + 1) * BLOCK:(b + 2) * BLOCK]) for b in files(image)[name])


def write_file(image, name, data):
    blocks = files(image).get(name) or create(image, name, (len(data) + BLOCK - 1) // BLOCK)
    for i, b in enumerate(blocks):
        chunk = data[i * BLOCK:(i + 1) * BLOCK]
        image[(b + 1) * BLOCK:(b + 1) * BLOCK + len(chunk)] = chunk


def save_name(n):
    return TEMPLATE + str(n)


def temp_name(n):
    return TEMPLATE + chr(ord("A") + n - 1)


def saves(image):
    """{file number 1..5: name} of MENU7's saves on the card."""
    return {int(n[-1]): n for n in files(image) if n.startswith(TEMPLATE) and n[-1] in "12345"}


# ---- MENU7's save file (260.c) ----
def decode(data):
    """_decode from 0x184 with the key at 0x180 (a copy)."""
    data = bytearray(data)
    key = struct.unpack_from("<I", data, 0x180)[0]
    for i in range(0x184, min(len(data), SAVE_SIZE)):
        key = (key * 0x19660D) & 0xFFFFFFFF
        data[i] = (data[i] - (key >> 24)) & 0xFF
    return data


def encode(plain):
    data = bytearray(plain)
    key = struct.unpack_from("<I", data, 0x180)[0]
    for i in range(0x184, min(len(data), SAVE_SIZE)):
        key = (key * 0x19660D) & 0xFFFFFFFF
        data[i] = (data[i] + (key >> 24)) & 0xFF
    return data


def checksums_ok(plain, sectors=92):
    """_verifySaveChecksums on decoded data."""
    sums = plain[0x1A4:0x1A4 + 0x5C]
    for i in range(sectors):
        if i == 1:
            continue
        x = 0
        for b in plain[i * 256:(i + 1) * 256]:
            x ^= b
        if x != sums[i]:
            return False
    x = 0
    for b in plain[256:512]:
        x ^= b
    return x == 0 and struct.unpack_from("<I", plain, 0x18C)[0] == 0x20000107


def generation(plain):
    return struct.unpack_from("<I", plain, 0x188)[0]


def with_generation(data, gen, hours=None):
    """The save with another generation (sector 1 re-sealed, re-encoded with its key); hours: also the
    header's play time (stats.gameTime at 0x190: frames, s, m, h) set to hours:00:00, a field
    _applyLoadedSaveFile copies to RAM (vs_main_gametime), so a load tells which file it read."""
    plain = decode(data)
    struct.pack_into("<I", plain, 0x188, gen)
    if hours is not None:
        plain[0x190:0x194] = bytes((0, 0, 0, hours))
    plain[0x1A4 + 1] = 0
    x = 0
    for b in plain[256:512]:
        x ^= b
    plain[0x1A4 + 1] = x
    return bytes(encode(plain))


def stats(plain):
    """The header's stats (stats_t at 0x190)."""
    _f, s, m, h, saves, game_saves, hp, hp_max, loc, clear, mapc, _p, mp, mp_max = struct.unpack_from(
        "<BBBBHHHHBBBBHH", plain, 0x190)
    return {"time": (h, m, s), "saves": saves, "hp": (hp, hp_max), "mp": (mp, mp_max), "location": loc,
            "clear": clear, "map": mapc}


# ---- fixtures (from a card with at least one save) ----
def multi_hours(source_hours, n):
    """The 'multi' fixture's file n play time hours (file 1: the source's own; 2 and 4 distinct, < 99)."""
    return source_hours if n == 1 else (source_hours + (11 if n == 2 else 22)) % 99


def first_save(source):
    image = bytearray(pathlib.Path(source).read_bytes())
    found = saves(image)
    if not found:
        raise ValueError("the source card has no save")
    return read_file(image, found[min(found)])


def fixture(kind, source, path):
    """Write fixture `kind` to path: multi, full, corrupt, orphan_temp, temp_only, blank, wrong_size."""
    path = pathlib.Path(path)
    if kind == "wrong_size":
        path.write_bytes(b"\0" * 1000)
        return path
    image = blank()
    if kind in ("multi", "corrupt", "orphan_temp", "temp_only", "one"):
        payload = first_save(source)
        gen = generation(decode(payload))
        if kind == "multi":  # files 1, 2 and 4: the source's save (current) and two older generations,
            # each with its own play time hours (multi_hours(): what a load of that file puts in RAM)
            write_file(image, save_name(1), payload)
            h = stats(decode(payload))["time"][0]
            write_file(image, save_name(2), with_generation(payload, max(gen - 1, 1), multi_hours(h, 2)))
            write_file(image, save_name(4), with_generation(payload, max(gen - 2, 1), multi_hours(h, 4)))
        elif kind == "corrupt":  # file 1 fine, file 2 with a flipped byte in its first 32 sectors
            write_file(image, save_name(1), payload)
            bad = bytearray(with_generation(payload, max(gen - 1, 1)))
            bad[0x1000] ^= 0x5A
            write_file(image, save_name(2), bytes(bad))
        elif kind == "orphan_temp":  # file 1 and its redundant temp (MENU7 erases it on a scan)
            write_file(image, save_name(1), payload)
            write_file(image, temp_name(1), payload)
        elif kind == "temp_only":  # file 3 a temp without its save (Temporary)
            write_file(image, save_name(1), payload)
            write_file(image, temp_name(3), payload)
        else:
            write_file(image, save_name(1), payload)
    elif kind == "full":  # another game's files: two blocks free, no slot for a save
        write_file(image, "BESLES-00000FILLER", bytes(13 * BLOCK))
    elif kind != "blank":
        raise ValueError(kind)
    path.write_bytes(bytes(image))
    return path


if __name__ == "__main__":
    # python3 pc/tests/card_fixtures.py SOURCE OUTDIR: every fixture, and a summary
    src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for k in ("multi", "full", "corrupt", "orphan_temp", "temp_only", "blank", "wrong_size"):
        p = fixture(k, src, out / f"{k}.mcd")
        if k != "wrong_size":
            img = bytearray(p.read_bytes())
            print(k, {n: len(b) for n, b in files(img).items()}, "free", len(free_blocks(img)),
                  {n: (generation(decode(read_file(img, nm))), checksums_ok(decode(read_file(img, nm)), 32))
                   for n, nm in saves(img).items()})
