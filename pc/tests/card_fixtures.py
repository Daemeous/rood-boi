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


# ---- the Container (MENU7 _copyContainer / vs_menu_containerData at 0x1DE0 of a save) ----
CONTAINER_AT = 0x1DE0  # savedata_t.containerData: _spmcimg + 0x79E0 = file image 2 + 0x1DE0


def container_layout(probe=None, out=None):
    """vs_menu_containerData's and the bag's layout from container_ops_probe --layout (the CMake
    build's, else built here with gcc into `out`): no record offsets in the tests."""
    import json
    candidates = [pathlib.Path(probe)] if probe else [ROOT / "pc/build/container_ops_probe"]
    exe = next((c for c in candidates if c.exists()), None)
    if exe is None:
        out = pathlib.Path(out or ROOT / "pc/build")
        out.mkdir(parents=True, exist_ok=True)
        exe = out / "container_ops_probe"
        subprocess.run(["gcc", "-std=gnu11", "-funsigned-char", "-DPC_CONTAINER_OPS_PROBE", "-I", str(ROOT / "pc/src/ui"),
                        str(ROOT / "pc/tests/container_ops_probe.c"), str(ROOT / "pc/src/ui/container_ops.c"), "-o",
                        str(exe)], check=True)
    return json.loads(subprocess.run([str(exe), "--layout"], capture_output=True, text=True, check=True).stdout)


def settings_offset(plain):
    """vs_main_settings_t in the save: found by its saveFileGeneration and save counts (the header's
    generation and stats), never hard-coded. Returns the struct's offset (containerEmpty: bit 4 of
    its first byte)."""
    gen = struct.unpack_from("<I", plain, 0x188)[0]
    saves, game_saves = struct.unpack_from("<HH", plain, 0x194)
    needle = struct.pack("<HHI", saves, game_saves, gen)
    hits = []
    i = plain.find(needle, 0x200)
    while i >= 0:
        hits.append(i)
        i = plain.find(needle, i + 1)
    if len(hits) != 1:
        raise ValueError(f"settings_offset: {len(hits)} candidates")
    return hits[0] - 24  # key (+20), cumulativeSaveCount (+24), currentGameSaveCount (+26), saveFileGeneration (+28)


def reseal(plain, sectors=92):
    """Every sector's sum (but sector 1's) recomputed, then sector 1 sealed to XOR 0 (_verifySaveChecksums)."""
    for i in range(sectors):
        if i == 1:
            continue
        x = 0
        for b in plain[i * 256:(i + 1) * 256]:
            x ^= b
        plain[0x1A4 + i] = x
    plain[0x1A4 + 1] = 0
    x = 0
    for b in plain[256:512]:
        x ^= b
    plain[0x1A4 + 1] = x


def build_container(bag, layout, misc_counts=(95, 70)):
    """A container's bytes (display order = slot order, as _copyContainer writes it) from the save's
    own bag `bag` (vs_main_inventory as RAM has it after the load): its first weapon with blade and
    grip (up to two gems set, as the grip's slots allow: synthetic gem records, the bag has none to
    copy), its first shield (one gem set when it has a slot, | 0x80), its first armor that is not
    an accessory (taken off), two loose gems, and two stacks (misc_counts) of the bag's first misc
    item plus one of its second. Returns (bytes, {what: slot}). Nothing is equipped."""
    L, F, S = layout["arrays"], layout["fields"], layout["sizes"]
    B = layout["bag"]
    box = bytearray(layout["container"])

    def rec(kind, src_off):
        return bytearray(bag[src_off:src_off + S[kind]])

    def put(kind, slot, data, index_field, index):
        data = bytearray(data)
        if index_field is not None:
            data[F[index_field]] = index
        box[L[kind] + slot * S[kind]:L[kind] + (slot + 1) * S[kind]] = data

    def bag_rec(kind, i):
        return rec(kind, B[kind] + i * S[kind])

    gem_slot = [0]
    made = {}

    def gem(gid, parent, strength):
        g = bytearray(S["gems"])
        struct.pack_into("<H", g, F["gem_id"], gid)
        g[F["gem_str"]] = strength
        struct.pack_into("<H", g, F["gem_parent"], parent)
        slot = gem_slot[0]
        struct.pack_into("<h", g, F["gem_index"], slot + 1)
        box[L["gems"] + slot * S["gems"]:L["gems"] + (slot + 1) * S["gems"]] = g
        gem_slot[0] += 1
        return slot

    # a weapon with its parts
    for w in range(8):
        wr = bag_rec("weapons", w)
        if wr[F["weapon_blade"]]:
            break
    else:
        raise ValueError("the bag has no weapon")
    blade = bag_rec("blades", wr[F["weapon_blade"]] - 1)
    # the bag's grip with the most gem slots (two at least: synthetic when none has them)
    grips = [bag_rec("grips", g) for g in range(16)]
    grips = [g for g in grips if struct.unpack_from("<H", g, 0)[0]]
    grip = max(grips, key=lambda g: g[F["grip_slots"]])
    if grip[F["grip_slots"]] < 2:
        grip[F["grip_slots"]] = 2
    blade[F["blade_parent"]] = 1
    put("blades", 0, blade, "blade_index", 1)
    struct.pack_into("<H", grip, F["grip_parent"], 1)
    struct.pack_into("<H", grip, F["grip_index"], 1)
    put("grips", 0, grip, None, 0)
    wr[F["weapon_blade"]] = 1
    wr[F["weapon_grip"]] = 1
    wr[F["weapon_equipped"]] = 0
    for i in range(4):
        wr[F["weapon_gems"] + i] = 0
    for i in range(min(2, grip[F["grip_slots"]])):
        wr[F["weapon_gems"] + i] = gem(263 + i, 1, 1 + i) + 1
    put("weapons", 0, wr, "weapon_index", 1)
    made["weapon"] = 0
    # a shield
    for s in range(8):
        sr = bag_rec("shields", s)
        if sr[4]:  # base.id
            sr[F["shield_equipped"]] = 0
            for i in range(4):
                sr[F["shield_gems"] + i] = 0
            if not sr[F["shield_slots"]]:
                sr[F["shield_slots"]] = 1  # synthetic: one slot for the shield's gem
            sr[F["shield_gems"]] = gem(265, 1 | 0x80, 2) + 1
            put("shields", 0, sr, "shield_index", 1)
            made["shield"] = 0
            break
    # an armor (not an accessory: category 7), taken off
    for a in range(16):
        ar = bag_rec("armor", a)
        if ar[0] and ar[3] != 7:
            ar[F["armor_limb"]] = 0
            put("armor", 0, ar, "armor_index", 1)
            made["armor"] = 0
            break
    for i in range(2):  # loose gems
        gem(266 + i, 0, 3)
    # misc: two stacks of the bag's first item, one of its second
    ids = []
    for i in range(64):
        mid = struct.unpack_from("<H", bag, B["misc"] + i * 4)[0]
        if mid and mid not in ids:
            ids.append(mid)
    if not ids:
        raise ValueError("the bag has no misc item")
    stacks = [(ids[0], misc_counts[0]), (ids[0], misc_counts[1])] + ([(ids[1], 1)] if len(ids) > 1 else [])
    for slot, (mid, n) in enumerate(stacks):
        m = bytearray(4)
        struct.pack_into("<H", m, F["misc_id"], mid)
        m[F["misc_count"]] = n
        m[F["misc_index"]] = slot + 1
        box[L["misc"] + slot * 4:L["misc"] + slot * 4 + 4] = m
    made["misc_id"] = ids[0]
    made["gems"] = gem_slot[0]
    return bytes(box), made


def with_container(payload, box):
    """The save with container bytes `box` at 0x1DE0 and its settings' containerEmpty cleared
    (settings_offset), every sector's sum recomputed and sector 1 re-sealed, re-encoded with its key."""
    plain = decode(payload)
    plain[CONTAINER_AT:CONTAINER_AT + len(box)] = box
    plain[settings_offset(plain)] &= ~0x10 & 0xFF
    reseal(plain)
    if not checksums_ok(plain):
        raise AssertionError("with_container: the checksums do not verify")
    return bytes(encode(plain))


def container_fixture(source, path, bag, layout, kind="container"):
    """A card (from `source`'s first save) whose file 1 holds the container built from `bag`
    (kind "container"); "container_none": the same with the header's generation one ahead of the
    settings' (MENU7's _findCurrentSave finds no save of this journey: emptyCard, then containerWarn)."""
    image = blank()
    payload = first_save(source)
    box, made = build_container(bag, layout)
    data = with_container(payload, box)
    if kind == "container_none":
        data = with_generation(data, generation(decode(data)) + 1)
    elif kind != "container":
        raise ValueError(kind)
    write_file(image, save_name(1), data)
    pathlib.Path(path).write_bytes(bytes(image))
    return made


INDEX_ORDER = ("weapons", "blades", "grips", "shields", "armor", "gems", "misc")  # vs_menu_containerIndices
CAPS = {"weapons": 32, "blades": 64, "grips": 64, "shields": 32, "armor": 64, "gems": 192, "misc": 256}


def pack_container(container, layout=None):
    """MENU7's _copyContainer (260.c, 3 gem bytes): a vs_menu_container (data, then its index lists) packed
    in display order as the card holds it: slot i of each category is its list's entry i, the parents'
    indices remapped (blade / grip assembledWeaponIndex, weapon blade / grip / gems, shield gems, gem
    setItemIndex: a shield's | 0x80)."""
    L = layout or container_layout()
    A, S, F = L["arrays"], L["sizes"], L["fields"]
    size = L["container"]
    data, idx = container[:size], container[size:]
    lists, at = {}, 0
    for k in INDEX_ORDER:
        lists[k] = list(struct.unpack_from(f"<{CAPS[k]}H", idx, at))
        at += CAPS[k] * 2
    target = {k: [0] * CAPS[k] for k in INDEX_ORDER}  # targetIndices: old slot -> new index
    for k in INDEX_ORDER:
        for j, src in enumerate(lists[k]):
            if src:
                target[k][src - 1] = j + 1
    out = bytearray(size)

    def rec(k, slot):
        return bytearray(data[A[k] + slot * S[k]:A[k] + (slot + 1) * S[k]])

    def put(k, slot, r):
        out[A[k] + slot * S[k]:A[k] + (slot + 1) * S[k]] = r

    for k in INDEX_ORDER:
        for i, src in enumerate(lists[k]):
            if not src:
                continue
            r = rec(k, src - 1)
            if k == "weapons":
                r[F["weapon_index"]] = i + 1
                r[F["weapon_blade"]] = target["blades"][r[F["weapon_blade"]] - 1] & 0xFF
                r[F["weapon_grip"]] = target["grips"][r[F["weapon_grip"]] - 1] & 0xFF
                for g in range(3):
                    if r[F["weapon_gems"] + g]:
                        r[F["weapon_gems"] + g] = target["gems"][r[F["weapon_gems"] + g] - 1] & 0xFF
            elif k == "blades":
                r[F["blade_index"]] = i + 1
                if r[F["blade_parent"]]:
                    r[F["blade_parent"]] = target["weapons"][r[F["blade_parent"]] - 1] & 0xFF
            elif k == "grips":
                struct.pack_into("<H", r, F["grip_index"], i + 1)
                p = struct.unpack_from("<H", r, F["grip_parent"])[0]
                if p:
                    struct.pack_into("<H", r, F["grip_parent"], target["weapons"][p - 1])
            elif k == "shields":
                r[F["shield_index"]] = i + 1
                for g in range(3):
                    if r[F["shield_gems"] + g]:
                        r[F["shield_gems"] + g] = target["gems"][r[F["shield_gems"] + g] - 1] & 0xFF
            elif k == "armor":
                r[F["armor_index"]] = i + 1
            elif k == "gems":
                struct.pack_into("<h", r, F["gem_index"], i + 1)
                p = struct.unpack_from("<H", r, F["gem_parent"])[0]
                if p:
                    if p & 0x80:
                        p = target["shields"][(p & 0xFFFF) - 0x81] | 0x80
                    else:
                        p = target["weapons"][p - 1]
                    struct.pack_into("<H", r, F["gem_parent"], p)
            elif k == "misc":
                r[F["misc_index"]] = i + 1
            put(k, i, r)
    return bytes(out)


# ---- the Score screen (MENU9: vs_main_scoredata, vs_main_stateFlags) ----
# vs_main_scoredata_t (src/SLUS_010.40/main.h) field offsets, 0x114 bytes in all
SCOREDATA = {"titles": 0x00, "kills": 0x04, "attacks": 0x14, "trials": 0x28, "max_chain": 0x88, "clear_count": 0x8C,
             "minutes": 0x90, "map": 0x94, "chests": 0x98, "kill_flags": 0x9C, "streak": 0x104, "misc": 0x108}
SCOREDATA_SIZE = 0x114
# vs_main_flags_t: clearCount, the six boss flags (damascusGolem .. asura), gold / chest keys, chestsOpened[64]
STATE_FLAGS = {"clear_count": 0x00, "bosses": 0xCC, "gold_key": 0x383, "chest_key": 0x387, "chests": 0x3C0}
TRIAL_NONE = 0x800000


def trial(time=TRIAL_NONE, rnd=0, difficulty=0):
    """vs_trialScore_t: time:24, round:7, difficulty:1."""
    return struct.pack("<I", (time & 0xFFFFFF) | (rnd & 0x7F) << 24 | (difficulty & 1) << 31)


def score_fixture(kind, kill_flags=None):
    """RAM patches for the Score screen's tests (they start from a checkpoint, so the card's save never loads:
    the fixture is written into vs_main_scoredata / vs_main_stateFlags before each open): {"scoredata": {offset:
    bytes}, "state": {offset: bytes}}. Kinds: fresh (all zero, no time attack), midgame (some kills, three bosses,
    two time attacks), cleared (one clear in 420 minutes), maxed (every title condition, the score at its cap,
    every foe). kill_flags: 26 words for the Bestiary (default by kind). Every kind writes every stateFlags
    field it models (clearCount, the six bosses, the gold / chest keys, chestsOpened[64]), zero unless the kind
    sets it, so nothing an earlier fixture or case wrote carries over."""
    sd = {}
    st = {STATE_FLAGS["clear_count"]: bytes(1), STATE_FLAGS["bosses"]: bytes(6), STATE_FLAGS["gold_key"]: bytes(1),
          STATE_FLAGS["chest_key"]: bytes(1), STATE_FLAGS["chests"]: bytes(64)}
    trials = [trial() for _ in range(24)]
    kills, attacks = [0] * 6, [0] * 10
    flags = [0] * 26
    chain = clears = minutes = 0
    if kind == "fresh":
        pass
    elif kind == "midgame":
        kills, attacks = [120, 80, 45, 30, 4, 12], [300, 220, 40, 10, 0, 5, 0, 0, 2, 75]
        trials[0], trials[1], trials[2] = trial(0x011E09, 0), trial(0x011F00, 1), trial(0x020000, 2)
        trials[3 * 3] = trial(0x002A05, 0)
        flags[0], flags[4] = 0x80000001, 0x00000100
        chain = 17
        st[STATE_FLAGS["bosses"]] = bytes([1, 1, 1, 0, 0, 0])
        st[STATE_FLAGS["chests"]] = bytes([1] * 20 + [0] * 44)
    elif kind == "cleared":
        kills, attacks = [900, 700, 500, 300, 60, 120], [2000, 900, 300, 120, 80, 60, 40, 20, 10, 600]
        chain, clears, minutes = 31, 1, 420
        trials = [trial(0x003000 + i, i % 3) for i in range(24)]
        flags = [0x5555AAAA] * 26
        st[STATE_FLAGS["clear_count"]] = bytes([1])
        st[STATE_FLAGS["bosses"]] = bytes([1] * 6)
        st[STATE_FLAGS["gold_key"]] = bytes([1])
    elif kind == "maxed":
        kills, attacks = [9999] * 6, [9999] * 10
        chain, clears, minutes = 99, 9, 200
        trials = [trial(0x000100 + i, 5) for i in range(24)]
        flags = [-1] * 26
        st[STATE_FLAGS["clear_count"]] = bytes([9])
        st[STATE_FLAGS["bosses"]] = bytes([1] * 6)
        st[STATE_FLAGS["gold_key"]] = bytes([1])
        st[STATE_FLAGS["chest_key"]] = bytes([1])
        st[STATE_FLAGS["chests"]] = bytes([1] * 64)
    else:
        raise ValueError(kind)
    if kill_flags is not None:
        flags = list(kill_flags)
    sd[SCOREDATA["titles"]] = struct.pack("<I", 0)  # MENU9 only ORs into it: start from none
    sd[SCOREDATA["map"]] = struct.pack("<II", 0, 0)  # the high-water marks: raised by the open
    sd[SCOREDATA["kills"]] = struct.pack("<6H", *kills)
    sd[SCOREDATA["attacks"]] = struct.pack("<10H", *attacks)
    sd[SCOREDATA["trials"]] = b"".join(trials)
    sd[SCOREDATA["max_chain"]] = struct.pack("<H", chain)
    sd[SCOREDATA["clear_count"]] = struct.pack("<II", clears, minutes)
    sd[SCOREDATA["kill_flags"]] = struct.pack("<26i", *[f if f < 0x80000000 else f - (1 << 32) for f in flags])
    sd[SCOREDATA["streak"]] = struct.pack("<II", 1234 if kind != "fresh" else 0, 99 if kind == "maxed" else 0)
    if kind == "maxed":
        sd[SCOREDATA["streak"]] = struct.pack("<II", 900000000, 900000000)
    return {"scoredata": sd, "state": st}
